#!/bin/gawk -f
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2007 Tobias Achterberg                              *
#*                                                                           *
#*                  2002-2007 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id: check.awk,v 1.64 2008/01/15 11:09:57 bzfpfend Exp $
#
#@file    check.awk
#@brief   SCIP Check Report Generator
#@author  Thorsten Koch
#@author  Tobias Achterberg
#@author  Alexander Martin
#@author  Timo Berthold
function abs(x)
{
   return x < 0 ? -x : x;
}
function min(x,y)
{
   return (x) < (y) ? (x) : (y);
}
function max(x,y)
{
   return (x) > (y) ? (x) : (y);
}
BEGIN {
   timegeomshift = 10.0;
   nodegeomshift = 100.0;
   sblpgeomshift = 0.0;
   pavshift = 1.0;
   onlyinsolufile = 0;  # should only instances be reported that are included in the .solu file?
   onlyintestfile = 0;  # should only instances be reported that are included in the .test file?  TEMPORARY HACK!
   onlypresolvereductions = 0;  # should only instances with presolve reductions be shown?
   useshortnames = 1;   # should problem name be truncated to fit into column?

   printf("\\documentclass[leqno]{article}\n")                      >TEXFILE;
   printf("\\usepackage{a4wide}\n")                                 >TEXFILE;
   printf("\\usepackage{amsmath,amsfonts,amssymb,booktabs}\n")      >TEXFILE;
   printf("\\pagestyle{empty}\n\n")                                 >TEXFILE;
   printf("\\begin{document}\n\n")                                  >TEXFILE;
   printf("\\begin{table}[p]\n")                                    >TEXFILE;
   printf("\\begin{center}\n")                                      >TEXFILE;
   printf("\\setlength{\\tabcolsep}{2pt}\n")                        >TEXFILE;
   printf("\\newcommand{\\g}{\\raisebox{0.25ex}{\\tiny $>$}}\n")    >TEXFILE;
   printf("\\begin{tabular*}{\\textwidth}{@{\\extracolsep{\\fill}}lrrrrrrr@{}}\n") >TEXFILE;
   printf("\\toprule\n")                                         >TEXFILE;
   printf("Name                &  Conss &   Vars &     Dual Bound &   Primal Bound &  Gap\\%% &     Nodes &     Time \\\\\n") > TEXFILE;
   printf("\\midrule\n")                                         >TEXFILE;
   
   printf("------------------+------+--- Original --+-- Presolved --+----------------+----------------+------+--------+-------+-------+-------\n");
   printf("Name              | Type | Conss |  Vars | Conss |  Vars |   Dual Bound   |  Primal Bound  | Gap%% |  Iters | Nodes |  Time |       \n");
   printf("------------------+------+-------+-------+-------+-------+----------------+----------------+------+--------+-------+-------+-------\n");

   nprobs = 0;
   sbab = 0;
   slp = 0;
   ssim = 0;
   ssblp = 0;
   stottime = 0.0;
   nodegeom = 0.0;
   timegeom = 0.0;
   sblpgeom = 0.0;
   conftimegeom = 0.0;
   basictimegeom = 0.0;
   overheadtimegeom = 0.0;
   shiftednodegeom = nodegeomshift;
   shiftedtimegeom = timegeomshift;
   shiftedsblpgeom = sblpgeomshift;
   shiftedconftimegeom = timegeomshift;
   shiftedbasictimegeom = timegeomshift;
   shiftedoverheadtimegeom = timegeomshift;
   timeouttime = 0.0;
   timeouts = 0;
   failtime = 0.0;
   fail = 0;
   pass = 0;
   settings = "default";
   conftottime = 0.0;
   overheadtottime = 0.0;
   timelimit = 0.0;
}
/^IP\// { # TEMPORARY HACK to parse .test files
   intestfile[$1] = 1;
}
/=opt=/  {  # get optimum
   if( NF >= 3 ) {
      solstatus[$2] = "opt";
      sol[$2] = $3;
   }
   else
      solstatus[$2] = "feas";
}
/=inf=/  { solstatus[$2] = "inf"; sol[$2] = 0.0; } # problem infeasible
/=best=/ { solstatus[$2] = "best"; sol[$2] = $3; } # get best known solution value
/=unkn=/ { solstatus[$2] = "unkn"; }               # no feasible solution known
/^@01/ { 
   filename = $2;

   n  = split ($2, a, "/");
   m = split(a[n], b, ".");
   prob = b[1];
   if( b[m] == "gz" || b[m] == "z" || b[m] == "GZ" || b[m] == "Z" )
      m--;
   for( i = 2; i < m; ++i )
      prob = prob "." b[i];

   if( useshortnames && length(prob) > 18 )
      shortprob = substr(prob, length(prob)-17, 18);
   else
      shortprob = prob;

   # Escape _ for TeX
   n = split(prob, a, "_");
   pprob = a[1];
   for( i = 2; i <= n; i++ )
      pprob = pprob "\\_" a[i];
   vars = 0;
   binvars = 0;
   intvars = 0;
   implvars = 0;
   contvars = 0;
   cons = 0;
   origvars = 0;
   origcons = 0;
   timeout = 0;
   feasible = 1;
   pb = +1e20;
   db = -1e20;
   simpiters = 0;
   bbnodes = 0;
   primlps = 0;
   primiter = 0;
   duallps = 0;
   dualiter = 0;
   sblps = 0;
   sbiter = 0;
   tottime = 0.0;
   inconflict = 0;
   inconstime = 0;
   confclauses = 0;
   confliterals = 0.0;
   conftime = 0.0;
   overheadtime = 0.0;
   aborted = 1;
   readerror = 0;
   gapreached = 0;
   sollimitreached = 0;
   memlimitreached = 0;
   timelimit = 0.0;
   starttime = 0.0;
   endtime = 0.0;
   inoriginalprob = 1;
}
/@03/ { starttime = $2; }
/@04/ { endtime = $2; }
/^SCIP> SCIP> / { $0 = substr($0, 13, length($0)-12); }
/^SCIP> / { $0 = substr($0, 7, length($0)-6); }
/^loaded parameter file/ { settings = $4; sub(/<.*settings\//, "", settings); sub(/\.set>/, "", settings); }
/^parameter <limits\/time> set to/ { timelimit = $5; }
#
# conflict analysis
#
/^Conflict Analysis  :/ { inconflict = 1; }
/^  propagation      :/ {
   if( inconflict == 1 )
   {
      conftime += $3; #confclauses += $5 + $7; confliterals += $5 * $6 + $7 * $8;
   }
}
/^  infeasible LP    :/ {
   if( inconflict == 1 )
   {
      conftime += $4; #confclauses += $6 + $8; confliterals += $6 * $7 + $8 * $9;
   }
}
/^  strong branching :/ {
   if( inconflict == 1 )
   {
      conftime += $4; #confclauses += $6 + $8; confliterals += $6 * $7 + $8 * $9;
   }
}
/^  pseudo solution  :/ {
   if( inconflict == 1 )
   {
      conftime += $4; #confclauses += $6 + $8; confliterals += $6 * $7 + $8 * $9;
   }
}
/^  applied globally :/ {
   if( inconflict == 1 )
   {
      confclauses += $7; confliterals += $7 * $8;
   }
}
/^  applied locally  :/ {
   if( inconflict == 1 )
   {
      confclauses += $7; confliterals += $7 * $8;
   }
}
/^Separators         :/ { inconflict = 0; }
/^Constraint Timings :/ { inconstime = 1; }
#/^  logicor          :/ { if( inconstime == 1 ) { overheadtime += $3; } }
/^Propagators        :/ { inconstime = 0; }
/^  switching time   :/ { overheadtime += $4; }
#
# problem size
#
/^Presolved Problem  :/ { inoriginalprob = 0; }
/^  Variables        :/ {
   if( inoriginalprob )
      origvars = $3;
   else
   {
      vars = $3;
      intvars = $6;
      implvars = $8;
      contvars = $11;
      binvars = vars - intvars - implvars - contvars;
   }
}
/^  Constraints      :/ {
   if( inoriginalprob )
      origcons = $3;
   else
      cons = $3;
}
#
# solution
#
/^Original Problem   : no problem exists./ { readerror = 1; }
/^SCIP Status        :/ { aborted = 0; }
/solving was interrupted/  { timeout = 1; }
/gap limit reached/ { gapreached = 1; }
/solution limit reached/ { sollimitreached = 1; }
/memory limit reached/ { memlimitreached = 1; }
/problem is solved/    { timeout = 0; }
/^  Primal Bound     :/ {
   if( $4 == "infeasible" )
   {
      pb = 1e+20;
      db = 1e+20;
      feasible = 0;
   }
   else if( $4 == "-" )
   {
      pb = 1e+20;
      feasible = 0;
   }
   else
      pb = $4;
}
/^  Dual Bound       :/ { 
   if( $4 != "-" ) 
      db = $4;
}
#
# iterations
#
/^  primal LP        :/ { simpiters += $6; }
/^  dual LP          :/ { simpiters += $6; }
/^  barrier LP       :/ { simpiters += $6; }
/^  nodes \(total\)    :/ { bbnodes = $4 }
/^  primal LP        :/ { primlps = $5; primiter = $6; }
/^  dual LP          :/ { duallps = $5; dualiter = $6; }
/^  strong branching :/ { sblps = $5; sbiter = $6; }
#
# time
#
/^Solving Time       :/ { tottime = $4 }
#
# Output
#
/^=ready=/ {
   if( (!onlyinsolufile || solstatus[prob] != "") &&
      (!onlyintestfile || intestfile[filename]) )
   {
      nprobs++;

      optimal = 0;
      markersym = "\\g";
      if( abs(pb - db) < 1e-06 )
      {
         gap = 0.0;
         optimal = 1;
         markersym = "  ";
      }
      else if( abs(db) < 1e-06 )
         gap = -1.0;
      else if( pb*db < 0.0 )
         gap = -1.0;
      else if( abs(db) >= 1e+20 )
         gap = -1.0;
      else if( abs(pb) >= 1e+20 )
         gap = -1.0;
      else
         gap = 100.0*abs((pb-db)/db);
      if( gap < 0.0 )
         gapstr = "  --  ";
      else if( gap < 1e+04 )
         gapstr = sprintf("%6.1f", gap);
      else
         gapstr = " Large";

      if( vars == 0 )
         probtype = "--";
      else if( binvars == 0 && intvars == 0 )
         probtype = "LP";
      else if( contvars == 0 )
      {
         if( intvars == 0 && implvars == 0 )
            probtype = "BP";
         else
            probtype = "IP";
      }
      else
      {
         if( intvars == 0 )
            probtype = "MBP";
         else
            probtype = "MIP";
      }

      if( aborted && endtime - starttime > timelimit && timelimit > 0.0 )
      {
         timeout = 1;
         aborted = 0;
         tottime = endtime - starttime;
      }
      else if( gapreached || sollimitreached )
         timeout = 0;
      if( aborted && tottime == 0.0 )
         tottime = timelimit;
      if( timelimit > 0.0 )
         tottime = min(tottime, timelimit);

      lps = primlps + duallps;
      simplex = primiter + dualiter;
      stottime += tottime;
      sbab += bbnodes;
      slp += lps;
      ssim += simplex;
      ssblp += sblps;
      conftottime += conftime;
      overheadtottime += overheadtime;
      basictime = tottime - conftime - overheadtime;

      nodegeom = nodegeom^((nprobs-1)/nprobs) * max(bbnodes, 1.0)^(1.0/nprobs);
      sblpgeom = sblpgeom^((nprobs-1)/nprobs) * max(sblps, 1.0)^(1.0/nprobs);
      timegeom = timegeom^((nprobs-1)/nprobs) * max(tottime, 1.0)^(1.0/nprobs);
      conftimegeom = conftimegeom^((nprobs-1)/nprobs) * max(conftime, 1.0)^(1.0/nprobs);
      overheadtimegeom = overheadtimegeom^((nprobs-1)/nprobs) * max(overheadtime, 1.0)^(1.0/nprobs);
      basictimegeom = basictimegeom^((nprobs-1)/nprobs) * max(basictime, 1.0)^(1.0/nprobs);

      shiftednodegeom = shiftednodegeom^((nprobs-1)/nprobs) * max(bbnodes+nodegeomshift, 1.0)^(1.0/nprobs);
      shiftedsblpgeom = shiftedsblpgeom^((nprobs-1)/nprobs) * max(sblps+sblpgeomshift, 1.0)^(1.0/nprobs);
      shiftedtimegeom = shiftedtimegeom^((nprobs-1)/nprobs) * max(tottime+timegeomshift, 1.0)^(1.0/nprobs);
      shiftedconftimegeom = shiftedconftimegeom^((nprobs-1)/nprobs) * max(conftime+timegeomshift, 1.0)^(1.0/nprobs);
      shiftedoverheadtimegeom = shiftedoverheadtimegeom^((nprobs-1)/nprobs) * max(overheadtime+timegeomshift, 1.0)^(1.0/nprobs);
      shiftedbasictimegeom = shiftedbasictimegeom^((nprobs-1)/nprobs) * max(basictime+timegeomshift, 1.0)^(1.0/nprobs);

      status = "";
      if( readerror )
      {
         status = "readerror";
         failtime += tottime;
         fail++;
      }
      else if( aborted )
      {
         status = "abort";
         failtime += tottime;
         fail++;
      }
      else if( solstatus[prob] == "opt" )
      {
         if( !sollimitreached && !gapreached && !timeout )
            wronganswer = (pb - db > 1e-4 || abs(pb - sol[prob]) > 1e-5*max(abs(pb),1.0));
         else if( pb >= db )
            wronganswer = (db > sol[prob] + 1e-5*max(abs(sol[prob]),1.0) ||
               pb < sol[prob] - 1e-5*max(abs(sol[prob]),1.0));
         else
            wronganswer = (pb > sol[prob] + 1e-5*max(abs(sol[prob]),1.0) ||
               db < sol[prob] - 1e-5*max(abs(sol[prob]),1.0));

         if( wronganswer )
         {
            status = "fail";
            failtime += tottime;
            fail++;
         }
         else if( timeout )
         {
            status = "timeout";
            timeouttime += tottime;
            timeouts++;
         }
         else
         {
            status = "ok";
            pass++;
         }
      }
      else if( solstatus[prob] == "feas" || solstatus[prob] == "inf" )
      {
         if( timeout )
            wronganswer = (feasible && solstatus[prob] == "inf");
         else
            wronganswer = (feasible != (solstatus[prob] == "feas"));

         if( wronganswer )
         {
            status = "fail";
            failtime += tottime;
            fail++;
         }
         else if( timeout )
         {
            status = "timeout";
            timeouttime += tottime;
            timeouts++;
         }
         else
         {
            status = "ok";
            pass++;
         }
      }
      else if( solstatus[prob] == "best" )
      {
         if( db > sol[prob] + 1e-4 )
         {
            status = "fail";
            failtime += tottime;
            fail++;
         }
         else if( timeout )
         {
            status = "timeout";
            timeouttime += tottime;
            timeouts++;
         }
         else
            status = "unknown";
      }
      else
      {
         if( timeout )
         {
            status = "timeout";
            timeouttime += tottime;
            timeouts++;
         }
         else
            status = "unknown";
      }

      if( !onlypresolvereductions || origcons > cons || origvars > vars )
      {
         printf("%-19s & %6d & %6d & %16.9g & %16.9g & %6s &%s%8d &%s%7.1f \\\\\n",
                pprob, cons, vars, db, pb, gapstr, markersym, bbnodes, markersym, tottime) >TEXFILE;
         printf("%-19s %-5s %7d %7d %7d %7d %16.9g %16.9g %6s %8d %7d %7.1f %s\n",
                shortprob, probtype, origcons, origvars, cons, vars, db, pb, gapstr, simpiters, bbnodes, tottime, status);
      }

      # PAVER output: see http://www.gamsworld.org/performance/paver/pprocess_submit.htm
      if( solstatus[prob] == "opt" || solstatus[prob] == "feas" )
         modelstat = 1;
      else if( solstatus[prob] == "inf" )
         modelstat = 1;
      else if( solstatus[prob] == "best" )
         modelstat = 8;
      else
         modelstat = 1;
      if( status == "ok" || status == "unknown" )
         solverstat = 1;
      else if( status == "timeout" )
         solverstat = 3;
      else
         solverstat = 10;
      pavprob = prob;
      if( length(pavprob) > 25 )
         pavprob = substr(pavprob, length(pavprob)-24,25);
      printf("%s,MIP,SCIP_%s,0,%d,%d,%g,%g\n", pavprob, settings, modelstat, solverstat, pb, tottime+pavshift) > PAVFILE;
   }
}
END {
   shiftednodegeom -= nodegeomshift;
   shiftedsblpgeom -= sblpgeomshift;
   shiftedtimegeom -= timegeomshift;
   shiftedconftimegeom -= timegeomshift;
   shiftedoverheadtimegeom -= timegeomshift;
   shiftedbasictimegeom -= timegeomshift;

   printf("\\midrule\n")                                                 >TEXFILE;
   printf("%-14s (%2d) &        &        &                &                &        & %9d & %8.1f \\\\\n",
          "Total", nprobs, sbab, stottime) >TEXFILE;
   printf("%-14s      &        &        &                &                &        & %9d & %8.1f \\\\\n",
          "Geom. Mean", nodegeom, timegeom) >TEXFILE;
   printf("%-14s      &        &        &                &                &        & %9d & %8.1f \\\\\n",
          "Shifted Geom.", shiftednodegeom, shiftedtimegeom) >TEXFILE;
   printf("------------------+------+-------+-------+-------+-------+----------------+----------------+------+--------+-------+-------+-------\n");
   printf("\n");
   printf("------------------------------[Nodes]---------------[Time]------\n");
   printf("  Cnt  Pass  Time  Fail  total(k)     geom.     total     geom. \n");
   printf("----------------------------------------------------------------\n");
   printf("%5d %5d %5d %5d %9d %9.1f %9.1f %9.1f \n",
          nprobs, pass, timeouts, fail, sbab / 1000, nodegeom, stottime, timegeom);
   printf(" shifted geom. [%5d/%5.1f]      %9.1f           %9.1f \n",
          nodegeomshift, timegeomshift, shiftednodegeom, shiftedtimegeom);
   printf("----------------------------------------------------------------\n");
   printf("\\bottomrule\n")                                              >TEXFILE;
   printf("\\noalign{\\vspace{6pt}}\n")                                  >TEXFILE;
   printf("\\end{tabular*}\n")                                           >TEXFILE;
   printf("\\caption{%s}\n", settings)                                   >TEXFILE;
   printf("\\end{center}\n")                                             >TEXFILE;
   printf("\\end{table}\n")                                              >TEXFILE;
   printf("\\end{document}\n")                                           >TEXFILE;

   printf("@02 timelimit: %g\n", timelimit);
   printf("@01 SCIP:%s\n", settings);
}
