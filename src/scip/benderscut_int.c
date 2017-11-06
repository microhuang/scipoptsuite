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

/**@file   benderscut_int.c
 * @brief  Generates a Laporte and Louveaux Benders' decomposition integer cut
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/benderscut_int.h"
#include "scip/pub_benders.h"
#include "scip/pub_benderscut.h"
#include "scip/misc_benders.h"

#include "scip/cons_linear.h"
#include "scip/pub_lp.h"


#define BENDERSCUT_NAME             "integer"
#define BENDERSCUT_DESC             "Laporte and Louveaux Benders' decomposition integer cut"
#define BENDERSCUT_PRIORITY         0
#define BENDERSCUT_LPCUT            FALSE
#define BENDERSCUT_MUSTAGG          TRUE        /** Must all subproblems be aggregated for the generation of this cut */



#define SCIP_DEFAULT_SOLTOL               1e-2  /** The tolerance used to determine optimality of the solution */
#define SCIP_DEFAULT_ADDCUTS             FALSE  /** Should cuts be generated, instead of constraints */

#define SCIP_DEFAULT_CUTCONSTANT             0

/* event handler properties */
#define EVENTHDLR_NAME         "bendersintcutnodesolved"
#define EVENTHDLR_DESC         "node solved event handler for the Benders' integer cuts"

/*
 * Data structures
 */

/* TODO: fill in the necessary compression data */

/** Benders' decomposition cuts data */
struct SCIP_BenderscutData
{
   SCIP_BENDERS*         benders;            /**< the Benders' decomposition data structure */
   SCIP_Real             cutconstant;        /**< the constant for computing the integer cuts */
   SCIP_Real             soltol;             /**< the tolerance for the check between the auxiliary var and subprob */
   SCIP_Bool             addcuts;            /**< should cuts be generated instead of constraints */
   SCIP_Bool             firstcut;           /**< flag to indicate that the first cut needs to be generated. */
};


/** updates the cut constant of the Benders' cuts data.
 *  This function solves the master problem with only the auxiliary variables in the objective function. */
static
SCIP_RETCODE updateCutConstant(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< the Benders' cut data */
   )
{
   SCIP_VAR** vars;
   int nvars;
   int nsubproblems;
   int probnumber;
   int i;
   SCIP_Bool lperror;
   SCIP_Bool cutoff;

   assert(masterprob != NULL);
   assert(benderscutdata != NULL);

   /* don't run in probing or in repropagation */
   if( SCIPinProbing(masterprob) || SCIPinRepropagation(masterprob) )
      return SCIP_OKAY;

   nsubproblems = SCIPbendersGetNSubproblems(benderscutdata->benders);

   SCIP_CALL( SCIPstartProbing(masterprob) );

   /* change the master problem variables to 0 */
   nvars = SCIPgetNVars(masterprob);
   vars = SCIPgetVars(masterprob);

   for( i = 0; i < nvars; i++ )
   {
      probnumber = 0;
      while( probnumber < nsubproblems && SCIPgetBendersSubproblemVar(masterprob, benderscutdata->benders, vars[i], probnumber) == NULL )
         probnumber++;

      /* if the probnumber == nsubproblems, then the variable is an auxiliary variable. */
      if( probnumber < nsubproblems )
         SCIPchgVarObjProbing(masterprob, vars[i], 0);
   }

   /* solving the probing LP to get a lower bound on the auxiliary variables */
   SCIP_CALL( SCIPsolveProbingLP(masterprob, -1, &lperror, &cutoff) );

   if( SCIPisGT(masterprob, SCIPgetSolTransObj(masterprob, NULL), -SCIPinfinity(masterprob)) )
      benderscutdata->cutconstant = SCIPgetSolTransObj(masterprob, NULL);

   SCIPdebugMessage("Cut constant: %g\n", benderscutdata->cutconstant);

   benderscutdata->firstcut = TRUE;

   SCIP_CALL( SCIPendProbing(masterprob) );

   return SCIP_OKAY;
}

/** exec the event handler */
static
SCIP_DECL_EVENTEXEC(eventExecBendersintcutNodesolved)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   benderscutdata = (SCIP_BENDERSCUTDATA*)SCIPeventhdlrGetData(eventhdlr);

   SCIP_CALL( updateCutConstant(scip, benderscutdata) );

   SCIP_CALL(SCIPdropEvent(scip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, NULL, -1));

   return SCIP_OKAY;
}

