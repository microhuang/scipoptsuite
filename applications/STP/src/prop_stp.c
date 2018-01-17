/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   prop_stp.c
 * @brief  propagator for Steiner tree problems, using the LP reduced costs
 * @author Daniel Rehfeldt
 *
 * This propagator makes use of the reduced cost of an optimally solved LP relaxation to propagate the variables
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include "prop_stp.h"
#include "scip/tree.h"


/**@name Propagator properties
 *
 * @{
 */

#define PROP_NAME              "stp"
#define PROP_DESC              "stp propagator"
#define PROP_TIMING            SCIP_PROPTIMING_DURINGLPLOOP | SCIP_PROPTIMING_AFTERLPLOOP
#define PROP_PRIORITY          1000000 /**< propagator priority */
#define PROP_FREQ                     1 /**< propagator frequency */
#define PROP_DELAY                FALSE /**< should propagation method be delayed, if other propagators found reductions? */

/**@} */

/**@name Default parameter values
 *
 * @{
 */

#define DEFAULT_MAXNWAITINGROUNDS        3    /**< maximum number of rounds to wait until propagating again */
#define REDUCTION_WAIT_RATIO             0.10 /**< ratio of edges to be newly fixed before performing reductions for additional fixing */

/**@} */


/*
 * Data structures
 */


/** propagator data */
struct SCIP_PropData
{
   SCIP_Real*            fixingbounds;       /**< saves largest upper bound to each variable that would allow to fix it */
   SCIP_Longint          nfails;             /**< number of failures since last successful call */
   SCIP_Longint          ncalls;             /**< number of calls */
   SCIP_Longint          nlastcall;          /**< number of last call */
   SCIP_Longint          lastnodenumber;     /**< number of last call */
   int                   nfixededges;        /**< total number of arcs fixed by 'fixedgevar' method of this propagator */
   int                   postrednfixededges; /**< total number of arcs fixed by 'fixedgevar' method of this propagator after the last reductions */
   int                   maxnwaitrounds;     /**< maximum number of rounds to wait until propagating again */
   SCIP_Bool             aggressive;         /**< be aggressive? */
};

/**@name Local methods
 *
 * @{
 */

#if 0
static
SCIP_RETCODE fixedgevarTo1(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             edgevar,            /**< the variable to be fixed */
   int*                  nfixed              /**< counter that is incriminated if variable could be fixed */
   )
{
   assert(scip != NULL);

   if( SCIPvarGetLbLocal(edgevar) < 0.5 && SCIPvarGetUbLocal(edgevar) > 0.5 )
   {
      SCIP_PROPDATA* propdata;

      assert(SCIPisEQ(scip, SCIPvarGetUbLocal(edgevar), 1.0));

      /* get propagator data */
      assert(SCIPfindProp(scip, "stp") != NULL);
      propdata = SCIPpropGetData(SCIPfindProp(scip, "stp"));
      assert(propdata != NULL);

      SCIP_CALL( SCIPchgVarLb(scip, edgevar, 1.0) );
      (*nfixed)++;
      propdata->nfixededges++;
      propdata->postrednfixededges++;
   }
   return SCIP_OKAY;
}
#endif

/** try to make global fixings */
static
SCIP_RETCODE globalfixing(
   SCIP*                 scip,               /**< SCIP structure */
   SCIP_VAR**            vars,               /**< variables */
   int*                  nfixededges,        /**< points to number of fixed edges */
   const SCIP_Real*      fixingbounds,       /**< fixing bounds */
   const GRAPH*          graph,              /**< graph structure */
   SCIP_Real             cutoffbound,        /**> cutoff bound  */
   int                   nedges              /**< number of edges */
)
{
   for( int e = 0; e < nedges; e++ )
   {
      if( SCIPisLT(scip, cutoffbound, fixingbounds[e]) )
      {
         SCIP_VAR* const edgevar = vars[e];

         if( SCIPvarGetLbGlobal(edgevar) < 0.5 && SCIPvarGetUbGlobal(edgevar) > 0.5 )
         {
            assert(SCIPisEQ(scip, SCIPvarGetUbGlobal(edgevar), 1.0));

//#ifdef SCIP_DEBUG
#if 1
            printf("lurking fix: ");
            graph_edge_printInfo(scip, graph, e);
#endif
            SCIPchgVarUbGlobal(scip, edgevar, 0.0);
            (*nfixededges)++;
         }
      }
   }

   return SCIP_OKAY;
}

