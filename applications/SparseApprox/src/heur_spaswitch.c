/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_spaswitch.c
 * @brief  improvement heuristic that trades spa-binary variables between clusters
 * @author Leon Eifler
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "probdata_spa.h"
#include "heur_spaswitch.h"
#include "scip/cons_and.h"
/* @note If the heuristic runs in the root node, the timing is changed to (SCIP_HEURTIMING_DURINGLPLOOP |
 *       SCIP_HEURTIMING_BEFORENODE), see SCIP_DECL_HEURINITSOL callback.
 */

#define HEUR_NAME             "spaswitch"
#define HEUR_DESC             "switch heuristic that tries to improve solution by trading bins betweeen clusters"
#define HEUR_DISPCHAR         '!'
#define HEUR_PRIORITY         -20000
#define HEUR_FREQ             1
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERNODE
#define HEUR_USESSUBSCIP      FALSE          /**< does the heuristic use a secondary SCIP instance? */

/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
   int                   lastsolindex;       /**< index of the last solution for which oneopt was performed */
};


/*
 * Local methods
 */


/**
 *  Initialize the q-matrix from a given (possibly incomplete) clusterassignment
 */
static
void computeIrrevMat(
   SCIP_Real**       clustering,          /**< The matrix containing the clusterassignment */
   SCIP_Real**       qmatrix,             /**< The matrix with the return-values, in each cell is the irreversibility between two clusters */
   SCIP_Real**       cmatrix,             /**< The transition-matrix containg the probability-data */
   int               nbins,               /**< The number of bins */
   int               ncluster             /**< The number of possible clusters */
)
{
   int i,j,k,l;
   for( k = 0; k < ncluster; ++k )
   {
      for( l = 0; l < ncluster; ++l )
      {
         qmatrix[k][l] = 0;
         for( i = 0; i < nbins; ++i )
         {
            for( j = 0; j < nbins; ++j )
            {
               /* As -1 and 0 are both interpreted as 0, this check is necessary. Compute x_ik*x_jl*c_ij */
               qmatrix[k][l] += cmatrix[i][j] * clustering[i][k] * clustering[j][l];
            }
         }
      }
   }
}
/**  calculate the current epsI-value for a q-matrix */
static
SCIP_Real getIrrevBound(
   SCIP_Real**       qmatrix,             /**< The irreversibility matrix*/
   int               ncluster             /**< The number of cluster*/
)
{
   SCIP_Real epsI = 10; /* this is the an upper bound to irreversibility */
   int i;
   int j;
   for( i = 0; i < ncluster; ++i )
   {
      for( j = 0; j < i; ++j )
      {
         SCIP_Real temp;
         temp = fabs(qmatrix[i][j] - qmatrix[j][i]);
         if( temp > 0 && temp < epsI )
         {
            epsI = temp;
         }
      }
   }
   /* if we have no transitions at all then irreversibility should be set to 0 */
   if( epsI == 10 )
   {
      epsI = 0.0;
   }
   return epsI;
}

/**
 * assign the variables in scip according to the found clusterassignment
 */
