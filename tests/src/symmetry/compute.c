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

/**@file   compute.c
 * @brief  unit tests for computing symmetry
 * @author Marc Pfetsch
 */

#include <scip/scip.h>
#include <include/scip_test.h>
#include <scip/prop_symmetry.c>
#include <scip/symmetry.h>
#include <symmetry/compute_symmetry.h>
#include <scip/scipdefplugins.h>


static SCIP* scip;

static
void checkArraysEqual(int* expected, int* candidate, int length, char* name)
{
   int i;

   for( i = 0; i < length; ++i )
      cr_expect(expected[i] == candidate[i], "%s[%d]: expected %d, but got %d\n", name, i, expected[i], candidate[i]);
}

/** setup: create SCIP */
static
void setup(void)
{
   SCIP_CALL( SCIPcreate(&scip) );
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

   /* turn on symmetry computation */
   SCIP_CALL( SCIPsetIntParam(scip, "misc/usesymmetry", 1) );

#ifdef SCIP_DEBUG
   /* output external codes in order to see which external symmetry computation code is used */
   SCIPprintExternalCodes(scip, NULL);
   SCIPinfoMessage(scip, NULL, "\n");
#endif
}

/** teardown: free SCIP */
static
void teardown(void)
{
   SCIP_CALL( SCIPfree(&scip) );
   cr_assert_eq(BMSgetMemoryUsed(), 0, "Memory leak!");
}

/* TEST SUITE */
TestSuite(test_compute_symmetry, .init = setup, .fini = teardown);

/* TEST 1 */
Test(test_compute_symmetry, basic1, .description = "compute symmetry for a simple example with 4 variables and 2 linear constraints")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_CONS* cons;
   SCIP_VAR* vars[2];
   SCIP_Real vals[2];
   SCIP_VAR** permvars;
   int** perms;
   int* orbits;
   int* orbitbegins;
   int* components;
   int* componentbegins;
   int* vartocomponent;
   int ncomponents;
   int norbits;
   int npermvars;
   int nperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     x1 + x2           = 1
    *               x3 + x4 = 1
    *     x1, ..., x4 binary
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic1"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );

   vars[0] = var1;
   vars[1] = var2;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e1", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e2", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn off presolving in order to avoid having trivial problem afterwards */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         &components, &componentbegins, &vartocomponent, &ncomponents) );
   cr_assert( nperms == 3 );
   cr_assert( ncomponents == 1 );
   cr_assert( componentbegins[0] == 0 );
   cr_assert( componentbegins[1] == 3 );
   cr_assert( vartocomponent[0] == 0 );
   cr_assert( vartocomponent[1] == 0 );
   cr_assert( vartocomponent[2] == 0 );
   cr_assert( vartocomponent[3] == 0 );

   /* compute orbits */
   SCIP_CALL( SCIPallocBufferArray(scip, &orbits, npermvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &orbitbegins, npermvars) );
   SCIP_CALL( SCIPcomputeOrbitsSym(scip, permvars, npermvars, perms, nperms, orbits, orbitbegins, &norbits) );
   cr_assert( norbits == 1 );
   cr_assert( orbitbegins[0] == 0 );
   cr_assert( orbitbegins[1] == 4 );
   cr_assert( orbits[0] == 0 );
   cr_assert( orbits[1] == 1 );
   cr_assert( orbits[2] == 2 );
   cr_assert( orbits[3] == 3 );
   SCIPfreeBufferArray(scip, &orbitbegins);
   SCIPfreeBufferArray(scip, &orbits);

#ifdef SCIP_DEBUG
   {
      int i;
      int j;

      for (i = 0; i < nperms; ++i)
      {
         SCIPinfoMessage(scip, NULL, "Permutation %d: (", i);
         for (j = 0; j < npermvars; ++j)
         {
            if ( j == 0 )
               SCIPinfoMessage(scip, NULL, "%d", perms[i][j]);
            else
               SCIPinfoMessage(scip, NULL, " %d", perms[i][j]);
         }
         SCIPinfoMessage(scip, NULL, ")\n");
      }
   }
