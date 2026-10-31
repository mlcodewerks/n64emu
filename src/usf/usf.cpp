/* lazyusf2 interface over the shared Parallax CPU/RSP/device core.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "usf.h"
#include "internal.h"
extern "C" {
#include "core/audio.h"
#include "core/types.h"
#include "device/device.h"
#include "device/r4300/cpu_ops.h"
#include "device/r4300/jit_runtime.h"
#include "main/main.h"
#include <audio/audio_resampler.h>
#include <libretro.h>
extern bool breakloop;
}
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint64_t magic = UINT64_C(0x31534655584c5250);
constexpr size_t max_rom = 0x4000000, max_save = 0x80275c;
struct State {
    std::vector<uint8_t> rom, save = std::vector<uint8_t>(max_save), checkpoint;
    std::vector<int16_t> audio, resampled;
    size_t audio_read = 0, resampled_read = 0, goal = 0;
    int32_t rate = 0, output_rate = 0;
    bool compare = false, fifo = false, hle = false, trimming = false;
    char error[512]{};
    uint64_t budget = 0;
    const retro_resampler_t *resampler = nullptr;
    void *resampler_data = nullptr;
    ~State() {
        if (resampler_data)
            resampler->free(resampler_data);
    }
    void reset_audio() {
        audio.clear();
        resampled.clear();
        audio_read = resampled_read = 0;
        rate = output_rate = 0;
        if (resampler_data)
            resampler->free(resampler_data);
        resampler_data = nullptr;
        resampler = nullptr;
    }
    const char *fail(const char *message) {
        std::snprintf(error, sizeof(error), "%s", message);
        return error;
    }
};
struct Handle {
    uint64_t tag;
    State *state;
};
std::mutex engine_mutex;
State *active = nullptr;
bool initialized = false;
State *get(void *memory) {
    if (!memory)
        return nullptr;
    Handle h{};
    std::memcpy(&h, memory, sizeof(h));
    return h.tag == magic ? h.state : nullptr;
}
void log_message(enum retro_log_level level, const char *format, ...) {
    if (!active || level < RETRO_LOG_ERROR)
        return;
    va_list args;
    va_start(args, format);
    std::vsnprintf(active->error, sizeof(active->error), format, args);
    va_end(args);
}
bool environment(unsigned command, void *data) {
    switch (command) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback *>(data)->log = log_message;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto *v = static_cast<retro_variable *>(data);
        if (!std::strcmp(v->key, "wtfn64_renderer"))
            v->value = "softrdp";
        else if (!std::strcmp(v->key, "wtfn64_hle"))
            v->value = active && active->hle ? "RSP HLE" : "Off";
        else if (!std::strcmp(v->key, "wtfn64_timing")) {
#ifdef PARALLAX_CPU_INTERPRETER
            // Base CPU cycles match lazyusf2 without the portable cache-tag cost.
            // AI/DAC and interrupt scheduling retain their normal timing.
            v->value = "off";
#else
            v->value = "fast";
#endif
        }
        else if (!std::strcmp(v->key, "wtfn64_angrylion_threads"))
            v->value = "1";
        else if (!std::strcmp(v->key, "wtfn64_performance_cores"))
            v->value = "disabled";
        else if (std::strstr(v->key, "-pak"))
            v->value = "none";
        else
            return false;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    default:
        return false;
    }
}
void video(const void *, unsigned, unsigned, size_t) {}
void poll() {}
int16_t input(unsigned, unsigned, unsigned, unsigned) { return 0; }
size_t audio_discard(const int16_t *, size_t frames) { return frames; }
void frequency(void *opaque, unsigned rate) { static_cast<State *>(opaque)->rate = int32_t(rate); }
void samples(void *opaque, const void *data, size_t bytes) {
    auto &s = *static_cast<State *>(opaque);
    auto *in = static_cast<const int16_t *>(data);
    if (s.audio_read == s.audio.size()) {
        s.audio.clear();
        s.audio_read = 0;
    }
    const size_t frames = bytes / 4;
    try {
        size_t base = s.audio.size();
        s.audio.resize(base + frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            s.audio[base + i * 2] = in[i * 2 + 1];
            s.audio[base + i * 2 + 1] = in[i * 2];
        }
    } catch (...) {
        s.fail("USF audio buffer allocation failed");
        breakloop = true;
        return;
    }
    if (frames)
        s.budget = g_dev.r4300.retired_ops + UINT64_C(50000000);
    if ((s.audio.size() - s.audio_read) / 2 >= s.goal)
        breakloop = true;
}
const audio_out_backend_interface sink = {frequency, samples, nullptr};
bool valid_rom_header(const uint8_t *p) {
    return !std::memcmp(p, "\x80\x37\x12\x40", 4) ||
           !std::memcmp(p, "\x37\x80\x40\x12", 4) ||
           !std::memcmp(p, "\x40\x12\x37\x80", 4);
}
void close_active() {
    if (active) {
        retro_unload_game();
        active = nullptr;
    }
}
void restart(State &s) {
    if (active == &s)
        close_active();
    s.checkpoint.clear();
    s.reset_audio();
    s.error[0] = 0;
}
void ensure_engine(State &s) {
    if (active == &s)
        return;
    if (active) {
        active->checkpoint.resize(retro_serialize_size());
        if (!retro_serialize(active->checkpoint.data(), active->checkpoint.size()))
            throw std::runtime_error("USF context checkpoint failed");
        close_active();
    }
    active = &s;
    try {
        if (!initialized) {
            retro_set_environment(environment);
            retro_set_video_refresh(video);
            retro_set_input_poll(poll);
            retro_set_input_state(input);
            retro_set_audio_sample_batch(audio_discard);
            retro_init();
            initialized = true;
        }
        std::vector<uint8_t> rom = s.rom;
        if (rom.size() < 4096)
            rom.resize(4096);
        // ROM-less USFs retain their cartridge header in the PJ64 state.
        if (!valid_rom_header(rom.data()) && s.save.size() >= 72)
            std::copy_n(s.save.begin() + 8, 64, rom.begin());
        if (!valid_rom_header(rom.data())) {
            rom[0] = 0x80;
            rom[1] = 0x37;
            rom[2] = 0x12;
            rom[3] = 0x40;
            rom[0x3e] = 'E';
        }
        rom.resize((rom.size() + 3) & ~size_t(3));
        retro_game_info game{};
        game.data = rom.data();
        game.size = rom.size();
        if (!retro_load_game(&game) || !parallax_usf_prepare())
            throw std::runtime_error("USF core initialization failed");
        // Native checkpoints restore PC before the first CPU run has happened.
        g_dev.r4300.pc = &g_dev.r4300.instruction_pc;
        g_dev.r4300.startup = 0;
        g_dev.r4300.stop = 0;
        if (!r4300_jit_init(&g_dev.r4300))
            throw std::runtime_error("USF CPU initialization failed");
#ifdef HAVE_PARALLAXJIT
        g_dev.r4300.execute_one = r4300_jit_execute_one;
#else
        g_dev.r4300.execute_one = r4300_ops_execute_one;
#endif
        g_dev.ai.aout = &s;
        g_dev.ai.iaout = &sink;
        if (s.checkpoint.empty()) {
            if (!parallax_usf_load_pj64(s.save.data(), s.save.size()))
                throw std::runtime_error("Invalid Project64 USF save state");
        } else if (!retro_unserialize(s.checkpoint.data(), s.checkpoint.size()))
            throw std::runtime_error("USF context restore failed");
        g_dev.ai.aout = &s;
        g_dev.ai.iaout = &sink;
        if (g_dev.ai.regs[AI_DACRATE_REG])
            s.rate = int32_t(g_dev.vi.clock / (g_dev.ai.regs[AI_DACRATE_REG] + 1u));
    } catch (...) {
        close_active();
        throw;
    }
}
void render_native(State &s, int16_t *buffer, size_t frames) {
    if (s.trimming)
        throw std::runtime_error("Parallax USF does not support trimming/coverage instrumentation");
    if (s.error[0])
        throw std::runtime_error(s.error);
    if (frames > SIZE_MAX / 4)
        throw std::runtime_error("USF sample count overflows address space");
    ensure_engine(s);
    size_t remaining = frames;
    do {
        size_t available = (s.audio.size() - s.audio_read) / 2;
        if (available) {
            if (!remaining)
                break;
            size_t n = std::min(available, remaining);
            if (buffer) {
                std::copy_n(s.audio.data() + s.audio_read, n * 2, buffer);
                buffer += n * 2;
            }
            s.audio_read += n * 2;
            remaining -= n;
            if (!remaining)
                break;
        }
        s.goal = remaining ? remaining : 1;
        // A malformed rip must yield an error instead of monopolizing its caller.
        s.budget = g_dev.r4300.retired_ops + UINT64_C(50000000);
        unsigned fields = 0;
        while ((s.audio.size() - s.audio_read) / 2 < s.goal) {
            const size_t buffered = s.audio.size();
            parallax_usf_step();
            if (s.error[0])
                throw std::runtime_error(s.error);
            if (g_dev.r4300.stop)
                throw std::runtime_error("USF emulation stopped");
            if (s.audio.size() != buffered)
                fields = 0;
            else if (++fields > 600)
                throw std::runtime_error("USF produced no audio within the rendering budget");
        }
        if (!frames)
            break;
    } while (remaining);
}
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void upload(State &s, const uint8_t *data, size_t bytes) {
    if (!data || bytes < 8)
        throw std::runtime_error("Truncated USF section");
    struct Chunk {
        bool rom;
        uint32_t offset, length;
        const uint8_t *data;
    };
    std::vector<Chunk> chunks;
    for (unsigned section = 0; section < 2; ++section) {
        if (bytes < 4)
            throw std::runtime_error("Truncated USF section header");
        auto tag = le32(data);
        data += 4;
        bytes -= 4;
        if (tag != 0x34365253) {
            if (tag)
                throw std::runtime_error("Invalid USF section marker");
            continue;
        }
        for (;;) {
            if (bytes < 4)
                throw std::runtime_error("Truncated USF chunk length");
            uint32_t n = le32(data);
            data += 4;
            bytes -= 4;
            if (!n)
                break;
            if (bytes < 4)
                throw std::runtime_error("Truncated USF chunk offset");
            uint32_t offset = le32(data);
            data += 4;
            bytes -= 4;
            size_t limit = section ? max_save : max_rom;
            if (offset > limit || n > limit - offset || bytes < n)
                throw std::runtime_error("USF chunk is outside its image");
            chunks.push_back({section == 0, offset, n, data});
            data += n;
            bytes -= n;
        }
    }
    restart(s);
    for (auto &c : chunks) {
        auto &image = c.rom ? s.rom : s.save;
        if (image.size() < size_t(c.offset) + c.length) {
            size_t extent = size_t(c.offset) + c.length;
            if (c.rom) {
                size_t capacity = 1024 * 1024;
                while (capacity < extent)
                    capacity *= 2;
                extent = capacity;
            }
            image.resize(extent);
        }
        std::copy_n(c.data, c.length, image.begin() + c.offset);
    }
}
} // namespace

extern "C" {
int parallax_usf_compare_enabled() { return active && active->compare; }
int parallax_usf_fifo_enabled() { return active && active->fifo; }
unsigned parallax_usf_rdram_size() {
    if (active && active->save.size() >= 8) {
        unsigned bytes = le32(active->save.data() + 4);
        if (bytes == 0x400000 || bytes == 0x800000)
            return bytes;
    }
    return 0x800000;
}
int parallax_usf_yield() {
    if (active && active->budget && g_dev.r4300.retired_ops >= active->budget) {
        active->fail("USF CPU instruction budget exceeded");
        return 1;
    }
    return 0;
}
USF_API size_t usf_get_state_size() { return sizeof(Handle); }
USF_API void usf_clear(void *memory) {
    if (!memory)
        return;
    std::lock_guard<std::mutex> lock(engine_mutex);
    State *s = nullptr;
    try {
        s = new State;
    } catch (...) {
    }
    Handle h{magic, s};
    std::memcpy(memory, &h, sizeof(h));
}
USF_API void usf_shutdown(void *memory) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (s) {
        if (active == s)
            close_active();
        delete s;
    }
    if (memory) {
        Handle h{};
        std::memcpy(memory, &h, sizeof(h));
    }
}
USF_API void usf_restart(void *memory) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    if (auto *s = get(memory))
        restart(*s);
}
#define SETTER(name, member)                                                                       \
    USF_API void name(void *memory, int enable) {                                                  \
        std::lock_guard<std::mutex> lock(engine_mutex);                                            \
        if (auto *s = get(memory)) {                                                               \
            if (s->member != bool(enable)) {                                                       \
                restart(*s);                                                                       \
                s->member = bool(enable);                                                          \
            }                                                                                      \
        }                                                                                          \
    }
SETTER(usf_set_compare, compare)
SETTER(usf_set_fifo_full, fifo)
SETTER(usf_set_hle_audio, hle)
SETTER(usf_set_trimming_mode, trimming)
#undef SETTER
USF_API void *usf_get_rom_coverage_barray(void *) { return nullptr; }
USF_API void *usf_get_ram_coverage_barray(void *) { return nullptr; }
USF_API int usf_upload_section(void *memory, const uint8_t *data, size_t bytes) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (!s)
        return -1;
    try {
        upload(*s, data, bytes);
        return 0;
    } catch (const std::exception &e) {
        s->fail(e.what());
        return -1;
    }
}
USF_API void usf_upload_rom(void *memory, const uint8_t *data, size_t bytes) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (!s)
        return;
    try {
        if (bytes > max_rom || (!data && bytes))
            throw std::runtime_error("Invalid USF ROM size");
        restart(*s);
        if (bytes)
            s->rom.assign(data, data + bytes);
        else
            s->rom.clear();
    } catch (const std::exception &e) {
        s->fail(e.what());
    }
}
USF_API void usf_upload_save_state(void *memory, const uint8_t *data, size_t bytes) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (!s)
        return;
    try {
        if (bytes > max_save || !data || bytes < 8)
            throw std::runtime_error("Invalid USF save-state size");
        restart(*s);
        s->save.assign(data, data + bytes);
    } catch (const std::exception &e) {
        s->fail(e.what());
    }
}
USF_API const char *usf_render(void *memory, int16_t *buffer, size_t frames, int32_t *rate) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (!s)
        return "USF state allocation failed or state is not initialized";
    try {
        render_native(*s, buffer, frames);
        if (rate)
            *rate = s->rate;
        return nullptr;
    } catch (const std::exception &e) {
        return s->fail(e.what());
    }
}
USF_API const char *usf_render_resampled(void *memory, int16_t *buffer, size_t frames,
                                         int32_t rate) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto *s = get(memory);
    if (!s)
        return "USF state is not initialized";
    try {
        if (rate <= 0 || rate > 384000 || frames > SIZE_MAX / 4)
            throw std::runtime_error("Invalid USF output sample rate or count");
        if (!s->rate)
            render_native(*s, nullptr, 0);
        if (s->output_rate && s->output_rate != rate)
            throw std::runtime_error("Restart USF before changing the output sample rate");
        s->output_rate = rate;
        if (!s->resampler_data &&
            !retro_resampler_realloc(&s->resampler_data, &s->resampler, "sinc",
                                     RESAMPLER_QUALITY_HIGHER, double(rate) / s->rate))
            throw std::runtime_error("USF resampler allocation failed");
        std::vector<int16_t> native(4096 * 2);
        std::vector<float> in(native.size()), out;
        while (frames) {
            size_t available = (s->resampled.size() - s->resampled_read) / 2;
            if (available) {
                size_t n = std::min(available, frames);
                if (buffer) {
                    std::copy_n(s->resampled.data() + s->resampled_read, n * 2, buffer);
                    buffer += n * 2;
                }
                s->resampled_read += n * 2;
                frames -= n;
                continue;
            }
            // Keep each DMA's DAC rate intact when a rip changes dividers.
            render_native(*s, nullptr, 0);
            const size_t native_frames = std::min<size_t>((s->audio.size() - s->audio_read) / 2, 4096);
            render_native(*s, native.data(), native_frames);
            const double ratio = double(rate) / s->rate;
            const size_t capacity = size_t(std::ceil(native_frames * ratio)) + 256;
            if (capacity > 16000000)
                throw std::runtime_error("Invalid USF DAC/output ratio");
            out.resize(capacity * 2);
            for (size_t i = 0; i < native_frames * 2; ++i)
                in[i] = native[i] / 32768.0f;
            resampler_data d{};
            d.data_in = in.data();
            d.data_out = out.data();
            d.input_frames = native_frames;
            d.ratio = ratio;
            s->resampler->process(s->resampler_data, &d);
            if (d.output_frames > capacity)
                throw std::runtime_error("USF resampler output exceeds capacity");
            s->resampled.resize(d.output_frames * 2);
            s->resampled_read = 0;
            for (size_t i = 0; i < s->resampled.size(); ++i)
                s->resampled[i] =
                    int16_t(std::clamp(std::lround(out[i] * 32768.0f), -32768L, 32767L));
        }
        return nullptr;
    } catch (const std::exception &e) {
        return s->fail(e.what());
    }
}
}
