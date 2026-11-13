





static int native_ram_opcode(enum r4300_opcode op)
{
    switch (op) {
    case R4300_OP_LB: case R4300_OP_LBU: case R4300_OP_LH: case R4300_OP_LHU:
    case R4300_OP_LW: case R4300_OP_LWU: case R4300_OP_LD:
    case R4300_OP_SB: case R4300_OP_SH: case R4300_OP_SW: case R4300_OP_SD:
    case R4300_OP_LWC1: case R4300_OP_LDC1: case R4300_OP_SWC1: case R4300_OP_SDC1: return 1;
    default: return 0;
    }
}
struct memory_exit {
    struct regcache_state registers;
    struct regcache_state resume_registers;
    unsigned char* resume;
    unsigned char* region_end;
    unsigned char* fault_pc;
    unsigned index, start, end, guard_count;
    unsigned char* guards[16];
};
static void memory_guard(struct memory_exit* exit, unsigned condition)
{
    assert(exit->guard_count < 16);
    exit->guards[exit->guard_count++] = emit_jump(condition);
}
static void emit_write_buffer_drain_native(const struct n64emujit_config* config,unsigned unissued=0)
{
    if(config->compat_issue_clock)return;
    emit_mem_imm(0x83,0,7,R12,-1,0,offsetof(struct r4300_core,write_buffer_count),0);
    auto *empty=emit_jump(4);
    JIT(push(Xbyak::Reg64(RAX)));JIT(push(Xbyak::Reg64(RDX)));
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,write_buffer_count));
    emit_addimm(RAX,-1,RAX);
    emit_mem(0x8b,1,RDX,R12,RAX,3,offsetof(struct r4300_core,write_buffer_end));
    emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_clock));
    emit_shift_reg(RAX,4,1,1);emit_reg_op(0x29,RAX,RDX,1);
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x29,RAX,RDX,1);emit_reg_op(0x29,R13,RDX,1);
    if(unissued)emit_addimm64(RDX,unissued,RDX);
    emit_reg_op(0x85,RDX,RDX,1);auto *retired=emit_jump(14);
    emit_reg_op(0x01,RDX,R13,1);patch_jump(retired,out);
    emit_state_imm(offsetof(struct r4300_core,write_buffer_count),0,0);
    JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RAX)));patch_jump(empty,out);
}
static void emit_icache_fill_wait_native(const struct n64emujit_config* config,unsigned unissued=0,unsigned index=UINT32_MAX)
{
    if(config->compat_issue_clock)return;
    emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,icache_fill_index),index==UINT32_MAX?0:index+1);
    auto *skip=emit_jump(index==UINT32_MAX?4:5);
    JIT(push(Xbyak::Reg64(RAX)));JIT(push(Xbyak::Reg64(RDX)));JIT(push(Xbyak::Reg64(RCX)));
    emit_mem(0x8b,1,RDX,R12,-1,0,offsetof(struct r4300_core,icache_fill_end));
    emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_clock));
    emit_shift_reg(RAX,4,1,1);emit_reg_op(0x29,RAX,RDX,1);
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x29,RAX,RDX,1);emit_reg_op(0x29,R13,RDX,1);
    if(unissued)emit_addimm64(RDX,unissued,RDX);
    emit_reg_op(0x85,RDX,RDX,1);auto *ready=emit_jump(14);
    emit_reg_op(0x01,RDX,R13,1);patch_jump(ready,out);
    emit_state_imm(offsetof(struct r4300_core,icache_fill_index),0,0);
    JIT(pop(Xbyak::Reg64(RCX)));JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RAX)));patch_jump(skip,out);
}
static void emit_icache_fill_cycles(unsigned dest,unsigned unissued)
{
 JIT(push(Xbyak::Reg64(RCX)));JIT(push(Xbyak::Reg64(RDX)));JIT(push(Xbyak::Reg64(R10)));JIT(push(Xbyak::Reg64(R11)));
 emit_movimm(R4300_ICACHE_FILL_IDLE_CYCLES,RDX);
 emit_mem(0x8b,1,RCX,R12,-1,0,offsetof(struct r4300_core,vi));
 emit_reg_op(0x85,RCX,RCX,1);auto *no_vi=emit_jump(4);
 emit_mem(0x8b,1,RCX,RCX,-1,0,offsetof(struct vi_controller,dp));
 emit_reg_op(0x85,RCX,RCX,1);auto *no_dp=emit_jump(4);
 emit_mem(0x8b,1,R10,RCX,-1,0,offsetof(struct rdp_core,timing)+offsetof(struct rdp_timing,deadline));
 // Native blocks defer COUNT updates. Sample the same instruction boundary
 // as the precise path, excluding this instruction's unissued base cycles.
 emit_mem(0x8b,0,R11,R12,-1,0,offsetof(struct r4300_core,cp0.count_phase));
 emit_reg_op(0x01,R13,R11,1);
 if(unissued)emit_addimm64(R11,-int(unissued),R11);
 emit_shift_reg(R11,5,1,1);
 emit_mem(0x8b,1,RCX,R12,-1,0,offsetof(struct r4300_core,cp0.count_clock));
 emit_reg_op(0x01,RCX,R11,1);
 emit_reg_op(0x39,R11,R10,1);auto *idle=emit_jump(14);emit_movimm(R4300_ICACHE_FILL_BUSY_CYCLES,RDX);
 patch_jump(idle,out);patch_jump(no_dp,out);patch_jump(no_vi,out);
 emit_reg_op(0x01,RDX,dest,dest==RAX);
 JIT(pop(Xbyak::Reg64(R11)));JIT(pop(Xbyak::Reg64(R10)));JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RCX)));
}
static void emit_icache_prefetch_native(const struct n64emujit_config* config,unsigned unissued,unsigned index)
{
    JIT(push(Xbyak::Reg64(RAX)));JIT(push(Xbyak::Reg64(RDX)));JIT(push(Xbyak::Reg64(RCX)));
    emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.count_clock));
    emit_shift_reg(RAX,4,1,1);
    emit_mem(0x8b,0,RDX,R12,-1,0,offsetof(struct r4300_core,cp0.count_phase));
    emit_reg_op(0x01,RDX,RAX,1);emit_reg_op(0x01,R13,RAX,1);
    if(unissued)emit_addimm64(RAX,-int(unissued),RAX);
    emit_mem(0x8b,0,RCX,R12,-1,0,offsetof(struct r4300_core,write_buffer_count));
    emit_reg_op(0x85,RCX,RCX,0);auto *empty=emit_jump(4);
    emit_addimm(RCX,-1,RCX);
    emit_mem(0x8b,1,RDX,R12,RCX,3,offsetof(struct r4300_core,write_buffer_end));
    emit_reg_op(0x39,RDX,RAX,1);auto *later=emit_jump(3);
    emit_mov64(RDX,RAX);patch_jump(later,out);patch_jump(empty,out);
    emit_icache_fill_cycles(RAX,unissued);emit_mem(0x89,1,RAX,R12,-1,0,offsetof(struct r4300_core,icache_fill_end));
    if(index==UINT32_MAX){
        emit_mov(R14,RDX);emit_shift_reg(RDX,5,5,0);emit_imm_op(4,RDX,511,0);emit_addimm(RDX,1,RDX);
        emit_mem(0x89,0,RDX,R12,-1,0,offsetof(struct r4300_core,icache_fill_index));
    }else emit_state_imm(offsetof(struct r4300_core,icache_fill_index),index+1,0);
    JIT(pop(Xbyak::Reg64(RCX)));JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RAX)));
}
/* Native regions can batch resident instruction translations. A refill can
 * overlap an older cache/bus transfer; let precise fetch establish its actual
 * order before another region resumes. This guard reads state only. */
