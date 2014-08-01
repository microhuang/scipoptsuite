/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2013 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_tm.c
 * @brief  TM primal heuristic
 * @author Gerald Gamrath
 * @author Thorsten Koch
 * @author Daniel Rehfeldt
 * @author Michael Winkler
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include "heur_tm.h"
#include "probdata_stp.h"
#include "grph.h"
#include "portab.h"
#include "scip/misc.h"
#include <time.h>
#define HEUR_NAME             "TM"
#define HEUR_DESC             "takahashi matsuyama primal heuristic for steiner trees"
#define HEUR_DISPCHAR         '+'
#define HEUR_PRIORITY         0
#define HEUR_FREQ             1
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           (SCIP_HEURTIMING_BEFORENODE | SCIP_HEURTIMING_DURINGLPLOOP | SCIP_HEURTIMING_AFTERLPLOOP | SCIP_HEURTIMING_AFTERNODE)
#define HEUR_USESSUBSCIP      FALSE  /**< does the heuristic use a secondary SCIP instance? */

#define DEFAULT_EVALRUNS 10 /*10*/
#define DEFAULT_INITRUNS 100 /* TODO CHG TO 100*/
#define DEFAULT_LEAFRUNS 10 /*10*/
#define DEFAULT_ROOTRUNS 50
#define DEFAULT_DURINGLPFREQ 10
#define VEL 1
#define AUTO 0
#define TM 1
#define TMPOLZIN 2

/*
 * Data structures
 */


/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_Longint ncalls;
   int evalruns;
   int initruns;
   int leafruns;
   int rootruns;
   int duringlpfreq;
   unsigned int timing;
};

/*
 * Static methods
 */

/** for debug purposes only */
static
SCIP_RETCODE printGraph(
   SCIP* scip,
   const GRAPH*          graph,              /**< Graph to be printed */
   const char*           filename,           /**< Name of the output file */
   int*                  result
   )
{
   char label[SCIP_MAXSTRLEN];
   FILE* file;
   int e;
   int n;
   int m;
   char* stnodes;
   SCIP_CALL( SCIPallocBufferArray(scip, &stnodes, graph->knots ) );

   assert(graph != NULL);
   file = fopen((filename != NULL) ? filename : "graphX.gml", "w");

   for( e = 0; e < graph->knots; e++ )
   {
      stnodes[e] = FALSE;
   }
   for( e = 0; e < graph->edges; e++ )
   {
      if( result[e] == CONNECT )
      {
	 stnodes[graph->tail[e]] = TRUE;
	 stnodes[graph->head[e]] = TRUE;
      }
   }

   /* write GML format opening, undirected */
   SCIPgmlWriteOpening(file, FALSE);

   /* write all nodes, discriminate between root, terminals and the other nodes */
   e = 0;
   m = 0;
   for( n = 0; n < graph->knots; ++n )
   {
      if( stnodes[n] )
      {
         if( n == graph->source[0] )
         {
            (void)SCIPsnprintf(label, SCIP_MAXSTRLEN, "(%d) Root", n);
            SCIPgmlWriteNode(file, (unsigned int)n, label, "rectangle", "#666666", NULL);
            m = 1;
         }
         else if( graph->term[n] == 0 )
         {
            (void)SCIPsnprintf(label, SCIP_MAXSTRLEN, "(%d) Terminal %d", n, e + 1);
            SCIPgmlWriteNode(file, (unsigned int)n, label, "circle", "#ff0000", NULL);
            e += 1;
         }
         else
         {
            (void)SCIPsnprintf(label, SCIP_MAXSTRLEN, "(%d) Node %d", n, n + 1 - e - m);
            SCIPgmlWriteNode(file, (unsigned int)n, label, "circle", "#336699", NULL);
         }

      }
   }

   /* write all edges (undirected) */
   for( e = 0; e < graph->edges; e ++ )
   {
      if( result[e] == CONNECT )
      {
         (void)SCIPsnprintf(label, SCIP_MAXSTRLEN, "%8.2f", graph->cost[e]);

	 SCIPgmlWriteEdge(file, (unsigned int)graph->tail[e], (unsigned int)graph->head[e], label, "#ff0000");
      }
   }
   SCIPfreeBufferArray(scip, &stnodes);
   /* write GML format closing */
   SCIPgmlWriteClosing(file);

   return SCIP_OKAY;
}

