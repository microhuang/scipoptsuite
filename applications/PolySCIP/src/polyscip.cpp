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

/** @brief  PolySCIP functions
 * @author Sebastian Schenker
 *
 * Implements PolySCIP solver class.
 */


#include "polyscip.h"

#include <algorithm> //std::transform, std::max, std::copy
#include <array>
#include <cmath> //std::abs
#include <cstddef> //std::size_t
#include <fstream>
#include <functional> //std::plus, std::negate, std::function, std::reference_wrapper
#include <iomanip> //std::set_precision
#include <iostream>
#include <iterator> //std::advance, std::back_inserter
#include <limits>
#include <list>
#include <ostream>
#include <map>
#include <memory> //std::addressof, std::unique_ptr
#include <numeric> //std::inner_product
#include <string>
#include <type_traits> //std::remove_const
#include <utility> //std::make_pair
#include <vector>

#include "polytope_representation.h"
#include "scip/scip.h"
#include "objscip/objscipdefplugins.h"
#include "cmd_line_args.h"
#include "global_functions.h"
#include "polyscip_types.h"
#include "prob_data_objectives.h"
#include "ReaderMOP.h"
#include "weight_space_polyhedron.h"

using std::addressof;
using std::array;
using std::begin;
using std::cout;
using std::end;
using std::list;
using std::make_pair;
using std::map;
using std::max;
using std::ostream;
using std::pair;
using std::reference_wrapper;
using std::size_t;
using std::string;
using std::vector;

namespace polyscip {

    using DDMethod = polytoperepresentation::DoubleDescriptionMethod;

    TwoDProj::TwoDProj(const OutcomeType& outcome, size_t first, size_t second)
            : proj_(outcome.at(first), outcome.at(second))
    {}

    ostream& operator<<(ostream& os, const TwoDProj& p) {
        os << "Proj = [" << p.proj_.first << ", " << p.proj_.second << "]";
        return os;
    }


    ostream& operator<<(ostream& os, const NondomProjections& nd) {
        os << "Nondominated projections: ";
        for (const auto& p_pair : nd.nondom_projections_)
            os << p_pair.first << " ";
        return os;
    }

    NondomProjections::ProjMap::iterator NondomProjections::add(TwoDProj proj, Result res) {
        auto ret_find = nondom_projections_.find(proj);
        if (ret_find == end(nondom_projections_)) { // key not found
            auto ret = nondom_projections_.emplace(std::move(proj), ResultContainer{std::move(res)});
            return ret.first;
        }
        else { // key found
            nondom_projections_[proj].push_back(std::move(res));
            return ret_find;
        }
    }


    NondomProjections::NondomProjections(double eps,
                                         const ResultContainer &supported,
                                         size_t first,
                                         size_t second)
            : epsilon_(eps),
              nondom_projections_([eps](const TwoDProj& lhs, const TwoDProj& rhs){
                  if (lhs.getFirst() + eps < rhs.getFirst())
                      return true;
                  else if (rhs.getFirst() + eps < lhs.getFirst())
                      return false;
                  else
                      return lhs.getSecond() < rhs.getSecond();
              })
    {
        assert (first < second);
        assert (!supported.empty());
        for (const auto& res : supported) {
            add(TwoDProj(res.second, first, second), res);
        }

        auto it = begin(nondom_projections_);
        while (std::next(it) != end(nondom_projections_)) {
            auto next = std::next(it);
            if (epsilonDominates(it->first, next->first)) {
                nondom_projections_.erase(next);
            }
            else {
                ++it;
            }
        }
        assert (!nondom_projections_.empty());
        current_ = begin(nondom_projections_);
    }

    bool NondomProjections::epsilonDominates(const TwoDProj& lhs, const TwoDProj& rhs) const {
        return lhs.getFirst() - epsilon_ < rhs.getFirst() && lhs.getSecond() - epsilon_ < rhs.getSecond();
    }

    void NondomProjections::update() {
        assert (current_ != std::prev(end(nondom_projections_)) && current_ != end(nondom_projections_));
        ++current_;
    }

    void NondomProjections::update(TwoDProj proj, Result res) {
        assert (current_ != std::prev(end(nondom_projections_)) && current_ != end(nondom_projections_));
        auto it = add(proj, res);
        if (epsilonDominates(proj, current_->first)) {
            nondom_projections_.erase(current_);
            current_ = it;
        }

        while (std::next(it) != end(nondom_projections_) && epsilonDominates(proj, std::next(it)->first)) {
            nondom_projections_.erase(std::next(it));
        }
    }

    vector<OutcomeType> NondomProjections::getNondomProjOutcomes() const {
        auto outcomes = vector<OutcomeType>{};
        for (auto it=begin(nondom_projections_); it!=end(nondom_projections_); ++it) {
            for (const auto& res : it->second)
                outcomes.push_back(res.second);
        }
        return outcomes;
    }

    bool NondomProjections::finished() const {
        assert (current_ != end(nondom_projections_));
        return current_ == std::prev(end(nondom_projections_));
    }

    RectangularBox::RectangularBox(const std::vector<Interval>& box)
            : box_(begin(box), end(box))
    {}

    RectangularBox::RectangularBox(std::vector<Interval>&& box)
            : box_(begin(box), end(box))
    {}

    RectangularBox::RectangularBox(vector<Interval>::const_iterator first_beg,
                                   vector<Interval>::const_iterator first_end,
                                   Interval second,
                                   vector<Interval>::const_iterator third_beg,
                                   vector<Interval>::const_iterator third_end) {
        std::copy(first_beg, first_end, std::back_inserter(box_));
        box_.push_back(second);
        std::copy(third_beg, third_end, std::back_inserter(box_));
    }

    size_t RectangularBox::size() const {
        return box_.size();
    }

    RectangularBox::Interval RectangularBox::getInterval(size_t index) const {
        assert (index < size());
        return box_[index];
    }

    std::ostream &operator<<(std::ostream& os, const RectangularBox& box) {
        for (auto interval : box.box_)
            os << "[ " << interval.first << ", " << interval.second << " ) ";
        os << "\n";
        return os;
    }

    bool RectangularBox::isSupersetOf(const RectangularBox &other) const {
        assert (other.box_.size() == this->box_.size());
        for (size_t i=0; i<box_.size(); ++i) {
            if (box_[i].first > other.box_[i].first || box_[i].second < other.box_[i].second)
                return false;
        }
        return true;
    }

    bool RectangularBox::isSubsetOf(const RectangularBox &other) const {
        assert (this->box_.size() == other.box_.size());
        for (size_t i=0; i<box_.size(); ++i) {
            if (box_[i].first < other.box_[i].first || box_[i].second > other.box_[i].second)
                return false;
        }
        return true;
    }

    bool RectangularBox::isDisjointFrom(const RectangularBox &other) const {
        assert (this->box_.size() == other.box_.size());
        for (size_t i=0; i<box_.size(); ++i) {
            auto int_beg = std::max(box_[i].first, other.box_[i].first);
            auto int_end = std::min(box_[i].second, other.box_[i].second);
            if (int_beg > int_end)
                return true;
        }
        return false;
    }

    bool RectangularBox::isFeasible(double epsilon) const {
        for (const auto& elem : box_) {
            if (elem.first + epsilon > elem.second)
                return false;
        }
        return true;
    }

    RectangularBox::Interval RectangularBox::getIntervalIntersection(std::size_t index, const RectangularBox& other) const {
        assert (box_.size() == other.box_.size());
        auto int_beg = std::max(box_[index].first, other.box_[index].first);
        auto int_end = std::min(box_[index].second, other.box_[index].second);
        assert (int_beg <= int_end);
        return {int_beg, int_end};
    }