/** solving process initialization method of event handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolBendersintcutNodesolved)
{
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   benderscutdata = (SCIP_BENDERSCUTDATA*)SCIPeventhdlrGetData(eventhdlr);

   if( SCIPbendersIsActive(benderscutdata->benders) )
      SCIP_CALL(SCIPcatchEvent(scip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, NULL, NULL));

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolBendersintcutNodesolved)
{
   assert(scip != NULL);

   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

#if 0 /* not sure whether this is actually needed */
   SCIP_CALL(SCIPdropEvent(scip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, NULL, -1));
#endif

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
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR** vars;
   int nvars;
   SCIP_Real subprobobj;   /* the objective function value of the subproblem */
   SCIP_Real lhs;          /* the left hand side of the cut */
   int nsubproblems;       /* number of subproblems */
   int probnumber;
   int i;

#ifndef NDEBUG
   SCIP_Real verifyobj = 0;
#endif

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* getting the best solution from the subproblem */
   subprobobj = 0;
   for( i = 0; i < nsubproblems; i++ )
   {
      SCIP* subproblem;
      SCIP_SOL* subprobsol;

#ifdef SCIP_DEBUG
      subproblem = SCIPbendersSubproblem(benders, i);
      subprobsol = SCIPgetBestSol(subproblem);
#endif

      if( SCIPgetStatus(SCIPbendersSubproblem(benders, i)) == SCIP_STATUS_OPTIMAL )
         subprobobj += SCIPbendersGetSubprobObjval(benders, i);
      else
         subprobobj += SCIPgetDualbound(SCIPbendersSubproblem(benders, i));

      SCIPdebugMessage("Subproblem %d - Objective Value: Stored - %g Orig Obj - %g\n", i,
         SCIPbendersGetSubprobObjval(benders, i), SCIPgetSolOrigObj(subproblem, subprobsol));
   }

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
      SCIP_Real coef;

      /* checking whether the variable has a corresponding subproblem variable.
       * This check ensures that auxiliary variables are not added to the cut.
       * NOTE: Need to check whether this is always valid. Not all master problem variable have a corresponding
       * subproblem variable. */
      probnumber = 0;
      while( probnumber < nsubproblems && SCIPgetBendersSubproblemVar(masterprob, benders, vars[i], probnumber) == NULL )
         probnumber++;

      /* if the probnumber == nsubproblems, then the variable is an auxiliary variable. */
      if( probnumber < nsubproblems )
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
            SCIP_CALL( SCIPaddVarToRow(masterprob, row, vars[i], coef) );
         else
            SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, vars[i], coef) );
      }
   }

   lhs += subprobobj;

   /* Update the lhs of the cut */
   if( addcut )
      SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
   else
      SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );


#ifndef NDEBUG
   if( addcut )
      lhs = SCIProwGetLhs(row);
   else
      lhs = SCIPgetLhsLinear(masterprob, cons);
   verifyobj = lhs;

   if( addcut )
      verifyobj -= SCIPgetRowSolActivity(masterprob, row, sol);
   else
      verifyobj -= SCIPgetActivityLinear(masterprob, cons, sol);
#endif

   //assert(SCIPisFeasEQ(masterprob, verifyobj, subprobobj));

   return SCIP_OKAY;
}


/* adds the auxiliary variable to the generated cut. If this is the first optimality cut for the subproblem, then the
 * auxiliary variable is first created and added to the master problem. */
static
SCIP_RETCODE addAuxiliaryVariableToCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR* auxiliaryvar;
   int nsubproblems;
   int i;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   for( i = 0; i < nsubproblems; i++ )
   {
      auxiliaryvar = SCIPbendersGetAuxiliaryVar(benders, i);

      /* adding the auxiliary variable to the generated cut */
      if( addcut )
         SCIP_CALL( SCIPaddVarToRow(masterprob, row, auxiliaryvar, 1.0) );
      else
         SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, auxiliaryvar, 1.0) );
   }


   return SCIP_OKAY;
}