/* prune the Steiner Tree in such a way, that all leaves are terminals */
static
SCIP_RETCODE do_prune(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   const SCIP_Real*      cost,               /**< edge costs */
   int                   layer,
   int*                  result,             /**< ST edges */
   char*                 connected           /**< ST nodes */
   )
{
   PATH*  mst;
   int i;
   int j;
   int count;
   int nnodes;
   nnodes = g->knots;
   SCIP_CALL( SCIPallocBufferArray(scip, &mst, nnodes) );

   /* compute the MST */
   for( i = 0; i < nnodes; i++ )
      g->mark[i] = connected[i];

   assert(g->source[layer] >= 0);
   assert(g->source[layer] <  nnodes);

   graph_path_exec(g, MST_MODE, g->source[layer], g->cost, mst);

   for( i = 0; i < nnodes; i++ )
   {
      if( connected[i] && (mst[i].edge != -1) )
      {
         assert(g->head[mst[i].edge] == i);
         assert(result[mst[i].edge] == -1);

         result[mst[i].edge] = layer;
      }
   }

   /* prune */
   do
   {
      SCIPdebug(fputc('C', stdout));
      SCIPdebug(fflush(stdout));

      count = 0;

      for( i = 0; i < nnodes; i++ )
      {
         if( !g->mark[i] )
            continue;

         if( g->term[i] == layer )
            continue;

         for( j = g->outbeg[i]; j != EAT_LAST; j = g->oeat[j] )
            if( result[j] == layer )
               break;

         if( j == EAT_LAST )
         {
            /* there has to be exactly one incoming edge
             */
            for( j = g->inpbeg[i]; j != EAT_LAST; j = g->ieat[j] )
            {
               if( result[j] == layer )
               {
                  result[j]    = -1;
                  g->mark[i]   = FALSE;
                  connected[i] = FALSE;
                  count++;
                  break;
               }
            }
            assert(j != EAT_LAST);
         }
      }
   }
   while( count > 0 );

   SCIPfreeBufferArray(scip, &mst);

   return SCIP_OKAY;

}

/* pure TM heuristic */
static
SCIP_RETCODE do_tm(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   PATH**                path,
   const SCIP_Real*      cost,
   const SCIP_Real*      costrev,
   int                   layer,
   int                   start,
   int*                  result,
   char*                 connected
   )
{
   int*   cluster;
   int    csize = 0;
   int    k;
   int    e;
   int    i;
   int    j;
   int    old;
   int    newval;
   int nnodes;
   SCIP_Real min;

   assert(scip      != NULL);
   assert(g         != NULL);
   assert(result    != NULL);
   assert(connected != NULL);
   assert(cost      != NULL);
   assert(costrev   != NULL);
   assert(path      != NULL);
   assert(layer >= 0 && layer < g->layers);
   nnodes = g->knots;

   SCIPdebugMessage("Heuristic: Start=%5d ", start);

   SCIP_CALL( SCIPallocBufferArray(scip, &cluster, nnodes) );

   cluster[csize++] = start;

   for( i = 0; i < nnodes; i++ )
   {
      g->mark[i]   = (g->grad[i] > 0);
      connected[i] = FALSE;
   }
   connected[start] = TRUE;

   /* CONSTCOND */
   for( ;; )
   {
      /* Find a terminal with minimal distance to the current ST
       */
      min = FARAWAY;
      old = -1;
      newval = -1;

      for( i = 0; i < nnodes; i++ )
      {
         if( g->grad[i] == 0 || g->term[i] != layer || connected[i] )
            continue;

         /*
          */
         if( path[i] == NULL )
         {
            SCIP_CALL( SCIPallocBufferArray(scip, &(path[i]), nnodes) );

            assert(path[i] != NULL);
            if( g->source[0] == i )
               graph_path_exec(g, FSP_MODE, i, cost, path[i]);
            else
               graph_path_exec(g, FSP_MODE, i, costrev, path[i]);
         }
         for( k = 0; k < csize; k++ )
         {
            j = cluster[k];

            assert(i != j);
            assert(connected[j]);

            if (LT(path[i][j].dist, min))
            {
               min = path[i][j].dist;
               newval = i;
               old = j;
            }
         }
      }
      /* Nichts mehr gefunden, also fertig
       */
      if (newval == -1)
         break;

      /* Weg setzten
       */
      assert((old > -1) && (newval > -1));
      assert(path[newval] != NULL);
      assert(path[newval][old].dist < FARAWAY);
      assert(g->term[newval] == layer);
      assert(!connected[newval]);
      assert(connected[old]);

      SCIPdebug(fputc('R', stdout));
      SCIPdebug(fflush(stdout));

      /*    printf("Connecting Knot %d-%d dist=%d\n", newval, old, path[newval][old].dist);
       */
      /* Gegen den Strom schwimmend alles markieren
       */
      k = old;

      while(k != newval)
      {
         e = path[newval][k].edge;
         k = g->tail[e];

         if (!connected[k])
         {
            connected[k] = TRUE;
            cluster[csize++] = k;
         }
      }
   }

   SCIPdebug(fputc('M', stdout));
   SCIPdebug(fflush(stdout));
   SCIPfreeBufferArray(scip, &cluster);

   SCIP_CALL( do_prune(scip, g, cost, layer, result, connected) );

   return SCIP_OKAY;
}


