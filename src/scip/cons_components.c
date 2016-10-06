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

/**@file   cons_components.c
 * @brief  constraint handler for handling independent components
 * @author Gerald Gamrath
 *
 * This constraint handler looks for independent components.
 */
#define DETAILED_OUTPUT
#define SCIP_DEBUG
/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/cons_components.h"

#define CONSHDLR_NAME          "components"
#define CONSHDLR_DESC          "independent components constraint handler"
#define CONSHDLR_ENFOPRIORITY         0 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY -9999999 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_EAGERFREQ           -1 /**< frequency for using all instead of only the useful constraints in separation,
                                              *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_NEEDSCONS        FALSE /**< should the constraint handler be skipped, if no constraints are available? */

#define CONSHDLR_PROPFREQ             1 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */
#define CONSHDLR_DELAYPROP         TRUE /**< should propagation method be delayed, if other propagators found reductions? */

#define CONSHDLR_PRESOLTIMING    SCIP_PRESOLTIMING_FINAL /**< presolving timing of the constraint handler (fast, medium, or exhaustive) */
#define CONSHDLR_PROP_TIMING     (SCIP_PROPTIMING_BEFORELP | SCIP_PROPTIMING_AFTERLPLOOP)

#define DEFAULT_MAXDEPTH             10      /**< maximum depth of a node to run components detection */
#define DEFAULT_MAXINTVARS          500      /**< maximum number of integer (or binary) variables to solve a subproblem directly in presolving (-1: no solving) */
#define DEFAULT_MINSIZE              50      /**< minimum absolute size (in terms of variables) to solve a component individually during branch-and-bound */
#define DEFAULT_MINRELSIZE          0.1      /**< minimum relative size (in terms of variables) to solve a component individually during branch-and-bound */
#define DEFAULT_NODELIMIT       10000LL      /**< maximum number of nodes to be solved in subproblems during presolving */
#define DEFAULT_INTFACTOR           1.0      /**< the weight of an integer variable compared to binary variables */
#define DEFAULT_RELDECREASE         0.2      /**< percentage by which the number of variables has to be decreased after the last component solving
                                              *   to allow running again during presolving (1.0: do not run again) */
#define DEFAULT_FEASTOLFACTOR       1.0      /**< default value for parameter to increase the feasibility tolerance in all sub-SCIPs */

/*
 * Data structures
 */

typedef struct Problem PROBLEM;

/** data related to one component */
typedef struct Component
{
   PROBLEM*              problem;            /** the problem this component belongs to */
   SCIP*                 subscip;            /** sub-SCIP representing the component */
   SCIP_SOL*             workingsol;         /** working solution for transferring solutions into the sub-SCIP */
   SCIP_VAR**            vars;               /** variables belonging to this component (in complete problem) */
   SCIP_VAR**            subvars;            /** variables belonging to this component (in subscip) */
   SCIP_VAR**            fixedvars;          /** variables in the sub-SCIP which were copied while copying the component's
                                              *  constraints, but do not count to the subvars, because they were locally fixed */
   SCIP_Real             fixedvarsobjsum;    /** objective contribution of all locally fixed variables */
   SCIP_Real             lastdualbound;      /** dual bound after last optimization call for this component */
   SCIP_Real             lastprimalbound;    /** primal bound after last optimization call for this component */
   SCIP_Longint          lastnodelimit;      /** node limit of last optimization call for this component */
   SCIP_STATUS           laststatus;         /** solution status of last optimization call for the sub-SCIP of this component */
   SCIP_Bool             solved;             /** was this component solved already? */
   int                   ncalls;             /** number of optimization calls for this component */
   int                   lastsolindex;       /** index of best solution after last optimization call for this component */
   int                   lastbestsolindex;
   int                   nvars;              /** number of variables belonging to this component */
   int                   nfixedvars;         /** number of fixed variables copied during constraint copying */
   int                   number;             /** component number */
} COMPONENT;

/** data related to one problem */
struct Problem
{
   SCIP*                 scip;               /** the SCIP instance this problem belongs to */
   COMPONENT**           components;         /** independent components into which the problem can be divided */
   SCIP_PQUEUE*          compqueue;          /** priority queue for components */
   SCIP_SOL*             bestsol;            /** best solution found so far for the problem */
   char*                 name;               /** name of the problem */
   SCIP_Real             fixedvarsobjsum;    /** objective contribution of all locally fixed variables */
   SCIP_Real             lowerbound;         /** lower bound of the problem */
   int                   ncomponents;        /** number of independent components into which the problem can be divided */
   int                   componentssize;     /** size of components array */
   int                   nfeascomps;         /** number of components for which a feasible solution was found */
   int                   nsolvedcomps;       /** number of components solved to optimality */
   int                   nlowerboundinf;     /** number of components with lower bound equal to -infinity */

};


/** control parameters */
struct SCIP_ConshdlrData
{
   SCIP_Longint          nodelimit;          /** maximum number of nodes to be solved in subproblems */
   SCIP_Real             intfactor;          /** the weight of an integer variable compared to binary variables */
   SCIP_Real             reldecrease;        /** percentage by which the number of variables has to be decreased after the last component solving
                                              *  to allow running again (1.0: do not run again) */
   SCIP_Real             feastolfactor;      /** parameter to increase the feasibility tolerance in all sub-SCIPs */
   SCIP_Bool             didsearch;          /** did the presolver already search for components? */
   SCIP_Bool             pluginscopied;      /** was the copying of the plugins successful? */
   SCIP_Bool             writeproblems;      /** should the single components be written as an .cip-file? */
   int                   maxintvars;         /** maximum number of integer (or binary) variables to solve a subproblem directly (-1: no solving) */
   int                   presollastnvars;    /** number of variables after last run of the presolver */
   int                   maxdepth;           /** maximum depth of a node to run components detection */
   int                   minsize;            /** minimum absolute size (in terms of variables) to solve a component individually during branch-and-bound */
   SCIP_Real             minrelsize;         /** minimum relative size (in terms of variables) to solve a component individually during branch-and-bound */
};


/** comparison method for sorting components */
static
SCIP_DECL_SORTPTRCOMP(componentSort)
{
   SCIP* scip;
   COMPONENT* comp1;
   COMPONENT* comp2;
   SCIP_Real gap1;
   SCIP_Real gap2;

   assert(elem1 != NULL);
   assert(elem2 != NULL);

   comp1 = (COMPONENT*)elem1;
   comp2 = (COMPONENT*)elem2;

   scip = comp1->problem->scip;

   if( comp1->ncalls == 0 )
      if( comp2->ncalls == 0 )
         return comp1->number - comp2->number;
      else
         return -1;
   else if( comp2->ncalls == 0 )
      return 1;

   gap1 = SQR(comp1->lastprimalbound - comp1->lastdualbound) / comp1->ncalls;
   gap2 = SQR(comp2->lastprimalbound - comp2->lastdualbound) / comp2->ncalls;

   if( SCIPisFeasGT(scip, gap1, gap2) )
      return -1;
   else if( SCIPisFeasLT(scip, gap1, gap2) )
      return +1;
   else
      return comp1->number - comp2->number;
}

static
int getMinsize(
   SCIP*                 scip,               /**< main SCIP data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata        /**< constraint handler data */
   )
{
   int minsize;

   minsize = conshdlrdata->minrelsize * SCIPgetNVars(scip);
   minsize = MAX(minsize, conshdlrdata->minsize);

   return minsize;
}


/** forward declaration: free subproblem structure */
static
SCIP_RETCODE freeProblem(PROBLEM**);


/** initialize component structure */
static
SCIP_RETCODE initComponent(
   PROBLEM*              problem             /**< subproblem structure */
   )
{
   COMPONENT* component;
   SCIP* scip;

   assert(problem != NULL);
   assert(problem->ncomponents < problem->componentssize);

   scip = problem->scip;
   assert(scip != NULL);

   SCIP_CALL( SCIPallocMemory(scip, &(problem->components[problem->ncomponents])) );
   component = problem->components[problem->ncomponents];
   assert(component != NULL);

   component->problem = problem;
   component->subscip = NULL;
   component->workingsol = NULL;
   component->vars = NULL;
   component->subvars = NULL;
   component->fixedvars = NULL;
   component->fixedvarsobjsum = 0.0;
   component->lastdualbound = -SCIPinfinity(scip);
   component->lastprimalbound = SCIPinfinity(scip);
   component->lastnodelimit = 0LL;
   component->laststatus = SCIP_STATUS_UNKNOWN;
   component->solved = FALSE;
   component->ncalls = 0;
   component->lastsolindex = -1;
   component->nvars = 0;
   component->nfixedvars = 0;
   component->number = problem->ncomponents;

   ++problem->ncomponents;

   return SCIP_OKAY;
}

/** free component structure */
static
SCIP_RETCODE freeComponent(
   COMPONENT**           component           /**< pointer to component structure */
   )
{
   PROBLEM* problem;
   SCIP* scip;

   assert(component != NULL);
   assert(*component != NULL);

   problem = (*component)->problem;
   assert(problem != NULL);

   scip = problem->scip;
   assert(scip != NULL);

   //SCIPdebugMessage("freeing component %d of problem <%s>\n", (*component)->number, (*component)->problem->name);
#if 0
   if( SCIPgetStage(scip) <= SCIP_STAGE_SOLVED )
   {
      SCIP_CALL( SCIPprintStatistics((*component)->subscip, NULL) );
   }
#endif
   assert(((*component)->vars != NULL) == ((*component)->subvars != NULL));
   if( (*component)->vars != NULL )
   {
      SCIPfreeMemoryArray(scip, &(*component)->vars);
      SCIPfreeMemoryArray(scip, &(*component)->subvars);
   }
   if( (*component)->fixedvars != NULL )
   {
      SCIPfreeMemoryArray(scip, &(*component)->fixedvars);
   }

   if( (*component)->subscip != NULL )
   {
      if( (*component)->workingsol != NULL )
      {
         SCIP_CALL( SCIPfreeSol((*component)->subscip, &(*component)->workingsol) );
      }

      SCIP_CALL( SCIPfree(&(*component)->subscip) );
   }

   SCIPfreeMemory(scip, component);

   return SCIP_OKAY;
}


