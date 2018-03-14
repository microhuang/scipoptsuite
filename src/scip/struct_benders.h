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

/**@file   struct_benders.h
 * @ingroup INTERNALAPI
 * @brief  data structures required for Benders' decomposition
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_STRUCT_BENDERS_H__
#define __SCIP_STRUCT_BENDERS_H__


#include "scip/def.h"
#include "scip/type_clock.h"
#include "scip/type_benders.h"
#include "scip/type_benderscut.h"

#ifdef __cplusplus
extern "C" {
#endif

/** variable benderss data */
struct SCIP_Benders
{
   char*                 name;               /**< name of Benders' decomposition */
   char*                 desc;               /**< description of Benders' decomposition */
   SCIP_DECL_BENDERSCOPY ((*benderscopy));   /**< copy method of benders or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSFREE ((*bendersfree));   /**< destructor of variable benders */
   SCIP_DECL_BENDERSINIT ((*bendersinit));   /**< initialize variable benders */
   SCIP_DECL_BENDERSEXIT ((*bendersexit));   /**< deinitialize variable benders */
   SCIP_DECL_BENDERSINITPRE((*bendersinitpre));/**< presolving initialization method for Benders' decomposition */
   SCIP_DECL_BENDERSEXITPRE((*bendersexitpre));/**< presolving deinitialization method for Benders' decomposition */
   SCIP_DECL_BENDERSINITSOL((*bendersinitsol));/**< solving process initialization method of variable benders */
   SCIP_DECL_BENDERSEXITSOL((*bendersexitsol));/**< solving process deinitialization method of variable benders */
   SCIP_DECL_BENDERSGETVAR((*bendersgetvar)); /**< returns the corresponding variable from the master or subproblem */
   SCIP_DECL_BENDERSPRESUBSOLVE((*benderspresubsolve));/**< called prior to the subproblem solving loop */
   SCIP_DECL_BENDERSCREATESUB((*benderscreatesub));/**< creates the Benders' decomposition subproblems */
   SCIP_DECL_BENDERSSOLVESUB((*benderssolvesub));/**< the solving method for the Benders' decomposition subproblems */
   SCIP_DECL_BENDERSPOSTSOLVE((*benderspostsolve));/**< called after the subproblems are solved. */
   SCIP_DECL_BENDERSFREESUB((*bendersfreesub));/**< the freeing method for the Benders' decomposition subproblems */
   SCIP_BENDERSDATA*     bendersdata;        /**< variable benderss local data */
   SCIP_CLOCK*           setuptime;          /**< time spend for setting up this benders for the next stages */
   SCIP_CLOCK*           bendersclock;       /**< benders execution time */
   int                   priority;           /**< priority of the Benders' decomposition */
   int                   ncalls;             /**< number of times, this benders was called */
   int                   ncutsfound;         /**< number of cuts found by the Benders' decomposition */
   int                   ntransferred;       /**< number of cuts transferred from sub SCIP to the master SCIP */
   SCIP_Bool             active;             /**< is the Benders' decomposition active? */
   SCIP_Bool             initialized;        /**< is Benders' decomposition initialized? */
   SCIP_Bool             cutlp;              /**< should Benders' cuts be generated for LP solutions? */
   SCIP_Bool             cutpseudo;          /**< should Benders' cuts be generated for pseudo solutions? */
   SCIP_Bool             cutrelax;           /**< should Benders' cuts be generated for relaxation solutions? */
   SCIP_Bool             shareauxvars;       /**< should this Benders' share the highest priority Benders' auxiliary vars */

   /* additional Benders' decomposition parameters */
   SCIP_Bool             transfercuts;       /**< Should Benders' cuts generated in LNS heuristics be transferred to the main SCIP instance? */
   SCIP_Bool             lnscheck;           /**< Should Benders' decomposition be used in LNS heuristics? */
   int                   lnsmaxdepth;        /**< The maximum depth at which the LNS check is performed */
   SCIP_Bool             cutsasconss;        /**< Should the transferred cuts be added as constraints? */
   int                   mipcheckfreq;       /**< the frequency that the MIP subproblem is checked for feasibility, -1 for always.*/
   SCIP_Real             subprobfrac;        /**< the fraction of subproblems that are solved in each iteration */

   /* information for heuristics */
   SCIP*                 sourcescip;         /**< the source scip from when the Benders' was copied */
   SCIP_Bool             iscopy;             /**< is the Benders' decomposition struct a copy */
   SCIP_HASHMAP*         mastervarsmap;      /**< hash map for the master variables from the subscip to the master */

   /* the subproblem information */
   SCIP**                subproblems;        /**< the Benders' decomposition subproblems */
   SCIP_VAR**            auxiliaryvars;      /**< the auxiliary variables for the Benders' optimality cuts */
   SCIP_Real*            subprobobjval;      /**< the objective value of the subproblem in the current iteration */
   SCIP_Real*            bestsubprobobjval;  /**< the best objective value of the subproblem */
   int                   naddedsubprobs;     /**< subproblems added to the Benders' decomposition data */
   int                   nsubproblems;       /**< number of subproblems */
   SCIP_Bool*            subprobislp;        /**< is the subproblem formulated as an LP? */
   int                   nlpsubprobs;        /**< the number of LP subproblems */
   SCIP_Bool             subprobscreated;    /**< have the subproblems been created for this Benders' decomposition.
                                                  This flag is used when retransforming the problem.*/
   SCIP_Bool*            mastervarscont;     /**< flag to indicate that the master problem variable have been converted
                                               to continuous variables. */
   SCIP_Bool*            subprobsetup;       /**< flag to indicate whether the subproblem has been set up. */
   int                   firstchecked;       /**< the subproblem index first checked in the current iteration */
   int                   lastchecked;        /**< the subproblem index last checked in the current iteration */

   /* Bender's cut information */
   SCIP_BENDERSCUT**     benderscuts;        /**< the available Benders' cut algorithms */
   int                   nbenderscuts;       /**< the number of Benders' cut algorithms */
   int                   benderscutssize;    /**< the size of the Benders' cuts algorithms array */
   SCIP_Bool             benderscutssorted;  /**< are the Benders' cuts algorithms sorted by priority */
   SCIP_Bool             benderscutsnamessorted;/**< are the Benders' cuts algorithms sorted by name */
};

/** parameters that are set to solve the subproblem. This will be changed from what the user inputs, so they are stored
 *  and reset after the solving loop. */
struct SCIP_SubprobParams
{
   int cons_linear_propfreq;
   int lp_disablecutoff;
   int lp_scaling;
   int prop_maxrounds;
   int prop_maxroundsroot;
   char lp_initalg;
   char lp_resolvealg;
   SCIP_Bool conflict_enable;
   SCIP_Bool misc_alwaysgetduals;
   SCIP_Bool misc_catchctrlc;
   SCIP_Bool misc_scaleobj;
};
typedef struct SCIP_SubprobParams SCIP_SUBPROBPARAMS;

#ifdef __cplusplus
}
#endif

#endif
