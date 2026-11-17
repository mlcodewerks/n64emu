static void emit_direct_links(void* block, const struct n64emujit_config* config)
{
    unsigned char* misses[19]; unsigned miss_count = 0;
    emit_mem_imm(0x81,1,7,R12,-1,0,offsetof(struct r4300_core,pc_extension),0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0xf7,0,0,R12,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_MODE_MASK); misses[miss_count++] = emit_jump(5);
    if(!config->compat_issue_clock){emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,irq_writeback_pending),0);misses[miss_count++]=emit_jump(5);}
    emit_movimm64((intptr_t)&breakloop,RAX);
    emit_mem_imm(0x80,0,7,RAX,-1,0,0,0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,delay_slot),0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0x81,0,7,R12,-1,0,offsetof(struct r4300_core,stop),0); misses[miss_count++] = emit_jump(5);
    emit_mem(0x8b,0,RDX,R12,-1,0,offsetof(struct r4300_core,instruction_pc.addr));
    /* Select the edge first, then share validation and the tail jump. Both
     * outcomes retain the same guards without duplicating their hot code. */
    emit_movimm64((intptr_t)&config->links[0],R11);
    emit_mem(0x39,0,RDX,R11,-1,0,offsetof(struct n64emujit_link_cache,pc));
    auto *first=emit_jump(4);
    emit_addimm64(R11,sizeof(struct n64emujit_link_cache),R11);
    emit_mem(0x39,0,RDX,R11,-1,0,offsetof(struct n64emujit_link_cache,pc));misses[miss_count++]=emit_jump(5);
    patch_jump(first,out);
    // Fetch independent validity dependencies together.
    emit_mem(0x8b,1,R8,R11,-1,0,offsetof(struct n64emujit_link_cache,current_entry));
    emit_reg_op(0x85,R8,R8,1);misses[miss_count++]=emit_jump(4);
    emit_mem(0x8b,1,R9,R11,-1,0,offsetof(struct n64emujit_link_cache,current_page_generation));
    emit_mem(0x8b,1,R10,R11,-1,0,offsetof(struct n64emujit_link_cache,current_segment_generation));
    emit_mem(0x8b,1,R8,R8,-1,0,0);
    emit_mem(0x8b,0,R9,R9,-1,0,0);
    emit_mem(0x8b,1,R10,R10,-1,0,0);
    emit_mem(0x39,1,R8,R11,-1,0,offsetof(struct n64emujit_link_cache,expected_entry));misses[miss_count++]=emit_jump(5);
    emit_mem(0x39,0,R9,R11,-1,0,offsetof(struct n64emujit_link_cache,page_generation));misses[miss_count++]=emit_jump(5);
    emit_mem(0x39,1,R10,R11,-1,0,offsetof(struct n64emujit_link_cache,segment_generation));misses[miss_count++]=emit_jump(5);
    emit_movimm64((intptr_t)config->epoch,RAX);emit_mem(0x8b,0,RAX,RAX,-1,0,0);
    emit_mem(0x39,0,RAX,R11,-1,0,offsetof(struct n64emujit_link_cache,epoch));misses[miss_count++]=emit_jump(5);
    emit_mem(0x8b,1,RAX,R11,-1,0,offsetof(struct n64emujit_link_cache,current_mapping));
    emit_reg_op(0x85,RAX,RAX,1); unsigned char* fixed_mapping = emit_jump(4);
    emit_mem(0x8b,0,RAX,RAX,-1,0,0);
    emit_mem(0x39,0,RAX,R11,-1,0,offsetof(struct n64emujit_link_cache,mapping)); misses[miss_count++] = emit_jump(5);
    patch_jump(fixed_mapping,out);
    emit_mem(0x8b,1,RAX,R11,-1,0,offsetof(struct n64emujit_link_cache,target_block));
    emit_movimm64((intptr_t)config->previous_block,RCX); emit_mem(0x89,1,RAX,RCX,-1,0,0);
    uint64_t* counters[] = {config->entries,config->linked_entries,config->direct_links};
    uintptr_t counter_base = 0;
    for (unsigned n = 0; n < 3; ++n) if (counters[n]) {
        int64_t offset = (int64_t)(uintptr_t)counters[n] - (int64_t)counter_base;
        if (!counter_base || offset < INT32_MIN || offset > INT32_MAX) {
            counter_base = (uintptr_t)counters[n]; offset = 0;
            emit_movimm64((intptr_t)counter_base,RAX);
        }
        emit_mem_imm(0x83,1,0,RAX,-1,0,(size_t)offset,1);
    }
    if (config->profile) {
        emit_mem(0x8b,1,RAX,R11,-1,0,offsetof(struct n64emujit_link_cache,profile_entries));
        emit_mem_imm(0x83,1,0,RAX,-1,0,0,1);
    }
    emit_mem(0xff,0,4,R11,-1,0,offsetof(struct n64emujit_link_cache,body));
    for (unsigned n = 0; n < miss_count; ++n) patch_jump(misses[n],out);
    emit_movimm64((intptr_t)config->previous_block,RCX);
    emit_movimm64((intptr_t)block,RAX); emit_mem(0x89,1,RAX,RCX,-1,0,0);
}
