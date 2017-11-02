/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   spaplugins.c
 * @brief  SCIP plugins for Sparse Approximation of Transition Networks
 * @author Leon Eifler
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "spaplugins.h"

#include "scip/scipdefplugins.h"
#include "sepa_edge.h"
#include "sepa_subtour.h"
#include "sepa_partition.h"
#include "heur_fuzzyround.h"
#include "heur_spakerlin.h"
#include "branch_multinode.h"

/** includes default plugins for coloring into SCIP */
SCIP_RETCODE SCIPincludeSpaPlugins(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   /* Default and reader */
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );
   SCIP_CALL( SCIPincludeReaderSpa(scip) );

   /* Heuristics */
   SCIP_CALL( SCIPincludeHeurSpakerlin(scip) );
   SCIP_CALL( SCIPincludeHeurFuzzyround(scip) );
   SCIP_CALL( SCIPincludeHeurSpaGreedy(scip) );

   /* Separators */
   SCIP_CALL( SCIPincludeSepaEdge(scip) );
   SCIP_CALL( SCIPincludeSepaPartition(scip) );
   SCIP_CALL( SCIPincludeSepaSubtour(scip) );

   /* Branching rule */
   SCIP_CALL( SCIPincludeBranchruleMultinode(scip) );

   return SCIP_OKAY;
}
