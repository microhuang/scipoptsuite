/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2003 Tobias Achterberg                              */
/*                            Thorsten Koch                                  */
/*                  2002-2003 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cmain.c
 * @brief  main file for C compilation
 * @author Tobias Achterberg
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "scip.h"
#include "reader_cnf.h"
#include "reader_mps.h"
#include "disp_default.h"
#include "cons_and.h"
#include "cons_binpack.h"
#include "cons_bitstring.h"
#include "cons_eqknapsack.h"
#include "cons_integral.h"
#include "cons_invarknapsack.h"
#include "cons_knapsack.h"
#include "cons_linear.h"
#include "cons_logicor.h"
#include "cons_setppc.h"
#include "cons_varlb.h"
#include "cons_varub.h"
#include "presol_dualfix.h"
#include "nodesel_bfs.h"
#include "nodesel_dfs.h"
#include "nodesel_restartdfs.h"
#include "branch_fullstrong.h"
#include "branch_mostinf.h"
#include "branch_leastinf.h"
#include "heur_diving.h"
#include "heur_rounding.h"
#include "sepa_gomory.h"



static
RETCODE runSCIP(
   int              argc,
   char**           argv
   )
{
   SCIP* scip = NULL;

   SCIPprintVersion(NULL);

   /*********
    * Setup *
    *********/

   printf("\nsetup SCIP\n");
   printf("==========\n\n");

   /* initialize SCIP */
   CHECK_OKAY( SCIPcreate(&scip) );

   /* include user defined callbacks */
   CHECK_OKAY( SCIPincludeReaderCNF(scip) );
   CHECK_OKAY( SCIPincludeReaderMPS(scip) );
   CHECK_OKAY( SCIPincludeDispDefault(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrAnd(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrBitstring(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrIntegral(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrLinear(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrLogicor(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrSetppc(scip) );

#if 0
   CHECK_OKAY( SCIPincludeConsHdlrKnapsack(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrEqknapsack(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrInvarknapsack(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrBinpack(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrVarlb(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrVarub(scip) );
#endif

   CHECK_OKAY( SCIPincludePresolDualfix(scip) );
   CHECK_OKAY( SCIPincludeNodeselBfs(scip) );
   CHECK_OKAY( SCIPincludeNodeselDfs(scip) );
   CHECK_OKAY( SCIPincludeNodeselRestartdfs(scip) );
   CHECK_OKAY( SCIPincludeBranchruleFullstrong(scip) );
   CHECK_OKAY( SCIPincludeBranchruleMostinf(scip) );
   CHECK_OKAY( SCIPincludeBranchruleLeastinf(scip) );
   CHECK_OKAY( SCIPincludeHeurDiving(scip) );
   CHECK_OKAY( SCIPincludeHeurRounding(scip) );
   CHECK_OKAY( SCIPincludeSepaGomory(scip) );
   
   /*CHECK_OKAY( includeTestEventHdlr(scip) );*/


   /**************
    * Parameters *
    **************/

   /*CHECK_OKAY( SCIPwriteParams(scip, "scip.set", TRUE) );*/
   if( argc >= 3 )
   {
      if( SCIPfileExists(argv[2]) )
      {
         printf("reading parameter file <%s>\n", argv[2]);
         CHECK_OKAY( SCIPreadParams(scip, argv[2]) );
      }
      else
         printf("parameter file <%s> not found - using default parameters\n", argv[2]);
   }
   else if( SCIPfileExists("scip.set") )
   {
      printf("reading parameter file <scip.set>\n");
      CHECK_OKAY( SCIPreadParams(scip, "scip.set") );
   }



   /********************
    * Problem Creation *
    ********************/

   if( argc < 2 )
   {
      printf("syntax: %s <problem> [parameter file]\n", argv[0]);
      return SCIP_OKAY;
   }

#if 0 /*?????????????????????*/
   printf("\nread problem <%s>\n", argv[1]);
   printf("============\n\n");
   CHECK_OKAY( SCIPreadProb(scip, argv[1]) );
#else
   {
      VAR** vars;
      CONS* andcons;
      CONS* cons;
      char varname[255];
      int v;

      CHECK_OKAY( SCIPcreateProb(scip, "testprob", NULL, NULL, NULL) );
      
      CHECK_OKAY( SCIPallocMemoryArray(scip, &vars, 3) );
      for( v = 0; v < 3; ++v )
      {
         sprintf(varname, "x%d", v);
         CHECK_OKAY( SCIPcreateVar(scip, &vars[v], varname, 0.0, 10.0, -1.0, SCIP_VARTYPE_INTEGER, FALSE) );
         CHECK_OKAY( SCIPaddVar(scip, vars[v]) );
      }

      CHECK_OKAY( SCIPcreateConsAnd(scip, &andcons, "andcons", 0, NULL, TRUE, TRUE, FALSE, FALSE) );

      /* +3x0 -11x1 +4x2 <= 0 */
      CHECK_OKAY( SCIPcreateConsLinear(scip, &cons, "lincons1", 0, NULL, NULL, -SCIPinfinity(scip), 0.0,
                     FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[0], +3.0) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[1], -11.0) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[2], +4.0) );
      CHECK_OKAY( SCIPaddConsAnd(scip, andcons, cons) );
      CHECK_OKAY( SCIPreleaseCons(scip, &cons) );

      /* +2x0 +3x1 +1x2 <= 7 */
      CHECK_OKAY( SCIPcreateConsLinear(scip, &cons, "lincons2", 0, NULL, NULL, -SCIPinfinity(scip), 7.0,
                     FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[0], +2.0) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[1], +3.0) );
      CHECK_OKAY( SCIPaddCoefConsLinear(scip, cons, vars[2], +1.0) );
      CHECK_OKAY( SCIPaddConsAnd(scip, andcons, cons) );
      CHECK_OKAY( SCIPreleaseCons(scip, &cons) );

      CHECK_OKAY( SCIPaddCons(scip, andcons) );
      CHECK_OKAY( SCIPreleaseCons(scip, &andcons) );

      for( v = 0; v < 3; ++v )
      {
         CHECK_OKAY( SCIPreleaseVar(scip, &vars[v]) );
      }
      SCIPfreeMemoryArray(scip, &vars);

      /* bitstring constraint */
      CHECK_OKAY( SCIPcreateConsBitstring(scip, &cons, "bitstring", 19, -1.0, TRUE, TRUE, TRUE, TRUE, TRUE) );
      CHECK_OKAY( SCIPaddCons(scip, cons) );
      CHECK_OKAY( SCIPreleaseCons(scip, &cons) );
      {
         VAR* var;
         var = SCIPfindVar(scip, "bitstring_w1");
         CHECK_OKAY( SCIPchgVarUb(scip, var, 1.0) );
      }
   }
#endif


   /*******************
    * Problem Solving *
    *******************/

   /* solve problem */
   printf("\nsolve problem\n");
   printf("=============\n\n");
   CHECK_OKAY( SCIPsolve(scip) );

#if 1
   printf("\ntransformed primal solution:\n");
   printf("============================\n\n");
   CHECK_OKAY( SCIPprintBestTransSol(scip, NULL) );
#endif

#if 1
   printf("\nprimal solution:\n");
   printf("================\n\n");
   CHECK_OKAY( SCIPprintBestSol(scip, NULL) );
#endif

#ifndef NDEBUG
   /*SCIPdebugMemory(scip);*/
#endif


   /**************
    * Statistics *
    **************/

   printf("\nStatistics\n");
   printf("==========\n\n");

   CHECK_OKAY( SCIPprintStatistics(scip, NULL) );


   /********************
    * Deinitialization *
    ********************/

   printf("\nfree SCIP\n");
   printf("=========\n\n");

   /* free SCIP */
   CHECK_OKAY( SCIPfree(&scip) );


   /*****************************
    * Local Memory Deallocation *
    *****************************/

#ifndef NDEBUG
   memoryCheckEmpty();
#endif

   return SCIP_OKAY;
}

int
main(
   int              argc,
   char**           argv
   )
{
   RETCODE retcode;

   todoMessage("implement remaining events");
   todoMessage("avoid addition of identical rows");
   todoMessage("avoid addition of identical constraints");
   todoMessage("pricing for pseudo solutions");
   todoMessage("integrality check on objective function, abort if gap is below 1.0");
   todoMessage("implement reduced cost fixing");
   todoMessage("statistics: count domain reductions and constraint additions of constraint handlers");
   todoMessage("it's a bit ugly, that user call backs may be called before the nodequeue was processed");
   todoMessage("unboundness detection in presolving -> convert problem into feasibility problem to decide unboundness/infeasibility");
   todoMessage("variable event PSSOLCHANGED, update pseudo activities in constraints to speed up checking of pseudo solutions");

   retcode = runSCIP(argc, argv);
   if( retcode != SCIP_OKAY )
   {
      SCIPprintError(retcode, stderr);
      return -1;
   }

   return 0;
}
