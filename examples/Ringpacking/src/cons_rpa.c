#define SCIP_DEBUG
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

/**@file   cons_rpa.c
 * @brief  constraint handler for recursive circle packing
 * @author Benjamin Mueller
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "cons_rpa.h"
#include "probdata_rpa.h"
#include "pattern.h"


/* fundamental constraint handler properties */
#define CONSHDLR_NAME          "rpa"
#define CONSHDLR_DESC          "ringpacking constraint handler"
#define CONSHDLR_ENFOPRIORITY  -3000000 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY        0 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_EAGERFREQ          100 /**< frequency for using all instead of only the useful constraints in separation,
                                         *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_NEEDSCONS        FALSE /**< should the constraint handler be skipped, if no constraints are available? */

/* optional constraint handler properties */
/* TODO: remove properties which are never used because the corresponding routines are not supported */
#define CONSHDLR_SEPAPRIORITY         0 /**< priority of the constraint handler for separation */
#define CONSHDLR_SEPAFREQ            -1 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */

#define CONSHDLR_PROPFREQ            -1 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_PROP_TIMING     SCIP_PROPTIMING_BEFORELP /**< propagation timing mask of the constraint handler*/

#define CONSHDLR_PRESOLTIMING    SCIP_PRESOLTIMING_MEDIUM /**< presolving timing of the constraint handler (fast, medium, or exhaustive) */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */



/*
 * Data structures
 */

/** constraint handler data */
struct SCIP_ConshdlrData
{
   SCIP_Bool*            locked;            /**< array to remember which (not verified) patterns have been locked */
   int                   lockedzise;        /**< size of locked array */
};


/*
 * Local methods
 */

/** auxiliary function to decide whether a proposed solution is feasible; a solution is called feasible if and only if
 *  z*_C = 0 holds for all circular patterns that are either not packable, i.e., SCIP_PACKABLE_NO or SCIP_PACKABLE_UNKNOWN
 */
static
SCIP_Bool isSolFeasible(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol                 /**< solution (NULL for LP solution) */
   )
{
   SCIP_PROBDATA* probdata;
   SCIP_PATTERN** cpatterns;
   SCIP_VAR** cvars;
   int ncpatterns;
   int p;

   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   /* get information about circular patterns and their corresponding variables */
   SCIPprobdataGetCInfos(probdata, &cpatterns, &cvars, &ncpatterns);
   assert(ncpatterns > 0);

   for( p = 0; p < ncpatterns; ++p )
   {
      assert(cpatterns[p] != NULL);
      assert(cvars[p] != NULL);

      /* check only circular patterns which might not be packable */
      if( SCIPpatternGetPackableStatus(cpatterns[p]) != SCIP_PACKABLE_YES )
      {
         SCIP_Real solval = SCIPgetSolVal(scip, sol, cvars[p]);

         if( !SCIPisFeasZero(scip, solval) )
         {
            SCIPdebugMsg(scip, "solution infeasible because of circular pattern %d = (%g,%d)\n", p,
               SCIPgetSolVal(scip, sol, cvars[p]), SCIPpatternGetPackableStatus(cpatterns[p]));
            return FALSE;
         }
      }
   }

   return TRUE;
}

/** auxiliary function for enforcing ringpacking constraint; the function checks whether
 *
 *  1. the solution is feasible; if yes -> skip
 *  2. tries to verify an unverified circular pattern C with z*_c > 0
 *     2a. case packable or unknown: go to 2.
 *     2b. case not packable: fix z_C to 0 -> skip
 *  3. fix all unverified circular patterns to 0
 *
 *  Note that after step 3. the dual bound is not valid anymore.
 */
