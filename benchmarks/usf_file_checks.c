/* File-level lazyusf2 ABI runner. Link with psflib and zlib; media stays local. */
#include "../src/usf/usf.h"
#include <math.h>
#include <psflib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYMBOL(h, n) GetProcAddress(h, n)
#define strcasecmp _stricmp
#else
#include <dlfcn.h>
#include <strings.h>
#define OPEN(p) dlopen(p, RTLD_NOW)
#define SYMBOL(h, n) dlsym(h, n)
#endif
#define APIS(X)                                                                                    \
    X(usf_get_state_size)                                                                          \
    X(usf_clear) X(usf_shutdown) X(usf_restart) X(usf_upload_section) X(usf_set_compare)           \
        X(usf_set_fifo_full) X(usf_set_hle_audio) X(usf_render) X(usf_render_resampled)
#define DECLARE(n) static __typeof__(&n) p_##n;
APIS(DECLARE)
static void *file_open(void *context, const char *name) {
    (void)context;
    return fopen(name, "rb");
}
static size_t file_read(void *p, size_t s, size_t n, void *f) { return fread(p, s, n, (FILE *)f); }
static int file_seek(void *f, int64_t offset, int origin) {
#ifdef _WIN32
    return _fseeki64((FILE *)f, offset, origin);
#else
    return fseeko((FILE *)f, offset, origin);
#endif
}
static int file_close(void *f) { return fclose((FILE *)f); }
static long file_tell(void *f) { return ftell((FILE *)f); }
static const psf_file_callbacks files = {"/\\",     NULL,       file_open, file_read,
                                         file_seek, file_close, file_tell};
