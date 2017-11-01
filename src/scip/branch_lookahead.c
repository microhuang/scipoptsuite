/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   branch_lookahead.c
 * @ingroup BRANCHINGRULES
 * @brief  lookahead LP branching rule
 * @author Christoph Schubert
 *
 * The (multi-level) lookahead branching rule applies strong branching to every fractional value of the LP solution
 * at the current node of the branch-and-bound tree, as well as recursivly to every temporary childproblem created by this
 * strong branching. The rule selects the candidate with the best proven dual bound.
 *
 * For a more mathematical description and a comparison between lookahead branching and other branching rules
 * in SCIP, we refer to
 *
 * @par
 * Christoph Schubert@n
 * Multi-Level Lookahead Branching@n
 * Master Thesis, Technische Universität Berlin, 2017@n
 */

/* Supported defines:
 * PRINTNODECONS: prints the binary constraints added
 * SCIP_DEBUG: prints detailed execution information
 * SCIP_STATISTIC: prints some statistics after the branching rule is freed */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <stddef.h>

#include "lpi/lpi.h"
#include "scip/branch_lookahead.h"
#include "scip/cons_logicor.h"

#define BRANCHRULE_NAME            "lookahead"
#define BRANCHRULE_DESC            "fullstrong branching over two levels"
#define BRANCHRULE_PRIORITY        0
#define BRANCHRULE_MAXDEPTH        -1
#define BRANCHRULE_MAXBOUNDDIST    1.0

#define DEFAULT_USEBINARYCONSTRAINTS         TRUE  /**< Should binary constraints be collected and applied? */
#define DEFAULT_ADDCLIQUE                    FALSE /**< Add binary constraints also as a clique. */
#define DEFAULT_ADDBINCONSROW                FALSE /**< Should binary constraints be added as rows to the base LP? */
#define DEFAULT_USEDOMAINREDUCTION           TRUE  /**< Should domain reductions be collected and applied? */
#define DEFAULT_MAXNUMBERVIOLATEDCONS        1     /**< How many constraints that are violated by the base lp solution should
                                                    *   be gathered until the rule is stopped and they are added? */
#define DEFAULT_STOREUNVIOLATEDSOL           TRUE  /**< If only non violating constraints are added, should the branching
                                                    *   decision be stored till the next call? */
#define DEFAULT_REEVALAGE                    10LL  /**< Max number of LPs solved after which a previous prob branching results
                                                    *   are recalculated. */
#define DEFAULT_FORCEBRANCHING               FALSE /**< Should LAB be forced, if only one candidate is given? */
#define DEFAULT_RECURSIONDEPTH               2     /**< The max depth of LAB. */
#define DEFAULT_ADDNONVIOCONS                FALSE /**< Should binary constraints, that are not violated by the base LP, be
                                                    *   collected and added? */
#define DEFAULT_DOWNFIRST                    TRUE  /**< Should the down branch be evaluated first? */
#define DEFAULT_PROPAGATE                    FALSE /**< Should domain propagation be executed before each temporary node is
                                                    *   solved? */
#define DEFAULT_ABBREVIATED                  FALSE /**< Toggles the abbreviated LAB. */
#define DEFAULT_MAXNCANDS                    4     /**< If abbreviated: The max number of candidates to consider per node */
#define DEFAULT_REUSEBASIS                   TRUE  /**< If abbreviated: Should the information gathered to obtain the best
                                                    *   candidates be reused? */
#define DEFAULT_ABBREVPSEUDO                 FALSE /**< If abbreviated: Use pseudo costs to estimate the score of a
                                                    *   candidate. */

#ifdef SCIP_DEBUG
/* Adjusted debug message that also prints the current probing depth. */
#define LABdebugMessage(scip,lvl,...)        do                                                                            \
                                             {                                                                             \
                                                SCIP_STAGE stage;                                                          \
                                                SCIPverbMessage(scip, lvl, NULL, "[%s:%-4d] ", __FILE__, __LINE__);        \
                                                stage = SCIPgetStage(scip);                                                \
                                                if( stage == SCIP_STAGE_INIT )                                             \
                                                {                                                                          \
                                                   SCIPverbMessage(scip, lvl, NULL, "Init   : ");                          \
                                                }                                                                          \
                                                else if( stage == SCIP_STAGE_FREE )                                        \
                                                {                                                                          \
                                                   SCIPverbMessage(scip, lvl, NULL, "Free   : ");                          \
                                                }                                                                          \
                                                else if( SCIPinProbing(scip) )                                             \
                                                {                                                                          \
                                                   SCIPverbMessage(scip, lvl, NULL, "Depth %i: ",                          \
                                                      SCIPgetProbingDepth(scip));                                          \
                                                }                                                                          \
                                                else                                                                       \
                                                {                                                                          \
                                                   SCIPverbMessage(scip, lvl, NULL, "Base   : ");                          \
                                                }                                                                          \
                                                SCIPverbMessage(scip, lvl, NULL, __VA_ARGS__);                             \
                                             }                                                                             \
                                             while( FALSE )
/* Writes a debug message without the leading information. Can be used to append something to an output of LABdebugMessage*/
#define LABdebugMessagePrint(scip,lvl,...)   do                                                                            \
                                             {                                                                             \
                                                SCIPverbMessage(scip, lvl, NULL, __VA_ARGS__);                             \
                                             }                                                                             \
                                             while( FALSE )
#else
#define LABdebugMessage(scip,lvl,...)        /**/
#define LABdebugMessagePrint(scip,lvl,...)   /**/
#endif

/*
 * Data structures
 */

/** Holds the information needed for branching on a variable. */
typedef struct
{
   SCIP_VAR*             var;                /**< Variable to branch on. May be NULL.*/
   SCIP_Real             val;                /**< the fractional value of the variable to branch on */
   SCIP_Real             downdb;             /**< dual bound for down branch */
   SCIP_Real             updb;               /**< dual bound for the up branch */
   SCIP_Real             proveddb;           /**< proven dual bound for the current node */
   SCIP_Bool             downdbvalid;        /**< Indicator for the validity of the downdb value. Is FALSE, if no actual
                                              *   branching occurred or the value was determined by an LP not solved to
                                              *   optimality. */

   SCIP_Bool             updbvalid;          /**< Indicator for the validity of the updb value. Is FALSE, if no actual
                                              *   branching occurred or the value was determined by an LP not solved to
                                              *   optimality. */
} BRANCHINGDECISION;

/** Allocates a branching decision in the buffer and initiates it with default values. */
static
SCIP_RETCODE branchingDecisionCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGDECISION**   decision            /**< pointer to the decision to allocate and initiate */
   )
{
   assert(scip != NULL);
   assert(decision != NULL);

   SCIP_CALL( SCIPallocBuffer(scip, decision) );
   (*decision)->var = NULL;
   (*decision)->downdb = -SCIPinfinity(scip);
   (*decision)->downdbvalid = FALSE;
   (*decision)->updb = -SCIPinfinity(scip);
   (*decision)->updbvalid = FALSE;
   (*decision)->proveddb = -SCIPinfinity(scip);

   return SCIP_OKAY;
}

/** Copies the data from the source to the target. */
static
void branchingDecisionCopy(
   BRANCHINGDECISION*    sourcedecision,     /**< the source branching decision */
   BRANCHINGDECISION*    targetdecision      /**< the target branching decision */
   )
{
   assert(sourcedecision != NULL);
   assert(targetdecision != NULL);

   targetdecision->var = sourcedecision->var;
   targetdecision->val = sourcedecision->val;
   targetdecision->downdb = sourcedecision->downdb;
   targetdecision->downdbvalid = sourcedecision->downdbvalid;
   targetdecision->updb = sourcedecision->updb;
   targetdecision->updbvalid = sourcedecision->updbvalid;
   targetdecision->proveddb = sourcedecision->proveddb;
}

/** Checks whether the given branching decision can be used to branch on. */
static
SCIP_Bool branchingDecisionIsValid(
   BRANCHINGDECISION*    decision            /**< the branching decision to check */
   )
{
   assert(decision != NULL);

   /* a branching decision is deemed valid, if the var point is not on the default NULL value (see the allocate method) */
   return decision->var != NULL;
}

/** Frees the allocated buffer memory of the branching decision. */
static
void branchingDecisionFree(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGDECISION**   decision            /**< pointer to the decision to be freed */
   )
{
   assert(scip != NULL);
   assert(decision != NULL);

   SCIPfreeBuffer(scip, decision);
}

/** A container to hold the result of a branching. */
typedef struct
{
   SCIP_Real             objval;             /**< The objective value of the solved lp. Only contains meaningful data, if
                                              *   cutoff == FALSE. */
   SCIP_Real             dualbound;          /**< The best dual bound for this branching, may be changed by deeper level
                                              *   branchings. */
   SCIP_Longint          niterations;        /**< The number of probing iterations needed in sub branch. */
   SCIP_Bool             cutoff;             /**< Indicates whether the node was infeasible and was cutoff. */
   SCIP_Bool             dualboundvalid;     /**< Us the value of the dual bound valid? That means, was the according LP
                                              *   or the sub problems solved to optimality? */
} BRANCHINGRESULTDATA;

/** Allocates a branching result in the buffer. */
static
SCIP_RETCODE branchingResultDataCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGRESULTDATA** resultdata          /**< pointer to the result to be allocated */
   )
{
   assert(scip != NULL);
   assert(resultdata != NULL);

   SCIP_CALL( SCIPallocBuffer(scip, resultdata) );

   return SCIP_OKAY;
}

/** Initiates the branching result with default values. */
static
void branchingResultDataInit(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGRESULTDATA*  resultdata          /**< pointer to the result to be initialized */
   )
{
   assert(scip != NULL);
   assert(resultdata != NULL);

   resultdata->objval = -SCIPinfinity(scip);
   resultdata->dualbound = -SCIPinfinity(scip);
   resultdata->cutoff = FALSE;
   resultdata->dualboundvalid = FALSE;
   resultdata->niterations = 0;
}

/** Copies the data from the source to the target. */
static
void branchingResultDataCopy(
   BRANCHINGRESULTDATA*  sourcedata,         /**< the source branching result */
   BRANCHINGRESULTDATA*  targetdata          /**< the target branching result */
   )
{
   assert(sourcedata != NULL);
   assert(targetdata != NULL);

   targetdata->cutoff = sourcedata->cutoff;
   targetdata->objval = sourcedata->objval;
   targetdata->dualbound = sourcedata->dualbound;
   targetdata->dualboundvalid = sourcedata->dualboundvalid;
   targetdata->niterations = sourcedata->niterations;
}

/** Frees the allocated buffer memory of the branching result. */
static
void branchingResultDataFree(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGRESULTDATA** resultdata          /**< pointer to the result to be freed */
   )
{
   assert(scip != NULL);
   assert(resultdata != NULL);

   SCIPfreeBuffer(scip, resultdata);
}

/** The data that is preserved over multiple runs of the branching rule. */
typedef struct
{
   SCIP_SOL*             prevbinsolution;    /**< The previous solution for the case that in the previous run only
                                              *   non-violating implied binary constraints were added. */
   BRANCHINGDECISION*    prevdecision;       /**< The previous decision that gets used for the case that in the previous run
                                              *   only non-violating implied binary constraints were added.*/
   SCIP_Longint*         lastbranchid;       /**< The node id at which the var was last branched on (for a given branching
                                              *   var). */
   SCIP_Longint*         lastbranchnlps;     /**< The number of (non-probing) LPs that where solved when the var was last
                                              *   branched on. */
   SCIP_Real*            lastbranchlpobjval; /**< The lp objval at which var was last branched on. */
   BRANCHINGRESULTDATA** lastbranchupres;    /**< The result of the last up branching for a given var. */
   BRANCHINGRESULTDATA** lastbranchdownres;  /**< The result of the last down branching for a given var. */
   int                   restartindex;       /**< The index at which the iteration over the number of candidates starts. */
} PERSISTENTDATA;

/** The parameter that can be changed by the user/caller and alter the behaviour of the lookahead branching. */
typedef struct
{
   SCIP_Longint          reevalage;          /**< The number of "normal" (not probing) lps that may have been solved before
                                              *   we stop using old data and start recalculating new first level data. */
   int                   maxnviolatedcons;   /**< The number of constraints we want to gather before restarting the run. Set
                                              *   to -1 for an unbounded list. */
   int                   recursiondepth;     /**< How deep should the recursion go? Default for Lookahead: 2 */
   int                   maxncands;          /**< If abbreviated == TRUE, at most how many candidates should be handled? */
   SCIP_Bool             usedomainreduction; /**< indicates whether the data for domain reductions should be gathered and
                                              *   used. */
   SCIP_Bool             usebincons;         /**< indicates whether the data for the implied binary constraints should
                                              *   be gathered and used */
   SCIP_Bool             addbinconsrow;      /**< Add the implied binary constraints as a row to the problem matrix */
   SCIP_Bool             stopbranching;      /**< indicates whether we should stop the first level branching after finding
                                              *   an infeasible first branch */
   SCIP_Bool             forcebranching;     /**< Execute the lookahead logic even if only one branching candidate is given.
                                              *   May be used to calculate the score of a single candidate. */
   SCIP_Bool             addnonviocons;      /**< Should constraints be added, that are not violated by the base LP? */
   SCIP_Bool             downfirst;          /**< Should the down branch be executed first? */
   SCIP_Bool             abbreviated;        /**< Should the abbreviated version be used? */
   SCIP_Bool             reusebasis;         /**< If abbreviated == TRUE, should the solution lp-basis of the FSB run be
                                              *   used in the first abbreviated level?  */
   SCIP_Bool             storeunviolatedsol; /**< Should a solution/decision be stored, to speed up the next iteration
                                              *   after adding the constraints/domreds? */
   SCIP_Bool             abbrevpseudo;       /**< If abbreviated == TRUE, should pseudocost values be used, to approximate
                                              *   the scoring? */
   SCIP_Bool             addclique;          /**< Should binary constraints found in the root node be added as cliques? */
   SCIP_Bool             propagate;          /**< Should the problem be propagated before solving each inner node? */
} CONFIGURATION;

/** Allocates a configuration in the buffer and initiates it with the default values. */
static
SCIP_RETCODE configurationCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION**       config              /**< pointer to the configuration to allocate in initialize */
   )
{
   assert(scip != NULL);
   assert(config != NULL);

   SCIP_CALL( SCIPallocBuffer(scip, config) );

   return SCIP_OKAY;
}

/** Copies the fields from copysource to config. */
static
void configurationCopy(
   CONFIGURATION*        config,             /**< pointer to the configuration to allocate in initialize */
   CONFIGURATION*        copysource          /**< copy the settings from this config */
   )
{
   assert(config != NULL);
   assert(copysource != NULL);

   config->addbinconsrow = copysource->addbinconsrow;
   config->addnonviocons = copysource->addnonviocons;
   config->downfirst = copysource->downfirst;
   config->forcebranching = copysource->forcebranching;
   config->maxnviolatedcons = copysource->maxnviolatedcons;
   config->recursiondepth = copysource->recursiondepth;
   config->reevalage = copysource->reevalage;
   config->stopbranching = copysource->stopbranching;
   config->usebincons = copysource->usebincons;
   config->usedomainreduction = copysource->usedomainreduction;
   config->abbreviated = copysource->abbreviated;
   config->maxncands = copysource->maxncands;
   config->reusebasis = copysource->reusebasis;
   config->storeunviolatedsol = copysource->storeunviolatedsol;
   config->abbrevpseudo = copysource->abbrevpseudo;
   config->addclique = copysource->addclique;
   config->propagate = copysource->propagate;
}

/** Frees the allocated buffer memory of the branching result. */
static
void configurationFree(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION**       config              /**< pointer to the configuration to free */
   )
{
   assert(scip != NULL);
   assert(config != NULL);

   SCIPfreeBuffer(scip, config);
}

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
/* An array containing all human readable names of the possible results. */
static const char* names[18] = { "", "SCIP_DIDNOTRUN", "SCIP_DELAYED", "SCIP_DIDNOTFIND", "SCIP_FEASIBLE",
   "SCIP_INFEASIBLE", "SCIP_UNBOUNDED", "SCIP_CUTOFF", "SCIP_SEPARATED", "SCIP_NEWROUND", "SCIP_REDUCEDDOM",
   "SCIP_CONSADDED", "SCIP_CONSCHANGED", "SCIP_BRANCHED", "SCIP_SOLVELP", "SCIP_FOUNDSOL", "SCIP_SUSPENDED", "SCIP_SUCCESS"
};

/** Returns a human readable name for the given result enum value. */
static
const char* getStatusString(
   SCIP_RESULT           result              /**< enum value to get the string representation for */
   )
{
   /* the result can be used as an array index, as it is internally handled as an int value. */
   return names[result];
}
#endif

#ifdef SCIP_STATISTIC
/** The data used for some statistical analysis. */
typedef struct
{
   int*                  nresults;           /**< Array of counters for each result state the lookahead branching finished.
                                              *   The first (0) entry is unused, as the result states are indexed 1-based
                                              *   and we use this index as our array index. */
   int*                  nsinglecutoffs;     /**< The number of single cutoffs on a (probing) node per probingdepth. */
   int*                  nfullcutoffs;       /**< The number of double cutoffs on a (probing) node per probingdepth. */
   int*                  nlpssolved;         /**< The number of all lps solved for a given probingdepth (incl. FSB). */
   int*                  nlpssolvedfsb;      /**< The number of lps solved by the initial FSB to get the FSB scores. */
   SCIP_Longint*         nlpiterations;      /**< The number of all lp iterations needed for a given probingdepth
                                              *   (incl. FSB). */
   SCIP_Longint*         nlpiterationsfsb;   /**< The number of lp iterations needed to get the FSB scores. */
   int*                  npropdomred;        /**< The number of domain reductions based on domain propagation per
                                              *   progingdepth. */
   int*                  nsinglecandidate;   /**< The number of times a single candidate was given to the recursion routine
                                              *   per probingdepth. */
   int*                  noldbranchused;     /**< The number of times old branching data is used (see the reevalage
                                              *   parameter in the CONFIGURATION struct) */
   int*                  chosenfsbcand;      /**< If abbreviated, this is the number of times each candidate was finally
                                              *   chosen by the following LAB */
   int                   ntotalresults;      /**< The total sum of the entries in nresults. */
   int                   nbinconst;          /**< The number of binary constraints added to the base node. */
   int                   nbinconstvio;       /**< The number of binary constraints added to the base node, that are violated
                                              *   by the LP at that node. */
   int                   ndomred;            /**< The number of domain reductions added to the base node. */
   int                   ndomredvio;         /**< The number of domain reductions added to the base node, that are violated
                                              *   by the LP at that node. */
   int                   ndepthreached;      /**< The number of times the branching was aborted due to a too small depth. */
   int                   ndomredcons;        /**< The number of binary constraints ignored, as they would be dom reds. */
   int                   ncutoffproofnodes;  /**< The number of nodes needed to proof all found cutoffs. */
   int                   ndomredproofnodes;  /**< The number of nodes needed to proof all found domreds. */
   int                   stopafterfsb;       /**< If abbreviated, this is the number of times the rule was stopped after
                                              *   scoring candidates by FSB, e.g. by adding constraints or domreds. */
   int                   cutoffafterfsb;     /**< If abbreviated, this is the number of times the rule was stopped after
                                              *   scoring candidates by FSB because of a found cutoff. */
   int                   domredafterfsb;     /**< If abbreviated, this is the number of times the rule was stopped after
                                              *   scoring candidates by FSB because of a found domain reduction. */
   int                   ncliquesadded;      /**< The number of cliques added in the root node. */
   int                   maxnbestcands;      /**< If abbreviated, this is the maximum number of candidates the method
                                              *   getBestCandidates will return. */
   int                   recursiondepth;     /**< The recursiondepth of the LAB. Can be used to access the depth-dependent
                                              *   arrays contained in the statistics. */
} STATISTICS;

/** Initializes the statistics with the start values. */
static
void statisticsInit(
   STATISTICS*           statistics          /**< the statistics to be initialized */
   )
{
   int i;

   assert(statistics != NULL);
   assert(statistics->recursiondepth > 0);

   statistics->ntotalresults = 0;
   statistics->nbinconst = 0;
   statistics->nbinconstvio = 0;
   statistics->ndomredvio = 0;
   statistics->ndepthreached = 0;
   statistics->ndomred = 0;
   statistics->ndomredcons = 0;
   statistics->ncutoffproofnodes = 0;
   statistics->ndomredproofnodes = 0;
   statistics->stopafterfsb = 0;
   statistics->cutoffafterfsb = 0;
   statistics->domredafterfsb = 0;
   statistics->ncliquesadded = 0;

   for( i = 0; i < 18; i++)
   {
      statistics->nresults[i] = 0;
   }

   for( i = 0; i < statistics->recursiondepth; i++ )
   {
      statistics->noldbranchused[i] = 0;
      statistics->nsinglecandidate[i] = 0;
      statistics->npropdomred[i] = 0;
      statistics->nfullcutoffs[i] = 0;
      statistics->nlpssolved[i] = 0;
      statistics->nlpssolvedfsb[i] = 0;
      statistics->nlpiterations[i] = 0;
      statistics->nlpiterationsfsb[i] = 0;
      statistics->nsinglecutoffs[i] = 0;
   }

   for( i = 0; i < statistics->maxnbestcands; i++ )
   {
      statistics->chosenfsbcand[i] = 0;
   }
}

/** Allocates a static in the buffer and initiates it with the default values. */
static
SCIP_RETCODE statisticsAllocate(
   SCIP*                 scip,               /**< SCIP data structure */
   STATISTICS**          statistics,         /**< pointer to the statistics to be allocated */
   int                   recursiondepth,     /**< the LAB recursion depth */
   int                   maxncands           /**< the max number of candidates to be used in ALAB */
   )
{

   assert(scip != NULL);
   assert(statistics != NULL);
   assert(recursiondepth > 0);
   assert(maxncands > 0);

   SCIP_CALL( SCIPallocBuffer(scip, statistics) );
   /* 17 current number of possible result values and the enum is 1 based, so 17 + 1 as array size with unused 0 element */
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nresults, 17+1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nsinglecutoffs, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nfullcutoffs, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nlpssolved, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nlpssolvedfsb, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nlpiterations, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nlpiterationsfsb, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->npropdomred, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->nsinglecandidate, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->noldbranchused, recursiondepth) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*statistics)->chosenfsbcand, maxncands) );

   (*statistics)->recursiondepth = recursiondepth;
   (*statistics)->maxnbestcands = maxncands;

   statisticsInit(*statistics);
   return SCIP_OKAY;
}