static unsigned char* emit_itlb_miss_guard(uint32_t pc)
{
    if((pc&UINT32_C(0xc0000000))==UINT32_C(0x80000000))return nullptr;
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.tlb.LUT_r)+(pc>>12)*4);
    emit_imm_op(4,RAX,UINT32_C(0xfffff000),0);
    unsigned char* hits[2];
    for(unsigned slot=0;slot<2;++slot) {
        unsigned char* miss[3];
        emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_valid),1u<<slot);
        miss[0]=emit_jump(4);
        emit_mem(0x39,0,RAX,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_map)+slot*4);
        miss[1]=emit_jump(5);
        emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_vpn)+slot*4,pc>>12);
        miss[2]=emit_jump(5);hits[slot]=emit_b();
        for(auto* branch:miss)patch_jump(branch,out);
    }
    auto* miss=emit_b();
    for(auto* branch:hits)patch_jump(branch,out);
    return miss;
}
static void emit_itlb_native(uint32_t pc)
{
    uint32_t vpn = pc >> 12;
    emit_mem(0x8b, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.tlb.LUT_r) + vpn * 4);
    emit_imm_op(4, RAX, UINT32_C(0xfffff000), 0);
    unsigned char* hits[2], *slot_misses[3];
    for (unsigned slot = 0; slot < 2; ++slot) {
        emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_valid),1u << slot);
        slot_misses[0] = emit_jump(4);
        emit_mem(0x39, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_map) + slot * 4);
        slot_misses[1] = emit_jump(5);
        emit_mem_imm(0x81, 0, 7, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_vpn) + slot * 4,vpn);
        slot_misses[2] = emit_jump(5);
        emit_state_imm(offsetof(struct r4300_core, pipeline_itlb_lru), slot ^ 1, 0);
        hits[slot] = emit_b();
        for (unsigned n = 0; n < 3; ++n) patch_jump(slot_misses[n], out);
    }
    emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_valid),1);
    unsigned char* empty_zero = emit_jump(4);
    emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_valid),2);
    unsigned char* empty_one = emit_jump(4);
    emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_lru),1);
    unsigned char* lru_zero = emit_jump(4);
    unsigned char* fills[2];
    for (int slot = 1; slot >= 0; --slot) {
        if (slot) patch_jump(empty_one, out);
        else { patch_jump(empty_zero, out); patch_jump(lru_zero, out); }
        emit_mem(0x89, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_map) + slot * 4);
        emit_state_imm(offsetof(struct r4300_core, pipeline_itlb_vpn) + slot * 4, vpn, 0);
        emit_mem_imm(0x81, 0, 1, R12, -1, 0, offsetof(struct r4300_core, pipeline_itlb_valid),1u << slot);
        emit_state_imm(offsetof(struct r4300_core, pipeline_itlb_lru), slot ^ 1, 0);
        fills[slot] = emit_b();
    }
    patch_jump(fills[0], out); patch_jump(fills[1], out);
    emit_addimm(R13, 3, R13);
    patch_jump(hits[0], out); patch_jump(hits[1], out);
}
/* Resolve a same-page target's IF access before executing its delay slot.
 * R14 holds the selected successor for conditional and indirect branches. */
static void emit_branch_prefetch(const struct n64emujit_config* config,unsigned char** cold_exit=nullptr,
    uint32_t instruction=0,uint32_t pc=0)
{
    if(!config->cache_timing||config->compat_issue_clock)return;
    unsigned major=instruction>>26;
    bool fixed=false;
    uint32_t target=0;
    if(major==2 || major==3) {
        target=((pc+4)&UINT32_C(0xf0000000))|((instruction&UINT32_C(0x03ffffff))<<2);
        fixed=true;
    } else if(major==1 || (major>=4 && major<=7) || (major>=20 && major<=23)) {
        target=pc+4+int32_t(int16_t(instruction))*4;
        // Both outcomes fetch the identical line, including its page/tag.
        fixed=(target>>5)==((pc+8)>>5);
    }
    if(fixed && (target>>12)!=(config->virtual_pc>>12))return;
    // The branch's own fetch has already made this line resident. Native
    // instructions cannot evict it, and the hit path has no other effects.
    if(fixed && (target>>5)==(pc>>5))return;
    emit_mem_imm(0xf7,0,0,RSP,-1,0,4,2);auto *uncached=emit_jump(4);
    // R11 is reserved scratch: the overwhelmingly common hit needs no saves.
    unsigned char *different_page=nullptr;
    if(!fixed) {
        emit_mov(R14,R11);emit_imm_op(4,R11,UINT32_C(0xfffff000),0);
        emit_imm_op(7,R11,config->virtual_pc&UINT32_C(0xfffff000),0);different_page=emit_jump(5);
        emit_mov(R14,R11);emit_shift_reg(R11,5,5,0);emit_imm_op(4,R11,511,0);
    }
    uint32_t key=(config->physical_page<<12)|1;
    size_t tag=offsetof(struct r4300_core,icache_tags)+(fixed?((target>>5)&511)*4:0);
    emit_mem_imm(0x81,0,7,R12,fixed?-1:R11,2,tag,key);auto *hit=emit_jump(4);
    if(cold_exit) {
        // A rewind must leave the target fetch to the precise branch.
        *cold_exit=emit_b();
        patch_jump(hit,out);if(different_page)patch_jump(different_page,out);patch_jump(uncached,out);
        return;
    }
    JIT(push(Xbyak::Reg64(RAX)));JIT(push(Xbyak::Reg64(RCX)));JIT(push(Xbyak::Reg64(RDX)));
    if(fixed)emit_movimm((target>>5)&511,RCX);else emit_mov(R11,RCX);
    emit_icache_fill_wait_native(config);
    emit_mem(0x8b,0,RAX,R12,RCX,2,offsetof(struct r4300_core,icache_shadow_tags));
    emit_reg_op(0x85,RAX,RAX,0);auto *plain=emit_jump(4);
    emit_mem(0x8b,1,RDX,R12,-1,0,offsetof(struct r4300_core,code_page_generation));
    emit_reg_op(0x85,RDX,RDX,1);auto *no_generation=emit_jump(4);
    emit_imm_op(4,RAX,0x1ffff000,0);emit_shift_reg(RAX,5,12,0);
    emit_mem_imm(0x81,0,0,RDX,RAX,2,0,1);
    patch_jump(no_generation,out);
    emit_mem_imm(0xc7,0,0,R12,RCX,2,offsetof(struct r4300_core,icache_shadow_tags),0);
    patch_jump(plain,out);
    emit_mem_imm(0xc7,0,0,R12,RCX,2,offsetof(struct r4300_core,icache_tags),key);
    emit_icache_prefetch_native(config,0,fixed?(target>>5)&511:UINT32_MAX);
    JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RCX)));JIT(pop(Xbyak::Reg64(RAX)));
    patch_jump(hit,out);if(different_page)patch_jump(different_page,out);patch_jump(uncached,out);
}
/* Fetch state changes happen only after the current instruction's guards.
 * A region stays on one code page, so its first fetch establishes the exact
 * two-entry instruction micro-TLB hit/replacement state for later fetches. */
