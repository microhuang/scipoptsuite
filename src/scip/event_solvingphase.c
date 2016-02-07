/* * * * * * * * * * * * *  * * * * * * * * * * * * * * * * * * * * * * * * */
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

/**@file   event_solvingphase.c
 * @brief  event handler for solving phase dependent parameter adjustment
 * @author Gregor Hendel
 *
 * this event handler provides methods to support parameter adjustment at every new of the three solving phases:
 *   - Feasibility phase - before the first solution is found
 *   - Improvement phase - after the first solution was found until an optimal solution is found or believed to be found
 *   - Proof phase - the remaining time of the solution process after an optimal or believed-to-be optimal incumbent has been found.
 *
 * Of course, this event handler cannot detect by itself whether a given incumbent is optimal prior to termination of the
 * solution process. It rather uses heuristic transitions based on properties of the search tree in order to
 * determine the appropriate stage. Settings files can be passed to this event handler for each of the three phases.
 *
 * This approach of phase-based parameter adjustment was first presented in
 *
 * Gregor Hendel
 * Empirical Analysis of Solving Phases in Mixed-Integer Programming
 * Master thesis, Technical University Berlin (2014)
 *
 * with the main results also available from
 *
 * Gregor Hendel
 * Exploiting solving phases in mixed-integer programs (2015)
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/event_solvingphase.h"
#include "string.h"

#define EVENTHDLR_NAME         "solvingphase"
#define EVENTHDLR_DESC         "event handler to adjust settings depending on current stage"

#define EVENTHDLR_EVENT SCIP_EVENTTYPE_BESTSOLFOUND | SCIP_EVENTTYPE_NODESOLVED /**< the actual event to be caught */
#define TRANSITIONMETHODS          "elor" /**< which heuristic transition method: (e)stimate based, (l)ogarithmic regression based, (o)ptimal value based (cheat!),
                                            * (r)ank-1 node based? */
#define DEFAULT_SETNAME     "default.set" /**< default settings file name for all solving phase setting files */
#define DEFAULT_TRANSITIONMETHOD      'r' /**< the default transition method */
#define DEFAULT_NODEOFFSET             50 /**< default node offset before transition to proof phase is active */
#define DEFAULT_FALLBACK            FALSE /**< should the phase transition fall back to suboptimal phase? */
#define DEFAULT_INTERRUPTOPTIMAL    FALSE /**< should solving process be interrupted if optimal solution was found? */
#define DEFAULT_ADJUSTRELPSWEIGHTS  FALSE /**< should the scoring weights of the hybrid reliability pseudo cost branching rule be adjusted? */
#define DEFAULT_USEFILEWEIGHTS      FALSE /**< should weights from a weight file be used to adjust branching score weights? */
#define DEFAULT_USEWEIGHTEDQUOTIENTS TRUE /**< should weighted quotients be used to adjust branching score weights? */

#define DEFAULT_ENABLED             FALSE /**< should the event handler be executed? */
#define DEFAULT_TESTMODE            FALSE /**< should the event handler test the criteria? */

#define DEFAULT_USERESTART1TO2      FALSE /**< should a restart be applied between the feasibility and improvement phase? */
#define DEFAULT_USERESTART2TO3      FALSE /**< should a restart be applied between the improvement and the proof phase? */

/* logarithmic regression settings */
#define DEFAULT_LOGREGRESSION_XTYPE   'n' /**< default type to use for log regression - (t)ime, (n)odes, (l)p iterations */
#define LOGREGRESSION_XTYPES        "lnt" /**< available types for log regression - (t)ime, (n)odes, (l)p iterations */
#define SQUARED(x) ((x) * (x))
/*
 * Data structures
 */

/** enumerator to represent the current solving phase */
enum SolvingPhase
{
   SOLVINGPHASE_UNINITIALIZED = -1,          /**< solving phase has not been initialized yet */
   SOLVINGPHASE_FEASIBILITY   = 0,           /**< no solution was found until now */
   SOLVINGPHASE_IMPROVEMENT   = 1,           /**< current incumbent solution is suboptimal */
   SOLVINGPHASE_PROOF         = 2            /**< current incumbent is optimal */
};
typedef enum SolvingPhase SOLVINGPHASE;


/** data structure for incremental logarithmic or linear regression of data points (X_i, Y_i)  */
struct SCIP_Regression
{
   SCIP_Real             lastx;              /**< X-value of last observation */
   SCIP_Real             lasty;              /**< Y-value of last observation */
   SCIP_Real             intercept;          /**< the current axis intercept of the regression */
   SCIP_Real             slope;              /**< the current slope of the regression */
   SCIP_Real             sumx;               /**< accumulated sum of all X observations */
   SCIP_Real             sumy;               /**< accumulated sum of all Y observations */
   SCIP_Real             sumxy;              /**< accumulated sum of all products X * Y */
   SCIP_Real             sumx2;              /**< sum of squares of all X observations */
   SCIP_Real             sumy2;              /**< sum of squares of all Y observations */
   SCIP_Real             corrcoef;           /**< correlation coefficient of X and Y */
   int                   n;                  /**< number of observations so far */
};

typedef struct SCIP_Regression SCIP_REGRESSION;

/** depth information structure */
struct DepthInfo
{
   int                   nsolvednodes;       /**< number of nodes that were solved so far at this depth */
   SCIP_Real             minestimate;        /**< the minimum estimate of a solved node */
   SCIP_NODE**           minnodes;           /**< points to the rank-1 nodes at this depth (open nodes whose estimate is lower than current
                                                  minimum estimate over solved nodes) */
   int                   nminnodes;          /**< the number of minimum nodes */
   int                   minnodescapacity;   /**< the capacity of the min nodes array */
};

typedef struct DepthInfo DEPTHINFO;

/** information about leave numbers of the tree */
struct LeafInfo
{
   SCIP_Longint          nobjleaves;         /**< the number of leave nodes that hit the objective limit */
   SCIP_Longint          ninfeasleaves;      /**< the number of leaf nodes that were infeasible */
};
typedef struct LeafInfo LEAFINFO;

/** event handler data */
struct SCIP_EventhdlrData
{
   char                 logregression_xtype; /**< type to use for log regression - (t)ime, (n)odes, (l)p iterations */
   SCIP_Bool            enabled;             /**< should the event handler be executed? */
   char*                solufilename;        /**< file to parse solution information from */
   char*                setfilefeasibility;  /**< settings file parameter for the feasibility phase */
   char*                setfileimprove;      /**< settings file parameter for the improvement phase */
   char*                setfileproof;        /**< settings file parameter for the proof phase */
   SCIP_Real            optimalvalue;        /**< value of optimal solution of the problem */
   SOLVINGPHASE         solvingphase;        /**< the current solving phase */
   char                 transitionmethod;    /**< transition method from improvement phase -> proof phase?
                                               *  (e)stimate based, (l)ogarithmic regression based, (o)ptimal value based (cheat!),
                                               *  (r)ank-1 node based */
   SCIP_Longint         nodeoffset;          /**< node offset for triggering rank-1 node based phased transition */
   SCIP_Bool            fallback;            /**< should the phase transition fall back to improvement phase? */
   SCIP_Bool            interruptoptimal;    /**< interrupt after optimal solution was found */
   SCIP_Bool            adjustrelpsweights;  /**< should the relpscost cutoff weights be adjusted? */
   SCIP_Bool            useweightedquotients;/**< should weighted quotients between infeasible and pruned leaf nodes be considered? */
   SCIP_Bool            userestart1to2;      /**< should a restart be applied between the feasibility and improvement phase? */
   SCIP_Bool            userestart2to3;      /**< should a restart be applied between the improvement and the proof phase? */
   SCIP_Bool            testmode;            /**< should transitions be tested only, but not triggered? */
   SCIP_Bool            rank1reached;        /**< has the rank-1 transition into proof phase been reached? */
   SCIP_Bool            estimatereached;     /**< has the best-estimate transition been reached? */
   SCIP_Bool            optimalreached;      /**< is the incumbent already optimal? */
   SCIP_Bool            logreached;          /**< has a logarithmic phase transition been reached? */