/** Merges two statistic structs together. */
static
void mergeFSBStatistics(
   STATISTICS*           mainstatistics,     /**< The statistics into which the other structs is merged */
   STATISTICS*           childstatistics     /**< Statistics struct going to be merged into the main struct. */
   )
{
   int i;

   assert(mainstatistics != NULL);
   assert(childstatistics != NULL);
   assert(mainstatistics->recursiondepth == childstatistics->recursiondepth);

   mainstatistics->ntotalresults += childstatistics->ntotalresults;
   mainstatistics->nbinconst += childstatistics->nbinconst;
   mainstatistics->nbinconstvio += childstatistics->nbinconstvio;
   mainstatistics->ndomredvio += childstatistics->ndomredvio;
   mainstatistics->ndepthreached += childstatistics->ndepthreached;
   mainstatistics->ndomred += childstatistics->ndomred;
   mainstatistics->ndomredcons += childstatistics->ndomredcons;
   mainstatistics->ncutoffproofnodes += childstatistics->ncutoffproofnodes;
   mainstatistics->ndomredproofnodes += childstatistics->ndomredproofnodes;

   for( i = 0; i < mainstatistics->recursiondepth; i++ )
   {
      mainstatistics->nresults[i] += childstatistics->nresults[i];
      mainstatistics->nsinglecutoffs[i] += childstatistics->nsinglecutoffs[i];
      mainstatistics->nfullcutoffs[i] += childstatistics->nfullcutoffs[i];
      mainstatistics->nlpssolved[i] += childstatistics->nlpssolved[i];
      mainstatistics->nlpssolvedfsb[i] += childstatistics->nlpssolved[i];
      mainstatistics->nlpiterations[i] += childstatistics->nlpiterations[i];
      mainstatistics->nlpiterationsfsb[i] += childstatistics->nlpiterations[i];
      mainstatistics->npropdomred[i] += childstatistics->npropdomred[i];
      mainstatistics->nsinglecandidate[i] += childstatistics->nsinglecandidate[i];
      mainstatistics->noldbranchused[i] += childstatistics->noldbranchused[i];
      mainstatistics->chosenfsbcand[i] += childstatistics->chosenfsbcand[i];
   }
}


/** Prints the content of the statistics to stdout.  */
static
void statisticsPrint(
   SCIP*                 scip,               /**< SCIP data structure */
   STATISTICS*           statistics          /**< the statistics to print */
   )
{

   assert(scip != NULL);
   assert(statistics != NULL);
   assert(statistics->recursiondepth > 0);

   /* only print something, if we have any statistics */
   if( statistics->ntotalresults > 0 )
   {
      int i;

      SCIPinfoMessage(scip, NULL, "Lookahead Branching was called <%i> times.\n", statistics->ntotalresults);
      for( i = 1; i < 18; i++ )
      {
         SCIP_RESULT currentresult = (SCIP_RESULT)i;
         /* see type_result.h for the id <-> enum mapping */
         SCIPinfoMessage(scip, NULL, "Result <%s> was chosen <%i> times\n", getStatusString(currentresult),
               statistics->nresults[i]);
      }

      SCIPinfoMessage(scip, NULL, "Branching was stopped after the scoring FSB %i times. That was:\n",
            statistics->stopafterfsb);
      SCIPinfoMessage(scip, NULL, " %i times because of a cutoff.\n", statistics->cutoffafterfsb);
      SCIPinfoMessage(scip, NULL, " %i times because of a domain reduction.\n", statistics->domredafterfsb);

      for( i = 0; i < statistics->maxnbestcands; i++ )
      {
         SCIPinfoMessage(scip, NULL, "The %i. variable (w.r.t. the FSB score) was chosen as the final result %i times.\n",
            i+1, statistics->chosenfsbcand[i]);
      }

      for( i = 0; i < statistics->recursiondepth; i++ )
      {
         SCIPinfoMessage(scip, NULL, "In depth <%i>, <%i> fullcutoffs and <%i> single cutoffs were found.\n",
               i, statistics->nfullcutoffs[i], statistics->nsinglecutoffs[i]);
         SCIPinfoMessage(scip, NULL, "In depth <%i>, <%i> LPs were solved, <%i> of them to calculate the FSB score.\n",
               i, statistics->nlpssolved[i], statistics->nlpssolvedfsb[i]);
         SCIPinfoMessage(scip, NULL, "In depth <%i>, <%" SCIP_LONGINT_FORMAT "> iterations were needed to solve the LPs, <%"
               SCIP_LONGINT_FORMAT "> of them to calculate the FSB score.\n", i, statistics->nlpiterations[i],
               statistics->nlpiterationsfsb[i]);
         SCIPinfoMessage(scip, NULL, "In depth <%i>, a decision was discarded <%i> times due to domain reduction because of"
               " propagation.\n", i, statistics->npropdomred[i]);
         SCIPinfoMessage(scip, NULL, "In depth <%i>, only one branching candidate was given <%i> times.\n",
               i, statistics->nsinglecandidate[i]);
         SCIPinfoMessage(scip, NULL, "In depth <%i>, old branching results were used in <%i> cases.\n",
               i, statistics->noldbranchused[i]);
      }

      SCIPinfoMessage(scip, NULL, "Depth limit was reached <%i> times.\n", statistics->ndepthreached);
      SCIPinfoMessage(scip, NULL, "Ignored <%i> binary constraints, that would be domain reductions.\n",
         statistics->ndomredcons);
      SCIPinfoMessage(scip, NULL, "Added <%i> binary constraints, of which <%i> where violated by the base LP.\n",
         statistics->nbinconst, statistics->nbinconstvio);
      SCIPinfoMessage(scip, NULL, "Reduced the domain of <%i> vars, <%i> of them where violated by the base LP.\n",
         statistics->ndomred, statistics->ndomredvio);
      SCIPinfoMessage(scip, NULL, "Added <%i> cliques found as binary constraint in the root node\n",
         statistics->ncliquesadded);
      SCIPinfoMessage(scip, NULL, "Needed <%i> additional nodes to proof the cutoffs of base nodes\n",
         statistics->ncutoffproofnodes);
      SCIPinfoMessage(scip, NULL, "Needed <%i> additional nodes to proof the domain reductions\n",
         statistics->ndomredproofnodes);
   }
}

/** Frees the allocated buffer memory of the statistics. */
static
void statisticsFree(
   SCIP*                 scip,               /**< SCIP data structure */
   STATISTICS**          statistics          /**< pointer to the statistics to free */
   )
{

   assert(scip != NULL);
   assert(statistics != NULL);


   SCIPfreeBufferArray(scip, &(*statistics)->chosenfsbcand);
   SCIPfreeBufferArray(scip, &(*statistics)->noldbranchused);
   SCIPfreeBufferArray(scip, &(*statistics)->nsinglecandidate);
   SCIPfreeBufferArray(scip, &(*statistics)->npropdomred);
   SCIPfreeBufferArray(scip, &(*statistics)->nlpiterationsfsb);
   SCIPfreeBufferArray(scip, &(*statistics)->nlpiterations);
   SCIPfreeBufferArray(scip, &(*statistics)->nlpssolvedfsb);
   SCIPfreeBufferArray(scip, &(*statistics)->nlpssolved);
   SCIPfreeBufferArray(scip, &(*statistics)->nfullcutoffs);
   SCIPfreeBufferArray(scip, &(*statistics)->nsinglecutoffs);
   SCIPfreeBufferArray(scip, &(*statistics)->nresults);
   SCIPfreeBuffer(scip, statistics);
}

/** Helper struct to store the statistical data needed in a single run. */
typedef struct
{
   int                   ncutoffproofnodes;  /**< The number of nodes needed to proof the current cutoff. */
   int                   ndomredproofnodes;  /**< The number of nodes needed to proof all current domreds. */
} LOCALSTATISTICS;

/** Allocates the local statistics in buffer memory and initializes it with default values. */
static
SCIP_RETCODE localStatisticsAllocate(
   SCIP*                 scip,               /**< SCIP data structure */
   LOCALSTATISTICS**     localstats          /**< pointer to the local statistics to allocate and initialize */
   )
{
   assert(scip != NULL);
   assert(localstats != NULL);

   SCIP_CALL( SCIPallocBuffer(scip, localstats) );

   (*localstats)->ncutoffproofnodes = 0;
   (*localstats)->ndomredproofnodes = 0;

   return SCIP_OKAY;
}

/** Frees the allocated buffer memory of the local statistics. */
static
void localStatisticsFree(
   SCIP*                 scip,               /**< SCIP data structure */
   LOCALSTATISTICS**     localstats          /**< pointer to the local statistics to be freed */
   )
{
   assert(scip != NULL);
   assert(localstats != NULL);

   SCIPfreeBuffer(scip, localstats);
}
#endif

/** branching rule data */
struct SCIP_BranchruleData
{
   CONFIGURATION*        config;             /**< the parameter that influence the behaviour of the lookahead branching */
   PERSISTENTDATA*       persistent;         /**< the data that persists over multiple branching decisions */
   SCIP_Bool             isinitialized;      /**< indicates whether the fields in this struct are initialized */
#ifdef SCIP_STATISTIC
   STATISTICS*           statistics;         /**< statistical data container */
#endif
};

/** all constraints that were created and may be added to the base node */
typedef struct
{
   SCIP_CONS**           constraints;        /**< The array of constraints. Length is adjusted as needed. */
   int                   nconstraints;       /**< The number of entries in the array 'constraints'. */
   int                   memorysize;         /**< The number of entries that the array 'constraints' may hold before the
                                              *   array is reallocated. */
   int                   nviolatedcons;      /**< Tracks the number of constraints that are violated by the base LP
                                              *   solution. */
} CONSTRAINTLIST;

/** Allocate and initialize the list holding the constraints. */
static
SCIP_RETCODE constraintListCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   CONSTRAINTLIST**      conslist,           /**< Pointer to the list to be allocated and initialized. */
   int                   startsize           /**< The number of entries the list initially can hold. */
   )
{
   assert(scip != NULL);
   assert(conslist != NULL);
   assert(startsize > 0);

   SCIP_CALL( SCIPallocBuffer(scip, conslist) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*conslist)->constraints, startsize) );

   /* We start without any constraints */
   (*conslist)->nconstraints = 0;
   (*conslist)->memorysize = startsize;
   (*conslist)->nviolatedcons = 0;

   return SCIP_OKAY;
}

/** Append an element to the end of the list of constraints. */
static
SCIP_RETCODE constraintListAppend(
   SCIP*                 scip,               /**< SCIP data structure */
   CONSTRAINTLIST*       list,               /**< The list to add the element to. */
   SCIP_CONS*            constoadd           /**< The element to add to the list. */
   )
{
   assert(scip != NULL);
   assert(list != NULL);
   assert(constoadd != NULL);

   /* In case the list tries to hold more elements than it has space, reallocate  */
   if( list->memorysize == list->nconstraints )
   {
      /* resize the array, such that it can hold the new element */
      int newmemsize = SCIPcalcMemGrowSize(scip, list->memorysize + 1);
      SCIP_CALL( SCIPreallocBufferArray(scip, &list->constraints, newmemsize) );
      list->memorysize = newmemsize;
   }

   /* Set the new var at the first unused place, which is the length used as index */
   list->constraints[list->nconstraints] = constoadd;
   list->nconstraints++;

   return SCIP_OKAY;
}

/** Free all resources of a constraint list in opposite order to the allocation. */
static
void constraintListFree(
   SCIP*                 scip,               /**< SCIP data structure */
   CONSTRAINTLIST**      conslist            /**< Pointer to the list to be freed. */
   )
{
   assert(scip != NULL);
   assert(conslist != NULL);

   SCIPfreeBufferArray(scip, &(*conslist)->constraints);
   SCIPfreeBuffer(scip, conslist);
}

/**
 * list of binary variables currently branched on
 * a down branching (x <= 0) is saved as the negated variable (1-x)
 * an up branching (x >= 1) is saved as the original variable (x)
 * these variables are used to build the binary constraint in case that a ('binary') branch is cut off
 */
typedef struct
{
   SCIP_VAR**            binaryvars;         /**< The binary variables currently branched on. */
   int                   nbinaryvars;        /**< The number of entries in 'nbinaryvars'. */
   int                   memorysize;         /**< The number of entries that the array 'binaryvars' may hold before the
                                              *   array is reallocated. */
} BINARYVARLIST;

/** Allocates and initializes the BINARYVARLIST struct. */
static
SCIP_RETCODE binaryVarListCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   BINARYVARLIST**       list,               /**< Pointer to the list to be allocated and initialized. */
   int                   startsize           /**< The number of entries the list initially can hold. */
   )
{
   assert(scip != NULL);
   assert(list != NULL);
   assert(startsize > 0);

   SCIP_CALL( SCIPallocBuffer(scip, list) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*list)->binaryvars, startsize) );

   /* We start with no entries and the (current) max length */
   (*list)->nbinaryvars = 0;
   (*list)->memorysize = startsize;

   return SCIP_OKAY;
}

/** Appends a binary variable to the list, reallocating the list if necessary. */
static
SCIP_RETCODE binaryVarListAppend(
   SCIP*                 scip,               /**< SCIP data structure */
   BINARYVARLIST*        list,               /**< The list to add the var to. */
   SCIP_VAR*             vartoadd            /**< The binary var to add to the list. */
   )
{
   assert(scip != NULL);
   assert(list != NULL);
   assert(vartoadd != NULL);
   assert(SCIPvarIsBinary(vartoadd));

   /* In case the list tries to hold more elements than it has space, reallocate  */
   if( list->memorysize == list->nbinaryvars )
   {
      /* resize the array, such that it can hold at least the new element */
      int newmemsize = SCIPcalcMemGrowSize(scip, list->memorysize + 1);
      SCIP_CALL( SCIPreallocBufferArray(scip, &list->binaryvars, newmemsize) );
      list->memorysize = newmemsize;
   }

   /* Set the new var at the first unused place, which is the length used as index */
   list->binaryvars[list->nbinaryvars] = vartoadd;
   list->nbinaryvars++;

   return SCIP_OKAY;
}

/** Remove the last element from the list. */
static
void binaryVarListDrop(
   BINARYVARLIST*        list                /**< The list to remove the last element from. */
   )
{
   assert(list != NULL);
   assert(list->nbinaryvars > 0);
   assert(list->binaryvars[list->nbinaryvars-1] != NULL);

   /* get the last element and set the last pointer to NULL (maybe unnecessary, but feels cleaner) */
   list->binaryvars[list->nbinaryvars-1] = NULL;

   /* decrement the number of entries in the actual list */
   list->nbinaryvars--;
}

/** Frees all resources allocated by a BINARYVARLIST in opposite order of allocation. */
static
void binaryVarListFree(
   SCIP*                 scip,               /**< SCIP data structure */
   BINARYVARLIST**       list                /**< Pointer to the list to free */
   )
{
   assert(scip != NULL);
   assert(list != NULL);

   SCIPfreeBufferArray(scip, &(*list)->binaryvars);
   SCIPfreeBuffer(scip, list);
}

/** struct holding the relevant data for handling binary constraints */
typedef struct
{
   BINARYVARLIST*        binaryvars;         /**< The current binary vars, used to created the constraints. */
   CONSTRAINTLIST*       createdconstraints; /**< The created constraints. */
} BINCONSDATA;

/** Allocate and initialize the BINCONSDATA struct. */
static
SCIP_RETCODE binConsDataCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   BINCONSDATA**         consdata,           /**< Pointer to the struct to be allocated and initialized. */
   int                   maxdepth,           /**< The depth of the recursion as an upper bound of branch vars to hold. */
   int                   nstartcons          /**< The start size of the array containing the constraints. */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);
   assert(maxdepth > 0);
   assert(nstartcons > 0);

   SCIP_CALL( SCIPallocBuffer(scip, consdata) );
   SCIP_CALL( binaryVarListCreate(scip, &(*consdata)->binaryvars, maxdepth) );
   SCIP_CALL( constraintListCreate(scip, &(*consdata)->createdconstraints, nstartcons) );

   return SCIP_OKAY;
}

/** Free all resources in a BINCONSDATA in opposite order of allocation. */
static
void binConsDataFree(
   SCIP*                 scip,               /**< SCIP data structure */
   BINCONSDATA**         consdata            /**< Pointer to he struct to be freed. */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);

   constraintListFree(scip, &(*consdata)->createdconstraints);
   binaryVarListFree(scip, &(*consdata)->binaryvars);
   SCIPfreeBuffer(scip, consdata);
}

/** A struct holding information to speed up the solving time for solving a problem again. This is filled by the FSB
 *  scoring routine that is run to get the best candidates. It is then read by the actual ALAB routine. */
typedef struct
{
   SCIP_LPISTATE*        lpistate;           /**< the basis information that may be set before another solve lp call */
   SCIP_LPINORMS*        lpinorms;           /**< the norms that may be set before another solve lp call */
   SCIP_Bool             primalfeas;         /**< indicates whether the solution was primal feasible */
   SCIP_Bool             dualfeas;           /**< indicates whether the solution was dual feasible */
} WARMSTARTINFO;

/** Allocates the warm start information on the buffer and initializes it with default values. */
static
SCIP_RETCODE warmStartInfoCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   WARMSTARTINFO**       warmstartinfo       /**< the warmstartinfo to allocate and initialize */
   )
{
   assert(scip != NULL);
   assert(warmstartinfo != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, warmstartinfo) );

   (*warmstartinfo)->lpistate = NULL;
   (*warmstartinfo)->lpinorms = NULL;
   (*warmstartinfo)->primalfeas = FALSE;
   (*warmstartinfo)->dualfeas = FALSE;

   return SCIP_OKAY;
}

/** Checks that the warm start info can be read into the lp solver. */
static
SCIP_Bool warmStartInfoIsReadable(
   WARMSTARTINFO*        memory              /**< The warm start info to check. May be NULL. */
   )
{
   /* the lpinorms may be NULL */
   return memory != NULL && memory->lpistate != NULL;
}

/** Frees the allocated buffer memory of the warm start info. */
static
SCIP_RETCODE warmStartInfoFree(
   SCIP*                 scip,               /**< SCIP data structure */
   WARMSTARTINFO**       warmstartinfo       /**< the warm start info to free */
   )
{
   SCIP_LPI* lpi;
   BMS_BLKMEM* blkmem;

   assert(scip != NULL);
   assert(warmstartinfo != NULL);

   SCIP_CALL( SCIPgetLPI(scip, &lpi) );
   blkmem = SCIPblkmem(scip);

   if( (*warmstartinfo)->lpistate != NULL )
   {
      SCIP_CALL( SCIPlpiFreeState(lpi, blkmem, &(*warmstartinfo)->lpistate) );
      (*warmstartinfo)->lpistate = NULL;
   }

   if( (*warmstartinfo)->lpinorms != NULL )
   {
      SCIP_CALL( SCIPlpiFreeNorms(lpi, blkmem, &(*warmstartinfo)->lpinorms) );
      (*warmstartinfo)->lpinorms = NULL;
   }

   SCIPfreeBlockMemory(scip, warmstartinfo);

   return SCIP_OKAY;
}

/** A struct containing all information needed to branch on a variable. */
typedef struct
{
   SCIP_VAR*             branchvar;          /**< the variable to branch on */
   SCIP_Real             branchval;          /**< the fractional value to branch on */
   SCIP_Real             fracval;            /**< the fractional part of the value to branch on (val - floor(val)) */
   WARMSTARTINFO*        downwarmstartinfo;  /**< the warm start info containing the lp data from a previous down branch */
   WARMSTARTINFO*        upwarmstartinfo;    /**< the warm start info containing the lp data from a previous up branch */
} CANDIDATE;

/** Allocates the candidate on the buffer and initializes it with default values. */
static
SCIP_RETCODE candidateCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATE**           candidate,          /**< the candidate to allocate and initialize */
   SCIP_Bool             storelpi            /**< should the candidate be able to store its lpi information? */
   )
{
   assert(scip != NULL);
   assert(candidate != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, candidate) );

   if( storelpi )
   {
      SCIP_CALL(warmStartInfoCreate(scip, &(*candidate)->downwarmstartinfo) );
      SCIP_CALL(warmStartInfoCreate(scip, &(*candidate)->upwarmstartinfo) );
   }
   else
   {
      (*candidate)->downwarmstartinfo = NULL;
      (*candidate)->upwarmstartinfo = NULL;
   }

   (*candidate)->branchvar = NULL;

   return SCIP_OKAY;
}

/** Frees the allocated buffer memory of the candidate and clears the contained lpi memories. */
static
SCIP_RETCODE candidateFree(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATE**           candidate           /**< the candidate to free */
   )
{
   assert(scip != NULL);
   assert(candidate != NULL);

   /* if a candidate is freed, we no longer need the content of the warm start info */
   if( (*candidate)->upwarmstartinfo != NULL )
   {
      SCIP_CALL( warmStartInfoFree(scip, &(*candidate)->upwarmstartinfo) );
   }
   if( (*candidate)->downwarmstartinfo != NULL )
   {
      SCIP_CALL( warmStartInfoFree(scip, &(*candidate)->downwarmstartinfo) );
   }

   SCIPfreeBlockMemory(scip, candidate);
   return SCIP_OKAY;
}

/** A struct acting as a fixed list of candidates */
typedef struct
{
   CANDIDATE**           candidates;         /**< the array of candidates */
   int                   ncandidates;        /**< the number of actual entries in candidates (without trailing NULLs); this
                                              *   is NOT the length of the candidates array, but the number of candidates in
                                              *   it */
} CANDIDATELIST;

