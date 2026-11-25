#include "sr_field_output.h"

#include <stdlib.h>
#include <string.h>

void sr_field_output_reset(sr_field_output *output)
{
    if (!output) return;
    free(output->pixels);
    memset(output, 0, sizeof(*output));
}

bool sr_field_output_compose(sr_field_output *output,
                             const sr_rgba8 *field_pixels,
                             uint32_t width, uint32_t height,
                             uint32_t stride, bool interlaced, bool field, bool bob,
                             uint32_t origin, uint32_t alternate_row_bytes,
                             const sr_rgba8 **pixels, uint32_t *output_height)
{
    if (!output || !field_pixels || !width || !height || stride < width ||
        !pixels || !output_height) return false;

    if (!interlaced) {
        /* Invalidate retained fields. */
        output->width = 0;
        output->height = 0;
        memset(output->bounds, 0, sizeof(output->bounds));
        *pixels = field_pixels;
        *output_height = height;
        return true;
    }

    if (height > UINT32_MAX / 2u ||
        (size_t)width > SIZE_MAX / (size_t)(height * 2u) / sizeof(sr_rgba8))
        return false;

    const uint32_t full_height = height * 2u;
    bool discard_previous = output->alternate_row_bytes != alternate_row_bytes;
    if (output->width != width || output->height != full_height) {
        sr_rgba8 *buffer = realloc(output->pixels,
                                  (size_t)width * full_height * sizeof(*buffer));
        if (!buffer) return false;
        output->pixels = buffer;
        output->width = width;
        output->height = full_height;
        discard_previous = true;
    }
    if (discard_previous) {
        output->shared_image = false;
    } else if (alternate_row_bytes) {
        const uint32_t distance = origin >= output->origin ?
            origin - output->origin : output->origin - origin;
        const bool adjacent = field != output->field && distance == alternate_row_bytes;
        /* Adjacent origins identify an image whose fields share alternating
         * source rows. A swap between fields leaves no matching opposite
         * field, so show the incoming field across both row parities. */
        output->shared_image |= adjacent;
        discard_previous = output->shared_image && distance && !adjacent;
    }
    output->have_opposite_field = !discard_previous &&
        (output->have_opposite_field || field != output->field);
    output->field = field;
    output->origin = origin;
    output->alternate_row_bytes = alternate_row_bytes;
    const bool repeat_field = !output->have_opposite_field;
    if (repeat_field) memset(output->bounds, 0, sizeof(output->bounds));

    /* Field 0 occupies odd rows. */
    const uint32_t parity = !field;
    for (uint32_t y = 0; y < height; y++)
    {
        memcpy(output->pixels + ((size_t)y * 2u + parity) * width,
               field_pixels + (size_t)y * stride,
               (size_t)width * sizeof(*field_pixels));
        if (bob || repeat_field)
            memcpy(output->pixels + ((size_t)y * 2u + !parity) * width,
                   field_pixels + (size_t)y * stride,
                   (size_t)width * sizeof(*field_pixels));
    }

    *pixels = output->pixels;
    *output_height = full_height;
    return true;
}
