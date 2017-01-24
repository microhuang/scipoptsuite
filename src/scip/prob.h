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

/**@file   prob.h
 * @brief  internal methods for storing and manipulating the main problem
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_PROB_H__
#define __SCIP_PROB_H__


#include "scip/def.h"
#include "blockmemshell/memory.h"
#include "scip/type_retcode.h"
#include "scip/type_set.h"
#include "scip/type_stat.h"
#include "scip/type_event.h"
#include "scip/type_lp.h"
#include "scip/type_var.h"
#include "scip/type_implics.h"
#include "scip/type_prob.h"
#include "scip/type_primal.h"
#include "scip/type_tree.h"
#include "scip/type_reopt.h"
#include "scip/type_branch.h"
#include "scip/type_cons.h"
#include "scip/type_conflictstore.h"

#include "scip/struct_prob.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * problem creation
 */

/** creates problem data structure by copying the source problem; 
 *  If the problem type requires the use of variable pricers, these pricers should be activated with calls
 *  to SCIPactivatePricer(). These pricers are automatically deactivated, when the problem is freed.
 */
SCIP_RETCODE SCIPprobCopy(
   SCIP_PROB**           prob,               /**< pointer to problem data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   const char*           name,               /**< problem name */
   SCIP*                 sourcescip,         /**< source SCIP data structure */
   SCIP_PROB*            sourceprob,         /**< source problem structure */
   SCIP_HASHMAP*         varmap,             /**< a hashmap to store the mapping of source variables corresponding
                                              *   target variables, or NULL */
   SCIP_HASHMAP*         consmap,            /**< a hashmap to store the mapping of source constraints to the corresponding
                                              *   target constraints, or NULL */
   SCIP_Bool             global              /**< create a global or a local copy? */
   );

/** creates problem data structure
 *  If the problem type requires the use of variable pricers, these pricers should be activated with calls
 *  to SCIPactivatePricer(). These pricers are automatically deactivated, when the problem is freed.
 */
extern
SCIP_RETCODE SCIPprobCreate(
   SCIP_PROB**           prob,               /**< pointer to problem data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   const char*           name,               /**< problem name */
   SCIP_DECL_PROBDELORIG ((*probdelorig)),   /**< frees user data of original problem */
   SCIP_DECL_PROBTRANS   ((*probtrans)),     /**< creates user data of transformed problem by transforming original user data */
   SCIP_DECL_PROBDELTRANS((*probdeltrans)),  /**< frees user data of transformed problem */
   SCIP_DECL_PROBINITSOL ((*probinitsol)),   /**< solving process initialization method of transformed data */
   SCIP_DECL_PROBEXITSOL ((*probexitsol)),   /**< solving process deinitialization method of transformed data */
   SCIP_DECL_PROBCOPY    ((*probcopy)),      /**< copies user data if you want to copy it to a subscip, or NULL */
   SCIP_PROBDATA*        probdata,           /**< user problem data set by the reader */
   SCIP_Bool             transformed         /**< is this the transformed problem? */
   );

/** sets callback to free user data of original problem */
extern
void SCIPprobSetDelorig(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBDELORIG ((*probdelorig))    /**< frees user data of original problem */
   );

/** sets callback to create user data of transformed problem by transforming original user data */
extern
void SCIPprobSetTrans(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBTRANS   ((*probtrans))      /**< creates user data of transformed problem by transforming original user data */
   );

/** sets callback to free user data of transformed problem */
extern
void SCIPprobSetDeltrans(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBDELTRANS((*probdeltrans))   /**< frees user data of transformed problem */
   );

/** sets solving process initialization callback of transformed data */
extern
void SCIPprobSetInitsol(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBINITSOL ((*probinitsol))    /**< solving process initialization callback of transformed data */
   );

/** sets solving process deinitialization callback of transformed data */
extern
void SCIPprobSetExitsol(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBEXITSOL ((*probexitsol))    /**< solving process deinitialization callback of transformed data */
   );

/** sets callback to copy user data to copy it to a subscip, or NULL */
extern
void SCIPprobSetCopy(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_DECL_PROBCOPY    ((*probcopy))       /**< copies user data if you want to copy it to a subscip, or NULL */
   );

/** frees problem data structure */
extern
SCIP_RETCODE SCIPprobFree(
   SCIP_PROB**           prob,               /**< pointer to problem data structure */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler */
   BMS_BLKMEM*           blkmem,             /**< block memory buffer */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_LP*              lp                  /**< current LP data (or NULL, if it's the original problem) */
   );