/** Allocates the candidate list on the buffer WITHOUT initializing the contained array of candidates. */
static
SCIP_RETCODE candidateListCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATELIST**       list,               /**< the candidate list to allocate */
   int                   ncandidates         /**< the number of candidates the list must hold */
   )
{
   assert(scip != NULL);
   assert(list != NULL);
   assert(ncandidates > 0);

   SCIP_CALL( SCIPallocBuffer(scip, list) );
   (*list)->candidates = NULL;
   (*list)->ncandidates = 0;

   SCIP_CALL( SCIPallocBufferArray(scip, &(*list)->candidates, ncandidates) );
   (*list)->ncandidates = ncandidates;

   return SCIP_OKAY;
}

static
SCIP_RETCODE candidateListCreateWithCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATELIST**       list,               /**< the candidate list to allocate */
   int                   ncandidates,        /**< the number of candidates the list must hold */
   SCIP_Bool             storewarmstartinfo  /**< should the candidates be able to store warm start information? */
   )
{
   int i;

   SCIP_CALL( candidateListCreate(scip, list, ncandidates) );

   for( i = 0; i < ncandidates; i++ )
   {
      SCIP_CALL( candidateCreate(scip, &(*list)->candidates[i], storewarmstartinfo) );
   }

   return SCIP_OKAY;
}

/** Allocates the given list and fills it with all fractional candidates of the current LP solution. */
static
SCIP_RETCODE candidateListGetAllFractionalCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Bool             storewarmstartinfo, /**< should warm start info of the LP be stored in the candidates? */
   CANDIDATELIST**       candidatelist       /**< the list to allocate and fill */
   )
{
   SCIP_VAR** lpcands;
   SCIP_Real* lpcandssol;
   SCIP_Real* lpcandsfrac;
   int nlpcands;
   int i;

   assert(scip != NULL);
   assert(candidatelist != NULL);

   /* get all fractional candidates */
   SCIP_CALL( SCIPgetLPBranchCands(scip, &lpcands, &lpcandssol, &lpcandsfrac, &nlpcands, NULL, NULL) );

   assert(lpcands != NULL);
   assert(lpcandssol != NULL);
   assert(lpcandsfrac != NULL);

   SCIP_CALL( candidateListCreateWithCandidates(scip, candidatelist, nlpcands, storewarmstartinfo) );

   for( i = 0; i < nlpcands; i++ )
   {
      CANDIDATE* candidate = (*candidatelist)->candidates[i];

      candidate->branchvar = lpcands[i];
      candidate->branchval = lpcandssol[i];
      candidate->fracval = lpcandsfrac[i];
   }

   return SCIP_OKAY;
}

/** Frees the allocated buffer memory of the candidate list and frees the contained candidates. */
static
SCIP_RETCODE candidateListFree(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATELIST**       list                /**< the list to be freed */
   )
{
   int i;

   assert(scip != NULL);
   assert(list != NULL);
   assert((*list)->ncandidates >= 0);
   assert((*list)->ncandidates > 0 || (*list)->candidates == NULL);

   if( (*list)->candidates != NULL )
   {
      for( i = (*list)->ncandidates-1; i >= 0; i-- )
      {
         CANDIDATE* cand = (*list)->candidates[i];
         if( cand != NULL )
         {
            SCIP_CALL(candidateFree(scip, &cand));
         }
      }

      SCIPfreeBufferArray(scip, &(*list)->candidates);
   }
   SCIPfreeBuffer(scip, list);

   return SCIP_OKAY;
}

/** Keeps only the candidates specified by the indices array, sorted by the order in there */
static
SCIP_RETCODE candidateListKeep(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATELIST*        candidatelist,      /**< the list to allocate and fill */
   const int*            indices,            /**< the indices to keep (in the specified order) */
   int                   nindices            /**< the number of indices to keep */
   )
{
   CANDIDATELIST* tmplist;
   int i;

   assert(scip != NULL);
   assert(candidatelist != NULL);
   assert(indices != NULL);
   assert(0 < nindices);
   assert(nindices <= candidatelist->ncandidates);

   SCIP_CALL( candidateListCreate(scip, &tmplist, nindices) );

   /* move the candidates to keep to the tmplist */
   for( i = 0; i < nindices; i++ )
   {
      int index = indices[i];
      CANDIDATE* cand = candidatelist->candidates[index];
      assert(cand != NULL);

      tmplist->candidates[i] = cand;
      candidatelist->candidates[index] = NULL;
   }

   /* free the remaining candidates */
   for( i = 0; i < candidatelist->ncandidates; i++ )
   {
      CANDIDATE* cand = candidatelist->candidates[i];
      if( cand != NULL )
      {
         SCIP_CALL( candidateFree(scip, &cand) );
         candidatelist->candidates[i] = NULL;
      }
   }

   /* move the selected candidates back to the original list */
   for( i = 0; i < nindices; i++ )
   {
      assert(tmplist->candidates[i] != NULL);

      candidatelist->candidates[i] = tmplist->candidates[i];
      tmplist->candidates[i] = NULL;
   }
   candidatelist->ncandidates = tmplist->ncandidates;

   SCIP_CALL( candidateListFree(scip, &tmplist) );

   return SCIP_OKAY;
}

/** all domain reductions found through cutoff of branches */
typedef struct
{
   SCIP_Real*            lowerbounds;        /**< The new lower bounds found for each variable in the problem. */
   SCIP_Real*            upperbounds;        /**< The new upper bounds found for each variable in the problem. */
   SCIP_Bool*            lowerboundset;      /**< Indicates whether the lower bound may be added to the base node. */
   SCIP_Bool*            upperboundset;      /**< Indicates whether the upper bound may be added to the base node. */
   SCIP_Bool*            baselpviolated;     /**< Indicates whether the base lp solution violates the new bounds of a var.*/
   int                   nviolatedvars;      /**< Tracks the number of vars that have a violated (by the base lp) new lower
                                              *   or upper bound. */
   int                   nchangedvars;       /**< Tracks the number of vars, that have a changed domain. (a change on both,
                                              *   upper and lower bound, counts as one.) */
#ifdef SCIP_STATISTIC
   int*                  lowerboundnproofs;  /**< The number of nodes needed to proof the lower bound for each variable. */
   int*                  upperboundnproofs;  /**< The number of nodes needed to proof the upper bound for each variable. */
#endif
} DOMAINREDUCTIONS;

/** allocate the struct on the buffer and initialize it with the default values */
static
SCIP_RETCODE domainReductionsCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   DOMAINREDUCTIONS**    domreds             /**< The struct that has to be allocated and initialized. */
   )
{
   int ntotalvars;

   assert(scip != NULL);
   assert(domreds != NULL);

   /* The arrays saves the data for all variables in the problem via the ProbIndex. See SCIPvarGetProbindex() */
   ntotalvars = SCIPgetNVars(scip);

   /* Allocate the struct and the contained arrays; initialize flags to FALSE */
   SCIP_CALL( SCIPallocBuffer(scip, domreds) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*domreds)->lowerbounds, ntotalvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*domreds)->upperbounds, ntotalvars) );
   SCIP_CALL( SCIPallocClearBufferArray(scip, &(*domreds)->lowerboundset, ntotalvars) );
   SCIP_CALL( SCIPallocClearBufferArray(scip, &(*domreds)->upperboundset, ntotalvars) );
   SCIP_CALL( SCIPallocClearBufferArray(scip, &(*domreds)->baselpviolated, ntotalvars) );
#ifdef SCIP_STATISTIC
   SCIP_CALL( SCIPallocClearBufferArray(scip, &(*domreds)->lowerboundnproofs, ntotalvars) );
   SCIP_CALL( SCIPallocClearBufferArray(scip, &(*domreds)->upperboundnproofs, ntotalvars) );
#endif

   /* At the start we have no domain reductions for any variable. */
   (*domreds)->nviolatedvars = 0;
   (*domreds)->nchangedvars = 0;

   return SCIP_OKAY;
}

/** frees the given DOMAINREDUCTIONS and all contained Arrays in the opposite order of allocation */
static
void domainReductionsFree(
   SCIP*                 scip,               /**< SCIP data structure */
   DOMAINREDUCTIONS**    domreds             /**< Pointer to the struct to be freed. */
   )
{
   assert(scip != NULL);
   assert(domreds != NULL);

#ifdef SCIP_STATISTIC
   SCIPfreeBufferArray(scip, &(*domreds)->upperboundnproofs);
   SCIPfreeBufferArray(scip, &(*domreds)->lowerboundnproofs);
#endif
   SCIPfreeBufferArray(scip, &(*domreds)->baselpviolated);
   SCIPfreeBufferArray(scip, &(*domreds)->upperboundset);
   SCIPfreeBufferArray(scip, &(*domreds)->lowerboundset);
   SCIPfreeBufferArray(scip, &(*domreds)->upperbounds);
   SCIPfreeBufferArray(scip, &(*domreds)->lowerbounds);
   SCIPfreeBuffer(scip, domreds);
}

/** information about the current status of the branching */
typedef struct
{
   SCIP_Bool             addbinconst;        /**< Was a binary constraint added? */
   SCIP_Bool             depthtoosmall;      /**< Was the remaining depth too small to branch on? */
   SCIP_Bool             lperror;            /**< Resulted a LP solving in an error? */
   SCIP_Bool             cutoff;             /**< Was the current node cutoff? */
   SCIP_Bool             domredcutoff;       /**< Was the current node cutoff due to domain reductions? */
   SCIP_Bool             domred;             /**< Were domain reductions added due to information obtained through
                                              *   branching? */
   SCIP_Bool             propagationdomred;  /**< Were domain reductions added due to domain propagation? */
   SCIP_Bool             limitreached;       /**< Was a limit (time, node, user, ...) reached? */
   SCIP_Bool             maxnconsreached;    /**< Was the max number of constraints(bin const and dom red) reached? */
} STATUS;

/** Allocates the status on the buffer memory and initializes it with default values. */
static
SCIP_RETCODE statusCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS**              status              /**< the status to be allocated */
   )
{
   assert(scip != NULL);
   assert(status != NULL);

   SCIP_CALL( SCIPallocBuffer(scip, status) );

   (*status)->addbinconst = FALSE;
   (*status)->depthtoosmall = FALSE;
   (*status)->lperror = FALSE;
   (*status)->cutoff = FALSE;
   (*status)->domred = FALSE;
   (*status)->domredcutoff = FALSE;
   (*status)->propagationdomred = FALSE;
   (*status)->limitreached = FALSE;
   (*status)->maxnconsreached = FALSE;

   return SCIP_OKAY;
}

/** Frees the allocated buffer memory of the status. */
static
void statusFree(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS**              status              /**< the status to be freed */
   )
{
   assert(scip != NULL);
   assert(status != NULL);
   SCIPfreeBuffer(scip, status);
}

/** Container struct to keep the calculated score for each variable. */
typedef struct
{
   SCIP_Real*            scores;             /**< the scores for each problem variable */
   int*                  bestsortedindices;  /**< array containing the best sorted variable indices w.r.t. their score */
   int                   nbestsortedindices; /**< number of elements in bestsortedindices */
/* TODO: replace the int array by a CANDIDATE* array? */
} SCORECONTAINER;

/** Resets the array containing the sorted indices w.r.t. their score. */
static
void scoreContainterResetBestSortedIndices(
   SCORECONTAINER*       scorecontainer      /**< the score container to reset */
   )
{
   int i;

   for( i = 0; i < scorecontainer->nbestsortedindices; i++ )
   {
      scorecontainer->bestsortedindices[i] = -1;
   }
}

/** Allocates the score container and inits it with default values */
static
SCIP_RETCODE scoreContainerCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SCORECONTAINER**      scorecontainer,     /**< pointer to the score container to init */
   CONFIGURATION*        config              /**< config struct with the user configuration */
   )
{
   int ntotalvars;
   int i;

   assert(scip != NULL);
   assert(scorecontainer != NULL);

   /* The container saves the score for all variables in the problem via the ProbIndex. See SCIPvarGetProbindex() */
   ntotalvars = SCIPgetNVars(scip);

   SCIP_CALL( SCIPallocBuffer(scip, scorecontainer) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*scorecontainer)->scores, ntotalvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &(*scorecontainer)->bestsortedindices, config->maxncands) );

   (*scorecontainer)->nbestsortedindices = config->maxncands;

   scoreContainterResetBestSortedIndices(*scorecontainer);

   /* init the scores to something negative, as scores are always non negative */
   for( i = 0; i < ntotalvars; i++ )
   {
      (*scorecontainer)->scores[i] = -1.0;
   }

   return SCIP_OKAY;
}

/** Find the insertion index for the given score in the sorted array in the container */
static
int scoreContainerFindScoreInsertionPoint(
   SCIP*                 scip,               /**< SCIP data structure */
   SCORECONTAINER*       scorecontainer,     /**< container to look for an insertion point */
   SCIP_Real             score               /**< score to find the insertion for */
   )
{
   int left = 0;
   int right = scorecontainer->nbestsortedindices - 1;

   /* find the insertion point via binary search */
   while( left <= right )
   {
      int mid = left + ((right - left) / 2);
      int midindex = scorecontainer->bestsortedindices[mid];
      SCIP_Real midscore = -SCIPinfinity(scip);
      if( midindex >= 0 )
      {
         midscore = scorecontainer->scores[midindex];
      }

      if( SCIPisGT(scip, score, midscore) )
      {
         right = mid - 1;
      }
      else
      {
         left = mid + 1;
      }
   }
   return right + 1;
}

/** Inserts the given proindex into the sorted array in the container, moving all indices after it to the right. Then
 * returns the element that does not fit into the array any longer. */
static
int scoreContainerUpdateSortOrder(
   SCORECONTAINER*       scorecontainer,     /**< container to the insert the index into */
   int                   probindex,          /**< the probindex of a vairable to store */
   int                   insertpoint         /**< point to store the index at */
   )
{
   int i;
   int moveindex = probindex;

   for( i = insertpoint; i < scorecontainer->nbestsortedindices; i++ )
   {
      int oldindex = scorecontainer->bestsortedindices[i];
      scorecontainer->bestsortedindices[i] = moveindex;
      moveindex = oldindex;
   }
   return moveindex;
}

/** Sets the score for the variable in the score container. */
static
SCIP_RETCODE scoreContainerSetScore(
   SCIP*                 scip,               /**< SCIP data structure */
   SCORECONTAINER*       scorecontainer,     /**< the container to write into */
   CANDIDATELIST*        candidates,         /**< TODO: to be removed */
   SCIP_VAR*             var,                /**< variable to add the score for */
   SCIP_Real             score               /**< score to add */
   )
{
   int probindex;
   int insertpoint;
   int droppedprobindex;
   int i;

   assert(scip != NULL);
   assert(scorecontainer != NULL);
   assert(var != NULL);
   assert(SCIPisGE(scip, score, 0.0));

   probindex = SCIPvarGetProbindex(var);

   scorecontainer->scores[probindex] = score;

   /* find the point in the sorted array where the new score should be inserted */
   insertpoint = scoreContainerFindScoreInsertionPoint(scip, scorecontainer, score);

   /* insert the current variable (probindex) at the position calculated above, returning the index of the variable that
    * was removed at the end of the list; this index can be the given probindex for the case that the score does not
    * belong to the best ones */
   droppedprobindex = scoreContainerUpdateSortOrder(scorecontainer, probindex, insertpoint);

   /* remove the warm start info from the dropped candidate */
   /* TODO: maybe rework to use a map probindex->CANDIDATE*? */
   for( i = 0; i < candidates->ncandidates; i++ )
   {
      CANDIDATE* cand = candidates->candidates[i];
      probindex = SCIPvarGetProbindex(cand->branchvar);

      if( droppedprobindex == probindex )
      {
         SCIP_CALL( warmStartInfoFree(scip, &cand->downwarmstartinfo) );
         SCIP_CALL( warmStartInfoFree(scip, &cand->upwarmstartinfo) );
      }
   }

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Stored score <%g> for var <%s>.\n", score, SCIPvarGetName(var));
   return SCIP_OKAY;
}

/** Frees the score container and all of its contained arrays. */
static
SCIP_RETCODE scoreContainerFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCORECONTAINER**      scorecontainer      /**< score container to free */
   )
{
   assert(scip != NULL);
   assert(scorecontainer != NULL);

   SCIPfreeBufferArray(scip, &(*scorecontainer)->bestsortedindices);
   SCIPfreeBufferArray(scip, &(*scorecontainer)->scores);
   SCIPfreeBuffer(scip, scorecontainer);

   return SCIP_OKAY;
}


/*
 * Local methods for the logic
 */

/** branches recursively on all given candidates */
static
SCIP_RETCODE selectVarRecursive(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS*               status,             /**< current status */
   PERSISTENTDATA*       persistent,         /**< container to store data over multiple calls to the branching rule */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCIP_SOL*             baselpsol,          /**< base lp solution */
   DOMAINREDUCTIONS*     domainreductions,   /**< container collecting all domain reductions found */
   BINCONSDATA*          binconsdata,        /**< container collecting all binary constraints */
   CANDIDATELIST*        candidates,         /**< list of candidates to branch on */
   BRANCHINGDECISION*    decision,           /**< struct to store the final decision */
   SCORECONTAINER*       scorecontainer,     /**< container to retrieve already calculated scores */
   SCIP_Bool             storewarmstartinfo, /**< should lp information be stored? */
   int                   recursiondepth,     /**< remaining recursion depth */
   SCIP_Real             lpobjval,           /**< base LP objective value */
   SCIP_Longint*         niterations         /**< pointer to store total number of iterations for this variable */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< general statistical data */
   ,LOCALSTATISTICS*     localstats          /**< local statistics, may be disregarded */
#endif
   );

/** starting point to obtain a branching decision via LAB/ALAB. */
static
SCIP_RETCODE selectVarStart(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   PERSISTENTDATA*       persistent,         /**< container to store data over multiple calls to the branching rule */
   STATUS*               status,             /**< current status */
   BRANCHINGDECISION*    decision,           /**< struct to store the final decision */
   SCORECONTAINER*       scorecontainer,     /**< container to retrieve already calculated scores */
   SCIP_Bool             storewarmstartinfo, /**< should lp information be stored? */
   CANDIDATELIST*        candidates          /**< list of candidates to branch on */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< general statistical data */
   ,LOCALSTATISTICS*     localstats          /**< local statistics, may be disregarded */
#endif
   );

/** Adds the given lower bound to the container struct. */
static
void addLowerBoundProofNode(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< The variable the bound should be added for. */
   SCIP_Real             lowerbound,         /**< The new lower bound for the variable. */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     domainreductions,   /**< The struct the domain reduction should be added to. */
   SCIP_Bool*            domredadded         /**< Pointer to store whether a domain reduction was already added. */
#ifdef SCIP_STATISTIC
   ,int                  nproofnodes         /**< The number of nodes needed to proof the new lower bound. */
#endif
   )
{
   int varindex;
   SCIP_Real basesolutionval;
   SCIP_Real newlowerbound;
   SCIP_Real oldlowerbound;
#ifdef SCIP_STATISTIC
   int newnproof;
#endif

   assert(scip != NULL);
   assert(var != NULL);
   assert(baselpsol != NULL);
   assert(domainreductions != NULL);
#ifdef SCIP_STATISTIC
   assert(nproofnodes >= 0);
#endif

   /* The arrays inside DOMAINREDUCTIONS are indexed via the problem index. */
   varindex = SCIPvarGetProbindex(var);

   lowerbound = SCIPadjustedVarLb(scip, var, lowerbound);
   oldlowerbound = SCIPvarGetLbLocal(var);

   /* We want to use the proposed lower bound only if it is stronger than the current lower bound.
    * The current lower bound may have changed due to conflict analysis etc. */
   if( SCIPisGT(scip, lowerbound, oldlowerbound) )
   {
      /* If we have an old lower bound we take the stronger one, so the MAX is taken. Otherwise we use the new lower bound
       * directly. */
      if( domainreductions->lowerboundset[varindex] )
      {
         if( SCIPisGE(scip, domainreductions->lowerbounds[varindex], lowerbound) )
         {
            /* the old lower bound is stronger (greater) than or equal to the given one, so we keep the older */
            newlowerbound = domainreductions->lowerbounds[varindex];
#ifdef SCIP_STATISTIC
            if( SCIPisEQ(scip, domainreductions->lowerbounds[varindex], lowerbound) )
            {
               /* if the given lower bound is equal to the old one we take the smaller number of proof nodes */
               newnproof = MIN(domainreductions->lowerboundnproofs[varindex], nproofnodes);
            }
            else
            {
               /* if the old bound is stronger (greater) we keep the old number of proof nodes */
               newnproof = domainreductions->lowerboundnproofs[varindex];
            }
#endif
         }
         else
         {
            /* the new lower bound is stronger (greater) than the old one,
             * so we update the bound and number of proof nodes */
            newlowerbound = lowerbound;
#ifdef SCIP_STATISTIC
            newnproof = nproofnodes;
#endif
         }
      }
      else
      {
         /* we have no old lower bound, so we can directly take the new one together with the number of proof nodes */
         newlowerbound = lowerbound;
#ifdef SCIP_STATISTIC
         newnproof = nproofnodes;
#endif

         if( !domainreductions->upperboundset[varindex] )
         {
            /* if we had no lower and upper bound set we now have a changed variable */
            domainreductions->nchangedvars++;
         }
      }

      /* set the calculated values */
      domainreductions->lowerbounds[varindex] = newlowerbound;
      domainreductions->lowerboundset[varindex] = TRUE;
#ifdef SCIP_STATISTIC
      domainreductions->lowerboundnproofs[varindex] = newnproof;
#endif

      /* We get the solution value to check whether the domain reduction is violated in the base LP */
      basesolutionval = SCIPgetSolVal(scip, baselpsol, var);

      /* In case the new lower bound is greater than the base solution val and the base solution val is not violated by a
       * previously found bound, we increment the nviolatedvars counter and set the baselpviolated flag.  */
      if( SCIPisGT(scip, newlowerbound, basesolutionval) && !domainreductions->baselpviolated[varindex] )
      {
         domainreductions->baselpviolated[varindex] = TRUE;
         domainreductions->nviolatedvars++;
      }
   }
   else if( domredadded != NULL )
   {
      /* the domain was already changed by the sb probing and/or conflict analysis */
      *domredadded = TRUE;
   }
}

