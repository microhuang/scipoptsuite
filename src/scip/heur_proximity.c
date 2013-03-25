/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
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

/**@file   heur_proximity.c
 * @brief  improvement heuristic which uses an auxiliary objective instead of the original objective function which
 *         is itself added as a constraint to a sub-SCIP instance. The heuristic was presented by Matteo Fischetti
 *         and Michele Monaci
 *
 *
 * @author Gregor Hendel
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/heur_proximity.h"
#include "scip/cons_linear.h"

#define HEUR_NAME             "proximity"
#define HEUR_DESC             "heuristic trying to improve the incumbent by an auxiliary proximity objective function"
#define HEUR_DISPCHAR         'P'
#define HEUR_PRIORITY         -2000000
#define HEUR_FREQ             -1
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERNODE
#define HEUR_USESSUBSCIP      TRUE  /**< does the heuristic use a secondary SCIP instance? */

/* event handler properties */
#define EVENTHDLR_NAME         "Proximity"
#define EVENTHDLR_DESC         "LP event handler for "HEUR_NAME" heuristic"

/* default values for proximity-specific parameters */
/* todo refine these values */
#define DEFAULT_MAXNODES      10000LL    /* maximum number of nodes to regard in the subproblem                        */
#define DEFAULT_MINIMPROVE    0.25       /* factor by which proximity should at least improve the incumbent            */
#define DEFAULT_MINGAP        0.01       /* minimum primal-dual gap for which the heuristic is executed                */
#define DEFAULT_MINNODES      1LL        /* minimum number of nodes to regard in the subproblem                        */
#define DEFAULT_MINLPITERS    200LL      /* minimum number of LP iterations to perform in one sub-mip                  */
#define DEFAULT_MAXLPITERS    100000LL   /* maximum number of LP iterations to be performed in the subproblem          */
#define DEFAULT_NODESOFS      50LL       /* number of nodes added to the contingent of the total nodes                 */
#define DEFAULT_WAITINGNODES  100LL      /* default waiting nodes since last incumbent before heuristic is executed    */
#define DEFAULT_NODESQUOT     0.1        /* default quotient of sub-MIP nodes with respect to number of processed nodes*/
/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_Longint          maxnodes;           /**< maximum number of nodes to regard in the subproblem                 */
   SCIP_Longint          minnodes;           /**< minimum number of nodes to regard in the subproblem                 */
   SCIP_Longint          maxlpiters;         /**< maximum number of LP iterations to be performed in the subproblem   */
   SCIP_Longint          nusedlpiters;       /**< number of actually performed LP iterations                          */
   SCIP_Longint          minlpiters;         /**< minimum number of LP iterations to perform in one sub-mip           */
   SCIP_Longint          nodesofs;           /**< number of nodes added to the contingent of the total nodes          */
   SCIP_Longint          usednodes;          /**< nodes already used by proximity in earlier calls                    */
   SCIP_Longint          waitingnodes;       /**< waiting nodes since last incumbent before heuristic is executed     */
   SCIP_Real             minimprove;         /**< factor by which proximity should at least improve the incumbent     */
   SCIP_Real             mingap;             /**< minimum primal-dual gap for which the heuristic is executed         */
   SCIP_Real             nodesquot;          /**< quotient of sub-MIP nodes with respect to number of processed nodes */
   SCIP*                 subscip;            /**< the subscip used by the heuristic                                   */
   SCIP_HASHMAP*         varmapfw;           /**< map between scip variables and subscip variables                    */
   SCIP_VAR**            subvars;            /**< variables in subscip                                                */

   int                   nsubvars;           /**< the number of subvars                                               */
   int                   lastsolidx;         /**< index of last solution on which the heuristic was processed         */
};


/*
 * Local methods
 */

