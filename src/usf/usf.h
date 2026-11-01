/* lazyusf2-compatible audio interface. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef N64EMU_USF_H
#define N64EMU_USF_H
#include <stddef.h>
#include <stdint.h>
typedef struct usf_state usf_state_t;
typedef struct usf_state_helper usf_state_helper_t;
#ifdef _WIN32
#ifdef N64EMU_USF
#define USF_API __declspec(dllexport)
#else
#define USF_API __declspec(dllimport)
#endif
#else
#define USF_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
USF_API size_t usf_get_state_size(void);
USF_API void usf_clear(void *state);
USF_API void usf_set_compare(void *state, int enable);
USF_API void usf_set_fifo_full(void *state, int enable);
USF_API void usf_set_hle_audio(void *state, int enable);
USF_API void usf_set_trimming_mode(void *state, int enable);
USF_API void *usf_get_rom_coverage_barray(void *state);
USF_API void *usf_get_ram_coverage_barray(void *state);
USF_API int usf_upload_section(void *state, const uint8_t *data, size_t size);
USF_API void usf_upload_rom(void *state, const uint8_t *data, size_t size);
USF_API void usf_upload_save_state(void *state, const uint8_t *data, size_t size);
/* count is stereo frames. NULL discards exactly count frames; NULL/zero
 * discovers the DAC rate while retaining the generated block for playback. */
USF_API const char *usf_render(void *state, int16_t *buffer, size_t count, int32_t *sample_rate);
USF_API const char *usf_render_resampled(void *state, int16_t *buffer, size_t count,
                                         int32_t sample_rate);
USF_API void usf_restart(void *state);
USF_API void usf_shutdown(void *state);
#ifdef __cplusplus
}
#endif
#endif
