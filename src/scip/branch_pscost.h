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
#pragma ident "@(#) $Id: branch_pscost.h,v 1.10.2.1 2009/06/19 07:53:38 bzfwolte Exp $"

/**@file   branch_pscost.h
 * @brief  pseudo costs branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BRANCH_PSCOST_H__
#define __SCIP_BRANCH_PSCOST_H__


#include "scip/scip.h"


/** creates the pseudo cost braching rule and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeBranchrulePscost(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
