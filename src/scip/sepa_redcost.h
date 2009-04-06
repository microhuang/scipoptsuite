/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: sepa_redcost.h,v 1.4 2009/04/06 13:07:01 bzfberth Exp $"

/**@file   sepa_redcost.h
 * @brief  reduced cost strengthening separator
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_SEPA_REDCOST_H__
#define __SCIP_SEPA_REDCOST_H__


#include "scip/scip.h"


/** creates the reduced cost strengthening separator and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeSepaRedcost(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