/** create the working solution for a given component and compute the objective offset */
static
SCIP_RETCODE componentSetupWorkingSol(
   COMPONENT*            component           /**< pointer to component structure */
   )
{
   SCIP* subscip;
   int nvars;
   int v;

   assert(component != NULL);

   subscip = component->subscip;
   assert(subscip != NULL);

   nvars = component->nvars;

   SCIP_CALL( SCIPtransformProb(subscip) );

   SCIP_CALL( SCIPcreateOrigSol(subscip, &(component->workingsol), NULL) );

   /* the number of variables was increased by copying the constraints */
   if( SCIPgetNOrigVars(subscip) > nvars )
   {
      SCIP_VAR** vars = SCIPgetOrigVars(subscip);
      int nnewvars = SCIPgetNOrigVars(subscip);
      int index = 0;
      int ninactive = 0;

      SCIP_CALL( SCIPallocMemoryArray(scip, &component->fixedvars, nnewvars - nvars) );

      for( v = 0; v < nnewvars; ++v )
      {
         if( SCIPvarGetIndex(vars[v]) >= nvars )
         {
            /* the variable is either locally fixed or could be an inactive variable present in a constraint
             * for which an aggregation constraint linking it to the active variable was created in the subscip
             */
            assert(SCIPisZero(subscip, SCIPvarGetObj(vars[v])) ||
               SCIPisEQ(subscip, SCIPvarGetLbGlobal(vars[v]), SCIPvarGetUbGlobal(vars[v])));

            /* locally fixed variable */
            if( SCIPisEQ(subscip, SCIPvarGetLbGlobal(vars[v]), SCIPvarGetUbGlobal(vars[v])) )
            {
               component->fixedvarsobjsum += SCIPvarGetLbGlobal(vars[v]) * SCIPvarGetObj(vars[v]);

               component->fixedvars[index] = vars[v];
               ++index;

               SCIP_CALL( SCIPsetSolVal(subscip, component->workingsol, vars[v], SCIPvarGetLbGlobal(vars[v])) );
            }
            /* inactive variable */
            else
            {
               ++ninactive;

               assert(SCIPisZero(subscip, SCIPvarGetObj(vars[v])));
            }
         }
#ifndef NDEBUG
         else
            assert(SCIPisLT(subscip, SCIPvarGetLbGlobal(vars[v]), SCIPvarGetUbGlobal(vars[v])));
#endif
      }
      component->nfixedvars = index;
      SCIPdebugMessage("%d locally fixed variables have been copied, objective contribution: %g\n",
         component->nfixedvars, component->fixedvarsobjsum);
   }

return SCIP_OKAY;
}

/** create a sub-SCIP for the given variables and constraints */
static
SCIP_RETCODE createSubscip(
   SCIP*                 scip,               /**< main SCIP data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP**                subscip             /**< pointer to store created sub-SCIP */
   )
{
   SCIP_Bool success;

   SCIP_CALL( SCIPcreate(subscip) );

   /* copy plugins, we omit pricers (because we do not run if there are active pricers) and dialogs */
   SCIP_CALL( SCIPcopyPlugins(scip, *subscip, TRUE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE,
         TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, &success) );

   /* only if the plugins were successfully copied */
   if( success )
   {
      /* copy parameter settings */
      SCIP_CALL( SCIPcopyParamSettings(scip, *subscip) );

      assert(!SCIPisParamFixed(*subscip, "limits/solutions"));
      assert(!SCIPisParamFixed(*subscip, "limits/bestsol"));
      assert(!SCIPisParamFixed(*subscip, "misc/usevartable"));
      assert(!SCIPisParamFixed(*subscip, "misc/useconstable"));
      assert(!SCIPisParamFixed(*subscip, "numerics/feastol"));
      assert(!SCIPisParamFixed(*subscip, "misc/usesmalltables"));

      /* disable solution limits */
      SCIP_CALL( SCIPsetIntParam(*subscip, "limits/solutions", -1) );
      SCIP_CALL( SCIPsetIntParam(*subscip, "limits/bestsol", -1) );

      SCIP_CALL( SCIPsetIntParam(*subscip, "constraints/" CONSHDLR_NAME "/maxdepth",
            MAX(-1, conshdlrdata->maxdepth - SCIPgetDepth(scip))) );

      SCIP_CALL( SCIPsetIntParam(*subscip, "constraints/" CONSHDLR_NAME "/maxprerounds", 0) );
      SCIP_CALL( SCIPfixParam(*subscip, "constraints/" CONSHDLR_NAME "/maxprerounds") );

      /* reduce the effort spent for hash tables */
      SCIP_CALL( SCIPsetBoolParam(*subscip, "misc/usevartable", FALSE) );
      SCIP_CALL( SCIPsetBoolParam(*subscip, "misc/useconstable", FALSE) );

      /* disable output, unless in extended debug mode */
#ifndef SCIP_MORE_DEBUG
      SCIP_CALL( SCIPsetIntParam(*subscip, "display/verblevel", 0) );
#endif
   }
   else
   {
      SCIP_CALL( SCIPfree(subscip) );
      *subscip = NULL;
   }

   return SCIP_OKAY;
}

/** copies the given variables and constraints into the given sub-SCIP */
static
SCIP_RETCODE copyToSubscip(
   SCIP*                 scip,               /**< source SCIP */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP*                 subscip,            /**< target SCIP */
   const char*           name,               /**< name for copied problem */
   SCIP_VAR**            vars,               /**< array of variables to copy */
   SCIP_VAR**            subvars,            /**< array to fill with copied vars */
   SCIP_CONS**           conss,              /**< constraint to copy */
   SCIP_HASHMAP*         varmap,             /**< hashmap used for the copy process of variables */
   SCIP_HASHMAP*         consmap,            /**< hashmap used for the copy process of constraints */
   int                   nvars,              /**< number of variables to copy */
   int                   nconss,             /**< number of constraints to copy */
   SCIP_Bool*            success             /**< pointer to store whether copying was successful */
   )
{
   SCIP_CONS* newcons;
   int i;

   assert(scip != NULL);
   assert(subscip != NULL);
   assert(vars != NULL);
   assert(subvars != NULL);
   assert(conss != NULL);
   assert(varmap != NULL);
   assert(consmap != NULL);
   assert(success != NULL);

   *success = TRUE;

   /* create problem in sub-SCIP */
   SCIP_CALL( SCIPcreateProb(subscip, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL) );

   /* copy variables */
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPgetVarCopy(scip, subscip, vars[i], &subvars[i], varmap, consmap, FALSE, success) );

      /* abort if variable was not successfully copied */
      if( !(*success) )
         goto TERMINATE;
   }

   /* In extended debug mode, we want to be informed if the number of variables was reduced during copying.
    * This might happen, since the components propagator uses SCIPgetConsVars() and then SCIPgetActiveVars() to get the
    * active representation, while SCIPgetConsCopy() might use SCIPgetProbvarLinearSum() and this might cancel out some
    * of the active variables and cannot be avoided. However, we want to notice it and check whether the constraint
    * handler could do something more clever.
    */
#ifdef SCIP_MORE_DEBUG
   if( nvars > SCIPgetNVars(subscip) )
   {
      SCIPdebugMessage("copying subscip <%s> reduced number of variables: %d -> %d\n", name, nvars,
         SCIPgetNVars(subscip));
   }
#endif

   /* copy constraints */
   for( i = 0; i < nconss; ++i )
   {
      assert(!SCIPconsIsModifiable(conss[i]));

      /* copy the constraint */
      SCIP_CALL( SCIPgetConsCopy(scip, subscip, conss[i], &newcons, SCIPconsGetHdlr(conss[i]), varmap, consmap, NULL,
            SCIPconsIsInitial(conss[i]), SCIPconsIsSeparated(conss[i]), SCIPconsIsEnforced(conss[i]),
            SCIPconsIsChecked(conss[i]), SCIPconsIsPropagated(conss[i]), FALSE, FALSE,
            SCIPconsIsDynamic(conss[i]), SCIPconsIsRemovable(conss[i]), FALSE, FALSE, success) );

      /* abort if constraint was not successfully copied */
      if( !(*success) )
         goto TERMINATE;

      SCIP_CALL( SCIPaddCons(subscip, newcons) );
      SCIP_CALL( SCIPreleaseCons(subscip, &newcons) );
   }

#if 0 /* for more debugging */
   /* write the problem, if requested */
   {
      char outname[SCIP_MAXSTRLEN];

      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s.cip", name);
      SCIPdebugMessage("write problem to file %s\n", outname);
      SCIP_CALL( SCIPwriteOrigProblem(subscip, outname, NULL, FALSE) );

      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s.set", name);
      SCIPdebugMessage("write settings to file %s\n", outname);
      SCIP_CALL( SCIPwriteParams(subscip, outname, TRUE, TRUE) );
   }
#endif

 TERMINATE:
   if( !(*success) )
   {
      SCIP_CALL( SCIPfreeTransform(subscip) );
   }

   return SCIP_OKAY;
}

/** create the sub-SCIP for a given component */
static
SCIP_RETCODE componentCreateSubscip(
   COMPONENT*            component,          /**< pointer to component structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP_HASHMAP*         varmap,             /**< variable hashmap used to improve performance */
   SCIP_HASHMAP*         consmap,            /**< constraint hashmap used to improve performance */
   SCIP_CONS**           conss,              /**< constraints contained in this component */
   int                   nconss,             /**< number of constraints contained in this component */
   SCIP_Bool*            success             /**< pointer to store whether the copying process was successful */
   )
{
   char name[SCIP_MAXSTRLEN];
   PROBLEM* problem;
   SCIP* scip;
   int minsize;

   assert(component != NULL);
   assert(consmap != NULL);
   assert(conss != NULL);

   problem = component->problem;
   assert(problem != NULL);

   scip = problem->scip;
   assert(scip != NULL);

   assert(component->nvars > 0);

   (*success) = TRUE;

   SCIP_CALL( createSubscip(scip, conshdlrdata, &component->subscip) );

   /* get minimum size of components to solve individually and set the parameter in the sub-SCIP */
   minsize = getMinsize(scip, conshdlrdata);

   SCIP_CALL( SCIPsetIntParam(component->subscip, "constraints/" CONSHDLR_NAME "/minsize", minsize) );

   if( component->subscip != NULL )
   {
      /* get name of the original problem and add "comp_nr" */
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s_comp_%d", problem->name, component->number);

      SCIP_CALL( copyToSubscip(scip, conshdlrdata, component->subscip, name, component->vars, component->subvars,
            conss, varmap, consmap, component->nvars, nconss, success) );

      if( !(*success) )
      {
         SCIP_CALL( SCIPfree(&component->subscip) );
         component->subscip = NULL;
      }
   }
   else
      (*success) = FALSE;

   return SCIP_OKAY;
}

