/* Host-independent facts for one guarded native region. No fact crosses a
 * helper boundary. Memory side exits snapshot this state before the access. */
static void regcache_forget(unsigned guest)
{
    regcache.values[guest] = ++regcache.serial;
    regcache.signed_words &= ~(UINT64_C(1) << guest);
}
static int regcache_constant(unsigned guest, uint64_t value)
{
    int h = cache_find(guest);
    if (h >= 0) { regcache.map[h] = -1; regcache.dirty &= ~(1u << h); }
    regcache.known |= UINT64_C(1) << guest;
    regcache.constant_dirty |= UINT64_C(1) << guest;
    regcache.constants[guest] = value;
    regcache_forget(guest);
    for (unsigned g = 0; g < GUEST_REGS; ++g)
        if (g != guest && (regcache.known & (UINT64_C(1) << g)) && regcache.constants[g] == value) {
            regcache.values[guest] = regcache.values[g]; break;
        }
    if (value == word_signed(value)) regcache.signed_words |= UINT64_C(1) << guest;
    return 1;
}
static bool signed_word(unsigned guest)
{
    return (regcache.signed_words & (UINT64_C(1) << guest)) != 0;
}
static bool known_value(unsigned guest, uint64_t &value)
{
    if (!(regcache.known & (UINT64_C(1) << guest))) return false;
    value = regcache.constants[guest]; return true;
}
/* Returns a source for an exact 64-bit identity, or -1. A word-sized add or
 * shift by zero is an identity only for a proven sign-extended operand. */
static int identity_source(uint32_t instruction)
{
    unsigned a,b,d; guest_operands(instruction,&a,&b,&d);
    auto op = r4300_get_idec(instruction)->opcode;
    uint64_t x=0,y=0; bool ka=known_value(a,x),kb=known_value(b,y);
    if((op==R4300_OP_OR || op==R4300_OP_AND) && regcache.values[a]==regcache.values[b])return a;
    switch(op) {
    case R4300_OP_MFHI: case R4300_OP_MFLO: case R4300_OP_MTHI: case R4300_OP_MTLO: return a;
    case R4300_OP_DADDIU: case R4300_OP_ORI: case R4300_OP_XORI: if (!(instruction&65535)) return a; break;
    case R4300_OP_ADDIU: if (!(instruction&65535) && signed_word(a)) return a; break;
    case R4300_OP_DADDU: case R4300_OP_OR: case R4300_OP_XOR:
        if(kb && !y)return a;if(ka && !x)return b;break;
    case R4300_OP_ADDU:
        if(kb && !y && signed_word(a))return a;if(ka && !x && signed_word(b))return b;break;
    case R4300_OP_DSUBU: if(kb && !y)return a;break;
    case R4300_OP_SUBU: if(kb && !y && signed_word(a))return a;break;
    case R4300_OP_AND:
        if(kb && y==UINT64_MAX)return a;if(ka && x==UINT64_MAX)return b;
        if(regcache.values[a]==regcache.values[b])return a;break;
    default:break;
    }
    return -1;
}
static bool folded_branch(enum r4300_opcode op,unsigned rs,unsigned rt,bool &taken)
{
    uint64_t x=0,y=0;bool ka=known_value(rs,x),kb=known_value(rt,y);
    switch(op) {
    case R4300_OP_BEQ:case R4300_OP_BEQL:case R4300_OP_BNE:case R4300_OP_BNEL:
        if(regcache.values[rs]!=regcache.values[rt] && (!ka||!kb))return false;
        taken=regcache.values[rs]==regcache.values[rt] || x==y;
        if(op==R4300_OP_BNE||op==R4300_OP_BNEL)taken=!taken;
        return true;
    case R4300_OP_BLEZ:case R4300_OP_BLEZL:if(!ka)return false;taken=int64_t(x)<=0;return true;
    case R4300_OP_BGTZ:case R4300_OP_BGTZL:if(!ka)return false;taken=int64_t(x)>0;return true;
    case R4300_OP_BLTZ:case R4300_OP_BLTZL:case R4300_OP_BLTZAL:case R4300_OP_BLTZALL:
        if(!ka)return false;taken=int64_t(x)<0;return true;
    case R4300_OP_BGEZ:case R4300_OP_BGEZL:case R4300_OP_BGEZAL:case R4300_OP_BGEZALL:
        if(!ka)return false;taken=int64_t(x)>=0;return true;
    default:return false;
    }
}
static int regcache_absorb(uint32_t instruction)
{
    unsigned a,b,d; guest_operands(instruction,&a,&b,&d);
    auto op=r4300_get_idec(instruction)->opcode;
    if(!d)return 0;
    uint64_t x=0,y=0;bool ka=known_value(a,x),kb=known_value(b,y);
    bool equal=regcache.values[a]==regcache.values[b];
    switch(op) {
    case R4300_OP_XOR: case R4300_OP_SUBU: case R4300_OP_DSUBU:
    case R4300_OP_SLT: case R4300_OP_SLTU: if(equal)return regcache_constant(d,0);break;
    case R4300_OP_AND: if((ka&&!x)||(kb&&!y))return regcache_constant(d,0);break;
    case R4300_OP_OR: if((ka&&x==UINT64_MAX)||(kb&&y==UINT64_MAX))return regcache_constant(d,UINT64_MAX);break;
    case R4300_OP_ANDI: case R4300_OP_SLTIU: if(!(instruction&65535))return regcache_constant(d,0);break;
    case R4300_OP_SLLV: case R4300_OP_SRLV: case R4300_OP_SRAV:
    case R4300_OP_DSLLV: case R4300_OP_DSRLV: case R4300_OP_DSRAV:
        if(kb&&!y)return regcache_constant(d,0);break;
    default:break;
    }
    return 0;
}
