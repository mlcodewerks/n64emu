/* Keep the precise per-instruction operand/result helpers, but establish fetch
 * cacheability once for a straight-line COP1 sequence. These helpers cannot
 * modify mappings or I-cache tags; faults leave before the next instruction. */
static unsigned emit_fpu_sequence(void* block,const uint32_t* ops,unsigned start,unsigned count,
    const struct n64emujit_config* config,n64emujit_step step,unsigned char** exits,unsigned* exit_count)
{
    const auto is_transfer = [](uint32_t op) { return native_fpu_transfer(r4300_get_idec(op)->opcode); };
    unsigned end=start+1;
    const bool accurate = config->cache_timing == 1 &&
        !r4300_cached_issue_reservation(config->compat_issue_clock, config->cache_timing);
    if (is_transfer(ops[start]) && (!accurate || config->fpu_prepare != r4300_ops_jit_fpu_prepare))
        return start;
    /* Hoist admission for ordinary native preparation as well as mapped
     * fetch classification. COP1 cannot change mode/mapping/CU1 while it
     * continues; exceptions leave before the next instruction. */
    if(config->cache_timing &&
       ((config->virtual_pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000) || accurate) &&
       config->fpu_prepare==r4300_ops_jit_fpu_prepare)
        while (end < count && (native_fpu_instruction(ops[end]) || (accurate && is_transfer(ops[end]))))
            ++end;
    bool transfer_region = false;
    unsigned controlled = 0;
    for (unsigned i=start; i<end; ++i) {
        if (is_transfer(ops[i])) transfer_region = true;
        else {
            const unsigned fn = ops[i] & 63, fmt = (ops[i] >> 21) & 31;
            controlled += fn < 48 && !(fn >= 5 && fn <= 7) && !(fmt == 20 && fn == 33);
        }
    }
    if (end-start < 3 && !transfer_region) {
        emit_fpu_native(ops[start],start,config,exits,exit_count);
        return start+1;
    }
    unsigned base=0;
    for(unsigned i=start;i<end;i++)base+=r4300_base_cycles(ops[i],CP0_STATUS_CU1);
    struct memory_exit prepare={0};
    emit_region_guard(config,start,end-start,base,0,&prepare,2);
    /* A mapped fetch miss must wait for pending transfers in the precise order. */
    if (auto* miss = emit_itlb_miss_guard(config->virtual_pc + start * 4))
        prepare.guards[prepare.guard_count++] = miss;
    // Individual operations restore host FP state before any helper or exit.
    if (controlled >= 2) emit_fpu_save_environment();
    for (unsigned i=start; i<end; ++i) {
        if (is_transfer(ops[i])) emit_prefetched_transfer(ops[i],i,start,config);
        else emit_fpu_native(ops[i],i,config,exits,exit_count,true,start,controlled >= 2);
    }
    auto *done=emit_b();
    for(unsigned i=0;i<prepare.guard_count;i++)patch_jump(prepare.guards[i],out);
    exits[(*exit_count)++]=emit_step(block,start,((end-start)<<8)|5,step);
    patch_jump(done,out);return end;
}
