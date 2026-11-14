/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_SDK_CPU_INFO_H
#define _LIBRETRO_SDK_CPU_INFO_H

#include <retro_common_api.h>

#include <stdint.h>

#include <libretro.h>

RETRO_BEGIN_DECLS

/**
 * cpu_features_get_perf_counter:
 *
 * Gets performance counter.
 *
 * Returns: performance counter.
 **/
retro_perf_tick_t cpu_features_get_perf_counter(void);

/**
 * cpu_features_get_time_usec:
 *
 * Gets time in microseconds, from an undefined epoch.
 * The epoch may change between computers or across reboots.
 *
 * Returns: time in microseconds
 **/
retro_time_t cpu_features_get_time_usec(void);

/**
 * cpu_features_get:
 *
 * Gets CPU features.
 *
 * Returns: bitmask of all CPU features available.
 **/
uint64_t cpu_features_get(void);

/**
 * cpu_features_get_core_amount:
 *
 * Gets the amount of available CPU cores.
 *
 * Returns: amount of CPU cores available.
 **/
unsigned cpu_features_get_core_amount(void);

void cpu_features_get_model_name(char *name, int len);

RETRO_END_DECLS

#endif