   SCIP_REGRESSION*     regression;          /**< regression data for log linear regression of the incumbent solutions */

   int                  eventfilterpos;      /**< the event filter position, or -1, if event has not (yet) been caught */
   DEPTHINFO**          depthinfos;          /**< array of depth infos for every depth of the search tree */
   int                  maxdepth;            /**< maximum depth so far */
   int                  nrank1nodes;         /**< number of rank-1 nodes */
   int                  nnodesbelowincumbent; /**< number of open nodes with an estimate lower than the current incumbent */
   LEAFINFO*            leafinfo;            /**< leaf information data structure */
};

/*
 * methods for rank-1 and active estimate transition
 */

/** nodes are sorted first by their estimates, and if estimates are equal, by their number */
static
SCIP_DECL_SORTPTRCOMP(sortCompTreeinfo)
{
   SCIP_NODE* node1;
   SCIP_NODE* node2;
   SCIP_Real estim1;
   SCIP_Real estim2;
   node1 = (SCIP_NODE*)elem1;
   node2 = (SCIP_NODE*)elem2;

   estim1 = SCIPnodeGetEstimate(node1);
   estim2 = SCIPnodeGetEstimate(node2);

   /* compare estimates */
   if( estim1 < estim2 )
      return -1;
   else if( estim1 > estim2 )
      return 1;
   else
   {
      SCIP_Longint number1;
      SCIP_Longint number2;

      number1 = SCIPnodeGetNumber(node1);
      number2 = SCIPnodeGetNumber(node2);

      /* compare numbers */
      if( number1 < number2 )
         return -1;
      else if( number1 > number2 )
         return 1;
   }

   return 0;
}

/** insert an array of open nodes (leaves/siblings/children) into the event handler data structures and update the transition information */
static
SCIP_RETCODE nodesUpdateRank1Nodes(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata,      /**< event handler data */
   SCIP_NODE**           nodes,              /**< array of nodes */
   int                   nnodes              /**< number of nodes */
   )
{
   int n;

   assert(nnodes == 0 || nodes != NULL);
   assert(scip != NULL);

   /* store every relevant node in the data structure for its depth */
   for( n = 0; n < nnodes; ++n )
   {
      SCIP_NODE* node = nodes[n];
      DEPTHINFO* depthinfo = eventhdlrdata->depthinfos[SCIPnodeGetDepth(node)];
      SCIP_Real estim = SCIPnodeGetEstimate(node);

      assert(SCIPnodeGetType(node) == SCIP_NODETYPE_CHILD || SCIPnodeGetType(node) == SCIP_NODETYPE_LEAF
            || SCIPnodeGetType(node) == SCIP_NODETYPE_SIBLING);

      /* an open node has rank 1 if it has an estimate at least as small as the best solved node
       * at this depth
       */
      if( depthinfo->nsolvednodes == 0 || SCIPisGE(scip, depthinfo->minestimate, SCIPnodeGetEstimate(node)) )
      {
         int pos;

         /* allocate additional memory to hold new node */
         if( depthinfo->nminnodes == depthinfo->minnodescapacity )
         {
            SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &depthinfo->minnodes, depthinfo->minnodescapacity, 2 * depthinfo->minnodescapacity) );
            depthinfo->minnodescapacity *= 2;
         }

         /* find correct insert position */
         SCIPsortedvecInsertPtr((void **)depthinfo->minnodes, sortCompTreeinfo, (void*)node, &depthinfo->nminnodes, &pos);
         assert(pos >= 0 && pos < depthinfo->nminnodes);
         assert(depthinfo->minnodes[pos] == node);

         /* update rank 1 node information */
         ++eventhdlrdata->nrank1nodes;
      }

      /* update active estimate information by bookkeeping nodes with an estimate smaller than the current incumbent */
      if( SCIPisLT(scip, estim, SCIPgetUpperbound(scip) ) )
         ++eventhdlrdata->nnodesbelowincumbent;
   }

   return SCIP_OKAY;
}

/** remove a node from the data structures of the event handler */
static
void removeNode(
   SCIP_NODE*            node,               /**< node that should be removed */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   DEPTHINFO* depthinfo;
   int pos;
   SCIP_Bool contained;

   assert(node != NULL);

   /* get depth information for the depth of this node */
   depthinfo = eventhdlrdata->depthinfos[SCIPnodeGetDepth(node)];

   /* no node is saved at this depth */
   if( depthinfo->nminnodes == 0 )
      return;

   /* search for the node by using binary search */
   contained = SCIPsortedvecFindPtr((void **)depthinfo->minnodes, sortCompTreeinfo, (void *)node, depthinfo->nminnodes, &pos);

   /* remove the node if it is contained */
   if( contained )
   {
      SCIPsortedvecDelPosPtr((void **)depthinfo->minnodes, sortCompTreeinfo, pos, &(depthinfo->nminnodes));
      --eventhdlrdata->nrank1nodes;
   }
}

/** returns the current number of rank 1 nodes in the tree */
static
int SCIPgetNRank1Nodes(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(SCIPfindEventhdlr(scip, EVENTHDLR_NAME));

   /* return the stored number of rank 1 nodes only during solving stage */
   if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
      return eventhdlrdata->nrank1nodes;
   else
      return -1;
}

/** returns the current number of open nodes which have an estimate lower than the incumbent solution */
static
int SCIPgetNNodesBelowIncumbent(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(SCIPfindEventhdlr(scip, EVENTHDLR_NAME));

   /* return the stored number of nodes only during solving stage */
   if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
      return eventhdlrdata->nnodesbelowincumbent;
   else
      return -1;
}

/** returns the number of leaves which hit the objective limit */
static
SCIP_Longint SCIPgetNObjLeaves(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   eventhdlrdata = SCIPeventhdlrGetData(SCIPfindEventhdlr(scip, EVENTHDLR_NAME));

   /* no leaf information available prior to solving stage */
   if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
      return eventhdlrdata->leafinfo->nobjleaves;
   else
      return -1;
}

/** returns the number of leaves which happened to be infeasible */
static
SCIP_Longint SCIPgetNInfeasLeaves(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   eventhdlrdata = SCIPeventhdlrGetData(SCIPfindEventhdlr(scip, EVENTHDLR_NAME));

   /* leaf information is only available during solving stage */
   if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
      return eventhdlrdata->leafinfo->ninfeasleaves;
   else
      return -1;
}

