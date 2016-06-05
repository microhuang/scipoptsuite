/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#define SCIP_DEBUG
/**@file   branch_lookahead.c
 * @brief  lookahead branching rule
 * @author Christoph Schubert
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/branch_lookahead.h"
#include "scip/branch_fullstrong.h"
#include "scip/var.h"

#define BRANCHRULE_NAME            "lookahead"
#define BRANCHRULE_DESC            "fullstrong branching with depth of 2" /* TODO CS: expand description */
#define BRANCHRULE_PRIORITY        536870911
#define BRANCHRULE_MAXDEPTH        -1
#define BRANCHRULE_MAXBOUNDDIST    1.0


#define DEFAULT_REEVALAGE         0LL        /**< number of intermediate LPs solved to trigger reevaluation of strong branching
                                              *   value for a variable that was already evaluated at the current node */
#define DEFAULT_MAXPROPROUNDS       0        /**< maximum number of propagation rounds to be performed during multaggr
                                              *   branching before solving the LP (-1: no limit, -2: parameter settings) */
#define DEFAULT_PROBINGBOUNDS    TRUE        /**< should valid bounds be identified in a probing-like fashion during
                                              *   lookahead branching (only with propagation)? */

/*
 * Data structures
 */

/* TODO: fill in the necessary branching rule data */

/** branching rule data */
struct SCIP_BranchruleData
{
   SCIP_Longint          reevalage;          /**< number of intermediate LPs solved to trigger reevaluation of strong branching
                                              *   value for a variable that was already evaluated at the current node */
   SCIP_Bool             probingbounds;      /**< should valid bounds be identified in a probing-like fashion during strong
                                              *   branching (only with propagation)? */
   int                   lastcand;           /**< last evaluated candidate of last branching rule execution */
   int                   maxproprounds;      /**< maximum number of propagation rounds to be performed during strong
                                              *   branching before solving the LP (-1: no limit, -2: parameter settings) */
   SCIP_Bool*            skipdown;           /**< should be branching on down child be skipped? */
   SCIP_Bool*            skipup;             /**< should be branching on up child be skipped? */
};


/*
 * Local methods
 */

/* put your local methods here, and declare them static */

/* TODO CS: reduce to the needed checks and "return" (via parameter?) the objective value and the pruning status */
static SCIP_RETCODE executeBranchingOnUpperBound(
   SCIP* scip,
   SCIP_VAR* branchingvar,
   SCIP_Real branchingvarsolvalue,
   SCIP_Real* objval,
   SCIP_Bool* cutoff
   )
{
   SCIP_Bool lperror;
   SCIP_LPSOLSTAT solstat;

   assert( scip != NULL);
   assert( branchingvar != NULL);

   SCIPdebugMessage("Started branching on upper bound.\n");

   SCIP_CALL( SCIPnewProbingNode(scip) );
   SCIP_CALL( SCIPchgVarUbProbing(scip, branchingvar, SCIPfeasFloor(scip, branchingvarsolvalue)) );

   SCIP_CALL( SCIPsolveProbingLP(scip, -1, &lperror, cutoff) );
   solstat = SCIPgetLPSolstat(scip);

   lperror = lperror || (solstat == SCIP_LPSOLSTAT_NOTSOLVED && *cutoff == 0) ||
         (solstat == SCIP_LPSOLSTAT_ITERLIMIT) || (solstat == SCIP_LPSOLSTAT_TIMELIMIT);
   assert(solstat != SCIP_LPSOLSTAT_UNBOUNDEDRAY);

   if( !lperror )
   {
      *objval = SCIPgetLPObjval(scip);
      *cutoff = *cutoff || SCIPisGE(scip, *objval, SCIPgetCutoffbound(scip));
      assert(((solstat != SCIP_LPSOLSTAT_INFEASIBLE) && (solstat != SCIP_LPSOLSTAT_OBJLIMIT)) || *cutoff);
   }

   SCIPdebugMessage("Finished branching on upper bound.\n");

   return SCIP_OKAY;
}