#endif

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
}


/* TEST 2 */
Test(test_compute_symmetry, basic2, .description = "compute symmetry for a simple example with 4 variables and 4 linear constraints - before presolving")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_CONS* cons;
   SCIP_VAR* vars[2];
   SCIP_Real vals[2];
   SCIP_VAR** permvars;
   int** perms;
   int* orbits;
   int* orbitbegins;
   int* components;
   int* componentbegins;
   int* vartocomponent;
   int ncomponents;
   int norbits;
   int npermvars;
   int nperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     x1 + x2           =  1
    *               x3 + x4 =  1
    *    2x1 +           x4 <= 2
    *         2x2 + x3      <= 2
    *     x1, ..., x4 binary
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic2"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );

   vars[0] = var1;
   vars[1] = var2;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e1", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e2", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var1;
   vars[1] = var4;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i1", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var2;
   vars[1] = var3;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i2", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* turn off presolving in order to avoid having trivial problem afterwards */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         &components, &componentbegins, &vartocomponent, &ncomponents) );
   cr_assert( nperms == 1 );
   cr_assert( ncomponents == 1 );
   cr_assert( componentbegins[0] == 0 );
   cr_assert( componentbegins[1] == 1 );
   cr_assert( vartocomponent[0] == 0 );
   cr_assert( vartocomponent[1] == 0 );
   cr_assert( vartocomponent[2] == 0 );
   cr_assert( vartocomponent[3] == 0 );

   /* compute orbits */
   SCIP_CALL( SCIPallocBufferArray(scip, &orbits, npermvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &orbitbegins, npermvars) );
   SCIP_CALL( SCIPcomputeOrbitsSym(scip, permvars, npermvars, perms, nperms, orbits, orbitbegins, &norbits) );
   cr_assert( norbits == 2 );
   cr_assert( orbitbegins[0] == 0 );
   cr_assert( orbitbegins[1] == 2 );
   cr_assert( orbits[0] == 0 );
   cr_assert( orbits[1] == 1 );
   cr_assert( orbits[2] == 2 );
   cr_assert( orbits[3] == 3 );
   SCIPfreeBufferArray(scip, &orbitbegins);
   SCIPfreeBufferArray(scip, &orbits);

#ifdef SCIP_DEBUG
   {
      int i;
      int j;

      for (i = 0; i < nperms; ++i)
      {
         SCIPinfoMessage(scip, NULL, "Permutation %d: (", i);
         for (j = 0; j < npermvars; ++j)
         {
            if ( j == 0 )
               SCIPinfoMessage(scip, NULL, "%d", perms[i][j]);
            else
               SCIPinfoMessage(scip, NULL, " %d", perms[i][j]);
         }
         SCIPinfoMessage(scip, NULL, ")\n");
      }
   }
#endif

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
}


/* TEST 3 */
Test(test_compute_symmetry, basic3, .description = "compute symmetry for a simple example with 4 variables and 4 linear constraints - after presolving")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_CONS* cons;
   SCIP_VAR* vars[2];
   SCIP_Real vals[2];
   SCIP_VAR** permvars;
   int** perms;
   int* components;
   int* componentbegins;
   int* vartocomponent;
   int ncomponents;
   int nperms;
   int npermvars;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     x1 + x2           =  1
    *               x3 + x4 =  1
    *    2x1 +           x4 <= 2
    *         2x2 + x3      <= 2
    *     x1, ..., x4 binary
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic3"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );

   vars[0] = var1;
   vars[1] = var2;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e1", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   vals[0] = 1.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e2", 2, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var1;
   vars[1] = var4;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i1", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var2;
   vars[1] = var3;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i2", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         &components, &componentbegins, &vartocomponent, &ncomponents) );
   cr_assert( nperms == -1 );  /* problem should be empty */
   cr_assert( ncomponents == -1 );
   cr_assert( components == NULL );
   cr_assert( componentbegins == NULL );
   cr_assert( vartocomponent == NULL );

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
}


