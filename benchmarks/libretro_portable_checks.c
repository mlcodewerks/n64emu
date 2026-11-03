/* A tiny IPL3 loop exercises core loading, CPU execution, VI yields and unload. */
#include "../deps/libretro-common/include/libretro.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYMBOL(h, n) GetProcAddress(h, n)
static DWORD WINAPI timeout(void *unused) {
    (void)unused;
    Sleep(15000);
    fputs("libretro check timed out\n", stderr);
    ExitProcess(3);
    return 0;
}
#else
#include <dlfcn.h>
#include <unistd.h>
#define OPEN(p) dlopen(p, RTLD_NOW)
#define SYMBOL(h, n) dlsym(h, n)
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "libretro check failed: %s\n", #x);                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define LOAD(n)                                                                                    \
    __typeof__(&n) p_##n = (__typeof__(&n))SYMBOL(library, #n);                                    \
    CHECK(p_##n)
static unsigned videos;
static void log_message(enum retro_log_level level, const char *format, ...) {
    if (level < RETRO_LOG_ERROR)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}
static bool environment(unsigned command, void *data) {
    if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        ((struct retro_log_callback *)data)->log = log_message;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *v = (struct retro_variable *)data;
        if (!strcmp(v->key, "wtfn64_renderer"))
            v->value = "softrdp";
        else if (!strcmp(v->key, "wtfn64_hle"))
            v->value = "off";
        else
            return false;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE) {
        *(bool *)data = false;
        return true;
    }
    return command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT;
}
static void video(const void *p, unsigned w, unsigned h, size_t pitch) {
    (void)p;
    (void)w;
    (void)h;
    (void)pitch;
    ++videos;
}
static size_t audio(const int16_t *data, size_t frames) {
    (void)data;
    return frames;
}
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id) {
    (void)p;
    (void)d;
    (void)i;
    (void)id;
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
#ifdef _WIN32
    HANDLE timer = CreateThread(NULL, 0, timeout, NULL, 0, NULL);
    CHECK(timer);
    CloseHandle(timer);
#else
    alarm(15);
#endif
    void *library = (void *)OPEN(argv[1]);
    CHECK(library);
    LOAD(retro_api_version);
    LOAD(retro_get_system_info);
    LOAD(retro_set_environment);
    LOAD(retro_set_video_refresh);
    LOAD(retro_set_audio_sample_batch);
    LOAD(retro_set_input_poll);
    LOAD(retro_set_input_state);
    LOAD(retro_init);
    LOAD(retro_deinit);
    LOAD(retro_load_game);
    LOAD(retro_run);
    LOAD(retro_unload_game);
    LOAD(retro_get_memory_size);
    LOAD(retro_get_memory_data);
    CHECK(p_retro_api_version() == RETRO_API_VERSION);
    struct retro_system_info info = {0};
    p_retro_get_system_info(&info);
    CHECK(info.library_name && info.valid_extensions);
    p_retro_set_environment(environment);
    p_retro_set_video_refresh(video);
    p_retro_set_audio_sample_batch(audio);
    p_retro_set_input_poll(poll);
    p_retro_set_input_state(input);
    p_retro_init();
    unsigned char rom[4096] = {0x80, 0x37, 0x12, 0x40};
    rom[0x3e] = 'E';
    const unsigned boot[] = {0x3c08a440, 0x3409020d, 0xad090018, 0x34090c15, 0xad09001c,
                             0x34090002, 0xad09000c, 0x1000ffff, 0};
    for (unsigned i = 0; i < sizeof(boot) / 4; ++i)
        for (unsigned b = 0; b < 4; ++b)
            rom[0x40 + i * 4 + b] = boot[i] >> ((3 - b) * 8);
    struct retro_game_info game = {0};
    game.data = rom;
    game.size = sizeof(rom);
    CHECK(p_retro_load_game(&game));
    p_retro_run();
    p_retro_run();
    CHECK(videos >= 2);
    CHECK(p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) == 0x800000);
    CHECK(p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    p_retro_unload_game();
    p_retro_deinit();
    puts("libretro load, portable CPU/VI execution, RAM and unload checks passed.");
    return 0;
}