/** discards all previous depth information and renews it */
static
SCIP_RETCODE storeRank1Nodes(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   SCIP_NODE** leaves;
   SCIP_NODE** children;
   SCIP_NODE** siblings;

   int nleaves;
   int nchildren;
   int nsiblings;
   int d;

   /* the required node information is only available after solving started */
   if( SCIPgetStage(scip) != SCIP_STAGE_SOLVING )
      return SCIP_OKAY;

   /* reset depth information */
   for( d = 0; d < eventhdlrdata->maxdepth; ++d )
      eventhdlrdata->depthinfos[d]->nminnodes = 0;

   eventhdlrdata->nrank1nodes = 0;
   eventhdlrdata->nnodesbelowincumbent = 0;

   assert(eventhdlrdata != NULL);

   nleaves = nchildren = nsiblings = 0;

   /* get leaves, children, and sibling arrays and update the event handler data structures */
   SCIP_CALL( SCIPgetOpenNodesData(scip, &leaves, &children, &siblings, &nleaves, &nchildren, &nsiblings) );

   SCIP_CALL ( nodesUpdateRank1Nodes(scip, eventhdlrdata, children, nchildren) );

   SCIP_CALL ( nodesUpdateRank1Nodes(scip, eventhdlrdata, siblings, nsiblings) );

   SCIP_CALL ( nodesUpdateRank1Nodes(scip, eventhdlrdata, leaves, nleaves) );

   return SCIP_OKAY;
}

/** allocates memory for a depth info */
static
SCIP_RETCODE createDepthinfo(
   SCIP*                 scip,               /**< SCIP data structure */
   DEPTHINFO**           depthinfo           /**< pointer to depth information structure */
   )
{
   assert(scip != NULL);
   assert(depthinfo != NULL);

   /* allocate the necessary memory */
   SCIP_CALL( SCIPallocMemory(scip, depthinfo) );

   /* reset the depth information */
   (*depthinfo)->minestimate = SCIPinfinity(scip);
   (*depthinfo)->nsolvednodes = 0;
   (*depthinfo)->nminnodes = 0;
   (*depthinfo)->minnodescapacity = 2;

   /* allocate array to store nodes */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(*depthinfo)->minnodes, (*depthinfo)->minnodescapacity) );

   return SCIP_OKAY;
}

/** frees depth information data structure */
static
SCIP_RETCODE freeDepthinfo(
   SCIP*                 scip,               /**< SCIP data structure */
   DEPTHINFO**           depthinfo           /**< pointer to depth information structure */
   )
{
   assert(scip != NULL);
   assert(depthinfo != NULL);
   assert(*depthinfo != NULL);
   assert((*depthinfo)->minnodes != NULL);

   /* free nodes data structure and then the structure itself */
   SCIPfreeBlockMemoryArray(scip, &(*depthinfo)->minnodes, (*depthinfo)->minnodescapacity);
   SCIPfreeMemory(scip, depthinfo);

   return SCIP_OKAY;
}

/** removes the node itself and updates the data if this node defined an active estimate globally or locally at its depth level */
static
void updateDepthinfo(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata,      /**< event handler data */
   SCIP_NODE*            node                /**< node to be removed from the data structures of the event handler */
   )
{
   DEPTHINFO* depthinfo;

   assert(scip != NULL);
   assert(node != NULL);
   assert(eventhdlrdata != NULL);

   /* get the correct depth info at the node depth */
   depthinfo = eventhdlrdata->depthinfos[SCIPnodeGetDepth(node)];
   assert(depthinfo != NULL);

   /* remove the node from the data structures */
   removeNode(node, eventhdlrdata);

   /* compare the node estimate to the minimum estimate of the particular depth */
   if( SCIPisLT(scip, SCIPnodeGetEstimate(node), depthinfo->minestimate) )
      depthinfo->minestimate = SCIPnodeGetEstimate(node);

   /* decrease counter of active estimate nodes if node has an estimate that is below the current incumbent */
   if( SCIPisLT(scip, SCIPnodeGetEstimate(node), SCIPgetUpperbound(scip)) && SCIPnodeGetDepth(node) > 0 )
      eventhdlrdata->nnodesbelowincumbent--;

   /* loop over remaining, unsolved nodes and decide whether they are still rank-1 nodes */
   while( depthinfo->nminnodes > 0 && SCIPisGT(scip, SCIPnodeGetEstimate(depthinfo->minnodes[depthinfo->nminnodes - 1]), depthinfo->minestimate) )
   {
      /* forget about node */
      --(depthinfo->nminnodes);
      --(eventhdlrdata->nrank1nodes);
   }

   /* increase the number of solved nodes at this depth */
   ++(depthinfo->nsolvednodes);
}

/** ensures the capacity of the event handler data structures and removes the current node */
static
SCIP_RETCODE storeDepthInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata,      /**< event handler data */
   SCIP_NODE*            node                /**< node to be removed from the data structures of the event handler */
   )
{
   int nodedepth;
   int newsize;
   int oldsize;

   assert(scip != NULL);
   assert(node != NULL);
   assert(eventhdlrdata != NULL);

   nodedepth = SCIPnodeGetDepth(node);
   oldsize = eventhdlrdata->maxdepth;
   newsize = oldsize;

   /* create depth info array with small initial size or enlarge the existing array if new node is deeper */
   if( oldsize == 0 )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &eventhdlrdata->depthinfos, 10) );
      newsize = 10;
   }
   else if( nodedepth + 1 >= eventhdlrdata->maxdepth )
   {
      assert(nodedepth > 0);
      SCIP_CALL( SCIPreallocMemoryArray(scip, &eventhdlrdata->depthinfos, 2 * nodedepth) );
      newsize = 2 * nodedepth;
   }

   /* create the according depth information pointers */
   if( newsize > oldsize )
   {
      int c;

      for( c = oldsize; c < newsize; ++c )
      {
         SCIP_CALL( createDepthinfo(scip, &(eventhdlrdata->depthinfos[c])) );

      }

      eventhdlrdata->maxdepth = newsize;
   }

   assert(newsize > nodedepth);

   /* remove the node from the data structures */
   updateDepthinfo(scip, eventhdlrdata, node);

   return SCIP_OKAY;
}

/** stores information on focus node */
SCIP_RETCODE SCIPstoreTreeInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_NODE*            focusnode           /**< node data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   /* if the focus node is NULL, we do not need to update event handler data */
   if( focusnode == NULL )
      return SCIP_OKAY;

   eventhdlrdata = SCIPeventhdlrGetData(SCIPfindEventhdlr(scip, EVENTHDLR_NAME));

   /* call removal of this node from the event handler data */
   SCIP_CALL( storeDepthInfo(scip, eventhdlrdata, focusnode) );

   return SCIP_OKAY;
}

/** update leaf information based on the solving status of the node */
static
void updateLeafInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   LEAFINFO*             leafinfo,           /**< leaf information structure */
   SCIP_EVENTTYPE        eventtype           /**< the event at the node in question */
   )
{
   /* increase one of the two counters if the current node was pruned or detected to be infeasible */
   if( SCIPgetLPSolstat(scip) == SCIP_LPSOLSTAT_OBJLIMIT )
      ++(leafinfo->nobjleaves);
   else if( eventtype & SCIP_EVENTTYPE_NODEINFEASIBLE )
      ++(leafinfo->ninfeasleaves);
}

/** reset leaf information */
static
void resetLeafInfo(
   LEAFINFO*             leafinfo            /**< leaf information structure */
   )
{
   leafinfo->ninfeasleaves = 0;
   leafinfo->nobjleaves = 0;
}

