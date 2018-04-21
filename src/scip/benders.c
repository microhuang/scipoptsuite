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

/**@file   benders.c
 * @brief  methods for Benders' decomposition
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
//#define SCIP_DEBUG
//#define SCIP_MOREDEBUG
#include <assert.h>
#include <string.h>

#include "scip/def.h"
#include "scip/set.h"
#include "scip/clock.h"
#include "scip/paramset.h"
#include "scip/lp.h"
#include "scip/prob.h"
#include "scip/pricestore.h"
#include "scip/scip.h"
#include "scip/benders.h"
#include "scip/pub_message.h"
#include "scip/pub_misc.h"
#include "scip/cons_linear.h"

#include "scip/struct_benders.h"
#include "scip/struct_benderscut.h"

#include "scip/benderscut.h"
#include "scip/misc_benders.h"

/* Defaults for parameters */
#define SCIP_DEFAULT_TRANSFERCUTS          TRUE  /** Should Benders' cuts generated in LNS heuristics be transferred to the main SCIP instance? */
#define SCIP_DEFAULT_CUTSASCONSS           TRUE  /** Should the transferred cuts be added as constraints? */
#define SCIP_DEFAULT_MIPCHECKFREQ             5  /** the number of iterations that the MIP is checked, -1 for always. */
#define SCIP_DEFAULT_LNSCHECK              TRUE  /** should the Benders' decomposition be used in LNS heuristics */
#define SCIP_DEFAULT_LNSMAXDEPTH             -1  /** the maximum depth at which the LNS check is performed */
#define SCIP_DEFAULT_SUBPROBFRAC            1.0  /** the fraction of subproblems that are solved in each iteration */

#define AUXILIARYVAR_NAME     "##bendersauxiliaryvar" /** the name for the Benders' auxiliary variables in the master problem */

/* event handler properties */
#define NODEFOCUS_EVENTHDLR_NAME         "bendersnodefocus"
#define NODEFOCUS_EVENTHDLR_DESC         "node focus event handler for Benders' decomposition"
#define MIPNODEFOCUS_EVENTHDLR_NAME      "bendersmipsolvenodefocus"
#define MIPNODEFOCUS_EVENTHDLR_DESC      "node focus event handler for the MIP solve method for Benders' decomposition"
#define UPPERBOUND_EVENTHDLR_NAME        "bendersupperbound"
#define UPPERBOUND_EVENTHDLR_DESC        "found solution event handler to terminate subproblem solve for a given upper bound"


struct SCIP_EventhdlrData
{
   int                   filterpos;          /**< the event filter entry */
   int                   numruns;            /**< the number of times that the problem has been solved */
   SCIP_Real             upperbound;         /**< an upper bound for the problem */
   SCIP_Bool             solvemip;           /**< is the event called from a MIP subproblem solve*/
};


/* ---------------- Local methods for event handlers ---------------- */
/** init method for the event handlers */
static
SCIP_RETCODE initEventhandler(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr           /**< the event handlers data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   eventhdlrdata->filterpos = -1;
   eventhdlrdata->numruns = 0;
   eventhdlrdata->upperbound = -SCIPinfinity(scip);
   eventhdlrdata->solvemip = FALSE;

   return SCIP_OKAY;
}


/** initsol method for the event handlers */
static
SCIP_RETCODE initsolEventhandler(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< the event handlers data structure */
   SCIP_EVENTTYPE        eventtype           /**< event type mask to select events to catch */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   SCIP_CALL(SCIPcatchEvent(scip, eventtype, eventhdlr, NULL, &eventhdlrdata->filterpos));

   return SCIP_OKAY;
}

/** the exit method for the event handlers */
static
SCIP_RETCODE exitsolEventhandler(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< the event handlers data structure */
   SCIP_EVENTTYPE        eventtype           /**< event type mask to select events to catch */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   if( eventhdlrdata->filterpos >= 0 )
   {
      SCIP_CALL(SCIPdropEvent(scip, eventtype, eventhdlr, NULL, eventhdlrdata->filterpos));
      eventhdlrdata->filterpos = -1;
   }

   return SCIP_OKAY;
}

/** free method for the event handler */
static
SCIP_RETCODE freeEventhandler(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr           /**< the event handlers data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   SCIPfreeBlockMemory(scip, &eventhdlrdata);

   SCIPeventhdlrSetData(eventhdlr, NULL);

   return SCIP_OKAY;
}



/* ---------------- Callback methods of node focus event handler ---------------- */

/** exec the event handler */
static
SCIP_DECL_EVENTEXEC(eventExecBendersNodefocus)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), NODEFOCUS_EVENTHDLR_NAME) == 0);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   /* sending an interrupt solve signal to return the control back to the Benders' decomposition plugin.
    * This will ensure the SCIP stage is SCIP_STAGE_SOLVING, allowing the use of probing mode. */
   SCIP_CALL( SCIPinterruptSolve(scip) );

   SCIP_CALL(SCIPdropEvent(scip, SCIP_EVENTTYPE_NODEFOCUSED, eventhdlr, NULL, eventhdlrdata->filterpos));
   eventhdlrdata->filterpos = -1;

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitBendersNodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), NODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}

/** solving process initialization method of event handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolBendersNodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), NODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_NODEFOCUSED) );

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolBendersNodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), NODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( exitsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_NODEFOCUSED) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTFREE(eventFreeBendersNodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), NODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( freeEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}


/* ---------------- Callback methods of MIP solve node focus event handler ---------------- */

/** exec the event handler */
static
SCIP_DECL_EVENTEXEC(eventExecBendersMipnodefocus)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), MIPNODEFOCUS_EVENTHDLR_NAME) == 0);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

   /* interrupting the solve so that the control is returned back to the Benders' core. */
   if( eventhdlrdata->numruns == 0 && !eventhdlrdata->solvemip )
      SCIP_CALL( SCIPinterruptSolve(scip) );

   SCIP_CALL(SCIPdropEvent(scip, SCIP_EVENTTYPE_NODEFOCUSED, eventhdlr, NULL, eventhdlrdata->filterpos));
   eventhdlrdata->filterpos = -1;

   eventhdlrdata->numruns++;

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitBendersMipnodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), MIPNODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}

/** solving process initialization method of event handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolBendersMipnodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), MIPNODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_NODEFOCUSED) );

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolBendersMipnodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), MIPNODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( exitsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_NODEFOCUSED) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTFREE(eventFreeBendersMipnodefocus)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), MIPNODEFOCUS_EVENTHDLR_NAME) == 0);

   SCIP_CALL( freeEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}

/* ---------------- Callback methods of solution found event handler ---------------- */

/** exec the event handler */
static
SCIP_DECL_EVENTEXEC(eventExecBendersUpperbound)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_SOL* bestsol;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), UPPERBOUND_EVENTHDLR_NAME) == 0);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   bestsol = SCIPgetBestSol(scip);

   if( SCIPisLT(scip, SCIPgetSolOrigObj(scip, bestsol), eventhdlrdata->upperbound) )
      SCIP_CALL( SCIPinterruptSolve(scip) );

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitBendersUpperbound)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), UPPERBOUND_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}

/** solving process initialization method of event handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_EVENTINITSOL(eventInitsolBendersUpperbound)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), UPPERBOUND_EVENTHDLR_NAME) == 0);

   SCIP_CALL( initsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_BESTSOLFOUND) );

   return SCIP_OKAY;
}

/** solving process deinitialization method of event handler (called before branch and bound process data is freed) */
static
SCIP_DECL_EVENTEXITSOL(eventExitsolBendersUpperbound)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), UPPERBOUND_EVENTHDLR_NAME) == 0);

   SCIP_CALL( exitsolEventhandler(scip, eventhdlr, SCIP_EVENTTYPE_BESTSOLFOUND) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTFREE(eventFreeBendersUpperbound)
{
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), UPPERBOUND_EVENTHDLR_NAME) == 0);

   SCIP_CALL( freeEventhandler(scip, eventhdlr) );

   return SCIP_OKAY;
}

/** updates the upper bound in the event handler data */
static
SCIP_RETCODE updateEventhdlrUpperbound(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Real             upperbound          /**< the upper bound value */
   )
{
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);

   eventhdlr = SCIPfindEventhdlr(SCIPbendersSubproblem(benders, probnumber), UPPERBOUND_EVENTHDLR_NAME);
   assert(eventhdlr != NULL);

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   assert(eventhdlrdata != NULL);

   eventhdlrdata->upperbound = upperbound;

   return SCIP_OKAY;
}

/* Local methods */

/** A workaround for GCG. This is a temp vardata that is set for the auxiliary variables */
struct SCIP_VarData
{
   int                   vartype;             /**< the variable type. In GCG this indicates whether the variable is a
                                                   master problem or subproblem variable. */
};


/** adds the auxiliary variables to the Benders' decomposition master problem */
static
SCIP_RETCODE addAuxiliaryVariablesToMaster(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< benders */
   SCIP_Real*            lowerbound          /**< array of lower bounds for the auxiliary variables */
   )
{
   SCIP_BENDERS* topbenders;        /* the highest priority Benders' decomposition */
   SCIP_VAR* auxiliaryvar;
   SCIP_VARDATA* vardata;
   char varname[SCIP_MAXSTRLEN];    /* the name of the auxiliary variable */
   SCIP_Bool shareauxvars;
   int i;

   /* this is a workaround for GCG. GCG expects that the variable has vardata when added. So a dummy vardata is created */
   SCIP_CALL( SCIPallocBlockMemory(scip, &vardata) );
   vardata->vartype = -1;

   /* getting the highest priority Benders' decomposition */
   topbenders = SCIPgetBenders(scip)[0];

   /* if the current Benders is the highest priority Benders, then we need to create the auxiliary variables.
    * Otherwise, if the shareauxvars flag is set, then the auxiliar variables from the highest priority Benders' are
    * stored with this Benders. */
   shareauxvars = FALSE;
   if( topbenders != benders && SCIPbendersShareAuxVars(benders) )
      shareauxvars = TRUE;

   for( i = 0; i < SCIPbendersGetNSubproblems(benders); i++ )
   {
      /* if the auxiliary variables are shared, then a pointer to the variable is retrieved from topbenders,
       * otherwise the auxiliaryvariable is created. */
      if( shareauxvars )
      {
         auxiliaryvar = SCIPbendersGetAuxiliaryVar(topbenders, i);

         SCIP_CALL( SCIPcaptureVar(scip, auxiliaryvar) );
      }
      else
      {
         (void) SCIPsnprintf(varname, SCIP_MAXSTRLEN, "%s_%d_%s", AUXILIARYVAR_NAME, i, SCIPbendersGetName(benders) );
         SCIP_CALL( SCIPcreateVarBasic(scip, &auxiliaryvar, varname, lowerbound[i], SCIPinfinity(scip), 1.0,
               SCIP_VARTYPE_CONTINUOUS) );

         SCIPvarSetData(auxiliaryvar, vardata);

         SCIP_CALL( SCIPaddVar(scip, auxiliaryvar) );
      }

      benders->auxiliaryvars[i] = auxiliaryvar;
   }

   SCIPfreeBlockMemory(scip, &vardata);

   return SCIP_OKAY;
}

/** assigns the copied auxiliary variables in the target scip to the target benders data */
static
SCIP_RETCODE assignAuxiliaryVariables(
   SCIP*                 scip,               /**< SCIP data structure, the target scip */
   SCIP_BENDERS*         benders             /**< benders */
   )
{
   SCIP_BENDERS* topbenders;        /* the highest priority Benders' decomposition */
   SCIP_VAR* targetvar;
   SCIP_VARDATA* vardata;
   char varname[SCIP_MAXSTRLEN];    /* the name of the auxiliary variable */
   SCIP_Bool shareauxvars;
   int i;

   assert(scip != NULL);
   assert(benders != NULL);

   /* this is a workaround for GCG. GCG expects that the variable has vardata when added. So a dummy vardata is created */
   SCIP_CALL( SCIPallocBlockMemory(scip, &vardata) );
   vardata->vartype = -1;

   /* getting the highest priority Benders' decomposition */
   topbenders = SCIPgetBenders(scip)[0];

   /* if the auxiliary variable are shared, then the variable name will have a suffix of the highest priority Benders'
    * name. So the shareauxvars flag indicates how to search for the auxiliary variables */
   shareauxvars = FALSE;
   if( topbenders != benders && SCIPbendersShareAuxVars(benders) )
      shareauxvars = TRUE;

   for( i = 0; i < SCIPbendersGetNSubproblems(benders); i++ )
   {
      if( shareauxvars )
         (void) SCIPsnprintf(varname, SCIP_MAXSTRLEN, "%s_%d_%s", AUXILIARYVAR_NAME, i, SCIPbendersGetName(topbenders));
      else
         (void) SCIPsnprintf(varname, SCIP_MAXSTRLEN, "%s_%d_%s", AUXILIARYVAR_NAME, i, SCIPbendersGetName(benders));

      /* finding the variable in the copied problem that has the same name as the auxiliary variable */
      targetvar = SCIPfindVar(scip, varname);
      assert(targetvar != NULL);

      SCIPvarSetData(targetvar, vardata);

      benders->auxiliaryvars[i] = SCIPvarGetTransVar(targetvar);

      SCIP_CALL( SCIPcaptureVar(scip, benders->auxiliaryvars[i]) );
   }

   SCIPfreeBlockMemory(scip, &vardata);

   return SCIP_OKAY;
}

/* sets the subproblem objective value array to -infinity */
static
void resetSubproblemObjectiveValue(
   SCIP_BENDERS*         benders             /**< the Benders' decomposition structure */
   )
{
   SCIP* subproblem;
   int nsubproblems;
   int i;

   assert(benders != NULL);

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   for( i = 0; i < nsubproblems; i++ )
   {
      subproblem = SCIPbendersSubproblem(benders, i);
      SCIPbendersSetSubprobObjval(benders, i, SCIPinfinity(subproblem));
   }
}


/** compares two benders w. r. to their priority */
SCIP_DECL_SORTPTRCOMP(SCIPbendersComp)
{  /*lint --e{715}*/
   return ((SCIP_BENDERS*)elem2)->priority - ((SCIP_BENDERS*)elem1)->priority;
}

/** comparison method for sorting benders w.r.t. to their name */
SCIP_DECL_SORTPTRCOMP(SCIPbendersCompName)
{
   return strcmp(SCIPbendersGetName((SCIP_BENDERS*)elem1), SCIPbendersGetName((SCIP_BENDERS*)elem2));
}

/** method to call, when the priority of a benders was changed */
static
SCIP_DECL_PARAMCHGD(paramChgdBendersPriority)
{  /*lint --e{715}*/
   SCIP_PARAMDATA* paramdata;

   paramdata = SCIPparamGetData(param);
   assert(paramdata != NULL);

   /* use SCIPsetBendersPriority() to mark the benderss unsorted */
   SCIPsetBendersPriority(scip, (SCIP_BENDERS*)paramdata, SCIPparamGetInt(param)); /*lint !e740*/

   return SCIP_OKAY;
}

/** copies the given benders to a new scip */
SCIP_RETCODE SCIPbendersCopyInclude(
   SCIP_BENDERS*         benders,            /**< benders */
   SCIP_SET*             sourceset,          /**< SCIP_SET of SCIP to copy from */
   SCIP_SET*             targetset,          /**< SCIP_SET of SCIP to copy to */
   SCIP_Bool*            valid               /**< was the copying process valid? */
   )
{
   SCIP_BENDERS* targetbenders;  /* the copy of the Benders' decomposition struct in the target set */
   int i;

   assert(benders != NULL);
   assert(targetset != NULL);
   assert(valid != NULL);
   assert(targetset->scip != NULL);

   (*valid) = FALSE;

   if( benders->benderscopy != NULL && targetset->benders_copybenders )
   {
      SCIPsetDebugMsg(targetset, "including benders %s in subscip %p\n", SCIPbendersGetName(benders), (void*)targetset->scip);
      SCIP_CALL( benders->benderscopy(targetset->scip, benders) );

      /* if the Benders' decomposition is active, then copy is not valid. */
      (*valid) = !SCIPbendersIsActive(benders);

      /* copying the Benders' cuts */
      targetbenders = SCIPsetFindBenders(targetset, SCIPbendersGetName(benders));

      /* storing the pointer to the source scip instance */
      targetbenders->sourcescip = sourceset->scip;

      /* the flag is set to indicate that the Benders' decomposition is a copy */
      targetbenders->iscopy = TRUE;

      /* calling the copy method for the Benders' cuts */
      SCIPbendersSortBenderscuts(benders);
      for( i = 0; i < benders->nbenderscuts; i++ )
      {
         SCIP_CALL( SCIPbenderscutCopyInclude(targetbenders, benders->benderscuts[i], targetset) );
      }
   }

   return SCIP_OKAY;
}

