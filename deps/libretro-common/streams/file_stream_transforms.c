/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#include <string.h>
#include <stdarg.h>

#include <libretro.h>
#include <streams/file_stream.h>

RFILE* rfopen(const char *path, const char *mode)
{
   RFILE          *output  = NULL;
   unsigned int retro_mode = RETRO_VFS_FILE_ACCESS_READ;
   bool position_to_end    = false;

   if (strstr(mode, "r"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_READ;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }
   else if (strstr(mode, "w"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE;
      if (strstr(mode, "+"))
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE;
   }
   else if (strstr(mode, "a"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE |
         RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      position_to_end = true;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }

   output = filestream_open(path, retro_mode,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (output && position_to_end)
      filestream_seek(output, 0, RETRO_VFS_SEEK_POSITION_END);

   return output;
}

int rfclose(RFILE* stream)
{
   return filestream_close(stream);
}

int64_t rftell(RFILE* stream)
{
   return filestream_tell(stream);
}

int64_t rfseek(RFILE* stream, int64_t offset, int origin)
{
   int seek_position = -1;
   switch (origin)
   {
      case SEEK_SET:
         seek_position = RETRO_VFS_SEEK_POSITION_START;
         break;
      case SEEK_CUR:
         seek_position = RETRO_VFS_SEEK_POSITION_CURRENT;
         break;
      case SEEK_END:
         seek_position = RETRO_VFS_SEEK_POSITION_END;
         break;
   }

   return filestream_seek(stream, offset, seek_position);
}

int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream)
{
   return (filestream_read(stream, buffer, elem_size * elem_count) / elem_size);
}

char *rfgets(char *buffer, int maxCount, RFILE* stream)
{
   return filestream_gets(stream, buffer, maxCount);
}

int rfgetc(RFILE* stream)
{
   return filestream_getc(stream);
}

int64_t rfwrite(void const* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream)
{
   return filestream_write(stream, buffer, elem_size * elem_count);
}

int rfputc(int character, RFILE * stream)
{
    return filestream_putc(stream, character);
}

int64_t rfflush(RFILE * stream)
{
    return filestream_flush(stream);
}

int rfprintf(RFILE * stream, const char * format, ...)
{
   int result;
   va_list vl;
   va_start(vl, format);
   result = filestream_vprintf(stream, format, vl);
   va_end(vl);
   return result;
}

int rferror(RFILE* stream)
{
   return filestream_error(stream);
}

int rfeof(RFILE* stream)
{
   return filestream_eof(stream);
}

int rfscanf(RFILE * stream, const char * format, ...)
{
   int result;
   va_list vl;
   va_start(vl, format);
   result = filestream_vscanf(stream, format, &vl);
   va_end(vl);
   return result;
}