/** creates leaf information data structure and resets it */
static
SCIP_RETCODE createLeafInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   LEAFINFO**            leafinfo            /**< leaf information structure */
   )
{
   assert(leafinfo != NULL);
   assert(*leafinfo == NULL);
   SCIP_CALL( SCIPallocMemory(scip, leafinfo) );

   resetLeafInfo(*leafinfo);

   return SCIP_OKAY;
}

/** frees leaf information data structure */
static
SCIP_RETCODE freeLeafInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   LEAFINFO**            leafinfo            /**< leaf information structure */
   )
{
   assert(*leafinfo != NULL);
   SCIPfreeMemory(scip, leafinfo);

   return SCIP_OKAY;
}

#ifndef NDEBUG
/* ensures correctness of counters by explicitly summing up all children, leaves, and siblings with small estimates */
static
int checkLeavesBelowIncumbent(
   SCIP* scip
   )
{
   SCIP_NODE** nodes;
   int nnodes;
   int n;
   SCIP_Real upperbound = SCIPgetUpperbound(scip);
   int nodesbelow = 0;

   /* compare children estimate and current upper bound */
   SCIPgetChildren(scip, &nodes, &nnodes);

   for( n = 0; n < nnodes; ++n )
   {
      if( SCIPisLT(scip, SCIPnodeGetEstimate(nodes[n]), upperbound) )
         ++nodesbelow;
   }

   /* compare sibling estimate and current upper bound */
   SCIPgetSiblings(scip, &nodes, &nnodes);

   for( n = 0; n < nnodes; ++n )
   {
      if( SCIPisLT(scip, SCIPnodeGetEstimate(nodes[n]), upperbound) )
         ++nodesbelow;
   }

   /* compare leaf node and current upper bound */
   SCIPgetLeaves(scip, &nodes, &nnodes);

   for( n = 0; n < nnodes; ++n )
   {
      if( SCIPisLT(scip, SCIPnodeGetEstimate(nodes[n]), upperbound) )
         ++nodesbelow;
   }

   assert(nodesbelow <= SCIPgetNNodesLeft(scip));
   return nodesbelow;
}
#endif

/*
 * SCIP regression methods
 */

/** get the point of the X axis for the regression according to the user choice of X type (time/nodes/iterations)*/
static
SCIP_Real getX(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   SCIP_Real x;

   switch( eventhdlrdata->logregression_xtype )
   {
   case 'l':
      /* get number of LP iterations so far */
      if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING || SCIPgetStage(scip) == SCIP_STAGE_SOLVED )
         x = (SCIP_Real)SCIPgetNLPIterations(scip);
      else
         x = 1.0;
      break;
   case 'n':
      /* get total number of solving nodes so far */
      if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING || SCIPgetStage(scip) == SCIP_STAGE_SOLVED )
         x = (SCIP_Real)SCIPgetNTotalNodes(scip);
      else
         x = 1.0;
      break;
   case 't':
      /* get solving time */
      x = SCIPgetSolvingTime(scip);
      break;
   default:
      x = 1.0;
      break;
   }

   /* prevent the calculation of logarithm too close to zero */
   x = MAX(x, .1);
   x = log(x);

   return x;
}

/** get axis intercept of current tangent to logarithmic regression curve */
static
SCIP_Real getCurrentRegressionTangentAxisIntercept(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data structure */
   )
{
   SCIP_REGRESSION* regression;
   SCIP_Real currentx;

   assert(scip != NULL);
   assert(eventhdlrdata != NULL);

   regression = eventhdlrdata->regression;
   assert(regression != NULL);

   if( regression->n <= 2 )
      return SCIPinfinity(scip);

   currentx = getX(scip, eventhdlrdata);

   return regression->slope * currentx + regression->intercept - regression->slope;
}

static
void updateRegression(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_REGRESSION*      regression,         /**< regression data structure */
   SCIP_Real             x,                  /**< X of observation */
   SCIP_Real             y                   /**< Y of the observation */
   )
{
   assert(scip != NULL);
   assert(regression != NULL);

   assert(!SCIPisInfinity(scip, y) && !SCIPisInfinity(scip, -y));

   /* replace last observation if too close */
   if( regression->n > 0 && SCIPisEQ(scip, regression->lastx, x) )
   {
      regression->sumx2 -= SQUARED(regression->lastx);
      regression->sumy2 -= SQUARED(regression->lasty);
      regression->sumy -= regression->lasty;
      regression->sumx -= regression->lastx;
      regression->sumxy -= regression->lastx * regression->lasty;
   }
   else
      ++(regression->n);

   regression->lastx = x;
   regression->lasty = y;
   regression->sumx += x;
   regression->sumx2 += SQUARED(x);
   regression->sumxy += x * y;
   regression->sumy += y;
   regression->sumy2 += SQUARED(y);

   /* return if there are not enough data points available */
   if( regression->n <= 2 )
      return;

   /* compute slope                 */
   regression->slope = (regression->n * regression->sumxy  -  regression->sumx * regression->sumy) /
       (regression->n * regression->sumx2 - SQUARED(regression->sumx));

   /* compute y-intercept           */
   regression->intercept = (regression->sumy * regression->sumx2  -  regression->sumx * regression->sumxy) /
       (regression->n * regression->sumx2  -  SQUARED(regression->sumx));

   /* compute correlation coeff     */
   regression->corrcoef = (regression->sumxy - regression->sumx * regression->sumy / regression->n) /
            sqrt((regression->sumx2 - SQUARED(regression->sumx)/regression->n) *
            (regression->sumy2 - SQUARED(regression->sumy)/regression->n));
}

/** reset regression data structure */
static
void regressionReset(
   SCIP_REGRESSION*      regression          /**< regression data structure */
   )
{
   regression->intercept = SCIP_INVALID;
   regression->slope = SCIP_INVALID;
   regression->sumx = 0;
   regression->sumx2 = 0;
   regression->sumxy = 0;
   regression->sumy = 0;
   regression->sumy2 = 0;
}

/** creates and resets a regression */
static
SCIP_RETCODE regressionCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_REGRESSION**     regression          /**< regression data structure */
   )
{
   assert(scip != NULL);
   assert(regression != NULL);

   /* allocate necessary memory */
   SCIP_CALL( SCIPallocMemory(scip, regression) );

   /* reset the regression */
   regressionReset(*regression);

   return SCIP_OKAY;
}

/** creates and resets a regression */
static
void regressionFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_REGRESSION**     regression          /**< regression data structure */
   )
{
   SCIPfreeMemory(scip, regression);
}

/*
 * Local methods
 */

/** returns the optimal value for this instance (as passed to the event handler) */
SCIP_Real SCIPgetOptimalSolutionValue(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   eventhdlr = SCIPfindEventhdlr(scip, EVENTHDLR_NAME);
   assert(eventhdlr != NULL);
   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   return eventhdlrdata->optimalvalue;
}

/** checks if rank-1 transition has been reached, that is, when all open nodes have a best-estimate higher than the best
 *  previously checked node at this depth
 */
static
SCIP_Bool checkRankOneTransition(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   /* at least one solution is required for the transition */
   if( SCIPgetNSols(scip) > 0 )
      return (SCIPgetNNodes(scip) > eventhdlrdata->nodeoffset && SCIPgetNRank1Nodes(scip) == 0);
   else
      return FALSE;
}

