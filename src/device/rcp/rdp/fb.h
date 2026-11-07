
#ifndef N64EMU_DEVICE_RCP_RDP_FB_H
#define N64EMU_DEVICE_RCP_RDP_FB_H

#include <stdint.h>

#include "core/backend.h"

struct memory;
struct rdram;
struct r4300_core;

/* Compatibility image list only. Native notifications enumerate every retained
 * color/depth view into the complete RDRAM page map below. */
enum { FB_INFOS_COUNT = 6 };
enum { FB_DIRTY_PAGES_COUNT = 0x800 };

struct rdp_timing;
struct fb
{
    struct rdp_timing *timing;
    struct memory* mem;
    struct rdram* rdram;
    struct r4300_core* r4300;

    unsigned char dirty_page[FB_DIRTY_PAGES_COUNT];
    unsigned char native_pages[FB_DIRTY_PAGES_COUNT];
    unsigned char native_regions[FB_DIRTY_PAGES_COUNT / 16];
    unsigned int native_notifications;
    unsigned char native_refresh_regions[FB_DIRTY_PAGES_COUNT / 16];
    unsigned int native_refresh_pending;
    core_framebuffer_info infos[FB_INFOS_COUNT];
    unsigned int once;
    uint32_t timing_address[8], timing_bytes[8];
};

void init_fb(struct fb* fb,
             struct memory* mem,
             struct rdram* rdram,
             struct r4300_core* r4300);

void poweron_fb(struct fb* fb);

void read_rdram_fb(void* opaque, uint32_t address, uint32_t* value);
void write_rdram_fb(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

void protect_framebuffers(struct fb* fb);
void unprotect_framebuffers(struct fb* fb);
/* Native renderers use direct memory plus explicit notifications. Preserve
 * their handler mappings across GPU deliveries and update only changed guards. */
static inline void framebuffer_prepare_gpu(struct fb* fb)
{
    if (!fb->native_notifications) unprotect_framebuffers(fb);
}

void pre_framebuffer_read(struct fb* fb, uint32_t address);
void pre_framebuffer_read_range(struct fb* fb, uint32_t address, uint32_t length);
void post_framebuffer_write(struct fb* fb, uint32_t address, uint32_t length);

#endif
