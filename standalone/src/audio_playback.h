#pragma once
#include "audio_queue.h"
#include <array>
#include <cmath>

inline double audio_output_ratio(double source, unsigned output, const AudioQueue &queue, bool started) {
    const double nominal = output / source;
    if (!started || !queue.capacity())
        return nominal;
    // Match wtfweg's free-space feedback with an output/input resampler ratio.
    return nominal * (1 + 0.005 * (1 - 2.0 * queue.frames() / queue.capacity()));
}

class AudioPlayback {
  public:
    void reset(unsigned rate) {
        reserve = size_t(std::ceil(rate * 0.160));
        max_reserve = size_t(std::ceil(rate * 0.192));
        restart();
    }

    void restart() {
        starved = false;
        fade = fade_length;
        last = {};
    }

    bool ready(const AudioQueue &queue) const { return queue.frames() >= reserve; }

    struct Read {
        size_t played;
        bool shortage_started;
        bool has_samples;
    };

    Read read(AudioQueue &queue, int16_t *pcm, size_t frames) {
        if (starved && queue.frames() >= reserve / 2)
            starved = false;
        const auto played = queue.take(pcm, frames);
        const bool missing = played < frames;
        const bool episode = missing && !starved;
        if (episode) {
            starved = true;
            reserve = max_reserve;
        }
        for (size_t i = 0; i < played && fade; ++i, --fade) {
            const int weight = int(fade_length - fade);
            pcm[i * 2] = int16_t(int(pcm[i * 2]) * weight / fade_length);
            pcm[i * 2 + 1] = int16_t(int(pcm[i * 2 + 1]) * weight / fade_length);
        }
        if (played)
            last = {pcm[played * 2 - 2], pcm[played * 2 - 1]};
        const bool tail_needed = missing && (last[0] || last[1]);
        if (missing) {
            const int tail = int(std::min(frames - played, size_t(fade_length)));
            for (int i = 0; i < tail; ++i)
                for (unsigned ch = 0; ch < 2; ++ch)
                    pcm[(played + i) * 2 + ch] = int16_t(int(last[ch]) * (tail - i - 1) / tail);
            std::fill(pcm + (played + tail) * 2, pcm + frames * 2, int16_t(0));
            last = {};
            fade = fade_length;
        }
        return {played, episode, played || tail_needed};
    }

  private:
    static constexpr int fade_length = 64;
    size_t reserve = 0, max_reserve = 0;
    bool starved = false;
    int fade = fade_length;
    std::array<int16_t, 2> last{};
};
