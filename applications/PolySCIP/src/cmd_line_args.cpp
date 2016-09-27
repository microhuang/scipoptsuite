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

#include "cmd_line_args.h"

#include <string>

#include "PolySCIPConfig.h" // defines EXECUTABLE_NAME, POLYSCIP_VERSION_{MAJOR,MINOR}
#include "tclap/CmdLine.h"

using std::string;
using TCLAP::CmdLine;
using TCLAP::SwitchArg;
using TCLAP::ValueArg;
using TCLAP::UnlabeledValueArg;

namespace polyscip {

    CmdLineArgs::CmdLineArgs(int argc, const char *const *argv)
            : executable_name_(EXECUTABLE_NAME)
    {
        version_no_ = std::to_string(POLYSCIP_VERSION_MAJOR) + string(".") + std::to_string(POLYSCIP_VERSION_MINOR);
        CmdLine cmd(executable_name_,' ', version_no_);
        cmd.setExceptionHandling(false); // set internal exception handling
        SwitchArg only_extremal_arg("x", "extremal", "compute only extremal supported non-dominated results", false);
        cmd.add(only_extremal_arg);
        SwitchArg be_verbose_arg("v", "verbose", "verbose PolySCIP cmd line output ", false);
        cmd.add(be_verbose_arg);
        SwitchArg write_results_arg("w", "writeResults", "write results to file; default path is ./", false);
        cmd.add(write_results_arg);
        SwitchArg output_sols_arg("s", "noSolutions", "switching output of solutions off", true);
        cmd.add(output_sols_arg);
        SwitchArg output_outcomes_arg("o", "noOutcomes", "switching output of outcomes off", true);
        cmd.add(output_outcomes_arg);
        ValueArg<TimeLimitType> time_limit_arg("t", "timeLimit",
                                               "time limit in seconds for total computation time",
                                               false, kTimeLimitInf, "seconds");
        cmd.add(time_limit_arg);
        ValueArg<double> epsilon_arg("e", "Epsilon", "epsilon used in computation of unsupported points; default value: 1e-5",
                                     false, 1e-5, "double");
        cmd.add(epsilon_arg);
        ValueArg<string> write_sols_path_arg("W", "writeSolsPath",
                                             "PATH for -w",
                                             false, "./", "PATH");
        cmd.add(write_sols_path_arg);
        ValueArg<string> param_file_arg("p", "params", "parameter settings file for SCIP",
                                        false, "", "paramFile.set");
        cmd.add(param_file_arg);
        UnlabeledValueArg<string> prob_file_arg("probFile", "problem file in MOP format",
                                                true, "", "problemFile.mop");
        cmd.add(prob_file_arg);
        cmd.parse(argc, argv);

        be_verbose_ = be_verbose_arg.getValue();
        only_extremal_ = only_extremal_arg.getValue();
        write_results_ = write_results_arg.getValue();
        output_solutions_ = output_sols_arg.getValue();
        output_outcomes_ = output_outcomes_arg.getValue();
        time_limit_ = time_limit_arg.getValue();
        epsilon_ = epsilon_arg.getValue();
        write_results_path_ = write_sols_path_arg.getValue();
        param_file_ = param_file_arg.getValue();
        prob_file_ = prob_file_arg.getValue();
    }

}