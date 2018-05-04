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

/**@file   benderscut_int.c
 * @brief  Generates a Laporte and Louveaux Benders' decomposition integer cut
 * @author Stephen J. Maher
 *
 * The classical Benders' decomposition algorithm is only applicable to problems with continuous second stage variables.
 * Laporte and Louveaux (1993) developed a method for generating cuts when Benders' decomposition is applied to problem
 * with discrete second stage variables. However, these cuts are only applicable when the master problem is a pure
 * binary problem.
 *
 * The integer optimality cuts are a point-wise underestimator of the optimal subproblem objective function value.
 * Similar to benderscuts_opt.c, an auxiliary variable, \f$\varphi\f$. is required in the master problem as a lower
 * bound on the optimal objective function value for the Benders' decomposition subproblem.
 *
 * Consider the Benders' decomposition subproblem that takes the master problem solution \f$\bar{x}\f$ as input:
 * \f[
 * z(\bar{x}) = \min\{d^{T}y : Ty \geq h - H\bar{x}, y \mbox{ integer}\}
 * \f]
 * If the subproblem is feasible, and \f$z(\bar{x}) > \varphi\f$ (indicating that the current underestimators are not
 * optimal) then the Benders' decomposition integer optimality cut can be generated from the optimal solution of the
 * subproblem. Let \f$S_{r}\f$ be the set of indicies for master problem variables that are 1 in \f$\bar{x}\f$ and
 * \f$L\f$ a known lowerbound on the subproblem objective function value.
 *
 * The resulting cut is:
 * \f[
 * \varphi \geq (z(\bar{x}) - L)(\sum_{i \in S_{r}}(x_{i} - 1) + \sum_{i \notin S_{r}}x_{i} + 1)
 * \f]
 *
 * Laporte, G. & Louveaux, F. V. The integer L-shaped method for stochastic integer programs with complete recourse
 * Operations Research Letters, 1993, 13, 133-142
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/benderscut_int.h"
#include "scip/pub_benders.h"
#include "scip/pub_benderscut.h"

#include "scip/cons_linear.h"
#include "scip/pub_lp.h"


#define BENDERSCUT_NAME             "integer"
#define BENDERSCUT_DESC             "Laporte and Louveaux Benders' decomposition integer cut"
#define BENDERSCUT_PRIORITY         0
#define BENDERSCUT_LPCUT        FALSE



#define SCIP_DEFAULT_SOLTOL               1e-2  /** The tolerance used to determine optimality of the solution */
#define SCIP_DEFAULT_ADDCUTS             FALSE  /** Should cuts be generated, instead of constraints */

#define SCIP_DEFAULT_CUTCONSTANT          -10000.0

/*
 * Data structures
 */

/** Benders' decomposition cuts data */
struct SCIP_BenderscutData
{
   SCIP_BENDERS*         benders;            /**< the Benders' decomposition data structure */
   SCIP_Real             cutconstant;        /**< the constant for computing the integer cuts */
   SCIP_Real*            subprobconstant;    /**< the constant for each subproblem used for computing the integer cuts */
   SCIP_Real             soltol;             /**< the tolerance for the check between the auxiliary var and subprob */
   SCIP_Bool             addcuts;            /**< should cuts be generated instead of constraints */
   SCIP_Bool*            firstcut;           /**< flag to indicate that the first cut needs to be generated. */
   int                   nsubproblems;       /**< the number of subproblems for the Benders' decomposition */
};

/** method to call, when the priority of a benders was changed */
static
SCIP_DECL_PARAMCHGD(paramChgdBenderscutintConstant)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;
   int i;

   benderscutdata = (SCIP_BENDERSCUTDATA*)SCIPparamGetData(param);
   assert(benderscutdata != NULL);

   for( i = 0; i < benderscutdata->nsubproblems; i++ )
      benderscutdata->subprobconstant[i] = benderscutdata->cutconstant;


   return SCIP_OKAY;
}


