/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   struct_decomp.h
 * @ingroup INTERNALAPI
 * @brief  data structures for decomposition
 * @author Gregor Hendel
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef SRC_SCIP_STRUCT_DECOMP_H_
#define SRC_SCIP_STRUCT_DECOMP_H_

#include "scip/type_misc.h"


#ifdef __cplusplus
extern "C" {
#endif

/** decomposition data structure */
struct SCIP_Decomp
{
   SCIP_HASHMAP*         var2block;          /**< hash map from SCIP variables to block labels */
   SCIP_HASHMAP*         cons2block;         /**< hash map from SCIP constraints to block labels */
   SCIP_Real             score;              /**< score of this decomposition */
   int                   nblocks;            /**< the number of variable blocks without the linking block */
   SCIP_Bool             haschanges;         /**< has this decomposition pending data structure updates? */
};

#ifdef __cplusplus
}
#endif

#endif