/** Add a lower bound to the DOMAINREDUCTIONS struct. This is used as a wrapper to the 'addLowerBoundProofNode' method. */
static
void addLowerBound(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< The variable the bound should be added for. */
   SCIP_Real             lowerbound,         /**< The new lower bound for the variable. */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     domainreductions,   /**< The struct the domain reduction should be added to. */
   SCIP_Bool*            domredadded         /**< Pointer to store whether a domain reduction was already added. */
   )
{
   /* We add the lower bound with number of proof nodes 2, as this method is only called from the recursion directly. There
    * it is called in case that only one child node is cutoff. As proof nodes we count the cutoff node as well as the valid
    * node, as we need both to proof, that this domain reduction is valid. */
#ifdef SCIP_STATISTIC
   addLowerBoundProofNode(scip, var, lowerbound, baselpsol, domainreductions, domredadded, 2);
#else
   addLowerBoundProofNode(scip, var, lowerbound, baselpsol, domainreductions, domredadded);
#endif
}

/** Adds the given upper bound to the container struct. */
static
void addUpperBoundProofNode(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< The variable the bound should be added for. */
   SCIP_Real             upperbound,         /**< The new upper bound for the variable. */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     domainreductions,   /**< The struct the domain reduction should be added to. */
   SCIP_Bool*            domredadded         /**< Pointer to store whether a domain reduction was already added. */
#ifdef SCIP_STATISTIC
   ,int                  nproofnodes         /**< The number of nodes needed to proof the new lower bound. */
#endif
   )
{
   int varindex;
   SCIP_Real basesolutionval;
   SCIP_Real newupperbound;
   SCIP_Real oldupperbound;
#ifdef SCIP_STATISTIC
   int newnproof;
#endif

   assert(scip != NULL);
   assert(var != NULL);
   assert(baselpsol != NULL);
   assert(domainreductions != NULL);
#ifdef SCIP_STATISTIC
   assert(nproofnodes >= 0);
#endif

   /* The arrays inside DOMAINREDUCTIONS are indexed via the problem index. */
   varindex = SCIPvarGetProbindex(var);

   upperbound = SCIPadjustedVarUb(scip, var, upperbound);
   oldupperbound = SCIPvarGetUbLocal(var);

   /* We want to use the proposed upper bound only if it is stronger than the current upper bound.
    * The current upper bound may have changed due to conflict analysis etc. */
   if( SCIPisLT(scip, upperbound, oldupperbound) )
   {
      /* If we have an old upper bound we take the stronger one, so the MIN is taken. Otherwise we use the new upper bound
       * directly. */
      if( domainreductions->upperboundset[varindex] )
      {
         if( SCIPisLE(scip, domainreductions->upperbounds[varindex], upperbound) )
         {
            /* the old upper bound is stronger (smaller) than or equal to the given one, so we keep the older */
            newupperbound = domainreductions->upperbounds[varindex];
#ifdef SCIP_STATISTIC
               if( SCIPisEQ(scip, domainreductions->upperbounds[varindex], upperbound) )
               {
                  /* if the given upper bound is equal to the old one we take the smaller number of proof nodes */
                  newnproof = MIN(domainreductions->upperboundnproofs[varindex], nproofnodes);
               }
               else
               {
                  /* if the old bound is stronger (smaller) we keep the old number of proof nodes */
                  newnproof = domainreductions->upperboundnproofs[varindex];
               }
#endif
         }
         else
         {
            /* the new upper bound is stronger (smaller) than the old one,
             * so we update the bound and number of proof nodes */
            newupperbound = upperbound;
#ifdef SCIP_STATISTIC
            newnproof = nproofnodes;
#endif
         }
      }
      else
      {
         /* we have no old upper bound, so we can directly take the new one together with the number of proof nodes */
         newupperbound = upperbound;
#ifdef SCIP_STATISTIC
         newnproof = nproofnodes;
#endif

         if( !domainreductions->lowerboundset[varindex] )
         {
            /* if we had no lower and upper bound set we now have a changed variable */
            domainreductions->nchangedvars++;
         }
      }

      /* set the calculated values */
      domainreductions->upperbounds[varindex] = newupperbound;
      domainreductions->upperboundset[varindex] = TRUE;
#ifdef SCIP_STATISTIC
      domainreductions->upperboundnproofs[varindex] = newnproof;
#endif

      /* We get the solution value to check whether the domain reduction is violated in the base LP */
      basesolutionval = SCIPgetSolVal(scip, baselpsol, var);

      /* In case the new upper bound is lesser than the base solution val and the base solution val is not violated by a
       * previously found bound, we increment the nviolatedvars counter and set the baselpviolated flag.  */
      if( SCIPisLT(scip, newupperbound, basesolutionval) && !domainreductions->baselpviolated[varindex] )
      {
         domainreductions->baselpviolated[varindex] = TRUE;
         domainreductions->nviolatedvars++;
      }
   }
   else if( domredadded != NULL )
   {
      /* the domain was already changed by the sb probing and/or conflict analysis */
      *domredadded = TRUE;
   }
}

/** Add a lower bound to the DOMAINREDUCTIONS struct. This is used as a wrapper to the 'addUpperBoundProofNode' method. */
static
void addUpperBound(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< The variable the bound should be added for. */
   SCIP_Real             upperbound,         /**< The new upper bound for the variable. */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     domainreductions,   /**< The struct the domain reduction should be added to. */
   SCIP_Bool*            domredadded         /**< Pointer to store whether a domain reduction was already added. */
   )
{
   /* We add the upper bound with number of proof nodes 2, as this method is only called from the recursion directly. There
    * it is called in case that only one child node is cutoff. As proof nodes we count the cutoff node as well as the valid
    * node, as we need both to proof, that this domain reduction is valid. */
#ifdef SCIP_STATISTIC
   addUpperBoundProofNode(scip, var, upperbound, baselpsol, domainreductions, domredadded, 2);
#else
   addUpperBoundProofNode(scip, var, upperbound, baselpsol, domainreductions, domredadded);
#endif
}

/**
 * merges the domain reduction data from the two given branching childs data into the target parent data
 */
static
void applyDeeperDomainReductions(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS*               status,             /**< the status struct to store whether a domain reduction was already added */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     targetdomreds,      /**< The target that should be filled with the merged data. */
   DOMAINREDUCTIONS*     downdomreds,        /**< One of the source DOMAINREDUCTIONS. */
   DOMAINREDUCTIONS*     updomreds           /**< The other source DOMAINREDUCTIONS. */
   )
{
   SCIP_VAR** vars;
   int nvars;
   int i;

   assert(scip != NULL);
   assert(baselpsol != NULL);
   assert(targetdomreds != NULL);
   assert(downdomreds != NULL);
   assert(updomreds != NULL);

   /* as the bounds are tracked for all vars we have to iterate over all vars */
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   assert(vars != NULL);
   assert(nvars > 0);

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Combining domain reductions from up and down child.\n");
   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Previous number of changed variable domains: %d\n",
         targetdomreds->nchangedvars);

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Number of changed variable domains in up child: %d\n",
         updomreds->nchangedvars);
   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Number of changed variable domains in down child: %d\n",
         downdomreds->nchangedvars);

   for( i = 0; i < nvars; i++ )
   {
      assert(vars[i] != NULL);

      /* If not both child branches have a lower bound for a var, we cannot apply the lower bound to the parent */
      if( downdomreds->lowerboundset[i] && updomreds->lowerboundset[i] )
      {
         SCIP_Real newlowerbound;
#ifdef SCIP_STATISTIC
         int newnproofs;
#endif

         /* If both child branches have a lower bound for a var, the MIN of both values represents a valid lower bound */
         newlowerbound = MIN(downdomreds->lowerbounds[i], updomreds->lowerbounds[i]);
#ifdef SCIP_STATISTIC
         newnproofs = downdomreds->lowerboundnproofs[i] + updomreds->lowerboundnproofs[i] + 2;
#endif

         /* This MIN can now be added via the default add method */
#ifdef SCIP_STATISTIC
         addLowerBoundProofNode(scip, vars[i], newlowerbound, baselpsol, targetdomreds, &status->domred, newnproofs);
#else
         addLowerBoundProofNode(scip, vars[i], newlowerbound, baselpsol, targetdomreds, &status->domred);
#endif
      }

      /* If not both child branches have an upper bound for a var, we cannot apply the upper bound to the parent */
      if( downdomreds->upperboundset[i] && updomreds->upperboundset[i] )
      {
         SCIP_Real newupperbound;
#ifdef SCIP_STATISTIC
         int newnproofs;
#endif

         /* If both child branches have an upper bound for a var, the MAX of both values represents a valid upper bound */
         newupperbound = MAX(downdomreds->upperbounds[i], updomreds->upperbounds[i]);
#ifdef SCIP_STATISTIC
         newnproofs = downdomreds->upperboundnproofs[i] + updomreds->upperboundnproofs[i] + 2;
#endif

         /* This MAX can now be added via the default add method */
#ifdef SCIP_STATISTIC
         addUpperBoundProofNode(scip, vars[i], newupperbound, baselpsol, targetdomreds, &status->domred, newnproofs);
#else
         addUpperBoundProofNode(scip, vars[i], newupperbound, baselpsol, targetdomreds, &status->domred);
#endif
      }
   }

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Subsequent number of changed variable domains: %d\n",
         targetdomreds->nchangedvars);
}

/** Applies the domain reductions to the current node. */
static
SCIP_RETCODE applyDomainReductions(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             baselpsol,          /**< The LP solution of the base problem. Used to check whether the domain
                                              *   reduction is violated by it. */
   DOMAINREDUCTIONS*     domreds,            /**< The domain reductions that should be applied the current node. */
   SCIP_Bool*            domredcutoff,       /**< pointer to store whether a cutoff was found due to domain reductions */
   SCIP_Bool*            domred              /**< pointer to store whether a domain change was added */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< The statistics container. */
#endif
   )
{
   int i;
   SCIP_VAR** probvars;
   int nprobvars;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
   int nboundsadded = 0;
   int nboundsaddedvio = 0;
#endif

   assert(scip != NULL);
   assert(baselpsol != NULL);
   assert(domreds != NULL);
   assert(domredcutoff != NULL);
   assert(domred != NULL);
#ifdef SCIP_STATISTIC
   assert(statistics != NULL);
#endif

   /* initially we have no cutoff */
   *domredcutoff = FALSE;

   /* as the bounds are tracked for all vars we have to iterate over all vars */
   probvars = SCIPgetVars(scip);
   nprobvars = SCIPgetNVars(scip);

   assert(probvars != NULL);
   assert(nprobvars > 0);

   for( i = 0; i < nprobvars && !(*domredcutoff); i++ )
   {
      SCIP_VAR* var;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
      SCIP_Real baselpval;
#endif

      var = probvars[i];

      assert(var != NULL);

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
      baselpval = SCIPgetSolVal(scip, baselpsol, var);
#endif

      if( domreds->lowerboundset[i] )
      {
         /* if we have a lower bound for the current var, apply it */
         SCIP_Bool infeasible;
         SCIP_Bool tightened;
         SCIP_Real proposedlowerbound;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
         SCIP_Real newlowerbound;
#ifdef SCIP_DEBUG
         SCIP_Real oldlowerbound;
#endif
#endif

         /* get the old and the new lower bound */
#ifdef SCIP_DEBUG
         oldlowerbound = SCIPvarGetLbLocal(var);
#endif
         proposedlowerbound = domreds->lowerbounds[i];

         /* add the new bound */
         SCIP_CALL( SCIPtightenVarLb(scip, var, proposedlowerbound, TRUE, &infeasible, &tightened) );

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
         newlowerbound = SCIPvarGetLbLocal(var);
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Variable <%s>, old lower bound <%g>, proposed lower bound <%g>, new "
               "lower bound <%g>\n", SCIPvarGetName(var), oldlowerbound, proposedlowerbound, newlowerbound);
#endif

         if( infeasible )
         {
            /* the domain reduction may result in an empty model (ub < lb) */
            *domredcutoff = TRUE;
            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The domain reduction of variable <%s> resulted in an empty "
                  "model.\n", SCIPvarGetName(var));
         }
         else if( tightened )
         {
            /* the lb is now strictly greater than before */
            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The lower bound of variable <%s> was successfully tightened.\n",
                  SCIPvarGetName(var));
            *domred = TRUE;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
            nboundsadded++;
#endif
#ifdef SCIP_STATISTIC
            statistics->ndomredproofnodes += domreds->lowerboundnproofs[i];
#endif

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
            if( SCIPisLT(scip, baselpval, newlowerbound) )
            {
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The lower bound of variable <%s> is violated by the base lp "
                     "value <%g>.\n", SCIPvarGetName(var), baselpval);

               nboundsaddedvio++;
            }
#endif
         }
      }

      if( !(*domredcutoff) && domreds->upperboundset[i] )
      {
         SCIP_Bool infeasible;
         SCIP_Bool tightened;
         SCIP_Real proposedupperbound;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
         SCIP_Real newupperbound;
#ifdef SCIP_DEBUG
         SCIP_Real oldupperbound;
#endif
#endif

         /* get the old and the new upper bound */
#ifdef SCIP_DEBUG
         oldupperbound = SCIPvarGetUbLocal(var);
#endif
         proposedupperbound = domreds->upperbounds[i];

         /* add the new bound */
         SCIP_CALL( SCIPtightenVarUb(scip, var, proposedupperbound, TRUE, &infeasible, &tightened) );

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
         newupperbound = SCIPvarGetUbLocal(var);
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Variable <%s>, old upper bound <%g>, proposed upper bound <%g>, new "
               "upper bound <%g>\n", SCIPvarGetName(var), oldupperbound, proposedupperbound, newupperbound);
#endif

         if( infeasible )
         {
            /* the domain reduction may result in an empty model (ub < lb) */
            *domredcutoff = TRUE;
            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The domain reduction of variable <%s> resulted in an empty "
                  "model.\n", SCIPvarGetName(var));
         }
         else if( tightened )
         {
            /* the ub is now strictly smaller than before */
            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The upper bound of variable <%s> was successfully tightened.\n",
                  SCIPvarGetName(var));
            *domred = TRUE;
#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
            nboundsadded++;
#endif
#ifdef SCIP_STATISTIC
            statistics->ndomredproofnodes += domreds->upperboundnproofs[i];
#endif

#if defined(SCIP_DEBUG) || defined(SCIP_STATISTIC)
            if( SCIPisGT(scip, baselpval, newupperbound) )
            {
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The upper bound of variable <%s> is violated by the base lp "
                     "value <%g>.\n", SCIPvarGetName(var), baselpval);

               nboundsaddedvio++;
            }
#endif
         }
      }
   }

#ifdef SCIP_STATISTIC
   statistics->ndomred += nboundsadded;
   statistics->ndomredvio += nboundsaddedvio;
#endif

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Truly changed <%d> domains of the problem, <%d> of them are violated by the "
         "base lp.\n", nboundsadded, nboundsaddedvio);
   return SCIP_OKAY;
}

/** Copies the current LP solution into the given pointer. Needs to be freed after usage! */
static
SCIP_RETCODE copyCurrentSolution(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL**            lpsol               /**< pointer to store the solution into */
   )
{
   /* create temporary solution */
   SCIP_CALL( SCIPcreateLPSol(scip, lpsol, NULL) );

   /* unlink the solution, so that newly solved lps don't have any influence on our copy */
   SCIP_CALL( SCIPunlinkSol(scip, *lpsol) );

   return SCIP_OKAY;
}

/** Executes the branching on a given variable with a given value. */
static
SCIP_RETCODE branchOnVar(
   SCIP*                 scip                /**< SCIP data structure */,
   BRANCHINGDECISION*    decision            /**< the decision with all the needed data */
   )
{
   SCIP_VAR* bestvar = decision->var;
   SCIP_Real bestval = decision->val;

   SCIP_NODE* downchild = NULL;
   SCIP_NODE* upchild = NULL;

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Effective branching on var <%s> with value <%g>. Old domain: [%g..%g].\n",
      SCIPvarGetName(bestvar), bestval, SCIPvarGetLbLocal(bestvar), SCIPvarGetUbLocal(bestvar));

   assert(!SCIPisIntegral(scip, bestval));

   /* branch on the given variable */
   SCIP_CALL( SCIPbranchVarVal(scip, bestvar, bestval, &downchild, NULL, &upchild) );

   assert(downchild != NULL);
   assert(upchild != NULL);

   /* update the lower bounds in the children; we must not do this if columns are missing in the LP
    * (e.g. because we are doing branch-and-price) or the problem should be solved exactly */
   if( SCIPallColsInLP(scip) && !SCIPisExactSolve(scip) )
   {
      SCIP_Real bestdown = decision->downdb;
      SCIP_Bool bestdownvalid = decision->downdbvalid;
      SCIP_Real bestup = decision->updb;
      SCIP_Bool bestupvalid = decision->updbvalid;
      SCIP_Real provedbound = decision->proveddb;

      /* update the lower bound for the LPs for further children of both created nodes */
      SCIP_CALL( SCIPupdateNodeLowerbound(scip, downchild, bestdownvalid ? MAX(bestdown, provedbound) : provedbound) );
      SCIP_CALL( SCIPupdateNodeLowerbound(scip, upchild, bestupvalid ? MAX(bestup, provedbound) : provedbound) );
   }
   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, " -> down child's lowerbound: %g\n", SCIPnodeGetLowerbound(downchild));
   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, " -> up child's lowerbound: %g\n", SCIPnodeGetLowerbound(upchild));

   return SCIP_OKAY;
}

/** Store the current lp solution in the warm start info for further usage. */
static
SCIP_RETCODE storeWarmStartInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   WARMSTARTINFO*        warmstartinfo       /**< the warm start info in which the data should be stored */
   )
{
   SCIP_LPI* lpi;
   BMS_BLKMEM* blkmem;

   assert(scip != NULL);
   assert(warmstartinfo != NULL);
   assert(warmstartinfo->lpistate == NULL);
   assert(warmstartinfo->lpinorms == NULL);

   SCIP_CALL( SCIPgetLPI(scip, &lpi) );
   blkmem = SCIPblkmem(scip);

   SCIP_CALL( SCIPlpiGetState(lpi, blkmem, &warmstartinfo->lpistate) );

   SCIP_CALL( SCIPlpiGetNorms(lpi, blkmem, &warmstartinfo->lpinorms) );

   warmstartinfo->primalfeas = SCIPlpiIsPrimalFeasible(lpi);
   warmstartinfo->dualfeas = SCIPlpiIsDualFeasible(lpi);

   assert(warmstartinfo->lpistate != NULL);
   /* warmstartinfo->lpinorms may be NULL */

   return SCIP_OKAY;
}

/** Sets the lp state and norms of the current node to the values stored in the warm start info. */
static
SCIP_RETCODE restoreFromWarmStartInfo(
   SCIP*                 scip,               /**< SCIP data structure */
   WARMSTARTINFO*        warmstartinfo       /**< the warm start info containing the stored data */
   )
{
   assert(scip != NULL);
   assert(warmstartinfo != NULL);
   assert(warmstartinfo->lpistate != NULL);

   /* as we solved the very same LP some time earlier and stored the state (the basis) and the norms we can now set those in
    * the LP solver, such that the solution does not (in best case) need any further calculation.
    * Some iterations may occur, as the conflict analysis may have added some constraints in the meantime. */
   SCIP_CALL( SCIPsetProbingLPState(scip, &(warmstartinfo->lpistate), &(warmstartinfo->lpinorms), warmstartinfo->primalfeas,
      warmstartinfo->dualfeas) );

   /* The state and norms will be freed later by the SCIP framework. Therefore they are set to NULL to enforce that we won't
    * free them on my own. */
   assert(warmstartinfo->lpistate == NULL);
   assert(warmstartinfo->lpinorms == NULL);

   return SCIP_OKAY;
}

/** Get the number of iterations the last LP needed */
static
SCIP_RETCODE getNIterationsLastLP(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Longint*         iterations          /**< pointer to store the number of iterations */
   )
{
   SCIP_LPI* lpi;
   int tmpiter;

   assert(scip != NULL);
   assert(iterations != NULL);

   /* get the LP interface of the last solved LP */
   SCIP_CALL( SCIPgetLPI(scip, &lpi) );

   /* get the number of iterations from the interface */
   SCIP_CALL( SCIPlpiGetIterations(lpi, &tmpiter) );

   *iterations = (SCIP_Longint)tmpiter;

   return SCIP_OKAY;
}


/** Creates a new probing node with a new bound for the given candidate. */
static
SCIP_RETCODE executeBranching(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< configuration to control the behavior */
   SCIP_Bool             downbranching,      /**< the branching direction */
   CANDIDATE*            candidate,          /**< the candidate to branch on */
   BRANCHINGRESULTDATA*  resultdata,         /**< pointer to the result data which gets filled with the status */
   SCIP_SOL*             baselpsol,          /**< the base lp solution */
   DOMAINREDUCTIONS*     domreds,            /**< struct to store the domain reduction found during propagation */
   STATUS*               status              /**< status will contain updated lperror and limit fields */
   )
{
   SCIP_Real oldupperbound;
   SCIP_Real oldlowerbound;
   SCIP_Real newbound;
   SCIP_LPSOLSTAT solstat;
   SCIP_VAR* branchvar;
   SCIP_Real branchval;

   assert(scip != NULL);
   assert(candidate != NULL);
   assert(resultdata != NULL);
   assert(status != NULL);

   branchvar = candidate->branchvar;
   branchval = candidate->branchval;

   assert(branchvar != NULL);
   assert(!SCIPisFeasIntegral(scip, branchval));

   if( downbranching )
   {
      /* round the given value down, so that it can be used as the new upper bound */
      newbound = SCIPfeasFloor(scip, branchval);
   }
   else
   {
      /* round the given value up, so that it can be used as the new lower bound */
      newbound = SCIPfeasCeil(scip, branchval);
   }

   oldupperbound = SCIPvarGetUbLocal(branchvar);
   oldlowerbound = SCIPvarGetLbLocal(branchvar);

#ifdef SCIP_DEBUG
   if( downbranching )
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "DownBranching: Var=<%s>, Proposed upper bound=<%g>, "
            "old bounds=[<%g>..<%g>], new bounds=[<%g>..<%g>]\n", SCIPvarGetName(branchvar), newbound, oldlowerbound,
            oldupperbound, oldlowerbound, newbound);
   }
   else
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "UpBranching: Var=<%s>, Proposed lower bound=<%g>, "
            "old bounds=[<%g>..<%g>], new bounds=[<%g>..<%g>]\n", SCIPvarGetName(branchvar), newbound, oldlowerbound,
            oldupperbound, newbound, oldupperbound);
   }