/** creates a Benders' decomposition structure
 *  To use the Benders' decomposition for solving a problem, it first has to be activated with a call to SCIPactivateBenders().
 */
SCIP_RETCODE SCIPbendersCreate(
   SCIP_BENDERS**        benders,            /**< pointer to Benders' decomposition data structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler */
   BMS_BLKMEM*           blkmem,             /**< block memory for parameter settings */
   const char*           name,               /**< name of Benders' decomposition */
   const char*           desc,               /**< description of Benders' decomposition */
   int                   priority,           /**< priority of the Benders' decomposition */
   SCIP_Bool             cutlp,              /**< should Benders' cuts be generated for LP solutions */
   SCIP_Bool             cutpseudo,          /**< should Benders' cuts be generated for pseudo solutions */
   SCIP_Bool             cutrelax,           /**< should Benders' cuts be generated for relaxation solutions */
   SCIP_Bool             shareauxvars,       /**< should this Benders' use the highest priority Benders aux vars */
   SCIP_DECL_BENDERSCOPY ((*benderscopy)),   /**< copy method of benders or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSFREE ((*bendersfree)),   /**< destructor of Benders' decomposition */
   SCIP_DECL_BENDERSINIT ((*bendersinit)),   /**< initialize Benders' decomposition */
   SCIP_DECL_BENDERSEXIT ((*bendersexit)),   /**< deinitialize Benders' decomposition */
   SCIP_DECL_BENDERSINITPRE((*bendersinitpre)),/**< presolving initialization method for Benders' decomposition */
   SCIP_DECL_BENDERSEXITPRE((*bendersexitpre)),/**< presolving deinitialization method for Benders' decomposition */
   SCIP_DECL_BENDERSINITSOL((*bendersinitsol)),/**< solving process initialization method of Benders' decomposition */
   SCIP_DECL_BENDERSEXITSOL((*bendersexitsol)),/**< solving process deinitialization method of Benders' decomposition */
   SCIP_DECL_BENDERSGETVAR((*bendersgetvar)),/**< returns the master variable for a given subproblem variable */
   SCIP_DECL_BENDERSCREATESUB((*benderscreatesub)),/**< creates a Benders' decomposition subproblem */
   SCIP_DECL_BENDERSPRESUBSOLVE((*benderspresubsolve)),/**< called prior to the subproblem solving loop */
   SCIP_DECL_BENDERSSOLVESUBCONVEX((*benderssolvesubconvex)),/**< the solving method for convex Benders' decomposition subproblems */
   SCIP_DECL_BENDERSSOLVESUB((*benderssolvesub)),/**< the solving method for the Benders' decomposition subproblems */
   SCIP_DECL_BENDERSPOSTSOLVE((*benderspostsolve)),/**< called after the subproblems are solved. */
   SCIP_DECL_BENDERSFREESUB((*bendersfreesub)),/**< the freeing method for the Benders' decomposition subproblems */
   SCIP_BENDERSDATA*     bendersdata         /**< Benders' decomposition data */
   )
{
   char paramname[SCIP_MAXSTRLEN];
   char paramdesc[SCIP_MAXSTRLEN];

   assert(benders != NULL);
   assert(name != NULL);
   assert(desc != NULL);

   /* Checking whether the benderssolvesub and the bendersfreesub are both implemented or both are not implemented */
   if( (benderssolvesubconvex == NULL && benderssolvesub == NULL && bendersfreesub != NULL)
      || ((benderssolvesubconvex != NULL || benderssolvesub != NULL) && bendersfreesub == NULL) )
   {
      SCIPerrorMessage("Benders' decomposition <%s> requires that if bendersFreesub%s is implemented, then at least "
         "one of bendersSolvesubconvex%s or bendersSolvesub%s are implemented.\n", name, name, name, name);
      return SCIP_INVALIDCALL;
   }

   SCIP_ALLOC( BMSallocMemory(benders) );
   BMSclearMemory(*benders);
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*benders)->name, name, strlen(name)+1) );
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*benders)->desc, desc, strlen(desc)+1) );
   (*benders)->priority = priority;
   (*benders)->cutlp = cutlp;
   (*benders)->cutpseudo = cutpseudo;
   (*benders)->cutrelax = cutrelax;
   (*benders)->shareauxvars = shareauxvars;
   (*benders)->benderscopy = benderscopy;
   (*benders)->bendersfree = bendersfree;
   (*benders)->bendersinit = bendersinit;
   (*benders)->bendersexit = bendersexit;
   (*benders)->bendersinitpre = bendersinitpre;
   (*benders)->bendersexitpre = bendersexitpre;
   (*benders)->bendersinitsol = bendersinitsol;
   (*benders)->bendersexitsol = bendersexitsol;
   (*benders)->bendersgetvar = bendersgetvar;
   (*benders)->benderscreatesub = benderscreatesub;
   (*benders)->benderspresubsolve = benderspresubsolve;
   (*benders)->benderssolvesubconvex = benderssolvesubconvex;
   (*benders)->benderssolvesub = benderssolvesub;
   (*benders)->benderspostsolve = benderspostsolve;
   (*benders)->bendersfreesub = bendersfreesub;
   (*benders)->bendersdata = bendersdata;
   SCIP_CALL( SCIPclockCreate(&(*benders)->setuptime, SCIP_CLOCKTYPE_DEFAULT) );
   SCIP_CALL( SCIPclockCreate(&(*benders)->bendersclock, SCIP_CLOCKTYPE_DEFAULT) );

   (*benders)->bestauxvarbound = -SCIPsetInfinity(set);
   (*benders)->bestsubprobbound = SCIPsetInfinity(set);

   /* add parameters */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/priority", name);
   (void) SCIPsnprintf(paramdesc, SCIP_MAXSTRLEN, "priority of benders <%s>", name);
   SCIP_CALL( SCIPsetAddIntParam(set, messagehdlr, blkmem, paramname, paramdesc,
                  &(*benders)->priority, FALSE, priority, INT_MIN/4, INT_MAX/4,
                  paramChgdBendersPriority, (SCIP_PARAMDATA*)(*benders)) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/cutlp", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "should Benders' cuts be generated for LP solutions?", &(*benders)->cutlp, FALSE, cutlp, NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/cutpseudo", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "should Benders' cuts be generated for pseudo solutions?", &(*benders)->cutpseudo, FALSE, cutpseudo, NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/cutrelax", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "should Benders' cuts be generated for relaxation solutions?", &(*benders)->cutrelax, FALSE, cutrelax, NULL, NULL) ); /*lint !e740*/

   /* These parameters are left for the user to decide in a settings file. This departs from the usual SCIP convention
    * where the settings available at the creation of the plugin can be set in the function call. */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/transfercuts", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "Should Benders' cuts from LNS heuristics be transferred to the main SCIP instance?", &(*benders)->transfercuts,
        FALSE, SCIP_DEFAULT_TRANSFERCUTS, NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/mipcheckfreq", name);
   SCIP_CALL( SCIPsetAddIntParam(set, messagehdlr, blkmem, paramname,
        "The frequency at which the MIP subproblems are checked, -1 for always", &(*benders)->mipcheckfreq, FALSE,
        SCIP_DEFAULT_MIPCHECKFREQ, -1, INT_MAX, NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/lnscheck", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "Should Benders' decomposition be used in LNS heurisics?", &(*benders)->lnscheck, FALSE, SCIP_DEFAULT_LNSCHECK,
        NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/lnsmaxdepth", name);
   SCIP_CALL( SCIPsetAddIntParam(set, messagehdlr, blkmem, paramname,
        "maximal depth level at which the LNS check is performed (-1: no limit)", &(*benders)->lnsmaxdepth, TRUE,
        SCIP_DEFAULT_LNSMAXDEPTH, -1, SCIP_MAXTREEDEPTH, NULL, NULL) );

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/cutsasconss", name);
   SCIP_CALL( SCIPsetAddBoolParam(set, messagehdlr, blkmem, paramname,
        "Should the transferred cuts be added as constraints?", &(*benders)->cutsasconss, FALSE,
        SCIP_DEFAULT_CUTSASCONSS, NULL, NULL) ); /*lint !e740*/

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/subprobfrac", name);
   SCIP_CALL( SCIPsetAddRealParam(set, messagehdlr, blkmem, paramname,
        "The fraction of subproblems that are solved in each iteration.", &(*benders)->subprobfrac, FALSE,
        SCIP_DEFAULT_SUBPROBFRAC, 0.0, 1.0, NULL, NULL) ); /*lint !e740*/

   return SCIP_OKAY;
}

/** calls destructor and frees memory of Benders' decomposition */
SCIP_RETCODE SCIPbendersFree(
   SCIP_BENDERS**        benders,            /**< pointer to Benders' decomposition data structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(benders != NULL);
   assert(*benders != NULL);
   assert(!(*benders)->initialized);
   assert(set != NULL);

   /* call destructor of Benders' decomposition */
   if( (*benders)->bendersfree != NULL )
   {
      SCIP_CALL( (*benders)->bendersfree(set->scip, *benders) );
   }

   /* freeing the Benders' cuts */
   for( i = 0; i < (*benders)->nbenderscuts; i++ )
   {
      SCIP_CALL( SCIPbenderscutFree(&((*benders)->benderscuts[i]), set) );
   }
   BMSfreeMemoryArrayNull(&(*benders)->benderscuts);

   SCIPclockFree(&(*benders)->bendersclock);
   SCIPclockFree(&(*benders)->setuptime);
   BMSfreeMemoryArray(&(*benders)->name);
   BMSfreeMemoryArray(&(*benders)->desc);
   BMSfreeMemory(benders);

   return SCIP_OKAY;
}

/** initialises a MIP subproblem by putting the problem into SCIP_STAGE_SOLVING. This is achieved by calling SCIPsolve
 *  and then interrupting the solve in a node focus event handler.
 *  The LP subproblem is also initialised using this method; however, a different event handler is added. This event
 *  handler will put the LP subproblem into probing mode.
 *  The MIP solving function is called to initialise the subproblem because this function calls SCIPsolve with the
 *  appropriate parameter settings for Benders' decomposition.
 */
static
SCIP_RETCODE initialiseSubproblem(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   SCIP* subproblem;
   SCIP_Bool infeasible;
   SCIP_Bool cutoff;

   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   subproblem = SCIPbendersSubproblem(benders, probnumber);
   assert(subproblem != NULL);

   /* Getting the problem into the right SCIP stage for solving */
   SCIP_CALL( SCIPbendersSolveSubproblemMIP(benders, probnumber, &infeasible, SCIP_BENDERSENFOTYPE_LP, FALSE) );

   assert(SCIPgetStage(subproblem) == SCIP_STAGE_SOLVING);

   /* Constructing the LP that can be solved in later iterations */
   SCIP_CALL( SCIPconstructLP(subproblem, &cutoff) );

   return SCIP_OKAY;
}


/** initialises an LP subproblem by putting the problem into probing mode. The probing mode is envoked in a node focus
 *  event handler. This event handler is added just prior to calling the initialise subproblem function.
 */
static
SCIP_RETCODE initialiseLPSubproblem(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   SCIP* subproblem;
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   subproblem = SCIPbendersSubproblem(benders, probnumber);
   assert(subproblem != NULL);

   /* include event handler into SCIP */
   SCIP_CALL( SCIPallocBlockMemory(subproblem, &eventhdlrdata) );
   SCIP_CALL( SCIPincludeEventhdlrBasic(subproblem, &eventhdlr, NODEFOCUS_EVENTHDLR_NAME, NODEFOCUS_EVENTHDLR_DESC,
         eventExecBendersNodefocus, eventhdlrdata) );
   SCIP_CALL( SCIPsetEventhdlrInit(subproblem, eventhdlr, eventInitBendersNodefocus) );
   SCIP_CALL( SCIPsetEventhdlrInitsol(subproblem, eventhdlr, eventInitsolBendersNodefocus) );
   SCIP_CALL( SCIPsetEventhdlrExitsol(subproblem, eventhdlr, eventExitsolBendersNodefocus) );
   SCIP_CALL( SCIPsetEventhdlrFree(subproblem, eventhdlr, eventFreeBendersNodefocus) );
   assert(eventhdlr != NULL);

   /* calling an initial solve to put the problem into probing mode */
   SCIP_CALL( initialiseSubproblem(benders, probnumber) );

   return SCIP_OKAY;
}


/** creates the subproblems and registers it with the Benders' decomposition struct */
static
SCIP_RETCODE createSubproblems(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   SCIP* subproblem;
   SCIP_EVENTHDLR* eventhdlr;
   int nbinvars;
   int nintvars;
   int nimplintvars;
   int nsubproblems;
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   /* if the subproblems have already been created, then they will not be created again. This is the case if the
    * transformed problem has been freed and then retransformed. The subproblems should only be created when the problem
    * is first transformed. */
   if( benders->subprobscreated )
      return SCIP_OKAY;

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* creating all subproblems */
   for( i = 0; i < nsubproblems; i++ )
   {
      /* calling the create subproblem call back method */
      SCIP_CALL( benders->benderscreatesub(set->scip, benders, i) );

      subproblem = SCIPbendersSubproblem(benders, i);

      assert(subproblem != NULL);

      /* setting global limits for the subproblems. This overwrites the limits set by the user */
      SCIP_CALL( SCIPsetIntParam(subproblem, "limits/maxorigsol", 0) );

      /* getting the number of integer and binary variables to determine the problem type */
      SCIP_CALL( SCIPgetVarsData(subproblem, NULL, NULL, &nbinvars, &nintvars, &nimplintvars, NULL) );

      /* if there are no binary and integer variables, then the subproblem is an LP.
       * In this case, the SCIP instance is put into probing mode via the use of an event handler. */
      if( nbinvars == 0 && nintvars == 0 && nimplintvars == 0 )
      {
         SCIPbendersSetSubprobIsConvex(benders, i, TRUE);

         /* if the user has not implemented a solve subproblem callback, then the subproblem solves are performed
          * internally. To be more efficient the subproblem is put into probing mode. */
         if( benders->benderssolvesubconvex == NULL && benders->benderssolvesub == NULL
            && SCIPgetStage(subproblem) <= SCIP_STAGE_PROBLEM )
            SCIP_CALL( initialiseLPSubproblem(benders, i) );
      }
      else
      {
         SCIP_EVENTHDLRDATA* eventhdlrdata_mipnodefocus;
         SCIP_EVENTHDLRDATA* eventhdlrdata_upperbound;

         SCIPbendersSetSubprobIsConvex(benders, i, FALSE);

         /* because the subproblems could be reused in the copy, the event handler is not created again.
          * NOTE: This currently works with the benders_default implementation. It may not be very general. */
         if( benders->benderssolvesubconvex == NULL && benders->benderssolvesub == NULL && !benders->iscopy )
         {
            SCIP_CALL( SCIPallocBlockMemory(subproblem, &eventhdlrdata_mipnodefocus) );
            SCIP_CALL( SCIPallocBlockMemory(subproblem, &eventhdlrdata_upperbound) );

            /* include the first LP solved event handler into the subproblem */
            SCIP_CALL( SCIPincludeEventhdlrBasic(subproblem, &eventhdlr, MIPNODEFOCUS_EVENTHDLR_NAME,
                  MIPNODEFOCUS_EVENTHDLR_DESC, eventExecBendersMipnodefocus, eventhdlrdata_mipnodefocus) );
            SCIP_CALL( SCIPsetEventhdlrInit(subproblem, eventhdlr, eventInitBendersMipnodefocus) );
            SCIP_CALL( SCIPsetEventhdlrInitsol(subproblem, eventhdlr, eventInitsolBendersMipnodefocus) );
            SCIP_CALL( SCIPsetEventhdlrExitsol(subproblem, eventhdlr, eventExitsolBendersMipnodefocus) );
            SCIP_CALL( SCIPsetEventhdlrFree(subproblem, eventhdlr, eventFreeBendersMipnodefocus) );
            assert(eventhdlr != NULL);


            /* include the upper bound interrupt event handler into the subproblem */
            SCIP_CALL( SCIPincludeEventhdlrBasic(subproblem, &eventhdlr, UPPERBOUND_EVENTHDLR_NAME,
                  UPPERBOUND_EVENTHDLR_DESC, eventExecBendersUpperbound, eventhdlrdata_upperbound) );
            SCIP_CALL( SCIPsetEventhdlrInit(subproblem, eventhdlr, eventInitBendersUpperbound) );
            SCIP_CALL( SCIPsetEventhdlrInitsol(subproblem, eventhdlr, eventInitsolBendersUpperbound) );
            SCIP_CALL( SCIPsetEventhdlrExitsol(subproblem, eventhdlr, eventExitsolBendersUpperbound) );
            SCIP_CALL( SCIPsetEventhdlrFree(subproblem, eventhdlr, eventFreeBendersUpperbound) );
            assert(eventhdlr != NULL);
         }
      }
   }

   benders->subprobscreated = TRUE;

   return SCIP_OKAY;

}


/** creates a variable mapping between the master problem variables of the source scip and the sub scip */
static
SCIP_RETCODE createMasterVarMapping(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   SCIP_VAR** vars;  /* the variables from the copied sub SCIP */
   SCIP_VAR* sourcevar;
   SCIP_VAR* origvar;
   SCIP_Real scalar;
   SCIP_Real constant;
   int nvars;
   int i;
   SCIP_RETCODE retcode;

   assert(benders != NULL);
   assert(set != NULL);
   assert(benders->iscopy);
   assert(benders->mastervarsmap == NULL);

   /* getting the master problem variable data */
   vars = SCIPgetVars(set->scip);
   nvars = SCIPgetNVars(set->scip);

   /* creating the hashmap for the mapping between the master variable of the target and source scip */
   SCIP_CALL( SCIPhashmapCreate(&benders->mastervarsmap, SCIPblkmem(set->scip), nvars) );

   for( i = 0; i < nvars; i++ )
   {
      origvar = vars[i];

      /* The variable needs to be transformed back into an original variable. If the variable is already original, then
       * this function just returns the same variable */
      retcode = SCIPvarGetOrigvarSum(&origvar, &scalar, &constant);
      if( retcode != SCIP_OKAY )
         assert(FALSE);

      sourcevar = SCIPfindVar(benders->sourcescip, SCIPvarGetName(origvar));
      if( sourcevar != NULL )
      {
         SCIP_CALL( SCIPhashmapInsert(benders->mastervarsmap, vars[i], sourcevar) );
         SCIP_CALL( SCIPcaptureVar(benders->sourcescip, sourcevar) );
      }
   }

   return SCIP_OKAY;
}


/** initializes Benders' decomposition */
SCIP_RETCODE SCIPbendersInit(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   if( benders->initialized )
   {
      SCIPerrorMessage("Benders' decomposition <%s> already initialized\n", benders->name);
      return SCIP_INVALIDCALL;
   }

   if( set->misc_resetstat )
   {
      SCIPclockReset(benders->setuptime);
      SCIPclockReset(benders->bendersclock);

      benders->ncalls = 0;
      benders->ncutsfound = 0;
      benders->ntransferred = 0;
   }

   /* start timing */
   SCIPclockStart(benders->setuptime, set);

   /* creates the subproblems and sets up the probing mode for LP subproblems. This function calls the benderscreatesub
    * callback. */
   SCIP_CALL( createSubproblems(benders, set) );

   if( benders->bendersinit != NULL )
      SCIP_CALL( benders->bendersinit(set->scip, benders) );

   /* getting the transformed auxiliary variables */
   //nsubproblems = SCIPbendersGetNSubproblems(benders);
   //for( i = 0; i < nsubproblems; i++ )
      //SCIP_CALL( SCIPgetTransformedVar(set->scip, benders->auxiliaryvars[i], &benders->auxiliaryvars[i]) );


   /* if the Benders' decomposition is a copy, then a variable mapping between the master problem variables is required */
   if( benders->iscopy )
      SCIP_CALL( createMasterVarMapping(benders, set) );


   /* initialising the Benders' cuts */
   SCIPbendersSortBenderscuts(benders);
   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      SCIP_CALL( SCIPbenderscutInit(benders->benderscuts[i], set) );
   }

   benders->initialized = TRUE;

   /* stop timing */
   SCIPclockStop(benders->setuptime, set);

   return SCIP_OKAY;
}


