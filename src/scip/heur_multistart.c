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

/**@file   heur_multistart.c
 * @brief  multistart heuristic for convex and nonconvex MINLPs
 * @author Benjamin Mueller
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/heur_multistart.h"
#include "scip/heur_subnlp.h"

#include "nlpi/exprinterpret.h"

#define HEUR_NAME             "multistart"
#define HEUR_DESC             "multistart heuristic for convex and nonconvex MINLPs"
#define HEUR_DISPCHAR         'm'
#define HEUR_PRIORITY         0
#define HEUR_FREQ             0
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERNODE
#define HEUR_USESSUBSCIP      TRUE           /**< does the heuristic use a secondary SCIP instance? */

#define DEFAULT_RANDSEED      59             /**< initial random seed */
#define DEFAULT_NRNDPOINTS    100            /**< default number of generated random points per call */
#define DEFAULT_MAXBOUNDSIZE  2e+4           /**< default maximum variable domain size for unbounded variables */
#define DEFAULT_NMAXITER      300            /**< default number of iterations to reduce the maximum violation of a point */
#define DEFAULT_MINIMPRFAC    0.05           /**< default minimum required improving factor to proceed in improvement of a point */
#define DEFAULT_MINIMPRITER   10             /**< default number of iteration when checking the minimum improvement */
#define DEFAULT_MAXRELDIST    0.15           /**< default maximum distance between two points in the same cluster */
#define DEFAULT_NLPMINIMPR    0.00           /**< default factor by which heuristic should at least improve the incumbent */
#define DEFAULT_MAXNCLUSTER   10             /**< default maximum number of considered clusters per heuristic call */

#define MAXVIOL               -1e+4          /**< maximum violation when improving the feasibility of a point */

/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_EXPRINT*         exprinterpreter;    /**< expression interpreter to compute gradients */
   int                   nrndpoints;         /**< number of random points generated per execution call */
   unsigned int          randseed;           /**< seed value for random number generator */
   SCIP_Real             maxboundsize;       /**< maximum variable domain size for unbounded variables */
   SCIP_RANDNUMGEN*      randnumgen;         /**< random number generator */

   int                   nmaxiter;           /**< number of iterations to reduce the maximum violation of a point */
   SCIP_Real             minimprfac;         /**< minimum required improving factor to proceed in the improvement of a single point */
   int                   minimpriter;        /**< number of iteration when checking the minimum improvement */

   SCIP_Real             maxreldist;         /**< maximum distance between two points in the same cluster */
   SCIP_Real             nlpminimpr;         /**< factor by which heuristic should at least improve the incumbent */

   int                   maxncluster;        /**< maximum number of considered clusters per heuristic call */
};


/*
 * Local methods
 */


/** returns an unique index of a variable in the range of 0,..,SCIPgetNVars(scip)-1 */
#ifndef NDEBUG
static
int getVarIndex(
   SCIP_HASHMAP*         varindex,           /**< maps variables to indicies between 0,..,SCIPgetNVars(scip)-1 */
   SCIP_VAR*             var                 /**< variable */
   )
{
   assert(varindex != NULL);
   assert(var != NULL);
   assert(SCIPhashmapExists(varindex, (void*)var));

   return (int)(size_t)SCIPhashmapGetImage(varindex, (void*)var);
}
#else
#define getVarIndex(varindex,var) ((int)(size_t)SCIPhashmapGetImage((varindex), (void*)(var)))
#endif

