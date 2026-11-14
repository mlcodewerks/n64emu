/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_FILE_STREAM_H
#define __LIBRETRO_SDK_FILE_STREAM_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include <sys/types.h>

#include <libretro.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include <boolean.h>

#include <stdarg.h>
#include <vfs/vfs_implementation.h>

#define FILESTREAM_REQUIRED_VFS_VERSION 2

RETRO_BEGIN_DECLS

typedef struct RFILE RFILE;

#define FILESTREAM_REQUIRED_VFS_VERSION 2

void filestream_vfs_init(const struct retro_vfs_interface_info* vfs_info);

int64_t filestream_get_size(RFILE *stream);

int64_t filestream_truncate(RFILE *stream, int64_t length);

/**
 * filestream_open:
 * @path               : path to file
 * @mode               : file mode to use when opening (read/write)
 * @bufsize            : optional buffer size (-1 or 0 to use default)
 *
 * Opens a file for reading or writing, depending on the requested mode.
 * Returns a pointer to an RFILE if opened successfully, otherwise NULL.
 **/
RFILE* filestream_open(const char *path, unsigned mode, unsigned hints);

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position);

int64_t filestream_read(RFILE *stream, void *data, int64_t len);

int64_t filestream_write(RFILE *stream, const void *data, int64_t len);

int64_t filestream_tell(RFILE *stream);

void filestream_rewind(RFILE *stream);

int filestream_close(RFILE *stream);

int64_t filestream_read_file(const char *path, void **buf, int64_t *len);

char* filestream_gets(RFILE *stream, char *s, size_t len);

int filestream_getc(RFILE *stream);

int filestream_vscanf(RFILE *stream, const char* format, va_list *args);

int filestream_scanf(RFILE *stream, const char* format, ...);

int filestream_eof(RFILE *stream);

bool filestream_write_file(const char *path, const void *data, int64_t size);

int filestream_putc(RFILE *stream, int c);

int filestream_vprintf(RFILE *stream, const char* format, va_list args);

int filestream_printf(RFILE *stream, const char* format, ...);

int filestream_error(RFILE *stream);

int filestream_flush(RFILE *stream);

int filestream_delete(const char *path);

int filestream_rename(const char *old_path, const char *new_path);

const char* filestream_get_path(RFILE *stream);

bool filestream_exists(const char *path);

/* Returned pointer must be freed by the caller. */
char* filestream_getline(RFILE *stream);

libretro_vfs_implementation_file* filestream_get_vfs_handle(RFILE *stream);

RETRO_END_DECLS

#endif
