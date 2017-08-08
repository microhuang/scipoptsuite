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

/**@file   event_treesizeprediction
 * @brief  eventhdlr for tree-size prediction related events
 * @author Pierre Le Bodic
 */


/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

//#define SCIP_DEBUG 1

#include "scip/event_treesizeprediction.h"
#include "scip/misc.h"
#include "scip/struct_tree.h"
#include "scip/struct_branch.h"

#include "string.h"
#define EVENTHDLR_NAME         "treesizeprediction"
#define EVENTHDLR_DESC         "event handler for tree-size prediction related events"

#define DEFAULT_HASHMAP_SIZE   100000
#define DEFAULT_MAXRATIOITERS  100
#define DEFAULT_ESTIMATION_METHOD 'r'

/*
 * Data structures
 */

/*
 * Indicates for a given node if/how the size of its subtree is computed.
 * UNKNOWN: the node has children, both with UNKNOWN sizes. No tree-size estimate at this node, it is UNKNOWN.
 * ESTIMATED: the node has children, exactly one of them has UNKNOWN size. The tree-size at this node is ESTIMATED.
 * KNOWN: the node is a leaf or both its children have KNOWN size. The tree-size at this node is thus KNOWN.
 */

typedef enum {UNKNOWN, ESTIMATED, KNOWN} SizeStatus;
typedef enum {RATIO, UNIFORM} EstimationMethod;

typedef struct TreeSizeEstimateTree TSEtree;
struct TreeSizeEstimateTree
{
   TSEtree *parent;
   TSEtree *leftchild;
   TSEtree *rightchild;

   SCIP_VAR* branchedvar; /* If any, the variable branched on at this node (supposing branching happened on a single variable) */
   SCIP_Bool prunedinPQ; /* Whether the node has been pruned while in the priority queue, and thus never focused */
   SCIP_Longint number; /* The number (id) of the node, as assigned by SCIP */
   SCIP_Real lowerbound; /* The lower bound at that node. TODO update this if a node gets an update */
   SizeStatus status; /* See enum SizeStatus documentation */
   /*SCIP_Longint treesize;*/ /* The computed tree-size */
};

/**
 * Estimates the tree-size of a tree, using the given upperbound to determine
 * if a node is counted as a leaf (independent of whether it has children).
 * Note that totalzise is not equal to the final total size of the B&B tree, it
 * should be equal to the final size of the B&B tree if we had known the optimal value at the start
 * and pruned nodes according to this upper bound.
 */
