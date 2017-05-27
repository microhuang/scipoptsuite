/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_ascendprune.c
 * @brief  reduction-based primal heuristic for Steiner problems
 * @author Daniel Rehfeldt
 *
 * This file implements a reducion and dual-cost based heuristic for Steiner problems. See
 * "SCIP-Jack - A solver for STP and variants with parallelization extensions" (2016) by
 * Gamrath, Koch, Maher, Rehfeldt and Shinano
 *
 * A list of all interface methods can be found in heur_ascendprune.h
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "scip/scip.h"
#include "scip/scipdefplugins.h"
#include "scip/cons_linear.h"
#include "heur_ascendprune.h"
#include "heur_local.h"
#include "heur_prune.h"
#include "grph.h"
#include "heur_tm.h"
#include "cons_stp.h"
#include "scip/pub_misc.h"
#include "probdata_stp.h"

#define HEUR_NAME             "ascendprune"
#define HEUR_DESC             "Dual-cost reduction heuristic for Steiner problems"
#define HEUR_DISPCHAR         'A'
#define HEUR_PRIORITY         2
#define HEUR_FREQ             1
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           (SCIP_HEURTIMING_DURINGLPLOOP | SCIP_HEURTIMING_AFTERLPLOOP | SCIP_HEURTIMING_AFTERNODE)
#define HEUR_USESSUBSCIP      FALSE           /**< does the heuristic use a secondary SCIP instance?                                 */

#define XXX 0

#define DEFAULT_MAXFREQPRUNE     FALSE         /**< executions of the heuristic at maximum frequency?                               */
#define ASCENPRUNE_MINLPIMPROVE     0.05          /**< minimum percentual improvement of dual bound (wrt to gap) mandatory to execute heuristic */

#ifdef WITH_UG
extern
int getUgRank(void);
#endif

/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_Real             lastdualbound;      /**< dual bound after the previous run                                 */
   int                   bestsolindex;       /**< best solution during the previous run                             */
   int                   nfailures;          /**< number of failures since last successful call                     */
   SCIP_Bool             maxfreq;            /**< should the heuristic be called at maximum frequency?              */
};


/*
 * Local methods
 */

/* put your local methods here, and declare them static */


/*
 * Callback methods of primal heuristic
 */


/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyAscendPrune)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurAscendPrune(scip) );

   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeAscendPrune)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(scip != NULL);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* free heuristic data */
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */

static
SCIP_DECL_HEURINIT(heurInitAscendPrune)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(scip != NULL);

   /* get heuristic's data */
   heurdata = SCIPheurGetData(heur);

   assert(heurdata != NULL);

   /* initialize data */
   heurdata->nfailures = 0;
   heurdata->bestsolindex = -1;
   heurdata->lastdualbound = 0.0;

   return SCIP_OKAY;
}

