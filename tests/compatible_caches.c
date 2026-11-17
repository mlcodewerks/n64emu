#include "headless.h"
#define CPU_CACHE_CHECKS_COMPAT_CLOCK 1
#define CPU_CACHE_CHECKS_MAIN compatible_cache_checks_main
#include "../benchmarks/accurate-perf/cpu_cache_checks.c"
int main(void)
{
    unit_headless();
    return compatible_cache_checks_main();
}
