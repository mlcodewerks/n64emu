static void emit_direct_links(void* block, const struct n64emujit_config* config)
{
    unsigned char* misses[19]; unsigned miss_count = 0;
    emit_mem_imm(0x81,1,7,STATE,-1,0,offsetof(struct r4300_core,pc_extension),0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0xf7,0,0,STATE,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_MODE_MASK); misses[miss_count++] = emit_jump(5);
    if(!config->compat_issue_clock){emit_mem_imm(0x81,0,7,STATE,-1,0,offsetof(struct r4300_core,irq_writeback_pending),0);misses[miss_count++]=emit_jump(5);}
    emit_movimm64((intptr_t)&breakloop,T0);
    emit_mem_imm(0x80,0,7,T0,-1,0,0,0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0x81,0,7,STATE,-1,0,offsetof(struct r4300_core,delay_slot),0); misses[miss_count++] = emit_jump(5);
    emit_mem_imm(0x81,0,7,STATE,-1,0,offsetof(struct r4300_core,stop),0); misses[miss_count++] = emit_jump(5);
    emit_mem(0x8b,0,T2,STATE,-1,0,offsetof(struct r4300_core,instruction_pc.addr));
    /* Select the edge first, then share validation and the tail jump. Both
     * outcomes retain the same guards without duplicating their hot code. */
    emit_movimm64((intptr_t)&config->links[0],ADDRESS);
    emit_mem(0x39,0,T2,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,pc));
    auto *first=emit_jump(4);
    emit_addimm64(ADDRESS,sizeof(struct n64emujit_link_cache),ADDRESS);
    emit_mem(0x39,0,T2,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,pc));misses[miss_count++]=emit_jump(5);
    patch_jump(first,out);
    // Fetch independent validity dependencies together.
    emit_mem(0x8b,1,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,current_entry));
    emit_reg_op(0x85,T0,T0,1);misses[miss_count++]=emit_jump(4);
    emit_mem(0x8b,1,T1,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,current_page_generation));
    emit_mem(0x8b,1,T2,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,current_segment_generation));
    emit_mem(0x8b,1,T0,T0,-1,0,0);
    emit_mem(0x8b,0,T1,T1,-1,0,0);
    emit_mem(0x8b,1,T2,T2,-1,0,0);
    emit_mem(0x39,1,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,expected_entry));misses[miss_count++]=emit_jump(5);
    emit_mem(0x39,0,T1,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,page_generation));misses[miss_count++]=emit_jump(5);
    emit_mem(0x39,1,T2,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,segment_generation));misses[miss_count++]=emit_jump(5);
    emit_movimm64((intptr_t)config->epoch,T0);emit_mem(0x8b,0,T0,T0,-1,0,0);
    emit_mem(0x39,0,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,epoch));misses[miss_count++]=emit_jump(5);
    emit_mem(0x8b,1,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,current_mapping));
    emit_reg_op(0x85,T0,T0,1); unsigned char* fixed_mapping = emit_jump(4);
    emit_mem(0x8b,0,T0,T0,-1,0,0);
    emit_mem(0x39,0,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,mapping)); misses[miss_count++] = emit_jump(5);
    patch_jump(fixed_mapping,out);
    emit_mem(0x8b,1,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,target_block));
    emit_movimm64((intptr_t)config->previous_block,T1); emit_mem(0x89,1,T0,T1,-1,0,0);
    uint64_t* counters[] = {config->entries,config->linked_entries,config->direct_links};
    uintptr_t counter_base = 0;
    for (unsigned n = 0; n < 3; ++n) if (counters[n]) {
        int64_t offset = (int64_t)(uintptr_t)counters[n] - (int64_t)counter_base;
        if (!counter_base || offset < INT32_MIN || offset > INT32_MAX) {
            counter_base = (uintptr_t)counters[n]; offset = 0;
            emit_movimm64((intptr_t)counter_base,T0);
        }
        emit_mem_imm(0x83,1,0,T0,-1,0,(size_t)offset,1);
    }
    if (config->profile) {
        emit_mem(0x8b,1,T0,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,profile_entries));
        emit_mem_imm(0x83,1,0,T0,-1,0,0,1);
    }
    emit_mem(0xff,0,4,ADDRESS,-1,0,offsetof(struct n64emujit_link_cache,body));
    for (unsigned n = 0; n < miss_count; ++n) patch_jump(misses[n],out);
    emit_movimm64((intptr_t)config->previous_block,T1);
    emit_movimm64((intptr_t)block,T0); emit_mem(0x89,1,T0,T1,-1,0,0);
}
