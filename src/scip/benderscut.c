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

/**@file   benderscut.c
 * @brief  methods for Benders' decomposition cuts
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/def.h"
#include "scip/set.h"
#include "scip/clock.h"
#include "scip/paramset.h"
#include "scip/scip.h"
#include "scip/benderscut.h"
#include "scip/reopt.h"
#include "scip/pub_message.h"
#include "scip/pub_misc.h"

#include "scip/struct_benderscut.h"

#define BENDERSCUT_ARRAYSIZE        10    /**< the initial size of the added constraints/cuts arrays */

/** compares two Benders' cuts w. r. to their delay positions and their priority */
SCIP_DECL_SORTPTRCOMP(SCIPbenderscutComp)
{  /*lint --e{715}*/
   SCIP_BENDERSCUT* benderscut1 = (SCIP_BENDERSCUT*)elem1;
   SCIP_BENDERSCUT* benderscut2 = (SCIP_BENDERSCUT*)elem2;

   assert(benderscut1 != NULL);
   assert(benderscut2 != NULL);

   return benderscut2->priority - benderscut1->priority; /* prefer higher priorities */
}

/** comparison method for sorting Benders' cuts w.r.t. to their name */
SCIP_DECL_SORTPTRCOMP(SCIPbenderscutCompName)
{
   return strcmp(SCIPbenderscutGetName((SCIP_BENDERSCUT*)elem1), SCIPbenderscutGetName((SCIP_BENDERSCUT*)elem2));
}

/** method to call, when the priority of a compression was changed */
static
SCIP_DECL_PARAMCHGD(paramChgdBenderscutPriority)
{  /*lint --e{715}*/
   SCIP_PARAMDATA* paramdata;

   paramdata = SCIPparamGetData(param);
   assert(paramdata != NULL);

   /* use SCIPsetBenderscutPriority() to mark the compressions unsorted */
   SCIP_CALL( SCIPsetBenderscutPriority(scip, (SCIP_BENDERSCUT*)paramdata, SCIPparamGetInt(param)) ); /*lint !e740*/

   return SCIP_OKAY;
}

SCIP_RETCODE SCIPbenderscutCopyInclude(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set                 /**< SCIP_SET of SCIP to copy to */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);
   assert(set->scip != NULL);

   if( benderscut->benderscutcopy != NULL )
   {
      SCIPsetDebugMsg(set, "including benderscut %s in subscip %p\n", SCIPbenderscutGetName(benderscut), (void*)set->scip);
      SCIP_CALL( benderscut->benderscutcopy(set->scip, benderscut) );
   }

   return SCIP_OKAY;
}

/** creates a Benders' decomposition cuts */
SCIP_RETCODE SCIPbenderscutCreate(
   SCIP_BENDERSCUT**     benderscut,         /**< pointer to Benders' decomposition cuts data structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler */
   BMS_BLKMEM*           blkmem,             /**< block memory for parameter settings */
   const char*           name,               /**< name of Benders' decomposition cuts */
   const char*           desc,               /**< description of Benders' decomposition cuts */
   int                   priority,           /**< priority of the Benders' decomposition cuts */
   SCIP_Bool             islpcut,            /**< indicates whether the cut is generated from the LP solution */
   SCIP_DECL_BENDERSCUTCOPY((*benderscutcopy)),/**< copy method of Benders' decomposition cuts or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSCUTFREE((*benderscutfree)),/**< destructor of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINIT((*benderscutinit)),/**< initialize Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXIT((*benderscutexit)),/**< deinitialize Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINITSOL((*benderscutinitsol)),/**< solving process initialization method of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXITSOL((*benderscutexitsol)),/**< solving process deinitialization method of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXEC((*benderscutexec)),/**< execution method of Benders' decomposition cuts */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< Benders' decomposition cuts data */
   )
{
   char paramname[SCIP_MAXSTRLEN];
   char paramdesc[SCIP_MAXSTRLEN];

   assert(benderscut != NULL);
   assert(name != NULL);
   assert(desc != NULL);
   assert(benderscutexec != NULL);

   SCIP_ALLOC( BMSallocMemory(benderscut) );
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*benderscut)->name, name, strlen(name)+1) );
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*benderscut)->desc, desc, strlen(desc)+1) );
   (*benderscut)->priority = priority;
   (*benderscut)->islpcut = islpcut;
   (*benderscut)->benderscutcopy = benderscutcopy;
   (*benderscut)->benderscutfree = benderscutfree;
   (*benderscut)->benderscutinit = benderscutinit;
   (*benderscut)->benderscutexit = benderscutexit;
   (*benderscut)->benderscutinitsol = benderscutinitsol;
   (*benderscut)->benderscutexitsol = benderscutexitsol;
   (*benderscut)->benderscutexec = benderscutexec;
   (*benderscut)->benderscutdata = benderscutdata;
   SCIP_CALL( SCIPclockCreate(&(*benderscut)->setuptime, SCIP_CLOCKTYPE_DEFAULT) );
   SCIP_CALL( SCIPclockCreate(&(*benderscut)->benderscutclock, SCIP_CLOCKTYPE_DEFAULT) );
   (*benderscut)->ncalls = 0;
   (*benderscut)->nfound = 0;
   (*benderscut)->initialized = FALSE;
   (*benderscut)->addedcons = NULL;
   (*benderscut)->addedcuts = NULL;
   (*benderscut)->addedconssize = BENDERSCUT_ARRAYSIZE;
   (*benderscut)->addedcutssize = BENDERSCUT_ARRAYSIZE;
   (*benderscut)->naddedcons = 0;
   (*benderscut)->naddedcuts = 0;

   /* add parameters */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/benderscut/%s/priority", name);
   (void) SCIPsnprintf(paramdesc, SCIP_MAXSTRLEN, "priority of Benders' cut <%s>", name);
   SCIP_CALL( SCIPsetAddIntParam(set, messagehdlr, blkmem, paramname, paramdesc,
                  &(*benderscut)->priority, TRUE, priority, INT_MIN/4, INT_MAX/4,
                  paramChgdBenderscutPriority, (SCIP_PARAMDATA*)(*benderscut)) ); /*lint !e740*/
   return SCIP_OKAY;
}

