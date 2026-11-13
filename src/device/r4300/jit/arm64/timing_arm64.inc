/* Straight-line ALU fetches cannot exit. Charge their fixed clocks and
 * uncached penalties together, before any memory guard, branch or commit. */
static void emit_pending_clocks(const struct n64emujit_config* config,unsigned base,unsigned fetches,size_t fetch_flag=4)
{
    if(base)emit_addimm(CLOCKS,base,CLOCKS);
    if(!config->cache_timing || !fetches)return;
    if((config->virtual_pc&UINT32_C(0xe0000000))==UINT32_C(0xa0000000)) {
        emit_write_buffer_drain_native(config,base);
        emit_addimm(CLOCKS,R4300_UNCACHED_FETCH_CYCLES*fetches,CLOCKS);return;
    }
    emit_mem_imm(0xf7,0,0,SP,-1,0,fetch_flag,2);
    auto *cached=emit_jump(5);
    emit_write_buffer_drain_native(config,base);
    emit_addimm(CLOCKS,R4300_UNCACHED_FETCH_CYCLES*fetches,CLOCKS);patch_jump(cached,out);
}

/* Previously queued bus work can outlive the next region's fixed costs.
 * Reserve its latest deadline before admitting a native region. COUNT writes
 * do not rebase these monotonic CPU-clock deadlines. This only guards entry;
 * the existing fetch/data paths still charge the actual remaining wait. */
static void emit_pending_transfer_guard(const struct n64emujit_config* config,
    unsigned cached_bound, unsigned uncached_bound, struct memory_exit* failure)
{
    if (!config->cache_timing || config->compat_issue_clock) return;
    emit_movimm(0, T2);
    emit_mem(0x8b, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, write_buffer_count));
    emit_reg_op(0x85, T0, T0, 0); auto *no_writes = emit_jump(4);
    emit_addimm(T0, -1, T0);
    emit_mem(0x8b, 1, T2, STATE, T0, 3, offsetof(struct r4300_core, write_buffer_end));
    patch_jump(no_writes, out);
    emit_mem(0x8b, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, icache_fill_index));
    emit_reg_op(0x85, T0, T0, 0); auto *no_fill = emit_jump(4);
    emit_mem(0x8b, 1, T0, STATE, -1, 0, offsetof(struct r4300_core, icache_fill_end));
    emit_reg_op(0x39, T0, T2, 1); auto *writes_later = emit_jump(3);
    emit_mov64(T0, T2);
    patch_jump(writes_later, out); patch_jump(no_fill, out);
    emit_reg_op(0x85, T2, T2, 1); auto *empty = emit_jump(4);
    /* The next event is at 2*(count_clock-cycle_count), independent of the
     * writable COUNT register and of the current half-clock phase. */
    emit_mem(0x8b, 1, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.count_clock));
    emit_mem(0x8b, 1, T1, STATE, -1, 0, offsetof(struct r4300_core, cp0.cycle_count));
    emit_reg_op(0x29, T1, T0, 1); emit_shift_reg(T0, 4, 1, 1);
    emit_addimm64(T0, -int(cached_bound), T0);
    if (cached_bound != uncached_bound) {
        emit_mem_imm(0xf7, 0, 0, SP, -1, 0, 4, 2);
        auto *cached = emit_jump(5);
        emit_addimm64(T0, int(cached_bound) - int(uncached_bound), T0);
        patch_jump(cached, out);
    }
    emit_reg_op(0x39, T0, T2, 1); memory_guard(failure, 3);
    patch_jump(empty, out);
}

