    void increment_counter(unsigned pointer) {
        host.load(0,pointer,0);host.immediate(ADD,0,1);host.store(0,pointer,0);
        host.load(0,pointer,4);host.imm(2,0);host.alu(ADC,0,2);host.store(0,pointer,4);
    }
    void tail_jump(unsigned target) {
        if (shared_frame) {
            host.jump_register(target);
            return;
        }
#ifdef N64EMU_JIT32_ARM
        host.mov(12,target);host.mov(0,STATE);host.leave();host.jump_register(12);
#else
        host.mov(0,target);host.leave();host.jump_register(0);
#endif
    }
    void direct_links(void* block,const n64emujit_config* config) {
        Label miss,selected,fixed_mapping;
        const auto guard_zero=[&](size_t offset,unsigned bytes=4) {
            host.load(0,STATE,int32_t(offset),bytes);host.immediate(CMP,0,0);host.branch(miss,NE);
        };
        guard_zero(offsetof(r4300_core,pc_extension));
        guard_zero(offsetof(r4300_core,pc_extension)+4);
        host.load(0,STATE,offsetof(r4300_core,cp0.regs[CP0_STATUS_REG]));
        host.immediate(TEST,0,CP0_STATUS_MODE_MASK);host.branch(miss,NE);
        guard_zero(offsetof(r4300_core,irq_writeback_pending));
        guard_zero(offsetof(r4300_core,prefetch_valid));
        host.imm(1,uint32_t(uintptr_t(config->break_requested)));
        host.load(0,1,0,1);host.immediate(CMP,0,0);host.branch(miss,NE);
        guard_zero(offsetof(r4300_core,delay_slot));guard_zero(offsetof(r4300_core,stop));
        host.load(0,STATE,offsetof(r4300_core,instruction_pc.addr));
        host.imm(3,uint32_t(uintptr_t(&config->links[0])));
        host.load(1,3,offsetof(n64emujit_link_cache,pc));host.alu(CMP,0,1);host.branch(selected,EQ);
        host.imm(3,uint32_t(uintptr_t(&config->links[1])));
        host.load(1,3,offsetof(n64emujit_link_cache,pc));host.alu(CMP,0,1);host.branch(miss,NE);
        host.bind(selected);
        const auto compare_slot=[&](unsigned value,size_t offset) {
            host.load(1,3,int32_t(offset));host.alu(CMP,value,1);host.branch(miss,NE);
        };
        host.load(0,3,offsetof(n64emujit_link_cache,current_entry));
        host.immediate(CMP,0,0);host.branch(miss,EQ);
        host.load(0,0,0);compare_slot(0,offsetof(n64emujit_link_cache,expected_entry));
        host.imm(0,uint32_t(uintptr_t(config->epoch)));host.load(0,0,0);
        compare_slot(0,offsetof(n64emujit_link_cache,epoch));
        host.load(0,3,offsetof(n64emujit_link_cache,current_page_generation));host.load(0,0,0);
        compare_slot(0,offsetof(n64emujit_link_cache,page_generation));
        host.load(0,3,offsetof(n64emujit_link_cache,current_mapping));
        host.immediate(CMP,0,0);host.branch(fixed_mapping,EQ);
        host.load(0,0,0);compare_slot(0,offsetof(n64emujit_link_cache,mapping));
        host.bind(fixed_mapping);
        host.load(2,3,offsetof(n64emujit_link_cache,current_segment_generation));
        host.load(0,2,0);compare_slot(0,offsetof(n64emujit_link_cache,segment_generation));
        host.load(0,2,4);compare_slot(0,offsetof(n64emujit_link_cache,segment_generation)+4);
        host.load(0,3,offsetof(n64emujit_link_cache,target_block));
        host.imm(1,uint32_t(uintptr_t(config->previous_block)));host.store(0,1,0);
        uint64_t* counters[]={config->entries,config->linked_entries,config->direct_links};
        for(auto* counter:counters)if(counter){host.imm(1,uint32_t(uintptr_t(counter)));increment_counter(1);}
        if(config->profile){host.load(1,3,offsetof(n64emujit_link_cache,profile_entries));increment_counter(1);}
        host.load(0,3,offsetof(n64emujit_link_cache,body));tail_jump(0);
        host.bind(miss);
        /* Precise callbacks can replace previous_block. Restore this source
         * before the shared C linker handles any guard miss. */
        host.imm(0,uint32_t(uintptr_t(block)));
        host.imm(1,uint32_t(uintptr_t(config->previous_block)));host.store(0,1,0);
    }
