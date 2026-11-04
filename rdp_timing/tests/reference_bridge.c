/* Optional original-header differential, isolated in its own translation unit. */
#include RDP_TIMING_REFERENCE_HEADER
#include "observe.h"
static struct rdp_timing reference;
static struct port_test_trace trace;
static uint32_t vi[14];
void port_reference_init(void)
{
    memset(&reference, 0, sizeof(reference));
    memset(&trace, 0, sizeof(trace));
    reference.rdram_bytes = 0x800000;
    reference.observe_busy = port_busy;
    reference.busy_context = &trace;
}
void port_reference_vi(const uint32_t *registers, int64_t start, uint32_t ticks)
{
    if (registers) memcpy(vi, registers, sizeof(vi));
    reference.vi_regs = registers ? vi : NULL;
    reference.vi_field_start = start;
    reference.vi_field_ticks = registers ? ticks : 0;
}
void port_reference_feed(const uint32_t *words, unsigned count, int64_t submitted)
{
    struct rdp_timing_sync sync;
    memset(&sync, 0, sizeof(sync));
    rdp_timing_words_sync(&reference, words, count, submitted, &sync);
    trace.fence_count = (unsigned)sync.seen;
    trace.first_fence = sync.deadline;
}
void port_reference_write(uint32_t address, uint64_t bytes)
{ rdp_timing_invalidate_range(&reference, address, bytes); }
uint64_t port_reference_hash(void) { return port_state_hash(&reference); }
struct port_test_trace port_reference_trace(void) { return trace; }
