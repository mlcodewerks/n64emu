
#include "core/clock.h"

#include <time.h>


static time_t ctime_plus_delta_get_time(void* clock)
{
    time_t user_delta = (clock == NULL)
        ? 0
        : *(time_t*)clock;

    return user_delta + time(NULL);
}

const struct clock_backend_interface g_iclock_ctime_plus_delta =
{
    ctime_plus_delta_get_time
};
