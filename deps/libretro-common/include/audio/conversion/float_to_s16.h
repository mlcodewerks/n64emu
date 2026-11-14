/*
 *  Copyright  (C) 2010-2021 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_CONVERSION_FLOAT_TO_S16_H__
#define __LIBRETRO_SDK_CONVERSION_FLOAT_TO_S16_H__

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

#include <stdint.h>
#include <stddef.h>

/**
 * convert_float_to_s16:
 * @out               : output buffer
 * @in                : input buffer
 * @samples           : size of samples to be converted
 *
 * Converts floating point
 * to signed integer 16-bit.
 **/
void convert_float_to_s16(int16_t *out,
      const float *in, size_t samples);

/**
 * convert_float_to_s16_init_simd:
 *
 * Sets up function pointers for conversion
 * functions based on CPU features.
 **/
void convert_float_to_s16_init_simd(void);

RETRO_END_DECLS

#endif
