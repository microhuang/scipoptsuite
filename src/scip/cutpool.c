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

/**@file   cutpool.c
 * @brief  methods and datastructures for storing cuts in a cut pool
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "cutpool.h"
#include "sort.h"


/** datastructure for cuts in a cut pool */
struct Cut
{
   ROW*             row;                /**< LP row of this cut */
   int              age;                /**< age of the cut: number of successive times, the cut was not violated */
   int              processedlp;        /**< last LP, where this cut was processed */
   int              pos;                /**< position of cut in the cuts array of the cut pool */
};
typedef struct Cut CUT;

/** storage for pooled cuts */
struct Cutpool
{
   HASHTABLE*       hashtable;          /**< hash table to identify already stored cuts */
   CUT**            cuts;               /**< stored cuts of the pool */
   int              cutssize;           /**< size of cuts array */
   int              ncuts;              /**< number of cuts stored in the pool */
   int              agelimit;           /**< maximum age a cut can reach before it is deleted from the pool */
   int              processedlp;        /**< last LP that has been processed */
   int              firstunprocessed;   /**< first cut that has not been processed in the last LP */
   int              ncalls;             /**< number of times, the cutpool was separated */
   int              ncutsfound;         /**< total number of cuts that were separated from the pool */
   int              maxncuts;           /**< maximal number of cuts stored in the pool at the same time */
};


/*
 * Hash functions
 */

/** gets the hash key of a cut */
static
DECL_HASHGETKEY(hashGetKeyCut)
{
   CUT* cut;

   cut = (CUT*)elem;
   assert(cut != NULL);
   assert(cut->row != NULL);

   /* the key of a cut is the row */
   return cut->row;
}

/** returns TRUE iff both cuts are identical */
static
DECL_HASHKEYEQ(hashKeyEqCut)
{
   /* Warning: The comparison of real values is made against default epsilon.
    *          This is ugly, but we have no settings at hand.
    */

   int i;

   ROW* row1;
   ROW* row2;

   row1 = (ROW*)key1;
   row2 = (ROW*)key2;
   assert(row1 != NULL);
   assert(row2 != NULL);

   /* sort the column indices of a row */
   SCIProwSort(row1);
   SCIProwSort(row2);
   assert(row1->sorted);
   assert(row1->validminmaxidx);
   assert(row2->sorted);
   assert(row2->validminmaxidx);

   /* compare the trivial characteristics of the rows */
   if( row1->len != row2->len
      || row1->minidx != row2->minidx
      || row1->maxidx != row2->maxidx
      || row1->nummaxval != row2->nummaxval
      || ABS(row1->lhs - row2->lhs) > SCIP_DEFAULT_EPSILON
      || ABS(row1->rhs - row2->rhs) > SCIP_DEFAULT_EPSILON
      || ABS(row1->sqrnorm - row2->sqrnorm) > SCIP_DEFAULT_SUMEPSILON
      || ABS(row1->maxval - row2->maxval) > SCIP_DEFAULT_EPSILON
       )
      return FALSE;

   /* compare the columns of the rows */
   for( i = 0; i < row1->len; ++i )
   {
      if( row1->cols[i] != row2->cols[i] )
         return FALSE;
   }

   /* compare the coefficients of the rows */
   for( i = 0; i < row1->len; ++i )
   {
      if( ABS(row1->vals[i] - row2->vals[i]) > SCIP_DEFAULT_EPSILON )
         return FALSE;
   }

   return TRUE;
}

static
DECL_HASHKEYVAL(hashKeyValCut)
{
   ROW* row;
   unsigned int keyval;

   row = (ROW*)key;
   assert(row != NULL);

   keyval =
      + (row->nummaxval << 29)
      + (row->len << 22)
      + (row->minidx << 11)
      + row->maxidx;

   return keyval;
}



/*
 * dynamic memory arrays
 */

