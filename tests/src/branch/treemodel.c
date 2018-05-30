/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   treemodel.c
 * @brief  unit test for treemodel variable selection rules
 * @author Daniel Anderson
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <scip/scip.h>
#include <scip/scipdefplugins.h>
#include <scip/treemodel.h>

#include <scip/struct_scip.h>
#include <scip/struct_set.h>
#include <scip/struct_primal.h>
#include <scip/struct_tree.h>
#include <scip/struct_var.h>
#include <scip/struct_history.h>
#include <scip/type_set.h>

#include "include/scip_test.h"

/* DATA STRUCTURES */

/** parameters required by the Treemodel branching rules */
struct SCIP_Treemodel
{
   SCIP_Bool            enabled;             /**< should candidate branching variables be scored using the Treemodel rule? */
   char                 highrule;            /**< scoring function to use at nodes predicted to be high in the tree. ('d'efault, 's'vts, 'r'atio, 't'ree sample) */
   char                 lowrule;             /**< scoring function to use at nodes predicted to be low in the tree ('d'efault, 's'vts, 'r'atio, 't'ree sample) */
   int                  height;              /**< estimated tree height at which we switch from using the low rule to the high rule */
   char                 filterhigh;          /**< should dominated candidates be filtered before using the high scoring function? ('a'uto, 't'rue, 'f'alse) [ADVANCED] */
   char                 filterlow;           /**< should dominated candidates be filtered before using the low scoring function? ('a'uto, 't'rue, 'f'alse) [ADVANCED] */
   int                  maxfpiter;           /**< maximum number of fixed-point iterations when computing the ratio [ADVANCED] */
   int                  maxsvtsheight;       /**< maximum height to compute the SVTS score exactly before approximating [ADVANCED] */
   char                 fallbackinf;         /**< which method should be used as a fallback if the tree size estimates are infinite? ('d'efault, 'r'atio) [ADVANCED] */
   char                 fallbacknoprim;      /**< which method should be used as a fallback if there is no primal bound available? ('d'efault, 'r'atio) [ADVANCED] */
   SCIP_Real            smallpscost;         /**< threshold at which pseudocosts are considered small, making hybrid scores more likely to be the deciding factor in branching [ADVANCED] */
};

/* GLOBAL VARIABLES */
static SCIP* scip;

/* Mock SCIP data structures */
static SCIP_TREE tree;
static SCIP_PRIMAL primal;
static SCIP_NODE node;
static SCIP_NODE* path;

/* Placeholders */
static SCIP_TREE* old_tree;
static SCIP_PRIMAL* old_primal;
static SCIP_STAGE old_stage;

/* Data for variable selection test */
SCIP_VAR** branchcands;
SCIP_Real* mingains;
SCIP_Real* maxgains;
SCIP_Real* scoresfrompc;
SCIP_Real* scoresfromothers;
SCIP_Real avgpscostscore;
int nbranchcands;

/* TEST SUITES */

/** setup SCIP for test run */
static
void setup(void) {
   scip = NULL;
   SCIP_CALL(SCIPcreate(&scip));
}

/** setup SCIP with a problem to solve */
static
void setup_problem(void)
{
   setup();

   /* include default SCIP plugins */
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

   /* create a problem */
   SCIP_CALL( SCIPcreateProbBasic(scip, "problem") );

   /* set the msghdlr off */
   SCIPsetMessagehdlrQuiet(scip, TRUE);

   /* Solve the problem to make SCIP initialise all of its internals */
   SCIPsolve(scip);

   /** Record what we are about to overwrite with mocks */
   old_tree = scip->tree;
   old_primal = scip->primal;
   old_stage = scip->set->stage;

   /** SCIP must be in solving stage to test branching */
   scip->set->stage = SCIP_STAGE_SOLVING;

   /** Mock the B&B tree to set the lower bound to zero */
   tree.root = &node;
   tree.focusnode = &node;
   tree.pathlen = 1;
   tree.path = &path;
   tree.cutoffdepth = INT_MAX;
   tree.repropdepth = INT_MAX;
   node.lowerbound = 0.0;
   node.depth = 0;
   node.active = TRUE;
   path = &node;
   scip->tree = &tree;

   /** Set a mock primal solution to mock the dual gap */
   scip->primal = &primal;
}

/** deinitialization method */
static
void teardown(void)
{
   /** Remove mocks */
   scip->set->stage = old_stage;
   scip->tree = old_tree;
   scip->primal = old_primal;

   SCIP_CALL( SCIPfree(&scip) );

   cr_assert_null(scip, "SCIP data structure is not null after being freed");
   cr_assert_eq(BMSgetMemoryUsed(), 0, "There is a memory leak!!");
}

TestSuite(branch_treemodel, .init = setup, .fini = teardown);
TestSuite(branch_treemodel_select, .init = setup_problem, .fini = teardown);

/* Set up required background */

