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
#pragma ident "@(#) $Id: reader_fzn.h,v 1.2.2.2 2009/06/19 07:53:49 bzfwolte Exp $"

/**@file   reader_fzn.h
 * @brief  FlatZinc file reader
 * @author Timo Berthold
 * @author Stefan Heinz
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __READER_FZN_H__
#define __READER_FZN_H__


#include "def.h"
#include "scip/scip.h"


/** includes the FlatZinc file reader into SCIP */
extern
SCIP_RETCODE SCIPincludeReaderFzn(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
