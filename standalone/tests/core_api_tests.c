#include "core/api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned audio_frames, video_frames;
static void audio(void *p, const int16_t *samples, size_t count)
{ (void)p; (void)samples; audio_frames += (unsigned)count; }
static void video(void *p, const void *pixels, unsigned w, unsigned h, size_t pitch)
{ (void)p; if (pixels && w && h && pitch >= w * 4) ++video_frames; }
static unsigned option_index(const char *key)
{
    for(unsigned i=0;i<n64emu_option_count();++i) {
        n64emu_option option;
        if(n64emu_get_option(i,&option) && !strcmp(option.key,key))return i;
    }
    return UINT_MAX;
}
int main(int argc, char **argv)
{
    n64emu_host host = {0};
    host.audio = audio; host.video = video;
    CHECK(!n64emu_load(NULL, NULL, 0));
    CHECK(!n64emu_init(NULL));
    CHECK(n64emu_init(&host));
    CHECK(!n64emu_init(&host));
    CHECK(n64emu_cheat_validate("80000000 0001;81000002 0400"));
    CHECK(!n64emu_cheat_validate("80000000 XXXX"));
    CHECK(!n64emu_cheat_set(0, true, "80000000 0001"));
    CHECK(n64emu_option_count() == 13);
    CHECK(n64emu_option_category_count() == 1);
    n64emu_option_category category;
    CHECK(n64emu_get_option_category(0, &category));
    CHECK(!strcmp(category.key, "hacks") && !strcmp(category.label, "Hacks"));
    CHECK(!n64emu_get_option_category(1, &category));
    n64emu_option option;
    unsigned deinterlace=option_index("wtfn64_angrylion_deinterlace");
    CHECK(n64emu_get_option(deinterlace, &option));
    CHECK(!strcmp(option.key, "wtfn64_angrylion_deinterlace"));
    CHECK(!strcmp(option.value, "weave") && option.count == 2);
    CHECK(n64emu_set_option(option.key, "bob"));
    CHECK(n64emu_get_option(deinterlace, &option) && !strcmp(option.value, "bob"));
    CHECK(!n64emu_set_option(option.key, "invalid"));
    CHECK(!n64emu_set_option("wtfn64_timing", "off"));
    CHECK(!n64emu_set_option("wtfn64_timing", "on"));
    CHECK(n64emu_get_option(option_index("n64emu_cycle_timing"), &option));
    CHECK(!strcmp(option.key, "n64emu_cycle_timing") && !strcmp(option.value, "on"));
    CHECK(n64emu_set_option(option.key, "off"));
    CHECK(!n64emu_set_option(option.key, "fast"));
    CHECK(n64emu_set_option(option.key, "on"));
    CHECK(n64emu_get_option(option_index("n64emu_cpu_overclock"), &option));
    CHECK(!strcmp(option.value, "1x") && strstr(option.label, "(restart required)"));
    CHECK(n64emu_set_option("n64emu_cpu_overclock", "2x"));
    CHECK(!n64emu_set_option("n64emu_cpu_overclock", "3x"));
    CHECK(n64emu_set_option("n64emu_cpu_overclock", "1x"));
    const char *switches[] = {"n64emu_ignore_tlb_exceptions", "n64emu_disable_expansion_pak"};
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(n64emu_get_option(option_index(switches[i]), &option));
        CHECK(!strcmp(option.value, "disabled") && strstr(option.label, "(restart required)"));
        CHECK(n64emu_set_option(option.key, "enabled"));
        CHECK(n64emu_set_option(option.key, "disabled"));
    }
    for (unsigned i = 0; i < n64emu_option_count(); ++i) {
        CHECK(n64emu_get_option(i, &option));
        if (!strcmp(option.key, "n64emu_cpu_overclock") || !strcmp(option.key, switches[0]) || !strcmp(option.key, switches[1]))
            CHECK(option.category && !strcmp(option.category, "hacks"));
        else CHECK(option.category == NULL);
        CHECK(strcmp(option.key, "wtfn64_timing"));
    }
    CHECK(!n64emu_get_option(100, &option));
    CHECK(!n64emu_set_option(NULL, "off"));
    unsigned char bad[8] = {0};
    CHECK(!n64emu_load("invalid.z64", bad, sizeof(bad)));
    CHECK(n64emu_state_size() == 0);
    CHECK(!n64emu_save_state(bad, sizeof(bad)));
    CHECK(!n64emu_load_state(bad, sizeof(bad)));
    size_t size = 99;
    CHECK(!n64emu_save_memory(&size) && size == 0);
    n64emu_run(); n64emu_reset(); n64emu_unload();
    n64emu_deinit(); n64emu_deinit();
    CHECK(n64emu_option_category_count() == 0);
    CHECK(n64emu_init(&host));
    CHECK(n64emu_get_option(option_index("wtfn64_angrylion_deinterlace"), &option) && !strcmp(option.value, "weave"));
    if (argc > 1) {
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            CHECK(n64emu_load(argv[1], NULL, 0));
            CHECK(!n64emu_cheat_set(0, true, NULL));
            CHECK(n64emu_cheat_set(0, false, "807ff000 0001"));
            CHECK(n64emu_cheat_set(0, true, NULL));
            CHECK(n64emu_cheat_set(0, false, NULL));
            n64emu_cheat_reset();
            CHECK(!n64emu_load(argv[1], NULL, 0));
            n64emu_av av;
            n64emu_get_av(&av);
            CHECK(av.fps > 40 && av.fps < 70 && av.sample_rate == 44100);
            for (unsigned frame = 0; frame < 12; ++frame) n64emu_run();
            size_t state_size = n64emu_state_size();
            void *state = malloc(state_size);
            CHECK(state && state_size > 16000000);
            CHECK(n64emu_save_state(state, state_size));
            n64emu_run();
            CHECK(!n64emu_load_state(state, 8));
            CHECK(n64emu_load_state(state, state_size));
            n64emu_run();
            free(state);
            CHECK(n64emu_save_memory(&size) && size > 0);
            n64emu_set_controller(1, false);
            CHECK(n64emu_set_option("wtfn64_vi_filter", "disabled"));
            n64emu_run();
            n64emu_reset(); n64emu_run();
            n64emu_unload();
        }
        CHECK(audio_frames > 0);
        printf("ROM/state/reset/reload passed: audio=%u video=%u\n", audio_frames, video_frames);
    }
    n64emu_deinit();
    puts("native API lifecycle/options/failure recovery passed");
    return 0;
}
