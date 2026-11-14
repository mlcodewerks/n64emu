/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#include <stdlib.h>
#include <ctype.h>

#include <compat/strl.h>

/* Implementation of strlcpy()/strlcat() based on OpenBSD. */

#ifndef __MACH__

size_t strlcpy(char *dest, const char *source, size_t size)
{
   size_t src_size = 0;
   size_t        n = size;

   if (n)
      while (--n && (*dest++ = *source++)) src_size++;

   if (!n)
   {
      if (size) *dest = '\0';
      while (*source++) src_size++;
   }

   return src_size;
}

size_t strlcat(char *dest, const char *source, size_t size)
{
   size_t len = strlen(dest);

   dest += len;

   if (len > size)
      size = 0;
   else
      size -= len;

   return len + strlcpy(dest, source, size);
}
#endif

char *strldup(const char *s, size_t n)
{
   char *dst = (char*)malloc(sizeof(char) * (n + 1));
   strlcpy(dst, s, n);
   return dst;
}