/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecAscendPrune)
{  /*lint --e{715}*/
   SCIP_HEURDATA*    heurdata;
   SCIP_PROBDATA*    probdata;
   SCIP_VAR**        vars;
   SCIP_SOL*         bestsol;                        /* best known solution */
   GRAPH*            graph;
   SCIP_Real         dualbound;
   SCIP_Real         gap;
   SCIP_Real*        redcosts;
   SCIP_Bool         success;
   int       e;
   int       nnodes;
   int       nedges;
   int       probtype;
   int*      edgearrint;
   int*      nodearrint;
   STP_Bool*     nodearrchar;

   assert(heur != NULL);
   assert(scip != NULL);
   assert(result != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* get problem data */
   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   /* get graph */
   graph = SCIPprobdataGetGraph(probdata);
   assert(graph != NULL);

   vars = SCIPprobdataGetVars(scip);
   assert(vars != NULL);

   *result = SCIP_DIDNOTRUN;
   probtype = graph->stp_type;

   /* if not STP like variant, return */
   if( probtype != STP_RSMT && probtype != STP_OARSMT )
      return SCIP_OKAY;

   nedges = graph->edges;
   nnodes = graph->knots;
   success = FALSE;

   /* get best current solution */
   bestsol = SCIPgetBestSol(scip);

   /* no solution available? */
   if( bestsol == NULL )
      return SCIP_OKAY;

   /* get dual bound */
   dualbound = SCIPgetDualbound(scip);

   /* no new best solution available? */
   if( heurdata->bestsolindex == SCIPsolGetIndex(SCIPgetBestSol(scip)) && !(heurdata->maxfreq) )
   {
      /* current optimality gap */
      gap = SCIPgetSolOrigObj(scip, bestsol) - dualbound;

      if( SCIPisLT(scip, dualbound - heurdata->lastdualbound, gap * ASCENPRUNE_MINLPIMPROVE ) )
         return SCIP_OKAY;
   }

   heurdata->lastdualbound = dualbound;

   /* allocate memory for ascent and prune */
   SCIP_CALL( SCIPallocBufferArray(scip, &redcosts, nedges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &edgearrint, nedges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nodearrint, nnodes ) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nodearrchar, nnodes) );

   for( e = 0; e < nedges; e++ )
   {
      assert(SCIPvarIsBinary(vars[e]));

      /* variable is already fixed, we must not trust the reduced cost */
      if( SCIPvarGetLbLocal(vars[e]) + 0.5 > SCIPvarGetUbLocal(vars[e]) )
      {
         if( SCIPvarGetLbLocal(vars[e]) > 0.5 )
            redcosts[e] = 0.0;
         else
         {
            assert(SCIPvarGetUbLocal(vars[e]) < 0.5);
            redcosts[e] = FARAWAY;
         }
      }
      else
      {
         if( SCIPisFeasZero(scip, SCIPgetSolVal(scip, NULL, vars[e])) )
         {
            assert(!SCIPisDualfeasNegative(scip, SCIPgetVarRedcost(scip, vars[e])));
            redcosts[e] = SCIPgetVarRedcost(scip, vars[e]);
         }
         else
         {
            assert(!SCIPisDualfeasPositive(scip, SCIPgetVarRedcost(scip, vars[e])));
            assert(SCIPisFeasEQ(scip, SCIPgetSolVal(scip, NULL, vars[e]), 1.0) || SCIPisDualfeasZero(scip, SCIPgetVarRedcost(scip, vars[e])));
            redcosts[e] = 0.0;
         }
      }

      if( SCIPisLT(scip, redcosts[e], 0.0) )
         redcosts[e] = 0.0;

      assert(SCIPisGE(scip, redcosts[e], 0.0));
      assert(!SCIPisEQ(scip, redcosts[e], SCIP_INVALID));
   }

   /* perform ascent and prune */
   SCIP_CALL( SCIPheurAscendAndPrune(scip, heur, graph, redcosts, edgearrint, nodearrint, graph->source[0], nodearrchar, &success, FALSE, TRUE) );

   if( success )
   {
      heurdata->nfailures = 0;
      *result = SCIP_FOUNDSOL;
   }
   else
   {
      heurdata->nfailures++;
   }

   heurdata->bestsolindex = SCIPsolGetIndex(SCIPgetBestSol(scip));

   /* free memory */
   SCIPfreeBufferArray(scip, &nodearrchar);
   SCIPfreeBufferArray(scip, &nodearrint);
   SCIPfreeBufferArray(scip, &edgearrint);
   SCIPfreeBufferArray(scip, &redcosts);

   return SCIP_OKAY;
}


/*
 * primal heuristic specific interface methods
 */


