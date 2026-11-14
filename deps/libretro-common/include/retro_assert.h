/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef __RETRO_ASSERT_H
#define __RETRO_ASSERT_H

#include <assert.h>

#ifdef RARCH_INTERNAL
#include <stdio.h>
#define retro_assert(cond) ((void)( (cond) || (printf("Assertion failed at %s:%d.\n", __FILE__, __LINE__), abort(), 0) ))
#else
#define retro_assert(cond) assert(cond)
#endif

#endif