static
SCIP_RETCODE assignVars(
   SCIP*             scip,                /**< SCIP data structure */
   SCIP_SOL*         sol,                 /**< The SCIP solution */
   SCIP_Real**       clustering,   /**< The matrix with the clusterassignment */
   int               nbins,               /**< The number of bins */
   int               ncluster,            /**< The number of cluster */
   SCIP_Real**       qmatrix              /**< The irreversibility matrix */
)
{
   int i,j;
   int c;
   SCIP_Real epsI;
   SCIP_Real q1;
   SCIP_Real q2;
   SCIP_VAR** vars;
   SCIP_VAR* resultant;
   SCIP_VAR* targetvar;
   SCIP_VAR** indvars;
   SCIP_VAR** absvars;
   SCIP_VAR*** binvars;

   assert(nbins > 0 && ncluster > 0);

   indvars = SCIPspaGetIndvars(scip);
   absvars = SCIPspaGetAbsvars(scip);
   binvars = SCIPspaGetBinvars(scip);
   targetvar = SCIPspaGetTargetvar(scip);

   epsI = getIrrevBound(qmatrix, ncluster);
   for ( c = 0; c < ncluster; ++c )
   {
      int isempty = 1;
      for( j = 0; j < nbins; ++j )
      {
         if( clustering[j][c] > 0 )
            isempty = 0;
      }
      /* set indicatorvar whether cluster is nonempty */

      if( !isempty && SCIPvarGetUbGlobal(indvars[c]) > 0 )
         SCIP_CALL( SCIPsetSolVal(scip, sol, indvars[c], 1.0) );
      else
      {
         if( SCIPvarGetLbGlobal(indvars[c]) == 0 )
            SCIP_CALL( SCIPsetSolVal(scip, sol, indvars[c], 0.0) );
      }

      /* set values of binary variables */
      for ( i = 0; i < nbins; ++i )
      {
         /* check if the clusterassignment ist feasible for the variable bounds. If not do not assign the variable */
         if( SCIPvarGetLbGlobal(binvars[i][c]) > 0 && clustering[i][c] == 0 )
            continue;
         if( SCIPvarGetUbGlobal(binvars[i][c]) < 1 && clustering[i][c] == 1 )
            continue;
         /* if the assignment respects the variable bounds, assign the binvars */
         SCIP_CALL( SCIPsetSolVal( scip, sol, binvars[i][c], (SCIP_Real) clustering[i][c]) );
      }

      /* set variables of absolute value and q-variables */
      for ( i = 0; i <= c; ++i )
      {
         q1 = qmatrix[c][i];
         q2 = qmatrix[i][c];
         /* set the absolute-value vars. Always check if the found values are feasible for the bounds of the variables */
         if( SCIPisGT(scip, q1, q2) )
         {
            if( SCIPvarGetUbGlobal(absvars[i + ncluster * c]) > 0 )
               SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[i + ncluster * c], 1.0) );
            if( SCIPvarGetLbGlobal(absvars[c + ncluster * i]) < 1 )
               SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[c + ncluster * i], 0.0) );
         }
         else if( SCIPisGT(scip, q2, q1) )
         {
            if( SCIPvarGetUbGlobal(absvars[c + ncluster * i]) > 0 )
               SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[c + ncluster * i], 1.0) );
            if( SCIPvarGetLbGlobal(absvars[i + ncluster * c]) < 1 )
               SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[i + ncluster * c], 0.0) );
         }
         else
         {
            SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[c + ncluster * i], SCIPvarGetLbGlobal(absvars[c + ncluster * i]) ) );
            SCIP_CALL( SCIPsetSolVal(scip, sol, absvars[i + ncluster * c], SCIPvarGetLbGlobal(absvars[i + ncluster * c]) ) );
         }
      }
   }
   /* set the value of the target-function variable */
   if( SCIPisGT(scip, epsI, SCIPvarGetLbGlobal(targetvar)) && SCIPisLT(scip, epsI, SCIPvarGetUbGlobal(targetvar)) )
   {
      SCIP_CALL( SCIPsetSolVal( scip, sol, targetvar, epsI) );
   }

   /* set the and variables that scip introduces in presoving */
   {
      int nconshdlrs = SCIPgetNConshdlrs(scip);
      SCIP_CONSHDLR** conshdlrs = SCIPgetConshdlrs(scip);
      for( i = 0; i < nconshdlrs ; ++i )
      {
         if ( 0 == strcmp( SCIPconshdlrGetName( conshdlrs[i] ) , "and" ) )
         {
            SCIP_CONS** cons = SCIPconshdlrGetConss(conshdlrs[i]);
            int ncons = SCIPconshdlrGetNConss(conshdlrs[i]);
            /* for each constraint, assign the value of the resultant */
            for( j = 0; j < ncons; ++j )
            {
               int k;
               int nvars = SCIPgetNVarsAnd(scip, cons[j]);
               SCIP_Real temp = 1.0;

               vars = SCIPgetVarsAnd(scip, cons[j]);
               resultant = SCIPgetResultantAnd(scip, cons[j]);
               for( k = 0; k < nvars; ++k )
               {
                  temp = temp * SCIPgetSolVal(scip, sol, vars[k]);
                  assert(SCIPisIntegral(scip, temp));
               }
               SCIP_CALL( SCIPsetSolVal(scip, sol, resultant, temp) );
            }
         }
      }
   }

   /* retransform the solution to original space, as the solution may be infeasible in transformed space due to presolving */
   SCIPretransformSol(scip, sol);



   return SCIP_OKAY;
}

/*
 * Callback methods of primal heuristic
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopySpaswitch)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurSpaswitch(scip) );

   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeSpaswitch)
{   /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* free heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin) */
static
SCIP_DECL_HEURINITSOL(heurInitsolSpaswitch)
{
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* create heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   return SCIP_OKAY;
}

/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed) */
static
SCIP_DECL_HEUREXITSOL(heurExitsolSpaswitch)
{
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* reset the timing mask to its default value */
   SCIPheurSetTimingmask(heur, HEUR_TIMING);

   return SCIP_OKAY;
}