/** create and add transferred cut */
static
SCIP_RETCODE createAndAddTransferredCut(
   SCIP*                 sourcescip,         /**< the source SCIP from when the Benders' decomposition was copied */
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition structure of the sub SCIP */
   SCIP_VAR**            vars,               /**< the variables from the source constraint */
   SCIP_Real*            vals,               /**< the coefficients of the variables in the source constriant */
   SCIP_Real             lhs,                /**< the LHS of the source constraint */
   SCIP_Real             rhs,                /**< the RHS of the source constraint */
   int                   nvars               /**< the number of variables in the source constraint */
   )
{
   SCIP_BENDERS* sourcebenders;     /* the Benders' decomposition of the source SCIP */
   SCIP_CONSHDLR* consbenders;      /* a helper variable for the Benders' decomposition constraint handler */
   SCIP_CONS* transfercons;         /* the constraint that is generated to transfer the constraints/cuts */
   SCIP_ROW* transfercut;           /* the cut that is generated to transfer the constraints/cuts */
   SCIP_VAR* sourcevar;             /* the source variable that will be added to the transferred cut */
   char cutname[SCIP_MAXSTRLEN];    /* the name of the transferred cut */
   int i;
   SCIP_Bool fail;

   assert(sourcescip != NULL);
   assert(benders != NULL);
   assert(vars != NULL);
   assert(vals != NULL);

   /* retrieving the source Benders' decomposition structure */
   sourcebenders = SCIPfindBenders(sourcescip, SCIPbendersGetName(benders));

   /* retrieving the Benders' decomposition constraint handler */
   consbenders = SCIPfindConshdlr(sourcescip, "benders");

   /* setting the name of the transferred cut */
   (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "transferredcut_%d",
      SCIPbendersGetNTransferredCuts(sourcebenders) );

   /* creating an empty row/constraint for the transferred cut */
   if( sourcebenders->cutsasconss )
   {
      SCIP_CALL( SCIPcreateConsBasicLinear(sourcescip, &transfercons, cutname, 0, NULL, NULL, lhs, rhs) );
      SCIP_CALL( SCIPsetConsRemovable(sourcescip, transfercons, TRUE) );
   }
   else
      SCIP_CALL( SCIPcreateEmptyRowCons(sourcescip, &transfercut, consbenders, cutname, lhs, rhs, FALSE,
            FALSE, TRUE) );

   fail = FALSE;
   for( i = 0; i < nvars; i++ )
   {
      /* getting the source var from the hash map */
      sourcevar = (SCIP_VAR*) SCIPhashmapGetImage(benders->mastervarsmap, vars[i]);

      /* if the source variable is not found, then the mapping in incomplete. So the constraint can not be
       * transferred. */
      if( sourcevar == NULL )
      {
         fail = TRUE;
         break;
      }

      if( sourcebenders->cutsasconss )
         SCIP_CALL( SCIPaddCoefLinear(sourcescip, transfercons, sourcevar, vals[i]) );    /*lint !e644*/
      else
         SCIP_CALL( SCIPaddVarToRow(sourcescip, transfercut, sourcevar, vals[i]) );       /*lint !e644*/

      /* NOTE: There could be a problem with the auxiliary variables. They may not be copied. */
   }

   /* if all of the source variables were found to generate the cut */
   if( !fail )
   {
      if( sourcebenders->cutsasconss )
         SCIP_CALL( SCIPaddCons(sourcescip, transfercons) );
      else
         SCIP_CALL( SCIPaddPoolCut(sourcescip, transfercut) );

      sourcebenders->ntransferred++;
   }

   /* release the row/constraint */
   if( sourcebenders->cutsasconss )
      SCIP_CALL( SCIPreleaseCons(sourcescip, &transfercons) );
   else
      SCIP_CALL( SCIPreleaseRow(sourcescip, &transfercut) );

   return SCIP_OKAY;
}


/** transfers the cuts generated in a subscip to the source scip */
static
SCIP_RETCODE transferBendersCuts(
   SCIP*                 sourcescip,         /**< the source SCIP from when the Benders' decomposition was copied */
   SCIP*                 subscip,            /**< the sub SCIP where the Benders' cuts were generated */
   SCIP_BENDERS*         benders             /**< the Benders' decomposition structure of the sub SCIP */
   )
{
   SCIP_BENDERS* sourcebenders;     /* the Benders' decomposition of the source SCIP */
   SCIP_BENDERSCUT* benderscut;     /* a helper variable for the Benders' cut plugin */
   SCIP_CONS** addedcons;           /* the constraints added by the Benders' cut */
   SCIP_ROW** addedcuts;            /* the cuts added by the Benders' cut */
   SCIP_VAR** vars;                 /* the variables of the added constraint/row */
   SCIP_Real* vals;                 /* the values of the added constraint/row */
   SCIP_Real lhs;                   /* the LHS of the added constraint/row */
   SCIP_Real rhs;                   /* the RHS of the added constraint/row */
   int naddedcons;
   int naddedcuts;
   int nvars;
   int i;
   int j;
   int k;

   assert(subscip != NULL);
   assert(benders != NULL);

   /* retrieving the source Benders' decomposition structure */
   sourcebenders = SCIPfindBenders(sourcescip, SCIPbendersGetName(benders));

   /* exit if the cuts should not be transferred from the sub SCIP to the source SCIP. */
   if( !sourcebenders->transfercuts )
      return SCIP_OKAY;

   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      benderscut = benders->benderscuts[i];

      /* retreiving the Benders' cuts constraints */
      SCIP_CALL( SCIPbenderscutGetCons(benderscut, &addedcons, &naddedcons) );

      /* looping over all added constraints to costruct the cut for the source scip */
      for( j = 0; j < naddedcons; j++ )
      {
         SCIP_CONSHDLR* conshdlr;
         const char * conshdlrname;

         conshdlr = SCIPconsGetHdlr(addedcons[j]);
         assert(conshdlr != NULL);
         conshdlrname = SCIPconshdlrGetName(conshdlr);

         /* it is only possible to transfer linear constraints. If the Benders' cut has been added as another
          * constraint, then this will not be transferred to the source SCIP */
         if( strcmp(conshdlrname, "linear") == 0 )
         {
            /* collecting the variable information from the constraint */
            nvars = SCIPgetNVarsLinear(subscip, addedcons[j]);
            vars = SCIPgetVarsLinear(subscip, addedcons[j]);
            vals = SCIPgetValsLinear(subscip, addedcons[j]);

            /* collecting the bounds from the constraint */
            lhs = SCIPgetLhsLinear(subscip, addedcons[j]);
            rhs = SCIPgetRhsLinear(subscip, addedcons[j]);

            /* create and add the cut to be transferred from the sub SCIP to the source SCIP */
            SCIP_CALL( createAndAddTransferredCut(sourcescip, benders, vars, vals, lhs, rhs, nvars) );
         }
      }

      /* retreiving the Benders' cuts added cuts */
      SCIP_CALL( SCIPbenderscutGetCuts(benderscut, &addedcuts, &naddedcuts) );

      /* looping over all added constraints to costruct the cut for the source scip */
      for( j = 0; j < naddedcuts; j++ )
      {
         SCIP_COL** cols;
         int ncols;

         cols = SCIProwGetCols(addedcuts[j]);
         ncols = SCIProwGetNNonz(addedcuts[j]);

         /* get all variables of the row */
         SCIP_CALL( SCIPallocBufferArray(subscip, &vars, ncols) );
         for( k = 0; k < ncols; ++k )
            vars[k] = SCIPcolGetVar(cols[k]);

         /* collecting the variable information from the constraint */
         vals = SCIProwGetVals(addedcuts[j]);

         /* collecting the bounds from the constraint */
         lhs = SCIProwGetLhs(addedcuts[j]) - SCIProwGetConstant(addedcuts[j]);
         rhs = SCIProwGetRhs(addedcuts[j]) - SCIProwGetConstant(addedcuts[j]);

         /* create and add the cut to be transferred from the sub SCIP to the source SCIP */
         SCIP_CALL( createAndAddTransferredCut(sourcescip, benders, vars, vals, lhs, rhs, ncols) );

         SCIPfreeBufferArray(subscip, &vars);
      }
   }

   return SCIP_OKAY;
}


/** releases the variables that have been captured in the hashmap */
static
SCIP_RETCODE releaseVarMappingHashmapVars(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   int nentries;
   int i;

   assert(scip != NULL);
   assert(benders != NULL);

   assert(benders->mastervarsmap != NULL);

   nentries = SCIPhashmapGetNEntries(benders->mastervarsmap);

   for( i = 0; i < nentries; ++i )
   {
      SCIP_HASHMAPENTRY* entry;
      entry = SCIPhashmapGetEntry(benders->mastervarsmap, i);

      if( entry != NULL )
      {
         SCIP_VAR* var;
         var = (SCIP_VAR*) SCIPhashmapEntryGetImage(entry);

         SCIP_CALL( SCIPreleaseVar(scip, &var) );
      }
   }

   return SCIP_OKAY;
}


/** calls exit method of Benders' decomposition */
SCIP_RETCODE SCIPbendersExit(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int nsubproblems;
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   if( !benders->initialized )
   {
      SCIPerrorMessage("Benders' decomposition <%s> not initialized\n", benders->name);
      return SCIP_INVALIDCALL;
   }

   /* start timing */
   SCIPclockStart(benders->setuptime, set);

   if( benders->bendersexit != NULL )
      SCIP_CALL( benders->bendersexit(set->scip, benders) );

   /* if the Benders' decomposition is a copy, then
    * - the generated cuts will be transferred to the source scip, and
    * - the hash map must be freed */
   if( benders->iscopy )
   {
      SCIP_CALL( transferBendersCuts(benders->sourcescip, set->scip, benders) );
      SCIP_CALL( releaseVarMappingHashmapVars(benders->sourcescip, benders) );
      SCIPhashmapFree(&benders->mastervarsmap);
   }

   /* releasing all of the auxiliary variables */
   nsubproblems = SCIPbendersGetNSubproblems(benders);
   for( i = 0; i < nsubproblems; i++ )
      SCIP_CALL( SCIPreleaseVar(set->scip, &benders->auxiliaryvars[i]) );

   /* calling the exit method for the Benders' cuts */
   SCIPbendersSortBenderscuts(benders);
   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      SCIP_CALL( SCIPbenderscutExit(benders->benderscuts[i], set) );
   }

   benders->initialized = FALSE;

   /* stop timing */
   SCIPclockStop(benders->setuptime, set);

   return SCIP_OKAY;
}

/** solves an independent subproblem to identify its lower bound. The lower bound is then used to update the bound on
 *  the auxiliary variable
 *  TODO: Infeasibility of the original problem could be detected here. Need to check how to inform SCIP that the
 *  problem is infeasible.
 */
