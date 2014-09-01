/* $Id: user_ad.hpp 2506 2012-10-24 19:36:49Z bradbell $ */
# ifndef CPPAD_USER_AD_INCLUDED
# define CPPAD_USER_AD_INCLUDED

/* --------------------------------------------------------------------------
CppAD: C++ Algorithmic Differentiation: Copyright (C) 2003-12 Bradley M. Bell

CppAD is distributed under multiple licenses. This distribution is under
the terms of the 
                    Eclipse Public License Version 1.0.

A copy of this license is included in the COPYING file of this distribution.
Please visit http://www.coin-or.org/CppAD/ for information on other licenses.
-------------------------------------------------------------------------- */
/*
---------------------------------------------------------------------------

$begin AD$$
$spell
	std
	bool
	cos
	Cpp
$$

$section AD Objects$$

$index AD, object$$

$head Purpose$$
The sections listed below describe the operations 
that are available to $cref/AD of Base/glossary/AD of Base/$$ objects.
These objects are used to $cref/tape/glossary/Tape/$$
an AD of $icode Base$$
$cref/operation sequence/glossary/Operation/Sequence/$$.
This operation sequence can
be transferred to an $cref ADFun$$ object where it
can be used to evaluate the corresponding 
function and derivative values.

$head Base Type Requirements$$
$index Base, require$$
The $icode Base$$ requirements are provided by the CppAD package 
for the following base types:
$code float$$, 
$code double$$,
$code std::complex<float>$$, 
$code std::complex<double>$$.
Otherwise, see $cref base_require$$.


$childtable%
	cppad/local/ad_ctor.hpp%
	cppad/local/ad_assign.hpp%
	cppad/local/convert.hpp%
	cppad/local/ad_valued.hpp%
	cppad/local/bool_valued.hpp%
	cppad/local/vec_ad.hpp%
	cppad/base_require.hpp
%$$

$end
---------------------------------------------------------------------------
*/

# include <cppad/local/ad_ctor.hpp>
# include <cppad/local/ad_assign.hpp>
# include <cppad/local/convert.hpp>
# include <cppad/local/vec_ad.hpp>
# include <cppad/local/ad_valued.hpp>
# include <cppad/local/bool_valued.hpp>

# endif
