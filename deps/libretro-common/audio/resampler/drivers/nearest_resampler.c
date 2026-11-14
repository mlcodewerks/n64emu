/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../../../LICENSE for the license terms.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include <audio/audio_resampler.h>

typedef struct rarch_nearest_resampler
{
   float fraction;
} rarch_nearest_resampler_t;

static void resampler_nearest_process(
      void *re_, struct resampler_data *data)
{
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)re_;
   audio_frame_float_t  *inp     = (audio_frame_float_t*)data->data_in;
   audio_frame_float_t  *inp_max = (audio_frame_float_t*)inp + data->input_frames;
   audio_frame_float_t  *outp    = (audio_frame_float_t*)data->data_out;
   float                   ratio = 1.0 / data->ratio;

   while (inp != inp_max)
   {
      while (re->fraction > 1)
      {
         *outp++       = *inp;
         re->fraction -= ratio;
      }
      re->fraction++;
      inp++;
   }

   data->output_frames = (outp - (audio_frame_float_t*)data->data_out);
}

static void resampler_nearest_free(void *re_)
{
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)re_;
   if (re)
      free(re);
}

static void *resampler_nearest_init(const struct resampler_config *config,
      double bandwidth_mod,
      enum resampler_quality quality,
      resampler_simd_mask_t mask)
{
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)
      calloc(1, sizeof(rarch_nearest_resampler_t));
   if (!re)
      return NULL;
   re->fraction = 0;
   return re;
}

retro_resampler_t nearest_resampler = {
   resampler_nearest_init,
   resampler_nearest_process,
   resampler_nearest_free,
   RESAMPLER_API_VERSION,
   "nearest",
   "nearest"
};
