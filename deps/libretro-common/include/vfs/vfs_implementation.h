/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_VFS_IMPLEMENTATION_H
#define __LIBRETRO_SDK_VFS_IMPLEMENTATION_H

#include <stdio.h>
#include <stdint.h>
#include <libretro.h>
#include <retro_environment.h>
#include <vfs/vfs.h>

RETRO_BEGIN_DECLS

libretro_vfs_implementation_file *retro_vfs_file_open_impl(const char *path, unsigned mode, unsigned hints);

int retro_vfs_file_close_impl(libretro_vfs_implementation_file *stream);

int retro_vfs_file_error_impl(libretro_vfs_implementation_file *stream);

int64_t retro_vfs_file_size_impl(libretro_vfs_implementation_file *stream);

int64_t retro_vfs_file_truncate_impl(libretro_vfs_implementation_file *stream, int64_t length);

int64_t retro_vfs_file_tell_impl(libretro_vfs_implementation_file *stream);

int64_t retro_vfs_file_seek_impl(libretro_vfs_implementation_file *stream, int64_t offset, int seek_position);

int64_t retro_vfs_file_read_impl(libretro_vfs_implementation_file *stream, void *s, uint64_t len);

int64_t retro_vfs_file_write_impl(libretro_vfs_implementation_file *stream, const void *s, uint64_t len);

int retro_vfs_file_flush_impl(libretro_vfs_implementation_file *stream);

int retro_vfs_file_remove_impl(const char *path);

int retro_vfs_file_rename_impl(const char *old_path, const char *new_path);

const char *retro_vfs_file_get_path_impl(libretro_vfs_implementation_file *stream);

int retro_vfs_stat_impl(const char *path, int32_t *size);

int retro_vfs_mkdir_impl(const char *dir);

libretro_vfs_implementation_dir *retro_vfs_opendir_impl(const char *dir, bool include_hidden);

bool retro_vfs_readdir_impl(libretro_vfs_implementation_dir *dirstream);

const char *retro_vfs_dirent_get_name_impl(libretro_vfs_implementation_dir *dirstream);

bool retro_vfs_dirent_is_dir_impl(libretro_vfs_implementation_dir *dirstream);

int retro_vfs_closedir_impl(libretro_vfs_implementation_dir *dirstream);

RETRO_END_DECLS

#endif
