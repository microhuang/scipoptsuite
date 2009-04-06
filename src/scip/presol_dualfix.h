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
#pragma ident "@(#) $Id: presol_dualfix.h,v 1.14 2009/04/06 13:06:54 bzfberth Exp $"

/**@file   presol_dualfix.h
 * @brief  fixing roundable variables to best bound
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_PRESOL_DUALFIX_H__
#define __SCIP_PRESOL_DUALFIX_H__


#include "scip/scip.h"


/** creates the dual fixing presolver and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludePresolDualfix(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
