/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_MEMALIGN_H
#define _LIBRETRO_MEMALIGN_H

#include <stddef.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

void *memalign_alloc(size_t boundary, size_t size);

void *memalign_alloc_aligned(size_t size);

void memalign_free(void *ptr);

RETRO_END_DECLS

#endif
