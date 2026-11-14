/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_MEMMAP_H
#define _LIBRETRO_MEMMAP_H

#include <stdio.h>
#include <stdint.h>

#if defined(PSP) || defined(PS2) || defined(GEKKO) || defined(VITA) || defined(_XBOX) || defined(_3DS) || defined(WIIU) || defined(SWITCH) || defined(HAVE_LIBNX) || defined(__PS3__) || defined(__PSL1GHT__)
/* No mman available */
#elif defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#include <errno.h>
#include <io.h>
#else
#define HAVE_MMAN
#include <sys/mman.h>
#endif

#if !defined(HAVE_MMAN) || defined(_WIN32)
void* mmap(void *addr, size_t len, int mmap_prot, int mmap_flags, int fildes, size_t off);

int munmap(void *addr, size_t len);

int mprotect(void *addr, size_t len, int prot);
#endif

int memsync(void *start, void *end);

int memprotect(void *addr, size_t len);

#endif
