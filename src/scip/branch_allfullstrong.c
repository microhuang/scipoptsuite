/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2005 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2005 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic License.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: branch_allfullstrong.c,v 1.20 2005/02/18 14:06:29 bzfpfend Exp $"

/**@file   branch_allfullstrong.c
 * @brief  all variables full strong LP branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/branch_allfullstrong.h"


#define BRANCHRULE_NAME          "allfullstrong"
#define BRANCHRULE_DESC          "all variables full strong branching"
#define BRANCHRULE_PRIORITY      -1000
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0


/** branching rule data */
struct BranchruleData
{
   int              lastcand;           /**< last evaluated candidate of last branching rule execution */
};



/** performs the all fullstrong branching */
static
RETCODE branch(
   SCIP*            scip,               /**< SCIP data structure */
   BRANCHRULE*      branchrule,         /**< branching rule */
   Bool             allowaddcons,       /**< should adding constraints be allowed to avoid a branching? */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   BRANCHRULEDATA* branchruledata;
   VAR** pseudocands;
   Real cutoffbound;
   Real lpobjval;
   Real bestdown;
   Real bestup;
   Real bestscore;
   Real provedbound;
   Bool bestdownvalid;
   Bool bestupvalid;
   Bool allcolsinlp;
   Bool exactsolve;
   int npseudocands;
   int npriopseudocands;
   int bestpseudocand;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   /* get current LP objective bound of the local sub problem and global cutoff bound */
   lpobjval = SCIPgetLPObjval(scip);
   cutoffbound = SCIPgetCutoffbound(scip);

   /* check, if we want to solve the problem exactly, meaning that strong branching information is not useful
    * for cutting off sub problems and improving lower bounds of children
    */
   exactsolve = SCIPisExactSolve(scip);

   /* check, if all existing columns are in LP, and thus the strong branching results give lower bounds */
   allcolsinlp = SCIPallColsInLP(scip);

   /* get all non-fixed variables (not only the fractional ones) */
   CHECK_OKAY( SCIPgetPseudoBranchCands(scip, &pseudocands, &npseudocands, &npriopseudocands) );
   assert(npseudocands > 0);
   assert(npriopseudocands > 0);

   /* if only one candidate exists, choose this one without applying strong branching */
   bestpseudocand = 0;
   bestdown = lpobjval;
   bestup = lpobjval;
   bestdownvalid = TRUE;
   bestupvalid = TRUE;
   bestscore = -SCIPinfinity(scip);
   provedbound = lpobjval;
   if( npseudocands > 1 )
   {
      Real solval;
      Real down;
      Real up;
      Real downgain;
      Real upgain;
      Real score;
      Bool integral;
      Bool lperror;
      Bool downvalid;
      Bool upvalid;
      Bool downinf;
      Bool upinf;
      Bool downconflict;
      Bool upconflict;
      int i;
      int c;

      /* search the full strong candidate:
       * cycle through the candidates, starting with the position evaluated in the last run
       */
      for( i = 0, c = branchruledata->lastcand; i < npseudocands; ++i, ++c )
      {
         c = c % npseudocands;
         assert(pseudocands[c] != NULL);

         /* we can only apply strong branching on COLUMN variables that are in the current LP */
         if( !SCIPvarIsInLP(pseudocands[c]) )
            continue;

         solval = SCIPvarGetLPSol(pseudocands[c]);
         integral = SCIPisFeasIntegral(scip, solval);

         debugMessage("applying strong branching on %s variable <%s>[%g,%g] with solution %g\n",
            integral ? "integral" : "fractional", SCIPvarGetName(pseudocands[c]), SCIPvarGetLbLocal(pseudocands[c]), 
            SCIPvarGetUbLocal(pseudocands[c]), solval);

         CHECK_OKAY( SCIPgetVarStrongbranch(scip, pseudocands[c], INT_MAX, 
               &down, &up, &downvalid, &upvalid, &downinf, &upinf, &downconflict, &upconflict, &lperror) );

         /* display node information line in root node */
         if( SCIPgetDepth(scip) == 0 && SCIPgetNStrongbranchs(scip) % 100 == 0 )
         {
            CHECK_OKAY( SCIPprintDisplayLine(scip, NULL, SCIP_VERBLEVEL_HIGH) );
         }

         /* check for an error in strong branching */
         if( lperror )
         {
            SCIPmessage(scip, SCIP_VERBLEVEL_HIGH,
               "(node %lld) error in strong branching call for variable <%s> with solution %g\n", 
               SCIPgetNNodes(scip), SCIPvarGetName(pseudocands[c]), solval);
            break;
         }

         /* evaluate strong branching */
         down = MAX(down, lpobjval);
         up = MAX(up, lpobjval);
         downgain = down - lpobjval;
         upgain = up - lpobjval;
         assert(!allcolsinlp || exactsolve || !downvalid || downinf == SCIPisGE(scip, down, cutoffbound));
         assert(!allcolsinlp || exactsolve || !upvalid || upinf == SCIPisGE(scip, up, cutoffbound));
         assert(downinf || !downconflict);
         assert(upinf || !upconflict);

         /* check if there are infeasible roundings */
         if( downinf || upinf )
         {
            assert(allcolsinlp);
            assert(!exactsolve);
            
            /* if for both infeasibilities, a conflict clause was created, we don't need to fix the variable by hand,
             * but better wait for the next propagation round to fix them as an inference, and potentially produce a
             * cutoff that can be analyzed
             */
            if( allowaddcons && downinf == downconflict && upinf == upconflict )
            {
               *result = SCIP_CONSADDED;
               break; /* terminate initialization loop, because constraint was added */
            }
            else if( downinf && upinf )
            {
               if( integral )
               {
                  Bool infeasible;
                  Bool fixed;
                  
                  /* both bound changes are infeasible: variable can be fixed to its current value */
                  CHECK_OKAY( SCIPfixVar(scip, pseudocands[c], solval, &infeasible, &fixed) );
                  assert(!infeasible);
                  assert(fixed);
                  *result = SCIP_REDUCEDDOM;
                  debugMessage(" -> integral variable <%s> is infeasible in both directions\n",
                     SCIPvarGetName(pseudocands[c]));
                  break; /* terminate initialization loop, because LP was changed */
               }
               else
               {
                  /* both roundings are infeasible: the node is infeasible */
                  *result = SCIP_CUTOFF;
                  debugMessage(" -> fractional variable <%s> is infeasible in both directions\n",
                     SCIPvarGetName(pseudocands[c]));
                  break; /* terminate initialization loop, because node is infeasible */
               }
            }
            else if( downinf )
            {
               Real newlb;

               /* downwards rounding is infeasible -> change lower bound of variable to upward rounding */
               newlb = SCIPfeasCeil(scip, solval);
               if( SCIPvarGetLbLocal(pseudocands[c]) < newlb - 0.5 )
               {
                  CHECK_OKAY( SCIPchgVarLb(scip, pseudocands[c], newlb) );
                  *result = SCIP_REDUCEDDOM;
                  debugMessage(" -> variable <%s> is infeasible in downward branch\n", SCIPvarGetName(pseudocands[c]));
                  break; /* terminate initialization loop, because LP was changed */
               }
            }
            else
            {
               Real newub;

               /* upwards rounding is infeasible -> change upper bound of variable to downward rounding */
               assert(upinf);
               newub = SCIPfeasFloor(scip, solval);
               if( SCIPvarGetUbLocal(pseudocands[c]) > newub + 0.5 )
               {
                  CHECK_OKAY( SCIPchgVarUb(scip, pseudocands[c], newub) );
                  *result = SCIP_REDUCEDDOM;
                  debugMessage(" -> variable <%s> is infeasible in upward branch\n", SCIPvarGetName(pseudocands[c]));
                  break; /* terminate initialization loop, because LP was changed */
               }
            }
         }
         else if( allcolsinlp && !exactsolve && downvalid && upvalid )
         {
            Real minbound;
               
            /* the minimal lower bound of both children is a proved lower bound of the current subtree */
            minbound = MIN(down, up);
            provedbound = MAX(provedbound, minbound);
         }

         /* check for a better score, if we are within the maximum priority candidates */
         if( c < npriopseudocands )
         {
            if( integral )
            {
               Real gains[3];

               gains[0] = downgain;
               gains[1] = 0.0;
               gains[2] = upgain;
               score = SCIPgetBranchScoreMultiple(scip, pseudocands[c], 3, gains);
            }
            else
               score = SCIPgetBranchScore(scip, pseudocands[c], downgain, upgain);

            if( score > bestscore )
            {
               bestpseudocand = c;
               bestdown = down;
               bestup = up;
               bestdownvalid = downvalid;
               bestupvalid = upvalid;
               bestscore = score;
            }
         }
         else
            score = 0.0;

         /* update pseudo cost values */
         if( !downinf )
         {
            CHECK_OKAY( SCIPupdateVarPseudocost(scip, pseudocands[c],
                  solval-SCIPfeasCeil(scip, solval-1.0), downgain, 1.0) );
         }
         if( !upinf )
         {
            CHECK_OKAY( SCIPupdateVarPseudocost(scip, pseudocands[c], 
                  solval-SCIPfeasFloor(scip, solval+1.0), upgain, 1.0) );
         }

         debugMessage(" -> var <%s> (solval=%g, downgain=%g, upgain=%g, score=%g) -- best: <%s> (%g)\n",
            SCIPvarGetName(pseudocands[c]), solval, downgain, upgain, score,
            SCIPvarGetName(pseudocands[bestpseudocand]), bestscore);
      }

      /* remember last evaluated candidate */
      branchruledata->lastcand = c;
   }

   if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM && *result != SCIP_CONSADDED )
   {
      NODE* node;
      VAR* var;
      Real solval;
      Real lb;
      Real ub;
      Real newlb;
      Real newub;
      Real downprio;

      assert(*result == SCIP_DIDNOTRUN);
      assert(0 <= bestpseudocand && bestpseudocand < npseudocands);
      assert(SCIPisLT(scip, provedbound, cutoffbound));

      var = pseudocands[bestpseudocand];
      solval = SCIPvarGetLPSol(var);
      lb = SCIPvarGetLbLocal(var);
      ub = SCIPvarGetUbLocal(var);

      /* choose preferred branching direction */
      switch( SCIPvarGetBranchDirection(var) )
      {
      case SCIP_BRANCHDIR_DOWNWARDS:
         downprio = 1.0;
         break;
      case SCIP_BRANCHDIR_UPWARDS:
         downprio = -1.0;
         break;
      case SCIP_BRANCHDIR_AUTO:
         downprio = SCIPvarGetRootSol(var) - solval;
         break;
      default:
         errorMessage("invalid preferred branching direction <%d> of variable <%s>\n", 
            SCIPvarGetBranchDirection(var), SCIPvarGetName(var));
         return SCIP_INVALIDDATA;
      }

      /* perform the branching */
      debugMessage(" -> %d candidates, selected candidate %d: variable <%s>[%g,%g] (solval=%g, down=%g, up=%g, score=%g)\n",
         npseudocands, bestpseudocand, SCIPvarGetName(var), lb, ub, solval, bestdown, bestup, bestscore);

      /* create child node with x <= ceil(x'-1) */
      newub = SCIPfeasCeil(scip, solval-1.0);
      if( newub >= lb - 0.5 )
      {
         debugMessage(" -> creating child: <%s> <= %g\n", SCIPvarGetName(var), newub);
         CHECK_OKAY( SCIPcreateChild(scip, &node, downprio) );
         CHECK_OKAY( SCIPchgVarUbNode(scip, node, var, newub) );
         if( allcolsinlp && !exactsolve )
         {
            CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, provedbound) );
            if( bestdownvalid )
            {
               CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, bestdown) );
            }
         }
         debugMessage(" -> child's lowerbound: %g\n", SCIPnodeGetLowerbound(node));
      }

      /* if the solution was integral, create child x == x' */
      if( SCIPisFeasIntegral(scip, solval) )
      {
         assert(solval > lb + 0.5 || solval < ub - 0.5); /* otherwise, the variable is already fixed */

         debugMessage(" -> creating child: <%s> == %g\n", SCIPvarGetName(var), solval);
         CHECK_OKAY( SCIPcreateChild(scip, &node, SCIPinfinity(scip)) );
         if( solval > lb + 0.5 )
         {
            CHECK_OKAY( SCIPchgVarLbNode(scip, node, var, solval) );
         }
         if( solval < ub - 0.5 )
         {
            CHECK_OKAY( SCIPchgVarUbNode(scip, node, var, solval) );
         }
         if( allcolsinlp && !exactsolve )
         {
            CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, provedbound) );
         }
         debugMessage(" -> child's lowerbound: %g\n", SCIPnodeGetLowerbound(node));
      }

      /* create child node with x >= floor(x'+1) */
      newlb = SCIPfeasFloor(scip, solval+1.0);
      if( newlb <= ub + 0.5 )
      {
         debugMessage(" -> creating child: <%s> >= %g\n", SCIPvarGetName(var), newlb);
         CHECK_OKAY( SCIPcreateChild(scip, &node, -downprio) );
         CHECK_OKAY( SCIPchgVarLbNode(scip, node, var, newlb) );
         if( allcolsinlp && !exactsolve )
         {
            CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, provedbound) );
            if( bestupvalid )
            {
               CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, bestup) );
            }
         }
         debugMessage(" -> child's lowerbound: %g\n", SCIPnodeGetLowerbound(node));
      }

      *result = SCIP_BRANCHED;
   }

   return SCIP_OKAY;
}




