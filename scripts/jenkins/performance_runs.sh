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
#        GITBRANCH=master ./performance_runs.sh

# Arguments | defaultvalue                             | possibilities
# ----------|------------------------------------------|--------------
# GITBRANCH | master                                   | master, bugfix

echo "This is performance_runs.sh running."

######################################
### evaluate commandline arguments ###
######################################

# set default arguments
# If no branch is given, try to guess the branch based on the current directory
if [ "${GITBRANCH}" == "" ]; then
  # GIT_BRANCH is a jenkins variable, if not present, try to get it from the git repository. The second thing is not robust because there may be more branches that this HEAD is present in.
  GITBRANCH=`echo ${GIT_BRANCH} | cut -d / -f 2`
  if [ "${GITBRANCH}" == "" ]; then
      GITBRANCH=`git show -s --pretty=%D | cut -d , -f 2 | cut -d / -f 2`
  fi
fi

if [ "${GITBRANCH}" != "master" ]; then
  if [[ ${GITBRANCH} =~ "bugfix" ]]; then
    GITBRANCH=bugfix
  else
    echo "Branch is neither 'master' nor 'bugfix'. Something is wrong. Exiting."
    exit 1
  fi
fi

export GITBRANCH
export MODE=performance

# This soplex there is installed on pushes to soplex by the jenkins job SOPLEX_install_${GITBRANCH}.
# We have to export these variables to make them available to cmake.
# Scripts will also use nonexported variables correctly.
export SOPLEX_DIR=/OPTI/adm_timo/soplex_${GITBRANCH}_Release/

export CRITERION_DIR=""
export IPOPT_DIR=/optimi/usr/sw/ipopt
export BLISS_DIR=/optimi/usr/sw/bliss

# Find out what day of week it is: mon-1 .. sun-7
DAY_OF_WEEK=`date +%u`

# create required directory
mkdir -p settings

####################################
### jobs configuration variables ###
####################################
# NOTES:
#  - If you change the configuration, you have to make sure that you update the number of jobs in the N_JOBS array.
#  - Jobs indices start at DAY_OF_WEEK,1 and not at zero.
#  - For all jobs the calls to 'make' and 'make testcluster' the flags are concatenated from
#      the given flags and the SCIP_FLAGS.
#  - To add settings please visit the section 'setup testruns'. This can only happen after compilation.
#  - Only 10 runs per day will be executed. If you need more you should overthink you overall concept.
# FORMAT:
#    JOBS[x,y]="EXCLUSIVE=true EXECUTABLE=scipoptspx MEM=100 QUEUE=opt TEST=short TIME=10 PERMUTE=2 SETTINGS=default PERFORMANCE=performance"

RANDOMSEED=`date +%Y%m%d`

# use associative arrays, this requires bash4
# declaration
declare -A JOBS
declare -A TRIGGER

# jobs running on saturday
JOBS[6,1]="EXECUTABLE=scipoptspx MEM=50000 QUEUE=M620v3 TEST=mipdev-solvable TIME=7200 SETTINGS=default PERFORMANCE=performance"
JOBS[6,2]="EXECUTABLE=scipoptspx MEM=50000 QUEUE=M640 TEST=minlpdev-solvable TIME=3600 SETTINGS=minlp_default PERFORMANCE=performance PERMUTE=4"
TRIGGER[6,1]="https://adm_timo:0bf48f6ec4dfdebe4276d217c026c607@cijenkins.zib.de/job/SCIP_SAP_perfrun_${GITBRANCH}_weekly/build?token=weeklysaptoken"

# jobs running on sunday
JOBS[7,1]="EXECUTABLE=scipoptspx MEM=50000 QUEUE=M630v2 TEST=sapdev-solvable TIME=3600 SETTINGS=sap-next-release-pure-diff PERFORMANCE=performance"

# symlink to SAP settings for the next release settings
ln -fs ~/sap-next-release-pure-diff.set settings/.