#endif

   if( (downbranching && newbound < oldlowerbound - 0.5)
      || (!downbranching && newbound > oldupperbound + 0.5) )
   {
      /* if lb > ub we can cutoff this node */
      resultdata->cutoff = TRUE;
   }
   else
   {
      if( !resultdata->cutoff )
      {
         SCIP_CALL( SCIPnewProbingNode(scip) );

         if( downbranching )
         {
            /* down branching preparations */
            if( SCIPisFeasLT(scip, newbound, oldupperbound) )
            {
               /* if the new upper bound is lesser than the old upper bound and also
                * greater than (or equal to) the old lower bound we set the new upper bound.
                * oldLowerBound <= newUpperBound < oldUpperBound */
               SCIP_CALL( SCIPchgVarUbProbing(scip, branchvar, newbound) );
            }

            if( warmStartInfoIsReadable(candidate->downwarmstartinfo) )
            {
               /* restore the stored LP data (e.g. the basis) from a previous run */
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Restoring lp information for down branch of variable <%s>\n",
                  SCIPvarGetName(branchvar));
               SCIP_CALL(restoreFromWarmStartInfo(scip, candidate->downwarmstartinfo) );
            }
         }
         else
         {
            /* up branching preparations */
            if( SCIPisFeasGT(scip, newbound, oldlowerbound) )
            {
               /* if the new lower bound is greater than the old lower bound and also
                * lesser than (or equal to) the old upper bound we set the new lower bound.
                * oldLowerBound < newLowerBound <= oldUpperBound */
               SCIP_CALL( SCIPchgVarLbProbing(scip, branchvar, newbound) );
            }

            if( warmStartInfoIsReadable(candidate->upwarmstartinfo) )
            {
               /* restore the stored LP data (e.g. the basis) from a previous run */
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Restoring lp information for up branch of variable <%s>\n",
                  SCIPvarGetName(branchvar));
               SCIP_CALL(restoreFromWarmStartInfo(scip, candidate->upwarmstartinfo) );
            }
         }

         if( config->propagate )
         {
            SCIP_Longint ndomredsfound = 0;

            SCIP_CALL( SCIPpropagateProbing(scip, -1, &resultdata->cutoff, &ndomredsfound) );

            if( ndomredsfound > 0 )
            {
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Found %d domain reductions via propagation.\n", ndomredsfound);

               /* domreds != NULL iff config->usedomainreduction */
               if( domreds != NULL )
               {
                  int i;
                  SCIP_VAR** problemvars = SCIPgetVars(scip);
                  int nproblemvars = SCIPgetNVars(scip);

                  for( i = 0; i < nproblemvars; i++ )
                  {
                     SCIP_VAR* var = problemvars[i];

                     SCIP_Real lowerbound = SCIPvarGetLbLocal(var);
                     SCIP_Real upperbound = SCIPvarGetUbLocal(var);

                     addLowerBound(scip, var, lowerbound, baselpsol, domreds, NULL);
                     addUpperBound(scip, var, upperbound, baselpsol, domreds, NULL);
                  }
               }
            }
         }

         if( !resultdata->cutoff )
         {
            /* solve the prepared probing LP */
            SCIP_CALL( SCIPsolveProbingLP(scip, -1, &status->lperror, &resultdata->cutoff) );

            /* store the number of iterations needed */
            SCIP_CALL( getNIterationsLastLP(scip, &resultdata->niterations) );

            solstat = SCIPgetLPSolstat(scip);
            assert(solstat != SCIP_LPSOLSTAT_UNBOUNDEDRAY);

            /* for us an error occurred, if an error during the solving occurred, or the lp could not be solved but was not
             * cutoff, or if the iter or time limit was reached. */
            status->lperror = status->lperror || (solstat == SCIP_LPSOLSTAT_NOTSOLVED && resultdata->cutoff == FALSE);

            /* if we seem to have reached a {time, iteration}-limit or the user cancelled the execution we want to stop
             * further calculations and instead return the current calculation state */
            status->limitreached = solstat == SCIP_LPSOLSTAT_ITERLIMIT || solstat == SCIP_LPSOLSTAT_TIMELIMIT;

            if( resultdata->cutoff )
            {
               resultdata->objval = SCIPinfinity(scip);
               resultdata->dualbound = SCIPinfinity(scip);
               resultdata->dualboundvalid = TRUE;
            }
            else if( !status->limitreached && !status->lperror )
            {
               SCIP_Bool foundsol = FALSE;

               SCIP_CALL( SCIPtryStrongbranchLPSol(scip, &foundsol, &resultdata->cutoff) );

               /* if we have no error, we save the new objective value and the cutoff decision in the resultdata */
               resultdata->objval = SCIPgetLPObjval(scip);
               resultdata->dualbound = SCIPgetLPObjval(scip);
               resultdata->dualboundvalid = TRUE;
               resultdata->cutoff = resultdata->cutoff || SCIPisGE(scip, resultdata->objval, SCIPgetCutoffbound(scip));

               assert(solstat != SCIP_LPSOLSTAT_INFEASIBLE || resultdata->cutoff);
            }
         }
      }
   }

   return SCIP_OKAY;
}

/** Creates a logic or constraint based on the given 'consvars'. This array has to consist of the negated
 * versions of the variables present on a cutoff "path" (path means all variables from the root directly
 * to the cutoff node).
 * Let x_1, ..., x_n be the variables on a path to a cutoff with the branchings x_i <= 1 for all i.
 * Summed up the constraints would look like x_1 + ... x_n <= n-1.
 * Let y_i = 1 - x_i. Then we have y_1 + ... + y_n >= 1 which is a logic or constraint.  */
static
SCIP_RETCODE createBinaryConstraint(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< configuration containing flags changing the behavior */
   SCIP_CONS**           constraint,         /**< Pointer to store the created constraint in */
   char*                 constraintname,     /**< name of the new constraint */
   SCIP_VAR**            consvars,           /**< array containing the negated binary vars */
   int                   nconsvars           /**< the number of elements in 'consvars' */
   )
{
   SCIP_Bool initial;
   SCIP_Bool separate;
   SCIP_Bool enforce = FALSE;
   SCIP_Bool check = FALSE;
   SCIP_Bool propagate = TRUE;
   SCIP_Bool local = TRUE;
   SCIP_Bool modifiable = FALSE;
   SCIP_Bool dynamic = FALSE;
   SCIP_Bool removable = FALSE;
   SCIP_Bool stickingatnode = FALSE;

   assert(scip != NULL);
   assert(config != NULL);
   assert(constraint != NULL);
   assert(constraintname != NULL);
   assert(consvars != NULL);
   assert(nconsvars > 0);

   initial = config->addbinconsrow;
   separate = config->addbinconsrow;

   /* creating a logic or constraint based on the list of vars in 'consvars'.
    * A logic or constraints looks like that: y_1 + ... + y_n >= 1. */
   SCIP_CALL( SCIPcreateConsLogicor(scip, constraint, constraintname, nconsvars, consvars, initial, separate, enforce,
         check, propagate, local, modifiable, dynamic, removable, stickingatnode) );
   return SCIP_OKAY;
}

/**
 * Create a name for the binary constraint.
 */
static
void createBinaryConstraintName(
   SCIP_VAR**            binaryvars,         /**< the variables contained in the constraint */
   int                   nbinaryvars,        /**< the number of elements in 'binaryvars' */
   char*                 constraintname      /**< the char pointer to store the name in */
   )
{
   int i;

   assert(binaryvars != NULL);
   assert(nbinaryvars > 0);
   assert(constraintname != NULL);

   sprintf(constraintname, "lookahead_bin_%s", SCIPvarGetName(binaryvars[0]));

   for( i = 1; i < nbinaryvars; i++ )
   {
      SCIP_VAR* var = binaryvars[i];
      const char* varname = SCIPvarGetName(var);
      char prevconstraintname[SCIP_MAXSTRLEN];

      /* we need to store the constraint name built till this point, as 'sprintf''s behaviour is undefined, if one of
       * the format params is also the target string */
      strcpy(prevconstraintname, constraintname);

      sprintf(constraintname, "%s_%s", prevconstraintname, varname);
   }
}

/**
 * Add the constraints found during the lookahead branching.
 * The implied binary bounds were found when two or more consecutive branching of binary variables were cutoff. Then these
 * branching constraints can be combined into a single 'binary constraint'.
 */
static
SCIP_RETCODE addBinaryConstraint(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   BINCONSDATA*          binconsdata,        /**< collected binary constraints */
   SCIP_SOL*             baselpsol           /**< the original lp solution, used to check the violation of the constraint */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< statistics data */
#endif
   )
{
   /* If we only have one var for the constraint we can ignore it, as it is already added as a domain reduction. */
   if( binconsdata->binaryvars->nbinaryvars > 1 )
   {
      int i;
      SCIP_CONS* constraint;
      char constraintname[SCIP_MAXSTRLEN];
      SCIP_VAR** negatedvars;
      SCIP_Real lhssum = 0;

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Adding binary constraint for <%i> vars.\n",
            binconsdata->binaryvars->nbinaryvars);

      SCIP_CALL( SCIPallocBufferArray(scip, &negatedvars, binconsdata->binaryvars->nbinaryvars) );

      for( i = 0; i < binconsdata->binaryvars->nbinaryvars; i++ )
      {
         SCIP_VAR* var = binconsdata->binaryvars->binaryvars[i];

         assert(SCIPvarIsBinary(var));

         SCIP_CALL( SCIPgetNegatedVar(scip, var, &negatedvars[i]) );
         lhssum += SCIPgetSolVal(scip, baselpsol, negatedvars[i]);
      }

      if( config->addnonviocons || lhssum < 1 )
      {
         /* create a name for the new constraint */
         createBinaryConstraintName(negatedvars, binconsdata->binaryvars->nbinaryvars, constraintname);
         /* create the constraint with the freshly created name */
         SCIP_CALL( createBinaryConstraint(scip, config, &constraint, constraintname, negatedvars,
               binconsdata->binaryvars->nbinaryvars) );

#ifdef PRINTNODECONS
         SCIPinfoMessage(scip, NULL, "Created constraint:\n");
         SCIP_CALL( SCIPprintCons(scip, constraint, NULL) );
         SCIPinfoMessage(scip, NULL, "\n");
#endif

         SCIP_CALL( constraintListAppend(scip, binconsdata->createdconstraints, constraint) );
         /* the constraint we are building is a logic or: we have a list of binary variables that were
          * cutoff while we branched on with >= 1. So we have the constraint: x_1 + ... + x_n <= n-1.
          * Let y = (1-x), then we have an equivalent formulation: y_1 + ... + y_n >= 1. If the base lp
          * is violating this constraint we count this for our number of violated constraints and bounds. */
         if( lhssum < 1 )
         {
            binconsdata->createdconstraints->nviolatedcons++;
         }
      }

      SCIPfreeBufferArray(scip, &negatedvars);
   }
   else
   {

#ifdef SCIP_STATISTIC
      statistics->ndomredcons++;
#endif
   }

   return SCIP_OKAY;
}

/** Applies the binary constraints to the original problem. */
static
SCIP_RETCODE applyBinaryConstraints(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_NODE*            basenode,           /**< original branching node */
   CONSTRAINTLIST*       conslist,           /**< list of constraints to be added */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCIP_Bool*            consadded,          /**< pointer to store whether at least one constraint was added */
   SCIP_Bool*            cutoff,             /**< pointer to store whether the original problem was made infeasible */
   SCIP_Bool*            boundchange         /**< pointer to store whether a bound change has been applied by adding the
                                              *   constraint as a clique */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< statistics data */
#endif
   )
{
   int i;

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Adding <%i> binary constraints.\n", conslist->nconstraints);

   for( i = 0; i < conslist->nconstraints; i++ )
   {
      SCIP_CONS* constraint = conslist->constraints[i];

#ifdef PRINTNODECONS
      SCIP_CALL( SCIPprintCons(scip, constraint, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");
#endif

      /* add the constraint to the given node */
      SCIP_CALL( SCIPaddConsNode(scip, basenode, constraint, NULL) );

      /* only add cliques for the root node */
      if( config->addclique && SCIPgetNNodes(scip) == 1 )
      {
         int nvars;

         assert(strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(constraint)), "logicor"));

         /* get the number of contained variables, used to add a clique */
         nvars = SCIPgetNVarsLogicor(scip, constraint);

         if( nvars == 2 )
         {
            SCIP_VAR** vars;
            SCIP_Bool* values;
            int j;
            SCIP_Bool infeasible;
            int nbdchgs;

            vars = SCIPgetVarsLogicor(scip, constraint);

            SCIP_CALL( SCIPallocBufferArray(scip, &values, nvars) );
            for( j = 0; j < nvars; j++ )
            {
               values[j] = FALSE;
            }
            /* a two-variable logicor constraint x + y >= 1 yields the implication x == 0 -> y == 1, and is represented
             * by the clique inequality ~x + ~y <= 1
             */
            SCIP_CALL( SCIPaddClique(scip, vars, values, nvars, FALSE, &infeasible, &nbdchgs) );

#ifdef SCIP_STATISTIC
            statistics->ncliquesadded++;
#endif

            if( infeasible )
            {
               *cutoff = TRUE;
            }

            if( nbdchgs > 0 )
            {
               *boundchange = TRUE;
            }
            SCIPfreeBufferArray(scip, &values);
         }
      }

      /* release the constraint, as it is no longer needed */
      SCIP_CALL( SCIPreleaseCons(scip, &constraint) );

      *consadded = TRUE;
   }

#ifdef SCIP_STATISTIC
   statistics->nbinconst += conslist->nconstraints;
   statistics->nbinconstvio += conslist->nviolatedcons;
#endif

   return SCIP_OKAY;
}

/** checks whether the given bounds are still the bounds of the given variable */
static
SCIP_Bool areBoundsChanged(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< variable to check the bounds of */
   SCIP_Real             lowerbound,         /**< reference lower bound */
   SCIP_Real             upperbound          /**< reference upper bound */
   )
{
   assert(SCIPisFeasIntegral(scip, lowerbound));
   assert(SCIPisFeasIntegral(scip, upperbound));
   assert(!SCIPisEQ(scip, lowerbound, upperbound));
   assert(SCIPvarGetType(var) < SCIP_VARTYPE_CONTINUOUS);

   /* due to roundings the value might have changed slightly without an actual influence on the integral value */
   return SCIPvarGetLbLocal(var) > lowerbound + 0.5 || SCIPvarGetUbLocal(var) < upperbound - 0.5;
}

/** Checks whether the branching rule should continue or terminate with the currently gathered data */
static
SCIP_Bool isBranchFurther(
   STATUS*               status              /**< current status */
   )
{
   return !status->lperror && !status->cutoff && !status->limitreached && !status->maxnconsreached
         && !status->propagationdomred && !status->domred;
}

/** Checks whether the branching rule should continue or terminate with the currently gathered data. Additionally decrements
 * the given loopcounter. This is needed to better emulate the behavior of FSB by LAB with a depth of 1. */
static
SCIP_Bool isBranchFurtherLoopDecrement(
   STATUS*               status,              /**< current status */
   int*                  loopcounter          /**< the counter to decrement */
   )
{
   SCIP_Bool branchfurther = isBranchFurther(status);
   if( !branchfurther )
   {
      (*loopcounter)--;
   }
   return branchfurther;
}

/** Determines whether previous branching results of a variable can be reused */
static
SCIP_Bool isUseOldBranching(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCIP_VAR*             branchvar           /**< variable to check */
   )
{
   /* an old branching can be reused, if we are still at the same node and just a few LPs were solved in between */
   return SCIPgetVarStrongbranchNode(scip, branchvar) == SCIPgetNNodes(scip)
      && SCIPgetVarStrongbranchLPAge(scip, branchvar) < config->reevalage;
}

/** Retrieves previous branching results for the given variable */
static
SCIP_RETCODE getOldBranching(
   SCIP*                 scip,               /**< SCIP data structure */
   PERSISTENTDATA*       persistent,         /**< data storage over multiple calls to the rulel */
   SCIP_VAR*             branchvar,          /**< variable to get previous results for */
   BRANCHINGRESULTDATA*  downbranchingresult,/**< pointer to store the previous down result in */
   BRANCHINGRESULTDATA*  upbranchingresult,  /**< pointer to store the previous up result in */
   SCIP_Real*            oldlpobjval         /**< pointer to store the previous base lp objval in */
   )
{
   int varindex = SCIPvarGetProbindex(branchvar);

   branchingResultDataCopy(persistent->lastbranchdownres[varindex], downbranchingresult);
   branchingResultDataCopy(persistent->lastbranchupres[varindex], upbranchingresult);

   SCIP_CALL( SCIPgetVarStrongbranchLast(scip, branchvar, &downbranchingresult->dualbound, &upbranchingresult->dualbound,
      NULL, NULL, NULL, oldlpobjval) );

   downbranchingresult->dualboundvalid = FALSE;
   upbranchingresult->dualboundvalid = FALSE;
   downbranchingresult->cutoff = FALSE;
   upbranchingresult->cutoff = FALSE;

#ifdef SCIP_DEBUG
   {
      SCIP_Real downgain;
      SCIP_Real upgain;

      downgain = MAX(downbranchingresult->dualbound - *oldlpobjval, 0);
      upgain = MAX(upbranchingresult->dualbound - *oldlpobjval, 0);

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Lookahead branching on variable <%s> already performed (lpage=%"
            SCIP_LONGINT_FORMAT ", down=%g (%+g), up=%g (%+g))\n", SCIPvarGetName(branchvar),
            SCIPgetVarStrongbranchLPAge(scip, branchvar), downbranchingresult->dualbound, downgain,
            upbranchingresult->dualbound, upgain);
   }
#endif

   return SCIP_OKAY;
}

/** Stores the data for use in a later call to the branching rule */
static
SCIP_RETCODE updateOldBranching(
   SCIP*                 scip,               /**< SCIP data structure */
   PERSISTENTDATA*       persistent,         /**< data storage over multiple calls to the rulel */
   SCIP_VAR*             branchvar,          /**< variable to store previous results for */
   SCIP_Real             branchval,          /**< the value of branchvar */
   SCIP_Longint          niterations,        /**< total number of iterations for this variable*/
   BRANCHINGRESULTDATA*  downbranchingresult,/**< down branching result to store */
   BRANCHINGRESULTDATA*  upbranchingresult,  /**< up branching result to store */
   SCIP_Real             lpobjval            /**< base lp obj val */
   )
{
   int varindex = SCIPvarGetProbindex(branchvar);

   SCIP_CALL( SCIPsetVarStrongbranchData(scip, branchvar, lpobjval, branchval, downbranchingresult->dualbound,
         upbranchingresult->dualbound, downbranchingresult->dualboundvalid, upbranchingresult->dualboundvalid, niterations,
         INT_MAX) );

   branchingResultDataCopy(downbranchingresult, persistent->lastbranchdownres[varindex]);
   branchingResultDataCopy(upbranchingresult, persistent->lastbranchupres[varindex]);

   persistent->lastbranchid[varindex] = SCIPgetNNodes(scip);
   persistent->lastbranchnlps[varindex] = SCIPgetNNodeLPs(scip);

   return SCIP_OKAY;
}

/** calculates the FSB scores for the given candidates */
static
SCIP_RETCODE getFSBResult(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        parentconfig,       /**< main configuration */
   CANDIDATELIST*        candidates,         /**< the candidates to get the scores for */
   STATUS*               status,             /**< status getting updated by the fsb routine */
   SCORECONTAINER*       scorecontainer      /**< container for the scores and lpi information */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          parentstatistics    /**< main statistics */
#endif
   )
{
   CONFIGURATION* config;
   BRANCHINGDECISION* decision;
   SCIP_Bool firstlevel;
#ifdef SCIP_STATISTIC
   STATISTICS* statistics;
   LOCALSTATISTICS* localstats;
#endif

   SCIP_CALL( configurationCreate(scip, &config) );
   configurationCopy(config, parentconfig);

   SCIP_CALL( branchingDecisionCreate(scip, &decision) );

   /* on the first level we are not in probing mode at this point */
   firstlevel = !SCIPinProbing(scip);

   /* In deeper levels we don't want any constraints to be added or domains to be reduced, as we are just interested in the
    * score. */
   config->usebincons = firstlevel && config->usebincons;
   config->usedomainreduction = firstlevel && config->usedomainreduction;

   /* Simple FSB is achieved by starting LAB with a max depth of 1 */
   config->recursiondepth = 1;
   config->abbreviated = FALSE;

   /* Even for one single candidate we want to get the corresponding score */
   config->forcebranching = TRUE;

#ifdef SCIP_STATISTIC
   /* we need to allocate enough space for all possible depths, as there is currently a problem with setting the FSB stats.
    * E.G.: we want to gather statistics for the FSB run started on layer 2 (1-indexed). Then we start in probing depth 1
    * and solve the nodes in depth 2.
    */
   SCIP_CALL( statisticsAllocate(scip, &statistics, parentconfig->recursiondepth, parentconfig->maxncands) );
   SCIP_CALL( localStatisticsAllocate(scip, &localstats) );

   SCIP_CALL( selectVarStart(scip, config, NULL, status, decision, scorecontainer, TRUE, candidates,
         statistics, localstats) );

   mergeFSBStatistics(parentstatistics, statistics);

   localStatisticsFree(scip, &localstats);
   statisticsFree(scip, &statistics);
#else
   SCIP_CALL( selectVarStart(scip, config, NULL, status, decision, scorecontainer, TRUE, candidates) );
#endif

   branchingDecisionFree(scip, &decision);
   configurationFree(scip, &config);

   return SCIP_OKAY;
}

/** Small struct used to call a comparator */
typedef struct
{
   SCORECONTAINER*       scorecontainer;     /**< container with the scores as the sort key */
   CANDIDATELIST*        candidatelist;      /**< list of candidates to be sorted */
} SCORESORTCONTAINER;