/** check if Best-Estimate criterion was reached, that is, when the active estimate is not better than the current incumbent solution */
static
SCIP_Bool checkEstimateCriterion(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   if( SCIPgetNSols(scip) > 0 )
      return SCIPgetNNodes(scip) > eventhdlrdata->nodeoffset && SCIPgetNNodesBelowIncumbent(scip) == 0;
   else
      return FALSE;
}

/** check if logarithmic phase transition has been reached.
 *
 *  the logarithmic phase transition is reached when the slope of the logarithmic primal progress (as a function of the number of
 *  LP iterations or solving nodes) becomes gentle. More concretely, we measure the slope by calculating the axis intercept of the tangent of
 *  the logarithmic primal progress. We then compare this axis intercept to the first and current primal bound and say that
 *  the logarithmic phase transition is reached as soon as the axis intercept passes the current primal bound so that the
 *  scalar becomes negative.
 *
 *  While it would be enough to directly compare the primal bound and the axis intercept of the
 *  tangent to check the criterion, the scalar allows for a continuous indicator how far the phase transition is still ahead
 */
static
SCIP_Bool checkLogCriterion(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   if( SCIPgetNSols(scip) > 0 )
   {
      SCIP_Real axisintercept = getCurrentRegressionTangentAxisIntercept(scip, eventhdlrdata);
      if( !SCIPisInfinity(scip, axisintercept) )
      {
         SCIP_Real primalbound;
         SCIP_Real lambda;
         SCIP_Real firstprimalbound = SCIPgetFirstPrimalBound(scip);

         primalbound = SCIPgetPrimalbound(scip);

         /* lambda is the scalar to describe the axis intercept as a linear combination of the current and the first primal bound
          * as intercept = pb_0 + lambda * (pb - pb_0) */
         lambda = (axisintercept - primalbound) / (firstprimalbound - primalbound);

         if( SCIPisNegative(scip, lambda) )
            return TRUE;
      }
   }
   return FALSE;
}

/** check if incumbent solution is nearly optimal; we allow a relative deviation of 10^-9 */
static
SCIP_Bool checkOptimalSolution(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   SCIP_Real referencevalue;
   SCIP_Real primalbound;

   referencevalue = eventhdlrdata->optimalvalue;
   primalbound = SCIPgetPrimalbound(scip);

   if(!SCIPisInfinity(scip, REALABS(primalbound)) && !SCIPisInfinity(scip, referencevalue) )
   {
      SCIP_Real max = MAX3(1.0, REALABS(primalbound), REALABS(referencevalue));

      if( EPSZ((primalbound - referencevalue)/max, 1e-9) )
         return TRUE;
   }
   return FALSE;
}

/** check if we are in the proof phase */
static
SCIP_Bool transitionPhase3(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata
   )
{
   if( eventhdlrdata->solvingphase == SOLVINGPHASE_PROOF && !eventhdlrdata->fallback )
      return TRUE;

   /* check criterion based on selected transition method */
   switch( eventhdlrdata->transitionmethod )
   {
      case 'r':

         /* check rank-1 transition */
         if( checkRankOneTransition(scip, eventhdlrdata) )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "reached rank-1 transition: nodes: %lld, rank-1: %d bound: %9.5g time: %.2f\n",
                  SCIPgetNNodes(scip), SCIPgetNRank1Nodes(scip), SCIPgetPrimalbound(scip), SCIPgetSolvingTime(scip));
            return TRUE;
         }
         break;
      case 'o':

         /* cheat and use knowledge about optimal solution */
         if( checkOptimalSolution(scip, eventhdlrdata) )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "optimal solution found: %lld, bound: %9.5g time: %.2f\n",
                  SCIPgetNNodes(scip), SCIPgetPrimalbound(scip), SCIPgetSolvingTime(scip));
            return TRUE;
         }
         break;
      case 'e':

         /* check best-estimate transition */
         if( checkEstimateCriterion(scip, eventhdlrdata) )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "reached best-estimate transition: nodes: %lld, estimate: %d bound: %9.5g time: %.2f\n",
                  SCIPgetNNodes(scip), SCIPgetNNodesBelowIncumbent(scip), SCIPgetPrimalbound(scip), SCIPgetSolvingTime(scip));
            return TRUE;
         }
         return FALSE;
         break;
      case 'l':

         /* check logarithmic transition */
         if( checkLogCriterion(scip, eventhdlrdata) )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "reached a logarithmic phase transition: %.2f\n", SCIPgetSolvingTime(scip));
            return TRUE;
         }
         break;
      default:
         return FALSE;
      break;
   }

   return FALSE;
}

/* determine the solving phase: feasibility phase if no solution was found yet, otherwise improvement phase or proof phase
 * depending on whether selected transition criterion was already reached and fallback is active or not
 */
static
void determineSolvingPhase(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   /* without solution, we are in the feasibility phase */
   if( SCIPgetNSols(scip) == 0 )
      eventhdlrdata->solvingphase = SOLVINGPHASE_FEASIBILITY;
   else if( eventhdlrdata->solvingphase != SOLVINGPHASE_PROOF || eventhdlrdata->fallback )
      eventhdlrdata->solvingphase = SOLVINGPHASE_IMPROVEMENT;

   if( eventhdlrdata->solvingphase == SOLVINGPHASE_IMPROVEMENT && transitionPhase3(scip, eventhdlrdata) )
      eventhdlrdata->solvingphase = SOLVINGPHASE_PROOF;
}

/**< adjust reliability pseudo cost weights depending on previously observed ratio between infeasible and pruned leaf nodes */
static
SCIP_RETCODE adjustRelpscostWeights(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   SCIP_Longint objleaves;
   SCIP_Real quotient;
   SCIP_Real newcutoffweight;
   SCIP_Real newconflictweight;
   SCIP_Real cutoffweight;
   SCIP_Real conflictweight;
   SCIP_Longint cutoffleaves;

   objleaves = SCIPgetNObjLeaves(scip);
   cutoffleaves = SCIPgetNInfeasLeaves(scip);
   objleaves = MAX(objleaves, 1);
   cutoffleaves = MAX(cutoffleaves, 1);

   /* ratio between infeasible and pruned leaf nodes (that were actually processed) */
   quotient = cutoffleaves / (SCIP_Real)objleaves;

   newcutoffweight = quotient;
   newconflictweight = quotient;

   SCIP_CALL( SCIPgetRealParam(scip, "branching/relpscost/conflictweight", &conflictweight) );
   SCIP_CALL( SCIPgetRealParam(scip, "branching/relpscost/cutoffweight", &cutoffweight) );

   /* weight the quotient by the respective weights in use before the adjustment */
   if( eventhdlrdata->useweightedquotients )
   {
      newcutoffweight *= cutoffweight;
      newconflictweight *= conflictweight;
   }

   /* set new parameter values */
   SCIP_CALL( SCIPsetRealParam(scip, "branching/relpscost/conflictweight", newconflictweight) );
   SCIP_CALL( SCIPsetRealParam(scip, "branching/relpscost/cutoffweight", newcutoffweight) );

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL,
          "  Adjusting relpscost weights, (quot = %.3g): cutoffweight %.4g --> %.4g, confweight: %.4g --> %.4g \n", quotient,
          cutoffweight, newcutoffweight, conflictweight, newconflictweight);

   return SCIP_OKAY;
}

