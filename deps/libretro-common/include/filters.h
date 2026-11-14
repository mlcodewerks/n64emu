/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_SDK_FILTERS_H
#define _LIBRETRO_SDK_FILTERS_H

/* for MSVC; should be benign under any circumstances */
#define _USE_MATH_DEFINES

#include <stdlib.h>
#include <math.h>
#include <retro_inline.h>
#include <retro_math.h>

static INLINE double sinc(double val)
{
   if (fabs(val) < 0.00001)
      return 1.0;
   return sin(val) / val;
}

/* Paeth prediction filter. */
static INLINE int paeth(int a, int b, int c)
{
   int p  = a + b - c;
   int pa = abs(p - a);
   int pb = abs(p - b);
   int pc = abs(p - c);

   if (pa <= pb && pa <= pc)
      return a;
   else if (pb <= pc)
      return b;
   return c;
}

/* Modified Bessel function of first order.
 * Check Wiki for mathematical definition ... */
static INLINE double besseli0(double x)
{
   unsigned i;
   double sum            = 0.0;
   double factorial      = 1.0;
   double factorial_mult = 0.0;
   double x_pow          = 1.0;
   double two_div_pow    = 1.0;
   double x_sqr          = x * x;

   /* Approximate. This is an infinite sum.
    * Luckily, it converges rather fast. */
   for (i = 0; i < 18; i++)
   {
      sum += x_pow * two_div_pow / (factorial * factorial);

      factorial_mult += 1.0;
      x_pow *= x_sqr;
      two_div_pow *= 0.25;
      factorial *= factorial_mult;
   }

   return sum;
}

static INLINE double kaiser_window_function(double index, double beta)
{
   return besseli0(beta * sqrtf(1 - index * index));
}

static INLINE double lanzcos_window_function(double index)
{
   return sinc(M_PI * index);
}

#endif
