/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#include <ctype.h>

#include <compat/posix_string.h>

#ifdef _WIN32

#undef strcasecmp
#undef strdup
#undef isblank
#undef strtok_r
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <compat/strl.h>

#include <string.h>

int retro_strcasecmp__(const char *a, const char *b)
{
   while (*a && *b)
   {
      int a_ = tolower(*a);
      int b_ = tolower(*b);

      if (a_ != b_)
         return a_ - b_;

      a++;
      b++;
   }

   return tolower(*a) - tolower(*b);
}

char *retro_strdup__(const char *orig)
{
   size_t len = strlen(orig) + 1;
   char *ret  = (char*)malloc(len);
   if (!ret)
      return NULL;

   strlcpy(ret, orig, len);
   return ret;
}

int retro_isblank__(int c)
{
   return (c == ' ') || (c == '\t');
}

char *retro_strtok_r__(char *str, const char *delim, char **saveptr)
{
   char *first = NULL;
   if (!saveptr || !delim)
      return NULL;

   if (str)
      *saveptr = str;

   do
   {
      char *ptr = NULL;
      first = *saveptr;
      while (*first && strchr(delim, *first))
         *first++ = '\0';

      if (*first == '\0')
         return NULL;

      ptr = first + 1;

      while (*ptr && !strchr(delim, *ptr))
         ptr++;

      *saveptr = ptr + (*ptr ? 1 : 0);
      *ptr     = '\0';
   } while (strlen(first) == 0);

   return first;
}

#endif
