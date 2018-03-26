/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   xternal_ringpacking.c
 * @brief  The ringpacking example of SCIP
 * @author Benjamin Mueller
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

/**@page RINGPACKING_MAIN Overview
 * @author Benjamin Mueller
 *
 * This example contains a branch-and-price approach for the ringpacking problem, also known as recursive circle packing
 * problem, which is realized with the framework \SCIP. Therefore, the following plugins are implemented:
 *
 * - a \ref reader_rpa.c "problem reader" which parses the problem out of a file and creates the corresponding problem within \SCIP
 * - a \ref probdata_rpa.c "(global) problem data structure" which contains all necessary information
 * - a \ref pricer_rpa.c "pricer" which generates new variables/columns during the search
 * - a \ref cons_rpa.c "constraint handler" which stores information about which patterns have been verified
 * - a \ref pattern.c "variable data structure" which provides fundamental functions for handling patterns
 *
 * In the following we introduce the problem, explain the use of the reader plugin and pricer plugin.
 *
 * -# \ref RINGPACKING_PROBLEM "Problem description"
 * -# \ref RINGPACKING_READER "Parsing the input format and creating the problem"
 * -# \ref RINGPACKING_PROBLEMDATA "Main problem data"
 * -# \ref RINGPACKING_PRICER "Pricing new variables"
 * -# \ref RINGPACKING_MAKEFILE "The Makefile"
 *
 */

/**@page RINGPACKING_PROBLEM Problem description
 *
 * The objective of the ringpacking problem is to select a minimum number of rectangles of the same size such that a
 * given set of rings can be packed into these rectangles in a non-overlapping way. A ring is characterized by an
 * internal and an external radius. Rings can be put recursively into larger ones or directly into a rectangle. The
 * following picture gives two examples of such packings:
 *
 * TODO: insert example picture here
 *
 * This problem can be formulated as a noncovex MINLP. We use the formulation presented in [TODO:insert Bennys Paper here]
 * which  is based on a Dantzig-Wolfe decomposition that can be solved by a branch-and-price algorithm. The first step
 * is a reformulation, which is similar to the classical reformulation for the Cutting Stock Problem, however, featuring
 * nonlinear and nonconvex sub-problems. This formulation breaks the symmetry between equivalent rectangles. As a second
 * step, we combine this reformulation with an enumeration scheme for patterns that are characterized by rings packed
 * inside other rings. Such patterns only allow for a one-level recursion and break the symmetry between rings with the
 * same internal and external radius in each rectangle.
 *
 * More precisely, we introduce an integral variable \f$z_{P}\f$ for each rectangular pattern \f$P\f$ and an integral
 * variable \f$z_{C}\f$ for each circular pattern \f$C\f$. A vector \f$P \in \Z_{+}^\mathcal{T}\f$, where \f$T\f$ is the
 * total number of ringtypes, is a rectangular pattern if and only if \f$P_t\f$ many circles with radius \f$\rext_t\f$
 * for each \f$t \in \mathcal{T}\f$ can be packed together into a rectangle. Similarly, a tuple
 * \f$(t,P)\in \mathcal{T} \times \Z_{+}^\ntypes\f$ is a circular pattern if it is possible to pack \f$P_1\f$ many
 * circles of type \f$1\f$, \f$P_2\f$ many circles of type \f$2\f$, \f$\ldots\f$, \f$P_T\f$ many circles of type \f$T\f$
 * into a larger ring of type \f$t\f$. Let \f$\mathcal{RP}\f$ and \f$\mathcal{CP}\f$ be the set of all rectangular or
 * circular patterns, respectively, and let \f$D_t\f$ denote the demand of ringtype \f$t\f$.
 *
 * Then the problem can be formulated as follows:
 *
 * \f[
 *  \begin{subequations}
 *  \begin{align}
 *   && \min \sum_{P \in \mathcal{RP}} z_P \quad\,\\
 *   && \text{s.t.} \sum_{C = (t,P) \in \mathcal{CP}} z_C &\ge \demand_t && \text{ for all } t \in \mathcal{T} \\
 *   && \sum_{C = (t,P) \in \mathcal{CP}} z_C &\le \sum_{P \in \mathcal{RP}} P_t \cdot z_P + \sum_{C = (t',P) \in \mathcal{CP}} P_t \cdot z_C && \text{ for all } t \in \mathcal{T} \\
 *   && z_C & \in \Z_{+} && \text{ for all } C \in \mathcal{CP} \\
 *   && z_P & \in \Z_{+} && \text{ for all } P \in \mathcal{RP}
 *  \end{align}
 *  \end{subequations}
 * \f]
 *
 * The objective minimizes the total number of used rectangles. The first constraint ensures that the demand for each
 * ring type is satisfied. The recursive decisions how to place rings into each other are implicitly modeled by the
 * second type of constraints. Each selection of a pattern allows us to choose \f$P_t\f$ circular patterns of the type
 * \f$(t',P)\f$. Note that at least one rectangular pattern needs to be selected before circular patterns can be packed.
 * This is true because the largest ring only fits into a rectangular pattern.
 *
 * Since \f$\mathcal{RP}\f$ can be of exponential size, we are using a column generation approach to solve this
 * problem. We initialize the (master) problem with a set of variables representing a selection of rectangular patterns
 * that are easy to verify. Now, we have to iteratively search for variables representing "better" patterns, i.e.,
 * a pattern which reduces the overall cost. Let \f$\lambda\f$ be the non-negative vector of dual multipliers for
 * the recursive constraints after solving the LP relaxation of the restricted master problem for the
 * current set of rectangular patterns. To compute a rectangular pattern with negative reduced cost we solve
 *
 * \f[
 *  \begin{equation}
 *    \min_{P \in \mathcal{RP}} \left\{1 - \sum_{t \in \mathcal{T}} \lambda_t P_t\right\},
 *  \end{equation}
 * \f]
 *
 * This problem is NP-hard and can be difficult to solve, but even if it cannot be solved to optimality within the
 * given time limit, a dual bound of the LP relaxation can be turned into a valid dual bound of the complete master
 * problem by using a theorem by Farley [TODO:insert ref here].
 *
 */



