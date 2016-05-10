/*
 * probdata_spa.h
 *
 *  Created on: Dec 1, 2015
 *  Author: bzfeifle
 */

#ifndef APPLICATIONS_SPARSEAPPROX_SRC_PROBDATA_SPA_H_
#define APPLICATIONS_SPARSEAPPROX_SRC_PROBDATA_SPA_H_

#include "scip/scip.h"
#include "tclique/tclique.h"
#include "scip/cons_setppc.h"
#include "scip/type_cons.h"
#include "scip/def.h"


/** free memory allocated for an nxn matrix */
extern
SCIP_RETCODE freeMatrix(
   SCIP_Real**       matrix,           /**< The matrix to be freed */
   int               nbins             /**< The size*/
);

/** gets the minmal non-zero value in a n x n matrix */
extern
SCIP_Real getMinNonZero(
   SCIP*             scip,             /**< SCIP data structure*/
   SCIP_Real**       matrix,           /**< The matrix*/
   int               size              /**< The matrix-size*/
);

/** getter methods for the probdata */
extern
SCIP_Real** SCIPspaGetCmatrix(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
int SCIPspaGetNrBins(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
int SCIPspaGetNrCluster(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
SCIP_VAR*** SCIPspaGetBinvars(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
SCIP_VAR***** SCIPspaGetEdgevars(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
SCIP_VAR** SCIPspaGetIndvars(
   SCIP*             scip              /**< SCIP data structure*/
);
extern
SCIP_VAR** SCIPspaGetAbsvars(
   SCIP*             scip              /**< SCIP data structure*/
);

extern
SCIP_VAR* SCIPspaGetTargetvar(
   SCIP*             scip              /**< SCIP data structure*/
);

/** print all the relevant solution data */
extern
SCIP_RETCODE SCIPspaPrintSolutionValues(
   SCIP*             scip,             /**< SCIP data structure*/
   SCIP_SOL*         sol               /**< The solution containing the values*/
);

/**
 * Create the probdata for an spa-clustering problem
 */
extern
SCIP_RETCODE SCIPcreateProbSpa(
   SCIP*             scip,             /**< SCIP data structure */
   const char*       name,             /**< problem name */
   int               nbins,            /**< number of bins */
   int               nedges,           /**< number of edges */
   int               ncluster,         /**< number of possible cluster */
   SCIP_Real**       edges,            /**< array with start- and endpoints of the edges */
   SCIP_Real*        sd               /**< array with the stationary distribution */
);

#endif /* APPLICATIONS_SPARSEAPPROX_SRC_PROBDATA_SPA_H_ */
