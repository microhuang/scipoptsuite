/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2014 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   pub_heur.h
 * @ingroup PUBLICMETHODS
 * @brief  public methods for primal heuristics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_PUB_HEUR_H__
#define __SCIP_PUB_HEUR_H__


#include "scip/def.h"
#include "scip/type_misc.h"
#include "scip/type_heur.h"

#ifdef __cplusplus
extern "C" {
#endif

/** compares two heuristics w. r. to their priority */
EXTERN
SCIP_DECL_SORTPTRCOMP(SCIPheurComp);

/** comparison method for sorting heuristics w.r.t. to their name */
EXTERN
SCIP_DECL_SORTPTRCOMP(SCIPheurCompName);

/** gets user data of primal heuristic */
EXTERN
SCIP_HEURDATA* SCIPheurGetData(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** sets user data of primal heuristic; user has to free old data in advance! */
EXTERN
void SCIPheurSetData(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_HEURDATA*        heurdata            /**< new primal heuristic user data */
   );

/** gets name of primal heuristic */
EXTERN
const char* SCIPheurGetName(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets description of primal heuristic */
EXTERN
const char* SCIPheurGetDesc(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets display character of primal heuristic */
EXTERN
char SCIPheurGetDispchar(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** returns the timing mask of the heuristic */
EXTERN
SCIP_HEURTIMING SCIPheurGetTimingmask(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** sets new timing mask for heuristic */
EXTERN
void SCIPheurSetTimingmask(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_HEURTIMING       timingmask          /**< new timing mask of heuristic */
   );

/** does the heuristic use a secondary SCIP instance? */
EXTERN
SCIP_Bool SCIPheurUsesSubscip(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets priority of primal heuristic */
EXTERN
int SCIPheurGetPriority(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets frequency of primal heuristic */
EXTERN
int SCIPheurGetFreq(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** sets frequency of primal heuristic */
EXTERN
void SCIPheurSetFreq(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   int                   freq                /**< new frequency of heuristic */
   );

/** gets frequency offset of primal heuristic */
EXTERN
int SCIPheurGetFreqofs(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets maximal depth level for calling primal heuristic (returns -1, if no depth limit exists) */
EXTERN
int SCIPheurGetMaxdepth(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets the number of times, the heuristic was called and tried to find a solution */
EXTERN
SCIP_Longint SCIPheurGetNCalls(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets the number of primal feasible solutions found by this heuristic */
EXTERN
SCIP_Longint SCIPheurGetNSolsFound(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets the number of new best primal feasible solutions found by this heuristic */
EXTERN
SCIP_Longint SCIPheurGetNBestSolsFound(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** is primal heuristic initialized? */
EXTERN
SCIP_Bool SCIPheurIsInitialized(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets time in seconds used in this heuristic for setting up for next stages */
EXTERN
SCIP_Real SCIPheurGetSetupTime(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** gets time in seconds used in this heuristic */
EXTERN
SCIP_Real SCIPheurGetTime(
   SCIP_HEUR*            heur                /**< primal heuristic */
   );

/** get the heuristic to which this diving setting belongs */
EXTERN
SCIP_HEUR* SCIPdivesetGetHeur(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the minimum relative depth of the diving settings */
EXTERN
SCIP_Real SCIPdivesetGetMinRelDepth(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the maximum relative depth of the diving settings */
EXTERN
SCIP_Real SCIPdivesetGetMaxRelDepth(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the number of successful runs of the diving settings */
EXTERN
int SCIPdivesetGetNSuccess(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the maximum LP iterations quotient of the diving settings */
EXTERN
SCIP_Real SCIPdivesetGetMaxLPIterQuot(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the maximum LP iterations offset of the diving settings */
EXTERN
int SCIPdivesetGetMaxLPIterOffset(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the number of LP iterations of the diving settings */
EXTERN
SCIP_Longint SCIPdivesetGetNLPIterations(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the maximum upper bound quotient parameter of the diving settings if no solution is available */
EXTERN
SCIP_Real SCIPdivesetGetMaxUbQuotNoSol(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the average quotient parameter of the diving settings if no solution is available */
EXTERN
SCIP_Real SCIPdivesetGetMaxAvgQuotNoSol(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the maximum upper bound quotient parameter of the diving settings if an incumbent solution exists */
EXTERN
SCIP_Real SCIPdivesetGetMaxUbQuot(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** get the average upper bound quotient parameter of the diving settings if an incumbent solution exists */
EXTERN
SCIP_Real SCIPdivesetGetMaxAvgQuot(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** should backtracking be applied? */
EXTERN
SCIP_Bool SCIPdivesetUseBacktrack(
   SCIP_DIVESET*         diveset             /**< diving settings */
   );

/** increases LP iterations counter of diving settings */
EXTERN
void SCIPdivesetIncreaseNLPIterations(
   SCIP_DIVESET*         diveset,            /**< diving settings */
   SCIP_Longint          niterstoadd         /**< additional number of LP iterations to be added */
   );

/** increases success counter of diving settings by 1 */
EXTERN
void SCIPdivesetIncreaseNSuccess(
   SCIP_DIVESET*         diveset             /**< diving settings which happened to earn the reward */
   );

/** frees memory of a diveset */
EXTERN
SCIP_RETCODE SCIPdivesetFree(
   SCIP_DIVESET**        diveset             /**< general diving settings */
   );

#ifdef __cplusplus
}
#endif

#endif