/** resizes cuts array to be able to store at least num entries */
static
RETCODE cutpoolEnsureCutsMem(
   CUTPOOL*         cutpool,            /**< cut pool */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimal number of slots in array */
   )
{
   assert(cutpool != NULL);
   assert(set != NULL);

   if( num > cutpool->cutssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&cutpool->cuts, newsize) );
      cutpool->cutssize = newsize;
   }
   assert(num <= cutpool->cutssize);

   return SCIP_OKAY;
}



/*
 * Cut methods
 */

/** creates a cut and captures the row */
static
RETCODE cutCreate(
   CUT**            cut,                /**< pointer to store the cut */
   MEMHDR*          memhdr,             /**< block memory */
   ROW*             row                 /**< row this cut represents */
   )
{
   assert(cut != NULL);
   assert(memhdr != NULL);
   assert(row != NULL);

   /* allocate cut memory */
   ALLOC_OKAY( allocBlockMemory(memhdr, cut) );
   (*cut)->row = row;
   (*cut)->age = 0;
   (*cut)->processedlp = -1;
   (*cut)->pos = -1;

   /* capture row */
   SCIProwCapture(row);

   return SCIP_OKAY;
}

/** frees a cut and releases the row */
static
RETCODE cutFree(
   CUT**            cut,                /**< pointer to store the cut */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   assert(cut != NULL);
   assert(*cut != NULL);
   assert((*cut)->row != NULL);
   assert(memhdr != NULL);
   
   /* release row */
   CHECK_OKAY( SCIProwRelease(&(*cut)->row, memhdr, set, lp) );

   /* free cut memory */
   freeBlockMemory(memhdr, cut);

   return SCIP_OKAY;
}



/*
 * Cutpool methods
 */

/** creates cut pool */
RETCODE SCIPcutpoolCreate(
   CUTPOOL**        cutpool,            /**< pointer to store cut pool */
   int              agelimit            /**< maximum age a cut can reach before it is deleted from the pool */
   )
{
   assert(cutpool != NULL);
   assert(agelimit >= 0);

   ALLOC_OKAY( allocMemory(cutpool) );

   CHECK_OKAY( SCIPhashtableCreate(&(*cutpool)->hashtable, SCIP_HASHSIZE_CUTPOOLS,
                  hashGetKeyCut, hashKeyEqCut, hashKeyValCut) );

   (*cutpool)->cuts = NULL;
   (*cutpool)->cutssize = 0;
   (*cutpool)->ncuts = 0;
   (*cutpool)->agelimit = agelimit;
   (*cutpool)->processedlp = -1;
   (*cutpool)->firstunprocessed = 0;
   (*cutpool)->ncalls = 0;
   (*cutpool)->ncutsfound = 0;
   (*cutpool)->maxncuts = 0;

   return SCIP_OKAY;
}

/** frees cut pool */
RETCODE SCIPcutpoolFree(
   CUTPOOL**        cutpool,            /**< pointer to store cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(cutpool != NULL);
   assert(*cutpool != NULL);

   /* free hash table */
   SCIPhashtableFree(&(*cutpool)->hashtable, memhdr);

   /* free cuts */
   for( i = 0; i < (*cutpool)->ncuts; ++i )
   {
      CHECK_OKAY( cutFree(&(*cutpool)->cuts[i], memhdr, set, lp) );
   }
   freeMemoryArrayNull(&(*cutpool)->cuts);
   
   freeMemory(cutpool);

   return SCIP_OKAY;
}

/** if not already existing, adds row to cut pool and captures it */
RETCODE SCIPcutpoolAddRow(
   CUTPOOL*         cutpool,            /**< cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   ROW*             row                 /**< cutting plane to add */
   )
{
   assert(cutpool != NULL);
   assert(row != NULL);

   /* check in hash table, if cut already exists in the pool */
   if( SCIPhashtableRetrieve(cutpool->hashtable, (void*)row) == NULL )
   {
      CHECK_OKAY( SCIPcutpoolAddNewRow(cutpool, memhdr, set, row) );
   }

   return SCIP_OKAY;
}