/* pure TM heuristic for degree constrained STPs */
static
SCIP_RETCODE do_tm_degcons(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   PATH**                path,
   const SCIP_Real*      cost,
   const SCIP_Real*      costrev,
   int                   layer,
   int                   start,
   int*                  result,
   char*                 connected
   )
{
   int*   cluster;
   int    csize = 0;
   int    k;
   int    e;
   int    i;
   int    j;
   int    old;
   int    newval;
   int nnodes;
   int currdeg;
   int* maxdeg;
   int* stpdeg;
   SCIP_Real min;

   assert(scip      != NULL);
   assert(g         != NULL);
   assert(result    != NULL);
   assert(connected != NULL);
   assert(cost      != NULL);
   assert(costrev   != NULL);
   assert(path      != NULL);
   assert(g->maxdeg != NULL);
   assert(layer >= 0 && layer < g->layers);
   nnodes = g->knots;
   maxdeg = g->maxdeg;
   SCIPdebugMessage("Heuristic: Start=%5d ", start);

   /* allocate memory */
   SCIP_CALL( SCIPallocBufferArray(scip, &cluster, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &stpdeg, nnodes) );

   cluster[csize++] = start;

   for( i = 0; i < nnodes; i++ )
   {
      g->mark[i]   = (g->grad[i] > 0);
      connected[i] = FALSE;
      stpdeg[i] = 0;
   }
   connected[start] = TRUE;

   /* CONSTCOND */
   for( ;; )
   {
      /* find a terminal with minimal distance to the current ST */
      min = FARAWAY;
      currdeg = -1;
      old = -1;
      newval = -1;
      for( i = 0; i < nnodes; i++ )
      {
         if( g->grad[i] == 0 || g->term[i] != layer || connected[i] )
            continue;

         /* if they don't exist yet, compute shortest paths from node i to all other nodes */
         if( path[i] == NULL )
         {
            SCIP_CALL( SCIPallocBufferArray(scip, &(path[i]), nnodes) );

            assert(path[i] != NULL);
            if( g->source[0] == i )
               graph_path_exec(g, FSP_MODE, i, cost, path[i]);
            else
               graph_path_exec(g, FSP_MODE, i, costrev, path[i]);
         }
         /* find shortest path from node i (terminal!) to the current Steiner subtree */
         for( k = 0; k < csize; k++ )
         {
            j = cluster[k];

            assert(i != j);
            assert(connected[j]);
            //printf("candidate %d (%d) for %d (%d) \n", i, maxdeg[i], j, maxdeg[j]);
            if( stpdeg[j] < maxdeg[j] && ( (maxdeg[i] > currdeg) || (maxdeg[i] >= currdeg && LT(path[i][j].dist, min)) ) )
            {
               min = path[i][j].dist;
               newval = i;
               old = j;
	       currdeg = maxdeg[i];
            }
         }
      }
      /* if no new value has been found, all terminals are connected */
      if( newval == -1 )
         break;

      assert((old > -1) && (newval > -1));
      assert(path[newval] != NULL);
      assert(path[newval][old].dist < FARAWAY);
      assert(g->term[newval] == layer);
      assert(!connected[newval]);
      assert(connected[old]);

      SCIPdebug(fputc('R', stdout));
      SCIPdebug(fflush(stdout));

      /* traverse the new path */
      k = old;
      stpdeg[old]++;
      while( k != newval )
      {
         e = path[newval][k].edge;
         k = g->tail[e];
         stpdeg[k]++;
         if (!connected[k])
         {
            connected[k] = TRUE;
            cluster[csize++] = k;
         }
      }
   }

   SCIPdebug(fputc('M', stdout));
   SCIPdebug(fflush(stdout));

   /* free local arrays */
   SCIPfreeBufferArray(scip, &cluster);
   SCIPfreeBufferArray(scip, &stpdeg);

   /* prune the steiner tree */
   SCIP_CALL( do_prune(scip, g, cost, layer, result, connected) );

   return SCIP_OKAY;
}

