/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2004 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2004 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: set.c,v 1.119 2004/11/01 13:48:08 bzfpfend Exp $"

/**@file   set.c
 * @brief  methods for global SCIP settings
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <math.h>

#include "def.h"
#include "set.h"
#include "stat.h"
#include "misc.h"
#include "event.h"
#include "lp.h"
#include "paramset.h"
#include "scip.h"
#include "branch.h"
#include "conflict.h"
#include "cons.h"
#include "disp.h"
#include "heur.h"
#include "nodesel.h"
#include "presol.h"
#include "pricer.h"
#include "reader.h"
#include "sepa.h"
#include "prop.h"



/*
 * Default settings
 */


/* Branching */

#define SCIP_DEFAULT_BRANCH_SCOREFAC      0.167 /**< branching score factor to weigh downward and upward gain prediction */
#define SCIP_DEFAULT_BRANCH_PREFERBINARY  FALSE /**< should branching on binary variables be prefered? */


/* Conflict Analysis */

#define SCIP_DEFAULT_CONF_MAXVARSFAC       0.02 /**< maximal fraction of binary variables involved in a conflict clause */
#define SCIP_DEFAULT_CONF_MINMAXVARS         30 /**< minimal absolute maximum of variables involved in a conflict clause */
#define SCIP_DEFAULT_CONF_MAXLPLOOPS        100 /**< maximal number of LP resolving loops during conflict analysis */
#define SCIP_DEFAULT_CONF_FUIPLEVELS         -1 /**< number of depth levels up to which first UIP's are used in conflict
                                                 *   analysis (-1: use All-FirstUIP rule) */
#define SCIP_DEFAULT_CONF_INTERCLAUSES        1 /**< maximal number of intermediate conflict clauses generated in conflict
                                                 *   graph (-1: use every intermediate clause) */
#define SCIP_DEFAULT_CONF_RECONVCLAUSES    TRUE /**< should reconvergence clauses be created for UIPs of last depth level? */
#define SCIP_DEFAULT_CONF_USEPROP          TRUE /**< should propagation conflict analysis be used? */
#define SCIP_DEFAULT_CONF_USELP           FALSE /**< should infeasible LP conflict analysis be used? */
#define SCIP_DEFAULT_CONF_USESB           FALSE /**< should infeasible strong branching conflict analysis be used? */
#define SCIP_DEFAULT_CONF_USEPSEUDO        TRUE /**< should pseudo solution conflict analysis be used? */
#define SCIP_DEFAULT_CONF_REPROPAGATE      TRUE /**< should earlier nodes be repropagated in order to replace branching
                                                 *   decisions by deductions */


/* Constraints */

#define SCIP_DEFAULT_CONS_AGELIMIT          200 /**< maximum age an unnecessary constraint can reach before it is deleted
                                                 *   (-1: constraints are never deleted) */
#define SCIP_DEFAULT_CONS_OBSOLETEAGE       100 /**< age of a constraint after which it is marked obsolete
                                                 *   (-1: constraints are never marked obsolete) */


/* Display */

#define SCIP_DEFAULT_DISP_VERBLEVEL SCIP_VERBLEVEL_NORMAL /**< verbosity level of output */
#define SCIP_DEFAULT_DISP_WIDTH             139 /**< maximal number of characters in a node information line */
#define SCIP_DEFAULT_DISP_FREQ              100 /**< frequency for displaying node information lines */
#define SCIP_DEFAULT_DISP_HEADERFREQ         15 /**< frequency for displaying header lines (every n'th node info line) */
#define SCIP_DEFAULT_DISP_LPINFO          FALSE /**< should the LP solver display status messages? */


/* Limits */

#define SCIP_DEFAULT_LIMIT_TIME           1e+20 /**< maximal time in seconds to run */
#define SCIP_DEFAULT_LIMIT_MEMORY         1e+20 /**< maximal memory usage in MB */
#define SCIP_DEFAULT_LIMIT_GAP              0.0 /**< solving stops, if the gap is below the given value */
#define SCIP_DEFAULT_LIMIT_NODES           -1LL /**< maximal number of nodes to process (-1: no limit) */
#define SCIP_DEFAULT_LIMIT_SOL               -1 /**< solving stops, if given number of sols were found (-1: no limit) */
#define SCIP_DEFAULT_LIMIT_BESTSOL           -1 /**< solving stops, if given number of solution improvements were found
                                                 *   (-1: no limit) */
#define SCIP_DEFAULT_LIMIT_MAXSOL           100 /**< maximal number of solutions to store in the solution storage */


/* LP */

