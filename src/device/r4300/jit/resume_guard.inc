/* Cold Accurate-mode admission checks share host code. The hot region keeps
 * its specialized inline guard; no guest clock or cache state is changed here. */
struct resume_admission {
    uint32_t pc, cached_bound, uncached_bound, memory_flags, compatibility;
    struct n64emujit_fetch_cache* fetch_cache;
    const uint64_t* fetch_generation;
};
static resume_admission make_resume_admission(const n64emujit_config* config,
    unsigned start,unsigned count,unsigned base,unsigned memory_count,unsigned flags)
{
    const unsigned pc=config->virtual_pc+start*4;
    const unsigned lines=((pc+(count-1)*4)>>5)-(pc>>5)+1;
    const unsigned common=base+count+memory_count*R4300_UNCACHED_DATA_CYCLES+1+
        ((pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000)?3:0);
    return {pc,common+lines*R4300_ICACHE_FILL_CYCLES,common+count*R4300_UNCACHED_FETCH_CYCLES,
        flags,config->compat_issue_clock,config->fetch_cache,config->fetch_generation};
}
static unsigned resume_region_admitted(struct r4300_core* r, const resume_admission* a)
{
    if (breakloop || r->skip_jump || r->cache_timing != 1 ||
        r->compat_issue_clock != a->compatibility) return 0;
    uint32_t status = r->cp0.regs[CP0_STATUS_REG];
    if ((status & CP0_STATUS_MODE_MASK) && !(status & (CP0_STATUS_EXL | CP0_STATUS_ERL))) return 0;
    if ((a->memory_flags & 1) && (r->mi->regs[MI_INIT_MODE_REG] & 0x80)) return 0;
    if ((a->memory_flags & 2) && !(status & CP0_STATUS_CU1)) return 0;
    unsigned cached;
    if ((a->pc & UINT32_C(0xc0000000)) == UINT32_C(0x80000000)) cached = r4300_access_cached(r,a->pc);
    else {
        const uint32_t tag = UINT32_C(0x80000000) | ((a->pc >> 12) << 8) | (r->cp0.regs[CP0_ENTRYHI_REG] & 255);
        auto* slot = a->fetch_cache;
        if (slot && a->fetch_generation && slot->tag == tag && slot->generation == *a->fetch_generation)
            cached = slot->cached;
        else {
            cached = r4300_access_cached(r,a->pc);
            if (slot && a->fetch_generation) {
                slot->cached = cached; slot->tag = tag; slot->generation = *a->fetch_generation;
            }
        }
    }
    unsigned bound = cached ? a->cached_bound : a->uncached_bound;
    if (r->cp0.cycle_count >= -(int64_t)((bound + 1) >> 1)) return 0;
    if (!a->compatibility && (r->write_buffer_count || r->icache_fill_index)) {
        uint64_t transfer = r->write_buffer_count ? r->write_buffer_end[r->write_buffer_count - 1] : 0;
        if (r->icache_fill_index && r->icache_fill_end > transfer) transfer = r->icache_fill_end;
        const uint64_t deadline = 2 * ((uint64_t)r->cp0.count_clock - (uint64_t)r->cp0.cycle_count) - bound;
        if (transfer && transfer >= deadline) return 0;
    }
    return 1 | (cached ? 2 : 0);
}

struct hit_admission { resume_admission base; uint32_t key, lines; };
static unsigned hit_region_admitted(struct r4300_core* r, const hit_admission* d)
{
    /* This is reached only after pessimistic admission fails. Within this
     * page, native ALU/RAM operations cannot evict covered I-cache lines.
     * Pending fills/writes still reserve their live completion deadlines. */
    resume_admission tight=d->base;
    unsigned retained=0;
    for(unsigned line=0;line<d->lines;++line)
        if(r->icache_tags[((tight.pc>>5)+line)&511]==d->key) {
            /* A branch's target prefetch can wait for a different sequential
             * fill started inside this region. Keep one fill reservation
             * even when every covered instruction line is already resident. */
            if(retained)tight.cached_bound-=R4300_ICACHE_FILL_CYCLES;
            else retained=1;
        }
    return resume_region_admitted(r,&tight);
}