/* TODO CS: reduce to the needed checks and "return" (via parameter?) the objective value and the pruning status */
static SCIP_Bool executeBranchingOnLowerBound(
   SCIP* scip,
   SCIP_VAR* fixedvar,
   SCIP_Real fixedvarsol,
   SCIP_Real* upobjval,
   SCIP_Bool* cutoff
   )
{
   SCIP_Bool lperror;
   SCIP_LPSOLSTAT solstat;

   assert( scip != NULL);
   assert( fixedvar != NULL);

   SCIPdebugMessage("Started branching on lower bound.\n");

   SCIP_CALL( SCIPnewProbingNode(scip) );
   SCIP_CALL( SCIPchgVarLbProbing(scip, fixedvar, SCIPfeasCeil(scip, fixedvarsol)) );

   SCIP_CALL( SCIPsolveProbingLP(scip, -1, &lperror, cutoff) );
   solstat = SCIPgetLPSolstat(scip);

   lperror = lperror || (solstat == SCIP_LPSOLSTAT_NOTSOLVED && *cutoff == 0) ||
         (solstat == SCIP_LPSOLSTAT_ITERLIMIT) || (solstat == SCIP_LPSOLSTAT_TIMELIMIT);
   assert(solstat != SCIP_LPSOLSTAT_UNBOUNDEDRAY);

   if( !lperror )
   {
      *upobjval = SCIPgetLPObjval(scip);
      *cutoff = *cutoff || SCIPisGE(scip, *upobjval, SCIPgetCutoffbound(scip));
      assert(((solstat != SCIP_LPSOLSTAT_INFEASIBLE) && (solstat != SCIP_LPSOLSTAT_OBJLIMIT)) || *cutoff);
   }

   SCIPdebugMessage("Finished branching on lower bound.\n");

   return SCIP_OKAY;
}

static
SCIP_RETCODE calculateWeight(SCIP* scip, SCIP_Real lowerbounddiff, SCIP_Real upperbounddiff, SCIP_Real* result)
{
   SCIP_Real min;
   SCIP_Real max;
   SCIP_Real minweight = 4;
   SCIP_Real maxweight = 1;

   if( SCIPisFeasGE(scip, lowerbounddiff, upperbounddiff) )
   {
      min = upperbounddiff;
      max = lowerbounddiff;
   }
   else
   {
      min = lowerbounddiff;
      max = upperbounddiff;
   }

   *result = minweight * min + maxweight * max;

   return SCIP_OKAY;
}

