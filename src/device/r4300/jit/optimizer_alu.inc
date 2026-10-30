/* Pure ALU value numbering. Loads, stores, traps and helpers are never CSE'd. */
static int pure_alu(enum r4300_opcode op)
{
    switch(op) {
    case R4300_OP_ADDIU:case R4300_OP_DADDIU:case R4300_OP_ADDU:case R4300_OP_DADDU:
    case R4300_OP_SUBU:case R4300_OP_DSUBU:case R4300_OP_LUI:
    case R4300_OP_AND:case R4300_OP_ANDI:case R4300_OP_OR:case R4300_OP_ORI:
    case R4300_OP_XOR:case R4300_OP_XORI:case R4300_OP_NOR:
    case R4300_OP_SLT:case R4300_OP_SLTU:case R4300_OP_SLTI:case R4300_OP_SLTIU:
    case R4300_OP_SLL:case R4300_OP_SLLV:case R4300_OP_SRL:case R4300_OP_SRLV:case R4300_OP_SRA:case R4300_OP_SRAV:
    case R4300_OP_DSLL:case R4300_OP_DSLL32:case R4300_OP_DSLLV:case R4300_OP_DSRL:case R4300_OP_DSRL32:case R4300_OP_DSRLV:
    case R4300_OP_DSRA:case R4300_OP_DSRA32:case R4300_OP_DSRAV:
    case R4300_OP_MFHI:case R4300_OP_MFLO:case R4300_OP_MTHI:case R4300_OP_MTLO:return 1;
    default:return 0;
    }
}
static int copy_value(unsigned destination,unsigned source)
{
    if(destination==source)return 1;
    uint64_t value;
    if(known_value(source,value))return regcache_constant(destination,value);
    unsigned id=regcache.values[source];bool word=signed_word(source);
    int left=allocated_source(source,OPT_TEMP0),dest=allocated_destination(destination);
    if(left!=dest)emit_mov64(left,dest);
    regcache.values[destination]=id;
    if(word)regcache.signed_words|=UINT64_C(1)<<destination;
    return 1;
}
static bool commutative_alu(enum r4300_opcode op)
{
    return op==R4300_OP_ADDU || op==R4300_OP_DADDU || op==R4300_OP_AND ||
           op==R4300_OP_OR || op==R4300_OP_NOR || op==R4300_OP_XOR;
}
#include "optimizer_muldiv.inc"
static int regcache_optimize(uint32_t instruction)
{
    auto op=r4300_get_idec(instruction)->opcode;
    if(regcache_muldiv(instruction,op))return 1;
    if(!pure_alu(op))return regcache_fold(instruction) || regcache_direct(instruction);
    unsigned a,b,d;guest_operands(instruction,&a,&b,&d);
    if(!d)return 1;
    // Preserve every pre-access state in regions with memory exits and every
    // pre-delay state in likely branches. Their snapshots observe dead values.
    if(regcache.allow_dead_store && (analysis.next[d][regcache.index+1]&0x80000000u))return 1;
    // A constant result needs no expression-history lookup or insertion.
    if(regcache_fold(instruction))return 1;
    if(regcache_absorb(instruction))return 1;
    int identity=identity_source(instruction);
    if(identity>=0)return copy_value(d,unsigned(identity));
    unsigned x=regcache.values[a],y=regcache.values[b];
    if(commutative_alu(op) && x>y){unsigned t=x;x=y;y=t;}
    unsigned major=instruction>>26;
    unsigned imm=major?instruction&65535:(instruction>>6)&31;
    for(unsigned i=0;i<analysis.expression_count;i++) {
        const auto &e=analysis.expressions[i];
        if(e.op!=unsigned(op)||e.a!=x||e.b!=y||e.immediate!=imm)continue;
        for(unsigned g=1;g<GUEST_REGS;g++)if(regcache.values[g]==e.value)return copy_value(d,g);
    }
    bool wa=signed_word(a),wb=signed_word(b);
    if(!regcache_direct(instruction))return 0;
    switch(op) {
    case R4300_OP_ADDIU:case R4300_OP_ADDU:case R4300_OP_SUBU:case R4300_OP_ANDI:
    case R4300_OP_SLT:case R4300_OP_SLTU:case R4300_OP_SLTI:case R4300_OP_SLTIU:
    case R4300_OP_SLL:case R4300_OP_SLLV:case R4300_OP_SRL:case R4300_OP_SRLV:case R4300_OP_SRA:case R4300_OP_SRAV:
        regcache.signed_words|=UINT64_C(1)<<d;break;
    case R4300_OP_ORI:case R4300_OP_XORI:if(wa)regcache.signed_words|=UINT64_C(1)<<d;break;
    case R4300_OP_AND:case R4300_OP_OR:case R4300_OP_XOR:case R4300_OP_NOR:
        if(wa&&wb)regcache.signed_words|=UINT64_C(1)<<d;break;
    default:break;
    }
    assert(analysis.expression_count<MAX_INSTRUCTIONS);
    analysis.expressions[analysis.expression_count++]={unsigned(op),x,y,imm,regcache.values[d]};
    return 1;
}