/* updates fixing bounds for reduced cost fixings */
static
void updateFixingBounds(
   SCIP_Real*            fixingbounds,       /**< fixing bounds */
   const GRAPH*          graph,              /**< graph data structure */
   const SCIP_Real*      cost,               /**< reduced costs */
   const SCIP_Real*      pathdist,           /**> shortest path distances  */
   const PATH*           vnoi,               /**> Voronoi paths  */
   SCIP_Real             lpobjal             /**> LP objective  */
)
{
   const int nnodes = graph->knots;

   for( int k = 0; k < nnodes; k++ )
   {
      if( Is_term(graph->term[k]) && (graph->stp_type == STP_MWCSP || graph->stp_type == STP_PCSPG) )
         continue;

      for( int e = graph->outbeg[k]; e != EAT_LAST; e = graph->oeat[e] )
      {
         const SCIP_Real fixbnd = pathdist[k] + cost[e] + vnoi[graph->head[e]].dist + lpobjal;
         if( fixbnd > fixingbounds[e] )
            fixingbounds[e] = fixbnd;
      }
   }
}


#define STPPROP_EDGE_KILLED -1
#define STPPROP_EDGE_UNSET   0
#define STPPROP_EDGE_SET     1
#define STPPROP_EDGE_FIXED   2


/** This methods tries to fix edges by performing reductions on the current graph.
 *  To this end, the already 0-fixed edges are (temporarily) removed from the underlying graph to strengthen the reduction methods. */