/** transform problem data into normalized form */
extern
SCIP_RETCODE SCIPprobTransform(
   SCIP_PROB*            source,             /**< problem to transform */
   BMS_BLKMEM*           blkmem,             /**< block memory buffer */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTFILTER*     eventfilter,        /**< event filter for global (not variable dependent) events */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CONFLICTSTORE*   conflictstore,      /**< conflict store */
   SCIP_PROB**           target              /**< pointer to target problem data structure */
   );

/** resets the global and local bounds of original variables in original problem to their original values */
extern
SCIP_RETCODE SCIPprobResetBounds(
   SCIP_PROB*            prob,               /**< original problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat                /**< problem statistics */
   );

/** (Re)Sort the variables, which appear in the four categories (binary, integer, implicit, continuous) after presolve
 *  with respect to their original index (within their categories). Adjust the problem index afterwards which is
 *  supposed to reflect the position in the variable array. This additional (re)sorting is supposed to get more robust
 *  against the order presolving fixed variables. (We also reobtain a possible block structure induced by the user
 *  model)
 */
extern
void SCIPprobResortVars(
   SCIP_PROB*            prob                /**< problem data */
   );


/*
 * problem modification
 */

/** sets user problem data */
extern
void SCIPprobSetData(
   SCIP_PROB*            prob,               /**< problem */
   SCIP_PROBDATA*        probdata            /**< user problem data to use */
   );

/** adds variable's name to the namespace */
extern
SCIP_RETCODE SCIPprobAddVarName(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_VAR*             var                 /**< variable */
   );

/** removes variable's name from the namespace */
extern
SCIP_RETCODE SCIPprobRemoveVarName(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_VAR*             var                 /**< variable */
   );

/** adds variable to the problem and captures it */
extern
SCIP_RETCODE SCIPprobAddVar(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory buffers */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTFILTER*     eventfilter,        /**< event filter for global (not variable dependent) events */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             var                 /**< variable to add */
   );

/** marks variable to be removed from the problem; however, the variable is NOT removed from the constraints */
extern
SCIP_RETCODE SCIPprobDelVar(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_Bool*            deleted             /**< pointer to store whether marking variable to be deleted was successful */
   );

/** actually removes the deleted variables from the problem and releases them */
extern
SCIP_RETCODE SCIPprobPerformVarDeletions(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_LP*              lp,                 /**< current LP data (may be NULL) */
   SCIP_BRANCHCAND*      branchcand          /**< branching candidate storage */
   );

/** changes the type of a variable in the problem */
extern
SCIP_RETCODE SCIPprobChgVarType(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_VAR*             var,                /**< variable to add */
   SCIP_VARTYPE          vartype             /**< new type of variable */
   );

/** informs problem, that the given loose problem variable changed its status */
extern
SCIP_RETCODE SCIPprobVarChangedStatus(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_VAR*             var                 /**< problem variable */
   );

/** adds constraint's name to the namespace */
extern
SCIP_RETCODE SCIPprobAddConsName(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_CONS*            cons                /**< constraint */
   );

/** remove constraint's name from the namespace */
extern
SCIP_RETCODE SCIPprobRemoveConsName(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_CONS*            cons                /**< constraint */
   );

/** adds constraint to the problem and captures it;
 *  a local constraint is automatically upgraded into a global constraint
 */
extern
SCIP_RETCODE SCIPprobAddCons(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_CONS*            cons                /**< constraint to add */
   );

/** releases and removes constraint from the problem; if the user has not captured the constraint for his own use, the
 *  constraint may be invalid after the call
 */
extern
SCIP_RETCODE SCIPprobDelCons(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_CONS*            cons                /**< constraint to remove */
   );

/** remembers the current number of constraints in the problem's internal data structure
 *  - resets maximum number of constraints to current number of constraints
 *  - remembers current number of constraints as starting number of constraints
 */
extern
void SCIPprobMarkNConss(
   SCIP_PROB*            prob                /**< problem data */
   );

/** sets objective sense: minimization or maximization */
extern
void SCIPprobSetObjsense(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_OBJSENSE         objsense            /**< new objective sense */
   );

/** adds value to objective offset */
extern
void SCIPprobAddObjoffset(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_Real             addval              /**< value to add to objective offset */
   );

/** sets the dual bound on objective function */
extern
void SCIPprobSetDualbound(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_Real             dualbound           /**< external dual bound */
   );

/** sets limit on objective function, such that only solutions better than this limit are accepted */
extern
void SCIPprobSetObjlim(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_Real             objlim              /**< external objective limit */
   );

