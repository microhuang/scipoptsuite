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

/**@file   bandit_epsgreedy.h
 * @ingroup BanditAlgorithms
 * @brief  public methods for epsilon greedy bandit selection
 * @author Gregor Hendel
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BANDIT_EPSGREEDY_H__
#define __SCIP_BANDIT_EPSGREEDY_H__


#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@addtogroup PublicBanditMethods
 *
 * Epsilon greedy is a randomized algorithm for the multi-armed bandit problem.
 *
 * In every iteration, it either
 * selects an action uniformly at random with
 * probability \f$ \varepsilon_t\f$
 * or it greedily exploits the best action seen so far with
 * probability \f$ 1 - \varepsilon_t \f$.
 *
 * In this implementation, \f$ \varepsilon_t \f$ decreases over time
 * (number of selections performed), controlled by the epsilon parameter.
 *
 * @{
 */

/** creates the epsilon greedy bandit algorithm includes it in SCIP */
EXTERN
SCIP_RETCODE SCIPincludeBanditvtableEpsgreedy(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** create an epsilon greedy bandit selector with the necessary callbacks */
EXTERN
SCIP_RETCODE SCIPcreateBanditEpsgreedy(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BANDIT**         epsgreedy,          /**< pointer to store the epsilon greedy bandit algorithm */
   SCIP_Real             eps,                /**< probability for exploration between all actions */
   int                   nactions            /**< the number of possible actions */
   );

/** get weights array of epsilon greedy bandit algorithm */
EXTERN
SCIP_Real* SCIPgetWeightsEpsgreedy(
   SCIP_BANDIT*          epsgreedy           /**< epsilon greedy bandit algorithm */
   );

/** set epsilon parameter of epsilon greedy bandit algorithm */
EXTERN
void SCIPsetEpsilonEpsgreedy(
   SCIP_BANDIT*          epsgreedy,          /**< epsilon greedy bandit algorithm */
   SCIP_Real             eps                 /**< epsilon parameter (increase for more exploration) */
   );

/* @} */

/** callback to free bandit specific data structures */
extern
SCIP_DECL_BANDITFREE(SCIPbanditFreeEpsgreedy);

/** selection callback for bandit algorithm */
extern
SCIP_DECL_BANDITSELECT(SCIPbanditSelectEpsgreedy);

/** update callback for bandit algorithm */
extern
SCIP_DECL_BANDITUPDATE(SCIPbanditUpdateEpsgreedy);

/** reset callback for bandit algorithm */
extern
SCIP_DECL_BANDITRESET(SCIPbanditResetEpsgreedy);


/** internal method to create an epsilon greedy bandit algorithm */
extern
SCIP_RETCODE SCIPbanditCreateEpsgreedy(
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_BANDITVTABLE*    vtable,             /**< virtual function table with epsilon greedy callbacks */
   SCIP_BANDIT**         epsgreedy,          /**< pointer to store the epsilon greedy bandit algorithm */
   SCIP_Real             eps,                /**< probability for exploration between all actions */
   int                   nactions,           /**< the number of possible actions */
   unsigned int          initseed            /**< initial random seed */
   );

#ifdef __cplusplus
}
#endif

#endif