/** samples and stores random points */
static
SCIP_RETCODE sampleRandomPoints(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL**            rndpoints,          /**< array to store all random points */
   int                   nrndpoints,         /**< total number of random points to compute */
   SCIP_Real             maxboundsize,       /**< maximum variable domain size for unbounded variables */
   SCIP_RANDNUMGEN*      randnumgen          /**< random number generator */
   )
{
   SCIP_VAR** vars;
   SCIP_Real val;
   SCIP_Real lb;
   SCIP_Real ub;
   int nvars;
   int i;
   int k;

   assert(scip != NULL);
   assert(rndpoints != NULL);
   assert(nrndpoints > 0);
   assert(maxboundsize > 0.0);
   assert(randnumgen != NULL);

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   for( k = 0; k < nrndpoints; ++k )
   {
      SCIP_CALL( SCIPcreateSol(scip, &rndpoints[k], NULL) );

      for( i = 0; i < nvars; ++i )
      {
         lb = MIN(SCIPvarGetLbLocal(vars[i]), SCIPvarGetUbLocal(vars[i]));
         ub = MAX(SCIPvarGetLbLocal(vars[i]), SCIPvarGetUbLocal(vars[i]));

         if( SCIPisEQ(scip, lb, ub) )
            val = (lb + ub) / 2.0;
         /* use a smaller domain for unbounded variables */
         else if( !SCIPisInfinity(scip, -lb) && !SCIPisInfinity(scip, ub) )
            val = SCIPrandomGetReal(randnumgen, lb, ub);
         else if( !SCIPisInfinity(scip, -lb) )
            val = SCIPrandomGetReal(randnumgen, lb, lb + maxboundsize);
         else if( !SCIPisInfinity(scip, ub) )
            val = SCIPrandomGetReal(randnumgen, ub - maxboundsize, ub);
         else
         {
            assert(SCIPisInfinity(scip, -lb) && SCIPisInfinity(scip, ub));
            val = SCIPrandomGetReal(randnumgen, -0.5*maxboundsize, 0.5*maxboundsize);
         }
         assert(SCIPisGE(scip, val ,lb) && SCIPisLE(scip, val, ub));

         /* set solution value */
         SCIP_CALL( SCIPsetSolVal(scip, rndpoints[k], vars[i], val) );
      }

      assert(rndpoints[k] != NULL);
   }

   return SCIP_OKAY;
}

/** computes the maximum violation of a given point; a negative value means that there is a violation */
static
SCIP_RETCODE getMaxViol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_NLROW**          nlrows,             /**< array containing all nlrows */
   int                   nnlrows,            /**< total number of nlrows */
   SCIP_SOL*             sol,                /**< solution */
   SCIP_Real*            maxviol             /**< buffer to store the maximum violation */
   )
{
   SCIP_Real tmp;
   int i;

   assert(scip != NULL);
   assert(sol != NULL);
   assert(maxviol != NULL);
   assert(nlrows != NULL);
   assert(nnlrows > 0);

   *maxviol = SCIPinfinity(scip);

   for( i = 0; i < nnlrows; ++i )
   {
      assert(nlrows[i] != NULL);

      SCIP_CALL( SCIPgetNlRowSolFeasibility(scip, nlrows[i], sol, &tmp) );
      *maxviol = MIN(*maxviol, tmp);
   }

   return SCIP_OKAY;
}