/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitSpaswitch)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(scip != NULL);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* initialize last solution index */
   heurdata->lastsolindex = -1;

   return SCIP_OKAY;
}


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecSpaswitch)
{  /*lint --e{715}*/

   SCIP_SOL* bestsol;                        /* incumbent solution */
   SCIP_HEURDATA* heurdata;

   SCIP_VAR*** varmatrix;                          /* SCIP variables                */
   SCIP_Real** solclustering;
   SCIP_Real** cmatrix;
   SCIP_Real** qmatrix;
   SCIP_Real* bincoherence;
   SCIP_Real* mincoherence;
   SCIP_Real objective;
   SCIP_Bool improvement = FALSE;
   int* minbin;
   int nbins;
   int ncluster;
   int c;
   int k,l;
   int i;
   int j;

   assert(heur != NULL);
   assert(scip != NULL);
   assert(result != NULL);


   *result = SCIP_DIDNOTRUN;

   /* we only want to process each solution once */
   heurdata = SCIPheurGetData(heur);
   bestsol = SCIPgetBestSol(scip);


   if( bestsol == NULL || heurdata->lastsolindex == SCIPsolGetIndex(bestsol) )
      return SCIP_OKAY;

   heurdata->lastsolindex = SCIPsolGetIndex(bestsol);
   if( !SCIPsolIsOriginal(bestsol) )
   {
      SCIPretransformSol(scip, bestsol);
   }

   objective = SCIPsolGetOrigObj(bestsol);
   if( objective == 0.0 )
      return SCIP_OKAY;
   /* reset the timing mask to its default value (at the root node it could be different) */
   if( SCIPgetNNodes(scip) > 1 )
      SCIPheurSetTimingmask(heur, HEUR_TIMING);

   /* get problem variables */
   nbins = SCIPspaGetNrBins(scip);
   ncluster = SCIPspaGetNrCluster(scip);
   varmatrix = SCIPspaGetBinvars(scip);
   cmatrix = SCIPspaGetCmatrix(scip);
   assert(nbins >= 0);
   assert(ncluster >= 0);
   assert(varmatrix != NULL);

   /* Get the bin-variable values from the solution */
   SCIPallocClearMemoryArray(scip, &bincoherence, nbins);
   SCIPallocClearMemoryArray(scip, &mincoherence, ncluster);
   SCIPallocClearMemoryArray(scip, &minbin, ncluster);
   SCIPallocClearMemoryArray(scip, &solclustering, nbins);
   SCIPallocClearMemoryArray(scip, &qmatrix, ncluster);

   for( i = 0; i < nbins; ++i )
   {
      SCIPallocClearMemoryArray(scip, &solclustering[i], ncluster);
      for( c = 0; c < ncluster; ++c )
      {
         SCIP_Real solval = SCIPgetSolVal(scip, bestsol, varmatrix[i][c]);
         assert(SCIPisIntegral(scip, solval));
         solclustering[i][c] = solval;
      }
   }

   for( c = 0; c < ncluster; ++c )
   {
      SCIPallocMemoryArray(scip, &qmatrix[c], ncluster);
   }

   computeIrrevMat(solclustering, qmatrix, cmatrix, nbins, ncluster);


   /* get the minimal coherence-bin from each cluster */
   for( i = 0; i < nbins; ++i )
   {
      for( c = 0; c < ncluster; ++c )
      {
         mincoherence[c] = 1.0;
         if( !SCIPisEQ(scip, solclustering[i][c], 1.0) )
            continue;
         for( j = 0; j < nbins; ++j )
         {
            bincoherence[i] += cmatrix[i][j]*solclustering[j][c];
         }
         if( bincoherence[i] < mincoherence[c] && !SCIPisEQ(scip, solclustering[i][c], 0.0) )
         {
            mincoherence[c] = bincoherence[i];
            minbin[c] = i;
         }
         if( SCIPisEQ( scip, solclustering[minbin[c]][c], 0.0) )
            return SCIP_OKAY;
      }
   }

   /* try to trade 1 bin to a different cluster */
   *result = SCIP_DIDNOTFIND;

   for( k = 0; k < ncluster; ++k )
   {
      if( 0 == minbin[k] )
         continue;
      for( l = 0; l < ncluster; ++l )
      {
         SCIP_Real qkl = qmatrix[k][l];
         SCIP_Real qlk = qmatrix[l][k];
         SCIP_Real neweps;
         if( k == l )
            continue;
         for( i = 0; i < nbins; ++i )
         {
            if( i == minbin[k] )
               continue;
            qkl -= solclustering[i][l] * cmatrix[minbin[k]][i];
            qlk -= solclustering[i][l] * cmatrix[i][minbin[k]];

            solclustering[minbin[k]][k] = 0;
            solclustering[minbin[k]][l] = 1;

            qlk += solclustering[i][k] * cmatrix[minbin[k]][i];
            qkl += solclustering[i][k] * cmatrix[i][minbin[k]];

            solclustering[minbin[k]][k] = 1;
            solclustering[minbin[k]][l] = 0;

         }
         neweps = fabs(qkl - qlk);
         if( neweps > objective )
         {
            solclustering[minbin[k]][k] = 0.0;
            solclustering[minbin[k]][l] = 1.0;
            computeIrrevMat(solclustering, qmatrix, cmatrix, nbins, ncluster);
            assert( SCIPisEQ(scip, fabs(qmatrix[k][l] - qmatrix[l][k]), neweps) );
            assert( SCIPisGT(scip, getIrrevBound(qmatrix, ncluster), objective));
            improvement = TRUE;
            break;
         }
      }
      if( improvement )
         break;
   }

   /* try to switch 1opt with any of the bins */

   if( !improvement )
   {
      int bin;
      for( bin = 0; bin < nbins; ++bin )
      {
         for( k = 0; k < ncluster; ++k )
         {
            if( 0 == solclustering[bin][k])
               continue;
            for( l = 0; l < ncluster; ++l )
            {
               SCIP_Real qkl = qmatrix[k][l];
               SCIP_Real qlk = qmatrix[l][k];
               SCIP_Real neweps;
               if( k == l )
                  continue;
               for( i = 0; i < nbins; ++i )
               {
                  if( i == bin )
                     continue;
                  qkl -= solclustering[i][l] * cmatrix[bin][i];
                  qlk -= solclustering[i][l] * cmatrix[i][bin];

                  solclustering[bin][k] = 0;
                  solclustering[bin][l] = 1;

                  qlk += solclustering[i][k] * cmatrix[bin][i];
                  qkl += solclustering[i][k] * cmatrix[i][bin];

                  solclustering[bin][k] = 1;
                  solclustering[bin][l] = 0;

               }
               neweps = fabs(qkl - qlk);
               if( neweps > objective )
               {
                  solclustering[bin][k] = 0.0;
                  solclustering[bin][l] = 1.0;
                  computeIrrevMat(solclustering, qmatrix, cmatrix, nbins, ncluster);
                  assert( SCIPisEQ(scip, fabs(qmatrix[k][l] - qmatrix[l][k]), neweps) );
                  /*assert( SCIPisGT(scip, getIrrevBound(qmatrix, ncluster), objective));*/
                  improvement = TRUE;
                  break;
               }
            }
            if( improvement )
               break;
         }
         if( improvement )
            break;
      }
   }
   if( !improvement )
   {
      /* try to circle the minbins */
      for( k = 0; k < ncluster; ++k )
      {
         solclustering[minbin[k]][k] = 0.0;
         if( k < ncluster - 1 )
            solclustering[minbin[k]][k+1] = 1.0;
         else
            solclustering[minbin[k]][0] = 1.0;
      }
      computeIrrevMat(solclustering, qmatrix, cmatrix, nbins, ncluster);
      if( getIrrevBound(qmatrix, ncluster) > objective )
      {
         improvement = TRUE;
      }
   }

   if( improvement )
   {
      SCIP_SOL* sol;
      SCIP_Bool feasible;
      SCIP_CALL( SCIPcreateSol(scip, &sol, heur) );
      SCIP_CALL( assignVars( scip, sol, solclustering, nbins, ncluster, qmatrix) );
      SCIP_CALL( SCIPtrySolFree(scip, &sol, FALSE , FALSE, FALSE, FALSE, &feasible) );

      if( feasible )
         *result = SCIP_FOUNDSOL;
   }

   for( i = 0; i < nbins; ++i )
   {
      SCIPfreeMemoryArray(scip, &solclustering[i]);
   }
   for( c = 0; c < ncluster; ++c )
   {
      SCIPfreeMemoryArray(scip, &qmatrix[c]);
   }
   SCIPfreeMemoryArray(scip, &qmatrix);
   SCIPfreeMemoryArray(scip, &bincoherence);
   SCIPfreeMemoryArray(scip, &mincoherence);
   SCIPfreeMemoryArray(scip, &minbin);
   SCIPfreeMemoryArray(scip, &solclustering);

   return SCIP_OKAY;
}

/*
 * primal heuristic specific interface methods
 */

/** creates the oneopt primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurSpaswitch(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create Oneopt primal heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecSpaswitch, heurdata) );

   assert(heur != NULL);

   /* set non-NULL pointers to callback methods */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopySpaswitch) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeSpaswitch) );
   SCIP_CALL( SCIPsetHeurInitsol(scip, heur, heurInitsolSpaswitch) );
   SCIP_CALL( SCIPsetHeurExitsol(scip, heur, heurExitsolSpaswitch) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitSpaswitch) );

   return SCIP_OKAY;
}
