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

/** @brief  PolySCIP solver class
 *
 * The PolySCIP solver class.
 */

#ifndef POLYSCIP_SRC_POLYSCIP_H_INCLUDED
#define POLYSCIP_SRC_POLYSCIP_H_INCLUDED

#include <iostream>
#include <ostream>
#include <string>
#include <utility> // std::pair
#include <vector>

#include "scip/def.h"

namespace polyscip {

    /** General print function
    * @param container Container to be printed
    * @param description Corresponding description
    * @param os Output stream to print to
    */
    template <typename Container>
    void print(const Container& container,
               std::string description,
               std::ostream& os = std::cout) {
        os << description << "[ ";
        for (const auto& elem : container)
            os << elem << " ";
        os << "]\n";
    }

    class Polyscip {
    public:
        /**< type for computed values */
        using ValueType = SCIP_Real;
        /**< type for points, rays in outcome space; needs to support: begin(), size(), operator[] */
        using OutcomeType = std::vector<ValueType>;
        /**< type for weights; needs to support: at(), size() */
        using WeightType = std::vector<ValueType>;
        /**< container type for results; needs to support: empty() */
        using ResultContainer = std::vector <std::pair<OutcomeType, WeightType>>;

        void computeSupportedNondomPoints() = delete;
        void computeUnSupportedNondomPoints() = delete;

        /** Prints given weight to given output stream
         */
        void printWeight(const WeightType& weight, std::ostream& os = std::cout);

        /** Prints given point to given output stream
         */
        void printPoint(const OutcomeType& point, std::ostream& os = std::cout);

        /** Prints given ray to given output stream
         */
        void printRay(const OutcomeType& ray, std::ostream& os = std::cout);

    private:
        ResultContainer supported_nondom_points_;
        ResultContainer unsupported_nondom_points_;
        ResultContainer unbounded_nondom_rays_;

    };

}

#endif // POLYSCIP_SRC_POLYSCIP_H_INCLUDED
