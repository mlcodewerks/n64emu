/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_RTIME_H__
#define __LIBRETRO_SDK_RTIME_H__

#include <retro_common_api.h>

#include <stdint.h>
#include <stddef.h>
#include <time.h>

RETRO_BEGIN_DECLS

/* TODO/FIXME: Move all generic time handling functions
 * to this file */

/* Must be called before using rtime_localtime() */
void rtime_init(void);

/* Must be called upon program termination */
void rtime_deinit(void);

/* Thread-safe wrapper for localtime() */
struct tm *rtime_localtime(const time_t *timep, struct tm *result);

RETRO_END_DECLS

#endif