/*
 * Callback methods
 */

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
DECL_BRANCHFREE(branchFreeAllfullstrong)
{  /*lint --e{715}*/
   BRANCHRULEDATA* branchruledata;

   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   return SCIP_OKAY;
}


/** initialization method of branching rule (called after problem was transformed) */
static
DECL_BRANCHINIT(branchInitAllfullstrong)
{
   BRANCHRULEDATA* branchruledata;

   /* init branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   branchruledata->lastcand = 0;

   return SCIP_OKAY;
}


/** deinitialization method of branching rule (called before transformed problem is freed) */
#define branchExitAllfullstrong NULL


/** solving process initialization method of branching rule (called when branch and bound process is about to begin) */
#define branchInitsolAllfullstrong NULL


/** solving process deinitialization method of branching rule (called before branch and bound process data is freed) */
#define branchExitsolAllfullstrong NULL


/** branching execution method for fractional LP solutions */
static
DECL_BRANCHEXECLP(branchExeclpAllfullstrong)
{
   assert(result != NULL);

   debugMessage("Execlp method of allfullstrong branching\n");

   *result = SCIP_DIDNOTRUN;
   
   CHECK_OKAY( branch(scip, branchrule, allowaddcons, result) );

   return SCIP_OKAY;
}


/** branching execution method for not completely fixed pseudo solutions */
static
DECL_BRANCHEXECPS(branchExecpsAllfullstrong)
{
   assert(result != NULL);

   debugMessage("Execps method of allfullstrong branching\n");

   *result = SCIP_DIDNOTRUN;

   if( SCIPhasCurrentNodeLP(scip) )
   {
      CHECK_OKAY( branch(scip, branchrule, allowaddcons, result) );
   }

   return SCIP_OKAY;
}




/*
 * branching specific interface methods
 */

/** creates the all variables full strong LP braching rule and includes it in SCIP */
RETCODE SCIPincludeBranchruleAllfullstrong(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   BRANCHRULEDATA* branchruledata;

   /* create allfullstrong branching rule data */
   CHECK_OKAY( SCIPallocMemory(scip, &branchruledata) );
   branchruledata->lastcand = 0;

   /* include allfullstrong branching rule */
   CHECK_OKAY( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY, 
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST,
         branchFreeAllfullstrong, branchInitAllfullstrong, branchExitAllfullstrong, 
         branchInitsolAllfullstrong, branchExitsolAllfullstrong, 
         branchExeclpAllfullstrong, branchExecpsAllfullstrong,
         branchruledata) );

   return SCIP_OKAY;
}
