/* Cache lookup results as runtime data, not baked mapping assumptions. A
 * shared slot may be reused by any virtual page or ASID; every hit checks
 * both its key and the live TLB/state-restoration generation. */
static void emit_code_cacheability(const struct n64emujit_config* config,uint32_t pc,
    unsigned state,unsigned result,unsigned slot,unsigned generation,unsigned address)
{
    if(!config->fetch_cache || !config->fetch_generation) {
        emit_movimm(pc,address);emit_mapped_cacheability();return;
    }
    const uint32_t page_tag=UINT32_C(0x80000000)|((pc>>12)<<8);
    emit_movimm64((intptr_t)config->fetch_cache,slot);
    emit_mem(0x8b,0,result,state,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_ENTRYHI_REG]));
    emit_imm_op(4,result,255,0);emit_imm_op(1,result,page_tag,0);
    emit_mem(0x39,0,result,slot,-1,0,offsetof(struct n64emujit_fetch_cache,tag));
    unsigned char* other=emit_jump(5);
    emit_movimm64((intptr_t)config->fetch_generation,generation);
    emit_mem(0x8b,1,generation,generation,-1,0,0);
    emit_mem(0x39,1,generation,slot,-1,0,offsetof(struct n64emujit_fetch_cache,generation));
    unsigned char* stale=emit_jump(5);
    emit_mem(0x8b,0,result,slot,-1,0,offsetof(struct n64emujit_fetch_cache,cached));
    unsigned char* ready=emit_b();
    patch_jump(other,out);patch_jump(stale,out);
    emit_movimm(pc,address);emit_mapped_cacheability();
    emit_movimm64((intptr_t)config->fetch_cache,slot);
    emit_mem(0x89,0,result,slot,-1,0,offsetof(struct n64emujit_fetch_cache,cached));
    emit_movimm64((intptr_t)config->fetch_generation,generation);
    emit_mem(0x8b,1,generation,generation,-1,0,0);
    emit_mem(0x89,1,generation,slot,-1,0,offsetof(struct n64emujit_fetch_cache,generation));
    emit_mem(0x8b,0,result,state,-1,0,offsetof(struct r4300_core,cp0.regs[CP0_ENTRYHI_REG]));
    emit_imm_op(4,result,255,0);emit_imm_op(1,result,page_tag,0);
    emit_mem(0x89,0,result,slot,-1,0,offsetof(struct n64emujit_fetch_cache,tag));
    emit_mem(0x8b,0,result,slot,-1,0,offsetof(struct n64emujit_fetch_cache,cached));
    patch_jump(ready,out);
}