static
SCIP_RETCODE computeSubproblemLowerbound(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem to be evaluated */
   SCIP_Bool             independent,        /**< is the subproblem independent? */
   SCIP_Real*            lowerbound          /**< the lowerbound for the subproblem */
   )
{
   SCIP* subproblem;
   SCIP_Longint totalnodes;
   int disablecutoff;
   int verblevel;

   SCIP_Bool lperror;
   SCIP_Bool cutoff;

   assert(scip != NULL);
   assert(benders != NULL);

   /* getting the subproblem to evaluate */
   subproblem = SCIPbendersSubproblem(benders, probnumber);

   SCIP_CALL( SCIPgetIntParam(subproblem, "display/verblevel", &verblevel) );
   SCIP_CALL( SCIPsetIntParam(subproblem, "display/verblevel", (int)SCIP_VERBLEVEL_NONE) );

   /* if the subproblem is independent, then the default SCIP settings are used. Otherwise, only the root node is solved
    * to compute a lower bound on the subproblem
    */
   if( !independent )
   {
      SCIP_CALL( SCIPgetLongintParam(subproblem, "limits/totalnodes", &totalnodes) );
      SCIP_CALL( SCIPgetIntParam(subproblem, "lp/disablecutoff", &disablecutoff) );
      SCIP_CALL( SCIPsetLongintParam(subproblem, "limits/totalnodes", 1) );
      SCIP_CALL( SCIPsetIntParam(subproblem, "lp/disablecutoff", 1) );
   }

   /* if the subproblem not independent and is convex, then the probing LP is solve. Otherwise, the MIP is solved */
   if( !independent && SCIPbendersSubprobIsConvex(benders, probnumber) )
   {
      assert(SCIPisLPConstructed(subproblem));

      SCIP_CALL( SCIPstartProbing(subproblem) );
      SCIP_CALL( SCIPsolveProbingLP(subproblem, -1, &lperror, &cutoff) );
   }
   else
   {
      SCIP_CALL( SCIPsolve(subproblem) );
   }

   /* getting the lower bound value */
   (*lowerbound) = SCIPgetDualbound(subproblem);

   if( !independent )
   {
      SCIP_CALL( SCIPsetLongintParam(subproblem, "limits/totalnodes", totalnodes) );
      SCIP_CALL( SCIPsetIntParam(subproblem, "lp/disablecutoff", disablecutoff) );
   }
   SCIP_CALL( SCIPsetIntParam(subproblem, "display/verblevel", verblevel) );

   /* the subproblem must be freed so that it is reset for the subsequent Benders' decomposition solves. If the
    * subproblems are independent, they are not freed. This is handled in SCIPbendersFreeSubproblem.
    */
   SCIP_CALL( SCIPfreeBendersSubproblem(scip, benders, probnumber) );

   return SCIP_OKAY;
}

/** checks whether a subproblem is independent.
 *  If it is independent, then a lower bounding constraint is added to the master problem
 */
static
SCIP_RETCODE checkSubproblemIndependenceAndLowerbound(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_Real*            lowerbound          /**< an array to store the lower bound for the auxiliary variables */
   )
{
   SCIP_VAR** vars;
   int nvars;
   int nsubproblems;
   int i;
   int j;

   assert(scip != NULL);
   assert(benders != NULL);

   /* retrieving the master problem variables */
   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL) );

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* looping over all subproblems to check whether there exists at least one master problem variable */
   for( i = 0; i < nsubproblems; i++ )
   {
      SCIP_Bool independent = TRUE;
      for( j = 0; j < nvars; j++ )
      {
         SCIP_VAR* subprobvar;

         /* getting the subproblem problem variable corresponding to the master problem variable */
         SCIP_CALL( SCIPgetBendersSubproblemVar(scip, benders, vars[j], &subprobvar, i) );

         /* if the subporblem variable is not NULL, then the subproblem depends on the master problem */
         if( subprobvar != NULL )
         {
            independent = FALSE;
            break;
         }
      }

      /* setting the independent flag */
      SCIPbendersSetSubprobIsIndependent(benders, i, independent);

      /* the lower bound is computed for all subproblems. If the subproblem is independent, then the lower bound is the
       * optimal objective of the subproblem
       */
      SCIP_CALL( computeSubproblemLowerbound(scip, benders, i, independent, &lowerbound[i]) );
   }

   return SCIP_OKAY;
}

/** informs the Benders' decomposition that the presolving process is being started */
SCIP_RETCODE SCIPbendersInitpre(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat                /**< dynamic problem statistics */
   )
{
   assert(benders != NULL);
   assert(set != NULL);
   assert(stat != NULL);

   if( !benders->iscopy )
   {
      SCIP_Real* lowerbound;
      int nsubproblems;
      int i;

      nsubproblems = SCIPbendersGetNSubproblems(benders);

      /* allocating memory for the auxiliay variables lower bounds array */
      SCIP_CALL( SCIPallocBufferArray(set->scip, &lowerbound, nsubproblems) );

      /* initialising the lower bound array */
      for( i = 0; i < nsubproblems; i++ )
         lowerbound[i] = -SCIPsetInfinity(set);

      /* check the subproblem independence and update the auxiliary variable lower bounds.
       * This check is only performed if the user has not implemented a solve subproblem function.
       */
      if( benders->benderssolvesubconvex == NULL && benders->benderssolvesub == NULL )
        SCIP_CALL( checkSubproblemIndependenceAndLowerbound(set->scip, benders, lowerbound) );

      /* adding the auxiliary variables to the master problem */
      SCIP_CALL( addAuxiliaryVariablesToMaster(set->scip, benders, lowerbound) );

      /* freeing the lower bound array */
      SCIPfreeBufferArray(set->scip, &lowerbound);
   }
   else
   {
      /* the copied auxiliary variables must be assigned to the target benders */
      SCIP_CALL( assignAuxiliaryVariables(set->scip, benders) );
   }


   /* call presolving initialization method of Benders' decomposition */
   if( benders->bendersinitpre != NULL )
   {
      /* start timing */
      SCIPclockStart(benders->setuptime, set);

      SCIP_CALL( benders->bendersinitpre(set->scip, benders) );

      /* stop timing */
      SCIPclockStop(benders->setuptime, set);
   }

   return SCIP_OKAY;
}


/** informs the Benders' decomposition that the presolving process has completed */
SCIP_RETCODE SCIPbendersExitpre(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat                /**< dynamic problem statistics */
   )
{
   assert(benders != NULL);
   assert(set != NULL);
   assert(stat != NULL);

   /* call presolving  deinitialization method of Benders' decomposition */
   if( benders->bendersexitpre != NULL )
   {
      /* start timing */
      SCIPclockStart(benders->setuptime, set);

      SCIP_CALL( benders->bendersexitpre(set->scip, benders) );

      /* stop timing */
      SCIPclockStop(benders->setuptime, set);
   }

   return SCIP_OKAY;
}

/** informs Benders' decomposition that the branch and bound process is being started */
SCIP_RETCODE SCIPbendersInitsol(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   /* call solving process initialization method of Benders' decomposition */
   if( benders->bendersinitsol != NULL )
   {
      /* start timing */
      SCIPclockStart(benders->setuptime, set);

      SCIP_CALL( benders->bendersinitsol(set->scip, benders) );

      /* stop timing */
      SCIPclockStop(benders->setuptime, set);
   }

   /* calling the initsol method for the Benders' cuts */
   SCIPbendersSortBenderscuts(benders);
   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      SCIP_CALL( SCIPbenderscutInitsol(benders->benderscuts[i], set) );
   }

   return SCIP_OKAY;
}

/** informs Benders' decomposition that the branch and bound process data is being freed */
SCIP_RETCODE SCIPbendersExitsol(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   /* call solving process deinitialization method of Benders' decomposition */
   if( benders->bendersexitsol != NULL )
   {
      /* start timing */
      SCIPclockStart(benders->setuptime, set);

      SCIP_CALL( benders->bendersexitsol(set->scip, benders) );

      /* stop timing */
      SCIPclockStop(benders->setuptime, set);
   }

   /* sorting the Benders' decomposition cuts in order of priority. Only a single cut is generated for each subproblem
    * per solving iteration. This is particularly important in the case of the optimality and feasibility cuts. Since
    * these work on two different solutions to the subproblem, it is not necessary to generate both cuts. So, once the
    * feasibility cut is generated, then no other cuts will be generated.
    */
   SCIPbendersSortBenderscuts(benders);

   /* calling the exitsol method for the Benders' cuts */
   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      SCIP_CALL( SCIPbenderscutExitsol(benders->benderscuts[i], set) );
   }

   return SCIP_OKAY;
}

/** activates benders such that it is called in LP solving loop */
SCIP_RETCODE SCIPbendersActivate(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   nsubproblems        /**< the number subproblems used in this decomposition */
   )
{
   int i;

   assert(benders != NULL);
   assert(set != NULL);
   assert(set->stage == SCIP_STAGE_INIT || set->stage == SCIP_STAGE_PROBLEM);

   if( !benders->active )
   {
      benders->active = TRUE;
      set->nactivebenders++;
      set->benderssorted = FALSE;

      benders->nsubproblems = nsubproblems;

      /* allocating memory for the subproblems arrays */
      SCIP_ALLOC( BMSallocMemoryArray(&benders->subproblems, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->auxiliaryvars, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->subprobobjval, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->bestsubprobobjval, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->subprobisconvex, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->subprobsetup, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->indepsubprob, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->subprobenabled, benders->nsubproblems) );
      SCIP_ALLOC( BMSallocMemoryArray(&benders->mastervarscont, benders->nsubproblems) );

      for( i = 0; i < benders->nsubproblems; i++ )
      {
         benders->subproblems[i] = NULL;
         benders->auxiliaryvars[i] = NULL;
         benders->subprobobjval[i] = SCIPsetInfinity(set);
         benders->bestsubprobobjval[i] = SCIPsetInfinity(set);
         benders->subprobisconvex[i] = FALSE;
         benders->subprobsetup[i] = FALSE;
         benders->indepsubprob[i] = FALSE;
         benders->subprobenabled[i] = TRUE;
         benders->mastervarscont[i] = FALSE;
      }
   }

   return SCIP_OKAY;
}

/** deactivates benders such that it is no longer called in LP solving loop */
void SCIPbendersDeactivate(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(benders != NULL);
   assert(set != NULL);
   assert(set->stage == SCIP_STAGE_INIT || set->stage == SCIP_STAGE_PROBLEM);

   if( benders->active )
   {
#ifndef NDEBUG
      int nsubproblems;
      int i;

      nsubproblems = SCIPbendersGetNSubproblems(benders);

      /* checking whether the auxiliary variables and subproblems are all NULL */
      for( i = 0; i < nsubproblems; i++ )
         assert(benders->auxiliaryvars[i] == NULL);
#endif

      benders->active = FALSE;
      set->nactivebenders--;
      set->benderssorted = FALSE;

      /* freeing the memory allocated during the activation of the Benders' decomposition */
      BMSfreeMemoryArray(&benders->mastervarscont);
      BMSfreeMemoryArray(&benders->subprobenabled);
      BMSfreeMemoryArray(&benders->indepsubprob);
      BMSfreeMemoryArray(&benders->subprobsetup);
      BMSfreeMemoryArray(&benders->subprobisconvex);
      BMSfreeMemoryArray(&benders->bestsubprobobjval);
      BMSfreeMemoryArray(&benders->subprobobjval);
      BMSfreeMemoryArray(&benders->auxiliaryvars);
      BMSfreeMemoryArray(&benders->subproblems);
   }
}

/** returns whether the given Benders decomposition is in use in the current problem */
SCIP_Bool SCIPbendersIsActive(
   SCIP_BENDERS*         benders             /**< the Benders' decomposition structure */
   )
{
   assert(benders != NULL);

   return benders->active;
}

/** merges a subproblem back into the master problem. This process just adds a copy of the subproblem variables and
 *  constraints to the master problem, but keeps the subproblem stored in the Benders data structure. The reason for
 *  keeping the subproblem available is for when it is queried for solutions after the problem is solved.
 *
 *  Once the subproblem is merged back into the master problem, then the subproblem is flagged as disabled. This means
 *  that it will not be solved in the subsequent subproblem solving loops.
 *  Additionally, the auxiliary variable associated with the subproblem is fixed to zero.
 *  TODO: The auxiliary variable could be removed or the objective function coefficient is set to zero.
 */
static
SCIP_RETCODE mergeSubproblemIntoMaster(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   probnumber          /**< the subproblem number*/
   )
{
   SCIP* subproblem;

   assert(benders != NULL);
   assert(probnumber != NULL);

   subproblem = SCIPbendersSubproblem(benders, probnumber);


   return SCIP_OKAY;
}


/** returns whether only the convex relaxations will be checked in this solve loop
 *  when Benders' is used in the LNS heuristics, only the convex relaxations of the master/subproblems are checked,
 *  i.e. no integer cuts are generated. In this case, then Benders' decomposition is performed under the assumption
 *  that all subproblems are convex relaxations.
 */
static
SCIP_Bool onlyCheckSubproblemConvexRelax(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   return benders->iscopy && benders->lnscheck;
}

/** returns the number of subproblems that will be checked in this iteration */
static
int numSubproblemsToCheck(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BENDERSENFOTYPE  type                /**< the type of solution being enforced */
   )
{
   if( benders->ncalls == 0 || type == SCIP_BENDERSENFOTYPE_CHECK || onlyCheckSubproblemConvexRelax(benders) )
      return SCIPbendersGetNSubproblems(benders);
   else
      return (int) SCIPsetCeil(set, (SCIP_Real) SCIPbendersGetNSubproblems(benders)*benders->subprobfrac);
}

/** solves each of the Benders' decomposition subproblems for the given solution. All, or a fraction, of subproblems are
 *  solved before the Benders' decomposition cuts are generated.
 *  Since a convex relaxation of the subproblem could be solved to generate cuts, a parameter nverified is used to
 *  identified the number of subproblems that have been solved in their "original" form. For example, if the subproblem
 *  is a MIP, then if the LP is solved to generate cuts, this does not constitute a verification. The verification is
 *  only performed when the MIP is solved.
 */
