#!/usr/bin/env bash
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2016 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

TSTNAME=$1
BINNAME=$2
SETNAMES=$3
BINID=$4
TIMELIMIT=$5
NODELIMIT=$6
MEMLIMIT=$7
THREADS=$8
FEASTOL=$9
DISPFREQ=${10}
CONTINUE=${11}
LOCK=${12}
VERSION=${13}
LPS=${14}
VALGRIND=${15}
CLIENTTMPDIR=${16}
REOPT=${17}
OPTCOMMAND=${18}
SETCUTOFF=${19}
MAXJOBS=${20}
VISUALIZE=${21}
PERMUTE=${22}

# check if all variables defined (by checking the last one)
if test -z $PERMUTE
then
    echo Skipping test since not all variables are defined
    echo "TSTNAME       = $TSTNAME"
    echo "BINNAME       = $BINNAME"
    echo "SETNAMES      = $SETNAMES"
    echo "BINID         = $BINID"
    echo "TIMELIMIT     = $TIMELIMIT"
    echo "NODELIMIT     = $NODELIMIT"
    echo "MEMLIMIT      = $MEMLIMIT"
    echo "THREADS       = $THREADS"
    echo "FEASTOL       = $FEASTOL"
    echo "DISPFREQ      = $DISPFREQ"
    echo "CONTINUE      = $CONTINUE"
    echo "LOCK          = $LOCK"
    echo "VERSION       = $VERSION"
    echo "LPS           = $LPS"
    echo "VALGRIND      = $VALGRIND"
    echo "CLIENTTMPDIR  = $CLIENTTMPDIR"
    echo "REOPT         = $REOPT"
    echo "OPTCOMMAND    = $OPTCOMMAND"
    echo "SETCUTOFF     = $SETCUTOFF"
    echo "MAXJOBS       = $MAXJOBS"
    echo "VISUALIZE     = $VISUALIZE"
    echo "PERMUTE       = $PERMUTE"
    exit 1;
fi

# call routines for creating the result directory, checking for existence
# of passed settings, etc
TIMEFORMAT="sec"
MEMFORMAT="kB"
. ./configuration_set.sh $BINNAME $TSTNAME $SETNAMES $TIMELIMIT $TIMEFORMAT $MEMLIMIT $MEMFORMAT $VALGRIND $SETCUTOFF

if test -e $SCIPPATH/../$BINNAME
then
   export EXECNAME=${VALGRINDCMD}$SCIPPATH/../$BINNAME
else
   export EXECNAME=$BINNAME
fi

# check if we can set hard memory limit (address, leak, or thread sanitzer don't like ulimit -v)
if [ `uname` == Linux ] && (ldd ${EXECNAME} | grep -q lib[alt]san) ; then
   # skip hard mem limit if using AddressSanitizer (libasan), LeakSanitizer (liblsan), or ThreadSanitizer (libtsan)
   HARDMEMLIMIT="none"
elif [ `uname` == Linux ] && (nm ${EXECNAME} | grep -q __[alt]san) ; then
   # skip hard mem limit if using AddressSanitizer, LeakSanitizer, or ThreadSanitizer linked statitically (__[alt]san symbols)
   HARDMEMLIMIT="none"
else
   ULIMITMEM="ulimit -v $HARDMEMLIMIT k;"
fi

INIT="true"
COUNT=0
for ((p = 0; $p <= $PERMUTE; p++))
do
    for INSTANCE in $INSTANCELIST DONE
    do
        COUNT=`expr $COUNT + 1`

        # loop over settings
        for SETNAME in ${SETTINGSLIST[@]}
        do
        # waiting while the number of jobs has reached the maximum
        if [ $MAXJOBS -ne 1 ]
        then
                while [ `jobs -r|wc -l` -ge $MAXJOBS ]
                do
                    sleep 10
                    echo "Waiting for jobs to finish."
                done
        fi

        # infer the names of all involved files from the arguments
            QUEUE=`hostname`

            if test "$INSTANCE" = "DONE"
            then
                wait
                #echo $EVALFILE
                ./evalcheck_cluster.sh -r $EVALFILE
                continue
            fi

            # infer the names of all involved files from the arguments
            . ./configuration_logfiles.sh $INIT $COUNT $INSTANCE $BINID $PERMUTE $SETNAME $TSTNAME $CONTINUE $QUEUE  $p

            if test "$SKIPINSTANCE" = "true"
            then
                continue
            fi

            # find out the solver that should be used
            SOLVER=`stripversion $BINNAME`

            CONFFILE="configuration_tmpfile_setup_${SOLVER}.sh"

            # we don't have separate configuration files for most examples and applications, use SCIP configuration file instead
            if ! test -f "$CONFFILE"
            then
                CONFFILE="configuration_tmpfile_setup_scip.sh"
            fi

            # overwrite the tmp file now
            # call tmp file configuration for SCIP
            . ./$CONFFILE $INSTANCE $SCIPPATH $TMPFILE $SETNAME $SETFILE $THREADS $SETCUTOFF \
                $FEASTOL $TIMELIMIT $MEMLIMIT $NODELIMIT $LPS $DISPFREQ  $REOPT $OPTCOMMAND $CLIENTTMPDIR $FILENAME $SETCUTOFF $VISUALIZE $SOLUFILE

            # additional environment variables needed by run.sh
            export SOLVERPATH=$SCIPPATH
            export BASENAME=$FILENAME
            export FILENAME=$INSTANCE
            export SOLNAME=$SOLCHECKFILE
            export CLIENTTMPDIR
            export CHECKERPATH=$SCIPPATH/solchecker

            echo Solving instance $INSTANCE with settings $SETNAME, hard time $HARDTIMELIMIT, hard mem $HARDMEMLIMIT
            if [ $MAXJOBS -eq 1 ]
            then
                bash -c "ulimit -t $HARDTIMELIMIT s; $ULIMITMEM ulimit -f 200000; ./run.sh"
            else
                bash -c "ulimit -t $HARDTIMELIMIT s; $ULIMITMEM ulimit -f 200000; ./run.sh" &
            fi
            #./run.sh
        done
        INIT="false"
    done
done
