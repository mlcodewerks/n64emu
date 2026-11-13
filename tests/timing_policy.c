/* Keep commercial-game Off estimates out of USF's compatibility clock. */
#include "device/r4300/memory_timing.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
#ifdef N64EMU_USF
    assert(R4300_OFF_RAM_CYCLES == 0);
#else
    assert(R4300_OFF_RAM_CYCLES == 7);
#endif
    for (unsigned detailed = 0; detailed < 2; ++detailed)
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility) {
#ifdef N64EMU_USF
            unsigned expected = compatibility;
#else
            unsigned expected = detailed && compatibility;
#endif
            assert(r4300_fpu_compatibility_latency(detailed, compatibility) == expected);
        }
    puts("Timing policy: RAM cost and all four FP policy combinations passed.");
    return 0;
}