static
SCIP_RETCODE redbasedvarfixing(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph data structure */
   int*                  nfixed              /**< pointer to number of fixed edges */
)
{
   SCIP_VAR** vars;
   GRAPH* copyg;
   IDX* curr;
   SCIP_Real offset;
   int* remain;
   int* edgestate;
   const int nedges = g->edges;

   assert(scip != NULL);
   assert(g != NULL);

   vars = SCIPprobdataGetVars(scip);

   if( vars == NULL )
      return SCIP_OKAY;

   offset = 0.0;

   SCIP_CALL( SCIPallocBufferArray(scip, &remain, nedges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &edgestate, nedges) );

   /* copy the graph */
   SCIP_CALL( graph_copy(scip, g, &copyg) ); // todo only copy and keep memory

   /* set ancestor data structures of the new graph */
   SCIP_CALL( graph_init_history(scip, copyg) );

   for( int e = 0; e < nedges; e++ )
      remain[e] = STPPROP_EDGE_UNSET;

   for( int e = 0; e < nedges; e += 2 )
   {
      const int erev = e + 1;

      /* e or its anti-parallel edge fixed to one? */
      if( SCIPvarGetLbLocal(vars[e]) > 0.5 || SCIPvarGetLbLocal(vars[erev]) > 0.5 )
      {
         const int tail = copyg->tail[e];
         const int head = copyg->head[e];

         graph_knot_chg(copyg, tail, 0);
         graph_knot_chg(copyg, head, 0);

         edgestate[e] = EDGE_BLOCKED;
         edgestate[erev] = EDGE_BLOCKED;

         copyg->cost[e] = 0.0;
         copyg->cost[erev] = 0.0;

         printf("block %d \n", e);

         remain[e] = STPPROP_EDGE_FIXED;
         remain[erev] = STPPROP_EDGE_FIXED;
      }
      else
      {
         edgestate[e] = EDGE_MODIFIABLE;
         edgestate[erev] = EDGE_MODIFIABLE;
      }

      /* both e and its anti-parallel edge fixed to zero? */
      if( SCIPvarGetUbLocal(vars[e]) < 0.5 && SCIPvarGetUbLocal(vars[erev]) < 0.5 )
      {
         assert(SCIPvarGetLbLocal(vars[e]) < 0.5 && SCIPvarGetLbLocal(vars[erev]) < 0.5);

         graph_edge_del(scip, copyg, e, TRUE);
         remain[e] = STPPROP_EDGE_KILLED;
         remain[erev] = STPPROP_EDGE_KILLED;
      }
   }

   /* not at root? */
   if( SCIPgetDepth(scip) > 0 )
   {
      SCIP_CONS* parentcons;
      int naddedconss;

      printf("added %d \n", SCIPnodeGetNAddedConss(SCIPgetCurrentNode(scip)));
      if( SCIPnodeGetNAddedConss(SCIPgetCurrentNode(scip)) != 1 )
         exit(1);

      assert(SCIPnodeGetNAddedConss(SCIPgetCurrentNode(scip)) == 1);

      /* move up branch-and-bound path and check constraints */
      for( SCIP_NODE* node = SCIPgetCurrentNode(scip); SCIPnodeGetDepth(node) > 0;
            node = SCIPnodeGetParent(node) )
      {
         if( SCIPnodeGetNAddedConss(node) != 1 )
            continue;

         char* consname;

         /* get constraints */
         SCIPnodeGetAddedConss(node, &parentcons, &naddedconss, 1);
         consname = (char*) SCIPconsGetName(parentcons);

         printf("CONSNAME %s \n", consname);

         /* terminal inclusion constraint? */
         if( strncmp(consname, "consin", 6) == 0 )
         {
            char* tailptr;
            const int term = (int) strtol(consname + 6, &tailptr, 10);

            SCIPdebugMessage("make terminal %d   \n", term);

            graph_knot_chg(copyg, term, 0);
         }
         /* node removal constraint? */
         else if( strncmp(consname, "consout", 7) == 0 )
         {
            char* tailptr;
            const int vert = (int) strtol(consname + 7, &tailptr, 10);

            SCIPdebugMessage("delete vertex %d \n", vert);

            graph_knot_del(scip, copyg, vert, TRUE);
         }
         else
         {
            printf("found unknown constraint at b&b node \n");
            return SCIP_ERROR;
         }
      }
   }

   SCIP_CALL( graph_path_init(scip, copyg) );

   /* reduce graph */

   SCIP_CALL( level0(scip, copyg) );
 //  SCIP_CALL( reduceStp(scip, &copyg, &offset, 5, FALSE, FALSE, edgestate, FALSE) );
   SCIP_CALL( reduceStp(scip, &copyg, &offset, 2, FALSE, FALSE, NULL, FALSE) );


   assert(graph_valid(copyg));

   graph_path_exit(scip, copyg);

   /* try to fix edges ... */

   for( int e = 0; e < nedges; e++ )
   {
      if( copyg->ieat[e] != EAT_FREE )
      {
         assert(copyg->ieat[flipedge(e)] != EAT_FREE);

         curr = copyg->ancestors[e];

         while( curr != NULL )
         {
            const int i = curr->index;
            assert(i < nedges);
            assert(remain[i] != STPPROP_EDGE_KILLED);

            if( remain[i] == STPPROP_EDGE_UNSET )
            {
               remain[i] = STPPROP_EDGE_SET;
               remain[flipedge(i)] = STPPROP_EDGE_SET;
            }

            curr = curr->parent;
         }
      }
   }

   curr = copyg->fixedges;

   while( curr != NULL )
   {
      const int e = curr->index;
      assert(e < nedges);

      remain[e] = STPPROP_EDGE_FIXED;
      remain[flipedge(e)] = STPPROP_EDGE_FIXED;

      curr = curr->parent;
   }

   /* 1-fixed edge to be deleted? */
   for( int e = 0; e < nedges; e++ )
      if( (remain[e] == STPPROP_EDGE_UNSET || remain[e] == STPPROP_EDGE_KILLED) && (SCIPvarGetLbLocal(vars[e]) > 0.5) )
      {
         int todo;
         printf("1 fix failed %d \n", 0);
         exit(1);

         SCIPdebugMessage("1-fixed arc deleted by reduction methods ... can't propagate  \n \n \n");

         goto TERMINATE;
      }

   /* fix to zero and one */
   for( int e = 0; e < nedges; e += 2 )
   {
      const int erev = e + 1;
      /* edge not set yet? */
      if( remain[e] == STPPROP_EDGE_UNSET )
      {
         assert(remain[erev] == STPPROP_EDGE_UNSET);

         SCIP_CALL( fixedgevar(scip, vars[e], nfixed) );
         SCIP_CALL( fixedgevar(scip, vars[erev], nfixed) );
      }
      else if( remain[e] == STPPROP_EDGE_FIXED )
      {
         int todo;
         assert(remain[erev] == STPPROP_EDGE_FIXED);

//         SCIP_CALL( fixedgevarTo1(scip, vars[e], nfixed) );
  //       SCIP_CALL( fixedgevarTo1(scip, vars[erev], nfixed) );
      }
   }

   printf("nfixed %d \n\n", *nfixed);


   TERMINATE:

   /* free memory */
   graph_free(scip, copyg, TRUE);
   SCIPfreeBufferArray(scip, &edgestate);
   SCIPfreeBufferArray(scip, &remain);

   return SCIP_OKAY;
}


