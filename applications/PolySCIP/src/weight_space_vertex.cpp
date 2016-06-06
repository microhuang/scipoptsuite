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

#include "weight_space_vertex.h"

#include <algorithm> // std::copy, std::set_intersection, std::sort
#include <cstddef>
#include <iterator>  // std::back_inserter
#include <memory> //std::shared
#include <numeric>   // std::inner_product;
#include <ostream>

#include "global_functions.h" //global::print
#include "polyscip_types.h"
#include "weight_space_facet.h"
#include "weight_space_polyhedron.h"

using std::ostream;
using std::shared_ptr;
using std::sort;
using std::vector;

namespace polyscip {


    WeightSpaceVertex::WeightSpaceVertex(WeightSpacePolyhedron::FacetContainer incident_facets,
                                         WeightType weight,
                                         ValueType weighted_obj_val,
                                         bool sort_facets)
            : incident_facets_(std::move(incident_facets)),
              weight_(std::move(weight)),
              weighted_obj_val_{weighted_obj_val} {
        if (sort_facets) // sort facets in order to be able to use std::set_intersection in other constructor
            sort(begin(incident_facets_), end(incident_facets_), WeightSpaceFacet::compare_facet_ptr);
    }

    ValueType WeightSpaceVertex::getCurrentWOV() const {
        return weighted_obj_val_;
    }

    WeightSpaceVertex::WeightSpaceVertex(SCIP* scip,
                                         const WeightSpaceVertex *obs,
                                         const WeightSpaceVertex *non_obs,
                                         const OutcomeType &outcome,
                                         bool outcome_is_ray)
    {
        assert (obs != non_obs);
        // get intersection of facets of obs and non_obs
        std::set_intersection(obs->incident_facets_.cbegin(),
                              obs->incident_facets_.cend(),
                              non_obs->incident_facets_.cbegin(),
                              non_obs->incident_facets_.cend(),
                              std::back_inserter(incident_facets_),
                              WeightSpaceFacet::compare_facet_ptr);
        assert(incident_facets_.size() + 1 == obs->incident_facets_.size());
        // add additional facet with respect to outcome
        auto wov_coeff = outcome_is_ray ? 0.0 : 1.0;
        auto new_facet = std::make_shared<const WeightSpaceFacet>(outcome, wov_coeff);
        auto upper_it = std::upper_bound(begin(incident_facets_),
                                         end(incident_facets_),
                                         new_facet, WeightSpaceFacet::compare_facet_ptr);
        incident_facets_.insert(upper_it, std::move(new_facet));
        auto h = calculateCombinationValue(non_obs, obs, outcome, outcome_is_ray);
        assert (SCIPisGE(scip, h, 0.) && SCIPisLE(scip, h, 1.));
        if (outcome_is_ray) // shift combination towards non-obsolete vertex
            h += 1e-7;
        weight_ = calculateWeightCombination(non_obs->weight_, obs->weight_, h);
        weighted_obj_val_ = h*non_obs->getCurrentWOV() + (1.0-h)*obs->getCurrentWOV();
    }


    WeightType WeightSpaceVertex::getWeight() const {
        return weight_;
    }

    ValueType WeightSpaceVertex::getWeightedOutcome(const OutcomeType& outcome) const {
        assert (outcome.size() == weight_.size());
        return std::inner_product(begin(outcome),
                                  end(outcome),
                                  begin(weight_),
                                  0.);
    }

//    bool WeightSpaceVertex::isMadeObsolete(SCIP* scip, const OutcomeType& outcome) const {
//        return isMadeObsolete(scip, outcome, weighted_obj_val_);
//    }
//
//    bool WeightSpaceVertex::isMadeObsolete(SCIP* scip, const OutcomeType& outcome, ValueType rhs) const {
//        assert (outcome.size() == weight_.size());
//        ValueType result = std::inner_product(begin(outcome),
//                                              end(outcome),
//                                              begin(weight_),
//                                              0.);
//        return SCIPisLT(scip, result, rhs) == TRUE;
//    }

    long double WeightSpaceVertex::calculateCombinationValue(const WeightSpaceVertex* non_obs,
                                                           const WeightSpaceVertex* obs,
                                                           const OutcomeType& outcome,
                                                           bool outcome_is_ray) {
        auto wov_obs = outcome_is_ray ? 0. : obs->getCurrentWOV();
        auto wov_non_obs = outcome_is_ray ? 0. : non_obs->getCurrentWOV();
        long double numerator = wov_obs - std::inner_product(begin(obs->weight_),
                                                             end(obs->weight_),
                                                             begin(outcome),
                                                             0.);
        long double denominator = numerator - wov_non_obs + std::inner_product(begin(non_obs->weight_),
                                                                               end(non_obs->weight_),
                                                                               begin(outcome),
                                                                               0.);
        assert (denominator != 0.);
        return numerator / denominator;
    }

    WeightType WeightSpaceVertex::calculateWeightCombination(const WeightType& weight1,
                                                             const WeightType& weight2,
                                                             ValueType h) {
        assert (weight1.size() == weight2.size());
        auto new_weight = WeightType(weight1.size(),0.);
        // set new_weight = h*weight1 + (1-h)*weight2
        transform(begin(weight1), end(weight1), begin(weight2),
                  begin(new_weight), [h](ValueType w1, ValueType w2){return h*w1 + (1-h)*w2;});
        return new_weight;
    }



    bool WeightSpaceVertex::hasSameWeight(const WeightType& weight) const {
        return weight_ == weight;
    }

    bool WeightSpaceVertex::hasUnitWeight() const {
        for (std::size_t i=0; i<weight_.size(); ++i) {
            auto weight = WeightType(weight_.size(), 0.);
            weight[i] = 1.;
            if (hasSameWeight(weight))
                return true;
        }
        return false;
    }

    void WeightSpaceVertex::print(ostream& os, bool printFacets) const {
        global::print(weight_, "WeightSpaceVertex: weight = ", os);
        os << "\n wov = " << weighted_obj_val_ << "\n";
        if (printFacets) {
            os << " defining facets: \n";
            for (const auto &facet : incident_facets_)
                facet->print(os);
        }
    }

}