/* generates and applies Benders' cuts */
static
SCIP_RETCODE generateAndApplyBendersIntegerCuts(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition */
   SCIP_BENDERSCUT*      benderscut,         /**< the benders' decomposition cut method */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_RESULT*          result,             /**< the result from solving the subproblems */
   SCIP_Bool             initcons            /**< is this function called to generate the initial constraint */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_CONSHDLR* consbenders;      /* the Benders' decomposition constraint handler */
   SCIP_CONS* cons;                 /* the cut that will be generated from the solution to the pricing problem */
   SCIP_ROW* row;                   /* the that is generated for the Benders' cut */
   char cutname[SCIP_MAXSTRLEN];    /* the name of the generated cut */
   SCIP_Bool optimal;               /* flag to indicate whether the current subproblem is optimal for the master */
   SCIP_Bool addcut;
   int nsubproblems;
   int i;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   row = NULL;
   cons = NULL;

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* retreiving the Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);

   /* if the cuts are generated prior to the solving stage, then rows can not be generated. So constraints must be added
    * to the master problem. */
   if( SCIPgetStage(masterprob) < SCIP_STAGE_INITSOLVE )
      addcut = FALSE;
   else
      addcut = benderscutdata->addcuts;

   /* retrieving the Benders' decomposition constraint handler */
   consbenders = SCIPfindConshdlr(masterprob, "benders");

   /* checking the optimality of the original problem with a comparison between the auxiliary variable and the
    * objective value of the subproblem */

   optimal = FALSE;
   for( i = 0; i < nsubproblems; i++ )
   {
      SCIP_CALL( SCIPcheckBendersAuxiliaryVar(masterprob, benders, sol, i, &optimal) );

      if( !optimal )
         break;
   }

   if( optimal )
   {
      SCIPdebugMsg(masterprob, "No <%s> cut added. Current Master Problem Obj: %g\n", BENDERSCUT_NAME,
         SCIPgetSolOrigObj(masterprob, NULL));
      return SCIP_OKAY;
   }

   /* if no integer cuts have been previously generated, then an initial lower bounding cut is added */
   if( benderscutdata->firstcut )
   {
      benderscutdata->firstcut = FALSE;
      SCIP_CALL( generateAndApplyBendersIntegerCuts(masterprob, benders, benderscut, sol, type, result, TRUE) );
   }

   /* setting the name of the generated cut */
   (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "integeroptcut_%d", SCIPbenderscutGetNFound(benderscut) );

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

      lhs += benderscutdata->cutconstant;

      /* Update the lhs of the cut */
      if( addcut )
         SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
      else
         SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
   }
   else
   {
      /* computing the coefficients of the optimality cut */
      SCIP_CALL( computeStandardIntegerOptCut(masterprob, benders, sol, cons, row, benderscutdata->cutconstant, addcut) );
   }

   /* adding the auxiliary variable to the optimality cut */
   SCIP_CALL( addAuxiliaryVariableToCut(masterprob, benders, cons, row, addcut) );

   /* adding the constraint to the master problem */
   if( addcut )
   {
      SCIP_Bool infeasible;

      if( type == LP || type == RELAX )
      {
         SCIP_CALL( SCIPaddCut(masterprob, sol, row, FALSE, &infeasible) );
         assert(!infeasible);
      }
      else
      {
         assert(type == CHECK || type == PSEUDO);
         SCIP_CALL( SCIPaddPoolCut(masterprob, row) );
      }

      /* storing the generated cut */
      //SCIP_CALL( SCIPstoreBenderscutCut(masterprob, benderscut, row) );

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
      //SCIP_CALL( SCIPstoreBenderscutCons(masterprob, benderscut, cons) );

#ifdef SCIP_DEBUG
      SCIP_CALL( SCIPprintCons(masterprob, cons, NULL) );
      SCIPinfoMessage(masterprob, NULL, ";\n");
#endif

      SCIP_CALL( SCIPreleaseCons(masterprob, &cons) );

      (*result) = SCIP_CONSADDED;
   }


   return SCIP_OKAY;
}

/*
 * Callback methods of Benders' decomposition cuts
 */

/* TODO: Implement all necessary Benders' decomposition cuts methods. The methods with an #if 0 ... #else #define ... are optional */

/** copy method for Benders' decomposition cuts plugins (called when SCIP copies plugins) */
#if 0
static
SCIP_DECL_BENDERSCUTCOPY(benderscutCopyInt)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of int Benders' decomposition cuts not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define benderscutCopyInt NULL
#endif

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
#if 0
static
SCIP_DECL_BENDERSCUTINIT(benderscutInitInt)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of int Benders' decomposition cuts not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define benderscutInitInt NULL
#endif


/** deinitialization method of Benders' decomposition cuts (called before transformed problem is freed) */
#if 0
static
SCIP_DECL_BENDERSCUTEXIT(benderscutExitInt)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of int Benders' decomposition cuts not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define benderscutExitInt NULL
#endif


/** solving process initialization method of Benders' decomposition cuts (called when branch and bound process is about to begin) */
#if 0
static
SCIP_DECL_BENDERSCUTINITSOL(benderscutInitsolInt)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of int Benders' decomposition cuts not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define benderscutInitsolInt NULL
#endif


/** solving process deinitialization method of Benders' decomposition cuts (called before branch and bound process data is freed) */
#if 0
static
SCIP_DECL_BENDERSCUTEXITSOL(benderscutExitsolInt)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of int Benders' decomposition cuts not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define benderscutExitsolInt NULL
#endif