/** creates a new solution for the original problem by copying the solution of the subproblem */
static
SCIP_RETCODE createNewSol(
   SCIP*                 scip,               /**< original SCIP data structure                        */
   SCIP*                 subscip,            /**< SCIP structure of the subproblem                    */
   SCIP_VAR**            subvars,            /**< the variables of the subproblem                     */
   SCIP_HEUR*            heur,               /**< proximity heuristic structure                       */
   SCIP_SOL*             subsol,             /**< solution of the subproblem                          */
   SCIP_Bool*            success             /**< used to store whether new solution was found or not */
   )
{
   SCIP_VAR** vars;                          /* the original problem's variables                */
   int        nvars;                         /* the original problem's number of variables      */
   SCIP_Real* subsolvals;                    /* solution values of the subproblem               */
   SCIP_SOL*  newsol;                        /* solution to be created for the original problem */

   assert(scip != NULL);
   assert(subscip != NULL);
   assert(subvars != NULL);
   assert(subsol != NULL);

   /* get variables' data */
   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL) );

   /* sub-SCIP may have more variables than the number of active (transformed) variables in the main SCIP
    * since constraint copying may have required the copy of variables that are fixed in the main SCIP
    */
   assert(nvars <= SCIPgetNOrigVars(subscip));

   SCIP_CALL( SCIPallocBufferArray(scip, &subsolvals, nvars) );

   /* copy the solution */
   SCIP_CALL( SCIPgetSolVals(subscip, subsol, nvars, subvars, subsolvals) );

   /* create new solution for the original problem */
   SCIP_CALL( SCIPcreateSol(scip, &newsol, heur) );
   SCIP_CALL( SCIPsetSolVals(scip, newsol, nvars, vars, subsolvals) );

   /* try to add new solution to scip and free it immediately */
   SCIP_CALL( SCIPtrySolFree(scip, &newsol, FALSE, TRUE, TRUE, TRUE, success) );

   SCIPfreeBufferArray(scip, &subsolvals);

   return SCIP_OKAY;
}

/** sets solving parameters for the subproblem created by the heuristic */
static
SCIP_RETCODE setupSubproblem(
   SCIP*                 subscip             /**< copied SCIP data structure */
   )
{
   assert(subscip != NULL);

   /* do not abort subproblem on CTRL-C */
   SCIP_CALL( SCIPsetBoolParam(subscip, "misc/catchctrlc", FALSE) );

   /* disable output to console */
   SCIP_CALL( SCIPsetIntParam(subscip, "display/verblevel", 0) );

   /* forbid recursive call of heuristics and separators solving sub-SCIPs */
   SCIP_CALL( SCIPsetSubscipsOff(subscip, TRUE) );

   /* use best dfs node selection */
   if( SCIPfindNodesel(subscip, "dfs") != NULL && !SCIPisParamFixed(subscip, "nodeselection/dfs/stdpriority") )
   {
      SCIP_CALL( SCIPsetIntParam(subscip, "nodeselection/dfs/stdpriority", INT_MAX/4) );
   }

   /* disable expensive presolving
    * todo maybe presolving can be entirely turned off here - parameter???
    */
   SCIP_CALL( SCIPsetPresolving(subscip, SCIP_PARAMSETTING_FAST, TRUE) );

   /* SCIP_CALL( SCIPsetPresolving(scip, SCIP_PARAMSETTING_OFF, TRUE) ); */
   if( !SCIPisParamFixed(subscip, "presolving/maxrounds") )
   {
      SCIP_CALL( SCIPsetIntParam(subscip, "presolving/maxrounds", 50) );
   }

   /* disable cutting plane separation */
   SCIP_CALL( SCIPsetSeparating(subscip, SCIP_PARAMSETTING_OFF, TRUE) );


   /* todo: check branching rule in sub-SCIP */
   if( SCIPfindBranchrule(subscip, "leastinf") != NULL && !SCIPisParamFixed(subscip, "branching/leastinf/priority") )
   {
	   SCIP_CALL( SCIPsetIntParam(subscip, "branching/leastinf/priority", INT_MAX/4) );
   }

   /* disable feasibility pump and fractional diving */
   if( !SCIPisParamFixed(subscip, "heuristics/feaspump/freq") )
   {
      SCIP_CALL( SCIPsetIntParam(subscip, "heuristics/feaspump/freq", -1) );
   }
   if( !SCIPisParamFixed(subscip, "heuristics/fracdiving/freq") )
   {
      SCIP_CALL( SCIPsetIntParam(subscip, "heuristics/fracdiving/freq", -1) );
   }


#ifdef SCIP_DEBUG
   /* for debugging proximity, enable MIP output */
   SCIP_CALL( SCIPsetIntParam(subscip, "display/verblevel", 5) );
   SCIP_CALL( SCIPsetIntParam(subscip, "display/freq", 100000000) );
#endif

   return SCIP_OKAY;
}