/** solve a given sub-SCIP up to the given limits */
static
SCIP_RETCODE solveSubscip(
   SCIP*                 scip,               /**< main SCIP */
   SCIP*                 subscip,            /**< sub-SCIP to solve */
   SCIP_Longint          nodelimit,          /**< node limit */
   SCIP_Real             gaplimit            /**< gap limit */
   )
{
   SCIP_Real timelimit;
   SCIP_Real softtimelimit;
   SCIP_Real memorylimit;

   assert(scip != NULL);
   assert(subscip != NULL);

   /* update time limit */
   SCIP_CALL( SCIPgetRealParam(scip, "limits/time", &timelimit) );
   if( !SCIPisInfinity(scip, timelimit) )
      timelimit -= SCIPgetSolvingTime(scip);
   timelimit += SCIPgetSolvingTime(subscip);

   /* update soft time limit */
   SCIP_CALL( SCIPgetRealParam(scip, "limits/softtime", &softtimelimit) );
   if( softtimelimit > -0.5 )
   {
      softtimelimit -= SCIPgetSolvingTime(scip);
      softtimelimit += SCIPgetSolvingTime(subscip);
      softtimelimit = MAX(softtimelimit, 0.0);
   }

   /* substract the memory already used by the main SCIP and the estimated memory usage of external software */
   SCIP_CALL( SCIPgetRealParam(scip, "limits/memory", &memorylimit) );
   if( !SCIPisInfinity(scip, memorylimit) )
   {
      memorylimit -= SCIPgetMemUsed(scip)/1048576.0;
      memorylimit -= SCIPgetMemExternEstim(scip)/1048576.0;
   }

   /* abort if no time is left or not enough memory to create a copy of SCIP, including external memory usage */
   // todo memory limit
   if( timelimit <= 0.0 )
   {
      SCIPdebugMessage("--> not solved (not enough memory or time left)\n");
      return SCIP_OKAY;
   }

   /* set time and memory limit for the subproblem */
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/time", timelimit) );
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/softtime", softtimelimit) );
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/memory", memorylimit) );

   /* set gap limit */
   SCIP_CALL( SCIPsetRealParam(subscip, "limits/gap", gaplimit) );

   /* set node limit */
   SCIP_CALL( SCIPsetLongintParam(subscip, "limits/nodes", nodelimit) );

   /* solve the subproblem */
   SCIP_CALL( SCIPsolve(subscip) );

#ifdef SCIP_MORE_DEBUG
   SCIP_CALL( SCIPprintStatistics(subscip, NULL) );
#endif

   return SCIP_OKAY;
}

/** solve a connected component during presolving and evaluate the result */
static
SCIP_RETCODE solveAndEvalSubscip(
   SCIP*                 scip,               /**< SCIP main data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< the components constraint handler data */
   SCIP*                 subscip,            /**< sub-SCIP to be solved */
   SCIP_VAR**            vars,               /**< array of variables copied to this component */
   SCIP_VAR**            subvars,            /**< array of sub-SCIP variables corresponding to the vars array */
   SCIP_CONS**           conss,              /**< array of constraints copied to this component */
   int                   nvars,              /**< number of variables copied to this component */
   int                   nconss,             /**< number of constraints copied to this component */
   int*                  ndeletedconss,      /**< pointer to store the number of deleted constraints */
   int*                  nfixedvars,         /**< pointer to store the number of fixed variables */
   int*                  ntightenedbounds,   /**< pointer to store the number of bound tightenings */
   SCIP_RESULT*          result,             /**< pointer to store the result of the component solving */
   SCIP_Bool*            solved              /**< pointer to store if the problem was solved to optimality */
   )
{
   int i;

   assert(scip != NULL);
   assert(conshdlrdata != NULL);
   assert(subscip != NULL);
   assert(vars != NULL);
   assert(conss != NULL);
   assert(ndeletedconss != NULL);
   assert(nfixedvars != NULL);
   assert(ntightenedbounds != NULL);
   assert(result != NULL);

   *solved  = FALSE;

   SCIP_CALL( solveSubscip(scip, subscip, conshdlrdata->nodelimit, 0.0) );

   if( SCIPgetStatus(subscip) == SCIP_STATUS_OPTIMAL )
   {
      SCIP_SOL* sol;
      SCIP_VAR* var;
      SCIP_VAR* subvar;
      SCIP_Real* fixvals;
      SCIP_Bool feasible;
      SCIP_Bool infeasible;
      SCIP_Bool fixed;

      sol = SCIPgetBestSol(subscip);

#ifdef SCIP_DEBUG
      SCIP_CALL( SCIPcheckSolOrig(subscip, sol, &feasible, TRUE, TRUE) );
#else
      SCIP_CALL( SCIPcheckSolOrig(subscip, sol, &feasible, FALSE, FALSE) );
#endif

      SCIPdebugMessage("--> solved to optimality: time=%.2f, solution is%s feasible\n", SCIPgetSolvingTime(subscip), feasible ? "" : " not");

      SCIP_CALL( SCIPallocBufferArray(scip, &fixvals, nvars) );

      if( feasible )
      {
         SCIP_Real glb;
         SCIP_Real gub;

         /* get values of variables in the optimal solution */
         for( i = 0; i < nvars; ++i )
         {
            var = vars[i];
            subvar = subvars[i];

            /* get global bounds */
            glb = SCIPvarGetLbGlobal(var);
            gub = SCIPvarGetUbGlobal(var);

            if( subvar != NULL )
            {
               /* get solution value from optimal solution of the sub-SCIP */
               fixvals[i] = SCIPgetSolVal(subscip, sol, subvar);

               assert(SCIPisFeasLE(scip, fixvals[i], SCIPvarGetUbLocal(var)));
               assert(SCIPisFeasGE(scip, fixvals[i], SCIPvarGetLbLocal(var)));

               /* checking a solution is done with a relative tolerance of feasibility epsilon, if we really want to
                * change the bounds of the variables by fixing them, the old bounds must not be violated by more than
                * the absolute epsilon; therefore, we change the fixing values, if needed, and mark that the solution
                * has to be checked again
                */
               if( SCIPisGT(scip, fixvals[i], gub) )
               {
                  SCIPdebugMessage("variable <%s> fixval: %f violates global upperbound: %f\n",
                     SCIPvarGetName(var), fixvals[i], gub);
                  fixvals[i] = gub;
                  feasible = FALSE;
               }
               else if( SCIPisLT(scip, fixvals[i], glb) )
               {
                  SCIPdebugMessage("variable <%s> fixval: %f violates global lowerbound: %f\n",
                     SCIPvarGetName(var), fixvals[i], glb);
                  fixvals[i] = glb;
                  feasible = FALSE;
               }
               assert(SCIPisLE(scip, fixvals[i], SCIPvarGetUbLocal(var)));
               assert(SCIPisGE(scip, fixvals[i], SCIPvarGetLbLocal(var)));
            }
            else
            {
               /* the variable was not copied, so it was cancelled out of constraints during copying;
                * thus, the variable is not constrained and we fix it to its best bound
                */
               if( SCIPisPositive(scip, SCIPvarGetObj(var)) )
                  fixvals[i] = glb;
               else if( SCIPisNegative(scip, SCIPvarGetObj(var)) )
                  fixvals[i] = gub;
               else
               {
                  fixvals[i] = 0.0;
                  fixvals[i] = MIN(fixvals[i], gub);
                  fixvals[i] = MAX(fixvals[i], glb);
               }
            }
         }

         /* the solution value of at least one variable is feasible with a relative tolerance of feasibility epsilon,
          * but infeasible with an absolute tolerance of epsilon; try to set the variables to the bounds and check
          * solution again (changing the values might now introduce infeasibilities of constraints)
          */
         if( !feasible )
         {
            SCIP_Real origobj;

            SCIPdebugMessage("solution violates bounds by more than epsilon, check the corrected solution...\n");

            origobj = SCIPgetSolOrigObj(subscip, SCIPgetBestSol(subscip));

            SCIP_CALL( SCIPfreeTransform(subscip) );

            SCIP_CALL( SCIPcreateOrigSol(subscip, &sol, NULL) );

            /* get values of variables in the optimal solution */
            for( i = 0; i < nvars; ++i )
            {
               subvar = subvars[i];

               SCIP_CALL( SCIPsetSolVal(subscip, sol, subvar, fixvals[i]) );
            }

            /* check the solution; integrality and bounds should be fulfilled and do not have to be checked */
            SCIP_CALL( SCIPcheckSol(subscip, sol, FALSE, FALSE, FALSE, FALSE, TRUE, &feasible) );

#ifndef NDEBUG
            /* in debug mode, we additionally check integrality and bounds */
            if( feasible )
            {
               SCIP_CALL( SCIPcheckSol(subscip, sol, FALSE, FALSE, TRUE, TRUE, FALSE, &feasible) );
               assert(feasible);
            }
#endif

            SCIPdebugMessage("--> corrected solution is%s feasible\n", feasible ? "" : " not");

            if( !SCIPisFeasEQ(subscip, SCIPsolGetOrigObj(sol), origobj) )
            {
               SCIPdebugMessage("--> corrected solution has a different objective value (old=%16.9g, corrected=%16.9g)\n",
                  origobj, SCIPsolGetOrigObj(sol));

               feasible = FALSE;
            }

            SCIP_CALL( SCIPfreeSol(subscip, &sol) );
         }

         /* if the solution is feasible, fix variables and delete constraints of the component */
         if( feasible )
         {
            /* fix variables */
            for( i = 0; i < nvars; ++i )
            {
               assert(SCIPisLE(scip, fixvals[i], SCIPvarGetUbLocal(vars[i])));
               assert(SCIPisGE(scip, fixvals[i], SCIPvarGetLbLocal(vars[i])));
               assert(SCIPisLE(scip, fixvals[i], SCIPvarGetUbGlobal(vars[i])));
               assert(SCIPisGE(scip, fixvals[i], SCIPvarGetLbGlobal(vars[i])));

               SCIP_CALL( SCIPfixVar(scip, vars[i], fixvals[i], &infeasible, &fixed) );
               assert(!infeasible);
               assert(fixed);
               (*nfixedvars)++;
            }

            /* delete constraints */
            for( i = 0; i < nconss; ++i )
            {
               SCIP_CALL( SCIPdelCons(scip, conss[i]) );
               (*ndeletedconss)++;
            }

            *result = SCIP_SUCCESS;
            *solved = TRUE;
         }
      }

      SCIPfreeBufferArray(scip, &fixvals);
   }
   else if( SCIPgetStatus(subscip) == SCIP_STATUS_INFEASIBLE )
   {
      *result = SCIP_CUTOFF;
   }
   else if( SCIPgetStatus(subscip) == SCIP_STATUS_UNBOUNDED || SCIPgetStatus(subscip) == SCIP_STATUS_INFORUNBD )
   {
      /* TODO: store unbounded ray in original SCIP data structure */
      *result = SCIP_UNBOUNDED;
   }
   else
   {
      SCIPdebugMessage("--> solving interrupted (status=%d, time=%.2f)\n",
         SCIPgetStatus(subscip), SCIPgetSolvingTime(subscip));

      /* transfer global fixings to the original problem; we can only do this, if we did not find a solution in the
       * subproblem, because otherwise, the primal bound might lead to dual reductions that cannot be transferred to
       * the original problem without also transferring the possibly suboptimal solution (which is currently not
       * possible)
       */
      if( SCIPgetNSols(subscip) == 0 )
      {
         SCIP_Bool infeasible;
         SCIP_Bool tightened;
         int ntightened;

         ntightened = 0;

         for( i = 0; i < nvars; ++i )
         {
            assert(subvars[i] != NULL);

            SCIP_CALL( SCIPtightenVarLb(scip, vars[i], SCIPvarGetLbGlobal(subvars[i]), FALSE,
                  &infeasible, &tightened) );
            assert(!infeasible);
            if( tightened )
               ntightened++;

            SCIP_CALL( SCIPtightenVarUb(scip, vars[i], SCIPvarGetUbGlobal(subvars[i]), FALSE,
                  &infeasible, &tightened) );
            assert(!infeasible);
            if( tightened )
               ntightened++;
         }

         *result = SCIP_SUCCESS;

         *ntightenedbounds += ntightened;

         SCIPdebugMessage("--> tightened %d bounds of variables due to global bounds in the sub-SCIP\n", ntightened);
      }
   }

   return SCIP_OKAY;
}