/** computes the gradient for a given point and nonlinear row */
static
SCIP_RETCODE computeGradient(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_NLROW*           nlrow,              /**< nonlinear row */
   SCIP_EXPRINT*         exprint,            /**< expressions interpreter */
   SCIP_SOL*             sol,                /**< solution to compute the gradient for */
   SCIP_HASHMAP*         varindex,           /**< maps variables to indicies between 0,..,SCIPgetNVars(scip)-1 uniquely */
   SCIP_Real*            grad,               /**< buffer to store the gradient; grad[varindex(i)] corresponds to SCIPgetVars(scip)[i] */
   SCIP_Real*            norm                /**< buffer to store ||grad||^2  */
   )
{
   SCIP_EXPRTREE* tree;
   SCIP_VAR* var;
   int i;

   assert(scip != NULL);
   assert(nlrow != NULL);
   assert(varindex != NULL);
   assert(exprint != NULL);
   assert(sol != NULL);
   assert(norm != NULL);

   BMSclearMemoryArray(grad, SCIPgetNVars(scip));
   *norm = 0.0;

   /* linear part */
   for( i = 0; i < SCIPnlrowGetNLinearVars(nlrow); i++ )
   {
      var = SCIPnlrowGetLinearVars(nlrow)[i];
      assert(var != NULL);
      assert(getVarIndex(varindex, var) >= 0 && getVarIndex(varindex, var) < SCIPgetNVars(scip));

      grad[getVarIndex(varindex, var)] += SCIPnlrowGetLinearCoefs(nlrow)[i];
   }

   /* quadratic part */
   for( i = 0; i < SCIPnlrowGetNQuadElems(nlrow); i++ )
   {
      SCIP_VAR* var1;
      SCIP_VAR* var2;

      var1  = SCIPnlrowGetQuadVars(nlrow)[SCIPnlrowGetQuadElems(nlrow)[i].idx1];
      var2  = SCIPnlrowGetQuadVars(nlrow)[SCIPnlrowGetQuadElems(nlrow)[i].idx2];

      assert(SCIPnlrowGetQuadElems(nlrow)[i].idx1 < SCIPnlrowGetNQuadVars(nlrow));
      assert(SCIPnlrowGetQuadElems(nlrow)[i].idx2 < SCIPnlrowGetNQuadVars(nlrow));
      assert(getVarIndex(varindex, var1) >= 0 && getVarIndex(varindex, var1) < SCIPgetNVars(scip));
      assert(getVarIndex(varindex, var2) >= 0 && getVarIndex(varindex, var2) < SCIPgetNVars(scip));

      grad[getVarIndex(varindex, var1)] += SCIPnlrowGetQuadElems(nlrow)[i].coef * SCIPgetSolVal(scip, sol, var2);
      grad[getVarIndex(varindex, var2)] += SCIPnlrowGetQuadElems(nlrow)[i].coef * SCIPgetSolVal(scip, sol, var1);
   }

   /* tree part */
   tree = SCIPnlrowGetExprtree(nlrow);
   if( tree != NULL )
   {
      SCIP_Real* treegrad;
      SCIP_Real* x;
      SCIP_Real val;

      assert(SCIPexprtreeGetNVars(tree) <= SCIPgetNVars(scip));

      SCIP_CALL( SCIPallocBufferArray(scip, &x, SCIPexprtreeGetNVars(tree)) );
      SCIP_CALL( SCIPallocBufferArray(scip, &treegrad, SCIPexprtreeGetNVars(tree)) );

      /* compile expression tree, if not done before */
      if( SCIPexprtreeGetInterpreterData(tree) == NULL )
      {
         SCIP_CALL( SCIPexprintCompile(exprint, tree) );
      }

      /* sets the solution value */
      for( i = 0; i < SCIPexprtreeGetNVars(tree); ++i )
         x[i] = SCIPgetSolVal(scip, sol, SCIPexprtreeGetVars(tree)[i]);

      SCIP_CALL( SCIPexprintGrad(exprint, tree, x, TRUE, &val, treegrad) );

      /* update corresponding gradient entry */
      for( i = 0; i < SCIPexprtreeGetNVars(tree); ++i )
      {
         var = SCIPexprtreeGetVars(tree)[i];
         assert(var != NULL);
         assert(getVarIndex(varindex, var) >= 0 && getVarIndex(varindex, var) < SCIPgetNVars(scip));

         grad[getVarIndex(varindex, var)] += treegrad[i];
      }

      SCIPfreeBufferArray(scip, &treegrad);
      SCIPfreeBufferArray(scip, &x);
   }

   /* compute ||grad||^2 */
   for( i = 0; i < SCIPgetNVars(scip); ++i )
      *norm += SQR(grad[i]);

   return SCIP_OKAY;
}

