#!/bin/bash -ex
# the -x is for writing each command to standard error (preceded by a '+') before it is executed.
# other flags for debugging: -Canvu, please also consult 'man sh'.

# NOTES:
#  - We use associative arrays, this requires bash4.

###################
### Description ###
###################

# This script is used by cijenkins.zib.de.
# Depending on the day of the week this script will start different testruns on the cluster.

# Usage: from scip root execute
#        GITBRANCH=master ./debug_runs.sh

# Arguments | defaultvalue                             | possibilities
# ----------|------------------------------------------|--------------
# GITBRANCH | master                                   | master, bugfix

echo "This is debug_runs.sh running."

# arguments and defaults
# Find out what day of week it is: mon-1 .. sun-7
: ${DAY_OF_WEEK:=$(date +%u)}

######################################
### evaluate commandline arguments ###
######################################

# set default arguments
# If no branch is given, try to guess the branch based on the current directory
if [ "${GITBRANCH}" == "" ]; then
  # GIT_BRANCH is a jenkins variable, if not present, try to get it from the git repository. The second thing is not robust because there may be more branches that this HEAD is present in.
  GITBRANCH=`echo ${GIT_BRANCH} | cut -d / -f 2`
  if [ "${GITBRANCH}" == "" ]; then
      GITBRANCH=`git show -s --pretty=%D | cut -s -d , -f 2 | cut -d / -f 2`
  fi
fi

if [ "${GITBRANCH}" != "master" ]; then
  if [ "${GITBRANCH}" != "consexpr" ]; then
    if [[ ${GITBRANCH} =~ "bugfix" ]]; then
      GITBRANCH=bugfix
    else
      export FAILREASON="Branch is neither 'master', 'bugfix' nor 'consexpr'. Something is wrong. Exiting."
      echo ${FAILREASON}
      exit 1
    fi
  fi
fi

export FULLGITHASH=$(git show -s --pretty=%H)
export GITBRANCH
export MODE=debug

# This soplex there is installed on pushes to soplex by the jenkins job SOPLEX_install_${GITBRANCH}.
# We have to export these variables to make them available to cmake.
# Scripts will also use nonexported variables correctly.
if [ "${GITBRANCH}" == "consexpr" ]; then
  export SOPLEX_DIR=/nfs/OPTI/adm_timo/soplex_master_Debug/
  export PAPILO_DIR=/nfs/OPTI/adm_timo/performance_papilo_master/
else
  export SOPLEX_DIR=/nfs/OPTI/adm_timo/soplex_${GITBRANCH}_Debug/
  export PAPILO_DIR=/nfs/OPTI/adm_timo/performance_papilo_${GITBRANCH}/
fi

export CRITERION_DIR=/nfs/optimi/usr/sw/criterion
export CPLEX_DIR=/nfs/optimi/usr/sw/cplex
export BLISS_DIR=/nfs/OPTI/bzfgleix/software/bliss-0.73p-Ubuntu18.04
#export IPOPT_DIR=/nfs/optimi/usr/sw/ipopt-static
export IPOPT_DIR=/nfs/optimi/usr/sw/Ipopt-3.12.13-static-build
export ZIMPL_DIR=/nfs/OPTI/jenkins/workspace/ZIMPL_monthly/build-gnu-Release/

# create all required directories
mkdir -p settings

# SAP settings

SAPSETTINGS=sap-600-pure-diff
if [ "${GITBRANCH}" != "bugfix" ]; then
    SAPSETTINGS=sap-next-release-pure-diff
fi

# symlink to SAP settings for the next release settings
ln -fs ~/sap-next-release-pure-diff.set settings/.
ln -fs ~/sap-600-pure-diff.set settings/.

####################################
### jobs configuration variables ###
####################################
# NOTES:
#  - If you change the configuration, you have to make sure that you update the number of jobs in the N_JOBS array.
#  - Jobs indices start at DAY_OF_WEEK,1 and not at zero.
#  - For all jobs the calls to 'make' and 'make testcluster' the flags are concatenated from
#      the given flags and the SCIP_FLAGS.
#  - To add settings please visit the section 'setup testruns'. This can only happen after compilation.
#  - Don't add LPS=xxx and LPSOPT=xxx but instead use EXECUTABLE=[scipdbgspx|scipdbgcpx].
#  - Only 10 runs per day will be executed. If you need more you should overthink you overall concept.
#  - The check/jenkins_*_cmake.sh evaluation scripts don't work yet if you use a global seed shift.
# FORMAT:
#    JOBS[x,y]="EXECUTABLE=scipdbgspx/bin/scip BINID=scipdbgspx-${GITBRANCH} MEM=100 QUEUE=M620,M620v2,M630,M630v2 TEST=short TIME=10 PERMUTE=2 SETTINGS=default PERFORMANCE=debug"
#    JOBS[x,y]="EXECUTABLE=scipdbgcpx/bin/scip BINID=scipdbgcpx-${GITBRANCH} MEM=100 QUEUE=M620,M620v2,M630,M630v2 TEST=short TIME=10 PERMUTE=2 SETTINGS=default PERFORMANCE=debug"