/** (continues) solving a connected component */
static
SCIP_RETCODE solveComponent(
   COMPONENT*            component,
   SCIP_Bool             lastcomponent,
   SCIP_RESULT*          result              /**< pointer to store the result of the solving process */
   )
{
   PROBLEM* problem;
   SCIP* scip;
   SCIP* subscip;
   SCIP_SOL* bestsol;
   SCIP_Longint nodelimit;
   SCIP_Real gaplimit;
   SCIP_STATUS status;

   assert(component != NULL);

   problem = component->problem;
   assert(problem != NULL);

   scip = problem->scip;
   assert(scip != NULL);

   subscip = component->subscip;
   assert(subscip != NULL);

   *result = SCIP_DIDNOTRUN;

   SCIPdebugMessage("solve component <%s> (ncalls=%d, absgap=%.9g)\n",
      SCIPgetProbName(subscip), component->ncalls, component->lastprimalbound - component->lastdualbound);

   bestsol = SCIPgetBestSol(scip);

   /* update best solution of component */
   if( bestsol != NULL && SCIPsolGetIndex(bestsol) != component->lastbestsolindex )
   {
      SCIP_SOL* compsol = component->workingsol;
      SCIP_VAR** vars = component->vars;
      SCIP_VAR** subvars = component->subvars;
      int nvars = component->nvars;
      int v;

      component->lastbestsolindex = SCIPsolGetIndex(bestsol);

      for( v = 0; v < nvars; ++v )
      {
         SCIP_CALL( SCIPsetSolVal(subscip, compsol, subvars[v], SCIPgetSolVal(scip, bestsol, vars[v])) );
      }
#ifndef NDEBUG
      for( v = 0; v < component->nfixedvars; ++v )
      {
         assert(SCIPisEQ(scip, SCIPgetSolVal(subscip, compsol, component->fixedvars[v]),
               SCIPvarGetLbGlobal(component->fixedvars[v])));
      }
#endif

      if( SCIPgetStage(subscip) == SCIP_STAGE_PROBLEM || SCIPisLT(subscip, SCIPgetSolOrigObj(subscip, compsol), SCIPgetPrimalbound(subscip)) )
      {
         SCIP_Bool feasible;

         SCIPdebugMessage("install new solution in component <%s> inherited from problem <%s>: primal bound %.9g --> %.9g\n",
            SCIPgetProbName(subscip), problem->name,
            SCIPgetStage(subscip) == SCIP_STAGE_PROBLEM ? SCIPinfinity(subscip) : SCIPgetPrimalbound(subscip), SCIPgetSolOrigObj(subscip, compsol));

         SCIP_CALL( SCIPcheckSolOrig(subscip, compsol, &feasible, FALSE, FALSE) );
         if( feasible )
         {
            SCIPdebugMessage("... feasible\n");

            SCIP_CALL( SCIPaddSol(subscip, compsol, &feasible) );
         }
         else
         {
            SCIPdebugMessage("... infeasible, update cutoff bound\n");

            assert(!SCIPisSumGT(subscip, SCIPgetSolOrigObj(subscip, compsol), SCIPgetCutoffbound(subscip)));

            if( SCIPgetSolOrigObj(subscip, compsol) < SCIPgetCutoffbound(subscip) )
            {
               SCIP_CALL( SCIPupdateCutoffbound(subscip, SCIPgetSolOrigObj(subscip, compsol)) );
            }
         }
      }
   }

   {
      assert(component->laststatus != SCIP_STATUS_OPTIMAL);

      printf("solve sub-SCIP for component <%s> (ncalls=%d, absgap=%16.9g)\n",
         SCIPgetProbName(component->subscip), component->ncalls, component->lastprimalbound - component->lastdualbound);

      if( component->ncalls == 0 )
      {
         nodelimit = 1LL;
         gaplimit = 0.0;
      }
      else
      {
         //nodelimit = SCIPgetNNodes(component->subscip) + 1;
         nodelimit = 2 * SCIPgetNNodes(component->subscip);
         nodelimit = MAX(nodelimit, 10LL);

         /* set a gap limit of half the current gap (at most 10%) */
         if( SCIPgetGap(component->subscip) < 0.2 )
            gaplimit = 0.5 * SCIPgetGap(component->subscip);
         else
            gaplimit = 0.1;

         if( lastcomponent )
         {
            int verblevel;

            SCIP_CALL( SCIPgetIntParam(scip, "display/verblevel", &verblevel) );

            if( verblevel >= 4 )
            {
               /* enable output */
               //SCIP_CALL( SCIPsetIntParam(subscip, "display/verblevel", 4) );
            }

            gaplimit = 0.0;
         }
      }

      SCIP_CALL( solveSubscip(scip, subscip, nodelimit, gaplimit) );

      SCIP_CALL( SCIPmergeStatistics(subscip, scip) );

      SCIP_CALL( SCIPprintDisplayLine(scip, NULL, SCIP_VERBLEVEL_NORMAL, TRUE) );

      status = SCIPgetStatus(subscip);

      component->lastnodelimit = nodelimit;
      component->laststatus = status;
      ++component->ncalls;

      printf("--> (status=%d, nodes=%lld, time=%.2f): gap: %12.5g%% absgap: %16.9g\n",
         status, SCIPgetNNodes(subscip), SCIPgetSolvingTime(subscip), 100.0*SCIPgetGap(subscip),
         SCIPgetPrimalbound(subscip) - SCIPgetDualbound(subscip));

      *result = SCIP_SUCCESS;

      switch( status )
      {
      case SCIP_STATUS_OPTIMAL:
         component->solved = TRUE;
         break;
      case SCIP_STATUS_INFEASIBLE:
         *result = SCIP_CUTOFF;
         component->solved = TRUE;
         break;
      case SCIP_STATUS_UNBOUNDED:
      case SCIP_STATUS_INFORUNBD:
         /* TODO: store unbounded ray in original SCIP data structure */
         *result = SCIP_UNBOUNDED;
         component->solved = TRUE;
         break;
      case SCIP_STATUS_USERINTERRUPT:
         SCIP_CALL( SCIPinterruptSolve(scip) );
      default:
         break;
      }
   }

   /* evaluate call */
   if( *result == SCIP_SUCCESS )
   {
      SCIP_SOL* sol = SCIPgetBestSol(subscip);
      SCIP_VAR* var;
      SCIP_VAR* subvar;
      SCIP_Real newdualbound;
      int v;

      /* get dual bound as the minimum of SCIP dual bound and sub-problems dual bound */
      newdualbound = SCIPgetDualbound(subscip) - component->fixedvarsobjsum;

      /* update dual bound of problem */
      if( !SCIPisEQ(scip, component->lastdualbound, newdualbound) )
      {
         assert(!SCIPisInfinity(scip, -newdualbound));

         /* first finite dual bound: decrease inf counter and add dual bound to problem dualbound */
         if( SCIPisInfinity(scip, -component->lastdualbound) )
         {
            --problem->nlowerboundinf;
            problem->lowerbound += newdualbound;
         }
         /* increase problem dual bound by dual bound delta */
         else
         {
            problem->lowerbound += (newdualbound - component->lastdualbound);
         }

         /* update problem dual bound if all problem components have a finite dual bound */
         if( problem->nlowerboundinf == 0 )
         {
            SCIPdebugMessage("component <%s>: dual bound increased from %16.9g to %16.9g, new dual bound of problem <%s>: %16.9g (gap: %16.9g, absgap: %16.9g)\n",
               SCIPgetProbName(subscip), component->lastdualbound, newdualbound, problem->name, SCIPretransformObj(scip, problem->lowerbound),
               problem->nfeascomps == problem->ncomponents ?
               (SCIPgetSolOrigObj(scip, problem->bestsol) - SCIPretransformObj(scip, problem->lowerbound)) / MAX(ABS(SCIPretransformObj(scip, problem->lowerbound)),SCIPgetSolOrigObj(scip, problem->bestsol)) : SCIPinfinity(scip),
               problem->nfeascomps == problem->ncomponents ?
               SCIPgetSolOrigObj(scip, problem->bestsol) - SCIPretransformObj(scip, problem->lowerbound) : SCIPinfinity(scip));
            SCIP_CALL( SCIPupdateLocalLowerbound(scip, problem->lowerbound) );
         }

         /* store dual bound of this call */
         component->lastdualbound = newdualbound;
      }

      /* update primal solution of problem */
      if( sol != NULL && component->lastsolindex != SCIPsolGetIndex(sol) )
      {
         component->lastsolindex = SCIPsolGetIndex(sol);

         /* increase counter for feasible problems if no solution was known before */
         if( SCIPisInfinity(scip, component->lastprimalbound) )
            ++(problem->nfeascomps);

         /* update working best solution in problem */
         for( v = 0; v < component->nvars; ++v )
         {
            var = component->vars[v];
            subvar = component->subvars[v];
            assert(var != NULL);
            assert(subvar != NULL);
            assert(SCIPvarIsActive(var));

            SCIP_CALL( SCIPsetSolVal(scip, problem->bestsol, var, SCIPgetSolVal(subscip, sol, subvar)) );
         }

         /* if we have a feasible solution for each component, add the working solution to the main problem */
         if( problem->nfeascomps == problem->ncomponents )
         {
            SCIP_Bool feasible;

            SCIP_CALL( SCIPcheckSol(scip, problem->bestsol, TRUE, FALSE, TRUE, TRUE, TRUE, &feasible) );
            assert(feasible);

            SCIP_CALL( SCIPaddSol(scip, problem->bestsol, &feasible) );

            SCIPdebugMessage("component <%s>: primal bound decreased from %16.9g to %16.9g, new primal bound of problem <%s>: %16.9g (gap: %16.9g, absgap: %16.9g)\n",
               SCIPgetProbName(subscip), component->lastprimalbound, SCIPgetPrimalbound(subscip), problem->name, SCIPgetSolOrigObj(scip, problem->bestsol),
               problem->nfeascomps == problem->ncomponents ?
               (SCIPgetSolOrigObj(scip, problem->bestsol) - SCIPretransformObj(scip, problem->lowerbound)) / MAX(ABS(SCIPretransformObj(scip, problem->lowerbound)),SCIPgetSolOrigObj(scip, problem->bestsol)) : SCIPinfinity(scip),
               problem->nfeascomps == problem->ncomponents ?
               SCIPgetSolOrigObj(scip, problem->bestsol) - SCIPretransformObj(scip, problem->lowerbound) : SCIPinfinity(scip));
         }

         /* store primal bound of this call */
         component->lastprimalbound = SCIPgetPrimalbound(subscip) - component->fixedvarsobjsum;
      }

      /* if the component was solved to optimality, we increase the respective counter and free the subscip */
      if( component->laststatus == SCIP_STATUS_OPTIMAL )
      {
         ++(problem->nsolvedcomps);
         component->solved = TRUE;

         /* free working solution and component */
         SCIP_CALL( SCIPfreeSol(subscip, &component->workingsol) );

         SCIP_CALL( SCIPfree(&subscip) );
         component->subscip = NULL;
      }
   }

   return SCIP_OKAY;
}

