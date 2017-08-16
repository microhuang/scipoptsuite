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

/**@file   sepa_zerohalf.c
 * @brief  {0,1/2}-cuts separator
 * @author Robert Lion Gottwald
 * @author Manuel Kutschka
 * @author Kati Wolter
 *
 * {0,1/2}-Chvátal-Gomory cuts separator. It solves the following separation problem:
 * Consider an integer program
 * \f[
 * \min \{ c^T x : Ax \leq b, x \geq 0, x \mbox{ integer} \}
 * \f]
 * and a fractional solution \f$x^*\f$ of its LP relaxation.  Find a weightvector \f$u\f$ whose entries \f$u_i\f$ are either 0 or
 * \f$\frac{1}{2}\f$ such that the following inequality is valid for all integral solutions and violated by \f$x^*\f$:
 * \f[
 * \lfloor(u^T A) x \rfloor \leq \lfloor u^T b\rfloor
 * \f]
 *
 * References:
 * - Alberto Caprara, Matteo Fischetti. {0,1/2}-Chvatal-Gomory cuts. Math. Programming, Volume 74, p221--235, 1996.
 * - Arie M. C. A. Koster, Adrian Zymolka and Manuel Kutschka. \n
 *   Algorithms to separate {0,1/2}-Chvatal-Gomory cuts.
 *   Algorithms - ESA 2007: 15th Annual European Symposium, Eilat, Israel, October 8-10, 2007, \n
 *   Proceedings. Lecture Notes in Computer Science, Volume 4698, p. 693--704, 2007.
 * - Arie M. C. A. Koster, Adrian Zymolka and Manuel Kutschka. \n
 *   Algorithms to separate {0,1/2}-Chvatal-Gomory cuts (Extended Version). \n
 *   ZIB Report 07-10, Zuse Institute Berlin, 2007. http://www.zib.de/Publications/Reports/ZR-07-10.pdf
 * - Manuel Kutschka. Algorithmen zur Separierung von {0,1/2}-Schnitten. Diplomarbeit. Technische Universitaet Berlin, 2007.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "string.h"
#include "scip/sepa_zerohalf.h"
#include "scip/cons_linear.h"
#include "scip/scipdefplugins.h"
#include "scip/struct_lp.h"

#define SEPA_NAME              "zerohalf"
#define SEPA_DESC              "{0,1/2}-cuts separator"
#define SEPA_PRIORITY             -6000
#define SEPA_FREQ                     0
#define SEPA_MAXBOUNDDIST           0.0
#define SEPA_USESSUBSCIP          FALSE
#define SEPA_DELAY                FALSE

#define MAXSLACK                    0.5

/* SCIPcalcMIR parameters */
#define BOUNDSWITCH                 0.5 /**< threshold for bound switching - see SCIPcalcMIR() */
#define USEVBDS                    TRUE /**< use variable bounds - see SCIPcalcMIR() */
#define ALLOWLOCAL                 TRUE /**< allow to generate local cuts - see SCIPcalcMIR() */
#define FIXINTEGRALRHS            FALSE /**< try to generate an integral rhs - see SCIPcalcMIR() */
#define BOUNDSFORTRANS             NULL
#define BOUNDTYPESFORTRANS         NULL
#define MINFRAC                    0.05
#define MAXFRAC                    1.00

/* SCIPcalcRowIntegralScalar parameters */
#define MAXDNOM                    1000
#define MAXSCALE                 1000.0

typedef struct Mod2Col MOD2_COL;
typedef struct Mod2Row MOD2_ROW;
typedef struct Mod2Matrix MOD2_MATRIX;
typedef struct TransIntRow TRANSINTROW;
typedef struct RowIndex ROWINDEX;

#define UNIQUE_INDEX(rowind) (3*(rowind).index + (rowind).type)

typedef enum {
   ORIG_RHS = 0,
   ORIG_LHS = 1,
   TRANSROW = 2,
} ROWIND_TYPE;

struct RowIndex
{
   unsigned int          type:2;             /**< type of row index; 0 means lp row using the right hand side,
                                              *   1 means lp row using the left hand side, and 2 means a
                                              *   transformed integral row
                                              */
   unsigned int          index:30;           /**< lp position of original row, or index of transformed integral row */
};

/** structure containing a transformed integral row obtained by relaxing an lp row */
struct TransIntRow
{
   SCIP_Real             slack;              /**< slack of row after transformation */
   SCIP_Real             rhs;                /**< right hand side value of integral row after transformation */
   SCIP_Real*            vals;               /**< values of row */
   int*                  varinds;            /**< problem variable indices of row */
   int                   size;               /**< alloc size of row */
   int                   len;                /**< length of row */
   int                   rank;               /**< rank of row */
   SCIP_Bool             local;              /**< is row local? */
};

/** structure representing a row in the mod 2 system */
struct Mod2Row {
   int                   index;
   int                   pos;
   int                   rhs;                /**< rhs of row */
   int                   nrowinds;           /**< number of elements in rowinds */
   int                   rowindssize;        /**< size of rowinds array */
   int                   nnonzcols;          /**< number of columns in nonzcols */
   int                   nonzcolssize;       /**< size of nonzcols array */
   ROWINDEX*             rowinds;
   MOD2_COL**            nonzcols;
   SCIP_Real             slack;
   SCIP_Real             maxsolval;
};

/** structure representing a column in the mod 2 system */
struct Mod2Col {
   int                   index;              /**< index of SCIP column associated to this column */
   int                   pos;                /**< position of column in matrix */
   SCIP_Real             solval;             /**< solution value of the column */
   SCIP_HASHTABLE*       nonzrows;           /**< map between a MOD2ROW and its index; used as a set, the set of rows that
                                              *   contain this column */
};

/** matrix representing the modulo 2 system */
struct Mod2Matrix {
   MOD2_COL**            cols;               /**< columns of the matrix */
   MOD2_ROW**            rows;               /**< rows of the matrix */
   TRANSINTROW*          transintrows;       /**< transformed integral rows obtained from non-integral lp rows */
   int                   ntransintrows;      /**< number of transformed integral rows obtained from non-integral lp rows */
   int                   nrows;              /**< number of rows of the matrix; number of elements in rows */
   int                   ncols;              /**< number of cols of the matrix; number of elements in cols */
   int                   rowssize;           /**< length of rows array */
   int                   colssize;           /**< length of cols array */
};

/** data of separator */
struct SCIP_SepaData
{
   SCIP_AGGRROW*         aggrrow;
   int                   ncuts;
   int                   nreductions;
   SCIP_Bool             infeasible;
};


#define COLINFO_GET_MOD2COL(x) ((MOD2_COL*)  (((uintptr_t)(x)) & ~((uintptr_t)1)))
#define COLINFO_GET_RHSOFFSET(x) ((int)  (((uintptr_t)(x)) & ((uintptr_t)1)))
#define COLINFO_CREATE(mod2col, rhsoffset)  ((void*) (((uintptr_t)(mod2col)) | ((uintptr_t)(rhsoffset))))


static
SCIP_DECL_SORTPTRCOMP(compareColIndex)
{
   MOD2_COL* col1;
   MOD2_COL* col2;

   col1 = (MOD2_COL*) elem1;
   col2 = (MOD2_COL*) elem2;

   if( col1->index < col2->index )
      return -1;
   if( col2->index < col1->index )
      return 1;

   return 0;
}

/** comparison function for slack of mod 2 rows */
static
SCIP_DECL_SORTPTRCOMP(compareRowSlack)
{
   MOD2_ROW* row1;
   MOD2_ROW* row2;

   row1 = (MOD2_ROW*) elem1;
   row2 = (MOD2_ROW*) elem2;

   /* small slack comes first */
   if( row1->slack < row2->slack )
      return -1;
   if( row2->slack < row1->slack )
      return 1;

   /* if slack is equal prefer rows that contain columns with large solution value */
   if( row1->maxsolval > row2->maxsolval )
      return -1;
   if( row2->maxsolval > row1->maxsolval )
      return 1;

   /* last tie breaker is to prefer sparser rows */
   if( row1->nnonzcols > row2->nnonzcols )
      return -1;
   if( row2->nnonzcols > row1->nnonzcols )
      return 1;

   return 0;
}

