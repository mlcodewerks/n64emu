#include "rsp_interpreter.hpp"
#include "rsp_step.hpp"

namespace RSP {
ReturnMode Interpreter::run(uint32_t limit) {
    state.cycle_limit = limit;
    if (*state.cp0.cr[CP0_REGISTER_SP_STATUS] & SP_STATUS_SSTEP) {
        invalidate_imem();
        return single_step(state, timed);
    }
    do {
        /* A CP0 poll can shorten the slice. Let its consumers finish before
         * returning to the scheduler, as the JIT's block-boundary guard does. */
        bool poll_boundary = state.cycle_limit == limit;
        if (!poll_boundary) {
            poll_boundary = true;
            for (const auto &stage : pipeline.previous)
                if (stage.rw || stage.vw) poll_boundary = false;
        }
        if (!state.has_delay_slot &&
            (state.cycles >= limit ||
             (state.cycles >= state.cycle_limit && poll_boundary)))
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
        auto mode = execute_instruction(state, op);
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
