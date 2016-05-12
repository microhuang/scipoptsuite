/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*        This file is part of the program PolySCIP                          */
/*                                                                           */
/*    Copyright (C) 2012-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  PolySCIP is distributed under the terms of the ZIB Academic License.     */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with PolySCIP; see the file LICENCE.                               */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/** @brief Class representing a facet of the weight space polyhedron
 *
 * Data structure representing a facet of the (partial) weight space
 * polyhedron P={(w,a) : w \cdot y >= a \forall y \in Y} where Y is
 * the (current) set of non-dominated points. A facet (w_coeffs_, wov_coeff_) is
 * represented by coefficients 'w_coeffs_' and a right hand side 'wov_coeff_'
 * yielding an inequality of the form w_coeffs_ \cdot w >= wov_coeff_ * wov
 * where wov stands for weighted objective value
 */

#ifndef POLYSCIP_SRC_WEIGHT_SPACE_FACET_H_INCLUDED
#define POLYSCIP_SRC_WEIGHT_SPACE_FACET_H_INCLUDED

#include <iosfwd>
#include <vector>

#include "polyscip_types.h"

namespace polyscip {

    /** Facet of the (partial) weight space polyhedron. */
    class WeightSpaceFacet {
    public:
        bool friend operator<(const WeightSpaceFacet& facet1,
                              const WeightSpaceFacet& facet2);

        /** Creates the facet: outcome \cdot w >= wov_coeff*weighted_obj_val
         *  @param outcome outcome in objective space
         *  @param wov_coeff coefficient for weighted objective value
         */
        explicit WeightSpaceFacet(const OutcomeType& outcome,
                                  ValueType wov_coeff);

        /** Creates the weight space facet w_i >= 0
         *  @param num_objs number of objectives of given problem
         *  @param index index i of w_i >= 0
         */
        explicit WeightSpaceFacet(unsigned num_objs, unsigned index);

        /** Prints facet information to output stream.
         */
        void print(std::ostream& os) const;

    private:
        /**< coefficients for the weight of the facet inequality */
        std::vector<ValueType> w_coeffs_;
        /**< coefficient for the weighted objective value of the facet inequality */
        ValueType wov_coeff_;
    };
}

#endif // POLYSCIP_SRC_WEIGHT_SPACE_FACET_H_INCLUDED