/** creates the Benders' decomposition cut data */
static
SCIP_RETCODE createBenderscutData(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< the Benders' cut data */
   )
{
   int i;

   assert(scip != NULL);
   assert(benderscutdata != NULL);

   /* allocating the memory for the subproblem constants */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &benderscutdata->subprobconstant, benderscutdata->nsubproblems) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &benderscutdata->firstcut, benderscutdata->nsubproblems) );

   for( i = 0; i < benderscutdata->nsubproblems; i++ )
   {
      benderscutdata->subprobconstant[i] = benderscutdata->cutconstant;
      benderscutdata->firstcut[i] = TRUE;
   }

   return SCIP_OKAY;
}

/*
 * Local methods
 */

/* computing as standard Benders' optimality cut from the dual solutions of the LP */
static
SCIP_RETCODE computeStandardIntegerOptCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   SCIP_Real             cutconstant,        /**< the constant value in the integer optimality cut */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_Bool             addcut,             /**< indicates whether a cut is created instead of a constraint */
   SCIP_Bool*            success             /**< was the cut generation successful? */
   )
{
   SCIP_VAR** vars;
   int nvars;
   SCIP_Real subprobobj;   /* the objective function value of the subproblem */
   SCIP_Real lhs;          /* the left hand side of the cut */
   int i;
   SCIP* subproblem;
   SCIP_SOL* subprobsol;


#ifndef NDEBUG
   SCIP_Real verifyobj = 0;
#endif

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   (*success) = FALSE;

   /* getting the best solution from the subproblem */

#ifdef SCIP_DEBUG
   subproblem = SCIPbendersSubproblem(benders, probnumber);
   subprobsol = SCIPgetBestSol(subproblem);
#endif

   subprobobj = SCIPbendersGetSubprobObjval(benders, probnumber);

   SCIPdebugMsg(masterprob, "Subproblem %d - Objective Value: Stored - %g Orig Obj - %g\n", probnumber,
      SCIPbendersGetSubprobObjval(benders, probnumber), SCIPgetSolOrigObj(subproblem, subprobsol));

   nvars = SCIPgetNVars(masterprob);
   vars = SCIPgetVars(masterprob);

   /* adding the subproblem objective function value to the lhs */
   if( addcut )
      lhs = SCIProwGetLhs(row);
   else
      lhs = SCIPgetLhsLinear(masterprob, cons);

   /* looping over all master problem variables to update the coefficients in the computed cut. */
   for( i = 0; i < nvars; i++ )
   {
      SCIP_VAR* subprobvar;
      SCIP_Real coef;

      SCIP_CALL( SCIPgetBendersSubproblemVar(masterprob, benders, vars[i], &subprobvar, probnumber) );

      /* if there is a corresponding subproblem variable, then the variable will not be NULL. */
      if( subprobvar != NULL )
      {
         /* if the variable is on its upper bound, then the subproblem objective value is added to the cut */
         if( SCIPisFeasEQ(masterprob, SCIPgetSolVal(masterprob, sol, vars[i]), 1.0) )
         {
            coef = -(subprobobj - cutconstant);
            lhs -= (subprobobj - cutconstant);
         }
         else
            coef = (subprobobj - cutconstant);

         /* adding the variable to the cut. The coefficient is the subproblem objective value */
         if( addcut )
         {
            SCIP_CALL( SCIPaddVarToRow(masterprob, row, vars[i], coef) );
         }
         else
         {
            SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, vars[i], coef) );
         }
      }
   }

   lhs += subprobobj;

   /* if the bound becomes infinite, then the cut generation terminates. */
   if( SCIPisInfinity(masterprob, lhs) || SCIPisInfinity(masterprob, -lhs) )
   {
      (*success) = FALSE;
      SCIPdebugMsg(masterprob, "Infinite bound when generating integer optimality cut.\n");
      return SCIP_OKAY;
   }

   /* Update the lhs of the cut */
   if( addcut )
   {
      SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
   }
   else
   {
      SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
   }


#ifndef NDEBUG
   if( addcut )
      lhs = SCIProwGetLhs(row);
   else
      lhs = SCIPgetLhsLinear(masterprob, cons);
   verifyobj += lhs;

   if( addcut )
      verifyobj -= SCIPgetRowSolActivity(masterprob, row, sol);
   else
      verifyobj -= SCIPgetActivityLinear(masterprob, cons, sol);
#endif

   assert(SCIPisFeasEQ(masterprob, verifyobj, subprobobj));

   (*success) = TRUE;

   return SCIP_OKAY;
}


