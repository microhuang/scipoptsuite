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

/**@file   cons_and.h
 * @brief  constraint handler for and constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __CONS_AND_H__
#define __CONS_AND_H__


#include "scip.h"


/** creates the handler for and constraints and includes it in SCIP */
extern
RETCODE SCIPincludeConsHdlrAnd(
   SCIP*            scip                /**< SCIP data structure */
   );

/** creates and captures a and constraint */
extern
RETCODE SCIPcreateConsAnd(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              nandconss,          /**< number of initial constraints in concatenation */
   CONS**           andconss,           /**< initial constraint in concatenation */
   Bool             enforce,            /**< should the constraint be enforced during node processing? */
   Bool             check,              /**< should the constraint be checked for feasibility? */
   Bool             local,              /**< is constraint only valid locally? */
   Bool             modifiable          /**< is constraint modifiable (subject to column generation)? */
   );

/** adds constraint to the concatenation of an and constraint */
extern
RETCODE SCIPaddConsAnd(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< and constraint */
   CONS*            andcons             /**< additional constraint in concatenation */
   );

#endif
