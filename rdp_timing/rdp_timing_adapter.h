/* Standalone host adapter for rdp_timing.h. Repository LICENSE applies.
 * Header-only C99 / C++11 or later. No allocation, renderer or emulator APIs.
 * Timestamps: signed 64-bit monotonic CPU COUNT ticks (46.875 MHz).
 * Busy intervals: quarter COUNT ticks; masks 1=buffer, 2=pipe, 4=TMEM.
 * Callbacks may enqueue host events; they must not reenter this adapter.
 * License: WTFPL
 */
#ifndef RDP_TIMING_ADAPTER_H
#define RDP_TIMING_ADAPTER_H
#include <stddef.h>
#include "rdp_timing.h"

struct rdp_timing_callbacks {
    void *context;
    void (*busy)(void *context, int64_t begin_quarters, int64_t end_quarters, unsigned mask);
    void (*full_sync)(void *context, int64_t deadline_count);
    /* Optional restore-time proof check. Synchronize backing RAM and verify
     * every packed Z halfword (including its DZ bits), not a few samples.
     * Absent/false validation drops the proof and retains conservative work. */
    int (*validate_uniform)(void *context, const struct rdp_uniform_depth *region);
};

struct rdp_timing_adapter {
    struct rdp_timing timing;
    uint32_t vi[14];
    struct rdp_timing_callbacks callbacks;
};

static inline void rdp_timing_adapter_callbacks(struct rdp_timing_adapter *a,
    const struct rdp_timing_callbacks *callbacks)
{
    if (callbacks) a->callbacks = *callbacks;
    else memset(&a->callbacks, 0, sizeof(a->callbacks));
    a->timing.observe_busy = a->callbacks.busy;
    a->timing.busy_context = a->callbacks.context;
}

static inline void rdp_timing_adapter_init(struct rdp_timing_adapter *a,
    int64_t now_count, uint32_t rdram_bytes, const struct rdp_timing_callbacks *callbacks)
{
    memset(a, 0, sizeof(*a));
    a->timing.deadline = now_count;
    a->timing.rdram_bytes = rdram_bytes;
    rdp_timing_adapter_callbacks(a, callbacks);
}

/* Copy numeric VI MMIO words in STATUS..Y_SCALE order. Null disables VI.
 * Rebind before feeding after VI writes, reset, restore or moving the adapter. */
static inline void rdp_timing_adapter_set_vi(struct rdp_timing_adapter *a,
    const uint32_t *registers14, int64_t field_start_count, uint32_t field_ticks_count)
{
    if (registers14) memmove(a->vi, registers14, sizeof(a->vi));
    else memset(a->vi, 0, sizeof(a->vi));
    a->timing.vi_regs = registers14 ? a->vi : NULL;
    a->timing.vi_field_start = field_start_count;
    a->timing.vi_field_ticks = registers14 ? field_ticks_count : 0;
}

static inline int rdp_timing_adapter_valid(const struct rdp_timing_adapter *a)
{
    const struct rdp_timing *t = &a->timing;
    return t->deadline >= 0 && t->clock_overhang <= 3 &&
        t->cycle_type <= 3 && t->fb_size <= 3 && t->tex_size <= 3 &&
        t->fb_width <= 1024 && t->tex_width <= 1024 && t->scissor_field <= 3 &&
        t->left <= 4095 && t->top <= 4095 && t->right <= 4095 && t->bottom <= 4095 &&
        (t->fb_address | t->tex_address | t->z_address) < 0x1000000u &&
        t->atomic_primitive <= 1 && t->reject <= 1 &&
        (!t->pos || (t->length == rdp_timing_packet_words(t->words[0]) && t->pos < t->length));
}

/* After deserializing explicit persistent scalar/array fields into timing:
 * validate, reattach callbacks, drop/validate proofs and require fresh VI state.
 * Do not serialize callback pointers, vi_regs, or the enclosing adapter bytes.
 * Restore the host COUNT epoch and scheduled DP/counter events separately. */
static inline int rdp_timing_adapter_after_load(struct rdp_timing_adapter *a,
    uint32_t rdram_bytes, const struct rdp_timing_callbacks *callbacks)
{
    unsigned i;
    if (!a || !rdp_timing_adapter_valid(a)) return 0;
    a->timing.rdram_bytes = rdram_bytes;
    rdp_timing_adapter_callbacks(a, callbacks);
    rdp_timing_adapter_set_vi(a, NULL, 0, 0);
    a->timing.vi_elapsed = 0;
    a->timing.span_fraction = 0;
    a->timing.validate_uniform_ram = 0;
    for (i = 0; i < 8; ++i) {
        struct rdp_uniform_depth *u = &a->timing.uniform[i];
        if (!u->valid) continue;
        if ((u->address & 7) || !u->width || !u->height || u->width > 1024 ||
            u->height > 1024 || u->z > 0x3ffff ||
            (uint64_t)u->address + (uint64_t)u->width * u->height * 2 > rdram_bytes ||
            !a->callbacks.validate_uniform ||
            !a->callbacks.validate_uniform(a->callbacks.context, u)) u->valid = 0;
    }
    return 1;
}

/* Feed numeric N64 words, not bytes in a host-dependent RAM layout. Count is
 * 32-bit words, not 64-bit commands or bytes. Retains partial packet state.
 * Fires each completed FullSync, even several in one call or split windows.
 * Returns 0 for invalid arguments/state before consuming any words. */
static inline int rdp_timing_adapter_feed_words(struct rdp_timing_adapter *a,
    const uint32_t *words, size_t count, int64_t submitted_count)
{
    if (!a || (count && !words) || submitted_count < 0 || !rdp_timing_adapter_valid(a)) return 0;
    while (count) {
        uint32_t n = a->timing.pos ? a->timing.length - a->timing.pos :
            rdp_timing_packet_words(*words);
        struct rdp_timing_sync sync;
        memset(&sync, 0, sizeof(sync));
        if ((size_t)n > count) n = (uint32_t)count;
        rdp_timing_words_sync(&a->timing, words, n, submitted_count, &sync);
        words += n;
        count -= n;
        if (sync.seen && a->callbacks.full_sync)
            a->callbacks.full_sync(a->callbacks.context, sync.deadline);
    }
    return 1;
}

/* Byte-order-independent big-endian input. A window must contain whole 32-bit
 * words; complete commands need not fit in a window. No unaligned loads. */
static inline int rdp_timing_adapter_feed_be(struct rdp_timing_adapter *a,
    const void *bytes, size_t byte_count, int64_t submitted_count)
{
    const unsigned char *p = (const unsigned char *)bytes;
    uint32_t words[64];
    if (!a || (byte_count && !bytes) || (byte_count & 3) || submitted_count < 0 ||
        !rdp_timing_adapter_valid(a)) return 0;
    while (byte_count) {
        size_t i, n = byte_count / 4;
        if (n > 64) n = 64;
        for (i = 0; i < n; ++i) {
            words[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                       (uint32_t)p[2] << 8 | (uint32_t)p[3];
            p += 4;
        }
        if (!rdp_timing_adapter_feed_words(a, words, n, submitted_count)) return 0;
        byte_count -= n * 4;
    }
    return 1;
}

/* Call for CPU/DMA/other writes before a later command can reuse a Z proof. */
static inline void rdp_timing_adapter_notify_write(struct rdp_timing_adapter *a,
    uint32_t address_bytes, uint64_t byte_count)
{
    rdp_timing_invalidate_range(&a->timing, address_bytes, byte_count);
}
#endif
