#include "audio_playback.h"
#include "dsp_chain.h"
#include "frame_pacing.h"
#include <array>
#include <cstdio>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    try {
        AudioQueue queue;
        queue.reset(4);
        const std::array<int16_t, 12> samples{1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6};
        std::array<int16_t, 8> output{};
        require(queue.push(samples.data(), 3) == 3, "initial queue push");
        require(queue.take(output.data(), 2) == 2 && output[3] == -2, "initial queue read");
        require(queue.push(samples.data() + 6, 3) == 3, "wrapped queue push");
        require(queue.take(output.data(), 4) == 4 &&
                std::equal(output.begin(), output.end(), samples.begin() + 4), "wrapped stereo ordering");
        queue.push(samples.data(), 2);
        require(queue.push(samples.data() + 4, 3) == 2, "full queue accepts only available space");
        require(queue.take(output.data(), 4) == 4 &&
                std::equal(output.begin(), output.end(), samples.begin()), "full queue preserves original PCM");
        require(queue.push(samples.data(), 6) == 4, "larger batch can resume from the accepted offset");
        require(queue.take(output.data(), 4) == 4 &&
                std::equal(output.begin(), output.end(), samples.begin()), "large batch preserves PCM ordering");
        queue.clear();
        require(queue.take(output.data(), 4) == 0, "clear discards queued audio");

        const double nominal = 48001 / 48000.5;
        for (size_t fill : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(4)}) {
            queue.clear();
            queue.push(samples.data(), fill);
            require(audio_output_ratio(48000.5, 48001, queue, false) == nominal,
                    "startup uses the advertised fractional clock");
            const auto expected = nominal * (1 + 0.005 * (1 - fill / 2.0));
            require(std::abs(audio_output_ratio(48000.5, 48001, queue, true) - expected) < 1e-12,
                    "rate control uses half the actual capacity and stays within 0.5%");
        }

        AudioPlayback playback;
        playback.reset(8000);
        queue.reset(2048);
        std::vector<int16_t> tone(1600 * 2, 1024), block(1600 * 2);
        queue.push(tone.data(), 1279);
        require(!playback.ready(queue), "startup waits for the reserve");
        queue.push(tone.data(), 1);
        require(playback.ready(queue), "160 ms primes playback");
        auto read = playback.read(queue, block.data(), 1600);
        require(read.played == 1280 && read.shortage_started && read.has_samples,
                "shortage reports the first missing block");
        require(block[0] == 0 && block[128] == 1024 && block[2560] > 0 && block[2688] == 0,
                "playback fades across both shortage edges");
        queue.push(tone.data(), 32);
        read = playback.read(queue, block.data(), 160);
        require(read.played == 32 && !read.shortage_started && block[0] == 0 && block[62] > 0,
                "incoming audio plays immediately during shortage recovery");
        queue.push(tone.data(), 1280);
        require(!playback.ready(queue), "shortage raises the startup reserve");
        queue.push(tone.data(), 256);
        require(playback.ready(queue), "raised reserve stays bounded at 192 ms");
        playback.restart();
        require(playback.ready(queue), "same-rate restart retains the learned reserve");
        queue.clear();
        queue.push(tone.data(), 1280);
        require(!playback.ready(queue), "same-rate restart still waits for 192 ms");
        playback.reset(8000);
        require(playback.ready(queue), "new endpoint starts with the base 160 ms reserve");

        dsp_chain dsp;
        std::vector<int16_t> scratch;
        const auto bypass = dsp.process(samples.data(), 6, scratch);
        require(bypass.samples == samples.data() && bypass.frames == 6 && scratch.empty(), "DSP bypass borrows the input");

        constexpr uint64_t field = 16666666;
        FramePacing pacing;
        pacing.reset(0);
        require(pacing.advance(0, 60) == field, "nominal field period");
        require(pacing.advance(field + 2000000, 60) == 33333333, "fractional phase after a late frame");
        const auto brief_overrun = pacing.advance(100000000, 60);
        require(brief_overrun >= 49999999 && brief_overrun <= 50000000, "brief overrun retains the core timeline");
        require(pacing.advance(160000000, 60) == 160000000, "discard debt after a long stall");
        pacing.reset(0);
        uint64_t deadline = 0;
        for (unsigned i = 0; i < 600; ++i)
            deadline = pacing.advance(deadline, 59.826097857);
        require(deadline == uint64_t(600e9 / 59.826097857), "fractional pacing preserves the advertised field rate");
        std::puts("PASS audio queue and frame pacing");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
