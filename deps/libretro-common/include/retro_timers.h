/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_COMMON_TIMERS_H
#define __LIBRETRO_COMMON_TIMERS_H

#include <stdint.h>

#if defined(XENON)
#include <time/time.h>
#elif !defined(__PSL1GHT__) && defined(__PS3__)
#include <sys/timer.h>
#elif defined(GEKKO) || defined(__PSL1GHT__) || defined(__QNX__)
#include <unistd.h>
#elif defined(WIIU)
#include <wiiu/os/thread.h>
#elif defined(PSP)
#include <pspthreadman.h>
#elif defined(VITA)
#include <psp2/kernel/threadmgr.h>
#elif defined(_3DS)
#include <3ds.h>
#else
#include <time.h>
#endif

#if defined(_WIN32) && !defined(_XBOX)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(_WIN32) && defined(_XBOX)
#include <Xtl.h>
#endif

#include <limits.h>

#ifdef _MSC_VER
#include <compat/msvc.h>
#endif
#include <retro_inline.h>

#ifdef DJGPP
#define timespec timeval
#define tv_nsec tv_usec
#include <unistd.h>

extern int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

static int nanosleepDOS(const struct timespec *rqtp, struct timespec *rmtp)
{
   usleep(1000000L * rqtp->tv_sec + rqtp->tv_nsec / 1000);

   if (rmtp)
      rmtp->tv_sec = rmtp->tv_nsec=0;

   return 0;
}

#define nanosleep nanosleepDOS
#endif

/**
 * retro_sleep:
 * @msec         : amount in milliseconds to sleep
 *
 * Sleeps for a specified amount of milliseconds (@msec).
 **/
#if defined(PSP) || defined(VITA)
#define retro_sleep(msec) (sceKernelDelayThread(1000 * (msec)))
#elif defined(_3DS)
#define retro_sleep(msec) (svcSleepThread(1000000 * (s64)(msec)))
#elif defined(__WINRT__) || defined(WINAPI_FAMILY) && WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
#define retro_sleep(msec) (SleepEx((msec), FALSE))
#elif defined(_WIN32)
#define retro_sleep(msec) (Sleep((msec)))
#elif defined(XENON)
#define retro_sleep(msec) (udelay(1000 * (msec)))
#elif !defined(__PSL1GHT__) && defined(__PS3__)
#define retro_sleep(msec) (sys_timer_usleep(1000 * (msec)))
#elif defined(GEKKO) || defined(__PSL1GHT__) || defined(__QNX__)
#define retro_sleep(msec) (usleep(1000 * (msec)))
#elif defined(WIIU)
#define retro_sleep(msec) (OSSleepTicks(ms_to_ticks((msec))))
#else
#define retro_sleep(msec) \
{ \
   struct timespec tv = {0}; \
   tv.tv_sec          = msec / 1000; \
   tv.tv_nsec         = (msec % 1000) * 1000000; \
   nanosleep(&tv, NULL); \
}
#endif

#endif
