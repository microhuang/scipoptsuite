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
#pragma ident "@(#) $Id: heur_actconsdiving.h,v 1.4 2009/04/06 13:06:51 bzfberth Exp $"

/**@file   heur_actconsdiving.h
 * @brief  LP diving heuristic that chooses fixings w.r.t. the active constraints the variable appear in
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_HEUR_ACTCONSDIVING_H__
#define __SCIP_HEUR_ACTCONSDIVING_H__


#include "scip/scip.h"


/** creates the actconsdiving heuristic and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeHeurActconsdiving(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
