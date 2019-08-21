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

/**@file   disable_upgrades.c
 * @brief  tests for or-constraint methods with upgrades to and-constraints disabled
 * @author Gregor Hendel
 * @author Helena Müller
 */

#include "scip/scip.h"
#include "include/scip_test.h"
#include "scip/scipdefplugins.h"
#include "scip/cons_or.h"
#include "scip/pub_cons.h"
#include <stdio.h>

/** GLOBAL VARIABLES **/
static SCIP* scip = NULL;


/* TEST SUITE */

/** the setup creates the necessary source SCIP, sets parameter */
static
void setup(void)
{
   SCIP_CALL( SCIPcreate(&scip) );

   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

   SCIP_CALL( SCIPsetIntParam(scip, "constraints/or/maxprerounds", 0) );
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrestarts", 0) );
   SCIP_CALL( SCIPsetIntParam(scip, "branching/inference/priority", 1000000) );

   SCIP_CALL( SCIPsetHeuristics(scip, SCIP_PARAMSETTING_OFF, FALSE) );
}

/** free all allocated memory */
static
void teardown(void)
{
   SCIP_CALL( SCIPfree(&scip) );

}


TestSuite(disable_upgrades, .init = setup, .fini = teardown);

/* TESTS  */
Test(disable_upgrades, create_and_free)
{
   /* calls setup and teardown */
}

Test(disable_upgrades, disable_upgrades_or, .description="disable upgrades of or-constraints to and-constraints to keep or-constraints during solution process")
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONS** conss;
   int nconss;
   int i;

   SCIP_CALL( SCIPreadProb(scip, "../check/instances/Or/or_constraint.cip", "cip") );

   conshdlr = SCIPfindConshdlr(scip, "or");

   nconss = SCIPgetNOrigConss(scip);
   conss = SCIPgetOrigConss(scip);

   cr_assert_eq(nconss, 8);

   /* set or-constraints to be modifiable */
   for( i = 0; i < nconss; ++i )
   {
      if( SCIPconsGetHdlr(conss[i]) == conshdlr )
         SCIPsetConsModifiable(scip, conss[i], TRUE);
   }

   SCIP_CALL( SCIPchgVarUbGlobal(scip, SCIPgetVars(scip)[3], 0.0) );
   SCIP_CALL( SCIPchgVarUbGlobal(scip, SCIPgetVars(scip)[4], 0.0) );
   SCIP_CALL( SCIPchgVarUbGlobal(scip, SCIPgetVars(scip)[2], 0.0) );
   SCIP_CALL( SCIPchgVarUbGlobal(scip, SCIPgetVars(scip)[1], 0.0) );

   SCIP_CALL( SCIPsetIntParam(scip, "constraints/or/maxprerounds", 1) );

   SCIP_CALL( SCIPsolve(scip) );
}

Test(disable_upgrades, write_problem, .description="test that CIP write method works for or constraints")
{
   SCIP_CALL( SCIPreadProb(scip, "../check/instances/Or/or_constraint.cip", "cip") );

   SCIP_CALL( SCIPwriteOrigProblem(scip, NULL, "cip", FALSE) );
}

Test(disable_upgrades, copy_problem, .description="test copying of or-constraints")
{
   SCIP* targetscip;
   SCIP_Bool valid;
   SCIP_CALL( SCIPreadProb(scip, "../check/instances/Or/or_constraint.cip", "cip") );

   SCIP_CALL( SCIPcreate(&targetscip) );

   SCIP_CALL( SCIPcopy(scip, targetscip, NULL, NULL, "copy_of_prob", TRUE, TRUE, FALSE, FALSE, &valid) );

   cr_assert(valid);

   SCIP_CALL( SCIPsolve(targetscip) );

   SCIP_CALL( SCIPfree(&targetscip) );
}

Test(disable_upgrades, test_basic_creation, .description="test SCIPcreateConsBasicOr")
{
   SCIP_CONS* newcons;
   SCIP_VAR** origvars;

   SCIP_CALL( SCIPreadProb(scip, "../check/instances/Or/or_constraint.cip", "cip") );

   cr_assert(SCIPgetNVars(scip) == 24);
   cr_assert(SCIPgetNBinVars(scip) == 24);

   origvars = SCIPgetOrigVars(scip);

   SCIP_CALL( SCIPcreateConsBasicOr(scip, &newcons, "new_or_constraint", origvars[3], 6, &origvars[4]) );

   SCIP_CALL( SCIPaddCons(scip, newcons) );

   SCIP_CALL( SCIPreleaseCons(scip, &newcons) );

   SCIP_CALL( SCIPsolve(scip) );

   SCIP_CALL( SCIPprintBestSol(scip, NULL, TRUE) );

}