/* apply the user-specified phase-based settings: A phase transition invokes the read of phase-specific settings from a file */
static
SCIP_RETCODE applySolvingPhase(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLRDATA*   eventhdlrdata       /**< event handler data */
   )
{
   FILE* file;
   SOLVINGPHASE oldsolvingphase;
   char* paramfilename;
   SCIP_Bool restart;

   /* return immediately if we are in the proof phase */
   if( eventhdlrdata->solvingphase == SOLVINGPHASE_PROOF && !eventhdlrdata->fallback )
      return SCIP_OKAY;

   /* save current solving phase */
   oldsolvingphase = eventhdlrdata->solvingphase;

   /* determine current solving phase */
   determineSolvingPhase(scip, eventhdlrdata);


   /* nothing has changed */
   if( oldsolvingphase == eventhdlrdata->solvingphase )
      return SCIP_OKAY;


   /* check if the solving process should be interrupted when the current solution is optimal */
   if( eventhdlrdata->solvingphase == SOLVINGPHASE_PROOF && eventhdlrdata->transitionmethod == 'o' &&
         eventhdlrdata->interruptoptimal )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Solution is optimal. Calling user interruption.\n");

      /* we call interrupt solve but do not return yet because user-specified settings for the proof phase are applied first */
      SCIP_CALL( SCIPinterruptSolve(scip) );
   }

   /* check if a restart should be performed after phase transition */
   if( eventhdlrdata->solvingphase == SOLVINGPHASE_IMPROVEMENT && eventhdlrdata->userestart1to2 )
      restart = TRUE;
   else if( eventhdlrdata->solvingphase == SOLVINGPHASE_PROOF && eventhdlrdata->userestart2to3 )
      restart = TRUE;
   else
      restart = FALSE;

   /* inform SCIP that a restart should be performed */
   if( restart )
   {
      SCIP_CALL( SCIPrestartSolve(scip) );
   }

   /* choose the settings file for the new solving phase */
   switch (eventhdlrdata->solvingphase)
   {
   case SOLVINGPHASE_FEASIBILITY:
      paramfilename = eventhdlrdata->setfilefeasibility;
      break;
   case SOLVINGPHASE_IMPROVEMENT:
      paramfilename = eventhdlrdata->setfileimprove;
      break;
   case SOLVINGPHASE_PROOF:
      paramfilename = eventhdlrdata->setfileproof;
      break;
   default:
      SCIPdebugMessage("Unknown solving phase: %d -> ABORT!\n ", eventhdlrdata->solvingphase);
      SCIPABORT();
      break;
   }
   assert(paramfilename != NULL);

   file = fopen(paramfilename, "r");

   /* test if file could be found and print a warning if not */
   if( file == NULL )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL,"Changed solving phase to %d.\n", eventhdlrdata->solvingphase);
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL,"Parameter file <%s> not found--keeping settings as before.\n", paramfilename);
   }
   else
   {
      char transitionmethod;
      SCIP_Bool interruptoptimal;

      /* we can close the file */
      fclose(file);

      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL,"Changed solving phase to phase %d \n", eventhdlrdata->solvingphase);

      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Reading parameters from file <%s>\n", paramfilename);

      /* save some more event handler specific parameters so that they don't get overwritten */
      interruptoptimal = eventhdlrdata->interruptoptimal;
      transitionmethod = eventhdlrdata->transitionmethod;

      SCIP_CALL( SCIPreadParams(scip, paramfilename) );

      eventhdlrdata->enabled = TRUE;
      eventhdlrdata->transitionmethod = transitionmethod;
      eventhdlrdata->interruptoptimal = interruptoptimal;
   }


   /* adjust hybrid reliability pseudo cost weights */
   if( eventhdlrdata->solvingphase == SOLVINGPHASE_PROOF && eventhdlrdata->adjustrelpsweights && SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
   {
      SCIP_CALL(adjustRelpscostWeights(scip, eventhdlrdata) );
   }

   return SCIP_OKAY;
}

/*
 * Callback methods of event handler
 */

/** copy method for event handler (called when SCIP copies plugins) */
static
SCIP_DECL_EVENTCOPY(eventCopySolvingphase)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* call inclusion method of event handler */
   SCIP_CALL( SCIPincludeEventHdlrSolvingphase(scip) );

   return SCIP_OKAY;
}

/** destructor of event handler to free user data (called when SCIP is exiting) */
static
SCIP_DECL_EVENTFREE(eventFreeSolvingphase)
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   regressionFree(scip, &eventhdlrdata->regression);

   SCIP_CALL( freeLeafInfo(scip, &eventhdlrdata->leafinfo) );

   SCIPfreeMemory(scip, &eventhdlrdata);
   eventhdlrdata = NULL;

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolSolvingphase)
{  /*lint --e{715}*/

   SCIP_EVENTHDLRDATA* eventhdlrdata;
   assert(scip != NULL);
   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   eventhdlrdata->depthinfos = NULL;
   eventhdlrdata->maxdepth = 0;
   eventhdlrdata->nnodesbelowincumbent = 0;
   eventhdlrdata->nrank1nodes = 0;

   resetLeafInfo(eventhdlrdata->leafinfo);

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolSolvingphase)
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   /* free all data storage acquired during this branch-and-bound run */
   if( eventhdlrdata->maxdepth > 0 )
   {
      int c;

      /* free depth information */
      for( c = 0; c < eventhdlrdata->maxdepth; ++c )
      {
         SCIP_CALL( freeDepthinfo(scip, &(eventhdlrdata->depthinfos[c])) );
      }

      /* free depth information array */
      SCIPfreeMemoryArray(scip, &eventhdlrdata->depthinfos);
      eventhdlrdata->maxdepth = 0;
   }

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */

static
SCIP_DECL_EVENTINIT(eventInitSolvingphase)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   /* initialize the solving phase */
   eventhdlrdata->solvingphase = SOLVINGPHASE_UNINITIALIZED;

   /* none of the transitions is reached yet */
   eventhdlrdata->optimalreached = FALSE;
   eventhdlrdata->logreached = FALSE;
   eventhdlrdata->rank1reached = FALSE;
   eventhdlrdata->estimatereached = FALSE;

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Optimal value for problem: %16.9g\n", eventhdlrdata->optimalvalue);

   /* apply solving phase for the first time after problem was transformed to apply settings for the feasibility phase */
   if( eventhdlrdata->enabled )
   {
      SCIP_CALL( applySolvingPhase(scip, eventhdlrdata) );
   }

   /* only start catching events if event handler is enabled or in test mode */
   if( eventhdlrdata->enabled || eventhdlrdata->testmode )
   {
      SCIP_CALL( SCIPcatchEvent(scip, EVENTHDLR_EVENT, eventhdlr, NULL, &eventhdlrdata->eventfilterpos) );
   }

   /* reset solving regression */
   regressionReset(eventhdlrdata->regression);

   return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecSolvingphase)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_EVENTTYPE eventtype;

   assert(scip != NULL);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   eventtype = SCIPeventGetType(event);

   assert(eventtype & (EVENTHDLR_EVENT));

   assert((eventtype & SCIP_EVENTTYPE_BESTSOLFOUND) || eventhdlrdata->nnodesbelowincumbent <= SCIPgetNNodesLeft(scip));

   if( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
   {
      if( eventtype & SCIP_EVENTTYPE_BESTSOLFOUND )
      {
         SCIP_CALL( storeRank1Nodes(scip, eventhdlrdata) );
      }
      else if( eventtype & SCIP_EVENTTYPE_NODEBRANCHED )
      {
         SCIP_NODE** children;
         int nchildren;
         SCIP_CALL( SCIPgetChildren(scip, &children, &nchildren) );
         SCIP_CALL ( nodesUpdateRank1Nodes(scip, eventhdlrdata, children, nchildren) );
      }
      else if( eventtype & SCIP_EVENTTYPE_NODESOLVED )
      {
         updateLeafInfo(scip, eventhdlrdata->leafinfo, eventtype);
      }
      assert(eventhdlrdata->nnodesbelowincumbent <= SCIPgetNNodesLeft(scip));
      assert(eventhdlrdata->nnodesbelowincumbent == checkLeavesBelowIncumbent(scip));
   }



   if( SCIPeventGetType(event) & SCIP_EVENTTYPE_BESTSOLFOUND )
   {
      updateRegression(scip, eventhdlrdata->regression, getX(scip, eventhdlrdata), SCIPgetPrimalbound(scip));
   }

   /* if the phase-based solver is enabled, we check if a phase transition occurred and alter the settings accordingly */
   if( eventhdlrdata->enabled )
   {
      SCIP_CALL( applySolvingPhase(scip, eventhdlrdata) );
   }


   /* in test mode, we check every transition criterion */
   if( eventhdlrdata->testmode )
   {
      if( !eventhdlrdata->logreached && checkLogCriterion(scip, eventhdlrdata) )
      {
         eventhdlrdata->logreached = TRUE;
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "  Log criterion reached after %lld nodes, %.2f sec.\n", SCIPgetNNodes(scip), SCIPgetSolvingTime(scip));
      }
      if( ! eventhdlrdata->rank1reached && checkRankOneTransition(scip, eventhdlrdata) )
      {
         eventhdlrdata->rank1reached = TRUE;
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "  Rank 1 criterion reached after %lld nodes, %.2f sec.\n", SCIPgetNNodes(scip), SCIPgetSolvingTime(scip));
      }

      if( ! eventhdlrdata->estimatereached && checkEstimateCriterion(scip, eventhdlrdata) )
      {
         eventhdlrdata->estimatereached = TRUE;
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "  Estimate criterion reached after %lld nodes, %.2f sec.\n", SCIPgetNNodes(scip), SCIPgetSolvingTime(scip));
      }

      if( ! eventhdlrdata->optimalreached && checkOptimalSolution(scip, eventhdlrdata) )
      {
         eventhdlrdata->optimalreached = TRUE;
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "  Optimum reached after %lld nodes, %.2f sec.\n", SCIPgetNNodes(scip), SCIPgetSolvingTime(scip));
      }

   }

   return SCIP_OKAY;
}

/*
 * displays that come with this event handler
 */

/* defines for the rank 1 node display */
#define DISP_NAME_NRANK1NODES         "nrank1nodes"
#define DISP_DESC_NRANK1NODES         "current number of rank1 nodes left"
#define DISP_HEAD_NRANK1NODES         "rank1"
#define DISP_WIDT_NRANK1NODES         7
#define DISP_PRIO_NRANK1NODES         40000
#define DISP_POSI_NRANK1NODES         500
#define DISP_STRI_NRANK1NODES         TRUE

/** output method of display column to output file stream 'file' */
static
SCIP_DECL_DISPOUTPUT(dispOutputNRank1Nodes)
{
   assert(disp != NULL);
   assert(strcmp(SCIPdispGetName(disp), DISP_NAME_NRANK1NODES) == 0);
   assert(scip != NULL);

   /* ouput number of rank 1 nodes */
   SCIPdispInt(SCIPgetMessagehdlr(scip), file, SCIPgetNRank1Nodes(scip), DISP_WIDT_NRANK1NODES);

   return SCIP_OKAY;
}

/* display for the number of leaves passing the objective limit */
#define DISP_NAME_NOBJLEAVES         "nobjleaves"
#define DISP_DESC_NOBJLEAVES         "current number of encountered objective limit leaves"
#define DISP_HEAD_NOBJLEAVES         "leavO"
#define DISP_WIDT_NOBJLEAVES         6
#define DISP_PRIO_NOBJLEAVES         40000
#define DISP_POSI_NOBJLEAVES         600
#define DISP_STRI_NOBJLEAVES         TRUE

/** output method of display column to output file stream 'file' */
static
SCIP_DECL_DISPOUTPUT(dispOutputNObjLeaves)
{
   assert(disp != NULL);
   assert(strcmp(SCIPdispGetName(disp), DISP_NAME_NOBJLEAVES) == 0);
   assert(scip != NULL);

   /* ouput number of leaves that hit the objective */
   SCIPdispLongint(SCIPgetMessagehdlr(scip), file, SCIPgetNObjLeaves(scip), DISP_WIDT_NOBJLEAVES);

   return SCIP_OKAY;
}

/* display for number of encountered infeasible leaf nodes */
#define DISP_NAME_NINFEASLEAVES         "ninfeasleaves"
#define DISP_DESC_NINFEASLEAVES         "number of encountered infeasible leaves"
#define DISP_HEAD_NINFEASLEAVES         "leavI"
#define DISP_WIDT_NINFEASLEAVES         6
#define DISP_PRIO_NINFEASLEAVES         40000
#define DISP_POSI_NINFEASLEAVES         800
#define DISP_STRI_NINFEASLEAVES         TRUE

/** output method of display column to output file stream 'file' */
static
SCIP_DECL_DISPOUTPUT(dispOutputNInfeasLeaves)
{
   assert(disp != NULL);
   assert(strcmp(SCIPdispGetName(disp), DISP_NAME_NINFEASLEAVES) == 0);
   assert(scip != NULL);

   /* output number of encountered infeasible leaf nodes */
   SCIPdispLongint(SCIPgetMessagehdlr(scip), file, SCIPgetNInfeasLeaves(scip), DISP_WIDT_NINFEASLEAVES);

   return SCIP_OKAY;
}

/* display for the number of nodes below the current incumbent */
#define DISP_NAME_NNODESBELOWINC         "nnodesbelowinc"
#define DISP_DESC_NNODESBELOWINC         "current number of nodes with an estimate better than the current incumbent"
#define DISP_HEAD_NNODESBELOWINC         "nbInc"
#define DISP_WIDT_NNODESBELOWINC         6
#define DISP_PRIO_NNODESBELOWINC         40000
#define DISP_POSI_NNODESBELOWINC         550
#define DISP_STRI_NNODESBELOWINC         TRUE

/** output method of display column to output file stream 'file' */
static
SCIP_DECL_DISPOUTPUT(dispOutputNnodesbelowinc)
{
   assert(disp != NULL);
   assert(strcmp(SCIPdispGetName(disp), DISP_NAME_NNODESBELOWINC) == 0);
   assert(scip != NULL);

   /* display the number of nodes with an estimate below the the current incumbent */
   SCIPdispLongint(SCIPgetMessagehdlr(scip), file, SCIPgetNNodesBelowIncumbent(scip), DISP_WIDT_NNODESBELOWINC);

   return SCIP_OKAY;
}

