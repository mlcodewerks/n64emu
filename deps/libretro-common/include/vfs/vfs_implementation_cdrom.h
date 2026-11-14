/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_VFS_IMPLEMENTATION_CDROM_H
#define __LIBRETRO_SDK_VFS_IMPLEMENTATION_CDROM_H

#include <vfs/vfs.h>
#include <cdrom/cdrom.h>

RETRO_BEGIN_DECLS

int64_t retro_vfs_file_seek_cdrom(libretro_vfs_implementation_file *stream, int64_t offset, int whence);

void retro_vfs_file_open_cdrom(
      libretro_vfs_implementation_file *stream,
      const char *path, unsigned mode, unsigned hints);

int retro_vfs_file_close_cdrom(libretro_vfs_implementation_file *stream);

int64_t retro_vfs_file_tell_cdrom(libretro_vfs_implementation_file *stream);

int64_t retro_vfs_file_read_cdrom(libretro_vfs_implementation_file *stream,
      void *s, uint64_t len);

int retro_vfs_file_error_cdrom(libretro_vfs_implementation_file *stream);

const cdrom_toc_t* retro_vfs_file_get_cdrom_toc(void);

const vfs_cdrom_t* retro_vfs_file_get_cdrom_position(const libretro_vfs_implementation_file *stream);

RETRO_END_DECLS

#endif