static
SCIP_RETCODE do_tm_polzin(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   SCIP_PQUEUE*          pqueue,
   GNODE**               gnodearr,
   const SCIP_Real*      cost,
   const SCIP_Real*      costrev,
   int                   layer,
   SCIP_Real**           distarr,
   int                   start,
   int*                  result,
   int*                  vcount,
   int*                  nodenterms,
   int**                 basearr,
   int**                 edgearr,
   char                  firstrun,
   char*                 connected
   )
{
   int    k;
   int    i;
   int    j;
   int    best;
   int    term;
   int    count;
   int   nnodes;
   int   nterms;

   assert(scip      != NULL);
   assert(g         != NULL);
   assert(result    != NULL);
   assert(connected != NULL);
   assert(cost      != NULL);
   assert(costrev   != NULL);
   nnodes = g->knots;
   nterms = g->terms;

   SCIPdebugMessage("TM_Polzin Heuristic: Start=%5d ", start);

   /* if the heuristic is called for the first time several data structures have to be set up */
   if( firstrun )
   {
      PATH* vnoi;
      SCIP_Real* vcost;
      int old;
      int oedge;
      int root = g->source[0];
      int   ntovisit;
      int   nneighbnodes;
      int   nneighbterms;
      int   nreachednodes;
      int*  state;
      int*  vbase;
      int*  terms;
      int*  tovisit;
      int*  reachednodes;
      char* termsmark;
      char* visited;
      /* PHASE I: */
      for( i = 0; i < nnodes; i++ )
      {
         g->mark[i] = (g->grad[i] > 0);
      }

      /* allocate memory needed in PHASE I */
      SCIP_CALL( SCIPallocBufferArray(scip, &terms, nterms) );
      SCIP_CALL( SCIPallocBufferArray(scip, &termsmark, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &vnoi, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &visited, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &reachednodes, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &vbase, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &tovisit, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &vcost, nnodes) );

      j = 0;
      for( i = 0; i < nnodes; i++ )
      {
         visited[i] = FALSE;
         if( Is_term(g->term[i]) )
         {
            termsmark[i] = TRUE;
            terms[j++] = i;
         }
         else
         {
            termsmark[i] = FALSE;
         }
      }
      assert(j == nterms);

      voronoi(g, cost, costrev, termsmark, vbase, vnoi);
      state = g->path_state;
      /* for( k = 0; k < nnodes; k++ )
         {
	 assert(termsmark[vbase[k]]);
         }*/

      for( k = 0; k < nnodes; k++ )
      {
         connected[k] = FALSE;
	 vcount[k] = 0;
	 SCIP_CALL( SCIPallocBuffer(scip, &gnodearr[k]) );
	 gnodearr[k]->number = k;
         if( !Is_term(g->term[k]) )
         {
	    distarr[k][0] = vnoi[k].dist;
            edgearr[k][0] = vnoi[k].edge;
            basearr[k][0] = vbase[k];
            nodenterms[k] = 1;
         }
         else
         {
            nodenterms[k] = 0;
	    edgearr[k][0] = UNKNOWN;
            termsmark[k] = FALSE;
         }
         state[k] = UNKNOWN;
         vcost[k] = vnoi[k].dist;
         vnoi[k].dist = FARAWAY;
      }

      /* for each terminal: extend the voronoi regions until all neighbouring terminals have been visited */
      for( i = 0; i < nterms; i++ )
      {
         //printf("term: %d \n", terms[i]);
         nneighbterms = 0;
         nneighbnodes = 0;
         nreachednodes = 0;

         /* DFS (starting from terminal i) until the entire voronoi region has been visited */
         tovisit[0] = terms[i];
         ntovisit = 1;
         visited[terms[i]] = TRUE;
         state[terms[i]] = CONNECT;
         while( ntovisit > 0 )
         {
            /* iterate all incident edges */
            old = tovisit[--ntovisit];

	    for( oedge = g->outbeg[old]; oedge != EAT_LAST; oedge = g->oeat[oedge] )
            {
               k = g->head[oedge];

               /* is node k in the voronoi region of the i-th terminal ? */
               if( vbase[k] == terms[i] )
               {
                  if( !visited[k] )
                  {
                     state[k] = CONNECT;
		     //printf("   vnoinode: %d ",k);
                     tovisit[ntovisit++] = k;
                     visited[k] = TRUE;
                     reachednodes[nreachednodes++] = k;
                  }
               }
               else
               {
                  if( !visited[k] )
                  {
                     visited[k] = TRUE;
                     vnoi[k].dist = vcost[old] + ((vbase[k] == root)? cost[oedge] : costrev[oedge]);
                     vnoi[k].edge = oedge;

                     if( termsmark[vbase[k]] == FALSE )
                     {
                        termsmark[vbase[k]] = TRUE;
                        nneighbterms++;
                     }
                     tovisit[nnodes - (++nneighbnodes)] = k;
                  }
                  else
                  {
                     /* if edge 'oedge' allows a shorter connection of node k, update */
                     if( SCIPisGT(scip, vnoi[k].dist, vcost[old] + ((vbase[k] == root)? cost[oedge] : costrev[oedge])) )
                     {
                        vnoi[k].dist = vcost[old] + ((vbase[k] == root)? cost[oedge] : costrev[oedge]);
                        vnoi[k].edge = oedge;
                     }
                  }
               }
            }
         }

         count = 0;
         for( j = 0; j < nneighbnodes; j++ )
         {
            heap_add(g->path_heap, state, &count, tovisit[nnodes - j - 1], vnoi);
            //   printf( "heap add %d cost %e\n", tovisit[nnodes - j - 1], vnoi[tovisit[nnodes - j - 1]].dist);
         }
         SCIP_CALL( voronoi_extend2(scip, g, ((vbase[k] == root)? cost : costrev), vnoi, distarr, basearr, edgearr, termsmark, reachednodes, &nreachednodes, nodenterms,
               nneighbterms, terms[i], nneighbnodes) );
         // printf( "terminal: %d\n", terms[i]);
         reachednodes[nreachednodes++] = terms[i];

         for( j = 0; j < nreachednodes; j++ )
         {
            //	 printf( "reachdnode: : %d, vnoibase: %d \n", reachednodes[j], vbase[reachednodes[j]]);
            vnoi[reachednodes[j]].dist = FARAWAY;
            state[reachednodes[j]] = UNKNOWN;
            visited[reachednodes[j]] = FALSE;
         }

         for( j = 0; j < nneighbnodes; j++ ) // TODO AVOID DOUBLE WORK
         {
            vnoi[tovisit[nnodes - j - 1]].dist = FARAWAY;
            state[tovisit[nnodes - j - 1]] = UNKNOWN;
            visited[tovisit[nnodes - j - 1]] = FALSE;
         }
      }

      /* for each node v: sort the terminal arrays according to their distance to v */
      for( i = 0; i < nnodes; i++ )
      {
	 SCIPsortRealIntInt(distarr[i], basearr[i], edgearr[i], nodenterms[i]);
	 //printf(" node %d terms: ", i);
         //for( j = 0; j < nodenterms[i]; j++ )
         //printf( " %d, ", basearr[i][j]);
         //printf(" \n ");
      }

      /* free memory */
      SCIPfreeBufferArray(scip, &vnoi);
      SCIPfreeBufferArray(scip, &vbase);
      SCIPfreeBufferArray(scip, &terms);
      SCIPfreeBufferArray(scip, &termsmark);
      SCIPfreeBufferArray(scip, &visited);
      SCIPfreeBufferArray(scip, &tovisit);
      SCIPfreeBufferArray(scip, &reachednodes);
      SCIPfreeBufferArray(scip, &vcost);
   }

   /** PHASE II **/
   else
   {
      for( k = 0; k < nnodes; k++ )
      {
         connected[k] = FALSE;
	 vcount[k] = 0;
      }
   }

   connected[start] = TRUE;
   gnodearr[start]->dist = distarr[start][0];
   SCIP_CALL( SCIPpqueueInsert(pqueue, gnodearr[start]) );

   //printf("inserted\n");
   while( SCIPpqueueNElems(pqueue) > 0 )
   {
      best = ((GNODE*) SCIPpqueueRemove(pqueue))->number;
      //printf("best: %d\n", best);
      term = basearr[best][vcount[best]];

      /* has the terminal already been connected? */
      if( !connected[term] )
      {
         /* connect the terminal */
         k = g->tail[edgearr[best][vcount[best]]];
         while( k != term )
         {
            j = 0;
            // printf(" term  : %d != term %d \n", nodeterms[k][ncheckedterms[k] + j]->terminal, best->terminal);
	    while( basearr[k][vcount[k] + j] != term )
            {
               j++;
            }
            //printf("  move on node %d \n", k);

            if( !connected[k] )
            {
	       assert(vcount[k] == 0);

               connected[k] = TRUE;
	       while( vcount[k] < nodenterms[k] && connected[basearr[k][vcount[k]]] )
               {
	          vcount[k]++;
		  j--;
               }

               if( vcount[k] < nodenterms[k] )
	       {
		  gnodearr[k]->dist = distarr[k][vcount[k]];
		  SCIP_CALL( SCIPpqueueInsert(pqueue, gnodearr[k]) );
                  //SCIP_CALL( SCIPpqueueInsert(pqueue, nodeterms[k][vcount[k]]) );
	       }
	    }

            assert( vcount[k] + j < nodenterms[k] );
	    k = g->tail[edgearr[k][vcount[k] + j]];
         }
         /* finally, connected the terminal reached*/
         //assert( k == best->terminal );
         assert( k == term );
         if( !connected[k] )
         {
            connected[k] = TRUE;
	    //printf("  move on term: %d \n", k);
	    //assert( ncheckedterms[k] == 0 );
	    assert( vcount[k] == 0 );
            while( vcount[k] < nodenterms[k] && connected[basearr[k][vcount[k]]] )
            {
	       vcount[k]++;
            }
            if( vcount[k] < nodenterms[k] )
	    {
	       gnodearr[k]->dist = distarr[k][vcount[k]];
	       SCIP_CALL( SCIPpqueueInsert(pqueue, gnodearr[k]) );
               //SCIP_CALL( SCIPpqueueInsert(pqueue, nodeterms[k][vcount[k]]) );
	    }
	 }
      }

      while( vcount[best] + 1 < nodenterms[best] )
      {
         if( !connected[basearr[best][++vcount[best]]] )
         {
	    gnodearr[best]->dist = distarr[best][vcount[best]];
	    SCIP_CALL( SCIPpqueueInsert(pqueue, gnodearr[best]) );
            //SCIP_CALL( SCIPpqueueInsert(pqueue, nodeterms[best][vcount[best]]) );
            break;
         }
      }
      // printf("after reinsert, k : %d \n", k);
   }

   /* prune the ST, so that all leaves are terminals */
   SCIP_CALL( do_prune(scip, g, cost, layer, result, connected) );

   return SCIP_OKAY;
}



