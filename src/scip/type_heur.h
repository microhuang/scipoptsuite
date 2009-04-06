/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: type_heur.h,v 1.21 2009/04/06 13:07:06 bzfberth Exp $"

/**@file   type_heur.h
 * @brief  type definitions for primal heuristics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_TYPE_HEUR_H__
#define __SCIP_TYPE_HEUR_H__


/** heurstics execution timing flags */
#define SCIP_HEURTIMING_BEFORENODE        0x01 /**< call heuristic before the processing of the node starts */
#define SCIP_HEURTIMING_DURINGLPLOOP      0x02 /**< call heuristic after each LP solving during cut-and-price loop */
#define SCIP_HEURTIMING_AFTERLPLOOP       0x04 /**< call heuristic after the cut-and-price loop was finished */
#define SCIP_HEURTIMING_AFTERLPNODE       0x08 /**< call heuristic after the processing of a node with solved LP was
					        *   finished */
#define SCIP_HEURTIMING_AFTERPSEUDONODE   0x10 /**< call heuristic after the processing of a node without solved LP was
					        *   finished */
#define SCIP_HEURTIMING_AFTERLPPLUNGE     0x20 /**< call heuristic after the processing of the last node in the current
						*   plunge was finished, and only if the LP was solved for this node */
#define SCIP_HEURTIMING_AFTERPSEUDOPLUNGE 0x40 /**< call heuristic after the processing of the last node in the current
						*   plunge was finished, and only if the LP was not solved for this node */
#define SCIP_HEURTIMING_DURINGPRICINGLOOP 0x80 /**< call heuristic during pricing loop */

typedef unsigned int SCIP_HEURTIMING;

/** call heuristic after the processing of a node was finished */
#define SCIP_HEURTIMING_AFTERNODE (SCIP_HEURTIMING_AFTERLPNODE | SCIP_HEURTIMING_AFTERPSEUDONODE)

/** call heuristic after the processing of the last node in the current plunge was finished */
#define SCIP_HEURTIMING_AFTERPLUNGE (SCIP_HEURTIMING_AFTERLPPLUNGE | SCIP_HEURTIMING_AFTERPSEUDOPLUNGE)

typedef struct SCIP_Heur SCIP_HEUR;               /**< primal heuristic */
typedef struct SCIP_HeurData SCIP_HEURDATA;       /**< locally defined primal heuristic data */



/** destructor of primal heuristic to free user data (called when SCIP is exiting)
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 */
#define SCIP_DECL_HEURFREE(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur)

/** initialization method of primal heuristic (called after problem was transformed)
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 */
#define SCIP_DECL_HEURINIT(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur)

/** deinitialization method of primal heuristic (called before transformed problem is freed)
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 */
#define SCIP_DECL_HEUREXIT(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur)

/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin)
 *
 *  This method is called when the presolving was finished and the branch and bound process is about to begin.
 *  The primal heuristic may use this call to initialize its branch and bound specific data.
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 */
#define SCIP_DECL_HEURINITSOL(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur)

/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed)
 *
 *  This method is called before the branch and bound process is freed.
 *  The primal heuristic should use this call to clean up its branch and bound data.
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 */
#define SCIP_DECL_HEUREXITSOL(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur)

/** execution method of primal heuristic
 *
 *  Searches for feasible primal solutions. The method is called in the node processing loop.
 *
 *  input:
 *  - scip            : SCIP main data structure
 *  - heur            : the primal heuristic itself
 *  - heurtiming      : current point in the node solving loop
 *  - result          : pointer to store the result of the heuristic call
 *
 *  possible return values for *result:
 *  - SCIP_FOUNDSOL   : at least one feasible primal solution was found
 *  - SCIP_DIDNOTFIND : the heuristic searched, but did not find a feasible solution
 *  - SCIP_DIDNOTRUN  : the heuristic was skipped
 *  - SCIP_DELAYED    : the heuristic was skipped, but should be called again as soon as possible, disregarding
 *                      its frequency
 */
#define SCIP_DECL_HEUREXEC(x) SCIP_RETCODE x (SCIP* scip, SCIP_HEUR* heur, SCIP_HEURTIMING heurtiming, \
      SCIP_RESULT* result)




#include "scip/def.h"
#include "scip/type_scip.h"
#include "scip/type_result.h"


#endif
