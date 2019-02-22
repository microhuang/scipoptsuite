/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2019 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   dijkstra.h
 * @brief  Definitions for Disjkstra's shortest path algorithm
 * @author Thorsten Koch
 * @author Marc Pfetsch
 */

#ifndef DIJSKSTRA_H
#define DIJSKSTRA_H

#ifdef __cplusplus
extern "C" {
#endif

/* declare own bools, if necessary */
#ifndef DIJKSTRA_Bool
#define DIJKSTRA_Bool unsigned int           /**< type used for boolean values */
#endif
#ifndef TRUE
#define TRUE  1                              /**< boolean value TRUE */
#define FALSE 0                              /**< boolean value FALSE */
#endif

#define DIJKSTRA_FARAWAY 0xffffffffu         /**< node has distance 'infinity' */
#define DIJKSTRA_UNUSED  0xffffffffu         /**< node is unused */


/** graph structure - use consecutive storage for arcs */
struct DIJKSTRA_Graph
{
   unsigned int          nodes;              /**< number of nodes */
   unsigned int*         outbeg;             /**< indices of out-arcs for each node in arcs array */
   unsigned int*         outcnt;             /**< number of out-arcs for each node */
   unsigned int          arcs;               /**< consecutive storage for all arcs */
   unsigned int*         weight;             /**< corresponding weights for all arcs */
   unsigned int*         head;               /**< target nodes for all arcs */
   unsigned int          minweight;          /**< total minimal weight */
   unsigned int          maxweight;          /**< total maximal weight */
};

/** graph structure - use consecutive storage for arcs */
typedef struct DIJKSTRA_Graph DIJKSTRA_GRAPH;



/** Check whether the data structures of the graph are valid. */
DIJKSTRA_Bool dijkstraGraphIsValid(
   const DIJKSTRA_GRAPH* G                   /**< directed graph to be checked */
   );

/** Dijkstra's algorithm using binary heaps */
unsigned int dijkstra(
   const DIJKSTRA_GRAPH* G,                  /**< directed graph */
   unsigned int          source,             /**< source node */
   unsigned long long*   dist,               /**< node distances (allocated by user) */
   unsigned int*         pred,               /**< node predecessors in final shortest path tree (allocated by user) */
   unsigned int*         entry,              /**< temporary storage (for each node - must be allocated by user) */
   unsigned int*         order               /**< temporary storage (for each node - must be allocated by user) */
   );

/** Dijkstra's algorithm for shortest paths between a pair of nodes using binary heaps */
unsigned int dijkstraPair(
   const DIJKSTRA_GRAPH* G,                  /**< directed graph */
   unsigned int          source,             /**< source node */
   unsigned int          target,             /**< target node */
   unsigned long long*   dist,               /**< node distances (allocated by user) */
   unsigned int*         pred,               /**< node predecessors in final shortest path tree (allocated by user) */
   unsigned int*         entry,              /**< temporary storage (for each node - must be allocated by user) */
   unsigned int*         order               /**< temporary storage (for each node - must be allocated by user) */
   );

/** Dijkstra's algorithm for shortest paths between a pair of nodes using binary heaps and truncated at cutoff */
unsigned int dijkstraPairCutoff(
   const DIJKSTRA_GRAPH* G,                  /**< directed graph */
   unsigned int          source,             /**< source node */
   unsigned int          target,             /**< target node */
   unsigned long long    cutoff,             /**< if the distance of a node reached this value, we truncate the search */
   unsigned long long*   dist,               /**< node distances (allocated by user) */
   unsigned int*         pred,               /**< node predecessors in final shortest path tree (allocated by user) */
   unsigned int*         entry,              /**< temporary storage (for each node - must be allocated by user) */
   unsigned int*         order               /**< temporary storage (for each node - must be allocated by user) */
   );

/** Dijkstra's algorithm for shortest paths between a pair of nodes ignoring nodes, using binary heaps, and truncated at cutoff */
unsigned int dijkstraPairCutoffIgnore(
   const DIJKSTRA_GRAPH* G,                  /**< directed graph */
   unsigned int          source,             /**< source node */
   unsigned int          target,             /**< target node */
   unsigned int*         ignore,             /**< marking nodes to be ignored (if value is nonzero) */
   unsigned long long    cutoff,             /**< if the distance of a node reached this value, we truncate the search */
   unsigned long long*   dist,               /**< node distances (allocated by user) */
   unsigned int*         pred,               /**< node predecessors in final shortest path tree (allocated by user) */
   unsigned int*         entry,              /**< temporary storage (for each node - must be allocated by user) */
   unsigned int*         order               /**< temporary storage (for each node - must be allocated by user) */
   );

#ifdef __cplusplus
}
#endif

#endif