/** gets the index of a mod 2 row/col */
static
SCIP_DECL_HASHKEYVAL(hashKeyValIndex)
{
   /* the index is the first member for both structures so interpreting it as an int is standards compliant */
   return *((int*)key);
}

/** take integral real value modulo 2 */
static
int mod2(
   SCIP*                 scip,               /**< scip data structure */
   SCIP_Real             val                 /**< value to take mod 2 */
)
{
   assert(SCIPisIntegral(scip, val));
   val *= 0.5;
   return (int) (!SCIPisEQ(scip, SCIPfloor(scip, val), val));
}

static
SCIP_RETCODE mod2MatrixTransformContRows(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix          /**< mod2 matrix structure */
   )
{
   SCIP_ROW** rows;
   SCIP_VAR** vars;
   int nrows;
   int* intvarpos;
   int i;
   SCIP_CALL( SCIPgetLPRowsData(scip, &rows, &nrows) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &mod2matrix->transintrows, 2*nrows) );
   mod2matrix->ntransintrows = 0;
   vars = SCIPgetVars(scip);

   SCIP_CALL( SCIPallocCleanBufferArray(scip, &intvarpos, SCIPgetNVars(scip)) );

   for( i = 0; i < nrows; ++i )
   {
      int k;
      int rowlen;
      SCIP_Bool success;
      SCIP_Real activity;
      SCIP_Real lhs;
      SCIP_Real rhs;
      SCIP_Real lhsslack;
      SCIP_Real rhsslack;
      SCIP_Real* rowvals;
      SCIP_COL** rowcols;

      /* skip modifiable rows and rows that are already integral */
      if( SCIProwIsModifiable(rows[i]) || SCIProwIsIntegral(rows[i]) )
         continue;

      lhs = SCIProwGetLhs(rows[i]) - SCIProwGetConstant(rows[i]);
      rhs = SCIProwGetRhs(rows[i]) - SCIProwGetConstant(rows[i]);
      activity = SCIPgetRowLPActivity(scip, rows[i]);

      /* compute lhsslack: activity - lhs */
      if( SCIPisInfinity(scip, -SCIProwGetLhs(rows[i])) )
         lhsslack = SCIPinfinity(scip);
      else
      {
         lhsslack = activity - lhs;
      }

      /* compute rhsslack: rhs - activity */
      if( SCIPisInfinity(scip, SCIProwGetRhs(rows[i])) )
         rhsslack = SCIPinfinity(scip);
      else
         rhsslack = rhs - activity;

      if( rhsslack > MAXSLACK && lhsslack > MAXSLACK )
         continue;

      rowlen = SCIProwGetNLPNonz(rows[i]);
      rowvals = SCIProwGetVals(rows[i]);
      rowcols = SCIProwGetCols(rows[i]);

      if( rhsslack <= MAXSLACK )
      {
         SCIP_Bool local;
         int transrowlen;
         SCIP_Real transrowrhs;
         int* transrowvars;
         SCIP_Real* transrowvals;

         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &transrowvars, rowlen) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &transrowvals, rowlen) );
         transrowlen = 0;
         transrowrhs = rhs;

         for( k = 0; k < rowlen; ++k )
         {
            if( !SCIPcolIsIntegral(rowcols[k]) )
               continue;

            transrowvars[transrowlen] = rowcols[k]->var_probindex;
            transrowvals[transrowlen] = rowvals[k];
            intvarpos[rowcols[k]->var_probindex] = ++transrowlen;
         }

         success = TRUE;
         local = SCIProwIsLocal(rows[i]);

         for( k = 0; k < rowlen; ++k )
         {
            int closestvbdind;
            SCIP_Real closestbound;
            SCIP_VAR* vbdvar;
            SCIP_Real vbdcoef;
            SCIP_Real vbdconst;
            SCIP_VAR* colvar;

            if( SCIPcolIsIntegral(rowcols[k]) )
               continue;

            colvar = SCIPcolGetVar(rowcols[k]);

            if( rowvals[k] > 0.0 )
            {
               SCIP_CALL( SCIPgetVarClosestVlb(scip, colvar, NULL, &closestbound, &closestvbdind) );
               if( closestvbdind >= 0 )
               {
                  vbdcoef = SCIPvarGetVlbCoefs(colvar)[closestvbdind];
                  vbdvar = SCIPvarGetVlbVars(colvar)[closestvbdind];
                  vbdconst = SCIPvarGetVlbConstants(colvar)[closestvbdind];
               }
               else
               {
                  closestbound = SCIPvarGetLbGlobal(colvar);
                  if( ALLOWLOCAL && SCIPvarGetLbLocal(colvar) > closestbound )
                  {
                     closestbound = SCIPvarGetLbLocal(colvar);
                     local = TRUE;
                  }
               }
            }
            else
            {
               SCIP_CALL( SCIPgetVarClosestVub(scip, colvar, NULL, &closestbound, &closestvbdind) );
               if( closestvbdind >= 0 )
               {
                  vbdcoef = SCIPvarGetVubCoefs(colvar)[closestvbdind];
                  vbdvar = SCIPvarGetVubVars(colvar)[closestvbdind];
                  vbdconst = SCIPvarGetVubConstants(colvar)[closestvbdind];
               }
               else
               {
                  closestbound = SCIPvarGetUbGlobal(colvar);
                  if( ALLOWLOCAL && SCIPvarGetUbLocal(colvar) < closestbound )
                  {
                     closestbound = SCIPvarGetUbLocal(colvar);
                     local = TRUE;
                  }
               }
            }

            if( closestvbdind >= 0 )
            {
               SCIP_Real coef = rowvals[k] * vbdcoef;
               transrowrhs -= rowvals[k] * vbdconst;

               int pos = intvarpos[SCIPvarGetProbindex(vbdvar)] - 1;
               if( pos >= 0 )
               {
                  transrowvals[pos] += coef;
               }
               else
               {
                  transrowvars[transrowlen] = SCIPvarGetProbindex(vbdvar);
                  transrowvals[transrowlen] = coef;
                  intvarpos[SCIPvarGetProbindex(vbdvar)] = ++transrowlen;
               }
            }
            else if( !SCIPisInfinity(scip, REALABS(closestbound)) )
            {
               transrowrhs -= rowvals[k] * closestbound;
            }
            else
            {
               success = FALSE;
               break;
            }
         }

         for( k = 0; k < transrowlen;)
         {
            intvarpos[transrowvars[k]] = 0;
            if( SCIPisZero(scip, transrowvals[k]) )
            {
               --transrowlen;
               transrowvals[k] = transrowvals[transrowlen];
               transrowvars[k] = transrowvars[transrowlen];
            }
            else
               ++k;
         }

         if( transrowlen <= 1 )
            success = FALSE;

         if( success )
         {
            SCIP_Real intscalar;

            SCIP_CALL( SCIPcalcIntegralScalar(transrowvals, transrowlen, -SCIPepsilon(scip), SCIPepsilon(scip), MAXDNOM, MAXSCALE, &intscalar, &success) );

            if( success )
            {
               transrowrhs = SCIPfeasFloor(scip, transrowrhs * intscalar);
               SCIP_Real slack = transrowrhs;
               for( k = 0; k < transrowlen; ++k )
               {
                  SCIP_Real solval = SCIPgetVarSol(scip, vars[transrowvars[k]]);
                  transrowvals[k] = SCIPfeasRound(scip, transrowvals[k] * intscalar);
                  slack -= solval * transrowvals[k];
               }

               if( slack <= MAXSLACK )
               {
                  int r = mod2matrix->ntransintrows++;
                  mod2matrix->transintrows[r].rhs = transrowrhs;
                  mod2matrix->transintrows[r].slack = slack;
                  mod2matrix->transintrows[r].vals = transrowvals;
                  mod2matrix->transintrows[r].varinds = transrowvars;
                  mod2matrix->transintrows[r].len = transrowlen;
                  mod2matrix->transintrows[r].size = rowlen;
                  mod2matrix->transintrows[r].local = local;
                  mod2matrix->transintrows[r].rank = SCIProwGetRank(rows[i]);
               }
               else
               {
                  success = FALSE;
               }
            }
         }

         if( !success )
         {
            SCIPfreeBlockMemoryArray(scip, &transrowvals, rowlen);
            SCIPfreeBlockMemoryArray(scip, &transrowvars, rowlen);
         }
      }

      if( lhsslack <= MAXSLACK )
      {
         SCIP_Bool local;
         int transrowlen;
         SCIP_Real transrowrhs;
         int* transrowvars;
         SCIP_Real* transrowvals;

         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &transrowvars, rowlen) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &transrowvals, rowlen) );
         transrowlen = 0;
         transrowrhs = -lhs;

         for( k = 0; k < rowlen; ++k )
         {
            if( !SCIPcolIsIntegral(rowcols[k]) )
               continue;

            transrowvars[transrowlen] = rowcols[k]->var_probindex;
            transrowvals[transrowlen] = -rowvals[k];
            intvarpos[rowcols[k]->var_probindex] = ++transrowlen;
         }

         success = TRUE;
         local = SCIProwIsLocal(rows[i]);

         for( k = 0; k < rowlen; ++k )
         {
            int closestvbdind;
            SCIP_Real closestbound;
            SCIP_VAR* vbdvar;
            SCIP_Real vbdcoef;
            SCIP_Real vbdconst;
            SCIP_VAR* colvar;

            if( SCIPcolIsIntegral(rowcols[k]) )
               continue;

            colvar = SCIPcolGetVar(rowcols[k]);

            if( -rowvals[k] > 0.0 )
            {
               SCIP_CALL( SCIPgetVarClosestVlb(scip, colvar, NULL, &closestbound, &closestvbdind) );
               if( closestvbdind >= 0 )
               {
                  vbdcoef = SCIPvarGetVlbCoefs(colvar)[closestvbdind];
                  vbdvar = SCIPvarGetVlbVars(colvar)[closestvbdind];
                  vbdconst = SCIPvarGetVlbConstants(colvar)[closestvbdind];
               }
               else
               {
                  closestbound = SCIPvarGetLbGlobal(colvar);
                  if( ALLOWLOCAL && SCIPvarGetLbLocal(colvar) > closestbound )
                  {
                     closestbound = SCIPvarGetLbLocal(colvar);
                     local = TRUE;
                  }
               }
            }
            else
            {
               SCIP_CALL( SCIPgetVarClosestVub(scip, colvar, NULL, &closestbound, &closestvbdind) );
               if( closestvbdind >= 0 )
               {
                  vbdcoef = SCIPvarGetVubCoefs(colvar)[closestvbdind];
                  vbdvar = SCIPvarGetVubVars(colvar)[closestvbdind];
                  vbdconst = SCIPvarGetVubConstants(colvar)[closestvbdind];
               }
               else
               {
                  closestbound = SCIPvarGetUbGlobal(colvar);
                  if( ALLOWLOCAL && SCIPvarGetUbLocal(colvar) < closestbound )
                  {
                     closestbound = SCIPvarGetUbLocal(colvar);
                     local = TRUE;
                  }
               }
            }

            if( closestvbdind >= 0 )
            {
               SCIP_Real coef = -rowvals[k] * vbdcoef;
               transrowrhs += rowvals[k] * vbdconst;

               int pos = intvarpos[SCIPvarGetProbindex(vbdvar)] - 1;
               if( pos >= 0 )
               {
                  transrowvals[pos] += coef;
               }
               else
               {
                  transrowvars[transrowlen] = SCIPvarGetProbindex(vbdvar);
                  transrowvals[transrowlen] = coef;
                  intvarpos[SCIPvarGetProbindex(vbdvar)] = ++transrowlen;
               }
            }
            else if( !SCIPisInfinity(scip, REALABS(closestbound)) )
            {
               transrowrhs += rowvals[k] * closestbound;
            }
            else
            {
               success = FALSE;
               break;
            }
         }

         for( k = 0; k < transrowlen;)
         {
            intvarpos[transrowvars[k]] = 0;
            if( SCIPisZero(scip, transrowvals[k]) )
            {
               --transrowlen;
               transrowvals[k] = transrowvals[transrowlen];
               transrowvars[k] = transrowvars[transrowlen];
            }
            else
               ++k;
         }

         if( transrowlen <= 1 )
            success = FALSE;

         if( success )
         {
            SCIP_Real intscalar;
            SCIP_CALL( SCIPcalcIntegralScalar(transrowvals, transrowlen, -SCIPepsilon(scip), SCIPepsilon(scip), MAXDNOM, MAXSCALE, &intscalar, &success) );

            if( success )
            {
               transrowrhs = SCIPfeasFloor(scip, transrowrhs * intscalar);
               SCIP_Real slack = transrowrhs;
               for( k = 0; k < transrowlen; ++k )
               {
                  SCIP_Real solval = SCIPgetVarSol(scip, vars[transrowvars[k]]);
                  transrowvals[k] = SCIPfeasRound(scip, transrowvals[k] * intscalar);
                  slack -= solval * transrowvals[k];
               }

               if( slack <= MAXSLACK )
               {
                  int r = mod2matrix->ntransintrows++;
                  mod2matrix->transintrows[r].rhs = transrowrhs;
                  mod2matrix->transintrows[r].slack = slack;
                  mod2matrix->transintrows[r].vals = transrowvals;
                  mod2matrix->transintrows[r].varinds = transrowvars;
                  mod2matrix->transintrows[r].len = transrowlen;
                  mod2matrix->transintrows[r].size = rowlen;
                  mod2matrix->transintrows[r].local = local;
                  mod2matrix->transintrows[r].rank = SCIProwGetRank(rows[i]);
               }
               else
               {
                  success = FALSE;
               }
            }
         }

         if( !success )
         {
            SCIPfreeBlockMemoryArray(scip, &transrowvals, rowlen);
            SCIPfreeBlockMemoryArray(scip, &transrowvars, rowlen);
         }
      }
   }

   SCIPfreeCleanBufferArray(scip, &intvarpos);

   return SCIP_OKAY;
}