/** informs the problem, that its objective value is always integral in every feasible solution */
extern
void SCIPprobSetObjIntegral(
   SCIP_PROB*            prob                /**< problem data */
   );

/** sets integral objective value flag, if all variables with non-zero objective values are integral and have 
 *  integral objective value and also updates the cutoff bound if primal solution is already known
 */
extern
SCIP_RETCODE SCIPprobCheckObjIntegral(
   SCIP_PROB*            transprob,          /**< tranformed problem data */
   SCIP_PROB*            origprob,           /**< original problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   );

/** if possible, scales objective function such that it is integral with gcd = 1 */
extern
SCIP_RETCODE SCIPprobScaleObj(
   SCIP_PROB*            transprob,          /**< tranformed problem data */
   SCIP_PROB*            origprob,           /**< original problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   );

/** remembers the current solution as root solution in the problem variables */
extern
void SCIPprobStoreRootSol(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< SCIP statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_Bool             roothaslp           /**< is the root solution from LP? */
   );

/** remembers the best solution w.r.t. root reduced cost propagation as root solution in the problem variables */
extern
void SCIPprobUpdateBestRootSol(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp                  /**< current LP data */
   );

/** informs problem, that the presolving process was finished, and updates all internal data structures */
extern
SCIP_RETCODE SCIPprobExitPresolve(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** initializes problem for branch and bound process */
extern
SCIP_RETCODE SCIPprobInitSolve(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** deinitializes problem after branch and bound process, and converts all COLUMN variables back into LOOSE variables */
extern
SCIP_RETCODE SCIPprobExitSolve(
   SCIP_PROB*            prob,               /**< problem data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_Bool             restart             /**< was this exit solve call triggered by a restart? */
   );




/*
 * problem information
 */

/** sets problem name */
extern
SCIP_RETCODE SCIPprobSetName(
   SCIP_PROB*            prob,               /**< problem data */
   const char*           name                /**< name to be set */
   );

/** returns the number of implicit binary variables, meaning variable of vartype != SCIP_VARTYPE_BINARY and !=
 *  SCIP_VARTYPE_CONTINUOUS but with global bounds [0,1]
 *
 *  @note this number needs to be computed, because it cannot be update like the othe counters for binary and interger
 *        variables, each time the variable type changes(, we would need to update this counter each time a global bound
 *        changes), even at the end of presolving this cannot be computed, because some variable can change to an
 *        implicit binary status
 */
extern
int SCIPprobGetNImplBinVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** returns the number of variables with non-zero objective coefficient */
extern
int SCIPprobGetNObjVars(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** update the number of variables with non-zero objective coefficient */
extern
void SCIPprobUpdateNObjVars(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             oldobj,             /**< old objective value for variable */
   SCIP_Real             newobj              /**< new objective value for variable */
   );

/** update the dual bound if its better as the current one */
extern
void SCIPprobUpdateDualbound(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_Real             newbound            /**< new dual bound for the node (if it's tighter than the old one) */
   );

/** invalidates the dual bound */
extern
void SCIPprobInvalidateDualbound(
   SCIP_PROB*            prob                /**< problem data */
   );

/** returns the external value of the given internal objective value */
extern
SCIP_Real SCIPprobExternObjval(
   SCIP_PROB*            transprob,          /**< tranformed problem data */
   SCIP_PROB*            origprob,           /**< original problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             objval              /**< internal objective value */
   );

/** returns the internal value of the given external objective value */
extern
SCIP_Real SCIPprobInternObjval(
   SCIP_PROB*            transprob,          /**< tranformed problem data */
   SCIP_PROB*            origprob,           /**< original problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             objval              /**< external objective value */
   );

/** returns variable of the problem with given name */
extern
SCIP_VAR* SCIPprobFindVar(
   SCIP_PROB*            prob,               /**< problem data */
   const char*           name                /**< name of variable to find */
   );

/** returns constraint of the problem with given name */
extern
SCIP_CONS* SCIPprobFindCons(
   SCIP_PROB*            prob,               /**< problem data */
   const char*           name                /**< name of variable to find */
   );

/** displays current pseudo solution */
extern
void SCIPprobPrintPseudoSol(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_MESSAGEHDLR*     messagehdlr         /**< message handler */
   );

/** outputs problem statistics */
extern
void SCIPprobPrintStatistics(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler */
   FILE*                 file                /**< output file (or NULL for standard output) */
   );


#ifndef NDEBUG

/* In debug mode, the following methods are implemented as function calls to ensure
 * type validity.
 */

/** is the problem permuted */
extern
SCIP_Bool SCIPprobIsPermuted(
   SCIP_PROB*            prob
   );

/** mark the problem as permuted */
extern
void SCIPprobMarkPermuted(
   SCIP_PROB*            prob
   );

/** is the problem data transformed */
extern
SCIP_Bool SCIPprobIsTransformed(
   SCIP_PROB*            prob                /**< problem data */
   );

/** returns whether the objective value is known to be integral in every feasible solution */
extern
SCIP_Bool SCIPprobIsObjIntegral(
   SCIP_PROB*            prob                /**< problem data */
   );

/** returns TRUE iff all columns, i.e. every variable with non-empty column w.r.t. all ever created rows, are present
 *  in the LP, and FALSE, if there are additional already existing columns, that may be added to the LP in pricing
 */
extern
SCIP_Bool SCIPprobAllColsInLP(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp                  /**< current LP data */
   );

/** gets limit on objective function in external space */
extern
SCIP_Real SCIPprobGetObjlim(
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** gets user problem data */
extern
SCIP_PROBDATA* SCIPprobGetData(
   SCIP_PROB*            prob                /**< problem */
   );

/** gets problem name */
extern
const char* SCIPprobGetName(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of problem variables */
extern
int SCIPprobGetNVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of binary problem variables */
extern
int SCIPprobGetNBinVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of integer problem variables */
extern
int SCIPprobGetNIntVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of implicit integer problem variables */
extern
int SCIPprobGetNImplVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of continuous problem variables */
extern
int SCIPprobGetNContVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets problem variables */
extern
SCIP_VAR** SCIPprobGetVars(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets number of problem constraints */
extern
int SCIPprobGetNConss(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets the objective offset */
extern
SCIP_Real SCIPprobGetObjoffset(
   SCIP_PROB*            prob                /**< problem data */
   );

/** gets the objective scalar */
extern
SCIP_Real SCIPprobGetObjscale(
   SCIP_PROB*            prob                /**< problem data */
   );

/** is constraint compression enabled for this problem? */
extern
SCIP_Bool SCIPprobIsConsCompressionEnabled(
   SCIP_PROB*            prob                /**< problem data */
   );

/** enable problem compression, i.e., constraints can reduce memory size by removing fixed variables during creation */
extern
void SCIPprobEnableConsCompression(
   SCIP_PROB*            prob                /**< problem data */
   );

#else

/* In optimized mode, the methods are implemented as defines to reduce the number of function calls and
 * speed up the algorithms.
 */

#define SCIPprobIsPermuted(prob)        ((prob)->permuted)
#define SCIPprobMarkPermuted(prob)      ((prob)->permuted = TRUE)
#define SCIPprobIsTransformed(prob)     ((prob)->transformed)
#define SCIPprobIsObjIntegral(prob)     ((prob)->objisintegral)
#define SCIPprobAllColsInLP(prob,set,lp) (SCIPlpGetNCols(lp) == (prob)->ncolvars && (set)->nactivepricers == 0)
#define SCIPprobGetObjlim(prob,set)     \
   ((prob)->objlim >= SCIP_INVALID ? (SCIP_Real)((prob)->objsense) * SCIPsetInfinity(set) : (prob)->objlim)
#define SCIPprobGetData(prob)           ((prob)->probdata)
#define SCIPprobGetName(prob)           ((prob)->name)
#define SCIPprobGetName(prob)           ((prob)->name)
#define SCIPprobGetNVars(prob)          ((prob)->nvars)
#define SCIPprobGetNBinVars(prob)       ((prob)->nbinvars)
#define SCIPprobGetNIntVars(prob)       ((prob)->nintvars)
#define SCIPprobGetNImplVars(prob)      ((prob)->nimplvars)
#define SCIPprobGetNContVars(prob)      ((prob)->ncontvars)
#define SCIPprobGetVars(prob)           ((prob)->vars)
#define SCIPprobGetNConss(prob)         ((prob)->nconss)
#define SCIPprobGetObjoffset(prob)      ((prob)->objoffset)
#define SCIPprobGetObjscale(prob)       ((prob)->objscale)
#define SCIPprobIsConsCompressionEnabled(prob)  ((prob)->conscompression)
#define SCIPprobEnableConsCompression(prob)  ((prob)->conscompression = TRUE)
#endif


#ifdef __cplusplus
}
#endif

#endif