    vector<RectangularBox> RectangularBox::getDisjointPartsFrom(double delta, const RectangularBox &other) const {
        auto size = this->box_.size();
        assert (size == other.box_.size());
        auto disjoint_partitions = vector<RectangularBox>{};
        auto intersections = vector<Interval>{};
        for (size_t i=0; i<size; ++i) {
            if (box_[i].first < other.box_[i].first - epsilon_) { // non-empty to the left
                auto new_box = RectangularBox(begin(intersections), end(intersections),
                                              {box_[i].first, other.box_[i].first - epsilon_},
                                              begin(box_)+(i+1), end(box_));
                if (new_box.isFeasible(delta))
                    disjoint_partitions.push_back(std::move(new_box));

            }
            if (other.box_[i].second + epsilon_ < box_[i].second) { // non-empty to the right
                auto new_box = RectangularBox(begin(intersections), end(intersections),
                                              {other.box_[i].second + epsilon_, box_[i].second},
                                              begin(box_)+(i+1), end(box_));
                if (new_box.isFeasible(delta))
                    disjoint_partitions.push_back(std::move(new_box));
            }
            intersections.push_back(getIntervalIntersection(i, other));
        }
        return disjoint_partitions;
    }

    Polyscip::Polyscip(int argc, const char *const *argv)
            : cmd_line_args_(argc, argv),
              polyscip_status_(PolyscipStatus::Unsolved),
              scip_(nullptr),
              obj_sense_(SCIP_OBJSENSE_MINIMIZE), // default objective sense is minimization
              no_objs_(0),
              clock_total_(nullptr),
              only_weight_space_phase_(false), // re-set in readProblem()
              is_lower_dim_prob_(false),
              is_sub_prob_(false)
    {
        SCIPcreate(&scip_);
        assert (scip_ != nullptr);
        SCIPincludeDefaultPlugins(scip_);
        SCIPincludeObjReader(scip_, new ReaderMOP(scip_), TRUE);
        SCIPcreateClock(scip_, addressof(clock_total_));

        if (cmd_line_args_.hasParameterFile()) {
            if (filenameIsOkay(cmd_line_args_.getParameterFile())) {
                SCIPreadParams(scip_, cmd_line_args_.getParameterFile().c_str());
            }
            else {
                cout << "Invalid parameter settings file.\n";
                polyscip_status_ = PolyscipStatus::Error;
            }
        }

        if (cmd_line_args_.hasTimeLimit() && cmd_line_args_.getTimeLimit() <= 0) {
            cout << "Invalid time limit.\n";
            polyscip_status_ = PolyscipStatus::Error;
        }

        if (!filenameIsOkay(cmd_line_args_.getProblemFile())) {
            cout << "Invalid problem file.\n";
            polyscip_status_ = PolyscipStatus::Error;
        }
    }

    /*Polyscip::Polyscip(const CmdLineArgs& cmd_line_args,
                       SCIP *scip,
                       SCIP_Objsense obj_sense,
                       pair<size_t, size_t> objs_to_be_ignored,
                       SCIP_CLOCK *clock_total)
            : cmd_line_args_{cmd_line_args},
              polyscip_status_{PolyscipStatus::ProblemRead},
              scip_{scip},
              obj_sense_{obj_sense},
              clock_total_{clock_total},
              only_weight_space_phase_{false},
              is_lower_dim_prob_(true),
              is_sub_prob_(false)
    {
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        obj_probdata->ignoreObjectives(objs_to_be_ignored.first, objs_to_be_ignored.second);
        no_objs_ = obj_probdata->getNoObjs();
    }*/

    Polyscip::Polyscip(const CmdLineArgs& cmd_line_args,
                       SCIP *scip,
                       size_t no_objs,
                       SCIP_CLOCK *clock_total)
            : cmd_line_args_{cmd_line_args},
              polyscip_status_{PolyscipStatus::ProblemRead},
              scip_{scip},
              obj_sense_{SCIP_OBJSENSE_MINIMIZE},//SCIPgetObjsense(scip_)
              no_objs_{no_objs},
              clock_total_{clock_total},
              only_weight_space_phase_{false},
              is_lower_dim_prob_(false),
              is_sub_prob_(true)
    {}

    Polyscip::~Polyscip() {
        if (is_lower_dim_prob_) {
            auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
            obj_probdata->unignoreObjectives();
        }
        else if (!is_sub_prob_) {
            SCIPfreeClock(scip_, addressof(clock_total_));
            SCIPfree(addressof(scip_));
        }
    }


    void Polyscip::printStatus(std::ostream& os) const {
        switch(polyscip_status_) {
            case PolyscipStatus::TwoProjPhase:
                os << "PolySCIP Status: ComputeProjectedNondomPointsPhase\n";
                break;
            case PolyscipStatus::Error:
                os << "PolySCIP Status: Error\n";
                break;
            case PolyscipStatus::Finished:
                os << "PolySCIP Status: Successfully finished\n";
                break;
            case PolyscipStatus::LexOptPhase:
                os << "PolySCIP Status: LexOptPhase\n";
                break;
            case PolyscipStatus::ProblemRead:
                os << "PolySCIP Status: ProblemRead\n";
                break;
            case PolyscipStatus::TimeLimitReached:
                os << "PolySCIP Status: TimeLimitReached\n";
                break;
            case PolyscipStatus::Unsolved:
                os << "PolySCIP Status: Unsolved\n";
                break;
            case PolyscipStatus::WeightSpacePhase:
                os << "PolySCIP Status: WeightSpacePhase\n";
                break;
        }
    }

