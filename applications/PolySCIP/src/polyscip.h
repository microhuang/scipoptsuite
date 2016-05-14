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

#include <cstdlib>
#include <iostream>
#include <ostream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "cmd_line_args.h"
#include "objscip/objscip.h"
#include "polyscip_types.h"
#include "weight_space_polyhedron.h"

namespace polyscip {

    class Polyscip {
    public:
        Polyscip(int argc, const char *const *argv);

        ~Polyscip();

        SCIP_RETCODE readProblem();

        void computeNondomPoints();

        /** Prints given weight to given output stream
         */
        void printWeight(const WeightType &weight, std::ostream &os = std::cout);

        /** Prints given point to given output stream
         */
        void printPoint(const OutcomeType &point, std::ostream &os = std::cout);

        /** Prints given ray to given output stream
         */
        void printRay(const OutcomeType &ray, std::ostream &os = std::cout);

    private:
        /**< A result comprises of a solution in feasible space,
         * the non-dominated point in objective space and a corresponding weight
         * for which solution is optimal
        */
        using Result = std::tuple<SCIP_SOL *, OutcomeType, WeightType>;
        /** Corresponding fields for Result */
        enum class ResultField {
            Solution, Outcome, Weight
        };

        enum class PolyscipStatus {
            Solved, TimeLimitReached, Unsolved
        };

        using ResultContainer = std::vector<Result>;

        bool filenameIsOkay(const std::string &filename);

        /** Computes first non-dominated point and initializes
         * the weight space polyhedron or finds out that there is no non-dominated point
         * @return true if first non-dom point was found and weight space polyhedron initialized;
         * false otherwise
         */
        SCIP_RETCODE initWeightSpace();

        SCIP_RETCODE setWeightedObjective(const WeightType& weight);

        SCIP_RETCODE solve();

        SCIP_STATUS separateINFORUNBD(const WeightType& weight);

        SCIP_RETCODE handleStatus(SCIP_STATUS status);

        /** Computes the supported solutions and corresponding non-dominated points
         */
        void computeSupported();

        /** Computes the unsupported solutions and corresponding non-dominated points
         */
        void computeUnsupported();

        CmdLineArgs cmd_line_args_;
        PolyscipStatus polyscip_status_;
        SCIP *scip_;
        /**< objective sense of given problem */
        SCIP_Objsense obj_sense_;
        /**< number of objectives */
        std::size_t no_objs_;
        /**< clock measuring the time needed for the entire program */
        SCIP_CLOCK* clock_total_;

        std::unique_ptr<WeightSpacePolyhedron> weight_space_poly_;
        ResultContainer supported_;
        ResultContainer unsupported_;
        ResultContainer unbounded_;
    };

}

#endif //POLYSCIP_SRC_POLYSCIP_H_INCLUDED
