#include "presenter.h"
#include <libretro.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern retro_environment_t environ_cb;
extern retro_video_refresh_t video_cb;
extern retro_log_printf_t log_cb;
extern bool libretro_swap_buffer;

static struct frame_buffer frame;
static unsigned width = 640, height = 480;

void vdac_init(struct n64video_config *config) { (void)config; }
void vdac_read(struct frame_buffer *fb, bool alpha) { (void)fb; (void)alpha; }
struct rgba *vdac_acquire(uint32_t w, uint32_t h, uint32_t *pitch, bool read)
{
    struct retro_framebuffer fb = {0};
    fb.width = w;
    fb.height = h;
    fb.access_flags = RETRO_MEMORY_ACCESS_WRITE |
        (read ? RETRO_MEMORY_ACCESS_READ : 0);
    if (!environ_cb ||
        !environ_cb(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER, &fb) ||
        !fb.data || fb.width != w || fb.height != h ||
        fb.format != RETRO_PIXEL_FORMAT_XRGB8888 ||
        fb.pitch < (size_t)w * sizeof(struct rgba) ||
        fb.pitch % sizeof(struct rgba) ||
        fb.pitch / sizeof(struct rgba) > UINT32_MAX ||
        (h && fb.pitch > SIZE_MAX / h))
        return NULL;
    *pitch = (uint32_t)(fb.pitch / sizeof(struct rgba));
    return fb.data;
}
void vdac_write(struct frame_buffer *fb) { frame = *fb; }
void vdac_sync(bool invalid)
{
    if (!invalid && frame.pixels && frame.width && frame.height)
        libretro_swap_buffer = true;
}
void vdac_close(void)
{
    memset(&frame, 0, sizeof(frame));
    width = 640;
    height = 480;
    libretro_swap_buffer = false;
}

void softrdp_present(bool new_frame)
{
    if (!new_frame || !frame.pixels)
    {
        video_cb(NULL, width, height, 0);
        memset(&frame, 0, sizeof(frame));
        return;
    }
    width = frame.width;
    height = frame.height;
    video_cb(frame.pixels, width, height, (size_t)frame.pitch * sizeof(struct rgba));
    /* Frontend framebuffer storage expires when retro_run returns. */
    memset(&frame, 0, sizeof(frame));
}

#define DEFINE_MESSAGE(name, level) \
void name(const char *fmt, ...) \
{ \
    char text[2048]; \
    va_list args; \
    va_start(args, fmt); \
    vsnprintf(text, sizeof(text), fmt, args); \
    va_end(args); \
    if (log_cb) log_cb(level, "%s\n", text); \
}
DEFINE_MESSAGE(msg_error, RETRO_LOG_ERROR)
DEFINE_MESSAGE(msg_warning, RETRO_LOG_WARN)
DEFINE_MESSAGE(msg_debug, RETRO_LOG_DEBUG)