static void emit_fetch_native(const struct n64emujit_config* config, unsigned index, unsigned start, unsigned base, int check_line, bool defer_base=false,size_t fetch_flag=4)
{
    unsigned cycles = base + r4300_issue_reservation(config->compat_issue_clock);
    if(!defer_base)emit_addimm(R13, cycles, R13);
    if (!config->cache_timing) return;
    uint32_t pc = config->virtual_pc + index * 4;
    if ((pc & UINT32_C(0xc0000000)) != UINT32_C(0x80000000) && index == start) emit_itlb_native(pc);
    bool lookahead=!config->compat_issue_clock&&(pc&31)>=24&&(pc&0xfff)<0xff8;
    if(defer_base && !check_line&&!lookahead)return;
    emit_mem_imm(0xf7, 0, 0, RSP, -1, 0, fetch_flag,2);
    unsigned char* uncached = emit_jump(4);
    for(unsigned future=0;future<2;++future) {
        if(future?!lookahead:!check_line)continue;
        uint32_t fetch_pc=pc+(future?8:0);
        if(!future)emit_icache_fill_wait_native(config,defer_base?0:cycles,(fetch_pc>>5)&511);
        size_t tag = offsetof(struct r4300_core, icache_tags) + ((fetch_pc >> 5) & 511) * 4;
        uint32_t key = (config->physical_page << 12) | 1;
        emit_mem_imm(0x81, 0, 7, R12, -1, 0, tag,key);
        unsigned char* hit = emit_jump(4);
        emit_icache_fill_wait_native(config,defer_base?0:cycles);
        if(!future||config->compat_issue_clock)emit_write_buffer_drain_native(config,defer_base?0:cycles);
        size_t shadow=offsetof(struct r4300_core,icache_shadow_tags)+((fetch_pc>>5)&511)*4;
        emit_mem_imm(0x81,0,7,R12,-1,0,shadow,0);
        unsigned char *plain=emit_jump(4);
        JIT(push(Xbyak::Reg64(RAX)));JIT(push(Xbyak::Reg64(RDX)));
        emit_mem(0x8b,1,RDX,R12,-1,0,offsetof(struct r4300_core,code_page_generation));
        emit_reg_op(0x85,RDX,RDX,1);unsigned char *no_generation=emit_jump(4);
        emit_mem(0x8b,0,RAX,R12,-1,0,shadow);
        emit_imm_op(4,RAX,0x1ffff000,0);emit_shift_reg(RAX,5,12,0);
        emit_mem(0x8d,1,RDX,RDX,RAX,2,0);
        emit_mem(0x8b,0,RAX,RDX,-1,0,0);emit_addimm(RAX,1,RAX);
        emit_mem(0x89,0,RAX,RDX,-1,0,0);
        patch_jump(no_generation,out);emit_state_imm(shadow,0,0);
        JIT(pop(Xbyak::Reg64(RDX)));JIT(pop(Xbyak::Reg64(RAX)));
        patch_jump(plain,out);
        emit_state_imm(tag, key, 0);
        if(future&&!config->compat_issue_clock)emit_icache_prefetch_native(config,defer_base?0:cycles,(fetch_pc>>5)&511);
        else emit_icache_fill_cycles(R13,defer_base?0:cycles);
        patch_jump(hit, out);
    }
    if(defer_base){
        if(config->compat_issue_clock)patch_jump(uncached,out);
        else {auto *done=emit_b();patch_jump(uncached,out);emit_icache_fill_wait_native(config);patch_jump(done,out);}
        return;
    }
    unsigned char* done = emit_b();
    patch_jump(uncached, out);
    emit_icache_fill_wait_native(config,cycles);
    emit_write_buffer_drain_native(config,cycles);
    emit_addimm(R13, R4300_UNCACHED_FETCH_CYCLES, R13);
    patch_jump(done, out);
}
static void emit_pipeline_clear(void)
{
    static_assert(offsetof(struct r4300_core,pipeline_fpu_result_mask)==offsetof(struct r4300_core,pipeline_load_mask)+8 &&
        offsetof(struct r4300_core,pipeline_fpu_cc)==offsetof(struct r4300_core,pipeline_load_mask)+16 &&
        offsetof(struct r4300_core,pipeline_cached_store)==offsetof(struct r4300_core,pipeline_load_mask)+20,"packed pipeline history");
    JIT(pxor(Xbyak::Xmm(0),Xbyak::Xmm(0)));
    JIT(movdqu(Xbyak::util::ptr[Xbyak::Reg64(R12)+offsetof(struct r4300_core,pipeline_load_mask)],Xbyak::Xmm(0)));
    JIT(movq(Xbyak::util::ptr[Xbyak::Reg64(R12)+offsetof(struct r4300_core,pipeline_fpu_cc)],Xbyak::Xmm(0)));
}
static bool deferred_pipeline(const struct n64emujit_config* config)
{
    return config->cache_timing == 1 && !r4300_issue_reservation(config->compat_issue_clock);
}
static bool native_store(uint32_t instruction)
{
    unsigned major = instruction >> 26;
    return major == 0x28 || major == 0x29 || major == 0x2b ||
        major == 0x3f || major == 0x39 || major == 0x3d;
}
static unsigned native_memory_flags(uint32_t instruction)
{
    if (!native_ram_opcode(r4300_get_idec(instruction)->opcode)) return 0;
    unsigned major = instruction >> 26;
    return unsigned(native_store(instruction)) |
        ((major == 0x31 || major == 0x35 || major == 0x39 || major == 0x3d) ? 2 : 0);
}
/* Within a native region, LDI uses the decoded predecessor. Store cacheability
 * remains live because a resumed precise store may have been uncached.
 * Materialize history only where C or a successor region can observe it.
 * This also runs in cold exits before committing their executed prefix. */
