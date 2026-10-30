/* Preserve both architectural results, including noncanonical MULT/DIV word
 * operands. These reductions affect host work only; guest latency is charged
 * by the enclosing native region exactly as for the original instruction. */
static uint64_t multiply_high(uint64_t x, uint64_t y)
{
    uint64_t low = uint64_t(uint32_t(x)) * uint32_t(y);
    uint64_t middle = (x >> 32) * uint32_t(y) + (low >> 32);
    uint64_t carry = middle >> 32;
    middle = uint32_t(middle) + uint64_t(uint32_t(x)) * (y >> 32);
    return (x >> 32) * (y >> 32) + carry + (middle >> 32);
}
static int regcache_muldiv(uint32_t instruction, enum r4300_opcode op)
{
    bool word, sign, multiply;
    switch (op) {
    case R4300_OP_MULT: word = sign = multiply = true; break;
    case R4300_OP_MULTU: word = multiply = true; sign = false; break;
    case R4300_OP_DMULT: sign = multiply = true; word = false; break;
    case R4300_OP_DMULTU: multiply = true; word = sign = false; break;
    case R4300_OP_DIV: word = sign = true; multiply = false; break;
    case R4300_OP_DIVU: word = true; sign = multiply = false; break;
    case R4300_OP_DDIV: sign = true; word = multiply = false; break;
    case R4300_OP_DDIVU: word = sign = multiply = false; break;
    default: return 0;
    }
    unsigned a = (instruction >> 21) & 31, b = (instruction >> 16) & 31;
    uint64_t x = 0, y = 0, lo = 0, hi = 0;
    bool ka = known_value(a, x), kb = known_value(b, y);
    if (word) {
        if (multiply && sign) y = uint64_t(int64_t(y << 29) >> 29);
        else if (!sign) { x = uint32_t(x); y = uint32_t(y); }
        else x = word_signed(x);
    }
    if (ka && kb) {
        if (multiply) {
            lo = x * y;
            hi = word ? lo >> 32 : multiply_high(x, y);
            if (!word && sign) {
                if (int64_t(x) < 0) hi -= y;
                if (int64_t(y) < 0) hi -= x;
            }
        } else if (!y) {
            lo = sign && int64_t(x) < 0 ? 1 : UINT64_MAX;
            hi = x;
        } else if (sign) {
            if (x == (UINT64_C(1) << 63) && y == UINT64_MAX) lo = x;
            else { lo = uint64_t(int64_t(x) / int64_t(y)); hi = uint64_t(int64_t(x) % int64_t(y)); }
        } else { lo = x / y; hi = x % y; }
        if (word) { lo = word_signed(lo); hi = word_signed(hi); }
        regcache_constant(GUEST_LO, lo); regcache_constant(GUEST_HI, hi);
        return 1;
    }
    if (multiply && ((ka && !x) || (kb && !y))) {
        regcache_constant(GUEST_LO, 0); regcache_constant(GUEST_HI, 0); return 1;
    }
    // MULT's signed 35-bit rhs makes swapping its operands invalid.
    if (multiply && !(word && sign) && ka && !kb) {
        unsigned swap = a; a = b; b = swap; y = x; kb = true;
    }
    if (!kb) return 0;
    if (!multiply && sign && (y == 1 || y == UINT64_MAX)) {
        emit_state_load(gpr_offset(a), OPT_TEMP0);
        if (word) emit_word_extend(OPT_TEMP0);
        if (y == UINT64_MAX) {
            emit_movimm(0, OPT_TEMP1);
            emit_reg_op(0x29, OPT_TEMP0, OPT_TEMP1, 1);
            emit_mov64(OPT_TEMP1, OPT_TEMP0);
            if (word) emit_word_extend(OPT_TEMP0);
        }
        emit_state_store(guest_offset(GUEST_LO), OPT_TEMP0);
        regcache_constant(GUEST_HI, 0); return 1;
    }
    if (!y || (y & (y - 1)) || (multiply && sign && int64_t(y) < 0) || (!multiply && sign)) return 0;
    unsigned shift = 0;
    for (uint64_t n = y; n > 1; n >>= 1) ++shift;
    emit_state_load(gpr_offset(a), OPT_TEMP0);
    if (word && !sign) emit_mov(OPT_TEMP0, OPT_TEMP0);
    emit_mov64(OPT_TEMP0, OPT_TEMP2);
    if (multiply) {
        if (shift) emit_shift_reg(OPT_TEMP0, 4, shift, 1);
        if (word) {
            emit_mov64(OPT_TEMP0, OPT_TEMP2); emit_shift_reg(OPT_TEMP2, 5, 32, 1);
        } else if (shift) emit_shift_reg(OPT_TEMP2, sign ? 7 : 5, 64 - shift, 1);
        else if (sign) emit_shift_reg(OPT_TEMP2, 7, 63, 1);
        else emit_movimm(0, OPT_TEMP2);
    } else {
        if (shift) emit_shift_reg(OPT_TEMP0, 5, shift, 1);
        emit_movimm64(y - 1, OPT_TEMP1);
        emit_reg_op(0x21, OPT_TEMP1, OPT_TEMP2, 1);
    }
    if (word) { emit_word_extend(OPT_TEMP0); emit_word_extend(OPT_TEMP2); }
    emit_state_store(guest_offset(GUEST_LO), OPT_TEMP0);
    emit_state_store(guest_offset(GUEST_HI), OPT_TEMP2);
    return 1;
}