/** calls destructor and frees memory of Benders' decomposition cuts */
SCIP_RETCODE SCIPbenderscutFree(
   SCIP_BENDERSCUT**     benderscut,         /**< pointer to Benders' decomposition cuts data structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benderscut != NULL);
   assert(*benderscut != NULL);
   assert(!(*benderscut)->initialized);
   assert(set != NULL);

   /* call destructor of Benders' decomposition cuts */
   if( (*benderscut)->benderscutfree != NULL )
   {
      SCIP_CALL( (*benderscut)->benderscutfree(set->scip, *benderscut) );
   }

   SCIPclockFree(&(*benderscut)->benderscutclock);
   SCIPclockFree(&(*benderscut)->setuptime);
   BMSfreeMemoryArray(&(*benderscut)->name);
   BMSfreeMemoryArray(&(*benderscut)->desc);
   BMSfreeMemory(benderscut);

   return SCIP_OKAY;
}

/** initializes Benders' decomposition cuts */
SCIP_RETCODE SCIPbenderscutInit(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);

   if( benderscut->initialized )
   {
      SCIPerrorMessage("Benders' decomposition cuts <%s> already initialized\n", benderscut->name);
      return SCIP_INVALIDCALL;
   }

   if( set->misc_resetstat )
   {
      SCIPclockReset(benderscut->setuptime);
      SCIPclockReset(benderscut->benderscutclock);

      benderscut->ncalls = 0;
      benderscut->nfound = 0;
   }

   /* allocating memory for the added constraint/cut arrays */
   SCIP_ALLOC( BMSallocMemoryArray(&benderscut->addedcons, benderscut->addedconssize) );
   SCIP_ALLOC( BMSallocMemoryArray(&benderscut->addedcuts, benderscut->addedcutssize) );

   if( benderscut->benderscutinit != NULL )
   {
      /* start timing */
      SCIPclockStart(benderscut->setuptime, set);

      SCIP_CALL( benderscut->benderscutinit(set->scip, benderscut) );

      /* stop timing */
      SCIPclockStop(benderscut->setuptime, set);
   }
   benderscut->initialized = TRUE;

   return SCIP_OKAY;
}

/** calls exit method of Benders' decomposition cuts */
SCIP_RETCODE SCIPbenderscutExit(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);

   if( !benderscut->initialized )
   {
      SCIPerrorMessage("Benders' decomposition cuts <%s> not initialized\n", benderscut->name);
      return SCIP_INVALIDCALL;
   }

   BMSfreeMemoryArray(&benderscut->addedcuts);
   BMSfreeMemoryArray(&benderscut->addedcons);
   benderscut->addedconssize = BENDERSCUT_ARRAYSIZE;
   benderscut->addedcutssize = BENDERSCUT_ARRAYSIZE;
   benderscut->naddedcons = 0;
   benderscut->naddedcuts = 0;

   if( benderscut->benderscutexit != NULL )
   {
      /* start timing */
      SCIPclockStart(benderscut->setuptime, set);

      SCIP_CALL( benderscut->benderscutexit(set->scip, benderscut) );

      /* stop timing */
      SCIPclockStop(benderscut->setuptime, set);
   }
   benderscut->initialized = FALSE;

   return SCIP_OKAY;
}

