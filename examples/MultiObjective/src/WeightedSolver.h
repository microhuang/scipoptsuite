/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2014 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   WeightedSolver.h
 * @brief  Class providing all methods for using the algorithm
 * @author Timo Strunk
 * 
 * @desc   Abstract superclass for a multi objective solver using weighted objective functions.
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef WEIGHTED_SOLVER
#define WEIGHTED_SOLVER

#include <vector>
#include <string>
#include <map>

#include "scip/scip.h"

/** generic weight based solver */
class WeightedSolver
{
 public:
   /** standard constructor */
   WeightedSolver(
      bool               verbose,            /**< true if scip output should be displayed */
      SCIP_Real          timelimit,          /**< maximum allowed time in seconds */
      int                solstore            /**< number of solutions stored in SCIP */
      );

   /** SCIP style constructor */
   WeightedSolver(
      const char*        paramfilename       /**< name of file with SCIP parameters */
      );

   /** destructor */
   virtual ~WeightedSolver();

   /** reads problem data from file */
   SCIP_RETCODE readProblem(
      const char*        filename            /**< name of instance file */
      );

   /** returns true if there is a weight left to check */
   virtual bool hasNext() const = 0;

   /** loads next weight into solver */
   virtual SCIP_RETCODE next() = 0;

   /** solves the current weighted problem */
   virtual SCIP_RETCODE solve() = 0;

   /** returns true if the last weighted run found a new pareto optimum */
   virtual bool foundNewOptimum() const;

   /** gets the last weight loaded into the solver */
   virtual const std::vector<SCIP_Real>* getWeight() const;

   /** gets cost vector of last found pareto optimum */
   virtual const std::vector<SCIP_Real>* getCost() const;

   /** returns the last found pareto optimal solution */
   virtual SCIP_SOL* getSolution() const;

   /** writes the last solution to a file */
   SCIP_RETCODE writeSolution();

   /** returns the name of the file containing the last written solution */
   std::string getSolutionFileName() const;

   /** returns the number of branch and bound nodes in the last weighted run */
   virtual SCIP_Longint getNNodesLastRun() const;

   /** returns the number of LP iterations used in the last run */
   virtual SCIP_Longint getNLPIterationsLastRun() const;

   /** returns the time needed for the last iteration in seconds */
   SCIP_Real getDurationLastRun() const;

   /** returns the number of objective functions */
   int getNObjs() const;

   /** returns the SCIP problem status */
   virtual SCIP_Status getStatus() const;

   /** returns the number of weighted runs so far */
   int getNRuns() const;

   /** returns the number of found pareto optima so far */
   virtual int getNSolutions() const;

   /** get total time for algorithm */
   virtual SCIP_Real getTotalDuration() const=0;

   /** get number of new vertices in the 1-skeleton added in last step*/
   virtual int getNNewVertices() const=0;

   /** get number of vertices in the 1-skeleton processed in last step*/
   virtual int getNProcessedVertices() const=0;

   /** delete non extremal solutions */
   SCIP_RETCODE enforceExtremality();

   /** return verblevel parameter set in SCIP */
   int getVerbosity() const;

 protected:
   SCIP*                 scip_;                   /**< SCIP solver */
   SCIP_Real             timelimit_;              /**< maximal time for entire solve in seconds */
   int                   verbosity_;

   bool                  found_new_optimum_;      /**< true if the last SCIP run found a new optimum */
   SCIP_SOL*             solution_;               /**< last found solution */
   SCIP_Longint          nnodes_last_run_;        /**< number of branch and bound nodes used in last run */
   SCIP_Longint          niterations_last_run_;   /**< number of lp iterations in last run */
   SCIP_Real             duration_last_run_;      /**< duration of last run in seconds */
   SCIP_Status           status_;                 /**< SCIP solver status */
   int                   nruns_;                  /**< number of weighted runs */

   const std::vector<SCIP_Real>*                  weight_;            /**< weight used in last run */
   const std::vector<SCIP_Real>*                  cost_vector_;       /**< cost vector of last found solution */
   std::vector< const std::vector< SCIP_Real>* >  nondom_points_;     /**< list of found non dominated points*/

 private:
   std::string           filename_;               /**< name of problem file */
   std::string           outfilestump_;           /**< beginning of outfile names */
   std::string           solution_file_name_;     /**< name of last written solution file */

   std::map<const std::vector<SCIP_Real>*, std::string> filename_by_point_;     /**< map linking cost vectors to
										    corresponding solution file names */
};

#endif
