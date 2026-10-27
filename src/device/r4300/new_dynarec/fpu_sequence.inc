/* Keep the precise per-instruction operand/result helpers, but establish fetch
 * cacheability once for a straight-line COP1 sequence. These helpers cannot
 * modify mappings or I-cache tags; faults leave before the next instruction. */
static unsigned emit_fpu_sequence(void* block,const uint32_t* ops,unsigned start,unsigned count,
    const struct ari64_config* config,ari64_step step,unsigned char** exits,unsigned* exit_count)
{
    unsigned end=start+1;
    /* Hoisting the mapped TLB scan pays for the sequence guard. Direct
     * KSEG fetches are cheaper through the specialized per-op helper. */
    if(config->cache_timing && (config->virtual_pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000) &&
       config->fpu_prepare==pure_interp_jit_fpu_prepare)
        while(end<count && native_fpu_instruction(ops[end]))++end;
    if(end-start<3){emit_fpu_native(ops[start],start,config,exits,exit_count);return start+1;}
    unsigned base=0;
    for(unsigned i=start;i<end;i++)base+=r4300_base_cycles(ops[i],CP0_STATUS_CU1);
    struct memory_exit prepare={0};
    emit_region_guard(config,start,end-start,base,0,&prepare);
    emit_fpu_save_fetch_flags();
    for(unsigned i=start;i<end;i++)emit_fpu_native(ops[i],i,config,exits,exit_count,true,start);
    auto *done=emit_b();
    for(unsigned i=0;i<prepare.guard_count;i++)patch_jump(prepare.guards[i],out);
    exits[(*exit_count)++]=emit_step(block,start,((end-start)<<8)|5,step);
    patch_jump(done,out);return end;
}