/** adds row to cut pool and captures it; doesn't check for multiple cuts */
RETCODE SCIPcutpoolAddNewRow(
   CUTPOOL*         cutpool,            /**< cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   ROW*             row                 /**< cutting plane to add */
   )
{
   CUT* cut;

   assert(cutpool != NULL);
   assert(row != NULL);

   /* check, if row is modifiable */
   if( row->modifiable )
   {
      errorMessage("cannot store a modifiable row in a cut pool");
      return SCIP_INVALIDDATA;
   }

   /* create the cut */
   CHECK_OKAY( cutCreate(&cut, memhdr, row) );
   cut->pos = cutpool->ncuts;

   /* add cut to the pool */
   CHECK_OKAY( cutpoolEnsureCutsMem(cutpool, set, cutpool->ncuts+1) );
   cutpool->cuts[cutpool->ncuts] = cut;
   cutpool->ncuts++;
   cutpool->maxncuts = MAX(cutpool->maxncuts, cutpool->ncuts);

   /* insert cut in the hash table */
   CHECK_OKAY( SCIPhashtableInsert(cutpool->hashtable, memhdr, (void*)cut) );

   /* lock the row */
   CHECK_OKAY( SCIProwLock(row) );

   return SCIP_OKAY;
}

/** removes the cut from the cut pool */
static
RETCODE cutpoolDelCut(
   CUTPOOL*         cutpool,            /**< cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   LP*              lp,                 /**< actual LP data */
   CUT*             cut                 /**< cut to remove */
   )
{
   int pos;

   assert(cutpool != NULL);
   assert(cutpool->firstunprocessed <= cutpool->ncuts);
   assert(memhdr != NULL);
   assert(stat != NULL);
   assert(cutpool->processedlp <= stat->nlp);
   assert(cut != NULL);
   assert(cut->row != NULL);

   pos = cut->pos;
   assert(0 <= pos && pos < cutpool->ncuts);
   assert(cutpool->cuts[pos] == cut);

   /* unlock the row */
   CHECK_OKAY( SCIProwUnlock(cut->row) );

   /* remove the cut from the hash table */
   CHECK_OKAY( SCIPhashtableRemove(cutpool->hashtable, memhdr, (void*)cut) );

   /* free the cut */
   CHECK_OKAY( cutFree(&cutpool->cuts[pos], memhdr, set, lp) );
   
   /* move the last cut of the pool to the free position */
   if( pos < cutpool->ncuts-1 )
   {
      cutpool->cuts[pos] = cutpool->cuts[cutpool->ncuts-1];
      cutpool->cuts[pos]->pos = pos;
      assert(cutpool->cuts[pos]->processedlp <= stat->nlp);
      if( cutpool->cuts[pos]->processedlp < stat->nlp )
         cutpool->firstunprocessed = MIN(cutpool->firstunprocessed, pos);
   }
   else
      cutpool->firstunprocessed = MIN(cutpool->firstunprocessed, cutpool->ncuts-1);

   cutpool->ncuts--;

   return SCIP_OKAY;
}