/* TEST 4 */
Test(test_compute_symmetry, basic4, .description = "compute symmetry for a simple example with 5 variables and 2 linear constraints")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_VAR* var5;
   SCIP_CONS* cons;
   SCIP_VAR* vars[3];
   SCIP_Real vals[3];
   SCIP_VAR** permvars;
   int** perms;
   int* orbits;
   int* orbitbegins;
   int* components;
   int* componentbegins;
   int* vartocomponent;
   int ncomponents;
   int norbits;
   int npermvars;
   int nperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     x1 + x2           + x5 = 1
    *               x3 + x4 + x5 = 2
    *     x1, ..., x4, x5  binary
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic4"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var5, "x5", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var5) );

   vars[0] = var1;
   vars[1] = var2;
   vars[2] = var5;
   vals[0] = 1.0;
   vals[1] = 1.0;
   vals[2] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e1", 3, vars, vals, 1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   vars[2] = var5;
   vals[0] = 1.0;
   vals[1] = 1.0;
   vals[2] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e2", 3, vars, vals, 2.0, 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn off presolving in order to avoid having trivial problem afterwards */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         &components, &componentbegins, &vartocomponent, &ncomponents) );
   cr_assert( nperms == 2 );
   cr_assert( ncomponents == 2 );
   cr_assert( vartocomponent[0] == vartocomponent[1] );
   cr_assert( vartocomponent[2] == vartocomponent[3] );
   cr_assert( vartocomponent[0] != vartocomponent[2] );
   cr_assert( vartocomponent[1] != vartocomponent[3] );
   cr_assert( vartocomponent[4] == -1 );
   cr_assert( componentbegins[0] == 0 );
   cr_assert( componentbegins[1] == 1 );
   cr_assert( componentbegins[2] == 2 );

   /* compute orbits */
   SCIP_CALL( SCIPallocBufferArray(scip, &orbits, npermvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &orbitbegins, npermvars) );
   SCIP_CALL( SCIPcomputeOrbitsSym(scip, permvars, npermvars, perms, nperms, orbits, orbitbegins, &norbits) );
   cr_assert( norbits == 2 );
   cr_assert( orbitbegins[0] == 0 );
   cr_assert( orbitbegins[1] == 2 );
   cr_assert( orbitbegins[2] == 4 );
   SCIPfreeBufferArray(scip, &orbitbegins);
   SCIPfreeBufferArray(scip, &orbits);

#ifdef SCIP_DEBUG
   {
      int i;
      int j;

      for (i = 0; i < nperms; ++i)
      {
         SCIPinfoMessage(scip, NULL, "Permutation %d: (", i);
         for (j = 0; j < npermvars; ++j)
         {
            if ( j == 0 )
               SCIPinfoMessage(scip, NULL, "%d", perms[i][j]);
            else
               SCIPinfoMessage(scip, NULL, " %d", perms[i][j]);
         }
         SCIPinfoMessage(scip, NULL, ")\n");
      }
   }
#endif

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
   SCIP_CALL( SCIPreleaseVar(scip, &var5) );
}