static
SCIP_RETCODE selectVarLookaheadBranching(
   SCIP*          scip,    /**< original SCIP data structure */
   SCIP_VAR*      branchingvariable,
   SCIP_RESULT*   result   /**< pointer to store results of branching */){

   SCIP_VAR** fixvars;
   SCIP_Real lpobjval;
   SCIP_Real downobjval;
   SCIP_Bool downcutoff;
   SCIP_Real upobjval;
   SCIP_Bool upcutoff;
   SCIP_Real fixvarssol;
   SCIP_Real highestweight = 0;
   int highestweightindex = -1;
   int nfixvars;
   int i;

   assert(scip != NULL);

   if( SCIPgetDepthLimit(scip) <= (SCIPgetDepth(scip) + 1) )
   {
      SCIPdebugMessage("cannot perform probing in selectVarLookaheadBranching, depth limit reached.\n");
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }
   lpobjval = SCIPgetLPObjval(scip);
   fixvars = SCIPgetFixedVars(scip);
   nfixvars = SCIPgetNFixedVars(scip);

   if( nfixvars != 0)
   {
      SCIP_CALL( SCIPstartProbing(scip) );
      SCIPdebugMessage("PROBING MODE:\n");

      for( i = 0; i < nfixvars; i++ )
      {
         SCIP_VAR** deepfixvars;
         SCIP_Real deepdownobjval;
         SCIP_Bool deepdowncutoff;
         SCIP_Real deepupobjval;
         SCIP_Bool deepupcutoff;
         SCIP_Real sumweightupperbound = 0;
         SCIP_Real sumweightsupperbound = 0;
         SCIP_Real sumweightlowerbound = 0;
         SCIP_Real sumweightslowerbound = 0;
         SCIP_Real highestweightupperbound = 0;
         SCIP_Real highestweightlowerbound = 0;
         SCIP_Real ncutoffs = 0;
         SCIP_Real lambda;
         SCIP_Real totalweight;
         int deepnfixvars;
         int j;

         assert(fixvars[i] != NULL);
         fixvarssol = SCIPvarGetLPSol(fixvars[i]);

         if (SCIPvarGetType(fixvars[i]) == SCIP_VARTYPE_INTEGER && !SCIPisFeasIntegral(scip, fixvarssol))
         {
            /* NOTE CS: Start of the probe branching on x <= floor(x') */
            SCIP_CALL( executeBranchingOnUpperBound(scip, fixvars[i], fixvarssol, &downobjval, &downcutoff) );

            if( !downcutoff )
            {
               deepfixvars = SCIPgetFixedVars(scip);
               deepnfixvars = SCIPgetNFixedVars(scip);

               if( deepnfixvars != 0 )
               {
                  for( j = 0; j < deepnfixvars; j++ )
                  {
                     SCIP_Real deepfixvarssol;
                     SCIP_Real upperbounddiff;
                     SCIP_Real lowerbounddiff;
                     SCIP_Real currentweight;

                     assert( deepfixvars != NULL );
                     deepfixvarssol = SCIPvarGetLPSol(deepfixvars[j]);

                     if (SCIPvarGetType(deepfixvars[j]) == SCIP_VARTYPE_INTEGER && !SCIPisFeasIntegral(scip, deepfixvarssol) )
                     {
                        /* NOTE CS: Start of the probe branching on x <= floor(x') and y <= floor(y') */
                        SCIP_CALL( executeBranchingOnUpperBound(scip, deepfixvars[j], deepfixvarssol, &deepdownobjval, &deepdowncutoff) );

                        /* go back one layer (we are currently in depth 2) */
                        SCIP_CALL( SCIPbacktrackProbing(scip, 1) );

                        /* NOTE CS: Start of the probe branching on x <= floor(x') and y >= ceil(y') */
                        SCIP_CALL( executeBranchingOnUpperBound(scip, deepfixvars[j], deepfixvarssol, &deepupobjval, &deepupcutoff) );

                        /* go back one layer (we are currently in depth 2) */
                        SCIP_CALL( SCIPbacktrackProbing(scip, 1) );

                        if( !deepdowncutoff && !deepupcutoff )
                        {
                           upperbounddiff = lpobjval - deepdownobjval;
                           lowerbounddiff = lpobjval - deepupobjval;

                           assert( SCIPisFeasPositive(scip, upperbounddiff) );
                           assert( SCIPisFeasPositive(scip, lowerbounddiff) );

                           SCIP_CALL( calculateWeight(scip, lowerbounddiff, upperbounddiff, &currentweight) );
                           if( SCIPisFeasGE(scip, currentweight, highestweightupperbound) )
                           {
                              highestweightupperbound = currentweight;
                           }
                           sumweightupperbound = sumweightupperbound + currentweight;
                           sumweightsupperbound = sumweightsupperbound + 1;
                        }
                        if( deepdowncutoff )
                        {
                           ncutoffs = ncutoffs + 1;
                        }
                        if( deepdowncutoff )
                        {
                           ncutoffs = ncutoffs + 1;
                        }
                     }
                  }
               }
            }
            SCIP_CALL( SCIPbacktrackProbing(scip, 0) );

            SCIP_CALL( executeBranchingOnLowerBound(scip, fixvars[i], fixvarssol, &upobjval, &upcutoff) );

            if( !upcutoff )
            {
               deepfixvars = SCIPgetFixedVars(scip);
               deepnfixvars = SCIPgetNFixedVars(scip);

               if( deepnfixvars != 0 )
               {
                  for( j = 0; j < deepnfixvars; j++ )
                  {
                     SCIP_Real deepfixvarssol;
                     SCIP_Real upperbounddiff;
                     SCIP_Real lowerbounddiff;
                     SCIP_Real currentweight;

                     assert( deepfixvars != NULL );
                     deepfixvarssol = SCIPvarGetLPSol(deepfixvars[j]);

                     if (SCIPvarGetType(deepfixvars[j]) == SCIP_VARTYPE_INTEGER && !SCIPisFeasIntegral(scip, deepfixvarssol) )
                     {
                        /* NOTE CS: Start of the probe branching on x >= ceil(x') and y <= floor(y') */
                        SCIP_CALL( executeBranchingOnUpperBound(scip, deepfixvars[j], deepfixvarssol, &deepdownobjval, &deepdowncutoff) );

                        /* go back one layer (we are currently in depth 2) */
                        SCIP_CALL( SCIPbacktrackProbing(scip, 1) );

                        /* NOTE CS: Start of the probe branching on x >= ceil(x') and y >= ceil(y') */
                        SCIP_CALL( executeBranchingOnUpperBound(scip, deepfixvars[j], deepfixvarssol, &deepupobjval, &deepupcutoff) );

                        /* go back one layer (we are currently in depth 2) */
                        SCIP_CALL( SCIPbacktrackProbing(scip, 1) );

                        if( !deepdowncutoff && !deepupcutoff )
                        {
                           upperbounddiff = lpobjval - deepdownobjval;
                           lowerbounddiff = lpobjval - deepupobjval;

                           assert( SCIPisFeasPositive(scip, upperbounddiff) );
                           assert( SCIPisFeasPositive(scip, lowerbounddiff) );

                           SCIP_CALL( calculateWeight(scip, lowerbounddiff, upperbounddiff, &currentweight) );
                           if( SCIPisFeasGE(scip, currentweight, highestweightlowerbound) )
                           {
                              highestweightlowerbound = currentweight;
                           }
                           sumweightlowerbound = sumweightlowerbound + currentweight;
                           sumweightslowerbound = sumweightslowerbound + 1;
                        }
                        if( deepdowncutoff )
                        {
                           ncutoffs = ncutoffs + 1;
                        }
                        if( deepdowncutoff )
                        {
                           ncutoffs = ncutoffs + 1;
                        }
                     }
                  }
               }
            }
            SCIP_CALL( SCIPbacktrackProbing(scip, 0) );

            lambda = (1/sumweightsupperbound)*sumweightupperbound + (1/sumweightslowerbound)*sumweightlowerbound;
            totalweight = highestweightlowerbound + highestweightupperbound + lambda*ncutoffs;
            if( SCIPisFeasGT(scip, totalweight, highestweight) )
            {
               highestweight = totalweight;
               highestweightindex = i;
            }
         }
      }

      SCIP_CALL( SCIPendProbing(scip) );

      if( highestweightindex != -1 )
      {
         *branchingvariable = *fixvars[highestweightindex];
      }
   }

   return SCIP_OKAY;
}

