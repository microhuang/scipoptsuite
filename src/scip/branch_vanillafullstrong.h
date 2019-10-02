/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2019 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   branch_vanillafullstrong.h
 * @ingroup BRANCHINGRULES
 * @brief  vanilla full strong LP branching rule
 * @author Tobias Achterberg
 * @author Maxime Gasse
 *
 * The vanilla full strong branching rule is a purged implementation of full strong branching, for academic purposes.
 * It implements full strong branching with the following specific features:
 * - no cutoff or domain reduction: only branching.
 * - idempotent (optional): leave SCIP, as much as possible, in the same state before / after the strong branching
 *   calls. Basically, do not update any statistic.
 * - donotbranch (optional): do no perform branching. So that the brancher can be called as an oracle only (on which
 *   variable would you branch ? But do not branch please).
 * - scoreall (optional): continue scoring variables, even if infeasibility is detected along the way.
 * - collectscores (optional): store the candidate scores from the last call, which can then be retrieved by calling
 *   SCIPgetVanillafullstrongData().
 * - integralcands (optional): get candidates from SCIPgetPseudoBranchCands() instead of SCIPgetLPBranchCands(), i.e.,
 *   consider all non-fixed variables as branching candidates, not only fractional ones.
 *
 * For a more mathematical description and a comparison between the strong branching rule and other branching rules
 * in SCIP, we refer to
 *
 * @par
 * Tobias Achterberg@n
 * Constraint Integer Programming@n
 * PhD Thesis, Technische Universität Berlin, 2007@n
 *
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BRANCH_VANILLAFULLSTRONG_H__
#define __SCIP_BRANCH_VANILLAFULLSTRONG_H__


#include "scip/def.h"
#include "scip/type_result.h"
#include "scip/type_retcode.h"
#include "scip/type_scip.h"
#include "scip/type_var.h"

#ifdef __cplusplus
extern "C" {
#endif

/** creates the vanilla full strong branching rule and includes it in SCIP
 *
 *  @ingroup BranchingRuleIncludes
 */
SCIP_EXPORT
SCIP_RETCODE SCIPincludeBranchruleVanillafullstrong(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** recovers candidate variables and their scores from last vanilla full
 * strong branching call */
SCIP_EXPORT
SCIP_RETCODE SCIPgetVanillafullstrongData(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR***           cands,              /**< candidate variables */
   SCIP_Real**           candscores,         /**< candidate scores */
   int*                  ncands,             /**< number of candidates */
   int*                  npriocands,         /**< number of priority candidates */
   int*                  bestcand            /**< best branching candidate */
   );


#ifdef __cplusplus
}
#endif

#endif