/** execution method of Benders' decomposition cuts */
static
SCIP_DECL_BENDERSCUTEXEC(benderscutExecInt)
{  /*lint --e{715}*/
   SCIP_Bool optimal;
   int nsubproblems;
   int i;

   assert(scip != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   /* it is only possible to generate the Laporte and Louveaux cuts for pure binary master problems */
   if( SCIPgetNBinVars(scip) == SCIPgetNVars(scip) )
      return SCIP_OKAY;

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* only generate optimality cuts if the subproblem is optimal */
   optimal = TRUE;
   for( i = 0; i < nsubproblems; i++ )
   {
      if( SCIPgetStatus(SCIPbendersSubproblem(benders, i)) >= SCIP_STATUS_INFEASIBLE )
      {
         optimal = FALSE;
         break;
      }
   }

   if( optimal )
   {
      /* generating a cut for a given subproblem */
      SCIP_CALL( generateAndApplyBendersIntegerCuts(scip, benders, benderscut, sol, type, result, FALSE) );
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
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;


   assert(benders != NULL);

   /* create int Benders' decomposition cuts data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &benderscutdata) );
   benderscutdata->soltol = 1e-04;
   benderscutdata->benders = benders;
   benderscutdata->firstcut = TRUE;

   benderscut = NULL;

   /* include Benders' decomposition cuts */
#if 0
   /* use SCIPincludeBenderscut() if you want to set all callbacks explicitly and realize (by getting compiler errors) when
    * new callbacks are added in future SCIP versions
    */
   SCIP_CALL( SCIPincludeBenderscut(scip, benders, BENDERSCUT_NAME, BENDERSCUT_DESC, BENDERSCUT_PRIORITY,
         BENDERSCUT_LPCUT, benderscutCopyInt, benderscutFreeXyz, benderscutInitXyz, benderscutExitXyz,
         benderscutInitsolXyz, benderscutExitsolInt, benderscutExecXyz, benderscutdata) );
#else
   /* use SCIPincludeBenderscutBasic() plus setter functions if you want to set callbacks one-by-one and your code should
    * compile independent of new callbacks being added in future SCIP versions
    */
   SCIP_CALL( SCIPincludeBenderscutBasic(scip, benders, &benderscut, BENDERSCUT_NAME, BENDERSCUT_DESC,
         BENDERSCUT_PRIORITY, BENDERSCUT_LPCUT, benderscutExecInt, benderscutdata) );

   assert(benderscut != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBenderscutCopy(scip, benderscut, benderscutCopyInt) );
   SCIP_CALL( SCIPsetBenderscutFree(scip, benderscut, benderscutFreeInt) );
   SCIP_CALL( SCIPsetBenderscutInit(scip, benderscut, benderscutInitInt) );
   SCIP_CALL( SCIPsetBenderscutExit(scip, benderscut, benderscutExitInt) );
   SCIP_CALL( SCIPsetBenderscutInitsol(scip, benderscut, benderscutInitsolInt) );
   SCIP_CALL( SCIPsetBenderscutExitsol(scip, benderscut, benderscutExitsolInt) );
#endif

   /* add int Benders' decomposition cuts parameters */
   SCIP_CALL( SCIPaddRealParam(scip,
         "benderscut/" BENDERSCUT_NAME "/cutsconstant",
         "the constant term of the integer Benders' cuts.",
         &benderscutdata->cutconstant, FALSE, SCIP_DEFAULT_CUTCONSTANT, -SCIPinfinity(scip), SCIPinfinity(scip),
         NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip,
         "benderscut/" BENDERSCUT_NAME "/solutiontol",
         "the tolerance used for the comparison between the auxiliary variable and the subproblem objective.",
         &benderscutdata->soltol, FALSE, SCIP_DEFAULT_SOLTOL, 0.0, 1.0, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "benderscut/" BENDERSCUT_NAME "/addcuts",
         "should cuts be generated and added to the cutpool instead of global constraints directly added to the problem.",
         &benderscutdata->addcuts, FALSE, SCIP_DEFAULT_ADDCUTS, NULL, NULL) );

   eventhdlrdata = (SCIP_EVENTHDLRDATA*)benderscutdata;

   /* include event handler into SCIP */
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC,
         eventExecBendersintcutNodesolved, eventhdlrdata) );
   SCIP_CALL( SCIPsetEventhdlrInitsol(scip, eventhdlr, eventInitsolBendersintcutNodesolved) );
   SCIP_CALL( SCIPsetEventhdlrExitsol(scip, eventhdlr, eventExitsolBendersintcutNodesolved) );
   assert(eventhdlr != NULL);

   return SCIP_OKAY;
}