/** informs Benders' cut that the branch and bound process is being started */
SCIP_RETCODE SCIPbenderscutInitsol(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cut */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);

   /* call solving process initialization method of Benders' decomposition cut */
   if( benderscut->benderscutinitsol != NULL )
   {
      /* start timing */
      SCIPclockStart(benderscut->setuptime, set);

      SCIP_CALL( benderscut->benderscutinitsol(set->scip, benderscut) );

      /* stop timing */
      SCIPclockStop(benderscut->setuptime, set);
   }

   return SCIP_OKAY;
}

/** informs Benders' decomposition that the branch and bound process data is being freed */
SCIP_RETCODE SCIPbenderscutExitsol(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);

   /* call solving process deinitialization method of Benders' decompositioni cut */
   if( benderscut->benderscutexitsol != NULL )
   {
      /* start timing */
      SCIPclockStart(benderscut->setuptime, set);

      SCIP_CALL( benderscut->benderscutexitsol(set->scip, benderscut) );

      /* stop timing */
      SCIPclockStop(benderscut->setuptime, set);
   }

   return SCIP_OKAY;
}
/** calls execution method of Benders' decomposition cuts */
SCIP_RETCODE SCIPbenderscutExec(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,         /**< the number of the subproblem for which the cut is generated */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(benderscut != NULL);
   assert(benderscut->benderscutexec != NULL);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   SCIPsetDebugMsg(set, "executing Benders' decomposition cuts <%s>\n", benderscut->name);

   /* start timing */
   SCIPclockStart(benderscut->benderscutclock, set);

   /* call external method */
   SCIP_CALL( benderscut->benderscutexec(set->scip, benders, benderscut, sol, probnumber, type, result) );

   /* stop timing */
   SCIPclockStop(benderscut->benderscutclock, set);

   /* evaluate result */
   if( *result != SCIP_CONSADDED
      && *result != SCIP_FEASIBLE
      && *result != SCIP_SEPARATED )
   {
      SCIPerrorMessage("execution method of Benders' decomposition cuts <%s> returned invalid result <%d>\n",
         benderscut->name, *result);
      return SCIP_INVALIDRESULT;
   }

   benderscut->ncalls++;

   if( *result == SCIP_CONSADDED )
      benderscut->nfound++;

   return SCIP_OKAY;
}

/** gets user data of Benders' decomposition cuts */
SCIP_BENDERSCUTDATA* SCIPbenderscutGetData(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->benderscutdata;
}

/** sets user data of Benders' decomposition cuts; user has to free old data in advance! */
void SCIPbenderscutSetData(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< new Benders' decomposition cuts user data */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutdata = benderscutdata;
}

/* new callback setter methods */

/** sets copy callback of Benders' decomposition cuts */
void SCIPbenderscutSetCopy(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTCOPY((*benderscutcopy))/**< copy callback of Benders' decomposition cuts or NULL if you don't want to copy your plugin into sub-SCIPs */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutcopy = benderscutcopy;
}

