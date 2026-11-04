#ifndef SOFTRDP_WORKER_EVENTCOUNT_H
#define SOFTRDP_WORKER_EVENTCOUNT_H

#include <stdbool.h>
#include "worker_atomic.h"

/* Native parking for the upstream eventcount protocol. prepare must be paired
 * with exactly one cancel or commit after rechecking the caller's predicate.
 * notify releases all registered waiters; commit may wake spuriously.
 * Init/free are owned by the pool's caller, with no concurrent users. */
typedef struct retro_eventcount {
#if defined(_WIN32) && defined(RETRO_ATOMIC_HAS_PTR)
    retro_atomic_ptr_t waitlist;
#endif
    retro_atomic_int_t epoch;
    retro_atomic_int_t waiters;
} retro_eventcount_t;

bool retro_eventcount_init(retro_eventcount_t *ec);
void retro_eventcount_free(retro_eventcount_t *ec);
void retro_eventcount_notify(retro_eventcount_t *ec);
int retro_eventcount_prepare_wait(retro_eventcount_t *ec);
void retro_eventcount_cancel_wait(retro_eventcount_t *ec);
void retro_eventcount_commit_wait(retro_eventcount_t *ec, int key);
const char *retro_eventcount_backend_name(void);

#endif
