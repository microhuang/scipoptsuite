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

/**@file   cons_invarknapsack.c
 * @brief  constraint handler for invarknapsack constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "cons_invarknapsack.h"
#include "cons_linear.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "invarknapsack"
#define CONSHDLR_DESC          "invariant knapsack constraint of the form  1^T x <= b or 1^T x == b, x binary"
#define CONSHDLR_SEPAPRIORITY   +000000
#define CONSHDLR_ENFOPRIORITY   +000000
#define CONSHDLR_CHECKPRIORITY  +000000
#define CONSHDLR_SEPAFREQ            -1
#define CONSHDLR_PROPFREQ            -1
#define CONSHDLR_NEEDSCONS         TRUE

#define LINCONSUPGD_PRIORITY    +000000




/*
 * Local methods
 */

/* put your local methods here, and declare them static */




/*
 * Callback methods of constraint handler
 */

/* TODO: Implement all necessary constraint handler methods. The methods with an #if 0 ... #else #define ... are optional */

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
#if 0
static
DECL_CONSFREE(consFreeInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consFreeInvarknapsack NULL
#endif


/** initialization method of constraint handler (called when problem solving starts) */
#if 0
static
DECL_CONSINIT(consInitInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consInitInvarknapsack NULL
#endif


/** deinitialization method of constraint handler (called when problem solving exits) */
#if 0
static
DECL_CONSEXIT(consExitInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consExitInvarknapsack NULL
#endif


/** frees specific constraint data */
#if 0
static
DECL_CONSDELETE(consDeleteInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDeleteInvarknapsack NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */ 
#if 0
static
DECL_CONSTRANS(consTransInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consTransInvarknapsack NULL
#endif


/** LP initialization method of constraint handler */
#if 0
static
DECL_CONSINITLP(consInitlpInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consInitlpInvarknapsack NULL
#endif


/** separation method of constraint handler */
#if 0
static
DECL_CONSSEPA(consSepaInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consSepaInvarknapsack NULL
#endif


/** constraint enforcing method of constraint handler for LP solutions */
static
DECL_CONSENFOLP(consEnfolpInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
DECL_CONSENFOPS(consEnfopsInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
DECL_CONSCHECK(consCheckInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 0
static
DECL_CONSPROP(consPropInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consPropInvarknapsack NULL
#endif


/** presolving method of constraint handler */
#if 0
static
DECL_CONSPRESOL(consPresolInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consPresolInvarknapsack NULL
#endif


/** conflict variable resolving method of constraint handler */
#if 0
static
DECL_CONSRESCVAR(consRescvarInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consRescvarInvarknapsack NULL
#endif


/** variable rounding lock method of constraint handler */
static
DECL_CONSLOCK(consLockInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** variable rounding unlock method of constraint handler */
static
DECL_CONSUNLOCK(consUnlockInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
DECL_CONSACTIVE(consActiveInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consActiveInvarknapsack NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
DECL_CONSDEACTIVE(consDeactiveInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDeactiveInvarknapsack NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
DECL_CONSENABLE(consEnableInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consEnableInvarknapsack NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
DECL_CONSDISABLE(consDisableInvarknapsack)
{
   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDisableInvarknapsack NULL
#endif




/*
 * Linear constraint upgrading
 */

#ifdef LINCONSUPGD_PRIORITY
static
DECL_LINCONSUPGD(linconsUpgdInvarknapsack)
{
   Bool upgrade;

   assert(upgdcons != NULL);
   
   /* check, if linear constraint can be upgraded to a invariant knapsack constraint
    * - all coefficients must be +/- 1
    * - all variables must be binary
    * - either one of the sides is infinite, or both sides are equal
    */
   upgrade = (nposbin + nnegbin == nvars);
   upgrade &= (ncoeffspone + ncoeffsnone == nvars);
   upgrade &= (SCIPisInfinity(scip, -lhs) || SCIPisInfinity(scip, rhs) || SCIPisEQ(scip, lhs, rhs));

   if( upgrade )
   {
      debugMessage("upgrading constraint <%s> to invarknapsack constraint\n", SCIPconsGetName(cons));
      
      /* create the bin Invarknapsack constraint (an automatically upgraded constraint is always unmodifiable) */
      assert(!SCIPconsIsModifiable(cons));
      CHECK_OKAY( SCIPcreateConsInvarknapsack(scip, upgdcons, SCIPconsGetName(cons), nvars, vars, lhs, rhs,
                     SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons), 
                     SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons), 
                     SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons), SCIPconsIsRemoveable(cons)) );
   }

   return SCIP_OKAY;
}
#endif




/*
 * constraint specific interface methods
 */

/** creates the handler for invarknapsack constraints and includes it in SCIP */
RETCODE SCIPincludeConsHdlrInvarknapsack(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   CONSHDLRDATA* conshdlrdata;

   /* create invarknapsack constraint handler data */
   conshdlrdata = NULL;
   /* TODO: (optional) create constraint handler specific data here */

   /* include constraint handler */
   CHECK_OKAY( SCIPincludeConsHdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
                  CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
                  CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_NEEDSCONS,
                  consFreeInvarknapsack, consInitInvarknapsack, consExitInvarknapsack,
                  consDeleteInvarknapsack, consTransInvarknapsack, consInitlpInvarknapsack,
                  consSepaInvarknapsack, consEnfolpInvarknapsack, consEnfopsInvarknapsack, consCheckInvarknapsack, 
                  consPropInvarknapsack, consPresolInvarknapsack, consRescvarInvarknapsack,
                  consLockInvarknapsack, consUnlockInvarknapsack,
                  consActiveInvarknapsack, consDeactiveInvarknapsack, 
                  consEnableInvarknapsack, consDisableInvarknapsack,
                  conshdlrdata) );

#ifdef LINCONSUPGD_PRIORITY
   /* include the linear constraint upgrade in the linear constraint handler */
   CHECK_OKAY( SCIPincludeLinconsUpgrade(scip, linconsUpgdInvarknapsack, LINCONSUPGD_PRIORITY) );
#endif

   /* add invarknapsack constraint handler parameters */
   /* TODO: (optional) add constraint handler specific parameters with SCIPaddTypeParam() here */

   return SCIP_OKAY;
}

/** creates and captures a invarknapsack constraint */
RETCODE SCIPcreateConsInvarknapsack(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              len,                /**< number of nonzeros in the constraint */
   VAR**            vars,               /**< array with variables of constraint entries */
   Real             lhs,                /**< left hand side of constraint */
   Real             rhs,                /**< right hand side of constraint */
   Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP? */
   Bool             separate,           /**< should the constraint be separated during LP processing? */
   Bool             enforce,            /**< should the constraint be enforced during node processing? */
   Bool             check,              /**< should the constraint be checked for feasibility? */
   Bool             propagate,          /**< should the constraint be propagated during node processing? */
   Bool             local,              /**< is constraint only valid locally? */
   Bool             modifiable,         /**< is constraint modifiable (subject to column generation)? */
   Bool             removeable          /**< should the constraint be removed from the LP due to aging or cleanup? */
   )
{
   CONSHDLR* conshdlr;
   CONSDATA* consdata;

   errorMessage("method of invarknapsack constraint handler not implemented yet");
   abort();

   /* find the invarknapsack constraint handler */
   conshdlr = SCIPfindConsHdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("invarknapsack constraint handler not found");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint data */
   consdata = NULL;
   /* TODO: create and store constraint specific data here */

   /* create constraint */
   CHECK_OKAY( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
                  local, modifiable, removeable) );

   return SCIP_OKAY;
}
