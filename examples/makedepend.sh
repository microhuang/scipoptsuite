#!/usr/bin/env bash
#
# This scripts generates the dependences for SCIP
#

EXAMPLES=(Binpacking CallableLibrary CAP Eventhdlr GMI LOP MIPSolver Queens Relaxator TSP VRP)
OPTS=(opt dbg opt-gccold)

for EXAMPLE in ${EXAMPLES[@]}
do
    echo ===== $EXAMPLE =====
    echo
    cd $EXAMPLE
    for OPT in ${OPTS[@]}
    do
	make OPT=$OPT ZIMPL=false LPS=none depend
    done
    cd ..
    echo
done
