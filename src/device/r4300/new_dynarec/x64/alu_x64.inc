
static int allocated_source(unsigned guest, int temporary)
{
    if (regcache.known & (UINT64_C(1) << guest)) {
        emit_movimm64(regcache.constants[guest],temporary); return temporary;
    }
    int h = cache_find(guest);
    if (h < 0) { h = cache_allocate(guest); emit_state_load_raw(guest_offset(guest),cache_host[h]); }
    return cache_host[h];
}
static int allocated_destination(unsigned guest)
{
    int h = cache_allocate(guest);
    regcache_forget(guest);
    regcache.known &= ~(UINT64_C(1) << guest); regcache.constant_dirty &= ~(UINT64_C(1) << guest);
    regcache.dirty |= 1u << h;
    return cache_host[h];
}
static void emit_word_extend(int reg)
{
    JIT(movsxd(Xbyak::Reg64(reg),Xbyak::Reg32(reg)));
}
static void emit_word_copy(int source,int dest) { JIT(movsxd(Xbyak::Reg64(dest),Xbyak::Reg32(source))); }
static void emit_allocated_variable_shift(int dest,unsigned kind,int wide,bool word) {
    if(word && wide)emit_imm_op(4,RCX,31,0);
    if(kind==4)JIT(shl(host_reg(dest,wide),assembler->cl));
    else if(kind==5)JIT(shr(host_reg(dest,wide),assembler->cl));
    else JIT(sar(host_reg(dest,wide),assembler->cl));
}
#define SHIFT_COUNT RCX
#define SHIFT_TEMP RAX
#include "../optimizer_shift.inc"
#undef SHIFT_COUNT
#undef SHIFT_TEMP
static int regcache_direct(uint32_t instruction)
{
    if(regcache_shift(instruction))return 1;
    enum r4300_opcode op = r4300_get_idec(instruction)->opcode;
    unsigned a,b,d; guest_operands(instruction,&a,&b,&d);
    int word = 0, immediate = 0, compare = 0;
    unsigned opcode = 0, extension = 0;
    switch (op) {
    case R4300_OP_ADDIU: case R4300_OP_DADDIU: immediate = 1; extension = 0; word = op == R4300_OP_ADDIU; break;
    case R4300_OP_ANDI: immediate = 1; extension = 4; break;
    case R4300_OP_ORI: immediate = 1; extension = 1; break;
    case R4300_OP_XORI: immediate = 1; extension = 6; break;
    case R4300_OP_ADDU: case R4300_OP_DADDU: opcode = 0x01; word = op == R4300_OP_ADDU; break;
    case R4300_OP_SUBU: case R4300_OP_DSUBU: opcode = 0x29; word = op == R4300_OP_SUBU; break;
    case R4300_OP_AND: opcode = 0x21; break;
    case R4300_OP_OR: case R4300_OP_NOR: opcode = 0x09; break;
    case R4300_OP_XOR: opcode = 0x31; break;
    case R4300_OP_SLT: case R4300_OP_SLTU: opcode = 0x39; compare = 1; break;
    case R4300_OP_SLTI: case R4300_OP_SLTIU: immediate = compare = 1; extension = 7; break;
    default: return 0;
    }
    if (!d) return 1;
    uint64_t constant=0;bool narrow_and=false;
    if(!immediate && known_value(a,constant) &&
       (op==R4300_OP_ADDU||op==R4300_OP_DADDU||op==R4300_OP_AND||op==R4300_OP_OR||op==R4300_OP_NOR||op==R4300_OP_XOR)) {
        unsigned swap=a;a=b;b=swap;
    }
    if(!immediate && known_value(b,constant) &&
       (word || constant==uint64_t(int64_t(int32_t(constant))) || (op==R4300_OP_AND && constant<=UINT32_MAX))) {
        immediate=2;
        extension=opcode==0x01?0:opcode==0x29?5:opcode==0x21?4:opcode==0x09?1:opcode==0x31?6:7;
        narrow_and=op==R4300_OP_AND && constant<=UINT32_MAX;
    }
    int left=allocated_source(a,RAX),right=immediate?-1:allocated_source(b,RCX);
    if (compare) {
        if (immediate) emit_imm_op(extension,left,immediate==2?uint32_t(constant):(uint32_t)(int32_t)(int16_t)instruction,1);
        else emit_reg_op(opcode,right,left,1);
        int dest = allocated_destination(d);
        emit_set_condition(dest,op == R4300_OP_SLT || op == R4300_OP_SLTI ? 12 : 2);
        return 1;
    }
    int dest = allocated_destination(d);
    if (!immediate && right == dest && left != dest) {
        if (opcode != 0x29) { int saved = left; left = right; right = saved; }
        else { emit_mov64(right,RCX); right = RCX; }
    }
    if (left != dest) emit_mov64(left,dest);
    if (immediate) {
        uint32_t imm = immediate==2?uint32_t(constant):op == R4300_OP_ADDIU || op == R4300_OP_DADDIU ? (uint32_t)(int32_t)(int16_t)instruction : (uint16_t)instruction;
        if (imm || extension==4) emit_imm_op(extension,dest,imm,!word && op != R4300_OP_ANDI && !narrow_and);
    } else emit_reg_op(opcode,right,dest,!word);
    if (op == R4300_OP_NOR) { JIT(not_(Xbyak::Reg64(dest))); }
    if (word) emit_word_extend(dest);
    return 1;
}