/** adds new column to the mod 2 matrix */
static
SCIP_RETCODE mod2MatrixAddCol(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,
   SCIP_HASHMAP*         origvar2col,
   SCIP_VAR*             origvar,
   SCIP_Real             solval,
   int                   rhsoffset
   )
{
   MOD2_COL* col;

   SCIP_CALL( SCIPallocBlockMemory(scip, &col) );
   col->pos = mod2matrix->ncols++;
   col->index = SCIPvarGetProbindex(origvar);
   col->solval = solval;
   SCIP_CALL( SCIPhashtableCreate(&col->nonzrows, SCIPblkmem(scip), 1, SCIPhashGetKeyStandard, SCIPhashKeyEqPtr,
                                  hashKeyValIndex, NULL) );

   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &mod2matrix->cols, &mod2matrix->colssize, mod2matrix->ncols) );
   mod2matrix->cols[col->pos] = col;

   SCIP_CALL( SCIPhashmapInsert(origvar2col, (void*) origvar, COLINFO_CREATE(col, rhsoffset)) );

   return SCIP_OKAY;
}

/** links row to mod 2 column */
static
SCIP_RETCODE mod2colLinkRow(
   MOD2_COL*             col,                /**< mod 2 column */
   MOD2_ROW*             row                 /**< mod 2 row */
   )
{
   SCIP_CALL( SCIPhashtableInsert(col->nonzrows, (void*)row) );

   row->maxsolval = MAX(col->solval, row->maxsolval);

   return SCIP_OKAY;
}

