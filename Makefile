#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2005 Tobias Achterberg                              *
#*                                                                           *
#*                  2002-2005 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic Licence.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id: Makefile,v 1.111 2005/06/23 16:07:37 bzfpfend Exp $

#@file    Makefile
#@brief   SCIP Makefile
#@author  Thorsten Koch
#@author  Tobias Achterberg

.PHONY:		depend lpidepend clean lint doc test testcplex

ARCH            :=      $(shell uname -m | \
                        sed \
			-e s/sun../sparc/ \
			-e s/i.86/x86/ \
			-e s/IP../mips/ \
			-e s/9000..../hppa/ \
			-e s/00........../pwr4/)
OSTYPE		:=	$(shell uname -s | tr '[:upper:]' '[:lower:]' | sed -e s/irix../irix/ )
MACHINENAME	:=	$(shell uname -n | tr '[:upper:]' '[:lower:]')


#-----------------------------------------------------------------------------
# default settings
#-----------------------------------------------------------------------------

OPT		=	dbg
LPS		=	cpx
COMP		=	gnu
TIME     	=  	3600
NODES           =       2100000000
MEM		=	1024
FEASTOL		=	default
TEST		=	miplib
SETTINGS        =       default

LINKER  	=	C

CC		=	gcc
CXX		=	g++
DCC		=	gcc
DCXX		=	g++
AR		=	ar
RANLIB		=	ranlib
LINT		=	flexelint
DOXY		=	doxygen

FLAGS		=	-I$(SRCDIR)
OFLAGS		=
CFLAGS		=	
CXXFLAGS	=	
LDFLAGS		=	-lpthread -lm
ARFLAGS		=	cr
DFLAGS		=	-MM

GCCWARN		=	-Wall -W -Wpointer-arith -Wcast-align -Wwrite-strings -Wshadow \
			-Wno-unknown-pragmas -Wno-unused-parameter \
			-Wredundant-decls -Wdisabled-optimization \
			-Wsign-compare -Wstrict-prototypes \
			-Wmissing-declarations -Wmissing-prototypes

GXXWARN		=	-Wall -W -Wpointer-arith -Wcast-align -Wwrite-strings -Wshadow \
			-Wno-unknown-pragmas -Wno-unused-parameter \
			-Wredundant-decls -Wdisabled-optimization \
			-Wctor-dtor-privacy -Wnon-virtual-dtor -Wreorder \
			-Woverloaded-virtual -Wsign-promo -Wsynth \
			-Wcast-qual -Wno-unused-parameter # -Wold-style-cast -Wshadow -Wundef

BASE		=	$(OSTYPE).$(ARCH).$(COMP).$(OPT)
OBJDIR		=	obj/O.$(BASE)
OBJSUBDIRS      =	scip objscip
SRCDIR		=	src
BINDIR		=	bin
LIBDIR		=	lib


#-----------------------------------------------------------------------------
include make/make.$(BASE)
#-----------------------------------------------------------------------------


#-----------------------------------------------------------------------------
# Memory Management
#-----------------------------------------------------------------------------

#FLAGS		+=	-DNOSAFEMEM
#FLAGS		+=	-DNOBLOCKMEM


#-----------------------------------------------------------------------------
# LP Solver Interface
#-----------------------------------------------------------------------------

LPILIBNAME	=	lpi$(LPS)

ifeq ($(LPS),cpx)
FLAGS		+=	-I$(LIBDIR)/cpxinc
LPSLDFLAGS	=	-lcplex.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_cpx.o scip/bitencode.o scip/memory.o
LPILIBSRC  	=	$(addprefix $(SRCDIR)/,$(LPILIBOBJ:.o=.c))
endif

ifeq ($(LPS),spx)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spxinc 
LPSLDFLAGS	=	-lsoplex.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_spx.o scip/bitencode.o scip/memory.o
LPILIBSRC	=	src/scip/lpi_spx.cpp src/scip/bitencode.c src/scip/memory.c
endif