/** Comparator used to order the candidates by their score. */
static
SCIP_DECL_SORTINDCOMP(scoreSortContainerScoreComp)
{  /*lint --e{715}*/
   SCORESORTCONTAINER* container = (SCORESORTCONTAINER*)dataptr;
   SCIP_VAR* var1;
   SCIP_VAR* var2;
   int probindex1;
   int probindex2;
   SCIP_Real score1;
   SCIP_Real score2;

   assert(container != NULL);
   assert(0 <= ind1 && ind1 < container->candidatelist->ncandidates);
   assert(0 <= ind2 && ind2 < container->candidatelist->ncandidates);

   var1 = container->candidatelist->candidates[ind1]->branchvar;
   var2 = container->candidatelist->candidates[ind2]->branchvar;
   probindex1 = SCIPvarGetProbindex(var1);
   probindex2 = SCIPvarGetProbindex(var2);
   score1 = container->scorecontainer->scores[probindex1];
   score2 = container->scorecontainer->scores[probindex2];

   if( score1 == score2 ) /*lint !e777*/
   {
      return 0;
   }
   else if( score1 < score2 ) /*lint !e777*/
   {
      return -1;
   }
   else
   {
      return 1;
   }
}

#ifdef SCIP_DEBUG
/** prints the given candidate list */
static
void printCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VERBLEVEL        lvl,                /**< verbosity level to print the list in */
   CANDIDATELIST*        list                /**< the list to be printed */
   )
{
   int i;
   int ncands = list->ncandidates;

   LABdebugMessagePrint(scip, lvl, "[");

   for( i = 0; i < ncands; i++ )
   {
      CANDIDATE* cand = list->candidates[i];

      assert(cand != NULL);
      assert(cand->branchvar != NULL);

      LABdebugMessagePrint(scip, lvl, "%s", SCIPvarGetName(cand->branchvar));
      if(i != ncands-1)
      {
         LABdebugMessagePrint(scip, lvl, ", ");
      }
   }
   LABdebugMessagePrint(scip, lvl, "]\n");
}
#endif

/** calculates the score based on the gains given */
static
SCIP_Real calculateScoreFromGain(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             branchvar,          /**< variable to get the score for */
   SCIP_Real             downgain,           /**< gain in the down branch of branchvar */
   SCIP_Real             upgain              /**< gain in the up branch of branchvar */
   )
{
   return SCIPgetBranchScore(scip, branchvar, downgain, upgain);
}

/** calculates the score based on the down and up branching result */
static
SCIP_Real calculcateScoreFromResult(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             branchvar,          /**< variable to get the score for */
   BRANCHINGRESULTDATA*  downbranchingresult,/**< branching result of the down branch */
   BRANCHINGRESULTDATA*  upbranchingresult,  /**< branching result of the up branch */
   SCIP_Real             lpobjval            /**< objective value to get difference to as gain */
   )
{
   SCIP_Real score;
   SCIP_Real downgain = 0;
   SCIP_Real upgain = 0;

   assert(scip != NULL);
   assert(branchvar != NULL);

   /* @todo how should the NULL case be handled? */
   if( downbranchingresult != NULL )
   {
      downgain = MAX(0, downbranchingresult->dualbound - lpobjval);
   }
   if( upbranchingresult != NULL )
   {
      upgain = MAX(0, upbranchingresult->dualbound - lpobjval);
   }

   score = calculateScoreFromGain(scip, branchvar, downgain, upgain);

   return score;
}

/** calculates the score based on the pseudocosts of the given variable */
static
SCIP_Real calculateScoreFromPseudocosts(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATE*            lpcand              /**< candidate to get the score for */
   )
{
   SCIP_Real downpseudocost;
   SCIP_Real uppseudocost;
   SCIP_Real score;

   downpseudocost = SCIPgetVarPseudocostVal(scip, lpcand->branchvar, 0-lpcand->fracval);
   uppseudocost = SCIPgetVarPseudocostVal(scip, lpcand->branchvar, 1-lpcand->fracval);

   score = calculateScoreFromGain(scip, lpcand->branchvar, downpseudocost, uppseudocost);

   return score;
}

/** creates a permuation vector containing the indices of the candidates list, sorted by their score */
static
SCIP_RETCODE sortCandidatesByScore(
   SCIP*                 scip,               /**< SCIP data structure */
   CANDIDATELIST*        candidates,         /**< list to sort */
   SCORECONTAINER*       scorecontainer,     /**< container with the scores as the sort key */
   int*                  permutation         /**< array going to be permuted s.t. candidates is sorted */
   )
{
   SCORESORTCONTAINER* container;
   SCIP_CALL( SCIPallocBuffer(scip, &container) );
   container->candidatelist = candidates;
   container->scorecontainer = scorecontainer;

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Sorting the candidates w.r.t. their FSB score.\n");

   /* sort the branching candidates according to their FSB score contained in the result struct */
   SCIPsortDown(permutation, scoreSortContainerScoreComp, container, candidates->ncandidates);

   SCIPfreeBuffer(scip, &container);
   return SCIP_OKAY;
}

/** Checks whether the given candidates is reliable, so that its psudocosts may be used. */
static
SCIP_Bool isCandidateReliable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             branchvar           /**< var to check for reliability */
   )
{
   SCIP_Real size;
   SCIP_Real downsize;
   SCIP_Real upsize;
   SCIP_Real reliable = 5;

   downsize = SCIPgetVarPseudocostCountCurrentRun(scip, branchvar, SCIP_BRANCHDIR_DOWNWARDS);
   upsize = SCIPgetVarPseudocostCountCurrentRun(scip, branchvar, SCIP_BRANCHDIR_UPWARDS);
   size = MIN(downsize, upsize);

   return size >= reliable;
}

/** Checks whether the current problem is feasible or cutoff */
static
SCIP_Bool isCurrentNodeCutoff(
   SCIP*                  scip               /**< SCIP data structure */
   )
{
   return (SCIPgetCutoffdepth(scip) <= SCIPgetDepth(scip));
}

/** Ensures that the scores are present in the scorecontainer for each of the candidates to consider */
static
SCIP_RETCODE ensureScoresPresent(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< main configuration */
   STATUS*               status,             /**< current status */
   CANDIDATELIST*        allcandidates,      /**< list containing all candidates to consider */
   SCORECONTAINER*       scorecontainer      /**< container to store the scores for later usage */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< statistical data */
#endif
   )
{
   int i;
   int nunscoredcandidates = 0;
   int* candidateunscored;

   SCIP_CALL( SCIPallocBufferArray(scip, &candidateunscored, allcandidates->ncandidates) );

   /* filter the candidates based on the presence of a score in the 'scorecontainer'. Only those without a score need a
    * new one.
    */
   for( i = 0; i < allcandidates->ncandidates; i++ )
   {
      CANDIDATE* lpcand = allcandidates->candidates[i];
      SCIP_VAR* branchvar = lpcand->branchvar;

      if( config->abbrevpseudo )
      {
         if( !isCandidateReliable(scip, branchvar) )
         {
            candidateunscored[nunscoredcandidates] = i;
            nunscoredcandidates++;
         }
         else
         {
            SCIP_Real score = calculateScoreFromPseudocosts(scip, lpcand);
            SCIP_CALL( scoreContainerSetScore(scip, scorecontainer, allcandidates, branchvar, score) );
         }
      }
      else
      {
         int probindex = SCIPvarGetProbindex(branchvar);
         SCIP_Real knownscore = scorecontainer->scores[probindex];

         if( SCIPisLT(scip, knownscore, 0.0) )
         {
            /* score is unknown and needs to be calculated */
            candidateunscored[nunscoredcandidates] = i;
            nunscoredcandidates++;
         }
      }
   }

   if( nunscoredcandidates > 0 )
   {
      CANDIDATELIST* unscoredcandidates;

      /* allocate the list of candidates without any score (gets updated further on) */
      SCIP_CALL( candidateListCreate(scip, &unscoredcandidates, nunscoredcandidates) );

      /* move the unscored candidates to the temp list */
      for( i = 0; i < nunscoredcandidates; i++ )
      {
         int candindex = candidateunscored[i];

         assert(allcandidates->candidates[candindex] != NULL);

         unscoredcandidates->candidates[i] = allcandidates->candidates[candindex];
      }

#ifdef SCIP_DEBUG
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Of the given %i candidates, %i have no score: ",
            allcandidates->ncandidates, nunscoredcandidates);
      printCandidates(scip, SCIP_VERBLEVEL_HIGH, unscoredcandidates);
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Calculating the FSB result to get a score for the remaining "
            "candidates.\n");
#endif

      /* Calculate all remaining FSB scores and collect the scores in the container */;
#ifdef SCIP_STATISTIC
      SCIP_CALL( getFSBResult(scip, config, unscoredcandidates, status, scorecontainer, statistics) );
#else
      SCIP_CALL( getFSBResult(scip, config, unscoredcandidates, status, scorecontainer) );
#endif

      /* move the now scored candidates back to the original list */
      for( i = 0; i < nunscoredcandidates; i++ )
      {
         assert(allcandidates->candidates[candidateunscored[i]] == unscoredcandidates->candidates[i]);

         assert(unscoredcandidates->candidates[i] != NULL);
         unscoredcandidates->candidates[i] = NULL;
      }

      /* reset the best sorted indices, as those are only valid on the FSB run already completed */
      scoreContainterResetBestSortedIndices(scorecontainer);

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Calculated the scores for the remaining candidates\n");

      SCIP_CALL( candidateListFree(scip, &unscoredcandidates) );
   }

   SCIPfreeBufferArray(scip, &candidateunscored);
   return SCIP_OKAY;
}

/** Gets the best candidates w.r.t. the scores stored in the scorecontainer and stores them in the given list */
static
SCIP_RETCODE filterBestCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCORECONTAINER*       scorecontainer,     /**< container to store the scores for later usage */
   CANDIDATELIST*        candidates          /**< list containing all candidates to consider */
   )
{
   int* permutation;
   int nusedcands;
#ifdef SCIP_DEBUG
   int i;
#endif

   nusedcands = MIN(config->maxncands, candidates->ncandidates);

   SCIP_CALL( SCIPallocBufferArray(scip, &permutation, candidates->ncandidates) );

   SCIP_CALL( sortCandidatesByScore(scip, candidates, scorecontainer, permutation) );

#ifdef SCIP_DEBUG
   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "All %i candidates, sorted by their FSB score:\n",
         candidates->ncandidates);
   for( i = 0; i < candidates->ncandidates; i++ )
   {
      int sortedindex = permutation[i];
      SCIP_VAR* var = candidates->candidates[sortedindex]->branchvar;
      SCIP_Real score = scorecontainer->scores[SCIPvarGetProbindex(var)];

      assert(var != NULL);

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, " Index %2i: Var %s Score %g\n", i, SCIPvarGetName(var), score);
   }
#endif

   SCIP_CALL( candidateListKeep(scip, candidates, permutation, nusedcands) );

   SCIPfreeBufferArray(scip, &permutation);

   return SCIP_OKAY;
}

/** Gets the best candidates, according the FSB score of each candidate */
static
SCIP_RETCODE getBestCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   STATUS*               status,             /**< current status */
   CANDIDATELIST*        candidates,         /**< list containing all candidates to consider */
   SCORECONTAINER*       scorecontainer      /**< container to store the scores for later usage */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< statistical data */
#endif
   )
{
   assert(scip != NULL);
   assert(config != NULL);
   assert(config->abbreviated);
   assert(candidates != NULL);
   assert(candidates->ncandidates > 0);
   assert(scorecontainer != NULL);
#ifdef SCIP_STATISTIC
   assert(statistics != NULL);
#endif

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Getting the best (at most) %i of the given %i candidates: ",
         config->maxncands, candidates->ncandidates);
#ifdef SCIP_DEBUG
   printCandidates(scip, SCIP_VERBLEVEL_HIGH, candidates);
#endif

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "%s", "Ensuring that all candidates have a score.\n");
#ifdef SCIP_STATISTIC
   SCIP_CALL( ensureScoresPresent(scip, config, status, candidates, scorecontainer, statistics) );
#else
   SCIP_CALL( ensureScoresPresent(scip, config, status, candidates, scorecontainer) );
#endif

   /* if we didn't find any domreds or constraints during the FSB, we branch on */
   if( isBranchFurther(status) )
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "%s", "Filter the candidates by their score.\n");

      SCIP_CALL( filterBestCandidates(scip, config, scorecontainer, candidates) );

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Strong Branching would branch on variable <%s>\n",
         SCIPvarGetName(candidates->candidates[0]->branchvar));
   }
#ifdef SCIP_DEBUG
   else
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Strong Branching would have stopped.");
   }
#endif

   if( isCurrentNodeCutoff(scip) )
   {
      status->cutoff = TRUE;
   }

   return SCIP_OKAY;
}

/** Get the candidates to temporarily branch on. In the LAB case this is the complete list of possible candidates. In the
 *  ALAB case only the 'best' candidates are returned. */
static
SCIP_RETCODE filterCandidates(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   STATUS*               status,             /**< current status */
   SCORECONTAINER*       scorecontainer,     /**< container to store the scores for later usage */
   CANDIDATELIST*        candidates          /**< list with the candidates */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< statistical data */
#endif
   )
{
   /* we want to take the abbreviated list of candidates only on the first probing level */
   if( config->abbreviated )
   {
      /* call LAB with depth 1 to get the best (w.r.t. FSB score) candidates */
#ifdef SCIP_STATISTIC
      SCIP_CALL( getBestCandidates(scip, config, status, candidates, scorecontainer, statistics) );
#else
      SCIP_CALL( getBestCandidates(scip, config, status, candidates, scorecontainer) );
#endif
   }
   else
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Getting the branching candidates by selecting all candidates.\n");
   }

   return SCIP_OKAY;
}

/** Executes the general branching on a node in a given direction (up/down) and repeats the algorithm from the new node */
static
SCIP_RETCODE executeBranchingRecursive(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS*               status,             /**< current status */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCIP_SOL*             baselpsol,          /**< the base lp solution */
   CANDIDATE*            candidate,          /**< candidate to branch on */
   SCIP_Real             localbaselpsolval,  /**< the objective value of the current temporary problem */
   int                   recursiondepth,     /**< remaining recursion depth */
   DOMAINREDUCTIONS*     domainreductions,   /**< container collecting all domain reductions found */
   BINCONSDATA*          binconsdata,        /**< container collecting all binary constraints */
   BRANCHINGRESULTDATA*  branchingresult,    /**< container to store the result of the branching in */
   SCORECONTAINER*       scorecontainer,     /**< container to retrieve already calculated scores */
   SCIP_Bool             storewarmstartinfo, /**< should lp information be stored? */
   SCIP_Bool             downbranching       /**< should we branch up or down in here? */
#ifdef SCIP_STATISTIC
   ,SCIP_Bool*           addeddomainreduction/**< pointer to store whether a domain reduction was added */
   ,STATISTICS*          statistics          /**< general statistical data */
   ,LOCALSTATISTICS*     localstats          /**< local statistics, may be disregarded */
#endif
   )
{
   int probingdepth;
   SCIP_VAR *branchvar = candidate->branchvar;
   SCIP_Real branchvalfrac = candidate->fracval;
#ifdef SCIP_DEBUG
   SCIP_Real branchval = candidate->branchval;
#endif

   probingdepth = SCIPgetProbingDepth(scip);

   if( config->usebincons && SCIPvarIsBinary(branchvar))
   {
      if( downbranching )
      {
         /* In case that the branch variable is binary, add the negated var to the list.
          * This list is used to generate a set packing constraint for cutoff branches which were reached by only using
          * binary variables.
          * DownBranching on a binary variable x means: x <= 0
          * When this cutoff occurs we have that: x >= 1 <=> 1-x <= 0
          */
         SCIP_VAR *negbranchvar;

         SCIP_CALL(SCIPgetNegatedVar(scip, branchvar, &negbranchvar));

         assert(negbranchvar != NULL);

         SCIP_CALL(binaryVarListAppend(scip, binconsdata->binaryvars, negbranchvar));
      }
      else
      {
         /* In case that the branch variable is binary, add the var to the list.
          * This list is used to generate a set packing constraint for cutoff branches which were reached by only using
          * binary variables.
          * UpBranching on a binary variable x means: x >= 1
          * When this cutoff occurs we have that: x <= 0
          */
         SCIP_CALL(binaryVarListAppend(scip, binconsdata->binaryvars, branchvar));
      }
   }

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Started %s branching on var <%s> with 'val > %g' and bounds [<%g>..<%g>]\n",
         downbranching ? "down" : "up", SCIPvarGetName(branchvar), branchval, SCIPvarGetLbLocal(branchvar),
         SCIPvarGetUbLocal(branchvar));

   SCIP_CALL( executeBranching(scip, config, downbranching, candidate, branchingresult, baselpsol, domainreductions,
         status) );

#ifdef SCIP_STATISTIC
   statistics->nlpssolved[probingdepth]++;
   statistics->nlpiterations[probingdepth] += branchingresult->niterations;
#endif
   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Solving the LP took %" SCIP_LONGINT_FORMAT " iterations.\n",
         branchingresult->niterations);

#ifdef SCIP_DEBUG
   if( status->lperror )
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The LP could not be solved.\n");
   }
   else if( branchingresult->cutoff )
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The solved LP was infeasible and as such is cutoff\n");
   }
   else
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The solved LP was feasible and has an objval <%g> (the parent objval was "
            "<%g>)\n", branchingresult->objval, localbaselpsolval);
   }
#endif

   if( !branchingresult->cutoff && !status->lperror && !status->limitreached )
   {
      SCIP_Real localgain;

      if( config->abbreviated && config->reusebasis && storewarmstartinfo )
      {
         /* store the warm start information in the candidate, so that it can be reused in a later branching */
         WARMSTARTINFO* warmstartinfo = downbranching ? candidate->downwarmstartinfo : candidate->upwarmstartinfo;

         LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Storing warm start information for %sbranching on var <%s>\n",
               downbranching ? "down" : "up", SCIPvarGetName(branchvar));

         SCIP_CALL( storeWarmStartInfo(scip, warmstartinfo) );
      }

      localgain = MAX(0, branchingresult->objval - localbaselpsolval);

      if( downbranching )
      {
         SCIP_CALL( SCIPupdateVarPseudocost(scip, branchvar, 0.0 - branchvalfrac, localgain, 1.0) );
      }
      else
      {
         SCIP_CALL( SCIPupdateVarPseudocost(scip, branchvar, 1.0 - branchvalfrac, localgain, 1.0) );
      }

      if( recursiondepth > 1 )
      {
         CANDIDATELIST* candidates;

         SCIP_CALL( candidateListGetAllFractionalCandidates(scip, config->abbreviated && config->reusebasis, &candidates) );

         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "%sbranching has <%i> candidates.\n", downbranching ? "Down" : "Up",
               candidates->ncandidates);

         if( candidates->ncandidates > 0 )
         {
            BRANCHINGDECISION* deeperdecision;
            STATUS* deeperstatus;
            PERSISTENTDATA* deeperpersistent = NULL;
            SCIP_Real deeperlpobjval = branchingresult->objval;
#ifdef SCIP_STATISTIC
            LOCALSTATISTICS* deeperlocalstats;
#endif

            SCIP_CALL(statusCreate(scip, &deeperstatus) );

#ifdef SCIP_STATISTIC
            SCIP_CALL( filterCandidates(scip, config, deeperstatus, scorecontainer, candidates, statistics) );
#else
            SCIP_CALL( filterCandidates(scip, config, deeperstatus, scorecontainer, candidates) );
#endif

            /* the status may have changed because of FSB to get the best candidates */
            if( isBranchFurther(deeperstatus) )
            {
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Now the objval is <%g>\n", branchingresult->objval);

               SCIP_CALL( branchingDecisionCreate(scip, &deeperdecision) );

#ifdef SCIP_STATISTIC
               SCIP_CALL( localStatisticsAllocate(scip, &deeperlocalstats) );
               SCIP_CALL( selectVarRecursive(scip, deeperstatus, deeperpersistent, config, baselpsol, domainreductions,
                     binconsdata, candidates, deeperdecision, scorecontainer, storewarmstartinfo, recursiondepth - 1,
                     deeperlpobjval, &branchingresult->niterations, statistics, deeperlocalstats) );
#else
               SCIP_CALL( selectVarRecursive(scip, deeperstatus, deeperpersistent, config, baselpsol, domainreductions,
                     binconsdata, candidates, deeperdecision, scorecontainer, storewarmstartinfo, recursiondepth - 1,
                     deeperlpobjval, &branchingresult->niterations) );
#endif

               /* the proved dual bound of the deeper branching cannot be less than the current dual bound, as every deeper
                * node has more/tighter constraints and as such cannot be better than the base LP. */
               assert(SCIPisGE(scip, deeperdecision->proveddb, branchingresult->dualbound));
               branchingresult->dualbound = deeperdecision->proveddb;
               branchingresult->dualboundvalid = TRUE;

#ifdef SCIP_STATISTIC
               if( deeperlocalstats->ndomredproofnodes > 0 )
               {
                  localstats->ndomredproofnodes += deeperlocalstats->ndomredproofnodes;
                  *addeddomainreduction = TRUE;
               }
#endif

               if( deeperstatus->cutoff )
               {
                  /* upbranchingresult->cutoff is TRUE, if the up child was directly infeasible (so here it is always
                   * false, as we don't want to branch on an infeasible node)
                   * deeperstatus->cutoff is TRUE, if any up/down child pair of the up child were cutoff
                   * */
                  branchingresult->cutoff = deeperstatus->cutoff;
#ifdef SCIP_STATISTIC
                  localstats->ncutoffproofnodes += deeperlocalstats->ncutoffproofnodes;
#endif
                  LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Both deeper children were cutoff, so the down branch is "
                        "cutoff\n");
               }

#ifdef SCIP_STATISTIC
               localStatisticsFree(scip, &deeperlocalstats);
#endif
               branchingDecisionFree(scip, &deeperdecision);
            }
            statusFree(scip, &deeperstatus);
         }
         SCIP_CALL( candidateListFree(scip, &candidates) );
      }
   }

   if( config->usebincons && branchingresult->cutoff && binconsdata->binaryvars->nbinaryvars == (probingdepth + 1) )
   {
#ifdef SCIP_STATISTIC
      SCIP_CALL( addBinaryConstraint(scip, config, binconsdata, baselpsol, statistics) );
#else
      SCIP_CALL( addBinaryConstraint(scip, config, binconsdata, baselpsol) );
#endif
   }

   if( config->usebincons && SCIPvarIsBinary(branchvar) )
   {
      binaryVarListDrop(binconsdata->binaryvars);
   }

   /* reset the probing depth to undo the previous branching */
   SCIP_CALL( SCIPbacktrackProbing(scip, probingdepth) );

   return SCIP_OKAY;
}

