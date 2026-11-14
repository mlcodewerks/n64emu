/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_VFS_H
#define __LIBRETRO_SDK_VFS_H

#include <retro_common_api.h>
#include <boolean.h>

#ifdef RARCH_INTERNAL
#ifndef VFS_FRONTEND
#define VFS_FRONTEND
#endif
#endif

RETRO_BEGIN_DECLS

#ifdef _WIN32
typedef void* HANDLE;
#endif

#ifdef HAVE_CDROM
typedef struct
{
   int64_t byte_pos;
   char *cue_buf;
   size_t cue_len;
   unsigned cur_lba;
   unsigned last_frame_lba;
   unsigned char cur_min;
   unsigned char cur_sec;
   unsigned char cur_frame;
   unsigned char cur_track;
   unsigned char last_frame[2352];
   char drive;
   bool last_frame_valid;
} vfs_cdrom_t;
#endif

enum vfs_scheme
{
   VFS_SCHEME_NONE = 0,
   VFS_SCHEME_CDROM
};

#ifndef __WINRT__
#ifdef VFS_FRONTEND
struct retro_vfs_file_handle
#else
struct libretro_vfs_implementation_file
#endif
{
#ifdef HAVE_CDROM
   vfs_cdrom_t cdrom; /* int64_t alignment */
#endif
   int64_t size;
   uint64_t mappos;
   uint64_t mapsize;
   FILE *fp;
#ifdef _WIN32
   HANDLE fh;
#endif
   char *buf;
   char* orig_path;
   uint8_t *mapped;
   int fd;
   unsigned hints;
   enum vfs_scheme scheme;
};
#endif

/* Replace the following symbol with something appropriate
 * to signify the file is being compiled for a front end instead of a core.
 * This allows the same code to act as reference implementation
 * for VFS and as fallbacks for when the front end does not provide VFS functionality.
 */

#ifdef VFS_FRONTEND
typedef struct retro_vfs_file_handle libretro_vfs_implementation_file;
#else
typedef struct libretro_vfs_implementation_file libretro_vfs_implementation_file;
#endif

#ifdef VFS_FRONTEND
typedef struct retro_vfs_dir_handle libretro_vfs_implementation_dir;
#else
typedef struct libretro_vfs_implementation_dir libretro_vfs_implementation_dir;
#endif

RETRO_END_DECLS

#endif
