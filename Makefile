#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2004 Tobias Achterberg                              *
#*                                                                           *
#*                  2002-2004 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the SCIP Academic Licence.        *
#*                                                                           *
#*  You should have received a copy of the SCIP Academic License             *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id: Makefile,v 1.91 2004/11/01 13:48:06 bzfpfend Exp $

#@file    Makefile
#@brief   SCIP Makefile
#@author  Thorsten Koch
#@author  Tobias Achterberg

.PHONY:		depend clean lint doc test testcplex

ARCH            :=      $(shell uname -m | \
                        sed \
			-e s/sun../sparc/ \
			-e s/i.86/x86/ \
			-e s/IP../mips/ \
			-e s/9000..../hppa/ \
			-e s/00........../pwr4/)
OSTYPE		:=	$(shell uname -s | tr A-Z a-z)
MACHINENAME	:=	$(shell uname -n | tr A-Z a-z)


#-----------------------------------------------------------------------------
# default settings
#-----------------------------------------------------------------------------

OPT		=	dbg
LPS		=	cpx
COMP		=	gnu
TIME     	=  	3600
NODES           =       2100000000
MEM		=	1024
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
CFLAGS		=	
CXXFLAGS	=	
LDFLAGS		=	-lpthread -lm
ARFLAGS		=	cr
DFLAGS		=	-MM

GCCWARN		=	-Wall -W -Wpointer-arith -Wcast-align -Wwrite-strings \
			-Wmissing-declarations -Wno-unknown-pragmas \
			-Wno-unused

GXXWARN		=	-Wall -W -Wpointer-arith \
			-Wcast-align -Wwrite-strings -Wconversion \
			-Wno-unknown-pragmas \
			-Wctor-dtor-privacy -Wnon-virtual-dtor -Wreorder \
			-Woverloaded-virtual -Wsign-promo -Wsynth \
			-Wcast-qual -Wno-unused # -Wold-style-cast -Wshadow -Wundef

BASE		=	$(OSTYPE).$(ARCH).$(COMP).$(OPT)
OBJDIR		=	obj/O.$(BASE)
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
LPILIBOBJ	=	lpi_cpx.o bitencode.o memory.o
LPILIBSRC  	=	$(addprefix $(SRCDIR)/,$(LPILIBOBJ:.o=.c))
endif

ifeq ($(LPS),spx)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spxinc 
LPSLDFLAGS	=	-lsoplex.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	lpi_spx.o bitencode.o memory.o
LPILIBSRC	=	src/lpi_spx.cpp src/bitencode.c
endif

ifeq ($(LPS),spxdbg)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spxinc 
LPSLDFLAGS	=	-lsoplexdbg.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	lpi_spx.o bitencode.o memory.o
LPILIBSRC	=	src/lpi_spx.cpp src/bitencode.c
endif

ifeq ($(LPS),spx121)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/spx121inc 
LPSLDFLAGS	=	-lsoplex121.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	lpi_spx121.o bitencode.o memory.o
LPILIBSRC	=	src/lpi_spx121.cpp src/bitencode.c
endif

ifeq ($(LPS),clp)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/clpinc
LPSLDFLAGS	=	-lclp.$(OSTYPE).$(ARCH).$(COMP) -lcoin.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	lpi_clp.o bitencode.o memory.o
LPILIBSRC  	=	$(addprefix $(SRCDIR)/,$(LPILIBOBJ:.o=.cpp))
endif

ifeq ($(LPS),clpdbg)
LINKER		=	CPP
FLAGS		+=	-I$(LIBDIR)/clpinc
LPSLDFLAGS	=	-lclpdbg.$(OSTYPE).$(ARCH).$(COMP) -lcoindbg.$(OSTYPE).$(ARCH).$(COMP)
LPILIBOBJ	=	lpi_clp.o bitencode.o memory.o
LPILIBSRC  	=	$(addprefix $(SRCDIR)/,$(LPILIBOBJ:.o=.cpp))
endif

