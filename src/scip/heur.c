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

/**@file   heur.c
 * @brief  methods and datastructures for primal heuristics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "heur.h"


/** primal heuristics data */
struct Heur
{
   char*            name;               /**< name of primal heuristic */
   char*            desc;               /**< description of primal heuristic */
   char             dispchar;           /**< display character of primal heuristic */
   int              priority;           /**< priority of the primal heuristic */
   int              freq;               /**< frequency for calling primal heuristic */
   DECL_HEURFREE    ((*heurfree));      /**< destructor of primal heuristic */
   DECL_HEURINIT    ((*heurinit));      /**< initialise primal heuristic */
   DECL_HEUREXIT    ((*heurexit));      /**< deinitialise primal heuristic */
   DECL_HEUREXEC    ((*heurexec));      /**< execution method of primal heuristic */
   HEURDATA*        heurdata;           /**< primal heuristics local data */
   int              ncalls;             /**< number of times, this heuristic was called */
   int              nsolsfound;         /**< number of feasible primal solutions found so far by this heuristic */
   unsigned int     pseudonodes:1;      /**< call heuristic at nodes where only a pseudo solution exist? */
   unsigned int     initialized:1;      /**< is primal heuristic initialized? */
};



/** creates a primal heuristic */
RETCODE SCIPheurCreate(
   HEUR**           heur,               /**< pointer to primal heuristic data structure */
   const char*      name,               /**< name of primal heuristic */
   const char*      desc,               /**< description of primal heuristic */
   char             dispchar,           /**< display character of primal heuristic */
   int              priority,           /**< priority of the primal heuristic */
   int              freq,               /**< frequency for calling primal heuristic */
   Bool             pseudonodes,        /**< call heuristic at nodes where only a pseudo solution exist? */
   DECL_HEURFREE    ((*heurfree)),      /**< destructor of primal heuristic */
   DECL_HEURINIT    ((*heurinit)),      /**< initialise primal heuristic */
   DECL_HEUREXIT    ((*heurexit)),      /**< deinitialise primal heuristic */
   DECL_HEUREXEC    ((*heurexec)),      /**< execution method of primal heuristic */
   HEURDATA*        heurdata            /**< primal heuristic data */
   )
{
   assert(heur != NULL);
   assert(name != NULL);
   assert(desc != NULL);
   assert(freq >= -1);
   assert(heurexec != NULL);

   ALLOC_OKAY( allocMemory(heur) );
   ALLOC_OKAY( duplicateMemoryArray(&(*heur)->name, name, strlen(name)+1) );
   ALLOC_OKAY( duplicateMemoryArray(&(*heur)->desc, desc, strlen(desc)+1) );
   (*heur)->dispchar = dispchar;
   (*heur)->priority = priority;
   (*heur)->freq = freq;
   (*heur)->pseudonodes = pseudonodes;
   (*heur)->heurfree = heurfree;
   (*heur)->heurinit = heurinit;
   (*heur)->heurexit = heurexit;
   (*heur)->heurexec = heurexec;
   (*heur)->heurdata = heurdata;
   (*heur)->ncalls = 0;
   (*heur)->nsolsfound = 0;
   (*heur)->initialized = FALSE;

   return SCIP_OKAY;
}

/** calls destructor and frees memory of primal heuristic */
RETCODE SCIPheurFree(
   HEUR**           heur,               /**< pointer to primal heuristic data structure */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(heur != NULL);
   assert(*heur != NULL);
   assert(!(*heur)->initialized);
   assert(scip != NULL);

   /* call destructor of primal heuristic */
   if( (*heur)->heurfree != NULL )
   {
      CHECK_OKAY( (*heur)->heurfree(scip, *heur) );
   }

   freeMemoryArray(&(*heur)->name);
   freeMemoryArray(&(*heur)->desc);
   freeMemory(heur);

   return SCIP_OKAY;
}

