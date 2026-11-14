/*
 *  Copyright  (C) 2010-2021 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../../LICENSE for the license terms.
 */
#ifndef __LIBRETRO_SDK_CONVERSION_S16_TO_FLOAT_H__
#define __LIBRETRO_SDK_CONVERSION_S16_TO_FLOAT_H__

#include <stdint.h>
#include <stddef.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/**
 * convert_s16_to_float:
 * @out               : output buffer
 * @in                : input buffer
 * @samples           : size of samples to be converted
 * @gain              : gain applied (.e.g. audio volume)
 *
 * Converts from signed integer 16-bit
 * to floating point.
 **/
void convert_s16_to_float(float *out,
      const int16_t *in, size_t samples, float gain);

/**
 * convert_s16_to_float_init_simd:
 *
 * Sets up function pointers for conversion
 * functions based on CPU features.
 **/
void convert_s16_to_float_init_simd(void);

RETRO_END_DECLS

#endif
