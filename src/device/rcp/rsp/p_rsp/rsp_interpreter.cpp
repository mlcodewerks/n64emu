#include "rsp_interpreter.hpp"
#include "rsp/quantize.hpp"
#include <array>

namespace RSP {
namespace {
using VectorOp = void (*)(CPUState *, unsigned, unsigned, unsigned, unsigned);
const auto vectors = [] {
    std::array<VectorOp, 64> ops{};
    ops.fill(RSP_RESERVED);
#define OP(i, n) ops[i] = RSP_##n
    OP(0x00, VMULF);
    OP(0x01, VMULU);
    OP(0x04, VMUDL);
    OP(0x05, VMUDM);
    OP(0x06, VMUDN);
    OP(0x07, VMUDH);
    OP(0x08, VMACF);
    OP(0x09, VMACU);
    OP(0x0c, VMADL);
    OP(0x0d, VMADM);
    OP(0x0e, VMADN);
    OP(0x0f, VMADH);
    OP(0x10, VADD);
    OP(0x11, VSUB);
    OP(0x13, VABS);
    OP(0x14, VADDC);
    OP(0x15, VSUBC);
    OP(0x1d, VSAR);
    OP(0x20, VLT);
    OP(0x21, VEQ);
    OP(0x22, VNE);
    OP(0x23, VGE);
    OP(0x24, VCL);
    OP(0x25, VCH);
    OP(0x26, VCR);
    OP(0x27, VMRG);
    OP(0x28, VAND);
    OP(0x29, VNAND);
    OP(0x2a, VOR);
    OP(0x2b, VNOR);
    OP(0x2c, VXOR);
    OP(0x2d, VNXOR);
    OP(0x30, VRCP);
    OP(0x31, VRCPL);
    OP(0x32, VRCPH);
    OP(0x33, VMOV);
    OP(0x34, VRSQ);
    OP(0x35, VRSQL);
    OP(0x36, VRSQH);
#undef OP
    return ops;
}();
using MemoryOp = void (*)(CPUState *, unsigned, unsigned, int, unsigned);
const MemoryOp loads[] = {RSP_LBV, RSP_LSV, RSP_LLV, RSP_LDV, RSP_LQV, RSP_LRV,
                          RSP_LPV, RSP_LUV, RSP_LHV, RSP_LFV, nullptr, RSP_LTV};
const MemoryOp stores[] = {RSP_SBV, RSP_SSV, RSP_SLV, RSP_SDV, RSP_SQV, RSP_SRV,
                           RSP_SPV, RSP_SUV, RSP_SHV, RSP_SFV, RSP_SWV, RSP_STV};
} // namespace
ReturnMode Interpreter::step(uint32_t op) {
    auto &s = state;
    auto *r = s.sr;
    const unsigned rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31,
                   sa = (op >> 6) & 31, fn = op & 63, major = op >> 26;
    const uint32_t pc = s.pc & 0xffc, next = s.has_delay_slot ? s.branch_target : (pc + 4) & 0xffc;
    s.has_delay_slot = 0;
    auto branch = [&](bool take, uint32_t target) {
        if (take) {
            s.has_delay_slot = 1;
            s.branch_target = target & 0xffc;
        }
    };
    auto byte = [&](uint32_t address) {
        return reinterpret_cast<uint8_t *>(s.dmem)[(address & 4095) ^ 3];
    };
    auto load = [&](uint32_t address, unsigned count) {
        uint32_t v = 0;
        for (unsigned i = 0; i < count; ++i)
            v = (v << 8) | byte(address + i);
        return v;
    };
    auto store = [&](uint32_t address, unsigned count, uint32_t value) {
        for (unsigned i = 0; i < count; ++i)
            reinterpret_cast<uint8_t *>(s.dmem)[((address + i) & 4095) ^ 3] =
                uint8_t(value >> ((count - 1 - i) * 8));
    };
    ReturnMode mode = MODE_CONTINUE;
    if ((op >> 25) == 0x25) {
        if (quantize_opcode(fn))
            execute_quantize(&s, op);
        else if (fn != 0x37 && fn != 0x3f)
            vectors[fn](&s, sa, rd, rt, rs & 15);
    } else
        switch (major) {
        case 0:
            switch (fn) {
            case 0:
                r[rd] = r[rt] << sa;
                break;
            case 2:
                r[rd] = r[rt] >> sa;
                break;
            case 3:
                r[rd] = uint32_t(int32_t(r[rt]) >> sa);
                break;
            case 4:
                r[rd] = r[rt] << (r[rs] & 31);
                break;
            case 6:
                r[rd] = r[rt] >> (r[rs] & 31);
                break;
            case 7:
                r[rd] = uint32_t(int32_t(r[rt]) >> (r[rs] & 31));
                break;
            case 8:
                branch(true, r[rs]);
                break;
            case 9: {
                const auto target = r[rs];
                r[rd] = (pc + 8) & 0xfff;
                branch(true, target);
                break;
            }
            case 13:
                mode = MODE_BREAK;
                break;
            case 32:
            case 33:
                r[rd] = r[rs] + r[rt];
                break;
            case 34:
            case 35:
                r[rd] = r[rs] - r[rt];
                break;
            case 36:
                r[rd] = r[rs] & r[rt];
                break;
            case 37:
                r[rd] = r[rs] | r[rt];
                break;
            case 38:
                r[rd] = r[rs] ^ r[rt];
                break;
            case 39:
                r[rd] = ~(r[rs] | r[rt]);
                break;
            case 42:
                r[rd] = int32_t(r[rs]) < int32_t(r[rt]);
                break;
            case 43:
                r[rd] = r[rs] < r[rt];
                break;
            default:
                break;
            }
            break;
        case 1: {
            const auto value = int32_t(r[rs]);
            if (rt == 0 || rt == 1 || rt == 16 || rt == 17) {
                if (rt & 16)
                    r[31] = (pc + 8) & 0xfff;
                branch(rt & 1 ? value >= 0 : value < 0,
                       pc + 4 + uint32_t(int32_t(int16_t(op)) * 4));
            }
            break;
        }
        case 2:
            branch(true, op << 2);
            break;
        case 3:
            r[31] = (pc + 8) & 0xfff;
            branch(true, op << 2);
            break;
        case 4:
            branch(r[rs] == r[rt], pc + 4 + uint32_t(int32_t(int16_t(op)) * 4));
            break;
        case 5:
            branch(r[rs] != r[rt], pc + 4 + uint32_t(int32_t(int16_t(op)) * 4));
            break;
        case 6:
            branch(int32_t(r[rs]) <= 0, pc + 4 + uint32_t(int32_t(int16_t(op)) * 4));
            break;
        case 7:
            branch(int32_t(r[rs]) > 0, pc + 4 + uint32_t(int32_t(int16_t(op)) * 4));
            break;
        case 8:
        case 9:
            r[rt] = r[rs] + uint32_t(int32_t(int16_t(op)));
            break;
        case 10:
            r[rt] = int32_t(r[rs]) < int32_t(int16_t(op));
            break;
        case 11:
            r[rt] = r[rs] < uint32_t(int32_t(int16_t(op)));
            break;
        case 12:
            r[rt] = r[rs] & uint16_t(op);
            break;
        case 13:
            r[rt] = r[rs] | uint16_t(op);
            break;
        case 14:
            r[rt] = r[rs] ^ uint16_t(op);
            break;
        case 15:
            r[rt] = op << 16;
            break;
        case 16:
            if (rs == 0)
                mode = ReturnMode(RSP_MFC0(&s, rt, rd));
            else if (rs == 4)
                mode = ReturnMode(RSP_MTC0(&s, rd, rt));
            break;
        case 18:
            switch (rs) {
            case 0:
                RSP_MFC2(&s, rt, rd, (op >> 7) & 15);
                break;
            case 2:
                RSP_CFC2(&s, rt, rd);
                break;
            case 4:
                RSP_MTC2(&s, rt, rd, (op >> 7) & 15);
                break;
            case 6:
                RSP_CTC2(&s, rt, rd);
                break;
            }
            break;
        case 32:
            r[rt] = uint32_t(int32_t(int8_t(load(r[rs] + int16_t(op), 1))));
            break;
        case 33:
            r[rt] = uint32_t(int32_t(int16_t(load(r[rs] + int16_t(op), 2))));
            break;
        case 35:
        case 39:
            r[rt] = load(r[rs] + int16_t(op), 4);
            break;
        case 36:
            r[rt] = load(r[rs] + int16_t(op), 1);
            break;
        case 37:
            r[rt] = load(r[rs] + int16_t(op), 2);
            break;
        case 40:
            store(r[rs] + int16_t(op), 1, r[rt]);
            break;
        case 41:
            store(r[rs] + int16_t(op), 2, r[rt]);
            break;
        case 43:
            store(r[rs] + int16_t(op), 4, r[rt]);
            break;
        case 50:
        case 58: {
            unsigned type = (op >> 11) & 31;
            auto table = major == 50 ? loads : stores;
            if (type < 12 && table[type])
                table[type](&s, rt, (op >> 7) & 15, int(op & 127) - (op & 64 ? 128 : 0), rs);
            break;
        }
        default:
            break;
        }
    r[0] = 0;
    s.pc = next;
    return mode;
}
ReturnMode Interpreter::run(uint32_t limit) {
    state.cycle_limit = limit;
    do {
        if (state.cycles >= limit && !state.has_delay_slot)
            return MODE_TIMESLICE;
        const auto pc = state.pc & 0xffc, op = state.imem[pc >> 2];
        auto decoded = RSPTimingDecoder::decoderEXECUTE(op);
        if (!timed)
            ++state.cycles;
        else if (paired)
            paired = false;
        else {
            auto second = RSPTimingDecoder::decoderEXECUTE(state.imem[((pc + 4) & 0xffc) >> 2]);
            paired = !pipeline.single && !state.has_delay_slot && !decoded.branch() &&
                     !decoded.mayHalt() && !decoded.endBlock() &&
                     RSPTimingDecoder::canDualIssue(decoded, second);
            state.cycles += pipeline.issue(decoded, paired ? &second : nullptr);
        }
        const bool prior_branch = state.has_delay_slot;
        auto mode = step(op);
        if (timed && prior_branch) {
            ++state.cycles;
            pipeline = {};
            paired = false;
        }
        if (mode == MODE_BREAK) {
            auto &status = *state.cp0.cr[CP0_REGISTER_SP_STATUS];
            status |= SP_STATUS_HALT | SP_STATUS_BROKE;
            if (status & SP_STATUS_INTR_BREAK)
                *state.cp0.irq |= 1;
        }
        if (mode != MODE_CONTINUE) {
            paired = false;
            return mode;
        }
    } while (true);
}
} // namespace RSP