/*
 * Callback methods of branching rule
 */


/** copy method for branchrule plugins (called when SCIP copies plugins) */
static
SCIP_DECL_BRANCHCOPY(branchCopyLookahead)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);

   SCIP_CALL( SCIPincludeBranchruleLookahead(scip) );

   return SCIP_OKAY;
}

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BRANCHFREE(branchFreeLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   SCIPfreeMemoryArrayNull(scip, &branchruledata->skipdown);
   SCIPfreeMemoryArrayNull(scip, &branchruledata->skipup);

   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   return SCIP_OKAY;
}


/** initialization method of branching rule (called after problem was transformed) */
static
SCIP_DECL_BRANCHINIT(branchInitLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   branchruledata->lastcand = 0;

   return SCIP_OKAY;
}


/** deinitialization method of branching rule (called before transformed problem is freed) */
static
SCIP_DECL_BRANCHEXIT(branchExitLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIPstatistic(int j = 0);

   /* initialize branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);
   assert((branchruledata->skipdown != NULL) == (branchruledata->skipup != NULL));

   return SCIP_OKAY;
}


/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpLookahead)
{  /*lint --e{715}*/

   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_VAR** tmplpcands;
   SCIP_VAR** lpcands;
   SCIP_Real* tmplpcandssol;
   SCIP_Real* tmplpcandsfrac;
   SCIP_Real* lpcandsfrac;
   SCIP_Real* lpcandssol;
   SCIP_Real bestup;
   SCIP_Real bestdown;
   SCIP_Real bestscore;
   SCIP_Real provedbound;
   SCIP_Bool bestdownvalid;
   SCIP_Bool bestupvalid;
   SCIP_Longint oldreevalage;
   int nlpcands;
   int npriolpcands;
   int bestcandpos;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   SCIPdebugMessage("Execlp method of lookahead branching\n");
   *result = SCIP_DIDNOTRUN;

   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   SCIP_CALL( SCIPgetLongintParam(scip, "branching/fullstrong/reevalage", &oldreevalage) );
   SCIP_CALL( SCIPsetLongintParam(scip, "branching/fullstrong/reevalage", branchruledata->reevalage) );

   SCIP_CALL( SCIPgetLPBranchCands(scip, &tmplpcands, &tmplpcandssol, &tmplpcandsfrac, &nlpcands, &npriolpcands, NULL) );
   assert(nlpcands > 0);
   assert(npriolpcands > 0);

   /* copy LP branching candidates and solution values, because they will be updated w.r.t. the strong branching LP
    * solution
    */
   SCIP_CALL( SCIPduplicateBufferArray(scip, &lpcands, tmplpcands, nlpcands) );
   SCIP_CALL( SCIPduplicateBufferArray(scip, &lpcandssol, tmplpcandssol, nlpcands) );
   SCIP_CALL( SCIPduplicateBufferArray(scip, &lpcandsfrac, tmplpcandsfrac, nlpcands) );

   if( branchruledata->skipdown == NULL )
   {
      int nvars = SCIPgetNVars(scip);

      assert(branchruledata->skipup == NULL);

      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->skipdown, nvars) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->skipup, nvars) );
      BMSclearMemoryArray(branchruledata->skipdown, nvars);
      BMSclearMemoryArray(branchruledata->skipup, nvars);
   }

   /* compute strong branching among the array of fractional variables in order to get the best one */
   SCIP_CALL( SCIPselectVarStrongBranching(scip, lpcands, lpcandssol, lpcandsfrac, branchruledata->skipdown,
         branchruledata->skipup, nlpcands, npriolpcands, nlpcands, &branchruledata->lastcand, allowaddcons,
         branchruledata->maxproprounds, branchruledata->probingbounds, TRUE,
         &bestcandpos, &bestdown, &bestup, &bestscore, &bestdownvalid, &bestupvalid, &provedbound, result) );

   /**/

   if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM && *result != SCIP_CONSADDED )
   {
      SCIP_VAR* branchingvariable = NULL;
      SCIP_NODE* lbnode = NULL;
      SCIP_NODE* ubnode = NULL;

      SCIP_CALL( selectVarLookaheadBranching(scip, branchingvariable, result) );

      if( branchingvariable != NULL )
      {
         SCIP_CALL( SCIPcreateChild(scip, &lbnode, 1, 0) );
         SCIP_CALL( SCIPchgVarLbNode(scip, lbnode, branchingvariable, SCIPvarGetLPSol(branchingvariable)) );

         SCIP_CALL( SCIPcreateChild(scip, &ubnode, 1, 0) );
         SCIP_CALL( SCIPchgVarUbNode(scip, ubnode, branchingvariable, SCIPvarGetLPSol(branchingvariable)) );

         *result = SCIP_BRANCHED;
      }

   }

   SCIP_CALL( SCIPsetLongintParam(scip, "branching/fullstrong/reevalage", oldreevalage) );

   return SCIP_OKAY;
}