/** Mock the dual gap */
static
void setDualGap(
   SCIP_Real            gap      /**< The desired dual gap */
)
{
   cr_assert_not_null(scip, "SCIP data structure is NULL");
   cr_assert_geq(gap, 0, "Dual gap must be non-negative");

   /** Set the primal bound to gap */
   primal.upperbound = gap;
}

/** Set up data for variable selection rule */
static
void setupTestVars(
   int ncands,     /**< Number of branching candidates */
   ...                     /**< Groups of four for each candidate indicating (mingain, maxgain, pscostscore, otherscore) */
)
{
   cr_assert_geq(ncands, 0, "Must initialise a non-zero number of variables");

   int c;
   va_list args;
   va_start(args, ncands);
   nbranchcands = ncands;
   avgpscostscore = 0.0;

   /** Allocate lp gains and scores */
   SCIP_CALL( SCIPallocBufferArray(scip, &mingains, nbranchcands) );
   SCIP_CALL( SCIPallocBufferArray(scip, &maxgains, nbranchcands) );
   SCIP_CALL( SCIPallocBufferArray(scip, &scoresfrompc, nbranchcands) );
   SCIP_CALL( SCIPallocBufferArray(scip, &scoresfromothers, nbranchcands) );

   for( c = 0; c < ncands; c++ )
   {
      mingains[c] = va_arg(args, double);
      maxgains[c] = va_arg(args, double);
      scoresfrompc[c] = va_arg(args, double);
      scoresfromothers[c] = va_arg(args, double);
      avgpscostscore += scoresfrompc[c];
   }

   avgpscostscore /= ncands;

   va_end(args);

   /** Allocate SCIP variables */
   SCIP_CALL( SCIPallocBufferArray(scip, &branchcands, nbranchcands) );

   /** Create variable history */
   for( c = 0; c < ncands; c++ )
   {
      SCIP_CALL( SCIPallocBlockMemory(scip, &branchcands[c]) );
      SCIP_CALL( SCIPallocBlockMemory(scip, &branchcands[c]->history) );
      branchcands[c]->history->ratiovalid = FALSE;
   }

}

/** Free test data from variable selection rules */
static
void freeTestVars(void)
{
   int c;

   /** Deallocate variables */
   for( c = 0; c < nbranchcands; c++ )
   {
      SCIPfreeBlockMemory(scip, &branchcands[c]->history);
      SCIPfreeBlockMemory(scip, &branchcands[c]);
   }

   SCIPfreeBufferArray(scip, &branchcands);

   /** Deallocate lp gains */
   SCIPfreeBufferArray(scip, &scoresfromothers);
   SCIPfreeBufferArray(scip, &scoresfrompc);
   SCIPfreeBufferArray(scip, &maxgains);
   SCIPfreeBufferArray(scip, &mingains);
}

/* TESTS */

/** Test the API function SCIPtreemodelIsEnabled */
Test(branch_treemodel, IsEnabled)
{
   cr_assert_not_null(scip, "SCIP data structure is NULL");

   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;

   cr_assert_eq(SCIPtreemodelIsEnabled(scip, &treemodel), TRUE, "Treemodel is enabled, but isEnabled returned FALSE");

   treemodel.enabled = FALSE;
   cr_assert_eq(SCIPtreemodelIsEnabled(scip, &treemodel), FALSE, "Treemodel is disabled, but isEnabled returned TRUE");
}

/** Test the API functions SCIPtreemodelInit and SCIPtreemodelFree */
Test(branch_treemodel, InitFree)
{
   cr_assert_not_null(scip, "SCIP data structure is NULL");

   SCIP_TREEMODEL* treemodel = NULL;
   SCIPtreemodelInit(scip, &treemodel);

   cr_assert_not_null(treemodel, "Treemodel is NULL, but it should be initialized");

   SCIPtreemodelFree(scip, &treemodel);

   cr_assert_null(treemodel, "Treemodel is not NULL after being freed");
}

/** Test that SCIPs best candidate is retained when the pseudocosts are small */
Test(branch_treemodel_select, SmallPseudocosts)
{
   int bestcand = 0;

   /** Enable Treemodel with small pseudocost threshold */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.1;
   treemodel.lowrule = 'r';
   treemodel.highrule = 'r';
   treemodel.height = 10;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';

   /** Branching candidates (1,1) and (2,2), where (1,1) is selected by hybrid scores */
   setupTestVars(
      2,
      1.0, 1.0, 0.0, 1.0,
      2.0, 2.0, 0.1, 0.0
   );

   /** Set the dual gap */
   setDualGap(0.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 0, "Treemodel rule selected another variable despite small pseudocosts");
}

/**  Test that the ratio rule selects the correct variable */
Test(branch_treemodel_select, RatioRule)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 'r';
   treemodel.highrule = 'r';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';

   /** Branching candidates (10,10), (2,49), (5,5), (2,48) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.6
   );

   /** Set the dual gap -- irrelevant for ratio rule */
   setDualGap(10.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 1, "Ratio rule selected (10,10) over (2,49) which is incorrect");
}

