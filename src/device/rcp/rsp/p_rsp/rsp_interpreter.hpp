#pragma once
#include "rsp/timing.hpp"
#include "rsp_op.hpp"

namespace RSP {
// Portable scalar issue engine; vector execution uses the same SSE2/NEON leaves
// as the native emitter. No executable allocation or pointer-width assumptions.
class Interpreter {
    CPUState state{};
    RSPTimingPipeline pipeline{};
    bool timed = true;
    bool paired = false;
    ReturnMode step(uint32_t instruction);

  public:
    CPUState &get_state() { return state; }
    void set_dmem(uint32_t *p) { state.dmem = p; }
    void set_imem(uint32_t *p) { state.imem = p; }
    void set_rdram(uint32_t *p) { state.rdram = p; }
    void invalidate_imem() {
        pipeline = {};
        paired = false;
    }
    bool cycle_timing_enabled() const { return timed; }
    void set_cycle_timing(bool enabled) {
        timed = enabled;
        invalidate_imem();
    }
    ReturnMode run(uint32_t cycle_limit = UINT32_MAX);
};
} // namespace RSP