/** initialize subproblem structure */
static
SCIP_RETCODE initProblem(
   SCIP*                 scip,               /**< SCIP data structure */
   PROBLEM**             problem,            /**< pointer to subproblem structure */
   SCIP_Real             fixedvarsobjsum,    /**< objective contribution of all locally fixed variables */
   int                   ncomponents         /**< number of independent components */
   )
{
   char name[SCIP_MAXSTRLEN];
   SCIP_VAR** vars;
   int nvars;
   int v;

   assert(scip != NULL);
   assert(problem != NULL);

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   SCIP_CALL( SCIPallocMemory(scip, problem) );
   assert(*problem != NULL);

   SCIP_CALL( SCIPallocMemoryArray(scip, &(*problem)->components, ncomponents) );
   SCIP_CALL( SCIPpqueueCreate(&(*problem)->compqueue, (int)(1.1*ncomponents), 1.2, componentSort) );

   (*problem)->scip = scip;
   (*problem)->lowerbound = fixedvarsobjsum;
   (*problem)->fixedvarsobjsum = fixedvarsobjsum;
   (*problem)->ncomponents = 0;
   (*problem)->componentssize = ncomponents;
   (*problem)->nlowerboundinf = ncomponents;
   (*problem)->nfeascomps = 0;
   (*problem)->nsolvedcomps = 0;

   if( SCIPgetDepth(scip) == 0 )
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s", SCIPgetProbName(scip));
   else
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s_node_%d", SCIPgetProbName(scip), SCIPnodeGetNumber(SCIPgetCurrentNode(scip)));

   SCIP_CALL( SCIPduplicateMemoryArray(scip, &(*problem)->name, name, strlen(name)+1) );

   SCIP_CALL( SCIPcreateSol(scip, &(*problem)->bestsol, NULL) );

   for( v = 0; v < nvars; v++ )
   {
      if( SCIPisFeasEQ(scip, SCIPvarGetLbLocal(vars[v]), SCIPvarGetUbLocal(vars[v])) )
      {
         SCIP_CALL( SCIPsetSolVal(scip, (*problem)->bestsol, vars[v],
               (SCIPvarGetUbLocal(vars[v]) + SCIPvarGetLbLocal(vars[v]))/2) );
      }
   }

   SCIPdebugMessage("initialized problem <%s>\n", (*problem)->name);

   return SCIP_OKAY;
}

/** free subproblem structure */
static
SCIP_RETCODE freeProblem(
   PROBLEM**             problem             /**< pointer to problem to free */
   )
{
   SCIP* scip;
   int c;

   assert(problem != NULL);
   assert(*problem != NULL);

   scip = (*problem)->scip;
   assert(scip != NULL);

   //SCIPdebugMessage("freeing problem <%s>\n", (*problem)->name);

   if( (*problem)->bestsol != NULL )
   {
      SCIP_CALL( SCIPfreeSol(scip, &(*problem)->bestsol) );
   }

   for( c = (*problem)->ncomponents - 1; c >= 0; --c )
   {
      SCIP_CALL( freeComponent(&(*problem)->components[c]) );
   }

   if( (*problem)->components != NULL )
   {
      SCIPfreeMemoryArray(scip, &(*problem)->components);
   }

   SCIPpqueueFree(&(*problem)->compqueue);

   SCIPfreeMemoryArray(scip, &(*problem)->name);

   SCIPfreeMemory(scip, problem);

   return SCIP_OKAY;
}

/** creates and captures a components constraint */
static
SCIP_RETCODE createConsComponents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   PROBLEM*              problem             /**< problem to be stored in the constraint */
   )
{
   SCIP_CONSHDLR* conshdlr;

   /* find the samediff constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("components constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, (SCIP_CONSDATA*)problem,
         FALSE, FALSE, FALSE, FALSE, TRUE,
         TRUE, FALSE, FALSE, FALSE, TRUE) );

   return SCIP_OKAY;
}


/** sort the components by size and sort vars and conss arrays by component numbers */
static
SCIP_RETCODE sortComponents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP_DIGRAPH*         digraph,
   SCIP_CONS**           conss,              /**< constraints */
   SCIP_VAR**            vars,               /**< variables */
   int*                  varcomponent,       /**< component numbers for the variables */
   int*                  conscomponent,      /**< array to store component numbers for the constraints */
   int                   nconss,             /**< number of constraints */
   int                   nvars,              /**< number of variables */
   int*                  firstvaridxpercons, /**< array with index of first variable in vars array for each constraint */
   int*                  ncompsminsize,      /**< pointer to store the number of components not exceeding the minimum size */
   int*                  ncompsmaxsize       /**< pointer to store the number of components not exceeding the maximum size */
   )
{
   SCIP_Real* compsize;
   int* permu;
   int ncomponents;
   int nbinvars;
   int nintvars;
   int ndiscvars;
   int ncontvars;
   int minsize;
   int v;
   int c;

   assert(scip != NULL);
   assert(conshdlrdata != NULL);
   assert(digraph != NULL);
   assert(conss != NULL);
   assert(vars != NULL);
   assert(firstvaridxpercons != NULL);

   /* compute minimum size of components to solve individually */
   minsize = getMinsize(scip, conshdlrdata);

   ncomponents = SCIPdigraphGetNComponents(digraph);
   *ncompsminsize = 0;
   *ncompsmaxsize = 0;

   /* We want to sort the components in increasing complexity (number of discrete variables,
    * integer weighted with factor intfactor, continuous used as tie-breaker).
    * Therefore, we now get the variables for each component, count the different variable types
    * and compute a size as described above. Then, we rename the components
    * such that for i < j, component i has no higher complexity than component j.
    */
   SCIP_CALL( SCIPallocBufferArray(scip, &compsize, ncomponents) );
   SCIP_CALL( SCIPallocBufferArray(scip, &permu, ncomponents) );

   /* get number of variables in the components */
   for( c = 0; c < ncomponents; ++c )
   {
      int* cvars;
      int ncvars;

      SCIPdigraphGetComponent(digraph, c, &cvars, &ncvars);
      permu[c] = c;
      nbinvars = 0;
      nintvars = 0;

      for( v = 0; v < ncvars; ++v )
      {
         /* check whether variable is of binary or integer type */
         if( SCIPvarGetType(vars[cvars[v]]) == SCIP_VARTYPE_BINARY )
            nbinvars++;
         else if( SCIPvarGetType(vars[cvars[v]]) == SCIP_VARTYPE_INTEGER )
            nintvars++;
      }
      ncontvars = ncvars - nintvars - nbinvars;
      ndiscvars = nbinvars + conshdlrdata->intfactor * nintvars;
      compsize[c] = ((1000 * ndiscvars + (950.0 * ncontvars)/nvars));

      /* component fulfills the maxsize requirement */
      if( ndiscvars <= conshdlrdata->maxintvars )
         ++(*ncompsmaxsize);

      /* component fulfills the minsize requirement */
      if( ncvars >= minsize )
         ++(*ncompsminsize);
   }

   /* get permutation of component numbers such that the size of the components is increasing */
   SCIPsortRealInt(compsize, permu, ncomponents);

   /* now, we need the reverse direction, i.e., for each component number, we store its new number
    * such that the components are sorted; for this, we abuse the conscomponent array
    */
   for( c = 0; c < ncomponents; ++c )
      conscomponent[permu[c]] = c;

   /* for each variable, replace the old component number by the new one */
   for( c = 0; c < nvars; ++c )
      varcomponent[c] = conscomponent[varcomponent[c]];

   SCIPfreeBufferArray(scip, &permu);
   SCIPfreeBufferArray(scip, &compsize);

   /* do the mapping from calculated components per variable to corresponding
    * constraints and sort the component-arrays for faster finding the
    * actual variables and constraints belonging to one component
    */
   for( c = 0; c < nconss; c++ )
      conscomponent[c] = (firstvaridxpercons[c] == -1 ? -1 : varcomponent[firstvaridxpercons[c]]);

   SCIPsortIntPtr(varcomponent, (void**)vars, nvars);
   SCIPsortIntPtr(conscomponent, (void**)conss, nconss);

   return SCIP_OKAY;
}



/** create PROBLEM structure for the current node and split it into components */
static
SCIP_RETCODE createAndSplitProblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP_Real             fixedvarsobjsum,    /**< objective contribution of all locally fixed variables */
   SCIP_VAR**            sortedvars,         /**< array of unfixed variables sorted by components */
   SCIP_CONS**           sortedconss,        /**< array of (checked) constraints sorted by components */
   int*                  compstartsvars,     /**< start points of components in sortedvars array */
   int*                  compstartsconss,    /**< start points of components in sortedconss array */
   int                   ncomponents,        /**< number of components */
   PROBLEM**             problem             /**< pointer to store problem structure */
   )
{
   COMPONENT* component;
   SCIP_HASHMAP* consmap;
   SCIP_HASHMAP* varmap;
   SCIP_VAR** compvars;
   SCIP_CONS** compconss;
   SCIP_Bool success;
   int ncompconss;
   int comp;

   /* init subproblem data structure */
   SCIP_CALL( initProblem(scip, problem, fixedvarsobjsum, ncomponents) );

   /* hashmap mapping from original constraints to constraints in the sub-SCIPs (for performance reasons) */
   SCIP_CALL( SCIPhashmapCreate(&consmap, SCIPblkmem(scip), 10 * compstartsconss[ncomponents]) );

   /* loop over all components */
   for( comp = 0; comp < ncomponents && !SCIPisStopped(scip); comp++ )
   {
      SCIP_CALL( initComponent(*problem) );

      assert((*problem)->components[comp] != NULL);
      component = (*problem)->components[comp];

      /* get component variables and store them in component structure */
      compvars = &(sortedvars[compstartsvars[comp]]);
      component->nvars = compstartsvars[comp + 1 ] - compstartsvars[comp];
      SCIP_CALL( SCIPduplicateMemoryArray(scip, &component->vars, compvars, component->nvars) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &component->subvars, component->nvars) );
      SCIP_CALL( SCIPhashmapCreate(&varmap, SCIPblkmem(scip), 10 * component->nvars) );

      /* get component constraints */
      compconss = &(sortedconss[compstartsconss[comp]]);
      ncompconss = compstartsconss[comp + 1] - compstartsconss[comp];

      if( component->nvars > 1 && ncompconss > 1 )

#ifdef DETAILED_OUTPUT
      {
         int nbinvars = 0;
         int nintvars = 0;
         int ncontvars = 0;
         int i;

         for( i = 0; i < component->nvars; ++i )
         {
            if( SCIPvarGetType(compvars[i]) == SCIP_VARTYPE_BINARY )
               ++nbinvars;
            else if( SCIPvarGetType(compvars[i]) == SCIP_VARTYPE_INTEGER )
               ++nintvars;
            else
               ++ncontvars;
         }
         SCIPinfoMessage(scip, NULL, "component %d at node %lld, depth %d: %d vars (%d bin, %d int, %d cont), %d conss\n",
            comp, SCIPnodeGetNumber(SCIPgetCurrentNode(scip)), SCIPgetDepth(scip), component->nvars, nbinvars, nintvars, ncontvars, ncompconss);
      }
#endif
      assert(ncompconss > 0 || component->nvars == 1);

      SCIPdebugMessage("build sub-SCIP for component %d of problem <%s>: %d vars, %d conss\n",
         component->number, (*problem)->name, component->nvars, ncompconss);

#ifndef NDEBUG
      {
         int i;
         for( i = 0; i < component->nvars; ++i )
            assert(SCIPvarIsActive(component->vars[i]));
      }
#endif
#if 0
      {
         int i;
         for( i = 0; i < component->nvars; ++i )
            printf("var %d: <%s>\n", i, SCIPvarGetName(component->vars[i]));
         for( i = 0; i < ncompconss; ++i )
            printf("cons %d: <%s>\n", i, SCIPconsGetName(compconss[i]));
      }
#endif
      /* build subscip for component */
      SCIP_CALL( componentCreateSubscip(component, conshdlrdata, varmap, consmap, compconss, ncompconss, &success) );

      SCIP_CALL( componentSetupWorkingSol(component) );

      /* add component to the priority queue of the problem structure */
      SCIP_CALL( SCIPpqueueInsert((*problem)->compqueue, component) );

      SCIPhashmapFree(&varmap);

      if( !success )
         break;
   }

   SCIPhashmapFree(&consmap);

   return SCIP_OKAY;
}

