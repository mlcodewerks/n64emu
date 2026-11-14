/* lazyusf2-compatible audio interface. SPDX-License-Identifier: BSD-3-Clause */
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
/* PSF container callbacks and loader, derived from psflib.
 * Copyright (c) 2012-2020 Christopher Snowhill. BSD-3-Clause. */
typedef struct psf_file_callbacks
{
    /* list of characters which act as path separators, null terminated */
    const char * path_separators;

    /* Context pointer for fopen callback */
    void * context;

    /* opens the file pointed to by path read-only in binary mode, 
     * accepts UTF-8 encoding, returns file handle */
    void * (* fopen )(void * context, const char * path);

    /* reads to specified buffer, returns count of size bytes read */
    size_t (* fread )(void * buffer, size_t size, size_t count, void * handle);

    /* returns zero on success, -1 on error */
    int    (* fseek )(void * handle, int64_t, int);

    /* returns zero on success, -1 on error */
    int    (* fclose)(void * handle);

    /* returns current file offset */
    long   (* ftell )(void * handle);
} psf_file_callbacks;

/* Receives exe and reserved bodies, with deepest _lib->_lib->_lib etc head first, followed
 * by the specified file itself, then followed by numbered library chains.
 *
 * Example:
 *
 * outermost file, a.psf, has _lib=b.psf and _lib2=c.psf tags; b.psf has _lib=d.psf:
 *
 * the callback will be passed the contents of d.psf, then b.psf, then a.psf, then c.psf
 *
 * Returning non-zero indicates an error.
 */
typedef int (* psf_load_callback)(void * context, const uint8_t * exe, size_t exe_size,
                                  const uint8_t * reserved, size_t reserved_size);

/* Receives the name/value pairs from the outermost file, one at a time.
 *
 * Returning non-zero indicates an error.
 */
typedef int (* psf_info_callback)(void * context, const char * name, const char * value);

/* Receives any status messages, which should be appended to one big message. */
typedef void (* psf_status_callback)(void * context, const char * message);

/* Loads the PSF chain starting with uri, opened using file_callbacks, passes the tags,
 * if any, to the optional info_target callback, then passes all loaded data to load_target
 * with the highest priority file first.
 *
 * allowed_version may be set to zero to probe the file version, but is not recommended when
 * actually loading files into an emulator state.
 *
 * Both load_target and info_target are optional.
 *
 * Returns negative on error, PSF version on success.
 */
USF_API int usf_load_psf( const char * uri, const psf_file_callbacks * file_callbacks, uint8_t allowed_version,
              psf_load_callback load_target, void * load_context, psf_info_callback info_target,
              void * info_context, int info_want_nested_tags, psf_status_callback status_target,
              void * status_context);


USF_API size_t usf_get_state_size(void);
USF_API void usf_clear(void *state);
USF_API void usf_set_compare(void *state, int enable);
USF_API void usf_set_fifo_full(void *state, int enable);
/* Legacy lazyusf2 ABI: retained as a no-op; all RSP audio executes natively. */
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