/** use consensus vectors to improve feasibility for a given starting point */
static
SCIP_RETCODE improvePoint(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_NLROW**          nlrows,             /**< array containing all nlrows */
   int                   nnlrows,            /**< total number of nlrows */
   SCIP_HASHMAP*         varindex,           /**< maps variables to indicies between 0,..,SCIPgetNVars(scip)-1 */
   SCIP_EXPRINT*         exprinterpreter,    /**< expression interpreter */
   SCIP_SOL*             point,              /**< random generated point */
   int                   nmaxiter,           /**< maximum number of iterations */
   SCIP_Real             minimprfac,         /**< minimum required improving factor to proceed in the improvement of a single point */
   int                   minimpriter,        /**< number of iteration when checking the minimum improvement */
   SCIP_Real*            maxviol             /**< pointer to store the maximum violation */
   )
{
   SCIP_VAR** vars;
   SCIP_Real* grad;
   SCIP_Real* updatevec;
   SCIP_Real lastmaxviol;
   int nvars;
   int r;
   int i;

   assert(varindex != NULL);
   assert(exprinterpreter != NULL);
   assert(point != NULL);
   assert(nmaxiter > 0);
   assert(maxviol != NULL);
   assert(nlrows != NULL);
   assert(nnlrows > 0);

   SCIP_CALL( getMaxViol(scip, nlrows, nnlrows, point, maxviol) );
#ifdef SCIP_DEBUG_IMPROVEPOINT
   SCIPdebugMessage("start maxviol = %e\n", *maxviol);
#endif

   /* stop since start point is feasible */
   if( !SCIPisFeasLT(scip, *maxviol, 0.0) )
   {
#ifdef SCIP_DEBUG_IMPROVEPOINT
      SCIPdebugMessage("start point is feasible");
#endif
      return SCIP_OKAY;
   }

   lastmaxviol = *maxviol;
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   SCIP_CALL( SCIPallocBufferArray(scip, &grad, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &updatevec, nvars) );

   /* main loop */
   for( r = 0; r < nmaxiter && SCIPisFeasLT(scip, *maxviol, 0.0); ++r )
   {
      SCIP_Real feasibility;
      SCIP_Real activity;
      SCIP_Real nlrownorm;
      SCIP_Real scale;
      int nviolnlrows;

      BMSclearMemoryArray(updatevec, nvars);
      nviolnlrows = 0;

      for( i = 0; i < nnlrows; ++i )
      {
         int j;

         SCIP_CALL( SCIPgetNlRowSolFeasibility(scip, nlrows[i], point, &feasibility) );

         /* do not consider non-violated constraints */
         if( SCIPisFeasGE(scip, feasibility, 0.0) )
            continue;

         /* increase number of violated nlrows */
         ++nviolnlrows;

         SCIP_CALL( SCIPgetNlRowSolActivity(scip, nlrows[i], point, &activity) );
         SCIP_CALL( computeGradient(scip, nlrows[i], exprinterpreter, point, varindex, grad, &nlrownorm) );

         /* stop if the gradient disappears at the current point */
         if( SCIPisZero(scip, nlrownorm) )
         {
            r = nmaxiter - 1;
#ifdef SCIP_DEBUG_IMPROVEPOINT
            SCIPdebugMessage("gradient vanished at current point -> stop\n");
#endif
            break;
         }

         /* compute -g(x_k) / ||grad(g)(x_k)||^2 for a constraint g(x_k) <= 0 */
         scale = -feasibility / nlrownorm;
         if( !SCIPisInfinity(scip, SCIPnlrowGetRhs(nlrows[i])) && SCIPisGT(scip, activity, SCIPnlrowGetRhs(nlrows[i])) )
            scale *= -1.0;

         /* skip nonliner row of the scaler is too small or too large */
         if( SCIPisEQ(scip, scale, 0.0) || SCIPisHugeValue(scip, REALABS(scale)) )
            continue;

         for( j = 0; j < nvars; ++j )
            updatevec[j] += scale * grad[j];
      }
      assert(nviolnlrows > 0);

      for( i = 0; i < nvars; ++i )
      {
         /* adjust point */
         updatevec[i] = SCIPgetSolVal(scip, point, vars[i]) + updatevec[i] / nviolnlrows;
         updatevec[i] = MIN(updatevec[i], SCIPvarGetUbLocal(vars[i]));
         updatevec[i] = MAX(updatevec[i], SCIPvarGetLbLocal(vars[i]));

         SCIP_CALL( SCIPsetSolVal(scip, point, vars[i], updatevec[i]) );
      }

      /* update violations */
      SCIP_CALL( getMaxViol(scip, nlrows, nnlrows, point, maxviol) );

      /* check stopping criterion */
      if( r % 5 == 0 && r > 0 )
      {
         if( *maxviol <= MAXVIOL || (*maxviol - lastmaxviol) / MAX(REALABS(*maxviol), REALABS(lastmaxviol)) < minimprfac )
            break;
         lastmaxviol = *maxviol;
      }
   }

#ifdef SCIP_DEBUG_IMPROVEPOINT
   SCIPdebugMessage("niter=%d maxviol=%e\n", r, *maxviol);
#endif

   SCIPfreeBufferArray(scip, &grad);
   SCIPfreeBufferArray(scip, &updatevec);

   return SCIP_OKAY;
}