/* ---------------- Callback methods of event handler ---------------- */

/* exec the event handler
 *
 * we interrupt the solution process
 */
static
SCIP_DECL_EVENTEXEC(eventExecProximity)
{
   SCIP_HEURDATA* heurdata;

   assert(eventhdlr != NULL);
   assert(eventdata != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(SCIPeventGetType(event) & SCIP_EVENTTYPE_NODESOLVED);

   heurdata = (SCIP_HEURDATA*)eventdata;
   assert(heurdata != NULL);

   /* interrupt solution process of sub-SCIP
    * todo adjust interruption limit */
   if( SCIPgetLPSolstat(scip) == SCIP_LPSOLSTAT_ITERLIMIT || SCIPgetNLPIterations(scip) >= heurdata->maxlpiters )
   {
      SCIP_CALL( SCIPinterruptSolve(scip) );
   }

   return SCIP_OKAY;
}
/* ---------------- Callback methods of primal heuristic ---------------- */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyProximity)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurProximity(scip) );

   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeProximity)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );
   assert( scip != NULL );

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert( heurdata != NULL );

   /* free heuristic data */
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitProximity)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );
   assert( scip != NULL );

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert( heurdata != NULL );

   /* initialize data */
   heurdata->usednodes = 0LL;
   heurdata->lastsolidx = -1;
   heurdata->nusedlpiters = 0LL;
   
   heurdata->subscip = NULL;
   heurdata->varmapfw = NULL;
   heurdata->subvars = NULL;

   heurdata->nsubvars = 0;

   return SCIP_OKAY;
}

/* solution process exiting method of proximity heuristic */
static
SCIP_DECL_HEUREXITSOL(heurExitsolProximity)
{
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );
   assert( scip != NULL );

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert( heurdata != NULL );

   /* free remaining memory from heuristic execution */
   if( heurdata->subscip != NULL )
   {
      assert(heurdata->varmapfw != NULL);
      assert(heurdata->subvars != NULL);

      SCIPfreeBlockMemoryArray(scip, &heurdata->subvars, heurdata->nsubvars);
      SCIPhashmapFree(&heurdata->varmapfw);
      SCIP_CALL( SCIPfree(&heurdata->subscip) );

      heurdata->subscip = NULL;
      heurdata->varmapfw = NULL;
      heurdata->subvars = NULL;
   }

   assert(heurdata->subscip == NULL && heurdata->varmapfw == NULL && heurdata->subvars == NULL);

   return SCIP_OKAY;
}