/** adds the auxiliary variable to the generated cut. If this is the first optimality cut for the subproblem, then the
 *  auxiliary variable is first created and added to the master problem.
 */
static
SCIP_RETCODE addAuxiliaryVariableToCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR* auxiliaryvar;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   auxiliaryvar = SCIPbendersGetAuxiliaryVar(benders, probnumber);

   /* adding the auxiliary variable to the generated cut */
   if( addcut )
   {
      SCIP_CALL( SCIPaddVarToRow(masterprob, row, auxiliaryvar, 1.0) );
   }
   else
   {
      SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, auxiliaryvar, 1.0) );
   }

   return SCIP_OKAY;
}


/** generates and applies Benders' cuts */
static
SCIP_RETCODE generateAndApplyBendersIntegerCuts(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition */
   SCIP_BENDERSCUT*      benderscut,         /**< the benders' decomposition cut method */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_RESULT*          result,             /**< the result from solving the subproblems */
   SCIP_Bool             initcons            /**< is this function called to generate the initial constraint */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_CONSHDLR* consbenders;
   SCIP_CONS* cons;
   SCIP_ROW* row;
   char cutname[SCIP_MAXSTRLEN];
   SCIP_Bool optimal;
   SCIP_Bool addcut;
   SCIP_Bool success;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   row = NULL;
   cons = NULL;

   /* retrieving the Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);

   /* if the cuts are generated prior to the solving stage, then rows can not be generated. So constraints must be added
    * to the master problem.
    */
   if( SCIPgetStage(masterprob) < SCIP_STAGE_INITSOLVE )
      addcut = FALSE;
   else
      addcut = benderscutdata->addcuts;

   /* retrieving the Benders' decomposition constraint handler */
   consbenders = SCIPfindConshdlr(masterprob, "benders");

   /* checking the optimality of the original problem with a comparison between the auxiliary variable and the
    * objective value of the subproblem
    */
   optimal = FALSE;
   SCIP_CALL( SCIPcheckBendersSubprobOptimality(masterprob, benders, sol, probnumber, &optimal) );

   if( optimal )
   {
      SCIPdebugMsg(masterprob, "No <%s> cut added. Current Master Problem Obj: %g\n", BENDERSCUT_NAME,
         SCIPgetSolOrigObj(masterprob, NULL));
      return SCIP_OKAY;
   }

   /* checking if the subproblem lower bound has been updated. If it is has changed, then firstcut is set to TRUE.
    * Otherwise, the constant remains the same.
    */
   if( SCIPisLT(masterprob, benderscutdata->subprobconstant[probnumber],
         SCIPbendersGetSubprobLowerbound(benders, probnumber)) )
   {
      benderscutdata->subprobconstant[probnumber] = SCIPbendersGetSubprobLowerbound(benders, probnumber);
      benderscutdata->firstcut[probnumber] = TRUE;
   }

   /* if no integer cuts have been previously generated, then an initial lower bounding cut is added */
   if( benderscutdata->firstcut[probnumber] )
   {
      benderscutdata->firstcut[probnumber] = FALSE;
      SCIP_CALL( generateAndApplyBendersIntegerCuts(masterprob, benders, benderscut, sol, probnumber, type, result,
            TRUE) );
   }

   /* setting the name of the generated cut */
   (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "integeroptcut_%d_%d", probnumber,
      SCIPbenderscutGetNFound(benderscut) );

   /* creating an empty row or constraint for the Benders' cut */
   if( addcut )
   {
      SCIP_CALL( SCIPcreateEmptyRowCons(masterprob, &row, consbenders, cutname, 0.0, SCIPinfinity(masterprob), FALSE,
            FALSE, TRUE) );
   }
   else
   {
      SCIP_CALL( SCIPcreateConsBasicLinear(masterprob, &cons, cutname, 0, NULL, NULL, 0.0, SCIPinfinity(masterprob)) );
      SCIP_CALL( SCIPsetConsDynamic(masterprob, cons, TRUE) );
      SCIP_CALL( SCIPsetConsRemovable(masterprob, cons, TRUE) );
   }

   if( initcons )
   {
      SCIP_Real lhs;

      /* adding the subproblem objective function value to the lhs */
      if( addcut )
         lhs = SCIProwGetLhs(row);
      else
         lhs = SCIPgetLhsLinear(masterprob, cons);

      lhs += benderscutdata->subprobconstant[probnumber];

      /* if the bound becomes infinite, then the cut generation terminates. */
      if( SCIPisInfinity(masterprob, lhs) || SCIPisInfinity(masterprob, -lhs) )
      {
         success = FALSE;
         SCIPdebugMsg(masterprob, "Infinite bound when generating integer optimality cut.\n");
      }

      /* Update the lhs of the cut */
      if( addcut )
      {
         SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
      }
      else
      {
         SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
      }
   }
   else
   {
      /* computing the coefficients of the optimality cut */
      SCIP_CALL( computeStandardIntegerOptCut(masterprob, benders, sol, cons, row,
            benderscutdata->subprobconstant[probnumber], probnumber, addcut, &success) );
   }

   /* if success is FALSE, then there was an error in generating the integer optimality cut. No cut will be added to
    * the master problem. Otherwise, the constraint is added to the master problem.
    */
   if( !success )
   {
      (*result) = SCIP_DIDNOTFIND;
      SCIPdebugMsg(masterprob, "Error in generating Benders' integer optimality cut for problem %d.\n", probnumber);
   }
   else
   {
      /* adding the auxiliary variable to the optimality cut */
      SCIP_CALL( addAuxiliaryVariableToCut(masterprob, benders, cons, row, probnumber, addcut) );

      /* adding the constraint to the master problem */
      if( addcut )
      {
         SCIP_Bool infeasible;

         if( type == SCIP_BENDERSENFOTYPE_LP || type == SCIP_BENDERSENFOTYPE_RELAX )
         {
            SCIP_CALL( SCIPaddRow(masterprob, row, FALSE, &infeasible) );
            assert(!infeasible);
         }
         else
         {
            assert(type == SCIP_BENDERSENFOTYPE_CHECK || type == SCIP_BENDERSENFOTYPE_PSEUDO);
            SCIP_CALL( SCIPaddPoolCut(masterprob, row) );
         }

         /* storing the generated cut */
         SCIP_CALL( SCIPstoreBenderscutCut(masterprob, benderscut, row) );

#ifdef SCIP_DEBUG
         SCIP_CALL( SCIPprintRow(masterprob, row, NULL) );
         SCIPinfoMessage(masterprob, NULL, ";\n");
#endif

         /* release the row */
         SCIP_CALL( SCIPreleaseRow(masterprob, &row) );

         (*result) = SCIP_SEPARATED;
      }
      else
      {
         SCIP_CALL( SCIPaddCons(masterprob, cons) );

         /* storing the generated cut */
         SCIP_CALL( SCIPstoreBenderscutCons(masterprob, benderscut, cons) );

         SCIPdebugPrintCons(masterprob, cons, NULL);

         SCIP_CALL( SCIPreleaseCons(masterprob, &cons) );

         (*result) = SCIP_CONSADDED;
      }
   }


   return SCIP_OKAY;
}

