
#ifndef N64EMU_CLOCK_H
#define N64EMU_CLOCK_H

#include <time.h>

struct clock_backend_interface
{
    /* Returns the current time
     */
    time_t (*get_time)(void* clock);
};

#ifdef __cplusplus
extern "C" {
#endif

/* Uses host time plus an optional time_t offset supplied as the context. */
extern const struct clock_backend_interface g_iclock_ctime_plus_delta;

#ifdef __cplusplus
}
#endif

#endif