/** branches recursively on all given candidates */
static
SCIP_RETCODE selectVarRecursive(
   SCIP*                 scip,               /**< SCIP data structure */
   STATUS*               status,             /**< current status */
   PERSISTENTDATA*       persistent,         /**< container to store data over multiple calls to the branching rule */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   SCIP_SOL*             baselpsol,          /**< base lp solution */
   DOMAINREDUCTIONS*     domainreductions,   /**< container collecting all domain reductions found */
   BINCONSDATA*          binconsdata,        /**< container collecting all binary constraints */
   CANDIDATELIST*        candidates,         /**< list of candidates to branch on */
   BRANCHINGDECISION*    decision,           /**< struct to store the final decision */
   SCORECONTAINER*       scorecontainer,     /**< container to retrieve already calculated scores */
   SCIP_Bool             storewarmstartinfo, /**< should lp information be stored? */
   int                   recursiondepth,     /**< remaining recursion depth */
   SCIP_Real             lpobjval,           /**< base LP objective value */
   SCIP_Longint*         niterations         /**< pointer to store total number of iterations for this variable */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< general statistical data */
   ,LOCALSTATISTICS*     localstats          /**< local statistics, may be disregarded */
#endif
   )
{
   int nlpcands;
#ifdef SCIP_STATISTIC
   int probingdepth;
#endif

   assert(scip != NULL);
   assert(status != NULL);
   assert(config != NULL);
   assert(!config->usedomainreduction || domainreductions != NULL);
   assert(!config->usebincons || binconsdata != NULL);
   assert(candidates != NULL);
   assert(candidates->ncandidates > 0);
   assert(decision != NULL);
   assert(recursiondepth >= 1);
#ifdef SCIP_STATISTIC
   assert(statistics != NULL);
#endif

   nlpcands = candidates->ncandidates;
#ifdef SCIP_STATISTIC
   probingdepth = SCIPgetProbingDepth(scip);
#endif

   /* init default decision */
   decision->var = candidates->candidates[0]->branchvar;
   decision->val = candidates->candidates[0]->branchval;
   decision->downdb = lpobjval;
   decision->downdbvalid = TRUE;
   decision->updb = lpobjval;
   decision->updbvalid = TRUE;
   decision->proveddb = lpobjval;

   if( !config->forcebranching && nlpcands == 1 )
   {
      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Only one candidate (<%s>) is given. This one is chosen without "
            "calculations.\n", SCIPvarGetName(decision->var));
#ifdef SCIP_STATISTIC
      statistics->nsinglecandidate[probingdepth]++;
#endif
   }
   else
   {
      if( SCIP_MAXTREEDEPTH <= (SCIPgetDepth(scip) + recursiondepth) )
      {
         /* we need at least 'recursiondepth' space for the branching */
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Cannot perform probing in selectVarRecursive, depth limit reached. "
               "Current:<%i>, Max:<%i>\n", SCIP_MAXTREEDEPTH, SCIPgetDepth(scip) + recursiondepth);
         status->depthtoosmall = TRUE;
#ifdef SCIP_STATISTIC
         statistics->ndepthreached++;
#endif
      }
      else
      {
         BRANCHINGRESULTDATA* downbranchingresult;
         BRANCHINGRESULTDATA* upbranchingresult;
         SCIP_LPI* lpi;
         SCIP_Real bestscore = -SCIPinfinity(scip);
         SCIP_Real bestscorelowerbound;
         SCIP_Real bestscoreupperbound;
         SCIP_Real localbaselpsolval = lpobjval;
         const int start = (persistent != NULL && !config->abbreviated) ? persistent->restartindex : 0;
         int i;
         int c;

         bestscorelowerbound = SCIPvarGetLbLocal(decision->var);
         bestscoreupperbound = SCIPvarGetUbLocal(decision->var);

         SCIP_CALL( branchingResultDataCreate(scip, &downbranchingresult) );
         SCIP_CALL( branchingResultDataCreate(scip, &upbranchingresult) );

         SCIP_CALL( SCIPgetLPI(scip, &lpi) );

#ifdef SCIP_DEBUG
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Started selectVarRecursive with <%i> candidates: ", nlpcands);
         printCandidates(scip, SCIP_VERBLEVEL_HIGH, candidates);
#endif

         LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Starting loop from index %d\n", start);

         for( i = 0, c = start;
            isBranchFurtherLoopDecrement(status, &c) && i < nlpcands && !SCIPisStopped(scip); i++, c++)
         {
            SCIP_Bool useoldbranching = FALSE;
            SCIP_Real oldlpobjval = -SCIPinfinity(scip);
            CANDIDATE* candidate;
            SCIP_VAR* branchvar;
            SCIP_Real branchval;
            SCIP_Real branchlb;
            SCIP_Real branchub;
#ifdef SCIP_STATISTIC
            SCIP_Bool addeddomainreduction = FALSE;
#endif

            c = c % nlpcands;

            candidate = candidates->candidates[c];

            assert(candidate != NULL);

            branchvar = candidate->branchvar;
            branchval = candidate->branchval;

            assert(branchvar != NULL);

            branchlb = SCIPvarGetLbLocal(branchvar);
            branchub = SCIPvarGetUbLocal(branchvar);

            if( SCIPisEQ(scip, branchlb, branchub) )
            {
               /* if both bounds are equal the variable is fixed and we cannot branch
                * this may happen if domain propagation on other candidates finds better bounds for the current candidate
                */
               status->propagationdomred = TRUE;
#ifdef SCIP_STATISTIC
               {
                  int probingdepth = SCIPgetProbingDepth(scip);
                  statistics->npropdomred[probingdepth]++;
               }
#endif
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Domain Propagation changed the bounds of a branching candidate."
                     "\n");
               continue;
            }

#ifdef SCIP_STATISTIC
            /* Reset the cutoffproofnodes, as the number of proof nodes from previous branching vars (which where not
             * cutoff, as we didn't break the loop) is not relevant for the min total sum of proof nodes.
             */
            localstats->ncutoffproofnodes = 0;
#endif

            branchingResultDataInit(scip, downbranchingresult);
            branchingResultDataInit(scip, upbranchingresult);

            /* use old lookahead branching result, if last call on this variable is not too long ago */
            if( persistent != NULL && isUseOldBranching(scip, config, branchvar) )
            {
               SCIP_CALL( getOldBranching(scip, persistent, branchvar, downbranchingresult, upbranchingresult,
                     &oldlpobjval) );
               useoldbranching = TRUE;
#ifdef SCIP_STATISTIC
               statistics->noldbranchused[probingdepth]++;
#endif
            }
            else
            {
               DOMAINREDUCTIONS* downdomainreductions = NULL;
               DOMAINREDUCTIONS* updomainreductions = NULL;
               DOMAINREDUCTIONS* localdomainreductions;
               BRANCHINGRESULTDATA* localbranchingresult;
               SCIP_Bool down;
               int k;

               LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, "Started branching on var <%s> with val <%g> and bounds "
                     "[<%g>..<%g>]\n", SCIPvarGetName(branchvar), branchval, SCIPvarGetLbLocal(branchvar),
                     SCIPvarGetUbLocal(branchvar));

               if( config->usedomainreduction )
               {
                  SCIP_CALL( domainReductionsCreate(scip, &downdomainreductions) );
                  SCIP_CALL( domainReductionsCreate(scip, &updomainreductions) );
               }

               down = config->downfirst;

               for( k = 0; k < 2; ++k )
               {
                  localdomainreductions = down ? downdomainreductions : updomainreductions;
                  localbranchingresult = down ? downbranchingresult : upbranchingresult;


#ifdef SCIP_STATISTIC /* ????????????????? */
                  SCIP_CALL( executeBranchingRecursive(scip, status, config, baselpsol, candidate, localbaselpsolval,
                        recursiondepth, localdomainreductions, binconsdata, localbranchingresult, scorecontainer,
                        storewarmstartinfo, down, addeddomainreduction, statistics, localstats) );
#else
                  SCIP_CALL( executeBranchingRecursive(scip, status, config, baselpsol, candidate, localbaselpsolval,
                        recursiondepth, localdomainreductions, binconsdata, localbranchingresult, scorecontainer,
                        storewarmstartinfo, down) );
#endif

                  /* check whether a new solutions rendered the previous child infeasible */
                  /* Check, if all existing columns are in LP.
                   * If this is not the case, we may still return that the up and down dual bounds are valid, because the
                   * branching rule should not apply them otherwise.
                   * However, we must not set the downinf or upinf pointers to TRUE based on the dual bound, because we
                   * cannot guarantee that this node can be cut off.
                   */
                  /* @todo break if result is infeasible (probably only in first layer) */
                  if( SCIPallColsInLP(scip) )
                  {
                     if( SCIPisGE(scip, localbranchingresult->dualbound, SCIPgetCutoffbound(scip)) )
                     {
                        localbranchingresult->cutoff = TRUE;
                        LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH,
                           "The up branching changed the cutoffbound and rendered the %s branching result infeasible.\n",
                           down ? "down" : "up");
                     }
                  }

                  down = !down;
               }

               LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, "-> down=%.9g (gain=%.9g, valid=%u, inf=%u), up=%.9g "
                     "(gain=%.9g, valid=%u, inf=%u)\n", downbranchingresult->dualbound,
                     downbranchingresult->dualbound - lpobjval, downbranchingresult->dualboundvalid,
                     downbranchingresult->cutoff, upbranchingresult->dualbound, upbranchingresult->dualbound - lpobjval,
                     upbranchingresult->dualboundvalid, upbranchingresult->cutoff);

               if( niterations != NULL )
               {
                  *niterations += downbranchingresult->niterations + upbranchingresult->niterations;
               }
               /* store results of branching call */
               if( persistent != NULL )
               {
                  SCIP_Longint iter = downbranchingresult->niterations + upbranchingresult->niterations;
                  SCIP_CALL( updateOldBranching(scip, persistent, branchvar, branchval, iter, downbranchingresult,
                     upbranchingresult, lpobjval) );
               }

               /* merge domain changes from the two child nodes */
               if( config->usedomainreduction )
               {
                  applyDeeperDomainReductions(scip, status, baselpsol, domainreductions, downdomainreductions,
                        updomainreductions);

                  domainReductionsFree(scip, &updomainreductions);
                  domainReductionsFree(scip, &downdomainreductions);
               }
            }

            if( !status->lperror && !status->limitreached )
            {
               SCIP_Real score;
               SCIP_Real scoringlpobjval = useoldbranching ? oldlpobjval : lpobjval;

               if( upbranchingresult->cutoff && downbranchingresult->cutoff )
               {
                  score = calculcateScoreFromResult(scip, branchvar, NULL, NULL, scoringlpobjval);

                  LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, " -> variable <%s> is infeasible in both directions\n",
                     SCIPvarGetName(branchvar));

                  /* in a higher level this cutoff may be transferred as a domain reduction/valid bound */
                  status->cutoff = TRUE;
#ifdef SCIP_STATISTIC
                  statistics->nfullcutoffs[probingdepth]++;
                  localstats->ncutoffproofnodes += 2;
#endif
               }
               else if( upbranchingresult->cutoff )
               {
                  score = calculcateScoreFromResult(scip, branchvar, downbranchingresult, NULL, scoringlpobjval);

                  LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, " -> variable <%s> is infeasible in upward branch\n",
                     SCIPvarGetName(branchvar));

                  if( config->usedomainreduction && !useoldbranching )
                  {
                     addUpperBound(scip, branchvar, branchval, baselpsol, domainreductions, &status->domred);
#ifdef SCIP_STATISTIC
                     addeddomainreduction = TRUE;
#endif
                  }

                  if( downbranchingresult->dualboundvalid )
                  {
                     decision->proveddb = MAX(decision->proveddb, downbranchingresult->dualbound);
                  }

#ifdef SCIP_STATISTIC
                  statistics->nsinglecutoffs[probingdepth]++;
#endif
               }
               else if( downbranchingresult->cutoff )
               {
                  score = calculcateScoreFromResult(scip, branchvar, NULL, upbranchingresult, scoringlpobjval);

                  LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, " -> variable <%s> is infeasible in downward branch\n",
                     SCIPvarGetName(branchvar));

#ifdef SCIP_STATISTIC
                  statistics->nsinglecutoffs[probingdepth]++;
#endif

                  if( config->usedomainreduction && !useoldbranching )
                  {
                     addLowerBound(scip, branchvar, branchval, baselpsol, domainreductions, &status->domred);
#ifdef SCIP_STATISTIC
                     addeddomainreduction = TRUE;
#endif
                  }
                  if( upbranchingresult->dualboundvalid )
                  {
                     decision->proveddb = MAX(decision->proveddb, upbranchingresult->dualbound);
                  }
               }
               else
               {
                  LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Neither branch is cutoff and no limit reached.\n");

                  score = calculcateScoreFromResult(scip, branchvar, downbranchingresult, upbranchingresult,
                        scoringlpobjval);

                  if( upbranchingresult->dualboundvalid && downbranchingresult->dualboundvalid )
                  {
                     decision->proveddb = MAX(decision->proveddb, MIN(upbranchingresult->dualbound,
                           downbranchingresult->dualbound));
                  }
               }

               if( SCIPisGE(scip, score, bestscore) )
               {
                  LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Old best var <%s> with bounds [<%g>..<%g>] and score %g\n",
                     SCIPvarGetName(decision->var), bestscorelowerbound, bestscoreupperbound, bestscore);

                  bestscore = score;

                  decision->var = branchvar;
                  decision->val = branchval;
                  decision->downdb = downbranchingresult->dualbound;
                  decision->downdbvalid = downbranchingresult->dualboundvalid;
                  decision->updb = upbranchingresult->dualbound;
                  decision->updbvalid = upbranchingresult->dualboundvalid;

                  bestscorelowerbound = branchlb;
                  bestscoreupperbound = branchub;

                  LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "New best var <%s> with bounds [<%g>..<%g>] and score %g\n",
                     SCIPvarGetName(decision->var), bestscorelowerbound, bestscoreupperbound, bestscore);
               }

#ifdef SCIP_DEBUG
               if( !upbranchingresult->cutoff && !downbranchingresult->cutoff )
               {
                  SCIP_Real downgain = MAX(downbranchingresult->objval - scoringlpobjval, 0);
                  SCIP_Real upgain = MAX(upbranchingresult->objval - scoringlpobjval, 0);

                  LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, " -> cand %d/%d var <%s> (solval=%g, downgain=%g, upgain=%g,"
                        " score=%g) -- best: <%s> (%g)\n", c, nlpcands, SCIPvarGetName(branchvar), branchval, downgain,
                        upgain, score, SCIPvarGetName(decision->var), bestscore);
               }
#endif

               if( scorecontainer != NULL && storewarmstartinfo )
               {
                  /* store the score for this variable to reuse it later (assuming this is a FSB run, later means the
                   * following LAB run.) ??????????????????????
                   */
                  SCIP_CALL( scoreContainerSetScore(scip, scorecontainer, candidates, branchvar, score) );
               }

               if( config->maxnviolatedcons != -1 && (config->usebincons || config->usedomainreduction) &&
                     !useoldbranching )
               {
                  int nimpliedbincons = 0;
                  int ndomreds = 0;

                  if( config->usebincons )
                  {
                     nimpliedbincons = binconsdata->createdconstraints->nviolatedcons;
                     LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Found <%i> violating binary constraints.\n",
                           nimpliedbincons);
                  }

                  if( config->usedomainreduction )
                  {
                     ndomreds = domainreductions->nchangedvars;
                     LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Found <%i> bound changes.\n", ndomreds);
                  }

                  /* @todo add two individual limits for constraints and dom changes */
                  if( nimpliedbincons + ndomreds >= config->maxnviolatedcons )
                  {
                     status->maxnconsreached = TRUE;
                  }
               }

#ifdef SCIP_STATISTIC
               /* Increment the number of domredproofnodes by one, as we needed the current node as a proof node. */
               /* ????????????? */
               if( addeddomainreduction )
               {
                  localstats->ndomredproofnodes++;
               }
#endif
            }

            if( areBoundsChanged(scip, decision->var, bestscorelowerbound, bestscoreupperbound) )
            {
               /* in case the bounds of the current highest scored solution have changed due to domain propagation during
                * the lookahead branching we can/should not branch on this variable but instead report the domain
                * reduction */
               status->propagationdomred = TRUE;
#ifdef SCIP_STATISTIC
               {
                  int probingdepth = SCIPgetProbingDepth(scip);
                  statistics->npropdomred[probingdepth]++;
               }
#endif
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Domain Propagation changed the bounds of a branching candidate."
                     "\n");
            }
         }

         branchingResultDataFree(scip, &upbranchingresult);
         branchingResultDataFree(scip, &downbranchingresult);

         if( persistent != NULL && !config->abbreviated )
         {
            persistent->restartindex = c;
         }
      }
   }

   return SCIP_OKAY;
}

/** checks whether the current decision should be stored. This is the case if we only found non violated domain reductions
 * and constraints. Then our current decision still holds true for the next call and can be reused without further
 * calculations */
static
SCIP_Bool isStoreDecision(
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   BINCONSDATA*          binconsdata,        /**< container collecting all binary constraints */
   DOMAINREDUCTIONS*     domainreductions    /**< container collecting all domain reductions found */
   )
{
   SCIP_Bool noviolatingbincons;
   SCIP_Bool noviolatingdomreds;

   noviolatingbincons = binconsdata != NULL && binconsdata->createdconstraints->nconstraints > 0 &&
         binconsdata->createdconstraints->nviolatedcons == 0;
   noviolatingdomreds = domainreductions != NULL && domainreductions->nchangedvars > 0 &&
         domainreductions->nviolatedvars == 0;
   return config->storeunviolatedsol && noviolatingbincons && noviolatingdomreds;
}

/** starting point to obtain a branching decision via LAB/ALAB. */
static
SCIP_RETCODE selectVarStart(
   SCIP*                 scip,               /**< SCIP data structure */
   CONFIGURATION*        config,             /**< the configuration of the branching rule */
   PERSISTENTDATA*       persistent,         /**< container to store data over multiple calls to the branching rule */
   STATUS*               status,             /**< current status */
   BRANCHINGDECISION*    decision,           /**< struct to store the final decision */
   SCORECONTAINER*       scorecontainer,     /**< container to retrieve already calculated scores */
   SCIP_Bool             storewarmstartinfo, /**< should lp information be stored? */
   CANDIDATELIST*        candidates          /**< list of candidates to branch on */
#ifdef SCIP_STATISTIC
   ,STATISTICS*          statistics          /**< general statistical data */
   ,LOCALSTATISTICS*     localstats          /**< local statistics, may be disregarded */
#endif
   )
{
   int recursiondepth;
   DOMAINREDUCTIONS* domainreductions = NULL;
   BINCONSDATA* binconsdata = NULL;
   SCIP_SOL* baselpsol = NULL;
   SCIP_Bool inprobing;
   SCIP_Real lpobjval;

   assert(scip != NULL);
   assert(config != NULL);
   assert(status != NULL);
   assert(decision != NULL);
#ifdef SCIP_STATISTIC
   assert(statistics != NULL);
#endif

   inprobing = SCIPinProbing(scip);
   recursiondepth = config->recursiondepth;

   assert(recursiondepth > 0);

   lpobjval = SCIPgetLPObjval(scip);
   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The objective value of the base lp is <%g>\n", lpobjval);

   if( config->usedomainreduction || config->usebincons )
   {
      /* we have to copy the current solution before getting the candidates, as we possibly solve some LPs during
       * the getter and as such would get a wrong LP copied */
      SCIP_CALL( copyCurrentSolution(scip, &baselpsol) );
   }

#ifdef SCIP_STATISTIC
   SCIP_CALL( filterCandidates(scip, config, status, scorecontainer, candidates, statistics) );
#else
   SCIP_CALL( filterCandidates(scip, config, status, scorecontainer, candidates) );
#endif

   /* the status may have changed because of FSB to get the best candidates
    * if that is the case we already changed the base node and should start again */
   if( isBranchFurther(status) )
   {
      assert(candidates->ncandidates > 0);

      /* we are at the top level, allocate some more data structures and start strong branching mode */
      if( !inprobing )
      {
         if( config->usedomainreduction )
         {
            SCIP_CALL( domainReductionsCreate(scip, &domainreductions) );
         }

         if( config->usebincons )
         {
            SCIP_CALL( binConsDataCreate(scip, &binconsdata, recursiondepth,
                  (int)SCIPceil(scip, 0.5*candidates->ncandidates)) );
         }

         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "About to start probing.\n");
         SCIP_CALL( SCIPstartStrongbranch(scip, TRUE) );
         SCIPenableVarHistory(scip);
      }

#ifdef SCIP_STATISTIC
      SCIP_CALL( selectVarRecursive(scip, status, persistent, config, baselpsol, domainreductions, binconsdata, candidates,
            decision, scorecontainer, storewarmstartinfo, recursiondepth, lpobjval, NULL, statistics, localstats) );
#else
      SCIP_CALL( selectVarRecursive(scip, status, persistent, config, baselpsol, domainreductions, binconsdata, candidates,
            decision, scorecontainer, storewarmstartinfo, recursiondepth, lpobjval, NULL) );
#endif

      /* we are at the top level */
      if( !inprobing )
      {
         SCIP_CALL( SCIPendStrongbranch(scip) );
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Ended probing.\n");

         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Applying found data to the base node.\n");

         /* only unviolating constraints and domain changes: store branching decision */
         if( persistent != NULL && isStoreDecision(config, binconsdata, domainreductions) )
         {
            SCIP_CALL( SCIPlinkLPSol(scip, persistent->prevbinsolution) );
            SCIP_CALL( SCIPunlinkSol(scip, persistent->prevbinsolution) );

            branchingDecisionCopy(decision, persistent->prevdecision);
         }

         /* apply binary constraints */
         if( config->usebincons )
         {
            SCIP_NODE* basenode;

            assert(binconsdata != NULL);

            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Applying binary constraints to the base node.\n");

            assert(binconsdata->binaryvars->nbinaryvars == 0);

            basenode = SCIPgetCurrentNode(scip);

            /* @todo are the first two checks needed? */
            if( /*!status->lperror && !status->depthtoosmall &&*/ !status->cutoff )
            {
#ifdef SCIP_STATISTIC
               SCIP_CALL( applyBinaryConstraints(scip, basenode, binconsdata->createdconstraints, config, &status->addbinconst,
                     &status->cutoff, &status->domred, statistics) );
#else
               SCIP_CALL( applyBinaryConstraints(scip, basenode, binconsdata->createdconstraints, config, &status->addbinconst,
                     &status->cutoff, &status->domred) );
#endif
            }
            binConsDataFree(scip, &binconsdata);

         }

         /* apply domain reductions */
         if( config->usedomainreduction )
         {
            assert(domainreductions != NULL);

            /* @todo are the first two checks needed? */
            if( /*!status->lperror && !status->depthtoosmall &&*/ !status->cutoff )
            {
               LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Applying domain reductions to the base node.\n");
#ifdef SCIP_STATISTIC
               SCIP_CALL( applyDomainReductions(scip, baselpsol, domainreductions, &status->domredcutoff,
                     &status->domred, statistics) );
#else
               SCIP_CALL( applyDomainReductions(scip, baselpsol, domainreductions, &status->domredcutoff,
                     &status->domred) );
#endif
            }
            domainReductionsFree(scip, &domainreductions);
         }
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Applied found data to the base node.\n");
      }

#ifdef SCIP_STATISTIC
      if( config->abbreviated )
      {
         if( candidates->ncandidates > 0 )
         {
            int i;

            assert(candidates->ncandidates <= statistics->maxnbestcands);

            /* find the "FSB-index" of the decision */
            for( i = 0; i < candidates->ncandidates; i++ )
            {
               SCIP_VAR* var = candidates->candidates[i]->branchvar;

               if( decision->var == var )
               {
                  statistics->chosenfsbcand[i] += 1;
                  break;
               }
            }
         }
      }
#endif
   }