/** sort points w.r.t their violations; filters out points with too large violation */
static
SCIP_RETCODE filterPoints(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL**            points,             /**< array containing improved points */
   SCIP_Real*            violations,         /**< array containing violations (sorted) */
   int                   npoints,            /**< total number of points */
   int*                  nusefulpoints       /**< pointer to store the total number of useful points */
   )
{
   SCIP_Real maxviolation;
   SCIP_Real meanviol;
   int i;

   assert(points != NULL);
   assert(violations != NULL);
   assert(npoints > 0);
   assert(nusefulpoints != NULL);

   /* sort points w.r.t their violations; non-negative violations correspond to feasible points for the NLP */
   SCIPsortDownRealPtr(violations, (void**)points, npoints);
   maxviolation = violations[npoints - 1];

   /* check if all points are feasible */
   if( SCIPisFeasGE(scip, maxviolation, 0.0) )
   {
      *nusefulpoints = npoints;
      return SCIP_OKAY;
   }

   *nusefulpoints = 0;

   /* compute shifted geometric mean of violations (shift value = maxviolation + 1) */
   meanviol = 1.0;
   for( i = 0; i < npoints; ++i )
   {
      assert(violations[i] - maxviolation + 1.0 >= 0.0);
      meanviol *= pow(violations[i] - maxviolation + 1.0, 1.0 / npoints);
   }
   meanviol += maxviolation - 1.0;
   SCIPdebugMessage("meanviol = %e\n", meanviol);

   for( i = 0; i < npoints; ++i )
   {
      if( SCIPisFeasLT(scip, violations[i], 0.0) && (violations[i] <= 1.05 * meanviol || SCIPisLE(scip, violations[i], MAXVIOL)) )
         break;

      ++(*nusefulpoints);
   }

   return SCIP_OKAY;
}

/** returns the relative distance between two points */
static
SCIP_Real getRelDistance(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             x,                  /**< first point */
   SCIP_SOL*             y                   /**< second point */
   )
{
   SCIP_VAR** vars;
   SCIP_Real distance;
   int i;

   vars = SCIPgetVars(scip);
   distance = 0.0;

   for( i = 0; i < SCIPgetNVars(scip); ++i )
   {
      distance += REALABS(SCIPgetSolVal(scip, x, vars[i]) - SCIPgetSolVal(scip, y, vars[i]))
         / (MAX(1.0, SCIPvarGetUbLocal(vars[i]) - SCIPvarGetLbLocal(vars[i])));
   }

   return distance / SCIPgetNVars(scip);
}

/** cluster useful points with a greedy algorithm */
static
SCIP_RETCODE clusterPointsGreedy(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL**            points,             /**< array containing improved points */
   int                   npoints,            /**< total number of points */
   int*                  clusteridx,         /**< array to store for each point the index of the cluster */
   int*                  ncluster,           /**< pointer to store the total number of cluster */
   SCIP_Real             maxreldist,         /**< maximum relative distance between any two points of the same cluster */
   int                   maxncluster         /**< maximum number of clusters to compute */
   )
{
   int i;

   assert(points != NULL);
   assert(npoints > 0);
   assert(clusteridx != NULL);
   assert(ncluster != NULL);
   assert(maxreldist >= 0.0);
   assert(maxncluster >= 0);

   /* initialize cluster indices */
   for( i = 0; i < npoints; ++i )
      clusteridx[i] = INT_MAX;

   *ncluster = 0;

   for( i = 0; i < npoints && (*ncluster < maxncluster); ++i )
   {
      int j;

      /* point is already assigned to a cluster */
      if( clusteridx[i] != INT_MAX )
         continue;

      /* create a new cluster for i */
      clusteridx[i] = *ncluster;

      for( j = i + 1; j < npoints; ++j )
      {
         if( clusteridx[j] == INT_MAX && getRelDistance(scip, points[i], points[j]) <= maxreldist )
            clusteridx[j] = *ncluster;
      }

      ++(*ncluster);
   }

#ifndef NDEBUG
   for( i = 0; i < npoints; ++i )
   {
      assert(clusteridx[i] >= 0);
      assert(clusteridx[i] < *ncluster || clusteridx[i] == INT_MAX);
   }
#endif

   return SCIP_OKAY;
}

