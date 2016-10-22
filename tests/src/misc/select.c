/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   select.c
 * @brief  unit tests for selection of unweighted and weighted median
 * @author Gregor Hendel
 */

#include "scip/pub_misc.h"
#include "scip/scip.h"

#include "include/scip_test.h"

/** GLOBAL VARIABLES **/
static SCIP_RANDNUMGEN* randgen;
static SCIP* scip;
static unsigned int randomseed = 42;

/* TEST SUITE */
static
void setup(void)
{
   SCIPcreate(&scip);
   SCIPrandomCreate(&randgen, SCIPblkmem(scip), randomseed);
}

static
void teardown(void)
{
   SCIPrandomFree(&randgen);
   SCIPfree(&scip);
}

TestSuite(select, .init = setup, .fini = teardown);

/* TESTS  */
Test(select, create_and_free)
{
   /* calls setup and teardown */
}

/* this number could be higher (700), but we use a smaller number due to a problem with cr_assert* being too slow */
#define ARRAYMEMSIZE 70
Test(select, random_permutation, .description = "tests selection on a bunch of random permutations of the integers 1...n")
{
   int len = ARRAYMEMSIZE;
   int key[ARRAYMEMSIZE];
   int i;
   int j;
   /* initialize key */
   for( j = 0; j < len; ++j )
      key[j] = j;


   /* loop over all positions of the array and check whether the correct element is selected after a random permutation */
   for( i = 0; i < len; ++i )
   {
      int inputkey[ARRAYMEMSIZE];

      SCIPrandomPermuteIntArray(randgen, key, 0, len);
      /* save input permutation for debugging */
      BMScopyMemoryArray(inputkey, key, len);
      SCIPselectInt(key, i, len);

      /* the element key[i] must be the index itself */
      cr_assert_eq(key[i], i, "Wrong key selected: %d ~= %d", key[i], i);

      /* check if the partial sorting correctly worked */
      for( j = 0; j < len; ++j )
      {
         int k;
         int start = j >= key[i] ? i : 0;
         int end = j >= key[i] ? len : i;

         for( k = start; k < end; ++k )
         {
            if( key[k] == j )
               break;
         }
         cr_assert_lt(k, end, "Element %d is not in the right partition [%d,%d]\n", j, start, end);
      }


   }
}
