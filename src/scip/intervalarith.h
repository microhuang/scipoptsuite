/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2010 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: intervalarith.h,v 1.28 2010/06/19 11:06:51 bzfviger Exp $"

/**@file   intervalarith.h
 * @brief  interval arithmetics for provable bounds
 * @author Tobias Achterberg
 * @author Stefan Vigerske
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_INTERVALARITH_H__
#define __SCIP_INTERVALARITH_H__


#include "scip/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/** interval given by infimum and supremum */
struct SCIP_Interval
{
   SCIP_Real             inf;                /**< infimum (lower bound) of interval */
   SCIP_Real             sup;                /**< supremum (upper bound) of interval */
};
typedef struct SCIP_Interval SCIP_INTERVAL;

/** rounding mode of floating point operations (upwards, downwards, nearest, ...)
 * exact values depend on machine and compiler, so we define a corresponding enum in the header file */
typedef int SCIP_ROUNDMODE;

/*
 * Interval arithmetic operations
 */

/** returns whether rounding mode control is available */
extern
SCIP_Bool SCIPintervalHasRoundingControl(
   void
   );

/** sets rounding mode of floating point operations */
extern
void SCIPintervalSetRoundingMode(
   SCIP_ROUNDMODE        roundmode           /**< rounding mode to activate */
   );

/** gets current rounding mode of floating point operations */
extern
SCIP_ROUNDMODE SCIPintervalGetRoundingMode(
   void
   );

/** sets rounding mode of floating point operations to downwards rounding */
extern
void SCIPintervalSetRoundingModeDownwards(
   void
   );

/** sets rounding mode of floating point operations to upwards rounding */
extern
void SCIPintervalSetRoundingModeUpwards(
   void
   );

#ifndef NDEBUG

/** returns infimum of interval */
extern
SCIP_Real SCIPintervalGetInf(
   SCIP_INTERVAL         interval            /**< interval */
   );

/** returns supremum of interval */
extern
SCIP_Real SCIPintervalGetSup(
   SCIP_INTERVAL         interval            /**< interval */
   );

/** stores given value as interval */
extern
void SCIPintervalSet(
   SCIP_INTERVAL*        resultant,          /**< interval to store value into */
   SCIP_Real             value               /**< value to store */
   );

/** stores given infimum and supremum as interval */
extern
void SCIPintervalSetBounds(
   SCIP_INTERVAL*        resultant,          /**< interval to store value into */
   SCIP_Real             inf,                /**< value to store as infimum */
   SCIP_Real             sup                 /**< value to store as supremum */
   );

/** sets interval to empty interval, which will be [infinity, -infinity] */
extern
void SCIPintervalSetEmpty(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant           /**< resultant interval of operation */
   );