/*
 * branching rule specific interface methods
 */

/** creates the lookahead branching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleLookahead(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_BRANCHRULE* branchrule;

   /* create lookahead branching rule data */
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata) );
   branchruledata->lastcand = 0;
   branchruledata->skipup = NULL;
   branchruledata->skipdown = NULL;
   /* TODO: (optional) create branching rule specific data here */

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchruleBasic(scip, &branchrule, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY,
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchruledata) );

   assert(branchrule != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBranchruleCopy(scip, branchrule, branchCopyLookahead) );
   SCIP_CALL( SCIPsetBranchruleFree(scip, branchrule, branchFreeLookahead) );
   SCIP_CALL( SCIPsetBranchruleInit(scip, branchrule, branchInitLookahead) );
   SCIP_CALL( SCIPsetBranchruleExit(scip, branchrule, branchExitLookahead) );
   SCIP_CALL( SCIPsetBranchruleExecLp(scip, branchrule, branchExeclpLookahead) );

   /* add lookahead branching rule parameters */
   SCIP_CALL( SCIPaddLongintParam(scip,
         "branching/lookahead/reevalage",
         "number of intermediate LPs solved to trigger reevaluation of strong branching value for a variable that was already evaluated at the current node",
         &branchruledata->reevalage, TRUE, DEFAULT_REEVALAGE, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/lookahead/maxproprounds",
         "maximum number of propagation rounds to be performed during lookahead branching before solving the LP (-1: no limit, -2: parameter settings)",
         &branchruledata->maxproprounds, TRUE, DEFAULT_MAXPROPROUNDS, -2, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/probingbounds",
         "should valid bounds be identified in a probing-like fashion during lookahead branching (only with propagation)?",
         &branchruledata->probingbounds, TRUE, DEFAULT_PROBINGBOUNDS, NULL, NULL) );

   return SCIP_OKAY;
}