LPILIB		=	$(LPILIBNAME).$(BASE)
LPILIBFILE	=	$(LIBDIR)/lib$(LPILIB).a
LPILIBXXX	=	$(addprefix $(OBJDIR)/,$(LPILIBOBJ))
LPILIBDEP	=	src/depend.lpilib.$(LPS).$(OPT)


#-----------------------------------------------------------------------------
# SCIP Library
#-----------------------------------------------------------------------------

SCIPLIBNAME	=	scip
SCIPLIBOBJ	=	branch.o \
			buffer.o \
			clock.o \
			conflict.o \
			cons.o \
			cutpool.o \
			dialog.o \
			disp.o \
			event.o \
			heur.o \
			history.o \
			interrupt.o \
			intervalarith.o \
			lp.o \
			mem.o \
			memory.o \
			message.o \
			misc.o \
			nodesel.o \
			paramset.o \
			presol.o \
			pricestore.o \
			pricer.o \
			primal.o \
			prob.o \
			prop.o \
			reader.o \
			retcode.o \
			scip.o \
			scipdefplugins.o \
			sepa.o \
			sepastore.o \
			set.o \
			sol.o \
			solve.o \
			stat.o \
			tree.o \
			var.o \
			vbc.o \
			branch_allfullstrong.o \
			branch_fullstrong.o \
			branch_inference.o \
			branch_mostinf.o \
			branch_leastinf.o \
			branch_pscost.o \
			branch_relpscost.o \
			cons_and.o \
			cons_binpack.o \
			cons_conjunction.o \
			cons_eqknapsack.o \
			cons_integral.o \
			cons_invarknapsack.o \
			cons_knapsack.o \
			cons_linear.o \
			cons_logicor.o \
			cons_or.o \
			cons_setppc.o \
			cons_varbound.o \
			cons_xor.o \
			dialog_default.o \
			disp_default.o \
			heur_coefdiving.o \
			heur_feaspump.o \
			heur_fixandinfer.o \
			heur_fracdiving.o \
			heur_guideddiving.o \
			heur_linesearchdiving.o \
			heur_objpscostdiving.o \
			heur_pscostdiving.o \
			heur_rootsoldiving.o \
			heur_rounding.o \
			heur_simplerounding.o \
			nodesel_bfs.o \
			nodesel_dfs.o \
			nodesel_restartdfs.o \
			presol_dualfix.o \
			presol_trivial.o \
			prop_pseudoobj.o \
			reader_cnf.o \
			reader_mps.o \
			sepa_cmir.o \
			sepa_gomory.o \
			sepa_intobj.o \
			sepa_strongcg.o

SCIPLIB		=	$(SCIPLIBNAME).$(BASE)
SCIPLIBFILE	=	$(LIBDIR)/lib$(SCIPLIB).a
SCIPLIBXXX	=	$(addprefix $(OBJDIR)/,$(SCIPLIBOBJ))
SCIPLIBSRC	=	$(addprefix $(SRCDIR)/,$(SCIPLIBOBJ:.o=.c))
SCIPLIBDEP	=	src/depend.sciplib.$(OPT)


#-----------------------------------------------------------------------------
# Objective SCIP Library
#-----------------------------------------------------------------------------

OBJSCIPLIBNAME	=	objscip
OBJSCIPLIBOBJ	=	objbranchrule.o \
			objconshdlr.o \
			objheur.o \
			objnodesel.o \
			objpresol.o \
			objpricer.o \
			objprobdata.o \
			objreader.o \
			objsepa.o \
			objvardata.o

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
endif
ifeq ($(LINKER),CPP)
MAINOBJ		=	cppmain.o
MAINSRC		=	$(addprefix $(SRCDIR)/,$(MAINOBJ:.o=.cpp))
endif

MAIN		=	$(MAINNAME).$(BASE).$(LPS)
MAINFILE	=	$(BINDIR)/$(MAIN)
MAINXXX		=	$(addprefix $(OBJDIR)/,$(MAINOBJ))
MAINDEP		=	src/depend.main.$(OPT)