/** unlinks row from mod 2 column */
static
SCIP_RETCODE mod2colUnlinkRow(
   MOD2_COL*             col,                /**< mod 2 column */
   MOD2_ROW*             row                 /**< mod 2 row */
   )
{
   SCIP_CALL( SCIPhashtableRemove(col->nonzrows, (void*)row) );

   return SCIP_OKAY;
}

/** unlinks row from mod 2 column */
static
void mod2rowUnlinkCol(
   MOD2_ROW*             row                 /**< mod 2 row */,
   MOD2_COL*             col                 /**< mod 2 column */
   )
{
   int i;

   assert(row->nnonzcols == 0 || row->nonzcols != NULL);

   SCIP_UNUSED( SCIPsortedvecFindPtr((void**) row->nonzcols, compareColIndex, col, row->nnonzcols, &i) );
   assert(row->nonzcols[i] == col);

   --row->nnonzcols;
   BMSmoveMemoryArray(row->nonzcols + i, row->nonzcols + i + 1, row->nnonzcols - i);

   if( row->maxsolval == col->solval )
   {
      row->maxsolval = 0.0;
      for( i = 0; i < row->nnonzcols; ++i )
      {
         row->maxsolval = MAX(row->nonzcols[i]->solval, row->maxsolval);
      }
   }
}

static
SCIP_RETCODE mod2MatrixAddOrigRow(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< modulo 2 matrix */
   SCIP_HASHMAP*         origcol2col,        /**< hashmap to retrieve the mod 2 column from a SCIP_COL */
   SCIP_ROW*             origrow,            /**< original SCIP row */
   SCIP_Real             slack,              /**< slack of row */
   ROWIND_TYPE           side,               /**< side of row that is used for mod 2 row, must be ORIG_RHS or ORIG_LHS */
   int                   rhsmod2             /**< modulo 2 value of the row's right hand side */
   )
{
   SCIP_Real* rowvals;
   SCIP_COL** rowcols;
   int rowlen;
   int i;
   MOD2_ROW* row;

   SCIP_CALL( SCIPallocBlockMemory(scip, &row) );

   row->index = mod2matrix->nrows++;
   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &mod2matrix->rows, &mod2matrix->rowssize, mod2matrix->nrows) );
   mod2matrix->rows[row->index] = row;

   row->slack = slack;
   row->maxsolval = 0.0;
   row->rhs = rhsmod2;
   row->nrowinds = 1;
   row->rowinds = NULL;
   row->rowindssize = 0;

   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->rowinds, &row->rowindssize, row->nrowinds) );
   row->rowinds[0].type = side;
   row->rowinds[0].index = SCIProwGetLPPos(origrow);

   row->nnonzcols = 0;
   row->nonzcolssize = 0;
   row->nonzcols = NULL;

   rowlen = SCIProwGetNNonz(origrow);
   rowvals = SCIProwGetVals(origrow);
   rowcols = SCIProwGetCols(origrow);

   for( i = 0; i < rowlen; ++i )
   {
      if( mod2(scip, rowvals[i]) == 1 )
      {
         void* colinfo = SCIPhashmapGetImage(origcol2col, (void*)SCIPcolGetVar(rowcols[i]));

         /* extract the righthand side offset from the colinfo and update the righthand side */
         int rhsoffset = COLINFO_GET_RHSOFFSET(colinfo);
         row->rhs = (row->rhs + rhsoffset) % 2;

         /* extract the column pointer from the colinfo */
         MOD2_COL* col = COLINFO_GET_MOD2COL(colinfo);

         if( col != NULL )
         {
            int k;

            k = row->nnonzcols++;

            SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->nonzcols, &row->nonzcolssize, row->nnonzcols) );
            row->nonzcols[k] = col;

            SCIP_CALL( mod2colLinkRow(col, row) );
         }
      }
   }

   SCIPsortPtr((void**)row->nonzcols, compareColIndex, row->nnonzcols);

   return SCIP_OKAY;
}

static
SCIP_RETCODE mod2MatrixAddTransRow(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< modulo 2 matrix */
   SCIP_HASHMAP*         origcol2col,        /**< hashmap to retrieve the mod 2 column from a SCIP_COL */
   int                   transrowind         /**< index to transformed int row */
   )
{
   int i;
   SCIP_VAR** vars;
   MOD2_ROW* row;
   TRANSINTROW* introw;

   SCIP_CALL( SCIPallocBlockMemory(scip, &row) );

   vars = SCIPgetVars(scip);
   introw = &mod2matrix->transintrows[transrowind];

   row->index = mod2matrix->nrows++;
   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &mod2matrix->rows, &mod2matrix->rowssize, mod2matrix->nrows) );
   mod2matrix->rows[row->index] = row;

   row->slack = introw->slack;
   row->rhs = mod2(scip, introw->rhs);
   row->nrowinds = 1;
   row->rowinds = NULL;
   row->rowindssize = 0;

   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->rowinds, &row->rowindssize, row->nrowinds) );
   row->rowinds[0].type = TRANSROW;
   row->rowinds[0].index = transrowind;

   row->nnonzcols = 0;
   row->nonzcolssize = 0;
   row->nonzcols = NULL;

   for( i = 0; i < introw->len; ++i )
   {
      if( mod2(scip, introw->vals[i]) == 1 )
      {
         void* colinfo = SCIPhashmapGetImage(origcol2col, (void*)vars[introw->varinds[i]]);

         /* extract the righthand side offset from the colinfo and update the righthand side */
         int rhsoffset = COLINFO_GET_RHSOFFSET(colinfo);
         row->rhs = (row->rhs + rhsoffset) % 2;

         /* extract the column pointer from the colinfo */
         MOD2_COL* col = COLINFO_GET_MOD2COL(colinfo);

         if( col != NULL )
         {
            int k;

            k = row->nnonzcols++;

            SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->nonzcols, &row->nonzcolssize, row->nnonzcols) );
            row->nonzcols[k] = col;

            SCIP_CALL( mod2colLinkRow(col, row) );
         }
      }
   }

   SCIPsortPtr((void**)row->nonzcols, compareColIndex, row->nnonzcols);

   return SCIP_OKAY;
}

static
void destroyMod2Matrix(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix
   )
{
   int i;

   for( i = 0; i < mod2matrix->ncols; ++i )
   {
      SCIPhashtableFree(&mod2matrix->cols[i]->nonzrows);
      SCIPfreeBlockMemory(scip, &mod2matrix->cols[i]);
   }

   for( i = 0; i < mod2matrix->nrows; ++i )
   {
      SCIPfreeBlockMemoryArrayNull(scip, &mod2matrix->rows[i]->nonzcols, mod2matrix->rows[i]->nonzcolssize);
      SCIPfreeBlockMemoryArrayNull(scip, &mod2matrix->rows[i]->rowinds, mod2matrix->rows[i]->rowindssize);
      SCIPfreeBlockMemory(scip, &mod2matrix->rows[i]);
   }

   for( i = 0; i < mod2matrix->ntransintrows; ++i )
   {
      SCIPfreeBlockMemoryArray(scip, &mod2matrix->transintrows[i].vals, mod2matrix->transintrows[i].size);
      SCIPfreeBlockMemoryArray(scip, &mod2matrix->transintrows[i].varinds, mod2matrix->transintrows[i].size);
   }

   SCIPfreeBlockMemoryArray(scip, &mod2matrix->transintrows, 2*SCIPgetNLPRows(scip));

   SCIPfreeBlockMemoryArrayNull(scip, &mod2matrix->rows, mod2matrix->rowssize);
   SCIPfreeBlockMemoryArrayNull(scip, &mod2matrix->cols, mod2matrix->colssize);
}