static
SCIP_RETCODE do_layer(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*  graph,
   int           layer,
   int*          best_result,
   int           runs,
   const SCIP_Real* cost,
   const SCIP_Real* costrev
   )
{
   PATH** path;
   char* connected;
   int* result;
   int* start;
   SCIP_Real obj;
   SCIP_Real objt;
   SCIP_Real min = FARAWAY;
   int best = -1;
   int k;
   int r;
   int e;
   int nnodes;
   int nedges;
   int mode;

   GNODE** gnodearr;
   int* nodenterms;
   int nterms;
   int** basearr;
   SCIP_Real** distarr;
   int** edgearr;
   int* vcount;
   SCIP_PQUEUE* pqueue;

   assert(scip != NULL);
   assert(graph != NULL);
   assert(best_result != NULL);
   assert(cost != NULL);
   assert(costrev != NULL);
   assert(layer >= 0 && layer < graph->layers);

   nnodes = graph->knots;
   nedges = graph->edges;
   nterms = graph->terms;

   SCIP_CALL( SCIPallocBufferArray(scip, &connected, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &start, nnodes) );
   SCIP_CALL( SCIPallocBufferArray(scip, &result, nedges) );

   /* get user parameter */
   SCIP_CALL( SCIPgetIntParam(scip, "stp/tmheuristic", &mode) );
   if( 1 )
      printf(" tmmode: %d ->", mode);
   assert(mode == AUTO || mode == TM || mode == TMPOLZIN);

   if( mode == AUTO )
   {
      /* are there enough terminals for the TM Polzin variant to (expectably) be advantageous? */
      if( SCIPisGE(scip, ((double) nterms) / ((double) nnodes ), 0.1) )
         mode = TMPOLZIN;
      else
         mode = TM;
   }
   if( 1 )
      printf(" %d \n", mode);
   /* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    * Patch um die heuristic nach einem restruct starten zu koennen
    * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    */
   if( best >= nnodes )
      best = -1;

   if( graph->layers > 1 )
   {
      /*  SCIP_CALL( do_heuristic(scip, graph, layer, best_result, graph->source[layer], connected, cost, costrev, path) );*/
      assert(0);
   }
   else
   {
      runs = (runs > nnodes) ? nnodes         : runs;
      best = (best < 0)            ? graph->source[layer] : best;

      for( k = 0; k < nnodes; k++ )
      {
         assert(graph->grad[k] > 0);

         start[k] = k;
      }

      /* if we run over all nodes, we do not need to do the following */
      if( runs < nnodes )
      {
#if 0
         int random;
         int tmp;

         /* swap the starting values randomly
            for( r = 0; r < runs; r++ )
            {
            random   = rand() % nnodes;
            tmp      = start[r];
            start[r] = start[random];
            start[random] = tmp;
            }*/
#else
         int* realterms = SCIPprobdataGetRTerms(scip);
         int nrealterms = SCIPprobdataGetRNTerms(scip);

         start[0] = graph->source[0];
         for( r = 1; r < runs; r++ )
         {
            if( r < nrealterms + 1 )
            {
               start[r] = realterms[r - 1];
               start[realterms[r - 1]] = r;
            }
            else
            {
               break;
            }
         }
#endif
         /* check if we have a best starting value */
         for( r = 0; r < runs; r++ )
            if( start[r] == best )
               break;

         /* do we need to set the start by hand? */
         if( r == runs )
            start[0] = best;
      }
      else
      {
         runs = nnodes;
      }

      if( mode == TM || graph->stp_type == STP_DEG_CONS )
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &path, nnodes) );
         BMSclearMemoryArray(path, nnodes);

         /* for( k = 0; k < nnodes; k++ )
            path[k] = NULL;  TODO why???*/
      }
      else
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &nodenterms, nnodes) );
         SCIP_CALL( SCIPallocBufferArray(scip, &gnodearr, nnodes) );
         SCIP_CALL( SCIPallocBufferArray(scip, &basearr, nnodes) );
         SCIP_CALL( SCIPallocBufferArray(scip, &distarr, nnodes) );
         SCIP_CALL( SCIPallocBufferArray(scip, &edgearr, nnodes) );

         for( k = 0; k < nnodes; k++ )
         {
            SCIP_CALL( SCIPallocBufferArray(scip, &basearr[k], nterms) );
            SCIP_CALL( SCIPallocBufferArray(scip, &distarr[k], nterms) );
            SCIP_CALL( SCIPallocBufferArray(scip, &edgearr[k], nterms) );
         }

         SCIP_CALL( SCIPallocBufferArray(scip, &vcount, nnodes) );
         SCIP_CALL( SCIPpqueueCreate( &pqueue, nnodes, 2, GNODECmpByDist) );
      }
      /*graph_path_exit(graph);
        graph_path_init(graph); */

      for( r = 0; r < runs; r++ )
      {
         /* incorrect if layers > 1 ! */
         assert(graph->layers == 1);

         for( e = 0; e < nedges; e++ )
            result[e] = -1;

         /* SCIP_CALL( do_heuristic(scip, graph, layer, result, start[r], connected, cost, costrev, path, nodeterms, nodenterms, ncheckedterms, pqueue, (r == 0) ,
            (r == runs - 1)) );
            SCIP_CALL( do_heuristic(scip, graph, pqueue, path, gnodearr, distarr, cost, costrev, layer, start[r], result,
            vcount, nodesid, nodenterms, basearr, edgearr, (r == 0), (r == runs - 1), connected) );*/

         if( graph->stp_type == STP_DEG_CONS )
	 {
	    mode = TM; //TODO
            SCIP_CALL( do_tm_degcons(scip, graph, path, cost, costrev, layer, start[r], result, connected) );
	 }
	 else if( mode == TM )
            SCIP_CALL( do_tm(scip, graph, path, cost, costrev, layer, start[r], result, connected) );
	 else
            SCIP_CALL( do_tm_polzin(scip, graph, pqueue, gnodearr, cost, costrev, layer, distarr, start[r], result, vcount,
                  nodenterms, basearr, edgearr, (r == 0), connected) );

         obj = 0.0;
         objt = 0.0;
         /* here another measure than in the do_(...) heuristics is being used*/
         for( e = 0; e < nedges; e++)
            obj += (result[e] > -1) ? graph->cost[e] : 0.0;
         for( e = 0; e < nedges; e++)
            objt += (result[e] > -1) ? cost[e] : 0.0;
         //SCIPdebugMessage(" Obj=%.12e\n", obj);

         if( SCIPisLT(scip, obj, min) )
         {
            min = obj;

            SCIPdebugMessage(" Objt=%.12e    ", objt);
            printf(" Obj(run: %d)=%.12e\n", r, obj);

            for( e = 0; e < nedges; e++ )
	    {
               best_result[e] = result[e];
               //      if( best_result[e] != - 1)
               //	  printf("%d->%d %d\n", graph->tail[e], graph->head[e], best_result[e]);
	    }
            //    printGraph(scip, graph, "DEGCON", best_result);
            //  assert(0);
            best = start[r];
         }
      }
   }

   /* free allocated memory */
   if( mode == TM )
   {
      for( k = 0; k < nnodes; k++ )
      {
         assert(path[k] == NULL || graph->term[k] == layer);
         SCIPfreeBufferArrayNull(scip, &(path[k]));
      }
      SCIPfreeBufferArray(scip, &path);
   }
   else if( mode == TMPOLZIN )
   {
      SCIPpqueueFree(&pqueue);
      for( k = nnodes - 1; k >= 0; k-- )
      {
         SCIPfreeBuffer(scip, &gnodearr[k]);
	 SCIPfreeBufferArray(scip, &distarr[k]);
	 SCIPfreeBufferArray(scip, &edgearr[k]);
	 SCIPfreeBufferArray(scip, &basearr[k]);
      }
      SCIPfreeBufferArray(scip, &distarr);
      SCIPfreeBufferArray(scip, &edgearr);
      SCIPfreeBufferArray(scip, &basearr);
      SCIPfreeBufferArray(scip, &gnodearr);
      SCIPfreeBufferArray(scip, &vcount);
      SCIPfreeBufferArray(scip, &nodenterms);
   }

   /* NOW IN EXTRA FILE
    *SCIP_CALL( do_local(scip, graph, cost, costrev, best_result) ); */
   SCIPfreeBufferArray(scip, &result);
   SCIPfreeBufferArray(scip, &start);
   SCIPfreeBufferArray(scip, &connected);

   return SCIP_OKAY;
}