static
SizeStatus estimateTreeSize(SCIP* scip, TSEtree *node, SCIP_Real upperbound, SCIP_Longint* totalsize, SCIP_Longint* remainingsize, EstimationMethod method)
{
   SizeStatus parentstatus;
   assert(node != NULL);
   assert(totalsize != NULL);
   assert(remainingsize != NULL);
   /* base cases: determine if the current node is a leaf */
   if( node->prunedinPQ == TRUE )
   {
      assert(node->leftchild == NULL);
      assert(node->rightchild == NULL);
      *totalsize = 1;
      *remainingsize = 0;
      parentstatus = KNOWN;
   }
   else if( SCIPisGE(scip, node->lowerbound, upperbound) )
   {
      *totalsize = 1;
      *remainingsize = 0;
      parentstatus = KNOWN;
   } 
   else if(node->leftchild == NULL) /* The node is not a leaf but still needs to be solved (and possibly branched on) */
   {
      assert(node->rightchild == NULL);
      *totalsize = -1;
      *remainingsize = -1;
      parentstatus = UNKNOWN;
   }
   else /* The node has two children (but perhaps only the left one has been created at the moment) */
   {
      SCIP_Longint lefttotalsize;
      SCIP_Longint leftremainingsize;
      SCIP_Longint righttotalsize;
      SCIP_Longint rightremainingsize;
      SizeStatus leftstatus;
      SizeStatus rightstatus;

      assert(node->leftchild != NULL);

      leftstatus = estimateTreeSize(scip, node->leftchild, upperbound, &lefttotalsize, &leftremainingsize, method);
      if( node->rightchild == NULL )
      {
         rightstatus = UNKNOWN;
         righttotalsize = -1;
         rightremainingsize = -1;
      }
      else
         rightstatus = estimateTreeSize(scip, node->rightchild, upperbound, &righttotalsize, &rightremainingsize, method);

      assert(lefttotalsize > 0 || leftstatus == UNKNOWN);
      assert(righttotalsize > 0 || rightstatus == UNKNOWN);

      if(leftstatus == UNKNOWN && rightstatus == UNKNOWN) /* Neither child has information on tree-size*/
      {
         *totalsize = -1;
         *remainingsize = -1;
         parentstatus = UNKNOWN;
      }
      else if ( leftstatus != UNKNOWN && rightstatus != UNKNOWN  ) /* If both left and right subtrees are known or estimated */
      {
         *totalsize = 1 + lefttotalsize + righttotalsize;
         *remainingsize = leftremainingsize + rightremainingsize;
         if( leftstatus == ESTIMATED || rightstatus == ESTIMATED)
            parentstatus = ESTIMATED;
         else
         {
             assert(leftstatus == KNOWN && rightstatus == KNOWN);
             parentstatus = KNOWN;
         }
      }
      else /* Exactly one subtree is UNKNOWN: we estimate */
      {
         SCIP_Real fractionleft;
         SCIP_Real fractionright;

         /* We check which estimation method to use */
         switch(method)
         {
            case RATIO:
            {
               SCIP_BRANCHRATIO branchratio;
               SCIP_Real LPgains[2];
               SCIP_Bool leftLPbetterthanrightLP;
               int maxiters;

               assert(node->branchedvar != NULL);

               /* TODO for code below: investigate whether for the known node, using pscosts or known bound is better for estimation. */
               if( leftstatus == UNKNOWN || SCIPisInfinity(scip, node->leftchild->lowerbound) == TRUE )
                  LPgains[0] = SCIPgetVarPseudocostCurrentRun(scip, node->branchedvar, SCIP_BRANCHDIR_DOWNWARDS); /* Here and below we assume that left is downward, as in relpscost. */
               else
                  LPgains[0] = node->leftchild->lowerbound - node->lowerbound;
               

               if( rightstatus == UNKNOWN || SCIPisInfinity(scip, node->rightchild->lowerbound) == TRUE )
                  LPgains[1] = SCIPgetVarPseudocostCurrentRun(scip, node->branchedvar, SCIP_BRANCHDIR_UPWARDS);
               else
                  LPgains[1] = node->rightchild->lowerbound - node->lowerbound;

               if(LPgains[0] > LPgains[1])
                  leftLPbetterthanrightLP = 1;
               else
                  leftLPbetterthanrightLP = 0;
               
               /* The first LP gain passed should be the minimum one, and the second the maximum one */
               SCIPgetIntParam(scip, "estimates/maxratioiters", &maxiters);
               SCIPcomputeBranchVarRatio(scip, NULL, LPgains[leftLPbetterthanrightLP], LPgains[1-leftLPbetterthanrightLP], maxiters, &branchratio);

               if( branchratio.valid == TRUE )
               {
                  /* Once the ratio phi has been computed, we compute the fraction of trees as:
                   * phi^{-l} for the left side (where l denotes the leftgain), and
                   * phi^{-r} for the right side.
                   * We use the fact that the sum of the two equals 1. */

                  if( leftLPbetterthanrightLP == 0 )
                  {
                      fractionleft = 1.0/branchratio.upratio;
                      fractionright = 1 - fractionleft;
                      assert(fractionleft >= fractionright);
                  }
                  else
                  {
                      fractionright = 1.0/branchratio.upratio;
                      fractionleft = 1 - fractionright;
                      assert(fractionleft <= fractionright);
                  }
                  break;
               }
               /* If the ratio computed is not valid, we do not break the switch, and we use the UNIFORM case */
            }
            case UNIFORM:
               fractionleft = .5;
               fractionright = .5;
               break;
            default:
               SCIPerrorMessage("Missing case in this switch.\n");
               SCIPABORT();
         }

         assert(SCIPisEQ(scip, 1.0, fractionleft + fractionright));
         assert(fractionleft > 0 && fractionright > 0);

         assert((leftstatus == UNKNOWN) ^ (rightstatus == UNKNOWN));

         if( leftstatus == UNKNOWN )
         {
            /* Below we use .5 to round to the closest integer in the truncation to int */
            leftremainingsize = .5 + fractionleft / fractionright * righttotalsize;
            if( leftremainingsize < 0 )
              leftremainingsize = SCIP_LONGINT_MAX;
            lefttotalsize = leftremainingsize;
            
         }
         else
         {
            /* Below we use .5 to round to the closest integer in the truncation to int */
            rightremainingsize = .5 + fractionright / fractionleft * lefttotalsize;
            if( rightremainingsize < 0 )
               rightremainingsize = SCIP_LONGINT_MAX;
           righttotalsize = rightremainingsize;
         }
         *remainingsize = leftremainingsize + rightremainingsize;
         *totalsize = 1 + lefttotalsize + righttotalsize;
         parentstatus = ESTIMATED;
      }

      /* We check that we do not exceed SCIP_Longint capacity */
      if( *remainingsize < leftremainingsize || *remainingsize < rightremainingsize ||
          *totalsize < lefttotalsize || *totalsize < righttotalsize )
      {
         *remainingsize = SCIP_LONGINT_MAX;
         *totalsize = SCIP_LONGINT_MAX; 
      }
   }

   assert(*remainingsize >= -1);
   assert(*totalsize >= -1);
   assert(*totalsize >= *remainingsize);

   return parentstatus;
}

