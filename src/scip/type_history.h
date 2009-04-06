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
#pragma ident "@(#) $Id: type_history.h,v 1.15 2009/04/06 13:07:06 bzfberth Exp $"

/**@file   type_history.h
 * @brief  type definitions for branching and inference history
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_TYPE_HISTORY_H__
#define __SCIP_TYPE_HISTORY_H__


/** branching direction for branching on variables */
enum SCIP_BranchDir
{
   SCIP_BRANCHDIR_DOWNWARDS = 0,        /**< downwards branching: decreasing upper bound */
   SCIP_BRANCHDIR_UPWARDS   = 1,        /**< upwards branching: increasing lower bound */
   SCIP_BRANCHDIR_AUTO      = 2         /**< automatic setting for choosing bound changes */
};
typedef enum SCIP_BranchDir SCIP_BRANCHDIR;       /**< branching direction for branching on variables */

typedef struct SCIP_History SCIP_HISTORY;         /**< branching and inference history information for single variable */


#endif
