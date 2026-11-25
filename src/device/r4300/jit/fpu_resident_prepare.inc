/* Resident COP1 preparation shared by the x64 and AArch64 emitters. */
#ifndef N64EMU_USF
/* A shared admission check keeps isolated FP instructions small. It only
 * reads guest state; operand classification and issue/result work stay native. */
static int fpu_resident_guard(const struct r4300_core *r,uint32_t pc,uint32_t tag,unsigned compatibility)
{
    if(r->cache_timing!=1 || r->compat_issue_clock!=compatibility || r->pc_extension)return 0;
    unsigned status=r->cp0.regs[CP0_STATUS_REG];
    if(!(status&CP0_STATUS_CU1) || ((status&CP0_STATUS_MODE_MASK) &&
       !(status&(CP0_STATUS_EXL|CP0_STATUS_ERL))))return 0;
    unsigned translation=1;
    bool mapped=(pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000);
    if(mapped) {
        uint32_t map=r->cp0.tlb.LUT_r[pc>>12];
        if(!map || ((map&UINT32_C(0x1ffff000))|1u)!=tag)return 0;
        translation=0;
        for(unsigned slot=0;slot<2;++slot)
            if((r->pipeline_itlb_valid&(1u<<slot)) &&
               r->pipeline_itlb_vpn[slot]==pc>>12 &&
               r->pipeline_itlb_map[slot]==(map&UINT32_C(0xfffff000))) {
                translation=slot+2u;break;
            }
        if(!translation)return 0;
    } else if((pc&UINT32_C(0xe0000000))!=UINT32_C(0x80000000))return 0;
    unsigned index=(pc>>5)&511;
    if((!mapped && (r->cp0.regs[CP0_CONFIG_REG]&7)==2) || r->icache_tags[index]!=tag ||
       r->icache_fill_index==index+1 || r->cp0.cycle_count>=-32)return 0;
    if((pc&31)>=24 && (pc&4095)<4088 && r->icache_tags[((pc+8)>>5)&511]!=tag)return 0;
    return translation;
}
#endif

/* A resident fetch with a resident prefetch line has no cache
 * side effects. Keep ordinary S/D operand and pipeline work native, sharing
 * isolated admission through the read-only guard. All guards precede
 * architectural changes; the precise preparation helper owns
 * misses, exceptional operands, privilege checks and event boundaries. */