/** build the modulo 2 matrix from all integral rows in the LP */
static
SCIP_RETCODE buildMod2Matrix(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix
   )
{
   SCIP_VAR** vars;
   SCIP_ROW** rows;
   SCIP_COL** cols;
   SCIP_HASHMAP* origcol2col;
   int ncols;
   int nrows;
   int nintvars;
   int i;
   SCIP_CALL( SCIPgetLPRowsData(scip, &rows, &nrows) );
   SCIP_CALL( SCIPgetLPColsData(scip, &cols, &ncols) );

   nintvars = SCIPgetNVars(scip) - SCIPgetNContVars(scip);
   vars = SCIPgetVars(scip);

   /* initialize fields */
   mod2matrix->cols = NULL;
   mod2matrix->colssize = 0;
   mod2matrix->ncols = 0;
   mod2matrix->rows = NULL;
   mod2matrix->rowssize = 0;
   mod2matrix->nrows = 0;

   SCIP_CALL( SCIPhashmapCreate(&origcol2col, SCIPblkmem(scip), 1) );

   /* add all integral vars if they are not at their bound */
   for( i = 0; i < nintvars; ++i )
   {
      SCIP_Real lb;
      SCIP_Real ub;
      SCIP_Real lbsol;
      SCIP_Real ubsol;
      SCIP_Real primsol;
      SCIP_Bool useub;

      primsol = SCIPgetVarSol(scip, vars[i]);

      lb = ALLOWLOCAL ? SCIPvarGetLbLocal(vars[i]) : SCIPvarGetLbGlobal(vars[i]);
      lbsol = primsol - lb;
      if( SCIPisZero(scip, lbsol) )
      {
         SCIP_CALL( SCIPhashmapInsert(origcol2col, (void*) vars[i], COLINFO_CREATE(NULL, mod2(scip, lb))) );
         continue;
      }

      ub = ALLOWLOCAL ? SCIPvarGetUbLocal(vars[i]) : SCIPvarGetUbGlobal(vars[i]);
      ubsol = ub - primsol;
      if( SCIPisZero(scip, ubsol) )
      {
         SCIP_CALL( SCIPhashmapInsert(origcol2col, (void*) vars[i], COLINFO_CREATE(NULL, mod2(scip, ub))) );
         continue;
      }

      if( SCIPisInfinity(scip, ub) ) /* if there is no ub, use lb */
         useub = FALSE;
      else if( SCIPisInfinity(scip, -lb) ) /* if there is no lb, use ub */
         useub = TRUE;
      else if( SCIPisLT(scip, primsol, (1.0 - BOUNDSWITCH) * lb + BOUNDSWITCH * ub) )
         useub = FALSE;
      else //if( SCIPisGT(scip, primsol, (1.0 - BOUNDSWITCH) * lb + BOUNDSWITCH * ub) )
         useub = TRUE;

      if( useub )
      {
         mod2MatrixAddCol(scip, mod2matrix, origcol2col, vars[i], ubsol, mod2(scip, ub));
      }
      else
      {
         mod2MatrixAddCol(scip, mod2matrix, origcol2col, vars[i], lbsol, mod2(scip, lb));
      }
   }

   /* add all integral rows using the created columns */
   for( i = 0; i < nrows; ++i )
   {
      if( SCIProwIsIntegral(rows[i]) )
      {
         SCIP_Real activity;
         SCIP_Real lhsslack;
         SCIP_Real rhsslack;
         int lhsmod2 = 0;
         int rhsmod2 = 0;

         activity = SCIPgetRowLPActivity(scip, rows[i]);

         /* compute lhsslack: activity - lhs */
         if( SCIPisInfinity(scip, -SCIProwGetLhs(rows[i])) )
            lhsslack = SCIPinfinity(scip);
         else
         {
            lhsslack = activity - SCIProwGetLhs(rows[i]);
            lhsmod2 = mod2(scip, SCIProwGetLhs(rows[i]));
         }

         /* compute rhsslack: rhs - activity */
         if( SCIPisInfinity(scip, SCIProwGetRhs(rows[i])) )
            rhsslack = SCIPinfinity(scip);
         else
         {
            rhsslack = SCIProwGetRhs(rows[i]) - activity;
            rhsmod2 = mod2(scip, SCIProwGetRhs(rows[i]));
         }

         if( rhsslack <= MAXSLACK && lhsslack <= MAXSLACK )
         {
            if( lhsmod2 == rhsmod2 )
            {
               /* MAXSLACK < 1 implies rhs - lhs = rhsslack + lhsslack < 2. Therefore lhs = rhs (mod2) can only hold if they
                * are equal
                */
               assert(SCIPisEQ(scip, SCIProwGetLhs(rows[i]), SCIProwGetRhs(rows[i])));

               /* use rhs */
               SCIP_CALL( mod2MatrixAddOrigRow(scip, mod2matrix, origcol2col, rows[i], rhsslack, ORIG_RHS, rhsmod2) );
            }
            else
            {
               /* use both */
               SCIP_CALL( mod2MatrixAddOrigRow(scip, mod2matrix, origcol2col, rows[i], lhsslack, ORIG_LHS, lhsmod2) );
               SCIP_CALL( mod2MatrixAddOrigRow(scip, mod2matrix, origcol2col, rows[i], rhsslack, ORIG_RHS, rhsmod2) );
            }
         }
         else if( rhsslack <= MAXSLACK )
         {
            /* use rhs */
            SCIP_CALL( mod2MatrixAddOrigRow(scip, mod2matrix, origcol2col, rows[i], rhsslack, ORIG_RHS, rhsmod2) );
         }
         else if( lhsslack <= MAXSLACK )
         {
            /* use lhs */
            SCIP_CALL( mod2MatrixAddOrigRow(scip, mod2matrix, origcol2col, rows[i], lhsslack, ORIG_LHS, lhsmod2) );
         }
      }
   }

   /* transform non-integral rows */
   SCIP_CALL( mod2MatrixTransformContRows(scip, mod2matrix) );

   /* add all transformed integral rows using the created columns */
   for( i = 0; i < mod2matrix->ntransintrows; ++i )
   {
      mod2MatrixAddTransRow(scip, mod2matrix, origcol2col, i);
   }

   SCIPhashmapFree(&origcol2col);

   return SCIP_OKAY;
}

static
SCIP_DECL_HASHKEYEQ(columnsEqual)
{
   MOD2_COL* col1;
   MOD2_COL* col2;
   int col1entries;
   int i;

   col1 = (MOD2_COL*) key1;
   col2 = (MOD2_COL*) key2;

   if( SCIPhashtableGetNElements(col1->nonzrows) != SCIPhashtableGetNElements(col2->nonzrows) )
      return FALSE;

   col1entries = SCIPhashtableGetNEntries(col1->nonzrows);
   for( i = 0; i < col1entries; ++i )
   {
      MOD2_ROW* row = (MOD2_ROW*)SCIPhashtableGetEntry(col1->nonzrows, i);

      if( row != NULL && !SCIPhashtableExists(col2->nonzrows, (void*)row) )
         return FALSE;
   }

   return TRUE;
}

static
SCIP_DECL_HASHKEYVAL(columnGetSignature)
{
   MOD2_COL* col;
   int i;
   int colentries;
   uint64_t signature;

   col = (MOD2_COL*) key;

   colentries = SCIPhashtableGetNEntries(col->nonzrows);
   signature = 0;
   for( i = 0; i < colentries; ++i )
   {
      MOD2_ROW* row = (MOD2_ROW*) SCIPhashtableGetEntry(col->nonzrows, i);
      if( row != NULL )
         signature |= SCIPhashSignature64(row->index);
   }

   return signature;
}

