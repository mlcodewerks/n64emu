/* Admission for shared-clock integer regions. Accurate mode uses the exact
 * fetch/pipeline functions before executing the generated register arithmetic.
 * No instruction in an admitted run can trap, access data, or change mappings. */
static int ci_alu_region_admitted(struct r4300_core* r, uint32_t pc,
    unsigned count, unsigned base)
{
    uint32_t status=r->cp0.regs[CP0_STATUS_REG];
    if(r->cache_timing>1 || breakloop || r->skip_jump || r->stop ||
       r->prefetch_valid || r->irq_writeback_pending || r->delay_slot || r->pc_extension ||
       ((status&CP0_STATUS_MODE_MASK) && !(status&(CP0_STATUS_EXL|CP0_STATUS_ERL))))return 0;
    const unsigned shift = cp0_cpu_clock_shift(&r->cp0);
    const unsigned tick_mask = (1u << shift) - 1;
    if (!r->cache_timing) {
        unsigned bound = base + count * r4300_cached_issue_reservation(r->compat_issue_clock,0);
        return r->cp0.cycle_count < -(int64_t)((bound + tick_mask) >> shift);
    }
    unsigned cached=r4300_access_cached(r,pc);
    unsigned lines=((pc+(count-1)*4)>>5)-(pc>>5)+1;
    unsigned common=base+count+1+((pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000)?3:0);
    unsigned bound = common + r4300_bus_cycles(r, cached ?
        lines * R4300_ICACHE_FILL_CYCLES : count * R4300_UNCACHED_FETCH_CYCLES);
    if (r->cp0.cycle_count >= -(int64_t)((bound + tick_mask) >> shift)) return 0;
    if(!r->compat_issue_clock && (r->write_buffer_count || r->icache_fill_index)) {
        uint64_t transfer=r->write_buffer_count?r->write_buffer_end[r->write_buffer_count-1]:0;
        if(r->icache_fill_index && r->icache_fill_end>transfer)transfer=r->icache_fill_end;
        uint64_t deadline = (((uint64_t)r->cp0.count_clock - (uint64_t)r->cp0.cycle_count) << shift) - bound;
        if(transfer && transfer>=deadline)return 0;
    }
    return 1;
}
