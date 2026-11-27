#pragma once
#include <cstdint>

class FramePacing {
  public:
    void reset(uint64_t now) {
        deadline = now;
        fraction = 0;
    }

    uint64_t advance(uint64_t now, double fps) {
        // Preserve the game's fractional frame period through short overruns.
        fraction += 1e9 / fps;
        const auto period = uint64_t(fraction);
        fraction -= period;
        deadline += period;
        if (now > deadline && now - deadline > period * 4)
            reset(now);
        return deadline;
    }

  private:
    uint64_t deadline = 0;
    double fraction = 0;
};
