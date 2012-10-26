/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2012 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
//#define SCIP_DEBUG
/**@file   branch_fullstrong.c
 * @brief  full strong LP branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/branch_fullstrong.h"


#define BRANCHRULE_NAME          "fullstrong"
#define BRANCHRULE_DESC          "full strong branching"
#define BRANCHRULE_PRIORITY      0
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0

#define DEFAULT_REEVALAGE        10LL        /**< number of intermediate LPs solved to trigger reevaluation of strong branching
                                              *   value for a variable that was already evaluated at the current node */
#define DEFAULT_MAXPROPROUNDS       0        /**< maximum number of propagation rounds to be performed during strong branching
                                              *   before solving the LP (-1: no limit, -2: parameter settings) */


/** branching rule data */
struct SCIP_BranchruleData
{
   SCIP_Longint          reevalage;          /**< number of intermediate LPs solved to trigger reevaluation of strong branching
                                              *   value for a variable that was already evaluated at the current node */
   int                   maxproprounds;      /**< maximum number of propagation rounds to be performed during strong branching
                                              *   before solving the LP (-1: no limit, -2: parameter settings) */
   int                   lastcand;           /**< last evaluated candidate of last branching rule execution */
   int nsbs;
   int niters;
   int npropiters;
   int nnoncutoffiters;
   int npropnoncutoffiters;
   int neqiters;
   int npropeqiters;
   int nbetteriters;
   int npropbetteriters;
   int ncutoffs;
   int npropcutoffs;
   int nbetterprop;
   int nworseprop;
   SCIP_Real betterpropgap;
   SCIP_Real avgsbgap;
   SCIP_Real avgpropsbgap;
   int nbetterpropcutoffs;
   int ndomchgs;
   int nupdomchgs;
   int ndowndomchgs;
   int nbetterdomchgs;
   int ninvalid;
   int npropinvalid;
   int nlperrors;
   int nproplperrors;
};


/*
 * Callback methods
 */

/** copy method for branchrule plugins (called when SCIP copies plugins) */
static
SCIP_DECL_BRANCHCOPY(branchCopyFullstrong)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);

   /* call inclusion method of branchrule */
   SCIP_CALL( SCIPincludeBranchruleFullstrong(scip) );
   
   return SCIP_OKAY;
}

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BRANCHFREE(branchFreeFullstrong)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   return SCIP_OKAY;
}