/**
 * Recursively frees memory in the tree.
 */
static
void freeTreeMemory(SCIP *scip, TSEtree **tree)
{
   assert(tree != NULL);
   assert(*tree != NULL);
   /* postfix traversal */
   if( (*tree)->leftchild != NULL )
      freeTreeMemory(scip, &((*tree)->leftchild));
   if( (*tree)->rightchild != NULL )
      freeTreeMemory(scip, &((*tree)->rightchild));
   SCIPdebugMessage("Freeing memory for node %"SCIP_LONGINT_FORMAT"\n", (*tree)->number);
   SCIPfreeMemory(scip, tree);
   *tree = NULL;
}

/** event handler data */
struct SCIP_EventhdlrData
{
   /* Parameters */
   int hashmapsize;
   int maxratioiters;
   char estimatemethod;

   /* Internal variables */
   unsigned int nodesfound;

   /* Enums */
   EstimationMethod estimationmethod;

   /* Complex Data structures */
   TSEtree *tree; /* The representation of the B&B tree */
   //SCIP_HASHMAP *opennodes; /* The open nodes (that have yet to be solved). The key is the (scip) id/number of the SCIP_Node */
   SCIP_HASHMAP *allnodes;
};

SCIP_Longint SCIPtreeSizeGetEstimateRemaining(SCIP* scip)
{
   SizeStatus status;
   SCIP_Longint totalsize;
   SCIP_Longint remainingsize;

   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   assert(scip != NULL);
   eventhdlr = SCIPfindEventhdlr(scip, EVENTHDLR_NAME);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   if(eventhdlrdata->tree == NULL)
      return -1;

   status = estimateTreeSize(scip, eventhdlrdata->tree, SCIPgetUpperbound(scip), &totalsize, &remainingsize, eventhdlrdata->estimationmethod);
   if( status == UNKNOWN )
      return -1;
   else
   {
      assert(totalsize >= 0);
      assert(remainingsize >= 0);
      return remainingsize;
   }
}


/** solving process initialization method of event handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolTreeSizePrediction)
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   /* SCIPdebugMsg(scip, "initsol method of eventhdlr "EVENTHDLR_NAME"\n"); */
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);


   switch(eventhdlrdata->estimatemethod)
   {
      case 'r':
         eventhdlrdata->estimationmethod = RATIO;
         break;
      case 'u':
         eventhdlrdata->estimationmethod = UNIFORM;
         break;
      default:
         SCIPerrorMessage("Missing case in this switch.\n");
         SCIPABORT();
   }


