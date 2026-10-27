/* SoftRDP's scanline jobs use Parallax's existing synchronous generation pool. */
#include "upstream/core/sr_threads.h"
#include "upstream/core/sr_async.h"
#include "../angrylion/parallel_al.h"
#include "../performance_cores.h"
#include <stddef.h>

static void (*current_job)(void *, uint32_t, uint32_t);
static void *current_context;
static int initialized;

void sr_threads_init(uint32_t workers)
{
    if (!initialized) {
        if (!workers) workers = performance_cores_count();
        if (workers > SR_THREADS_MAX) workers = SR_THREADS_MAX;
        parallel_alinit(workers); initialized = 1;
    }
}
uint32_t sr_threads_count(void) { return parallel_num_workers(); }
uint32_t sr_threads_physical_cores(void) { return performance_cores_count(); }
void sr_threads_shutdown(void)
{
    if (initialized) { parallel_close(); initialized = 0; }
}
static void run_lane(uint32_t id)
{ current_job(current_context, id, parallel_num_workers()); }
void sr_threads_run(void (*fn)(void *, uint32_t, uint32_t), void *context)
{
    current_job = fn; current_context = context;
    parallel_run(run_lane);
}
/* VI follows completed RDP work. Reuse the pool rather than adding another
 * executor or changing the calling-thread lane and parking policy. */
void sr_vi_threads_init(uint32_t workers) { sr_threads_init(workers); }
uint32_t sr_vi_threads_count(void) { return sr_threads_count(); }
void sr_vi_threads_run(void (*fn)(void *, uint32_t, uint32_t), void *context)
{ sr_threads_run(fn, context); }
void sr_vi_threads_shutdown(void) { }

/* Upstream's optional asynchronous owner is deliberately absent here. Its
 * documented NULL-queue path submits synchronously to the retained pool. */
sr_async *sr_async_create(void) { return NULL; }
void sr_async_submit(sr_async *queue, void (*fn)(void *), void *payload)
{ (void)queue; fn(payload); }
void sr_async_wait(sr_async *queue) { (void)queue; }
void sr_async_destroy(sr_async *queue) { (void)queue; }
