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

/**@file   presol_dualsparsify.c
 * @brief  cancel non-zeros of the constraint matrix
 * @author Dieter Weninger
 * @author Robert Lion Gottwald
 * @author Ambros Gleixner
 *
 * This presolver attempts to cancel non-zero entries of the constraint
 * matrix by adding scaled variables to other variables.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "blockmemshell/memory.h"
#include "scip/cons_linear.h"
#include "scip/presol_dualsparsify.h"
#include "scip/pub_cons.h"
#include "scip/pub_matrix.h"
#include "scip/pub_message.h"
#include "scip/pub_misc.h"
#include "scip/pub_misc_sort.h"
#include "scip/pub_presol.h"
#include "scip/pub_var.h"
#include "scip/scip_cons.h"
#include "scip/scip_general.h"
#include "scip/scip_mem.h"
#include "scip/scip_message.h"
#include "scip/scip_nlp.h"
#include "scip/scip_numerics.h"
#include "scip/scip_param.h"
#include "scip/scip_presol.h"
#include "scip/scip_pricer.h"
#include "scip/scip_prob.h"
#include "scip/scip_probing.h"
#include "scip/scip_timing.h"
#include "scip/scip_var.h"
#include <string.h>

#define PRESOL_NAME            "dualsparsify"
#define PRESOL_DESC            "eliminate non-zero coefficients"

#define PRESOL_PRIORITY              -240    /**< priority of the presolver (>= 0: before, < 0: after constraint handlers) */
#define PRESOL_MAXROUNDS               -1    /**< maximal number of presolving rounds the presolver participates in (-1: no limit) */
#define PRESOL_TIMING           SCIP_PRESOLTIMING_EXHAUSTIVE /* timing of the presolver (fast, medium, or exhaustive) */

#define DEFAULT_ENABLECOPY           TRUE    /**< should dualsparsify presolver be copied to sub-SCIPs? */
#define DEFAULT_CANCELLINEAR         TRUE    /**< should we cancel nonzeros in constraints of the linear constraint handler? */
#define DEFAULT_PRESERVEINTCOEFS     TRUE    /**< should we forbid cancellations that destroy integer coefficients? */
#define DEFAULT_MAX_CONT_FILLIN         0    /**< default value for the maximal fillin for continuous variables */
#define DEFAULT_MAX_BIN_FILLIN          0    /**< default value for the maximal fillin for binary variables */
#define DEFAULT_MAX_INT_FILLIN          0    /**< default value for the maximal fillin for integer variables (including binary) */
#define DEFAULT_MAXNONZEROS            -1    /**< maximal support of one equality to be used for cancelling (-1: no limit) */
#define DEFAULT_MAXCONSIDEREDNONZEROS  70    /**< maximal number of considered non-zeros within one row (-1: no limit) */
#define DEFAULT_ROWSORT               'd'    /**< order in which to process inequalities ('n'o sorting, 'i'ncreasing nonzeros, 'd'ecreasing nonzeros) */
#define DEFAULT_MAXRETRIEVEFAC      100.0    /**< limit on the number of useless vs. useful hashtable retrieves as a multiple of the number of constraints */
#define DEFAULT_WAITINGFAC            2.0    /**< number of calls to wait until next execution as a multiple of the number of useless calls */

#define MAXSCALE                   1000.0    /**< maximal allowed scale for cancelling non-zeros */


/*
 * Data structures
 */

/** presolver data */
struct SCIP_PresolData
{
   int                   ncancels;           /**< total number of canceled nonzeros (net value, i.e., removed minus added nonzeros) */
   int                   nfillin;            /**< total number of added nonzeros */
   int                   nfailures;          /**< number of calls to presolver without success */
   int                   nwaitingcalls;      /**< number of presolver calls until next real execution */
   int                   maxcontfillin;      /**< maximal fillin for continuous variables */
   int                   maxintfillin;       /**< maximal fillin for integer variables*/
   int                   maxbinfillin;       /**< maximal fillin for binary variables */
   int                   maxnonzeros;        /**< maximal support of one equality to be used for cancelling (-1: no limit) */
   int                   maxconsiderednonzeros;/**< maximal number of considered non-zeros within one row (-1: no limit) */
   SCIP_Real             maxretrievefac;     /**< limit on the number of useless vs. useful hashtable retrieves as a multiple of the number of constraints */
   SCIP_Real             waitingfac;         /**< number of calls to wait until next execution as a multiple of the number of useless calls */
   char                  rowsort;            /**< order in which to process inequalities ('n'o sorting, 'i'ncreasing nonzeros, 'd'ecreasing nonzeros) */
   SCIP_Bool             enablecopy;         /**< should dualsparsify presolver be copied to sub-SCIPs? */
   SCIP_Bool             cancellinear;       /**< should we cancel nonzeros in constraints of the linear constraint handler? */
   SCIP_Bool             preserveintcoefs;   /**< should we forbid cancellations that destroy integer coefficients? */
};