/** initialization method of branching rule (called after problem was transformed) */
static
SCIP_DECL_BRANCHINIT(branchInitFullstrong)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   /* initialize branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   branchruledata->lastcand = 0;

   branchruledata->nsbs = 0;
   branchruledata->niters = 0;
   branchruledata->npropiters = 0;
   branchruledata->nnoncutoffiters = 0;
   branchruledata->npropnoncutoffiters = 0;
   branchruledata->neqiters = 0;
   branchruledata->npropeqiters = 0;
   branchruledata->nbetteriters = 0;
   branchruledata->npropbetteriters = 0;
   branchruledata->ncutoffs = 0;
   branchruledata->npropcutoffs = 0;
   branchruledata->nbetterprop = 0;
   branchruledata->nworseprop = 0;
   branchruledata->betterpropgap = 0.0;
   branchruledata->avgsbgap = 0.0;
   branchruledata->avgpropsbgap = 0.0;
   branchruledata->nbetterpropcutoffs = 0;
   branchruledata->ndomchgs = 0;
   branchruledata->nupdomchgs = 0;
   branchruledata->ndowndomchgs = 0;
   branchruledata->nbetterdomchgs = 0;
   branchruledata->ninvalid = 0;
   branchruledata->npropinvalid = 0;
   branchruledata->nlperrors = 0;
   branchruledata->nproplperrors = 0;

   return SCIP_OKAY;
}

static
SCIP_DECL_BRANCHEXIT(branchExitFullstrong)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   /* initialize branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);

   printf("###%-12s nsbs: %d iters: %d %d noncutoffiters: %d %d eqiters: %d %d cutoffs: %d %d avginc: %f %f betterprop: %d %f %d betteriters: %d %d domchgs: %d %d %d %d invalid: %d/%d lperrors: %d/%d\n",
      SCIPgetProbName(scip), branchruledata->nsbs, branchruledata->niters, branchruledata->npropiters,
      branchruledata->nnoncutoffiters, branchruledata->npropnoncutoffiters, branchruledata->neqiters, branchruledata->npropeqiters,
      branchruledata->ncutoffs, branchruledata->npropcutoffs, branchruledata->avgsbgap, branchruledata->avgpropsbgap,
      branchruledata->nbetterprop, branchruledata->betterpropgap, branchruledata->nworseprop, branchruledata->nbetteriters, branchruledata->npropbetteriters,
      branchruledata->ndowndomchgs, branchruledata->nupdomchgs, branchruledata->ndomchgs, branchruledata->nbetterdomchgs,
      branchruledata->ninvalid, branchruledata->npropinvalid, branchruledata->nlperrors, branchruledata->nproplperrors);

   return SCIP_OKAY;
}
/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpFullstrong)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_VAR** lpcands;
   SCIP_Real* lpcandssol;
   SCIP_Real* lpcandsfrac;
#ifndef NDEBUG
   SCIP_Real cutoffbound;
#endif
   SCIP_Real lpobjval;
   SCIP_Real bestdown;
   SCIP_Real bestup;
   SCIP_Real bestscore;
   SCIP_Real provedbound;
   SCIP_Bool bestdownvalid;
   SCIP_Bool bestupvalid;
   SCIP_Bool allcolsinlp;
   SCIP_Bool exactsolve;
   int nlpcands;
   int npriolpcands;
   int bestcand;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   SCIPdebugMessage("Execlp method of fullstrong branching\n");

   *result = SCIP_DIDNOTRUN;

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   /* get current LP objective bound of the local sub problem and global cutoff bound */
   lpobjval = SCIPgetLPObjval(scip);
#ifndef NDEBUG
   cutoffbound = SCIPgetCutoffbound(scip);
