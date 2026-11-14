/* lazyusf2 interface over the shared n64emu CPU/RSP/device core.
 * SPDX-License-Identifier: BSD-3-Clause */
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
#include <cstdlib>
#include <miniz/miniz.h>
#ifdef _WIN32
#define psf_strcasecmp _stricmp
#define psf_strdup _strdup
#else
#include <strings.h>
#define psf_strcasecmp strcasecmp
#define psf_strdup strdup
#endif
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
    bool compare = false, fifo = false, trimming = false;
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
        if (!std::strcmp(v->key, "wtfn64_timing")) {
            // USF snapshots require the ripper's legacy instruction clock.
            v->value = "off";
        }
        else if (!std::strcmp(v->key, "wtfn64_angrylion_threads"))
            v->value = "1";
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
        if (!retro_load_game(&game) || !n64emu_usf_prepare())
            throw std::runtime_error("USF core initialization failed");
        // Native checkpoints restore PC before the first CPU run has happened.
        g_dev.r4300.pc = &g_dev.r4300.instruction_pc;
        g_dev.r4300.startup = 0;
        g_dev.r4300.stop = 0;
        if (!r4300_jit_init(&g_dev.r4300))
            throw std::runtime_error("USF CPU initialization failed");
#ifdef HAVE_N64EMUJIT
        g_dev.r4300.execute_one = r4300_jit_execute_one;
#else
        g_dev.r4300.execute_one = r4300_ops_execute_one;