/** sets destructor callback of Benders' decomposition cuts */
void SCIPbenderscutSetFree(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTFREE((*benderscutfree))/**< destructor of Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutfree = benderscutfree;
}

/** sets initialization callback of Benders' decomposition cuts */
void SCIPbenderscutSetInit(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINIT((*benderscutinit))/**< initialize Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutinit = benderscutinit;
}

/** sets deinitialization callback of Benders' decomposition cuts */
void SCIPbenderscutSetExit(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXIT((*benderscutexit))/**< deinitialize Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutexit = benderscutexit;
}

/** sets solving process initialization callback of Benders' decomposition cuts */
void SCIPbenderscutSetInitsol(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINITSOL((*benderscutinitsol))/**< solving process initialization callback of Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutinitsol = benderscutinitsol;
}

/** sets solving process deinitialization callback of Benders' decomposition cuts */
void SCIPbenderscutSetExitsol(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXITSOL((*benderscutexitsol))/**< solving process deinitialization callback of Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->benderscutexitsol = benderscutexitsol;
}

/** gets name of Benders' decomposition cuts */
const char* SCIPbenderscutGetName(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->name;
}

/** gets description of Benders' decomposition cuts */
const char* SCIPbenderscutGetDesc(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->desc;
}

/** gets priority of Benders' decomposition cuts */
int SCIPbenderscutGetPriority(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->priority;
}

/** sets priority of Benders' decomposition cuts */
void SCIPbenderscutSetPriority(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   int                   priority            /**< new priority of the Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   benderscut->priority = priority;
}

/** gets the number of times, the heuristic was called and tried to find a solution */
SCIP_Longint SCIPbenderscutGetNCalls(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->ncalls;
}

/** gets the number of benders cuts found by this benderscutession */
SCIP_Longint SCIPbenderscutGetNFound(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->nfound;
}

/** is Benders' decomposition cuts initialized? */
SCIP_Bool SCIPbenderscutIsInitialized(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return benderscut->initialized;
}

/** gets time in seconds used in this heuristic for setting up for next stages */
SCIP_Real SCIPbenderscutGetSetupTime(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return SCIPclockGetTime(benderscut->setuptime);
}

/** gets time in seconds used in this heuristic */
SCIP_Real SCIPbenderscutGetTime(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cuts */
   )
{
   assert(benderscut != NULL);

   return SCIPclockGetTime(benderscut->benderscutclock);
}

/** adds the generated constraint to the Benders cut storage */
SCIP_RETCODE SCIPbenderscutStoreCons(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONS*            cons                /**< the constraint to be added to the Benders' cut storage */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);
   assert(cons != NULL);

   /* ensuring the required memory is available for the added constraints array */
   if( benderscut->addedconssize < benderscut->naddedcons + 1 )
   {
      benderscut->addedconssize = SCIPsetCalcMemGrowSize(set, benderscut->naddedcons + 1);
      SCIP_ALLOC( BMSreallocMemoryArray(&benderscut->addedcons, benderscut->addedconssize) );
   }
   assert(benderscut->addedconssize >= benderscut->naddedcons + 1);

   /* adding the constraint to the Benders' cut storage */
   benderscut->addedcons[benderscut->naddedcons] = cons;
   benderscut->naddedcons++;

   return SCIP_OKAY;
}

/* adds the generated cuts to the Benders' cut storage */
SCIP_RETCODE SCIPbenderscutStoreCut(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_ROW*             cut                 /**< the cut to be added to the Benders' cut storage */
   )
{
   assert(benderscut != NULL);
   assert(set != NULL);
   assert(cut != NULL);

   /* ensuring the required memory is available for the added cuts array */
   if( benderscut->addedcutssize < benderscut->naddedcuts + 1 )
   {
      benderscut->addedcutssize = SCIPsetCalcMemGrowSize(set, benderscut->naddedcuts + 1);
      SCIP_ALLOC( BMSreallocMemoryArray(&benderscut->addedcuts, benderscut->addedcutssize) );
   }
   assert(benderscut->addedcutssize >= benderscut->naddedcuts + 1);

   /* adding the cuts to the Benders' cut storage */
   benderscut->addedcuts[benderscut->naddedcuts] = cut;
   benderscut->naddedcuts++;

   return SCIP_OKAY;
}

/** returns the constraints that have been added by the Benders' cut plugin */
SCIP_RETCODE SCIPbenderscutGetCons(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cut */
   SCIP_CONS***          addedcons,          /**< pointer to store the constraint array */
   int*                  naddedcons          /**< pointer to store the number of added constraints */
   )
{
   assert(benderscut != NULL);
   assert(addedcons != NULL);
   assert(naddedcons != NULL);

   (*addedcons) = benderscut->addedcons;
   (*naddedcons) = benderscut->naddedcons;

   return SCIP_OKAY;
}

/** returns the cuts that have been added by the Benders' cut plugin */
SCIP_RETCODE SCIPbenderscutGetCuts(
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cut */
   SCIP_ROW***           addedcuts,          /**< pointer to store the cuts array */
   int*                  naddedcuts          /**< pointer to store the number of added cut */
   )
{
   assert(benderscut != NULL);
   assert(addedcuts != NULL);
   assert(naddedcuts != NULL);

   (*addedcuts) = benderscut->addedcuts;
   (*naddedcuts) = benderscut->naddedcuts;

   return SCIP_OKAY;
}

/** returns the number of constraints that have been added by the Benders' cut plugin */
int SCIPbenderscutGetNAddedCons(
   SCIP_BENDERSCUT*      benderscut         /**< Benders' decomposition cut */
   )
{
   assert(benderscut != NULL);

   return benderscut->naddedcons;
}

/** returns the number of cuts that have been added by the Benders' cut plugin */
int SCIPbenderscutGetNAddedCuts(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cut */
   )
{
   assert(benderscut != NULL);

   return benderscut->naddedcuts;
}

/** returns whether the Benders' cut uses the LP information */
SCIP_Bool SCIPbenderscutIsLPCut(
   SCIP_BENDERSCUT*      benderscut          /**< Benders' decomposition cut */
   )
{
   assert(benderscut != NULL);

   return benderscut->islpcut;
}