/*
 * Callback methods of primal heuristic
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyTM)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* @todo copy heuristic? (probdata needs to be copied as well) */
#if 0
   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurTM(scip) );
#endif
   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeTM)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* free heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitTM)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   heurdata->ncalls = 0;

   SCIPheurSetTimingmask(heur, (SCIP_HEURTIMING) heurdata->timing);

   return SCIP_OKAY;
}


/** deinitialization method of primal heuristic (called before transformed problem is freed) */
#if 0
static
SCIP_DECL_HEUREXIT(heurExitTM)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of TM primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define heurExitTM NULL
#endif


/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin) */
#if 0
static
SCIP_DECL_HEURINITSOL(heurInitsolTM)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of TM primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define heurInitsolTM NULL
#endif


/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed) */
#if 0
static
SCIP_DECL_HEUREXITSOL(heurExitsolTM)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of TM primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define heurExitsolTM NULL
#endif


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecTM)
{  /*lint --e{715}*/
   SCIP_VAR** vars;
   SCIP_PROBDATA* probdata;
   SCIP_HEURDATA* heurdata;
   SCIP_SOL* sol;
   GRAPH* graph;
   SCIP_Real* cost;
   SCIP_Real* costrev;
   SCIP_Real* nval;
   SCIP_Real* xval;
   int* results;
   SCIP_Real pobj;
   int nvars;
   int layer;
   int runs;
   int e;
   int v;

   assert(scip != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   *result = SCIP_DELAYED;
   *result = SCIP_DIDNOTRUN;

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   graph = SCIPprobdataGetGraph(probdata);
   assert(graph != NULL);

   runs = 0;

   if( heurtiming & SCIP_HEURTIMING_BEFORENODE )
   {
      if( SCIPgetDepth(scip) > 0 )
         return SCIP_OKAY;

      runs = heurdata->initruns;
   }
   else if( ((heurtiming & SCIP_HEURTIMING_DURINGLPLOOP) && (heurdata->ncalls % heurdata->duringlpfreq == 0)) || (heurtiming & SCIP_HEURTIMING_AFTERLPLOOP) )
      runs = heurdata->evalruns;
   else if( heurtiming & SCIP_HEURTIMING_AFTERNODE )
   {
      if( SCIPgetDepth(scip) == 0 )
         runs = heurdata->rootruns;
      else
         runs = heurdata->leafruns;
   }

   heurdata->ncalls++;

   if( runs == 0 )
      return SCIP_OKAY;

   SCIPdebugMessage("Heuristic Start\n");

   nvars = SCIPprobdataGetNVars(scip);
   vars = SCIPprobdataGetVars(scip);

   SCIP_CALL( SCIPallocBufferArray(scip, &cost, graph->edges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &costrev, graph->edges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &results, graph->edges) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nval, nvars) );

   *result = SCIP_DIDNOTFIND;

   /* */
   if( !SCIPhasCurrentNodeLP(scip) || SCIPgetLPSolstat(scip) != SCIP_LPSOLSTAT_OPTIMAL )
   {
      sol = NULL;
      xval = NULL;
   }
   else
   {
      SCIP_CALL( SCIPcreateSol(scip, &sol, heur) );

      /* copy the current LP solution to the working solution */
      SCIP_CALL( SCIPlinkLPSol(scip, sol) );

      xval = SCIPprobdataGetXval(scip, sol);

      SCIPfreeSol(scip, &sol);
   }

   for( e = 0; e < graph->edges; e++ )
      results[e] = -1;

   for( layer = 0; layer < graph->layers; layer++ )
   {
      if( xval == NULL )
      {
         BMScopyMemoryArray(cost, graph->cost, graph->edges);
         /* TODO chg. for asymmetric graphs */
         for( e = 0; e < graph->edges; e += 2 )
         {
            costrev[e] = cost[e + 1];
            costrev[e + 1] = cost[e];
         }
      }
      else
      {
         /* swap costs; set a high cost if the variable is fixed to 0 */
         for( e = 0; e < graph->edges; e += 2)
         {
            if( SCIPvarGetUbLocal(vars[layer * graph->edges + e + 1]) < 0.5 )
            {
               costrev[e] = 1e+10; /* ???? why does FARAWAY/2 not work? */
               cost[e + 1] = 1e+10;
            }
            else
            {
               costrev[e] = ((1.0 - xval[layer * graph->edges + e + 1]) * graph->cost[e + 1]);
               cost[e + 1] = costrev[e];
            }

            if( SCIPvarGetUbLocal(vars[layer * graph->edges + e]) < 0.5 )
            {
               costrev[e + 1] = 1e+10; /* ???? why does FARAWAY/2 not work? */
               cost[e] = 1e+10;
            }
            else
            {
               costrev[e + 1] = ((1.0 - xval[layer * graph->edges + e]) * graph->cost[e]);
               cost[e] = costrev[e + 1];
            }
         }
      }
      /* can we connect the network */
      SCIP_CALL( do_layer(scip, graph, layer, results, runs, cost, costrev) );

      /* take the path */
      if( graph->layers > 1 )
      {
         for( e = 0; e < graph->edges; e += 2)
         {
            if( (results[e] == layer) || (results[e + 1] == layer) )
               graph_edge_hide(graph, e);
         }
      }
   }

   if( graph->layers > 1 )
      graph_uncover(graph);

   for( v = 0; v < nvars; v++ )
      nval[v] = (results[v % graph->edges] == (v / graph->edges)) ? 1.0 : 0.0;

   if( validate(graph, nval) )
   {
      pobj = 0.0;

      for( v = 0; v < nvars; v++ )
         pobj += graph->cost[v % graph->edges] * nval[v];

      if( SCIPisLT(scip, pobj, SCIPgetPrimalbound(scip)) )
      {
         SCIP_Bool success;

         SCIP_CALL( SCIPprobdataAddNewSol(scip, nval, sol, heur, &success) );

         if( success )
            *result = SCIP_FOUNDSOL;
      }
   }

   SCIPfreeBufferArray(scip, &nval);
   SCIPfreeBufferArray(scip, &results);
   SCIPfreeBufferArray(scip, &cost);
   SCIPfreeBufferArray(scip, &costrev);
   return SCIP_OKAY;
}