#endif
        g_dev.ai.aout = &s;
        g_dev.ai.iaout = &sink;
        if (s.checkpoint.empty()) {
            if (!n64emu_usf_load_pj64(s.save.data(), s.save.size()))
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
        throw std::runtime_error("n64emu USF does not support trimming/coverage instrumentation");
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
            n64emu_usf_step();
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
int n64emu_usf_compare_enabled() { return active && active->compare; }
int n64emu_usf_fifo_enabled() { return active && active->fifo; }
unsigned n64emu_usf_rdram_size() {
    if (active && active->save.size() >= 8) {
        unsigned bytes = le32(active->save.data() + 4);
        if (bytes == 0x400000 || bytes == 0x800000)
            return bytes;
    }
    return 0x800000;
}
int n64emu_usf_yield() {
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
void usf_set_hle_audio(void *state, int enable) { (void)state; (void)enable; }
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

/* PSF/miniUSF container loading, integrated from psflib.
 * Copyright (c) 2012-2015 Christopher Snowhill.
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../LICENSE for the license terms. */
namespace { namespace psf {
static const char * strrpbrk(const char * s, const char * accept)
{
	const char * start;

	if (!s || !*s || !accept || !*accept) return NULL;

	start = s;
	s += strlen(s) - 1;

	while (s >= start)
	{
		const char *a = accept;
		while (*a != '\0')
			if (*a++ == *s)
				return s;
		--s;
	}

	return NULL;
}

enum { max_recursion_depth = 10 };

typedef struct psf_load_state
{
	int                        depth;

	unsigned char              allowed_version;

	char                     * base_path;
	const psf_file_callbacks * file_callbacks;

	psf_load_callback          load_target;
	void                     * load_context;

	psf_info_callback          info_target;
	void                     * info_context;
	int                        info_want_nested_tags;

	psf_status_callback        status_target;
	void                     * status_context;

	char                       lib_name_temp[32];
} psf_load_state;

static int psf_load_internal(psf_load_state * state, const char * file_name);


static int psf_want_status(psf_load_state * state);
static void psf_status(psf_load_state * state, const char * message, int indent);

int psf_load(const char * uri, const psf_file_callbacks * file_callbacks, uint8_t allowed_version,
	psf_load_callback load_target, void * load_context, psf_info_callback info_target,
	void * info_context, int info_want_nested_tags, psf_status_callback status_target,
	void * status_context)
{
	int rval;

	psf_load_state state;

	const char * file_name;

	if (!uri || !*uri || !file_callbacks || !file_callbacks->path_separators || !*file_callbacks->path_separators || !file_callbacks->fopen ||
		!file_callbacks->fread || !file_callbacks->fseek || !file_callbacks->fclose || !file_callbacks->ftell) return -1;

	state.depth = 0;
	state.allowed_version = allowed_version;
	state.file_callbacks = file_callbacks;
	state.load_target = load_target;
	state.load_context = load_context;
	state.info_target = info_target;
	state.info_context = info_context;
	state.info_want_nested_tags = info_want_nested_tags;
	state.status_target = status_target;
	state.status_context = status_context;

	state.base_path = psf_strdup(uri);
	if (!state.base_path)
	{
		psf_status(&state, "Out of memory allocating state.base_path\n", 1);
		return -1;
	}

	file_name = strrpbrk(uri, file_callbacks->path_separators);

	if (file_name)
	{
		++file_name;
		state.base_path[file_name - uri] = '\0';
	}
	else
	{
		state.base_path[0] = '\0';
		file_name = uri;
	}

	rval = psf_load_internal(&state, file_name);

	free(state.base_path);

	psf_status(&state, "Done.", 0);

	return rval;
}

typedef struct psf_tag psf_tag;

struct psf_tag {
	char * name;
	char * value;
	psf_tag * next, *prev;
};

#define FIELDS_SPLIT 6
static const char * fields_to_split[FIELDS_SPLIT] = { "ARTIST", "ALBUM ARTIST", "PRODUCER", "COMPOSER", "PERFORMER", "GENRE" };

static int check_split_value(const char * name)
{
	unsigned i;
	for (i = 0; i < FIELDS_SPLIT; i++)
	{
		if (!psf_strcasecmp(name, fields_to_split[i])) return 1;
	}
	return 0;
}

static psf_tag * find_tag(psf_tag * tags, const char * name)
{
	if (tags && name && *name)
	{
		while (tags)
		{
			if (!psf_strcasecmp(tags->name, name)) return tags;
			tags = tags->next;
		}
	}

	return NULL;
}

static void free_tags(psf_tag * tags)
{
	psf_tag * tag = tags, *next;
	while (tag)
	{
		next = tag->next;
		if (tag->name) free(tag->name);
		if (tag->value) free(tag->value);
		free(tag);
		tag = next;
	}
}

static psf_tag * add_tag_multi(psf_tag * tags, const char * name, const char ** values, int values_count)
{
	psf_tag * footer;
	psf_tag * tag;

	int i;

	if (!name || !*name || !values || !values_count || !*values) return NULL;

	footer = tags;

	tag = find_tag(tags, name);
	if (!tag)
	{
		tag = static_cast<psf_tag *>(calloc(1, sizeof(psf_tag)));
		if (!tag) return footer;
		tag->name = psf_strdup(name);
		if (!tag->name)
		{
			free(tag);
			return footer;
		}
		tag->next = tags;
		if (tags) tags->prev = tag;
		footer = tag;
	}
	if (tag->value)
	{
		size_t old_length = strlen(tag->value);
		size_t new_length = strlen(values[0]);
		char * new_value = (char *)realloc(tag->value, old_length + new_length + 2);
		if (!new_value) return footer;
		tag->value = new_value;
		new_value[old_length] = '\n';
#if _MSC_VER >= 1300
		strcpy_s(new_value + old_length + 1, new_length + 1, values[0]);
#else
		strcpy(new_value + old_length + 1, values[0]);
#endif
	}
	else
	{
		tag->value = psf_strdup(values[0]);
		if (!tag->value) return footer;
	}

	for (i = 1; i < values_count; i++)
	{
		tag = static_cast<psf_tag *>(calloc(1, sizeof(psf_tag)));
		if (!tag) return footer;
		tag->name = psf_strdup(name);
		if (!tag->name)
		{
			free(tag);
			return footer;
		}
		tag->value = psf_strdup(values[i]);
		if (!tag->value)
		{
			free(tag->name);
			free(tag);
			return footer;
		}
		tag->next = footer;
		if (footer) footer->prev = tag;
		footer = tag;
	}

	return footer;
}

static psf_tag * add_tag(psf_tag * tags, const char * name, const char * value)
{
	int values_count;
	const char ** values;
	char * value_split;

	if (!name || !*name || !value || !*value) return tags;

	if (check_split_value(name))
	{
		char * split_point, *remain;
		const char ** new_values;
		values_count = 0;
		values = NULL;
		value_split = psf_strdup(value);
		if (!value_split) return tags;
		remain = value_split;
		split_point = strstr(value_split, "; ");
		while (split_point)
		{
			values_count++;
			new_values = (const char **)realloc((void *)values, sizeof(const char*) * ((values_count + 3) & ~3));
			if (!new_values)
			{
				if (values) free((void *)values);
				free(value_split);
				return tags;
			}
			values = new_values;
			*split_point = '\0';
			values[values_count - 1] = remain;
			remain = split_point + 2;
			split_point = strstr(remain, "; ");
		}
		if (*remain)
		{
			values_count++;
			new_values = (const char **)realloc((void *)values, sizeof(char*) * ((values_count + 3) & ~3));
			if (!new_values)
			{
				if (values) free((void *)values);
				free(value_split);
				return tags;
			}
			values = new_values;
			values[values_count - 1] = remain;
		}
	}
	else
	{
		values_count = 1;
		value_split = NULL;
		values = (const char **)malloc(sizeof(const char *));
		if (!values) return tags;
		values[0] = value;
	}

	tags = add_tag_multi(tags, name, values, values_count);

	if (value_split) free(value_split);
	free((void *)values);

	return tags;
}

/* Split line on first equals sign, and remove any whitespace surrounding the name and value fields */

static psf_tag * process_tag_line(psf_tag * tags, char * line)
{
	char * name, *value, *end;
	char * equals = strchr(line, '=');
	if (!equals) return tags;

	name = line;
	value = equals + 1;
	end = line + strlen(line);

	while (name < equals && *name > 0 && *name <= ' ') name++;
	if (name == equals) return tags;

	--equals;
	while (equals > name && *equals > 0 && *equals <= ' ') --equals;
	equals[1] = '\0';

	while (value < end && *value > 0 && *value <= ' ') value++;
	if (value == end) return tags;

	--end;
	while (end > value && *end > 0 && *end <= ' ') --end;
	end[1] = '\0';

	if (*name == '_')
	{
		psf_tag * tag = find_tag(tags, name);
		if (tag) return tags;
	}

	return add_tag(tags, name, value);
}

static psf_tag * process_tags(char * buffer)
{
	psf_tag * tags = NULL;
	char * line_end;
	if (!buffer || !*buffer) return NULL;

	line_end = strpbrk(buffer, "\n\r");
	while (line_end)
	{
		*line_end++ = '\0';
		tags = process_tag_line(tags, buffer);
		while (*line_end && (*line_end == '\n' || *line_end == '\r')) line_end++;
		buffer = line_end;
		line_end = strpbrk(buffer, "\n\r");
	}
	if (*buffer) tags = process_tag_line(tags, buffer);

	return tags;
}

static int psf_load_internal(psf_load_state * state, const char * file_name)
{
	psf_tag * tags = NULL;
	psf_tag * tag;

	char * full_path;

	void * file;

	long file_size, tag_size;

	int n;

	uint8_t header_buffer[16];

	uint8_t * exe_compressed_buffer = NULL;
	uint8_t * exe_decompressed_buffer = NULL;
	uint8_t * reserved_buffer = NULL;
	char * tag_buffer = NULL;

	uint32_t exe_compressed_size, exe_crc32, reserved_size;
	mz_ulong exe_decompressed_size, try_exe_decompressed_size;

	int zerr;

	size_t full_path_size;

	if (++state->depth > max_recursion_depth)
	{
		psf_status(state, "Exceeded maximum file nesting depth.\n", 1);
		return -1;
	}

	full_path_size = strlen(state->base_path) + strlen(file_name) + 1;
	full_path = (char *)malloc(full_path_size);
	if (!full_path) return -1;

#if _MSC_VER >= 1300
	strcpy_s(full_path, full_path_size, state->base_path);
	strcat_s(full_path, full_path_size, file_name);
#else
	strcpy(full_path, state->base_path);
	strcat(full_path, file_name);
#endif

	file = state->file_callbacks->fopen(state->file_callbacks->context, full_path);

	free(full_path);

	if (!file)
	{
		if (psf_want_status(state))
		{
			psf_status(state, "Error opening file: ", 1);
			psf_status(state, file_name, 0);
			psf_status(state, "\n", 0);
			psf_status(state, "From base path: ", 1);
			psf_status(state, state->base_path, 0);
			psf_status(state, "\n", 0);
		}
		return -1;
	}

	if (psf_want_status(state))
	{
		psf_status(state, "Opened file: ", 1);
		psf_status(state, file_name, 0);
		psf_status(state, "\n", 0);
		psf_status(state, "From base path: ", 1);
		psf_status(state, state->base_path, 0);
		psf_status(state, "\n", 0);
	}

	if (state->file_callbacks->fread(header_buffer, 1, 16, file) < 16)
	{
		psf_status(state, "File too small to contain a valid header.\n", 1);
		goto error_close_file;
	}

	if (memcmp(header_buffer, "PSF", 3))
	{
		psf_status(state, "File does not contain a valid PSF signature.\n", 1);
		goto error_close_file;
	}

	if (state->allowed_version && (header_buffer[3] != state->allowed_version))
	{
		if (psf_want_status(state))
		{
			char temp[8];
			psf_status(state, "Expected PSF version ", 1);
			snprintf(temp, 7, "%d", (int)state->allowed_version);
			temp[7] = '\0';
			psf_status(state, temp, 0);
			psf_status(state, ", got ", 0);
			snprintf(temp, 7, "%d", (int)header_buffer[3]);
			temp[7] = '\0';
			psf_status(state, temp, 0);
			psf_status(state, "\n", 0);
		}
		goto error_close_file;
	}

	reserved_size = header_buffer[4] | (header_buffer[5] << 8) | (header_buffer[6] << 16) | (header_buffer[7] << 24);
	exe_compressed_size = header_buffer[8] | (header_buffer[9] << 8) | (header_buffer[10] << 16) | (header_buffer[11] << 24);
	exe_crc32 = header_buffer[12] | (header_buffer[13] << 8) | (header_buffer[14] << 16) | (header_buffer[15] << 24);

	if (state->file_callbacks->fseek(file, 0, SEEK_END))
	{
		psf_status(state, "Could not seek to end of file to determine file size.\n", 1);
		goto error_close_file;
	}

	file_size = state->file_callbacks->ftell(file);

	if (file_size <= 0)
	{
		psf_status(state, "Could not determine file size.\n", 1);
		goto error_close_file;
	}

	if ((unsigned long)file_size >= 16 + reserved_size + exe_compressed_size + 5)
	{
		psf_status(state, "Tag detected, attempting to read it.\n", 1);

		tag_size = file_size - (16 + reserved_size + exe_compressed_size);
		if (state->file_callbacks->fseek(file, -tag_size, SEEK_CUR))
		{
			psf_status(state, "Could not seek back to read tag.\n", 1);
			goto error_close_file;
		}
		tag_buffer = (char *)malloc(tag_size + 1);
		if (!tag_buffer)
		{
			psf_status(state, "Out of memory allocating tag buffer.\n", 1);
			goto error_close_file;
		}
		if (state->file_callbacks->fread(tag_buffer, 1, tag_size, file) < (size_t)tag_size)
		{
			psf_status(state, "Could not read tag.\n", 1);
			goto error_free_buffers;
		}
		tag_buffer[tag_size] = 0;
		if (!memcmp(tag_buffer, "[TAG]", 5)) tags = process_tags(tag_buffer + 5);
		free(tag_buffer);
		tag_buffer = NULL;

		if (tags && state->info_target && (state->depth == 1 || state->info_want_nested_tags))
		{
			tag = tags;
			while (tag->next) tag = tag->next;
			while (tag)
			{
				if (state->info_target(state->info_context, tag->name, tag->value))
				{
					if (psf_want_status(state))
					{
						psf_status(state, "Caller rejected tag: ", 1);
						psf_status(state, tag->name, 0);
						psf_status(state, "=", 0);
						psf_status(state, tag->value, 0);
						psf_status(state, "\n", 0);
					}
					goto error_free_tags;
				}
				tag = tag->prev;
			}
		}
	}

	if (!state->load_target) goto done;

	tag = find_tag(tags, "_lib");
	if (tag)
	{
		if (psf_want_status(state))
		{
			psf_status(state, "Found _lib: ", 1);
			psf_status(state, tag->value, 0);
			psf_status(state, "\n", 0);
		}
		if (psf_load_internal(state, tag->value) < 0) goto error_free_tags;
	}

	reserved_buffer = (uint8_t *)malloc(reserved_size ? reserved_size : 1);
	if (!reserved_buffer)
	{
		psf_status(state, "Out of memory allocating buffer for reserved section.\n", 1);
		goto error_free_tags;
	}
	exe_compressed_buffer = (uint8_t *)malloc(exe_compressed_size ? exe_compressed_size : 1);
	if (!exe_compressed_buffer)
	{
		psf_status(state, "Out of memory allocating buffer for compressed exe section.\n", 1);
		goto error_free_tags;
	}

	if (state->file_callbacks->fseek(file, 16, SEEK_SET))
	{
		psf_status(state, "Could not seek back to main data section of file.", 1);
		goto error_free_tags;
	}
	if (reserved_size && state->file_callbacks->fread(reserved_buffer, 1, reserved_size, file) < reserved_size)
	{
		psf_status(state, "Could not read reserved section.\n", 1);
		goto error_free_tags;
	}
	if (exe_compressed_size && state->file_callbacks->fread(exe_compressed_buffer, 1, exe_compressed_size, file) < exe_compressed_size)
	{
		psf_status(state, "Could not read compressed exe section.\n", 1);
		goto error_free_tags;
	}
	state->file_callbacks->fclose(file);
	file = NULL;

	psf_status(state, "File closed.\n", 1);

	if (exe_compressed_size)
	{
		uint32_t got_crc32 = mz_crc32(mz_crc32(0L, nullptr, 0), exe_compressed_buffer, exe_compressed_size);
		if (exe_crc32 != got_crc32)
		{
			if (psf_want_status(state))
			{
				char temp[16];
				psf_status(state, "CRC mismatch on compressed exe section.\nWanted: 0x", 1);
				snprintf(temp, 15, "%X", exe_crc32);
				temp[15] = '\0';
				psf_status(state, temp, 0);
				psf_status(state, ", got 0x", 0);
				snprintf(temp, 15, "%X", got_crc32);
				temp[15] = '\0';
				psf_status(state, temp, 0);
				psf_status(state, "\n", 0);
			}
			goto error_free_tags;
		}

		try_exe_decompressed_size = exe_compressed_size * 3;
		// Most files are smaller arbitrary sizes, but some are larger powers of two, usually with a small header as well
		exe_decompressed_size = try_exe_decompressed_size + 128;
		exe_decompressed_buffer = (uint8_t *)malloc(exe_decompressed_size ? exe_decompressed_size : 1);
		if (!exe_decompressed_buffer)
		{
			psf_status(state, "Out of memory allocating buffer for decompressed exe section.\n", 1);
			goto error_free_tags;
		}

		while (MZ_OK != (zerr = mz_uncompress(exe_decompressed_buffer, &exe_decompressed_size, exe_compressed_buffer, exe_compressed_size)))
		{
			void * try_exe_decompressed_buffer;

			if (MZ_MEM_ERROR != zerr && MZ_BUF_ERROR != zerr)
			{
				psf_status(state, "Could not decompress exe section.\n", 1);
				goto error_free_tags;
			}

			if (try_exe_decompressed_size < (1 * 1024 * 1024))
				try_exe_decompressed_size = (1 * 1024 * 1024);
			else
				try_exe_decompressed_size <<= 1;

			if (try_exe_decompressed_size >= (512 * 1024 * 1024))
			{
				psf_status(state, "PSF exe section exceeds 512MB, giving up.\n", 1);
				goto error_free_tags;
			}

			exe_decompressed_size = try_exe_decompressed_size + 128;

			try_exe_decompressed_buffer = realloc(exe_decompressed_buffer, exe_decompressed_size);
			if (!try_exe_decompressed_buffer)
			{
				psf_status(state, "Out of memory reallocating buffer for decompressed exe section.\n", 1);
				goto error_free_tags;
			}

			exe_decompressed_buffer = (uint8_t *)try_exe_decompressed_buffer;
		}
	}
	else
	{
		exe_decompressed_size = 0;
		exe_decompressed_buffer = (uint8_t *)malloc(exe_decompressed_size ? exe_decompressed_size : 1);
		if (!exe_decompressed_buffer)
		{
			psf_status(state, "Out of memory allocating dummy buffer for exe section.\n", 1);
			goto error_free_tags;
		}
	}

	free(exe_compressed_buffer);
	exe_compressed_buffer = NULL;

	psf_status(state, "Passing exe and reserved back out.\n", 1);

	if (state->load_target(state->load_context, exe_decompressed_buffer, exe_decompressed_size, reserved_buffer, reserved_size))
	{
		psf_status(state, "Data handler returned an error.\n", 1);
		goto error_free_tags;
	}

	free(reserved_buffer);
	reserved_buffer = NULL;

	free(exe_decompressed_buffer);
	exe_decompressed_buffer = NULL;

	n = 2;
	snprintf(state->lib_name_temp, 31, "_lib%u", n);
	state->lib_name_temp[31] = '\0';
	tag = find_tag(tags, state->lib_name_temp);
	while (tag)
	{
		if (psf_want_status(state))
		{
			psf_status(state, "Found ", 1);
			psf_status(state, tag->name, 0);
			psf_status(state, ": ", 0);
			psf_status(state, tag->value, 0);
			psf_status(state, "\n", 0);
		}
		if (psf_load_internal(state, tag->value) < 0) goto error_free_tags;
		++n;
		snprintf(state->lib_name_temp, 31, "_lib%u", n);
		state->lib_name_temp[31] = '\0';
		tag = find_tag(tags, state->lib_name_temp);
	}

done:
	if (file) state->file_callbacks->fclose(file);

	free_tags(tags);

	--state->depth;

	return header_buffer[3];

error_free_tags:
	free_tags(tags);
error_free_buffers:
	if (exe_compressed_buffer) free(exe_compressed_buffer);
	if (exe_decompressed_buffer) free(exe_decompressed_buffer);
	if (reserved_buffer) free(reserved_buffer);
	if (tag_buffer) free(tag_buffer);
error_close_file:
	if (file) state->file_callbacks->fclose(file);
	return -1;
}

int psf_want_status(psf_load_state * state)
{
	return !!state->status_target;
}

void psf_status(psf_load_state * state, const char * message, int indent)
{
	if (state->status_target)
	{
		if (indent)
		{
			char indent[16];
			int indent_level = state->depth > 1 ? state->depth - 1 : 0;
			memset(indent, ' ', indent_level);
			indent[indent_level] = '\0';
			state->status_target(state->status_context, indent);
		}
		state->status_target(state->status_context, message);
	}
}

} }

extern "C" USF_API int usf_load_psf(
    const char *uri, const psf_file_callbacks *files, uint8_t version,
    psf_load_callback load, void *load_context, psf_info_callback info,
    void *info_context, int nested_tags, psf_status_callback status, void *status_context) {
    return psf::psf_load(uri, files, version, load, load_context, info, info_context,
                         nested_tags, status, status_context);
}