/** calls the sub-NLP heuristic for a given cluster */
static
SCIP_RETCODE solveNLP(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< multi-start heuristic */
   SCIP_HEUR*            nlpheur,            /**< pointer to NLP local search heuristics */
   SCIP_SOL**            points,             /**< array containing improved points */
   int                   npoints,            /**< total number of points */
   SCIP_Longint          itercontingent,     /**< iteration limit for NLP solver */
   SCIP_Real             timelimit,          /**< time limit for NLP solver */
   SCIP_Real             minimprove,         /**< desired minimal relative improvement in objective function value */
   SCIP_Bool*            success             /**< pointer to store if we could find a solution */
   )
{
   SCIP_VAR** vars;
   SCIP_SOL* refpoint;
   SCIP_RESULT nlpresult;
   SCIP_Real val;
   int nbinvars;
   int nintvars;
   int nvars;
   int i;

   assert(points != NULL);
   assert(npoints > 0);

   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, &nbinvars, &nintvars, NULL, NULL) );
   *success = FALSE;

   SCIP_CALL( SCIPcreateSol(scip, &refpoint, heur) );

   /* compute reference point */
   for( i = 0; i < nvars; ++i )
   {
      int p;

      val = 0.0;

      for( p = 0; p < npoints; ++p )
      {
         assert(points[p] != NULL);
         val += SCIPgetSolVal(scip, points[p], vars[i]);
      }

      SCIP_CALL( SCIPsetSolVal(scip, refpoint, vars[i], val / npoints) );
   }

   /** round point for sub-NLP heuristic */
   SCIP_CALL( SCIProundSol(scip, refpoint, success) );
   SCIPdebugMessage("rounding refpoint successful? %d\n", *success);

   /* round variables manually if the locks did not allow us to round them */
   if( !(*success) )
   {
      for( i = 0; i < nbinvars + nintvars; ++i )
      {
         val = SCIPgetSolVal(scip, refpoint, vars[i]);

         if( !SCIPisFeasIntegral(scip, val) )
         {
            assert(SCIPisFeasIntegral(scip, SCIPvarGetLbLocal(vars[i])));
            assert(SCIPisFeasIntegral(scip, SCIPvarGetUbLocal(vars[i])));

            /* round and adjust value */
            val = SCIPround(scip, val);
            val = MIN(val, SCIPvarGetUbLocal(vars[i]));
            val = MAX(val, SCIPvarGetLbLocal(vars[i]));
            assert(SCIPisFeasIntegral(scip, val));

            SCIP_CALL( SCIPsetSolVal(scip, refpoint, vars[i], val) );
         }
      }
   }

   /* call sub-NLP heuristic */
   SCIP_CALL( SCIPapplyHeurSubNlp(scip, nlpheur, &nlpresult, refpoint, itercontingent, timelimit, minimprove, NULL, refpoint) );
   SCIPdebugMessage("SUBNLPRESULT = %d SOLVAL=%e\n", nlpresult, SCIPgetSolOrigObj(scip, refpoint));

   if( nlpresult == SCIP_FOUNDSOL )
   {
#ifdef SCIP_DEBUG
      SCIP_CALL( SCIPtrySolFree(scip, &refpoint, TRUE, TRUE, TRUE, TRUE, TRUE, success) );
#else
      SCIP_CALL( SCIPtrySolFree(scip, &refpoint, FALSE, FALSE, FALSE, FALSE, FALSE, success) );
#endif
   }
   else
   {
      SCIP_CALL( SCIPfreeSol(scip, &refpoint) );
   }

   return SCIP_OKAY;
}

/** main function of the multi-start heuristic; the algorithm contains the following steps
 *
 *  1. sample random points in the box defined by the variable bounds; shrink the domain of unbounded variables
 *
 *  2. improve all points by using constraint consensus vectors
 *     a. s^i_{k+1} = x_k + -g(x_k) / ||grad g(x_k)||^2 grad g(x_k) for all violated constraints i = 1,..,m
 *     b. x_k{+1} = (1 / nviols) sum_{i=1,..,m} s^i_{k+1} where nviols is the number of violated constraints for x_k
 *
 *  3. filter points which have a too large violation; let v_p the maximum violation of point p
 *     a. compute the mean violation v_mean (use shifted geometric mean)
 *     b. filter points p with v_p < scalar * v_mean
 *
 *  4. compute disjoint cluster C_1,..,C_K for the filtered points
 *
 *  5. solve sub-problem by using cluster information; cluster are used either as
 *     a. solve NLP with reference point x = 1/|C_k| sum{p in C_k} p
 *     b. solve sub-SCIP with reduced domains [lb_i,ub_i] = [min_{p in C_k} p_i, max_{p in C_k} p_i]
 *     c. solve linear relaxation with additional variables lambda_p >=0 for each p in C_k and an additional constraint
 *        x = sum_{p in C_k} lambda_p * p
 */