#define SCIP_DEFAULT_LP_SOLVEFREQ             1 /**< frequency for solving LP at the nodes; -1: never; 0: only root LP */
#define SCIP_DEFAULT_LP_SOLVEDEPTH           -1 /**< maximal depth for solving LPs (-1: no depth limit) */
#define SCIP_DEFAULT_LP_COLAGELIMIT          -1 /**< maximum age a dynamic column can reach before it is deleted from LP
                                                 *   (-1: don't delete columns due to aging) */
#define SCIP_DEFAULT_LP_ROWAGELIMIT          -1 /**< maximum age a dynamic row can reach before it is deleted from LP
                                                 *   (-1: don't delete rows due to aging) */
#define SCIP_DEFAULT_LP_CLEANUPCOLS       FALSE /**< should new non-basic columns be removed after LP solving? */
#define SCIP_DEFAULT_LP_CLEANUPROWS        TRUE /**< should new basic rows be removed after LP solving? */
#define SCIP_DEFAULT_LP_CHECKFEAS          TRUE /**< should LP solutions be checked to resolve LP at numerical troubles? */
#define SCIP_DEFAULT_LP_FASTMIP            TRUE /**< should FASTMIP setting of LP solver be used? */
#define SCIP_DEFAULT_LP_SCALING            TRUE /**< should scaling of LP solver be used? */
#define SCIP_DEFAULT_LP_PRESOLVING         TRUE /**< should presolving of LP solver be used? */


/* Memory */

#define SCIP_DEFAULT_MEM_SAVEFAC            0.8 /**< fraction of maximal mem usage when switching to memory saving mode */
#define SCIP_DEFAULT_MEM_ARRAYGROWFAC       1.2 /**< memory growing factor for dynamically allocated arrays */
#define SCIP_DEFAULT_MEM_TREEGROWFAC        2.0 /**< memory growing factor for tree array */
#define SCIP_DEFAULT_MEM_PATHGROWFAC        2.0 /**< memory growing factor for path array */
#define SCIP_DEFAULT_MEM_ARRAYGROWINIT        4 /**< initial size of dynamically allocated arrays */
#define SCIP_DEFAULT_MEM_TREEGROWINIT     65536 /**< initial size of tree array */
#define SCIP_DEFAULT_MEM_PATHGROWINIT       256 /**< initial size of path array */


/* Miscellaneous */

#define SCIP_DEFAULT_MISC_CATCHCTRLC       TRUE /**< should the CTRL-C interrupt be caught by SCIP? */
#define SCIP_DEFAULT_MISC_EXACTSOLVE      FALSE /**< should the problem be solved exactly (with proven dual bounds)? */


/* Presolving */

#define SCIP_DEFAULT_PRESOL_ABORTFAC      1e-04 /**< abort presolve, if l.t. frac of the problem was changed in last round */
#define SCIP_DEFAULT_PRESOL_MAXROUNDS        -1 /**< maximal number of presolving rounds (-1: unlimited) */
#define SCIP_DEFAULT_PRESOL_RESTARTBDCHGS   100 /**< number of root node bound changes triggering a restart with
                                                 *   preprocessing (-1: no restart, 0: restart only after complete root
                                                 *   node evaluation) */


/* Pricing */

#define SCIP_DEFAULT_PRICE_ABORTFAC         2.0 /**< pricing is aborted, if fac * price_maxvars pricing candidates were
                                                 *   found */
#define SCIP_DEFAULT_PRICE_MAXVARS          100 /**< maximal number of variables priced in per pricing round */
#define SCIP_DEFAULT_PRICE_MAXVARSROOT     2000 /**< maximal number of priced variables at the root node */


/* Propagating */

#define SCIP_DEFAULT_PROP_MAXROUNDS         100 /**< maximal number of propagation rounds per node (-1: unlimited) */
#define SCIP_DEFAULT_PROP_MAXROUNDSROOT    1000 /**< maximal number of propagation rounds in root node (-1: unlimited) */
#define SCIP_DEFAULT_PROP_REDCOSTFREQ         1 /**< frequency for reduced cost fixing (-1: never; 0: only root LP) */


/* Separation */

#define SCIP_DEFAULT_SEPA_MAXBOUNDDIST      0.2 /**< maximal relative distance from current node's dual bound to primal 
                                                 *   bound compared to best node's dual bound for applying separation
                                                 *   (0.0: only on current best node, 1.0: on all nodes) */
#define SCIP_DEFAULT_SEPA_MINEFFICACY      0.05 /**< minimal efficacy for a cut to enter the LP */
#define SCIP_DEFAULT_SEPA_MINEFFICACYROOT  0.01 /**< minimal efficacy for a cut to enter the LP in the root node */
#define SCIP_DEFAULT_SEPA_MINORTHO         0.50 /**< minimal orthogonality for a cut to enter the LP */
#define SCIP_DEFAULT_SEPA_MINORTHOROOT     0.50 /**< minimal orthogonality for a cut to enter the LP in the root node */
#define SCIP_DEFAULT_SEPA_ORTHOFAC         1.00 /**< factor to scale orthogonality of cut in score calculation */
#define SCIP_DEFAULT_SEPA_EFFICACYNORM      'e' /**< row norm to use for efficacy calculation ('e'uclidean, 'm'aximum,
                                                 *   's'um, 'd'iscrete) */
#define SCIP_DEFAULT_SEPA_MAXROUNDS           5 /**< maximal number of separation rounds per node (-1: unlimited) */
#define SCIP_DEFAULT_SEPA_MAXROUNDSROOT      -1 /**< maximal number of separation rounds in the root node (-1: unlimited) */
#define SCIP_DEFAULT_SEPA_MAXADDROUNDS        1 /**< maximal additional number of separation rounds in subsequent
                                                 *   price-and-cut loops (-1: no additional restriction) */
#define SCIP_DEFAULT_SEPA_MAXSTALLROUNDS    100 /**< maximal number of consecutive separation rounds without objective
                                                 *   improvement (-1: no additional restriction) */
#define SCIP_DEFAULT_SEPA_MAXCUTS           100 /**< maximal number of cuts separated per separation round */
#define SCIP_DEFAULT_SEPA_MAXCUTSROOT      2000 /**< maximal separated cuts at the root node */
#define SCIP_DEFAULT_SEPA_CUTAGELIMIT       100 /**< maximum age a cut can reach before it is deleted from global cut pool
                                                 *   (-1: cuts are never deleted from the global cut pool) */
#define SCIP_DEFAULT_SEPA_POOLFREQ            5 /**< separation frequency for the global cut pool */


/* Timing */

#define SCIP_DEFAULT_TIME_CLOCKTYPE  SCIP_CLOCKTYPE_CPU  /**< default clock type for timing */
#define SCIP_DEFAULT_TIME_ENABLED          TRUE /**< is timing enabled? */


/* VBC Tool output */
#define SCIP_DEFAULT_VBC_FILENAME           "-" /**< name of the VBC Tool output file, or "-" if no output should be
                                                 *   created */
#define SCIP_DEFAULT_VBC_REALTIME          TRUE /**< should the real solving time be used instead of a time step counter
                                                 *   in VBC output? */




/** calculate memory size for dynamically allocated arrays */
static
int calcGrowSize(
   int              initsize,           /**< initial size of array */
   Real             growfac,            /**< growing factor of array */
   int              num                 /**< minimum number of entries to store */
   )
{
   int size;

   assert(initsize >= 0);
   assert(growfac >= 1.0);

   if( growfac == 1.0 )
      size = MAX(initsize, num);
   else
   {
      /* calculate the size with this loop, such that the resulting numbers are always the same (-> block memory) */
      size = initsize;
      while( size < num )
         size = (int)(growfac * size + 1);
   }

   return size;
}


/** information method for a parameter change of feastol */
static
DECL_PARAMCHGD(paramChgdFeastol)
{  /*lint --e{715}*/
   Real newfeastol;

   newfeastol = SCIPparamGetReal(param);
   
   /* change the feastol through the SCIP call in order to mark the LP unsolved */
   CHECK_OKAY( SCIPchgFeastol(scip, newfeastol) );

   return SCIP_OKAY;
}

/** information method for a parameter change of dualfeastol */
static
DECL_PARAMCHGD(paramChgdDualfeastol)
{  /*lint --e{715}*/
   Real newdualfeastol;

   newdualfeastol = SCIPparamGetReal(param);
   
   /* change the dualfeastol through the SCIP call in order to mark the LP unsolved */
   CHECK_OKAY( SCIPchgDualfeastol(scip, newdualfeastol) );

   return SCIP_OKAY;
}

/** creates global SCIP settings */
RETCODE SCIPsetCreate(
   SET**            set,                /**< pointer to SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(set != NULL);
   assert(scip != NULL);

   ALLOC_OKAY( allocMemory(set) );

   (*set)->scip = scip;

   CHECK_OKAY( SCIPparamsetCreate(&(*set)->paramset, memhdr) );
   CHECK_OKAY( SCIPbufferCreate(&(*set)->buffer) );

   (*set)->readers = NULL;
   (*set)->nreaders = 0;
   (*set)->readerssize = 0;
   (*set)->pricers = NULL;
   (*set)->npricers = 0;
   (*set)->nactivepricers = 0;
   (*set)->pricerssize = 0;
   (*set)->pricerssorted = FALSE;
   (*set)->conshdlrs = NULL;
   (*set)->nconshdlrs = 0;
   (*set)->conshdlrssize = 0;
   (*set)->conflicthdlrs = NULL;
   (*set)->nconflicthdlrs = 0;
   (*set)->conflicthdlrssize = 0;
   (*set)->conflicthdlrssorted = FALSE;
   (*set)->presols = NULL;
   (*set)->npresols = 0;
   (*set)->presolssize = 0;
   (*set)->presolssorted = FALSE;
   (*set)->sepas = NULL;
   (*set)->nsepas = 0;
   (*set)->sepassize = 0;
   (*set)->sepassorted = FALSE;
   (*set)->props = NULL;
   (*set)->nprops = 0;
   (*set)->propssize = 0;
   (*set)->propssorted = FALSE;
   (*set)->heurs = NULL;
   (*set)->nheurs = 0;
   (*set)->heurssize = 0;
   (*set)->heurssorted = FALSE;
   (*set)->eventhdlrs = NULL;
   (*set)->neventhdlrs = 0;
   (*set)->eventhdlrssize = 0;
   (*set)->nodesels = NULL;
   (*set)->nnodesels = 0;
   (*set)->nodeselssize = 0;
   (*set)->nodesel = NULL;
   (*set)->branchrules = NULL;
   (*set)->nbranchrules = 0;
   (*set)->branchrulessize = 0;
   (*set)->branchrulessorted = FALSE;
   (*set)->disps = NULL;
   (*set)->ndisps = 0;
   (*set)->dispssize = 0;
   (*set)->vbc_filename = NULL;

   /* branching parameters */
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr, 
         "branching/scorefac",
         "branching score factor to weigh downward and upward gain prediction",
         &(*set)->branch_scorefac, SCIP_DEFAULT_BRANCH_SCOREFAC, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "branching/preferbinary",
         "should branching on binary variables be prefered?",
         &(*set)->branch_preferbinary, SCIP_DEFAULT_BRANCH_PREFERBINARY,
         NULL, NULL) );

   /* conflict analysis parameters */
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/useprop",
         "should propagation conflict analysis be used?",
         &(*set)->conf_useprop, SCIP_DEFAULT_CONF_USEPROP,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/uselp",
         "should infeasible LP conflict analysis be used?",
         &(*set)->conf_uselp, SCIP_DEFAULT_CONF_USELP,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/usesb",
         "should infeasible strong branching conflict analysis be used?",
         &(*set)->conf_usesb, SCIP_DEFAULT_CONF_USESB,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/usepseudo",
         "should pseudo solution conflict analysis be used?",
         &(*set)->conf_usepseudo, SCIP_DEFAULT_CONF_USEPSEUDO,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "conflict/maxvarsfac",
         "maximal fraction of binary variables involved in a conflict clause",
         &(*set)->conf_maxvarsfac, SCIP_DEFAULT_CONF_MAXVARSFAC, 0.0, REAL_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "conflict/minmaxvars",
         "minimal absolute maximum of variables involved in a conflict clause",
         &(*set)->conf_minmaxvars, SCIP_DEFAULT_CONF_MINMAXVARS, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "conflict/maxlploops",
         "maximal number of LP resolving loops during conflict analysis",
         &(*set)->conf_maxlploops, SCIP_DEFAULT_CONF_MAXLPLOOPS, 1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "conflict/fuiplevels",
         "number of depth levels up to which first UIP's are used in conflict analysis (-1: use All-FirstUIP rule)",
         &(*set)->conf_fuiplevels, SCIP_DEFAULT_CONF_FUIPLEVELS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "conflict/interclauses",
         "maximal number of intermediate conflict clauses generated in conflict graph (-1: use every intermediate clause)",
         &(*set)->conf_interclauses, SCIP_DEFAULT_CONF_INTERCLAUSES, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/repropagate",
         "should earlier nodes be repropagated in order to replace branching decisions by deductions",
         &(*set)->conf_repropagate, SCIP_DEFAULT_CONF_REPROPAGATE,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "conflict/reconvclauses",
         "should reconvergence clauses be created for UIPs of last depth level?",
         &(*set)->conf_reconvclauses, SCIP_DEFAULT_CONF_RECONVCLAUSES,
         NULL, NULL) );

   /* constraint parameters */
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "constraints/agelimit",
         "maximum age an unnecessary constraint can reach before it is deleted, or -1 to keep all constraints",
         &(*set)->cons_agelimit, SCIP_DEFAULT_CONS_AGELIMIT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "constraints/obsoleteage",
         "age of a constraint after which it is marked obsolete, or -1 to not mark constraints obsolete",
         &(*set)->cons_obsoleteage, SCIP_DEFAULT_CONS_OBSOLETEAGE, -1, INT_MAX,
         NULL, NULL) );

   /* display parameters */
   assert(sizeof(int) == sizeof(VERBLEVEL));
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "display/verblevel",
         "verbosity level of output",
         (int*)&(*set)->disp_verblevel, (int)SCIP_DEFAULT_DISP_VERBLEVEL,
         (int)SCIP_VERBLEVEL_NONE, (int)SCIP_VERBLEVEL_FULL,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "display/width",
         "maximal number of characters in a node information line",
         &(*set)->disp_width, SCIP_DEFAULT_DISP_WIDTH, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "display/freq",
         "frequency for displaying node information lines",
         &(*set)->disp_freq, SCIP_DEFAULT_DISP_FREQ, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "display/headerfreq",
         "frequency for displaying header lines (every n'th node information line)",
         &(*set)->disp_headerfreq, SCIP_DEFAULT_DISP_HEADERFREQ, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "display/lpinfo",
         "should the LP solver display status messages?",
         &(*set)->disp_lpinfo, SCIP_DEFAULT_DISP_LPINFO,
         NULL, NULL) );

   /* limit parameters */
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "limits/time",
         "maximal time in seconds to run",
         &(*set)->limit_time, SCIP_DEFAULT_LIMIT_TIME, 0.0, REAL_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddLongintParam(*set, memhdr,
         "limits/nodes",
         "maximal number of nodes to process (-1: no limit)",
         &(*set)->limit_nodes, SCIP_DEFAULT_LIMIT_NODES, -1LL, LONGINT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "limits/memory",
         "maximal memory usage in MB; reported memory usage is lower than real memory usage!",
         &(*set)->limit_memory, SCIP_DEFAULT_LIMIT_MEMORY, 0.0, REAL_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "limits/gap",
         "solving stops, if the gap = |(primalbound - dualbound)/dualbound| is below the given value",
         &(*set)->limit_gap, SCIP_DEFAULT_LIMIT_GAP, 0.0, REAL_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "limits/sol",
         "solving stops, if the given number of solutions were found (-1: no limit)",
         &(*set)->limit_sol, SCIP_DEFAULT_LIMIT_SOL, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "limits/bestsol",
         "solving stops, if the given number of solution improvements were found (-1: no limit)",
         &(*set)->limit_bestsol, SCIP_DEFAULT_LIMIT_BESTSOL, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "limits/maxsol",
         "maximal number of solutions to store in the solution storage",
         &(*set)->limit_maxsol, SCIP_DEFAULT_LIMIT_MAXSOL, 1, INT_MAX,
         NULL, NULL) );

   /* LP parameters */
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "lp/solvefreq",
         "frequency for solving LP at the nodes (-1: never; 0: only root LP)",
         &(*set)->lp_solvefreq, SCIP_DEFAULT_LP_SOLVEFREQ, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "lp/solvedepth",
         "maximal depth for solving LP at the nodes (-1: no depth limit)",
         &(*set)->lp_solvedepth, SCIP_DEFAULT_LP_SOLVEDEPTH, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "lp/colagelimit",
         "maximum age a dynamic column can reach before it is deleted from the LP (-1: don't delete columns due to aging)",
         &(*set)->lp_colagelimit, SCIP_DEFAULT_LP_COLAGELIMIT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "lp/rowagelimit",
         "maximum age a dynamic row can reach before it is deleted from the LP (-1: don't delete rows due to aging)",
         &(*set)->lp_rowagelimit, SCIP_DEFAULT_LP_ROWAGELIMIT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/cleanupcols",
         "should new non-basic columns be removed after LP solving?",
         &(*set)->lp_cleanupcols, SCIP_DEFAULT_LP_CLEANUPCOLS,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/cleanuprows",
         "should new basic rows be removed after LP solving?",
         &(*set)->lp_cleanuprows, SCIP_DEFAULT_LP_CLEANUPROWS,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/checkfeas",
         "should LP solutions be checked, resolving LP when numerical troubles occur?",
         &(*set)->lp_checkfeas, SCIP_DEFAULT_LP_CHECKFEAS,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/fastmip",
         "should FASTMIP setting of LP solver be used?",
         &(*set)->lp_fastmip, SCIP_DEFAULT_LP_FASTMIP,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/scaling",
         "should scaling of LP solver be used?",
         &(*set)->lp_scaling, SCIP_DEFAULT_LP_SCALING,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "lp/presolving",
         "should presolving of LP solver be used?",
         &(*set)->lp_presolving, SCIP_DEFAULT_LP_PRESOLVING,
         NULL, NULL) );

   /* memory parameters */
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr, 
         "memory/savefac",
         "fraction of maximal memory usage resulting in switch to memory saving mode",
         &(*set)->mem_savefac, SCIP_DEFAULT_MEM_SAVEFAC, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "memory/arraygrowfac",
         "memory growing factor for dynamically allocated arrays",
         &(*set)->mem_arraygrowfac, SCIP_DEFAULT_MEM_ARRAYGROWFAC, 1.0, 10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "memory/arraygrowinit",
         "initial size of dynamically allocated arrays",
         &(*set)->mem_arraygrowinit, SCIP_DEFAULT_MEM_ARRAYGROWINIT, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "memory/treegrowfac",
         "memory growing factor for tree array",
         &(*set)->mem_treegrowfac, SCIP_DEFAULT_MEM_TREEGROWFAC, 1.0, 10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "memory/treegrowinit",
         "initial size of tree array",
         &(*set)->mem_treegrowinit, SCIP_DEFAULT_MEM_TREEGROWINIT, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr, 
         "memory/pathgrowfac",
         "memory growing factor for path array",
         &(*set)->mem_pathgrowfac, SCIP_DEFAULT_MEM_PATHGROWFAC, 1.0, 10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "memory/pathgrowinit",
         "initial size of path array",
         &(*set)->mem_pathgrowinit, SCIP_DEFAULT_MEM_PATHGROWINIT, 0, INT_MAX,
         NULL, NULL) );

   /* miscellaneous parameters */
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "misc/catchctrlc",
         "should the CTRL-C interrupt be caught by SCIP?",
         &(*set)->misc_catchctrlc, SCIP_DEFAULT_MISC_CATCHCTRLC,
         NULL, NULL) );
   /**@todo activate exactsolve parameter and finish implementation of solving MIPs exactly */