/** ascent and prune */
SCIP_RETCODE SCIPheurAscendAndPrune(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< heuristic data structure or NULL */
   const GRAPH*          g,                  /**< the graph */
   const SCIP_Real*      redcosts,           /**< the reduced costs */
   int*                  edgearrint,         /**< int edges array to store solution */
   int*                  nodearrint,         /**< int vertices array for internal computations */
   int                   root,               /**< the root (used for dual ascent) */
   STP_Bool*             nodearrchar,        /**< STP_Bool vertices array for internal computations */
   SCIP_Bool*            solfound,           /**< has a solution been found? */
   SCIP_Bool             dualascredcosts,    /**< reduced costs from dual ascent? */
   SCIP_Bool             addsol              /**< should the solution be added to SCIP by this method? */
   )
{
   GRAPH* newgraph;
   SCIP_Real maxcost;
   SCIP_Real* nval;
   IDX* curr;
   IDX** ancestors;
   int k;
   int i;
   int a;
   int e;
   int j;
   int tail;
   int head;
   int nvars;
   int nnodes;
   int nedges;
   int probtype;
   int nnewnodes;
   int nnewedges;
   int* mark;
   int* queue;
   int* newedges;
   int* nodechild;
   int* edgeancestor;
   SCIP_Bool pcmw;
   SCIP_Bool success;

#if XXX
   SCIP_Real pobj;
   addsol = FALSE;
#endif

   assert(g != NULL);
   assert(scip != NULL);
   assert(redcosts != NULL);
   assert(edgearrint != NULL);
   assert(nodearrint != NULL);
   assert(nodearrchar != NULL);

   if( root < 0 )
      root = g->source[0];

   mark = g->mark;
   nnodes = g->knots;
   nedges = g->edges;
   newedges = edgearrint;
   probtype = g->stp_type;
   nnewedges = 0;
   nodechild = nodearrint;

   nvars = SCIPprobdataGetNVars(scip);

   /* (R)PCSTP or (R)MWCSP?  */
   pcmw = (probtype == STP_PCSPG || probtype == STP_MWCSP || probtype == STP_RPCSPG || probtype == STP_RMWCSP);

   if( addsol || !dualascredcosts )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &nval, nvars) );
   }
   else
   {
      nval = NULL;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &queue, nnodes + 1) );

   /* red costs not from dual ascent? */
   if( !dualascredcosts )
   {
      /* construct new graph by using reduced costs */

      PATH* vnoi;
      SCIP_Real* costrevorg;
      SCIP_Real* pathdistroot;
      int* scanned;
      int* pathedgeroot;

      assert(nval != NULL);
      assert(nedges >= nnodes);

      maxcost = -FARAWAY;
      costrevorg = nval;
      pathedgeroot = nodearrint;

      SCIP_CALL( SCIPallocBufferArray(scip, &pathdistroot, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &vnoi, nnodes) );

      for( e = 0; e < nedges; e++ )
         costrevorg[e] = redcosts[flipedge(e)];

      for( k = 0; k < nnodes; k++ )
         mark[k] = TRUE;

      /* shortest paths from root to all other vertices with respect to reduced costs */
      graph_path_execX(scip, g, root, redcosts, pathdistroot, pathedgeroot);

      /* compute maximum shortest path from a root to another terminal */
      for( k = 0; k < nnodes; k++ )
      {
         if( Is_term(g->term[k]) && k != root )
         {
            if( SCIPisGT(scip, pathdistroot[k], maxcost) )
               maxcost = pathdistroot[k];
         }
      }

      mark[root] = FALSE;

      /* inward Voronoi region, bases: all terminals except root */
      voronoi_terms(scip, g, costrevorg, vnoi, nodearrint, g->path_heap, g->path_state);

      scanned = nodearrint;

      /* mark vertices that are to stay in the graph */
      for( k = 0; k < nnodes; k++ )
      {
         mark[k] = FALSE;
         scanned[k] = FALSE;

         if( Is_term(g->term[k]) || SCIPisLE(scip, pathdistroot[k] + vnoi[k].dist, maxcost) )
            nodearrchar[k] = TRUE;
         else
            nodearrchar[k] = FALSE;
      }

      /* bfs from root along marked vertices */

      nnewnodes = 0;
      mark[root] = TRUE;
      queue[nnewnodes++] = root;

      /* bfs loop */
      for( j = 0; j < nnewnodes; j++ )
      {
         assert(nnewnodes <= nnodes);

         k = queue[j];

         assert(k < nnodes);

         scanned[k] = TRUE;

         /* traverse outgoing arcs */
         for (a = g->outbeg[k]; a != EAT_LAST; a = g->oeat[a])
         {
            head = g->head[a];

            if( nodearrchar[head] )
            {
               /* vertex not labeled yet? */
               if( !mark[head] )
               {
                  mark[head] = TRUE;
                  queue[nnewnodes++] = head;
               }

               if( !(scanned[head]) && (SCIPisLT(scip, redcosts[a], FARAWAY) || SCIPisLT(scip, redcosts[flipedge(a)], FARAWAY)) )
                  newedges[nnewedges++] = a;
            }
         }
      }

      for( k = 0; k < nnodes; k++ )
         nodechild[k] = -1;

      /* free memory */
      SCIPfreeBufferArray(scip, &vnoi);
      SCIPfreeBufferArray(scip, &pathdistroot);
   }
   else
   {
      int* scanned = nodearrint;

      /* construct new graph corresponding to zero cost paths from the root to all terminals */
      for( k = 0; k < nnodes; k++ )
      {
         scanned[k] = FALSE;
         mark[k] = FALSE;
      }

      /* BFS from root along outgoing arcs of zero reduced cost */

      nnewnodes = 0;
      mark[root] = TRUE;
      queue[nnewnodes++] = root;

      if( pcmw )
      {
         /* bfs loop */
         for( j = 0; j < nnewnodes; j++ )
         {
            assert(nnewnodes <= nnodes);

            k = queue[j];

            assert(k < nnodes);

            scanned[k] = TRUE;

            /* traverse outgoing arcs */
            for( a = g->outbeg[k]; a != EAT_LAST; a = g->oeat[a] )
            {
               if( SCIPisZero(scip, redcosts[a]) )
               {
                  head = g->head[a];

                  if( k == root && Is_term(g->term[head]) )
                     continue;

                  /* vertex not labeled yet? */
                  if( !mark[head] )
                  {
                     mark[head] = TRUE;
                     queue[nnewnodes++] = head;
                  }
                  if( (!scanned[head] || !SCIPisZero(scip, redcosts[flipedge(a)])) && !Is_term(g->term[head]) )
                     newedges[nnewedges++] = a;
               }
            }
         }

         /*
          * add edges to terminals
          * */
         for( k = 0; k < nnodes; k++ )
         {
            if( mark[k] && Is_pterm(g->term[k]) )
            {
               for( e = g->outbeg[k]; e != EAT_LAST; e = g->oeat[e] )
                  if( Is_term(g->term[g->head[e]]) && root != g->head[e] )
                     break;

               assert(e != EAT_LAST);

               newedges[nnewedges++] = e;

               if( !mark[g->head[e]] )
               {
                  nnewnodes++;
                  mark[g->head[e]] = TRUE;
               }
            }
         }

         for( a = g->outbeg[root]; a != EAT_LAST; a = g->oeat[a] )
            if( mark[g->head[a]] )
               newedges[nnewedges++] = a;
      }
      /* no (R)PCSPG or (R)MWCSP */
      else
      {
         /* bfs loop */
         for( j = 0; j < nnewnodes; j++ )
         {
            assert(nnewnodes <= nnodes);

            k = queue[j];
            assert(k < nnodes);

            scanned[k] = TRUE;

            /* traverse outgoing arcs */
            for( a = g->outbeg[k]; a != EAT_LAST; a = g->oeat[a] )
            {
               if( SCIPisZero(scip, redcosts[a]) )
               {
                  head = g->head[a];

                  /* vertex not labeled yet? */
                  if( !mark[head] )
                  {
                     mark[head] = TRUE;
                     queue[nnewnodes++] = head;
                  }
                  if( !scanned[head] || !SCIPisZero(scip, redcosts[flipedge(a)]) )
                     newedges[nnewedges++] = a;
               }
            }
         }
      }

      /* has to be reset because scanned == nodearrint == nodechild */
      for( k = 0; k < nnodes; k++ )
         nodechild[k] = -1;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &edgeancestor, 2 * nnewedges) );

   /* initialize new graph */
   SCIP_CALL( graph_init(scip, &newgraph, nnewnodes, 2 * nnewedges, 1, 0) );

   if( probtype == STP_RSMT || probtype == STP_OARSMT || probtype == STP_GSTP )
      newgraph->stp_type = STP_SPG;
   else
      newgraph->stp_type = probtype;

   if( pcmw )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &(newgraph->prize), nnewnodes) );

      for( k = 0; k < nnodes; k++ )
      {
         if( mark[k] )
         {
            if( (!Is_term(g->term[k])) )
               newgraph->prize[newgraph->knots] = g->prize[k];
            else
               newgraph->prize[newgraph->knots] = 0.0;

            nodechild[k] = newgraph->knots;

            graph_knot_add(newgraph, g->term[k]);
         }
      }

      newgraph->norgmodelknots = nnewnodes;
   }
   else
   {
      for( k = 0; k < nnodes; k++ )
      {
         if( mark[k] )
         {
            nodechild[k] = newgraph->knots;
            graph_knot_add(newgraph, g->term[k]);
         }
      }
   }

   assert(nnewnodes == newgraph->knots);

   /* set root of new graph */
   newgraph->source[0] = nodechild[root];

   assert(newgraph->source[0] >= 0);

   if( g->stp_type == STP_RPCSPG )
      newgraph->prize[newgraph->source[0]] = FARAWAY;

   /* add edges to new graph */
   for( a = 0; a < nnewedges; a++ )
   {
      e = newedges[a];

      tail = nodechild[g->tail[e]];
      head = nodechild[g->head[e]];

      assert(tail >= 0);
      assert(head >= 0);

      for( i = newgraph->outbeg[tail]; i != EAT_LAST; i = newgraph->oeat[i] )
         if( newgraph->head[i] == head )
            break;

      if( i == EAT_LAST )
      {
         edgeancestor[newgraph->edges] = e;
         edgeancestor[newgraph->edges + 1] = flipedge(e);
         graph_edge_add(scip, newgraph, tail, head, g->cost[e], g->cost[flipedge(e)]);
      }
   }
   newgraph->norgmodeledges = newgraph->edges;

   /* initialize ancestors of new graph edges */
   SCIP_CALL( graph_init_history(scip, newgraph) );

   /* initialize shortest path algorithm */
   SCIP_CALL( graph_path_init(scip, newgraph) );

   SCIP_CALL( level0(scip, newgraph) );

