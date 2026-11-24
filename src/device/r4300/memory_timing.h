#ifndef N64EMU_R4300_MEMORY_TIMING_H
#define N64EMU_R4300_MEMORY_TIMING_H

/* compat_issue_clock values; relaxed status retains Compatible's clocks. */
enum { R4300_ISSUE_ACCURATE = 0, R4300_ISSUE_COMPATIBLE = 1, R4300_ISSUE_RELAXED_FP_STATUS = 2 };

static inline unsigned r4300_fp_status_relaxed(unsigned profile)
{
#ifdef N64EMU_USF
    (void)profile;
    return 0;
#else
    return profile == R4300_ISSUE_RELAXED_FP_STATUS;
#endif
}

/* Only disabled inexact reporting may be omitted. Mixed exception flags and
 * enabled inexact traps retain the precise status/exception path. */
static inline unsigned r4300_fpu_omit_inexact_status(unsigned profile, unsigned fcr31, unsigned flags)
{
    return r4300_fp_status_relaxed(profile) && flags == 1 && !(fcr31 & 0x80);
}

/* CPU clocks charged in addition to instruction issue (two CPU clocks per
 * CP0 COUNT tick). C paths, native emitters and their event-budget bounds must
 * use these same values. VI surcharges are separate; shared RDRAM arbitration
 * and refresh latency distributions are not completely modeled.
 */
enum {
    /* Empirical shared-memory service estimate, including idle bus handoff.
     * Pending RDP work selects the contention estimate. Keep the maximum
     * as the native region/event bound. Compatibility and audio checks:
     * benchmarks/fast-timing-study/DK64_ACCURATE_REGRESSION.md.
     * This is an approximation of contention, not measured bus arbitration. */
    R4300_ICACHE_FILL_IDLE_CYCLES = 36,
    R4300_ICACHE_FILL_BUSY_CYCLES = 128,
    R4300_ICACHE_FILL_CYCLES = R4300_ICACHE_FILL_BUSY_CYCLES,
    R4300_DCACHE_FILL_CYCLES = 41,
    R4300_COMPAT_DCACHE_FILL_CYCLES = 40,
    R4300_DCACHE_WRITEBACK_CYCLES = 40,
    R4300_UNCACHED_FETCH_CYCLES = 40,
    R4300_UNCACHED_READ_CYCLES = 31,
    R4300_UNCACHED_DATA_CYCLES = 38,
    R4300_RCP_READ_CYCLES = 21,
    R4300_RCP_WRITE_CYCLES = 8,
    R4300_VI_LINE_CYCLES = 1,
    R4300_VI_BANK_CYCLES = 4
};

/* Off uses a fixed RAM service estimate without cache tags or interlocks.
 * USF rips require their legacy instruction clock, with no added RAM cost. */
#ifdef N64EMU_USF
enum { R4300_OFF_RAM_CYCLES = 0 };
#else
enum { R4300_OFF_RAM_CYCLES = 7 };
#endif

/* Only accurate CPU timing uses the cache and pipeline models. */
static inline unsigned r4300_profile_cache_enabled(unsigned accurate)
{
    return accurate != 0;
}

static inline unsigned r4300_off_ram_cycles(unsigned faster)
{
#ifdef N64EMU_USF
    (void)faster;
    return 0;
#else
    return faster ? 3 : R4300_OFF_RAM_CYCLES;
#endif
}

static inline unsigned r4300_fpu_compatibility_latency(unsigned cache_timing, unsigned compatibility)
{
#ifdef N64EMU_USF
    (void)cache_timing;
    return compatibility != 0;
#else
    /* Commercial profiles keep operand-dependent FP execution latencies. */
    (void)cache_timing;
    (void)compatibility;
    return 0;
#endif
}

#endif
