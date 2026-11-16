#include "core/api.h"
#include "core/runtime.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs_implementation.h>

static n64emu_host host;
static char *system_directory;
static bool initialized, loaded, changed;
static const struct retro_core_option_v2_definition *definitions;
static const char *selected[64];
static unsigned option_count;

/* Reuse bundled UTF-8/large-file IO without involving a host VFS service. */
static struct retro_vfs_file_handle *file_open(const char *p, unsigned m, unsigned h)
{ return (struct retro_vfs_file_handle *)retro_vfs_file_open_impl(p, m, h); }
static int file_close(struct retro_vfs_file_handle *f)
{ return retro_vfs_file_close_impl((libretro_vfs_implementation_file *)f); }
static int64_t file_size(struct retro_vfs_file_handle *f)
{ return retro_vfs_file_size_impl((libretro_vfs_implementation_file *)f); }
static int64_t file_tell(struct retro_vfs_file_handle *f)
{ return retro_vfs_file_tell_impl((libretro_vfs_implementation_file *)f); }
static int64_t file_seek(struct retro_vfs_file_handle *f, int64_t o, int w)
{ return retro_vfs_file_seek_impl((libretro_vfs_implementation_file *)f, o, w); }
static int64_t file_read(struct retro_vfs_file_handle *f, void *p, uint64_t n)
{ return retro_vfs_file_read_impl((libretro_vfs_implementation_file *)f, p, n); }
static int64_t file_write(struct retro_vfs_file_handle *f, const void *p, uint64_t n)
{ return retro_vfs_file_write_impl((libretro_vfs_implementation_file *)f, p, n); }
static int file_flush(struct retro_vfs_file_handle *f)
{ return retro_vfs_file_flush_impl((libretro_vfs_implementation_file *)f); }
static struct retro_vfs_interface files = {
    .open = file_open, .close = file_close, .size = file_size, .tell = file_tell,
    .seek = file_seek, .read = file_read, .write = file_write, .flush = file_flush
};

static void av_convert(n64emu_av *out, const struct retro_system_av_info *in)
{
    *out = (n64emu_av){in->geometry.base_width, in->geometry.base_height,
        in->geometry.max_width, in->geometry.max_height, in->geometry.aspect_ratio,
        in->timing.fps, in->timing.sample_rate};
}
static void log_message(enum retro_log_level level, const char *format, ...)
{
    if (!host.log) return;
    char buffer[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    host.log(host.userdata, (unsigned)level, buffer);
}
static bool rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
    if (!host.rumble) return false;
    host.rumble(host.userdata, port, effect == RETRO_RUMBLE_STRONG, strength);
    return true;
}
/* Compatibility with the core's existing settings, file, audio and renderer
 * internals is private here. Hosts use api.h and never negotiate libretro. */