#if 0 // debug
   for( k = 0; k < nnodes && !pcmw; k++ )
   {
      if( Is_term(g->term[k]) )
      {
         i = nodechild[k];
         if( i < 0 )
         {
            printf("k %d root %d \n", k, root);
            printf("FAIL in AP \n\n\n");
            return SCIP_ERROR;
         }

         if( newgraph->grad[i] == 0 )
         {
            printf("FAIL GRAD \n\n\n");
            return SCIP_ERROR;

         }
      }
   }
#endif

   nnewedges = newgraph->edges;

   assert(graph_valid(newgraph));

   /* get solution on new graph by PRUNE heuristic */
   SCIP_CALL( SCIPheurPrune(scip, NULL, newgraph, newedges, &success, FALSE, TRUE) );

#if 0 // debug
   for( k = 0; k < newgraph->knots; ++k )
   {
      if( Is_term(newgraph->term[k]) && newgraph->grad[k] == 0 && k != root )
      {
         printf("after i %d r %d \n", k, root);
         return SCIP_ERROR;
      }
   }

   if( !graph_sol_valid(scip, newgraph, newedges) )
   {
      printf("not valid %d \n", 0);
      return SCIP_ERROR;
   }
#endif
   assert(graph_sol_valid(scip, newgraph, newedges));

   graph_path_exit(scip, newgraph);

   if( !success )
   {
      printf("failed to build tree in ascend-prune (by prune) \n");
      goto TERMINATE;
   }

   /* re-transform solution found by prune heuristic */

   ancestors = newgraph->ancestors;

   for( k = 0; k < nnodes; k++ )
      nodearrchar[k] = FALSE;

   for( e = 0; e < nnewedges; e++ )
   {
      if( newedges[e] == CONNECT )
      {
         /* iterate through list of ancestors */
         curr = ancestors[e];
         while( curr != NULL )
         {
            i = edgeancestor[curr->index];

            nodearrchar[g->tail[i]] = TRUE;
            nodearrchar[g->head[i]] = TRUE;

            curr = curr->parent;
         }
      }
   }

   /* prune solution (in the original graph) */

   for( e = 0; e < nedges; e++ )
      newedges[e] = UNKNOWN;

   if( pcmw )
      SCIP_CALL( SCIPheurPrunePCSteinerTree(scip, g, g->cost, newedges, nodearrchar) );
   else
      SCIP_CALL( SCIPheurPruneSteinerTree(scip, g, g->cost, 0, newedges, nodearrchar) );


   assert(graph_sol_valid(scip, g, newedges));

   if( addsol )
   {
      assert(nval != NULL);
      for( e = 0; e < nedges; e++ )
      {
         if( newedges[e] == CONNECT )
            nval[e] = 1.0;
         else
            nval[e] = 0.0;
      }
   }

