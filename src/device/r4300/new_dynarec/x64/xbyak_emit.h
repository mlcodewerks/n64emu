/* Xbyak encoding for the shared Ari64 frontend. GPL-2.0-or-later. */
static unsigned char* out;
struct x64_emitter : Xbyak::CodeGenerator {
    std::map<unsigned char*, std::unique_ptr<Xbyak::Label>> jumps;
    x64_emitter(size_t size, void* memory) : CodeGenerator(size, memory) {}
};
static x64_emitter* assembler;
struct emitter_binding {
    x64_emitter* previous;
    emitter_binding(x64_emitter& code) : previous(assembler) { assembler=&code; }
    ~emitter_binding() { assembler=previous; }
};
#define JIT(...) do { assembler->__VA_ARGS__; out = assembler->getCurr<unsigned char*>(); } while (0)
static Xbyak::Reg host_reg(int reg, int wide) { return Xbyak::Reg(reg, Xbyak::Operand::REG, wide ? 64 : 32); }
static Xbyak::Reg8 byte_reg(int reg) { return Xbyak::Reg8(reg, reg >= 4); }
static Xbyak::Address host_mem(int bits, int base, int index, unsigned scale, size_t offset)
{
    Xbyak::RegExp address = Xbyak::Reg64(base) + (int32_t)offset;
    if (index >= 0) address = address + Xbyak::Reg64(index) * (1u << scale);
    return Xbyak::AddressFrame(bits)[address];
}
static void emit_mov(int source, int dest) { JIT(mov(Xbyak::Reg32(dest), Xbyak::Reg32(source))); }
static void emit_mov64(int source, int dest) { JIT(mov(Xbyak::Reg64(dest), Xbyak::Reg64(source))); }
static void emit_movimm(int value, unsigned dest) { JIT(mov(Xbyak::Reg32(dest), (uint32_t)value)); }
static void emit_movimm64(int64_t value, unsigned dest) { JIT(mov(Xbyak::Reg64(dest), (uint64_t)value)); }
static void emit_addimm(int source, int value, int dest)
{
    if (source != dest) { if (value) JIT(lea(Xbyak::Reg32(dest), host_mem(0,source,-1,0,value))); else emit_mov(source,dest); }
    else if (value) JIT(add(Xbyak::Reg32(dest), value));
}
static void emit_addimm64(int source, int value, int dest)
{
    if (source != dest) { if (value) JIT(lea(Xbyak::Reg64(dest), host_mem(0,source,-1,0,value))); else emit_mov64(source,dest); }
    else if (value) JIT(add(Xbyak::Reg64(dest), value));
}
static void emit_shift_reg(int reg, unsigned kind, unsigned amount, int wide)
{
    if (!amount) return;
    if (kind == 4) JIT(shl(host_reg(reg,wide), amount));
    else if (kind == 5) JIT(shr(host_reg(reg,wide), amount));
    else { assert(kind == 7); JIT(sar(host_reg(reg,wide), amount)); }
}
static void emit_shlimm(int source,unsigned amount,int dest) { if(source!=dest) emit_mov(source,dest); emit_shift_reg(dest,4,amount,0); }
static void emit_shlimm64(int source,unsigned amount,int dest) { if(source!=dest) emit_mov64(source,dest); emit_shift_reg(dest,4,amount,1); }
static void emit_shrimm(int source,unsigned amount,int dest) { if(source!=dest) emit_mov(source,dest); emit_shift_reg(dest,5,amount,0); }
static void emit_shrimm64(int source,unsigned amount,int dest) { if(source!=dest) emit_mov64(source,dest); emit_shift_reg(dest,5,amount,1); }
static void emit_sarimm(int source,unsigned amount,int dest) { if(source!=dest) emit_mov(source,dest); emit_shift_reg(dest,7,amount,0); }
static void emit_reg_op(unsigned opcode,int source,int dest,int wide)
{
    const Xbyak::Reg rs=host_reg(source,wide), rt=host_reg(dest,wide);
    switch(opcode) {
    case 0x01: JIT(add(rt,rs)); break;
    case 0x09: JIT(or_(rt,rs)); break;
    case 0x21: JIT(and_(rt,rs)); break;
    case 0x29: JIT(sub(rt,rs)); break;
    case 0x31: JIT(xor_(rt,rs)); break;
    case 0x39: JIT(cmp(rt,rs)); break;
    case 0x85: JIT(test(rt,rs)); break;
    default: abort();
    }
}
static void emit_imm_op(unsigned ext,int reg,uint32_t value,int wide)
{
    const Xbyak::Reg rt=host_reg(reg,wide);
    switch(ext) {
    case 0: JIT(add(rt,value)); break;
    case 1: JIT(or_(rt,value)); break;
    case 4: JIT(and_(rt,value)); break;
    case 5: JIT(sub(rt,value)); break;
    case 6: JIT(xor_(rt,value)); break;
    case 7: JIT(cmp(rt,value)); break;
    default: abort();
    }
}
static void emit_mem(unsigned opcode,int wide,int reg,int base,int index,unsigned scale,size_t offset)
{
    const auto mem=host_mem(wide?64:32,base,index,scale,offset);
    const auto rt=host_reg(reg,wide);
    switch(opcode) {
    case 0x8b: JIT(mov(rt,mem)); break;
    case 0x63: JIT(movsxd(Xbyak::Reg64(reg),host_mem(32,base,index,scale,offset))); break;
    case 0x89: JIT(mov(mem,rt)); break;
    case 0x88: JIT(mov(host_mem(8,base,index,scale,offset),byte_reg(reg))); break;
    case 0x8d: JIT(lea(rt,host_mem(0,base,index,scale,offset))); break;
    case 0x39: JIT(cmp(mem,rt)); break;
    case 0x3b: JIT(cmp(rt,mem)); break;
    case 0x38: JIT(cmp(host_mem(8,base,index,scale,offset),byte_reg(reg))); break;
    case 0x01: JIT(add(mem,rt)); break;
    case 0x09: JIT(or_(mem,rt)); break;
    case 0xff: assert(reg==4); JIT(jmp(host_mem(0,base,index,scale,offset))); break;
    default: abort();
    }
}
static void emit_mem_imm(unsigned opcode,int wide,int ext,int base,int index,unsigned scale,size_t offset,uint32_t value)
{
    const auto mem=host_mem(opcode==0x80?8:wide?64:32,base,index,scale,offset);
    if(opcode==0xc7) { JIT(mov(mem,value)); return; }
    if(opcode==0xf7) { JIT(test(mem,value)); return; }
    switch(ext) {
    case 0: JIT(add(mem,value)); break;
    case 1: JIT(or_(mem,value)); break;
    case 4: JIT(and_(mem,value)); break;
    case 5: JIT(sub(mem,value)); break;
    case 6: JIT(xor_(mem,value)); break;
    case 7: JIT(cmp(mem,value)); break;
    default: abort();
    }
}
static void emit_state_imm(size_t offset,uint32_t value,int wide) { emit_mem_imm(0xc7,wide,0,R12,-1,0,offset,value); }
static unsigned char* emit_jump(unsigned condition)
{
    auto site=out;
    auto label=std::make_unique<Xbyak::Label>();
    switch(condition) {
#define COND(n,op) case n: JIT(op(*label,Xbyak::CodeGenerator::T_NEAR)); break
    COND(0,jo); COND(1,jno); COND(2,jb); COND(3,jae);
    COND(4,je); COND(5,jne); COND(6,jbe); COND(7,ja);
    COND(8,js); COND(9,jns); COND(10,jp); COND(11,jnp);
    COND(12,jl); COND(13,jge); COND(14,jle); COND(15,jg);
#undef COND
    default: abort();
    }
    assembler->jumps.emplace(site,std::move(label));
    return site;
}
static unsigned char* emit_b()
{
    auto site=out;
    auto label=std::make_unique<Xbyak::Label>();
    JIT(jmp(*label,Xbyak::CodeGenerator::T_NEAR));
    assembler->jumps.emplace(site,std::move(label));
    return site;
}
static void patch_jump(unsigned char* site,unsigned char* target)
{
    const size_t saved=assembler->getSize();
    assembler->setSize(target-assembler->getCode());
    assembler->L(*assembler->jumps.at(site));
    assembler->setSize(saved);
}
static void emit_set_condition(int reg,unsigned condition)
{
    const auto dest=byte_reg(reg);
    switch(condition) {
#define COND(n,op) case n: JIT(op(dest)); break
    COND(0,seto); COND(1,setno); COND(2,setb); COND(3,setae);
    COND(4,sete); COND(5,setne); COND(6,setbe); COND(7,seta);
    COND(8,sets); COND(9,setns); COND(10,setp); COND(11,setnp);
    COND(12,setl); COND(13,setge); COND(14,setle); COND(15,setg);
#undef COND
    default: abort();
    }
    JIT(movzx(Xbyak::Reg32(reg),dest));
}