#-----------------------------------------------------------------------------
# Rules
#-----------------------------------------------------------------------------

all:            $(SCIPLIBFILE) $(OBJSCIPLIBFILE) $(LPILIBFILE) $(MAINFILE)

lint:		$(SCIPLIBSRC) $(OBJSCIPLIBSRC) $(LPILIBSRC) $(MAINSRC)
		$(LINT) lint/$(MAINNAME).lnt -os\(lint.out\) \
		$(FLAGS) -UNDEBUG $^

doc:		
		cd doc; $(DOXY) $(MAINNAME).dxy

test:		
		cd check; \
		/bin/sh ./check.sh $(TEST) $(MAINFILE) $(SETTINGS) $(MAIN).$(MACHINENAME) $(TIME) $(NODES) $(MEM);

testcplex:		
		cd check; \
		/bin/sh ./check_cplex.sh $(TEST) $(OSTYPE).$(ARCH).$(MACHINENAME) $(TIME) $(NODES) $(MEM);

$(OBJDIR):	
		-mkdir -p $(OBJDIR)

$(LIBDIR):
		-mkdir -p $(LIBDIR)

$(BINDIR):
		-mkdir -p $(BINDIR)

clean:
		-rm -rf $(OBJDIR)/* $(SCIPLIBFILE) $(OBJSCIPLIBFILE) $(LPILIBFILE) $(MAINFILE)

depend:
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(MAINSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(MAINDEP)'
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(SCIPLIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(SCIPLIBDEP)'
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(OBJSCIPLIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(OBJSCIPLIBDEP)'
ifeq ($(LINKER),C)
		$(SHELL) -ec '$(DCC) $(FLAGS) $(DFLAGS) $(LPILIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(LPILIBDEP)'
endif
ifeq ($(LINKER),CPP)
		$(SHELL) -ec '$(DCXX) $(FLAGS) $(DFLAGS) $(LPILIBSRC) \
		| sed '\''s|^\([0-9A-z\_]\{1,\}\)\.o|$$\(OBJDIR\)/\1.o|g'\'' \
		>$(LPILIBDEP)'
endif

-include	$(MAINDEP)
-include	$(SCIPLIBDEP)
-include	$(OBJSCIPLIBDEP)
-include 	$(LPILIBDEP)

$(MAINFILE):	$(OBJDIR) $(BINDIR) $(SCIPLIBFILE) $(LPILIBFILE) $(MAINXXX)
ifeq ($(LINKER),C)
		$(CC) $(MAINXXX) \
		-L$(LIBDIR) -l$(SCIPLIB) -l$(LPILIB) $(LPSLDFLAGS) \
		$(LDFLAGS) -o $@
endif
ifeq ($(LINKER),CPP)
		$(CXX) $(MAINXXX) \
		-L$(LIBDIR) -l$(SCIPLIB) -l$(LPILIB) $(LPSLDFLAGS) \
		$(LDFLAGS) -o $@
endif

$(SCIPLIBFILE):	$(OBJDIR) $(LIBDIR) $(SCIPLIBXXX) 
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(SCIPLIBXXX) 
		$(RANLIB) $@

$(OBJSCIPLIBFILE):	$(OBJDIR) $(LIBDIR) $(OBJSCIPLIBXXX) 
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(OBJSCIPLIBXXX) 
		$(RANLIB) $@

$(LPILIBFILE):	$(OBJDIR) $(LIBDIR) $(LPILIBXXX)
		-rm -f $@
		$(AR) $(ARFLAGS) $@ $(LPILIBXXX)
		$(RANLIB) $@

$(OBJDIR)/%.o:	$(SRCDIR)/%.c
		$(CC) $(FLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o:	$(SRCDIR)/%.cpp
		$(CXX) $(FLAGS) $(CXXFLAGS) -c $< -o $@

# --- EOF ---------------------------------------------------------------------
