/* Synthetic ROM-less USF: CPU submits a known stereo AI DMA in a loop. */
#include "../src/usf/usf.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYMBOL(h, n) GetProcAddress(h, n)
#else
#include <dlfcn.h>
#define OPEN(p) dlopen(p, RTLD_NOW)
#define SYMBOL(h, n) dlsym(h, n)
#endif
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "USF check failed at line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#ifdef PARALLAX_USF_LINKED
#define LOAD(n) auto p_##n = &n
#else
#define LOAD(n)                                                                                    \
    __typeof__(&n) p_##n = (__typeof__(&n))SYMBOL(library, #n);                                    \
    CHECK(p_##n)
#endif
static void put32(unsigned char *p, unsigned v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
static unsigned char *state_image(int bias, unsigned ram_size) {
    unsigned char *image = (unsigned char *)calloc(1, ram_size + 0x275c);
    if (!image)
        return NULL;
    put32(image, 0x23d8a6c8);
    put32(image + 4, ram_size);
    image[8] = 0x80;
    image[9] = 0x37;
    image[10] = 0x12;
    image[11] = 0x40;
    image[8 + 0x3e] = 'E';
    put32(image + 72, 10000);
    put32(image + 76, 0x80000000);
    put32(image + 592 + 12 * 4, 0x34000000);
    put32(image + 592 + 11 * 4, 0xffffffff);
    // Put the 8 MB image's samples above 4 MB to exercise Expansion Pak RAM.
    unsigned sample_address = ram_size == 0x800000 ? 0x701000 : 0x1000;
    const unsigned program[] = {0x3c08a450, 0x3c090000 | (sample_address >> 16), 0x35291000,
                                0x340a044f, 0xad0a0010, 0x340b0010, 0xad090000, 0xad0b0004,
                                0x8d0c000c, 0x3c0d4000, 0x018d6024, 0x1580fffc, 0x00000000,
                                0x08000006, 0x00000000};
    for (unsigned i = 0; i < sizeof(program) / sizeof(program[0]); ++i)
        put32(image + 0x75c + i * 4, program[i]);
    for (unsigned i = 0; i < 4; ++i) {
        int16_t l = (int16_t)(bias + (i + 1) * 100), r = -l;
        put32(image + 0x75c + sample_address + i * 4, ((unsigned)(uint16_t)l << 16) | (uint16_t)r);
    }
    return image;
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: usf_checks <USF shared library>\n");
        return 2;
    }
#ifndef PARALLAX_USF_LINKED
    void *library = (void *)OPEN(argv[1]);
    CHECK(library);
#endif
    LOAD(usf_get_state_size);
    LOAD(usf_clear);
    LOAD(usf_shutdown);
    LOAD(usf_restart);
    LOAD(usf_upload_save_state);
    LOAD(usf_upload_section);
    LOAD(usf_render);
    LOAD(usf_render_resampled);
    LOAD(usf_set_compare);
    LOAD(usf_set_fifo_full);
    LOAD(usf_set_hle_audio);
    void *a = malloc(p_usf_get_state_size()), *b = malloc(p_usf_get_state_size());
    CHECK(a && b);
    p_usf_clear(a);
    p_usf_clear(b);
    unsigned char *image = state_image(0, 0x400000), *image_b = state_image(1000, 0x800000);
    CHECK(image && image_b);
    // Exercise ordered sparse upload using the same reserved-section layout as psf_load.
    unsigned char *section = (unsigned char *)malloc(0x40275c + 24);
    CHECK(section);
    put32(section, 0);
    put32(section + 4, 0x34365253);
    put32(section + 8, 0x40275c);
    put32(section + 12, 0);
    memcpy(section + 16, image, 0x40275c);
    put32(section + 16 + 0x40275c, 0);
    CHECK(!p_usf_upload_section(a, section, 0x40275c + 20));
    // A separate ROM bank supplies B's samples through real CPU cartridge reads.
    unsigned char rom_section[108] = {0};
    put32(rom_section, 0x34365253);
    put32(rom_section + 4, 64);
    put32(rom_section + 12, 0x80371240); // word-swapped cartridge image
    rom_section[12 + (0x3e ^ 3)] = 'E';
    put32(rom_section + 76, 16);
    put32(rom_section + 80, 0x1000);
    memcpy(rom_section + 84, image_b + 0x75c + 0x701000, 16);
    memset(image_b + 0x75c + 0x701000, 0, 16);
    const unsigned copy_rom[] = {0x3c0cb000, 0x358c1000, 0x3c0e8070, 0x35ce1000,
                                 0x8d8d0000, 0xadcd0000, 0x8d8d0004, 0xadcd0004,
                                 0x8d8d0008, 0xadcd0008, 0x8d8d000c, 0xadcd000c};
    memmove(image_b + 0x75c + sizeof(copy_rom), image_b + 0x75c, 15 * 4);
    for (unsigned i = 0; i < sizeof(copy_rom) / 4; ++i)
        put32(image_b + 0x75c + i * 4, copy_rom[i]);
    put32(image_b + 0x75c + sizeof(copy_rom) + 13 * 4, 0x08000012);
    CHECK(!p_usf_upload_section(b, rom_section, sizeof(rom_section)));
    p_usf_upload_save_state(b, image_b, 0x80275c);
    p_usf_set_compare(a, 0);
    p_usf_set_fifo_full(a, 0);
    p_usf_set_hle_audio(a, 0);
    int32_t rate = 0;
    const char *error = p_usf_render(a, NULL, 0, &rate);
    if (error)
        fprintf(stderr, "render: %s\n", error);
    CHECK(!error && rate > 44000 && rate < 44200);
    int16_t audio[32];
    CHECK(!p_usf_render(a, audio, 2, &rate));
    CHECK(audio[0] == 100 && audio[1] == -100 && audio[2] == 200 && audio[3] == -200);
    CHECK(!p_usf_render(a, NULL, 1, &rate));
    CHECK(!p_usf_render(a, audio, 3, &rate));
    CHECK(audio[0] == 400 && audio[2] == 100 && audio[4] == 200);
    CHECK(!p_usf_render(b, audio, 1, &rate));
    CHECK(audio[0] == 1100 && audio[1] == -1100);
    CHECK(!p_usf_render(a, audio, 1, &rate));
    CHECK(audio[0] == 300 && audio[1] == -300);
    p_usf_restart(a);
    CHECK(!p_usf_render(a, audio, 1, &rate));
    CHECK(audio[0] == 100 && audio[1] == -100);
    p_usf_set_compare(a, 1);
    p_usf_set_fifo_full(a, 1);
    p_usf_set_hle_audio(a, 1);
    error = p_usf_render(a, audio, 8, &rate);
    if (error)
        fprintf(stderr, "flags render: %s\n", error);
    CHECK(!error);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(audio[i * 2] == (int16_t)((i % 4 + 1) * 100));
    p_usf_restart(a);
    CHECK(!p_usf_render_resampled(a, audio, 16, 48000));
    p_usf_restart(a);
    CHECK(!p_usf_render_resampled(a, NULL, 7, 48000));
    int16_t tail[18];
    CHECK(!p_usf_render_resampled(a, tail, 9, 48000));
    CHECK(!memcmp(tail, audio + 14, sizeof(tail)));
    put32(section + 12, 0xffffffff);
    CHECK(p_usf_upload_section(a, section, 0x40275c + 20) == -1);
    p_usf_restart(a);
    CHECK(!p_usf_render(a, audio, 1, &rate));
    CHECK(audio[0] == 100);
    // A trimmed rip cannot visit a slow PI-completion path absent from its image.
    // Exercise both immediate PI completion and the compare/FIFO length/8 delay.
    void *c = malloc(p_usf_get_state_size());
    unsigned char *pi_image = state_image(1000, 0x400000);
    CHECK(c && pi_image);
    p_usf_clear(c);
    const unsigned copy_pi[] = {
        0x3c0ea460, 0x340f1000, 0xadcf0000, 0x3c0f1000, 0x35ef1000, 0xadcf0004,
        0x340f000f, 0xadcf000c, 0, 0, 0, 0, 0x8dcf0010, 0x31ef0001,
        0x11e00004, 0, 0x3c0d8000, 0x340f03e7, 0xadaf1000
    };
    memmove(pi_image + 0x75c + sizeof(copy_pi), pi_image + 0x75c, 15 * 4);
    for (unsigned i = 0; i < sizeof(copy_pi) / 4; ++i)
        put32(pi_image + 0x75c + i * 4, copy_pi[i]);
    put32(pi_image + 0x75c + sizeof(copy_pi) + 13 * 4, 0x08000019);
    memset(pi_image + 0x75c + 0x1000, 0, 16);
    CHECK(!p_usf_upload_section(c, rom_section, sizeof(rom_section)));
    p_usf_upload_save_state(c, pi_image, 0x40275c);
    for (int delayed = 0; delayed <= 1; ++delayed) {
        p_usf_set_compare(c, delayed);
        p_usf_set_fifo_full(c, delayed);
        CHECK(!p_usf_render(c, audio, 1, &rate));
        CHECK(audio[0] == 1100 && audio[1] == -1100);
    }
    free(pi_image);

    // Dropping the saved AI DMA also drops its latched interrupt. Enabling
    // interrupts must not enter an exception handler missing from the rip.
    unsigned char *irq_image = state_image(0, 0x400000);
    CHECK(irq_image);
    const unsigned enable_irq[] = {0x3c0e2000, 0x35ce0401, 0x408e6000, 0};
    memmove(irq_image + 0x75c + sizeof(enable_irq), irq_image + 0x75c, 15 * 4);
    for (unsigned i = 0; i < sizeof(enable_irq) / 4; ++i)
        put32(irq_image + 0x75c + i * 4, enable_irq[i]);
    put32(irq_image + 0x75c + sizeof(enable_irq) + 13 * 4, 0x0800000a);
    put32(irq_image + 984 + 2 * 4, 4); // MI_INTR_AI
    put32(irq_image + 984 + 3 * 4, 4); // mask all other device interrupts
    put32(irq_image + 592 + 13 * 4, 0x400);
    p_usf_upload_save_state(c, irq_image, 0x40275c);
    p_usf_set_compare(c, 0);
    p_usf_set_fifo_full(c, 0);
    CHECK(!p_usf_render(c, audio, 1, &rate));
    CHECK(audio[0] == 100 && audio[1] == -100);
    /* Legacy timing and startup graphics must work without any game data.
     * Each prefix corrupts the known PCM marker if its condition fails. */
    const unsigned count_clock[] = {
        0x400e4800, 0x400f4800, 0x01ee7823, 0x340d0002, 0x11ed0004, 0,
        0x3c0d8000, 0x340f03e7, 0xadaf1000
    };
    const unsigned immediate_si[] = {
        0x3c0ea480, 0x340f2000, 0xadcf0000, 0x3c0f1fc0, 0x35ef07c0,
        0xadcf0010, 0x8dcf0018, 0x31ef0001, 0x11e00004, 0,
        0x3c0d8000, 0x340f03e7, 0xadaf1000
    };
    const unsigned startup_graphics[] = {
        0x3c0ea404, 0x340f0105, 0xadcf0010, 0x3c0ea430, 0x8dcf0008,
        0x31ef0020, 0x15e00004, 0, 0x3c0d8000, 0x340f03e7, 0xadaf1000
    };
    const unsigned *prefixes[] = {count_clock, immediate_si, startup_graphics, startup_graphics};
    const unsigned counts[] = {sizeof(count_clock)/4, sizeof(immediate_si)/4, sizeof(startup_graphics)/4, sizeof(startup_graphics)/4};
    for (unsigned test = 0; test < 4; ++test) {
        unsigned char *regression = state_image(0, 0x400000);
        CHECK(regression);
        memmove(regression + 0x75c + counts[test]*4, regression + 0x75c, 15*4);
        for (unsigned i = 0; i < counts[test]; ++i)
            put32(regression + 0x75c + i*4, prefixes[test][i]);
        put32(regression + 0x75c + (counts[test]+13)*4, 0x08000006 + counts[test]);
        if (test >= 2) put32(regression + 0x75c + 0x400000 + 0xfc0, 1);
        if (test == 3) {
            /* A graphics task must also signal DP when SP IRQs are disabled. */
            put32(regression + 0x75c + 4, 0x340f0005);
            put32(regression + 0x75c + 12, 0x400f6800); /* MFC0 t7,CAUSE */
            put32(regression + 0x75c + 16, 0);
            put32(regression + 0x75c + 20, 0x31ef0400);
            put32(regression + 984 + 3*4, 0x20); /* MI_INTR_MASK_DP */
        }
        p_usf_upload_save_state(c, regression, 0x40275c);
        for (int hle = 0; hle <= 1; ++hle) {
            p_usf_set_hle_audio(c, hle);
            p_usf_restart(c);
            CHECK(!p_usf_render(c, audio, 1, &rate));
            if (audio[0] != 100 || audio[1] != -100)
                fprintf(stderr, "USF startup regression %u (audio HLE %d)\n", test, hle);
            CHECK(audio[0] == 100 && audio[1] == -100);
        }
        free(regression);
    }
    p_usf_shutdown(c);
    free(c);
    free(irq_image);
    p_usf_shutdown(a);
    p_usf_shutdown(a);
    p_usf_shutdown(b);
    free(a);
    free(b);
    free(image);
    free(image_b);
    free(section);
    printf("USF sparse upload, PCM, skip, restart, contexts, flags, resampling, DMA, legacy clock/SI and graphics-stub checks passed "
           "(%d Hz).\n",
           rate);
    return 0;
}