ifeq ($(LPS),spxdbg)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spxinc 
LPSLDFLAGS	=	-lsoplexdbg.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_spx.o scip/bitencode.o scip/memory.o
LPILIBSRC	=	src/scip/lpi_spx.cpp src/scip/bitencode.c src/scip/memory.c
endif

ifeq ($(LPS),spx121)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spx121inc 
LPSLDFLAGS	=	-lsoplex121.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_spx121.o scip/bitencode.o scip/memory.o
LPILIBSRC	=	src/scip/lpi_spx121.cpp src/scip/bitencode.c src/scip/memory.c
endif

ifeq ($(LPS),clp)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/clpinc
LPSLDFLAGS	=	-lclp.$(OSTYPE).$(ARCH).$(COMP) -lcoin.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_clp.o scip/bitencode.o scip/memory.o
LPILIBSRC	=	src/scip/lpi_clp.cpp src/scip/bitencode.c src/scip/memory.c
endif

ifeq ($(LPS),clpdbg)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/clpinc
LPSLDFLAGS	=	-lclpdbg.$(OSTYPE).$(ARCH).$(COMP) -lcoindbg.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	scip/lpi_clp.o scip/bitencode.o scip/memory.o
LPILIBSRC	=	src/scip/lpi_clp.cpp src/scip/bitencode.c src/scip/memory.c
endif

LPILIB		=	$(LPILIBNAME).$(BASE)
LPILIBFILE	=	$(LIBDIR)/lib$(LPILIB).a
LPILIBXXX	=	$(addprefix $(OBJDIR)/,$(LPILIBOBJ))
LPILIBDEP	=	src/depend.lpilib.$(LPS).$(OPT)


#-----------------------------------------------------------------------------
# SCIP Library
#-----------------------------------------------------------------------------

SCIPLIBNAME	=	scip
SCIPLIBOBJ	=	scip/branch.o \
			scip/buffer.o \
			scip/clock.o \
			scip/conflict.o \
			scip/cons.o \
			scip/cutpool.o \
			scip/debug.o \
			scip/dialog.o \
			scip/disp.o \
			scip/event.o \
			scip/heur.o \
			scip/history.o \
			scip/interrupt.o \
			scip/intervalarith.o \
			scip/lp.o \
			scip/mem.o \
			scip/memory.o \
			scip/message.o \
			scip/misc.o \
			scip/nodesel.o \
			scip/paramset.o \
			scip/presol.o \
			scip/pricestore.o \
			scip/pricer.o \
			scip/primal.o \
			scip/prob.o \
			scip/prop.o \
			scip/reader.o \
			scip/relax.o \
			scip/retcode.o \
			scip/scip.o \
			scip/scipdefplugins.o \
			scip/sepa.o \
			scip/sepastore.o \
			scip/set.o \
			scip/sol.o \
			scip/solve.o \
			scip/stat.o \
			scip/tree.o \
			scip/var.o \
			scip/vbc.o \
			scip/branch_allfullstrong.o \
			scip/branch_fullstrong.o \
			scip/branch_inference.o \
			scip/branch_mostinf.o \
			scip/branch_leastinf.o \
			scip/branch_pscost.o \
			scip/branch_relpscost.o \
			scip/cons_and.o \
			scip/cons_binpack.o \
			scip/cons_conjunction.o \
			scip/cons_eqknapsack.o \
			scip/cons_integral.o \
			scip/cons_invarknapsack.o \
			scip/cons_knapsack.o \
			scip/cons_linear.o \
			scip/cons_logicor.o \
			scip/cons_or.o \
			scip/cons_setppc.o \
			scip/cons_varbound.o \
			scip/cons_xor.o \
			scip/dialog_default.o \
			scip/disp_default.o \
			scip/heur_coefdiving.o \
			scip/heur_feaspump.o \
			scip/heur_fixandinfer.o \
			scip/heur_fracdiving.o \
			scip/heur_guideddiving.o \
			scip/heur_linesearchdiving.o \
			scip/heur_objpscostdiving.o \
			scip/heur_pscostdiving.o \
			scip/heur_rootsoldiving.o \
			scip/heur_rounding.o \
			scip/heur_simplerounding.o \
			scip/nodesel_bfs.o \
			scip/nodesel_dfs.o \
			scip/nodesel_restartdfs.o \
			scip/presol_dualfix.o \
			scip/presol_probing.o \
			scip/presol_trivial.o \
			scip/prop_pseudoobj.o \
			scip/reader_cnf.o \
			scip/reader_mps.o \
			scip/sepa_clique.o \
			scip/sepa_cmir.o \
			scip/sepa_gomory.o \
			scip/sepa_impliedbounds.o \
			scip/sepa_intobj.o \
			scip/sepa_strongcg.o \
			scip/tclique_branch.o \
			scip/tclique_coloring.o \
			scip/tclique_graph.o

