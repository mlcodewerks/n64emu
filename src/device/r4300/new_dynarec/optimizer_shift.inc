static int regcache_shift(uint32_t instruction)
{
    auto op=r4300_get_idec(instruction)->opcode;
    bool word=false,variable=false;unsigned kind=0;
    switch(op) {
    case R4300_OP_SLL:word=true;kind=4;break;case R4300_OP_SRL:word=true;kind=5;break;case R4300_OP_SRA:word=true;kind=7;break;
    case R4300_OP_SLLV:word=variable=true;kind=4;break;case R4300_OP_SRLV:word=variable=true;kind=5;break;case R4300_OP_SRAV:word=variable=true;kind=7;break;
    case R4300_OP_DSLL:case R4300_OP_DSLL32:kind=4;break;case R4300_OP_DSRL:case R4300_OP_DSRL32:kind=5;break;case R4300_OP_DSRA:case R4300_OP_DSRA32:kind=7;break;
    case R4300_OP_DSLLV:variable=true;kind=4;break;case R4300_OP_DSRLV:variable=true;kind=5;break;case R4300_OP_DSRAV:variable=true;kind=7;break;
    default:return 0;
    }
    unsigned a,b,d;guest_operands(instruction,&a,&b,&d);
    unsigned source=variable?b:a,amount=(instruction>>6)&31;
    if(op==R4300_OP_DSLL32||op==R4300_OP_DSRL32||op==R4300_OP_DSRA32)amount+=32;
    uint64_t count=0;
    if(variable && known_value(a,count)){amount=unsigned(count)&(word?31:63);variable=false;}
    if(variable)emit_state_load(gpr_offset(a),SHIFT_COUNT);
    int left=allocated_source(source,SHIFT_TEMP),dest=allocated_destination(d);
    if(!variable && !amount && word){emit_word_copy(left,dest);return 1;}
    if(left!=dest)emit_mov64(left,dest);
    // R4300 word arithmetic shifts deliberately consume the full 64-bit
    // operand before sign-extending the low result, matching the interpreter.
    int wide=!word || kind==7;
    if(variable)emit_allocated_variable_shift(dest,kind,wide,word);
    else emit_shift_reg(dest,kind,amount,wide);
    if(word)emit_word_extend(dest);
    return 1;
}
