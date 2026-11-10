#ifndef N64EMU_R4300_MEMORY_TIMING_H
#define N64EMU_R4300_MEMORY_TIMING_H

/* CPU clocks charged in addition to instruction issue (two CPU clocks per
 * CP0 COUNT tick). C paths, native emitters and their event-budget bounds must
 * use these same values. VI surcharges are separate; shared RDRAM arbitration
 * and refresh latency distributions are not completely modeled.
 */
enum {
    /* Empirical shared-memory service estimate: a pending RDP deadline
     * selects the contended instruction-fill cost. Keep the maximum as the
     * native region/event bound; actual fills select the cost at runtime.
     * See benchmarks/hle-calibration/DEMO_REPAIR.md for joint DK64/demo checks.
     * This is an approximation of contention, not measured bus arbitration. */
    R4300_ICACHE_FILL_IDLE_CYCLES = 32,
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

#endif
