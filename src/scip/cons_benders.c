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

/**@file   cons_benders.c
 * @brief  constraint handler for benders decomposition
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include "scip/scip.h"

#include "cons_benders.h"


/* fundamental constraint handler properties */
#define CONSHDLR_NAME          "benders"
#define CONSHDLR_DESC          "constraint handler to execute Benders' Decomposition"
#define CONSHDLR_ENFOPRIORITY    10000 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY   10000 /**< priority of the constraint handler for checking feasibility */
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


#define DEFAULT_USEHEURSOLVING           FALSE      /**< should heuristic solving be used */
#define DEFAULT_ABORTSOLVINGBOUND        FALSE      /**< should the subproblem solve be aborted when exceeding the upper bound */
#define DEFAULT_DISPINFOS                FALSE      /**< should additional information be displayed */
#define DEFAULT_DISABLECUTOFF            2          /**< should the cutoffbound be applied in master LP solving? (0: on, 1:off, 2:auto) */
#define DEFAULT_SORTING                  2          /**< default sorting method for pricing mips
                                                     *    0 :   order of pricing problems
                                                     *    1 :   according to dual solution of convexity constraint
                                                     *    2 :   according to reliability from previous round)
                                                     */
#define DEFAULT_THREADS                  0          /**< number of threads (0 is OpenMP default) */
#define DEFAULT_EAGERFREQ                10         /**< frequency at which all pricingproblems should be solved (0 to disable) */


/* TODO: (optional) enable linear or nonlinear constraint upgrading */
#if 0
#include "scip/cons_linear.h"
#include "scip/cons_nonlinear.h"
#define LINCONSUPGD_PRIORITY          0 /**< priority of the constraint handler for upgrading of linear constraints */
#define NONLINCONSUPGD_PRIORITY       0 /**< priority of the constraint handler for upgrading of nonlinear constraints */
#endif

#define SUBPROBLEM_STAT_ARRAYLEN_TIME 1024                /**< length of the array for Time histogram representation */
#define SUBPROBLEM_STAT_BUCKETSIZE_TIME 10                /**< size of the buckets for Time histogram representation */
#define SUBPROBLEM_STAT_ARRAYLEN_CUTS 1024                /**< length of the array for foundVars histogram representation */
#define SUBPROBLEM_STAT_BUCKETSIZE_CUTS 1                 /**< size of the buckets for foundVars histogram representation */

/*
 * Data structures
 */

/* TODO: fill in the necessary constraint data */

/** constraint data for benders constraints */
//struct SCIP_ConsData
//{
//};

/** constraint handler data */
struct SCIP_ConshdlrData
{
   SCIP_BENDERS**        benders;            /**< the Benders' decomposition structures */
   int                   nbenders;           /**< the number of Benders' decomposition structures */
   //SCIP_VAR**            auxiliaryvars;      /**< the auxiliary variables added to the master problem */
   int                   ncalls;             /**< the number of calls to the constraint handler. */

   //SCIP_SOL**            subproblemsols;     /**< the solutions from the subproblem. Used to create an original problem solution */

   /** parameter values */
   SCIP_Bool             dispinfos;          /**< should subproblem solving information be displayed? */
   int                   disablecutoff;      /**< should the cutoffbound be applied in master LP solving (0: on, 1:off, 2:auto)? */
   int                   eagerfreq;          /**< frequency at which all pricingproblems should be solved */
   int                   threads;            /**< the number of threads used to solve the subproblems */

   /** statistics */
   int                   eagerage;           /**< iterations since last eager iteration */
};


/*
 * Local methods
 */

#if 0
/* applies the generated cut to the master problem*/
static
SCIP_RETCODE applyCut(
   //SCIP_BENDERS_CUTTYPE  cuttype
   )
{
   return SCIP_OKAY;
}

/* apply Magnanti-Wong strengthening of the dual solutions from the optimal LP */
static
SCIP_RETCODE applyMagnantiWongDualStrengthening(
   )
{
   return SCIP_OKAY;
}
#endif


