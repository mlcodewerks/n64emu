/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifdef HAVE_THREADS
#include <rthreads/rthreads.h>
#include <retro_assert.h>
#include <stdlib.h>
#endif

#include <string.h>
#include <time/rtime.h>

#ifdef HAVE_THREADS
/* TODO/FIXME - global */
slock_t *rtime_localtime_lock = NULL;
#endif

/* Must be called before using rtime_localtime() */
void rtime_init(void)
{
   rtime_deinit();
#ifdef HAVE_THREADS
   if (!rtime_localtime_lock)
      rtime_localtime_lock = slock_new();

   retro_assert(rtime_localtime_lock);
#endif
}

/* Must be called upon program termination */
void rtime_deinit(void)
{
#ifdef HAVE_THREADS
   if (rtime_localtime_lock)
   {
      slock_free(rtime_localtime_lock);
      rtime_localtime_lock = NULL;
   }
#endif
}

/* Thread-safe wrapper for localtime() */
struct tm *rtime_localtime(const time_t *timep, struct tm *result)
{
   struct tm *time_info = NULL;

   /* Lock mutex */
#ifdef HAVE_THREADS
   slock_lock(rtime_localtime_lock);
#endif

   time_info = localtime(timep);
   if (time_info)
      memcpy(result, time_info, sizeof(struct tm));

   /* Unlock mutex */
#ifdef HAVE_THREADS
   slock_unlock(rtime_localtime_lock);
#endif

   return result;
}
