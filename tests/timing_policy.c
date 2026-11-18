/* Keep commercial-game Off estimates out of USF's compatibility clock. */
#include "device/r4300/timing.h"
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
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
#ifdef N64EMU_USF
        assert(r4300_profile_cache_enabled(cycle) == cycle);
        assert(r4300_cached_issue_reservation(cycle, 0) == 3);
        assert(r4300_cached_issue_reservation(cycle, 1) == 3);
#else
        assert(r4300_profile_cache_enabled(cycle) == 1);
        assert(r4300_cached_issue_reservation(cycle, 0) == cycle);
        assert(r4300_cached_issue_reservation(cycle, 1) == 0);
#endif
    }
    for (unsigned detailed = 0; detailed < 2; ++detailed)
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility) {
#ifdef N64EMU_USF
            unsigned expected = compatibility;
#else
            unsigned expected = 0;
#endif
            assert(r4300_fpu_compatibility_latency(detailed, compatibility) == expected);
        }
    puts("Timing policy: RAM cost, cache selection and all four FP policy combinations passed.");
#ifndef N64EMU_USF
    assert(r4300_fpu_compatibility_latency(1,R4300_ISSUE_RELAXED_FP_STATUS)==0);
    assert(r4300_fpu_compatibility_latency(0,R4300_ISSUE_RELAXED_FP_STATUS)==0);
    assert(r4300_cached_issue_reservation(R4300_ISSUE_RELAXED_FP_STATUS,1)==0);
#endif
    for(unsigned profile=0;profile<3;++profile)
        for(unsigned enabled=0;enabled<32;++enabled)
            for(unsigned flags=0;flags<64;++flags) {
#ifdef N64EMU_USF
                unsigned omit=0;
#else
                unsigned omit=profile==R4300_ISSUE_RELAXED_FP_STATUS && flags==1 && !(enabled&1);
#endif
                assert(r4300_fpu_omit_inexact_status(profile,enabled<<7,flags)==omit);
            }
    return 0;
}