static
SCIP_RETCODE enforceCons(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< solution (NULL for LP solution) */
   SCIP_RESULT*          result              /**< pointer to store the result */
   )
{
   SCIP_PROBDATA* probdata;
   SCIP_PATTERN** cpatterns;
   SCIP_VAR** cvars;
   int ncpatterns;
   int p;

#ifdef SCIP_DEBUG
      SCIPdebugMsg(scip, "enforce solution:\n");
      SCIP_CALL( SCIPprintSol(scip, sol, NULL, TRUE) );
#endif

   *result = SCIP_FEASIBLE;

   /* (1.) check whether the solution is already feasible */
   if( isSolFeasible(scip, sol) )
      return SCIP_OKAY;

   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   /* get circular pattern information */
   SCIPprobdataGetCInfos(probdata, &cpatterns, &cvars, &ncpatterns);
   assert(cpatterns != NULL);
   assert(cvars != NULL);
   assert(ncpatterns > 0);

   /* (2.) try to verify a pattern */
   for( p = 0; p < ncpatterns; ++p )
   {
      SCIP_Real solval;
      SCIP_Bool infeasible;
      SCIP_Bool success;

      assert(cpatterns[p] != NULL);
      assert(cvars[p] != NULL);

      solval = SCIPgetSolVal(scip, sol, cvars[p]);

      /* skip packable and unused circular patterns */
      if( SCIPpatternGetPackableStatus(cpatterns[p]) == SCIP_PACKABLE_YES || SCIPisFeasZero(scip, solval) )
         continue;

      assert(SCIPpatternGetPackableStatus(cpatterns[p]) == SCIP_PACKABLE_UNKNOWN);

      /* TODO verify pattern */
      SCIPpatternSetPackableStatus(cpatterns[p], SCIP_PACKABLE_NO);

      /* (2a.) fix corresponding variable to zero if pattern is not packable */
      if( SCIPpatternGetPackableStatus(cpatterns[p]) == SCIP_PACKABLE_NO )
      {
         SCIP_CALL( SCIPfixVar(scip, cvars[p], 0, &infeasible, &success) );
         SCIPdebugMsg(scip, "fix pattern %d\n", p);
         assert(success);
         assert(!infeasible);
         *result = SCIP_REDUCEDDOM;
         return SCIP_OKAY;
      }
   }

   SCIPdebugMsg(scip, "fix all unverified circular patterns\n");

   /* (3.) fix all unverified patterns */
   for( p = 0; p < ncpatterns; ++p )
   {
      if( SCIPpatternGetPackableStatus(cpatterns[p]) == SCIP_PACKABLE_UNKNOWN )
      {
         SCIP_Bool success;
         SCIP_Bool infeasible;

         SCIP_CALL( SCIPfixVar(scip, cvars[p], 0, &infeasible, &success) );
         SCIPdebugMsg(scip, "fix pattern %d (success=%u)\n", p, success);
         assert(!infeasible);

         if( success )
            *result = SCIP_REDUCEDDOM;
      }
   }

   return SCIP_OKAY;
}

/*
 * Callback methods of constraint handler
 */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
#if 0
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define conshdlrCopyRpa NULL
#endif


/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeRpa)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata = SCIPconshdlrGetData(conshdlr);

   if( conshdlrdata->locked != NULL )
   {
      SCIPfreeBlockMemoryArray(scip, &conshdlrdata->locked, conshdlrdata->lockedzise);
      conshdlrdata->lockedzise = 0;
   }

   SCIPfreeBlockMemory(scip, &conshdlrdata);

   return SCIP_OKAY;
}