#if XXX

   pobj = 0.0;

   for( e = 0; e < nedges; e++ )
      if( newedges[e] == CONNECT )
         pobj += g->cost[e];

   FILE *fptr;
   fptr=fopen("redStats.txt","a");
   if(fptr==NULL)
   {
      printf("Error!");
   }

   fprintf(fptr,"%f ", pobj );

   fclose(fptr);
#endif


   success = graph_sol_valid(scip, g, newedges);

   if( success && addsol )
   {
      /* add solution */
      SCIP_SOL* sol = NULL;
      SCIP_CALL( SCIPprobdataAddNewSol(scip, nval, sol, heur, &success) );
   }

   *solfound = success;

 TERMINATE:

   for( k = 0; k < nnodes; k++ )
      mark[k] = (g->grad[k] > 0);

   /* free memory */
   graph_free(scip, newgraph, TRUE);
   SCIPfreeBufferArray(scip, &edgeancestor);
   SCIPfreeBufferArray(scip, &queue);
   SCIPfreeBufferArrayNull(scip, &nval);

   return SCIP_OKAY;
}


/** ascent and prune for prize-collecting Steiner tree and maximum weight connected subgraph */
SCIP_RETCODE SCIPheurAscendAndPrunePcMw(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< heuristic data structure or NULL */
   const GRAPH*          g,                  /**< the graph */
   const SCIP_Real*      redcosts,           /**< the reduced costs */
   int*                  edgearrint,         /**< int edges array to store solution */
   int*                  nodearrint,         /**< int vertices array for internal computations */
   int                   root,               /**< the root (used for dual ascent) */
   STP_Bool*             nodearrchar,        /**< STP_Bool vertices array for internal computations */
   SCIP_Bool*            solfound,           /**< has a solution been found? */
   SCIP_Bool             dualascredcosts,    /**< reduced costs from dual ascent? */
   SCIP_Bool             addsol              /**< should the solution be added to SCIP by this method? */
   )
{
   GRAPH* newgraph;
   SCIP_QUEUE* queue;
   SCIP_Real maxcost;
   SCIP_Real* nval;
   IDX* curr;
   IDX** ancestors;
   int k;
   int i;
   int a;
   int e;
   int tail;
   int head;
   int nvars;
   int nnodes;
   int nedges;
   int probtype;
   int nnewnodes;
   int nnewedges;
   int* mark;
   int* pnode;
   int* newedges;
   int* nodechild;
   int* edgeancestor;
   SCIP_Bool success;

#if XXX
   addsol = FALSE;
#endif

   assert(g != NULL);
   assert(scip != NULL);
   assert(redcosts != NULL);
   assert(edgearrint != NULL);
   assert(nodearrint != NULL);
   assert(nodearrchar != NULL);

   root = g->source[0];

   mark = g->mark;
   nnodes = g->knots;
   nedges = g->edges;
   newedges = edgearrint;
   probtype = g->stp_type;
   nnewnodes = 0;
   nnewedges = 0;
   nodechild = nodearrint;

   nvars = SCIPprobdataGetNVars(scip);

   assert(probtype == STP_PCSPG || probtype == STP_MWCSP);

   if( addsol || !dualascredcosts )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &nval, nvars) );
   }
   else
   {
      nval = NULL;
   }
   SCIP_CALL( SCIPqueueCreate(&queue, nnodes, 2.0) );

   /* red costs not from dual ascent? */
   if( !dualascredcosts )
   {
      /* construct new graph by using reduced costs */

      PATH* vnoi;
      SCIP_Real* costrevorg;
      SCIP_Real* pathdistroot;
      int* scanned;
      int* pathedgeroot;

      assert(nval != NULL);
      assert(nedges >= nnodes);

      maxcost = -FARAWAY;
      costrevorg = nval;
      pathedgeroot = nodearrint;

      SCIP_CALL( SCIPallocBufferArray(scip, &pathdistroot, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &vnoi, nnodes) );

      for( e = 0; e < nedges; e++ )
         costrevorg[e] = redcosts[flipedge(e)];

      for( k = 0; k < nnodes; k++ )
         mark[k] = TRUE;

      /* shortest paths from root to all other vertices with respect to reduced costs */
      graph_path_execX(scip, g, root, redcosts, pathdistroot, pathedgeroot);

      /* compute maximum shortest path from a root to another terminal */
      for( k = 0; k < nnodes; k++ )
      {
         if( Is_term(g->term[k]) && k != root )
         {
            if( SCIPisGT(scip, pathdistroot[k], maxcost) )
               maxcost = pathdistroot[k];
         }
      }

      mark[root] = FALSE;

      /* inward Voronoi region, bases: all terminals except root */
      voronoi_terms(scip, g, costrevorg, vnoi, nodearrint, g->path_heap, g->path_state);

      scanned = nodearrint;

      /* mark vertices that are to stay in the graph */
      for( k = 0; k < nnodes; k++ )
      {
         mark[k] = FALSE;
         scanned[k] = FALSE;

         if( Is_term(g->term[k]) || SCIPisLE(scip, pathdistroot[k] + vnoi[k].dist, maxcost) )
            nodearrchar[k] = TRUE;
         else
            nodearrchar[k] = FALSE;
      }

      /* BFS from root along marked vertices */

      mark[root] = TRUE;
      nnewnodes++;
      SCIP_CALL( SCIPqueueInsert(queue, &g->head[g->inpbeg[root]]) );

      while( !SCIPqueueIsEmpty(queue) )
      {
         pnode = (SCIPqueueRemove(queue));
         k = *pnode;

         scanned[k] = TRUE;

         /* traverse outgoing arcs */
         for( a = g->outbeg[k]; a != EAT_LAST; a = g->oeat[a] )
         {
            head = g->head[a];

            if( nodearrchar[head] )
            {
               /* vertex not labeled yet? */
               if( !mark[head] )
               {
                  mark[head] = TRUE;
                  nnewnodes++;
                  SCIP_CALL( SCIPqueueInsert(queue, &(g->head[a])) );
               }

               if( !(scanned[head]) && (SCIPisLT(scip, redcosts[a], FARAWAY) || SCIPisLT(scip, redcosts[flipedge(a)], FARAWAY)) )
                  newedges[nnewedges++] = a;
            }
         }
      }

      for( k = 0; k < nnodes; k++ )
         nodechild[k] = -1;

      /* free memory */
      SCIPfreeBufferArray(scip, &vnoi);
      SCIPfreeBufferArray(scip, &pathdistroot);
   }
   else
   {
      int* scanned = nodearrint;

      /* construct new graph corresponding to zero cost paths from the root to all terminals */
      for( k = 0; k < nnodes; k++ )
      {
         scanned[k] = FALSE;
         mark[k] = FALSE;
      }

      /* BFS from root along outgoing arcs of zero cost */
      mark[root] = TRUE;
      nnewnodes++;
      SCIP_CALL( SCIPqueueInsert(queue, &g->head[g->inpbeg[root]]) );

      while( !SCIPqueueIsEmpty(queue) )
      {
         pnode = (SCIPqueueRemove(queue));
         k = *pnode;

         scanned[k] = TRUE;

         /* traverse outgoing arcs */
         for( a = g->outbeg[k]; a != EAT_LAST; a = g->oeat[a] )
         {
            head = g->head[a];

            if( SCIPisZero(scip, redcosts[a]) )
            {
               if( k == root && Is_term(g->term[head]) )
                  continue;

               /* vertex not labeled yet? */
               if( !mark[head] )
               {
                  mark[head] = TRUE;
                  nnewnodes++;
                  SCIP_CALL( SCIPqueueInsert(queue, &(g->head[a])) );
               }
               if( !scanned[head] || !SCIPisZero(scip, redcosts[flipedge(a)]) )
                  newedges[nnewedges++] = a;
            }
         }
      }

      for( a = g->outbeg[root]; a != EAT_LAST; a = g->oeat[a] )
      {
         head = g->head[a];
         if( Is_term(g->term[head]) && mark[head] )
            newedges[nnewedges++] = a;
      }

      /* has to be reset because scanned == nodearrint == nodechild */
      for( k = 0; k < nnodes; k++ )
         nodechild[k] = -1;
   }
   SCIPqueueFree(&queue);

   SCIP_CALL( SCIPallocBufferArray(scip, &edgeancestor, 2 * nnewedges) );

   /* initialize new graph */
   SCIP_CALL( graph_init(scip, &newgraph, nnewnodes, 2 * nnewedges, 1, 0) );

   newgraph->stp_type = probtype;

   SCIP_CALL( SCIPallocMemoryArray(scip, &(newgraph->prize), nnewnodes) );

   for( k = 0; k < nnodes; k++ )
   {
      if( mark[k] )
      {
         if( (!Is_term(g->term[k])) )
            newgraph->prize[newgraph->knots] = g->prize[k];
         else
            newgraph->prize[newgraph->knots] = 0.0;

         nodechild[k] = newgraph->knots;

         graph_knot_add(newgraph, g->term[k]);
      }
   }

   newgraph->norgmodelknots = nnewnodes;

   assert(nnewnodes == newgraph->knots);

   /* set root of new graph */
   newgraph->source[0] = nodechild[root];

   /* add edges to new graph */
   for( a = 0; a < nnewedges; a++ )
   {
      e = newedges[a];

      tail = nodechild[g->tail[e]];
      head = nodechild[g->head[e]];

      assert(tail >= 0);
      assert(head >= 0);

      for( i = newgraph->outbeg[tail]; i != EAT_LAST; i = newgraph->oeat[i] )
         if( newgraph->head[i] == head )
            break;

      if( i == EAT_LAST )
      {
         edgeancestor[newgraph->edges] = e;
         edgeancestor[newgraph->edges + 1] = flipedge(e);
         graph_edge_add(scip, newgraph, tail, head, g->cost[e], g->cost[flipedge(e)]);
      }
   }
   newgraph->norgmodeledges = newgraph->edges;

   SCIP_CALL( level0(scip, newgraph) );

   /* initialize ancestors of new graph edges */
   SCIP_CALL( graph_init_history(scip, newgraph) );

   nnewedges = newgraph->edges;

   /* initialize shortest path algorithm */
   SCIP_CALL( graph_path_init(scip, newgraph) );

   assert(graph_valid(newgraph));

   /* get solution on new graph by PRUNE heuristic */
   SCIP_CALL( SCIPheurPrune(scip, NULL, newgraph, newedges, &success, FALSE, TRUE) );

   PATH* path;
   SCIP_Bool dummy;

   SCIP_CALL( SCIPallocBufferArray(scip, &path, newgraph->knots) );

   SCIP_CALL( greedyExtensionPcMw(scip, newgraph, newgraph->cost, path, newedges, nodechild, nodearrchar, &dummy) );

   SCIPfreeBufferArray(scip, &path);

   assert(graph_sol_valid(scip, newgraph, newedges));

   graph_path_exit(scip, newgraph);

   if( !success )
   {
      printf("failed to build tree\n");
      goto TERMINATE;
   }

   /* re-transform solution found by prune heuristic
    *
    * NOTE: actually not necessary */

   ancestors = newgraph->ancestors;

   for( k = 0; k < nnodes; k++ )
      nodearrchar[k] = FALSE;

   for( e = 0; e < nnewedges; e++ )
   {
      if( newedges[e] == CONNECT )
      {
         /* iterate through list of ancestors */
         curr = ancestors[e];

         while( curr != NULL )
         {
            i = edgeancestor[curr->index];

            nodearrchar[g->tail[i]] = TRUE;
            nodearrchar[g->head[i]] = TRUE;

            curr = curr->parent;
         }
      }
   }

   /* prune solution (in the original graph) */

   for( e = 0; e < nedges; e++ )
      newedges[e] = UNKNOWN;

   SCIP_CALL( SCIPheurPrunePCSteinerTree(scip, g, g->cost, newedges, nodearrchar) );

   assert(graph_sol_valid(scip, g, newedges));