/*
 * Local methods
 */

/* add variable colidx1 to variable colidx2 */
static
SCIP_RETCODE aggregation(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_MATRIX*          matrix,             /**< the constraint matrix */
   int                   colidx1,            /**< one of the indexes of column to try non-zero cancellation for */
   int                   colidx2,            /**< one of the indexes of column to try non-zero cancellation for */
   SCIP_Real             weight1             /**< weight variable one in the aggregated expression */
      )
{
   SCIP_VAR* vars[2];
   SCIP_Real coefs[2];
   SCIP_VAR* aggregatedvar;
   SCIP_VAR* newvar;
   SCIP_CONS* newcons;
   SCIP_Real newlb;
   SCIP_Real newub;
   char newvarname[SCIP_MAXSTRLEN];
   char newconsname[SCIP_MAXSTRLEN];
   SCIP_Bool infeasible;
   SCIP_Bool aggregated;


   assert( !SCIPisZero(weight1) );
   assert( !SCIPisZero(weight2) );

   aggregatedvar = SCIPmatrixGetVar(matrix, colidx2);

   (void) SCIPsnprintf(newvarname, SCIP_MAXSTRLEN, "%s_agg_%s",
       SCIPvarGetName(SCIPmatrixGetVar(matrix, colidx1)), SCIPvarGetName(aggregatedvar));
   /* TODO: we need to consider the infinite bounds here */
   if( weight1 > 0 )
   {
      newlb = weight1*SCIPmatrixGetColLb(matrix, colidx1) + SCIPmatrixGetColLb(matrix, colidx2);
      newub = weight1*SCIPmatrixGetColUb(matrix, colidx1) + SCIPmatrixGetColUb(matrix, colidx2);
   }
   else
   {
      newlb = weight1*SCIPmatrixGetColUb(matrix, colidx1) + SCIPmatrixGetColLb(matrix, colidx2);
      newub = weight1*SCIPmatrixGetColLb(matrix, colidx1) + SCIPmatrixGetColUb(matrix, colidx2);
   }

   SCIP_CALL( SCIPcreateVar(scip, &newvar, newvarname, newlb, newub, 0.0, SCIP_VARTYPE_CONTINUOUS,
            SCIPvarIsInitial(aggregatedvar), SCIPvarIsRemovable(aggregatedvar), NULL, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPaddVar(scip, newvar) );
 
   vars[0] = SCIPmatrixGetVar(matrix, colidx1);
   vars[1] = newvar;
   coefs[0] = -weight1;
   coefs[1] = 1;

   SCIP_CALL( SCIPmultiaggregateVar(scip, aggregatedvar, 2, vars, coefs, 0.0, &infeasible, &aggregated) );
   assert(!infeasible);
   assert(aggregated);

   (void) SCIPsnprintf(newconsname, SCIP_MAXSTRLEN, "%s_dual_%s",
         SCIPvarGetName(vars[0]), SCIPvarGetName(aggregatedvar));
   SCIP_CALL( SCIPcreateConsLinear(scip, &newcons, newconsname, 2, vars, coefs,
            SCIPmatrixGetColLb(matrix, colidx2), SCIPmatrixGetColUb(matrix, colidx2),
            TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE) );
   SCIP_CALL( SCIPaddCons(scip, newcons) );
   SCIPdebugPrintCons(scip, newcons, NULL);

   SCIP_CALL( SCIPreleaseCons(scip, &newcons) );
   SCIP_CALL( SCIPreleaseVar(scip, &newvar) );
}


static
SCIP_RETCODE cancelCol(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_MATRIX*          matrix,             /**< the constraint matrix */
   int                   colidx1,            /**< one of the indexes of column to try non-zero cancellation for */
   int                   colidx2,            /**< one of the indexes of column to try non-zero cancellation for */
   SCIP_Real*            col1tocol2ratio,    /**< pointer to store the ratio of var1 to var2 when applying variable substitution */
   SCIP_Real*            col2tocol1ratio,    /**< pointer to store the ratio of var2 to var1 when applying variable substitution */
   SCIP_Bool*            col1tocol2success,  /**< pointer to store whether var1 can be added to var2 to sparsify the matrix */
   SCIP_Bool*            col2tocol1success,  /**< pointer to store whether var2 can be added to var1 to sparsify the matrix */
   SCIP_Real*            ratios,             /**< ratio of the vectors*/
   int*                  nratios,            /**< number of different ratios*/
   int*                  nchgcoefs,          /**< pointer to update number of changed coefficients */
   int*                  ncanceled,          /**< pointer to update number of canceled nonzeros */
   int*                  nfillin             /**< pointer to update the produced fill-in */
      )
{
   int i;
   int j;
   int varlen1;
   int varlen2;
   int *inds1;
   int *inds2;
   int nnz1;
   int nnz2;
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   SCIP_Real *vals1;
   SCIP_Real *vals2;

   varlen1 =  SCIPmatrixGetColNNonzs(matrix, colidx1);
   varlen2 =  SCIPmatrixGetColNNonzs(matrix, colidx2);
   assert(varlen1 >= 10);
   assert(varlen2 >= 10);
   var1 = SCIPmatrixGetVar(matrix, colidx1);
   var2 = SCIPmatrixGetVar(matrix, colidx2);
   inds1 = SCIPmatrixGetColIdxPtr(matrix, colidx1);
   inds2 = SCIPmatrixGetColIdxPtr(matrix, colidx2);
   vals1 = SCIPmatrixGetColValPtr(matrix, colidx1);
   vals2 = SCIPmatrixGetColValPtr(matrix, colidx2);
   
   i = 0;
   j = 0;
   nnz1 = 0;
   nnz2 = 0;
   *nratios = 0;
   *col1tocol2success = FALSE;
   *col2tocol1success = FALSE;
   printf("%d, %d\n", varlen1, varlen2);
   while(i < varlen1 && j < varlen2)
   {
      if(inds1[i] == inds2[j])
      {
         if( SCIPisZero(scip, vals1[i]) )
         {
            nnz2 += 1;
         }
         else if( SCIPisZero(scip, vals2[j]) )
         {
            nnz1 += 1;
         }
         else
         {
            ratios[*nratios] = vals1[i]/vals2[j];
            (*nratios)++;
         }
         i++;
         j++;
      }
      else if(inds1[i] < inds2[j])
      {
         i++;
         if( !SCIPisZero(scip, vals1[i]) )
            nnz1 += 1;
      }
      else
      {
         j++;
         if( !SCIPisZero(scip, vals2[j]) )
            nnz2 += 1;
      }
   }
   if( i < varlen1 )
      nnz1 += varlen1 - i;
   if( j < varlen2 )
      nnz2 += varlen2 - j;

   if( nnz1<*nratios || nnz2<*nratios )
   {
      SCIP_Real maxratio;
      SCIP_Real secmaxratio;
      SCIP_Real curratio;
      int nmaxratio;
      int nsecmaxratio;
      int ncurratio;
      int tmp;
      SCIP_Bool tmp_sign;


      nmaxratio = 0;
      nsecmaxratio = 0;
      SCIPsortDownReal(ratios, *nratios);
      curratio = ratios[0];
      ncurratio = 1;
      for( i=1; i<*nratios; i++ )
      {
         if( SCIPisEQ(scip, curratio, ratios[i]) )
         {
            ncurratio++;
         }
         else
         {
            if( ncurratio > nmaxratio )
            {
               maxratio = curratio;
               nmaxratio = ncurratio;
            }
            else if( ncurratio > nsecmaxratio )
            {
               secmaxratio = curratio;
               nsecmaxratio = ncurratio;
            }
            curratio = ratios[i];
            ncurratio = 1;
         }
      }
      if( ncurratio > nmaxratio )
      {
         maxratio = curratio;
         nmaxratio = ncurratio;
      }
      else if( ncurratio > nsecmaxratio )
      {
         secmaxratio = curratio;
         nsecmaxratio = ncurratio;
      }

      tmp = nnz1 < nnz2 ? nnz1 : nnz2;
      tmp_sign = nnz1 < nnz2 ? TRUE : FALSE;
      /* TODO: by a conservative way */
      if( nmaxratio > tmp )
      {
         if( tmp_sign )
         {
            *col1tocol2ratio = maxratio;
            *col1tocol2success = TRUE;
            SCIP_CALL( aggregation(scip, matrix, colidx2, colidx1, 1.0/maxratio) );
            *ncanceled = *ncanceled + nmaxratio - nnz1;
            *nchgcoefs = *nchgcoefs + varlen2;
            printf("%s, %s\n", SCIPvarGetName(var1), SCIPvarGetName(var2));
            printf("wei %8.4f\n", maxratio);

            if( nsecmaxratio > nnz2 )
            {
               *col2tocol1ratio = 1.0/secmaxratio;
               *col2tocol1success = TRUE;
            }
         }
         else
         {
            *col1tocol2ratio = maxratio;
            *col1tocol2success = TRUE;
            SCIP_CALL( aggregation(scip, matrix, colidx1, colidx2, maxratio) );
            *ncanceled = *ncanceled + nmaxratio - nnz2;
            *nchgcoefs = *nchgcoefs + varlen1;
            printf("%s, %s\n", SCIPvarGetName(var1), SCIPvarGetName(var2));
            printf("chen  %8.4f\n", maxratio);
            if( nsecmaxratio > nnz1 )
            {
               *col2tocol1ratio = 1.0/secmaxratio;
               *col2tocol1success = TRUE;
            }
         }
         printf("dualsuccess: %d, %d, %d, %d\n", nmaxratio, nsecmaxratio, nnz1, nnz2);
      }
   }
}


/*
 * Callback methods of presolver
 */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_PRESOLCOPY(presolCopyDualsparsify)
{
   SCIP_PRESOLDATA* presoldata;

   assert(scip != NULL);
   assert(presol != NULL);
   assert(strcmp(SCIPpresolGetName(presol), PRESOL_NAME) == 0);

   /* call inclusion method of presolver if copying is enabled */
   presoldata = SCIPpresolGetData(presol);
   assert(presoldata != NULL);
   if( presoldata->enablecopy )
   {
      SCIP_CALL( SCIPincludePresolDualsparsify(scip) );
   }

   return SCIP_OKAY;
}

/** execution method of presolver */
static
SCIP_DECL_PRESOLEXEC(presolExecDualsparsify)
{  /*lint --e{715}*/
   SCIP_MATRIX* matrix;
   SCIP_Bool initialized;
   SCIP_Bool complete;
   int ncols;
   int nrows;
   int r;
   int i;
   int j;
   int ncancels;
   int nfillins;
   int nchgcoef;
   int* locks;
   int* perm;
   int* rowidxsorted;
   int* rowsparsity;
   int nvarpairs;
   int varpairssize;
   SCIP_PRESOLDATA* presoldata;
   SCIP_Longint maxuseless;
   SCIP_Longint nuseless;

   int *processedvarsidx;
   int nprocessedvarsidx;

   nprocessedvarsidx = 0;

   assert(result != NULL);

   *result = SCIP_DIDNOTRUN;

   if( (SCIPgetStage(scip) != SCIP_STAGE_PRESOLVING) || SCIPinProbing(scip) || SCIPisNLPEnabled(scip) )
      return SCIP_OKAY;

   if( SCIPisStopped(scip) || SCIPgetNActivePricers(scip) > 0 )
      return SCIP_OKAY;

   presoldata = SCIPpresolGetData(presol);

   if( presoldata->nwaitingcalls > 0 )
   {
      presoldata->nwaitingcalls--;
      SCIPdebugMsg(scip, "skipping dualsparsify: nfailures=%d, nwaitingcalls=%d\n", presoldata->nfailures,
         presoldata->nwaitingcalls);
      return SCIP_OKAY;
   }
   SCIPdebugMsg(scip, "starting dualsparsify. . .\n");
   *result = SCIP_DIDNOTFIND;

   matrix = NULL;
   SCIP_CALL( SCIPmatrixCreate(scip, &matrix, &initialized, &complete) );
   if( initialized && complete )
   {
      SCIP_Real *ratios;
      int nratios;

      ncols = SCIPmatrixGetNColumns(matrix);
      nrows = SCIPmatrixGetNRows(matrix);
      nratios = 0;
      ncancels = 0;
      nchgcoef = 0;
      SCIP_CALL( SCIPallocBufferArray(scip, &processedvarsidx, ncols) );
      SCIP_CALL( SCIPallocBufferArray(scip, &ratios, nrows) );
      for(i=0; i<ncols; i++)
      {
         int nnonz;
         nnonz = SCIPmatrixGetColNNonzs(matrix, i);
         if(nnonz > 10 && SCIPvarGetType(SCIPmatrixGetVar(matrix, i)) == SCIP_VARTYPE_CONTINUOUS
               && !SCIPdoNotMultaggrVar(scip, SCIPmatrixGetVar(matrix, i)))
         {
            processedvarsidx[nprocessedvarsidx] = i;
            nprocessedvarsidx++;
         }
      }

      for(i=0; i<nprocessedvarsidx; i++)
      {
         int* colpnt = SCIPmatrixGetColIdxPtr(matrix, processedvarsidx[i]);
         SCIP_Real* valpnt = SCIPmatrixGetColValPtr(matrix, processedvarsidx[i]);
         SCIPsortIntReal(colpnt, valpnt, SCIPmatrixGetColNNonzs(matrix, processedvarsidx[i]));
   //      printf("%s, %d\n", SCIPmatrixGetColName(matrix, processedvarsidx[i]),
  //             SCIPmatrixGetColNNonzs(matrix, processedvarsidx[i]));
      }

      for(i=0; i<nprocessedvarsidx; i++)
      {
         int* colpnti = SCIPmatrixGetColIdxPtr(matrix, processedvarsidx[i]);
         SCIP_Real* valpnti = SCIPmatrixGetColValPtr(matrix, processedvarsidx[i]);
         SCIP_VAR* vari;
         SCIP_VAR* varj;
         SCIP_VAR* newvar;
         SCIP_CONS* newcons;
         SCIP_VAR* vars[2];
         SCIP_Real coefs[2];


         vari = SCIPmatrixGetVar(matrix, processedvarsidx[i]);
         SCIP_Real newlb;
         SCIP_Real newub;

         for(j=i+1; j<nprocessedvarsidx; j++)
         {
            SCIP_Real col1tocol2ratio;
            SCIP_Real col2tocol1ratio;
            SCIP_Bool infeasible;
            SCIP_Bool aggregated;
            SCIP_Bool col1tocol2success;
            SCIP_Bool col2tocol1success;


            varj = SCIPmatrixGetVar(matrix, processedvarsidx[j]);

            cancelCol(scip, matrix, processedvarsidx[i], processedvarsidx[j], &col1tocol2ratio, &col2tocol1ratio, &col1tocol2success,
                  &col2tocol1success, ratios, &nratios, &nchgcoef, &ncancels, &nfillins);
  //          printf("%8.4f %8.4f %8d %8d\n", col1tocol2ratio, col2tocol1ratio, col1tocol2success, col2tocol1success);
         }
      }

      presoldata->ncancels += ncancels;

      *nchgcoefs += nchgcoef;

      if( ncancels > 0 )
      {
         *result = SCIP_SUCCESS;
      }

      SCIPfreeBufferArray(scip, ratios);
      SCIPfreeBufferArray(scip, processedvarsidx);
   }

   SCIPmatrixFree(scip, &matrix);

   return SCIP_OKAY;
}

/*
 * presolver specific interface methods
 */

/** destructor of presolver to free user data (called when SCIP is exiting) */
static
SCIP_DECL_PRESOLFREE(presolFreeDualsparsify)
{  /*lint --e{715}*/
   SCIP_PRESOLDATA* presoldata;

   /* free presolver data */
   presoldata = SCIPpresolGetData(presol);
   assert(presoldata != NULL);

   SCIPfreeBlockMemory(scip, &presoldata);
   SCIPpresolSetData(presol, NULL);

   return SCIP_OKAY;
}

/** initialization method of presolver (called after problem was transformed) */
static
SCIP_DECL_PRESOLINIT(presolInitDualsparsify)
{
   SCIP_PRESOLDATA* presoldata;

   /* set the counters in the init (and not in the initpre) callback such that they persist across restarts */
   presoldata = SCIPpresolGetData(presol);
   presoldata->ncancels = 0;
   presoldata->nfillin = 0;
   presoldata->nfailures = 0;
   presoldata->nwaitingcalls = 0;

   return SCIP_OKAY;
}

/** creates the dualsparsify presolver and includes it in SCIP */
SCIP_RETCODE SCIPincludePresolDualsparsify(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_PRESOLDATA* presoldata;
   SCIP_PRESOL* presol;

   /* create dualsparsify presolver data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &presoldata) );

   /* include presolver */
   SCIP_CALL( SCIPincludePresolBasic(scip, &presol, PRESOL_NAME, PRESOL_DESC, PRESOL_PRIORITY, PRESOL_MAXROUNDS,
         PRESOL_TIMING, presolExecDualsparsify, presoldata) );

   SCIP_CALL( SCIPsetPresolCopy(scip, presol, presolCopyDualsparsify) );
   SCIP_CALL( SCIPsetPresolFree(scip, presol, presolFreeDualsparsify) );
   SCIP_CALL( SCIPsetPresolInit(scip, presol, presolInitDualsparsify) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "presolving/dualsparsify/enablecopy",
         "should dualsparsify presolver be copied to sub-SCIPs?",
         &presoldata->enablecopy, TRUE, DEFAULT_ENABLECOPY, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "presolving/dualsparsify/cancellinear",
         "should we cancel nonzeros in constraints of the linear constraint handler?",
         &presoldata->cancellinear, TRUE, DEFAULT_CANCELLINEAR, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "presolving/dualsparsify/preserveintcoefs",
         "should we forbid cancellations that destroy integer coefficients?",
         &presoldata->preserveintcoefs, TRUE, DEFAULT_PRESERVEINTCOEFS, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "presolving/dualsparsify/maxcontfillin",
         "maximal fillin for continuous variables (-1: unlimited)",
         &presoldata->maxcontfillin, FALSE, DEFAULT_MAX_CONT_FILLIN, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "presolving/dualsparsify/maxbinfillin",
         "maximal fillin for binary variables (-1: unlimited)",
         &presoldata->maxbinfillin, FALSE, DEFAULT_MAX_BIN_FILLIN, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "presolving/dualsparsify/maxintfillin",
         "maximal fillin for integer variables including binaries (-1: unlimited)",
         &presoldata->maxintfillin, FALSE, DEFAULT_MAX_INT_FILLIN, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "presolving/dualsparsify/maxnonzeros",
         "maximal support of one equality to be used for cancelling (-1: no limit)",
         &presoldata->maxnonzeros, TRUE, DEFAULT_MAXNONZEROS, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "presolving/dualsparsify/maxconsiderednonzeros",
         "maximal number of considered non-zeros within one row (-1: no limit)",
         &presoldata->maxconsiderednonzeros, TRUE, DEFAULT_MAXCONSIDEREDNONZEROS, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddCharParam(scip,
         "presolving/dualsparsify/rowsort",
         "order in which to process inequalities ('n'o sorting, 'i'ncreasing nonzeros, 'd'ecreasing nonzeros)",
         &presoldata->rowsort, TRUE, DEFAULT_ROWSORT, "nid", NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip,
         "presolving/dualsparsify/maxretrievefac",
         "limit on the number of useless vs. useful hashtable retrieves as a multiple of the number of constraints",
         &presoldata->maxretrievefac, TRUE, DEFAULT_MAXRETRIEVEFAC, 0.0, SCIP_REAL_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip,
         "presolving/dualsparsify/waitingfac",
         "number of calls to wait until next execution as a multiple of the number of useless calls",
         &presoldata->waitingfac, TRUE, DEFAULT_WAITINGFAC, 0.0, SCIP_REAL_MAX, NULL, NULL) );

   return SCIP_OKAY;
}
