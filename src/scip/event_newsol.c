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

/**@file   event_newsol.c
 * @brief  eventhdlr for best/poor solution event
 * @author Jakob Witzig
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "probdata_spa.h"
#include "event_newsol.h"
#include "scip/cons_logicor.h"

#define EVENTHDLR_NAME         "newsol"
#define EVENTHDLR_DESC         "event handler for solution events"

/** copy method for event handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_EVENTCOPY(eventCopyNewsol)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* call inclusion method of event handler */
   SCIP_CALL( SCIPincludeEventHdlrNewsol(scip) );

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitNewsol)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* notify SCIP that your event handler wants to react on the event type best solution found */
   SCIP_CALL( SCIPcatchEvent( scip, SCIP_EVENTTYPE_BESTSOLFOUND | SCIP_EVENTTYPE_POORSOLFOUND, eventhdlr, NULL, NULL) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTEXIT(eventExitNewsol)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* notify SCIP that your event handler wants to drop the event type best solution found */
   SCIP_CALL( SCIPdropEvent( scip, SCIP_EVENTTYPE_BESTSOLFOUND | SCIP_EVENTTYPE_POORSOLFOUND, eventhdlr, NULL, -1) );

   return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecNewsol)
{  /*lint --e{715}*/
   SCIP_VAR*** varmatrix;
   SCIP_SOL* newsol;
   SCIP_CONS* cons;
   SCIP_Real factor = 1.0;
   char name[SCIP_MAXSTRLEN];
   int nbins;
   int ncluster;
   int b;
   int c;

   if( SCIPprobdataGetType(scip) == STP_MAX_NODE_WEIGHT )
      factor = -1.0;

   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(scip != NULL);
   assert(SCIPeventGetType(event) == SCIP_EVENTTYPE_BESTSOLFOUND || SCIPeventGetType(event) == SCIP_EVENTTYPE_POORSOLFOUND);

   SCIPdebugMessage("exec method of event handler for newsol solution found\n");

   newsol = SCIPeventGetSol(event);
   assert(newsol != NULL);

   SCIPdebugMessage("catch event for solution %p with obj=%g.\n", (void*) newsol, SCIPgetSolOrigObj(scip, newsol));

   /* get binary variables corresonding to bin-cluster assignment */
   nbins = SCIPspaGetNrBins(scip);
   ncluster = SCIPspaGetNrCluster(scip);
   varmatrix = SCIPspaGetBinvars(scip);
   assert(nbins >= 0);
   assert(ncluster >= 0);
   assert(varmatrix != NULL);

   /* create a logic-or constraint to separate the current clustering */
   (void) SCIPsnprintf(&name, SCIP_MAXSTRLEN, "newsol_%d", SCIPsolGetIndex(newsol));
   SCIP_CALL( SCIPcreateConsLogicor(scip, &cons, name, 0, NULL, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE) );

   /* iterate through all bins */
   for( b = 0; b < nbins; b++ )
   {
      /* iterate through all clusters */
      for( c = 0; c < ncluster; c++ )
      {
         SCIP_VAR* var;
         SCIP_Real solval;

         var = varmatrix[b][c];
         assert(var != NULL);
         assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

         /* get transformed variable */
         if( !SCIPvarIsTransformed(var) )
            var = SCIPvarGetTransVar(var);
         assert(var != NULL);

         /* skip variabls that are not active */
         if( !SCIPvarIsActive(var) )
            continue;

         /* skip vars that are globally fixed */
         if( SCIPisGE(scip, SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var)) )
            continue;

         solval = SCIPgetSolVal(scip, newsol, var);
         assert(!SCIPisInfinity(scip, REALABS(solval)));
         assert(SCIPisIntegral(scip, solval));

         /* skip variables with solution value 0.0 */
         if( SCIPisEQ(scip, solval, 0.0) )
            continue;
         asert(SCIPisEQ(scip, solval, 1.0));

         SCIP_CALL( SCIPgetNegatedVar(scip, var, &var) );
         assert(var != NULL);

         /* add variable to constraint */
         SCIP_CALL( SCIPaddCoefLogicor(scip, cons, var) );

         /* we can break at this point, because there is at most one varibale set to 1.0 per cluster */
         break;
      }
   }

   /* add and release constraint */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   return SCIP_OKAY;
}

/** includes event handler for best solution found */
SCIP_RETCODE SCIPincludeEventHdlrNewsol(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_EVENTHDLR* eventhdlr;
   eventhdlrdata = NULL;

   eventhdlr = NULL;
   /* create event handler for events on watched variables */
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC, eventExecNewsol, eventhdlrdata) );
   assert(eventhdlr != NULL);

   SCIP_CALL( SCIPsetEventhdlrCopy(scip, eventhdlr, eventCopyNewsol) );
   SCIP_CALL( SCIPsetEventhdlrInit(scip, eventhdlr, eventInitNewsol) );
   SCIP_CALL( SCIPsetEventhdlrExit(scip, eventhdlr, eventExitNewsol) );

   return SCIP_OKAY;
}
