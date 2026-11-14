/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#include <stdint.h>
#include <stdlib.h>

#include <memalign.h>

void *memalign_alloc(size_t boundary, size_t size)
{
   void **place   = NULL;
   uintptr_t addr = 0;
   void *ptr      = (void*)malloc(boundary + size + sizeof(uintptr_t));
   if (!ptr)
      return NULL;

   addr           = ((uintptr_t)ptr + sizeof(uintptr_t) + boundary)
      & ~(boundary - 1);
   place          = (void**)addr;
   place[-1]      = ptr;

   return (void*)addr;
}

void memalign_free(void *ptr)
{
   void **p = NULL;
   if (!ptr)
      return;

   p = (void**)ptr;
   free(p[-1]);
}

void *memalign_alloc_aligned(size_t size)
{
#if defined(__x86_64__) || defined(__LP64) || defined(__IA64__) || defined(_M_X64) || defined(_M_X64) || defined(_WIN64)
   return memalign_alloc(64, size);
#elif defined(__i386__) || defined(__i486__) || defined(__i686__) || defined(GEKKO) || defined(_M_IX86)
   return memalign_alloc(32, size);
#else
   return memalign_alloc(32, size);
#endif
}