/*
 * Callback methods of Benders' decomposition cuts
 */

/** destructor of Benders' decomposition cuts to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BENDERSCUTFREE(benderscutFreeInt)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert( benderscut != NULL );
   assert( strcmp(SCIPbenderscutGetName(benderscut), BENDERSCUT_NAME) == 0 );

   /* free Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert( benderscutdata != NULL );

   SCIPfreeBlockMemory(scip, &benderscutdata);

   SCIPbenderscutSetData(benderscut, NULL);

   return SCIP_OKAY;
}


/** initialization method of Benders' decomposition cuts (called after problem was transformed) */
static
SCIP_DECL_BENDERSCUTINIT(benderscutInitInt)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert( benderscut != NULL );
   assert( strcmp(SCIPbenderscutGetName(benderscut), BENDERSCUT_NAME) == 0 );

   /* free Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert( benderscutdata != NULL );

   benderscutdata->nsubproblems = SCIPbendersGetNSubproblems(benderscutdata->benders);
   SCIP_CALL( createBenderscutData(scip, benderscutdata) );

   return SCIP_OKAY;
}

/** deinitialization method of Benders' decomposition cuts (called before transformed problem is freed) */
static
SCIP_DECL_BENDERSCUTEXIT(benderscutExitInt)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert( benderscut != NULL );
   assert( strcmp(SCIPbenderscutGetName(benderscut), BENDERSCUT_NAME) == 0 );

   /* free Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert( benderscutdata != NULL );

   SCIPfreeBlockMemoryArray(scip, &benderscutdata->firstcut, benderscutdata->nsubproblems);
   SCIPfreeBlockMemoryArray(scip, &benderscutdata->subprobconstant, benderscutdata->nsubproblems);

   return SCIP_OKAY;
}

/** execution method of Benders' decomposition cuts */
static
SCIP_DECL_BENDERSCUTEXEC(benderscutExecInt)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   /* it is only possible to generate the Laporte and Louveaux cuts for pure binary master problems */
   if( SCIPgetNBinVars(scip) != (SCIPgetNVars(scip) - SCIPbendersGetNSubproblems(benders)) )
   {
      SCIPinfoMessage(scip, NULL, "The Laporte and Louveaux Benders' decomposition integer optimality cuts"
         " can only be applied to problems with a pure binary master problem.\n"
         "No integer optimality cuts will be generated for this problem. As such, your solution will be suboptimal.\n");

      SCIPinfoMessage(scip, NULL, "The Laporte and Louveaux Benders' decomposition cuts will be disabled.\n");

      SCIPbenderscutSetEnabled(benderscut, FALSE);

      return SCIP_OKAY;
   }

   /* the integer subproblem could terminate early if the auxiliary variable value is much greater than the optimal
    * solution. As such, it is only necessary to generate a cut if the subproblem is OPTIMAL */
   if( SCIPgetStatus(SCIPbendersSubproblem(benders, probnumber)) == SCIP_STATUS_OPTIMAL )
   {
      /* generating a cut for a given subproblem */
      SCIP_CALL( generateAndApplyBendersIntegerCuts(scip, benders, benderscut, sol, probnumber, type, result, FALSE) );
   }

   return SCIP_OKAY;
}