//   eventhdlrdata->initialized = TRUE;
   eventhdlrdata->nodesfound = 0;
   eventhdlrdata->tree = NULL;
   //SCIP_CALL( SCIPhashmapCreate(&(eventhdlrdata->opennodes), SCIPblkmem(scip), eventhdlrdata->hashmapsize) );
   SCIP_CALL( SCIPhashmapCreate(&(eventhdlrdata->allnodes), SCIPblkmem(scip), eventhdlrdata->hashmapsize) );

   /* We catch node solved events */
   SCIP_CALL( SCIPcatchEvent(scip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, NULL, NULL) );

   /* We catch priority queue nodes being removed by bound */
   SCIP_CALL( SCIPcatchEvent(scip, SCIP_EVENTTYPE_PQNODEINFEASIBLE, eventhdlr, NULL, NULL) );

   /* We catch updates to the primal bound */
   /* SCIP_CALL( SCIPcatchEvent(scip, SCIP_EVENTTYPE_BESTSOLFOUND, eventhdlr, NULL, NULL) ); */

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolTreeSizePrediction)
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   /* SCIPdebugMsg(scip, "exitsol method of eventhdlr "EVENTHDLR_NAME"\n"); */
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   SCIPdebugMessage("Found %u nodes in the B&B tree\n", eventhdlrdata->nodesfound);

   #ifdef SCIP_DEBUG
   {
      SCIP_Longint remainingsize;
      SCIP_Longint totalsize;
      SizeStatus status;
      if( eventhdlrdata->tree != NULL ) /* SCIP may not have created any node */
      {
         status = estimateTreeSize(scip, eventhdlrdata->tree, SCIPgetUpperbound(scip), &totalsize, &remainingsize, eventhdlrdata->estimationmethod);
         assert(status == KNOWN);
         SCIPdebugMessage("Estimated remaining nodes: %" SCIP_LONGINT_FORMAT " nodes in the B&B tree\n", remainingsize);
      }
   }
   #endif

   //SCIPhashmapFree(&(eventhdlrdata->opennodes));
   SCIPhashmapFree(&(eventhdlrdata->allnodes));

   if( eventhdlrdata->tree != NULL )
   {
      freeTreeMemory(scip, &(eventhdlrdata->tree));
   }

   /* We drop node solved events */
   SCIP_CALL( SCIPdropEvent(scip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, NULL, -1) );

   /* We drop priority queue nodes being removed by bound */
   SCIP_CALL( SCIPdropEvent(scip, SCIP_EVENTTYPE_PQNODEINFEASIBLE, eventhdlr, NULL, -1) );

   /* We drop updates to the primal bound */
   /* SCIP_CALL( SCIPdropEvent(scip, SCIP_EVENTTYPE_BESTSOLFOUND, eventhdlr, NULL, -1) ); */

   return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecTreeSizePrediction)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_NODE* scipnode; /* The node found by the event (if any) */
   SCIP_NODE *scipparent; /* The parent of the scipnode we found */
   SCIP_Longint scipnodenumber;
   TSEtree *eventnode;
   TSEtree *parentnode;

 
   /* SCIPdebugMsg(scip, "exec method of eventhdlr "EVENTHDLR_NAME"\n"); */
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);
   //assert(eventhdlrdata->opennodes != NULL);
   assert(eventhdlrdata->allnodes != NULL);

   scipnode = SCIPeventGetNode(event);
   assert(scipnode != NULL);
   scipnodenumber = SCIPnodeGetNumber(scipnode);

   #ifdef SCIP_DEBUG
   {
   char eventstr[50];
   switch(SCIPeventGetType(event)) {
      case SCIP_EVENTTYPE_PQNODEINFEASIBLE:
         strcpy(eventstr, "PQNODEINFEASIBLE");
         break;
      case SCIP_EVENTTYPE_NODEFEASIBLE:
         strcpy(eventstr, "NODEFEASIBLE");
         break;
      case SCIP_EVENTTYPE_NODEINFEASIBLE:
         strcpy(eventstr, "NODEINFEASIBLE");
         break;
      case SCIP_EVENTTYPE_NODEBRANCHED:
         strcpy(eventstr, "NODEBRANCHED");
         break;
      default:
         SCIPerrorMessage("Missing case in this switch.\n");
         SCIPABORT();
   }
   SCIPdebugMessage("Event %s for node %"SCIP_LONGINT_FORMAT"\n", eventstr, scipnodenumber);
   }
   #endif

   /* This node may already be in the set of nodes. if this happens, it means that there was a first event PQNODEINFEASIBLE with this node, and now a NODESOLVED event with the same node. */
   eventnode = SCIPhashmapGetImage(eventhdlrdata->allnodes, (void *)scipnodenumber);
   if( eventnode == NULL )
   {
      eventhdlrdata->nodesfound += 1;

      /* We initialise data for this node */
      SCIPdebugMessage("Allocating memory for node %"SCIP_LONGINT_FORMAT"\n", scipnodenumber);
      SCIP_CALL( SCIPallocMemory(scip, &eventnode) );
      eventnode->lowerbound = SCIPnodeGetLowerbound(scipnode);
      eventnode->leftchild = NULL;
      eventnode->rightchild = NULL;
      eventnode->number = SCIPnodeGetNumber(scipnode);


      SCIP_CALL( SCIPhashmapInsert(eventhdlrdata->allnodes, (void *) (eventnode->number), eventnode) );

      /* We update the parent with this new child */
      scipparent = SCIPnodeGetParent(scipnode);
      if( scipparent == NULL )
      {
         /* Then this should be the root node (maybe the root node of a restart) */
         assert(SCIPgetNNodes(scip) <= 1);
         parentnode = NULL;
         eventnode->parent = parentnode;
         eventhdlrdata->tree = eventnode;
      }
      else
      {
         SCIP_Longint parentnumber = SCIPnodeGetNumber(scipparent);
         parentnode = (TSEtree*) SCIPhashmapGetImage(eventhdlrdata->allnodes, (void *)parentnumber);
         assert(parentnode != NULL);
         eventnode->parent = parentnode;
         if(parentnode->leftchild == NULL)
            parentnode->leftchild = eventnode;
         else
         {
            assert(parentnode->rightchild == NULL);
            parentnode->rightchild = eventnode;
         }
      }
   }
   else
   {
      /* If this is not the first time we see this node, we update the lower bound */
      eventnode->lowerbound = SCIPnodeGetLowerbound(scipnode);
   }

   /* SCIPdebugMessage("Node event for focus node #%" SCIP_LONGINT_FORMAT "\n", eventnode->number); */

   eventnode->prunedinPQ = FALSE;
   switch(SCIPeventGetType(event)) {
      case SCIP_EVENTTYPE_PQNODEINFEASIBLE:
         eventnode->prunedinPQ = TRUE;
         SCIPdebugMessage("Node %" SCIP_LONGINT_FORMAT " with parent %" SCIP_LONGINT_FORMAT " pruned directly from the priority queue\n", eventnode->number, (parentnode == NULL?((SCIP_Longint) 0): parentnode->number));
         /* The node might be in the opennodes queue: in this case we would need to remove it */
         //SCIP_CALL( SCIPhashmapRemove(eventhdlrdata->opennodes, (void *)(eventnode->number)) );
      case SCIP_EVENTTYPE_NODEFEASIBLE:
      case SCIP_EVENTTYPE_NODEINFEASIBLE:
         /* When an (in)feasible node is found, this corresponds to a new sample
          * (in Knuth's algorithm). This may change the tree-size estimate. */
         eventnode->status = KNOWN;
         break;
      case SCIP_EVENTTYPE_NODEBRANCHED:
      {
         int nbranchvars;
         SCIP_Real branchbounds;
         SCIP_BOUNDTYPE boundtypes;
         SCIP_NODE** children;
         int nchildren;

         /* When a node is branched on, we need to add the corresponding nodes
          * to our own data structure */
         eventnode->status = UNKNOWN;

         /* We need to get the variable that this node has been branched on.
          * First we get on of its children. */
         assert(SCIPgetFocusNode(scip) == scipnode);
         assert(SCIPgetNChildren(scip) > 0);
         SCIPgetChildren(scip, &children, &nchildren);
         assert(children != NULL);
         assert(nchildren > 0);

         /* We also collect the variable branched on, if this node has been branched on. */
         SCIPnodeGetParentBranchings(children[0], &(eventnode->branchedvar), &branchbounds, &boundtypes, &nbranchvars, 1);
         assert(nbranchvars <= 1); /* We check that this is a simple branching, i.e. on a single var. Also, sometimes there is no branching (yet).*/
         assert(eventnode->branchedvar != NULL);
         //SCIPdebugMessage("Inserting node %" SCIP_LONGINT_FORMAT " with parent %" SCIP_LONGINT_FORMAT " into opennode hashmap\n", eventnode->number, (parentnode == NULL?((SCIP_Longint) 0): parentnode->number));
         //SCIP_CALL( SCIPhashmapInsert(eventhdlrdata->opennodes, (void *) (eventnode->number), eventnode) );
         break;
      }
      default:
         SCIPerrorMessage("Missing case in this switch.\n");
         SCIPABORT();
   }
   return SCIP_OKAY;
}