/** initializes primal heuristic */
RETCODE SCIPheurInit(
   HEUR*            heur,               /**< primal heuristic */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(heur != NULL);
   assert(scip != NULL);

   if( heur->initialized )
   {
      char s[255];
      sprintf(s, "primal heuristic <%s> already initialized", heur->name);
      errorMessage(s);
      return SCIP_INVALIDCALL;
   }

   if( heur->heurinit != NULL )
   {
      CHECK_OKAY( heur->heurinit(scip, heur) );
      heur->ncalls = 0;
      heur->nsolsfound = 0;
   }
   heur->initialized = TRUE;

   return SCIP_OKAY;
}

/** calls exit method of primal heuristic */
RETCODE SCIPheurExit(
   HEUR*            heur,               /**< primal heuristic */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(heur != NULL);
   assert(scip != NULL);

   if( !heur->initialized )
   {
      char s[255];
      sprintf(s, "primal heuristic <%s> not initialized", heur->name);
      errorMessage(s);
      return SCIP_INVALIDCALL;
   }

   if( heur->heurexit != NULL )
   {
      CHECK_OKAY( heur->heurexit(scip, heur) );
   }
   heur->initialized = FALSE;

   return SCIP_OKAY;
}

/** calls execution method of primal heuristic */
RETCODE SCIPheurExec(
   HEUR*            heur,               /**< primal heuristic */
   const SET*       set,                /**< global SCIP settings */
   int              actdepth,           /**< depth of active node */
   Bool             actnodehaslp,       /**< is LP being processed in the active node? */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   Bool execute;

   assert(heur != NULL);
   assert(heur->heurexec != NULL);
   assert(heur->freq >= -1);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(actdepth >= 0);
   assert(result != NULL);

   if( heur->pseudonodes )
   {
      /* heuristic may be executed on every node: check, if the actual depth matches the execution frequency */
      execute = (actdepth == 0 && heur->freq == 0) || (heur->freq > 0 && actdepth % heur->freq == 0);
   }
   else
   {
      /* heuristic may only be executed on LP nodes: check, if the node is an LP node and a node matching the
       * execution frequency lies between the current node and the last LP node of the path
       */
      execute = actnodehaslp;
      execute &= (actdepth == 0 && heur->freq >= 0)
         || (heur->freq > 0 && (actdepth / heur->freq != (actdepth-set->lpsolvefreq) / heur->freq));
   }

   if( execute )
   {
      debugMessage("executing primal heuristic <%s>\n", heur->name);
      CHECK_OKAY( heur->heurexec(set->scip, heur, result) );
      if( *result != SCIP_FOUNDSOL
         && *result != SCIP_DIDNOTFIND
         && *result != SCIP_DIDNOTRUN )
      {
         char s[255];
         sprintf(s, "execution method of primal heuristic <%s> returned invalid result <%d>", 
            heur->name, *result);
         errorMessage(s);
         return SCIP_INVALIDRESULT;
      }
      if( *result != SCIP_DIDNOTRUN )
         heur->ncalls++;
   }
   else
      *result = SCIP_DIDNOTRUN;
   
   return SCIP_OKAY;
}


/** gets name of primal heuristic */
const char* SCIPheurGetName(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->name;
}

/** gets display character of primal heuristic */
char SCIPheurGetDispchar(
   HEUR*            heur                /**< primal heuristic */
   )
{
   if( heur == NULL )
      return '*';
   else
      return heur->dispchar;
}

/** gets user data of primal heuristic */
HEURDATA* SCIPheurGetData(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->heurdata;
}

/** sets user data of primal heuristic; user has to free old data in advance! */
void SCIPheurSetData(
   HEUR*            heur,               /**< primal heuristic */
   HEURDATA*        heurdata            /**< new primal heuristic user data */
   )
{
   assert(heur != NULL);

   heur->heurdata = heurdata;
}

/** gets frequency of primal heuristic */
int SCIPheurGetFreq(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->freq;
}

/** gets the number of times, the heuristic was called and tried to find a solution */
int SCIPheurGetNCalls(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->ncalls;
}

/** increases the number of primal feasible solutions found by this heuristic */
void SCIPheurIncNSolsFound(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   heur->nsolsfound++;
}

/** gets the number of primal feasible solutions found by this heuristic */
int SCIPheurGetNSolsFound(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->nsolsfound;
}

/** is primal heuristic initialized? */
Bool SCIPheurIsInitialized(
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->initialized;
}
