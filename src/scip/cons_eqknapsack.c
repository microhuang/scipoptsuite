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
#pragma ident "@(#) $Id: cons_eqknapsack.c,v 1.20 2005/01/17 12:45:04 bzfpfend Exp $"

/**@file   cons_eqknapsack.c
 * @brief  constraint handler for eqknapsack constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "cons_eqknapsack.h"
#include "cons_linear.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "eqknapsack"
#define CONSHDLR_DESC          "equality knapsack constraints of the form  a^T x == b, x binary"
#define CONSHDLR_SEPAPRIORITY   +000000
#define CONSHDLR_ENFOPRIORITY   +000000
#define CONSHDLR_CHECKPRIORITY  +000000
#define CONSHDLR_SEPAFREQ            -1
#define CONSHDLR_PROPFREQ            -1
#define CONSHDLR_EAGERFREQ          100
#define CONSHDLR_MAXPREROUNDS        -1
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
DECL_CONSFREE(consFreeEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consFreeEqknapsack NULL
#endif


/** initialization method of constraint handler (called after problem was transformed) */
#if 0
static
DECL_CONSINIT(consInitEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitEqknapsack NULL
#endif


/** deinitialization method of constraint handler (called before transformed problem is freed) */
#if 0
static
DECL_CONSEXIT(consExitEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitEqknapsack NULL
#endif


/** presolving initialization method of constraint handler (called when presolving is about to begin) */
#if 0
static
DECL_CONSINITPRE(consInitpreEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitpreEqknapsack NULL
#endif


/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
#if 0
static
DECL_CONSEXITPRE(consExitpreEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitpreEqknapsack NULL
#endif


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
#if 0
static
DECL_CONSINITSOL(consInitsolEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitsolEqknapsack NULL
#endif


/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
#if 0
static
DECL_CONSEXITSOL(consExitsolEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consExitsolEqknapsack NULL
#endif


/** frees specific constraint data */
#if 0
static
DECL_CONSDELETE(consDeleteEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeleteEqknapsack NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */ 
#if 0
static
DECL_CONSTRANS(consTransEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consTransEqknapsack NULL
#endif


/** LP initialization method of constraint handler */
#if 0
static
DECL_CONSINITLP(consInitlpEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consInitlpEqknapsack NULL
#endif


/** separation method of constraint handler */
#if 0
static
DECL_CONSSEPA(consSepaEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consSepaEqknapsack NULL
#endif


/** constraint enforcing method of constraint handler for LP solutions */
static
DECL_CONSENFOLP(consEnfolpEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
DECL_CONSENFOPS(consEnfopsEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
DECL_CONSCHECK(consCheckEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 0
static
DECL_CONSPROP(consPropEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPropEqknapsack NULL
#endif


/** presolving method of constraint handler */
#if 0
static
DECL_CONSPRESOL(consPresolEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPresolEqknapsack NULL
#endif


/** propagation conflict resolving method of constraint handler */
#if 0
static
DECL_CONSRESPROP(consRespropEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consRespropEqknapsack NULL
#endif


/** variable rounding lock method of constraint handler */
static
DECL_CONSLOCK(consLockEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
DECL_CONSACTIVE(consActiveEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consActiveEqknapsack NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
DECL_CONSDEACTIVE(consDeactiveEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeactiveEqknapsack NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
DECL_CONSENABLE(consEnableEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consEnableEqknapsack NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
DECL_CONSDISABLE(consDisableEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDisableEqknapsack NULL
#endif

/** constraint display method of constraint handler */
#if 0
static
DECL_CONSPRINT(consPrintEqknapsack)
{  /*lint --e{715}*/
   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consPrintEqknapsack NULL
#endif




/*
 * Linear constraint upgrading
 */

#ifdef LINCONSUPGD_PRIORITY
static
DECL_LINCONSUPGD(linconsUpgdEqknapsack)
{  /*lint --e{715}*/
   Bool upgrade;

   assert(upgdcons != NULL);
   
   /* check, if linear constraint can be upgraded to a equality knapsack constraint
    * - all variables must be binary
    * - all coefficients must be integral
    * - both sides must be equal
    */
   upgrade = (nposbin + nnegbin == nvars)
      && (ncoeffspone + ncoeffsnone + ncoeffspint + ncoeffsnint == nvars)
      && SCIPisEQ(scip, lhs, rhs);

   if( upgrade )
   {
      debugMessage("upgrading constraint <%s> to eqknapsack constraint\n", SCIPconsGetName(cons));
      
      /* create the bin Eqknapsack constraint (an automatically upgraded constraint is always unmodifiable) */
      assert(!SCIPconsIsModifiable(cons));
      CHECK_OKAY( SCIPcreateConsEqknapsack(scip, upgdcons, SCIPconsGetName(cons), nvars, vars, vals, rhs,
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

/** creates the handler for eqknapsack constraints and includes it in SCIP */
RETCODE SCIPincludeConshdlrEqknapsack(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   CONSHDLRDATA* conshdlrdata;

   /* create eqknapsack constraint handler data */
   conshdlrdata = NULL;
   /* TODO: (optional) create constraint handler specific data here */

   /* include constraint handler */
   CHECK_OKAY( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS, CONSHDLR_NEEDSCONS,
         consFreeEqknapsack, consInitEqknapsack, consExitEqknapsack, 
         consInitpreEqknapsack, consExitpreEqknapsack, consInitsolEqknapsack, consExitsolEqknapsack,
         consDeleteEqknapsack, consTransEqknapsack, consInitlpEqknapsack,
         consSepaEqknapsack, consEnfolpEqknapsack, consEnfopsEqknapsack, consCheckEqknapsack, 
         consPropEqknapsack, consPresolEqknapsack, consRespropEqknapsack, consLockEqknapsack,
         consActiveEqknapsack, consDeactiveEqknapsack, 
         consEnableEqknapsack, consDisableEqknapsack,
         consPrintEqknapsack,
         conshdlrdata) );

#ifdef LINCONSUPGD_PRIORITY
   /* include the linear constraint upgrade in the linear constraint handler */
   CHECK_OKAY( SCIPincludeLinconsUpgrade(scip, linconsUpgdEqknapsack, LINCONSUPGD_PRIORITY) );
#endif

   /* add eqknapsack constraint handler parameters */
   /* TODO: (optional) add constraint handler specific parameters with SCIPaddTypeParam() here */

   return SCIP_OKAY;
}

/** creates and captures a eqknapsack constraint */
RETCODE SCIPcreateConsEqknapsack(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              len,                /**< number of nonzeros in the constraint */
   VAR**            vars,               /**< array with variables of constraint entries */
   Real*            vals,               /**< array with coefficients of constraint entries */
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

   errorMessage("method of eqknapsack constraint handler not implemented yet\n");
   abort(); /*lint --e{527} --e{715}*/

   /* find the eqknapsack constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("eqknapsack constraint handler not found\n");
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