static
SCIP_RETCODE applyHeur(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< heuristic */
   SCIP_RESULT*          result              /**< pointer to store the result */
   )
{
   SCIP_SOL** points;
   SCIP_HASHMAP* varindex;
   SCIP_HEURDATA* heurdata;
   SCIP_Real* violations;
   int* clusteridx;
   int nusefulpoints;
   int ncluster;
   int start;
   int i;

   assert(scip != NULL);
   assert(heur != NULL);
   assert(result != NULL);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   SCIPdebugMessage("call applyHeur()\n");

   if( heurdata->exprinterpreter == NULL )
   {
      SCIP_CALL( SCIPexprintCreate(SCIPblkmem(scip), &heurdata->exprinterpreter) );
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &points, heurdata->nrndpoints) );
   SCIP_CALL( SCIPallocBufferArray(scip, &violations, heurdata->nrndpoints) );
   SCIP_CALL( SCIPallocBufferArray(scip, &clusteridx, heurdata->nrndpoints) );
   SCIP_CALL( SCIPhashmapCreate(&varindex, SCIPblkmem(scip), SCIPcalcHashtableSize(SCIPgetNVars(scip))) );

   /* create an unique mapping of all variables to 0,..,SCIPgetNVars(scip)-1 */
   for( i = 0; i < SCIPgetNVars(scip); ++i )
   {
      SCIP_CALL( SCIPhashmapInsert(varindex, (void*)SCIPgetVars(scip)[i], (void*)(size_t)i) );
   }

   /*
    * 1. sample random points; note that the solutions need to be freed again
    */
   SCIP_CALL( sampleRandomPoints(scip, points, heurdata->nrndpoints, heurdata->maxboundsize, heurdata->randnumgen) );

   /*
    * 2. improve points via consensus vectors
    */
   for( i = 0; i < heurdata->nrndpoints; ++i )
   {
      SCIP_CALL( improvePoint(scip, SCIPgetNLPNlRows(scip), SCIPgetNNLPNlRows(scip), varindex, heurdata->exprinterpreter, points[i],
         heurdata->nmaxiter, heurdata->minimprfac, heurdata->minimpriter, &violations[i]) );
   }

   /*
    * 3. filter points with a too large violation
    */
   SCIP_CALL( filterPoints(scip, points, violations, heurdata->nrndpoints, &nusefulpoints) );
   assert(nusefulpoints >= 0);
   SCIPdebugMessage("nusefulpoints = %d\n", nusefulpoints);

   if( nusefulpoints == 0 )
      goto TERMINATE;

   /*
    * 4. compute clusters
    */
   SCIP_CALL( clusterPointsGreedy(scip, points, nusefulpoints, clusteridx, &ncluster, heurdata->maxreldist,
         heurdata->maxncluster) );
   assert(ncluster >= 0 && ncluster <= heurdata->maxncluster);
   SCIPdebugMessage("ncluster = %d\n", ncluster);

   SCIPsortIntPtr(clusteridx, (void**)points, nusefulpoints);

   /*
    * 5. solve for each cluster a corresponding sub-problem
    */
   start = 0;
   while( start < nusefulpoints && clusteridx[start] != INT_MAX && !SCIPisStopped(scip) )
   {
      SCIP_Real timelimit;
      SCIP_Bool success;
      int end;

      end = start;
      while( end < nusefulpoints && clusteridx[start] == clusteridx[end] )
         ++end;

      assert(end - start > 0);

      SCIP_CALL( SCIPgetRealParam(scip, "limits/time", &timelimit) );
      if( !SCIPisInfinity(scip, timelimit) )
         timelimit -= SCIPgetSolvingTime(scip);

      /* only solve sub-NLP if we have enough time left */
      if( timelimit <= 0.0 )
      {
         SCIPdebugMessage("no time left!\n");
         break;
      }

      /* call sub-NLP heuristic */
      SCIP_CALL( solveNLP(scip, heur, SCIPfindHeur(scip, "subnlp"), &points[start], end - start, -1LL,
            timelimit, heurdata->nlpminimpr, &success) );
      SCIPdebugMessage("solveNLP result = %d\n", success);

      if( success )
         *result = SCIP_FOUNDSOL;

      /* go to the next cluster */
      start = end;
   }

