/* Cached Compatible must retain precise FP issue, fetch and exception state. */
#include "headless.h"
#define CPU_CACHE_CHECKS_COMPAT_CLOCK 1
#define FP_MIXED_CHECKS_MAIN compatible_fpu_mixed_checks_main
#include "../benchmarks/softrdp/fp_mixed_checks.c"
int main(void)
{
    unit_headless();
    return compatible_fpu_mixed_checks_main();
}
