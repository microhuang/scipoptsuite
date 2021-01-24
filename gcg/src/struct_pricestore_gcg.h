/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2019 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   struct_pricestore.h
 * @brief  datastructures for storing priced cols
 * @author Jonas Witt
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __GCG_STRUCT_PRICESTORE_H__
#define __GCG_STRUCT_PRICESTORE_H__


#include "scip/def.h"
#include "scip/type_lp.h"
#include "scip/type_var.h"
#include "type_pricestore_gcg.h"
#include "pub_gcgcol.h"
#include "type_gcgcol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** storage for priced cols */
struct GCG_PriceStore
{
   SCIP*                 scip;               /**< SCIP data structure */
   GCG_COL**             cols;               /**< array with priced cols sorted by score */
   SCIP_Real*            objparallelisms;    /**< parallelism of col to the objective function */
   SCIP_Real*            orthogonalities;    /**< minimal orthogonality of col with all other cols of larger score */
   SCIP_Real*            scores;             /**< score for each priced col: weighted sum of efficacy and orthogonality */
   int                   colssize;           /**< size of cols and score arrays */
   int                   ncols;              /**< number of priced cols (max. is set->price_maxcols) */
   int                   nforcedcols;        /**< number of forced priced cols (first positions in cols array) */
   int                   nefficaciouscols;   /**< number of improving priced cols */
   int                   ncolsfound;         /**< total number of cols found so far */
   int                   ncolsfoundround;    /**< number of cols found so far in this pricing round */
   int                   ncolsapplied;       /**< total number of cols applied to the LPs */
   SCIP_Bool             infarkas;           /**< is the price storage currently being filled with the columns from farkas pricing? */
   SCIP_Bool             forcecols;          /**< should the cols be used despite the number of cols parameter limit? */
   SCIP_Real             efficiacyfac;       /**< factor of efficiacy in score function */
   SCIP_Real             objparalfac;        /**< factor of objective parallelism in score function */
   SCIP_Real             orthofac;           /**< factor of orthogonalities in score function */
   SCIP_Real             mincolorth;         /**< minimal orthogonality of columns to add
                                                  (with respect to columns added in the current round) */
   SCIP_CLOCK*           priceclock;         /**< pricing time */
   GCG_EFFICIACYCHOICE   efficiacychoice;    /**< choice to base efficiacy on */
};

#ifdef __cplusplus
}
#endif

#endif
