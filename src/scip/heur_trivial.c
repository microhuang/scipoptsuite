/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2008 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: heur_trivial.c,v 1.2 2009/09/09 10:30:43 bzfberth Exp $"

/**@file   heur_trivial.c
 * @ingroup PRIMALHEURISTICS
 * @brief  trivial primal heuristic
 * @author Timo Berthold
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "scip/heur_trivial.h"


#define HEUR_NAME             "trivial"
#define HEUR_DESC             "start heuristic which tries some trivial solutions"
#define HEUR_DISPCHAR         't'
#define HEUR_PRIORITY         1000
#define HEUR_FREQ             0
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_BEFORENODE

/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
};

/*
 * Local methods
 */

#define heurFreeTrivial NULL
#define heurInitTrivial NULL
#define heurExitTrivial NULL
#define heurInitsolTrivial NULL
#define heurExitsolTrivial NULL


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecTrivial)
{  /*lint --e{715}*/
   SCIP_VAR** vars;
   SCIP_SOL* lbsol;                     /* solution where all variables are set to their lower bounds */
   SCIP_SOL* ubsol;                     /* solution where all variables are set to their upper bounds */
   SCIP_SOL* zerosol;                   /* solution where all variables are set to zero */
   SCIP_SOL* locksol;                   /* solution where all variables are set to the bound with the fewer locks */

   SCIP_Real infinity;

   int nvars;
   int nbinvars;
   int i;

   SCIP_Bool success;
   SCIP_Bool zerovalid;

   *result = SCIP_DIDNOTFIND;
   success = FALSE;

   /* initialize data structure */
   SCIP_CALL( SCIPcreateSol(scip, &lbsol, heur) );
   SCIP_CALL( SCIPcreateSol(scip, &ubsol, heur) );
   SCIP_CALL( SCIPcreateSol(scip, &zerosol, heur) );
   SCIP_CALL( SCIPcreateSol(scip, &locksol, heur) );

   infinity = SCIPceil(scip, SCIPinfinity(scip) / 1000000000.0);

   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, &nbinvars, NULL, NULL, NULL) );

   /* if the problem is binary, we do not have to check the zero solution, since it is equal to the lower bound solution */
   zerovalid = (nvars != nbinvars);   
   assert(vars != NULL || nvars == 0);

   for( i = 0; i < nvars; i++ )
   {
      SCIP_Real lb;
      SCIP_Real ub;

      lb = SCIPvarGetLbLocal(vars[i]);
      ub = SCIPvarGetUbLocal(vars[i]);

      /* set infinite bounds to sufficient large value */
      if( SCIPisInfinity(scip, -lb) )
         lb = -infinity;
      if( SCIPisInfinity(scip, ub) )
         ub = infinity;

      SCIP_CALL( SCIPsetSolVal(scip, lbsol, vars[i], lb) );
      SCIP_CALL( SCIPsetSolVal(scip, ubsol, vars[i], ub) );

      /* try the zero vector, if it is in the bounds region */
      if( zerovalid )
      {
         if( SCIPisLE(scip, lb, 0.0) && SCIPisLE(scip, 0.0, ub) )
         {
            SCIP_CALL( SCIPsetSolVal(scip, zerosol, vars[i], 0.0) );
         }
         else
            zerovalid = FALSE;         
      }

      /* set variables to the bound with fewer locks, if tie choose an average value */
      if( SCIPvarGetNLocksDown(vars[i]) >  SCIPvarGetNLocksUp(vars[i]) )
      {
         SCIP_CALL( SCIPsetSolVal(scip, lbsol, vars[i], ub) );
      }
      else if( SCIPvarGetNLocksDown(vars[i]) <  SCIPvarGetNLocksUp(vars[i]) )
      {
         SCIP_CALL( SCIPsetSolVal(scip, lbsol, vars[i], lb) );
      }      
      else
      {
         SCIP_Real solval;
         solval = (lb+ub)/2.0;
         
         /* if a tie occurs, roughly every third integer variable will be rounded up */
         if( SCIPvarGetType(vars[i]) != SCIP_VARTYPE_CONTINUOUS )
            solval = i % 3 == 0 ? SCIPceil(scip,solval) : SCIPfloor(scip,solval);
         
         SCIP_CALL( SCIPsetSolVal(scip, locksol, vars[i], solval) );
      }
   }
   
   /* try and free lower bound solution */
   SCIP_CALL( SCIPtrySolFree(scip, &lbsol, FALSE, TRUE, TRUE, &success) );

   if( success )
   {
#ifdef SCIP_DEBUG
      SCIPdebugMessage("found feasible lower bound solution:\n");
      SCIPprintSol(scip, lbsol, NULL, FALSE);
#endif
      *result = SCIP_FOUNDSOL;
   }

   /* try and free upper bound solution */   
   SCIP_CALL( SCIPtrySolFree(scip, &ubsol, FALSE, TRUE, TRUE, &success) );

   if( success )
   {
#ifdef SCIP_DEBUG
      SCIPdebugMessage("found feasible upper bound solution:\n");
      SCIPprintSol(scip, ubsol, NULL, FALSE);
#endif
      *result = SCIP_FOUNDSOL;
   }

   /* try and free zero solution */
   if( zerovalid )
   {
      SCIP_CALL( SCIPtrySolFree(scip, &zerosol, FALSE, TRUE, TRUE, &success) );
      
      if( success )
      {
#ifdef SCIP_DEBUG
         SCIPdebugMessage("found feasible zero solution:\n");
         SCIPprintSol(scip, zerosol, NULL, FALSE);
#endif
         *result = SCIP_FOUNDSOL;
      }
   }
   else
   {
      SCIP_CALL( SCIPfreeSol(scip, &zerosol) );
   }

   /* try and free lock solution */
   SCIP_CALL( SCIPtrySolFree(scip, &locksol, FALSE, TRUE, TRUE, &success) );

   if( success )
   {
#ifdef SCIP_DEBUG
      SCIPdebugMessage("found feasible lock solution:\n");
      SCIPprintSol(scip, locksol, NULL, FALSE);
#endif
      *result = SCIP_FOUNDSOL;
   }

   return SCIP_OKAY;
}


/*
 * primal heuristic specific interface methods
 */

/** creates the trivial primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurTrivial(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeur(scip, HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING,
         heurFreeTrivial, heurInitTrivial, heurExitTrivial, 
         heurInitsolTrivial, heurExitsolTrivial, heurExecTrivial,
         NULL) );

   return SCIP_OKAY;
}