#ifdef SCIP_STATISTIC
   else
   {
      statistics->stopafterfsb++;

      if( status->cutoff )
      {
         statistics->cutoffafterfsb++;
      }
      else
      {
         statistics->domredafterfsb++;
      }
   }
#endif

#ifdef SCIP_DEBUG
   if( config->abbreviated )
   {
      if( candidates->ncandidates > 0 )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Strong Branching would branch on variable <%s>\n",
            SCIPvarGetName(candidates->candidates[0]->branchvar));

         if( isBranchFurther(status) && branchingDecisionIsValid(decision) )
         {
            LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Lookahead Branching would branch on variable <%s>\n",
               SCIPvarGetName(decision->var));
         }

      }
      else if( status->domred )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Strong Branching has added domain reductions. LAB restarts.\n");
      }
      else if( status->cutoff )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Strong Branching cutoff this node.\n");
      }
      else
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Something unexpected happened.");
         SCIPABORT();
      }
   }
#endif

   if( config->usedomainreduction || config->usebincons )
   {
      SCIP_CALL( SCIPfreeSol(scip, &baselpsol) );
   }

   return SCIP_OKAY;
}

/**
 * We can use the previous result, stored in the branchruledata, if the branchingvariable (as an indicator) is set and
 * the current lp solution is equal to the previous lp solution.
 *
 * @return \ref TRUE, if we can branch on the previous decision, \ref FALSE, else.
 */
static
SCIP_Bool isUsePreviousResult(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             currentsol,         /**< the current base lp solution */
   PERSISTENTDATA*       persistent          /**< container to store data over multiple calls to the branching rule */
   )
{
   return branchingDecisionIsValid(persistent->prevdecision)
      && SCIPareSolsEqual(scip, currentsol, persistent->prevbinsolution);
}

/**
 * Uses the results from the previous run saved in the branchruledata to branch.
 * This is the case, if in the previous run only non-violating constraints were added. In that case we can use the
 * branching decision we would have made then.
 * If everything worked the result pointer contains SCIP_BRANCHED.
 *
 * @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 */
static
SCIP_RETCODE usePreviousResult(
   SCIP*                 scip,               /**< SCIP data structure */
   BRANCHINGDECISION*    decision,           /**< decision to branch on */
   SCIP_RESULT*          result              /**< the pointer to the branching result */
   )
{
   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Branching based on previous solution.\n");

   /* execute the actual branching */
   SCIP_CALL( branchOnVar(scip, decision) );
   *result = SCIP_BRANCHED;

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Branched based on previous solution. Variable <%s>\n",
      SCIPvarGetName(decision->var));

   /* reset the var pointer, as this is our indicator whether we should branch on prev data in the next call */
   decision->var = NULL;

   return SCIP_OKAY;
}

/** initializes the branchruledata and the contained structs */
static
SCIP_RETCODE initBranchruleData(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULEDATA*  branchruledata      /**< the branch rule data to initialize */
   )
{
   int nvars;
   int i;

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Entering initBranchruleData\n");

   /* Create an empty solution. Gets filled in case of implied binary bounds. */
   SCIP_CALL( SCIPcreateSol(scip, &branchruledata->persistent->prevbinsolution, NULL) );

   /* The variables given by the SCIPgetVars() array are sorted with the binaries at first and the integer variables
    * directly afterwards. With the SCIPvarGetProbindex() method we can access the index of a given variable in the
    * SCIPgetVars() array and as such we can use it to access our arrays which should only contain binary and integer
    * variables.
    */
   nvars = SCIPgetNBinVars(scip) + SCIPgetNIntVars(scip);

   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &branchruledata->persistent->lastbranchid, nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &branchruledata->persistent->lastbranchnlps, nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &branchruledata->persistent->lastbranchupres, nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &branchruledata->persistent->lastbranchdownres, nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &branchruledata->persistent->lastbranchlpobjval, nvars) );

   branchruledata->persistent->prevdecision->var = NULL;

   for( i = 0; i < nvars; i++ )
   {
      branchruledata->persistent->lastbranchid[i] = -1;
      branchruledata->persistent->lastbranchnlps[i] = 0;

      SCIP_CALL( SCIPallocBlockMemory(scip, &branchruledata->persistent->lastbranchupres[i]) ); /*lint !e866*/
      SCIP_CALL( SCIPallocBlockMemory(scip, &branchruledata->persistent->lastbranchdownres[i]) ); /*lint !e866*/
   }

   branchruledata->isinitialized = TRUE;

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Leaving initBranchruleData\n");

   return SCIP_OKAY;
}

/*
 * Callback methods of branching rule
 */

/** copy method for branchrule plugins (called when SCIP copies plugins) */
static
SCIP_DECL_BRANCHCOPY(branchCopyLookahead)
{  /*lint --e{715}*/
   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Entering branchCopyLookahead\n");

   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);

   SCIP_CALL( SCIPincludeBranchruleLookahead(scip) );

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Leaving branchCopyLookahead\n");

   return SCIP_OKAY;
}

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BRANCHFREE(branchFreeLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Entering branchFreeLookahead\n");

   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   SCIPfreeMemory(scip, &branchruledata->persistent->prevdecision);
   SCIPfreeMemory(scip, &branchruledata->persistent);
   SCIPfreeMemory(scip, &branchruledata->config);
   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Leaving branchFreeLookahead\n");

   return SCIP_OKAY;
}

/** initialization method of branching rule (called after problem was transformed) */
static
SCIP_DECL_BRANCHINIT(branchInitLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Entering branchInitLookahead\n");

   branchruledata = SCIPbranchruleGetData(branchrule);
   branchruledata->persistent->restartindex = 0;

#ifdef SCIP_STATISTIC
   {
      int recursiondepth;
      int maxncands;

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Allocating space for the statistics struct.\n");

      recursiondepth = branchruledata->config->recursiondepth;
      maxncands = branchruledata->config->maxncands;

      SCIP_CALL( SCIPallocMemory(scip, &branchruledata->statistics) );
      /* 17 current number of possible result values and the index is 1 based, so 17 + 1 as array size with unused 0
       * element */
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nresults, 17 + 1) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nsinglecutoffs, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nfullcutoffs, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nlpssolved, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nlpssolvedfsb, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nlpiterations, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nlpiterationsfsb, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->npropdomred, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->nsinglecandidate, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->noldbranchused, recursiondepth) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->statistics->chosenfsbcand, maxncands) );

      branchruledata->statistics->recursiondepth = recursiondepth;
      branchruledata->statistics->maxnbestcands = maxncands;

      statisticsInit(branchruledata->statistics);
   }
#endif

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Leaving branchInitLookahead\n");

   return SCIP_OKAY;
}

#ifdef SCIP_STATISTIC
/** deinitialization method of branching rule (called before transformed problem is freed) */
static
SCIP_DECL_BRANCHEXIT(branchExitLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;
   STATISTICS* statistics;

   branchruledata = SCIPbranchruleGetData(branchrule);
   statistics = branchruledata->statistics;

   statisticsPrint(scip, statistics);

   SCIPfreeMemoryArray(scip, &statistics->chosenfsbcand);
   SCIPfreeMemoryArray(scip, &statistics->noldbranchused);
   SCIPfreeMemoryArray(scip, &statistics->nsinglecandidate);
   SCIPfreeMemoryArray(scip, &statistics->npropdomred);
   SCIPfreeMemoryArray(scip, &statistics->nlpiterationsfsb);
   SCIPfreeMemoryArray(scip, &statistics->nlpiterations);
   SCIPfreeMemoryArray(scip, &statistics->nlpssolvedfsb);
   SCIPfreeMemoryArray(scip, &statistics->nlpssolved);
   SCIPfreeMemoryArray(scip, &statistics->nfullcutoffs);
   SCIPfreeMemoryArray(scip, &statistics->nsinglecutoffs);
   SCIPfreeMemoryArray(scip, &statistics->nresults);
   SCIPfreeMemory(scip, &statistics);

   return SCIP_OKAY;
}
#endif

/** solving process deinitialization method of branching rule (called before branch and bound process data is freed) */
static
SCIP_DECL_BRANCHEXITSOL(branchExitSolLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Entering branchExitSolLookahead\n");

   branchruledata = SCIPbranchruleGetData(branchrule);

   if( branchruledata->isinitialized )
   {
      int nvars;
      int i;

      nvars = SCIPgetNBinVars(scip) + SCIPgetNIntVars(scip);

      for( i = nvars-1; i >= 0; i--)
      {
         SCIPfreeBlockMemory(scip, &branchruledata->persistent->lastbranchdownres[i]); /*lint !e866*/
         SCIPfreeBlockMemory(scip, &branchruledata->persistent->lastbranchupres[i]); /*lint !e866*/
      }

      SCIPfreeBlockMemoryArray(scip, &branchruledata->persistent->lastbranchlpobjval, nvars);
      SCIPfreeBlockMemoryArray(scip, &branchruledata->persistent->lastbranchdownres, nvars);
      SCIPfreeBlockMemoryArray(scip, &branchruledata->persistent->lastbranchupres, nvars);
      SCIPfreeBlockMemoryArray(scip, &branchruledata->persistent->lastbranchnlps, nvars);
      SCIPfreeBlockMemoryArray(scip, &branchruledata->persistent->lastbranchid, nvars);

      /* Free the solution that was used for implied binary bounds. */
      SCIP_CALL( SCIPfreeSol(scip, &branchruledata->persistent->prevbinsolution) );

      branchruledata->isinitialized = FALSE;

      LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Freed branchruledata\n");
   }

   LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Leaving branchExitSolLookahead\n");

   return SCIP_OKAY;
}

/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpLookahead)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_SOL* baselpsol = NULL;
   CONFIGURATION* config;
   SCIP_Bool userusebincons;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Entering branchExeclpLookahead.\n");

   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   config = branchruledata->config;

   /* we are only allowed to add binary constraints, if the corresponding flag is given */
   userusebincons = config->usebincons;
   config->usebincons = config->usebincons && allowaddcons;

   if( !branchruledata->isinitialized )
   {
      SCIP_CALL( initBranchruleData(scip, branchruledata) );
   }

   if( config->usebincons || config->usedomainreduction )
   {
      /* create a copy of the current lp solution to compare it with a previously  */
      SCIP_CALL( copyCurrentSolution(scip, &baselpsol) );
      LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Created an unlinked copy of the base lp solution.\n");
   }

   if( config->storeunviolatedsol
         && (config->usebincons || config->usedomainreduction)
         && isUsePreviousResult(scip, baselpsol, branchruledata->persistent) )
   {
      /* in case we stopped the previous run without a branching decisions we have stored the decision and execute it
       * now */
      SCIP_CALL( usePreviousResult(scip, branchruledata->persistent->prevdecision, result) );
   }
   else
   {
      BRANCHINGDECISION* decision;
      SCORECONTAINER* scorecontainer = NULL;
      CANDIDATELIST* candidates;
      STATUS* status;
#ifdef SCIP_STATISTIC
      LOCALSTATISTICS* localstats;
#endif

      /* create a struct to store the algorithm status */
      SCIP_CALL( statusCreate(scip, &status) );

      /* create a struct to store the branching decision (in case there is one) */
      SCIP_CALL( branchingDecisionCreate(scip, &decision) );
      if( config->abbreviated )
      {
         /* allocate and init the container used to store the FSB scores, later used to filter the candidates */
         SCIP_CALL( scoreContainerCreate(scip, &scorecontainer, config) );
      }

      SCIP_CALL( candidateListGetAllFractionalCandidates(scip, config->abbreviated && config->reusebasis, &candidates) );

      LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "The base lp has <%i> variables with fractional value.\n",
            candidates->ncandidates);

      /* execute the main logic */
#ifdef SCIP_STATISTIC
      /* create a struct to store the statistics needed for this single run */
      SCIP_CALL( localStatisticsAllocate(scip, &localstats) );
      SCIP_CALL( selectVarStart(scip, config, branchruledata->persistent, status, decision,
            scorecontainer, FALSE, candidates, branchruledata->statistics, localstats) );
#else
      SCIP_CALL( selectVarStart(scip, config, branchruledata->persistent, status, decision,
            scorecontainer, FALSE, candidates) );
#endif

      if( status->cutoff || status->domredcutoff )
      {
         *result = SCIP_CUTOFF;
#ifdef SCIP_STATISTIC
         branchruledata->statistics->ncutoffproofnodes += localstats->ncutoffproofnodes;
#endif
      }
      else if( status->addbinconst )
      {
         *result = SCIP_CONSADDED;
      }
      else if( status->domred || status->propagationdomred )
      {
         *result = SCIP_REDUCEDDOM;
      }
      else if( status->lperror )
      {
         *result = SCIP_DIDNOTFIND;
      }

      LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Result before branching is %s\n", getStatusString(*result));

      if( *result != SCIP_CUTOFF /* a variable could not be branched in any direction or any of the calculated domain
                                  * reductions was infeasible */
         && *result != SCIP_REDUCEDDOM /* the domain of a variable was reduced by evaluating the calculated cutoffs */
         && *result != SCIP_CONSADDED /* implied binary constraints were already added */
         && *result != SCIP_DIDNOTFIND /* an lp error occurred on the way */
         && !status->depthtoosmall /* branching depth wasn't high enough */
         && branchingDecisionIsValid(decision)
         /*&& (0 <= bestcand && bestcand < nlpcands)*/ /* no valid candidate index could be found */
         )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_NORMAL, " -> %d candidates, selected variable <%s> (solval=%g, down=%g, "
               "up=%g)\n", candidates->ncandidates, SCIPvarGetName(decision->var), decision->val, decision->downdb,
               decision->updb);

         /* execute the branching as a result of the branching logic */
         SCIP_CALL( branchOnVar(scip, decision) );

         *result = SCIP_BRANCHED;
      }

#ifdef SCIP_DEBUG
      LABdebugMessage(scip, SCIP_VERBLEVEL_FULL, "Result after branching is %s\n", getStatusString(*result));

      if( *result == SCIP_BRANCHED )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Finished LookaheadBranching by branching.\n");
      }
      else if( *result == SCIP_REDUCEDDOM )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Finished LookaheadBranching by reducing domains.\n");
      }
      else if( *result == SCIP_CUTOFF )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Finished LookaheadBranching by cutting of, as the current "
               "problem is infeasible.\n");
      }
      else if( *result == SCIP_CONSADDED )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Finished LookaheadBranching by adding constraints.\n");
      }
      else if( *result == SCIP_DIDNOTFIND )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: An error occurred during the solving of one of the lps.\n");
      }
      else if( status->depthtoosmall )
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: The remaining tree depth did not allow for multi level "
               "lookahead branching.\n");
      }
      else
      {
         LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Result: Could not find any variable to branch on.\n");
      }
#endif

#ifdef SCIP_STATISTIC
      localStatisticsFree(scip, &localstats);
#endif
      SCIP_CALL( candidateListFree(scip, &candidates) );

      /* scorecontainer != NULL iff branchruledata->config->abbreviated == TRUE */
      if( scorecontainer != NULL )
      {
         SCIP_CALL( scoreContainerFree(scip, &scorecontainer) );
      }
      branchingDecisionFree(scip, &decision);
      statusFree(scip, &status);
   }

   if( config->usebincons )
   {
      SCIP_CALL( SCIPfreeSol(scip, &baselpsol) );
   }

#ifdef SCIP_STATISTIC
   branchruledata->statistics->ntotalresults++;
   branchruledata->statistics->nresults[*result]++;
#endif

   config->usebincons = userusebincons;

   LABdebugMessage(scip, SCIP_VERBLEVEL_HIGH, "Exiting branchExeclpLookahead.\n");

   return SCIP_OKAY;
}

/*
 * branching rule specific interface methods
 */

/** creates the lookahead branching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleLookahead(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_BRANCHRULE* branchrule;

   /* create lookahead branching rule data */
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata) );
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata->config) );

   /* needs to be allocated here, such that the previous decision can be filled and reset over multiple runs */
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata->persistent) );
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata->persistent->prevdecision) );
   branchruledata->isinitialized = FALSE;

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchruleBasic(scip, &branchrule, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY,
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchruledata) );

   assert(branchrule != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBranchruleCopy(scip, branchrule, branchCopyLookahead) );
   SCIP_CALL( SCIPsetBranchruleFree(scip, branchrule, branchFreeLookahead) );
   SCIP_CALL( SCIPsetBranchruleInit(scip, branchrule, branchInitLookahead) );
#ifdef SCIP_STATISTIC
   SCIP_CALL( SCIPsetBranchruleExit(scip, branchrule, branchExitLookahead) );
#endif
   SCIP_CALL( SCIPsetBranchruleExitsol(scip, branchrule, branchExitSolLookahead) );
   SCIP_CALL( SCIPsetBranchruleExecLp(scip, branchrule, branchExeclpLookahead) );

   /* add lookahead branching rule parameters */
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/useimpliedbincons",
         "should binary constraints be collected and applied?",
         &branchruledata->config->usebincons, TRUE, DEFAULT_USEBINARYCONSTRAINTS, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/addbinconsrow",
         "should binary constraints be added as rows to the base LP?",
         &branchruledata->config->addbinconsrow, TRUE, DEFAULT_ADDBINCONSROW, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "branching/lookahead/maxnumberviolatedcons",
         "how many constraints that are violated by the base lp solution should be gathered until the rule is stopped and "\
               "they are added?",
         &branchruledata->config->maxnviolatedcons, TRUE, DEFAULT_MAXNUMBERVIOLATEDCONS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddLongintParam(scip,
         "branching/lookahead/reevalage",
         "max number of LPs solved after which a previous prob branching results are recalculated",
         &branchruledata->config->reevalage, TRUE, DEFAULT_REEVALAGE, 0LL, SCIP_LONGINT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/forcebranching",
         "should LAB be forced, if only one candidate is given?",
         &branchruledata->config->forcebranching, TRUE, DEFAULT_FORCEBRANCHING, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "branching/lookahead/recursiondepth",
         "the max depth of LAB.",
         &branchruledata->config->recursiondepth, TRUE, DEFAULT_RECURSIONDEPTH, 1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/usedomainreduction",
         "should domain reductions be collected and applied?",
         &branchruledata->config->usedomainreduction, TRUE, DEFAULT_USEDOMAINREDUCTION, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/addnonviocons",
         "should binary constraints, that are not violated by the base LP, be collected and added?",
         &branchruledata->config->addnonviocons, TRUE, DEFAULT_ADDNONVIOCONS, NULL, NULL) );
   /* @todo: make switch in scip.c:20818 publicly available and use this here instead of the parameter */
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/downbranchfirst",
         "should the down branch be evaluated first?",
         &branchruledata->config->downfirst, TRUE, DEFAULT_DOWNFIRST, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/abbreviated",
         "toggles the abbreviated LAB.",
         &branchruledata->config->abbreviated, TRUE, DEFAULT_ABBREVIATED, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "branching/lookahead/maxncands",
         "if abbreviated: The max number of candidates to consider per node.",
         &branchruledata->config->maxncands, TRUE, DEFAULT_MAXNCANDS, 2, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/reusebasis",
         "if abbreviated: Should the information gathered to obtain the best candidates be reused?",
         &branchruledata->config->reusebasis, TRUE, DEFAULT_REUSEBASIS, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/storeunviolatedsol",
         "if only non violating constraints are added, should the branching decision be stored till the next call?",
         &branchruledata->config->storeunviolatedsol, TRUE, DEFAULT_STOREUNVIOLATEDSOL, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/abbrevpseudo",
         "if abbreviated: Use pseudo costs to estimate the score of a candidate.",
         &branchruledata->config->abbrevpseudo, TRUE, DEFAULT_ABBREVPSEUDO, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/addclique",
         "add binary constraints also as a clique.",
         &branchruledata->config->addclique, TRUE, DEFAULT_ADDCLIQUE, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/lookahead/propagate",
         "should domain propagation be executed before each temporary node is solved?",
         &branchruledata->config->propagate, TRUE, DEFAULT_PROPAGATE, NULL, NULL) );

   return SCIP_OKAY;
}
