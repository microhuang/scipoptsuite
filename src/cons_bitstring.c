/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2003 Tobias Achterberg                              */
/*                            Thorsten Koch                                  */
/*                  2002-2003 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define DEBUG
/**@file   cons_bitstring.c
 * @brief  constraint handler for bitstring constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "cons_bitstring.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "bitstring"
#define CONSHDLR_DESC          "arbitrarily long integer variables represented as bit strings"
#define CONSHDLR_SEPAPRIORITY  +2000000
#define CONSHDLR_ENFOPRIORITY  - 500000
#define CONSHDLR_CHECKPRIORITY - 500000
#define CONSHDLR_SEPAFREQ             1
#define CONSHDLR_PROPFREQ             1
#define CONSHDLR_NEEDSCONS         TRUE

#define EVENTHDLR_NAME         "bitstring"
#define EVENTHDLR_DESC         "bound change event handler for bitstring constraints"

#define WORDSIZE                     16 /**< number of bits in one word of the bitstring */
#define WORDPOWER       (1 << WORDSIZE) /**< number of different values of one word (2^WORDSIZE) */



/*
 * Data structures
 */

/** constraint data for bitstring constraints */
struct ConsData
{
   VAR**            bits;               /**< binaries representing bits of the bitstring, least significant first */
   VAR**            words;              /**< integers representing words of the bitstring, least significant first */
   ROW**            rows;               /**< LP rows storing equalities for each word */
   int              nbits;              /**< number of bits */
   int              nwords;             /**< number of words: nwords = ceil(nbits/WORDSIZE) */
   unsigned int     propagated:1;       /**< is constraint already preprocessed/propagated? */
};

/** constraint handler data */
struct ConsHdlrData
{
   EVENTHDLR*       eventhdlr;          /**< event handler for bound change events */
};




/*
 * Local methods
 */

/** returns the number of bits of the given word */
static
int wordSize(
   CONSDATA*        consdata,           /**< bitstring constraint data */
   int              word                /**< word number */
   )
{
   assert(consdata != NULL);
   assert(0 <= word && word < consdata->nwords);

   if( word < consdata->nwords-1 )
      return WORDSIZE;
   else
      return consdata->nbits - (consdata->nwords-1) * WORDSIZE;
}

/** returns the number of different values the given word store (2^#bits) */
static
int wordPower(
   CONSDATA*        consdata,           /**< bitstring constraint data */
   int              word                /**< word number */
   )
{
   assert(consdata != NULL);
   assert(0 <= word && word < consdata->nwords);

   if( word < consdata->nwords-1 )
      return WORDPOWER;
   else
      return (1 << (consdata->nbits - (consdata->nwords-1) * WORDSIZE));
}

/** creates constaint handler data for bitstring constraint handler */
static
RETCODE conshdlrdataCreate(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA**   conshdlrdata        /**< pointer to store the constraint handler data */
   )
{
   assert(conshdlrdata != NULL);

   CHECK_OKAY( SCIPallocMemory(scip, conshdlrdata) );

   /* get event handler for updating linear constraint activity bounds */
   (*conshdlrdata)->eventhdlr = SCIPfindEventHdlr(scip, EVENTHDLR_NAME);
   if( (*conshdlrdata)->eventhdlr == NULL )
   {
      errorMessage("event handler for bitstring constraints not found");
      return SCIP_PLUGINNOTFOUND;
   }

   return SCIP_OKAY;
}

/** frees constaint handler data for bitstring constraint handler */
static
RETCODE conshdlrdataFree(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA**   conshdlrdata        /**< pointer the constraint handler data */
   )
{
   assert(conshdlrdata != NULL);

   SCIPfreeMemory(scip, conshdlrdata);

   return SCIP_OKAY;
}

/** creates a bitstring constraint data object along with the corresponding variables */
static
RETCODE consdataCreate(
   SCIP*            scip,               /**< SCIP data structure */
   CONSDATA**       consdata,           /**< pointer to store the bitstring constraint data */
   int              nbits               /**< number of bits in the bitstring */
   )
{
   assert(consdata != NULL);
   assert(nbits >= 1);

   /* allocate memory */
   CHECK_OKAY( SCIPallocBlockMemory(scip, consdata) );
   (*consdata)->nbits = nbits;
   (*consdata)->nwords = (nbits+WORDSIZE-1)/WORDSIZE;
   CHECK_OKAY( SCIPallocBlockMemoryArray(scip, &(*consdata)->bits, (*consdata)->nbits) );
   CHECK_OKAY( SCIPallocBlockMemoryArray(scip, &(*consdata)->words, (*consdata)->nwords) );
   (*consdata)->rows = NULL;
   (*consdata)->propagated = FALSE;

   /* clear the bits and words arrays */
   clearMemoryArray((*consdata)->bits, (*consdata)->nbits);
   clearMemoryArray((*consdata)->words, (*consdata)->nwords);

   return SCIP_OKAY;
}