#########################
### process variables ###
#########################

# To improve accessibility move todays jobs into seperate array
TODAYS_N_JOBS=0
TODAYS_N_TRIGGERS=0

# NOTE: only check up to 10 runs. If there are more there is something wrong...
for i in `seq 1 10`; do
  if [ "${JOBS[${DAY_OF_WEEK},$i]}" == "" ]; then
    break
  fi
  TODAYS_N_JOBS=$i
done

declare -A TODAYS_JOBS

for i in `seq 1 ${TODAYS_N_JOBS}`; do
  TODAYS_JOBS[$i]="${JOBS[${DAY_OF_WEEK},$i]} OUTPUTDIR=results${RANDOMSEED}_${i}"

  # append /bin/scip to executable
  TODAYS_JOBS[$i]=`echo ${TODAYS_JOBS[$i]}|sed "s@\(scipopt[cs]px\)@\1/bin/scip@"`
done

# Print some information about what is happening
echo "Today is `date +%A`. Running the following ${TODAYS_N_JOBS} jobs (index ${DAY_OF_WEEK},*):"
for i in `seq 1 ${TODAYS_N_JOBS}`; do
  echo "- job configuration: '${TODAYS_JOBS[$i]}'"
done
echo "Triggering the following jobs:"
for i in `seq 1 10`; do
  if [ "${TRIGGER[${DAY_OF_WEEK},$i]}" == "" ]; then
    break
  fi
  echo "- ${TRIGGER[${DAY_OF_WEEK},$i]}"
  TODAYS_N_TRIGGERS=$i
done

# exit if nothing to do
if [ "${TODAYS_N_JOBS}" == "0" -a "${TODAYS_N_TRIGGERS}" == "0" ]; then
  echo "No schedules for today! Exiting."
  exit 0
fi

if [ "${TODAYS_N_JOBS}" != "0" ]; then
  ###################
  ### Compilation ###
  ###################

  # build with soplex only if today we have some soplex runs scheduled
  BUILD_DIR=scipoptspx
  mkdir -p ${BUILD_DIR}
  cd ${BUILD_DIR}
  cmake .. -DCMAKE_BUILD_TYPE=Release -DLPS=spx -DSOPLEX_DIR=${SOPLEX_DIR}
  make -j4
  cd ..

  ######################
  ### Setup testruns ###
  ######################

  SCIP_BINARY=${BUILD_DIR}/bin/scip

  # NOTES:
  #  - When building a default setting with random seed, use a capital D. No setting name should be a prefix of another!

  # MIP settings

  # MINLP settings
  ${SCIP_BINARY} -c "set numerics checkfeastolfac 1000.0 set limits gap 1e-4 set diffsave settings/minlp_default.set q"

  # create more required symlinks
  ln -fs /optimi/kombadon/IP check/
  ln -fs /optimi/kombadon/MINLP check/

  #######################
  ### Submit Testruns ###
  #######################

  for i in `seq 1 ${TODAYS_N_JOBS}`; do
    FLAGS=${TODAYS_JOBS[$i]}
    for j in "EXECUTABLE MEM QUEUE TEST TIME PERMUTE PERFORMANCE EXCLUSIVE SETTINGS OUTPUTDIR"; do
      unset $j
    done
    export ${FLAGS}
    echo "Submitting job with configuration:\n- compilation: ${SCIPFLAGS}'\n- make testcluster: ${FLAGS}"
    make testcluster ${FLAGS} | check/jenkins_check_results_cmake.sh
  done
fi

if [ "${TODAYS_N_TRIGGERS}" != "0" ]; then
  # NOTE: only check up to 10 triggers. If there are more there is something wrong...
  echo "Will trigger the following jobs:"
  for i in `seq 1 ${TODAYS_N_TRIGGERS}`; do
    curl -f -I "${TRIGGER[${DAY_OF_WEEK},$i]}"
  done
fi