static
SCIP_DECL_HASHKEYEQ(rowsEqual)
{
   MOD2_ROW* row1;
   MOD2_ROW* row2;
   int i;

   row1 = (MOD2_ROW*) key1;
   row2 = (MOD2_ROW*) key2;

   assert(row1 != NULL);
   assert(row2 != NULL);
   assert(row1->nnonzcols == 0 || row1->nonzcols != NULL);
   assert(row2->nnonzcols == 0 || row2->nonzcols != NULL);

   if( row1->nnonzcols != row2->nnonzcols || row1->rhs != row2->rhs )
      return FALSE;

   for( i = 0; i < row1->nnonzcols; ++i )
   {
      if( row1->nonzcols[i] != row2->nonzcols[i] )
         return FALSE;
   }

   return TRUE;
}

static
SCIP_DECL_HASHKEYVAL(rowGetSignature)
{
   MOD2_ROW* row;
   int i;
   uint64_t signature;

   row = (MOD2_ROW*) key;
   assert(row->nnonzcols == 0 || row->nonzcols != NULL);

   signature = row->rhs;

   for( i = 0; i < row->nnonzcols; ++i )
      signature |= SCIPhashSignature64(row->nonzcols[i]->index);

   return signature;
}


static
void mod2matrixRemoveRow(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< the mod 2 matrix */
   MOD2_ROW*             row                 /**< mod 2 row */
   )
{
   int i;
   int position = row->pos;

   /* remove the row from the array */
   --mod2matrix->nrows;
   mod2matrix->rows[position] = mod2matrix->rows[mod2matrix->nrows];
   mod2matrix->rows[position]->pos = position;

   /* unlink columns from row */
   for( i = 0; i < row->nnonzcols; ++i )
   {
      mod2colUnlinkRow(row->nonzcols[i], row);
   }

   /* free row */
   SCIPfreeBlockMemoryArrayNull(scip, &row->nonzcols, row->nonzcolssize);
   SCIPfreeBlockMemoryArray(scip, &row->rowinds, row->rowindssize);
   SCIPfreeBlockMemory(scip, &row);
}

/** removes a column from the mod 2 matrix */
static
void mod2matrixRemoveCol(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< the mod 2 matrix */
   MOD2_COL*             col                 /**< a column in the mod 2 matrix */
   )
{
   int i;
   int colentries;
   int position = col->pos;

   /* remove column from arrays */
   --mod2matrix->ncols;
   mod2matrix->cols[position] = mod2matrix->cols[mod2matrix->ncols];
   mod2matrix->cols[position]->pos = position;

   colentries = SCIPhashtableGetNEntries(col->nonzrows);

   /* adjust rows of column */
   for( i = 0; i < colentries; ++i )
   {
      MOD2_ROW* row = (MOD2_ROW*) SCIPhashtableGetEntry(col->nonzrows, i);
      if( row != NULL )
         mod2rowUnlinkCol(row, col);
   }

   /* free column */
   SCIPhashtableFree(&col->nonzrows);
   SCIPfreeBlockMemory(scip, &col);
}

/* remove columns that are (Prop3 iii) zero (Prop3 iv) identify indentical columns (Prop3 v) unit vector columns */
static
SCIP_RETCODE mod2matrixPreprocessColumns(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< mod 2 matrix */
   SCIP_SEPADATA*        sepadata            /**< zerohalf separator data */
   )
{
   int i;
   SCIP_HASHTABLE* columntable;

   SCIP_CALL( SCIPhashtableCreate(&columntable, SCIPblkmem(scip), mod2matrix->ncols,
                                  SCIPhashGetKeyStandard, columnsEqual, columnGetSignature, NULL) );

   for( i = 0; i < mod2matrix->ncols; )
   {
      MOD2_COL* col = mod2matrix->cols[i];
      int nnonzrows = SCIPhashtableGetNElements(col->nonzrows);
      if( nnonzrows == 0 )
      { /* Prop3 iii */
         mod2matrixRemoveCol(scip, mod2matrix, col);
      }
      else if( nnonzrows == 1 )
      { /* Prop3 v */
         MOD2_ROW* row;
         int j = 0;
         do
         {
            row = (MOD2_ROW*) SCIPhashtableGetEntry(col->nonzrows, j++);
         } while( row == NULL );
         /* column is unit vector, so add its solution value to the rows slack and remove it */
         row->slack += col->solval;

         mod2matrixRemoveCol(scip, mod2matrix, col);
         ++sepadata->nreductions;
      }
      else
      {
         MOD2_COL* identicalcol;
         identicalcol = (MOD2_COL*)SCIPhashtableRetrieve(columntable, col);
         if( identicalcol != NULL )
         {
            assert(identicalcol != col);
            /* column is identical to other column so add its solution value to the other one and then remove and free it */
            identicalcol->solval += col->solval;

            mod2matrixRemoveCol(scip, mod2matrix, col);
         }
         else
         {
            SCIP_CALL( SCIPhashtableInsert(columntable, (void*)col) );
            ++i;
         }
      }
   }

   SCIPhashtableFree(&columntable);

   return SCIP_OKAY;
}


/** generate a zerohalf cut from a given mod 2 row, i.e., try if aggregations of rows of the
 * mod2 matrix give violated cuts
 */
static
SCIP_RETCODE generateZerohalfCut(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< mod 2 matrix */
   SCIP_SEPA*            sepa,               /**< zerohalf separator */
   SCIP_SEPADATA*        sepadata,           /**< zerohalf separator data */
   MOD2_ROW*             row                 /**< mod 2 row */
   )
{
   SCIP_ROW** rows;
   int i;
   SCIP_Bool cutislocal;
   int cutnnz;
   SCIP_Real* cutcoefs;
   int* cutinds;
   SCIP_Real cutrhs;
   SCIP_Real cutefficacy;
   SCIP_Bool success;
   int cutrank;

   rows = SCIPgetLPRows(scip);

   SCIPaggrRowClear(sepadata->aggrrow);

   for( i = 0; i < row->nrowinds; ++i )
   {
      switch( row->rowinds[i].type )
      {
         case ORIG_RHS:
            SCIP_CALL( SCIPaggrRowAddRow(scip, sepadata->aggrrow, rows[row->rowinds[i].index], 0.5, 1) );
            break;
         case ORIG_LHS:
            SCIP_CALL( SCIPaggrRowAddRow(scip, sepadata->aggrrow, rows[row->rowinds[i].index], -0.5, -1) );
            break;
         case TRANSROW: {
            TRANSINTROW* introw = &mod2matrix->transintrows[row->rowinds[i].index];
            SCIPdebugMsg(scip, "using transformed row %i of length %i with slack %f and rhs %f for cut\n", row->rowinds[i].index, introw->len, introw->slack, introw->rhs);
            SCIP_CALL( SCIPaggrRowAddCustomCons(scip, sepadata->aggrrow, introw->varinds, introw->vals,
                                                introw->len, introw->rhs, 0.5, introw->rank, introw->local) );
         }

      }
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &cutcoefs, SCIPgetNVars(scip)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &cutinds, SCIPgetNVars(scip)) );
   SCIP_CALL( SCIPcalcMIR(scip, NULL, BOUNDSWITCH, USEVBDS, ALLOWLOCAL, FIXINTEGRALRHS, BOUNDSFORTRANS, BOUNDTYPESFORTRANS,
                          MINFRAC, MAXFRAC, 1.0, sepadata->aggrrow, cutcoefs, &cutrhs, cutinds, &cutnnz, &cutefficacy, &cutrank,
                          &cutislocal, &success) );

   if( success && SCIPisEfficacious(scip, cutefficacy) )
   {
      SCIP_VAR** vars;
      SCIP_ROW* cut;
      char cutname[SCIP_MAXSTRLEN];
      int v;

      vars = SCIPgetVars(scip);

      /* create the cut */
      (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "zerohalf%d_x%d", SCIPgetNLPs(scip), row->index);

      SCIP_CALL( SCIPcreateEmptyRowSepa(scip, &cut, sepa, cutname, -SCIPinfinity(scip), cutrhs, cutislocal, FALSE, TRUE) );

      /*SCIPdebug( SCIP_CALL(SCIPprintRow(scip, cut, NULL)) );*/
      SCIProwChgRank(cut, cutrank);

      /* cache the row extension and only flush them if the cut gets added */
      SCIP_CALL( SCIPcacheRowExtensions(scip, cut) );

      /* collect all non-zero coefficients */
      for( v = 0; v < cutnnz; ++v )
      {
         SCIP_CALL( SCIPaddVarToRow(scip, cut, vars[cutinds[v]], cutcoefs[v]) );
      }

#if 0
      /* try to scale the cut to integral values */
      SCIP_CALL( SCIPmakeRowIntegral(scip, cut, -SCIPepsilon(scip), SCIPsumepsilon(scip),
         maxdnom, maxscale, MAKECONTINTEGRAL, &success) );
#endif
      /* flush all changes before adding the cut */
      SCIP_CALL( SCIPflushRowExtensions(scip, cut) );

      /*SCIPdebug( SCIP_CALL(SCIPprintRow(scip, cut, NULL)) );*/
      SCIP_CALL( SCIPaddCut(scip, NULL, cut, FALSE, &sepadata->infeasible) );

      if( !sepadata->infeasible && !cutislocal )
      {
         SCIP_CALL( SCIPaddPoolCut(scip, cut) );
      }

      sepadata->ncuts++;

      /* release the row */
      SCIP_CALL( SCIPreleaseRow(scip, &cut) );
      assert(success);
   }

   SCIPfreeBufferArray(scip, &cutinds);
   SCIPfreeBufferArray(scip, &cutcoefs);

   return SCIP_OKAY;
}