#if XXX
   SCIP_Real pobj;
   pobj = 0.0;
   for( e = 0; e < nedges; e++ )
   if( newedges[e] == CONNECT )
   pobj += g->cost[e];

   FILE *fptr;
   fptr=fopen("redStats.txt","a");
   if(fptr==NULL)
   {
      printf("Error!");
   }
   if( g->stp_type == STP_MWCSP )
   fprintf(fptr,"%f ", - pobj - SCIPprobdataGetOffset(scip));
   else
   fprintf(fptr,"%f ", pobj);

   fclose(fptr);
#endif

   success = graph_sol_valid(scip, g, newedges);

   if( success && addsol )
   {
      SCIP_SOL* sol = NULL;
      assert(nval != NULL);

      for( e = 0; e < nedges; e++ )
      {
         if( newedges[e] == CONNECT )
            nval[e] = 1.0;
         else
            nval[e] = 0.0;
      }

      /* add solution */
      SCIP_CALL( SCIPprobdataAddNewSol(scip, nval, sol, heur, &success) );
   }

   *solfound = success;

 TERMINATE:

   for( k = 0; k < nnodes; k++ )
      mark[k] = (g->grad[k] > 0);

   /* free memory */
   graph_free(scip, newgraph, TRUE);
   SCIPfreeBufferArray(scip, &edgeancestor);
   SCIPfreeBufferArrayNull(scip, &nval);

   return SCIP_OKAY;
}


/** creates the prune primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurAscendPrune(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create prune primal heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecAscendPrune, heurdata) );

   assert(heur != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopyAscendPrune) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeAscendPrune) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitAscendPrune) );


   /* add ascend and prune primal heuristic parameters */
   SCIP_CALL( SCIPaddBoolParam(scip, "heuristics/"HEUR_NAME"/maxfreq",
         "should the heuristic be executed at maximum frequeny?",
         &heurdata->maxfreq, FALSE, DEFAULT_MAXFREQPRUNE, NULL, NULL) );

   return SCIP_OKAY;
}