/**@} */

/**@name Callback methods of propagator
 *
 * @{
 */

/** copy method for propagator plugins (called when SCIP copies plugins) */
static
SCIP_DECL_PROPCOPY(propCopyStp)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(prop != NULL);
   assert(strcmp(SCIPpropGetName(prop), PROP_NAME) == 0);

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludePropStp(scip) );

   return SCIP_OKAY;
}


/** destructor of propagator to free user data (called when SCIP is exiting) */
static
SCIP_DECL_PROPFREE(propFreeStp)
{  /*lint --e{715}*/
   SCIP_PROPDATA* propdata;

   /* free propagator data */
   propdata = SCIPpropGetData(prop);
   assert(propdata != NULL);

   SCIPfreeMemoryArrayNull(scip, &(propdata->fixingbounds));
   SCIPfreeMemory(scip, &propdata);

   SCIPpropSetData(prop, NULL);

   return SCIP_OKAY;
}


/** reduced cost propagation method for an LP solution */
static
SCIP_DECL_PROPEXEC(propExecStp)
{  /*lint --e{715}*/
   SCIP_PROPDATA* propdata;
   SCIP_PROBDATA* probdata;
   SCIP_VAR** vars;
   SCIP_VAR* edgevar;
   GRAPH* graph;
   PATH* vnoi;
   SCIP_Real* cost;
   SCIP_Real* costrev;
   SCIP_Real* pathdist;
   SCIP_Real redratio;
   SCIP_Real lpobjval;
   SCIP_Real cutoffbound;
   SCIP_Real minpathcost;
   int* vbase;
   int* pathedge;
   int nedges;
   int nnodes;
   int nfixed;
   SCIP_Bool callreduce;

   *result = SCIP_DIDNOTRUN;

   /* propagator can only be applied during solving stage */
   if( SCIPgetStage(scip) < SCIP_STAGE_SOLVING )
      return SCIP_OKAY;

   /* only call propagator if current node has an LP */
   if( !SCIPhasCurrentNodeLP(scip) )
      return SCIP_OKAY;

   /* only call propagator if optimal LP solution is at hand */
   if( SCIPgetLPSolstat(scip) != SCIP_LPSOLSTAT_OPTIMAL )
      return SCIP_OKAY;

   /* only call propagator if current LP is valid relaxation */
   if( !SCIPisLPRelax(scip) )
      return SCIP_OKAY;

   /* we cannot apply reduced cost strengthening, if no simplex basis is available */
   if( !SCIPisLPSolBasic(scip) )
      return SCIP_OKAY;

   cutoffbound = SCIPgetCutoffbound(scip);

      /* reduced cost strengthening can only be applied, if cutoff is finite */
   if( SCIPisInfinity(scip, cutoffbound) )
      return SCIP_OKAY;

   /* get problem data */
   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   graph = SCIPprobdataGetGraph(probdata);
   assert(graph != NULL);

   nedges = graph->edges;
   nnodes = graph->knots;

   /* get all variables (corresponding to the edges) */
   vars = SCIPprobdataGetVars(scip);

   if( vars == NULL )
      return SCIP_OKAY;

   /* check if all integral variables are fixed */
   if( SCIPgetNPseudoBranchCands(scip) == 0 )
      return SCIP_OKAY;

   /* get propagator data */
   propdata = SCIPpropGetData(prop);
   assert(propdata != NULL);

   propdata->ncalls++;

   if( propdata->nfails > 0 && (propdata->nlastcall + propdata->maxnwaitrounds >= propdata->ncalls)
     && (propdata->nlastcall + propdata->nfails > propdata->ncalls) )
      return SCIP_OKAY;

   propdata->nlastcall = propdata->ncalls;

   /* get LP objective value */
   lpobjval = SCIPgetLPObjval(scip);
   *result = SCIP_DIDNOTFIND;

   /* the required reduced path cost to be surpassed */
   minpathcost = cutoffbound - lpobjval;

   SCIPdebugMessage("cutoffbound %f, lpobjval %f\n", cutoffbound, lpobjval);

   if( propdata->fixingbounds == NULL )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &(propdata->fixingbounds), nedges) );
      for( int i = 0; i < nedges; i++ )
         propdata->fixingbounds[i] = -FARAWAY;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &cost, nedges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &costrev, nedges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &pathdist, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &vbase, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &pathedge, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &vnoi, nnodes) );

   for( unsigned int e = 0; e < (unsigned) nedges; ++e )
   {
      assert(SCIPvarIsBinary(vars[e]));

      /* variable is already fixed, we must not trust the reduced cost */
      if( SCIPvarGetLbLocal(vars[e]) + 0.5 > SCIPvarGetUbLocal(vars[e]) )
      {
         if( SCIPvarGetLbLocal(vars[e]) > 0.5 )
            cost[e] = 0.0;
         else
         {
            assert(SCIPvarGetUbLocal(vars[e]) < 0.5);
            cost[e] = FARAWAY;
         }
      }
      else
      {
         if( SCIPisFeasZero(scip, SCIPgetSolVal(scip, NULL, vars[e])) )
         {
            assert(!SCIPisDualfeasNegative(scip, SCIPgetVarRedcost(scip, vars[e])));
            cost[e] = SCIPgetVarRedcost(scip, vars[e]);
         }
         else
         {
            assert(!SCIPisDualfeasPositive(scip, SCIPgetVarRedcost(scip, vars[e])));
            assert(SCIPisFeasEQ(scip, SCIPgetSolVal(scip, NULL, vars[e]), 1.0) || SCIPisDualfeasZero(scip, SCIPgetVarRedcost(scip, vars[e])));
            cost[e] = 0.0;
         }
      }

      if( SCIPisLT(scip, cost[e], 0.0) )
         cost[e] = 0.0;

      if( e % 2 == 0 )
         costrev[e + 1] = cost[e];
      else
         costrev[e - 1] = cost[e];
   }

   for( int k = 0; k < nnodes; k++ )
      graph->mark[k] = (graph->grad[k] > 0);

   /* distance from root to all nodes */
   graph_path_execX(scip, graph, graph->source, cost, pathdist, pathedge);

   /* no paths should go back to the root */
   for( int e = graph->outbeg[graph->source]; e != EAT_LAST; e = graph->oeat[e] )
      costrev[e] = FARAWAY;

   /* build voronoi diagram */
   graph_voronoiTerms(scip, graph, costrev, vnoi, vbase, graph->path_heap, graph->path_state);

   nfixed = 0;

   for( int k = 0; k < nnodes; k++ )
   {
      if( Is_term(graph->term[k]) && (graph->stp_type == STP_MWCSP || graph->stp_type == STP_PCSPG) )
            continue;

      if( !Is_term(graph->term[k]) && SCIPisGT(scip, pathdist[k] + vnoi[k].dist, minpathcost) )
      {
         for( int e = graph->outbeg[k]; e != EAT_LAST; e = graph->oeat[e] )
         {
            /* try to fix edge and reversed one */
            SCIP_CALL( fixedgevar(scip, vars[e], &nfixed) );
            SCIP_CALL( fixedgevar(scip, vars[flipedge(e)], &nfixed) );
         }
      }
      else
      {
         for( int e = graph->outbeg[k]; e != EAT_LAST; e = graph->oeat[e] )
         {
            if( SCIPisGT(scip, pathdist[k] + cost[e] + vnoi[graph->head[e]].dist, minpathcost) )
            {
               edgevar = vars[e];
               /* try to fix edge */
               SCIP_CALL( fixedgevar(scip, edgevar, &nfixed) );
            }
         }
      }
   }

   /* at root? */
   if( SCIPgetDepth(scip) == 0 )
      updateFixingBounds(propdata->fixingbounds, graph, cost, pathdist, vnoi, lpobjval);

   SCIP_CALL( globalfixing(scip, vars, &nfixed, propdata->fixingbounds, graph, cutoffbound, nedges) );

   SCIPfreeBufferArray(scip, &vnoi);
   SCIPfreeBufferArray(scip, &pathedge);
   SCIPfreeBufferArray(scip, &vbase);
   SCIPfreeBufferArray(scip, &pathdist);
   SCIPfreeBufferArray(scip, &costrev);
   SCIPfreeBufferArray(scip, &cost);

   /* is ratio of newly fixed edges higher than bound? */
   redratio = ((SCIP_Real) propdata->postrednfixededges ) / (graph->edges);

   callreduce = FALSE;

   if( graph->stp_type == STP_SPG || graph->stp_type == STP_RSMT )
   {
      /* not at root? */
      if( SCIPgetDepth(scip) > 0 )
      {
         SCIP_NODE* const currnode = SCIPgetCurrentNode(scip);
         const SCIP_Longint nodenumber = SCIPnodeGetNumber(currnode);

         if( nodenumber != propdata->lastnodenumber || propdata->aggressive )
         {
            printf("new node %lld \n", nodenumber);
            propdata->lastnodenumber = nodenumber;
            callreduce = TRUE;
         }
      }
      else if( SCIPisGT(scip, redratio, REDUCTION_WAIT_RATIO) )
      {
         callreduce = TRUE;
         assert(SCIPisLE(scip, ((SCIP_Real) propdata->nfixededges) / (graph->edges), 1.0));
      }
   }

   if( callreduce )
   {
      SCIPdebugMessage("use reduction techniques \n");
      SCIP_CALL( redbasedvarfixing(scip, graph, &nfixed) );
      propdata->postrednfixededges = 0;
   }

   if( nfixed > 0 )
   {
      SCIPdebugMessage("newly fixed by STP propagator: %d \n", nfixed );

      propdata->nfails = 0;
      *result = SCIP_REDUCEDDOM;

      if( graph->stp_type == STP_SPG || graph->stp_type == STP_RSMT || graph->stp_type == STP_RPCSPG ||
          graph->stp_type == STP_PCSPG || graph->stp_type == STP_DCSTP )
      {
         for( int e = 0; e < nedges; e += 2 )
         {
            const int erev = e + 1;

            /* both e and its anti-parallel edge fixed to zero? */
            if( SCIPvarGetUbGlobal(vars[e]) < 0.5 && SCIPvarGetUbGlobal(vars[erev]) < 0.5 && graph->cost[e] < BLOCKED )
            {
               assert(SCIPvarGetLbLocal(vars[e]) < 0.5 && SCIPvarGetLbLocal(vars[erev]) < 0.5);
               if( graph->cost[e] == graph->cost[erev] )
               {
                  graph->cost[e] = BLOCKED;
                  graph->cost[erev] = BLOCKED;
               }
            }
         }
      }
   }
   else
   {
      propdata->nfails++;
   }

   return SCIP_OKAY;
}