static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
        struct retro_vfs_interface_info *info = data;
        if (info->required_interface_version > 1) return false;
        info->iface = &files; return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = system_directory; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_message; return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        ((struct retro_rumble_interface *)data)->set_rumble_state = rumble; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        definitions = ((struct retro_core_options_v2 *)data)->definitions;
        option_count = 0;
        while (option_count < 64 && definitions[option_count].key) {
            selected[option_count] = definitions[option_count].default_value;
            ++option_count;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = data;
        for (unsigned i = 0; i < option_count; ++i)
            if (!strcmp(var->key, definitions[i].key)) {
                var->value = selected[i]; return true;
            }
        var->value = NULL; return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = changed; changed = false; return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        n64emu_av av;
        av_convert(&av, data);
        if (host.av_changed) host.av_changed(host.userdata, &av);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        return true;
    default: return false;
    }
}
static void video(const void *pixels, unsigned width, unsigned height, size_t pitch)
{
    if (host.video) host.video(host.userdata, pixels, width, height, pitch);
}
static size_t audio(const int16_t *samples, size_t frames)
{
    if (host.audio) host.audio(host.userdata, samples, frames);
    return frames;
}
static void poll(void) {}
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
    n64emu_input state = {0};
    if (port >= 4 || !host.input) return 0;
    state = host.input(host.userdata, port);
    if (device == RETRO_DEVICE_JOYPAD && id < 16)
        return (state.buttons >> id) & 1;
    if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
        return id == RETRO_DEVICE_ID_ANALOG_X ? state.stick_x : state.stick_y;
    return 0;
}
bool n64emu_init(const n64emu_host *callbacks)
{
    if (initialized || !callbacks) return false;
    const char *path = callbacks->system_directory ? callbacks->system_directory : ".";
    system_directory = malloc(strlen(path) + 1);
    if (!system_directory) return false;
    strcpy(system_directory, path);
    host = *callbacks;
    option_count = 0; definitions = NULL; changed = false;
    core_runtime_set_environment(environment);
    core_runtime_set_video_refresh(video);
    core_runtime_set_audio_sample_batch(audio);
    core_runtime_set_input_poll(poll);
    core_runtime_set_input_state(input);
    core_runtime_init();
    initialized = true;
    for (unsigned i = 0; i < 4; ++i) n64emu_set_controller(i, true);
    return true;
}
void n64emu_deinit(void)
{
    if (!initialized) return;
    n64emu_unload();
    core_runtime_deinit();
    free(system_directory); system_directory = NULL;
    memset(&host, 0, sizeof(host));
    initialized = false; definitions = NULL; option_count = 0;
}
bool n64emu_load(const char *path, const void *rom, size_t size)
{
    if (!initialized || loaded || (!path && !rom) || size > UINT32_MAX) return false;
    struct retro_game_info game = {path, rom, size, NULL};
    loaded = core_runtime_load_game(&game);
    return loaded;
}
void n64emu_unload(void)
{
    if (loaded) core_runtime_unload_game();
    loaded = false;
}
void n64emu_run(void) { if (loaded) core_runtime_run(); }
void n64emu_reset(void) { if (loaded) core_runtime_reset(); }
void n64emu_get_av(n64emu_av *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!loaded) return;
    struct retro_system_av_info av;
    core_runtime_get_system_av_info(&av);
    av_convert(out, &av);
}
void n64emu_set_controller(unsigned port, bool connected)
{
    if (initialized && port < 4)
        core_runtime_set_controller_port_device(port, connected ? RETRO_DEVICE_JOYPAD : RETRO_DEVICE_NONE);
}
unsigned n64emu_option_count(void) { return option_count; }
bool n64emu_get_option(unsigned index, n64emu_option *out)
{
    if (!out || index >= option_count) return false;
    const struct retro_core_option_v2_definition *d = &definitions[index];
    memset(out, 0, sizeof(*out));
    out->key = d->key; out->label = d->desc; out->info = d->info;
    out->category = d->category_key; out->value = selected[index];
    while (out->count < 128 && d->values[out->count].value) {
        unsigned i = out->count++;
        out->values[i] = d->values[i].value;
        out->labels[i] = d->values[i].label ? d->values[i].label : d->values[i].value;
    }
    return true;
}
bool n64emu_set_option(const char *key, const char *value)
{
    if (!initialized || !key || !value) return false;
    if (!strcmp(key, "wtfn64_timing")) {
        if (!strcmp(value, "accurate") || !strcmp(value, "cycle") || !strcmp(value, "cycle-based"))
            value = "on";
        else if (!strcmp(value, "compatible") || !strcmp(value, "fast") || !strcmp(value, "no-cache"))
            value = "off";
    }
    for (unsigned i = 0; i < option_count; ++i)
        if (!strcmp(key, definitions[i].key))
            for (unsigned j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX && definitions[i].values[j].value; ++j)
                if (!strcmp(value, definitions[i].values[j].value)) {
                    selected[i] = definitions[i].values[j].value;
                    changed = true; return true;
                }
    return false;
}
size_t n64emu_state_size(void) { return loaded ? core_runtime_serialize_size() : 0; }
bool n64emu_save_state(void *data, size_t size) { return loaded && core_runtime_serialize(data, size); }
bool n64emu_load_state(const void *data, size_t size) { return loaded && core_runtime_unserialize(data, size); }
void *n64emu_save_memory(size_t *size)
{
    if (size) *size = loaded ? core_runtime_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
    return loaded ? core_runtime_get_memory_data(RETRO_MEMORY_SAVE_RAM) : NULL;
}