/*
 * Callback methods of constraint handler
 */

/* TODO: Implement all necessary constraint handler methods. The methods with #if 0 ... #else #define ... are optional */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
#if 0
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define conshdlrCopyBenders NULL
#endif

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert(scip != NULL);
   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   /* free memory for conshdlrdata*/
   if( conshdlrdata != NULL )
   {
      SCIPfreeMemory(scip, &conshdlrdata);
   }

   return SCIP_OKAY;
}


/** initialization method of constraint handler (called after problem was transformed) */
#if 0
static
SCIP_DECL_CONSINIT(consInitBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   /* A possible implementation from gcg_pricer.cpp */
   assert(scip == scip_);
   assert(reducedcostpricing != NULL);
   assert(farkaspricing != NULL);

   SCIP_CALL( solversInit() );

   SCIP_CALL( reducedcostpricing->resetCalls() );
   SCIP_CALL( farkaspricing->resetCalls() );

   return SCIP_OKAY;
}
#else
#define consInitBenders NULL
#endif


/** deinitialization method of constraint handler (called before transformed problem is freed) */
#if 0
static
SCIP_DECL_CONSEXIT(consExitBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitBenders NULL
#endif


/** presolving initialization method of constraint handler (called when presolving is about to begin) */
#if 0
static
SCIP_DECL_CONSINITPRE(consInitpreBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitpreBenders NULL
#endif


/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
#if 0
static
SCIP_DECL_CONSEXITPRE(consExitpreBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitpreBenders NULL
#endif


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   conshdlrdata->ncalls = 0;
   conshdlrdata->eagerage = 0;

   return SCIP_OKAY;
}


#define SCIP_DECL_CONSEXITSOL(x) SCIP_RETCODE x (SCIP* scip, SCIP_CONSHDLR* conshdlr, SCIP_CONS** conss, int nconss, SCIP_Bool restart)
/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
static
SCIP_DECL_CONSEXITSOL(consExitsolBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int i;

   assert(scip != NULL);
   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   return SCIP_OKAY;
}


/** frees specific constraint data */
#if 0
static
SCIP_DECL_CONSDELETE(consDeleteBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeleteBenders NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */
#if 0
static
SCIP_DECL_CONSTRANS(consTransBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consTransBenders NULL
#endif


/** LP initialization method of constraint handler (called before the initial LP relaxation at a node is solved) */
#if 0
static
SCIP_DECL_CONSINITLP(consInitlpBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitlpBenders NULL
#endif


/** separation method of constraint handler for LP solutions */
#if 0
static
SCIP_DECL_CONSSEPALP(consSepalpBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consSepalpBenders NULL
#endif


/** separation method of constraint handler for arbitrary primal solutions */
#if 0
static
SCIP_DECL_CONSSEPASOL(consSepasolBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consSepasolBenders NULL
#endif


//#define SCIP_DECL_CONSENFOLP(x) SCIP_RETCODE x (SCIP* scip, SCIP_CONSHDLR* conshdlr, SCIP_CONS** conss, int nconss, int nusefulconss, \
      //SCIP_Bool solinfeasible, SCIP_RESULT* result)
/** constraint enforcing method of constraint handler for LP solutions */
static
SCIP_DECL_CONSENFOLP(consEnfolpBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int i;

   SCIP_Bool infeasible;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(result != NULL);

   (*result) = SCIP_FEASIBLE;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   for( i = 0; i < conshdlrdata->nbenders; i++ )
   {
      if( SCIPbendersCutLP(conshdlrdata->benders[i]) )
      {
         SCIP_CALL( SCIPsolveBendersSubproblems(scip, conshdlrdata->benders[i], NULL, result, FALSE) );
      }
   }

   return SCIP_OKAY;
}


//#define SCIP_DECL_CONSENFORELAX(x) SCIP_RETCODE x (SCIP* scip, SCIP_SOL* sol, SCIP_CONSHDLR* conshdlr, SCIP_CONS** conss, int nconss, int nusefulconss, \
      //SCIP_Bool solinfeasible, SCIP_RESULT* result)
/** constraint enforcing method of constraint handler for relaxation solutions */
static
SCIP_DECL_CONSENFORELAX(consEnforelaxBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int i;

   SCIP_Bool infeasible;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(result != NULL);

   (*result) = SCIP_FEASIBLE;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   for( i = 0; i < conshdlrdata->nbenders; i++ )
   {
      if( SCIPbendersCutLP(conshdlrdata->benders[i]) )
      {
         SCIP_CALL( SCIPsolveBendersSubproblems(scip, conshdlrdata->benders[i], NULL, result, FALSE) );
      }
   }

   return SCIP_OKAY;
}


//#define SCIP_DECL_CONSENFOPS(x) SCIP_RETCODE x (SCIP* scip, SCIP_CONSHDLR* conshdlr, SCIP_CONS** conss, int nconss, int nusefulconss, \
      //SCIP_Bool solinfeasible, SCIP_Bool objinfeasible, SCIP_RESULT* result)
/** constraint enforcing method of constraint handler for pseudo solutions */
static
SCIP_DECL_CONSENFOPS(consEnfopsBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int i;

   SCIP_Bool infeasible;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(result != NULL);

   (*result) = SCIP_FEASIBLE;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   for( i = 0; i < conshdlrdata->nbenders; i++ )
   {
      if( SCIPbendersCutLP(conshdlrdata->benders[i]) )
      {
         SCIP_CALL( SCIPsolveBendersSubproblems(scip, conshdlrdata->benders[i], NULL, result, FALSE) );
      }
   }

   return SCIP_OKAY;
}


/* The define is kept as a comment so I know what is being passed to this function */
#if 0
#define SCIP_DECL_CONSCHECK(x) SCIP_RETCODE x (SCIP* scip, SCIP_CONSHDLR* conshdlr, SCIP_CONS** conss, int nconss, SCIP_SOL* sol, \
      SCIP_Bool checkintegrality, SCIP_Bool checklprows, SCIP_Bool printreason, SCIP_RESULT* result)
#endif
/** feasibility check method of constraint handler for integral solutions */
static
SCIP_DECL_CONSCHECK(consCheckBenders)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int i;

   SCIP_Bool infeasible;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(result != NULL);

   (*result) = SCIP_FEASIBLE;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   for( i = 0; i < conshdlrdata->nbenders; i++ )
   {
      if( SCIPbendersCutLP(conshdlrdata->benders[i]) )
      {
         SCIP_CALL( SCIPsolveBendersSubproblems(scip, conshdlrdata->benders[i], NULL, result, TRUE) );
      }

      /* if the result is infeasible, it is not necessary to check any more subproblems. */
      if( (*result) == SCIP_INFEASIBLE )
         break;
   }

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 0
static
SCIP_DECL_CONSPROP(consPropBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPropBenders NULL
#endif


/** presolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSPRESOL(consPresolBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPresolBenders NULL
#endif


/** propagation conflict resolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSRESPROP(consRespropBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consRespropBenders NULL
#endif


/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockBenders)
{  /*lint --e{715}*/
   //SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   //SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSACTIVE(consActiveBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consActiveBenders NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDEACTIVE(consDeactiveBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeactiveBenders NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSENABLE(consEnableBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consEnableBenders NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDISABLE(consDisableBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDisableBenders NULL
#endif

/** variable deletion of constraint handler */
#if 0
static
SCIP_DECL_CONSDELVARS(consDelvarsBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDelvarsBenders NULL
#endif


/** constraint display method of constraint handler */
#if 0
static
SCIP_DECL_CONSPRINT(consPrintBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPrintBenders NULL
#endif


/** constraint copying method of constraint handler */
#if 0
static
SCIP_DECL_CONSCOPY(consCopyBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consCopyBenders NULL
#endif


/** constraint parsing method of constraint handler */
#if 0
static
SCIP_DECL_CONSPARSE(consParseBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consParseBenders NULL
#endif


/** constraint method of constraint handler which returns the variables (if possible) */
#if 0
static
SCIP_DECL_CONSGETVARS(consGetVarsBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetVarsBenders NULL
#endif

/** constraint method of constraint handler which returns the number of variables (if possible) */
#if 0
static
SCIP_DECL_CONSGETNVARS(consGetNVarsBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetNVarsBenders NULL
#endif

/** constraint handler method to suggest dive bound changes during the generic diving algorithm */
#if 0
static
SCIP_DECL_CONSGETDIVEBDCHGS(consGetDiveBdChgsBenders)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consGetDiveBdChgsBenders NULL
#endif


/*
 * constraint specific interface methods
 */

/** creates the handler for benders constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 origprob            /**< SCIP data structure of the original problem */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata = NULL;
   SCIP_CONSHDLR* conshdlr;

   /* create benders constraint handler data */
   conshdlrdata = NULL;

   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );
   conshdlrdata->origprob = origprob;
   conshdlrdata->solvers = NULL;
   conshdlrdata->nsolvers = 0;
   conshdlrdata->nodetimehist = NULL;
   conshdlrdata->optimalitycutshist = NULL;
   conshdlrdata->feasibilitycutshist = NULL;

   conshdlr = NULL;

   /* include constraint handler */
#if 0
   /* use SCIPincludeConshdlr() if you want to set all callbacks explicitly and realize (by getting compiler errors) when
    * new callbacks are added in future SCIP versions
    */
   SCIP_CALL( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS,
         CONSHDLR_DELAYSEPA, CONSHDLR_DELAYPROP, CONSHDLR_NEEDSCONS,
         CONSHDLR_PROP_TIMING, CONSHDLR_PRESOLTIMING,
         conshdlrCopyBenders,
         consFreeBenders, consInitBenders, consExitBenders,
         consInitpreBenders, consExitpreBenders, consInitsolBenders, consExitsolBenders,
         consDeleteBenders, consTransBenders, consInitlpBenders,
         consSepalpBenders, consSepasolBenders, consEnfolpBenders, consEnforelaxBenders, consEnfopsBenders, consCheckBenders,
         consPropBenders, consPresolBenders, consRespropBenders, consLockBenders,
         consActiveBenders, consDeactiveBenders,
         consEnableBenders, consDisableBenders, consDelvarsBenders,
         consPrintBenders, consCopyBenders, consParseBenders,
         consGetVarsBenders, consGetNVarsBenders, consGetDiveBdChgsBenders, conshdlrdata) );
#else
   /* use SCIPincludeConshdlrBasic() plus setter functions if you want to set callbacks one-by-one and your code should
    * compile independent of new callbacks being added in future SCIP versions
    */
   SCIP_CALL( SCIPincludeConshdlrBasic(scip, &conshdlr, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY, CONSHDLR_EAGERFREQ, CONSHDLR_NEEDSCONS,
         consEnfolpBenders, consEnfopsBenders, consCheckBenders, consLockBenders,
         conshdlrdata) );
   assert(conshdlr != NULL);

   /* set non-fundamental callbacks via specific setter functions */
   SCIP_CALL( SCIPsetConshdlrActive(scip, conshdlr, consActiveBenders) );
   SCIP_CALL( SCIPsetConshdlrCopy(scip, conshdlr, conshdlrCopyBenders, consCopyBenders) );
   SCIP_CALL( SCIPsetConshdlrDeactive(scip, conshdlr, consDeactiveBenders) );
   SCIP_CALL( SCIPsetConshdlrDelete(scip, conshdlr, consDeleteBenders) );
   SCIP_CALL( SCIPsetConshdlrDelvars(scip, conshdlr, consDelvarsBenders) );
   SCIP_CALL( SCIPsetConshdlrDisable(scip, conshdlr, consDisableBenders) );
   SCIP_CALL( SCIPsetConshdlrEnable(scip, conshdlr, consEnableBenders) );
   SCIP_CALL( SCIPsetConshdlrExit(scip, conshdlr, consExitBenders) );
   SCIP_CALL( SCIPsetConshdlrExitpre(scip, conshdlr, consExitpreBenders) );
   SCIP_CALL( SCIPsetConshdlrExitsol(scip, conshdlr, consExitsolBenders) );
   SCIP_CALL( SCIPsetConshdlrFree(scip, conshdlr, consFreeBenders) );
   SCIP_CALL( SCIPsetConshdlrGetDiveBdChgs(scip, conshdlr, consGetDiveBdChgsBenders) );
   SCIP_CALL( SCIPsetConshdlrGetVars(scip, conshdlr, consGetVarsBenders) );
   SCIP_CALL( SCIPsetConshdlrGetNVars(scip, conshdlr, consGetNVarsBenders) );
   SCIP_CALL( SCIPsetConshdlrInit(scip, conshdlr, consInitBenders) );
   SCIP_CALL( SCIPsetConshdlrInitpre(scip, conshdlr, consInitpreBenders) );
   SCIP_CALL( SCIPsetConshdlrInitsol(scip, conshdlr, consInitsolBenders) );
   SCIP_CALL( SCIPsetConshdlrInitlp(scip, conshdlr, consInitlpBenders) );
   SCIP_CALL( SCIPsetConshdlrParse(scip, conshdlr, consParseBenders) );
   SCIP_CALL( SCIPsetConshdlrPresol(scip, conshdlr, consPresolBenders, CONSHDLR_MAXPREROUNDS, CONSHDLR_PRESOLTIMING) );
   SCIP_CALL( SCIPsetConshdlrPrint(scip, conshdlr, consPrintBenders) );
   SCIP_CALL( SCIPsetConshdlrProp(scip, conshdlr, consPropBenders, CONSHDLR_PROPFREQ, CONSHDLR_DELAYPROP,
         CONSHDLR_PROP_TIMING) );
   SCIP_CALL( SCIPsetConshdlrResprop(scip, conshdlr, consRespropBenders) );
   SCIP_CALL( SCIPsetConshdlrSepa(scip, conshdlr, consSepalpBenders, consSepasolBenders, CONSHDLR_SEPAFREQ, CONSHDLR_SEPAPRIORITY, CONSHDLR_DELAYSEPA) );
   SCIP_CALL( SCIPsetConshdlrTrans(scip, conshdlr, consTransBenders) );
   SCIP_CALL( SCIPsetConshdlrEnforelax(scip, conshdlr, consEnforelaxBenders) );

#endif


   /* parameters for the Benders' decomposition constraint handler */
   SCIP_CALL( SCIPaddBoolParam(origprob, "benders/subproblem/useheursolving",
         "should subproblem solving be performed heuristically before solving the LPs to optimality?",
         &conshdlrdata->useheursolving, TRUE, DEFAULT_USEHEURSOLVING, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(origprob, "benders/subproblem/abortsolvingbound",
         "should solving be aborted when the objective function is less than the current upper bound?",
         &conshdlrdata->abortsolvebound, TRUE, DEFAULT_ABORTSOLVINGBOUND, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(origprob, "benders/subproblem/dispinfos",
         "should additional informations concerning the subproblem solving process be displayed?",
         &conshdlrdata->dispinfos, FALSE, DEFAULT_DISPINFOS, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(origprob, "benders/subproblem/sorting",
         "which sorting method should be used to sort the subproblems problems (0 = order of pricing problems, 1 = according to dual solution of convexity constraint, 2 = according to reliability from previous round)",
         &conshdlrdata->sorting, FALSE, DEFAULT_SORTING, 0, 5, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(origprob, "benders/subproblem/threads",
         "how many threads should be used to concurrently solve the subprolems (0 to guess threads by OpenMP)",
         &conshdlrdata->threads, FALSE, DEFAULT_THREADS, 0, 4096, NULL, NULL) );

   //SCIP_CALL( SCIPsetIntParam(scip, "lp/disablecutoff", DEFAULT_DISABLECUTOFF) );
   //SCIP_CALL( SCIPaddIntParam(origprob, "benders/subproblem/disablecutoff",
         //"should the cutoffbound be applied in master LP solving (0: on, 1:off, 2:auto)?",
         //&conshdlrdata->disablecutoff, FALSE, DEFAULT_DISABLECUTOFF, 0, 2, paramChgdDisablecutoff, NULL) );

   SCIP_CALL( SCIPaddIntParam(origprob, "benders/subproblem/eagerfreq",
            "frequency at which all subproblems should be solved (0 to disable)",
            &conshdlrdata->eagerfreq, FALSE, DEFAULT_EAGERFREQ, 0, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}

/** creates and captures a benders constraint
 *
 *  @note the constraint gets captured, hence at one point you have to release it using the method SCIPreleaseCons()
 */
SCIP_RETCODE SCIPcreateConsBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   int                   nvars,              /**< number of variables in the constraint */
   SCIP_VAR**            vars,               /**< array with variables of constraint entries */
   SCIP_Real*            coefs,              /**< array with coefficients of constraint entries */
   SCIP_Real             lhs,                /**< left hand side of constraint */
   SCIP_Real             rhs,                /**< right hand side of constraint */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP?
                                              *   Usually set to TRUE. Set to FALSE for 'lazy constraints'. */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             local,              /**< is constraint only valid locally?
                                              *   Usually set to FALSE. Has to be set to TRUE, e.g., for branching constraints. */
   SCIP_Bool             modifiable,         /**< is constraint modifiable (subject to column generation)?
                                              *   Usually set to FALSE. In column generation applications, set to TRUE if pricing
                                              *   adds coefficients to this constraint. */
   SCIP_Bool             dynamic,            /**< is constraint subject to aging?
                                              *   Usually set to FALSE. Set to TRUE for own cuts which
                                              *   are separated as constraints. */
   SCIP_Bool             removable,          /**< should the relaxation be removed from the LP due to aging or cleanup?
                                              *   Usually set to FALSE. Set to TRUE for 'lazy constraints' and 'user cuts'. */
   SCIP_Bool             stickingatnode      /**< should the constraint always be kept at the node where it was added, even
                                              *   if it may be moved to a more global node?
                                              *   Usually set to FALSE. Set to TRUE to for constraints that represent node data. */
   )
{
   /* TODO: (optional) modify the definition of the SCIPcreateConsBenders() call, if you don't need all the information */

   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSDATA* consdata;

   SCIPerrorMessage("method of benders constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527} --e{715}*/

   /* find the benders constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("benders constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint data */
   consdata = NULL;
   /* TODO: create and store constraint specific data here */

   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
         local, modifiable, dynamic, removable, stickingatnode) );

   return SCIP_OKAY;
}

/** creates and captures a benders constraint with all its constraint flags set to their
 *  default values
 *
 *  @note the constraint gets captured, hence at one point you have to release it using the method SCIPreleaseCons()
 */
SCIP_RETCODE SCIPcreateConsBasicBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   int                   nvars,              /**< number of variables in the constraint */
   SCIP_VAR**            vars,               /**< array with variables of constraint entries */
   SCIP_Real*            coefs,              /**< array with coefficients of constraint entries */
   SCIP_Real             lhs,                /**< left hand side of constraint */
   SCIP_Real             rhs                 /**< right hand side of constraint */
   )
{
   SCIP_CALL( SCIPcreateConsBenders(scip, cons, name, nvars, vars, coefs, lhs, rhs,
         TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE) );

   return SCIP_OKAY;
}