/**@} */

/**@name Interface methods
 *
 * @{
 */


/** fix a variable (corresponding to an edge) to zero */
SCIP_RETCODE fixedgevar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             edgevar,            /**< the variable to be fixed */
   int*                  nfixed              /**< counter that is incriminated if variable could be fixed */
   )
{
   assert(scip != NULL);

   if( SCIPvarGetLbLocal(edgevar) < 0.5 && SCIPvarGetUbLocal(edgevar) > 0.5 )
   {
      SCIP_PROPDATA* propdata;

      /* get propagator data */
      assert(SCIPfindProp(scip, "stp") != NULL);
      propdata = SCIPpropGetData(SCIPfindProp(scip, "stp"));

      assert(propdata != NULL);

      SCIP_CALL( SCIPchgVarUb(scip, edgevar, 0.0) );
      (*nfixed)++;
      propdata->nfixededges++;
      propdata->postrednfixededges++;
   }
   return SCIP_OKAY;
}

/** return total number of arcs fixed by 'fixedgevar' method of this propagator */
int SCIPstpNfixedEdges(
   SCIP*                 scip                /**< SCIP data structure */
      )
{
   SCIP_PROPDATA* propdata;

   assert(scip != NULL);

   /* get propagator data */
   assert(SCIPfindProp(scip, "stp") != NULL);
   propdata = SCIPpropGetData(SCIPfindProp(scip, "stp"));

   assert(propdata != NULL);

   return (propdata->nfixededges);
}


