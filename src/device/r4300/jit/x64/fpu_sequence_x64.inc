/* Keep the precise per-instruction operand/result helpers, but establish fetch
 * cacheability once for a straight-line COP1 sequence. These helpers cannot
 * modify mappings or I-cache tags; faults leave before the next instruction. */
static unsigned emit_fpu_sequence(void* block,const uint32_t* ops,unsigned start,unsigned count,
    const struct n64emujit_config* config,n64emujit_step step,unsigned char** exits,unsigned* exit_count)
{
    unsigned end=start+1;
    bool resident_preparation=false;
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(N64EMUJIT_CROSS_ASSEMBLE)
    resident_preparation=config->cache_timing==1 && !r4300_issue_reservation(config->compat_issue_clock);
#endif
    if(native_fpu_transfer(r4300_get_idec(ops[start])->opcode) &&
        (!resident_preparation || config->fpu_prepare!=r4300_ops_jit_fpu_prepare))return start;
    /* Hoist admission for ordinary native preparation as well as mapped
     * fetch classification. COP1 cannot change mode/mapping/CU1 while it
     * continues; exceptions leave before the next instruction. */
    if(config->cache_timing &&
       ((config->virtual_pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000) || resident_preparation) &&
       config->fpu_prepare==r4300_ops_jit_fpu_prepare)
        while(end<count && (native_fpu_instruction(ops[end]) ||
            (resident_preparation && native_fpu_transfer(r4300_get_idec(ops[end])->opcode))))++end;
    bool transfer_region=false;
    if(resident_preparation && config->fpu_prepare==r4300_ops_jit_fpu_prepare)
        for(unsigned i=start;i<end;++i)transfer_region|=native_fpu_transfer(r4300_get_idec(ops[i])->opcode)!=0;
    if(end-start<3 && !transfer_region){
        if(native_fpu_transfer(r4300_get_idec(ops[start])->opcode))return start;
        emit_fpu_native(ops[start],start,config,exits,exit_count);return start+1;
    }
    unsigned base=0;
    for(unsigned i=start;i<end;i++)base+=r4300_base_cycles(ops[i],CP0_STATUS_CU1);
    struct memory_exit prepare={0};
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(N64EMUJIT_CROSS_ASSEMBLE)
    emit_region_guard(config,start,end-start,base,0,&prepare,2);
    /* The precise fetch reserves a micro-TLB refill's clocks until after an
     * older pending I-cache fill has been waited. The combined native fetch
     * currently charges them ahead of that wait. Keep refill boundaries on
     * the precise path; ordinary resident micro-TLB hits still batch COP1. */
    uint32_t pc=config->virtual_pc+start*4;
    if((pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000)) {
    emit_mem(0x8b,0,RAX,R12,-1,0,offsetof(struct r4300_core,cp0.tlb.LUT_r)+(pc>>12)*4);
    emit_imm_op(4,RAX,UINT32_C(0xfffff000),0);
    unsigned char *hits[2];
    for(unsigned slot=0;slot<2;++slot) {
        unsigned char *miss[3];
        emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_valid),1u<<slot);
        miss[0]=emit_jump(4);
        emit_mem(0x39,0,RAX,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_map)+slot*4);
        miss[1]=emit_jump(5);
        emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,pipeline_itlb_vpn)+slot*4,pc>>12);
        miss[2]=emit_jump(5);
        hits[slot]=emit_b();
        for(auto *branch:miss)patch_jump(branch,out);
    }
    assert(prepare.guard_count<16);
    prepare.guards[prepare.guard_count++]=emit_b();
    for(auto *branch:hits)patch_jump(branch,out);
    }
#else
    emit_region_guard(config,start,end-start,base,0,&prepare);
#endif
    emit_fpu_save_fetch_flags();
    unsigned controlled=0;
    for(unsigned i=start;i<end;++i)if(native_fpu_instruction(ops[i])) {
        unsigned fn=ops[i]&63,fmt=(ops[i]>>21)&31;
        if(fn<48 && !(fn>=5 && fn<=7) && !(fmt==20 && fn==33))++controlled;
    }
    bool use_envelope=controlled>=2;
    unsigned char *local_exits[MAX_INSTRUCTIONS*14+16];unsigned local_count=0;
    fpu_mxcsr_scope envelope_scope(use_envelope);
    if(use_envelope)emit_mem_imm(0xc7,0,0,RSP,-1,0,80,0);
    for(unsigned i=start;i<end;i++) {
        if(native_fpu_transfer(r4300_get_idec(ops[i])->opcode))emit_prefetched_transfer(ops[i],i,start,config);
        else emit_fpu_native(ops[i],i,config,use_envelope?local_exits:exits,
            use_envelope?&local_count:exit_count,true,start);
    }
    if(use_envelope)emit_fpu_restore_host();
    auto *done=emit_b();
    if(use_envelope) {
        for(unsigned i=0;i<local_count;++i)patch_jump(local_exits[i],out);
        emit_fpu_restore_host();exits[(*exit_count)++]=emit_b();
    }
    envelope_scope.finish();
    for(unsigned i=0;i<prepare.guard_count;i++)patch_jump(prepare.guards[i],out);
    exits[(*exit_count)++]=emit_step(block,start,((end-start)<<8)|5,step);
    patch_jump(done,out);return end;
}