/** removes the LP row from the cut pool */
RETCODE SCIPcutpoolDelRow(
   CUTPOOL*         cutpool,            /**< cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   LP*              lp,                 /**< actual LP data */
   ROW*             row                 /**< row to remove */
   )
{
   CUT* cut;

   assert(cutpool != NULL);
   assert(row != NULL);

   /* find the cut in hash table */
   cut = (CUT*)SCIPhashtableRetrieve(cutpool->hashtable, (void*)row);
   if( cut == NULL )
   {
      char s[255];
      sprintf(s, "row <%s> is not existing in cutpool %p", SCIProwGetName(row), cutpool);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   CHECK_OKAY( cutpoolDelCut(cutpool, memhdr, set, stat, lp, cut) );

   return SCIP_OKAY;
}


/** separates cuts of the cut pool */
RETCODE SCIPcutpoolSeparate(
   CUTPOOL*         cutpool,            /**< cut pool */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   LP*              lp,                 /**< actual LP data */
   SEPA*            sepa,               /**< separation storage */
   Bool             root,               /**< are we at the root node? */
   RESULT*          result              /**< pointer to store the result of the separation call */
   )
{
   CUT* cut;
   Bool found;
   int oldncutsfound;
   int c;

   assert(cutpool != NULL);
   assert(stat != NULL);
   assert(cutpool->processedlp <= stat->nlp);
   assert(cutpool->firstunprocessed <= cutpool->ncuts);
   assert(result != NULL);

   if( cutpool->processedlp < stat->nlp )
      cutpool->firstunprocessed = 0;
   if( cutpool->firstunprocessed == cutpool->ncuts )
   {
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   *result = SCIP_DIDNOTFIND;
   cutpool->ncalls++;
   found = FALSE;

   debugMessage("separating cut pool %p with %d cuts, beginning with cut %d\n",
      cutpool, cutpool->ncuts, cutpool->firstunprocessed);

   /* remember the current total number of found cuts */
   oldncutsfound = SCIPsepaGetNCutsFound(sepa);

   /* process all unprocessed cuts in the pool */
   for( c = cutpool->firstunprocessed; c < cutpool->ncuts; ++c )
   {
      cut = cutpool->cuts[c];
      assert(cut != NULL);
      assert(cut->processedlp <= stat->nlp);
      assert(cut->pos == c);

      if( cut->processedlp < stat->nlp )
      {
         ROW* row;

         cut->processedlp = stat->nlp;
         row = cut->row;

         debugMessage("separating cut <%s> from the cut pool\n", SCIProwGetName(row));
         
         if( !SCIProwIsInLP(row) )
         {         
            Real feasibility;
            
            feasibility = SCIProwGetLPFeasibility(row, stat);
            debugMessage("  cut feasibility = %g\n", feasibility);
            if( !SCIPsetIsFeasible(set, feasibility) )
            {
               /* insert cut in separation storage */
               CHECK_OKAY( SCIPsepaAddCut(sepa, memhdr, set, lp, row,
                              -feasibility/SCIProwGetNorm(row)/(SCIProwGetNNonz(row)+1), root ) );
               found = TRUE;
            }
            else
            {
               cut->age++;
               if( cut->age > cutpool->agelimit )
               {
                  CHECK_OKAY( cutpoolDelCut(cutpool, memhdr, set, stat, lp, cut) );
               }
            }
         }
      }
   }
   
   cutpool->processedlp = stat->nlp;
   cutpool->firstunprocessed = cutpool->ncuts;

   /* update the number of found cuts */
   cutpool->ncutsfound += SCIPsepaGetNCutsFound(sepa) - oldncutsfound;

   if( found )
      *result = SCIP_SEPARATED;

   return SCIP_OKAY;
}

/** get number of cuts in the cut pool */
int SCIPcutpoolGetNCuts(
   CUTPOOL*         cutpool             /**< cut pool */
   )
{
   assert(cutpool != NULL);

   return cutpool->ncuts;
}

/** get number of times, the cut pool was separated */
int SCIPcutpoolGetNCalls(
   CUTPOOL*         cutpool             /**< cut pool */
   )
{
   assert(cutpool != NULL);

   return cutpool->ncalls;
}

/** get total number of cuts that were separated from the cut pool */
int SCIPcutpoolGetNCutsFound(
   CUTPOOL*         cutpool             /**< cut pool */
   )
{
   assert(cutpool != NULL);

   return cutpool->ncutsfound;
}

/** get maximum number of cuts that were stored in the cut pool at the same time */
int SCIPcutpoolGetMaxNCuts(
   CUTPOOL*         cutpool             /**< cut pool */
   )
{
   assert(cutpool != NULL);

   return cutpool->maxncuts;
}