/** continue solving a problem  */
static
SCIP_RETCODE solveProblem(
   PROBLEM*              problem,
   SCIP_RESULT*          result              /**< ??? */
   )
{
   COMPONENT* component;
   SCIP_RESULT subscipresult;

   assert(problem != NULL);

   *result = SCIP_SUCCESS;

   //SCIPinfoMessage(problem->scip, NULL, "solving problem <%s>: %d components left\n", problem->name, SCIPpqueueNElems(problem->compqueue));

   component = (COMPONENT*)SCIPpqueueRemove(problem->compqueue);

   /* continue solving the component */
   SCIP_CALL( solveComponent(component, SCIPpqueueNElems(problem->compqueue) == 0, &subscipresult) );

   if( subscipresult == SCIP_CUTOFF || subscipresult == SCIP_UNBOUNDED )
   {
      *result = subscipresult;
   }
   else if( !component->solved )
   {
      SCIP_CALL( SCIPpqueueInsert(problem->compqueue, component) );
      *result = SCIP_DELAYNODE;
   }
   else if( SCIPpqueueNElems(problem->compqueue) == 0 )
      *result = SCIP_CUTOFF;
   else
      *result = SCIP_DELAYNODE;

   return SCIP_OKAY;
}

/*
 * Local methods
 */

/** loop over constraints, get active variables and fill directed graph */
static
SCIP_RETCODE fillDigraph(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIGRAPH*         digraph,            /**< directed graph */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   int*                  unfixedvarpos,      /**< mapping from variable problem index to unfixed var index */
   int                   nunfixedvars,       /**< number of unfixed variables */
   int*                  firstvaridxpercons, /**< array to store for each constraint the index in the local vars array
                                              *   of the first variable of the constraint */
   SCIP_Bool*            success             /**< flag indicating successful directed graph filling */
   )
{
   SCIP_VAR** consvars;
   int requiredsize;
   int nconsvars;
   int nvars;
   int idx1;
   int idx2;
   int c;
   int v;

   assert(scip != NULL);
   assert(digraph != NULL);
   assert(conss != NULL);
   assert(firstvaridxpercons != NULL);
   assert(success != NULL);

   *success = TRUE;

   nconsvars = 0;
   requiredsize = 0;
   nvars = SCIPgetNVars(scip);

   /* use big buffer for storing active variables per constraint */
   SCIP_CALL( SCIPallocBufferArray(scip, &consvars, nvars) );

   for( c = 0; c < nconss; ++c )
   {
      /* check for reached timelimit */
      if( (c % 1000 == 0) && SCIPisStopped(scip) )
      {
         *success = FALSE;
         break;
      }

      /* get number of variables for this constraint */
      SCIP_CALL( SCIPgetConsNVars(scip, conss[c], &nconsvars, success) );

      if( !(*success) )
         break;

      if( nconsvars > nvars )
      {
         nvars = nconsvars;
         SCIP_CALL( SCIPreallocBufferArray(scip, &consvars, nvars) );
      }

#ifndef NDEBUG
      /* clearing variables array to check for consistency */
      if( nconsvars == nvars )
      {
	 BMSclearMemoryArray(consvars, nconsvars);
      }
      else
      {
	 assert(nconsvars < nvars);
	 BMSclearMemoryArray(consvars, nconsvars + 1);
      }
#endif

      /* get variables for this constraint */
      SCIP_CALL( SCIPgetConsVars(scip, conss[c], consvars, nvars, success) );

      if( !(*success) )
      {
#ifndef NDEBUG
	 /* it looks strange if returning the number of variables was successful but not returning the variables */
	 SCIPwarningMessage(scip, "constraint <%s> returned number of variables but returning variables failed\n", SCIPconsGetName(conss[c]));
#endif
         break;
      }

#ifndef NDEBUG
      /* check if returned variables are consistent with the number of variables that were returned */
      for( v = nconsvars - 1; v >= 0; --v )
	 assert(consvars[v] != NULL);
      if( nconsvars < nvars )
	 assert(consvars[nconsvars] == NULL);
#endif

      /* transform given variables to active variables */
      SCIP_CALL( SCIPgetActiveVars(scip, consvars, &nconsvars, nvars, &requiredsize) );
      assert(requiredsize <= nvars);

      firstvaridxpercons[c] = -1;

      if( nconsvars > 0 )
      {
         v = 0;
         idx1 = -1;

         while( idx1 == -1 && v < nconsvars )
         {
            idx1 = SCIPvarGetProbindex(consvars[v]);
            assert(idx1 >= 0);
            idx1 = unfixedvarpos[idx1];
            assert(idx1 < nunfixedvars);
            ++v;
         }

         if( idx1 >= 0 )
         {

            /* save index of the first variable for later component assignment */
            firstvaridxpercons[c] = idx1;

            /* create sparse directed graph
             * sparse means, to add only those edges necessary for component calculation
             */
            for(; v < nconsvars; ++v )
            {
               idx2 = SCIPvarGetProbindex(consvars[v]);
               assert(idx2 >= 0);
               idx2 = unfixedvarpos[idx2];
               assert(idx2 < nunfixedvars);

               if( idx2 < 0 )
                  continue;

               /* we add only one directed edge, because the other direction is automatically added for component computation */
               SCIP_CALL( SCIPdigraphAddArc(digraph, idx1, idx2, NULL) );
            }
         }
      }
   }

   SCIPfreeBufferArray(scip, &consvars);

   return SCIP_OKAY;
}

/** performs propagation by searching for components */
static
SCIP_RETCODE findComponents(
   SCIP*                 scip,               /**< SCIP main data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< the components constraint handler data */
   SCIP_Real*            fixedvarsobjsum,    /**< objective contribution of all locally fixed variables, or NULL if fixed variables should not be disregarded */
   SCIP_VAR**            sortedvars,         /**< array to store variables sorted by components, should have enough size for all variables */
   SCIP_CONS**           sortedconss,        /**< array to store (checked) constraints sorted by components, should have enough size for all constraints */
   int*                  compstartsvars,     /**< start points of components in sortedvars array */
   int*                  compstartsconss,    /**< start points of components in sortedconss array */
   int*                  nsortedvars,        /**< pointer to store the number of variables belonging to any component */
   int*                  nsortedconss,       /**< pointer to store the number of (checked) constraints in components */
   int*                  ncomponents,        /**< pointer to store the number of components */
   int*                  ncompsminsize,      /**< pointer to store the number of components not exceeding the minimum size */
   int*                  ncompsmaxsize       /**< pointer to store the number of components not exceeding the maximum size */

   )
{
   SCIP_CONS** tmpconss;
   SCIP_VAR** vars;
   SCIP_Bool success;
   int ntmpconss;
   int nvars;
   int c;

   assert(scip != NULL);
   assert(conshdlrdata != NULL);
   assert(sortedvars != NULL);
   assert(sortedconss != NULL);
   assert(compstartsvars != NULL);
   assert(compstartsconss != NULL);
   assert(nsortedvars != NULL);
   assert(nsortedconss != NULL);
   assert(ncomponents != NULL);
   assert(ncompsminsize != NULL);
   assert(ncompsmaxsize != NULL);

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   if( fixedvarsobjsum != NULL )
      *fixedvarsobjsum = 0.0;

   *ncomponents = 0;
   *ncompsminsize = 0;
   *ncompsmaxsize = 0;

   /* collect checked constraints for component detection */
   ntmpconss = SCIPgetNConss(scip);
   tmpconss = SCIPgetConss(scip);
   (*nsortedconss) = 0;
   for( c = 0; c < ntmpconss; c++ )
   {
      if( SCIPconsIsChecked(tmpconss[c]) )
      {
         sortedconss[(*nsortedconss)] = tmpconss[c];
         (*nsortedconss)++;
      }
   }

   if( nvars > 1 && *nsortedconss > 1 )
   {
      int* unfixedvarpos;
      int* firstvaridxpercons;
      int* varlocks;
      int nunfixedvars = 0;
      int v;

      /* copy variables into a local array */
      SCIP_CALL( SCIPallocBufferArray(scip, &firstvaridxpercons, *nsortedconss) );
      SCIP_CALL( SCIPallocBufferArray(scip, &varlocks, nvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &unfixedvarpos, nvars) );

      /* count number of varlocks for each variable (up + down locks) and multiply it by 2;
       * that value is used as an estimate of the number of arcs incident to the variable's node in the digraph
       * to be safe, we double this value
       */
      for( v = 0; v < nvars; ++v )
      {
         if( (fixedvarsobjsum == NULL) || SCIPisLT(scip, SCIPvarGetLbLocal(vars[v]), SCIPvarGetUbLocal(vars[v])) )
         {
            assert(nunfixedvars <= v);
            sortedvars[nunfixedvars] = vars[v];
            varlocks[nunfixedvars] = 4 * (SCIPvarGetNLocksDown(vars[v]) + SCIPvarGetNLocksUp(vars[v]));
            unfixedvarpos[v] = nunfixedvars;
            ++nunfixedvars;
         }
         else
         {
            unfixedvarpos[v] = -1;
            (*fixedvarsobjsum) += SCIPvarGetObj(vars[v]) * SCIPvarGetLbLocal(vars[v]);
         }
      }
      *nsortedvars = nunfixedvars;

      if( nunfixedvars > 0 )
      {
         SCIP_DIGRAPH* digraph;

         /* create and fill directed graph */
         SCIP_CALL( SCIPdigraphCreate(&digraph, nunfixedvars) );
         SCIP_CALL( SCIPdigraphSetSizes(digraph, varlocks) );
         SCIP_CALL( fillDigraph(scip, digraph, sortedconss, *nsortedconss, unfixedvarpos, nunfixedvars, firstvaridxpercons, &success) );

         if( success )
         {
            int* varcomponent;
            int* conscomponent;

            SCIP_CALL( SCIPallocBufferArray(scip, &varcomponent, nunfixedvars) );
            SCIP_CALL( SCIPallocBufferArray(scip, &conscomponent, nunfixedvars) );

            /* compute independent components */
            SCIP_CALL( SCIPdigraphComputeUndirectedComponents(digraph, 1, varcomponent, ncomponents) );
#ifndef NDEBUG
            SCIPverbMessage(scip, SCIP_VERBLEVEL_FULL, NULL,
               "prop components found %d undirected components at node %lld, depth %d\n",
               *ncomponents, SCIPnodeGetNumber(SCIPgetCurrentNode(scip)), SCIPgetDepth(scip));
#else
            SCIPdebugMessage("prop components found %d undirected components at node %lld, depth %d\n",
               *ncomponents, SCIPnodeGetNumber(SCIPgetCurrentNode(scip)), SCIPgetDepth(scip));
#endif

            if( *ncomponents > 1 )
            {
               int nconss = *nsortedconss;
               int i;

               nvars = *nsortedvars;

               /* create subproblems from independent components */
               SCIP_CALL( sortComponents(scip, conshdlrdata, digraph, sortedconss, sortedvars, varcomponent, conscomponent, nconss, *nsortedvars,
                     firstvaridxpercons, ncompsminsize, ncompsmaxsize) );

               c = 0;
               i = 0;

               while( i < nconss && conscomponent[i] == -1 )
                  ++i;

               for( c = 0; c < *ncomponents + 1; ++c )
               {
                  assert(i == nconss || conscomponent[i] >= c);

                  compstartsconss[c] = i;

                  while( i < nconss && conscomponent[i] == c )
                     ++i;
               }

               for( c = 0, i = 0; c < *ncomponents + 1; ++c )
               {
                  assert(i == nvars || varcomponent[i] >= c);

                  compstartsvars[c] = i;

                  while( i < nvars && varcomponent[i] == c )
                     ++i;
               }

#ifndef NDEBUG
               for( c = 0; c < *ncomponents; ++c )
               {
                  for( i = compstartsconss[c]; i < compstartsconss[c+1]; ++i )
                     assert(conscomponent[i] == c);
                  for( i = compstartsvars[c]; i < compstartsvars[c+1]; ++i )
                     assert(varcomponent[i] == c);
               }
#endif
            }

            SCIPfreeBufferArray(scip, &conscomponent);
            SCIPfreeBufferArray(scip, &varcomponent);
         }

         SCIPdigraphFree(&digraph);
      }

      SCIPfreeBufferArray(scip, &unfixedvarpos);
      SCIPfreeBufferArray(scip, &varlocks);
      SCIPfreeBufferArray(scip, &firstvaridxpercons);
   }

   return SCIP_OKAY;
}


