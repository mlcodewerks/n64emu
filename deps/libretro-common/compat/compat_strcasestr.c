/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#include <ctype.h>

#include <compat/strcasestr.h>

/* Pretty much strncasecmp. */
static int casencmp(const char *a, const char *b, size_t n)
{
   size_t i;

   for (i = 0; i < n; i++)
   {
      int a_lower = tolower(a[i]);
      int b_lower = tolower(b[i]);
      if (a_lower != b_lower)
         return a_lower - b_lower;
   }

   return 0;
}

char *strcasestr_retro__(const char *haystack, const char *needle)
{
   size_t i, search_off;
   size_t hay_len    = strlen(haystack);
   size_t needle_len = strlen(needle);

   if (needle_len > hay_len)
      return NULL;

   search_off = hay_len - needle_len;
   for (i = 0; i <= search_off; i++)
      if (!casencmp(haystack + i, needle, needle_len))
         return (char*)haystack + i;

   return NULL;
}