static
SCIP_RETCODE solveBendersSubproblems(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_BENDERSENFOTYPE  type,               /**< the type of solution being enforced */
   SCIP_BENDERSSOLVELOOP solveloop,          /**< the current solve loop */
   SCIP_Bool             checkint,           /**< are the subproblems called during a check/enforce of integer sols? */
   int*                  nchecked,   /**< the number of subproblems checked in this solve loop, they may not be solved */
   int*                  nverified,          /**< the number of subproblems verified in the current loop */
   SCIP_Bool*            subprobsolved,      /**< an array indicating the subproblems that were solved in this loop. */
   SCIP_Bool*            subisinfeas,        /**< array to store whether a subproblem is infeasible */
   SCIP_Bool*            infeasible,         /**< is the master problem infeasible with respect to the Benders' cuts? */
   SCIP_Bool*            optimal             /**< is the current solution optimal? */
   )
{
   SCIP_Bool onlyconvexcheck;
   int nsubproblems;
   int numtocheck;
   int numnotopt;
   int subproblemcount;
   int i;

   assert(benders != NULL);
   assert(set != NULL);

   /* getting the number of subproblems in the Benders' decompsition */
   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* in the case of an LNS check, only the convex relaxations of the subproblems will be solved. This is a performance
    * feature, since solving the convex relaxation is typically much faster than solving the corresponding CIP. While
    * the CIP is not solved during the LNS check, the solutions are still of higher quality than when Benders' is not
    * employed.
    */
   onlyconvexcheck = onlyCheckSubproblemConvexRelax(benders);

   /* it is possible to only solve a subset of subproblems. This is given by a parameter. */
   numtocheck = numSubproblemsToCheck(benders, set, type);

   SCIPdebugMessage("Performing the subproblem solving process. Number of subproblems to check %d\n", numtocheck);

   SCIPdebugMessage("Benders' decomposition - solve loop %d\n", solveloop);
   numnotopt = 0;
   subproblemcount = 0;

   if( type == SCIP_BENDERSENFOTYPE_CHECK && sol == NULL )
   {
      /* TODO: Check whether this is absolutely necessary. I think that this if statment can be removed. */
      (*infeasible) = TRUE;
   }
   else
   {
      /* solving each of the subproblems for Benders decomposition */
      /* TODO: ensure that the each of the subproblems solve and update the parameters with the correct return values
       */
      i = benders->firstchecked;
      /*for( i = 0; i < nsubproblems; i++ )*/
      while( subproblemcount < nsubproblems && numnotopt < numtocheck )
      {
         SCIP_Bool subinfeas = FALSE;
         SCIP_Bool convexsub = SCIPbendersSubprobIsConvex(benders, i);
         SCIP_Bool solvesub = TRUE;
         SCIP_Bool solved;

         /* the subproblem is initially flagged as not solved for this solving loop */
         subprobsolved[i] = FALSE;

         /* for the second solving loop, if the problem is an LP, it is not solved again. If the problem is a MIP,
          * then the subproblem objective function value is set to infinity. However, if the subproblem is proven
          * infeasible from the LP, then the IP loop is not performed.
          * If the solve loop is SCIP_BENDERSSOLVELOOP_USERCIP, then nothing is done. It is assumed that the user will
          * correctly update the objective function within the user-defined solving function.
          */
         if( solveloop == SCIP_BENDERSSOLVELOOP_CIP )
         {
            if( convexsub || subisinfeas[i] )
               solvesub = FALSE;
            else
               SCIPbendersSetSubprobObjval(benders, i, SCIPinfinity(SCIPbendersSubproblem(benders, i)));
         }

         if( solvesub )
         {
            SCIP_CALL( SCIPbendersExecSubproblemSolve(benders, set, sol, i, solveloop, FALSE, &solved, &subinfeas, type) );

#ifdef SCIP_DEBUG
            if( type == SCIP_BENDERSENFOTYPE_LP )
            {
               SCIPdebugMessage("LP: Subproblem %d (%f < %f)\n", i, SCIPbendersGetAuxiliaryVarVal(benders, set, sol, i),
                  SCIPbendersGetSubprobObjval(benders, i));
            }
#endif
            subprobsolved[i] = solved;

            (*infeasible) = (*infeasible) || subinfeas;
            subisinfeas[i] = subinfeas;

            /* if the subproblems are solved to check integer feasibility, then the optimality check must be performed.
             * This will only be performed if checkint is TRUE and the subproblem was solved. The subproblem may not be
             * solved if the user has defined a solving function
             */
            if( checkint && subprobsolved[i] )
            {
               /* if the subproblem is feasible, then it is necessary to update the value of the auxiliary variable to the
                * objective function value of the subproblem.
                */
               if( !subinfeas )
               {
                  SCIP_Bool subproboptimal = FALSE;

                  SCIP_CALL( SCIPbendersCheckSubprobOptimality(benders, set, sol, i, &subproboptimal) );

                  /* It is only possible to determine the optimality of a solution within a given subproblem in four
                   * different cases:
                   * i) solveloop == SCIP_BENDERSSOLVELOOP_CONVEX or USERCONVEX and the subproblem is convex.
                   * ii) solveloop == SCIP_BENDERSOLVELOOP_CONVEX  and only the convex relaxations will be checked.
                   * iii) solveloop == SCIP_BENDERSSOLVELOOP_USERCIP and the subproblem was solved, since the user has
                   * defined a solve function, it is expected that the solving is correctly executed.
                   * iv) solveloop == SCIP_BENDERSSOLVELOOP_CIP and the MIP for the subproblem has been solved.
                   */
                  if( convexsub || onlyconvexcheck
                     || solveloop == SCIP_BENDERSSOLVELOOP_CIP
                     || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP )
                     (*optimal) = (*optimal) && subproboptimal;

#ifdef SCIP_DEBUG
                  if( convexsub || solveloop >= SCIP_BENDERSSOLVELOOP_CIP )
                  {
                     if( subproboptimal )
                     {
                        SCIPdebugMessage("Subproblem %d is Optimal (%f >= %f)\n", i,
                           SCIPbendersGetAuxiliaryVarVal(benders, set, sol, i), SCIPbendersGetSubprobObjval(benders, i));
                     }
                     else
                     {
                        SCIPdebugMessage("Subproblem %d is NOT Optimal (%f < %f)\n", i,
                           SCIPbendersGetAuxiliaryVarVal(benders, set, sol, i), SCIPbendersGetSubprobObjval(benders, i));
                     }
                  }
#endif

                  /* the nverified variable is only incremented when the original form of the subproblem has been solved.
                   * What is meant by "original" is that the LP relaxation of CIPs are solved to generate valid cuts. So
                   * if the subproblem is defined as a CIP, then it is only classified as checked if the CIP is solved.
                   * There are three cases where the "original" form is solved are:
                   * i) solveloop == SCIP_BENDERSSOLVELOOP_CONVEX or USERCONVEX and the subproblem is an LP
                   *    - the original form has been solved.
                   * ii) solveloop == SCIP_BENDERSSOLVELOOP_CIP or USERCIP and the CIP for the subproblem has been
                   *    solved.
                   * iii) or, only a convex check is performed.
                   */
                  if( ((solveloop == SCIP_BENDERSSOLVELOOP_CONVEX || solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX)
                        && convexsub)
                     || ((solveloop == SCIP_BENDERSSOLVELOOP_CIP || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP)
                        && !convexsub)
                     || onlyconvexcheck )
                     (*nverified)++;


                  if( !subproboptimal )
                  {
                     numnotopt++;
                     assert(numnotopt <= nsubproblems);
                  }
               }
               else
               {
                  numnotopt++;
                  assert(numnotopt <= nsubproblems);
               }
            }
         }

         subproblemcount++;
         i++;
         if( i >= nsubproblems )
            i = 0;
         benders->lastchecked = i;
      }
   }

   (*nchecked) = subproblemcount;

   return SCIP_OKAY;
}

/** calls the Benders' decompsition cuts for the given solve loop. There are four cases:
 *  i) solveloop == SCIP_BENDERSSOLVELOOP_CONVEX - only the LP Benders' cuts are called
 *  ii) solveloop == SCIP_BENDERSSOLVELOOP_CIP - only the CIP Benders' cuts are called
 *  iii) solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX - only the LP Benders' cuts are called
 *  iv) solveloop == SCIP_BENDERSSOLVELOOP_USERCIP - only the CIP Benders' cuts are called
 */
static
SCIP_RETCODE generateBendersCuts(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_RESULT*          result,             /**< result of the pricing process */
   SCIP_BENDERSENFOTYPE  type,               /**< the type of solution being enforced */
   SCIP_BENDERSSOLVELOOP solveloop,          /**< the current solve loop */
   SCIP_Bool             checkint,           /**< are the subproblems called during a check/enforce of integer sols? */
   int                   nchecked,   /**< the number of subproblems checked in this solve loop, they may not be solved */
   SCIP_Bool*            subprobsolved,      /**< an array indicating the subproblems that were solved in this loop. */
   int*                  nsolveloops         /**< the number of solve loops, is updated w.r.t added cuts */
   )
{
   SCIP_BENDERSCUT** benderscuts;
   int nbenderscuts;
   int nsubproblems;
   int subproblemcount;
   int i;
   int j;
   SCIP_Bool onlyconvexcheck;

   assert(benders != NULL);
   assert(set != NULL);

   /* getting the Benders' decomposition cuts */
   benderscuts = SCIPbendersGetBenderscuts(benders);
   nbenderscuts = SCIPbendersGetNBenderscuts(benders);

   /* getting the number of subproblems in the Benders' decompsition */
   nsubproblems = SCIPbendersGetNSubproblems(benders);

   /* in the case of an LNS check, only the convex relaxations of the subproblems will be solved. This is a performance
    * feature, since solving the convex relaxation is typically much faster than solving the corresponding CIP. While
    * the CIP is not solved during the LNS check, the solutions are still of higher quality than when Benders' is not
    * employed.
    */
   onlyconvexcheck = onlyCheckSubproblemConvexRelax(benders);

   /* It is only possible to add cuts to the problem if it has not already been solved */
   if( SCIPsetGetStage(set) < SCIP_STAGE_SOLVED )
   {
      SCIP_Longint addedcuts = 0;

      /* This is done in two loops. The first is by subproblem and the second is by cut type. */
      //for( i = 0; i < nsubproblems; i++ )
      i = benders->firstchecked;
      subproblemcount = 0;
      while( subproblemcount < nchecked )
      {
         SCIP_Bool convexsub = SCIPbendersSubprobIsConvex(benders, i);

         /* cuts can only be generated if the subproblem is not independent and if it has been solved. The subproblem
          * solved flag is important for the user-defined subproblem solving methods
          */
         if( !SCIPbendersSubprobIsIndependent(benders, i) && subprobsolved[i] )
         {
            for( j = 0; j < nbenderscuts; j++ )
            {
               SCIP_RESULT cutresult;
               SCIP_Longint prevaddedcuts;

               assert(benderscuts[j] != NULL);

               prevaddedcuts = SCIPbenderscutGetNFound(benderscuts[j]);

               cutresult = SCIP_DIDNOTRUN;

               /* if the subproblem is an LP, then only LP based cuts are generated. This is also only performed in
                * the first iteration of the solve loop.
                */
               if( (SCIPbenderscutIsLPCut(benderscuts[j]) && (solveloop == SCIP_BENDERSSOLVELOOP_CONVEX
                        || solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX))
                  || (!SCIPbenderscutIsLPCut(benderscuts[j]) && ((solveloop == SCIP_BENDERSSOLVELOOP_CIP && !convexsub)
                        || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP)) )
                  SCIP_CALL( SCIPbenderscutExec(benderscuts[j], set, benders, sol, i, type, &cutresult) );

               addedcuts += (SCIPbenderscutGetNFound(benderscuts[j]) - prevaddedcuts);

               /* the result is updated only if a Benders' cut is generated */
               if( cutresult == SCIP_CONSADDED || cutresult == SCIP_SEPARATED )
               {
                  *result = cutresult;

                  benders->ncutsfound++;

                  /* at most a single cut is generated for each subproblem */
                  break;
               }
            }
         }

         subproblemcount++;
         i++;
         if( i >= nsubproblems )
            i = 0;
      }

      /* if no cuts were added, then the number of solve loops is increased */
      if( addedcuts == 0 && SCIPbendersGetNConvexSubprobs(benders) < SCIPbendersGetNSubproblems(benders)
         && checkint && !onlyconvexcheck )
         (*nsolveloops) = 2;
   }

   return SCIP_OKAY;
}

/** solves the subproblem using the current master problem solution. */
/*  TODO: consider allowing the possibility to pass solution information back from the subproblems instead of the scip
 *  instance. This would allow the use of different solvers for the subproblems, more importantly allowing the use of an
 *  LP solver for LP subproblems. */
