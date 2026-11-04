/* Compile: cc -std=c99 -Wall -Wextra -Werror example.c -o example
 * Copy this file and the two headers into any directory; no core is linked. */
#include <stdio.h>
#include "rdp_timing_adapter.h"

static void full_sync(void *context, int64_t deadline_count)
{
    unsigned *fences = (unsigned *)context;
    ++*fences;
    printf("FullSync %u: enqueue DP interrupt at COUNT %lld\n", *fences,
        (long long)deadline_count);
}

int main(void)
{
    struct rdp_timing_adapter dp;
    struct rdp_timing_callbacks callbacks;
    unsigned fences = 0;
    /* RGBA16 320x240, scissor in quarter pixels, one-cycle rectangle. */
    const uint32_t commands[] = {
        0x3f10013f, 0x00100000, /* SetColorImage: size=2, width=320 */
        0x2d000000, 0x005003c0, /* SetScissor: [0,320) x [0,240) */
        0x2f000000, 0x00000000, /* SetOtherModes: one-cycle, no reads */
        0x365003c0, 0x00000000, /* FillRectangle under one-cycle mode */
        0x29000000, 0x00000000  /* FullSync */
    };
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &fences;
    callbacks.full_sync = full_sync;
    rdp_timing_adapter_init(&dp, 0, 8u * 1024 * 1024, &callbacks);
    /* VI stays disabled here; a host with scanout calls set_vi before feed. */
    if (!rdp_timing_adapter_feed_words(&dp, commands,
            sizeof(commands) / sizeof(commands[0]), 0)) return 1;
    printf("Queue deadline: %lld COUNT ticks\n", (long long)dp.timing.deadline);
    return fences == 1 && dp.timing.deadline == 60304 ? 0 : 1;
}