SCIPLIB		=	$(SCIPLIBNAME).$(BASE)
SCIPLIBFILE	=	$(LIBDIR)/lib$(SCIPLIB).a
SCIPLIBXXX	=	$(addprefix $(OBJDIR)/,$(SCIPLIBOBJ))
SCIPLIBSRC	=	$(addprefix $(SRCDIR)/,$(SCIPLIBOBJ:.o=.c))
SCIPLIBDEP	=	src/depend.sciplib.$(OPT)


#-----------------------------------------------------------------------------
# Objective SCIP Library
#-----------------------------------------------------------------------------

OBJSCIPLIBNAME	=	objscip
OBJSCIPLIBOBJ	=	objscip/objbranchrule.o \
			objscip/objconshdlr.o \
			objscip/objeventhdlr.o \
			objscip/objheur.o \
			objscip/objnodesel.o \
			objscip/objpresol.o \
			objscip/objpricer.o \
			objscip/objprobdata.o \
			objscip/objreader.o \
			objscip/objrelax.o \
			objscip/objsepa.o \
			objscip/objvardata.o

OBJSCIPLIB	=	$(OBJSCIPLIBNAME).$(BASE)
OBJSCIPLIBFILE	=	$(LIBDIR)/lib$(OBJSCIPLIB).a
OBJSCIPLIBXXX	=	$(addprefix $(OBJDIR)/,$(OBJSCIPLIBOBJ))
OBJSCIPLIBSRC	=	$(addprefix $(SRCDIR)/,$(OBJSCIPLIBOBJ:.o=.cpp))
OBJSCIPLIBDEP	=	src/depend.objsciplib.$(OPT)


#-----------------------------------------------------------------------------
# Main Program
#-----------------------------------------------------------------------------

MAINNAME	=	scip

ifeq ($(LINKER),C)
MAINOBJ		=	cmain.o
MAINSRC		=	$(addprefix $(SRCDIR)/,$(MAINOBJ:.o=.c))
MAINDEP		=	src/depend.cmain.$(OPT)
endif
ifeq ($(LINKER),CPP)
MAINOBJ		=	cppmain.o
MAINSRC		=	$(addprefix $(SRCDIR)/,$(MAINOBJ:.o=.cpp))
MAINDEP		=	src/depend.cppmain.$(OPT)
endif

MAIN		=	$(MAINNAME).$(BASE).$(LPS)
MAINFILE	=	$(BINDIR)/$(MAIN)
MAINXXX		=	$(addprefix $(OBJDIR)/,$(MAINOBJ))


#-----------------------------------------------------------------------------
# Rules
#-----------------------------------------------------------------------------

all:            $(SCIPLIBFILE) $(OBJSCIPLIBFILE) $(LPILIBFILE) $(MAINFILE)

