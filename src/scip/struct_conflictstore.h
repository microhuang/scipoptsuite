/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   struct_sepastore.h
 * @brief  datastructures for storing conflicts
 * @author Jakob Witzig
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_STRUCT_CONFLICTSTORE_H__
#define __SCIP_STRUCT_CONFLICTSTORE_H__


#include "scip/def.h"
#include "scip/type_conflictstore.h"

#ifdef __cplusplus
extern "C" {
#endif

/** storage for conflicts */
struct SCIP_ConflictStore
{
   SCIP_EVENTHDLR*       eventhdlr;          /**< event handler to catch improving solutions */
   SCIP_CONS**           conflicts;          /**< array with conflicts */
   SCIP_QUEUE*           dualrays;           /**< queue of dual rays */
   SCIP_Real*            primalbounds;       /**< array of primal bounds valid at the time the corresponding bound exceeding
                                               *   conflict was found (-infinity if the conflict as based on an infeasible LP) */
   SCIP_QUEUE*           slotqueue;          /**< queue of empty slots in the conflicts and primalbounds array */
   SCIP_QUEUE*           orderqueue;         /**< queue of conflict pointer preserving the order of generation */
   SCIP_Real             avgswitchlength;    /**< average length of switched paths */
   SCIP_Longint          lastnodenum;        /**< number of the last seen node */
   SCIP_Longint          ncleanups;          /**< number of storage cleanups */
   int                   conflictsize;       /**< size of conflict array (bounded by conflict->maxpoolsize) */
   int                   nconflicts;         /**< number of stored conflicts */
   int                   ncbconflicts;       /**< number of conflicts depending on cutoff bound */
   int                   nconflictsfound;    /**< total number of conflicts found so far */
   int                   cleanupfreq;        /**< frequency to cleanup the storage if the storage is not full */
   int                   nswitches;          /**< number of path switches */
   int                   maxstoresize;       /**< maximal size of the storage */

   // statistics, only temp
   int*                  dualrayinitsize;
   int*                  dualraysize;
   int*                  nconflictsets;
   int*                  nclauses;
   int                   maxsize;
};

#ifdef __cplusplus
}
#endif

#endif
