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

/**@file   reader_cor.h
 * @ingroup FILEREADERS
 * @brief  COR file reader (MPS format of the core problem for stochastic programs)
 * @author Stephen J. Maher
 *
 * This is a reader for the core file of a stochastic programming instance in SMPS format.
 * The three files that must be read are:
 * - .cor
 * - .tim
 * - .sto
 *
 * Alternatively, it is possible to create a .smps file with the relative path to the .cor, .tim and .sto files.
 * A file reader is available for the .smps file.
 *
 * The core file is in the form of an MPS.
 *
 * Details regarding the SMPS file format can be found at:
 * Birge, J. R.; Dempster, M. A.; Gassmann, H. I.; Gunn, E.; King, A. J. & Wallace, S. W.
 * A standard input format for multiperiod stochastic linear programs
 * IIASA, Laxenburg, Austria, WP-87-118, 1987
 *
 */


/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_READER_COR_H__
#define __SCIP_READER_COR_H__


#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** includes the cor file reader into SCIP
 *
 *  @ingroup FileReaderIncludes
 */
EXTERN
SCIP_RETCODE SCIPincludeReaderCor(
   SCIP*                 scip                /**< SCIP data structure */
   );

/**@addtogroup FILEREADERS
 *
 * @{
 */

/** reads problem from file */
EXTERN
SCIP_RETCODE SCIPreadCor(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           filename,           /**< full path and name of file to read, or NULL if stdin should be used */
   SCIP_RESULT*          result              /**< pointer to store the result of the file reading call */
   );

/** writes problem to file */
EXTERN
SCIP_RETCODE SCIPwriteCor(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_READER*          reader,             /**< the file reader itself */
   FILE*                 file,               /**< output file, or NULL if standard output should be used */
   const char*           name,               /**< problem name */
   SCIP_Bool             transformed,        /**< TRUE iff problem is the transformed problem */
   SCIP_OBJSENSE         objsense,           /**< objective sense */
   SCIP_Real             objscale,           /**< scalar applied to objective function; external objective value is
                                              * extobj = objsense * objscale * (intobj + objoffset) */
   SCIP_Real             objoffset,          /**< objective offset from bound shifting and fixing */
   SCIP_VAR**            vars,               /**< array with active variables ordered binary, integer, implicit, continuous */
   int                   nvars,              /**< number of active variables in the problem */
   int                   nbinvars,           /**< number of binary variables */
   int                   nintvars,           /**< number of general integer variables */
   int                   nimplvars,          /**< number of implicit integer variables */
   int                   ncontvars,          /**< number of continuous variables */
   SCIP_VAR**            fixedvars,          /**< array with fixed and aggregated variables */
   int                   nfixedvars,         /**< number of fixed and aggregated variables in the problem */
   SCIP_CONS**           conss,              /**< array with constraints of the problem */
   int                   nconss,             /**< number of constraints in the problem */
   SCIP_Bool             genericnames,       /**< should generic names be used for the variables and constraints? */
   SCIP_RESULT*          result              /**< pointer to store the result of the file writing call */
   );

/*
 * Interface method for the tim and sto readers
 */

/** returns whether the COR file has been successfully read. This is used by the TIM and STO readers. */
EXTERN
SCIP_Bool SCIPcorHasRead(
   SCIP_READER*          reader              /**< the file reader itself */
   );

/** returns the number of variable names in the COR problem */
EXTERN
int SCIPcorGetNVarNames(
   SCIP_READER*          reader              /**< the file reader itself */
   );

/** returns the number of constraint names in the COR problem */
EXTERN
int SCIPcorGetNConsNames(
   SCIP_READER*          reader              /**< the file reader itself */
   );

/** returns the variable name for the given index */
EXTERN
const char* SCIPcorGetVarName(
   SCIP_READER*          reader,             /**< the file reader itself */
   int                   i                   /**< the index of the variable that is requested */
   );

/** returns the constraint name for the given index */
EXTERN
const char* SCIPcorGetConsName(
   SCIP_READER*          reader,             /**< the file reader itself */
   int                   i                   /**< the index of the constraint that is requested */
   );

/* @} */

#ifdef __cplusplus
}
#endif

#endif