/** creates variables for the bitstring and adds them to the problem */
static
RETCODE consdataCreateVars(
   SCIP*            scip,               /**< SCIP data structure */
   CONSDATA*        consdata,           /**< bitstring constraint data */
   EVENTHDLR*       eventhdlr,          /**< event handler for bound change events */
   const char*      name,               /**< prefix for variable names */
   Real             obj                 /**< objective value of bitstring variable */
   )
{
   char varname[MAXSTRLEN];
   Real bitobj;
   int i;

   assert(consdata != NULL);
   assert(name != NULL);

   /* create binary variables for bits */
   bitobj = obj;
   for( i = 0; i < consdata->nbits; ++i )
   {
      sprintf(varname, "%s_b%d", name, i);
      CHECK_OKAY( SCIPcreateVar(scip, &consdata->bits[i], varname, 0.0, 1.0, bitobj, SCIP_VARTYPE_BINARY, TRUE) );
      CHECK_OKAY( SCIPaddVar(scip, consdata->bits[i]) );
      bitobj *= 2.0;
      
      /* if we are in the transformed problem, catch bound tighten events on variable */
      if( SCIPvarIsTransformed(consdata->bits[i]) )
      {
         CHECK_OKAY( SCIPcatchVarEvent(scip, consdata->bits[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr,
                        (EVENTDATA*)consdata) );
      }
   }

   /* create integer variables for words */
   for( i = 0; i < consdata->nwords; ++i )
   {
      sprintf(varname, "%s_w%d", name, i);
      CHECK_OKAY( SCIPcreateVar(scip, &consdata->words[i], varname, 0.0, wordPower(consdata, i)-1.0, 0.0,
                     SCIP_VARTYPE_INTEGER, TRUE) );
      CHECK_OKAY( SCIPaddVar(scip, consdata->words[i]) );
      
      /* if we are in the transformed problem, catch bound tighten events on variable */
      if( SCIPvarIsTransformed(consdata->words[i]) )
      {
         CHECK_OKAY( SCIPcatchVarEvent(scip, consdata->words[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr,
                        (EVENTDATA*)consdata) );
      }
   }

   /* issue warning message, if objective value for most significant bit grew too large */
   if( ABS(obj) > SCIPinfinity(scip)/10000.0 )
   {
      char msg[MAXSTRLEN];

      sprintf(msg, "Warning! objective value %g of %d-bit string grew up to %g in last bit\n", 
         obj, consdata->nbits, bitobj/2.0);
      SCIPmessage(scip, SCIP_VERBLEVEL_MINIMAL, msg);
   }
   
   return SCIP_OKAY;
}

/** create variables in target constraint data by transforming variables of the source constraint data */
static
RETCODE consdataTransformVars(
   SCIP*            scip,               /**< SCIP data structure */
   CONSDATA*        sourcedata,         /**< bitstring constraint data of the source constraint */
   CONSDATA*        targetdata,         /**< bitstring constraint data of the target constraint */
   EVENTHDLR*       eventhdlr           /**< event handler for bound change events */
   )
{
   int i;

   assert(sourcedata != NULL);
   assert(sourcedata->bits != NULL);
   assert(sourcedata->words != NULL);
   assert(targetdata != NULL);
   assert(targetdata->bits != NULL);
   assert(targetdata->words != NULL);
   assert(sourcedata->nbits == targetdata->nbits);
   assert(sourcedata->nwords == targetdata->nwords);

   /* get transformed bit variables */
   for( i = 0; i < sourcedata->nbits; ++i )
   {
      CHECK_OKAY( SCIPgetTransformedVar(scip, sourcedata->bits[i], &targetdata->bits[i]) );
      CHECK_OKAY( SCIPcaptureVar(scip, targetdata->bits[i]) );

      /* catch bound tighten events on variable */
      assert(SCIPvarIsTransformed(targetdata->bits[i]));
      CHECK_OKAY( SCIPcatchVarEvent(scip, targetdata->bits[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr,
                     (EVENTDATA*)targetdata) );
   }

   /* get transformed word variables */
   for( i = 0; i < sourcedata->nwords; ++i )
   {
      CHECK_OKAY( SCIPgetTransformedVar(scip, sourcedata->words[i], &targetdata->words[i]) );
      CHECK_OKAY( SCIPcaptureVar(scip, targetdata->words[i]) );

      /* catch bound tighten events on variable */
      assert(SCIPvarIsTransformed(targetdata->words[i]));
      CHECK_OKAY( SCIPcatchVarEvent(scip, targetdata->words[i], SCIP_EVENTTYPE_BOUNDTIGHTENED, eventhdlr,
                     (EVENTDATA*)targetdata) );
   }

   return SCIP_OKAY;
}

/** frees a bitstring constraint data object and releases corresponding variables */
static
RETCODE consdataFree(
   SCIP*            scip,               /**< SCIP data structure */
   CONSDATA**       consdata,           /**< pointer to the bitstring constraint data */
   EVENTHDLR*       eventhdlr           /**< event handler for bound change events */
   )
{
   int i;

   assert(consdata != NULL);
   assert(*consdata != NULL);

   /* release binary variables for bits */
   for( i = 0; i < (*consdata)->nbits; ++i )
   {
      /* if we are in the transformed problem, drop bound tighten events on variable */
      if( SCIPvarIsTransformed((*consdata)->bits[i]) )
      {
         CHECK_OKAY( SCIPdropVarEvent(scip, (*consdata)->bits[i], eventhdlr, (EVENTDATA*)(*consdata)) );
      }

      /* release variable */
      CHECK_OKAY( SCIPreleaseVar(scip, &(*consdata)->bits[i]) );
   }

   /* release integer variables for words */
   for( i = 0; i < (*consdata)->nwords; ++i )
   {
      /* if we are in the transformed problem, drop bound tighten events on variable */
      if( SCIPvarIsTransformed((*consdata)->words[i]) )
      {
         CHECK_OKAY( SCIPdropVarEvent(scip, (*consdata)->words[i], eventhdlr, (EVENTDATA*)(*consdata)) );
      }

      /* release variable */
      CHECK_OKAY( SCIPreleaseVar(scip, &(*consdata)->words[i]) );
   }

   /* release rows */
   if( (*consdata)->rows != NULL )
   {
      for( i = 0; i < (*consdata)->nwords; ++i )
      {
         if( (*consdata)->rows[i] != NULL )
         {
            CHECK_OKAY( SCIPreleaseRow(scip, &(*consdata)->rows[i]) );
         }
      }
   }

   /* free memory */
   SCIPfreeBlockMemoryArray(scip, &(*consdata)->bits, (*consdata)->nbits);
   SCIPfreeBlockMemoryArray(scip, &(*consdata)->words, (*consdata)->nwords);
   SCIPfreeBlockMemoryArrayNull(scip, &(*consdata)->rows, (*consdata)->nwords);
   SCIPfreeBlockMemory(scip, consdata);

   return SCIP_OKAY;
}

/** checks given word of bitstring constraint for feasibility of given solution or actual solution */
static
RETCODE checkWord(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   int              word,               /**< word number to check */
   SOL*             sol,                /**< solution to be checked, or NULL for actual solution */
   Bool             checklprows,        /**< has bitstring constraint to be checked, if it is already in current LP? */
   int*             nviolatedbits       /**< pointer to store the number of violated bits */
   )
{
   CONSDATA* consdata;
   Real wordsol;
   Real bitsol;
   Bool wordbitisset;
   Bool bitsolisone;
   int wordsolint;
   int wordsize;
   int bitmask;
   int b;

   assert(nviolatedbits != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(0 <= word && word < consdata->nwords);

   debugMessage("checking bitstring constraint <%s> at word %d\n", SCIPconsGetName(cons), word);

   *nviolatedbits = 0;

   if( !checklprows && consdata->rows != NULL && consdata->rows[word] != NULL && SCIProwIsInLP(consdata->rows[word]) )
      return SCIP_OKAY;

   /* get the value of the word and convert it into an integer */
   wordsol = SCIPgetVarSol(scip, consdata->words[word]);
   assert(SCIPisIntegral(scip, wordsol));
   wordsolint = (int)(wordsol+0.5);
   assert(SCIPisFeasEQ(scip, wordsol, (Real)wordsolint));

   /* compare each bit in the word's solution with the value of the corresponding binary variable */
   wordsize = wordSize(consdata, word);
   bitmask = 1;
   for( b = 0; b < wordsize; ++b )
   {
      assert(0 <= bitmask && bitmask <= WORDPOWER/2);
      bitsol = SCIPgetVarSol(scip, consdata->bits[word*WORDSIZE+b]);
      assert(SCIPisIntegral(scip, bitsol));
      assert(SCIPisFeasEQ(scip, bitsol, 0.0) || SCIPisFeasEQ(scip, bitsol, 1.0));
      bitsolisone = (bitsol > 0.5);
      wordbitisset = ((wordsolint & bitmask) > 0);
      if( bitsolisone != wordbitisset )
         (*nviolatedbits)++;
      bitmask <<= 1;
   }

   /* update constraint's age */
   if( *nviolatedbits == 0 )
   {
      CHECK_OKAY( SCIPincConsAge(scip, cons) );
   }
   else
   {
      CHECK_OKAY( SCIPresetConsAge(scip, cons) );
   }

   return SCIP_OKAY;
}

/** checks all words of bitstring constraint for feasibility of given solution or actual solution */
static
RETCODE check(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   SOL*             sol,                /**< solution to be checked, or NULL for actual solution */
   Bool             checklprows,        /**< has bitstring constraint to be checked, if it is already in current LP? */
   Bool*            violated            /**< pointer to store whether the constraint is violated */
   )
{
   CONSDATA* consdata;
   int nviolatedbits;
   int w;

   assert(violated != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   *violated = FALSE;

   for( w = 0; w < consdata->nwords && !(*violated); ++w )
   {
      CHECK_OKAY( checkWord(scip, cons, w, sol, checklprows, &nviolatedbits) );
      (*violated) |= (nviolatedbits > 0);
   }

   return SCIP_OKAY;
}

/** creates an LP row for a single word in a bitstring constraint */
static
RETCODE createRow(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   int              word                /**< word number to create the row for */
   )
{
   CONSDATA* consdata;
   char rowname[MAXSTRLEN];
   Real coef;
   int bitstart;
   int bitend;
   int b;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->bits != NULL);
   assert(consdata->words != NULL);
   assert(0 <= word && word < consdata->nwords);

   /* create the rows array, if not yet existing */
   if( consdata->rows == NULL )
   {
      CHECK_OKAY( SCIPallocBlockMemoryArray(scip, &consdata->rows, consdata->nwords) );
      clearMemoryArray(consdata->rows, consdata->nwords);
   }
   assert(consdata->rows != NULL);
   assert(consdata->rows[word] == NULL);

   /* create equality  - word + 2^0*bit[0] + 2^1*bit[1] + ... + 2^(WORDSIZE-1)*bit[WORDSIZE-1] == 0 */
   sprintf(rowname, "c_%s", SCIPvarGetName(consdata->words[word]));
   CHECK_OKAY( SCIPcreateRow(scip, &consdata->rows[word], rowname, 0, NULL, NULL, 0.0, 0.0,
                  SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons), SCIPconsIsRemoveable(cons)) );
   
   CHECK_OKAY( SCIPaddVarToRow(scip, consdata->rows[word], consdata->words[word], -1.0) );
   bitstart = word*WORDSIZE;
   bitend = bitstart + wordSize(consdata, word);
   assert(bitstart < bitend);
   coef = 1.0;
   for( b = bitstart; b < bitend; ++b )
   {
      CHECK_OKAY( SCIPaddVarToRow(scip, consdata->rows[word], consdata->bits[b], coef) );
      coef *= 2.0;
   }
   assert(SCIPisEQ(scip, coef, (Real)(wordPower(consdata, word))));

   return SCIP_OKAY;
}

/** adds bitstring constraint as cuts to the LP */
static
RETCODE addCut(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   int              word,               /**< word number to add the cut for */
   Real             violation           /**< absolute violation of the constraint */
   )
{
   CONSDATA* consdata;
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(0 <= word && word < consdata->nwords);

   /* create the selected row, if not yet existing */
   if( consdata->rows == NULL || consdata->rows[word] == NULL )
   {
      /* convert consdata object into LP row */
      CHECK_OKAY( createRow(scip, cons, word) );
   }
   assert(consdata->rows != NULL);
   assert(consdata->rows[word] != NULL);
   assert(!SCIProwIsInLP(consdata->rows[word]));
   
   /* insert LP row as cut */
   CHECK_OKAY( SCIPaddCut(scip, consdata->rows[word], 
                  violation/SCIProwGetNorm(consdata->rows[word])/(SCIProwGetNNonz(consdata->rows[word])+1)) );

   return SCIP_OKAY;
}

/** separates bitstring constraint: adds each word of bitstring constraint as cut, if violated by current LP solution */
static
RETCODE separate(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   RESULT*          result              /**< pointer to store result of separation */
   )
{
   CONSDATA* consdata;
   int w;

   assert(result != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   for( w = 0; w < consdata->nwords; ++w )
   {
      int nviolatedbits;

      CHECK_OKAY( checkWord(scip, cons, w, NULL, FALSE, &nviolatedbits) );
      
      if( nviolatedbits > 0 )
      {
         /* insert LP row as cut */
         CHECK_OKAY( addCut(scip, cons, w, (Real)nviolatedbits) );
         *result = SCIP_SEPARATED;
      }
   }

   return SCIP_OKAY;
}

/** propagates domains of variables of a single word in a bitstring constraint */
static
RETCODE propagateWord(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   int              word,               /**< word number to add the cut for */
   int*             nfixedvars,         /**< pointer to add up the number of fixed variables */
   int*             nchgbds             /**< pointer to add up the number of fixed bounds */
   )
{
   CONSDATA* consdata;
   VAR* wordvar;
   VAR* bitvar;
   Real lb;
   Real ub;
   Bool lbbitset;
   Bool ubbitset;
   int wordsize;
   int bitstart;
   int bitend;
   int fixedval;
   int bitval;
   int nfixedbits;
   int unfixedpower;
   int lbint;
   int ubint;
   int b;

   assert(nfixedvars != NULL);
   assert(nchgbds != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->bits != NULL);
   assert(consdata->words != NULL);

   /* beginning with the most significant bit, check for fixed bits */
   wordsize = wordSize(consdata, word);
   bitstart = word*WORDSIZE;
   bitend = bitstart + wordsize;
   assert(bitstart < bitend);
   fixedval = 0;
   nfixedbits = 0;
   bitval = wordPower(consdata, word);
   for( b = bitend-1; b >= bitstart; --b )
   {
      bitval >>= 1;
      assert(bitval == (1 << (b - bitstart)));
      bitvar = consdata->bits[b];
      lb = SCIPvarGetLbLocal(bitvar);
      ub = SCIPvarGetUbLocal(bitvar);
      assert(SCIPisEQ(scip, lb, 0.0) || SCIPisEQ(scip, lb, 1.0));
      assert(SCIPisEQ(scip, ub, 0.0) || SCIPisEQ(scip, ub, 1.0));
      assert(SCIPisLE(scip, lb, ub));

      if( lb > 0.5 )
      {
         fixedval += bitval;
         nfixedbits++;
      }
      else if( ub < 0.5 )
         nfixedbits++;
      else
         break;
   }
   assert(nfixedbits <= wordsize);
   
   /* get the word along with its bounds */
   wordvar = consdata->words[word];
   lb = SCIPvarGetLbLocal(wordvar);
   ub = SCIPvarGetUbLocal(wordvar);

   /* update the bounds of the word respectively: if the most significant k bits of an n-bit word are fixed,
    * the value of the word must be in  [fixedval, fixedval+2^(n-k)-1]
    */
   if( nfixedbits > 0 )
   {
      unfixedpower = (1 << (wordsize-nfixedbits));
      if( lb < fixedval - 0.5 )
      {
         /* lower bound can be adjusted to fixedval */
         debugMessage("bitstring <%s>: adjusting lower bound of word %d <%s>: [%g,%g] -> [%g,%g]\n",
            SCIPconsGetName(cons), word, SCIPvarGetName(wordvar), lb, ub, (Real)fixedval, ub);
         lb = fixedval;
         CHECK_OKAY( SCIPchgVarLb(scip, wordvar, lb) );
         (*nchgbds)++;
      }
      if( ub > fixedval + unfixedpower - 1 + 0.5 )
      {
         /* upper bound can be adjusted to fixedval + unfixedpower - 1 */
         debugMessage("bitstring <%s>: adjusting upper bound of word %d <%s>: [%g,%g] -> [%g,%g]\n",
            SCIPconsGetName(cons), word, SCIPvarGetName(wordvar), lb, ub, lb, (Real)(fixedval + unfixedpower - 1));
         ub = fixedval + unfixedpower - 1;
         CHECK_OKAY( SCIPchgVarUb(scip, wordvar, ub) );
         (*nchgbds)++;
      }
   }

   /* fix some more bits: if lb and ub are equal in more than nfixedbits bits, the corresponding bit variables
    * can be fixed
    */
   lbint = (int)(lb+0.5);
   ubint = (int)(ub+0.5);
   assert(bitval == (1 << (wordsize-nfixedbits-1)));
   for( b = bitend-1 - nfixedbits; b >= bitstart; --b )
   {
      assert(bitval == (1 << (b - bitstart)));
      lbbitset = ((lbint & bitval) > 0);
      ubbitset = ((ubint & bitval) > 0);
      if( lbbitset == ubbitset )
      {
         Bool infeasible;

         /* both bounds are identical in this bit: fix the corresponding bit variables */
         if( lbbitset )
         {
            debugMessage("bitstring <%s>: fixing bit %d <%s> to 1.0 (word %d <%s>: [%g,%g])\n",
               SCIPconsGetName(cons), b, SCIPvarGetName(consdata->bits[b]), word, SCIPvarGetName(wordvar), lb, ub);
            CHECK_OKAY( SCIPfixVar(scip, consdata->bits[b], 1.0, &infeasible) );
         }
         else
         {
            debugMessage("bitstring <%s>: fixing bit %d <%s> to 0.0 (word %d <%s>: [%g,%g])\n",
               SCIPconsGetName(cons), b, SCIPvarGetName(consdata->bits[b]), word, SCIPvarGetName(wordvar), lb, ub);
            CHECK_OKAY( SCIPfixVar(scip, consdata->bits[b], 0.0, &infeasible) );
         }
         assert(!infeasible);
         (*nfixedvars)++;
         bitval >>= 1;
      }
      else
      {
         /* the bounds differ in this bit: break the fixing loop */
         break;
      }
   }

   return SCIP_OKAY;
}

/** propagates domains of variables of a bitstring constraint */
static
RETCODE propagate(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< bitstring constraint */
   int*             nfixedvars,         /**< pointer to add up the number of fixed variables */
   int*             nchgbds             /**< pointer to add up the number of fixed bounds */
   )
{
   CONSDATA* consdata;
   int w;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   /* check, if the constraint is already propagated */
   if( consdata->propagated )
      return SCIP_OKAY;

   /* propagate all words in the bitstring */
   for( w = 0; w < consdata->nwords; ++w )
   {
      CHECK_OKAY( propagateWord(scip, cons, w, nfixedvars, nchgbds) );
   }

   /* mark the constraint propagated */
   consdata->propagated = TRUE;

   return SCIP_OKAY;
}




/*
 * Callback methods of constraint handler
 */

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
DECL_CONSFREE(consFreeBitstring)
{
   CONSHDLRDATA* conshdlrdata;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* free constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   conshdlrdataFree(scip, &conshdlrdata);

   SCIPconshdlrSetData(conshdlr, NULL);

   return SCIP_OKAY;
}


/** initialization method of constraint handler (called when problem solving starts) */
#define consInitBitstring NULL


/** deinitialization method of constraint handler (called when problem solving exits) */
#define consExitBitstring NULL


/** frees specific constraint data */
static
DECL_CONSDELETE(consDeleteBitstring)
{
   CONSHDLRDATA* conshdlrdata;

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   /* free constraint data */
   CHECK_OKAY( consdataFree(scip, consdata, conshdlrdata->eventhdlr) );

   return SCIP_OKAY;
}


/** transforms constraint data into data belonging to the transformed problem */ 
static
DECL_CONSTRANS(consTransBitstring)
{
   CONSHDLRDATA* conshdlrdata;
   CONSDATA* sourcedata;
   CONSDATA* targetdata;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   /* get source constraint data */
   sourcedata = SCIPconsGetData(sourcecons);
   assert(sourcedata != NULL);
   assert(sourcedata->rows == NULL);  /* in original problem, there cannot be LP rows */

   /* create constraint data for target constraint */
   CHECK_OKAY( consdataCreate(scip, &targetdata, sourcedata->nbits) );
   CHECK_OKAY( consdataTransformVars(scip, sourcedata, targetdata, conshdlrdata->eventhdlr) );

   /* create target constraint */
   CHECK_OKAY( SCIPcreateCons(scip, targetcons, SCIPconsGetName(sourcecons), conshdlr, targetdata,
                  SCIPconsIsInitial(sourcecons), SCIPconsIsSeparated(sourcecons), SCIPconsIsEnforced(sourcecons),
                  SCIPconsIsChecked(sourcecons), SCIPconsIsPropagated(sourcecons),
                  SCIPconsIsLocal(sourcecons), SCIPconsIsModifiable(sourcecons), SCIPconsIsRemoveable(sourcecons)) );

   return SCIP_OKAY;
}


/** LP initialization method of constraint handler */
static
DECL_CONSINITLP(consInitlpBitstring)
{
   int c;
   int w;

   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   for( c = 0; c < nconss; ++c )
   {
      if( SCIPconsIsInitial(conss[c]) )
      {
         CONSDATA* consdata;

         consdata = SCIPconsGetData(conss[c]);
         assert(consdata != NULL);

         /** add rows for all words in the bitstring constraint */
         for( w = 0; w < consdata->nwords; ++w )
         {
            CHECK_OKAY( addCut(scip, conss[c], w, 0.0) );
         }
      }
   }

   return SCIP_OKAY;
}


/** separation method of constraint handler */
static
DECL_CONSSEPA(consSepaBitstring)
{
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   *result = SCIP_DIDNOTFIND;

   /* step 1: check all useful bitstring constraints for feasibility */
   for( c = 0; c < nusefulconss; ++c )
   {
      /*debugMessage("separating bitstring constraint <%s>\n", SCIPconsGetName(conss[c]));*/
      CHECK_OKAY( separate(scip, conss[c], result) );
   }

   /* step 2: if no cuts were found and we are in the root node, check remaining bitstring constraints for feasibility */
   if( SCIPgetActDepth(scip) == 0 )
   {
      for( c = nusefulconss; c < nconss && *result == SCIP_DIDNOTFIND; ++c )
      {
         CHECK_OKAY( separate(scip, conss[c], result) );
      }
   }

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for LP solutions */
static
DECL_CONSENFOLP(consEnfolpBitstring)
{
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   /* check for violated constraints
    * LP is processed at current node -> we can add violated bitstring constraints to the LP */

   *result = SCIP_FEASIBLE;

   /* step 1: check all useful bitstring constraints for feasibility */
   for( c = 0; c < nusefulconss; ++c )
   {
      CHECK_OKAY( separate(scip, conss[c], result) );
   }
   if( *result != SCIP_FEASIBLE )
      return SCIP_OKAY;

   /* step 2: check all obsolete bitstring constraints for feasibility */
   for( c = nusefulconss; c < nconss && *result == SCIP_FEASIBLE; ++c )
   {
      CHECK_OKAY( separate(scip, conss[c], result) );
   }

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
DECL_CONSENFOPS(consEnfopsBitstring)
{
   Bool violated;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   /* if the solution is infeasible anyway due to objective value, skip the enforcement */
   if( objinfeasible )
   {
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   /* check all bitstring constraints for feasibility */
   violated = FALSE;
   for( c = 0; c < nconss && !violated; ++c )
   {
      CHECK_OKAY( check(scip, conss[c], NULL, TRUE, &violated) );
   }

   if( violated )
      *result = SCIP_INFEASIBLE;
   else
      *result = SCIP_FEASIBLE;

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
DECL_CONSCHECK(consCheckBitstring)
{
   Bool violated;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   /* check all bitstring constraints for feasibility */
   violated = FALSE;
   for( c = 0; c < nconss && !violated; ++c )
   {
      CHECK_OKAY( check(scip, conss[c], sol, checklprows, &violated) );
   }

   if( violated )
      *result = SCIP_INFEASIBLE;
   else
      *result = SCIP_FEASIBLE;

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
static
DECL_CONSPROP(consPropBitstring)
{
   int nfixedvars;
   int nchgbds;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   *result = SCIP_DIDNOTFIND;

   /* propagate all useful bitstring constraints */
   nfixedvars = 0;
   nchgbds = 0;
   for( c = 0; c < nusefulconss; ++c )
   {
      CHECK_OKAY( propagate(scip, conss[c], &nfixedvars, &nchgbds) );
   }

   /* adjust the result */
   if( nfixedvars > 0 || nchgbds > 0 )
      *result = SCIP_REDUCEDDOM;

   return SCIP_OKAY;
}


/** presolving method of constraint handler */
static
DECL_CONSPRESOL(consPresolBitstring)
{
   int nactfixedvars;
   int nactchgbds;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(result != NULL);

   *result = SCIP_DIDNOTFIND;

   /* propagate all bitstring constraints */
   nactfixedvars = 0;
   nactchgbds = 0;
   for( c = 0; c < nconss; ++c )
   {
      CHECK_OKAY( propagate(scip, conss[c], &nactfixedvars, &nactchgbds) );
   }

   /* adjust the result */
   if( nactfixedvars > 0 || nactchgbds > 0 )
   {
      *result = SCIP_SUCCESS;
      (*nfixedvars) += nactfixedvars;
      (*nchgbds) += nactchgbds;
   }

   return SCIP_OKAY;
}


/** conflict variable resolving method of constraint handler */
#define consRescvarBitstring NULL


/** variable rounding lock method of constraint handler */
static
DECL_CONSLOCK(consLockBitstring)
{
   CONSDATA* consdata;
   int i;

   /* every change in variable values alters the feasibility of the bitstring equations:
    * we have to lock rounding in both directions for positive and negative constraint
    */
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->bits != NULL);
   assert(consdata->words != NULL);

   /* lock bit variables */
   for( i = 0; i < consdata->nbits; ++i )
   {
      SCIPvarLock(consdata->bits[i], nlockspos + nlocksneg, nlockspos + nlocksneg);
   }

   /* lock word variables */
   for( i = 0; i < consdata->nwords; ++i )
   {
      SCIPvarLock(consdata->words[i], nlockspos + nlocksneg, nlockspos + nlocksneg);
   }

   return SCIP_OKAY;
}


/** variable rounding unlock method of constraint handler */
static
DECL_CONSUNLOCK(consUnlockBitstring)
{
   CONSDATA* consdata;
   int i;

   /* every change in variable values alters the feasibility of the bitstring equations:
    * we have to unlock rounding in both directions for positive and negative constraint
    */
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->bits != NULL);
   assert(consdata->words != NULL);

   /* unlock bit variables */
   for( i = 0; i < consdata->nbits; ++i )
   {
      SCIPvarUnlock(consdata->bits[i], nunlockspos + nunlocksneg, nunlockspos + nunlocksneg);
   }

   /* unlock word variables */
   for( i = 0; i < consdata->nwords; ++i )
   {
      SCIPvarUnlock(consdata->words[i], nunlockspos + nunlocksneg, nunlockspos + nunlocksneg);
   }

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#define consActiveBitstring NULL


/** constraint deactivation notification method of constraint handler */
#define consDeactiveBitstring NULL


/** constraint enabling notification method of constraint handler */
#define consEnableBitstring NULL


/** constraint disabling notification method of constraint handler */
#define consDisableBitstring NULL




/*
 * bitstring event handler methods
 */

static
DECL_EVENTEXEC(eventExecBitstring)
{
   CONSDATA* consdata;

   consdata = (CONSDATA*)eventdata;
   consdata->propagated = FALSE;

   return SCIP_OKAY;
}




/*
 * constraint specific interface methods
 */

/** creates the handler for bitstring constraints and includes it in SCIP */
RETCODE SCIPincludeConsHdlrBitstring(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   CONSHDLRDATA* conshdlrdata;

   /* create event handler for bound change events */
   CHECK_OKAY( SCIPincludeEventhdlr(scip, EVENTHDLR_NAME, EVENTHDLR_DESC,
                  NULL, NULL, NULL,
                  NULL, eventExecBitstring,
                  NULL) );

   /* create bitstring constraint handler data */
   CHECK_OKAY( conshdlrdataCreate(scip, &conshdlrdata) );

   /* include constraint handler */
   CHECK_OKAY( SCIPincludeConsHdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
                  CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
                  CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_NEEDSCONS,
                  consFreeBitstring, consInitBitstring, consExitBitstring,
                  consDeleteBitstring, consTransBitstring, consInitlpBitstring,
                  consSepaBitstring, consEnfolpBitstring, consEnfopsBitstring, consCheckBitstring, 
                  consPropBitstring, consPresolBitstring, consRescvarBitstring,
                  consLockBitstring, consUnlockBitstring,
                  consActiveBitstring, consDeactiveBitstring, 
                  consEnableBitstring, consDisableBitstring,
                  conshdlrdata) );

   return SCIP_OKAY;
}

/** creates and captures a bitstring constraint
 *  Warning! Either the bitstring should be short, or the objective value should be zero, because the objective
 *  value of the most significant bit in the string would be 2^(nbits-1)*obj
 */
RETCODE SCIPcreateConsBitstring(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              nbits,              /**< number of bits in the bitstring */
   Real             obj,                /**< objective value of bitstring variable */
   Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP? */
   Bool             separate,           /**< should the constraint be separated during LP processing? */
   Bool             enforce,            /**< should the constraint be enforced during node processing? */
   Bool             propagate,          /**< should the constraint be propagated during node processing? */
   Bool             removeable          /**< should the constraint be removed from the LP due to aging or cleanup? */
   )
{
   CONSHDLR* conshdlr;
   CONSHDLRDATA* conshdlrdata;
   CONSDATA* consdata;
   const Bool check = TRUE;       /* bit string constraints must always be checked for feasibility */
   const Bool local = FALSE;      /* bit strings are never local, because they represent problem variables */
   const Bool modifiable = FALSE; /* bit strings are never modifiable, because they represent problem variables */

   /* find the bitstring constraint handler */
   conshdlr = SCIPfindConsHdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("bitstring constraint handler not found");
      return SCIP_PLUGINNOTFOUND;
   }

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   /* create constraint data */
   CHECK_OKAY( consdataCreate(scip, &consdata, nbits) );
   CHECK_OKAY( consdataCreateVars(scip, consdata, conshdlrdata->eventhdlr, name, obj) );

   /* create constraint */
   CHECK_OKAY( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
                  local, modifiable, removeable) );

   return SCIP_OKAY;
}