static unsigned char* emit_fpu_resident_prepare(uint32_t instruction, unsigned index,
    const struct n64emujit_config* config,bool prefetched)
{
#ifdef N64EMU_USF
    // The USF ripper uses a four-clock issue envelope even with compat off.
    (void)instruction;(void)index;(void)config;(void)prefetched;
    return NULL;
#else
    unsigned fmt=(instruction>>21)&31,fn=instruction&63;
    bool word_conversion=fmt==20 && (fn==32 || fn==33);
    bool float_conversion=(fmt==16 && fn==33) || (fmt==17 && fn==32);
    bool comparison=(fmt==16 || fmt==17) && fn>=48;
    uint32_t pc=config->virtual_pc+index*4;
    bool mapped=!prefetched && (pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000);
    if((!prefetched && (pc&UINT32_C(0xe0000000))==UINT32_C(0xa0000000)) ||
       (mapped && (!config->fetch_cache || !config->fetch_generation)))return NULL;
    /* Compatible's generated fetch does not read ahead. Keep its precise
     * preparation at line edges, where that helper can fetch the next line. */
    if(prefetched && config->compat_issue_clock && (pc&31)>=24 && (pc&4095)<4088)return NULL;
    if(config->cache_timing!=1 ||
       config->fpu_prepare!=r4300_ops_jit_fpu_prepare ||
       (!word_conversion && ((fmt!=16 && fmt!=17) || (fn>7 && !float_conversion && !comparison)))) return NULL;
    unsigned char* slow[24];unsigned slow_count=0;
    if(!prefetched) {
        if(mapped) {
            /* Cacheability is valid only for this ASID and mapping epoch.
             * Cold/stale metadata and all micro-TLB refills remain precise. */
            emit_movimm64((intptr_t)config->fetch_cache,FPU_INFO);
            emit_movimm64((intptr_t)config->fetch_generation,FPU_METADATA);
            emit_mem(0x8b,1,FPU_TEMP,FPU_METADATA,-1,0,0);
            emit_mem(0x39,1,FPU_TEMP,FPU_INFO,-1,0,offsetof(n64emujit_fetch_cache,generation));
            slow[slow_count++]=emit_jump(5);
            emit_mem(0x8b,0,FPU_TEMP,FPU_STATE,-1,0,offsetof(r4300_core,cp0.regs[CP0_ENTRYHI_REG]));
            emit_imm_op(4,FPU_TEMP,255,0);
            emit_imm_op(1,FPU_TEMP,UINT32_C(0x80000000)|((pc>>12)<<8),0);
            emit_mem(0x39,0,FPU_TEMP,FPU_INFO,-1,0,offsetof(n64emujit_fetch_cache,tag));
            slow[slow_count++]=emit_jump(5);
            emit_mem_imm(0x83,0,7,FPU_INFO,-1,0,offsetof(n64emujit_fetch_cache,cached),0);
            slow[slow_count++]=emit_jump(4);
        }
        emit_mov64(FPU_STATE,ARG1_REG);emit_movimm(pc,ARG2_REG);
        emit_movimm((config->physical_page<<12)|1,ARG3_REG);
        emit_movimm(config->compat_issue_clock,ARG4_REG);
        emit_helper_call((const void*)fpu_resident_guard);
        emit_reg_op(0x85,FPU_TEMP,FPU_TEMP,0);slow[slow_count++]=emit_jump(4);
        if(mapped)emit_mem(0x89,0,FPU_TEMP,FPU_STACK,-1,0,56);
    }
    unsigned fs=(instruction>>11)&31,ft=(instruction>>16)&31,fd=(instruction>>6)&31;
    emit_state_load_raw(offsetof(struct r4300_core,cp1.regs_double)+fs*sizeof(void*),FPU_TEMP);
    emit_mem(0x8b,1,FPU_LEFT,FPU_TEMP,-1,0,0);
    emit_state_load_raw(offsetof(struct r4300_core,cp1.regs)+ft*sizeof(cp1_reg),FPU_RIGHT);
    unsigned latency=fn==6?0:r4300_base_cycles(instruction,CP0_STATUS_CU1)-1;
    emit_movimm(latency,FPU_INFO);
    if(word_conversion) {
        /* CVT.S.W/CVT.D.W accept every signed low word. Their zero
         * shortcut ignores the upper word, matching the precise classifier. */
        emit_reg_op(0x85,FPU_LEFT,FPU_LEFT,0);auto *nonzero=emit_jump(5);
        emit_movimm(1,FPU_INFO);patch_jump(nonzero,out);
    } else if(comparison) {
        // Compare accepts finite values, subnormals and infinities. NaN
        // classification, sticky Invalid and enabled exceptions stay precise.
        for(unsigned n=0;n<2;++n) {
            emit_mov64(n?FPU_RIGHT:FPU_LEFT,FPU_TEMP);
            if(fmt==16) {emit_imm_op(4,FPU_TEMP,0x7f800000,0);emit_imm_op(7,FPU_TEMP,0x7f800000,0);}
            else {emit_shift_reg(FPU_TEMP,5,52,1);emit_imm_op(4,FPU_TEMP,2047,0);emit_imm_op(7,FPU_TEMP,2047,0);}
            auto *finite=emit_jump(5);
            emit_mov64(n?FPU_RIGHT:FPU_LEFT,FPU_TEMP);
            if(fmt==16)emit_imm_op(4,FPU_TEMP,0x007fffff,0);else emit_shift_reg(FPU_TEMP,4,12,1);
            emit_reg_op(0x85,FPU_TEMP,FPU_TEMP,fmt!=16);slow[slow_count++]=emit_jump(5);
            patch_jump(finite,out);
        }
    } else if(fn!=6) {
        for(unsigned n=0;n<(fn<=3?2u:1u);++n) {
            emit_mov64(n?FPU_RIGHT:FPU_LEFT,FPU_TEMP);
            if(fmt==16) {emit_imm_op(4,FPU_TEMP,0x7f800000,0);emit_shift_reg(FPU_TEMP,5,23,0);}
            else {emit_shift_reg(FPU_TEMP,5,52,1);emit_imm_op(4,FPU_TEMP,2047,0);}
            emit_reg_op(0x85,FPU_TEMP,FPU_TEMP,0);auto *normal=emit_jump(5);
            emit_mov64(n?FPU_RIGHT:FPU_LEFT,FPU_TEMP);emit_shift_reg(FPU_TEMP,4,fmt==16?33:1,1);
            emit_reg_op(0x85,FPU_TEMP,FPU_TEMP,1);slow[slow_count++]=emit_jump(5);
            if(fn<=4)emit_movimm(1,FPU_INFO);
            auto *zero=emit_b();patch_jump(normal,out);
            // Subnormals and infinity/NaN retain the precise classifier.
            emit_addimm(FPU_TEMP,-1,FPU_TEMP);emit_imm_op(7,FPU_TEMP,fmt==16?253:2045,0);
            slow[slow_count++]=emit_jump(7);
            patch_jump(zero,out);
        }
        if(fn==2) {
            for(unsigned n=0;n<2;++n) {
                emit_mov64(n?FPU_RIGHT:FPU_LEFT,FPU_TEMP);emit_shift_reg(FPU_TEMP,4,fmt==16?41:12,1);
                emit_reg_op(0x85,FPU_TEMP,FPU_TEMP,1);auto *fraction=emit_jump(5);
                emit_movimm(1,FPU_INFO);patch_jump(fraction,out);
            }
        }
        if(fn==4) {
            emit_reg_op(0x85,FPU_LEFT,FPU_LEFT,fmt==17);
            slow[slow_count++]=emit_jump(8);
        }
    }
    emit_mem(0x89,1,FPU_LEFT,FPU_STACK,-1,0,32);emit_mem(0x89,1,FPU_RIGHT,FPU_STACK,-1,0,40);
    if(comparison)emit_movimm(0,FPU_TEMP); // inputs[2] is unordered, not rounding.
    else {
        emit_mem(0x8b,0,FPU_TEMP,FPU_STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31));
        emit_imm_op(4,FPU_TEMP,3,0);
    }
    emit_mem(0x89,0,FPU_TEMP,FPU_STACK,-1,0,48);
    emit_mem(0x89,0,FPU_INFO,FPU_STACK,-1,0,52);
    // LDI gives FPR history priority over mixed restored GPR/FPR history.
    uint64_t ldi[2],reads[2],result[2];
    for(unsigned fr=0;fr<2;++fr) {
        auto mask=[fr](unsigned reg){return UINT64_C(1)<<(32+(fr?reg:reg&~1u));};
        ldi[fr]=mask(fs)|(fn==3?0:mask(ft));reads[fr]=mask(fs)|mask(ft);result[fr]=comparison?0:mask(fd);
    }
    emit_movimm64(ldi[0],FPU_LEFT);emit_movimm64(reads[0],FPU_RIGHT);emit_movimm64(result[0],FPU_INFO);
    if(ldi[0]!=ldi[1]||reads[0]!=reads[1]||result[0]!=result[1]) {
        emit_mem_imm(0xf7,0,0,FPU_STATE,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_STATUS_REG]),CP0_STATUS_FR);
        auto *paired=emit_jump(4);
        emit_movimm64(ldi[1],FPU_LEFT);emit_movimm64(reads[1],FPU_RIGHT);emit_movimm64(result[1],FPU_INFO);
        patch_jump(paired,out);
    }
    if(mapped) {
        /* All remaining failures use finish, not instruction replay. A
         * successful fetch updates LRU once; operand rejection above leaves
         * the original preparer to perform its own fetch and update. */
        emit_mem(0x8b,0,FPU_TEMP,FPU_STACK,-1,0,56);emit_imm_op(6,FPU_TEMP,3,0);
        emit_mem(0x89,0,FPU_TEMP,FPU_STATE,-1,0,offsetof(r4300_core,pipeline_itlb_lru));
    }
    emit_movimm(1,FPU_CLOCKS);
    emit_mem(0x85,1,FPU_LEFT,FPU_STATE,-1,0,offsetof(struct r4300_core,pipeline_load_mask));auto *no_ldi=emit_jump(4);
    emit_addimm(FPU_CLOCKS,1,FPU_CLOCKS);patch_jump(no_ldi,out);
    emit_mem(0x85,1,FPU_RIGHT,FPU_STATE,-1,0,offsetof(struct r4300_core,pipeline_fpu_result_mask));auto *no_fpu=emit_jump(4);
    emit_addimm(FPU_CLOCKS,1,FPU_CLOCKS);patch_jump(no_fpu,out);
    emit_state_imm(offsetof(struct r4300_core,pipeline_load_mask),0,1);
    emit_mem(0x89,1,FPU_INFO,FPU_STATE,-1,0,offsetof(struct r4300_core,pipeline_fpu_result_mask));
    emit_state_imm(offsetof(struct r4300_core,pipeline_fpu_cc),comparison?1:0,1);
    if(fn!=6)emit_mem_imm(0x81,0,4,FPU_STATE,-1,0,offsetof(struct r4300_core,cp1.fcr31),~UINT32_C(0x3f000));
    /* The following host arithmetic cannot observe CPU clocks. Combine issue
     * with completion on the fast path; flush it before any precise finish. */
    emit_mem(0x89,0,FPU_CLOCKS,FPU_STACK,-1,0,fpu_issue_cycles);
    auto *done=emit_b();
    assert(slow_count<=24);
    for(unsigned n=0;n<slow_count;++n)patch_jump(slow[n],out);
    return done;
#endif
}