/*
 * Benders' decomposition cuts specific interface methods
 */

/** creates the int Benders' decomposition cuts and includes it in SCIP */
SCIP_RETCODE SCIPincludeBenderscutInt(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_BENDERSCUT* benderscut;
   char paramname[SCIP_MAXSTRLEN];


   assert(benders != NULL);

   /* create int Benders' decomposition cuts data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &benderscutdata) );
   benderscutdata->soltol = 1e-04;
   benderscutdata->benders = benders;

   benderscut = NULL;

   /* include Benders' decomposition cuts */
   SCIP_CALL( SCIPincludeBenderscutBasic(scip, benders, &benderscut, BENDERSCUT_NAME, BENDERSCUT_DESC,
         BENDERSCUT_PRIORITY, BENDERSCUT_LPCUT, benderscutExecInt, benderscutdata) );

   assert(benderscut != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBenderscutFree(scip, benderscut, benderscutFreeInt) );
   SCIP_CALL( SCIPsetBenderscutInit(scip, benderscut, benderscutInitInt) );
   SCIP_CALL( SCIPsetBenderscutExit(scip, benderscut, benderscutExitInt) );

   /* add int Benders' decomposition cuts parameters */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/cutsconstant",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddRealParam(scip, paramname,
         "the constant term of the integer Benders' cuts.",
         &benderscutdata->cutconstant, FALSE, SCIP_DEFAULT_CUTCONSTANT, -SCIPinfinity(scip), SCIPinfinity(scip),
         paramChgdBenderscutintConstant, (SCIP_PARAMDATA*)benderscutdata) );

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/solutiontol",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddRealParam(scip, paramname,
         "the tolerance used for the comparison between the auxiliary variable and the subproblem objective.",
         &benderscutdata->soltol, FALSE, SCIP_DEFAULT_SOLTOL, 0.0, 1.0, NULL, NULL) );

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/addcuts",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddBoolParam(scip, paramname,
         "should cuts be generated and added to the cutpool instead of global constraints directly added to the problem.",
         &benderscutdata->addcuts, FALSE, SCIP_DEFAULT_ADDCUTS, NULL, NULL) );

   return SCIP_OKAY;
}