lint:		$(SCIPLIBSRC) $(LPILIBSRC) $(MAINSRC) $(OBJSCIPLIBSRC)
		-rm -f lint.out
		$(SHELL) -ec 'for i in $^; \
			do \
			echo $$i; \
			$(LINT) lint/$(MAINNAME).lnt +os\(lint.out\) -u -zero \
			$(FLAGS) -UNDEBUG -UWITH_READLINE -UROUNDING_FE $$i; \
			done'

doc:		
		cd doc; $(DOXY) $(MAINNAME).dxy

test:		
		cd check; \
		/bin/sh ./check.sh $(TEST) $(MAINFILE) $(SETTINGS) $(MAIN).$(MACHINENAME) $(TIME) $(NODES) $(MEM) $(FEASTOL);

testcplex:		
		cd check; \
		/bin/sh ./check_cplex.sh $(TEST) $(OSTYPE).$(ARCH).$(MACHINENAME) $(TIME) $(NODES) $(MEM) $(FEASTOL);

$(OBJDIR):	
		-mkdir -p $(OBJDIR)

$(OBJSUBDIRS):	$(OBJDIR)
		-mkdir -p $(OBJDIR)/$@

$(LIBDIR):
		-mkdir -p $(LIBDIR)

$(BINDIR):
		-mkdir -p $(BINDIR)

clean:
		-rm -rf $(OBJDIR)/* $(SCIPLIBFILE) $(OBJSCIPLIBFILE) $(LPILIBFILE) $(MAINFILE)

lpidepend:
ifeq ($(LINKER),C)
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(LPILIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/scip/\1.o|g'\'' \
		>$(LPILIBDEP)'
endif
ifeq ($(LINKER),CPP)
		$(SHELL) -ec '$(DCXX) $(FLAGS) $(DFLAGS) $(LPILIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/scip/\1.o|g'\'' \
		>$(LPILIBDEP)'
endif

depend:		lpidepend
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(MAINSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(MAINDEP)'
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(SCIPLIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/scip/\1.o|g'\'' \
		>$(SCIPLIBDEP)'
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(OBJSCIPLIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/objscip/\1.o|g'\'' \
		>$(OBJSCIPLIBDEP)'

-include	$(MAINDEP)
-include	$(SCIPLIBDEP)
-include	$(OBJSCIPLIBDEP)
-include 	$(LPILIBDEP)

$(MAINFILE):	$(BINDIR) $(SCIPLIBFILE) $(LPILIBFILE) $(MAINXXX)
ifeq ($(LINKER),C)
		$(CC) $(MAINXXX) \
		-L$(LIBDIR) -l$(SCIPLIB) -l$(LPILIB) $(OFLAGS) $(LPSLDFLAGS) \
		$(LDFLAGS) -o $@
endif
ifeq ($(LINKER),CPP)
		$(CXX) $(MAINXXX) \
		-L$(LIBDIR) -l$(SCIPLIB) -l$(LPILIB) $(OFLAGS) $(LPSLDFLAGS) \
		$(LDFLAGS) -o $@
endif

$(SCIPLIBFILE):	$(OBJSUBDIRS) $(LIBDIR) $(SCIPLIBXXX) 
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(SCIPLIBXXX) 
		$(RANLIB) $@

$(OBJSCIPLIBFILE):	$(OBJSUBDIRS) $(LIBDIR) $(OBJSCIPLIBXXX) 
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(OBJSCIPLIBXXX) 
		$(RANLIB) $@

$(LPILIBFILE):	$(OBJSUBDIRS) $(LIBDIR) $(LPILIBXXX)
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(LPILIBXXX)
		$(RANLIB) $@

$(OBJDIR)/%.o:	$(SRCDIR)/%.c
		$(CC) $(FLAGS) $(OFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o:	$(SRCDIR)/%.cpp
		$(CXX) $(FLAGS) $(OFLAGS) $(CXXFLAGS) -c $< -o $@

# --- EOF ---------------------------------------------------------------------