/**@page RINGPACKING_MAKEFILE The Makefile
 *
 * The Makefile is based on the main \SCIP Makefile. This means that all compiling options which are
 * available for \SCIP are also available for the RINGPACKING project. Below, you find a list
 * of the most important compiling flags, the values they can take, and a short description. The
 * values in bold face are the default values.
 *
 * - <code>LPS={clp | cpx | none | <b>spx</b>}</code>
 *   <br>
 *   Defines the linear program solver to use:
 *   - <code>clp</code> use COIN-OR CLP
 *   - <code>cpx</code> use IBM CPLEX
 *   - <code>none</code> no LP solver
 *   - <code><b>spx</b></code> use SoPlex
 *
 * - <code>OPT={dbg | <b>opt</b>}</code>
 *   <br>
 *   Defines if the projects gets compiled in debug (<code>dbg</code>) mode or
 *   optimized (<code><b>opt</b></code>) mode. In the debug mode all assertions are checked.
 *
 * - <code>ZIMPL={false | <b>true</b>}</code>
 *   <br>
 *   Defines if the modeling language ZIMPL should be linked to binary or not.
 *
 * In the following we explain the all <b>Makefile targets</b>.
 *
 * - <b>lint</b>
 *   <br>
 *   Statically checks the code for uninitialized variables and many other possible problems,
 *   which even do not lead to compiler errors. For this,
 *   the external tool flexelint is needed. The call produces the file <code>lint.out</code>
 *   which contains all the detected warnings. From the experience when developing \SCIP, we strongly
 *   recommend to use such a code checker. It is always a surprising the stuff such tools detect.
 *   <br>
 *
 * - <b>clean</b>
 *   <br>
 *   Remove all objective files, libraries, and binaries.
 *   <br>
 *
 * - <b>test</b>
 *   <br>
 *   Starts an automated test run based on the SCIP test runs (see \ref TEST "How to run automated tests with SCIP").
 *   <br>
 *
 * - <b>tags</b>
 *   <br>
 *   Generates tags which can be used in the editor <b>emacs</b> and <b>xemacs</b>.
 *   <br>
 *
 * - <b>depend</b>
 *   <br>
 *   Generates the dependencies for the compiling process.
 */