RANDOMSEED=$(date +%Y%m%d)
export DATESTR=$(date "+%Y-%m-%d %H:%M:%S")

# use associative arrays, this requires bash4
# declaration
declare -A JOBS

# for descriptions on the testsets see scip/check/testsets/README.md

# the following jobs run on all branches
# jobs running on monday
JOBS[1,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=MINLP TIME=60 SETTINGS=minlp_default PERFORMANCE=debug"

# jobs running on tuesday
JOBS[2,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=MINLP TIME=60 SETTINGS=minlp_default_${RANDOMSEED} PERFORMANCE=debug"

# jobs running on wednesday
JOBS[3,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=MINLP TIME=60 SETTINGS=minlp_presolaggr_sepaaggr_heuroff_${RANDOMSEED} PERFORMANCE=debug"

# jobs running on thursday
JOBS[4,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=MINLP TIME=60 SETTINGS=minlp_heuraggr_${RANDOMSEED} PERFORMANCE=debug"

# jobs running on friday
JOBS[5,1]="EXECUTABLE=scipdbgcpx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgcpx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=MINLP TIME=60 SETTINGS=minlp_default PERFORMANCE=debug"

# jobs running on sunday
JOBS[7,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=minlpdev-solvable TIME=7200 SETTINGS=minlp_default PERFORMANCE=debug"

# mip tests don't run on consexpr
if [ "${GITBRANCH}" != "consexpr" ]; then
  # jobs running on monday
  JOBS[1,2]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdebug TIME=60 SETTINGS=default PERFORMANCE=debug"
  JOBS[1,3]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=miplib2017_benchmark_hard TIME=7200 SETTINGS=default PERFORMANCE=debug"

  # jobs running on tuesday
  JOBS[2,2]="EXECUTABLE=scipdbgcpx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgcpx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=miplib2017_benchmark TIME=7200 SETTINGS=default_${RANDOMSEED} PERFORMANCE=debug"
  JOBS[2,3]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdebug TIME=60 SETTINGS=default_${RANDOMSEED} PERFORMANCE=debug"

  # jobs running on wednesday
  JOBS[3,2]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdebug TIME=60 SETTINGS=presolaggr_sepaaggr_heuroff_${RANDOMSEED} PERFORMANCE=debug"

  # jobs running on thursday
  JOBS[4,2]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdebug TIME=60 SETTINGS=heuraggr_${RANDOMSEED} PERFORMANCE=debug"
  JOBS[4,3]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=sap-benchmark TIME=1 SETTINGS=${SAPSETTINGS} PERFORMANCE=debug"

  # jobs running on friday
  JOBS[5,2]="EXECUTABLE=scipdbgcpx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgcpx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdebug TIME=60 SETTINGS=default PERFORMANCE=debug"

  # jobs running on saturday
  JOBS[6,1]="EXECUTABLE=scipdbgspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipdbgspx-${GITBRANCH}_${RANDOMSEED} OPT=dbg MEM=6000 QUEUE=M620,M620v2,M630,M630v2 TEST=mipdev2-solvable TIME=7200 SETTINGS=default PERFORMANCE=debug"
fi

#########################
### process variables ###
#########################

# To improve accessibility move todays jobs into separate array
TODAYS_N_JOBS=0

# NOTE: only check up to 10 runs. If there are more there is something wrong...
for i in `seq 1 10`; do
  if [ "${JOBS[${DAY_OF_WEEK},$i]}" == "" ]; then
    break
  fi
  TODAYS_N_JOBS=$i
done
if [ "${TODAYS_N_JOBS}" == "0" ]; then
  echo "No schedules for today! Exiting."
  exit 0
fi

declare -A TODAYS_JOBS

LPSVERSIONS=""
for i in `seq 1 ${TODAYS_N_JOBS}`; do
  TODAYS_JOBS[$i]="${JOBS[${DAY_OF_WEEK},$i]} OUTPUTDIR=results${RANDOMSEED}_${i}"

  # Collect versions to know which lpsolvers to compile
  LPSVERSION=`echo ${TODAYS_JOBS[$i]} |grep -o 'scip[a-z]*'`
  LPSVERSIONS="${LPSVERSIONS} ${LPSVERSION}"
done

# Print some information about what is happening
echo "Today is `date +%A`. Running the following ${TODAYS_N_JOBS} jobs (index ${DAY_OF_WEEK},*):"
for i in `seq 1 ${TODAYS_N_JOBS}`; do
  echo "- job configuration: '${TODAYS_JOBS[$i]}'"
done

###################
### Compilation ###
###################

make solchecker

BUILD_DIR=""
# build with soplex only if today we have some soplex runs scheduled
# that is the case if $LPSVERSIONS contains 'scipdbgspx'
if [[ ${LPSVERSIONS} =~ "scipdbgspx" ]] ; then
  BUILD_DIR=scipdbgspx_${GITBRANCH}_${RANDOMSEED}
  mkdir -p ${BUILD_DIR}
  cd ${BUILD_DIR}
  cmake .. -DCMAKE_C_FLAGS="-DCHECKCLEANBUFFER" -DCMAKE_BUILD_TYPE=Debug -DLPS=spx -DSOPLEX_DIR=${SOPLEX_DIR} -LA
  make -j4
  cd ..
fi
# build with cplex only if today we have some cplex runs scheduled
# that is the case if $LPSVERSIONS contains 'scipdbgcpx'
if [[ ${LPSVERSIONS} =~ "scipdbgcpx" ]] ; then
  BUILD_DIR=scipdbgcpx_${GITBRANCH}_${RANDOMSEED}
  mkdir -p ${BUILD_DIR}
  cd ${BUILD_DIR}
  cmake .. -DCMAKE_C_FLAGS="-DCHECKCLEANBUFFER" -DCMAKE_BUILD_TYPE=Debug -DLPS=cpx -LA
  make -j4
  cd ..
fi

######################
### Setup testruns ###
######################

SCIP_BINARY=${BUILD_DIR}/bin/scip

# NOTES:
#  - When building a default setting with random seed, use a capital D. No setting name should be a prefix of another!

# MIP settings
${SCIP_BINARY} -c "set rand rand ${RANDOMSEED} set diffsave settings/default_${RANDOMSEED}.set q"
${SCIP_BINARY} -c "set heur emph aggr set rand rand ${RANDOMSEED} set diffsave settings/heuraggr_${RANDOMSEED}.set q"
${SCIP_BINARY} -c "set sepa emph aggr set presol emph aggr set heur emph off set rand rand ${RANDOMSEED} set diffsave settings/presolaggr_sepaaggr_heuroff_${RANDOMSEED}.set q"

# MINLP settings
${SCIP_BINARY} -c "set numerics checkfeastolfac 1000.0 set diffsave settings/minlp_default.set q"
${SCIP_BINARY} -c "set numerics checkfeastolfac 1000.0 set rand rand ${RANDOMSEED} set diffsave settings/minlp_default_${RANDOMSEED}.set q"
${SCIP_BINARY} -c "set heur emph aggr set numerics checkfeastolfac 1000.0 set rand rand ${RANDOMSEED} set diffsave settings/minlp_heuraggr_${RANDOMSEED}.set q"
${SCIP_BINARY} -c "set sepa emph aggr set presol emph aggr set heur emph off set numerics checkfeastolfac 1000.0 set rand rand ${RANDOMSEED} set diffsave settings/minlp_presolaggr_sepaaggr_heuroff_${RANDOMSEED}.set q"

# create more required symlinks
ln -fs /nfs/optimi/kombadon/IP check/
ln -fs /nfs/optimi/kombadon/MINLP check/

# get testset files to the correct place
cp check/IP/instancedata/testsets/*.test check/testset/

#######################
### Submit Testruns ###
#######################

for i in `seq 1 ${TODAYS_N_JOBS}`; do
  FLAGS=${TODAYS_JOBS[$i]}
  for j in "OPT SEEDS EXECUTABLE BINID MEM QUEUE TEST TIME PERMUTE PERFORMANCE EXCLUSIVE SETTINGS OUTPUTDIR"; do
    unset $j
  done
  export ${FLAGS}

  echo "Submitting job with configuration:\n- compilation: ${SCIPFLAGS}'\n- make testcluster: ${FLAGS}"
  make testcluster DEBGUTOOL=gdb ${FLAGS} | check/jenkins_check_results_cmake.sh
done