#if 0
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "misc/exactsolve",
         "should the problem be solved exactly (with proven dual bounds)?",
         &(*set)->misc_exactsolve, SCIP_DEFAULT_MISC_EXACTSOLVE,
         NULL, NULL) );
#else
   (*set)->misc_exactsolve = FALSE;
#endif

   /* numerical parameters */
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/infinity",
         "values larger than this are considered infinity",
         &(*set)->num_infinity, SCIP_DEFAULT_INFINITY, 1e+10, SCIP_INVALID/10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/epsilon",
         "absolute values smaller than this are considered zero",
         &(*set)->num_epsilon, SCIP_DEFAULT_EPSILON, SCIP_MINEPSILON, SCIP_MAXEPSILON,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/sumepsilon",
         "absolute values of sums smaller than this are considered zero",
         &(*set)->num_sumepsilon, SCIP_DEFAULT_SUMEPSILON, SCIP_MINEPSILON*1e+03, SCIP_MAXEPSILON,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/feastol",
         "LP feasibility tolerance for constraints",
         &(*set)->num_feastol, SCIP_DEFAULT_FEASTOL, SCIP_MINEPSILON*1e+03, SCIP_MAXEPSILON,
         paramChgdFeastol, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/dualfeastol",
         "LP feasibility tolerance for reduced costs",
         &(*set)->num_dualfeastol, SCIP_DEFAULT_DUALFEASTOL, SCIP_MINEPSILON*1e+03, SCIP_MAXEPSILON,
         paramChgdDualfeastol, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/boundstreps",
         "minimal improve for strengthening bounds",
         &(*set)->num_boundstreps, SCIP_DEFAULT_BOUNDSTREPS, SCIP_MINEPSILON*1e+03, SCIP_INVALID/10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/pseudocosteps",
         "minimal variable distance value to use for branching pseudo cost updates",
         &(*set)->num_pseudocosteps, SCIP_DEFAULT_PSEUDOCOSTEPS, SCIP_MINEPSILON*1e+03, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "numerics/pseudocostdelta",
         "minimal objective distance value to use for branching pseudo cost updates",
         &(*set)->num_pseudocostdelta, SCIP_DEFAULT_PSEUDOCOSTDELTA, 0.0, REAL_MAX,
         NULL, NULL) );

   /* presolving parameters */
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "presolving/maxrounds",
         "maximal number of presolving rounds (-1: unlimited)",
         &(*set)->presol_maxrounds, SCIP_DEFAULT_PRESOL_MAXROUNDS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr, 
         "presolving/abortfac",
         "abort presolve, if less than this fraction of the problem was changed in last presolve round",
         &(*set)->presol_abortfac, SCIP_DEFAULT_PRESOL_ABORTFAC, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "presolving/restartbdchgs",
         "number of root node bound changes triggering a restart with preprocessing (-1: no restart, 0: restart only after complete root node evaluation)",
         &(*set)->presol_restartbdchgs, SCIP_DEFAULT_PRESOL_RESTARTBDCHGS, -1, INT_MAX,
         NULL, NULL) );

   /* pricing parameters */
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "pricing/maxvars",
         "maximal number of variables priced in per pricing round",
         &(*set)->price_maxvars, SCIP_DEFAULT_PRICE_MAXVARS, 1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "pricing/maxvarsroot",
         "maximal number of priced variables at the root node",
         &(*set)->price_maxvarsroot, SCIP_DEFAULT_PRICE_MAXVARSROOT, 1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "pricing/abortfac",
         "pricing is aborted, if fac * pricing/maxvars pricing candidates were found",
         &(*set)->price_abortfac, SCIP_DEFAULT_PRICE_ABORTFAC, 1.0, REAL_MAX,
         NULL, NULL) );

   /* propagation parameters */
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "propagating/maxrounds",
         "maximal number of propagation rounds per node (-1: unlimited)",
         &(*set)->prop_maxrounds, SCIP_DEFAULT_PROP_MAXROUNDS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr, 
         "propagating/maxroundsroot",
         "maximal number of propagation rounds in the root node (-1: unlimited)",
         &(*set)->prop_maxroundsroot, SCIP_DEFAULT_PROP_MAXROUNDSROOT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "propagating/redcostfreq",
         "frequency for applying reduced cost fixing (-1: never; 0: only root LP)",
         &(*set)->prop_redcostfreq, SCIP_DEFAULT_PROP_REDCOSTFREQ, -1, INT_MAX,
         NULL, NULL) );

   /* separation parameters */
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/maxbounddist",
         "maximal relative distance from current node's dual bound to primal bound compared to best node's dual bound for applying separation (0.0: only on current best node, 1.0: on all nodes)",
         &(*set)->sepa_maxbounddist, SCIP_DEFAULT_SEPA_MAXBOUNDDIST, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/minefficacy",
         "minimal efficacy for a cut to enter the LP",
         &(*set)->sepa_minefficacy, SCIP_DEFAULT_SEPA_MINEFFICACY, 0.0, SCIP_INVALID/10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/minefficacyroot",
         "minimal efficacy for a cut to enter the LP in the root node",
         &(*set)->sepa_minefficacyroot, SCIP_DEFAULT_SEPA_MINEFFICACYROOT, 0.0, SCIP_INVALID/10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/minortho",
         "minimal orthogonality for a cut to enter the LP",
         &(*set)->sepa_minortho, SCIP_DEFAULT_SEPA_MINORTHO, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/minorthoroot",
         "minimal orthogonality for a cut to enter the LP in the root node",
         &(*set)->sepa_minorthoroot, SCIP_DEFAULT_SEPA_MINORTHOROOT, 0.0, 1.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddRealParam(*set, memhdr,
         "separating/orthofac",
         "factor to scale orthogonality of cut in separation score calculation (0.0 to disable orthogonality calculation)",
         &(*set)->sepa_orthofac, SCIP_DEFAULT_SEPA_ORTHOFAC, 0.0, SCIP_INVALID/10.0,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddCharParam(*set, memhdr,
         "separating/efficacynorm",
         "row norm to use for efficacy calculation ('e'uclidean, 'm'aximum, 's'um, 'd'iscrete)",
         &(*set)->sepa_efficacynorm, SCIP_DEFAULT_SEPA_EFFICACYNORM, "emsd",
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxrounds",
         "maximal number of separation rounds per node (-1: unlimited)",
         &(*set)->sepa_maxrounds, SCIP_DEFAULT_SEPA_MAXROUNDS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxroundsroot",
         "maximal number of separation rounds in the root node (-1: unlimited)",
         &(*set)->sepa_maxroundsroot, SCIP_DEFAULT_SEPA_MAXROUNDSROOT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxaddrounds",
         "maximal additional number of separation rounds in subsequent price-and-cut loops (-1: no additional restriction)",
         &(*set)->sepa_maxaddrounds, SCIP_DEFAULT_SEPA_MAXADDROUNDS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxstallrounds",
         "maximal number of consecutive separation rounds without objective improvement (-1: no additional restriction)",
         &(*set)->sepa_maxstallrounds, SCIP_DEFAULT_SEPA_MAXSTALLROUNDS, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxcuts",
         "maximal number of cuts separated per separation round (0: disable local separation)",
         &(*set)->sepa_maxcuts, SCIP_DEFAULT_SEPA_MAXCUTS, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/maxcutsroot",
         "maximal number of separated cuts at the root node (0: disable root node separation)",
         &(*set)->sepa_maxcutsroot, SCIP_DEFAULT_SEPA_MAXCUTSROOT, 0, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/cutagelimit",
         "maximum age a cut can reach before it is deleted from the global cut pool, or -1 to keep all cuts",
         &(*set)->sepa_cutagelimit, SCIP_DEFAULT_SEPA_CUTAGELIMIT, -1, INT_MAX,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "separating/poolfreq",
         "separation frequency for the global cut pool (-1: disable global cut pool, 0: only separate pool at the root)",
         &(*set)->sepa_poolfreq, SCIP_DEFAULT_SEPA_POOLFREQ, -1, INT_MAX,
         NULL, NULL) );

   /* timing parameters */
   assert(sizeof(int) == sizeof(CLOCKTYPE));
   CHECK_OKAY( SCIPsetAddIntParam(*set, memhdr,
         "timing/clocktype",
         "default clock type (1: CPU user seconds, 2: wall clock time)",
         (int*)&(*set)->time_clocktype, (int)SCIP_DEFAULT_TIME_CLOCKTYPE, 1, 2,
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "timing/enabled",
         "is timing enabled?",
         &(*set)->time_enabled, SCIP_DEFAULT_TIME_ENABLED,
         NULL, NULL) );

   /* VBC tool parameters */
   CHECK_OKAY( SCIPsetAddStringParam(*set, memhdr,
         "vbc/filename",
         "name of the VBC Tool output file, or - if no VBC Tool output should be created",
         &(*set)->vbc_filename, SCIP_DEFAULT_VBC_FILENAME, 
         NULL, NULL) );
   CHECK_OKAY( SCIPsetAddBoolParam(*set, memhdr,
         "vbc/realtime",
         "should the real solving time be used instead of a time step counter in VBC output?",
         &(*set)->vbc_realtime, SCIP_DEFAULT_VBC_REALTIME,
         NULL, NULL) );

   return SCIP_OKAY;
}