static int load(void *state, const uint8_t *exe, size_t exe_size, const uint8_t *reserved,
                size_t size) {
    (void)exe;
    return exe_size ? -1 : p_usf_upload_section(state, reserved, size);
}
static int enabled(const char *v) {
    return *v && strcasecmp(v, "0") && strcasecmp(v, "false") && strcasecmp(v, "no");
}
static int tag(void *state, const char *name, const char *value) {
    if (!strcasecmp(name, "_enablecompare"))
        p_usf_set_compare(state, enabled(value));
    else if (!strcasecmp(name, "_enablefifofull"))
        p_usf_set_fifo_full(state, enabled(value));
    else if (!strcasecmp(name, "title") || !strcasecmp(name, "length"))
        printf("%s=%s\n", name, value);
    return 0;
}
static void status(void *context, const char *message) {
    (void)context;
    fputs(message, stdout);
}
static void put16(FILE *f, unsigned value) {
    fputc(value & 255, f);
    fputc((value >> 8) & 255, f);
}
static void put32(FILE *f, unsigned value) {
    put16(f, value);
    put16(f, value >> 16);
}
static void wav_header(FILE *f, unsigned rate, unsigned frames) {
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, rate);
    put32(f, rate * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, frames * 4);
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / frequency.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
#endif
}
static uint64_t hash_samples(uint64_t hash, const int16_t *audio, size_t frames) {
    for (size_t i = 0; i < frames * 2; ++i) {
        hash ^= (uint16_t)audio[i] & 255;
        hash *= UINT64_C(1099511628211);
        hash ^= (uint16_t)audio[i] >> 8;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
int main(int argc, char **argv) {
    if (argc < 5) {
        fputs("usage: usf_file_checks engine track seconds hle [wav] [output-rate]\n", stderr);
        return 2;
    }
    void *library = (void *)OPEN(argv[1]);
    if (!library) {
        fputs("Cannot load engine\n", stderr);
        return 1;
    }
#define RESOLVE(n)                                                                                 \
    p_##n = (__typeof__(&n))SYMBOL(library, #n);                                                   \
    if (!p_##n) {                                                                                  \
        fprintf(stderr, "Missing %s\n", #n);                                                       \
        return 1;                                                                                  \
    }
    APIS(RESOLVE)
    void *state = malloc(p_usf_get_state_size());
    if (!state)
        return 1;
    p_usf_clear(state);
    p_usf_set_hle_audio(state, atoi(argv[4]) != 0);
    const double start = now();
    if (psf_load(argv[2], &files, 0x21, load, state, tag, state, 1, status, NULL) < 0) {
        fputs("PSF load failed\n", stderr);
        return 1;
    }
    int32_t native_rate = 0;
    const char *error = p_usf_render(state, NULL, 0, &native_rate);
    if (error) {
        fprintf(stderr, "rate discovery: %s\n", error);
        return 1;
    }
    const int32_t output_rate = argc > 6 ? atoi(argv[6]) : 0,
                  rate = output_rate ? output_rate : native_rate;
    printf("native_rate=%d output_rate=%d hle=%d\n", native_rate, rate, atoi(argv[4]) != 0);
    fflush(stdout);
    const size_t frames = (size_t)(atof(argv[3]) * rate);
    if (!rate || frames > UINT32_MAX / 4)
        return 2;
    FILE *wav = argc > 5 ? fopen(argv[5], "wb") : NULL;
    if (argc > 5 && !wav)
        return 1;
    if (wav)
        wav_header(wav, rate, (unsigned)frames);
    int16_t block[2048];
    uint64_t hash = UINT64_C(14695981039346656037), window_hash = hash;
    size_t nonzero = 0, window_start = (size_t)rate * 2,
           window_frames = frames > window_start ? (size_t)rate : 0;
    if (frames > window_start && window_frames > frames - window_start)
        window_frames = frames - window_start;
    int peak = 0;
    long double energy = 0;
    for (size_t done = 0; done < frames;) {
        size_t count = frames - done;
        if (count > 1024)
            count = 1024;
        error = output_rate ? p_usf_render_resampled(state, block, count, output_rate)
                            : p_usf_render(state, block, count, &native_rate);
        if (error) {
            fprintf(stderr, "decode at frame %zu: %s\n", done, error);
            return 1;
        }
        if (!output_rate && native_rate != rate) {
            fputs("DAC rate changed during WAV capture\n", stderr);
            return 1;
        }
        hash = hash_samples(hash, block, count);
        for (size_t i = 0; i < count * 2; ++i) {
            int sample = block[i];
            if (sample)
                ++nonzero;
            int absolute = sample < 0 ? -sample : sample;
            if (absolute > peak)
                peak = absolute;
            energy += (long double)sample * sample;
        }
        size_t begin = done > window_start ? done : window_start, end = done + count;
        if (end > window_start + window_frames)
            end = window_start + window_frames;
        if (end > begin)
            window_hash = hash_samples(window_hash, block + (begin - done) * 2, end - begin);
        if (wav)
            fwrite(block, 4, count, wav);
        done += count;
    }
    if (wav)
        fclose(wav);
    printf("frames=%zu nonzero_samples=%zu peak=%d rms=%.3f fnv1a=%016llx elapsed=%.3fs\n", frames,
           nonzero, peak, frames ? sqrt((double)(energy / (frames * 2))) : 0,
           (unsigned long long)hash, now() - start);
    fflush(stdout);
    if (window_frames) {
        p_usf_restart(state);
        error = output_rate ? p_usf_render_resampled(state, NULL, window_start, output_rate)
                            : p_usf_render(state, NULL, window_start, &native_rate);
        uint64_t seek_hash = UINT64_C(14695981039346656037);
        for (size_t done = 0; !error && done < window_frames;) {
            size_t count = window_frames - done;
            if (count > 1024)
                count = 1024;
            error = output_rate ? p_usf_render_resampled(state, block, count, output_rate)
                                : p_usf_render(state, block, count, &native_rate);
            if (!error)
                seek_hash = hash_samples(seek_hash, block, count);
            done += count;
        }
        if (error || seek_hash != window_hash) {
            fprintf(stderr, "restart/skip mismatch: %s (%016llx != %016llx)\n",
                    error ? error : "PCM", (unsigned long long)seek_hash,
                    (unsigned long long)window_hash);
            return 1;
        }
        puts("restart/skip reproduces sequential PCM at 2 seconds.");
    }
    p_usf_shutdown(state);
    free(state);
    return nonzero ? 0 : 1;
}