/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecProximity)
{  /*lint --e{715}*/

   SCIP_HEURDATA* heurdata; /* heuristic's data                            */
   SCIP_Longint nnodes;     /* number of stalling nodes for the subproblem */
   SCIP_Longint nlpiters;   /* lp iteration limit for the subproblem       */
   SCIP_Bool foundsol;

   assert(heur != NULL);
   assert(scip != NULL);
   assert(result != NULL);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* calculate the maximal number of branching nodes until heuristic is aborted */
   /* todo set node limit depending on the variable number */
   if( SCIPgetNNodes(scip) <= 1 )
      nnodes = (SCIP_Longint)SCIPgetNLPBranchCands(scip);
   else
      nnodes = (SCIP_Longint) (heurdata->nodesquot * SCIPgetNNodes(scip));
   nnodes += heurdata->nodesofs;

   /* determine the node and LP iteration limit for the solve of the sub-SCIP */
   nnodes -= heurdata->usednodes;
   nnodes = MIN(nnodes, heurdata->maxnodes);

   nlpiters = 2 * SCIPgetNLPIterations(scip);
   nlpiters = MIN(nlpiters, heurdata->maxlpiters);

   /* check whether we have enough nodes left to call subproblem solving */
   if( nnodes < heurdata->minnodes )
   {
      SCIPdebugMessage("skipping proximity: nnodes=%"SCIP_LONGINT_FORMAT", minnodes=%"SCIP_LONGINT_FORMAT"\n", nnodes, heurdata->minnodes);
      return SCIP_OKAY;
   }

   /* do not run proximity, if the problem does not have an objective function anyway */
   if( SCIPgetNObjVars(scip) == 0 )
   {
      SCIPdebugMessage("skipping proximity: pure feasibility problem anyway\n");
      return SCIP_OKAY;
   }

   foundsol = FALSE;

   do
   {
      /* main loop of proximity: in every iteration, a new subproblem is set up and solved until no improved solution
       * is found or one of the heuristic limits on nodes or LP iterations is hit
       */
      SCIP_Longint nusednodes;
      SCIP_Longint nusedlpiters;

      nusednodes = 0LL;
      nusedlpiters = 0LL;

      nlpiters = MAX(nlpiters, heurdata->minlpiters);

      /* define and solve the proximity subproblem */
      SCIP_CALL( SCIPapplyProximity(scip, heur, result, heurdata->minimprove, nnodes, nlpiters, &nusednodes, &nusedlpiters) );

      /* adjust node limit and LP iteration limit for future iterations */
      assert(nusednodes <= nnodes);
      heurdata->usednodes += nusednodes;
      nnodes -= nusednodes;

      nlpiters -= nusedlpiters;
      heurdata->nusedlpiters += nusedlpiters;

      /* memorize if a new solution has been found in at least one iteration */
      if( *result == SCIP_FOUNDSOL )
         foundsol = TRUE;
   }
   while( *result == SCIP_FOUNDSOL && !SCIPisStopped(scip) && nnodes > 0 );

   /* reset result pointer if solution has been found in previous iteration */
   if( foundsol )
      *result = SCIP_FOUNDSOL;

   return SCIP_OKAY;
}


/*
 * primal heuristic specific interface methods
 */