/** indicates whether interval is empty, i.e., whether inf > sup */
extern
SCIP_Bool SCIPintervalIsEmpty(
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** sets interval to entire [-infinity, +infinity] */
extern
void SCIPintervalSetEntire(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant           /**< resultant interval of operation */
   );

/** indicates whether interval is entire, i.e., whether inf <= -infinity and sup >= infinity */
extern
SCIP_Bool SCIPintervalIsEntire(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

#else

/* In optimized mode, some methods are implemented as defines to reduce the number of function calls and
 * speed up the algorithms.
 */

#define SCIPintervalGetInf(interval)               (interval).inf
#define SCIPintervalGetSup(interval)               (interval).sup
#define SCIPintervalSet(resultant, value)          do { (resultant)->inf = (value);     (resultant)->sup = (value);     } while( FALSE )
#define SCIPintervalSetBounds(resultant, i, s)     do { (resultant)->inf = (i);         (resultant)->sup = (s);         } while( FALSE )
#define SCIPintervalSetEmpty(infinity, resultant)  do { (resultant)->inf =  (infinity); (resultant)->sup = -(infinity); } while( FALSE )
#define SCIPintervalSetEntire(infinity, resultant) do { (resultant)->inf = -(infinity); (resultant)->sup =  (infinity); } while( FALSE )
#define SCIPintervalIsEmpty(operand)               ( (operand).sup < (operand).inf )
#define SCIPintervalIsEntire(infinity, operand)    ( (operand).inf <= -(infinity) && (operand).sup >= (infinity) )

#endif

/** indicates whether operand1 is contained in operand2 */
extern
SCIP_Bool SCIPintervalIsSubsetEQ(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** intersection of two intervals */
extern
void SCIPintervalIntersect(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** interval enclosure of the union of two intervals */
extern
void SCIPintervalUnify(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** adds operand1 and operand2 and stores result in resultant */
extern
void SCIPintervalAdd(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** adds operand1 and scalar operand2 and stores result in resultant */
extern
void SCIPintervalAddScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** substracts operand2 from operand1 and stores result in resultant */
extern
void SCIPintervalSub(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** substracts scalar operand2 from operand1 and stores result in resultant */
extern
void SCIPintervalSubScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** undoes a substraction operation.
 * 
 * In number arithmetic, this would be addition.
 * Substractions of unbounded intervals cannot be undone, but resultant gives still a valid (but probably larger) interval.
 * 
 * This is a ''dirty'' operation.
 */
extern
void SCIPintervalUndoSub(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** multiplies operand1 with operand2 and stores result in resultant */
extern
void SCIPintervalMul(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** multiplies operand1 with scalar operand2 and stores result in resultant */
extern
void SCIPintervalMulScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** divides operand1 by operand2 and stores result in resultant */
extern
void SCIPintervalDiv(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** divides operand1 by scalar operand2 and stores result in resultant
 * 
 * if operand2 is 0.0, gives an empty interval as result */
extern
void SCIPintervalDivScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** computes an upper bound on the scalar product of two vectors of numbers and stores result in resultant
 * assumes that numbers are not at +/- infinity */
extern
void SCIPintervalScalarProductRealsSup(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_Real*            resultant,          /**< resultant of operation */
   int                   length,             /**< length of vectors */
   SCIP_Real*            operand1,           /**< first  vector as array of numbers */
   SCIP_Real*            operand2            /**< second vector as array of numbers */
   );

/** computes a lower bound on the scalar product of two vectors of numbers and stores result in resultant
 * assumes that numbers are not at +/- infinity */
extern
void SCIPintervalScalarProductRealsInf(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_Real*            resultant,          /**< resultant of operation */
   int                   length,             /**< length of vectors */
   SCIP_Real*            operand1,           /**< first  vector as array of numbers */
   SCIP_Real*            operand2            /**< second vector as array of numbers */
   );

/** computes the scalar product of two vectors of numbers and stores result in resultant
 * assumes that numbers are not at +/- infinity */
extern
void SCIPintervalScalarProductReals(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   int                   length,             /**< length of vectors */
   SCIP_Real*            operand1,           /**< first  vector as array of numbers */
   SCIP_Real*            operand2            /**< second vector as array of numbers */
   );

/** computes the scalar product of a vector of intervals and a vector of numbers and stores result in resultant
 * assumes that numbers are not at +/- infinity */
extern
void SCIPintervalScalarProductRealsIntervals(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   int                   length,             /**< length of vectors */
   SCIP_INTERVAL*        operand1,           /**< first  vector as array of intervals */
   SCIP_Real*            operand2            /**< second vector as array of numbers */
   );

/** computes the scalar product of two vectors of intervals and stores result in resultant */
extern
void SCIPintervalScalarProduct(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   int                   length,             /**< length of vectors */
   SCIP_INTERVAL*        operand1,           /**< first  vector as array of intervals */
   SCIP_INTERVAL*        operand2            /**< second vector as array of intervals */
   );

/** squares operand and stores result in resultant */
extern
void SCIPintervalSquare(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores (positive part of) square root of operand in resultant */
extern
void SCIPintervalSquareRoot(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores operand1 to the power of operand2 in resultant
 * 
 * uses SCIPintervalPowerScalar if operand2 is a scalar, otherwise computes exp(op2*log(op1)) */
extern
void SCIPintervalPower(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** stores operand1 to the power of the scalar operand2 in resultant */
extern
void SCIPintervalPowerScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** stores operand1 to the signed power of the scalar positive operand2 in resultant 
 * 
 * the signed power of x w.r.t. an exponent n >= 0 is given as sign(x) * abs(x)^n
 * */
extern
void SCIPintervalSignPowerScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_Real             operand2            /**< second operand of operation */
   );

/** computes the reciprocal of an interval
 *
 * if operand is 0.0, gives an empty interval as result */
extern
void SCIPintervalReciprocal(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores exponential of operand in resultant */
extern
void SCIPintervalExp(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores natural logarithm of operand in resultant */
extern
void SCIPintervalLog(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores minimum of operands in resultant */
extern
void SCIPintervalMin(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** stores maximum of operands in resultant */
extern
void SCIPintervalMax(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand1,           /**< first operand of operation */
   SCIP_INTERVAL         operand2            /**< second operand of operation */
   );

/** stores absolute value of operand in resultant */
extern
void SCIPintervalAbs(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** stores sign of operand in resultant */
extern
void SCIPintervalSign(
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_INTERVAL         operand             /**< operand of operation */
   );

/** computes exact upper bound on \f$ a x^2 + b x \f$ for x in [xlb, xub], b an interval, and a scalar
 * 
 * Uses Algorithm 2.2 from Domes and Neumaier: Constraint propagation on quadratic constraints (2008) */
extern
SCIP_Real SCIPintervalQuadUpperBound(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_Real             a,                  /**< coefficient of x^2 */
   SCIP_INTERVAL         b,                  /**< coefficient of x */
   SCIP_INTERVAL         xrng                /**< range of x */
   );

/** stores range of quadratic term in resultant
 * 
 * given scalar a and intervals b and x, computes interval for \f$ a x^2 + b x \f$ */
extern
void SCIPintervalQuad(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_Real             sqrcoeff,           /**< coefficient of x^2 */
   SCIP_INTERVAL         lincoeff,           /**< coefficient of x */
   SCIP_INTERVAL         xrng                /**< range of x */
   );


/** solves a quadratic equation with interval linear and constant coefficients
 * 
 * Given a scalar a and intervals b and c, this function computes an interval that contains all positive solutions of \f$ a x^2 + b x \geq c\f$. */
extern
void SCIPintervalSolveUnivariateQuadExpressionPositive(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_Real             sqrcoeff,           /**< coefficient of x^2 */
   SCIP_INTERVAL         lincoeff,           /**< coefficient of x */
   SCIP_INTERVAL         rhs                 /**< right hand side of equation */
);

/** solves a quadratic equation with linear and constant coefficients
 * 
 * Given scalar a, b, and c, this function computes an interval that contains all positive solutions of \f$ a x^2 + b x \geq c\f$.
 * Implements Algorithm 3.2 from Domes and Neumaier: Constraint propagation on quadratic constraints (2008). */
extern
void SCIPintervalSolveUnivariateQuadExpressionPositiveAllScalar(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_Real             sqrcoeff,           /**< coefficient of x^2 */
   SCIP_Real             lincoeff,           /**< coefficient of x */
   SCIP_Real             rhs                 /**< right hand side of equation */
);

/** solves a quadratic equation with interval linear and constant coefficients
 * 
 * Given a scalar a and intervals b and c, this function computes an interval that contains all solutions of \f$ a x^2 + b x \in c\f$ */
extern
void SCIPintervalSolveUnivariateQuadExpression(
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        resultant,          /**< resultant interval of operation */
   SCIP_Real             sqrcoeff,           /**< coefficient of x^2 */
   SCIP_INTERVAL         lincoeff,           /**< coefficient of x */
   SCIP_INTERVAL         rhs                 /**< right hand side of equation */
);

#ifdef __cplusplus
}
#endif

#endif