/*
 * Callback methods of constraint handler
 */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyComponents)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludeConshdlrComponents(scip) );

   *valid = TRUE;

   return SCIP_OKAY;
}

/** destructor of constraint handler to free user data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(conshdlrFreeComponents)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   /* free constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIPfreeMemory(scip, &conshdlrdata);
   SCIPconshdlrSetData(conshdlr, NULL);

   return SCIP_OKAY;
}

/** domain propagation method of constraint handler */
static
SCIP_DECL_CONSPROP(consPropComponents)
{  /*lint --e{715}*/
   PROBLEM* problem;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_Longint nodelimit;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);
   assert(SCIPconshdlrGetNActiveConss(conshdlr) >= 0);
   assert(SCIPconshdlrGetNActiveConss(conshdlr) <= 1);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_DIDNOTRUN;

   /** @warning Don't run in probing or in repropagation since this can lead to wrong conclusion
    *
    *  do not run if propagation w.r.t. current objective is not allowed
    */
   if( SCIPinProbing(scip) || SCIPinRepropagation(scip) )
      return SCIP_OKAY;

   /* do not run, if not all variables are explicitly known */
   if( SCIPgetNActivePricers(scip) > 0 )
      return SCIP_OKAY;

   /* we do not want to run, if there are no variables left */
   if( SCIPgetNVars(scip) == 0 )
      return SCIP_OKAY;

   /* check for a reached timelimit */
   if( SCIPisStopped(scip) )
      return SCIP_OKAY;

   /* the components presolver does kind of dual reductions */
   if( !SCIPallowDualReds(scip) )
      return SCIP_OKAY;

   /* only at the root node do we want to run after the node */
   if( proptiming == SCIP_PROPTIMING_AFTERLPLOOP && SCIPgetDepth(scip) > 0 )
      return SCIP_OKAY;

   if( SCIPgetDepth(scip) > conshdlrdata->maxdepth )
   {
      assert(SCIPconshdlrGetNActiveConss(conshdlr) == 0);

      return SCIP_OKAY;
   }

   problem = NULL;
   *result = SCIP_DIDNOTFIND;

   if( SCIPconshdlrGetNActiveConss(conshdlr) >= 1 )
   {
      assert(SCIPconshdlrGetNActiveConss(conshdlr) == 1);

      problem = (PROBLEM*)SCIPconsGetData(SCIPconshdlrGetConss(conshdlr)[0]);
   }
   else
   {
      SCIP_Real fixedvarsobjsum;
      SCIP_VAR** sortedvars;
      SCIP_CONS** sortedconss;
      int* compstartsvars;
      int* compstartsconss;
      int nsortedvars;
      int nsortedconss;
      int ncomponents;
      int ncompsminsize;
      int ncompsmaxsize;

      assert(SCIPconshdlrGetNActiveConss(conshdlr) == 0);

      /* allocate memory for sorted components */
      SCIP_CALL( SCIPallocBufferArray(scip, &sortedvars, SCIPgetNVars(scip)) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sortedconss, SCIPgetNConss(scip)) );
      SCIP_CALL( SCIPallocBufferArray(scip, &compstartsvars, SCIPgetNVars(scip) + 1) );
      SCIP_CALL( SCIPallocBufferArray(scip, &compstartsconss, SCIPgetNVars(scip) + 1) );

      /* search for components */
      SCIP_CALL( findComponents(scip, conshdlrdata, &fixedvarsobjsum, sortedvars, sortedconss, compstartsvars,
            compstartsconss, &nsortedvars, &nsortedconss, &ncomponents, &ncompsminsize, &ncompsmaxsize) );

      if( ncompsminsize > 1 )
      {
         SCIP_CONS* cons;

         SCIPinfoMessage(scip, NULL, "found %d components (%d fulfulling the minsize requirement) at node %lld at depth %d\n",
            ncomponents, ncompsminsize, SCIPnodeGetNumber(SCIPgetCurrentNode(scip)), SCIPgetDepth(scip));

         /* if there are components with size smaller than the limit, we merge them with the smallest component */
         if( ncomponents > ncompsminsize )
         {
            int minsize;
            int size;
            int c;
            int m = 0;

            /* compute minimum size of components to solve individually */
            minsize = getMinsize(scip, conshdlrdata);

            for( c = 0; c < ncomponents; ++c )
            {
               size = compstartsvars[c+1] - compstartsvars[c];

               if( size >= minsize )
               {
                  ++m;
                  compstartsvars[m] = compstartsvars[c+1];
                  compstartsconss[m] = compstartsconss[c+1];
               }
               /* the last component is too small */
               else if( c == ncomponents - 1 )
               {
                  assert(m == ncompsminsize);
                  compstartsvars[m] = compstartsvars[c+1];
                  compstartsconss[m] = compstartsconss[c+1];
               }
            }
            assert(m == ncompsminsize);
            assert(compstartsvars[m] = nsortedvars);
            assert(compstartsconss[m] = nsortedconss);

            ncomponents = m;
         }

         SCIP_CALL( createAndSplitProblem(scip, conshdlrdata, fixedvarsobjsum, sortedvars, sortedconss, compstartsvars, compstartsconss, ncomponents, &problem) );

         SCIP_CALL( createConsComponents(scip, &cons, problem->name, problem) );
         SCIP_CALL( SCIPaddConsNode(scip, SCIPgetCurrentNode(scip), cons, NULL) );
         SCIP_CALL( SCIPreleaseCons(scip, &cons) );
      }

      SCIPfreeBufferArray(scip, &compstartsconss);
      SCIPfreeBufferArray(scip, &compstartsvars);
      SCIPfreeBufferArray(scip, &sortedconss);
      SCIPfreeBufferArray(scip, &sortedvars);
   }

   SCIP_CALL( SCIPgetLongintParam(scip, "limits/nodes", &nodelimit) );

   do
   {
      if( problem != NULL )
      {
         SCIP_CALL( solveProblem(problem, result) );
      }
   } while( *result == SCIP_DELAYNODE && SCIPgetDepth(scip) == 0 && !SCIPisStopped(scip) && SCIPgetNNodes(scip) < nodelimit);

   return SCIP_OKAY;
}