/**  Test that the SVTS rule selects the correct variable */
Test(branch_treemodel_select, SvtsRule1)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 's';
   treemodel.highrule = 's';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';
   treemodel.maxsvtsheight = 100;

   /** Branching candidates (10,10), (2,49), (5,5), (2,48) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.6
   );

   /** Set the dual gap */
   setDualGap(40.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 0, "SVTS did not select (10,10) at G = 40 which is incorrect");
}

/**  Test that the SVTS rule selects the correct variable when there is a tie */
Test(branch_treemodel_select, SvtsRule2)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 's';
   treemodel.highrule = 's';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';
   treemodel.maxsvtsheight = 100;

   /** Branching candidates (10,10), (2,49), (5,5), (2,48) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.9
   );

   /** Set the dual gap */
   setDualGap(41.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 3, "SVTS did not select (2,48) at G = 41 (tied with (2,49) with higher hybrid scores) which is incorrect");
}

/**  Test that the SVTS rule selects the correct variable when filtering is enabled */
Test(branch_treemodel_select, SvtsRule3)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 's';
   treemodel.highrule = 's';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 't';
   treemodel.filterhigh = 't';
   treemodel.maxsvtsheight = 100;

   /** Branching candidates (10,10), (5,5), (2,48), (2,49) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.9,
      2.0, 49.0, 98.0, 9.8
   );

   /** Set the dual gap */
   setDualGap(41.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 3, "SVTS did not select (2,49) at G = 41 which is incorrect");
}

/**  Test that the Sampling rule selects the correct variable */
Test(branch_treemodel_select, SamplingRule1)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 't';
   treemodel.highrule = 't';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';
   treemodel.maxsvtsheight = 100;

   /** Branching candidates (10,10), (2,49), (5,5), (2,48) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.6
   );

   /** Set the dual gap */
   setDualGap(41.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 1, "Sampling did not select (2,49) at G = 41 which is incorrect");
}

/**  Test that the Sampling rule selects the correct variable when filtering is enabled */
Test(branch_treemodel_select, SamplingRule2)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 't';
   treemodel.highrule = 't';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 't';
   treemodel.filterhigh = 't';
   treemodel.maxsvtsheight = 100;

   /** Branching candidates (10,10), (5,5), (2,48), (2,49) */
   setupTestVars(
      4,
      10.0, 10.0, 100.0, 10.0,
      5.0, 5.0, 25.0, 2.5,
      2.0, 48.0, 96.0, 9.9,
      2.0, 49.0, 98.0, 9.8
   );

   /** Set the dual gap */
   setDualGap(41.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 3, "Sampling did not select (2,49) at G = 41 which is incorrect");
}

/**  Test that the SVTS rule goes to the fallback strategy when the tree size is infinite */
Test(branch_treemodel_select, SvtsInfFallback)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 's';
   treemodel.highrule = 's';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';
   treemodel.maxsvtsheight = 100;
   treemodel.fallbackinf = 'r';

   /** Branching candidates (1,1), (10,10), (2,49) */
   setupTestVars(
      4,
      1.0, 1.0, 1.0, 0.1,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8
   );

   /** Set the dual gap */
   setDualGap(1000000.0);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 2, "SVTS did not use ratio fallback when treesize was infinite");
}

/**  Test that the SVTS rule goes to the fallback strategy when there is no primal bound */
Test(branch_treemodel_select, SvtsNoPrimalFallback)
{
   int bestcand = 0;

   /** Enable Treemodel */
   SCIP_TREEMODEL treemodel;
   treemodel.enabled = TRUE;
   treemodel.smallpscost = 0.0;
   treemodel.lowrule = 's';
   treemodel.highrule = 's';
   treemodel.height = 10;
   treemodel.maxfpiter = 100;
   treemodel.filterlow = 'f';
   treemodel.filterhigh = 'f';
   treemodel.maxsvtsheight = 100;
   treemodel.fallbacknoprim = 'r';

   /** Branching candidates (1,1), (10,10), (2,49) */
   setupTestVars(
      4,
      1.0, 1.0, 1.0, 0.1,
      10.0, 10.0, 100.0, 10.0,
      2.0, 49.0, 98.0, 9.8
   );

   /** Set the dual gap to infinity */
   setDualGap(SCIP_REAL_MAX);

   /** Apply Treemodel rule */
   SCIPtreemodelSelectCandidate(scip, &treemodel, branchcands, mingains, maxgains, scoresfrompc, scoresfromothers, avgpscostscore, nbranchcands, &bestcand);

   /** Free the branching candidates */
   freeTestVars();

   cr_assert_eq(bestcand, 2, "SVTS did not use ratio fallback when there was no primal bound");
}