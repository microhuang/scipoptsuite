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
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   reader_tim.h
 * @ingroup FILEREADERS
 * @brief  TIM file reader - the stage information for a stochastic programming instance in SMPS format
 * @author Stephen J. Maher
 *
 * This is a reader for the time file of a stochastic programming instance in SMPS format.
 * The three files that must be read are:
 * - .cor
 * - .tim
 * - .sto
 *
 * Alternatively, it is possible to create a .smps file with the relative path to the .cor, .tim and .sto files.
 * A file reader is available for the .smps file.
 *
 * Details regarding the SMPS file format can be found at:
 * Birge, J. R.; Dempster, M. A.; Gassmann, H. I.; Gunn, E.; King, A. J. & Wallace, S. W.
 * A standard input format for multiperiod stochastic linear programs
 * IIASA, Laxenburg, Austria, WP-87-118, 1987
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_READER_TIM_H__
#define __SCIP_READER_TIM_H__


#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** includes the tim file reader into SCIP
 *
 *  @ingroup FileReaderIncludes
 */
EXTERN
SCIP_RETCODE SCIPincludeReaderTim(
   SCIP*                 scip                /**< SCIP data structure */
   );

/**@addtogroup FILEREADERS
 *
 * @{
 */

/** reads the stage information for a stochastic programming instance in SMPS format */
EXTERN
SCIP_RETCODE SCIPreadTim(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           filename,           /**< full path and name of file to read, or NULL if stdin should be used */
   SCIP_RESULT*          result              /**< pointer to store the result of the file reading call */
   );

/* @} */

/*
 * Interface methods for the cor and sto files
 */

/* return whether the tim file has been read */
EXTERN
SCIP_Bool SCIPtimHasRead(
   SCIP_READER*          reader              /**< the file reader itself */
   );

/* returns the number of stages */
EXTERN
int SCIPtimGetNStages(
   SCIP*                 scip                /**< SCIP data structure */
   );

/* returns the name for a given stage */
EXTERN
const char* SCIPtimGetStageName(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   stagenum            /**< the number of the requested stage */
   );

/* returns the stage name for a given constraint name */
const char* SCIPtimConsGetStageName(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           consname            /**< the constraint to search for */
   );

/* returns the number for a given stage */
EXTERN
int SCIPtimFindStage(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           stage               /**< the name of the requested stage */
   );

/* returns the array of variables for a given stage */
EXTERN
SCIP_VAR** SCIPtimGetStageVars(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   stagenum            /**< the number of the requested stage */
   );

/* returns an array of constraints for a given stage */
EXTERN
SCIP_CONS** SCIPtimGetStageConss(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   stagenum            /**< the number of the requested stage */
   );

/* returns the number of variables for a given stage */
EXTERN
int SCIPtimGetStageNVars(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   stagenum            /**< the number of the requested stage */
   );

/* returns the number of constraints for a given stage */
EXTERN
int SCIPtimGetStageNConss(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   stagenum            /**< the number of the requested stage */
   );

#ifdef __cplusplus
}
#endif

#endif