/*
 * primal heuristic specific interface methods
 */

/** creates the TM primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurTM(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;
   char paramdesc[SCIP_MAXSTRLEN];

   /* create TM primal heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );
   heur = NULL;

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecTM, heurdata) );

   assert(heur != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopyTM) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeTM) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitTM) );
#if 0
   SCIP_CALL( SCIPsetHeurExit(scip, heur, heurExitTM) );
   SCIP_CALL( SCIPsetHeurInitsol(scip, heur, heurInitsolTM) );
   SCIP_CALL( SCIPsetHeurExitsol(scip, heur, heurExitsolTM) );
#endif

   /* add TM primal heuristic parameters */
   /* TODO: (optional) add primal heuristic specific parameters with SCIPaddTypeParam() here */

   /* add TM primal heuristic parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/evalruns",
         "number of runs for eval",
         &heurdata->evalruns, FALSE, DEFAULT_EVALRUNS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/initruns",
         "number of runs for init",
         &heurdata->initruns, FALSE, DEFAULT_INITRUNS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/leafruns",
         "number of runs for leaf",
         &heurdata->leafruns, FALSE, DEFAULT_LEAFRUNS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/rootruns",
         "number of runs for root",
         &heurdata->rootruns, FALSE, DEFAULT_ROOTRUNS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/duringlpfreq",
         "frequency for calling heuristic during LP loop",
         &heurdata->duringlpfreq, FALSE, DEFAULT_DURINGLPFREQ, 1, INT_MAX, NULL, NULL) );

   (void) SCIPsnprintf(paramdesc, SCIP_MAXSTRLEN, "timing when heuristc should be called (%u:BEFORENODE, %u:DURINGLPLOOP, %u:AFTERLPLOOP, %u:AFTERNODE)", SCIP_HEURTIMING_BEFORENODE, SCIP_HEURTIMING_DURINGLPLOOP, SCIP_HEURTIMING_AFTERLPLOOP, SCIP_HEURTIMING_AFTERNODE);
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/"HEUR_NAME"/timing", paramdesc,
	 (int*) &heurdata->timing, TRUE, (int) HEUR_TIMING, (int) SCIP_HEURTIMING_BEFORENODE, 2 * (int) SCIP_HEURTIMING_AFTERNODE - 1, NULL, NULL) ); /*lint !e713*/

   return SCIP_OKAY;
}