/** main procedure of the proximity heuristic, creates and solves a sub-SCIP */
SCIP_RETCODE SCIPapplyProximity(
   SCIP*                 scip,               /**< original SCIP data structure                                        */
   SCIP_HEUR*            heur,               /**< heuristic data structure                                            */
   SCIP_RESULT*          result,             /**< result data structure                                               */
   SCIP_Real             minimprove,         /**< factor by which proximity should at least improve the incumbent     */
   SCIP_Longint          nnodes,             /**< node limit for the subproblem                                       */
   SCIP_Longint          nlpiters,           /**< LP iteration limit for the subproblem                               */
   SCIP_Longint*         nusednodes,         /**< pointer to store number of used nodes in subscip                    */
   SCIP_Longint*         nusedlpiters        /**< pointer to store number of used LP iterations in subscip            */
   )
{
   SCIP*                 subscip;            /* the subproblem created by proximity              */
   SCIP_HASHMAP*         varmapfw;           /* mapping of SCIP variables to sub-SCIP variables */
   SCIP_VAR**            vars;               /* original problem's variables                    */
   SCIP_VAR**            subvars;            /* subproblem's variables                          */
   SCIP_HEURDATA*        heurdata;           /* heuristic's private data structure              */
   SCIP_EVENTHDLR*       eventhdlr;          /* event handler for LP events                     */

   SCIP_SOL* incumbent;
   SCIP_CONS* objcons;
   SCIP_RETCODE retcode;
   SCIP_Longint iterlim;

   SCIP_Real timelimit;                      /* time limit for proximity subproblem              */
   SCIP_Real memorylimit;                    /* memory limit for proximity subproblem            */

   SCIP_Real large;
   SCIP_Real inf;

   SCIP_Real bestobj;
   SCIP_Real objcutoff;
   SCIP_Real lowerbound;

   int nvars;                                /* number of original problem's variables          */
   int nsubsols;
   int solidx;
   int i;

   SCIP_Bool valid;

   assert(scip != NULL);
   assert(heur != NULL);
   assert(result != NULL);

   assert(nnodes >= 0);
   assert(0.0 <= minimprove && minimprove <= 1.0);

   *result = SCIP_DIDNOTRUN;

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* only call the heuristic if we have an incumbent  */
   if( SCIPgetNSolsFound(scip) == 0 )
      return SCIP_OKAY;

   /* do not use heuristic on problems without binary variables */
   if( SCIPgetNBinVars(scip) == 0 )
      return SCIP_OKAY;

   incumbent = SCIPgetBestSol(scip);
   assert(incumbent != NULL);

   /* make sure that the incumbent is valid for the transformed space, otherwise terminate */
   if( SCIPsolIsOriginal(incumbent) )
      return SCIP_OKAY;

   solidx = SCIPsolGetIndex(incumbent);

   if( heurdata->lastsolidx == solidx )
      return SCIP_OKAY;

   /* waitingnodes parameter defines the minimum number of nodes to wait before a new incumbent is processed */
   if( SCIPgetNNodes(scip) > 1 && SCIPgetNNodes(scip) - SCIPsolGetNodenum(incumbent) < heurdata->waitingnodes )
      return SCIP_OKAY;

   bestobj = SCIPgetSolTransObj(scip, incumbent);
   lowerbound = SCIPgetLowerbound(scip);

   /* use knowledge about integrality of objective to round up lower bound */
   if( SCIPisObjIntegral(scip) )
   {
      assert(SCIPisFeasIntegral(scip, bestobj));
      SCIPdebugMessage(" Rounding up lower bound: %f --> %f \n", lowerbound, SCIPceil(scip, lowerbound));
      lowerbound = SCIPfeasCeil(scip, lowerbound);
   }

   /* do not trigger heuristic if primal and dual bound are already close together */
   if( SCIPisFeasEQ(scip, bestobj, lowerbound) || SCIPgetGap(scip) <= heurdata->mingap )
      return SCIP_OKAY;

   /* check whether there is enough time and memory left */
   timelimit = 0.0;
   memorylimit = 0.0;
   SCIP_CALL( SCIPgetRealParam(scip, "limits/time", &timelimit) );
   if( !SCIPisInfinity(scip, timelimit) )
      timelimit -= SCIPgetSolvingTime(scip);
   SCIP_CALL( SCIPgetRealParam(scip, "limits/memory", &memorylimit) );

   /* substract the memory already used by the main SCIP and the estimated memory usage of external software */
   if( !SCIPisInfinity(scip, memorylimit) )
   {
      memorylimit -= SCIPgetMemUsed(scip)/1048576.0;
      memorylimit -= SCIPgetMemExternEstim(scip)/1048576.0;
   }

   /* abort if no time is left or not enough memory to create a copy of SCIP, including external memory usage */
   if( timelimit <= 0.0 || memorylimit <= 2.0 * SCIPgetMemExternEstim(scip) / 1048576.0 )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTFIND;

   heurdata->lastsolidx = solidx;

   /* get variable data */
   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL) );

   /* create a subscip and copy the original scip instance into it */
   if( heurdata->subscip == NULL )
   {
	   assert(heurdata->varmapfw == NULL);

	   /* initialize the subproblem */
	   SCIP_CALL( SCIPcreate(&subscip) );

	   /* create the variable mapping hash map */
	   SCIP_CALL( SCIPhashmapCreate(&varmapfw, SCIPblkmem(subscip), SCIPcalcHashtableSize(5 * nvars)) );
	   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &subvars, nvars) );

	   /* copy complete SCIP instance */
	   valid = FALSE;
	   SCIP_CALL( SCIPcopy(scip, subscip, varmapfw, NULL, "proximity", TRUE, FALSE, TRUE, &valid) );
	   SCIPdebugMessage("Copying the SCIP instance was %s complete.\n", valid ? "" : "not ");

	   /* create event handler for LP events */
	   eventhdlr = NULL;
	   SCIP_CALL( SCIPincludeEventhdlrBasic(subscip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC, eventExecProximity, NULL) );
	   if( eventhdlr == NULL )
	   {
	      SCIPerrorMessage("event handler for "HEUR_NAME" heuristic not found.\n");
	      return SCIP_PLUGINNOTFOUND;
	   }

	   /* set up parameters for the copied instance */
	   SCIP_CALL( setupSubproblem(subscip) );
   }
   else
   {
      /* the instance, event handler, hash map and variable array were already copied in a previous iteration
       * and stored in heuristic data
       */
	   assert(heurdata->varmapfw != NULL);
	   assert(heurdata->subvars != NULL);

	   subscip = heurdata->subscip;
	   varmapfw = heurdata->varmapfw;
	   subvars = heurdata->subvars;

	   eventhdlr = SCIPfindEventhdlr(subscip, EVENTHDLR_NAME);
	   assert(eventhdlr != NULL);
   }


   /* calculate the minimum improvement for a heuristic solution in terms of the distance between incumbent objective
    * and the lower bound
    */
   objcutoff = lowerbound + (1 - minimprove) * (bestobj - lowerbound);

   /* use integrality of the objective function to round down (and thus strengthen) the objective cutoff */
   if( SCIPisObjIntegral(scip) )
	   objcutoff = SCIPfeasFloor(scip, objcutoff);

   if( SCIPisFeasLT(scip, objcutoff, lowerbound) )
      objcutoff = lowerbound;

   /* create the objective constraint in the sub scip, first without variables and values which will be added later */
   SCIP_CALL( SCIPcreateConsBasicLinear(subscip, &objcons, "objbound_of_origscip", 0, NULL, NULL, -SCIPinfinity(subscip), objcutoff) );

   /* determine large value to set variable bounds to, safe-guard to avoid fixings to infinite values */
   large = SCIPinfinity(scip);
   if( !SCIPisInfinity(scip, 0.1 / SCIPfeastol(scip)) )
      large = 0.1 / SCIPfeastol(scip);
   inf = SCIPinfinity(subscip);

   /* get variable image and change objective to proximity function (Manhattan distance) in sub-SCIP */
   for( i = 0; i < nvars; i++ )
   {
      SCIP_Real adjustedbound;
      SCIP_Real lb;
      SCIP_Real ub;

      subvars[i] = (SCIP_VAR*) SCIPhashmapGetImage(varmapfw, vars[i]);

      /* objective coefficients are only set for binary variables of the problem */
      if( SCIPvarIsBinary(vars[i]) )
      {
         SCIP_Real solval;

         solval = SCIPgetSolVal(scip, incumbent, vars[i]);
         assert(SCIPisFeasEQ(scip, solval, 1.0) || SCIPisFeasEQ(scip, solval, 0.0));

         if( solval < 0.5 )
         {
            SCIP_CALL( SCIPchgVarObj(subscip, subvars[i], 1.0) );
         }
         else
         {
            SCIP_CALL( SCIPchgVarObj(subscip, subvars[i], -1.0) );
         }
      }
      else
      {
         SCIP_CALL( SCIPchgVarObj(subscip, subvars[i], 0.0) );
      }

      lb = SCIPvarGetLbGlobal(subvars[i]);
      ub = SCIPvarGetUbGlobal(subvars[i]);

      /* adjust infinite bounds in order to avoid that variables with non-zero objective
       * get fixed to infinite value in proximity subproblem
       */
      if( SCIPisInfinity(subscip, ub ) )
      {
         adjustedbound = MAX(large, lb+large);
         adjustedbound = MIN(adjustedbound, inf);
         SCIP_CALL( SCIPchgVarUbGlobal(subscip, subvars[i], adjustedbound) );
      }
      if( SCIPisInfinity(subscip, -lb ) )
      {
         adjustedbound = MIN(-large, ub-large);
         adjustedbound = MAX(adjustedbound, -inf);
         SCIP_CALL( SCIPchgVarLbGlobal(subscip, subvars[i], adjustedbound) );
      }

      /* add all nonzero objective coefficients to the objective constraint */
      if( !SCIPisFeasZero(subscip, SCIPvarGetObj(vars[i])) )
      {
         SCIP_CALL( SCIPaddCoefLinear(subscip, objcons, subvars[i], SCIPvarGetObj(vars[i])) );
      }
   }

   /* add objective constraint to the subscip */
   SCIP_CALL( SCIPaddCons(subscip, objcons) );
   SCIP_CALL( SCIPreleaseCons(subscip, &objcons) );

   /* set limits for the subproblem */
   SCIP_CALL( SCIPsetLongintParam(subscip, "limits/nodes", nnodes) );
   SCIP_CALL( SCIPsetIntParam(subscip, "limits/solutions", 1) );
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/time", timelimit) );
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/memory", memorylimit) );

   /* restrict LP iterations */
   /*todo set iterations limit depending on the number of iterations of the original problem root */
   iterlim = nlpiters / 2;
   SCIP_CALL( SCIPsetLongintParam(subscip, "lp/iterlim", MAX(1, iterlim / MIN(10, nnodes))) );
   SCIP_CALL( SCIPsetLongintParam(subscip, "lp/rootiterlim", iterlim) );

   /* catch LP events of sub-SCIP */
   SCIP_CALL( SCIPtransformProb(subscip) );
   SCIP_CALL( SCIPcatchEvent(subscip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, (SCIP_EVENTDATA*) heurdata, NULL) );

   SCIPstatisticMessage("solving subproblem at Node: %"SCIP_LONGINT_FORMAT" "
         "nnodes: %"SCIP_LONGINT_FORMAT" "
         "iterlim: %"SCIP_LONGINT_FORMAT"\n", SCIPgetNNodes(scip), nnodes, iterlim);

   /* solve the subproblem with all previously adjusted parameters */
   retcode = SCIPsolve(subscip);

   SCIPstatisticMessage("solve of subscip:"
         "usednodes: %"SCIP_LONGINT_FORMAT" "
         "lp iters: %"SCIP_LONGINT_FORMAT" "
         "root iters: %"SCIP_LONGINT_FORMAT" "
         "Presolving Time: %.2f\n",
         SCIPgetNNodes(subscip), SCIPgetNLPIterations(subscip), SCIPgetNRootLPIterations(subscip), SCIPgetPresolvingTime(subscip));

   /* drop LP events of sub-SCIP */
   SCIP_CALL( SCIPdropEvent(subscip, SCIP_EVENTTYPE_NODESOLVED, eventhdlr, (SCIP_EVENTDATA*) heurdata, -1) );

   /* errors in solving the subproblem should not kill the overall solving process;
    * hence, the return code is caught and a warning is printed, only in debug mode, SCIP will stop.
    */
   if( retcode != SCIP_OKAY )
   {
#ifndef NDEBUG
      SCIP_CALL( retcode );
#endif
      SCIPwarningMessage(scip, "Error while solving subproblem in proximity heuristic; sub-SCIP terminated with code <%d>\n",retcode);
   }

   /* keep track of relevant information for future runs of heuristic */
   if( nusednodes != NULL )
      *nusednodes = SCIPgetNNodes(subscip);
   if( nusedlpiters != NULL )
      *nusedlpiters = SCIPgetNLPIterations(subscip);

   /* check, whether a solution was found */
   nsubsols = SCIPgetNSols(subscip);
   incumbent = SCIPgetBestSol(subscip);
   assert(nsubsols == 0 || incumbent != NULL);

   if( nsubsols > 0 )
   {
      /* try to translate the sub problem solution to the original scip instance */
      SCIP_Bool success;

      success = FALSE;
      SCIP_CALL( createNewSol(scip, subscip, subvars, heur, incumbent, &success) );

      if( success )
         *result = SCIP_FOUNDSOL;
   }