/** presolving method of constraint handler */
static
SCIP_DECL_CONSPRESOL(consPresolComponents)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_VAR** sortedvars;
   SCIP_CONS** sortedconss;
   int* compstartsvars;
   int* compstartsconss;
   int nsortedvars;
   int nsortedconss;
   int ncomponents;
   int ncompsminsize;
   int ncompsmaxsize;
   int nvars;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);
   assert(SCIPconshdlrGetNActiveConss(conshdlr) >= 0);
   assert(SCIPconshdlrGetNActiveConss(conshdlr) <= 1);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_DIDNOTRUN;

   if( SCIPgetStage(scip) != SCIP_STAGE_PRESOLVING || SCIPinProbing(scip) )
      return SCIP_OKAY;

   /* do not run, if not all variables are explicitly known */
   if( SCIPgetNActivePricers(scip) > 0 )
      return SCIP_OKAY;

   nvars = SCIPgetNVars(scip);

   /* we do not want to run, if there are no variables left */
   if( nvars == 0 )
      return SCIP_OKAY;

   /* the presolver should be executed only if it didn't run so far or the number of variables was significantly reduced
    * since the last run
    */
   if( conshdlrdata->presollastnvars != -1 && (nvars > (1 - conshdlrdata->reldecrease) * conshdlrdata->presollastnvars) )
      return SCIP_OKAY;

   /* only call the components presolving, if presolving would be stopped otherwise */
   if( !SCIPisPresolveFinished(scip) )
      return SCIP_OKAY;

   /* check for a reached timelimit */
   if( SCIPisStopped(scip) )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTFIND;

   assert(SCIPconshdlrGetNActiveConss(conshdlr) == 0);

   /* allocate memory for sorted components */
   SCIP_CALL( SCIPallocBufferArray(scip, &sortedvars, SCIPgetNVars(scip)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &sortedconss, SCIPgetNConss(scip)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &compstartsvars, SCIPgetNVars(scip) + 1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &compstartsconss, SCIPgetNVars(scip) + 1) );

   /* search for components */
   SCIP_CALL( findComponents(scip, conshdlrdata, NULL, sortedvars, sortedconss, compstartsvars,
         compstartsconss, &nsortedvars, &nsortedconss, &ncomponents, &ncompsminsize, &ncompsmaxsize) );

   if( ncompsmaxsize > 0 )
   {
      char name[SCIP_MAXSTRLEN];
      SCIP* subscip;
      SCIP_HASHMAP* consmap;
      SCIP_HASHMAP* varmap;
      SCIP_VAR** compvars;
      SCIP_VAR** subvars;
      SCIP_CONS** compconss;
      SCIP_Bool success;
      SCIP_Bool solved;
      int nsolved = 0;
      int ncompvars;
      int ncompconss;
      int comp;

      SCIPinfoMessage(scip, NULL, "found %d components (%d with small size) during presolving; overall problem size: %d vars (%d int, %d bin, %d cont), %d conss\n",
         ncomponents, ncompsmaxsize, SCIPgetNVars(scip), SCIPgetNBinVars(scip), SCIPgetNIntVars(scip), SCIPgetNContVars(scip) + SCIPgetNImplVars(scip), SCIPgetNConss(scip));

      /* build subscip */
      SCIP_CALL( createSubscip(scip, conshdlrdata, &subscip) );

      if( subscip == NULL )
         goto TERMINATE;

      SCIP_CALL( SCIPsetBoolParam(subscip, "misc/usesmalltables", TRUE) );
      SCIP_CALL( SCIPsetIntParam(subscip, "constraints/" CONSHDLR_NAME "/propfreq", -1) );

      /* hashmap mapping from original constraints to constraints in the sub-SCIPs (for performance reasons) */
      SCIP_CALL( SCIPhashmapCreate(&consmap, SCIPblkmem(scip), 10 * nsortedconss) );

      SCIP_CALL( SCIPallocBufferArray(scip, &subvars, nsortedvars) );

      /* loop over all components */
      for( comp = 0; comp < ncompsmaxsize && !SCIPisStopped(scip); comp++ )
      {
         /* get component variables */
         compvars = &(sortedvars[compstartsvars[comp]]);
         ncompvars = compstartsvars[comp + 1 ] - compstartsvars[comp];

         /* get component constraints */
         compconss = &(sortedconss[compstartsconss[comp]]);
         ncompconss = compstartsconss[comp + 1] - compstartsconss[comp];

         /* if we have an unlocked variable, let duality fixing do the job! */
         if( ncompconss == 0 )
         {
            assert(ncompvars == 1);
            continue;
         }

         SCIP_CALL( SCIPhashmapCreate(&varmap, SCIPblkmem(scip), 10 * ncompvars) );
#ifdef DETAILED_OUTPUT
         {
            int nbinvars = 0;
            int nintvars = 0;
            int ncontvars = 0;
            int i;

            for( i = 0; i < ncompvars; ++i )
            {
               if( SCIPvarGetType(compvars[i]) == SCIP_VARTYPE_BINARY )
                  ++nbinvars;
               else if( SCIPvarGetType(compvars[i]) == SCIP_VARTYPE_INTEGER )
                  ++nintvars;
               else
                  ++ncontvars;
            }
            SCIPinfoMessage(scip, NULL, "solve component %d: %d vars (%d bin, %d int, %d cont), %d conss\n",
               comp, ncompvars, nbinvars, nintvars, ncontvars, ncompconss);
         }
#endif
#ifndef NDEBUG
         {
            int i;
            for( i = 0; i < ncompvars; ++i )
               assert(SCIPvarIsActive(compvars[i]));
         }
#endif

         /* get name of the original problem and add "comp_nr" */
         (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s_comp_%d", SCIPgetProbName(scip), comp);

         SCIP_CALL( copyToSubscip(scip, conshdlrdata, subscip, name, compvars, subvars,
               compconss, varmap, consmap, ncompvars, ncompconss, &success) );

         if( !success )
         {
            SCIPhashmapFree(&varmap);
            SCIP_CALL( SCIPfreeTransform(subscip) );
            continue;
         }

         /* solve the subproblem and evaluate the result, i.e. apply fixings of variables and remove constraints */
         SCIP_CALL( solveAndEvalSubscip(scip, conshdlrdata, subscip, compvars, subvars, compconss,
               ncompvars, ncompconss, ndelconss, nfixedvars, nchgbds, result, &solved) );

         /* free variable hash map */
         SCIPhashmapFree(&varmap);

         if( solved )
            ++nsolved;

         /* if the component is unbounded or infeasible, this holds for the complete problem as well */
         if( *result == SCIP_UNBOUNDED || *result == SCIP_CUTOFF )
            break;
         /* if there is only one component left, let's solve this in the main SCIP */
         else if( nsolved == ncomponents - 1 )
            break;
      }

      SCIPfreeBufferArray(scip, &subvars);
      SCIPhashmapFree(&consmap);

      SCIP_CALL( SCIPfree(&subscip) );
   }

 TERMINATE:
   SCIPfreeBufferArray(scip, &compstartsconss);
   SCIPfreeBufferArray(scip, &compstartsvars);
   SCIPfreeBufferArray(scip, &sortedconss);
   SCIPfreeBufferArray(scip, &sortedvars);

   conshdlrdata->presollastnvars = SCIPgetNVars(scip);

   return SCIP_OKAY;
}

/** frees specific constraint data */
static
SCIP_DECL_CONSDELETE(consDeleteComponents)
{  /*lint --e{715}*/
   PROBLEM* problem;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(consdata != NULL);
   assert(*consdata != NULL);

   problem = (PROBLEM*)(*consdata);

   SCIP_CALL( freeProblem(&problem) );

   *consdata = NULL;

   return SCIP_OKAY;
}


/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockComponents)
{  /*lint --e{715}*/
   return SCIP_OKAY;
}

/** presolving initialization method of constraint handler (called when presolving is about to begin) */
static
SCIP_DECL_CONSINITPRE(consInitpreComponents)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert(conshdlr != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   conshdlrdata->presollastnvars = -1;

   return SCIP_OKAY;
}


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolComponents)
{  /*lint --e{715}*/
   assert(nconss == 0);

   return SCIP_OKAY;
}

/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
static
SCIP_DECL_CONSEXITSOL(consExitsolComponents)
{  /*lint --e{715}*/
   if( nconss > 0 )
   {
      assert(nconss == 1);

      SCIP_CALL( SCIPdelCons(scip, conss[0]) );
   }

   return SCIP_OKAY;
}


#define consEnfolpComponents NULL
#define consEnfopsComponents NULL
#define consCheckComponents NULL

/**@} */

/**@name Interface methods
 *
 * @{
 */

/** creates the components constraint handler and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrComponents(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSHDLR* conshdlr;

   /* create components propagator data */
   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlrBasic(scip, &conshdlr, CONSHDLR_NAME, CONSHDLR_DESC, CONSHDLR_ENFOPRIORITY,
         CONSHDLR_CHECKPRIORITY, CONSHDLR_EAGERFREQ, CONSHDLR_NEEDSCONS,
         consEnfolpComponents, consEnfopsComponents, consCheckComponents, consLockComponents,
         conshdlrdata) );
   assert(conshdlr != NULL);

   SCIP_CALL( SCIPsetConshdlrProp(scip, conshdlr, consPropComponents,
         CONSHDLR_PROPFREQ, CONSHDLR_DELAYPROP, CONSHDLR_PROP_TIMING));
   SCIP_CALL( SCIPsetConshdlrPresol(scip, conshdlr, consPresolComponents,
         CONSHDLR_MAXPREROUNDS, CONSHDLR_PRESOLTIMING));

   SCIP_CALL( SCIPsetConshdlrFree(scip, conshdlr, conshdlrFreeComponents) );
   SCIP_CALL( SCIPsetConshdlrInitpre(scip, conshdlr, consInitpreComponents) );
   SCIP_CALL( SCIPsetConshdlrInitsol(scip, conshdlr, consInitsolComponents) );
   SCIP_CALL( SCIPsetConshdlrExitsol(scip, conshdlr, consExitsolComponents) );
   SCIP_CALL( SCIPsetConshdlrCopy(scip, conshdlr, conshdlrCopyComponents, NULL) );
   SCIP_CALL( SCIPsetConshdlrDelete(scip, conshdlr, consDeleteComponents) );

   SCIP_CALL( SCIPaddIntParam(scip,
         "constraints/" CONSHDLR_NAME "/maxdepth",
         "maximum depth of a node to run components detection",
         &conshdlrdata->maxdepth, FALSE, DEFAULT_MAXDEPTH, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "constraints/" CONSHDLR_NAME "/maxintvars",
         "maximum number of integer (or binary) variables to solve a subproblem during presolving (-1: unlimited)",
         &conshdlrdata->maxintvars, TRUE, DEFAULT_MAXINTVARS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "constraints/" CONSHDLR_NAME "/minsize",
         "minimum absolute size (in terms of variables) to solve a component individually during branch-and-bound",
         &conshdlrdata->minsize, TRUE, DEFAULT_MINSIZE, 1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/" CONSHDLR_NAME "/minrelsize",
         "minimum relative size (in terms of variables) to solve a component individually during branch-and-bound",
         &conshdlrdata->minrelsize, TRUE, DEFAULT_MINRELSIZE, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddLongintParam(scip,
         "constraints/" CONSHDLR_NAME "/nodelimit",
         "maximum number of nodes to be solved in subproblems during presolving",
         &conshdlrdata->nodelimit, FALSE, DEFAULT_NODELIMIT, -1LL, SCIP_LONGINT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/" CONSHDLR_NAME "/intfactor",
         "the weight of an integer variable compared to binary variables",
         &conshdlrdata->intfactor, FALSE, DEFAULT_INTFACTOR, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/" CONSHDLR_NAME "/reldecrease",
         "percentage by which the number of variables has to be decreased after the last component solving to allow running again during presolving (1.0: do not run again)",
         &conshdlrdata->reldecrease, FALSE, DEFAULT_RELDECREASE, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/" CONSHDLR_NAME "/feastolfactor",
         "factor to increase the feasibility tolerance of the main SCIP in all sub-SCIPs, default value 1.0",
         &conshdlrdata->feastolfactor, TRUE, DEFAULT_FEASTOLFACTOR, 0.0, 1000000.0, NULL, NULL) );


   return SCIP_OKAY;
}
