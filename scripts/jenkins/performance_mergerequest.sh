#!/bin/bash -ex
# the -x is for writing each command to standard error (preceded by a '+') before it is executed.
# other flags for debugging: -Canvu, please also consult 'man sh'.

# NOTES:
#  - We use associative arrays, this requires bash4.

###################
### Description ###
###################

# This script is used by cijenkins.zib.de.

# Usage: from scip root execute
#        TESTMODE=short GITBRANCH=master ./scripts/jenkins/performance_mergerequest.sh

# Arguments | defaultvalue                             | possibilities
# ----------|------------------------------------------|--------------
# GITBRANCH | master                                   | master, v60-bugfix, consexpr
# TESTMODE  | ""                                       | mip, minlp, short, minlplib
# QUICKMODE | ""                                       | quick, continue, ""

echo "This is performance_mergerequest.sh running."
: ${TESTMODE:=""}
: ${QUICKMODE:=""}
: ${GITBRANCH:=${gitlabTargetBranch}}

if [ "${gitlabTriggerPhrase}" != "" ]; then
  TESTMODE=$(echo $gitlabTriggerPhrase | cut -f3 -d " ") # get third field (testset)
  QUICKMODE=$(echo "${gitlabTriggerPhrase}" | cut -f4 -d " ")
else
  echo "Nothing to do, please check your triggerphrase: '${gitlabTriggerPhrase}'. Exiting."
  exit 1
fi

env

ORIGBRANCH=${GITBRANCH}

if [ "${TESTMODE}" == "short" ]; then
  echo "Testing short"
elif [ "${TESTMODE}" == "mip" ]; then
  echo "Testing mip"
  if [ "${QUICKMODE}" == "quick" ]; then
    echo "Testing mip quick"
    TESTMODE=quick_mip
  elif [ "${QUICKMODE}" == "continue" ]; then
    echo "Testing mip continue"
    TESTMODE=continue_mip
  fi
elif [ "${TESTMODE}" == "minlp" ]; then
  echo "Testing minlp"
  if [ "${QUICKMODE}" == "quick" ]; then
    echo "Testing minlp quick"
    TESTMODE=quick_minlp
  elif [ "${QUICKMODE}" == "continue" ]; then
    echo "Testing minlp continue"
    TESTMODE=continue_minlp
  fi
elif [ "${TESTMODE}" == "minlplib" ]; then
  echo "Testing minlplib"
  TESTMODE=quick_minlplib
else
  echo "Nothing to do, please check your triggerphrase: '${gitlabTriggerPhrase}'. Exiting."
  exit 1
fi

######################################
### evaluate commandline arguments ###
######################################

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
export MODE=performance
export MRSETTINGS="MR-${gitlabMergeRequestIid}"

####################################
### jobs configuration variables ###
####################################
# NOTES:
#  - If you change the configuration, you have to make sure that you update the number of jobs in the N_JOBS array.
#  - For all jobs the calls to 'make' and 'make testcluster' the flags are concatenated from
#      the given flags and the SCIP_FLAGS.
#  - To add settings please visit the section 'setup testruns'. This can only happen after compilation.
#  - Don't add LPS=xxx and LPSOPT=xxx but instead use EXECUTABLE=[scipdbgspx|scipdbgcpx].
#  - Only 10 runs will be executed. If you need more you should overthink you overall concept.
#  - The check/jenkins_*_cmake.sh evaluation scripts don't work yet if you use a global seed shift.
# FORMAT:
#    JOBS[x,y]="EXCLUSIVE=true EXECUTABLE=scipoptspx/bin/scip BINID=scipoptspx-${GITBRANCH} MEM=100 QUEUE=opt TEST=short TIME=10 PERMUTE=2 SETTINGS=default PERFORMANCE=mergerequest"

RANDOMSEED=$(date +%Y%m%d%H%M)

# for descriptions on the testsets see scip/check/testsets/README.md
# jobs running