/** remove rows that are (a) zero (b) identical to other rows (keep the one with smallest slack) (c) have slack greater
 * than 1 (d) for zero rows with 1 as rhs and slack less than 1, we can directly generate a cut and remove the row (Lemma 4)
 */
static
SCIP_RETCODE mod2matrixPreprocessRows(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< the mod 2 matrix */
   SCIP_SEPA*            sepa,               /**< the zerohalf separator */
   SCIP_SEPADATA*        sepadata            /**< data of the zerohalf separator */
   )
{
   int i;
   SCIP_HASHTABLE* rowtable;

   SCIP_CALL( SCIPhashtableCreate(&rowtable, SCIPblkmem(scip), mod2matrix->nrows,
                                  SCIPhashGetKeyStandard, rowsEqual, rowGetSignature, NULL) );

   for( i = 0; i < mod2matrix->nrows; )
   {
      MOD2_ROW* row = mod2matrix->rows[i];
      row->pos = i;

      assert(row->nnonzcols == 0 || row->nonzcols != NULL);


      if( (row->nnonzcols == 0 && row->rhs == 0) || row->slack > MAXSLACK )
      { /* (a) and (c) */
         sepadata->nreductions += row->nnonzcols;
         mod2matrixRemoveRow(scip, mod2matrix, row);
      }
      else if( row->nnonzcols > 0 )
      { /* (b) */
         MOD2_ROW* identicalrow;
         identicalrow = (MOD2_ROW*)SCIPhashtableRetrieve(rowtable, (void*)row);
         if( identicalrow != NULL )
         {
            assert(identicalrow != row);
            assert(identicalrow->nnonzcols == 0 || identicalrow->nonzcols != NULL);

            /* row is identical to other row; only keep the one with smaller slack */
            if( identicalrow->slack <= row->slack )
            {
               mod2matrixRemoveRow(scip, mod2matrix, row);
            }
            else
            {
               assert(SCIPhashtableExists(rowtable, (void*)identicalrow));

               SCIP_CALL( SCIPhashtableRemove(rowtable, (void*)identicalrow) );
               assert(!SCIPhashtableExists(rowtable, (void*)identicalrow));

               SCIP_CALL( SCIPhashtableInsert(rowtable, (void*)row) );

               SCIPswapPointers((void**) &mod2matrix->rows[row->pos], (void**) &mod2matrix->rows[identicalrow->pos]);
               SCIPswapInts(&row->pos, &identicalrow->pos);

               assert(mod2matrix->rows[row->pos] == row && mod2matrix->rows[identicalrow->pos] == identicalrow);
               assert(identicalrow->pos == i);
               assert(row->pos < i);

               mod2matrixRemoveRow(scip, mod2matrix, identicalrow);
            }
         }
         else
         {
            SCIP_CALL( SCIPhashtableInsert(rowtable, (void*)row) );
            ++i;
         }
      }
      else
      {
         /* (d) */
         assert(row->nnonzcols == 0 && row->rhs == 1 && SCIPisLT(scip, row->slack, 1.0));

         SCIP_CALL( generateZerohalfCut(scip, mod2matrix, sepa, sepadata, row) );

         if( sepadata->infeasible )
            return SCIP_OKAY;

         mod2matrixRemoveRow(scip, mod2matrix, row);
         ++i;
      }
   }

   SCIPhashtableFree(&rowtable);

   return SCIP_OKAY;
}

/** add a mod2 row to another one */
static
SCIP_RETCODE mod2rowAddRow(
   SCIP*                 scip,               /**< scip data structure */
   MOD2_MATRIX*          mod2matrix,         /**< mod 2 matrix */
   MOD2_ROW*             row,                /**< mod 2 row */
   MOD2_ROW*             rowtoadd            /**< mod 2 row that is added to the other mod 2 row */
   )
{
   uint8_t* contained;
   int i;
   int j;
   int k;
   int nnewentries;
   int nlprows;
   MOD2_COL** newnonzcols;

   assert(row->nnonzcols == 0 || row->nonzcols != NULL);
   assert(rowtoadd->nnonzcols == 0 || rowtoadd->nonzcols != NULL);

   nlprows = SCIPgetNLPRows(scip);
   row->rhs ^= rowtoadd->rhs;
   row->slack += rowtoadd->slack;

   /* shift indices by nlprows due to the left hand side indices being negative and allocate twice
    * twice the number of rows
    */
   int allocsize = 3 * MAX(nlprows, mod2matrix->ntransintrows);
   SCIP_CALL( SCIPallocCleanBufferArray(scip, &contained, allocsize) );

   /* remember entries that are in the row to add */
   for( i = 0; i < rowtoadd->nrowinds; ++i )
   {
      contained[UNIQUE_INDEX(rowtoadd->rowinds[i])] = 1;
   }

   /* remove the entries that are in both rows from the row (1 + 1 = 0 (mod 2)) */
   nnewentries = rowtoadd->nrowinds;
   for( i = 0; i < row->nrowinds; )
   {
      if( contained[UNIQUE_INDEX(row->rowinds[i])] )
      {
         --nnewentries;
         contained[UNIQUE_INDEX(row->rowinds[i])] = 0;
         --row->nrowinds;
         row->rowinds[i] = row->rowinds[row->nrowinds];
      }
      else
      {
         ++i;
      }
   }

   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->rowinds, &row->rowindssize, row->nrowinds + nnewentries) );

   /* add remaining entries of row to add */
   for ( i = 0; i < rowtoadd->nrowinds; ++i )
   {
      if( contained[UNIQUE_INDEX(rowtoadd->rowinds[i])] )
      {
         contained[UNIQUE_INDEX(rowtoadd->rowinds[i])] = 0;
         row->rowinds[row->nrowinds++] = rowtoadd->rowinds[i];
      }
   }

   SCIPfreeCleanBufferArray(scip, &contained);

   SCIP_CALL( SCIPallocBufferArray(scip, &newnonzcols, row->nnonzcols + rowtoadd->nnonzcols) );

   i = 0;
   j = 0;
   k = 0;

   /* since columns are sorted we can merge them */
   while( i < row->nnonzcols && j < rowtoadd->nnonzcols )
   {
      if( row->nonzcols[i] == rowtoadd->nonzcols[j] )
      {
         mod2colUnlinkRow(row->nonzcols[i], row);
         ++i;
         ++j;
      }
      else if( row->nonzcols[i]->index < rowtoadd->nonzcols[j]->index )
      {
         newnonzcols[k++] = row->nonzcols[i++];
      }
      else
      {
         SCIP_CALL( mod2colLinkRow(rowtoadd->nonzcols[j], row) );
         newnonzcols[k++] = rowtoadd->nonzcols[j++];
      }
   }

   while( i < row->nnonzcols )
   {
      newnonzcols[k++] = row->nonzcols[i++];
   }

   while( j <  rowtoadd->nnonzcols )
   {
      SCIP_CALL( mod2colLinkRow(rowtoadd->nonzcols[j], row) );
      newnonzcols[k++] = rowtoadd->nonzcols[j++];
   }

   row->nnonzcols = k;
   SCIP_CALL( SCIPensureBlockMemoryArray(scip, &row->nonzcols, &row->nonzcolssize, row->nnonzcols) );
   BMScopyMemoryArray(row->nonzcols, newnonzcols, row->nnonzcols);

   SCIPfreeBufferArray(scip, &newnonzcols);

   assert(row->nnonzcols == 0 || row->nonzcols != NULL);

   return SCIP_OKAY;
}

