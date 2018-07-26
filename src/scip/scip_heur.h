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

/**@file   scip_heur.h
 * @ingroup PUBLICCOREAPI
 * @brief  public methods for primal heuristic plugins and divesets
 * @author Tobias Achterberg
 * @author Timo Berthold
 * @author Thorsten Koch
 * @author Alexander Martin
 * @author Marc Pfetsch
 * @author Kati Wolter
 * @author Gregor Hendel
 * @author Robert Lion Gottwald
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_SCIP_HEUR_H__
#define __SCIP_SCIP_HEUR_H__


#include "scip/def.h"
#include "scip/type_heur.h"
#include "scip/type_result.h"
#include "scip/type_retcode.h"
#include "scip/type_scip.h"
#include "scip/type_timing.h"
#include "scip/type_var.h"

/* In debug mode, we include the SCIP's structure in scip.c, such that no one can access
 * this structure except the interface methods in scip.c.
 * In optimized mode, the structure is included in scip.h, because some of the methods
 * are implemented as defines for performance reasons (e.g. the numerical comparisons).
 * Additionally, the internal "set.h" is included, such that the defines in set.h are
 * available in optimized mode.
 */
#ifdef NDEBUG
#include "scip/struct_scip.h"
#include "scip/struct_stat.h"
#include "scip/set.h"
#include "scip/tree.h"
#include "scip/misc.h"
#include "scip/var.h"
#include "scip/cons.h"
#include "scip/solve.h"
#include "scip/debug.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**@addtogroup PublicHeuristicMethods
 *
 * @{
 */

/** creates a primal heuristic and includes it in SCIP.
 *
 *  @note method has all heuristic callbacks as arguments and is thus changed every time a new
 *        callback is added in future releases; consider using SCIPincludeHeurBasic() and setter functions
 *        if you seek for a method which is less likely to change in future releases
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. otherwise a suitable error code is passed. see \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPincludeHeur(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           name,               /**< name of primal heuristic */
   const char*           desc,               /**< description of primal heuristic */
   char                  dispchar,           /**< display character of primal heuristic */
   int                   priority,           /**< priority of the primal heuristic */
   int                   freq,               /**< frequency for calling primal heuristic */
   int                   freqofs,            /**< frequency offset for calling primal heuristic */
   int                   maxdepth,           /**< maximal depth level to call heuristic at (-1: no limit) */
   SCIP_HEURTIMING       timingmask,         /**< positions in the node solving loop where heuristic should be executed;
                                              *   see definition of SCIP_HEURTIMING for possible values */
   SCIP_Bool             usessubscip,        /**< does the heuristic use a secondary SCIP instance? */
   SCIP_DECL_HEURCOPY    ((*heurcopy)),      /**< copy method of primal heuristic or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_HEURFREE    ((*heurfree)),      /**< destructor of primal heuristic */
   SCIP_DECL_HEURINIT    ((*heurinit)),      /**< initialize primal heuristic */
   SCIP_DECL_HEUREXIT    ((*heurexit)),      /**< deinitialize primal heuristic */
   SCIP_DECL_HEURINITSOL ((*heurinitsol)),   /**< solving process initialization method of primal heuristic */
   SCIP_DECL_HEUREXITSOL ((*heurexitsol)),   /**< solving process deinitialization method of primal heuristic */
   SCIP_DECL_HEUREXEC    ((*heurexec)),      /**< execution method of primal heuristic */
   SCIP_HEURDATA*        heurdata            /**< primal heuristic data */
   );

/** creates a primal heuristic and includes it in SCIP with its most fundamental callbacks.
 *  All non-fundamental (or optional) callbacks
 *  as, e. g., init and exit callbacks, will be set to NULL.
 *  Optional callbacks can be set via specific setter functions, see SCIPsetHeurCopy(), SCIPsetHeurFree(),
 *  SCIPsetHeurInit(), SCIPsetHeurExit(), SCIPsetHeurInitsol(), and SCIPsetHeurExitsol()
 *
*  @note if you want to set all callbacks with a single method call, consider using SCIPincludeHeur() instead
 */
EXTERN
SCIP_RETCODE SCIPincludeHeurBasic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR**           heur,               /**< pointer to the heuristic */
   const char*           name,               /**< name of primal heuristic */
   const char*           desc,               /**< description of primal heuristic */
   char                  dispchar,           /**< display character of primal heuristic */
   int                   priority,           /**< priority of the primal heuristic */
   int                   freq,               /**< frequency for calling primal heuristic */
   int                   freqofs,            /**< frequency offset for calling primal heuristic */
   int                   maxdepth,           /**< maximal depth level to call heuristic at (-1: no limit) */
   SCIP_HEURTIMING       timingmask,         /**< positions in the node solving loop where heuristic should be executed;
                                              *   see definition of SCIP_HEURTIMING for possible values */
   SCIP_Bool             usessubscip,        /**< does the heuristic use a secondary SCIP instance? */
   SCIP_DECL_HEUREXEC    ((*heurexec)),      /**< execution method of primal heuristic */
   SCIP_HEURDATA*        heurdata            /**< primal heuristic data */
   );

