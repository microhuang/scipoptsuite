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

/**@file   cons_varlb.c
 * @brief  constraint handler for varlb constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "cons_varlb.h"
#include "cons_linear.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "varlb"
#define CONSHDLR_DESC          "variable lower bounds of the form  y >= a*x, x binary"
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
DECL_CONSFREE(consFreeVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consFreeVarlb NULL
#endif


/** initialization method of constraint handler (called when problem solving starts) */
#if 0
static
DECL_CONSINIT(consInitVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consInitVarlb NULL
#endif


/** deinitialization method of constraint handler (called when problem solving exits) */
#if 0
static
DECL_CONSEXIT(consExitVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consExitVarlb NULL
#endif


/** frees specific constraint data */
#if 0
static
DECL_CONSDELETE(consDeleteVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDeleteVarlb NULL
#endif


/** transforms constraint data into data belonging to the transformed problem */ 
#if 0
static
DECL_CONSTRANS(consTransVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consTransVarlb NULL
#endif


/** LP initialization method of constraint handler */
#if 0
static
DECL_CONSINITLP(consInitlpVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consInitlpVarlb NULL
#endif


/** separation method of constraint handler */
#if 0
static
DECL_CONSSEPA(consSepaVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consSepaVarlb NULL
#endif


/** constraint enforcing method of constraint handler for LP solutions */
static
DECL_CONSENFOLP(consEnfolpVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
DECL_CONSENFOPS(consEnfopsVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
DECL_CONSCHECK(consCheckVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
#if 0
static
DECL_CONSPROP(consPropVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consPropVarlb NULL
#endif


/** presolving method of constraint handler */
#if 0
static
DECL_CONSPRESOL(consPresolVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consPresolVarlb NULL
#endif


/** conflict variable resolving method of constraint handler */
#if 0
static
DECL_CONSRESCVAR(consRescvarVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consRescvarVarlb NULL
#endif


/** variable rounding lock method of constraint handler */
static
DECL_CONSLOCK(consLockVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** variable rounding unlock method of constraint handler */
static
DECL_CONSUNLOCK(consUnlockVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
DECL_CONSACTIVE(consActiveVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consActiveVarlb NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
DECL_CONSDEACTIVE(consDeactiveVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDeactiveVarlb NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
DECL_CONSENABLE(consEnableVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consEnableVarlb NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
DECL_CONSDISABLE(consDisableVarlb)
{
   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   return SCIP_OKAY;
}
#else
#define consDisableVarlb NULL
#endif




/*
 * Linear constraint upgrading
 */

#ifdef LINCONSUPGD_PRIORITY
static
DECL_LINCONSUPGD(linconsUpgdVarlb)
{
   Bool upgrade;

   assert(upgdcons != NULL);
   
   /* check, if linear constraint can be upgraded to a variable lower bound constraint
    * - there is one binary variable and one non-binary variable
    * - if the non-binary variable coefficient is negative, the right hand side is zero and the left hand side is infinite,
    *   if the non-binary variable coefficient is positive, the left hand side is zero and the right hand side is infinite
    */
   upgrade = (nvars == 2);
   upgrade &= (nposbin + nnegbin == 1);
   upgrade &= (nposint + nnegint + nposimpl + nnegimpl + nposcont + nnegcont == 1);
   upgrade &= ((nnegint + nnegimpl + nnegcont == 1 && SCIPisZero(scip, rhs) && SCIPisInfinity(scip, -lhs))
      || (nposint + nposimpl + nposcont == 1 && SCIPisZero(scip, lhs) && SCIPisInfinity(scip, rhs)));

   if( upgrade )
   {
      VAR* var;
      VAR* switchvar;
      Real val;

      debugMessage("upgrading constraint <%s> to varlb constraint\n", SCIPconsGetName(cons));

      /* find out the variable with variable bound, and the bound switching variable */
      if( SCIPvarGetType(vars[0]) == SCIP_VARTYPE_BINARY )
      {
         switchvar = vars[0];
         var = vars[1];
         val = -vals[0]/vals[1];
      }
      else
      {
         switchvar = vars[1];
         var = vars[0];
         val = -vals[1]/vals[0];
      }

      /* create the bin Varlb constraint (an automatically upgraded constraint is always unmodifiable) */
      assert(!SCIPconsIsModifiable(cons));
      CHECK_OKAY( SCIPcreateConsVarlb(scip, upgdcons, SCIPconsGetName(cons), nvars, var, switchvar, val,
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

/** creates the handler for varlb constraints and includes it in SCIP */
RETCODE SCIPincludeConsHdlrVarlb(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   CONSHDLRDATA* conshdlrdata;

   /* create varlb constraint handler data */
   conshdlrdata = NULL;
   /* TODO: (optional) create constraint handler specific data here */

   /* include constraint handler */
   CHECK_OKAY( SCIPincludeConsHdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
                  CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
                  CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_NEEDSCONS,
                  consFreeVarlb, consInitVarlb, consExitVarlb,
                  consDeleteVarlb, consTransVarlb, consInitlpVarlb,
                  consSepaVarlb, consEnfolpVarlb, consEnfopsVarlb, consCheckVarlb, 
                  consPropVarlb, consPresolVarlb, consRescvarVarlb,
                  consLockVarlb, consUnlockVarlb,
                  consActiveVarlb, consDeactiveVarlb, 
                  consEnableVarlb, consDisableVarlb,
                  conshdlrdata) );

#ifdef LINCONSUPGD_PRIORITY
   /* include the linear constraint upgrade in the linear constraint handler */
   CHECK_OKAY( SCIPincludeLinconsUpgrade(scip, linconsUpgdVarlb, LINCONSUPGD_PRIORITY) );
#endif

   /* add varlb constraint handler parameters */
   /* TODO: (optional) add constraint handler specific parameters with SCIPaddTypeParam() here */

   return SCIP_OKAY;
}

/** creates and captures a varlb constraint */
RETCODE SCIPcreateConsVarlb(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              len,                /**< number of nonzeros in the constraint */
   VAR*             var,                /**< variable that has variable bound */
   VAR*             switchvar,          /**< binary variable to activate bound */
   Real             val,                /**< bound value */
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

   errorMessage("method of varlb constraint handler not implemented yet");
   abort();

   /* find the varlb constraint handler */
   conshdlr = SCIPfindConsHdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("varlb constraint handler not found");
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
