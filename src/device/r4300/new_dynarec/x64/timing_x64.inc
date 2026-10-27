/* Straight-line ALU fetches cannot exit. Charge their fixed clocks and
 * uncached penalties together, before any memory guard, branch or commit. */
static void emit_pending_clocks(const struct ari64_config* config,unsigned base,unsigned fetches,size_t fetch_flag=4)
{
    if(base)emit_addimm(R13,base,R13);
    if(!config->cache_timing || !fetches)return;
    if((config->virtual_pc&UINT32_C(0xe0000000))==UINT32_C(0xa0000000)) {
        emit_addimm(R13,40*fetches,R13);return;
    }
    emit_mem_imm(0xf7,0,0,RSP,-1,0,fetch_flag,2);
    auto *cached=emit_jump(5);
    emit_addimm(R13,40*fetches,R13);patch_jump(cached,out);
}

static void emit_region_guard(const struct ari64_config* config, unsigned start,
    unsigned count, unsigned base, unsigned memory_count, struct memory_exit* failure)
{
    emit_movimm64((intptr_t)&breakloop, R11);
    emit_mem_imm(0x80, 0, 7, R11, -1, 0, 0,0); memory_guard(failure, 5);
    emit_mem_imm(0x81, 0, 7, R12, -1, 0, offsetof(struct r4300_core, skip_jump),0); memory_guard(failure, 5);
    static_assert(sizeof(unsigned)==4 && offsetof(struct r4300_core,compat_issue_clock)==offsetof(struct r4300_core,cache_timing)+4,"adjacent timing mode words");
    emit_movimm64(uint64_t(config->cache_timing)|(uint64_t(config->compat_issue_clock)<<32),RAX);
    emit_mem(0x39,1,RAX,R12,-1,0,offsetof(struct r4300_core,cache_timing));memory_guard(failure,5);
    emit_mem(0x8b, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_STATUS_REG]));
    // KSU=kernel is the common case, with or without an active exception.
    JIT(test(Xbyak::Reg32(RAX),CP0_STATUS_MODE_MASK));unsigned char* kernel=emit_jump(4);
    JIT(test(Xbyak::Reg32(RAX),CP0_STATUS_EXL|CP0_STATUS_ERL));memory_guard(failure,4);
    patch_jump(kernel, out);
    if (config->cache_timing) {
        uint32_t pc = config->virtual_pc + start * 4;
        if ((pc & UINT32_C(0xe0000000)) == UINT32_C(0x80000000)) {
            emit_mem(0x8b, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_CONFIG_REG]));
            emit_imm_op(4, RAX, 7, 0); emit_imm_op(7, RAX, 2, 0);
            emit_set_condition(RAX,5);
        } else if ((pc & UINT32_C(0xe0000000)) == UINT32_C(0xa0000000)) emit_movimm(0, RAX);
        else emit_code_cacheability(config,pc,R12,RAX,RCX,RDX,R11);
        emit_shift_reg(RAX, 4, 1, 0);
        emit_mem(0x89, 0, RAX, RSP, -1, 0, 4);
    }
    /* Bound emitted native paths, rather than charging a hypothetical cache
     * miss to every instruction. Each new I-cache line can miss once; uncached
     * fetches cost 40 clocks each. Native data accesses either hit D-cache or
     * use the <=38-clock RAM bus path. Refills/devices/traps leave through a
     * precise side exit and recheck the remaining region before resuming.
     * Only the first fetch can refill the instruction micro-TLB. Reserve one
     * interlock/compatibility clock per instruction, plus one for restored
     * simultaneous LDI/DCB history at entry. */
    unsigned compatibility = !config->cache_timing && config->compat_issue_clock ? count : 0;
    unsigned cached_bound = base + compatibility, uncached_bound = base + compatibility;
    if (config->cache_timing && count) {
        uint32_t pc = config->virtual_pc + start * 4;
        unsigned lines = ((pc + (count - 1) * 4) >> 5) - (pc >> 5) + 1;
        unsigned common = base + count + memory_count * 38 + 1 +
            ((pc & UINT32_C(0xc0000000)) != UINT32_C(0x80000000) ? 3 : 0);
        cached_bound = common + lines * 48;
        uncached_bound = common + count * 40;
    }
    /* Rounding up covers either half-clock phase without reading it. An odd
     * bound may fall back one COUNT tick early; precise execution still
     * delivers the original instruction/event boundary. */
    unsigned char *uncached = NULL, *done = NULL;
    if (config->cache_timing && cached_bound != uncached_bound) {
        emit_mem_imm(0xf7, 0, 0, RSP, -1, 0, 4,2);
        uncached = emit_jump(4);
    }
    emit_mem_imm(0x81, 1, 7, R12, -1, 0, offsetof(struct r4300_core, cp0.cycle_count),-(int32_t)((cached_bound + 1) >> 1)); memory_guard(failure, 13);
    if (uncached) {
        done = emit_b();
        patch_jump(uncached, out);
        emit_mem_imm(0x81, 1, 7, R12, -1, 0, offsetof(struct r4300_core, cp0.cycle_count),-(int32_t)((uncached_bound + 1) >> 1)); memory_guard(failure, 13);
        patch_jump(done, out);
    }
}
static void emit_region_commit_native(const struct ari64_config* config,
    unsigned start, unsigned count, int branch)
{
    emit_mem(0x8b, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.count_phase));
    emit_reg_op(0x01, R13, RAX, 0); emit_mov(RAX, RDX); emit_imm_op(4, RDX, 1, 0);
    emit_mem(0x89, 0, RDX, R12, -1, 0, offsetof(struct r4300_core, cp0.count_phase));
    emit_shift_reg(RAX, 5, 1, 0);
    emit_mem(0x01, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_COUNT_REG]));
    emit_mem(0x01, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.cycle_count));
    emit_mem(0x01, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.count_clock));
    emit_state_imm(gpr_offset(0), 0, 1);
    if (branch) {
        emit_mem(0x89, 0, R14, R12, -1, 0, offsetof(struct r4300_core, interp_PC.addr));
        emit_mem(0x89, 0, R14, R12, -1, 0, offsetof(struct r4300_core, cp0.last_addr));
    } else emit_state_imm(offsetof(struct r4300_core, interp_PC.addr), config->virtual_pc + (start + count) * 4, 0);
    emit_mem(0x8b, 0, RAX, RSP, -1, 0, 8);
    emit_movimm(count, RDX); emit_reg_op(0x29, RAX, RDX, 0);
    uint64_t* counters[2] = {config->commits, config->fast_commits};
    uintptr_t counter_base = 0;
    for (unsigned n = 0; n < 2; ++n) if (counters[n]) {
        int64_t offset = (int64_t)(uintptr_t)counters[n] - (int64_t)counter_base;
        if (!counter_base || offset < INT32_MIN || offset > INT32_MAX) {
            counter_base = (uintptr_t)counters[n]; offset = 0;
            emit_movimm64((intptr_t)counter_base, R11);
        }
        emit_mem(0x01, 1, RDX, R11, -1, 0, (size_t)offset);
    }
}
