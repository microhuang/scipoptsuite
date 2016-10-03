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

### configures the right test output files such as the .eval, the .tmp and the .set
### files to run a test on.
### the invoking script should pass "init" as argument to signalize that
### files need to be reset

### environment variables declared in this script
### OUTFILE - the name of the (sticked together) output file
### ERRFILE - the name of the (sticked together) error file
### SHORTPROBNAME - the basename of $INSTANCE without file extension
### FILENAME - the basename of the local files (.out, .tmp, and .err)
### EVALFILE - evaluation file to glue single output and error files together
### SKIPINSTANCE - should the instance be skipped because it was already evaluated in a previous setting?
### BASENAME - $SCIPPATH/results/$FILENAME cf. FILENAME argument
### TMPFILE  - the batch file name to pass for solver instructions
### SETFILE  - the name of the settings file to save solver settings to

### environment variables passed as arguements to this script
INIT=$1      # should log files be initialized (this overwrite or copy/move some existing log files)
COUNT=$2     # the instance count as part of the filename
INSTANCE=$3  # the name of the instance
BINID=$4     # the ID of the binary to use
PERMUTE=$5   # the number of permutations to use - 0 for no permutation
SEEDS=$6     # the number of random seeds - 0 only default seeds
SETNAME=$7   # the name of the setting
TSTNAME=$8   # the name of the testset
CONTINUE=$9  # should test continue an existing run
# optional variables
QUEUE=$9     # the queue name
p=${10}         # the index of the current permutation - only needed if permutations are used
s=${11}         # shift of the global random seed - only needed if different seeds are used

if test "$QUEUE" = ""
then
    QUEUE=`hostname`
fi

OUTFILE=results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME.out
ERRFILE=results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME.err

# if number of permutations is positive, add postfix
if test $PERMUTE -gt 0
then
    # if number of seeds is positive, add postfix
    if test $SEEDS -gt 0
    then
        EVALFILE=$SCIPPATH/results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME"#s"$s"#p"$p.eval
    else
        EVALFILE=$SCIPPATH/results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME"#p"$p.eval
    fi
else
    # if number of seeds is positive, add postfix
    if test $SEEDS -gt 0
    then
        EVALFILE=$SCIPPATH/results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME"#s"$s.eval
    else
        EVALFILE=$SCIPPATH/results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME.eval
    fi
fi

if test "$INSTANCE" = "DONE"
then
    return
fi

# reset files if flag is set to 'init'
if test $INIT = "true"
then
    #reset the eval file
    echo > $EVALFILE

    #mv existing out and error files
    if test "$CONTINUE" = "true"
    then
        MVORCP=cp
    else
        MVORCP=mv
    fi
    DATEINT=`date +"%s"`
    for FILE in OUTFILE ERRFILE
    do
        if test -e $FILE
        then
            $MVORCP $FILE $FILE.old-$DATEINT
        fi
    done
fi


# filter all parseable file format extensions
SHORTPROBNAME=`basename $INSTANCE .gz`
for EXTENSION in .mps .lp .opb .gms .pip .zpl .cip .fzn .osil .wbo .cnf .difflist
do
    SHORTPROBNAME=`basename $SHORTPROBNAME $EXTENSION`
done
SHORTPROBNAME=`"${SHORTPROBNAME:0:25}"`

# if number of permutations is positive, add postfix
if test $PERMUTE -gt 0
then
    # if number of seeds is positive, add postfix
    if test $SEEDS -gt 0
    then
        FILENAME=$USER.$TSTNAME.$COUNT"_"$SHORTPROBNAME.$BINID.$QUEUE.$SETNAME#"s"$s#"p"$p
    else
        FILENAME=$USER.$TSTNAME.$COUNT"_"$SHORTPROBNAME.$BINID.$QUEUE.$SETNAME#"p"$p
    fi
else
    # if number of seeds is positive, add postfix
    if test $SEEDS -gt 0
    then
        FILENAME=$USER.$TSTNAME.$COUNT"_"$SHORTPROBNAME.$BINID.$QUEUE.$SETNAME#"s"$s
    else
        FILENAME=$USER.$TSTNAME.$COUNT"_"$SHORTPROBNAME.$BINID.$QUEUE.$SETNAME
    fi
fi

SKIPINSTANCE="false"
# in case we want to continue we check if the job was already performed
if test "$CONTINUE" = "true" && test -e results/$FILENAME.out
then
    echo skipping file $INSTANCE due to existing output file results/$FILENAME.out
    SKIPINSTANCE="true"
fi

# configure global names TMPFILE (batch file) and SETFILE to save settings to
BASENAME=$SCIPPATH/results/$FILENAME
TMPFILE=$BASENAME.tmp
SETFILE=$BASENAME.set
# even if we decide skip this instance, we write the basename to the eval file
echo $BASENAME >> $EVALFILE