static void emit_pipeline_history(uint32_t instruction)
{
    if (native_store(instruction)) {
        emit_state_imm(offsetof(struct r4300_core,pipeline_load_mask),0,1);
        emit_state_imm(offsetof(struct r4300_core,pipeline_fpu_result_mask),0,1);
        emit_state_imm(offsetof(struct r4300_core,pipeline_fpu_cc),0,0);
        return;
    }
    emit_pipeline_clear();
    if (fp_delay_zero_opcode(instruction)) {
        unsigned fd=(instruction>>6)&31;
        emit_movimm64(UINT64_C(1)<<(32+(fd&~1u)),RAX);
        if(fd&1) {
            emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
            auto *paired=emit_jump(4);
            emit_movimm64(UINT64_C(1)<<(32+fd),RAX);patch_jump(paired,out);
        }
        emit_mem(0x89,1,RAX,R12,-1,0,offsetof(r4300_core,pipeline_fpu_result_mask));
        return;
    }
    if (!native_ram_opcode(r4300_get_idec(instruction)->opcode)) return;
    unsigned rt = (instruction >> 16) & 31;
    unsigned major = instruction >> 26;
    bool fp = major == 0x31 || major == 0x35;
    if (!rt && !fp) return;
    emit_movimm64(UINT64_C(1) << (rt + (fp ? 32 : 0)), RAX);
    if (fp && (rt & 1)) {
        emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
        auto *fr = emit_jump(5);
        emit_movimm64(UINT64_C(1) << (32 + (rt & ~1u)),RAX);
        patch_jump(fr,out);
    }
    emit_mem(0x89,1,RAX,R12,-1,0,offsetof(struct r4300_core,pipeline_load_mask));
}
/* Native timing cannot absorb LDI in a compatibility issue reservation. Test
 * raw fields just like the hardware's deliberately imprecise detector. */
static void emit_ldi_native(uint32_t instruction, int fp, uint32_t previous, int known)
{
    if (known) {
        unsigned major = previous >> 26, load = (previous >> 16) & 31;
        if ((major >= 0x20 && major <= 0x25) || major == 0x27 || major == 0x37 ||
            (major==0x10 && ((previous>>21)&31)<=1)) {
            if (load && (r4300_pipeline_ldi_gprs(instruction)&(UINT32_C(1)<<load)))
                emit_addimm(R13, 1, R13);
        } else if (fp && (major == 0x31 || major == 0x35)) {
            unsigned consumer = (instruction >> 16) & 31;
            if (load == consumer) emit_addimm(R13, 1, R13);
            else if ((load & ~1u) == (consumer & ~1u)) {
                emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
                unsigned char* fr = emit_jump(5);
                emit_addimm(R13, 1, R13); patch_jump(fr, out);
            }
        }
        return;
    }
    unsigned rt = (instruction >> 16) & 31;
    uint32_t candidates = r4300_pipeline_ldi_gprs(instruction);
    if (!fp && !candidates) return;
    emit_mem(0x8b, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, pipeline_load_mask));
    unsigned char* fpr_history = NULL;
    if (fp) {
        // Restored history can contain both banks. The shared detector gives
        // FPR history priority, rather than treating an integer base match as
        // an additional FP dependency in that case.
        emit_mov64(RAX,RDX);emit_shift_reg(RDX,5,32,1);
        emit_reg_op(0x85,RDX,RDX,0);auto *fpr=emit_jump(5);
        emit_imm_op(4,RAX,candidates,0);auto *selected=emit_b();
        patch_jump(fpr,out);
        emit_movimm64(UINT64_C(1) << (32 + rt), RDX);
        if (rt & 1) {
            emit_mem_imm(0xf7, 0, 0, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
            unsigned char* fr = emit_jump(5);
            emit_movimm64(UINT64_C(1) << (32 + (rt & ~1u)), RDX); patch_jump(fr, out);
        }
        emit_reg_op(0x21, RDX, RAX, 1);patch_jump(selected,out);
    } else {
        emit_mov64(RAX, RDX); emit_shift_reg(RDX, 5, 32, 1);
        emit_reg_op(0x85, RDX, RDX, 0); fpr_history = emit_jump(5);
        emit_imm_op(4, RAX, candidates, 0);
    }
    emit_reg_op(0x85, RAX, RAX, fp ? 1 : 0);
    unsigned char* no_load = emit_jump(4);
    emit_addimm(R13, 1, R13);
    patch_jump(no_load, out);
    if (fpr_history) patch_jump(fpr_history, out);
}
/* Match the authoritative TLB-entry scan, including overlapping entries,
 * global mappings, ASID and cache mode. Read it live rather than caching an
 * answer that could survive a same-PFN rewrite with different attributes. */