#ifdef SCIP_DEBUG
   /* enable output of SCIP statistics in DEBUG mode */
   SCIP_CALL( SCIPprintStatistics(subscip, NULL) );
#endif

   /* free the transformed subproblem data */
   SCIP_CALL( SCIPfreeTransform(subscip) );

   /* save subproblem in heuristic data for subsequent runs if it has been successful, otherwise free subproblem */
   if( *result == SCIP_FOUNDSOL )
   {
	   heurdata->subscip = subscip;
	   heurdata->varmapfw = varmapfw;
	   heurdata->subvars = subvars;
	   heurdata->nsubvars = nvars;
   }
   else
   {
	   SCIPfreeBlockMemoryArray(scip, &subvars, nvars);
	   /* free hash map */
	   SCIPhashmapFree(&varmapfw);
	   SCIP_CALL( SCIPfree(&subscip) );

	   heurdata->subscip = NULL;
	   heurdata->varmapfw = NULL;
	   heurdata->subvars = NULL;
   }

   return SCIP_OKAY;
}


/** creates the proximity primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurProximity(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );

   /* include primal heuristic */
   heur = NULL;
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecProximity, heurdata) );
   assert(heur != NULL);

   /* set non-NULL pointers to callback methods */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopyProximity) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeProximity) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitProximity) );
   SCIP_CALL( SCIPsetHeurExitsol(scip, heur, heurExitsolProximity) );

   /* add proximity primal heuristic parameters */
   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/maxnodes",
         "maximum number of nodes to regard in the subproblem",
         &heurdata->maxnodes, TRUE,DEFAULT_MAXNODES, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/nodesofs",
         "number of nodes added to the contingent of the total nodes",
         &heurdata->nodesofs, TRUE, DEFAULT_NODESOFS, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/minnodes",
         "minimum number of nodes required to start the subproblem",
         &heurdata->minnodes, TRUE, DEFAULT_MINNODES, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/maxlpiters",
         "maximum number of LP iterations to be performed in the subproblem",
         &heurdata->maxlpiters, TRUE, DEFAULT_MAXLPITERS, -1LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/minlpiters", "minimum number of LP iterations performed in "
         "subproblem", &heurdata->minlpiters, TRUE, DEFAULT_MINLPITERS, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddLongintParam(scip, "heuristics/"HEUR_NAME"/waitingnodes",
          "waiting nodes since last incumbent before heuristic is executed", &heurdata->waitingnodes, TRUE, DEFAULT_WAITINGNODES,
          0LL, SCIP_LONGINT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/"HEUR_NAME"/minimprove",
         "factor by which proximity should at least improve the incumbent",
         &heurdata->minimprove, TRUE, DEFAULT_MINIMPROVE, 0.0, 1.0, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/"HEUR_NAME"/nodesquot", "sub-MIP node limit w.r.t number of original nodes",
         &heurdata->nodesquot, TRUE, DEFAULT_NODESQUOT, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/"HEUR_NAME"/mingap",
         "minimum primal-dual gap for which the heuristic is executed",
         &heurdata->mingap, TRUE, DEFAULT_MINGAP, 0.0, SCIPinfinity(scip), NULL, NULL) );

   return SCIP_OKAY;
}