#endif

   /* check, if we want to solve the problem exactly, meaning that strong branching information is not useful
    * for cutting off sub problems and improving lower bounds of children
    */
   exactsolve = SCIPisExactSolve(scip);

   /* check, if all existing columns are in LP, and thus the strong branching results give lower bounds */
   allcolsinlp = SCIPallColsInLP(scip);

   /* get branching candidates */
   SCIP_CALL( SCIPgetLPBranchCands(scip, &lpcands, &lpcandssol, &lpcandsfrac, &nlpcands, &npriolpcands) );
   assert(nlpcands > 0);
   assert(npriolpcands > 0);

   /* if only one candidate exists, choose this one without applying strong branching */
   bestcand = 0;
   bestdown = lpobjval;
   bestup = lpobjval;
   bestdownvalid = TRUE;
   bestupvalid = TRUE;
   bestscore = -SCIPinfinity(scip);
   provedbound = lpobjval;
   if( nlpcands > 1 )
   {
      SCIP_VAR** vars;
      SCIP_Real* newlbs;
      SCIP_Real* newubs;
      SCIP_Longint nodenum;
      SCIP_Real down;
      SCIP_Real up;
      SCIP_Real downgain;
      SCIP_Real upgain;
      SCIP_Real score;
      SCIP_Bool lperror;
      SCIP_Bool downvalid;
      SCIP_Bool upvalid;
      SCIP_Bool downinf;
      SCIP_Bool upinf;
      SCIP_Bool downconflict;
      SCIP_Bool upconflict;
      SCIP_Bool propagate;
      int nsbcalls;
      int nvars;
      int i;
      int c;

      vars = SCIPgetVars(scip);
      nvars = SCIPgetNVars(scip);

      SCIP_CALL( SCIPallocBufferArray(scip, &newlbs, nvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &newubs, nvars) );

      /* check whether propagation should be performed */
      propagate = (branchruledata->maxproprounds != 0);

      /* initialize strong branching */
      SCIP_CALL( SCIPstartStrongbranch(scip, propagate) );

      /* get current node number */
      nodenum = SCIPgetNNodes(scip);

      /* search the full strong candidate
       * cycle through the candidates, starting with the position evaluated in the last run
       */
      nsbcalls = 0;
      for( i = 0, c = branchruledata->lastcand; i < nlpcands; ++i, ++c )
      {
         c = c % nlpcands;
         assert(lpcands[c] != NULL);

         /* don't use strong branching on variables that have already been initialized at the current node,
          * and that were evaluated not too long ago
          */
         if( SCIPgetVarStrongbranchNode(scip, lpcands[c]) == nodenum
            && SCIPgetVarStrongbranchLPAge(scip, lpcands[c]) < branchruledata->reevalage )
         {
            SCIP_Real lastlpobjval;

            /* use the score of the strong branching call at the current node */
            SCIP_CALL( SCIPgetVarStrongbranchLast(scip, lpcands[c], &down, &up, NULL, NULL, NULL, &lastlpobjval) );
            downgain = MAX(down - lastlpobjval, 0.0);
            upgain = MAX(up - lastlpobjval, 0.0);
            downvalid = FALSE;
            upvalid = FALSE;
            downinf = FALSE;
            upinf = FALSE;
            downconflict = FALSE;
            upconflict = FALSE;
            lperror = FALSE;
            SCIPdebugMessage("strong branching on variable <%s> already performed (lpage=%"SCIP_LONGINT_FORMAT", down=%g (%+g), up=%g (%+g))\n",
               SCIPvarGetName(lpcands[c]), SCIPgetVarStrongbranchLPAge(scip, lpcands[c]), down, downgain, up, upgain);
         }
         else
         {
            SCIPdebugMessage("applying strong branching on variable <%s> with solution %g\n",
               SCIPvarGetName(lpcands[c]), lpcandssol[c]);

            if( propagate )
            {
               /* apply strong branching */
               SCIP_CALL( SCIPgetVarStrongbranchWithPropagationFrac(scip, lpcands[c], lpcandssol[c], lpobjval, 3,
                     branchruledata->maxproprounds, &down, &up, &downvalid, &upvalid, &downinf, &upinf,
                     &downconflict, &upconflict, &lperror, newlbs, newubs, NULL, NULL, NULL) );

               SCIPdebugMessage("-> down=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d), up=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d)\n",
                  down, down - lpobjval, downvalid, downinf, downconflict, up, up - lpobjval, upvalid, upinf, upconflict);
            }
            else
            {
               SCIP_Real solval;
               SCIP_Real propdown;
               SCIP_Real propup;
               SCIP_Bool propdownvalid;
               SCIP_Bool propdowninf;
               SCIP_Bool propdownconflict;
               SCIP_Bool propupvalid;
               SCIP_Bool propupinf;
               SCIP_Bool propupconflict;
               SCIP_Bool proplperror;

               SCIP_Longint oldsbiters;
               SCIP_Longint olddiveiters;
               SCIP_Longint oldlpiters;
               SCIP_Longint normalsbiters;
               SCIP_Longint propsbiters;
               int nchgbdsdown;
               int nchgbdsup;
               int info;

               oldsbiters = SCIPgetNStrongbranchLPIterations(scip);

               solval = SCIPgetSolVal(scip, NULL, lpcands[c]);

               if( SCIPisFeasIntegral(scip, solval) )
                  continue;

               /* apply strong branching */
               SCIP_CALL( SCIPgetVarStrongbranchFrac(scip, lpcands[c], INT_MAX,
                     &down, &up, &downvalid, &upvalid, &downinf, &upinf, &downconflict, &upconflict, &lperror) );

               SCIPdebugMessage("->           normal strong branching: down=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d), up=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d), %lld LP iterations\n",
                  down, down - lpobjval, downvalid, downinf, downconflict, up, up - lpobjval, upvalid, upinf, upconflict,
                  SCIPgetNStrongbranchLPIterations(scip) - oldsbiters);

               normalsbiters = SCIPgetNStrongbranchLPIterations(scip) - oldsbiters;

               /* end strong branching */
               SCIP_CALL( SCIPendStrongbranch(scip) );

               assert(solval == SCIPgetSolVal(scip, NULL, lpcands[c]));

               /* end strong branching */
               SCIP_CALL( SCIPstartStrongbranch(scip, TRUE) );

               olddiveiters = SCIPgetNDivingLPIterations(scip);
               oldlpiters = SCIPgetNLPIterations(scip);

               /* apply strong branching */
               SCIP_CALL( SCIPgetVarStrongbranchWithPropagationFrac(scip, lpcands[c], solval, lpobjval, INT_MAX,
                     -2, &propdown, &propup, &propdownvalid, &propupvalid, &propdowninf, &propupinf,
                     &propdownconflict, &propupconflict, &proplperror, newlbs, newubs, &nchgbdsdown, &nchgbdsup, &info) );

               propsbiters = SCIPgetNDivingLPIterations(scip) - olddiveiters;
               assert(propsbiters == SCIPgetNLPIterations(scip) - oldlpiters);

               SCIPdebugMessage("-> strong branching with propagation: down=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d), up=%.9g (gain=%.9g, valid=%d, inf=%d, conflict=%d), %lld LP iterations\n",
                  propdown, propdown - lpobjval, propdownvalid, propdowninf, propdownconflict, propup, propup - lpobjval, propupvalid, propupinf, propupconflict,
                  propsbiters);
#if 0
               if( propdowninf )
                  propup = up;
#endif

#if 1
               printf("sb: lpobj=%16.9g pb=%16.9f cutoffbound=%16.9f down=%13.7g/%13.7g up=%13.7g/%13.7g downvalid=%d/%d upvalid=%d/%d downinf=%d/%d upinf=%d/%d iters=%4lld/%4lld domchgs=%d/%d error=%d/%d info=%d\n",
                  lpobjval, SCIPgetUpperbound(scip), SCIPgetCutoffbound(scip), down, propdown, up, propup, downvalid, propdownvalid, upvalid, propupvalid,
                  downinf, propdowninf, upinf, propupinf, normalsbiters, propsbiters, nchgbdsdown, nchgbdsup, lperror, proplperror, info);
#endif

               if( lperror || proplperror )
               {
                  if( !SCIPisStopped(scip) )
                  {
                     if( lperror )
                        branchruledata->nlperrors++;
                     if( proplperror )
                        branchruledata->nproplperrors++;
                  }
               }
               else
               {
                  if( !downvalid || !upvalid || !propdownvalid || !propupvalid )
                  {
                     if( !SCIPisStopped(scip) )
                     {
                        if( !downvalid || !upvalid )
                           branchruledata->ninvalid++;
                        if( !propdownvalid || !propupvalid )
                           branchruledata->npropinvalid++;
                     }
                  }
                  else
                  {
                     branchruledata->nsbs+=2;
                     branchruledata->niters += normalsbiters;
                     branchruledata->npropiters += propsbiters;
                     branchruledata->ndomchgs += (nchgbdsdown + nchgbdsup);
                     branchruledata->ndowndomchgs += nchgbdsdown;
                     branchruledata->nupdomchgs += nchgbdsup;

                     if( downinf )
                        branchruledata->ncutoffs++;
                     if( upinf )
                        branchruledata->ncutoffs++;
                     if( propdowninf)
                        branchruledata->npropcutoffs++;
                     if( propupinf )
                        branchruledata->npropcutoffs++;

                     if( !downinf && !upinf && !propdowninf && !propupinf )
                     {
                        SCIP_Bool better = FALSE;
                        SCIP_Bool worse = FALSE;

                        branchruledata->nnoncutoffiters += normalsbiters;
                        branchruledata->npropnoncutoffiters += propsbiters;

                        if( SCIPgetCutoffbound(scip) < 1e+18 )
                        {
                           branchruledata->avgsbgap += (100.0 * (down - lpobjval)/(SCIPgetCutoffbound(scip) - lpobjval));
                           branchruledata->avgpropsbgap += (100.0 * (propdown - lpobjval)/(SCIPgetCutoffbound(scip) - lpobjval));
                           branchruledata->avgsbgap += (100.0 * (up - lpobjval)/(SCIPgetCutoffbound(scip) - lpobjval));
                           branchruledata->avgpropsbgap += (100.0 * (propup - lpobjval)/(SCIPgetCutoffbound(scip) - lpobjval));
                        }
                        else
                        {
                           branchruledata->avgsbgap += (100.0 * (down - lpobjval)/(lpobjval));
                           branchruledata->avgpropsbgap += (100.0 * (propdown - lpobjval)/(lpobjval));
                           branchruledata->avgsbgap += (100.0 * (up - lpobjval)/(lpobjval));
                           branchruledata->avgpropsbgap += (100.0 * (propup - lpobjval)/(lpobjval));
                        }

                        if( SCIPisFeasGT(scip, propdown, down) )
                        {
                           branchruledata->nbetterprop++;
                           branchruledata->nbetterdomchgs += nchgbdsdown;
                           better = TRUE;
                           if( SCIPgetCutoffbound(scip) < 1e+18 )
                           {
                              branchruledata->betterpropgap += (100.0 * (propdown - down)/(SCIPgetCutoffbound(scip) - down));
                           }
                           else
                           {
                              branchruledata->betterpropgap += (100.0 * (propdown - down)/down);
                           }
                        }
                        else if( SCIPisFeasLT(scip, propdown, down) )
                        {
                           branchruledata->nworseprop++;
                           worse = TRUE;
                        }

                        if( SCIPisFeasGT(scip, propup, up) )
                        {
                           branchruledata->nbetterprop++;
                           branchruledata->nbetterdomchgs += nchgbdsup;
                           better = TRUE;
                           if( SCIPgetCutoffbound(scip) < 1e+18 )
                           {
                              branchruledata->betterpropgap += (100.0 * (propup - up)/(SCIPgetCutoffbound(scip) - up));
                           }
                           else
                           {
                              branchruledata->betterpropgap += (100.0 * (propup - up)/up);
                           }
                        }
                        else if( SCIPisFeasLT(scip, propup, up) )
                        {
                           branchruledata->nworseprop++;
                           worse = TRUE;
                        }

                        if( better )
                        {
                           branchruledata->nbetteriters += normalsbiters;
                           branchruledata->npropbetteriters += propsbiters;
                        }
                        else if( !worse )
                        {
                           branchruledata->neqiters += normalsbiters;
                           branchruledata->npropeqiters += propsbiters;
                        }
                     }
                  }
               }

               //assert(propsbiters > 0 || propdowninf || propupinf); 0 iterations in LP solve is also valid

               // assert(propdowninf || SCIPisFeasEQ(scip, down, propdown));
               // assert(propdowninf || !downinf);
               // assert(propdowninf || propupinf || SCIPisFeasEQ(scip, up, propup));
               // assert(propdowninf || propupinf || !upinf);

               /* end strong branching */
               SCIP_CALL( SCIPendStrongbranch(scip) );

               SCIP_CALL( SCIPstartStrongbranch(scip, FALSE) );
            }
            nsbcalls++;

            /* display node information line */
            if( SCIPgetDepth(scip) == 0 && nsbcalls % 100 == 0 )
            {
               SCIP_CALL( SCIPprintDisplayLine(scip, NULL, SCIP_VERBLEVEL_HIGH) );
            }

            /* check for an error in strong branching */
            if( lperror )
            {
               SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL,
                  "(node %"SCIP_LONGINT_FORMAT") error in strong branching call%s for variable <%s> with solution %g\n",
                  SCIPgetNNodes(scip), propagate ? " with propagation" : "", SCIPvarGetName(lpcands[c]), lpcandssol[c]);
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
               /* if we didn't do propagation, we can only detect infeasibility if the LP is a valid relaxation */
               assert(allcolsinlp || propagate);
               assert(!exactsolve);

               /* if for both infeasibilities, a conflict constraint was created, we don't need to fix the variable by
                * hand, but better wait for the next propagation round to fix them as an inference, and potentially
                * produce a cutoff that can be analyzed
                */
               if( allowaddcons && downinf == downconflict && upinf == upconflict )
               {
                  *result = SCIP_CONSADDED;
                  break; /* terminate initialization loop, because constraint was added */
               }
               else if( downinf && upinf )
               {
                  /* both roundings are infeasible -> node is infeasible */
                  *result = SCIP_CUTOFF;
                  SCIPdebugMessage(" -> variable <%s> is infeasible in both directions\n", SCIPvarGetName(lpcands[c]));
                  break; /* terminate initialization loop, because node is infeasible */
               }
               else if( downinf )
               {
                  SCIP_Bool infeasible;
                  SCIP_Bool tightened;

                  /* downwards rounding is infeasible -> change lower bound of variable to upward rounding */
                  SCIP_CALL( SCIPtightenVarLb(scip, lpcands[c], SCIPfeasCeil(scip, lpcandssol[c]), TRUE, &infeasible, &tightened) );
                  assert(!infeasible);

                  /* if we did propagation, the bound change might already have been added */
                  //assert(tightened || propagate);

                  *result = SCIP_REDUCEDDOM;
                  SCIPdebugMessage(" -> variable <%s> is infeasible in downward branch\n", SCIPvarGetName(lpcands[c]));
                  break; /* terminate initialization loop, because LP was changed */
               }
               else
               {
                  SCIP_Bool infeasible;
                  SCIP_Bool tightened;

                  assert(upinf);

                  /* upwards rounding is infeasible -> change upper bound of variable to downward rounding */
                  SCIP_CALL( SCIPtightenVarUb(scip, lpcands[c], SCIPfeasFloor(scip, lpcandssol[c]), TRUE, &infeasible, &tightened) );
                  assert(!infeasible);

                  /* if we did propagation, the bound change might already have been added */
                  //assert(tightened || propagate);

                  *result = SCIP_REDUCEDDOM;
                  SCIPdebugMessage(" -> variable <%s> is infeasible in upward branch\n", SCIPvarGetName(lpcands[c]));
                  break; /* terminate initialization loop, because LP was changed */
               }
            }
            else if( allcolsinlp && !exactsolve && downvalid && upvalid )
            {
               SCIP_Real minbound;

               /* the minimal lower bound of both children is a proved lower bound of the current subtree */
               minbound = MIN(down, up);
               provedbound = MAX(provedbound, minbound);

               if( propagate )
               {
                  int nboundchgs;
                  int v;

                  nboundchgs = 0;

                  for( v = 0; v < nvars; ++v )
                  {
                     if( SCIPisGT(scip, newlbs[v], SCIPvarGetLbLocal(vars[v])) )
                     {
                        printf("better lower bound for variable <%s>: %.9g -> %.9g (strongbranching on var <%s>\n",
                           SCIPvarGetName(vars[v]), SCIPvarGetLbLocal(vars[v]), newlbs[v], SCIPvarGetName(lpcands[c]));

                        SCIPchgVarLb(scip, vars[v], newlbs[v]);
                        ++nboundchgs;
                     }
                     if( SCIPisLT(scip, newubs[v], SCIPvarGetUbLocal(vars[v])) )
                     {
                        printf("better upper bound for variable <%s>: %.9g -> %.9g (strongbranching on var <%s>\n",
                           SCIPvarGetName(vars[v]), SCIPvarGetUbLocal(vars[v]), newubs[v], SCIPvarGetName(lpcands[c]));

                        SCIPchgVarUb(scip, vars[v], newubs[v]);
                        ++nboundchgs;
                     }
                  }

                  if( nboundchgs > 0 )
                  {
                     *result = SCIP_REDUCEDDOM;
                     SCIPdebugMessage(" -> strong branching with propagation on variable <%s> led to %d bound changes\n", SCIPvarGetName(lpcands[c]), nboundchgs);
                     break; /* terminate initialization loop, because LP was changed */
                  }
               }
            }

            /* update pseudo cost values */
            assert(!downinf); /* otherwise, we would have terminated the initialization loop */
            assert(!upinf);
            SCIP_CALL( SCIPupdateVarPseudocost(scip, lpcands[c], 0.0-lpcandsfrac[c], downgain, 1.0) );
            SCIP_CALL( SCIPupdateVarPseudocost(scip, lpcands[c], 1.0-lpcandsfrac[c], upgain, 1.0) );
         }

         /* check for a better score, if we are within the maximum priority candidates */
         if( c < npriolpcands )
         {
            score = SCIPgetBranchScore(scip, lpcands[c], downgain, upgain);
            if( score > bestscore )
            {
               bestcand = c;
               bestdown = down;
               bestup = up;
               bestdownvalid = downvalid;
               bestupvalid = upvalid;
               bestscore = score;
            }
         }
         else
            score = 0.0;

         SCIPdebugMessage(" -> cand %d/%d (prio:%d) var <%s> (solval=%g, downgain=%g, upgain=%g, score=%g) -- best: <%s> (%g)\n",
            c, nlpcands, npriolpcands, SCIPvarGetName(lpcands[c]), lpcandssol[c], downgain, upgain, score,
            SCIPvarGetName(lpcands[bestcand]), bestscore);
      }

      /* end strong branching */
      SCIP_CALL( SCIPendStrongbranch(scip) );

      /* remember last evaluated candidate */
      branchruledata->lastcand = c;

      SCIPfreeBufferArray(scip, &newlbs);
      SCIPfreeBufferArray(scip, &newubs);

   }

   if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM && *result != SCIP_CONSADDED )
   {
      SCIP_NODE* downchild;
      SCIP_NODE* upchild;
      SCIP_VAR* var;

      assert(*result == SCIP_DIDNOTRUN);
      assert(0 <= bestcand && bestcand < nlpcands);
      assert(SCIPisLT(scip, provedbound, cutoffbound));

      var = lpcands[bestcand];

      /* perform the branching */
      SCIPdebugMessage(" -> %d candidates, selected candidate %d: variable <%s> (solval=%g, down=%g, up=%g, score=%g)\n",
         nlpcands, bestcand, SCIPvarGetName(var), lpcandssol[bestcand], bestdown, bestup, bestscore);
      SCIP_CALL( SCIPbranchVar(scip, var, &downchild, NULL, &upchild) );
      assert(downchild != NULL);
      assert(upchild != NULL);

      /* update the lower bounds in the children */
      if( allcolsinlp && !exactsolve )
      {
         SCIP_CALL( SCIPupdateNodeLowerbound(scip, downchild, bestdownvalid ? MAX(bestdown, provedbound) : provedbound) );
         SCIP_CALL( SCIPupdateNodeLowerbound(scip, upchild, bestupvalid ? MAX(bestup, provedbound) : provedbound) );
      }
      SCIPdebugMessage(" -> down child's lowerbound: %g\n", SCIPnodeGetLowerbound(downchild));
      SCIPdebugMessage(" -> up child's lowerbound: %g\n", SCIPnodeGetLowerbound(upchild));

      *result = SCIP_BRANCHED;
   }

   return SCIP_OKAY;
}