SCIP_RETCODE SCIPbendersExec(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_RESULT*          result,             /**< result of the pricing process */
   SCIP_Bool*            infeasible,         /**< is the master problem infeasible with respect to the Benders' cuts? */
   SCIP_Bool*            auxviol,            /**< set to TRUE only if the solution is feasible but the aux vars are violated */
   SCIP_BENDERSENFOTYPE  type,               /**< the type of solution being enforced */
   SCIP_Bool             checkint            /**< should the integer solution be checked by the subproblems */
   )
{
   int nsubproblems;
   int subproblemcount;
   int nchecked;
   int nsolveloops;     /* the number of times the subproblems are solved. An additional loop is required when integer
                           variables are in the subproblem */
   int i;
   int l;
   SCIP_Bool optimal;
   SCIP_Bool allverified;      /* flag to indicate whether all subproblems have been checked */
   int nverified;              /* the number of subproblems that have been checked */
   SCIP_Bool* subprobsolved;
   SCIP_Bool* subisinfeas;

   SCIPdebugMessage("Starting Benders' decomposition subproblem solving. type %d checkint %d\n", type, checkint);

   /* start timing */
   SCIPclockStart(benders->bendersclock, set);

   nsubproblems = SCIPbendersGetNSubproblems(benders);

   (*auxviol) = FALSE;
   (*infeasible) = FALSE;

   /* It is assumed that the problem is optimal, until a subproblem is found not to be optimal. However, not all
    * subproblems could be checked in each iteration. As such, it is not possible to state that the problem is optimal
    * if not all subproblems are checked. Situations where this may occur is when a subproblem is a MIP and only the LP
    * is solved. Also, in a distributed computation, then it may be advantageous to only solve some subproblems before
    * resolving the master problem. As such, for a problem to be optimal, then (optimal && allverified) == TRUE
    */
   optimal = TRUE;
   nverified = 0;

   assert(benders != NULL);
   assert(result != NULL);

   /* if the Benders' decomposition is called from a sub-scip, it is assumed that this is an LNS heuristic. As such, the
    * check is not performed and the solution is assumed to be feasible
    */
   if( benders->iscopy
      && (!benders->lnscheck
         || (benders->lnsmaxdepth > -1 && SCIPgetDepth(benders->sourcescip) > benders->lnsmaxdepth)) )
   {
      (*result) = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   /* it is not necessary to check all primal solutions by solving the Benders' decomposition subproblems.
    * Only the improving solutions are checked to improve efficiency of the algorithm.
    * If the solution is non-improving, the result FEASIBLE is returned. While this may be incorrect w.r.t to the
    * Benders' subproblems, this solution will never be the optimal solution. A non-improving solution may be used
    * within LNS primal heuristics. If this occurs, the improving solution, if found, will be checked by the solving
    * the Benders' decomposition subproblems.
    * TODO: Add a parameter to control this behaviour.
    */
   if( checkint && SCIPsetIsFeasLE(set, SCIPgetPrimalbound(set->scip), SCIPgetSolOrigObj(set->scip, sol)) )
   {
      (*result) = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   /* setting the first subproblem to check in this round of subproblem checks */
   benders->firstchecked = benders->lastchecked;

   /* allocating memory for the infeasible subproblem array */
   SCIP_CALL( SCIPallocClearBlockMemoryArray(set->scip, &subprobsolved, nsubproblems) );
   SCIP_CALL( SCIPallocClearBlockMemoryArray(set->scip, &subisinfeas, nsubproblems) );

   /* sets the stored objective function values of the subproblems to infinity */
   resetSubproblemObjectiveValue(benders);

   if( benders->benderspresubsolve != NULL )
      SCIP_CALL( benders->benderspresubsolve(set->scip, benders) );

   *result = SCIP_DIDNOTRUN;

   /* by default the number of solve loops is 1. This is the case if all subproblems are LP or the user has defined a
    * benderssolvesub callback. If there is a subproblem that is not an LP, then 2 solve loops are performed. The first
    * loop is the LP solving loop, the second solves the subproblem to integer optimality.
    */
   nsolveloops = 1;

   for( l = 0; l < nsolveloops; l++ )
   {
      SCIP_BENDERSSOLVELOOP solveloop;    /* identifies what problem type is solve in this solve loop */

      /* if either benderssolvesubconvex or benderssolvesub are implemented, then the user callbacks are invoked */
      if( benders->benderssolvesubconvex != NULL || benders->benderssolvesub != NULL )
      {
         if( l == 0 )
            solveloop = SCIP_BENDERSSOLVELOOP_USERCONVEX;
         else
            solveloop = SCIP_BENDERSSOLVELOOP_USERCIP;
      }
      else
         solveloop = l;


      /* solving the subproblems for this round of enforcement/checking. */
      SCIP_CALL( solveBendersSubproblems(benders, set, sol, type, solveloop, checkint, &nchecked, &nverified,
            subprobsolved, subisinfeas, infeasible, &optimal) );


      /* Generating cuts for the subproblems. Cuts are only generated when the solution is from primal heuristics,
       * relaxations or the LP
       */
      if( type != SCIP_BENDERSENFOTYPE_PSEUDO )
         SCIP_CALL( generateBendersCuts(benders, set, sol, result, type, solveloop, checkint, nchecked,
               subprobsolved, &nsolveloops) );
      else
      {
         /* if the problems are not infeasible, then the */
         if( !infeasible && checkint && !onlyCheckSubproblemConvexRelax(benders)
            && SCIPbendersGetNConvexSubprobs(benders) < SCIPbendersGetNSubproblems(benders))
            nsolveloops = 2;
      }

   }

   allverified = (nverified == nsubproblems);

#ifdef SCIP_DEBUG
   if( (*result) == SCIP_CONSADDED )
   {
      SCIPdebugMessage("Benders decomposition: Cut added\n");
   }
#endif

   if( type == SCIP_BENDERSENFOTYPE_PSEUDO )
   {
      if( (*infeasible) || !allverified )
         (*result) = SCIP_SOLVELP;
      else
      {
         (*result) = SCIP_FEASIBLE;

         /* if the subproblems are not infeasible, but they are also not optimal. This means that there is a violation
          * in the auxiliary variable values. In this case, a feasible result is returned with the auxviol flag set to
          * TRUE.
          */
         (*auxviol) = !optimal;
      }
   }
   else if( checkint && (type == SCIP_BENDERSENFOTYPE_CHECK || (*result) != SCIP_CONSADDED) )
   {
      /* if the subproblems are being solved as part of conscheck, then the results flag must be returned after the solving
       * has completed.
       */
      if( (*infeasible) || !allverified )
         (*result) = SCIP_INFEASIBLE;
      else
      {
         (*result) = SCIP_FEASIBLE;

         /* if the subproblems are not infeasible, but they are also not optimal. This means that there is a violation
          * in the auxiliary variable values. In this case, a feasible result is returned with the auxviol flag set to
          * TRUE.
          */
         (*auxviol) = !optimal;
      }
   }

   /* calling the post-solve call back for the Benders' decomposition algorithm. This allows the user to work directly
    * with the solved subproblems and the master problem */
   if( benders->benderspostsolve != NULL )
   {
      SCIP_CALL( benders->benderspostsolve(set->scip, benders, sol, (*infeasible)) );
   }

   /* freeing the subproblems after the cuts are generated */
   i = benders->firstchecked;
   subproblemcount = 0;
   while( subproblemcount < nchecked )
   /*for( i = 0; i < benders->nsubproblems; i++ )*/
   {
      SCIP_CALL( SCIPbendersFreeSubproblem(benders, set, i) );

      subproblemcount++;
      i++;
      if( i >= nsubproblems )
         i = 0;
   }

   /* increment the number of calls to the Benders' decomposition subproblem solve */
   benders->ncalls++;

   SCIPdebugMessage("End Benders' decomposition subproblem solve. result %d infeasible %d auxviol %d\n", *result,
      *infeasible, *auxviol);

   /* end timing */
   SCIPclockStop(benders->bendersclock, set);

   /* freeing memory */
   SCIPfreeBlockMemoryArray(set->scip, &subisinfeas, nsubproblems);
   SCIPfreeBlockMemoryArray(set->scip, &subprobsolved, nsubproblems);

   return SCIP_OKAY;
}

/** solves the user-defined subproblem solving function */
static
SCIP_RETCODE executeUserDefinedSolvesub(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,         /**< the subproblem number */
   SCIP_BENDERSSOLVELOOP solveloop,          /**< the solve loop iteration. The first iter is for LP, the second for IP */
   SCIP_Bool*            infeasible,         /**< returns whether the current subproblem is infeasible */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_Real*            objective,          /**< the objective function value of the subproblem */
   SCIP_RESULT*          result              /**< the result from solving the subproblem */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);
   assert(benders->benderssolvesubconvex != NULL || benders->benderssolvesub != NULL);

   assert(solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP);

   (*objective) = -SCIPsetInfinity(set);

   /* calls the user defined subproblem solving method. Only the convex relaxations are solved during the Large
    * Neighbourhood Benders' Search. */
   if( solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX )
   {
      if( benders->benderssolvesubconvex != NULL )
      {
         SCIP_CALL( benders->benderssolvesubconvex(set->scip, benders, sol, probnumber,
               onlyCheckSubproblemConvexRelax(benders), objective, result) );
      }
      else
         (*result) = SCIP_DIDNOTRUN;
   }
   else if( solveloop == SCIP_BENDERSSOLVELOOP_USERCIP )
   {
      if( benders->benderssolvesub != NULL )
      {
         SCIP_CALL( benders->benderssolvesub(set->scip, benders, sol, probnumber, objective, result) );
      }
      else
         (*result) = SCIP_DIDNOTRUN;
   }

   /* evaluate result */
   if( (*result) != SCIP_DIDNOTRUN
      && (*result) != SCIP_FEASIBLE
      && (*result) != SCIP_INFEASIBLE
      && (*result) != SCIP_UNBOUNDED )
   {
      SCIPerrorMessage("the user-defined solving method for the Benders' decomposition <%s> returned invalid result <%d>\n",
         benders->name, *result);
      return SCIP_INVALIDRESULT;
   }

   if( (*result) == SCIP_INFEASIBLE )
      (*infeasible) = TRUE;

   if( (*result) == SCIP_FEASIBLE
      && !(SCIPsetIsGT(set, (*objective), -SCIPsetInfinity(set))
      && SCIPsetIsLT(set, (*objective), SCIPsetInfinity(set))) )
   {
      SCIPerrorMessage("the user-defined solving method for the Benders' decomposition <%s> returned objective value %g\n",
         benders->name, (*objective));
      return SCIP_ERROR;
   }

   return SCIP_OKAY;
}

/** solves the subproblems. */
SCIP_RETCODE SCIPbendersExecSubproblemSolve(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,            /**< the subproblem number */
   SCIP_BENDERSSOLVELOOP solveloop,          /**< the solve loop iteration. The first iter is for LP, the second for IP */
   SCIP_Bool             enhancement,        /**< is the solve performed as part of and enhancement? */
   SCIP_Bool*            solved,             /**< flag to indicate whether the subproblem was solved */
   SCIP_Bool*            infeasible,         /**< returns whether the current subproblem is infeasible */
   SCIP_BENDERSENFOTYPE  type                /**< the enforcement type calling this function */
   )
{
   SCIP* subproblem;
   SCIP_SOL* bestsol;
   SCIP_RESULT result;
   SCIP_Real objective;

   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);

   SCIPdebugMessage("Benders decomposition: solving subproblem %d\n", probnumber);

   /* initially setting the solved flag to FALSE */
   (*solved) = FALSE;

   /* if the subproblem solve callback is implemented, then that is used instead of the default setup */
   if( solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP )
   {
      /* calls the user defined subproblem solving method. Only the convex relaxations are solved during the Large
       * Neighbourhood Benders' Search. */
      SCIP_CALL( executeUserDefinedSolvesub(benders, set, sol, probnumber, solveloop, infeasible, type, &objective,
            &result) );

      /* if the result is DIDNOTRUN, then the subproblem was not solved */
      (*solved) = (result != SCIP_DIDNOTRUN);
   }
   else
   {
      /* setting up the subproblem */
      if( solveloop == SCIP_BENDERSSOLVELOOP_CONVEX )
         SCIP_CALL( SCIPbendersSetupSubproblem(benders, set, sol, probnumber) );
      else
         SCIP_CALL( updateEventhdlrUpperbound(benders, probnumber, SCIPbendersGetAuxiliaryVarVal(benders, set, sol, probnumber)) );


      /* solving the subproblem
       * the LP of the subproblem is solved in the first solveloop.
       * In the second solve loop, the MIP problem is solved */
      if( solveloop == SCIP_BENDERSSOLVELOOP_CONVEX || SCIPbendersSubprobIsConvex(benders, probnumber) )
         SCIP_CALL( SCIPbendersSolveSubproblemLP(benders, probnumber, infeasible) );
      else
         SCIP_CALL( SCIPbendersSolveSubproblemMIP(benders, probnumber, infeasible, type, FALSE) );

      /* if the generic subproblem solving methods are used, then the subproblems are always solved. */
      (*solved) = TRUE;
   }

   subproblem = SCIPbendersSubproblem(benders, probnumber);

   bestsol = SCIPgetBestSol(subproblem);

   if( !enhancement )
   {
      /* The following handles the cases when the subproblem is OPTIMAL, INFEASIBLE and UNBOUNDED.
       * If a subproblem is unbounded, then the auxiliary variables are set to -infinity and the unbounded flag is
       * returned as TRUE. No cut will be generated, but the result will be set to SCIP_FEASIBLE.
       */
      if( solveloop == SCIP_BENDERSSOLVELOOP_CONVEX )
      {
         if( SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_OPTIMAL )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPgetSolOrigObj(subproblem, NULL));
         else if( SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_INFEASIBLE )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPsetInfinity(set));
         else if( SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_UNBOUNDEDRAY )
         {
            SCIPerrorMessage("The LP of Benders' decomposition subproblem %d is unbounded. This should not happen.\n",
               probnumber);
            SCIPABORT();
         }
         else
         {
            SCIPerrorMessage("Invalid status returned from solving the LP of Benders' decomposition subproblem %d. LP status: %d\n",
               probnumber, SCIPgetLPSolstat(subproblem));
            SCIPABORT();
         }
      }
      else if( solveloop == SCIP_BENDERSSOLVELOOP_CIP )
      {
         /* TODO: Consider whether other solutions status should be handled */
         if( SCIPgetStatus(subproblem) == SCIP_STATUS_OPTIMAL )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPgetSolOrigObj(subproblem, bestsol));
         else if( SCIPgetStatus(subproblem) == SCIP_STATUS_INFEASIBLE )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPsetInfinity(set));
         else if( SCIPgetStatus(subproblem) == SCIP_STATUS_USERINTERRUPT || SCIPgetStatus(subproblem) == SCIP_STATUS_BESTSOLLIMIT )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPgetSolOrigObj(subproblem, bestsol));
         else if( SCIPgetStatus(subproblem) == SCIP_STATUS_UNBOUNDED )
         {
            SCIPerrorMessage("The Benders' decomposition subproblem %d is unbounded. This should not happen.\n",
               probnumber);
            SCIPABORT();
         }
         else
         {
            SCIPerrorMessage("Invalid status returned from solving Benders' decomposition subproblem %d. Solution status: %d\n",
               probnumber, SCIPgetStatus(subproblem));
            SCIPABORT();
         }
      }
      else
      {
         assert(solveloop == SCIP_BENDERSSOLVELOOP_USERCONVEX || solveloop == SCIP_BENDERSSOLVELOOP_USERCIP);
         if( result == SCIP_FEASIBLE )
            SCIPbendersSetSubprobObjval(benders, probnumber, objective);
         else if( result == SCIP_INFEASIBLE )
            SCIPbendersSetSubprobObjval(benders, probnumber, SCIPsetInfinity(set));
         else if( result == SCIP_UNBOUNDED )
         {
            SCIPerrorMessage("The Benders' decomposition subproblem %d is unbounded. This should not happen.\n",
               probnumber);
            SCIPABORT();
         }
         else if( result != SCIP_DIDNOTRUN )
         {
            SCIPerrorMessage("Invalid result <%d> from user-defined subproblem solving method. This should not happen.\n",
               result);
         }
      }
   }

   return SCIP_OKAY;
}

