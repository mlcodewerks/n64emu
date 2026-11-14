/* Run native/precise memory, branch and event-boundary vectors in both
 * simplified timing profiles. Reuse the Accurate differential harness. */
static unsigned off_profile = 1;
#define CPU_CACHE_CHECKS_OFF_PROFILE off_profile
#define CPU_CACHE_CHECKS_MAIN profile_checks_main
#include "../benchmarks/accurate-perf/cpu_cache_checks.c"
int main(void)
{
    for (off_profile = 1; off_profile <= 2; ++off_profile)
        if (profile_checks_main()) return 1;
    return 0;
}