/*
 * branching specific interface methods
 */

/** creates the full strong LP branching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleFullstrong(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_BRANCHRULE* branchrule;

   /* create fullstrong branching rule data */
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata) );
   branchruledata->lastcand = 0;

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchruleBasic(scip, &branchrule, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY,
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchruledata) );

   assert(branchrule != NULL);

   /* set non-fundamental callbacks via specific setter functions*/
   SCIP_CALL( SCIPsetBranchruleCopy(scip, branchrule, branchCopyFullstrong) );
   SCIP_CALL( SCIPsetBranchruleFree(scip, branchrule, branchFreeFullstrong) );
   SCIP_CALL( SCIPsetBranchruleInit(scip, branchrule, branchInitFullstrong) );
   SCIP_CALL( SCIPsetBranchruleExit(scip, branchrule, branchExitFullstrong) );
   SCIP_CALL( SCIPsetBranchruleExecLp(scip, branchrule, branchExeclpFullstrong) );

   /* fullstrong branching rule parameters */
   SCIP_CALL( SCIPaddLongintParam(scip,
         "branching/fullstrong/reevalage",
         "number of intermediate LPs solved to trigger reevaluation of strong branching value for a variable that was already evaluated at the current node",
         &branchruledata->reevalage, TRUE, DEFAULT_REEVALAGE, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/fullstrong/maxproprounds",
         "maximum number of propagation rounds to be performed during strong branching before solving the LP (-1: no limit, -2: parameter settings)",
         &branchruledata->maxproprounds, TRUE, DEFAULT_MAXPROPROUNDS, -2, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}
