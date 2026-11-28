/* Native, statically linkable n64emu host interface. No frontend ABI dependency.
 * Single emulator per process; call all functions on the emulation thread.
 * Callbacks are synchronous and must not throw or reenter the core. */
#ifndef N64EMU_API_H
#define N64EMU_API_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum n64emu_button {
    N64EMU_A, N64EMU_B, N64EMU_L, N64EMU_START,
    N64EMU_UP, N64EMU_DOWN, N64EMU_LEFT, N64EMU_RIGHT,
    N64EMU_C_DOWN, N64EMU_C_UP, N64EMU_C_LEFT, N64EMU_C_RIGHT,
    N64EMU_Z, N64EMU_R, N64EMU_UNUSED_14, N64EMU_UNUSED_15
};
typedef struct n64emu_input {
    uint16_t buttons; /* 1 << n64emu_button */
    int16_t stick_x, stick_y; /* -32768..32767; positive Y points down */
} n64emu_input;
typedef struct n64emu_av {
    unsigned width, height, max_width, max_height;
    float aspect;
    double fps, sample_rate;
} n64emu_av;
typedef struct n64emu_option {
    const char *key, *label, *info, *category, *value;
    unsigned count;
    const char *values[128], *labels[128];
} n64emu_option;
typedef struct n64emu_option_category {
    const char *key, *label, *info;
} n64emu_option_category;
typedef struct n64emu_host {
    void *userdata;
    /* XRGB8888; NULL pixels duplicates the previous frame. */
    void (*video)(void *, const void *, unsigned, unsigned, size_t);
    void (*audio)(void *, const int16_t *, size_t); /* 44100 Hz stereo frames */
    n64emu_input (*input)(void *, unsigned port);
    void (*rumble)(void *, unsigned port, bool strong, uint16_t strength);
    void (*log)(void *, unsigned level, const char *message); /* 0 debug .. 3 error */
    void (*av_changed)(void *, const n64emu_av *);
    const char *system_directory; /* copied at init; UTF-8 */
} n64emu_host;
bool n64emu_init(const n64emu_host *host);
void n64emu_deinit(void);
bool n64emu_load(const char *path, const void *rom, size_t size);
void n64emu_unload(void);
void n64emu_run(void);
void n64emu_reset(void);
void n64emu_get_av(n64emu_av *av);
void n64emu_set_controller(unsigned port, bool connected);
unsigned n64emu_option_count(void);
bool n64emu_get_option(unsigned index, n64emu_option *option);
unsigned n64emu_option_category_count(void);
bool n64emu_get_option_category(unsigned index, n64emu_option_category *category);
bool n64emu_set_option(const char *key, const char *value);
size_t n64emu_state_size(void);
bool n64emu_save_state(void *data, size_t size);
bool n64emu_load_state(const void *data, size_t size);
void *n64emu_save_memory(size_t *size); /* valid until unload */
/* GameShark address/value pairs, separated by whitespace, '+', ';' or ','.
 * A NULL code toggles an existing index. A failed replacement leaves it intact.
 * Disabling or clearing codes stops future writes. Codes clear on unload. */
bool n64emu_cheat_validate(const char *codes);
bool n64emu_cheat_set(unsigned index, bool enabled, const char *codes);
void n64emu_cheat_reset(void);
void n64emu_cheat_press_button(void); /* for the next frame */
#ifdef __cplusplus
}
#endif
#endif