/** creates event handler for Solvingphase event */
SCIP_RETCODE SCIPincludeEventHdlrSolvingphase(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_EVENTHDLR* eventhdlr;

   /* create Solvingphase event handler data */
   eventhdlrdata = NULL;
   SCIP_CALL( SCIPallocMemory(scip, &eventhdlrdata) );
   assert(eventhdlrdata != NULL);

   eventhdlrdata->setfilefeasibility = NULL;
   eventhdlrdata->setfileimprove = NULL;
   eventhdlrdata->setfileproof = NULL;


   eventhdlrdata->depthinfos = NULL;
   eventhdlrdata->maxdepth = 0;
   eventhdlrdata->leafinfo = NULL;
   eventhdlrdata->eventfilterpos = -1;

   /* create leaf information */
   SCIP_CALL( createLeafInfo(scip, &eventhdlrdata->leafinfo) );

   /* create a regression */
   eventhdlrdata->regression = NULL;
   SCIP_CALL( regressionCreate(scip, &eventhdlrdata->regression) );

   eventhdlr = NULL;

   /* include event handler into SCIP */
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC,
         eventExecSolvingphase, eventhdlrdata) );
   assert(eventhdlr != NULL);

   /* include the new displays into scip */
   SCIP_CALL( SCIPincludeDisp(scip, DISP_NAME_NRANK1NODES, DISP_DESC_NRANK1NODES, DISP_HEAD_NRANK1NODES, SCIP_DISPSTATUS_ON,
         NULL, NULL, NULL, NULL, NULL, NULL, dispOutputNRank1Nodes, NULL, DISP_WIDT_NRANK1NODES, DISP_PRIO_NRANK1NODES, DISP_POSI_NRANK1NODES,
         DISP_STRI_NRANK1NODES) );
   SCIP_CALL( SCIPincludeDisp(scip, DISP_NAME_NOBJLEAVES, DISP_DESC_NOBJLEAVES, DISP_HEAD_NOBJLEAVES, SCIP_DISPSTATUS_ON,
         NULL, NULL, NULL, NULL, NULL, NULL, dispOutputNObjLeaves, NULL, DISP_WIDT_NOBJLEAVES, DISP_PRIO_NOBJLEAVES, DISP_POSI_NOBJLEAVES,
         DISP_STRI_NOBJLEAVES) );
   SCIP_CALL( SCIPincludeDisp(scip, DISP_NAME_NINFEASLEAVES, DISP_DESC_NINFEASLEAVES, DISP_HEAD_NINFEASLEAVES, SCIP_DISPSTATUS_ON,
         NULL, NULL, NULL, NULL, NULL, NULL, dispOutputNInfeasLeaves, NULL, DISP_WIDT_NINFEASLEAVES, DISP_PRIO_NINFEASLEAVES, DISP_POSI_NINFEASLEAVES,
         DISP_STRI_NINFEASLEAVES) );
   SCIP_CALL( SCIPincludeDisp(scip, DISP_NAME_NNODESBELOWINC, DISP_DESC_NNODESBELOWINC, DISP_HEAD_NNODESBELOWINC, SCIP_DISPSTATUS_ON,
         NULL, NULL, NULL, NULL, NULL, NULL, dispOutputNnodesbelowinc, NULL, DISP_WIDT_NNODESBELOWINC, DISP_PRIO_NNODESBELOWINC, DISP_POSI_NNODESBELOWINC,
         DISP_STRI_NNODESBELOWINC) );

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetEventhdlrCopy(scip, eventhdlr, eventCopySolvingphase) );
   SCIP_CALL( SCIPsetEventhdlrFree(scip, eventhdlr, eventFreeSolvingphase) );
   SCIP_CALL( SCIPsetEventhdlrInit(scip, eventhdlr, eventInitSolvingphase) );
   SCIP_CALL( SCIPsetEventhdlrInitsol(scip, eventhdlr, eventInitsolSolvingphase) );
   SCIP_CALL( SCIPsetEventhdlrExitsol(scip, eventhdlr, eventExitsolSolvingphase) );

   /* add Solvingphase event handler parameters */
   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/enabled", "should the event handler be executed?",
         &eventhdlrdata->enabled, FALSE, DEFAULT_ENABLED, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/testmode", "should the event handler test for phase transition?",
         &eventhdlrdata->testmode, FALSE, DEFAULT_TESTMODE, NULL, NULL) );

   SCIP_CALL( SCIPaddStringParam(scip, "eventhdlr/"EVENTHDLR_NAME"/nosolsetname", "bla",
         &eventhdlrdata->setfilefeasibility, FALSE, DEFAULT_SETNAME, NULL, NULL) );

   SCIP_CALL( SCIPaddStringParam(scip, "eventhdlr/"EVENTHDLR_NAME"/suboptsetname", "settings file for suboptimal solving phase",
         &eventhdlrdata->setfileimprove, FALSE, DEFAULT_SETNAME, NULL, NULL) );

   SCIP_CALL( SCIPaddStringParam(scip, "eventhdlr/"EVENTHDLR_NAME"/optsetname", "settings file for optimal solving phase",
         &eventhdlrdata->setfileproof, FALSE, DEFAULT_SETNAME, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "eventhdlr/"EVENTHDLR_NAME"/nodeoffset", "node offset", &eventhdlrdata->nodeoffset,
         FALSE, DEFAULT_NODEOFFSET, 1, SCIP_LONGINT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/fallback", "should the event handler fall back from optimal phase?",
         &eventhdlrdata->fallback, FALSE, DEFAULT_FALLBACK, NULL, NULL) );
   SCIP_CALL( SCIPaddCharParam(scip ,"eventhdlr/"EVENTHDLR_NAME"/transitionmethod", "transition method 'e','l','o','r'",
         &eventhdlrdata->transitionmethod, FALSE, DEFAULT_TRANSITIONMETHOD, TRANSITIONMETHODS, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/interruptoptimal", "should the event handler interrupt after optimal solution was found?",
         &eventhdlrdata->interruptoptimal, FALSE, DEFAULT_INTERRUPTOPTIMAL, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/adjustrelpsweights", "should the branching score weights for cutoffs and "
         "conflicts be adjusted after optimal solution was found?", &eventhdlrdata->adjustrelpsweights, FALSE,
         DEFAULT_ADJUSTRELPSWEIGHTS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/useweightedquotients", "use weighted quotients?", &eventhdlrdata->useweightedquotients,
         FALSE, DEFAULT_USEWEIGHTEDQUOTIENTS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/userestart1to2",
         "should a restart be applied between the feasibility and improvement phase?",
         &eventhdlrdata->userestart1to2, FALSE, DEFAULT_USERESTART1TO2, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "eventhdlr/"EVENTHDLR_NAME"/userestart2to3",
         "should a restart be applied between the improvement and the proof phase?",
         &eventhdlrdata->userestart2to3, FALSE, DEFAULT_USERESTART2TO3, NULL, NULL) );

   SCIP_CALL(SCIPaddRealParam(scip, "eventhdlr/"EVENTHDLR_NAME"/optsolvalue", "optimal solution value for problem",
         &eventhdlrdata->optimalvalue, FALSE, SCIP_INVALID, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );

   /* add parameter for logarithmic regression */
   SCIP_CALL( SCIPaddCharParam(scip, "eventhdlr/"EVENTHDLR_NAME"/xtype", "x type for log regression - (t)ime, (n)odes, (l)p iterations",
        &eventhdlrdata->logregression_xtype, FALSE, DEFAULT_LOGREGRESSION_XTYPE, LOGREGRESSION_XTYPES, NULL, NULL) );

   return SCIP_OKAY;
}