/* TEST 5 */
Test(test_compute_symmetry, basic5, .description = "compute symmetry for a simple example with bounddisjunctions")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_CONS* cons;
   SCIP_VAR* vars[2];
   SCIP_BOUNDTYPE boundtypes[2];
   SCIP_Real bounds[2];
   SCIP_VAR** permvars;
   int** perms;
   int* orbits;
   int* orbitbegins;
   int norbits;
   int npermvars;
   int nperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     BD(x1 >= 1, x2 >= 1)
    *     BD(x3 >= 1, x4 >= 1)
    *     x1, ..., x4 binary
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic1"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );

   vars[0] = var1;
   vars[1] = var2;
   boundtypes[0] = SCIP_BOUNDTYPE_LOWER;
   boundtypes[1] = SCIP_BOUNDTYPE_LOWER;
   bounds[0] = 1.0;
   bounds[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicBounddisjunction(scip, &cons, "d1", 2, vars, boundtypes, bounds) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   boundtypes[0] = SCIP_BOUNDTYPE_LOWER;
   boundtypes[1] = SCIP_BOUNDTYPE_LOWER;
   bounds[0] = 1.0;
   bounds[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicBounddisjunction(scip, &cons, "d2", 2, vars, boundtypes, bounds) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn off presolving in order to avoid having trivial problem afterwards */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPprintTransProblem(scip, NULL, "cip", FALSE) );
#endif

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         NULL, NULL, NULL, NULL) );

   /* compute orbits */
   SCIP_CALL( SCIPallocBufferArray(scip, &orbits, npermvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &orbitbegins, npermvars) );
   SCIP_CALL( SCIPcomputeOrbitsSym(scip, permvars, npermvars, perms, nperms, orbits, orbitbegins, &norbits) );
   cr_assert( norbits == 1 );
   cr_assert( orbitbegins[0] == 0 );
   cr_assert( orbitbegins[1] == 4 );
   cr_assert( orbits[0] == 0 );
   cr_assert( orbits[1] == 1 );
   cr_assert( orbits[2] == 2 );
   cr_assert( orbits[3] == 3 );
   SCIPfreeBufferArray(scip, &orbitbegins);
   SCIPfreeBufferArray(scip, &orbits);

#ifdef SCIP_DEBUG
   {
      int i;
      int j;

      for (i = 0; i < nperms; ++i)
      {
         SCIPinfoMessage(scip, NULL, "Permutation %d: (", i);
         for (j = 0; j < npermvars; ++j)
         {
            if ( j == 0 )
               SCIPinfoMessage(scip, NULL, "%d", perms[i][j]);
            else
               SCIPinfoMessage(scip, NULL, " %d", perms[i][j]);
         }
         SCIPinfoMessage(scip, NULL, ")\n");
      }
   }
#endif

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
}


