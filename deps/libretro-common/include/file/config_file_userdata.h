/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../LICENSE for the license terms.
 */

#ifndef _LIBRETRO_SDK_CONFIG_FILE_USERDATA_H
#define _LIBRETRO_SDK_CONFIG_FILE_USERDATA_H

#include <string.h>

#include <file/config_file.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

struct config_file_userdata
{
   config_file_t *conf;
   const char *prefix[2];
};

int config_userdata_get_float(void *userdata, const char *key_str,
      float *value, float default_value);

int config_userdata_get_int(void *userdata, const char *key_str,
      int *value, int default_value);

int config_userdata_get_hex(void *userdata, const char *key_str,
      unsigned *value, unsigned default_value);

int config_userdata_get_float_array(void *userdata, const char *key_str,
      float **values, unsigned *out_num_values,
      const float *default_values, unsigned num_default_values);

int config_userdata_get_int_array(void *userdata, const char *key_str,
      int **values, unsigned *out_num_values,
      const int *default_values, unsigned num_default_values);

int config_userdata_get_string(void *userdata, const char *key_str,
      char **output, const char *default_output);

void config_userdata_free(void *ptr);

RETRO_END_DECLS

#endif