TERMINATE:
   /* free memory */
   for( i = heurdata->nrndpoints - 1; i >= 0 ; --i )
   {
      assert(points[i] != NULL);
      SCIP_CALL( SCIPfreeSol(scip, &points[i]) );
   }

   SCIPhashmapFree(&varindex);
   SCIPfreeBufferArray(scip, &clusteridx);
   SCIPfreeBufferArray(scip, &violations);
   SCIPfreeBufferArray(scip, &points);

   return SCIP_OKAY;
}

/*
 * Callback methods of primal heuristic
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyMultistart)
{  /*lint --e{715}*/
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurMultistart(scip) );

   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeMultistart)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   /* free heuristic data */
   heurdata = SCIPheurGetData(heur);

   if( heurdata->exprinterpreter != NULL )
   {
      SCIP_CALL( SCIPexprintFree(&heurdata->exprinterpreter) );
   }

   SCIPfreeBlockMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitMultistart)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   SCIP_CALL( SCIPrandomCreate(&heurdata->randnumgen, SCIPblkmem(scip),
         SCIPinitializeRandomSeed(scip, DEFAULT_RANDSEED)) );

   return SCIP_OKAY;
}


/** deinitialization method of primal heuristic (called before transformed problem is freed) */
static
SCIP_DECL_HEUREXIT(heurExitMultistart)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   assert(heurdata->randnumgen != NULL);

   SCIPrandomFree(&heurdata->randnumgen);

   return SCIP_OKAY;
}


/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin) */
#if 0
static
SCIP_DECL_HEURINITSOL(heurInitsolMultistart)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of multistart primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define heurInitsolMultistart NULL
#endif


/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed) */
#if 0
static
SCIP_DECL_HEUREXITSOL(heurExitsolMultistart)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of multistart primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define heurExitsolMultistart NULL
#endif


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecMultistart)
{  /*lint --e{715}*/
   *result = SCIP_DIDNOTRUN;

   /* check cases for which the heuristic is not applicable */
   if( !SCIPisNLPConstructed(scip) || SCIPfindHeur(scip, "subnlp") == NULL )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTFIND;

   SCIP_CALL( applyHeur(scip, heur, result) );

   return SCIP_OKAY;
}


/*
 * primal heuristic specific interface methods
 */

/** creates the multistart primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurMultistart(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create multistart primal heuristic data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &heurdata) );
   BMSclearMemory(heurdata);

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecMultistart, heurdata) );

   assert(heur != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopyMultistart) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeMultistart) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitMultistart) );
   SCIP_CALL( SCIPsetHeurExit(scip, heur, heurExitMultistart) );
   SCIP_CALL( SCIPsetHeurInitsol(scip, heur, heurInitsolMultistart) );
   SCIP_CALL( SCIPsetHeurExitsol(scip, heur, heurExitsolMultistart) );

   /* add multistart primal heuristic parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/" HEUR_NAME "/nrndpoints",
         "number of random points generated per execution call",
         &heurdata->nrndpoints, FALSE, DEFAULT_NRNDPOINTS, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/" HEUR_NAME "/maxboundsize",
         "maximum variable domain size for unbounded variables",
         &heurdata->maxboundsize, FALSE, DEFAULT_MAXBOUNDSIZE, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/" HEUR_NAME "/nmaxiter",
         "number of iterations to reduce the maximum violation of a point",
         &heurdata->nmaxiter, FALSE, DEFAULT_NMAXITER, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/" HEUR_NAME "/minimprfac",
         "minimum required improving factor to proceed in improvement of a single point",
         &heurdata->minimprfac, FALSE, DEFAULT_MINIMPRFAC, -SCIPinfinity(scip), SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/" HEUR_NAME "/minimpriter",
         "number of iteration when checking the minimum improvement",
         &heurdata->minimpriter, FALSE, DEFAULT_MINIMPRITER, 1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/" HEUR_NAME "/maxreldist",
         "maximum distance between two points in the same cluster",
         &heurdata->maxreldist, FALSE, DEFAULT_MAXRELDIST, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "heuristics/" HEUR_NAME "/nlpminimpr",
         "factor by which heuristic should at least improve the incumbent",
         &heurdata->nlpminimpr, FALSE, DEFAULT_NLPMINIMPR, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/" HEUR_NAME "/maxncluster",
         "maximum number of considered clusters per heuristic call",
         &heurdata->maxncluster, FALSE, DEFAULT_MAXNCLUSTER, 0, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}
