/* Xbyak_aarch64 encoding for the shared ParallaxJIT frontend. GPL-2.0-or-later. */
using namespace Xbyak_aarch64;
enum { T0=9,T1=10,T2=11,ADDRESS=12,STATE=19,CLOCKS=20,TARGET=21,
       CACHE3=22,CACHE0=23,CACHE1=24,CACHE2=25,CACHE4=26,CACHE5=27,CACHE6=28,SP=31,
       ARG1_REG=0,ARG2_REG=1,ARG3_REG=2,ARG4_REG=3,MAX_INSTRUCTIONS=32 };
static unsigned char* out;
static uint64_t compile_absolute_calls;
struct arm64_emitter : CodeGenerator {
    std::map<unsigned char*,std::unique_ptr<Label>> jumps;
    arm64_emitter(size_t size,void* memory) : CodeGenerator(size,memory) {}
};
static arm64_emitter* assembler;
struct emitter_binding {
    arm64_emitter* previous;
    emitter_binding(arm64_emitter& code) : previous(assembler) { assembler=&code; }
    ~emitter_binding() { assembler=previous; }
};
#define JIT(...) do { assembler->__VA_ARGS__; out=assembler->getCurr<unsigned char*>(); } while(0)
static void emit_movimm64(int64_t value,unsigned reg)
{
    const uint64_t v=(uint64_t)value;
    JIT(movz(XReg(reg),v&65535));
    for(unsigned i=1;i<4;++i) if((v>>(16*i))&65535) JIT(movk(XReg(reg),(v>>(16*i))&65535,16*i));
}
static void emit_movimm(uint32_t value,unsigned reg) { emit_movimm64(value,reg); }
static void emit_mov64(int source,int dest)
{
    if(source==dest) return;
    if(source==SP || dest==SP) JIT(add(XReg(dest),XReg(source),0));
    else JIT(mov(XReg(dest),XReg(source)));
}
static void emit_mov(int source,int dest) { JIT(mov(WReg(dest),WReg(source))); }
static Cond host_condition(unsigned condition)
{
    static const Cond conditions[16]={VS,VC,LO,HS,EQ,NE,LS,HI,MI,PL,VS,VC,LT,GE,LE,GT};
    assert(condition<16); return conditions[condition];
}
static unsigned char* emit_jump(unsigned condition)
{
    auto site=out; auto label=std::make_unique<Label>();
    JIT(b(host_condition(condition),*label));
    assembler->jumps.emplace(site,std::move(label)); return site;
}
static unsigned char* emit_b()
{
    auto site=out; auto label=std::make_unique<Label>(); JIT(b(*label));
    assembler->jumps.emplace(site,std::move(label)); return site;
}
static void patch_jump(unsigned char* site,unsigned char* target)
{
    const size_t saved=assembler->getSize();
    assembler->setSize(target-assembler->getCode()); assembler->L(*assembler->jumps.at(site)); assembler->setSize(saved);
}
static void emit_set_condition(int reg,unsigned condition) { JIT(cset(XReg(reg),host_condition(condition))); }
static void emit_reg_op(unsigned opcode,int source,int dest,int wide)
{
#define BIN(op) do { if(wide) JIT(op(XReg(dest),XReg(dest),XReg(source))); else JIT(op(WReg(dest),WReg(dest),WReg(source))); } while(0)
    switch(opcode) {
    case 0x01: BIN(adds); break;
    case 0x29: BIN(subs); break;
    case 0x39: if(wide) JIT(cmp(XReg(dest),XReg(source))); else JIT(cmp(WReg(dest),WReg(source))); return;
    case 0x85: if(wide) JIT(tst(XReg(dest),XReg(source))); else JIT(tst(WReg(dest),WReg(source))); return;
    case 0x21: BIN(and_); break;
    case 0x09: BIN(orr); break;
    case 0x31: BIN(eor); break;
    default: abort();
    }
    if(opcode==0x21 || opcode==0x09 || opcode==0x31) {
        if(wide) JIT(tst(XReg(dest),XReg(dest))); else JIT(tst(WReg(dest),WReg(dest)));
    }
#undef BIN
}
static void emit_imm_op(unsigned ext,int reg,uint32_t value,int wide)
{
    emit_movimm64(wide?(int64_t)(int32_t)value:(int64_t)value,15);
    static const unsigned ops[8]={0x01,0x09,0,0,0x21,0x29,0x31,0x39};
    assert(ops[ext]); emit_reg_op(ops[ext],15,reg,wide);
}
static void emit_addimm64(int source,int value,int dest)
{
    emit_movimm64(value,15);
    if(source==dest) JIT(adds(XReg(dest),XReg(source),XReg(15)));
    else JIT(add(XReg(dest),XReg(source),XReg(15)));
}
static void emit_addimm(int source,int value,int dest)
{
    emit_movimm(value,15);
    if(source==dest) JIT(adds(WReg(dest),WReg(source),WReg(15)));
    else JIT(add(WReg(dest),WReg(source),WReg(15)));
}
static void emit_shift_reg(int reg,unsigned kind,unsigned amount,int wide)
{
    if(!amount) return;
#define SHIFT(op) do { if(wide) JIT(op(XReg(reg),XReg(reg),amount)); else JIT(op(WReg(reg),WReg(reg),amount)); } while(0)
    if(kind==4) SHIFT(lsl); else if(kind==7) SHIFT(asr); else SHIFT(lsr);
#undef SHIFT
}
static void emit_address(int base,int index,unsigned scale,size_t offset)
{
    emit_mov64(base,13);
    if(index>=0) JIT(add(XReg(13),XReg(13),XReg(index),LSL,scale));
    if(offset) { emit_movimm64((int64_t)offset,14); JIT(add(XReg(13),XReg(13),XReg(14))); }
}
static void emit_load_at(int reg,int wide,unsigned bytes,int sign)
{
    const auto mem=ptr(XReg(13));
    if(bytes==1) { if(sign) JIT(ldrsb(XReg(reg),mem)); else JIT(ldrb(WReg(reg),mem)); }
    else if(bytes==2) { if(sign) JIT(ldrsh(XReg(reg),mem)); else JIT(ldrh(WReg(reg),mem)); }
    else if(sign) JIT(ldrsw(XReg(reg),mem));
    else if(wide) JIT(ldr(XReg(reg),mem)); else JIT(ldr(WReg(reg),mem));
}
static void emit_store_at(int reg,int wide,unsigned bytes)
{
    const auto mem=ptr(XReg(13));
    if(bytes==1) JIT(strb(WReg(reg),mem)); else if(bytes==2) JIT(strh(WReg(reg),mem));
    else if(wide) JIT(str(XReg(reg),mem)); else JIT(str(WReg(reg),mem));
}
static void emit_mem(unsigned opcode,int wide,int reg,int base,int index,unsigned scale,size_t offset)
{
    emit_address(base,index,scale,offset);
    switch(opcode) {
    case 0x8b: emit_load_at(reg,wide,wide?8:4,0); break;
    case 0x63: emit_load_at(reg,1,4,1); break;
    case 0x89: emit_store_at(reg,wide,wide?8:4); break;
    case 0x88: emit_store_at(reg,0,1); break;
    case 0x8d: emit_mov64(13,reg); break;
    case 0x39: case 0x3b: case 0x38:
        emit_load_at(14,wide,opcode==0x38?1:wide?8:4,0);
        emit_reg_op(0x39,opcode==0x3b?14:reg,opcode==0x3b?reg:14,wide); break;
    case 0x01: case 0x09:
        emit_load_at(14,wide,wide?8:4,0); emit_reg_op(opcode,reg,14,wide); emit_store_at(14,wide,wide?8:4); break;
    case 0xff: emit_load_at(16,1,8,0); JIT(br(XReg(16))); break;
    default: abort();
    }
}
static void emit_mem_imm(unsigned opcode,int wide,int ext,int base,int index,unsigned scale,size_t offset,uint32_t value)
{
    emit_address(base,index,scale,offset);
    if(opcode==0xc7) { emit_movimm64(wide?(int64_t)(int32_t)value:value,14); emit_store_at(14,wide,wide?8:4); return; }
    emit_load_at(14,wide,opcode==0x80?1:wide?8:4,0);
    if(opcode==0xf7) { emit_movimm64(wide?(int64_t)(int32_t)value:value,15); emit_reg_op(0x85,15,14,wide); return; }
    emit_imm_op(ext,14,value,wide);
    if(ext!=7) emit_store_at(14,wide,opcode==0x80?1:wide?8:4);
}
static void emit_state_imm(size_t offset,uint32_t value,int wide) { emit_mem_imm(0xc7,wide,0,STATE,-1,0,offset,value); }
static size_t gpr_offset(unsigned reg) { return offsetof(struct r4300_core,regs)+reg*8; }
static void emit_state_load_raw(size_t offset,int reg) { emit_mem(0x8b,1,reg,STATE,-1,0,offset); }
static void emit_state_store_raw(size_t offset,int reg) { emit_mem(0x89,1,reg,STATE,-1,0,offset); }
static void emit_sign_extend_word() { JIT(sxtw(XReg(T0),WReg(T0))); }
static void emit_helper_call(const void* function)
{
    emit_movimm64((intptr_t)function,16); JIT(blr(XReg(16))); emit_mov64(0,T0); ++compile_absolute_calls;
}
static void emit_bit_test(int base,int index)
{
    emit_mov64(index,15); emit_shift_reg(15,5,6,1);
    emit_address(base,15,3,0); emit_load_at(14,1,8,0);
    JIT(lsr(XReg(14),XReg(14),XReg(index))); emit_imm_op(4,14,1,1);
    JIT(cmp(assembler->xzr,XReg(14)));
}
static void emit_frame_enter()
{
    JIT(hint(34)); /* BTI c */
    JIT(sub(assembler->sp,assembler->sp,160));
    JIT(stp(XReg(29),XReg(30),ptr(assembler->sp,144)));
    JIT(add(XReg(29),assembler->sp,144));
    for(unsigned i=0;i<5;++i) JIT(stp(XReg(19+2*i),XReg(20+2*i),ptr(assembler->sp,(int)(64+16*i))));
}
static void emit_frame_leave()
{
    for(unsigned i=0;i<5;++i) JIT(ldp(XReg(19+2*i),XReg(20+2*i),ptr(assembler->sp,(int)(64+16*i))));
    JIT(ldp(XReg(29),XReg(30),ptr(assembler->sp,144)));
    JIT(add(assembler->sp,assembler->sp,160));
}