/** frees global SCIP settings */
RETCODE SCIPsetFree(
   SET**            set,                /**< pointer to SCIP settings */
   MEMHDR*          memhdr              /**< block memory */
   )
{
   int i;

   assert(set != NULL);

   /* free parameter set */
   SCIPparamsetFree(&(*set)->paramset, memhdr);

   /* free memory buffers */
   SCIPbufferFree(&(*set)->buffer);

   /* free file readers */
   for( i = 0; i < (*set)->nreaders; ++i )
   {
      CHECK_OKAY( SCIPreaderFree(&(*set)->readers[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->readers);

   /* free variable pricers */
   for( i = 0; i < (*set)->npricers; ++i )
   {
      CHECK_OKAY( SCIPpricerFree(&(*set)->pricers[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->pricers);

   /* free constraint handlers */
   for( i = 0; i < (*set)->nconshdlrs; ++i )
   {
      CHECK_OKAY( SCIPconshdlrFree(&(*set)->conshdlrs[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->conshdlrs);

   /* free conflict handlers */
   for( i = 0; i < (*set)->nconflicthdlrs; ++i )
   {
      CHECK_OKAY( SCIPconflicthdlrFree(&(*set)->conflicthdlrs[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->conflicthdlrs);

   /* free presolvers */
   for( i = 0; i < (*set)->npresols; ++i )
   {
      CHECK_OKAY( SCIPpresolFree(&(*set)->presols[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->presols);

   /* free separators */
   for( i = 0; i < (*set)->nsepas; ++i )
   {
      CHECK_OKAY( SCIPsepaFree(&(*set)->sepas[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->sepas);

   /* free propagators */
   for( i = 0; i < (*set)->nprops; ++i )
   {
      CHECK_OKAY( SCIPpropFree(&(*set)->props[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->props);

   /* free primal heuristics */
   for( i = 0; i < (*set)->nheurs; ++i )
   {
      CHECK_OKAY( SCIPheurFree(&(*set)->heurs[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->heurs);

   /* free event handlers */
   for( i = 0; i < (*set)->neventhdlrs; ++i )
   {
      CHECK_OKAY( SCIPeventhdlrFree(&(*set)->eventhdlrs[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->eventhdlrs);

   /* free node selectors */
   for( i = 0; i < (*set)->nnodesels; ++i )
   {
      CHECK_OKAY( SCIPnodeselFree(&(*set)->nodesels[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->nodesels);

   /* free branching methods */
   for( i = 0; i < (*set)->nbranchrules; ++i )
   {
      CHECK_OKAY( SCIPbranchruleFree(&(*set)->branchrules[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->branchrules);

   /* free display columns */
   for( i = 0; i < (*set)->ndisps; ++i )
   {
      CHECK_OKAY( SCIPdispFree(&(*set)->disps[i], (*set)->scip) );
   }
   freeMemoryArrayNull(&(*set)->disps);

   freeMemory(set);

   return SCIP_OKAY;
}

/** creates a Bool parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddBoolParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   Bool*            valueptr,           /**< pointer to store the current parameter value, or NULL */
   Bool             defaultvalue,       /**< default value of the parameter */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddBool(set->paramset, memhdr, name, desc, valueptr, defaultvalue, paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** creates a int parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddIntParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   int*             valueptr,           /**< pointer to store the current parameter value, or NULL */
   int              defaultvalue,       /**< default value of the parameter */
   int              minvalue,           /**< minimum value for parameter */
   int              maxvalue,           /**< maximum value for parameter */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddInt(set->paramset, memhdr, name, desc, valueptr, defaultvalue, minvalue, maxvalue, 
         paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** creates a Longint parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddLongintParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   Longint*         valueptr,           /**< pointer to store the current parameter value, or NULL */
   Longint          defaultvalue,       /**< default value of the parameter */
   Longint          minvalue,           /**< minimum value for parameter */
   Longint          maxvalue,           /**< maximum value for parameter */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddLongint(set->paramset, memhdr, name, desc, valueptr, defaultvalue, minvalue, maxvalue, 
         paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** creates a Real parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddRealParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   Real*            valueptr,           /**< pointer to store the current parameter value, or NULL */
   Real             defaultvalue,       /**< default value of the parameter */
   Real             minvalue,           /**< minimum value for parameter */
   Real             maxvalue,           /**< maximum value for parameter */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddReal(set->paramset, memhdr, name, desc, valueptr, defaultvalue, minvalue, maxvalue, 
         paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** creates a char parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddCharParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   char*            valueptr,           /**< pointer to store the current parameter value, or NULL */
   char             defaultvalue,       /**< default value of the parameter */
   const char*      allowedvalues,      /**< array with possible parameter values, or NULL if not restricted */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddChar(set->paramset, memhdr, name, desc, valueptr, defaultvalue, allowedvalues, 
         paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** creates a string parameter, sets it to its default value, and adds it to the parameter set */
RETCODE SCIPsetAddStringParam(
   SET*             set,                /**< global SCIP settings */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of the parameter */
   const char*      desc,               /**< description of the parameter */
   char**           valueptr,           /**< pointer to store the current parameter value, or NULL */
   const char*      defaultvalue,       /**< default value of the parameter */
   DECL_PARAMCHGD   ((*paramchgd)),     /**< change information method of parameter */
   PARAMDATA*       paramdata           /**< locally defined parameter specific data */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetAddString(set->paramset, memhdr, name, desc, valueptr, defaultvalue, paramchgd, paramdata) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing Bool parameter */
RETCODE SCIPsetGetBoolParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Bool*            value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetBool(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing Int parameter */
RETCODE SCIPsetGetIntParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   int*             value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetInt(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing Longint parameter */
RETCODE SCIPsetGetLongintParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Longint*         value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetLongint(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing Real parameter */
RETCODE SCIPsetGetRealParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Real*            value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetReal(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing Char parameter */
RETCODE SCIPsetGetCharParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   char*            value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetChar(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** gets the value of an existing String parameter */
RETCODE SCIPsetGetStringParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   char**           value               /**< pointer to store the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetGetString(set->paramset, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing Bool parameter */
RETCODE SCIPsetSetBoolParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Bool             value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetBool(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing Int parameter */
RETCODE SCIPsetSetIntParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   int              value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetInt(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing Longint parameter */
RETCODE SCIPsetSetLongintParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Longint          value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetLongint(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing Real parameter */
RETCODE SCIPsetSetRealParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   Real             value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetReal(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing Char parameter */
RETCODE SCIPsetSetCharParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   char             value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetChar(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** changes the value of an existing String parameter */
RETCODE SCIPsetSetStringParam(
   SET*             set,                /**< global SCIP settings */
   const char*      name,               /**< name of the parameter */
   const char*      value               /**< new value of the parameter */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetSetString(set->paramset, set->scip, name, value) );
   
   return SCIP_OKAY;
}

/** reads parameters from a file */
RETCODE SCIPsetReadParams(
   SET*             set,                /**< global SCIP settings */
   const char*      filename            /**< file name */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetRead(set->paramset, set->scip, filename) );

   return SCIP_OKAY;
}

/** writes all parameters in the parameter set to a file */
RETCODE SCIPsetWriteParams(
   SET*             set,                /**< global SCIP settings */
   const char*      filename,           /**< file name, or NULL for stdout */
   Bool             comments,           /**< should parameter descriptions be written as comments? */
   Bool             onlychanged         /**< should only the parameters been written, that are changed from default? */
   )
{
   assert(set != NULL);

   CHECK_OKAY( SCIPparamsetWrite(set->paramset, filename, comments, onlychanged) );

   return SCIP_OKAY;
}

/** returns the array of all available SCIP parameters */
PARAM** SCIPsetGetParams(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return SCIPparamsetGetParams(set->paramset);
}

/** returns the total number of all available SCIP parameters */
int SCIPsetGetNParams(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return SCIPparamsetGetNParams(set->paramset);
}

/** inserts file reader in file reader list */
RETCODE SCIPsetIncludeReader(
   SET*             set,                /**< global SCIP settings */
   READER*          reader              /**< file reader */
   )
{
   assert(set != NULL);
   assert(reader != NULL);

   if( set->nreaders >= set->readerssize )
   {
      set->readerssize = SCIPsetCalcMemGrowSize(set, set->nreaders+1);
      ALLOC_OKAY( reallocMemoryArray(&set->readers, set->readerssize) );
   }
   assert(set->nreaders < set->readerssize);
   
   set->readers[set->nreaders] = reader;
   set->nreaders++;

   return SCIP_OKAY;
}   

/** returns the file reader of the given name, or NULL if not existing */
READER* SCIPsetFindReader(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of file reader */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nreaders; ++i )
   {
      if( strcmp(SCIPreaderGetName(set->readers[i]), name) == 0 )
         return set->readers[i];
   }

   return NULL;
}

/** inserts variable pricer in variable pricer list */
RETCODE SCIPsetIncludePricer(
   SET*             set,                /**< global SCIP settings */
   PRICER*          pricer              /**< variable pricer */
   )
{
   assert(set != NULL);
   assert(pricer != NULL);

   if( set->npricers >= set->pricerssize )
   {
      set->pricerssize = SCIPsetCalcMemGrowSize(set, set->npricers+1);
      ALLOC_OKAY( reallocMemoryArray(&set->pricers, set->pricerssize) );
   }
   assert(set->npricers < set->pricerssize);
   
   set->pricers[set->npricers] = pricer;
   set->npricers++;
   set->pricerssorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the variable pricer of the given name, or NULL if not existing */
PRICER* SCIPsetFindPricer(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of variable pricer */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->npricers; ++i )
   {
      if( strcmp(SCIPpricerGetName(set->pricers[i]), name) == 0 )
         return set->pricers[i];
   }

   return NULL;
}

/** sorts pricers by priorities */
void SCIPsetSortPricers(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->pricerssorted )
   {
      SCIPbsortPtr((void**)set->pricers, set->npricers, SCIPpricerComp);
      set->pricerssorted = TRUE;
   }
}

/** inserts constraint handler in constraint handler list */
RETCODE SCIPsetIncludeConshdlr(
   SET*             set,                /**< global SCIP settings */
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   int checkpriority;
   int i;

   assert(set != NULL);
   assert(conshdlr != NULL);
   assert(!SCIPconshdlrIsInitialized(conshdlr));

   if( set->nconshdlrs >= set->conshdlrssize )
   {
      set->conshdlrssize = SCIPsetCalcMemGrowSize(set, set->nconshdlrs+1);
      ALLOC_OKAY( reallocMemoryArray(&set->conshdlrs, set->conshdlrssize) );
   }
   assert(set->nconshdlrs < set->conshdlrssize);

   checkpriority = SCIPconshdlrGetCheckPriority(conshdlr);
   for( i = set->nconshdlrs; i > 0 && SCIPconshdlrGetCheckPriority(set->conshdlrs[i-1]) < checkpriority; --i )
   {
      set->conshdlrs[i] = set->conshdlrs[i-1];
   }

   set->conshdlrs[i] = conshdlr;
   set->nconshdlrs++;

   return SCIP_OKAY;
}   

/** returns the constraint handler of the given name, or NULL if not existing */
CONSHDLR* SCIPsetFindConshdlr(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of constraint handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nconshdlrs; ++i )
   {
      if( strcmp(SCIPconshdlrGetName(set->conshdlrs[i]), name) == 0 )
         return set->conshdlrs[i];
   }

   return NULL;
}

/** inserts conflict handler in conflict handler list */
RETCODE SCIPsetIncludeConflicthdlr(
   SET*             set,                /**< global SCIP settings */
   CONFLICTHDLR*    conflicthdlr        /**< conflict handler */
   )
{
   assert(set != NULL);
   assert(conflicthdlr != NULL);
   assert(!SCIPconflicthdlrIsInitialized(conflicthdlr));

   if( set->nconflicthdlrs >= set->conflicthdlrssize )
   {
      set->conflicthdlrssize = SCIPsetCalcMemGrowSize(set, set->nconflicthdlrs+1);
      ALLOC_OKAY( reallocMemoryArray(&set->conflicthdlrs, set->conflicthdlrssize) );
   }
   assert(set->nconflicthdlrs < set->conflicthdlrssize);

   set->conflicthdlrs[set->nconflicthdlrs] = conflicthdlr;
   set->nconflicthdlrs++;
   set->conflicthdlrssorted = FALSE;

   return SCIP_OKAY;
}

/** returns the conflict handler of the given name, or NULL if not existing */
CONFLICTHDLR* SCIPsetFindConflicthdlr(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of conflict handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nconflicthdlrs; ++i )
   {
      if( strcmp(SCIPconflicthdlrGetName(set->conflicthdlrs[i]), name) == 0 )
         return set->conflicthdlrs[i];
   }

   return NULL;
}

/** sorts conflict handlers by priorities */
void SCIPsetSortConflicthdlrs(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->conflicthdlrssorted )
   {
      SCIPbsortPtr((void**)set->conflicthdlrs, set->nconflicthdlrs, SCIPconflicthdlrComp);
      set->conflicthdlrssorted = TRUE;
   }
}

/** inserts presolver in presolver list */
RETCODE SCIPsetIncludePresol(
   SET*             set,                /**< global SCIP settings */
   PRESOL*          presol              /**< presolver */
   )
{
   assert(set != NULL);
   assert(presol != NULL);

   if( set->npresols >= set->presolssize )
   {
      set->presolssize = SCIPsetCalcMemGrowSize(set, set->npresols+1);
      ALLOC_OKAY( reallocMemoryArray(&set->presols, set->presolssize) );
   }
   assert(set->npresols < set->presolssize);
   
   set->presols[set->npresols] = presol;
   set->npresols++;
   set->presolssorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the presolver of the given name, or NULL if not existing */
PRESOL* SCIPsetFindPresol(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of presolver */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->npresols; ++i )
   {
      if( strcmp(SCIPpresolGetName(set->presols[i]), name) == 0 )
         return set->presols[i];
   }

   return NULL;
}

/** sorts presolvers by priorities */
void SCIPsetSortPresols(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->presolssorted )
   {
      SCIPbsortPtr((void**)set->presols, set->npresols, SCIPpresolComp);
      set->presolssorted = TRUE;
   }
}

/** inserts separator in separator list */
RETCODE SCIPsetIncludeSepa(
   SET*             set,                /**< global SCIP settings */
   SEPA*            sepa                /**< separator */
   )
{
   assert(set != NULL);
   assert(sepa != NULL);
   assert(!SCIPsepaIsInitialized(sepa));

   if( set->nsepas >= set->sepassize )
   {
      set->sepassize = SCIPsetCalcMemGrowSize(set, set->nsepas+1);
      ALLOC_OKAY( reallocMemoryArray(&set->sepas, set->sepassize) );
   }
   assert(set->nsepas < set->sepassize);
   
   set->sepas[set->nsepas] = sepa;
   set->nsepas++;
   set->sepassorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the separator of the given name, or NULL if not existing */
SEPA* SCIPsetFindSepa(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of separator */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nsepas; ++i )
   {
      if( strcmp(SCIPsepaGetName(set->sepas[i]), name) == 0 )
         return set->sepas[i];
   }

   return NULL;
}

/** sorts separators by priorities */
void SCIPsetSortSepas(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->sepassorted )
   {
      SCIPbsortPtr((void**)set->sepas, set->nsepas, SCIPsepaComp);
      set->sepassorted = TRUE;
   }
}

/** inserts propagator in propagator list */
RETCODE SCIPsetIncludeProp(
   SET*             set,                /**< global SCIP settings */
   PROP*            prop                /**< propagator */
   )
{
   assert(set != NULL);
   assert(prop != NULL);
   assert(!SCIPpropIsInitialized(prop));

   if( set->nprops >= set->propssize )
   {
      set->propssize = SCIPsetCalcMemGrowSize(set, set->nprops+1);
      ALLOC_OKAY( reallocMemoryArray(&set->props, set->propssize) );
   }
   assert(set->nprops < set->propssize);
   
   set->props[set->nprops] = prop;
   set->nprops++;
   set->propssorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the propagator of the given name, or NULL if not existing */
PROP* SCIPsetFindProp(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of propagator */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nprops; ++i )
   {
      if( strcmp(SCIPpropGetName(set->props[i]), name) == 0 )
         return set->props[i];
   }

   return NULL;
}

/** sorts propagators by priorities */
void SCIPsetSortProps(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->propssorted )
   {
      SCIPbsortPtr((void**)set->props, set->nprops, SCIPpropComp);
      set->propssorted = TRUE;
   }
}

/** inserts primal heuristic in primal heuristic list */
RETCODE SCIPsetIncludeHeur(
   SET*             set,                /**< global SCIP settings */
   HEUR*            heur                /**< primal heuristic */
   )
{
   assert(set != NULL);
   assert(heur != NULL);
   assert(!SCIPheurIsInitialized(heur));

   if( set->nheurs >= set->heurssize )
   {
      set->heurssize = SCIPsetCalcMemGrowSize(set, set->nheurs+1);
      ALLOC_OKAY( reallocMemoryArray(&set->heurs, set->heurssize) );
   }
   assert(set->nheurs < set->heurssize);
   
   set->heurs[set->nheurs] = heur;
   set->nheurs++;
   set->heurssorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the primal heuristic of the given name, or NULL if not existing */
HEUR* SCIPsetFindHeur(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of primal heuristic */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nheurs; ++i )
   {
      if( strcmp(SCIPheurGetName(set->heurs[i]), name) == 0 )
         return set->heurs[i];
   }

   return NULL;
}

/** sorts heuristics by priorities */
void SCIPsetSortHeurs(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->heurssorted )
   {
      SCIPbsortPtr((void**)set->heurs, set->nheurs, SCIPheurComp);
      set->heurssorted = TRUE;
   }
}

/** inserts event handler in event handler list */
RETCODE SCIPsetIncludeEventhdlr(
   SET*             set,                /**< global SCIP settings */
   EVENTHDLR*       eventhdlr           /**< event handler */
   )
{
   assert(set != NULL);
   assert(eventhdlr != NULL);
   assert(!SCIPeventhdlrIsInitialized(eventhdlr));

   if( set->neventhdlrs >= set->eventhdlrssize )
   {
      set->eventhdlrssize = SCIPsetCalcMemGrowSize(set, set->neventhdlrs+1);
      ALLOC_OKAY( reallocMemoryArray(&set->eventhdlrs, set->eventhdlrssize) );
   }
   assert(set->neventhdlrs < set->eventhdlrssize);
   
   set->eventhdlrs[set->neventhdlrs] = eventhdlr;
   set->neventhdlrs++;

   return SCIP_OKAY;
}   

/** returns the event handler of the given name, or NULL if not existing */
EVENTHDLR* SCIPsetFindEventhdlr(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of event handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->neventhdlrs; ++i )
   {
      if( strcmp(SCIPeventhdlrGetName(set->eventhdlrs[i]), name) == 0 )
         return set->eventhdlrs[i];
   }

   return NULL;
}

/** inserts node selector in node selector list */
RETCODE SCIPsetIncludeNodesel(
   SET*             set,                /**< global SCIP settings */
   NODESEL*         nodesel             /**< node selector */
   )
{
   int i;

   assert(set != NULL);
   assert(nodesel != NULL);
   assert(!SCIPnodeselIsInitialized(nodesel));

   if( set->nnodesels >= set->nodeselssize )
   {
      set->nodeselssize = SCIPsetCalcMemGrowSize(set, set->nnodesels+1);
      ALLOC_OKAY( reallocMemoryArray(&set->nodesels, set->nodeselssize) );
   }
   assert(set->nnodesels < set->nodeselssize);
   
   for( i = set->nnodesels; i > 0 && SCIPnodeselGetStdPriority(nodesel) > SCIPnodeselGetStdPriority(set->nodesels[i-1]);
        --i )
   {
      set->nodesels[i] = set->nodesels[i-1];
   }
   set->nodesels[i] = nodesel;
   set->nnodesels++;

   return SCIP_OKAY;
}   

/** returns the node selector of the given name, or NULL if not existing */
NODESEL* SCIPsetFindNodesel(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of event handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nnodesels; ++i )
   {
      if( strcmp(SCIPnodeselGetName(set->nodesels[i]), name) == 0 )
         return set->nodesels[i];
   }

   return NULL;
}

/** returns node selector with highest priority in the current mode */
NODESEL* SCIPsetGetNodesel(
   SET*             set,                /**< global SCIP settings */
   STAT*            stat                /**< dynamic problem statistics */
   )
{
   assert(set != NULL);
   assert(stat != NULL);

   /* check, if old node selector is still valid */
   if( set->nodesel == NULL && set->nnodesels > 0 )
   {
      int i;

      set->nodesel = set->nodesels[0];

      /* search highest priority node selector */
      if( stat->memsavemode )
      {
         for( i = 1; i < set->nnodesels; ++i )
         {
            if( SCIPnodeselGetMemsavePriority(set->nodesels[i]) > SCIPnodeselGetMemsavePriority(set->nodesel) )
               set->nodesel = set->nodesels[i];
         }
      }
      else
      {
         for( i = 1; i < set->nnodesels; ++i )
         {
            if( SCIPnodeselGetStdPriority(set->nodesels[i]) > SCIPnodeselGetStdPriority(set->nodesel) )
               set->nodesel = set->nodesels[i];
         }
      }
   }
   
   return set->nodesel;
}

/** inserts branching rule in branching rule list */
RETCODE SCIPsetIncludeBranchrule(
   SET*             set,                /**< global SCIP settings */
   BRANCHRULE*      branchrule          /**< branching rule */
   )
{
   assert(set != NULL);
   assert(branchrule != NULL);
   assert(!SCIPbranchruleIsInitialized(branchrule));

   if( set->nbranchrules >= set->branchrulessize )
   {
      set->branchrulessize = SCIPsetCalcMemGrowSize(set, set->nbranchrules+1);
      ALLOC_OKAY( reallocMemoryArray(&set->branchrules, set->branchrulessize) );
   }
   assert(set->nbranchrules < set->branchrulessize);

   set->branchrules[set->nbranchrules] = branchrule;
   set->nbranchrules++;
   set->branchrulessorted = FALSE;

   return SCIP_OKAY;
}   

/** returns the branching rule of the given name, or NULL if not existing */
BRANCHRULE* SCIPsetFindBranchrule(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of event handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->nbranchrules; ++i )
   {
      if( strcmp(SCIPbranchruleGetName(set->branchrules[i]), name) == 0 )
         return set->branchrules[i];
   }

   return NULL;
}

/** sorts branching rules by priorities */
void SCIPsetSortBranchrules(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   if( !set->branchrulessorted )
   {
      SCIPbsortPtr((void**)set->branchrules, set->nbranchrules, SCIPbranchruleComp);
      set->branchrulessorted = TRUE;
   }
}

/** inserts display column in display column list */
RETCODE SCIPsetIncludeDisp(
   SET*             set,                /**< global SCIP settings */
   DISP*            disp                /**< display column */
   )
{
   int i;

   assert(set != NULL);
   assert(disp != NULL);
   assert(!SCIPdispIsInitialized(disp));

   if( set->ndisps >= set->dispssize )
   {
      set->dispssize = SCIPsetCalcMemGrowSize(set, set->ndisps+1);
      ALLOC_OKAY( reallocMemoryArray(&set->disps, set->dispssize) );
   }
   assert(set->ndisps < set->dispssize);

   for( i = set->ndisps; i > 0 && SCIPdispGetPosition(disp) < SCIPdispGetPosition(set->disps[i-1]); --i )
   {
      set->disps[i] = set->disps[i-1];
   }
   set->disps[i] = disp;
   set->ndisps++;

   return SCIP_OKAY;
}   

/** returns the display column of the given name, or NULL if not existing */
DISP* SCIPsetFindDisp(
   SET*             set,                /**< global SCIP settings */
   const char*      name                /**< name of event handler */
   )
{
   int i;

   assert(set != NULL);
   assert(name != NULL);

   for( i = 0; i < set->ndisps; ++i )
   {
      if( strcmp(SCIPdispGetName(set->disps[i]), name) == 0 )
         return set->disps[i];
   }

   return NULL;
}

/** initializes all user callback functions */
RETCODE SCIPsetInitCallbacks(
   SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(set != NULL);

   /* active variable pricers */
   SCIPsetSortPricers(set);
   for( i = 0; i < set->nactivepricers; ++i )
   {
      CHECK_OKAY( SCIPpricerInit(set->pricers[i], set->scip) );
   }

   /* constraint handlers */
   for( i = 0; i < set->nconshdlrs; ++i )
   {
      CHECK_OKAY( SCIPconshdlrInit(set->conshdlrs[i], set->scip) );
   }

   /* conflict handlers */
   for( i = 0; i < set->nconflicthdlrs; ++i )
   {
      CHECK_OKAY( SCIPconflicthdlrInit(set->conflicthdlrs[i], set->scip) );
   }

   /* presolvers */
   for( i = 0; i < set->npresols; ++i )
   {
      CHECK_OKAY( SCIPpresolInit(set->presols[i], set->scip) );
   }

   /* separators */
   for( i = 0; i < set->nsepas; ++i )
   {
      CHECK_OKAY( SCIPsepaInit(set->sepas[i], set->scip) );
   }

   /* propagators */
   for( i = 0; i < set->nprops; ++i )
   {
      CHECK_OKAY( SCIPpropInit(set->props[i], set->scip) );
   }

   /* primal heuristics */
   for( i = 0; i < set->nheurs; ++i )
   {
      CHECK_OKAY( SCIPheurInit(set->heurs[i], set->scip) );
   }

   /* event handlers */
   for( i = 0; i < set->neventhdlrs; ++i )
   {
      CHECK_OKAY( SCIPeventhdlrInit(set->eventhdlrs[i], set->scip) );
   }

   /* node selectors */
   for( i = 0; i < set->nnodesels; ++i )
   {
      CHECK_OKAY( SCIPnodeselInit(set->nodesels[i], set->scip) );
   }

   /* branching rules */
   for( i = 0; i < set->nbranchrules; ++i )
   {
      CHECK_OKAY( SCIPbranchruleInit(set->branchrules[i], set->scip) );
   }

   /* display columns */
   for( i = 0; i < set->ndisps; ++i )
   {
      CHECK_OKAY( SCIPdispInit(set->disps[i], set->scip) );
   }
   CHECK_OKAY( SCIPdispAutoActivate(set) );

   return SCIP_OKAY;
}

/** calls exit methods of all user callback functions */
RETCODE SCIPsetExitCallbacks(
   SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(set != NULL);

   /* active variable pricers */
   SCIPsetSortPricers(set);
   for( i = 0; i < set->nactivepricers; ++i )
   {
      CHECK_OKAY( SCIPpricerExit(set->pricers[i], set->scip) );
   }

   /* constraint handlers */
   for( i = 0; i < set->nconshdlrs; ++i )
   {
      CHECK_OKAY( SCIPconshdlrExit(set->conshdlrs[i], set->scip) );
   }

   /* conflict handlers */
   for( i = 0; i < set->nconflicthdlrs; ++i )
   {
      CHECK_OKAY( SCIPconflicthdlrExit(set->conflicthdlrs[i], set->scip) );
   }

   /* presolvers */
   for( i = 0; i < set->npresols; ++i )
   {
      CHECK_OKAY( SCIPpresolExit(set->presols[i], set->scip) );
   }

   /* separators */
   for( i = 0; i < set->nsepas; ++i )
   {
      CHECK_OKAY( SCIPsepaExit(set->sepas[i], set->scip) );
   }

   /* propagators */
   for( i = 0; i < set->nprops; ++i )
   {
      CHECK_OKAY( SCIPpropExit(set->props[i], set->scip) );
   }

   /* primal heuristics */
   for( i = 0; i < set->nheurs; ++i )
   {
      CHECK_OKAY( SCIPheurExit(set->heurs[i], set->scip) );
   }

   /* event handlers */
   for( i = 0; i < set->neventhdlrs; ++i )
   {
      CHECK_OKAY( SCIPeventhdlrExit(set->eventhdlrs[i], set->scip) );
   }

   /* node selectors */
   for( i = 0; i < set->nnodesels; ++i )
   {
      CHECK_OKAY( SCIPnodeselExit(set->nodesels[i], set->scip) );
   }

   /* branchruleing rules */
   for( i = 0; i < set->nbranchrules; ++i )
   {
      CHECK_OKAY( SCIPbranchruleExit(set->branchrules[i], set->scip) );
   }

   /* display columns */
   for( i = 0; i < set->ndisps; ++i )
   {
      CHECK_OKAY( SCIPdispExit(set->disps[i], set->scip) );
   }

   return SCIP_OKAY;
}

/** calculate memory size for dynamically allocated arrays */
int SCIPsetCalcMemGrowSize(
   SET*             set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   return calcGrowSize(set->mem_arraygrowinit, set->mem_arraygrowfac, num);
}

/** calculate memory size for tree array */
int SCIPsetCalcTreeGrowSize(
   SET*             set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   return calcGrowSize(set->mem_treegrowinit, set->mem_treegrowfac, num);
}

/** calculate memory size for path array */
int SCIPsetCalcPathGrowSize(
   SET*             set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   return calcGrowSize(set->mem_pathgrowinit, set->mem_pathgrowfac, num);
}

/** sets verbosity level for message output */
RETCODE SCIPsetSetVerbLevel(
   SET*             set,                /**< global SCIP settings */
   VERBLEVEL        verblevel           /**< verbosity level for message output */
   )
{
   assert(set != NULL);

   if( verblevel > SCIP_VERBLEVEL_FULL )
   {
      errorMessage("invalid verbosity level <%d>, maximum is <%d>\n", verblevel, SCIP_VERBLEVEL_FULL);
      return SCIP_INVALIDCALL;
   }
   
   set->disp_verblevel = verblevel;

   return SCIP_OKAY;
}

/** sets LP feasibility tolerance */
RETCODE SCIPsetSetFeastol(
   SET*             set,                /**< global SCIP settings */
   Real             feastol             /**< new feasibility tolerance */
   )
{
   assert(set != NULL);

   set->num_feastol = feastol;

   return SCIP_OKAY;
}

/** sets LP feasibility tolerance for reduced costs */
RETCODE SCIPsetSetDualfeastol(
   SET*             set,                /**< global SCIP settings */
   Real             dualfeastol         /**< new reduced costs feasibility tolerance */
   )
{
   assert(set != NULL);

   set->num_dualfeastol = dualfeastol;

   return SCIP_OKAY;
}

/** returns the maximal number of variables priced into the LP per round */
int SCIPsetGetPriceMaxvars(
   SET*             set,                /**< global SCIP settings */
   Bool             root                /**< are we at the root node? */
   )
{
   assert(set != NULL);

   if( root )
      return set->price_maxvarsroot;
   else
      return set->price_maxvars;
}

/** returns the maximal number of cuts separated per round */
int SCIPsetGetSepaMaxcuts(
   SET*             set,                /**< global SCIP settings */
   Bool             root                /**< are we at the root node? */
   )
{
   assert(set != NULL);

   if( root )
      return set->sepa_maxcutsroot;
   else
      return set->sepa_maxcuts;
}

   

#ifndef NDEBUG

/* In debug mode, the following methods are implemented as function calls to ensure
 * type validity.
 */

/** returns value treated as infinity */
Real SCIPsetInfinity(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_infinity;
}

/** returns value treated as zero */
Real SCIPsetEpsilon(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_epsilon;
}

/** returns value treated as zero for sums of floating point values */
Real SCIPsetSumepsilon(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_sumepsilon;
}

/** returns feasibility tolerance for constraints */
Real SCIPsetFeastol(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_feastol;
}

/** returns feasibility tolerance for reduced costs */
Real SCIPsetDualfeastol(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_dualfeastol;
}

/** returns minimal variable distance value to use for pseudo cost updates */
Real SCIPsetPseudocosteps(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_pseudocosteps;
}

/** returns minimal minimal objective distance value to use for pseudo cost updates */
Real SCIPsetPseudocostdelta(
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(set != NULL);

   return set->num_pseudocostdelta;
}

/** returns the relative difference: (val1-val2)/max(|val1|,|val2|,1.0) */
Real SCIPsetRelDiff(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real absval1;
   Real absval2;
   Real quot;

   assert(set != NULL);

   absval1 = REALABS(val1);
   absval2 = REALABS(val2);
   quot = MAX3(1.0, absval1, absval2);
   
   return (val1-val2)/quot;
}

/** checks, if values are in range of epsilon */
Bool SCIPsetIsEQ(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSEQ(val1, val2, set->num_epsilon);
}

/** checks, if val1 is (more than epsilon) lower than val2 */
Bool SCIPsetIsLT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSLT(val1, val2, set->num_epsilon);
}

/** checks, if val1 is not (more than epsilon) greater than val2 */
Bool SCIPsetIsLE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSLE(val1, val2, set->num_epsilon);
}

/** checks, if val1 is (more than epsilon) greater than val2 */
Bool SCIPsetIsGT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSGT(val1, val2, set->num_epsilon);
}

/** checks, if val1 is not (more than epsilon) lower than val2 */
Bool SCIPsetIsGE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSGE(val1, val2, set->num_epsilon);
}

/** checks, if value is in range epsilon of 0.0 */
Bool SCIPsetIsZero(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSZ(val, set->num_epsilon);
}

/** checks, if value is greater than epsilon */
Bool SCIPsetIsPositive(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSP(val, set->num_epsilon);
}

/** checks, if value is lower than -epsilon */
Bool SCIPsetIsNegative(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSN(val, set->num_epsilon);
}

/** checks, if values are in range of sumepsilon */
Bool SCIPsetIsSumEQ(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSEQ(val1, val2, set->num_sumepsilon);
}

/** checks, if val1 is (more than sumepsilon) lower than val2 */
Bool SCIPsetIsSumLT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSLT(val1, val2, set->num_sumepsilon);
}

/** checks, if val1 is not (more than sumepsilon) greater than val2 */
Bool SCIPsetIsSumLE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSLE(val1, val2, set->num_sumepsilon);
}

/** checks, if val1 is (more than sumepsilon) greater than val2 */
Bool SCIPsetIsSumGT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSGT(val1, val2, set->num_sumepsilon);
}

/** checks, if val1 is not (more than sumepsilon) lower than val2 */
Bool SCIPsetIsSumGE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   assert(set != NULL);

   return EPSGE(val1, val2, set->num_sumepsilon);
}

/** checks, if value is in range sumepsilon of 0.0 */
Bool SCIPsetIsSumZero(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSZ(val, set->num_sumepsilon);
}

/** checks, if value is greater than sumepsilon */
Bool SCIPsetIsSumPositive(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSP(val, set->num_sumepsilon);
}

/** checks, if value is lower than -sumepsilon */
Bool SCIPsetIsSumNegative(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSN(val, set->num_sumepsilon);
}

/** checks, if relative difference of values is in range of feastol */
Bool SCIPsetIsFeasEQ(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSZ(diff, set->num_feastol);
}

/** checks, if relative difference of val1 and val2 is lower than feastol */
Bool SCIPsetIsFeasLT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSN(diff, set->num_feastol);
}

/** checks, if relative difference of val1 and val2 is not greater than feastol */
Bool SCIPsetIsFeasLE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSP(diff, set->num_feastol);
}

/** checks, if relative difference of val1 and val2 is greater than feastol */
Bool SCIPsetIsFeasGT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSP(diff, set->num_feastol);
}

/** checks, if relative difference of val1 and val2 is not lower than -feastol */
Bool SCIPsetIsFeasGE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSN(diff, set->num_feastol);
}

/** checks, if value is in range feasibility tolerance of 0.0 */
Bool SCIPsetIsFeasZero(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSZ(val, set->num_feastol);
}

/** checks, if value is greater than feasibility tolerance */
Bool SCIPsetIsFeasPositive(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSP(val, set->num_feastol);
}

/** checks, if value is lower than -feasibility tolerance */
Bool SCIPsetIsFeasNegative(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSN(val, set->num_feastol);
}

/** checks, if the first given lower bound is tighter (w.r.t. bound strengthening epsilon) than the second one */
Bool SCIPsetIsLbBetter(
   SET*             set,                /**< global SCIP settings */
   Real             lb1,                /**< first lower bound to compare */
   Real             lb2                 /**< second lower bound to compare */
   )
{
   assert(set != NULL);

   return EPSGT(lb1, lb2, set->num_boundstreps);
}

/** checks, if the first given upper bound is tighter (w.r.t. bound strengthening epsilon) than the second one */
Bool SCIPsetIsUbBetter(
   SET*             set,                /**< global SCIP settings */
   Real             ub1,                /**< first upper bound to compare */
   Real             ub2                 /**< second upper bound to compare */
   )
{
   assert(set != NULL);

   return EPSLT(ub1, ub2, set->num_boundstreps);
}

/** checks, if the given cut's efficacy is larger than the minimal cut efficacy */
Bool SCIPsetIsEfficacious(
   SET*             set,                /**< global SCIP settings */
   Bool             root,               /**< should the root's minimal cut efficacy be used? */
   Real             efficacy            /**< efficacy of the cut */
   )
{
   assert(set != NULL);

   if( root )
      return EPSP(efficacy, set->sepa_minefficacyroot);
   else
      return EPSP(efficacy, set->sepa_minefficacy);
}

/** checks, if relative difference of values is in range of epsilon */
Bool SCIPsetIsRelEQ(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSZ(diff, set->num_epsilon);
}

/** checks, if relative difference of val1 and val2 is lower than epsilon */
Bool SCIPsetIsRelLT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSN(diff, set->num_epsilon);
}

/** checks, if relative difference of val1 and val2 is not greater than epsilon */
Bool SCIPsetIsRelLE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSP(diff, set->num_epsilon);
}

/** checks, if relative difference of val1 and val2 is greater than epsilon */
Bool SCIPsetIsRelGT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSP(diff, set->num_epsilon);
}

/** checks, if relative difference of val1 and val2 is not lower than -epsilon */
Bool SCIPsetIsRelGE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSN(diff, set->num_epsilon);
}

/** checks, if relative difference of values is in range of sumepsilon */
Bool SCIPsetIsSumRelEQ(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSZ(diff, set->num_sumepsilon);
}

/** checks, if relative difference of val1 and val2 is lower than sumepsilon */
Bool SCIPsetIsSumRelLT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSN(diff, set->num_sumepsilon);
}

/** checks, if relative difference of val1 and val2 is not greater than sumepsilon */
Bool SCIPsetIsSumRelLE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSP(diff, set->num_sumepsilon);
}

/** checks, if relative difference of val1 and val2 is greater than sumepsilon */
Bool SCIPsetIsSumRelGT(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return EPSP(diff, set->num_sumepsilon);
}

/** checks, if relative difference of val1 and val2 is not lower than -sumepsilon */
Bool SCIPsetIsSumRelGE(
   SET*             set,                /**< global SCIP settings */
   Real             val1,               /**< first value to be compared */
   Real             val2                /**< second value to be compared */
   )
{
   Real diff;

   assert(set != NULL);

   diff = SCIPsetRelDiff(set, val1, val2);

   return !EPSN(diff, set->num_sumepsilon);
}

/** checks, if value is (positive) infinite */
Bool SCIPsetIsInfinity(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against infinity */
   )
{
   assert(set != NULL);

   return (val >= set->num_infinity);
}

/** checks, if value is non-negative within the LP feasibility bounds */
Bool SCIPsetIsFeasible(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return (val >= -set->num_feastol);
}

/** checks, if value is integral within the LP feasibility bounds */
Bool SCIPsetIsIntegral(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSISINT(val, set->num_feastol);
}

/** checks, if given fractional part is smaller than feastol */
Bool SCIPsetIsFracIntegral(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);
   assert(SCIPsetIsGE(set, val, -set->num_feastol));
   assert(SCIPsetIsLE(set, val, 1.0+set->num_feastol));

   return (val <= set->num_feastol);
}

/** rounds value + feasibility tolerance down to the next integer */
Real SCIPsetFloor(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSFLOOR(val, set->num_feastol);
}

/** rounds value - feasibility tolerance up to the next integer */
Real SCIPsetCeil(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to be compared against zero */
   )
{
   assert(set != NULL);

   return EPSCEIL(val, set->num_feastol);
}

/** returns fractional part of value, i.e. x - floor(x) */
Real SCIPsetFrac(
   SET*             set,                /**< global SCIP settings */
   Real             val                 /**< value to return fractional part for */
   )
{
   assert(set != NULL);

   return EPSFRAC(val, set->num_feastol);
}

#endif