/** sets up the subproblem using the solution to the master problem  */
SCIP_RETCODE SCIPbendersSetupSubproblem(
   SCIP_BENDERS*         benders,            /**< variable benders */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber          /**< the subproblem number */
   )
{
   SCIP* subproblem;
   SCIP_VAR** vars;
   SCIP_VAR* mastervar;
   SCIP_Real solval;
   int nvars;
   int i;

   assert(benders != NULL);
   assert(set != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   /* changing all of the master problem variable to continuous. */
   SCIP_CALL( SCIPbendersChgMastervarsToCont(benders, set, probnumber) );

   subproblem = SCIPbendersSubproblem(benders, probnumber);

   /* if the Benders subproblem is an LP, then probing mode must be started.
    * If the subproblem is a MIP, the problem must be initialised, put into SCIP_STAGE_SOLVING to be able to change the
    * variable bounds. The probing mode is entered once the variable bounds are set.
    * In the MIP case, the transformed problem is freed after each subproblem solve round. */
   if( SCIPbendersSubprobIsConvex(benders, probnumber) )
      SCIP_CALL( SCIPstartProbing(subproblem) );
   else
      SCIP_CALL( initialiseSubproblem(benders, probnumber) );

   vars = SCIPgetVars(subproblem);
   nvars = SCIPgetNVars(subproblem);

   /* looping over all variables in the subproblem to find those corresponding to the master problem variables. */
   /* TODO: It should be possible to store the pointers to the master variables to speed up the subproblem setup */
   for( i = 0; i < nvars; i++ )
   {
      SCIP_CALL( SCIPbendersGetVar(benders, set, vars[i], &mastervar, -1) );

      if( mastervar != NULL )
      {
         /** It is possible due to numerics that the solution value exceeds the upper or lower bounds. When this
          * happens, it causes an error in the LP solver as a result of inconsistent bounds. So the following statements
          * are used to enusure that the bounds are not exceeded when applying the fixings for the Benders'
          * decomposition subproblems
          */
         solval = SCIPgetSolVal(set->scip, sol, mastervar);
         if( solval > SCIPvarGetUbLocal(vars[i]) )
            solval = SCIPvarGetUbLocal(vars[i]);
         else if( solval < SCIPvarGetLbLocal(vars[i]) )
            solval = SCIPvarGetLbLocal(vars[i]);

         /* fixing the variable in the subproblem */
         if( !SCIPisEQ(subproblem, SCIPvarGetLbLocal(vars[i]), SCIPvarGetUbLocal(vars[i])) )
         {
            if( SCIPisGT(subproblem, solval, SCIPvarGetLbLocal(vars[i])) )
               SCIP_CALL( SCIPchgVarLb(subproblem, vars[i], solval) );
            if( SCIPisLT(subproblem, solval, SCIPvarGetUbLocal(vars[i])) )
               SCIP_CALL( SCIPchgVarUb(subproblem, vars[i], solval) );
         }

         assert(SCIPisEQ(subproblem, SCIPvarGetLbLocal(vars[i]), SCIPvarGetUbLocal(vars[i])));
      }
   }

   /* if the subproblem is a MIP, the probing mode is entered after setting up the subproblem */
   if( !SCIPbendersSubprobIsConvex(benders, probnumber) )
      SCIP_CALL( SCIPstartProbing(subproblem) );

   /* set the flag to indicate that the subproblems have been set up */
   SCIPbendersSetSubprobIsSetup(benders, probnumber, TRUE);

   return SCIP_OKAY;
}

/** Solve a Benders' decomposition subproblems. This will either call the user defined method or the generic solving
 * methods. If the generic method is called, then the subproblem must be set up before calling this method. */
SCIP_RETCODE SCIPbendersSolveSubproblem(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution, can be NULL */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool*            infeasible,         /**< returns whether the current subproblem is infeasible */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_Bool             solvemip,           /**< directly solve the MIP subproblem */
   SCIP_Real*            objective           /**< the objective function value of the subproblem, can be NULL */
   )
{
   assert(benders != NULL);
   assert(set != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   /* the subproblem must be set up before this function is called. */
   if( SCIPbendersSubprobIsSetup(benders, probnumber) )
   {
      SCIPerrorMessage("Benders subproblem %d must be set up before calling SCIPbendersSolveSubproblem(). Call SCIPsetupSubproblem() first.\n", probnumber);
      return SCIP_ERROR;
   }

   /* if the subproblem solve callback is implemented, then that is used instead of the default setup */
   if( benders->benderssolvesubconvex != NULL ||  benders->benderssolvesub != NULL)
   {
      SCIP_BENDERSSOLVELOOP solveloop;
      SCIP_RESULT result;
      SCIP_Real subobj;

      if( solvemip )
         solveloop = SCIP_BENDERSSOLVELOOP_USERCIP;
      else
         solveloop = SCIP_BENDERSSOLVELOOP_USERCONVEX;

      SCIP_CALL( executeUserDefinedSolvesub(benders, set, sol, probnumber, solveloop, infeasible, type, &subobj,
            &result) );

      if( objective != NULL )
         (*objective) = subobj;
   }
   else
   {
      SCIP* subproblem;

      subproblem = SCIPbendersSubproblem(benders, probnumber);

      /* solving the subproblem */
      if( solvemip )
      {
         SCIP_CALL( SCIPbendersSolveSubproblemMIP(benders, probnumber, infeasible, type, solvemip) );

         if( objective != NULL )
            (*objective) = SCIPgetSolOrigObj(subproblem, SCIPgetBestSol(subproblem));
      }
      else
      {
         /* if the subproblem is an LP, then it should have been initialised and in SCIP_STAGE_SOLVING.
          * in this case, the subproblem only needs to be put into probing mode. */
         if( SCIPbendersSubprobIsConvex(benders, probnumber) )
         {
            /* if the subproblem is not in probing mode, then it must be put into that mode for the LP solve. */
            if( !SCIPinProbing(subproblem) )
               SCIP_CALL( SCIPstartProbing(subproblem) );
         }
         else
            SCIP_CALL( initialiseSubproblem(benders, probnumber) );

         SCIP_CALL( SCIPbendersSolveSubproblemLP(benders, probnumber, infeasible) );

         if( objective != NULL )
            (*objective) = SCIPgetSolOrigObj(subproblem, NULL);
      }
   }

   return SCIP_OKAY;
}

/** stores the original parameters from the subproblem */
static
SCIP_RETCODE storeOrigSubprobParams(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_SUBPROBPARAMS*   origparams          /**< the original subproblem parameters */
   )
{
   assert(scip != NULL);
   assert(origparams != NULL);

   SCIP_CALL( SCIPgetBoolParam(scip, "conflict/enable", &origparams->conflict_enable) );
   SCIP_CALL( SCIPgetIntParam(scip, "lp/disablecutoff", &origparams->lp_disablecutoff) );
   SCIP_CALL( SCIPgetIntParam(scip, "lp/scaling", &origparams->lp_scaling) );
   SCIP_CALL( SCIPgetCharParam(scip, "lp/initalgorithm", &origparams->lp_initalg) );
   SCIP_CALL( SCIPgetCharParam(scip, "lp/resolvealgorithm", &origparams->lp_resolvealg) );
   SCIP_CALL( SCIPgetBoolParam(scip, "misc/alwaysgetduals", &origparams->misc_alwaysgetduals) );
   SCIP_CALL( SCIPgetBoolParam(scip, "misc/scaleobj", &origparams->misc_scaleobj) );
   SCIP_CALL( SCIPgetBoolParam(scip, "misc/catchctrlc", &origparams->misc_catchctrlc) );
   SCIP_CALL( SCIPgetIntParam(scip, "propagating/maxrounds", &origparams->prop_maxrounds) );
   SCIP_CALL( SCIPgetIntParam(scip, "propagating/maxroundsroot", &origparams->prop_maxroundsroot) );
   SCIP_CALL( SCIPgetIntParam(scip, "constraints/linear/propfreq", &origparams->cons_linear_propfreq) );

   return SCIP_OKAY;
}

/** sets the parameters for the subproblem */
static
SCIP_RETCODE setSubprobParams(
   SCIP*                 scip                /**< the SCIP data structure */
   )
{
   assert(scip != NULL);

   /* Do we have to disable presolving? If yes, we have to store all presolving parameters. */
   SCIP_CALL( SCIPsetPresolving(scip, SCIP_PARAMSETTING_OFF, TRUE) );

   /* Disabling heuristics so that the problem is not trivially solved */
   SCIP_CALL( SCIPsetHeuristics(scip, SCIP_PARAMSETTING_OFF, TRUE) );

   /* store parameters that are changed for the generation of the subproblem cuts */
   SCIP_CALL( SCIPsetParam(scip, "conflict/enable", FALSE) );

   SCIP_CALL( SCIPsetIntParam(scip, "lp/disablecutoff", 1) );
   SCIP_CALL( SCIPsetIntParam(scip, "lp/scaling", 0) );

   SCIP_CALL( SCIPsetCharParam(scip, "lp/initalgorithm", 'd') );
   SCIP_CALL( SCIPsetCharParam(scip, "lp/resolvealgorithm", 'd') );

   SCIP_CALL( SCIPsetBoolParam(scip, "misc/alwaysgetduals", TRUE) );
   SCIP_CALL( SCIPsetBoolParam(scip, "misc/scaleobj", FALSE) );

   /* do not abort subproblem on CTRL-C */
   SCIP_CALL( SCIPsetBoolParam(scip, "misc/catchctrlc", FALSE) );

   SCIP_CALL( SCIPsetIntParam(scip, "display/verblevel", (int)SCIP_VERBLEVEL_NONE) );

   SCIP_CALL( SCIPsetIntParam(scip, "propagating/maxrounds", 0) );
   SCIP_CALL( SCIPsetIntParam(scip, "propagating/maxroundsroot", 0) );

   SCIP_CALL( SCIPsetIntParam(scip, "constraints/linear/propfreq", -1) );

   return SCIP_OKAY;
}

/** resets the original parameters from the subproblem */
static
SCIP_RETCODE resetOrigSubprobParams(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_SUBPROBPARAMS*   origparams          /**< the original subproblem parameters */
   )
{
   assert(scip != NULL);
   assert(origparams != NULL);

   SCIP_CALL( SCIPsetBoolParam(scip, "conflict/enable", origparams->conflict_enable) );
   SCIP_CALL( SCIPsetIntParam(scip, "lp/disablecutoff", origparams->lp_disablecutoff) );
   SCIP_CALL( SCIPsetIntParam(scip, "lp/scaling", origparams->lp_scaling) );
   SCIP_CALL( SCIPsetCharParam(scip, "lp/initalgorithm", origparams->lp_initalg) );
   SCIP_CALL( SCIPsetCharParam(scip, "lp/resolvealgorithm", origparams->lp_resolvealg) );
   SCIP_CALL( SCIPsetBoolParam(scip, "misc/alwaysgetduals", origparams->misc_alwaysgetduals) );
   SCIP_CALL( SCIPsetBoolParam(scip, "misc/scaleobj", origparams->misc_scaleobj) );
   SCIP_CALL( SCIPsetBoolParam(scip, "misc/catchctrlc", origparams->misc_catchctrlc) );
   SCIP_CALL( SCIPsetIntParam(scip, "propagating/maxrounds", origparams->prop_maxrounds) );
   SCIP_CALL( SCIPsetIntParam(scip, "propagating/maxroundsroot", origparams->prop_maxroundsroot) );
   SCIP_CALL( SCIPsetIntParam(scip, "constraints/linear/propfreq", origparams->cons_linear_propfreq) );

   return SCIP_OKAY;
}

/** solves the LP of the Benders' decomposition subproblem. This requires that the subproblem is in probing mode */
SCIP_RETCODE SCIPbendersSolveSubproblemLP(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition data structure */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool*            infeasible          /**< a flag to indicate whether all subproblems are feasible */
   )
{
   SCIP* subproblem;
   SCIP_SUBPROBPARAMS* origparams;
   SCIP_Bool lperror;
   SCIP_Bool cutoff;

   assert(benders != NULL);
   assert(infeasible != NULL);
   assert(SCIPbendersSubprobIsSetup(benders, probnumber));

   (*infeasible) = FALSE;


   /* TODO: This should be solved just as an LP, so as a MIP. There is too much overhead with the MIP.
    * Need to change status check for checking the LP. */
   subproblem = SCIPbendersSubproblem(benders, probnumber);

   assert(SCIPisLPConstructed(subproblem));
   assert(SCIPinProbing(subproblem));

   /* allocating memory for the parameter storage */
   SCIP_CALL( SCIPallocBlockMemory(subproblem, &origparams) );

   /* store the original parameters of the subproblem */
   SCIP_CALL( storeOrigSubprobParams(subproblem, origparams) );

   /* setting the subproblem parameters */
   SCIP_CALL( setSubprobParams(subproblem) );

   SCIP_CALL( SCIPsolveProbingLP(subproblem, -1, &lperror, &cutoff) );

   assert(!lperror);

   if( SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_INFEASIBLE )
      (*infeasible) = TRUE;
   else if( SCIPgetLPSolstat(subproblem) != SCIP_LPSOLSTAT_OPTIMAL
      && SCIPgetLPSolstat(subproblem) != SCIP_LPSOLSTAT_UNBOUNDEDRAY )
   {
      SCIPerrorMessage("Invalid status: %d. Solving the LP relaxation of Benders' decomposition subproblem %d.\n",
         SCIPgetLPSolstat(subproblem), probnumber);
      SCIPABORT();
   }

   /* resetting the subproblem parameters */
   SCIP_CALL( resetOrigSubprobParams(subproblem, origparams) );

   /* freeing the parameter storage */
   SCIPfreeBlockMemory(subproblem, &origparams);

   return SCIP_OKAY;
}

/** solves the Benders' decomposition subproblem. */
SCIP_RETCODE SCIPbendersSolveSubproblemMIP(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition data structure */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool*            infeasible,         /**< returns whether the current subproblem is infeasible */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_Bool             solvemip            /**< directly solve the MIP subproblem */
   )
{
   SCIP* subproblem;
   SCIP_SUBPROBPARAMS* origparams;

   assert(benders != NULL);
   assert(infeasible != NULL);

   (*infeasible) = FALSE;

   subproblem = SCIPbendersSubproblem(benders, probnumber);

   /* allocating memory for the parameter storage */
   SCIP_CALL( SCIPallocBlockMemory(subproblem, &origparams) );

   /* store the original parameters of the subproblem */
   SCIP_CALL( storeOrigSubprobParams(subproblem, origparams) );

   /* If the solve has been stopped for the subproblem, then we need to restart it to complete the solve. The subproblem
    * is stopped when it is a MIP so that LP cuts and IP cuts can be generated. */
   if( SCIPgetStage(subproblem) == SCIP_STAGE_SOLVING )
   {
      /* the subproblem should be in probing mode. Otherwise, the eventhandler did not work correctly */
      assert( SCIPinProbing(subproblem) );

      /* the probing mode needs to be stopped so that the MIP can be solved */
      SCIP_CALL( SCIPendProbing(subproblem) );

      /* the problem was interrupted in the event handler, so SCIP needs to be informed that the problem is to be restarted */
      SCIP_CALL( SCIPrestartSolve(subproblem) );

      /* if the solve type is for CHECK, then the FEASIBILITY emphasis setting is used. */
      if( type == SCIP_BENDERSENFOTYPE_CHECK )
      {
         SCIP_CALL( SCIPsetHeuristics(subproblem, SCIP_PARAMSETTING_FAST, TRUE) );

         /* the number of solution improvements is limited to try and prove feasibility quickly */
         /* NOTE: This should be a parameter */
         /* SCIP_CALL( SCIPsetIntParam(subproblem, "limits/bestsol", 5) ); */
      }
   }
   else if( solvemip )
   {
      /* if the MIP will be solved directly, then the probing mode needs to be skipped.
       * This is achieved by setting the solvemip flag in the event handler data to TRUE
       */
      SCIP_EVENTHDLR* eventhdlr;
      SCIP_EVENTHDLRDATA* eventhdlrdata;

      eventhdlr = SCIPfindEventhdlr(subproblem, MIPNODEFOCUS_EVENTHDLR_NAME);
      eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);

      eventhdlrdata->solvemip = TRUE;
   }
   else
   {
      /* if the problem is not in probing mode, then we need to solve the LP. That requires all methods that will
       * modify the structure of the problem need to be deactivated */

      /* setting the subproblem parameters */
      SCIP_CALL( setSubprobParams(subproblem) );

#ifdef SCIP_MOREDEBUG
      SCIP_CALL( SCIPsetBoolParam(subproblem, "display/lpinfo", TRUE) );
#endif
   }

#ifdef SCIP_MOREDEBUG
      SCIP_CALL( SCIPsetIntParam(subproblem, "display/verblevel", (int)SCIP_VERBLEVEL_FULL) );
#endif

   SCIP_CALL( SCIPsolve(subproblem) );

   if( SCIPgetStatus(subproblem) == SCIP_STATUS_INFEASIBLE )
      (*infeasible) = TRUE;
   else if( SCIPgetStatus(subproblem) != SCIP_STATUS_OPTIMAL && SCIPgetStatus(subproblem) != SCIP_STATUS_UNBOUNDED
      && SCIPgetStatus(subproblem) != SCIP_STATUS_USERINTERRUPT && SCIPgetStatus(subproblem) != SCIP_STATUS_BESTSOLLIMIT)
   {
      SCIPerrorMessage("Invalid status: %d. Solving the CIP of Benders' decomposition subproblem %d.\n",
         SCIPgetLPSolstat(subproblem), probnumber);
      SCIPABORT();
   }

   /* resetting the subproblem parameters */
   SCIP_CALL( resetOrigSubprobParams(subproblem, origparams) );

   /* freeing the parameter storage */
   SCIPfreeBlockMemory(subproblem, &origparams);

   return SCIP_OKAY;
}

/** frees the subproblems. */
SCIP_RETCODE SCIPbendersFreeSubproblem(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(benders->bendersfreesub != NULL
      || (benders->bendersfreesub == NULL && benders->benderssolvesubconvex == NULL && benders->benderssolvesub == NULL));
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);

   if( benders->bendersfreesub != NULL )
      SCIP_CALL( benders->bendersfreesub(set->scip, benders, probnumber) );
   else
   {
      /* the subproblem is only freed if it is not independent */
      if( !SCIPbendersSubprobIsIndependent(benders, probnumber) )
      {
         SCIP* subproblem = SCIPbendersSubproblem(benders, probnumber);

         if( SCIPbendersSubprobIsConvex(benders, probnumber) )
         {
            /* ending probing mode to reset the current node. The probing mode will be restarted at the next solve */
            SCIP_CALL( SCIPendProbing(subproblem) );
         }
         else
         {
            /* if the subproblems were solved as part of an enforcement stage, then they will still be in probing mode. The
             * probing mode must first be finished and then the problem can be freed */
            if( SCIPgetStage(subproblem) >= SCIP_STAGE_TRANSFORMED && SCIPinProbing(subproblem) )
               SCIP_CALL( SCIPendProbing(subproblem) );

            SCIP_CALL( SCIPfreeTransform(subproblem) );
         }
      }
   }

   /* setting the setup flag for the subproblem to FALSE */
   SCIPbendersSetSubprobIsSetup(benders, probnumber, FALSE);
   return SCIP_OKAY;
}

/** compares the subproblem objective value with the auxiliary variable value for optimality */
SCIP_RETCODE SCIPbendersCheckSubprobOptimality(
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool*            optimal             /**< flag to indicate whether the current subproblem is optimal for the master */
   )
{
   SCIP_Real auxiliaryvarval;
   SCIP_Real soltol;

   assert(benders != NULL);
   assert(set != NULL);
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);

   (*optimal) = FALSE;

   auxiliaryvarval = SCIPbendersGetAuxiliaryVarVal(benders, set, sol, probnumber);

   SCIP_CALL( SCIPsetGetRealParam(set, "benders/solutiontol", &soltol) );

   SCIPsetDebugMsg(set, "Subproblem %d - Auxiliary Variable: %g Subproblem Objective: %g Reldiff: %g Soltol: %g\n",
      probnumber, auxiliaryvarval, SCIPbendersGetSubprobObjval(benders, probnumber),
      SCIPrelDiff(SCIPbendersGetSubprobObjval(benders, probnumber), auxiliaryvarval), soltol);

   if( SCIPrelDiff(SCIPbendersGetSubprobObjval(benders, probnumber), auxiliaryvarval) < soltol )
      (*optimal) = TRUE;

   return SCIP_OKAY;
}

/** returns the value of the auxiliary variable value in a master problem solution */
SCIP_Real SCIPbendersGetAuxiliaryVarVal(
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber          /**< the subproblem number */
   )
{
   SCIP_VAR* auxiliaryvar;

   assert(benders != NULL);
   assert(set != NULL);

   auxiliaryvar = SCIPbendersGetAuxiliaryVar(benders, probnumber);
   assert(auxiliaryvar != NULL);

   return SCIPgetSolVal(set->scip, sol, auxiliaryvar);
}

/** returns the corresponding master or subproblem variable for the given variable.
 * This provides a call back for the variable mapping between the master and subproblems */
SCIP_RETCODE SCIPbendersGetVar(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             var,                /**< the variable for which the corresponding variable is desired */
   SCIP_VAR**            mappedvar,          /**< the variable that is mapped to var */
   int                   probnumber          /**< the problem number for the desired variable, -1 for the master problem */
   )
{
   assert(benders != NULL);
   assert(set != NULL);
   assert(var != NULL);
   assert(mappedvar != NULL);
   assert(benders->bendersgetvar != NULL);

   (*mappedvar) = NULL;

   /* if the variable name matches the auxiliary variable, then the master variable is returned as NULL */
   if( strstr(SCIPvarGetName(var), AUXILIARYVAR_NAME) != NULL )
      return SCIP_OKAY;

   SCIP_CALL( benders->bendersgetvar(set->scip, benders, var, mappedvar, probnumber) );

   return SCIP_OKAY;
}

/** gets user data of Benders' decomposition */
SCIP_BENDERSDATA* SCIPbendersGetData(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->bendersdata;
}

/** sets user data of Benders' decomposition; user has to free old data in advance! */
void SCIPbendersSetData(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_BENDERSDATA*     bendersdata         /**< new Benders' decomposition user data */
   )
{
   assert(benders != NULL);

   benders->bendersdata = bendersdata;
}

/** sets copy callback of benders */
void SCIPbendersSetCopy(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSCOPY ((*benderscopy))    /**< copy callback of benders */
   )
{
   assert(benders != NULL);

   benders->benderscopy = benderscopy;
}