/** creates the stp propagator and includes it in SCIP */
SCIP_RETCODE SCIPincludePropStp(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_PROP* prop;
   SCIP_PROPDATA* propdata;

   /* create redcost propagator data */
   SCIP_CALL( SCIPallocMemory(scip, &propdata) );

   /* include propagator */
   SCIP_CALL( SCIPincludePropBasic(scip, &prop, PROP_NAME, PROP_DESC, PROP_PRIORITY, PROP_FREQ, PROP_DELAY, PROP_TIMING,
         propExecStp, propdata) );

   assert(prop != NULL);

   /* set optional callbacks via setter functions */
   SCIP_CALL( SCIPsetPropCopy(scip, prop, propCopyStp) );
   SCIP_CALL( SCIPsetPropFree(scip, prop, propFreeStp) );
   propdata->nfails = 0;
   propdata->ncalls = 0;
   propdata->nlastcall = 0;
   propdata->lastnodenumber = -1;
   propdata->nfixededges = 0;
   propdata->postrednfixededges = 0;
   propdata->fixingbounds = NULL;

   /* add parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "propagating/" PROP_NAME "/nwaitingrounds",
         "maximum number of rounds before propagating again",
         &propdata->maxnwaitrounds, FALSE, DEFAULT_MAXNWAITINGROUNDS, 1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "propagating/"PROP_NAME"/aggressive",
         "should the heuristic be executed at maximum frequeny?",
         &propdata->aggressive, FALSE, FALSE, NULL, NULL) );

   return SCIP_OKAY;
}

/**@} */
