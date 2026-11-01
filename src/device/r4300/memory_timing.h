#ifndef N64EMU_R4300_MEMORY_TIMING_H
#define N64EMU_R4300_MEMORY_TIMING_H

/* CPU clocks charged in addition to instruction issue (two CPU clocks per
 * CP0 COUNT tick). C paths, native emitters and their event-budget bounds must
 * use these same values. VI surcharges are separate; shared RDRAM arbitration
 * and refresh latency distributions are not completely modeled.
 */
enum {
    /* Retained shared estimate from the DK64 80/96-clock sweep: 96 completes
     * the tested crossing in both cache modes with HLE or native RSP. The
     * selected Nemu groups pass at either value and do not independently
     * measure instruction-fill latency. See benchmarks/dk64-timing/
     * MEMORY_INTERLOCKS.md and benchmarks/cpu-optimizer/ACCURATE_FB.md. */
    R4300_ICACHE_FILL_CYCLES = 96,
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

#endif