/** sets destructor callback of Benders' decomposition */
void SCIPbendersSetFree(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSFREE ((*bendersfree))    /**< destructor of Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersfree = bendersfree;
}

/** sets initialization callback of Benders' decomposition */
void SCIPbendersSetInit(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINIT((*bendersinit))     /**< initialize the Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersinit = bendersinit;
}

/** sets deinitialization callback of Benders' decomposition */
void SCIPbendersSetExit(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXIT((*bendersexit))     /**< deinitialize the Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersexit = bendersexit;
}

/** sets presolving initialization callback of Benders' decomposition */
void SCIPbendersSetInitpre(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINITPRE((*bendersinitpre))/**< initialize presolving for Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersinitpre = bendersinitpre;
}

/** sets presolving deinitialization callback of Benders' decomposition */
void SCIPbendersSetExitpre(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXITPRE((*bendersexitpre))/**< deinitialize presolving for Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersexitpre = bendersexitpre;
}

/** sets solving process initialization callback of Benders' decomposition */
void SCIPbendersSetInitsol(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINITSOL((*bendersinitsol))/**< solving process initialization callback of Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersinitsol = bendersinitsol;
}

/** sets solving process deinitialization callback of Benders' decomposition */
void SCIPbendersSetExitsol(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXITSOL((*bendersexitsol))/**< solving process deinitialization callback of Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->bendersexitsol = bendersexitsol;
}

/** sets the pre subproblem solve callback of Benders' decomposition */
void SCIPbendersSetPresubsolve(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSPRESUBSOLVE((*benderspresubsolve))/**< called prior to the subproblem solving loop */
   )
{
   assert(benders != NULL);

   benders->benderspresubsolve = benderspresubsolve;
}

/** sets convex solve callback of Benders' decomposition */
void SCIPbendersSetSolvesubconvex(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSSOLVESUBCONVEX((*benderssolvesubconvex))/**< solving method for the convex Benders' decomposition subproblem */
   )
{
   assert(benders != NULL);

   benders->benderssolvesubconvex = benderssolvesubconvex;
}

/** sets solve callback of Benders' decomposition */
void SCIPbendersSetSolvesub(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSSOLVESUB((*benderssolvesub))/**< solving method for a Benders' decomposition subproblem */
   )
{
   assert(benders != NULL);

   benders->benderssolvesub = benderssolvesub;
}

/** sets post-solve callback of Benders' decomposition */
void SCIPbendersSetPostsolve(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSPOSTSOLVE((*benderspostsolve))/**< solving process deinitialization callback of Benders' decomposition */
   )
{
   assert(benders != NULL);

   benders->benderspostsolve = benderspostsolve;
}

/** sets free subproblem callback of Benders' decomposition */
void SCIPbendersSetFreesub(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSFREESUB((*bendersfreesub))/**< the freeing callback for the subproblem */
   )
{
   assert(benders != NULL);

   benders->bendersfreesub = bendersfreesub;
}

/** gets name of Benders' decomposition */
const char* SCIPbendersGetName(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->name;
}

/** gets description of Benders' decomposition */
const char* SCIPbendersGetDesc(
   SCIP_BENDERS*         benders             /**< Benders' dnumberecomposition */
   )
{
   assert(benders != NULL);

   return benders->desc;
}

/** gets priority of Benders' decomposition */
int SCIPbendersGetPriority(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->priority;
}

/** sets priority of Benders' decomposition */
void SCIPbendersSetPriority(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   priority            /**< new priority of the Benders' decomposition */
   )
{
   assert(benders != NULL);
   assert(set != NULL);

   benders->priority = priority;
   set->benderssorted = FALSE;
}

/** gets the number of subproblems for the Benders' decomposition */
int SCIPbendersGetNSubproblems(
   SCIP_BENDERS*         benders             /**< the Benders' decomposition data structure */
   )
{
   assert(benders != NULL);

   return benders->nsubproblems;
}

/** returns the SCIP instance for a given subproblem */
SCIP* SCIPbendersSubproblem(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition data structure */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < benders->nsubproblems);

   return benders->subproblems[probnumber];
}

/** gets the number of times, the benders was called and tried to find a variable with negative reduced costs */
int SCIPbendersGetNCalls(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->ncalls;
}

/** gets the number of optimality cuts found by the collection of Benders' decomposition subproblems */
int SCIPbendersGetNCutsFound(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->ncutsfound;
}

/** gets time in seconds used in this benders for setting up for next stages */
SCIP_Real SCIPbendersGetSetupTime(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return SCIPclockGetTime(benders->setuptime);
}

/** gets time in seconds used in this benders */
SCIP_Real SCIPbendersGetTime(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return SCIPclockGetTime(benders->bendersclock);
}

/** enables or disables all clocks of \p benders, depending on the value of the flag */
void SCIPbendersEnableOrDisableClocks(
   SCIP_BENDERS*         benders,            /**< the benders for which all clocks should be enabled or disabled */
   SCIP_Bool             enable              /**< should the clocks of the benders be enabled? */
   )
{
   assert(benders != NULL);

   SCIPclockEnableOrDisable(benders->setuptime, enable);
   SCIPclockEnableOrDisable(benders->bendersclock, enable);
}

/** is Benders' decomposition initialized? */
SCIP_Bool SCIPbendersIsInitialized(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->initialized;
}

/** are Benders' cuts generated from the LP solutions? */
SCIP_Bool SCIPbendersCutLP(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->cutlp;
}

/** are Benders' cuts generated from the pseudo solutions? */
SCIP_Bool SCIPbendersCutPseudo(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->cutpseudo;
}

/** are Benders' cuts generated from the relaxation solutions? */
SCIP_Bool SCIPbendersCutRelaxation(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->cutrelax;
}

/** should this Benders' use the auxiliary variables from the highest priority Benders' */
SCIP_Bool SCIPbendersShareAuxVars(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->shareauxvars;
}

/** Adds a subproblem to the Benders' decomposition data */
SCIP_RETCODE SCIPbendersAddSubproblem(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP*                 subproblem          /**< subproblem to be added to the data storage */
   )
{
   assert(benders != NULL);
   assert(subproblem != NULL);
   assert(benders->subproblems != NULL);
   assert(benders->naddedsubprobs + 1 <= benders->nsubproblems);

   benders->subproblems[benders->naddedsubprobs] = subproblem;

   benders->naddedsubprobs++;

   return SCIP_OKAY;
}

/** Removes the subproblems from the Benders' decomposition data */
void SCIPbendersRemoveSubproblems(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);
   assert(benders->subproblems != NULL);

   BMSclearMemoryArray(&benders->subproblems, benders->naddedsubprobs);
   benders->naddedsubprobs = 0;
}

/** returns the auxiliary variable for the given subproblem */
SCIP_VAR* SCIPbendersGetAuxiliaryVar(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->auxiliaryvars[probnumber];
}

/** returns all auxiliary variables */
SCIP_VAR** SCIPbendersGetAuxiliaryVars(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->auxiliaryvars;
}
/** stores the objective function value of the subproblem for use in cut generation */
void SCIPbendersSetSubprobObjval(
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition structure */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Real             objval              /**< the objective function value for the subproblem */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   /* updating the best objval */
   if( objval < benders->bestsubprobobjval[probnumber] )
      benders->bestsubprobobjval[probnumber] = objval;

   benders->subprobobjval[probnumber] = objval;
}

/** returns the objective function value of the subproblem for use in cut generation */
SCIP_Real SCIPbendersGetSubprobObjval(
   SCIP_BENDERS*         benders,            /**< variable benders */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->subprobobjval[probnumber];
}

/* sets the flag indicating whether a subproblem is convex. It is possible that this can change during the solving
 * process. One example is when the three-phase method is employed, where the first phase solves the of both the master
 * and subproblems and by the third phase the integer subproblem is solved. */
void SCIPbendersSetSubprobIsConvex(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool             isconvex            /**< flag to indicate whether the subproblem is convex */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   if( isconvex && !benders->subprobisconvex[probnumber] )
      benders->nconvexsubprobs++;
   else if( !isconvex && benders->subprobisconvex[probnumber] )
      benders->nconvexsubprobs--;

   benders->subprobisconvex[probnumber] = isconvex;

   assert(benders->nconvexsubprobs >= 0 && benders->nconvexsubprobs <= benders->nsubproblems);
}

/* returns whether the subproblem is convex. This means that the dual solution can be used to generate cuts. */
SCIP_Bool SCIPbendersSubprobIsConvex(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->subprobisconvex[probnumber];
}

/** returns the number of subproblems that are convex */
int SCIPbendersGetNConvexSubprobs(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->nconvexsubprobs;
}

/** changes all of the master problem variables in the given subproblem to continuous */
SCIP_RETCODE SCIPbendersChgMastervarsToCont(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   probnumber          /**< the subproblem number */
   )
{
   SCIP* subproblem;
   SCIP_VAR** vars;
   int nbinvars;
   int nintvars;
   int nimplvars;
   int chgvarscount;
   int origintvars;
   int i;
   SCIP_Bool infeasible;

   assert(benders != NULL);
   assert(set != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   subproblem = SCIPbendersSubproblem(benders, probnumber);
   assert(subproblem != NULL);

   /* only set the master problem variable to continuous if they have not already been changed. */
   if( !SCIPbendersGetMastervarsCont(benders, probnumber) )
   {
      /* retrieving the variable data */
      SCIP_CALL( SCIPgetVarsData(subproblem, &vars, NULL, &nbinvars, &nintvars, &nimplvars, NULL) );

      origintvars = nbinvars + nintvars + nimplvars;

      chgvarscount = 0;

      /* looping over all integer variables to change the master variables to continuous */
      i = 0;
      while( i < nbinvars + nintvars + nimplvars )
      {
         SCIP_VAR* mastervar;

         SCIP_CALL( SCIPbendersGetVar(benders, set, vars[i], &mastervar, -1) );

         if( SCIPvarGetType(vars[i]) != SCIP_VARTYPE_CONTINUOUS && mastervar != NULL )
         {
            SCIP_CALL( SCIPchgVarType(subproblem, vars[i], SCIP_VARTYPE_CONTINUOUS, &infeasible) );

            assert(!infeasible);

            chgvarscount++;
            SCIP_CALL( SCIPgetVarsData(subproblem, NULL, NULL, &nbinvars, &nintvars, &nimplvars, NULL) );
         }
         else
            i++;
      }

      /* if all of the integer variables have been changed to continuous, then the subproblem must now be an LP. In this
       * case, the subproblem is initialised and then put into probing mode */
      if( chgvarscount > 0 && chgvarscount == origintvars )
      {
         SCIP_CALL( initialiseLPSubproblem(benders, probnumber) );
         SCIPbendersSetSubprobIsConvex(benders, probnumber, TRUE);
      }

      SCIP_CALL( SCIPbendersSetMastervarsCont(benders, probnumber, TRUE) );
   }

   return SCIP_OKAY;
}

/** sets the subproblem setup flag */
void SCIPbendersSetSubprobIsSetup(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool             issetup             /**< flag to indicate whether the subproblem has been setup */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   benders->subprobsetup[probnumber] = issetup;
}

/** returns the subproblem setup flag */
SCIP_Bool SCIPbendersSubprobIsSetup(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->subprobsetup[probnumber];
}

/** sets the independent subproblem flag */
void SCIPbendersSetSubprobIsIndependent(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool             isindep             /**< flag to indicate whether the subproblem is independent */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   benders->indepsubprob[probnumber] = isindep;
}

/** returns whether the subproblem is independent */
SCIP_Bool SCIPbendersSubprobIsIndependent(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->indepsubprob[probnumber];
}

/** sets whether the subproblem is enabled or disabled. A subproblem is disabled if it has been merged into the master
 * problem
 */
extern
void SCIPbendersSetSubprobEnabled(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool             enabled             /**< flag to indicate whether the subproblem is enabled */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   benders->subprobenabled[probnumber] = enabled;
}

/** returns whether the subproblem is enabled */
extern
SCIP_Bool SCIPbendersSubprobIsEnabled(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->subprobenabled[probnumber];
}

/** sets a flag to indicate whether the master variables are all set to continuous */
SCIP_RETCODE SCIPbendersSetMastervarsCont(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool             arecont             /**< flag to indicate whether the master problem variables are continuous */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   /* if the master variables were all continuous and now are not, then the subproblem must exit probing mode and be
    * changed to non-LP subproblem */
   if( benders->mastervarscont[probnumber] && !arecont )
   {
      if( SCIPinProbing(SCIPbendersSubproblem(benders, probnumber)) )
         SCIP_CALL( SCIPendProbing(SCIPbendersSubproblem(benders, probnumber)) );

      SCIPbendersSetSubprobIsConvex(benders, probnumber, FALSE);
   }

   benders->mastervarscont[probnumber] = arecont;

   return SCIP_OKAY;
}

/** returns whether the master variables are all set to continuous */
SCIP_Bool SCIPbendersGetMastervarsCont(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   )
{
   assert(benders != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   return benders->mastervarscont[probnumber];
}

/** returns the number of cuts that have been transferred from sub SCIPs to the master SCIP */
int SCIPbendersGetNTransferredCuts(
   SCIP_BENDERS*         benders             /**< the Benders' decomposition data structure */
   )
{
   assert(benders != NULL);

   return benders->ntransferred;
}

/** sets the sorted flags in the Benders' decomposition */
void SCIPbendersSetBenderscutsSorted(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition structure */
   SCIP_Bool             sorted              /**< the value to set the sorted flag to */
   )
{
   assert(benders != NULL);

   benders->benderscutssorted = sorted;
   benders->benderscutsnamessorted = sorted;
}

/** inserts a Benders' cut into the Benders' cnumberuts list */
SCIP_RETCODE SCIPbendersIncludeBenderscut(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BENDERSCUT*      benderscut          /**< Benders' cut */
   )
{
   assert(benders != NULL);
   assert(benderscut != NULL);

   if( benders->nbenderscuts >= benders->benderscutssize )
   {
      benders->benderscutssize = SCIPsetCalcMemGrowSize(set, benders->nbenderscuts+1);
      SCIP_ALLOC( BMSreallocMemoryArray(&benders->benderscuts, benders->benderscutssize) );
   }
   assert(benders->nbenderscuts < benders->benderscutssize);

   benders->benderscuts[benders->nbenderscuts] = benderscut;
   benders->nbenderscuts++;
   benders->benderscutssorted = FALSE;

   return SCIP_OKAY;
}

/** returns the Benders' cut of the given name, or NULL if not existing */
SCIP_BENDERSCUT* SCIPfindBenderscut(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   const char*           name                /**< name of Benderscut' decomposition */
   )
{
   int i;

   assert(benders != NULL);
   assert(name != NULL);

   for( i = 0; i < benders->nbenderscuts; i++ )
   {
      if( strcmp(SCIPbenderscutGetName(benders->benderscuts[i]), name) == 0 )
         return benders->benderscuts[i];
   }

   return NULL;
}

/** returns the array of currently available Benders' cuts; active benders are in the first slots of the array */
SCIP_BENDERSCUT** SCIPbendersGetBenderscuts(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   if( !benders->benderscutssorted )
   {
      SCIPsortPtr((void**)benders->benderscuts, SCIPbenderscutComp, benders->nbenderscuts);
      benders->benderscutssorted = TRUE;
      benders->benderscutsnamessorted = FALSE;
   }

   return benders->benderscuts;
}

/** returns the number of currently available Benders' cuts */
int SCIPbendersGetNBenderscuts(
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   assert(benders != NULL);

   return benders->nbenderscuts;
}

/** sets the priority of a Benders' decomposition */
SCIP_RETCODE SCIPbendersSetBenderscutPriority(
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' cut */
   int                   priority            /**< new priority of the Benders' decomposition */
   )
{
   assert(benders != NULL);
   assert(benderscut != NULL);

   benderscut->priority = priority;
   benders->benderscutssorted = FALSE;

   return SCIP_OKAY;
}

/** sorts benders cuts by priorities */
void SCIPbendersSortBenderscuts(
   SCIP_BENDERS*         benders             /**< benders */
   )
{
   assert(benders != NULL);

   if( !benders->benderscutssorted )
   {
      SCIPsortPtr((void**)benders->benderscuts, SCIPbenderscutComp, benders->nbenderscuts);
      benders->benderscutssorted = TRUE;
      benders->benderscutsnamessorted = FALSE;
   }
}

/** sorts benders cuts by name */
void SCIPbendersSortBenderscutsName(
   SCIP_BENDERS*         benders             /**< benders */
   )
{
   assert(benders != NULL);

   if( !benders->benderscutsnamessorted )
   {
      SCIPsortPtr((void**)benders->benderscuts, SCIPbenderscutCompName, benders->nbenderscuts);
      benders->benderscutssorted = FALSE;
      benders->benderscutsnamessorted = TRUE;
   }
}
