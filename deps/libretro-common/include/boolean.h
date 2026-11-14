/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_BOOLEAN_H
#define __LIBRETRO_SDK_BOOLEAN_H

#ifndef __cplusplus

#if defined(_MSC_VER) && _MSC_VER < 1800 && !defined(SN_TARGET_PS3)
/* Hack applied for MSVC when compiling in C89 mode as it isn't C99 compliant. */
#define bool unsigned char
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#endif

#endif