/** initialization method of constraint handler (called after problem was transformed) */
#if 0
static
SCIP_DECL_CONSINIT(consInitRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitRpa NULL
#endif


/** deinitialization method of constraint handler (called before transformed problem is freed) */
#if 0
static
SCIP_DECL_CONSEXIT(consExitRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitRpa NULL
#endif


/** presolving initialization method of constraint handler (called when presolving is about to begin) */
#if 0
static
SCIP_DECL_CONSINITPRE(consInitpreRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitpreRpa NULL
#endif


/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
#if 0
static
SCIP_DECL_CONSEXITPRE(consExitpreRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitpreRpa NULL
#endif


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
#if 0
static
SCIP_DECL_CONSINITSOL(consInitsolRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitsolRpa NULL
#endif


/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
#if 0
static
SCIP_DECL_CONSEXITSOL(consExitsolRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitsolRpa NULL
#endif


/** frees specific constraint data */
#if 0
static
SCIP_DECL_CONSDELETE(consDeleteRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeleteRpa NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */
#if 0
static
SCIP_DECL_CONSTRANS(consTransRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consTransRpa NULL
#endif


/** LP initialization method of constraint handler (called before the initial LP relaxation at a node is solved) */
#if 0
static
SCIP_DECL_CONSINITLP(consInitlpRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitlpRpa NULL
#endif


/** separation method of constraint handler for LP solutions */
#if 0
static
SCIP_DECL_CONSSEPALP(consSepalpRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consSepalpRpa NULL
#endif


/** separation method of constraint handler for arbitrary primal solutions */
#if 0
static
SCIP_DECL_CONSSEPASOL(consSepasolRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consSepasolRpa NULL
#endif


/** constraint enforcing method of constraint handler for LP solutions */
static
SCIP_DECL_CONSENFOLP(consEnfolpRpa)
{  /*lint --e{715}*/
   SCIP_CALL( enforceCons(scip, NULL, result) );

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for relaxation solutions */
static
SCIP_DECL_CONSENFORELAX(consEnforelaxRpa)
{  /*lint --e{715}*/
   SCIP_CALL( enforceCons(scip, sol, result) );

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
SCIP_DECL_CONSENFOPS(consEnfopsRpa)
{  /*lint --e{715}*/
   SCIP_CALL( enforceCons(scip, NULL, result) );

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
SCIP_DECL_CONSCHECK(consCheckRpa)
{  /*lint --e{715}*/
   *result = isSolFeasible(scip, sol) ? SCIP_FEASIBLE : SCIP_INFEASIBLE;

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 0
static
SCIP_DECL_CONSPROP(consPropRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPropRpa NULL
#endif


/** presolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSPRESOL(consPresolRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPresolRpa NULL
#endif


/** propagation conflict resolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSRESPROP(consRespropRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consRespropRpa NULL
#endif


/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockRpa)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_PROBDATA* probdata;
   SCIP_PATTERN** cpatterns;
   SCIP_VAR** cvars;
   SCIP_Bool first;
   int ncpatterns;
   int p;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   probdata = SCIPgetProbData(scip);
   assert(probdata != NULL);

   /* get circular patterns and corresponding variables */
   SCIPprobdataGetCInfos(probdata, &cpatterns, &cvars, &ncpatterns);
   assert(cpatterns != NULL);
   assert(cvars != NULL);
   assert(ncpatterns > 0);

   /* remember whether we have locked the variables for the first time */
   if( conshdlrdata->locked == NULL )
   {
      first = TRUE;
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &conshdlrdata->locked, ncpatterns) );
      BMSclearMemoryArray(conshdlrdata->locked, ncpatterns);
      conshdlrdata->lockedzise = ncpatterns;
   }
   else
      first = FALSE;

   /* lock all unverified circular patterns */
   for( p = 0; p < ncpatterns; ++p )
   {
      assert(cpatterns[p] != NULL);
      assert(cvars[p] != NULL);

      if( first && SCIPpatternGetPackableStatus(cpatterns[p]) == SCIP_PACKABLE_UNKNOWN )
      {
         assert(!conshdlrdata->locked[p]);

         SCIP_CALL( SCIPaddVarLocks(scip, cvars[p], nlocksneg + nlockspos, nlocksneg + nlockspos) );
         conshdlrdata->locked[p] = TRUE;
         SCIPdebugMsg(scip, "lock %s\n", SCIPvarGetName(cvars[p]));
      }
      else if( !first && conshdlrdata->locked[p] )
      {
         conshdlrdata->locked[p] = FALSE;
         SCIPdebugMsg(scip, "unlock %s\n", SCIPvarGetName(cvars[p]));
      }
   }

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSACTIVE(consActiveRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consActiveRpa NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDEACTIVE(consDeactiveRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeactiveRpa NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSENABLE(consEnableRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consEnableRpa NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDISABLE(consDisableRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDisableRpa NULL
#endif

/** variable deletion of constraint handler */
#if 0
static
SCIP_DECL_CONSDELVARS(consDelvarsRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDelvarsRpa NULL
#endif


/** constraint display method of constraint handler */
#if 0
static
SCIP_DECL_CONSPRINT(consPrintRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPrintRpa NULL
#endif


/** constraint copying method of constraint handler */
#if 0
static
SCIP_DECL_CONSCOPY(consCopyRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consCopyRpa NULL
#endif


/** constraint parsing method of constraint handler */
#if 0
static
SCIP_DECL_CONSPARSE(consParseRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consParseRpa NULL
#endif


/** constraint method of constraint handler which returns the variables (if possible) */
#if 0
static
SCIP_DECL_CONSGETVARS(consGetVarsRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetVarsRpa NULL
#endif

/** constraint method of constraint handler which returns the number of variables (if possible) */
#if 0
static
SCIP_DECL_CONSGETNVARS(consGetNVarsRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetNVarsRpa NULL
#endif

/** constraint handler method to suggest dive bound changes during the generic diving algorithm */
#if 0
static
SCIP_DECL_CONSGETDIVEBDCHGS(consGetDiveBdChgsRpa)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of rpa constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetDiveBdChgsRpa NULL
#endif


/*
 * constraint specific interface methods
 */

/** creates the handler for ringpacking */
SCIP_RETCODE SCIPincludeConshdlrRpa(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSHDLR* conshdlr = NULL;

   SCIP_CALL( SCIPallocBlockMemory(scip, &conshdlrdata) );
   BMSclearMemory(conshdlrdata);

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlrBasic(scip, &conshdlr, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY, CONSHDLR_EAGERFREQ, CONSHDLR_NEEDSCONS,
         consEnfolpRpa, consEnfopsRpa, consCheckRpa, consLockRpa,
         conshdlrdata) );
   assert(conshdlr != NULL);

   /* set non-fundamental callbacks via specific setter functions */
   SCIP_CALL( SCIPsetConshdlrActive(scip, conshdlr, consActiveRpa) );
   SCIP_CALL( SCIPsetConshdlrCopy(scip, conshdlr, conshdlrCopyRpa, consCopyRpa) );
   SCIP_CALL( SCIPsetConshdlrDeactive(scip, conshdlr, consDeactiveRpa) );
   SCIP_CALL( SCIPsetConshdlrDelete(scip, conshdlr, consDeleteRpa) );
   SCIP_CALL( SCIPsetConshdlrDelvars(scip, conshdlr, consDelvarsRpa) );
   SCIP_CALL( SCIPsetConshdlrDisable(scip, conshdlr, consDisableRpa) );
   SCIP_CALL( SCIPsetConshdlrEnable(scip, conshdlr, consEnableRpa) );
   SCIP_CALL( SCIPsetConshdlrExit(scip, conshdlr, consExitRpa) );
   SCIP_CALL( SCIPsetConshdlrExitpre(scip, conshdlr, consExitpreRpa) );
   SCIP_CALL( SCIPsetConshdlrExitsol(scip, conshdlr, consExitsolRpa) );
   SCIP_CALL( SCIPsetConshdlrFree(scip, conshdlr, consFreeRpa) );
   SCIP_CALL( SCIPsetConshdlrGetDiveBdChgs(scip, conshdlr, consGetDiveBdChgsRpa) );
   SCIP_CALL( SCIPsetConshdlrGetVars(scip, conshdlr, consGetVarsRpa) );
   SCIP_CALL( SCIPsetConshdlrGetNVars(scip, conshdlr, consGetNVarsRpa) );
   SCIP_CALL( SCIPsetConshdlrInit(scip, conshdlr, consInitRpa) );
   SCIP_CALL( SCIPsetConshdlrInitpre(scip, conshdlr, consInitpreRpa) );
   SCIP_CALL( SCIPsetConshdlrInitsol(scip, conshdlr, consInitsolRpa) );
   SCIP_CALL( SCIPsetConshdlrInitlp(scip, conshdlr, consInitlpRpa) );
   SCIP_CALL( SCIPsetConshdlrParse(scip, conshdlr, consParseRpa) );
   SCIP_CALL( SCIPsetConshdlrPresol(scip, conshdlr, consPresolRpa, CONSHDLR_MAXPREROUNDS, CONSHDLR_PRESOLTIMING) );
   SCIP_CALL( SCIPsetConshdlrPrint(scip, conshdlr, consPrintRpa) );
   SCIP_CALL( SCIPsetConshdlrProp(scip, conshdlr, consPropRpa, CONSHDLR_PROPFREQ, CONSHDLR_DELAYPROP,
         CONSHDLR_PROP_TIMING) );
   SCIP_CALL( SCIPsetConshdlrResprop(scip, conshdlr, consRespropRpa) );
   SCIP_CALL( SCIPsetConshdlrSepa(scip, conshdlr, consSepalpRpa, consSepasolRpa, CONSHDLR_SEPAFREQ, CONSHDLR_SEPAPRIORITY, CONSHDLR_DELAYSEPA) );
   SCIP_CALL( SCIPsetConshdlrTrans(scip, conshdlr, consTransRpa) );
   SCIP_CALL( SCIPsetConshdlrEnforelax(scip, conshdlr, consEnforelaxRpa) );

   /* add constraint handler parameters */

   return SCIP_OKAY;
}
