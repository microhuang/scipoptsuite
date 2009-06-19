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
#pragma ident "@(#) $Id: heur_guideddiving.h,v 1.11.2.1 2009/06/19 07:53:43 bzfwolte Exp $"

/**@file   heur_guideddiving.h
 * @brief  LP diving heuristic that chooses fixings in direction of incumbent solutions
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_HEUR_GUIDEDDIVING_H__
#define __SCIP_HEUR_GUIDEDDIVING_H__


#include "scip/scip.h"


/** creates the guideddiving heuristic and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeHeurGuideddiving(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