static void emit_region_guard(const struct n64emujit_config* config, unsigned start,
    unsigned count, unsigned base, unsigned memory_count, struct memory_exit* failure)
{
    emit_movimm64((intptr_t)&breakloop, ADDRESS);
    emit_mem_imm(0x80, 0, 7, ADDRESS, -1, 0, 0,0); memory_guard(failure, 5);
    emit_mem_imm(0x81, 0, 7, STATE, -1, 0, offsetof(struct r4300_core, skip_jump),0); memory_guard(failure, 5);
    static_assert(sizeof(unsigned)==4 && offsetof(struct r4300_core,compat_issue_clock)==offsetof(struct r4300_core,cache_timing)+4,"adjacent timing mode words");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    emit_movimm64((uint64_t(config->cache_timing)<<32)|uint64_t(config->compat_issue_clock),T0);
#else
    emit_movimm64(uint64_t(config->cache_timing)|(uint64_t(config->compat_issue_clock)<<32),T0);
#endif
    emit_mem(0x39,1,T0,STATE,-1,0,offsetof(struct r4300_core,cache_timing));memory_guard(failure,5);
    emit_mem(0x8b, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_STATUS_REG]));
    // KSU=kernel is the common case, with or without an active exception.
    JIT(tst(WReg(T0),CP0_STATUS_MODE_MASK));unsigned char* kernel=emit_jump(4);
    JIT(tst(WReg(T0),CP0_STATUS_EXL|CP0_STATUS_ERL));memory_guard(failure,4);
    patch_jump(kernel, out);
    if (config->cache_timing) {
        uint32_t pc = config->virtual_pc + start * 4;
        if ((pc & UINT32_C(0xe0000000)) == UINT32_C(0x80000000)) {
            emit_mem(0x8b, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_CONFIG_REG]));
            emit_imm_op(4, T0, 7, 0); emit_imm_op(7, T0, 2, 0);
            emit_set_condition(T0,5);
        } else if ((pc & UINT32_C(0xe0000000)) == UINT32_C(0xa0000000)) emit_movimm(0, T0);
        else emit_code_cacheability(config,pc,STATE,T0,T1,T2,ADDRESS);
        emit_shift_reg(T0, 4, 1, 0);
        emit_mem(0x89, 0, T0, SP, -1, 0, 4);
    }
    /* Bound emitted native paths, rather than charging a hypothetical cache
     * miss to every instruction. Each new I-cache line can miss once; uncached
     * fetches cost 40 clocks each. Native data accesses either hit D-cache or
     * use the <=38-clock RAM bus path. Refills/devices/traps leave through a
     * precise side exit and recheck the remaining region before resuming.
     * Only the first fetch can refill the instruction micro-TLB. Reserve one
     * interlock/compatibility clock per instruction, plus one for restored
     * simultaneous LDI/DCB history at entry. */
    unsigned compatibility = !config->cache_timing ? count * r4300_issue_reservation(config->compat_issue_clock) : 0;
    unsigned ram = config->cache_timing ? 0 : memory_count * R4300_OFF_RAM_CYCLES;
    unsigned cached_bound = base + compatibility + ram, uncached_bound = base + compatibility + ram;
    if (config->cache_timing && count) {
        uint32_t pc = config->virtual_pc + start * 4;
        unsigned lines = ((pc + (count - 1) * 4) >> 5) - (pc >> 5) + 1;
        unsigned common = base + count + memory_count * R4300_UNCACHED_DATA_CYCLES + 1 +
            ((pc & UINT32_C(0xc0000000)) != UINT32_C(0x80000000) ? 3 : 0);
        cached_bound = common + lines * R4300_ICACHE_FILL_CYCLES;
        uncached_bound = common + count * R4300_UNCACHED_FETCH_CYCLES;
    }
    /* Rounding up covers either half-clock phase without reading it. An odd
     * bound may fall back one COUNT tick early; precise execution still
     * delivers the original instruction/event boundary. */
    unsigned char *uncached = NULL, *done = NULL;
    if (config->cache_timing && cached_bound != uncached_bound) {
        emit_mem_imm(0xf7, 0, 0, SP, -1, 0, 4,2);
        uncached = emit_jump(4);
    }
    emit_mem_imm(0x81, 1, 7, STATE, -1, 0, offsetof(struct r4300_core, cp0.cycle_count),-(int32_t)((cached_bound + 1) >> 1)); memory_guard(failure, 13);
    if (uncached) {
        done = emit_b();
        patch_jump(uncached, out);
        emit_mem_imm(0x81, 1, 7, STATE, -1, 0, offsetof(struct r4300_core, cp0.cycle_count),-(int32_t)((uncached_bound + 1) >> 1)); memory_guard(failure, 13);
        patch_jump(done, out);
    }
    emit_pending_transfer_guard(config, cached_bound, uncached_bound, failure);
}
static void emit_region_commit_native(const struct n64emujit_config* config,
    unsigned start, unsigned count, int branch)
{
    emit_mem(0x8b, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.count_phase));
    emit_reg_op(0x01, CLOCKS, T0, 0); emit_mov(T0, T2); emit_imm_op(4, T2, 1, 0);
    emit_mem(0x89, 0, T2, STATE, -1, 0, offsetof(struct r4300_core, cp0.count_phase));
    emit_shift_reg(T0, 5, 1, 0);
    emit_mem(0x01, 0, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_COUNT_REG]));
    emit_mem(0x01, 1, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.cycle_count));
    emit_mem(0x01, 1, T0, STATE, -1, 0, offsetof(struct r4300_core, cp0.count_clock));
    emit_state_imm(gpr_offset(0), 0, 1);
    if (branch) {
        emit_mem(0x89, 0, TARGET, STATE, -1, 0, offsetof(struct r4300_core, instruction_pc.addr));
        emit_mem(0x89, 0, TARGET, STATE, -1, 0, offsetof(struct r4300_core, cp0.last_addr));
    } else emit_state_imm(offsetof(struct r4300_core, instruction_pc.addr), config->virtual_pc + (start + count) * 4, 0);
    emit_mem(0x8b, 0, T0, SP, -1, 0, 8);
    emit_movimm(count, T2); emit_reg_op(0x29, T0, T2, 0);
    uint64_t* counters[2] = {config->commits, config->fast_commits};
    uintptr_t counter_base = 0;
    for (unsigned n = 0; n < 2; ++n) if (counters[n]) {
        int64_t offset = (int64_t)(uintptr_t)counters[n] - (int64_t)counter_base;
        if (!counter_base || offset < INT32_MIN || offset > INT32_MAX) {
            counter_base = (uintptr_t)counters[n]; offset = 0;
            emit_movimm64((intptr_t)counter_base, ADDRESS);
        }
        emit_mem(0x01, 1, T2, ADDRESS, -1, 0, (size_t)offset);
    }
}
