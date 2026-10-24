#ifndef PARALLAX_AUDIO_H
#define PARALLAX_AUDIO_H

#include <stddef.h>

struct audio_out_backend_interface
{
    /* Allow the backend to be notified of sample frequency.
     */
    void (*set_frequency)(void* aout, unsigned int frequency);

    /* Push samples to be played by the backend
     */
    void (*push_samples)(void* aout, const void* samples, size_t size);
    /* Advance the DAC while the FIFO is empty, in source stereo frames. */
    void (*push_idle_samples)(void* aout, size_t frames);
};

#ifdef __cplusplus
extern "C" {
#endif

void init_audio_libretro(unsigned max_frames);
void deinit_audio_libretro(void);

extern const struct audio_out_backend_interface g_libretro_audio_backend;

#ifdef __cplusplus
}
#endif

#endif