if [ "${TESTMODE}" == "short" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} EXCLUSIVE=false MEM=5000 QUEUE=opt TEST=short TIME=60 SETTINGS=${MRSETTINGS} PERFORMANCE=mergerequest SEEDS=0"
elif [ "${TESTMODE}" == "mip" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M620v3 TEST=mipdev2-solvable TIME=7200 SETTINGS=${MRSETTINGS} PERFORMANCE=mergerequest SEEDS=4"
elif [ "${TESTMODE}" == "minlp" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M640 TEST=minlpdev-solvable TIME=3600 SETTINGS=minlp_${MRSETTINGS} PERFORMANCE=mergerequest PERMUTE=4"
elif [ "${TESTMODE}" == "quick_mip" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M620v3 TEST=mipdev2-solvable TIME=7200 SETTINGS=${MRSETTINGS} PERFORMANCE=mergerequest SEEDS=1"
elif [ "${TESTMODE}" == "quick_minlp" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M640 TEST=minlpdev-solvable TIME=3600 SETTINGS=minlp_${MRSETTINGS} PERFORMANCE=mergerequest PERMUTE=1"
elif [ "${TESTMODE}" == "quick_minlplib" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M640 TEST=MINLP_minlplib TIME=3600 SETTINGS=minlp_${MRSETTINGS} PERFORMANCE=mergerequest"
elif [ "${TESTMODE}" == "continue_mip" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M620v3 TEST=mipdev2-solvable TIME=7200 SETTINGS=${MRSETTINGS} PERFORMANCE=mergerequest SEEDS=2 GLBSEEDSHIFT=2"
elif [ "${TESTMODE}" == "continue_minlp" ]; then
  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M640 TEST=minlpdev-solvable TIME=3600 SETTINGS=minlp_${MRSETTINGS} PERFORMANCE=mergerequest PERMUTE=2 STARTPERM=2"
#elif [ "${TESTMODE}" == "sap" ]; then
#  JOB="EXECUTABLE=scipoptspx_${GITBRANCH}_${RANDOMSEED}/bin/scip BINID=scipoptspx_${GITBRANCH}_${RANDOMSEED} SLURMACCOUNT=scip EXCLUSIVE=true MEM=50000 QUEUE=M630v2 TEST=sapdev-solvable TIME=3600 SETTINGS=${SAPSETTINGS} PERFORMANCE=mergerequest SEEDS=2"
fi

#SAPSETTINGS=sap-next-release-pure-diff
#if [ "${GITBRANCH}" != "master" ]; then
#  SAPSETTINGS=sap-600-pure-diff
#fi

# create required directory
mkdir -p settings

# symlink to SAP settings for the next release settings
ln -fs ~/sap-next-release-pure-diff.set settings/.
ln -fs ~/sap-600-pure-diff.set settings/.

JOB="${JOB} OUTPUTDIR=results${RANDOMSEED}"
FLAGS=${JOB}
export ${FLAGS}
export PERFORMANCE=mergerequest

# from check/jenkins_failcheck.sh
SCIP_BUILDDIR=$(echo ${EXECUTABLE}| cut -d '/' -f 1|cut -d '_' -f 1)

# get the comparison db for githash
if [ "${TESTMODE}" == "short" ]; then
  COMPARERBDB="/nfs/OPTI/adm_timo/databases/rbdb/${GITBRANCH}_${MODE}_${TEST}_${SETTINGS}_${SCIP_BUILDDIR}_rbdb.txt"
elif [ "${TESTMODE}" == "mip" ]; then
  COMPARERBDB="/nfs/OPTI/adm_timo/databases/rbdb/${GITBRANCH}_${MODE}_${TEST}_${SETTINGS}_${SCIP_BUILDDIR}_rbdb.txt"
elif [ "${TESTMODE}" == "minlp" ]; then
  COMPARERBDB="/nfs/OPTI/adm_timo/databases/rbdb/${GITBRANCH}_${MODE}_${TEST}_${SETTINGS}_${SCIP_BUILDDIR}_rbdb.txt"
# elif [ "${testmode}" == "sap" ]; then
fi

# get git hash of current performance comparison run
export COMPAREHASH=$(git rev-parse origin/performance-${GITBRANCH})

# ensure that the current branch is based on the last performance run
set +e
GITLOG="$(git log --pretty=format:'%H' | grep ${COMPAREHASH})"
if [ "${GITLOG}" == "" ]; then
  export FAILREASON="Latest performance run of ${ORIGBRANCH} is not part of your branch. Please merge!"
  echo ${FAILREASON}
  exit 1
fi

# ensure that the current branch has not branched off ahead of the latest performance run
GITLOG=$(git log origin/${ORIGBRANCH} --pretty=format:'%H' | grep ${COMPAREHASH} -B1 | head -n 1)
if [ "${GITLOG}" != "${COMPAREHASH}" ]; then
  GITCHECK=$(git log --pretty=format:'%H' | grep ${GITLOG})
  if [ "${GITCHECK}" != "" ]; then
    export FAILREASON="Your branch has not branched off from the same commit on ${ORIGBRANCH} where the latest performance run started (look for branch with name performance-*). Abort!"
    echo ${FAILREASON}
    exit 1
  fi
fi
set -e

export CRITERION_DIR=""
export BLISS_DIR=/nfs/OPTI/bzfgleix/software/bliss-0.73p-Ubuntu18.04
#export IPOPT_DIR=/nfs/optimi/usr/sw/ipopt-static
export IPOPT_DIR=/nfs/optimi/usr/sw/Ipopt-3.12.13-static-build
export ZIMPL_DIR=/nfs/OPTI/jenkins/workspace/ZIMPL_monthly/build-gnu-Release/

# We have to export these variables to make them available to cmake.
# Scripts will also use nonexported variables correctly.
if [ "${GITBRANCH}" == "consexpr" ]; then
  export SOPLEX_DIR=/nfs/OPTI/adm_timo/performance_soplex_master/
  export PAPILO_DIR=/nfs/OPTI/adm_timo/performance_papilo_master/
else
  export SOPLEX_DIR=/nfs/OPTI/adm_timo/performance_soplex_${GITBRANCH}/
  export PAPILO_DIR=/nfs/OPTI/adm_timo/performance_papilo_${GITBRANCH}/
fi

###################
### Compilation ###
###################

make solchecker

# build with soplex
BUILD_DIR=scipoptspx_${GITBRANCH}_${RANDOMSEED}
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

cmake .. -DCMAKE_BUILD_TYPE=Release -DLPS=spx -DSOPLEX_DIR=${SOPLEX_DIR} -DPAPILO_DIR=${PAPILO_DIR} -LA
make -j4
cd ..

######################
### Setup testruns ###
######################

SCIP_BINARY=${BUILD_DIR}/bin/scip

# NOTES:
#  - When building a default setting with random seed, use a capital D. No setting name should be a prefix of another!

# MIP settings
touch "settings/${MRSETTINGS}.set"

# MINLP settings
${SCIP_BINARY} -c "set numerics checkfeastolfac 1000.0 set limits gap 1e-4 set diffsave settings/minlp_${MRSETTINGS}.set q"

# create more required symlinks
ln -fs /nfs/optimi/kombadon/IP check/
ln -fs /nfs/optimi/kombadon/MINLP check/

# get testset files to the correct place
cp check/IP/instancedata/testsets/*.test check/testset/

#######################
### Submit Testrun ###
#######################

echo "Submitting job with configuration:\n '\n- make testcluster: ${FLAGS}"
make testcluster ${FLAGS} | check/jenkins_check_results_cmake.sh