    SCIP_RETCODE Polyscip::computeNondomPoints() {
        if (polyscip_status_ == PolyscipStatus::ProblemRead) {
            SCIP_CALL(SCIPstartClock(scip_, clock_total_));

            auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
            auto nonzero_orig_vars = vector<vector<SCIP_VAR*>>{};
            auto nonzero_orig_vals = vector<vector<ValueType>>{};
            for (size_t obj=0; obj < no_objs_; ++obj) {
                nonzero_orig_vars.push_back(obj_probdata->getNonZeroCoeffVars(obj));
                assert (!nonzero_orig_vars.empty());
                auto nonzero_obj_vals = vector<ValueType>{};
                std::transform(nonzero_orig_vars.back().cbegin(),
                               nonzero_orig_vars.back().cend(),
                               std::back_inserter(nonzero_obj_vals),
                               [obj, obj_probdata](SCIP_VAR *var) { return obj_probdata->getObjCoeff(var, obj); });
                nonzero_orig_vals.push_back(std::move(nonzero_obj_vals));
            }

            SCIP_CALL( computeLexicographicOptResults(nonzero_orig_vars, nonzero_orig_vals) );

            if (polyscip_status_ == PolyscipStatus::LexOptPhase) {
                if (no_objs_ > 3) {
                    cout << "Number of objectives > 3: only computing SNDE Points\n";
                    SCIP_CALL(computeWeightSpaceResults());
                }
                else if (only_weight_space_phase_) {
                    SCIP_CALL(computeWeightSpaceResults());
                }
                else {
                    SCIP_CALL(computeTwoProjResults(nonzero_orig_vars, nonzero_orig_vals));
                }
            }
            SCIP_CALL(SCIPstopClock(scip_, clock_total_));
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeLexicographicOptResults(vector<vector<SCIP_VAR*>>& orig_vars,
                                                          vector<vector<ValueType>>& orig_vals) {
        polyscip_status_ = PolyscipStatus::LexOptPhase;
        for (size_t obj=0; obj<no_objs_; ++obj) {
            if (polyscip_status_ == PolyscipStatus::LexOptPhase)
                SCIP_CALL(computeLexicographicOptResult(obj, orig_vars, orig_vals));
            else
                break;
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeLexicographicOptResult(size_t considered_obj,
                                                          vector<vector<SCIP_VAR*>>& orig_vars,
                                                          vector<vector<ValueType>>& orig_vals) {
        assert (considered_obj < no_objs_);
        auto current_obj = considered_obj;
        auto obj_val_cons = vector<SCIP_CONS*>{};
        auto weight = WeightType(no_objs_, 0.);
        auto scip_status = SCIP_STATUS_UNKNOWN;
        for (size_t counter = 0; counter<no_objs_; ++counter) {
            weight[current_obj] = 1;
            SCIP_CALL( setWeightedObjective(weight) );
            SCIP_CALL( solve() );
            scip_status = SCIPgetStatus(scip_);
            if (scip_status == SCIP_STATUS_INFORUNBD)
                scip_status = separateINFORUNBD(weight);

            if (scip_status == SCIP_STATUS_OPTIMAL) {
                if (counter < no_objs_-1) {
                    auto opt_value = SCIPgetPrimalbound(scip_);

                    SCIP_CALL(SCIPfreeTransform(scip_));
                    auto cons = createObjValCons(orig_vars[current_obj],
                                                 orig_vals[current_obj],
                                                 opt_value,
                                                 opt_value);
                    SCIP_CALL (SCIPaddCons(scip_, cons));
                    obj_val_cons.push_back(cons);
                }
            }
            else if (scip_status == SCIP_STATUS_UNBOUNDED) {
                assert (current_obj == considered_obj);
                SCIP_CALL( handleUnboundedStatus(true) );
                break;
            }
            else if (scip_status == SCIP_STATUS_TIMELIMIT) {
                polyscip_status_ = PolyscipStatus::TimeLimitReached;
                break;
            }
            else if (scip_status == SCIP_STATUS_INFEASIBLE) {
                assert (current_obj == 0);
                polyscip_status_ = PolyscipStatus::Finished;
                break;
            }
            else {
                polyscip_status_ = PolyscipStatus::Error;
                break;
            }
            weight[current_obj] = 0;
            current_obj = (current_obj+1) % no_objs_;
        }

        if (scip_status == SCIP_STATUS_OPTIMAL) {
            auto lex_opt_result = getOptimalResult();
            if (outcomeIsNew(lex_opt_result.second, begin(bounded_), end(bounded_))) {
                bounded_.push_back(std::move(lex_opt_result));
            }
        }

        // release and delete added constraints
        for (auto cons : obj_val_cons) {
            SCIP_CALL( SCIPfreeTransform(scip_) );
            SCIP_CALL( SCIPdelCons(scip_, cons) );
            SCIP_CALL( SCIPreleaseCons(scip_, addressof(cons)) );
        }
        return SCIP_OKAY;
    }


    SCIP_RETCODE Polyscip::computeTwoProjResults(const vector<vector<SCIP_VAR*>>& orig_vars,
                                                 const vector<vector<ValueType>>& orig_vals) {
        polyscip_status_ = PolyscipStatus::TwoProjPhase;

        // consider all (k over 2 ) combinations of considered objective functions
        std::map<ObjPair, vector<OutcomeType>> proj_nondom_outcomes_map;
        for (size_t obj_1=0; obj_1!=no_objs_-1; ++obj_1) {
            for (auto obj_2=obj_1+1; obj_2!=no_objs_; ++obj_2) {
                if (polyscip_status_ == PolyscipStatus::TwoProjPhase) {
                    auto proj_nondom_outcomes = solveWeightedTchebycheff(orig_vars,
                                                                         orig_vals,
                                                                         obj_1, obj_2);

                    proj_nondom_outcomes_map.insert({ObjPair(obj_1, obj_2), proj_nondom_outcomes});
                }
            }
        }


        if (no_objs_ == 3) {
            auto feasible_boxes = computeFeasibleBoxes(proj_nondom_outcomes_map,
                                                       orig_vars,
                                                       orig_vals);
            auto disjoint_boxes = computeDisjointBoxes(std::move(feasible_boxes));

            assert (feasible_boxes.size() <= disjoint_boxes.size());

            for (const auto& box : disjoint_boxes) {
                auto new_res = computeNondomPointsInBox(box,
                                                        orig_vars,
                                                        orig_vals);
                for (const auto& res : new_res) {
                    if (is_sub_prob_) {
                        bounded_.push_back(res);
                    }
                    else {
                        if (!boxResultIsDominated(res.second, orig_vars, orig_vals)) {
                            bounded_.push_back(std::move(res));
                        }
                    }
                }
            }
        }

        if (polyscip_status_ == PolyscipStatus::TwoProjPhase)
            polyscip_status_ = PolyscipStatus::Finished;

        return SCIP_OKAY;
    }

    bool Polyscip::boxResultIsDominated(const OutcomeType& outcome,
                                        const vector<vector<SCIP_VAR*>>& orig_vars,
                                        const vector<vector<ValueType>>& orig_vals) {

        auto size = outcome.size();
        assert (size == orig_vars.size());
        assert (size == orig_vals.size());
        auto is_dominated = false;

        auto ret = SCIPfreeTransform(scip_);
        assert (ret == SCIP_OKAY);

        auto new_cons = vector<SCIP_CONS*>{};
        // add objective value constraints
        for (size_t i=0; i<size; ++i) {
            auto cons = createObjValCons(orig_vars[i],
                                         orig_vals[i],
                                         -SCIPinfinity(scip_),
                                         outcome[i]);
            ret =  SCIPaddCons(scip_, cons);
            assert (ret == SCIP_OKAY);
            new_cons.push_back(cons);
        }

        auto weight = WeightType(no_objs_, 1.);
        setWeightedObjective(weight/*zero_weight*/);
        solve(); // compute with zero objective
        auto scip_status = SCIPgetStatus(scip_);

        // check solution status
        if (scip_status == SCIP_STATUS_OPTIMAL) {
            assert (weight.size() == outcome.size());
            if (SCIPgetPrimalbound(scip_) + cmd_line_args_.getEpsilon() < std::inner_product(begin(weight),
                                                                                             end(weight),
                                                                                             begin(outcome),
                                                                                             0.)) {
                is_dominated = true;
            }
        }
        else if (scip_status == SCIP_STATUS_TIMELIMIT) {
            polyscip_status_ = PolyscipStatus::TimeLimitReached;
        }
        else {
            polyscip_status_ = PolyscipStatus::Error;
        }

        ret = SCIPfreeTransform(scip_);
        assert (ret == SCIP_OKAY);
        // release and delete added constraints
        for (auto cons : new_cons) {
            ret = SCIPdelCons(scip_, cons);
            assert (ret == SCIP_OKAY);
            ret = SCIPreleaseCons(scip_, addressof(cons));
            assert (ret == SCIP_OKAY);
        }

        return is_dominated;
    }

    ResultContainer Polyscip::computeNondomPointsInBox(const RectangularBox& box,
                                                       const vector<vector<SCIP_VAR *>>& orig_vars,
                                                       const vector<vector<ValueType>>& orig_vals) {
        assert (box.size() == orig_vars.size());
        assert (box.size() == orig_vals.size());
        // add constraints on objective values given by box
        auto obj_val_cons = vector<SCIP_CONS *>{};
        if (SCIPisTransformed(scip_)) {
            auto ret = SCIPfreeTransform(scip_);
            assert (ret == SCIP_OKAY);
        }
        for (size_t i=0; i<box.size(); ++i) {
            auto interval = box.getInterval(i);
            auto new_cons = createObjValCons(orig_vars[i],
                                             orig_vals[i],
                                             interval.first,
                                             interval.second - cmd_line_args_.getDelta());
            auto ret = SCIPaddCons(scip_, new_cons);
            assert (ret == SCIP_OKAY);
            obj_val_cons.push_back(new_cons);
        }

        std::unique_ptr<Polyscip> sub_poly(new Polyscip(cmd_line_args_,
                                                        scip_,
                                                        no_objs_,
                                                        clock_total_) );
        sub_poly->computeNondomPoints();

        // release and delete objective value constraints
        if (SCIPisTransformed(scip_)) {
            auto ret = SCIPfreeTransform(scip_);
            assert (ret == SCIP_OKAY);
        }
        for (auto cons : obj_val_cons) {
            auto ret = SCIPdelCons(scip_, cons);
            assert (ret == SCIP_OKAY);
            ret = SCIPreleaseCons(scip_, addressof(cons));
            assert (ret == SCIP_OKAY);
        }

        // check computed subproblem results
        assert (!sub_poly->unboundedResultsExist());
        assert (sub_poly->getStatus() == PolyscipStatus::Finished);

        auto new_nondom_res = ResultContainer{};
        if (sub_poly->numberOfBoundedResults() > 0) {
            for (auto it = sub_poly->supportedCBegin(); it != sub_poly->supportedCEnd(); ++it) {
                new_nondom_res.push_back(std::move(*it));
            }
        }
        sub_poly.reset();
        return new_nondom_res;
    }


    vector<RectangularBox> Polyscip::computeDisjointBoxes(list<RectangularBox>&& feasible_boxes) const {
        // delete redundant boxes
        auto current = begin(feasible_boxes);
        while (current != end(feasible_boxes)) {
            auto increment_current = true;
            auto it = begin(feasible_boxes);
            while (it != end(feasible_boxes)) {
                if (current != it) {
                    if (current->isSupersetOf(*it)) {
                        it = feasible_boxes.erase(it);
                        continue;
                    }
                    else if (current->isSubsetOf(*it)) {
                        current = feasible_boxes.erase(current);
                        increment_current = false;
                        break;
                    }
                }
                ++it;
            }
            if (increment_current)
                ++current;
        }
        // compute disjoint boxes
        auto disjoint_boxes = vector<RectangularBox>{};
        while (!feasible_boxes.empty()) {
            auto box_to_be_added = feasible_boxes.back();
            feasible_boxes.pop_back();

            auto current_boxes = vector<RectangularBox>{};
            for (const auto& elem : disjoint_boxes) {
                assert (!box_to_be_added.isSubsetOf(elem));
                if (box_to_be_added.isDisjointFrom(elem)) {
                    current_boxes.push_back(elem);
                }
                else if (box_to_be_added.isSupersetOf(elem)) {
                    continue;
                }
                else {
                    auto elem_disjoint = elem.getDisjointPartsFrom(cmd_line_args_.getDelta(), box_to_be_added);
                    std::move(begin(elem_disjoint), end(elem_disjoint), std::back_inserter(current_boxes));
                }
            }
            disjoint_boxes.clear();
            std::move(begin(current_boxes), end(current_boxes), std::back_inserter(disjoint_boxes));
            disjoint_boxes.push_back(std::move(box_to_be_added));
        }
        return disjoint_boxes;
    }


    list<RectangularBox> Polyscip::computeFeasibleBoxes(const map<ObjPair, vector<OutcomeType>> &proj_nd_outcomes,
                                                        const vector<vector<SCIP_VAR *>> &orig_vars,
                                                        const vector<vector<ValueType>> &orig_vals) {

        auto& nd_outcomes_01 = proj_nd_outcomes.at(ObjPair(0,1));
        assert (!nd_outcomes_01.empty());
        auto& nd_outcomes_02 = proj_nd_outcomes.at(ObjPair(0,2));
        assert (!nd_outcomes_02.empty());
        auto& nd_outcomes_12 = proj_nd_outcomes.at(ObjPair(1,2));
        assert (!nd_outcomes_12.empty());

        auto feasible_boxes = list<RectangularBox>{};
        for (const auto& nd_01 : nd_outcomes_01) {
            for (const auto& nd_02 : nd_outcomes_02) {
                for (const auto& nd_12 : nd_outcomes_12) {
                    auto box = RectangularBox({{max(nd_01[0], nd_02[0]), nd_12[0]},
                                               {max(nd_01[1], nd_12[1]), nd_02[1]},
                                               {max(nd_02[2], nd_12[2]), nd_01[2]}});
                    if (box.isFeasible(cmd_line_args_.getDelta())) {
                        feasible_boxes.push_back(box);
                    }
                }
            }
        }
        return feasible_boxes;
    }

    SCIP_CONS* Polyscip::createNewVarTransformCons(SCIP_VAR *new_var,
                                                   const vector<SCIP_VAR *> &orig_vars,
                                                   const vector<ValueType> &orig_vals,
                                                   const ValueType &rhs,
                                                   const ValueType &beta_i) {
        auto vars = vector<SCIP_VAR*>(begin(orig_vars), end(orig_vars));
        auto vals = vector<ValueType>(orig_vals.size(), 0.);
        std::transform(begin(orig_vals),
                       end(orig_vals),
                       begin(vals),
                       [beta_i](ValueType val){return -beta_i*val;});
        vars.push_back(new_var);
        vals.push_back(1.);

        SCIP_CONS* cons = nullptr;
        // add contraint new_var  - beta_i* vals \cdot vars >= - beta_i * ref_point[i]
        SCIPcreateConsBasicLinear(scip_,
                                  addressof(cons),
                                  "new_variable_transformation_constraint",
                                  global::narrow_cast<int>(vars.size()),
                                  vars.data(),
                                  vals.data(),
                                  -beta_i*rhs,
                                  SCIPinfinity(scip_));
        assert (cons != nullptr);
        return cons;
    }

    /** create constraint:
     *
     * @param orig_vars
     * @param orig_vals
     * @param lhs
     * @param rhs
     * @return
     */
    SCIP_CONS* Polyscip::createObjValCons(const vector<SCIP_VAR *>& vars,
                                          const vector<ValueType>& vals,
                                          const ValueType& lhs,
                                          const ValueType& rhs) {
        SCIP_CONS* cons = nullptr;
        std::remove_const<const vector<SCIP_VAR*>>::type non_const_vars(vars);
        std::remove_const<const vector<ValueType>>::type non_const_vals(vals);
        SCIPcreateConsBasicLinear(scip_,
                                  addressof(cons),
                                  "lhs <= c_i^T x <= rhs",
                                  global::narrow_cast<int>(vars.size()),
                                  non_const_vars.data(),
                                  non_const_vals.data(),
                                  lhs,
                                  rhs);
        assert (cons != nullptr);
        return cons;
    }

    SCIP_RETCODE Polyscip::computeNondomProjResult(SCIP_CONS *cons1,
                                                   SCIP_CONS *cons2,
                                                   ValueType rhs_cons1,
                                                   ValueType rhs_cons2,
                                                   size_t obj_1,
                                                   size_t obj_2,
                                                   ResultContainer &results) {

        SCIP_CALL( SCIPchgRhsLinear(scip_, cons1, rhs_cons1) );
        SCIP_CALL( SCIPchgRhsLinear(scip_, cons2, rhs_cons2) );
        // set new objective function
        auto intermed_obj = WeightType(no_objs_, 0.);
        intermed_obj.at(obj_1) = 1.;
        intermed_obj.at(obj_2) = 1.;
        SCIP_CALL( setWeightedObjective(intermed_obj) );

        // solve auxiliary problem
        SCIP_CALL( solve() );
        auto scip_status = SCIPgetStatus(scip_);
        if (scip_status == SCIP_STATUS_OPTIMAL) {
            if (no_objs_ > 2) {
                auto intermed_result = getOptimalResult();
                SCIP_CALL(SCIPchgLhsLinear(scip_, cons1, intermed_result.second[obj_1]));
                SCIP_CALL(SCIPchgRhsLinear(scip_, cons1, intermed_result.second[obj_1]));
                SCIP_CALL(SCIPchgLhsLinear(scip_, cons2, intermed_result.second[obj_2]));
                SCIP_CALL(SCIPchgRhsLinear(scip_, cons2, intermed_result.second[obj_2]));
                SCIP_CALL(setWeightedObjective(WeightType(no_objs_, 1.)));
                SCIP_CALL(solve());
                if (SCIPgetStatus(scip_) == SCIP_STATUS_TIMELIMIT) {
                    polyscip_status_ = PolyscipStatus::TimeLimitReached;
                }
                else {
                    assert (SCIPgetStatus(scip_) == SCIP_STATUS_OPTIMAL);
                }
            }

            auto nondom_result = getOptimalResult();
            results.push_back(std::move(nondom_result));
        }
        else if (scip_status == SCIP_STATUS_TIMELIMIT) {
            polyscip_status_ = PolyscipStatus::TimeLimitReached;
        }
        else {
            cout << "unexpected SCIP status in computeNondomProjResult: " +
                         std::to_string(SCIPgetStatus(scip_)) + "\n";
            polyscip_status_ = PolyscipStatus::Error;
        }

        // unset objective function
        SCIP_CALL( setWeightedObjective( WeightType(no_objs_, 0.)) );
        return SCIP_OKAY;
    }



    vector<OutcomeType> Polyscip::solveWeightedTchebycheff(const vector<vector<SCIP_VAR*>>& orig_vars,
                                                    const vector<vector<ValueType>>& orig_vals,
                                                    size_t obj_1,
                                                    size_t obj_2){

        assert (orig_vars.size() == orig_vals.size());
        assert (orig_vals.size() == no_objs_);
        assert (obj_1 < no_objs_ && obj_2 < no_objs_);

        // change objective values of existing variabless to zero
        setWeightedObjective(WeightType(no_objs_, 0.));

        // add new variable with objective value = 1 (for transformed Tchebycheff norm objective)
        SCIP_VAR* z = nullptr;
        SCIPcreateVarBasic(scip_,
                                  addressof(z),
                                  "z",
                                  -SCIPinfinity(scip_),
                                  SCIPinfinity(scip_),
                                  1,
                                  SCIP_VARTYPE_CONTINUOUS);
        assert (z != nullptr);
        SCIPaddVar(scip_, z);

        auto nondom_projs = NondomProjections(cmd_line_args_.getEpsilon(),
                                              bounded_,
                                              obj_1,
                                              obj_2);

        auto last_proj = nondom_projs.getLastProj();

        while (!nondom_projs.finished() && polyscip_status_ == PolyscipStatus::TwoProjPhase) {
            auto left_proj = nondom_projs.getLeftProj();
            auto right_proj = nondom_projs.getRightProj();

            assert (left_proj.getFirst() < right_proj.getFirst());
            assert (left_proj.getSecond() > last_proj.getSecond());

            // create constraint pred.first <= c_{objs.first} \cdot x <= succ.first
            auto obj_val_cons = vector<SCIP_CONS*>{};
            obj_val_cons.push_back( createObjValCons(orig_vars[obj_1],
                                                     orig_vals[obj_1],
                                                     left_proj.getFirst(),
                                                     right_proj.getFirst()));
            // create constraint optimal_val_objs.second <= c_{objs.second} \cdot x <= pred.second
            obj_val_cons.push_back( createObjValCons(orig_vars[obj_2],
                                                     orig_vals[obj_2],
                                                     last_proj.getSecond(),
                                                     left_proj.getSecond()));
            for (auto c : obj_val_cons) {
                SCIPaddCons(scip_, c);
            }

            auto ref_point = std::make_pair(left_proj.getFirst() - 1., last_proj.getSecond() - 1.);
            // set beta = (beta_1,beta_2) s.t. pred and succ are both on the norm rectangle defined by beta
            auto beta_1 = 1.0;
            auto beta_2 = (right_proj.getFirst() - ref_point.first) / (left_proj.getSecond() - ref_point.second);
            // create constraint with respect to beta_1
            auto var_trans_cons = vector<SCIP_CONS*>{};
            var_trans_cons.push_back( createNewVarTransformCons(z,
                                                                orig_vars[obj_1],
                                                                orig_vals[obj_1],
                                                                ref_point.first,
                                                                beta_1));
            // create constraint with respect to beta_2
            var_trans_cons.push_back(createNewVarTransformCons(z,
                                                               orig_vars[obj_2],
                                                               orig_vals[obj_2],
                                                               ref_point.second,
                                                               beta_2));
            for (auto c : var_trans_cons) {
                SCIPaddCons(scip_, c);
            }

            solve();
            std::unique_ptr<TwoDProj> new_proj(nullptr);
            auto scip_status = SCIPgetStatus(scip_);
            if (scip_status == SCIP_STATUS_OPTIMAL) {
                assert (SCIPisGE(scip_, SCIPgetPrimalbound(scip_), 0.));
                auto res = getOptimalResult();
                global::print(res.second, "computed outcome: ", "\n");
                new_proj = global::make_unique<TwoDProj>(res.second, obj_1, obj_2);
                assert (new_proj);
            }
            else if (scip_status == SCIP_STATUS_TIMELIMIT) {
                polyscip_status_ = PolyscipStatus::TimeLimitReached;
            }
            else if (scip_status == SCIP_STATUS_INFEASIBLE) {
                cout << "Numerical troubles between " << left_proj << " and " << right_proj << "\n";
                cout << "Continuing with next subproblem.\n";
                nondom_projs.update();
            }
            else {
                cout << "Unexpected SCIP status in solveWeightedTchebycheff: " +
                        std::to_string(SCIPgetStatus(scip_)) + "\n";
                polyscip_status_ = PolyscipStatus::Error;
            }

            // release and delete variable transformation constraints
            SCIPfreeTransform(scip_);
            for (auto c : var_trans_cons) {
                SCIPdelCons(scip_, c);
                SCIPreleaseCons(scip_, addressof(c));
            }

            if (new_proj) {
                if (nondom_projs.epsilonDominates(left_proj, *new_proj) ||
                    nondom_projs.epsilonDominates(right_proj, *new_proj)) {
                    nondom_projs.update();
                }
                else {
                    // temporarily delete variable 'z' from problem
                    SCIP_Bool var_deleted = FALSE;
                    SCIPdelVar(scip_, z, addressof(var_deleted));
                    assert (var_deleted);

                    computeNondomProjResult(obj_val_cons.front(), // constraint wrt obj_1
                                            obj_val_cons.back(), // constraint wrt obj2
                                            new_proj->getFirst(),
                                            new_proj->getSecond(),
                                            obj_1,
                                            obj_2,
                                            bounded_);
                    auto nd_proj = TwoDProj(bounded_.back().second, obj_1, obj_2);

                    nondom_projs.update(std::move(nd_proj), bounded_.back());

                    // add variable 'z' back to problem
                    SCIPaddVar(scip_, z);

                }
                new_proj.reset();
            }

            SCIPfreeTransform(scip_);
            for (auto c : obj_val_cons) {
                SCIPdelCons(scip_, c);
                SCIPreleaseCons(scip_, addressof(c));
            }

        }

        std::cout << nondom_projs << "\n";

        // clean up
        SCIP_Bool var_deleted = FALSE;
        SCIPdelVar(scip_, z, addressof(var_deleted));
        assert (var_deleted);
        SCIPreleaseVar(scip_, addressof(z));

        return nondom_projs.getNondomProjOutcomes();

    }

    Polyscip::PolyscipStatus Polyscip::getStatus() const {
        return polyscip_status_;
    }

    std::size_t Polyscip::numberOfBoundedResults() const {
        return bounded_.size();
    }

    std::size_t Polyscip::numberofUnboundedResults() const {
        return unbounded_.size();
    }

    SCIP_STATUS Polyscip::separateINFORUNBD(const WeightType& weight, bool with_presolving) {
        if (!with_presolving)
            SCIPsetPresolving(scip_, SCIP_PARAMSETTING_OFF, TRUE);
        auto zero_weight = WeightType(no_objs_, 0.);
        setWeightedObjective(zero_weight);
        solve(); // re-compute with zero objective
        if (!with_presolving)
            SCIPsetPresolving(scip_, SCIP_PARAMSETTING_DEFAULT, TRUE);
        auto status = SCIPgetStatus(scip_);
        setWeightedObjective(weight); // re-set to previous objective
        if (status == SCIP_STATUS_INFORUNBD) {
            if (with_presolving)
                separateINFORUNBD(weight, false);
            else {
                cout << "INFORUNBD Status for problem with zero objective and no presolving.\n";
                polyscip_status_ = PolyscipStatus::Error;
            }
        }
        else if (status == SCIP_STATUS_UNBOUNDED) {
            cout << "UNBOUNDED Status for problem with zero objective.\n";
            polyscip_status_ = PolyscipStatus::Error;
        }
        else if (status == SCIP_STATUS_OPTIMAL) { // previous problem was unbounded
            return SCIP_STATUS_UNBOUNDED;
        }
        return status;
    }


    SCIP_RETCODE Polyscip::handleNonOptNonUnbdStatus(SCIP_STATUS status) {
        assert (status != SCIP_STATUS_OPTIMAL && status != SCIP_STATUS_UNBOUNDED);
        if (status == SCIP_STATUS_TIMELIMIT) {
            polyscip_status_ = PolyscipStatus::TimeLimitReached;
        }
        else if (is_sub_prob_) {
            assert (status == SCIP_STATUS_INFORUNBD || status == SCIP_STATUS_INFEASIBLE);
            polyscip_status_ = PolyscipStatus::Finished;
        }
        else {
            polyscip_status_ = PolyscipStatus::Error;
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::handleUnboundedStatus(bool check_if_new_result) {
        if (!SCIPhasPrimalRay(scip_)) {
            SCIP_CALL( SCIPsetPresolving(scip_, SCIP_PARAMSETTING_OFF, TRUE) );
            if (SCIPisTransformed(scip_))
                SCIP_CALL( SCIPfreeTransform(scip_) );
            SCIP_CALL( solve() );
            SCIP_CALL( SCIPsetPresolving(scip_, SCIP_PARAMSETTING_DEFAULT, TRUE) );
            if (SCIPgetStatus(scip_) != SCIP_STATUS_UNBOUNDED || !SCIPhasPrimalRay(scip_)) {
                polyscip_status_ = PolyscipStatus::Error;
                return SCIP_OKAY;
            }
        }
        auto result = getResult(false);
        if (!check_if_new_result || outcomeIsNew(result.second, false)) {
            unbounded_.push_back(std::move(result));
        }
        else if (cmd_line_args_.beVerbose()) {
            global::print(result.second, "Outcome: [", "]");
            cout << "not added to results.\n";
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::handleOptimalStatus(const WeightType& weight,
                                               ValueType current_opt_val) {
        auto best_sol = SCIPgetBestSol(scip_);
        SCIP_SOL *finite_sol{nullptr};
        SCIP_Bool same_obj_val{FALSE};
        SCIP_CALL(SCIPcreateFiniteSolCopy(scip_, addressof(finite_sol), best_sol, addressof(same_obj_val)));

        if (!same_obj_val) {
            auto diff = std::fabs(SCIPgetSolOrigObj(scip_, best_sol) -
                                  SCIPgetSolOrigObj(scip_, finite_sol));
            if (diff > 1.0e-5) {
                cout << "absolute value difference after calling SCIPcreateFiniteSolCopy: " << diff << "\n";
                SCIP_CALL(SCIPfreeSol(scip_, addressof(finite_sol)));
                cout << "SCIPcreateFiniteSolCopy: unacceptable difference in objective values.\n";
                polyscip_status_ = PolyscipStatus::Error;
                return SCIP_OKAY;
            }
        }
        assert (finite_sol != nullptr);
        auto result = getResult(true, finite_sol);

        assert (weight.size() == result.second.size());
        auto weighted_outcome = std::inner_product(weight.cbegin(),
                                                   weight.cend(),
                                                   result.second.cbegin(),
                                                   0.);

        if (SCIPisLT(scip_, weighted_outcome, current_opt_val)) {
            bounded_.push_back(std::move(result));
        }

        SCIP_CALL(SCIPfreeSol(scip_, addressof(finite_sol)));
        return SCIP_OKAY;
    }

    Result Polyscip::getResult(bool outcome_is_bounded, SCIP_SOL *primal_sol) {
        SolType sol;
        auto outcome = OutcomeType(no_objs_,0.);
        auto no_vars = SCIPgetNOrigVars(scip_);
        auto vars = SCIPgetOrigVars(scip_);
        auto objs_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        for (auto i=0; i<no_vars; ++i) {
            auto var_sol_val = outcome_is_bounded ? SCIPgetSolVal(scip_, primal_sol, vars[i]) :
                               SCIPgetPrimalRayVal(scip_, vars[i]);

            if (!SCIPisZero(scip_, var_sol_val)) {
                sol.emplace_back(SCIPvarGetName(vars[i]), var_sol_val);
                auto var_obj_vals = OutcomeType(no_objs_, 0.);
                for (size_t index=0; index!=no_objs_; ++index) {
                    var_obj_vals[index] = objs_probdata->getObjVal(vars[i], index, var_sol_val);
                }
                std::transform(begin(outcome), end(outcome),
                               begin(var_obj_vals),
                               begin(outcome),
                               std::plus<ValueType>());

            }
        }
        return {sol, outcome};
    }



    Result Polyscip::getOptimalResult() {
        auto best_sol = SCIPgetBestSol(scip_);
        assert (best_sol != nullptr);
        SCIP_SOL *finite_sol{nullptr};
        SCIP_Bool same_obj_val{FALSE};
        auto retcode = SCIPcreateFiniteSolCopy(scip_, addressof(finite_sol), best_sol, addressof(same_obj_val));
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("SCIPcreateFiniteSolCopy: return code != SCIP_OKAY.\n");
        if (!same_obj_val) {
            auto diff = std::fabs(SCIPgetSolOrigObj(scip_, best_sol) -
                                  SCIPgetSolOrigObj(scip_, finite_sol));
            if (diff > 1.0e-5) {
                std::cerr << "absolute value difference after calling SCIPcreateFiniteSolCopy: " << diff << "\n";
                SCIPfreeSol(scip_, addressof(finite_sol));
                throw std::runtime_error("SCIPcreateFiniteSolCopy: unacceptable difference in objective values.");
            }
        }
        assert (finite_sol != nullptr);
        auto new_result = getResult(true, finite_sol);
        SCIPfreeSol(scip_, addressof(finite_sol));
        return new_result;
    }


    bool Polyscip::outcomeIsNew(const OutcomeType& outcome, bool outcome_is_bounded) const {
        auto beg_it = outcome_is_bounded ? begin(bounded_) : begin(unbounded_);
        auto end_it = outcome_is_bounded ? end(bounded_) : end(unbounded_);
        return std::find_if(beg_it, end_it, [&outcome](const Result& res){return outcome == res.second;}) == end_it;
    }

    bool Polyscip::outcomeIsNew(const OutcomeType& outcome,
                                ResultContainer::const_iterator beg,
                                ResultContainer::const_iterator last) const {
        auto eps = cmd_line_args_.getEpsilon();
        return std::none_of(beg, last, [eps, &outcome](const Result& res)
        {
            return outcomesCoincide(outcome, res.second, eps);
        });
    }

    bool Polyscip::outcomesCoincide(const OutcomeType& a, const OutcomeType& b, double epsilon) {
        assert (a.size() == b.size());
        return std::equal(begin(a), end(a), begin(b),
                          [epsilon](ValueType v, ValueType w)
                          {
                              return std::fabs(v-w) < epsilon;
                          });
    }

    SCIP_RETCODE Polyscip::solve() {
        if (cmd_line_args_.hasTimeLimit()) { // set SCIP timelimit
            auto remaining_time = std::max(cmd_line_args_.getTimeLimit() -
                                           SCIPgetClockTime(scip_, clock_total_), 0.);
            SCIP_CALL(SCIPsetRealParam(scip_, "limits/time", remaining_time));
        }
        SCIP_CALL( SCIPsolve(scip_) );    // actual SCIP solver call
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::setWeightedObjective(const WeightType& weight){
        if (SCIPisTransformed(scip_))
            SCIP_CALL( SCIPfreeTransform(scip_) );
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        assert (obj_probdata != nullptr);
        auto vars = SCIPgetOrigVars(scip_);
        auto no_vars = SCIPgetNOrigVars(scip_);
        if (weight == WeightType(weight.size(), 0.)) { // weight coincides with all zeros vector
            for (auto i = 0; i < no_vars; ++i) {
                SCIP_CALL(SCIPchgVarObj(scip_, vars[i], 0.));
            }
        }
        else { // weight != all zeros vector
            for (auto i = 0; i < no_vars; ++i) {
                auto val = obj_probdata->getWeightedObjVal(vars[i], weight);
                SCIP_CALL(SCIPchgVarObj(scip_, vars[i], val));
            }
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeWeightSpaceResults() {
        polyscip_status_ = PolyscipStatus::WeightSpacePhase;
        auto v_rep = DDMethod(scip_, no_objs_, bounded_, unbounded_);
        v_rep.computeVRep_Var1();
        weight_space_poly_ = global::make_unique<WeightSpacePolyhedron>(scip_,
                                                                        no_objs_,
                                                                        v_rep.moveVRep(),
                                                                        v_rep.moveHRep());
        assert (weight_space_poly_->hasValidSkeleton(no_objs_));

        while (weight_space_poly_->hasUntestedWeight() && polyscip_status_ == PolyscipStatus::WeightSpacePhase) {
            auto untested_weight = weight_space_poly_->getUntestedWeight();
            SCIP_CALL(setWeightedObjective(untested_weight));
            SCIP_CALL(solve());
            auto scip_status = SCIPgetStatus(scip_);
            if (scip_status == SCIP_STATUS_INFORUNBD && !is_sub_prob_) {
                scip_status = separateINFORUNBD(untested_weight);
            }
            if (scip_status == SCIP_STATUS_OPTIMAL) {
                //if (SCIPisLT(scip_, SCIPgetPrimalbound(scip_), weight_space_poly_->getUntestedVertexWOV(untested_weight))) {
                auto supported_size_before = bounded_.size();
                SCIP_CALL(handleOptimalStatus(untested_weight,
                                              weight_space_poly_->getUntestedVertexWOV(
                                                      untested_weight))); //might add bounded result to bounded_
                if (supported_size_before < bounded_.size()) {
                    weight_space_poly_->incorporateNewOutcome(scip_,
                                                              untested_weight,
                                                              bounded_.back().second); // was added by handleOptimalStatus()
                }
                else {
                    weight_space_poly_->incorporateKnownOutcome(untested_weight);
                }
            }
            else if (scip_status == SCIP_STATUS_UNBOUNDED) {
                SCIP_CALL(handleUnboundedStatus()); //adds unbounded result to unbounded_
                weight_space_poly_->incorporateNewOutcome(scip_,
                                                          untested_weight,
                                                          unbounded_.back().second, // was added by handleUnboundedStatus()
                                                          true);
            }
            else {
                SCIP_CALL(handleNonOptNonUnbdStatus(scip_status));
            }
        }
        if (polyscip_status_ == PolyscipStatus::WeightSpacePhase) {
            polyscip_status_ = PolyscipStatus::Finished;
        }
        return SCIP_OKAY;
    }

    void Polyscip::printResults(ostream &os) const {
        for (const auto& result : bounded_) {
            if (cmd_line_args_.outputOutcomes())
                outputOutcome(result.second, os);
            if (cmd_line_args_.outputSols())
                printSol(result.first, os);
            os << "\n";
        }
        for (const auto& result : unbounded_) {
            if (cmd_line_args_.outputOutcomes())
                outputOutcome(result.second, os, "Ray = ");
            if (cmd_line_args_.outputSols())
                printSol(result.first, os);
            os << "\n";
        }
    }

    void Polyscip::printSol(const SolType& sol, ostream& os) const {
        for (const auto& elem : sol)
            os << elem.first << "=" << elem.second << " ";
    }

    void Polyscip::outputOutcome(const OutcomeType &outcome, std::ostream &os, const std::string desc) const {
        if (obj_sense_ == SCIP_OBJSENSE_MAXIMIZE) {
            global::print(outcome, desc + "[ ", "] ", os, true);
        }
        else {
            global::print(outcome, desc + "[ ", "] ", os);
        }
    }

    bool Polyscip::filenameIsOkay(const string& filename) {
        std::ifstream file(filename.c_str());
        return file.good();
    }

    void Polyscip::printObjective(size_t obj_no,
                                  const std::vector<int>& nonzero_indices,
                                  const std::vector<SCIP_Real>& nonzero_vals,
                                  ostream& os) const {
        assert (!nonzero_indices.empty());
        auto size = nonzero_indices.size();
        assert (size == nonzero_vals.size());
        auto obj = vector<SCIP_Real>(global::narrow_cast<size_t>(SCIPgetNOrigVars(scip_)), 0);
        for (size_t i=0; i<size; ++i)
            obj[nonzero_indices[i]] = nonzero_vals[i];
        global::print(obj, std::to_string(obj_no) + ". obj: [", "]", os);
        os << "\n";
    }

    bool Polyscip::objIsRedundant(const vector<int>& begin_nonzeros,
                                  const vector< vector<int> >& obj_to_nonzero_indices,
                                  const vector< vector<SCIP_Real> >& obj_to_nonzero_values,
                                  size_t checked_obj) const {
        bool is_redundant = false;
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));

        assert (obj_probdata != nullptr);
        assert (checked_obj >= 1 && checked_obj < obj_probdata->getNoObjs());

        SCIP_LPI* lpi;
        auto retcode = SCIPlpiCreate(addressof(lpi), nullptr, "check objective redundancy", SCIP_OBJSEN_MINIMIZE);
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiCreate\n.");

        auto no_cols = global::narrow_cast<int>(checked_obj);
        auto obj = vector<SCIP_Real>(checked_obj, 1.);
        auto lb = vector<SCIP_Real>(checked_obj, 0.);
        auto ub = vector<SCIP_Real>(checked_obj, SCIPlpiInfinity(lpi));
        auto no_nonzero = begin_nonzeros.at(checked_obj);

        auto beg = vector<int>(begin(begin_nonzeros), begin(begin_nonzeros)+checked_obj);
        auto ind = vector<int>{};
        ind.reserve(global::narrow_cast<size_t>(no_nonzero));
        auto val = vector<SCIP_Real>{};
        val.reserve(global::narrow_cast<size_t>(no_nonzero));
        for (size_t i=0; i<checked_obj; ++i) {
            ind.insert(end(ind), begin(obj_to_nonzero_indices[i]), end(obj_to_nonzero_indices[i]));
            val.insert(end(val), begin(obj_to_nonzero_values[i]), end(obj_to_nonzero_values[i]));
        }

        auto no_rows = SCIPgetNOrigVars(scip_);
        auto vars = SCIPgetOrigVars(scip_);
        auto lhs = vector<SCIP_Real>(global::narrow_cast<size_t>(no_rows), 0.);
        for (auto i=0; i<no_rows; ++i)
            lhs[i] = obj_probdata->getObjCoeff(vars[i], checked_obj);
        auto rhs = vector<SCIP_Real>(lhs);

        retcode =  SCIPlpiLoadColLP(lpi,
                                    SCIP_OBJSEN_MINIMIZE,
                                    no_cols,
                                    obj.data(),
                                    lb.data(),
                                    ub.data(),
                                    nullptr,
                                    no_rows,
                                    lhs.data(),
                                    rhs.data(),
                                    nullptr,
                                    no_nonzero,
                                    beg.data(),
                                    ind.data(),
                                    val.data());

        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiLoadColLP\n");

        //SCIPlpiWriteLP(lpi, "redundancy_check.lp");

        retcode = SCIPlpiSolvePrimal(lpi);
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiSolvePrimal\n");

        if (SCIPlpiIsPrimalFeasible(lpi)) {
            is_redundant = true;
        }
        else {
            assert (SCIPlpiIsPrimalInfeasible(lpi));
        }

        retcode = SCIPlpiFree(addressof(lpi));
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiFree\n");

        return is_redundant;
    }

    SCIP_RETCODE Polyscip::readProblem() {
        if (polyscip_status_ != PolyscipStatus::Unsolved) {
            return  SCIP_OKAY;
        }
        auto filename = cmd_line_args_.getProblemFile();
        SCIP_CALL( SCIPreadProb(scip_, filename.c_str(), "mop") );
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        assert (obj_probdata != nullptr);
        no_objs_ = obj_probdata->getNoObjs();

        if (cmd_line_args_.onlyExtremal() || SCIPgetNOrigContVars(scip_) == SCIPgetNOrigVars(scip_)) {
            only_weight_space_phase_ = true;
        }

        auto vars = SCIPgetOrigVars(scip_);
        auto begin_nonzeros = vector<int>(no_objs_, 0);
        for (size_t i = 0; i < no_objs_ - 1; ++i)
            begin_nonzeros[i + 1] = global::narrow_cast<int>(
                    begin_nonzeros[i] + obj_probdata->getNumberNonzeroCoeffs(i));

        auto obj_to_nonzero_inds = vector<vector<int> >{};
        auto obj_to_nonzero_vals = vector<vector<SCIP_Real> >{};
        for (size_t obj_ind = 0; obj_ind < no_objs_; ++obj_ind) {
            auto nonzero_vars = obj_probdata->getNonZeroCoeffVars(obj_ind);
            auto size = nonzero_vars.size();
            if (size == 0) {
                cout << obj_ind << ". objective is zero objective!\n";
                polyscip_status_ = PolyscipStatus::Error;
                return SCIP_OKAY;
            }
            auto nonzero_inds = vector<int>(size, 0);
            std::transform(begin(nonzero_vars),
                           end(nonzero_vars),
                           begin(nonzero_inds),
                           [](SCIP_VAR *var) { return SCIPvarGetProbindex(var); });
            std::sort(begin(nonzero_inds), end(nonzero_inds));

            auto nonzero_vals = vector<SCIP_Real>(size, 0.);
            std::transform(begin(nonzero_inds),
                           end(nonzero_inds),
                           begin(nonzero_vals),
                           [&](int var_ind) { return obj_probdata->getObjCoeff(vars[var_ind], obj_ind); });


            if (cmd_line_args_.beVerbose())
                printObjective(obj_ind, nonzero_inds, nonzero_vals);

            obj_to_nonzero_inds.push_back(std::move(nonzero_inds));  // nonzero_inds invalid from now on
            obj_to_nonzero_vals.push_back(std::move(nonzero_vals));  // nonzero_vals invalid from now on

            if (obj_ind > 0 && objIsRedundant(begin_nonzeros, // first objective is always non-redundant
                               obj_to_nonzero_inds,
                               obj_to_nonzero_vals,
                               obj_ind)) {
                cout << obj_ind << ". objective is non-negative linear combination of previous objectives!\n";
                cout << "Only problems with non-redundant objectives will be solved.\n";
                polyscip_status_ = PolyscipStatus::Error;
                return SCIP_OKAY;
            }
        }

        if (SCIPgetObjsense(scip_) == SCIP_OBJSENSE_MAXIMIZE) {
            obj_sense_ = SCIP_OBJSENSE_MAXIMIZE;
            // internally we treat problem as min problem and negate objective coefficients
            SCIPsetObjsense(scip_, SCIP_OBJSENSE_MINIMIZE);
            obj_probdata->negateAllCoeffs();
        }
        if (cmd_line_args_.beVerbose()) {
            cout << "Objective sense: ";
            if (obj_sense_ == SCIP_OBJSENSE_MAXIMIZE)
                cout << "MAXIMIZE\n";
            else
                cout << "MINIMIZE\n";
            cout << "Number of objectives: " << no_objs_ << "\n";
        }
        polyscip_status_ = PolyscipStatus::ProblemRead;
        return SCIP_OKAY;
    }

    void Polyscip::writeResultsToFile() const {
        auto prob_file = cmd_line_args_.getProblemFile();
        size_t prefix = prob_file.find_last_of("/"), //separate path/ and filename.mop
                suffix = prob_file.find_last_of("."),      //separate filename and .mop
                start_ind = (prefix == string::npos) ? 0 : prefix + 1,
                end_ind = (suffix != string::npos) ? suffix : string::npos;
        string file_name = "solutions_" +
                           prob_file.substr(start_ind, end_ind - start_ind) + ".txt";
        auto write_path = cmd_line_args_.getWritePath();
        if (write_path.back() != '/')
            write_path.push_back('/');
        std::ofstream solfs(write_path + file_name);
        if (solfs.is_open()) {
            printResults(solfs);
            solfs.close();
            cout << "#Solution file " << file_name
            << " written to: " << write_path << "\n";
        }
        else
            std::cerr << "ERROR writing solution file\n.";
    }

    bool Polyscip::isDominatedOrEqual(ResultContainer::const_iterator it,
                                      ResultContainer::const_iterator beg_it,
                                      ResultContainer::const_iterator end_it) const {
        for (auto curr = beg_it; curr != end_it; ++curr) {
            if (it == curr)
                continue;
            else if (std::equal(begin(curr->second),
                                end(curr->second),
                                begin(it->second),
                                std::less_equal<ValueType>())) {
                return true;
            }
        }
        return false;
    }


    bool Polyscip::dominatedPointsFound() const {
        for (auto cur=begin(bounded_); cur!=end(bounded_); ++cur) {
            if (isDominatedOrEqual(cur, begin(bounded_), end(bounded_)))
                return true;
        }
        return false;
    }


}