/* --------------------------------------------------------------------------------------------------------------------
 * callback methods of separator
 * -------------------------------------------------------------------------------------------------------------------- */

/** copy method for separator plugins (called when SCIP copies plugins) */
static
SCIP_DECL_SEPACOPY(sepaCopyZerohalf)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(sepa != NULL);
   assert(strcmp(SCIPsepaGetName(sepa), SEPA_NAME) == 0);

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludeSepaZerohalf(scip) );

   return SCIP_OKAY;
}

/** destructor of separator to free user data (called when SCIP is exiting) */
static
SCIP_DECL_SEPAFREE(sepaFreeZerohalf)
{
   SCIP_SEPADATA* sepadata;

   assert(strcmp(SCIPsepaGetName(sepa), SEPA_NAME) == 0);

   /* free separator data */
   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   SCIPfreeBlockMemory(scip, &sepadata);
   SCIPsepaSetData(sepa, NULL);

   return SCIP_OKAY;
}

/** LP solution separation method of separator */
static
SCIP_DECL_SEPAEXECLP(sepaExeclpZerohalf)
{
   int i;
   int k;
   SCIP_SEPADATA* sepadata;
   MOD2_MATRIX mod2matrix;
   MOD2_ROW** nonzrows;

   /* TODO: check something? that there are fractional integer variables?
    * that scip is not stopped? something at all ?
    * also add asserts
    */
   sepadata = SCIPsepaGetData(sepa);
   SCIP_CALL( SCIPaggrRowCreate(scip, &sepadata->aggrrow) );
   sepadata->ncuts = 0;
   sepadata->infeasible = FALSE;

   SCIP_CALL( buildMod2Matrix(scip, &mod2matrix) );

   SCIPdebugMsg(scip, "built mod2 matrix (%i rows, %i cols)\n", mod2matrix.nrows, mod2matrix.ncols);

   SCIPallocBufferArray(scip, &nonzrows, mod2matrix.nrows);

   for( k = 0; k < 100; ++k ) /* TODO: what is this magic 10? define a macro */
   {
      sepadata->nreductions = 0;
      SCIP_CALL( mod2matrixPreprocessRows(scip, &mod2matrix, sepa, sepadata) );

      if( sepadata->infeasible )
      {
         *result = SCIP_CUTOFF;
         goto TERMINATE;
      }

      SCIPdebugMsg(scip, "preprocessed rows (%i rows, %i cols, %i cuts) \n", mod2matrix.nrows, mod2matrix.ncols,
                   sepadata->ncuts);

      if( mod2matrix.nrows == 0 )
         break;

      SCIP_CALL( mod2matrixPreprocessColumns(scip, &mod2matrix, sepadata) );

      SCIPdebugMsg(scip, "preprocessed columns (%i rows, %i cols)\n", mod2matrix.nrows, mod2matrix.ncols);

      if( mod2matrix.ncols == 0 )
         break;

      SCIPsortPtr((void**) mod2matrix.rows, compareRowSlack, mod2matrix.nrows);
      /* apply Prop5 */ /* TODO: this should be in another function, just like the preprocess stuff */
      for( i = 0; i < mod2matrix.nrows; ++i )
      {
         int j;
         MOD2_COL* col = NULL;
         MOD2_ROW* row = mod2matrix.rows[i];

         if( SCIPisPositive(scip, row->slack) || row->nnonzcols == 0 )
            continue;

         for( j = 0; j < row->nnonzcols; ++j )
         {
            if( col == NULL || row->nonzcols[j]->solval > col->solval )
            {
               col = row->nonzcols[j];
            }
         }

         if( col != NULL )
         {
            int colentries;
            int nnonzrows;

            ++sepadata->nreductions;

            nnonzrows = 0;
            colentries = SCIPhashtableGetNEntries(col->nonzrows);

            for( j = 0; j < colentries; ++j )
            {
               MOD2_ROW* colrow = (MOD2_ROW*) SCIPhashtableGetEntry(col->nonzrows, j);
               if( colrow != NULL && colrow != row )
                  nonzrows[nnonzrows++] = colrow;
            }

            for( j = 0; j < nnonzrows; ++j )
            {
               SCIP_CALL( mod2rowAddRow(scip, &mod2matrix, nonzrows[j], row) );
            }

            row->slack = col->solval;
            mod2matrixRemoveCol(scip, &mod2matrix, col);
         }
      }

      SCIPdebugMsg(scip, "applied proposition five (%i rows, %i cols)\n", mod2matrix.nrows, mod2matrix.ncols);

      if( sepadata->nreductions == 0 || mod2matrix.ncols == 0 )
      {
         SCIPdebugMsg(scip, "no change, stopping (%i rows, %i cols)\n", mod2matrix.nrows, mod2matrix.ncols);
         break;
      }
   }

   SCIPfreeBufferArray(scip, &nonzrows);

   for( i = 0; i < mod2matrix.nrows; ++i )
   {
      MOD2_ROW* row = mod2matrix.rows[i];

      if( SCIPisGE(scip, row->slack, 1.0) )
         break;

      if( row->rhs == 0 || row->slack > MAXSLACK )
         continue;

      SCIP_CALL( generateZerohalfCut(scip, &mod2matrix, sepa, sepadata, row) );

      if( sepadata->infeasible )
      {
         *result = SCIP_CUTOFF;
         goto TERMINATE;
      }
   }

   SCIPdebugMsg(scip, "total number of cuts found: %i\n", sepadata->ncuts);
   if( sepadata->ncuts > 0  )
      *result = SCIP_SEPARATED;

TERMINATE:
   SCIPaggrRowFree(scip, &sepadata->aggrrow);

   destroyMod2Matrix(scip, &mod2matrix);

   return SCIP_OKAY;
}

/** creates the zerohalf separator and includes it in SCIP */
SCIP_RETCODE SCIPincludeSepaZerohalf(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPADATA*        sepadata;
   SCIP_SEPA* sepa;

   /* create zerohalf separator data */
   SCIP_CALL(SCIPallocBlockMemory(scip, &sepadata));

   /* include separator */
   SCIP_CALL( SCIPincludeSepaBasic(scip, &sepa, SEPA_NAME, SEPA_DESC, SEPA_PRIORITY, SEPA_FREQ, SEPA_MAXBOUNDDIST,
                                   SEPA_USESSUBSCIP, SEPA_DELAY, sepaExeclpZerohalf, NULL, sepadata) );

   assert(sepa != NULL);

   /* set non-NULL pointers to callback methods */
   SCIP_CALL( SCIPsetSepaCopy(scip, sepa, sepaCopyZerohalf) );
   SCIP_CALL( SCIPsetSepaFree(scip, sepa, sepaFreeZerohalf) );

   return SCIP_OKAY;
}