static void emit_mapped_cacheability(void)
{
    emit_mem(0x89, 0, RDX, RSP, -1, 0, 0);
    emit_mem(0x8d, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.tlb.entries));
    emit_mem(0x8b, 0, RCX, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_ENTRYHI_REG]));
    emit_imm_op(4, RCX, 255, 0); emit_movimm(32, RDX);
    unsigned char* loop = out;
    emit_mem_imm(0x80, 0, 7, RAX, -1, 0, offsetof(struct tlb_entry, g),0);
    unsigned char* global = emit_jump(5);
    emit_mem(0x38, 0, RCX, RAX, -1, 0, offsetof(struct tlb_entry, asid));
    unsigned char* other_asid = emit_jump(5);
    patch_jump(global, out);
    emit_mem_imm(0x80, 0, 7, RAX, -1, 0, offsetof(struct tlb_entry, v_even),0);
    unsigned char* invalid_even = emit_jump(4);
    emit_mem(0x3b, 0, R11, RAX, -1, 0, offsetof(struct tlb_entry, start_even));
    unsigned char* before_even = emit_jump(2);
    emit_mem(0x3b, 0, R11, RAX, -1, 0, offsetof(struct tlb_entry, end_even));
    unsigned char* after_even = emit_jump(7);
    emit_mem_imm(0x80, 0, 7, RAX, -1, 0, offsetof(struct tlb_entry, c_even),2);
    unsigned char* even_uncached = emit_jump(4);
    unsigned char* even_cached = emit_b();
    patch_jump(invalid_even, out); patch_jump(before_even, out); patch_jump(after_even, out);
    emit_mem_imm(0x80, 0, 7, RAX, -1, 0, offsetof(struct tlb_entry, v_odd),0);
    unsigned char* invalid_odd = emit_jump(4);
    emit_mem(0x3b, 0, R11, RAX, -1, 0, offsetof(struct tlb_entry, start_odd));
    unsigned char* before_odd = emit_jump(2);
    emit_mem(0x3b, 0, R11, RAX, -1, 0, offsetof(struct tlb_entry, end_odd));
    unsigned char* after_odd = emit_jump(7);
    emit_mem_imm(0x80, 0, 7, RAX, -1, 0, offsetof(struct tlb_entry, c_odd),2);
    unsigned char* odd_uncached = emit_jump(4);
    unsigned char* odd_cached = emit_b();
    patch_jump(other_asid, out); patch_jump(invalid_odd, out); patch_jump(before_odd, out); patch_jump(after_odd, out);
    emit_addimm64(RAX, sizeof(struct tlb_entry), RAX); emit_addimm(RDX, -1, RDX);
    unsigned char* again = emit_jump(5); patch_jump(again, loop);
    patch_jump(even_uncached, out); patch_jump(odd_uncached, out);
    emit_movimm(0, RAX);
    unsigned char* done = emit_b();
    patch_jump(even_cached, out); patch_jump(odd_cached, out); emit_movimm(1, RAX);
    patch_jump(done, out);
    emit_mem(0x8b, 0, RDX, RSP, -1, 0, 0);
}
#include "../fetch_cache.inc"
static void emit_data_cache_slot(const n64emujit_config* config)
{
    emit_mov(R11,RAX);emit_shift_reg(RAX,5,12,0);emit_imm_op(4,RAX,63,0);emit_shift_reg(RAX,4,4,0);
    emit_movimm64((intptr_t)config->data_cache,RCX);emit_reg_op(0x01,RCX,RAX,1);
}
static void emit_data_cache_tag(void)
{
    emit_mov(R11,RCX);emit_shift_reg(RCX,5,12,0);emit_shift_reg(RCX,4,8,0);
    emit_imm_op(1,RCX,UINT32_C(0x80000000),0);
    emit_mem(0x8b,0,RDX,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_ENTRYHI_REG]));
    emit_imm_op(4,RDX,255,0);emit_reg_op(0x09,RDX,RCX,0);
}
static void emit_data_cacheability(const n64emujit_config* config)
{
    if(!config->data_cache || !config->fetch_generation){emit_mapped_cacheability();return;}
    emit_mem(0x89,0,RDX,RSP,-1,0,0);
    emit_data_cache_slot(config);emit_data_cache_tag();
    emit_mem(0x39,0,RCX,RAX,-1,0,offsetof(n64emujit_fetch_cache,tag));
    auto* tag_miss=emit_jump(5);
    emit_movimm64((intptr_t)config->fetch_generation,RCX);emit_mem(0x8b,1,RCX,RCX,-1,0,0);
    emit_mem(0x39,1,RCX,RAX,-1,0,offsetof(n64emujit_fetch_cache,generation));
    auto* generation_miss=emit_jump(5);
    emit_mem(0x8b,0,RAX,RAX,-1,0,offsetof(n64emujit_fetch_cache,cached));
    emit_mem(0x8b,0,RDX,RSP,-1,0,0);auto* done=emit_b();
    patch_jump(tag_miss,out);patch_jump(generation_miss,out);
    /* A metadata miss fills attributes only. Keep the guest instruction on
     * the existing native/precise data path, with no extra guest replay. */
    emit_mem(0x8b,0,RDX,RSP,-1,0,0);
    const int saved[]={RCX,RDX,R8,R9,R10,R11
#ifndef _WIN32
        ,RSI,RDI
#endif
    };
    for(int reg:saved)JIT(push(Xbyak::Reg64(reg)));
#ifdef _WIN32
    JIT(sub(Xbyak::Reg64(RSP),32));
#endif
    emit_mov64(R12,ARG1_REG);emit_mov(R11,ARG2_REG);
    emit_helper_call((const void*)r4300_jit_mapped_cached);
#ifdef _WIN32
    JIT(add(Xbyak::Reg64(RSP),32));
#endif
    for(unsigned n=sizeof(saved)/sizeof(saved[0]);n--;)JIT(pop(Xbyak::Reg64(saved[n])));
    patch_jump(done,out);
}
static void emit_memory_native(uint32_t instruction, unsigned index,
    const struct n64emujit_config* config, struct memory_exit* exit, int check_line,
    uint32_t previous, int known)
{
    enum r4300_opcode op = r4300_get_idec(instruction)->opcode;
    int fp = op == R4300_OP_LWC1 || op == R4300_OP_LDC1 || op == R4300_OP_SWC1 || op == R4300_OP_SDC1;
    unsigned rs = (instruction >> 21) & 31, rt = (instruction >> 16) & 31;
    unsigned size = op == R4300_OP_LD || op == R4300_OP_SD || op == R4300_OP_LDC1 || op == R4300_OP_SDC1 ? 8 :
        op == R4300_OP_LW || op == R4300_OP_LWU || op == R4300_OP_SW || fp ? 4 :
        op == R4300_OP_LH || op == R4300_OP_LHU || op == R4300_OP_SH ? 2 : 1;
    int write = op == R4300_OP_SB || op == R4300_OP_SH || op == R4300_OP_SW || op == R4300_OP_SD || op == R4300_OP_SWC1 || op == R4300_OP_SDC1;
    uint64_t base_constant=0;bool constant_address=known_value(rs,base_constant);
    uint32_t address=uint32_t(base_constant)+int32_t(int16_t(instruction));
    const bool fixed_direct=constant_address && (address&UINT32_C(0xc0000000))==UINT32_C(0x80000000);
    const uint32_t physical=address&UINT32_C(0x1ffffffc);
    if(constant_address)emit_movimm(address,R11);
    else {
    emit_state_load(gpr_offset(rs), R11);
    emit_addimm64(R11, (int16_t)instruction, RAX);
    emit_addimm(R11, (int16_t)instruction, R11);
    }
    exit->registers = regcache;
    // Preserve extended addresses and reject malformed compatibility pointers.
    if (constant_address) {
        uint64_t effective = base_constant + uint64_t(int64_t(int16_t(instruction)));
        if (effective != uint64_t(int64_t(int32_t(effective)))) {
            assert(exit->guard_count < 16);
            exit->guards[exit->guard_count++] = emit_b();
        }
    } else {
        JIT(movsxd(Xbyak::Reg64(RDX), Xbyak::Reg32(R11)));
        emit_reg_op(0x39, RDX, RAX, 1);
        memory_guard(exit, 5);
    }
    if (size > 1 && (!constant_address || (address & (size-1)))) {
        JIT(test(Xbyak::Reg32(R11),size-1)); memory_guard(exit, 5);
    }
    unsigned char *direct_cache_hit = nullptr;
    if (config->cache_timing == 1 && (!write || !config->fastmem) &&
        (!constant_address || (fixed_direct && !(address & UINT32_C(0x20000000))))) {
        // A real D-cache hit uses its resident bytes, even if backing RAM is
        // GPU-owned or its handler has changed. KSEG0 needs no TLB/probe. Keep
        // the full address, alignment, CU1 and MI guards above this shortcut.
        // Tracked-code stores still need the shared invalidator; all misses,
        // mapped accesses retain the guarded memory path.
        unsigned char *misses[4]; unsigned misses_count = 0;
        if (!constant_address) {
            emit_mov(R11,RAX);emit_imm_op(4,RAX,UINT32_C(0xe0000000),0);
            emit_imm_op(7,RAX,UINT32_C(0x80000000),0);
            misses[misses_count++] = emit_jump(5);
        }
        if ((config->virtual_pc & UINT32_C(0xe0000000)) == UINT32_C(0x80000000))
            emit_mem_imm(0xf7,0,0,RSP,-1,0,4,2);
        else {
            emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_CONFIG_REG]));
            emit_imm_op(4,RAX,7,0);emit_imm_op(7,RAX,2,0);
        }
        misses[misses_count++] = emit_jump(4);
        emit_mov(R11,RCX);emit_shift_reg(RCX,5,4,0);emit_imm_op(4,RCX,511,0);
        emit_mem(0x8b,0,RAX,R12,RCX,2,offsetof(struct r4300_core,dcache_tags));
        emit_imm_op(4,RAX,~UINT32_C(2),0);
        emit_mov(R11,RDX);emit_imm_op(4,RDX,UINT32_C(0x1ffff000),0);emit_imm_op(1,RDX,1,0);
        emit_reg_op(0x39,RDX,RAX,0);misses[misses_count++] = emit_jump(5);
        if (write) {
            emit_mov(R11,RDX);emit_imm_op(4,RDX,UINT32_C(0x1ffff000),0);emit_shift_reg(RDX,5,12,0);
            emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,cached_code_pages));
            JIT(bt(host_mem(64,RAX,-1,0,0),Xbyak::Reg64(RDX)));
            misses[misses_count++] = emit_jump(2);
            emit_mem_imm(0x81,0,1,R12,RCX,2,offsetof(struct r4300_core,dcache_tags),2);
        }
        emit_mov(R11,R14);emit_imm_op(4,R14,UINT32_C(0x1ffc),0);
        emit_mem(0x8d,1,R14,R12,R14,0,offsetof(struct r4300_core,dcache_words));
        emit_movimm(1,RCX);emit_movimm(r4300_dcache_hit_cycles(config->compat_issue_clock),RDX);
        direct_cache_hit = emit_b();
        for (unsigned n=0;n<misses_count;++n)patch_jump(misses[n],out);
    }
    if (config->fastmem) {
        /* Outside the resident KSEG0 read shortcut, probe before any new
         * cache/timing/guest side effect. Invalid
         * translations and protected code writes point at a private no-access
         * page; the host handler redirects this exact PC to the precise exit. */
        emit_movimm64((intptr_t)config->fastmem, R14);
        emit_mov(R11, RAX); emit_shift_reg(RAX, 5, 12, 0);
        emit_mem(0x8b, 1, R14, R14, RAX, 3, write ?
            offsetof(struct r4300_fastmem, write) : offsetof(struct r4300_fastmem, read));
        emit_mem(0x8b, 1, R14, R14, -1, 0, 0);
        /* A five-byte, non-crossing probe can become JMP precise_exit after
         * repeated faults. Keep it entirely within one protection page. */
        while (((uintptr_t)out & 4095) > 4091) JIT(nop());
        exit->fault_pc = out;
        JIT(test(Xbyak::util::byte[Xbyak::Reg64(R14)], 0));
        JIT(nop());
        assert(out - exit->fault_pc == 5);
        emit_mov(R11, RAX); emit_imm_op(4, RAX, 4092, 0);
        emit_reg_op(0x01, RAX, R14, 1);
        emit_mov64(R14, RDX);
        emit_movimm64(config->fastmem->ram, RAX);
        emit_reg_op(0x29, RAX, RDX, 1);
    } else {
    // Only architectural KSEG translation is folded. Handlers, backing RAM,
    // bounds and code-page invalidation are still checked on every access.
    if(fixed_direct)emit_movimm(physical,RDX);
    else {
    emit_mov(R11, RDX);
    emit_mov(R11, RAX); emit_imm_op(4, RAX, UINT32_C(0xc0000000), 0);
    emit_imm_op(7, RAX, UINT32_C(0x80000000), 0);
    unsigned char* direct = emit_jump(4);
    emit_mov(R11, RAX); emit_shift_reg(RAX, 5, 12, 0);
    emit_mem(0x8b, 0, RDX, R12, RAX, 2, write ?
        offsetof(struct r4300_core, cp0.tlb.LUT_w) : offsetof(struct r4300_core, cp0.tlb.LUT_r));
    emit_reg_op(0x85, RDX, RDX, 0); memory_guard(exit, 4);
    emit_imm_op(4, RDX, UINT32_C(0xfffff000), 0);
    emit_mov(R11, RAX); emit_imm_op(4, RAX, 4095, 0);
    emit_reg_op(0x09, RAX, RDX, 0);
    emit_reg_op(0x85, RDX, RDX, 0); memory_guard(exit, 4);
    patch_jump(direct, out);
    emit_imm_op(4, RDX, UINT32_C(0x1ffffffc), 0);
    }
    if (write) {
        if(fixed_direct)emit_movimm(physical>>12,RCX);
        else {emit_mov(RDX, RCX); emit_shift_reg(RCX, 5, 12, 0);}
        emit_mem(0x8b, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, cached_code_pages));
        /* BT treats the memory operand as a bit string, indexing the complete
         * physical-page bitmap. A tracked page must use shared invalidation. */
        JIT(bt(host_mem(64,RAX,-1,0,0),Xbyak::Reg64(RCX)));
        memory_guard(exit, 2);
    }
    emit_mem(0x8b, 1, R14, R12, -1, 0, offsetof(struct r4300_core, mem));
    if(fixed_direct)emit_mem(0x8d,1,R14,R14,-1,0,offsetof(struct memory,handlers)+(physical>>16)*sizeof(struct mem_handler));
    else {
    emit_mov(RDX, RAX); emit_shift_reg(RAX, 5, 16, 0);
    emit_mem(0x8d, 1, RAX, RAX, RAX, 1, 0); /* three pointers per handler */
    emit_mem(0x8d, 1, R14, R14, RAX, 3, offsetof(struct memory, handlers));
    }
    emit_movimm64((intptr_t)(write ? (void*)write_rdram_dram : (void*)read_rdram_dram), RCX);
    emit_mem(0x39, 1, RCX, R14, -1, 0, write ? offsetof(struct mem_handler, write32) : offsetof(struct mem_handler, read32));
    memory_guard(exit, 5);
    emit_mem(0x8b, 1, R14, R14, -1, 0, offsetof(struct mem_handler, opaque));
    if(fixed_direct)emit_movimm((physical&UINT32_C(0x00fffffc))+(size==8?8:4),RAX);
    else {emit_mov(RDX, RAX); emit_imm_op(4, RAX, UINT32_C(0x00fffffc), 0);emit_addimm(RAX, size == 8 ? 8 : 4, RAX);}
    emit_mem(0x3b, 1, RAX, R14, -1, 0, offsetof(struct rdram, dram_size));
    memory_guard(exit, 7); /* unsigned greater: dispatch retains bounds behavior */
    emit_mem(0x8b, 1, R14, R14, -1, 0, offsetof(struct rdram, dram));
    if(fixed_direct)emit_addimm64(R14,physical&UINT32_C(0x00fffffc),R14);
    else {emit_mov(RDX, RAX); emit_imm_op(4, RAX, UINT32_C(0x00fffffc), 0);emit_reg_op(0x01, RAX, R14, 1);}
    }
    if (config->cache_timing) {
        unsigned char *uncached=nullptr,*config_uncached=nullptr,*mapped_uncached=nullptr,*data_ready=nullptr;
        if(!fixed_direct || !(address&UINT32_C(0x20000000))) {
        unsigned char* mapped=nullptr;
        if(!fixed_direct) {
        emit_mov(R11, RAX); emit_imm_op(4, RAX, UINT32_C(0xe0000000), 0);
        emit_imm_op(7, RAX, UINT32_C(0xa0000000), 0);
        uncached = emit_jump(4);
        emit_imm_op(7, RAX, UINT32_C(0x80000000), 0);
        mapped = emit_jump(5);
        }
        // A KSEG0 code region has already sampled this same live K0 mode.
        // Precise memory exits establish a fresh flag before resuming it.
        if((config->virtual_pc&UINT32_C(0xe0000000))==UINT32_C(0x80000000))
            emit_mem_imm(0xf7,0,0,RSP,-1,0,4,2);
        else {
        emit_mem(0x8b, 0, RAX, R12, -1, 0, offsetof(struct r4300_core, cp0.regs[CP0_CONFIG_REG]));
        emit_imm_op(4, RAX, 7, 0); emit_imm_op(7, RAX, 2, 0);
        }
        config_uncached = emit_jump(4);
        if(!fixed_direct) {
        unsigned char* direct_cached = emit_b();
        patch_jump(mapped, out);
        emit_data_cacheability(config); emit_reg_op(0x85, RAX, RAX, 0);
        mapped_uncached = emit_jump(4);
        patch_jump(direct_cached, out);
        }
        if(constant_address)emit_movimm((address>>4)&511,RCX);
        else {emit_mov(R11, RCX); emit_shift_reg(RCX, 5, 4, 0); emit_imm_op(4, RCX, 511, 0);}
        emit_mem(0x8b, 0, RAX, R12, RCX, 2, offsetof(struct r4300_core, dcache_tags));
        emit_imm_op(4, RAX, ~UINT32_C(2), 0);
        /* Uncached branches retain the original PA; cached accesses replace
         * the backing-memory pointer with the resident cache line below. */
        int key_reg = R14;
        if(fixed_direct)emit_movimm((physical&UINT32_C(0x1ffff000))|1,key_reg);
        else {if (key_reg != RDX) emit_mov(RDX, key_reg);emit_imm_op(4, key_reg, UINT32_C(0x1ffff000), 0);emit_imm_op(1, key_reg, 1, 0);}
        emit_reg_op(0x39, key_reg, RAX, 0);
        memory_guard(exit, 5);
        if (write) {
            emit_mem_imm(0x81, 0, 1, R12, RCX, 2, offsetof(struct r4300_core, dcache_tags),2);
        }
        if(constant_address)emit_mem(0x8d,1,R14,R12,-1,0,offsetof(struct r4300_core,dcache_words)+(address&UINT32_C(0x1ffc)));
        else {emit_mov(R11, R14); emit_imm_op(4, R14, UINT32_C(0x1ffc), 0);emit_mem(0x8d, 1, R14, R12, R14, 0, offsetof(struct r4300_core, dcache_words));}
        emit_movimm(1, RCX); emit_movimm(r4300_dcache_hit_cycles(config->compat_issue_clock), RDX);
        data_ready = emit_b();
        }
        if(uncached)patch_jump(uncached,out);
        if(config_uncached)patch_jump(config_uncached,out);
        if(mapped_uncached)patch_jump(mapped_uncached,out);
        emit_movimm(0, RCX);
        if(!config->compat_issue_clock) {
            /* Posted writes and dependent external reads share the C FIFO
             * boundary; cached RAM hits keep their native path. */
            if(write) {assert(exit->guard_count<16);exit->guards[exit->guard_count++]=emit_b();}
            else {
                emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,write_buffer_count),0);memory_guard(exit,5);
                emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,icache_fill_index),0);memory_guard(exit,5);
                emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,vi));
                emit_reg_op(0x85,RAX,RAX,1);auto *no_vi=emit_jump(4);
                emit_mem(0x8b,0,RAX,RAX,-1,0,offsetof(struct vi_controller,regs)+VI_STATUS_REG*4);
                emit_imm_op(4,RAX,3,0);emit_imm_op(7,RAX,2,0);memory_guard(exit,3);
                patch_jump(no_vi,out);
            }
        }
        if (!write) {
            emit_imm_op(7, RDX, UINT32_C(0x00800000), 0);
            unsigned char* larger = emit_jump(3);
            emit_movimm(R4300_UNCACHED_READ_CYCLES, RDX);
            unsigned char* bus_ready = emit_b();
            patch_jump(larger, out); emit_movimm(R4300_UNCACHED_DATA_CYCLES, RDX); patch_jump(bus_ready, out);
        } else emit_movimm(R4300_UNCACHED_DATA_CYCLES, RDX);
        if(data_ready)patch_jump(data_ready,out);
        if(direct_cache_hit)patch_jump(direct_cache_hit,out);
        emit_fetch_native(config, index, exit->start, 1, check_line);
        emit_reg_op(0x01, RDX, R13, 0);
        if (!r4300_issue_reservation(config->compat_issue_clock)) {
            emit_ldi_native(instruction, fp, previous, known);
            if (!known || native_store(previous)) {
                emit_reg_op(0x85, RCX, RCX, 0); unsigned char* no_cache = emit_jump(4);
                emit_mem_imm(0x81, 0, 7, R12, -1, 0, offsetof(struct r4300_core, pipeline_cached_store),0);
                unsigned char* no_store = emit_jump(4);
                emit_addimm(R13, 1, R13);
                patch_jump(no_store, out);
                patch_jump(no_cache, out);
            }
        } else {
        /* LDI and DCB can coexist in restored/test history at region entry.
         * Thereafter every instruction writes mutually exclusive load/store
         * history, so the reserved issue clock absorbs either single stall. */
        if (index == exit->start) {
        emit_reg_op(0x85, RCX, RCX, 0); unsigned char* no_cache = emit_jump(4);
        emit_mem_imm(0x81, 0, 7, R12, -1, 0, offsetof(struct r4300_core, pipeline_cached_store),0);
        unsigned char* no_store = emit_jump(4);
        unsigned char* fpr_history = NULL;
        if (fp) {
            emit_movimm64(UINT64_C(1) << (32 + rt),RDX);
            if (rt & 1) {
                emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
                unsigned char* fr = emit_jump(5); emit_movimm64(UINT64_C(1) << (32 + (rt & ~1u)),RDX); patch_jump(fr,out);
            }
            emit_mem(0x8b,1,RAX,R12,-1,0,offsetof(struct r4300_core,pipeline_load_mask));
            emit_reg_op(0x21,RDX,RAX,1); emit_reg_op(0x85,RAX,RAX,1);
        } else {
            uint32_t candidates = (rs ? UINT32_C(1) << rs : 0) | (rt ? UINT32_C(1) << rt : 0);
            /* Shared LDI detection gives FPR history priority, including
             * restored masks containing both integer and FP bits. */
            emit_mem(0x8b, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, pipeline_load_mask));
            emit_mov64(RAX, RDX); emit_shift_reg(RDX, 5, 32, 1);
            emit_reg_op(0x85, RDX, RDX, 0); fpr_history = emit_jump(5);
            emit_imm_op(4, RAX, candidates, 0); emit_reg_op(0x85, RAX, RAX, 0);
        }
        unsigned char* no_load = emit_jump(4);
        emit_addimm(R13, 1, R13);
        patch_jump(no_load, out); patch_jump(no_store, out); patch_jump(no_cache, out);
        if (fpr_history) patch_jump(fpr_history, out);
        }
        }
        if (write || !deferred_pipeline(config)) {
            if (!write) emit_movimm(0, RCX);
            emit_mem(0x89, 0, RCX, R12, -1, 0, offsetof(struct r4300_core, pipeline_cached_store));
        }
    } else {
        emit_fetch_native(config, index, exit->start, 1, check_line);
        if (R4300_OFF_RAM_CYCLES) emit_addimm(R13, R4300_OFF_RAM_CYCLES, R13);
        emit_state_imm(offsetof(struct r4300_core, pipeline_cached_store), 0, 0);
    }
    if (!deferred_pipeline(config)) {
    emit_movimm64(config->cache_timing && !write && (rt || fp) ? UINT64_C(1) << (rt + (fp ? 32 : 0)) : 0, RAX);
    if (fp && config->cache_timing && !write && (rt & 1)) {
        emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
        unsigned char* fr = emit_jump(5); emit_movimm64(UINT64_C(1) << (32 + (rt & ~1u)),RAX); patch_jump(fr,out);
    }
    emit_mem(0x89, 1, RAX, R12, -1, 0, offsetof(struct r4300_core, pipeline_load_mask));
    if (index == exit->start) {
        emit_state_imm(offsetof(struct r4300_core, pipeline_fpu_result_mask), 0, 1);
        emit_state_imm(offsetof(struct r4300_core, pipeline_fpu_cc), 0, 0);
    }
    }
    if (size < 4) {
        if(constant_address)emit_movimm((address^(size==1?3:2))&(size==1?3:2),RDX);
        else {emit_mov(R11, RDX); emit_imm_op(4, RDX, size == 1 ? 3 : 2, 0); emit_imm_op(6, RDX, size == 1 ? 3 : 2, 0);}
    }
    if (write) {
        if (fp) {
            emit_state_load_raw((size == 8 ? offsetof(struct r4300_core,cp1.regs_double) : offsetof(struct r4300_core,cp1.regs_simple)) + rt * sizeof(void*),RCX);
            emit_mem(0x8b,size == 8,RAX,RCX,-1,0,0);
        } else emit_state_load(gpr_offset(rt), RAX);
        if (size == 8) {
            emit_mov64(RAX, RCX); emit_shift_reg(RCX, 5, 32, 1);
            emit_mem(0x89, 0, RCX, R14, -1, 0, 0);
            emit_mem(0x89, 0, RAX, R14, -1, 0, 4);
        } else {
            JIT(mov(host_mem(size*8,R14,size<4?RDX:-1,0,0),Xbyak::Reg(RAX,Xbyak::Operand::REG,size*8)));
        }
    } else {
        if (size < 4) {
            int sign = op == R4300_OP_LB || op == R4300_OP_LH;
            if(sign) JIT(movsx(Xbyak::Reg64(RAX),host_mem(size*8,R14,RDX,0,0)));
            else JIT(movzx(Xbyak::Reg32(RAX),host_mem(size*8,R14,RDX,0,0)));
        } else if (size == 4) emit_mem(op == R4300_OP_LW ? 0x63 : 0x8b, op == R4300_OP_LW, RAX, R14, -1, 0, 0);
        else {
            emit_mem(0x8b, 0, RAX, R14, -1, 0, 0); emit_shift_reg(RAX, 4, 32, 1);
            emit_mem(0x8b, 0, RCX, R14, -1, 0, 4); emit_reg_op(0x09, RCX, RAX, 1);
        }
        if (fp) {
            emit_state_load_raw((size == 8 ? offsetof(struct r4300_core,cp1.regs_double) : offsetof(struct r4300_core,cp1.regs_simple)) + rt * sizeof(void*),RCX);
            emit_mem(0x89,size == 8,RAX,RCX,-1,0,0);
        } else if (rt) emit_state_store(gpr_offset(rt), RAX);
    }
}

