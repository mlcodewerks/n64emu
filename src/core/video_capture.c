#include "core/video_capture.h"

#include <stdlib.h>
#include <string.h>

/* Dummy video capture backend
 *
 * Returns a black frame
 */
struct dummy_video_capture
{
    /* BGR frame size (no stride) */
    size_t size;
};

static core_result dummy_init(void** vcap, const char* config)
{
    (void)config;
    struct dummy_video_capture* dummy = malloc(sizeof(*dummy));
    if (dummy == NULL) {
        *vcap = NULL;
        return CORE_NO_MEMORY;
    }

    memset(dummy, 0, sizeof(*dummy));

    *vcap = dummy;
    return CORE_OK;
}

static void dummy_release(void* vcap)
{
    free(vcap);
}

static core_result dummy_open(void* vcap, unsigned int width, unsigned int height)
{
    struct dummy_video_capture* dummy = (struct dummy_video_capture*)vcap;
    dummy->size = 3 * width * height;
    return CORE_OK;
}

static void dummy_close(void* vcap)
{
    struct dummy_video_capture* dummy = (struct dummy_video_capture*)vcap;
    dummy->size = 0;
}

static core_result dummy_grab_image(void* vcap, void* data)
{
    struct dummy_video_capture* dummy = (struct dummy_video_capture*)vcap;
    memset(data, 0, dummy->size);
    return CORE_OK;
}

static const struct video_capture_backend_interface g_idummy_video_capture_backend =
{
    "dummy",
    dummy_init,
    dummy_release,
    dummy_open,
    dummy_close,
    dummy_grab_image
};

/* exported video capture backends */
#if defined(N64EMU_OPENCV)
extern const struct video_capture_backend_interface g_iopencv_video_capture_backend;
#endif


static const struct video_capture_backend_interface* const video_capture_backends[] =
{
#if defined(N64EMU_OPENCV)
    &g_iopencv_video_capture_backend,
#endif
    &g_idummy_video_capture_backend,
    NULL /* sentinel - must be last element */
};


const struct video_capture_backend_interface* get_video_capture_backend(const char* name)
{
    const struct video_capture_backend_interface* const* i;

    /* passing NULL or empty string gives the dummy video input backend interface */
    if (!name || *name == '\0') { return &g_idummy_video_capture_backend; }

    /* iterate through interfaces to find matching name */
    for (i = video_capture_backends; (*i) != NULL; ++i) {
        if (strcmp((*i)->name, name) == 0) { return (*i); }
    }

    return NULL;
}
