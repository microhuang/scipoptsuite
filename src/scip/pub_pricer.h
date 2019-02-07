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

/**@file   pub_pricer.h
 * @ingroup PUBLICCOREAPI
 * @brief  public methods for variable pricers
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_PUB_PRICER_H__
#define __SCIP_PUB_PRICER_H__


#include "scip/def.h"
#include "scip/type_misc.h"
#include "scip/type_pricer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@addtogroup PublicPricerMethods
 *
 * @{
 */

/** compares two pricers w. r. to their priority */
SCIP_EXPORT extern
SCIP_DECL_SORTPTRCOMP(SCIPpricerComp);

/** comparison method for sorting pricers w.r.t. to their name */
SCIP_EXPORT extern
SCIP_DECL_SORTPTRCOMP(SCIPpricerCompName);

/** gets user data of variable pricer */
SCIP_EXPORT extern
SCIP_PRICERDATA* SCIPpricerGetData(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** sets user data of variable pricer; user has to free old data in advance! */
SCIP_EXPORT extern
void SCIPpricerSetData(
   SCIP_PRICER*          pricer,             /**< variable pricer */
   SCIP_PRICERDATA*      pricerdata          /**< new variable pricer user data */
   );

/** gets name of variable pricer */
SCIP_EXPORT extern
const char* SCIPpricerGetName(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets description of variable pricer */
SCIP_EXPORT extern
const char* SCIPpricerGetDesc(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets priority of variable pricer */
SCIP_EXPORT extern
int SCIPpricerGetPriority(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets the number of times, the pricer was called and tried to find a variable with negative reduced costs */
SCIP_EXPORT extern
int SCIPpricerGetNCalls(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets the number of variables with negative reduced costs found by this pricer */
SCIP_EXPORT extern
int SCIPpricerGetNVarsFound(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets time in seconds used in this pricer for setting up for next stages */
SCIP_EXPORT extern
SCIP_Real SCIPpricerGetSetupTime(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** gets time in seconds used in this pricer */
SCIP_EXPORT extern
SCIP_Real SCIPpricerGetTime(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** returns whether the given pricer is in use in the current problem */
SCIP_EXPORT extern
SCIP_Bool SCIPpricerIsActive(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** returns whether the pricer should be delayed until no other pricer finds a new variable */
SCIP_EXPORT extern
SCIP_Bool SCIPpricerIsDelayed(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/** is variable pricer initialized? */
SCIP_EXPORT extern
SCIP_Bool SCIPpricerIsInitialized(
   SCIP_PRICER*          pricer              /**< variable pricer */
   );

/* @} */

#ifdef __cplusplus
}
#endif

#endif