/** destructor of event handler to free user data (called when SCIP is exiting) */
static
SCIP_DECL_EVENTFREE(eventFreeTreeSizePrediction)
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   /* SCIPdebugMsg(scip, "eventfree method of eventhdlr "EVENTHDLR_NAME"\n"); */
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(scip != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   SCIPfreeMemory(scip, &eventhdlrdata);
   return SCIP_OKAY;
}

/** creates event handler for tree-size prediction event */
SCIP_RETCODE SCIPincludeEventHdlrTreeSizePrediction(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_EVENTHDLR* eventhdlr;

   /* create tree-size prediction event handler data */
   SCIP_CALL( SCIPallocMemory(scip, &eventhdlrdata) );

   eventhdlr = NULL;

   /* include event handler into SCIP */
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC,
         eventExecTreeSizePrediction, eventhdlrdata) );
   assert(eventhdlr != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetEventhdlrCopy(scip, eventhdlr, NULL) );
   SCIP_CALL( SCIPsetEventhdlrFree(scip, eventhdlr, eventFreeTreeSizePrediction) );
   SCIP_CALL( SCIPsetEventhdlrInit(scip, eventhdlr, NULL) );
   SCIP_CALL( SCIPsetEventhdlrExit(scip, eventhdlr, NULL) );
   SCIP_CALL( SCIPsetEventhdlrInitsol(scip, eventhdlr, eventInitsolTreeSizePrediction) );
   SCIP_CALL( SCIPsetEventhdlrExitsol(scip, eventhdlr, eventExitsolTreeSizePrediction) );
   SCIP_CALL( SCIPsetEventhdlrDelete(scip, eventhdlr, NULL) );

   /* add tree-size prediction event handler parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "estimates/hashmapsize", "Default hashmap size to store the open nodes of the B&B tree", &(eventhdlrdata->hashmapsize), TRUE, DEFAULT_HASHMAP_SIZE, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "estimates/maxratioiters", "Maximum number of iterations to compute the ratio of a variable", &(eventhdlrdata->maxratioiters), TRUE, DEFAULT_MAXRATIOITERS, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddCharParam(scip, "estimates/estimatemethod", "Method to estimate the sizes of unkown subtrees based on their siblings ('r'atio, 'u'niform)", &(eventhdlrdata->estimatemethod), TRUE, DEFAULT_ESTIMATION_METHOD, "ru", NULL, NULL) );

   return SCIP_OKAY;
}


