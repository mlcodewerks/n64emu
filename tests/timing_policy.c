/* Keep commercial-game Off estimates out of USF's compatibility clock. */
#include "device/r4300/memory_timing.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
#ifdef N64EMU_USF
    assert(R4300_OFF_RAM_CYCLES == 0);
    assert(r4300_off_ram_cycles(0) == 0 && r4300_off_ram_cycles(1) == 0);
#else
    assert(R4300_OFF_RAM_CYCLES == 7);
    assert(r4300_off_ram_cycles(0) == 7 && r4300_off_ram_cycles(1) == 3);
#endif
    for (unsigned cycle = 0; cycle < 2; ++cycle)
        for (unsigned tagged = 0; tagged < 2; ++tagged) {
#ifdef N64EMU_USF
            assert(r4300_use_fast_timing(cycle, tagged) == 0);
#else
            assert(r4300_use_fast_timing(cycle, tagged) == (!cycle && !tagged));
#endif
        }
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