/** sets copy method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurCopy(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEURCOPY    ((*heurcopy))       /**< copy method of primal heuristic or NULL if you don't want to copy your plugin into sub-SCIPs */
   );

/** sets destructor method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEURFREE    ((*heurfree))       /**< destructor of primal heuristic */
   );

/** sets initialization method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurInit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEURINIT    ((*heurinit))       /**< initialize primal heuristic */
   );

/** sets deinitialization method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurExit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEUREXIT    ((*heurexit))       /**< deinitialize primal heuristic */
   );

/** sets solving process initialization method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurInitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEURINITSOL ((*heurinitsol))    /**< solving process initialization method of primal heuristic */
   );

/** sets solving process deinitialization method of primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurExitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_DECL_HEUREXITSOL ((*heurexitsol))    /**< solving process deinitialization method of primal heuristic */
   );

/** returns the primal heuristic of the given name, or NULL if not existing */
EXTERN
SCIP_HEUR* SCIPfindHeur(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           name                /**< name of primal heuristic */
   );

/** returns the array of currently available primal heuristics */
EXTERN
SCIP_HEUR** SCIPgetHeurs(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** returns the number of currently available primal heuristics */
EXTERN
int SCIPgetNHeurs(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** sets the priority of a primal heuristic */
EXTERN
SCIP_RETCODE SCIPsetHeurPriority(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< primal heuristic */
   int                   priority            /**< new priority of the primal heuristic */
   );

/* @} */

/**@addtogroup PublicDivesetMethods
 *
 * @{
 */

/** create a diving set associated with a primal heuristic. The primal heuristic needs to be included
 *  before this method can be called. The diveset is installed in the array of divesets of the heuristic
 *  and can be retrieved later by accessing SCIPheurGetDivesets()
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. otherwise a suitable error code is passed. see \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPcreateDiveset(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIVESET**        diveset,            /**< pointer to created diving heuristic settings, or NULL if not needed */
   SCIP_HEUR*            heur,               /**< primal heuristic to which the diveset belongs */
   const char*           name,               /**< name for the diveset, or NULL if the name of the heuristic should be used */
   SCIP_Real             minreldepth,        /**< minimal relative depth to start diving */
   SCIP_Real             maxreldepth,        /**< maximal relative depth to start diving */
   SCIP_Real             maxlpiterquot,      /**< maximal fraction of diving LP iterations compared to node LP iterations */
   SCIP_Real             maxdiveubquot,      /**< maximal quotient (curlowerbound - lowerbound)/(cutoffbound - lowerbound)
                                              *   where diving is performed (0.0: no limit) */
   SCIP_Real             maxdiveavgquot,     /**< maximal quotient (curlowerbound - lowerbound)/(avglowerbound - lowerbound)
                                              *   where diving is performed (0.0: no limit) */
   SCIP_Real             maxdiveubquotnosol, /**< maximal UBQUOT when no solution was found yet (0.0: no limit) */
   SCIP_Real             maxdiveavgquotnosol,/**< maximal AVGQUOT when no solution was found yet (0.0: no limit) */
   SCIP_Real             lpresolvedomchgquot,/**< percentage of immediate domain changes during probing to trigger LP resolve */
   int                   lpsolvefreq,        /**< LP solve frequency for (0: only if enough domain reductions are found by propagation)*/
   int                   maxlpiterofs,       /**< additional number of allowed LP iterations */
   unsigned int          initialseed,        /**< initial seed for random number generation */
   SCIP_Bool             backtrack,          /**< use one level of backtracking if infeasibility is encountered? */
   SCIP_Bool             onlylpbranchcands,  /**< should only LP branching candidates be considered instead of the slower but
                                              *   more general constraint handler diving variable selection? */
   SCIP_Bool             ispublic,           /**< is this dive set publicly available (ie., can be used by other primal heuristics?) */
   SCIP_Bool             specificsos1score,  /**< should SOS1 variables be scored by the diving heuristics specific score function;
                                              *   otherwise use the score function of the SOS1 constraint handler */
   SCIP_DECL_DIVESETGETSCORE((*divesetgetscore)), /**< method for candidate score and rounding direction */
   SCIP_DECL_DIVESETAVAILABLE((*divesetavailable)) /**< callback to check availability of dive set at the current stage, or NULL if always available */
   );

/** check specific preconditions for diving, e.g., if an incumbent solution is available */
EXTERN
SCIP_RETCODE SCIPisDivesetAvailable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIVESET*         diveset,            /**< diving heuristic settings */
   SCIP_Bool*            available           /**< pointer to store if the diving can run at the current solving stage */
   );

/* @} */

#ifdef __cplusplus
}
#endif

#endif
