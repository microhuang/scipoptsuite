/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2013 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cmain.c
 * @brief  Main file for binpacking pricing example
 * @author Timo Berthold
 * @author Stefan Heinz
 *
 *  This the file contains the \ref main() main function of the projects. This includes all the default plugins of
 *  \SCIP and the once which belong to that projects. After that is starts the interactive shell of \SCIP or processes
 *  the shell arguments if given.
 */
#include <stdio.h>

#include "scip/scip.h"
#include "scip/scipshell.h"
#include "scip/scipdefplugins.h"

#include "reader_stp.h"
#include "cons_stp.h"
#include "heur_tm.h"
#include "heur_local.h"
#include "heur_rs.h"
#include "pricer_stp.h"

/** creates a SCIP instance with default plugins, evaluates command line parameters, runs SCIP appropriately,
 *  and frees the SCIP instance
 */
static
SCIP_RETCODE runShell(
   int                        argc,               /**< number of shell parameters */
   char**                     argv,               /**< array with shell parameters */
   const char*                defaultsetname      /**< name of default settings file */
   )
{
   SCIP* scip = NULL;

   /*********
    * Setup *
    *********/

   /* initialize SCIP */
   SCIP_CALL( SCIPcreate(&scip) );

   /* include stp pricer */
   SCIP_CALL( SCIPincludePricerStp(scip) );

   /* include steiner tree reader */
   SCIP_CALL( SCIPincludeReaderStp(scip) );

   /* include default SCIP plugins */
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

   /* include steiner tree constraint handler */
   SCIP_CALL( SCIPincludeConshdlrStp(scip) );

   /* include Takahashi Matsuyama heuristic */
   SCIP_CALL( SCIPincludeHeurTM(scip) );
#if 0

   /* include Rayward-Smith heuristic */
   SCIP_CALL( SCIPincludeHeurRS(scip) );
#endif

   /* include local heuristics */
   SCIP_CALL( SCIPincludeHeurLocal(scip) );

   /* for column generation instances, disable restarts */
   SCIP_CALL( SCIPsetIntParam(scip,"presolving/maxrestarts",0) );

   /**********************************
    * Process command line arguments *
    **********************************/
   SCIP_CALL( SCIPprocessShellArguments(scip, argc, argv, defaultsetname) );

   /********************
    * Deinitialization *
    ********************/

   SCIP_CALL( SCIPfree(&scip) );

   BMScheckEmptyMemory();

   return SCIP_OKAY;
}

int
main(
   int                        argc,
   char**                     argv
   )
{
   SCIP_RETCODE retcode;

   retcode = runShell(argc, argv, "scip.set");
   if( retcode != SCIP_OKAY )
   {
      SCIPprintError(retcode);
      return -1;
   }

   return 0;
}