/* TEST 6 */
Test(test_compute_symmetry, basic6, .description = "compute symmetry for a simple example with additional bounddisjunctions")
{
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_VAR* var3;
   SCIP_VAR* var4;
   SCIP_VAR* var5;
   SCIP_CONS* cons;
   SCIP_VAR* vars[3];
   SCIP_Real vals[3];
   SCIP_BOUNDTYPE boundtypes[2];
   SCIP_Real bounds[2];
   SCIP_VAR** permvars;
   int** perms;
   int* orbits;
   int* orbitbegins;
   int norbits;
   int npermvars;
   int nperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   /* setup problem:
    * min x1 + x2 + x3 + x4
    *     x1 + x2           + x5 =  3
    *               x3 + x4 + x5 =  3
    *    2x1 +           x4      <= 2
    *         2x2 + x3           <= 2
    *    BD(x5 <= 1, x5 >= 3)
    *     x1, ..., x4 binary, x5 continuous
    */
   SCIP_CALL( SCIPcreateProbBasic(scip, "basic5"));

   SCIP_CALL( SCIPcreateVarBasic(scip, &var1, "x1", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var1) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var2, "x2", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var2) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var3, "x3", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var3) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var4, "x4", 0.0, 1.0, 1.0, SCIP_VARTYPE_BINARY) );
   SCIP_CALL( SCIPaddVar(scip, var4) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &var5, "x5", 0.0, 5.0, 1.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPaddVar(scip, var5) );

   vars[0] = var1;
   vars[1] = var2;
   vars[2] = var5;
   vals[0] = 1.0;
   vals[1] = 1.0;
   vals[2] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e1", 3, vars, vals, 3.0, 3.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var3;
   vars[1] = var4;
   vars[2] = var5;
   vals[0] = 1.0;
   vals[1] = 1.0;
   vals[2] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "e2", 3, vars, vals, 3.0, 3.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var1;
   vars[1] = var4;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i1", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var2;
   vars[1] = var3;
   vals[0] = 2.0;
   vals[1] = 1.0;
   SCIP_CALL( SCIPcreateConsBasicLinear(scip, &cons, "i2", 2, vars, vals, -SCIPinfinity(scip), 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   vars[0] = var5;
   vars[1] = var5;
   boundtypes[0] = SCIP_BOUNDTYPE_UPPER;
   boundtypes[1] = SCIP_BOUNDTYPE_LOWER;
   bounds[0] = 1.0;
   bounds[1] = 3.0;
   SCIP_CALL( SCIPcreateConsBasicBounddisjunction(scip, &cons, "d1", 2, vars, boundtypes, bounds) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* turn on checking of symmetries */
   SCIP_CALL( SCIPsetBoolParam(scip, "propagating/symmetry/checksymmetries", TRUE) );

   /* turn off presolving in order to avoid having trivial problem afterwards */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );

   /* presolve problem (symmetry will be available afterwards) */
   SCIP_CALL( SCIPpresolve(scip) );

#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPprintTransProblem(scip, NULL, "cip", FALSE) );
#endif

   /* get symmetry */
   SCIP_CALL( SCIPgetSymmetry(scip,
         &npermvars, &permvars, NULL, &nperms, &perms, NULL, NULL, NULL,
         NULL, NULL, NULL, NULL) );

   /* compute orbits */
   SCIP_CALL( SCIPallocBufferArray(scip, &orbits, npermvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &orbitbegins, npermvars) );
   SCIP_CALL( SCIPcomputeOrbitsSym(scip, permvars, npermvars, perms, nperms, orbits, orbitbegins, &norbits) );
   cr_assert( nperms == 1 );
   cr_assert( norbits == 2 );
   cr_assert( orbitbegins[0] == 0 );
   cr_assert( orbitbegins[1] == 2 );
   cr_assert( orbits[0] == 0 );
   cr_assert( orbits[1] == 1 );
   cr_assert( orbits[2] == 2 );
   cr_assert( orbits[3] == 3 );
   SCIPfreeBufferArray(scip, &orbitbegins);
   SCIPfreeBufferArray(scip, &orbits);

#ifdef SCIP_DEBUG
   {
      int i;
      int j;

      for (i = 0; i < nperms; ++i)
      {
         SCIPinfoMessage(scip, NULL, "Permutation %d: (", i);
         for (j = 0; j < npermvars; ++j)
         {
            if ( j == 0 )
               SCIPinfoMessage(scip, NULL, "%d", perms[i][j]);
            else
               SCIPinfoMessage(scip, NULL, " %d", perms[i][j]);
         }
         SCIPinfoMessage(scip, NULL, ")\n");
      }
   }
#endif

   SCIP_CALL( SCIPreleaseVar(scip, &var1) );
   SCIP_CALL( SCIPreleaseVar(scip, &var2) );
   SCIP_CALL( SCIPreleaseVar(scip, &var3) );
   SCIP_CALL( SCIPreleaseVar(scip, &var4) );
   SCIP_CALL( SCIPreleaseVar(scip, &var5) );
}

/* TEST 7 (subgroups) */
Test(test_compute_symmetry, subgroups1, .description = "detect symetric subgroups for artificial propdata")
{
   SCIP_PROPDATA propdata;
   int* perms[6];
   int permorder1[6] = {0,1,2,3,4,5};
   int permorder2[6] = {2,3,4,5,0,1};
   int permorder3[6] = {5,0,1,2,3,4};
   int perm1[10] = {1,0,2,4,3,5,6,7,8,9};
   int perm2[10] = {0,2,1,3,5,4,6,7,8,9};
   int perm3[10] = {0,1,2,3,4,5,7,6,8,9};
   int perm4[10] = {0,1,2,3,4,5,6,8,7,9};
   int perm5[10] = {0,1,2,3,4,5,6,7,9,8};
   int perm6[10] = {6,7,2,8,9,5,0,1,3,4};
   int components[6] = {0,1,2,3,4,5};
   int componentbegins[2] = {0,6};
   SCIP_Shortbool componentblocked[2] = {FALSE, FALSE};
   int* graphcomponents;
   int* graphcompbegins;
   int* compcolorbegins;
   int ngraphcomponents;
   int ncompcolors;
   int nusedperms;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   perms[0] = perm1;
   perms[1] = perm2;
   perms[2] = perm3;
   perms[3] = perm4;
   perms[4] = perm5;
   perms[5] = perm6;

   propdata.npermvars = 10;
   propdata.nperms = 6;
   propdata.perms = perms;
   propdata.ncomponents = 1;
   propdata.components = components;
   propdata.componentbegins = componentbegins;
   propdata.componentblocked = componentblocked;

   /* check canonical order */

   SCIP_CALL( buildSubgroupGraph(scip, &propdata, permorder1, 6, 0, &graphcomponents, &graphcompbegins,
         &compcolorbegins, &ngraphcomponents, &ncompcolors, &nusedperms) );

   cr_assert(graphcomponents != NULL);
   cr_assert(graphcompbegins != NULL);
   cr_assert(compcolorbegins != NULL);
   cr_assert(nusedperms == 5, "expected 5 used permutations, but got %d\n", nusedperms);
   cr_assert(ngraphcomponents == 3, "expected 3 graph components, but got %d\n", ngraphcomponents);
   cr_assert(ncompcolors == 2, "expected 2 component colors, but got %d\n", ncompcolors);

   int expectedcomps[10] = {0,1,2,3,4,5,6,7,8,9};
   checkArraysEqual(expectedcomps, graphcomponents, 10, "components");

   int expectedcompbegins[4] = {0,3,6,10};
   checkArraysEqual(expectedcompbegins, graphcompbegins, ngraphcomponents, "compbegins");

   int expectedcolbegins[3] = {0,2,3};
   checkArraysEqual(expectedcolbegins, compcolorbegins, ncompcolors, "colorbegins");

   SCIPfreeBlockMemoryArray(scip, &compcolorbegins, ncompcolors + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcompbegins, ngraphcomponents + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcomponents, 10);

   /* check different order */

   SCIP_CALL( buildSubgroupGraph(scip, &propdata, permorder2, 6, 0, &graphcomponents, &graphcompbegins,
         &compcolorbegins, &ngraphcomponents, &ncompcolors, &nusedperms) );

   cr_assert(graphcomponents != NULL);
   cr_assert(graphcompbegins != NULL);
   cr_assert(compcolorbegins != NULL);
   cr_assert(nusedperms == 5, "expected 5 used permutations, but got %d\n", nusedperms);
   cr_assert(ngraphcomponents == 3, "expected 3 graph components, but got %d\n", ngraphcomponents);
   cr_assert(ncompcolors == 2, "expected 2 component colors, but got %d\n", ncompcolors);

   int expectedcomps1[10] = {0,1,2,3,4,5,6,7,8,9};
   checkArraysEqual(expectedcomps1, graphcomponents, 10, "components");

   int expectedcompbegins1[4] = {0,3,6,10};
   checkArraysEqual(expectedcompbegins1, graphcompbegins, ngraphcomponents, "compbegins");

   int expectedcolbegins1[3] = {0,2,3};
   checkArraysEqual(expectedcolbegins1, compcolorbegins, ncompcolors, "colorbegins");

   SCIPfreeBlockMemoryArray(scip, &compcolorbegins, ncompcolors + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcompbegins, ngraphcomponents + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcomponents, 10);

   /* check order that leads to trivial subgroup */

   SCIP_CALL( buildSubgroupGraph(scip, &propdata, permorder3, 6, 0, &graphcomponents, &graphcompbegins,
         &compcolorbegins, &ngraphcomponents, &ncompcolors, &nusedperms) );

   cr_assert(graphcomponents != NULL);
   cr_assert(graphcompbegins != NULL);
   cr_assert(compcolorbegins != NULL);
   cr_assert(nusedperms == 2, "expected 2 used permutations, but got %d\n", nusedperms);
   cr_assert(ngraphcomponents == 4, "expected 4 graph components, but got %d\n", ngraphcomponents);
   cr_assert(ncompcolors == 1, "expected 1 component colors, but got %d\n", ncompcolors);

   int expectedcomps2[10] = {0,6,2,1,7,3,8,4,5,9};
   checkArraysEqual(expectedcomps2, graphcomponents, 10, "components");

   int expectedcompbegins2[5] = {0,2,5,7,10};
   checkArraysEqual(expectedcompbegins2, graphcompbegins, ngraphcomponents, "compbegins");

   int expectedcolbegins2[2] = {0,4};
   checkArraysEqual(expectedcolbegins2, compcolorbegins, ncompcolors, "colorbegins");

   SCIPfreeBlockMemoryArray(scip, &compcolorbegins, ncompcolors + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcompbegins, ngraphcomponents + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcomponents, 10);
}

/* TEST 8 (subgroups) */
Test(test_compute_symmetry, subgroups2, .description = "detect symetric subgroups for artificial propdata")
{
   SCIP_PROPDATA propdata;
   int* perms[6];
   int perm1[10] = {0,2,1,3,5,4,6,7,8,9};
   int perm2[10] = {0,1,2,3,4,5,7,6,8,9};
   int perm3[10] = {0,1,2,3,4,5,6,8,7,9};
   int perm4[10] = {6,7,0,8,9,5,2,1,3,4};
   int perm5[10] = {1,0,2,4,3,5,6,7,8,9};
   int perm6[10] = {0,1,2,3,4,5,6,7,9,8};
   int components[6] = {0,1,2,3,4,5};
   int componentbegins[2] = {0,6};
   SCIP_Shortbool componentblocked[2] = {FALSE, FALSE};
   int* permorder;
   int* graphcomponents;
   int* graphcompbegins;
   int* compcolorbegins;
   int ngraphcomponents;
   int ncompcolors;
   int ntwocycleperms;
   int nusedperms;
   int i;

   /* skip test if no symmetry can be computed */
   if ( ! SYMcanComputeSymmetry() )
      return;

   perms[0] = perm1;
   perms[1] = perm2;
   perms[2] = perm3;
   perms[3] = perm4;
   perms[4] = perm5;
   perms[5] = perm6;

   propdata.npermvars = 10;
   propdata.nperms = 6;
   propdata.perms = perms;
   propdata.ncomponents = 1;
   propdata.components = components;
   propdata.componentbegins = componentbegins;
   propdata.componentblocked = componentblocked;

   /* check sorted order */

   SCIP_CALL( SCIPallocBufferArray(scip, &permorder, 6) );

   for( i = 0; i < 6; ++i )
      permorder[i] = i;

   SCIP_CALL( chooseOrderOfGenerators(scip, &propdata, 0, &permorder, &ntwocycleperms) );
   cr_assert(ntwocycleperms == 5);

   int expectedpermorder[6] = {5,1,2,4,0,3};
   checkArraysEqual(expectedpermorder, permorder, 6, "permorder");

   SCIP_CALL( buildSubgroupGraph(scip, &propdata, permorder, ntwocycleperms, 0, &graphcomponents,
         &graphcompbegins, &compcolorbegins, &ngraphcomponents, &ncompcolors, &nusedperms) );

   cr_assert(graphcomponents != NULL);
   cr_assert(graphcompbegins != NULL);
   cr_assert(compcolorbegins != NULL);
   cr_assert(nusedperms == 5, "expected 5 used permutations, but got %d\n", nusedperms);
   cr_assert(ngraphcomponents == 3, "expected 3 graph components, but got %d\n", ngraphcomponents);
   cr_assert(ncompcolors == 2, "expected 2 component colors, but got %d\n", ncompcolors);

   int expectedcomps[10] = {0,1,2,3,4,5,6,7,8,9};
   checkArraysEqual(expectedcomps, graphcomponents, 10, "components");

   int expectedcompbegins[4] = {0,3,6,10};
   checkArraysEqual(expectedcompbegins, graphcompbegins, ngraphcomponents, "compbegins");

   int expectedcolbegins[3] = {0,2,3};
   checkArraysEqual(expectedcolbegins, compcolorbegins, ncompcolors, "colorbegins");

   SCIPfreeBlockMemoryArray(scip, &compcolorbegins, ncompcolors + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcompbegins, ngraphcomponents + 1);
   SCIPfreeBlockMemoryArray(scip, &graphcomponents, 10);
   SCIPfreeBufferArray(scip, &permorder);
}