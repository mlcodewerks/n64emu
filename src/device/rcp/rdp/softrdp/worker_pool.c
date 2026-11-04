/* SoftRDP pool adapted from libretro/parallel-n64 parallel_al.c,
 * commit 3ce14f1be1826b61d9a908f7dc92b8b1532cefd6.
 * Existing n64emu worker selection and affinity policy are retained.
 *
 * One thread per worker, worker 0 being the calling thread. Every
 * parallel_run() is a generation: the caller publishes the task, bumps
 * the generation counter, runs its own lane and waits for the other
 * workers to report in. Workers park on one eventcount between
 * generations and the caller parks on another while the workers finish,
 * so a pool that is not rendering costs nothing: every thread the
 * renderer is not using is a thread the emulator, the frontend or an SMT
 * sibling gets back.
 *
 * No lock anywhere. The generation and the completion counter are the
 * predicates; the eventcounts only park and wake. A notify with nobody
 * parked is one atomic RMW and one load, so the hot path (workers still
 * awake from the last generation) never takes a lock or a syscall.
 *
 * The two counters the threads hammer live on their own cache lines:
 * workers poll the generation while the completion counter is being
 * decremented, and neither should invalidate the other.
 */

#include "worker_pool.h"
#include "../performance_cores.h"

#include <stdlib.h>
#include <string.h>

#include "worker_atomic.h"
#include <rthreads/rthreads.h>
#include "worker_eventcount.h"

#define PARALLEL_LINE_PAD 64

struct parallel_pool
{
    void (*task)(uint32_t);
    /* workers park here between generations */
    retro_eventcount_t work_ec;
    /* the caller parks here until the workers finish */
    retro_eventcount_t done_ec;
    int ec_live;
    sthread_t *threads[PARALLEL_MAX_WORKERS];
    uint32_t worker_ids[PARALLEL_MAX_WORKERS];
    /* number of workers, worker 0 included; 0 while no pool is live */
    uint32_t num_workers;
    /* cleared before the final generation bump; the release store of
     * the generation publishes it to the workers */
    int accept_work;
    char pad0[PARALLEL_LINE_PAD];
    /* bumped once per parallel_run() and once at shutdown */
    retro_atomic_int_t generation;
    char pad1[PARALLEL_LINE_PAD];
    /* workers still busy in the current generation */
    retro_atomic_int_t remaining;
    char pad2[PARALLEL_LINE_PAD];
};

static struct parallel_pool pool;

/* Block until the generation moves past the one this worker last ran.
 * Returns the new generation. */
static int parallel_await_generation(struct parallel_pool *p, int seen)
{
    int gen;
    int key;

    for (;;)
    {
        gen = retro_atomic_load_acquire_int(&p->generation);
        if (gen != seen)
            return gen;
        key = retro_eventcount_prepare_wait(&p->work_ec);
        gen = retro_atomic_load_acquire_int(&p->generation);
        if (gen != seen)
        {
            retro_eventcount_cancel_wait(&p->work_ec);
            return gen;
        }
        retro_eventcount_commit_wait(&p->work_ec, key);
    }
}

static void parallel_worker(void *data)
{
    struct parallel_pool *p = &pool;
    uint32_t worker_id = *(const uint32_t*)data;
    int seen = 0;
    struct performance_affinity affinity;
    performance_core_enter(worker_id, &affinity);

    for (;;)
    {
        seen = parallel_await_generation(p, seen);
        if (!p->accept_work)
            break;

        p->task(worker_id);

        /* the last worker out wakes the caller */
        if (retro_atomic_fetch_sub_int(&p->remaining, 1) == 1)
            retro_eventcount_notify(&p->done_ec);
    }
    performance_core_leave(&affinity);
}

static void parallel_wait_completion(struct parallel_pool *p)
{
    int key;

    for (;;)
    {
        if (retro_atomic_load_acquire_int(&p->remaining) == 0)
            return;
        key = retro_eventcount_prepare_wait(&p->done_ec);
        if (retro_atomic_load_acquire_int(&p->remaining) == 0)
        {
            retro_eventcount_cancel_wait(&p->done_ec);
            return;
        }
        retro_eventcount_commit_wait(&p->done_ec, key);
    }
}

/* Publish the next generation. Only the caller writes the generation, so
 * a plain load and a release store are enough; the notify costs nothing
 * extra while every worker is still awake. */
static void parallel_bump_generation(struct parallel_pool *p)
{
    int gen = retro_atomic_load_acquire_int(&p->generation);
    retro_atomic_store_release_int(&p->generation, (int)((unsigned)gen + 1u));
    retro_eventcount_notify(&p->work_ec);
}

void parallel_alinit(uint32_t num)
{
    struct parallel_pool *p = &pool;
    uint32_t i;

    if (p->num_workers)
        parallel_close();

    performance_cores_init();
    if (num == 0)
        num = performance_cores_count();
    if (num == 0)
        num = 1;
    if (num > PARALLEL_MAX_WORKERS)
        num = PARALLEL_MAX_WORKERS;
#if !defined(RETRO_ATOMIC_LOCK_FREE)
    /* the counters need real atomics; without them the renderer stays
     * on the calling thread */
    num = 1;
#endif

    memset(p, 0, sizeof(*p));
    retro_atomic_int_init(&p->generation, 0);
    retro_atomic_int_init(&p->remaining, 0);
    p->num_workers = 1;
    if (num == 1)
        return;

    p->ec_live = 1;
    if (  !retro_eventcount_init(&p->work_ec)
       || !retro_eventcount_init(&p->done_ec))
    {
        parallel_close();
        p->num_workers = 1;
        return;
    }
    p->accept_work = 1;

    /* worker_ids is what the threads read their id from, so it has to
     * be final before the first thread starts */
    for (i = 0; i < num; i++)
        p->worker_ids[i] = i;

    /* a thread that fails to start caps the pool at the workers that
     * did: ids are dense, so the lanes stay consistent */
    for (i = 1; i < num; i++)
    {
        p->threads[i] = sthread_create(parallel_worker, &p->worker_ids[i]);
        if (!p->threads[i])
            break;
        p->num_workers = i + 1;
    }
}

void parallel_run(void task(uint32_t))
{
    struct parallel_pool *p = &pool;
    struct performance_affinity affinity;
    performance_core_enter(0, &affinity);

    /* single-worker pools and no pool at all have nobody to hand the
     * work to */
    if (p->num_workers <= 1 || !p->accept_work)
    {
        task(0);
        performance_core_leave(&affinity);
        return;
    }

    p->task = task;
    retro_atomic_store_release_int(&p->remaining, (int)(p->num_workers - 1));
    parallel_bump_generation(p);

    task(0);
    parallel_wait_completion(p);
    performance_core_leave(&affinity);
}

uint32_t parallel_num_workers(void)
{
    return pool.num_workers ? pool.num_workers : 1;
}

void parallel_close(void)
{
    struct parallel_pool *p = &pool;
    uint32_t i;

    if (p->num_workers > 1)
    {
        p->accept_work = 0;
        parallel_bump_generation(p);

        for (i = 1; i < p->num_workers; i++)
            sthread_join(p->threads[i]);
    }

    /* free is safe on a zeroed object and on one init failed for */
    if (p->ec_live)
    {
        retro_eventcount_free(&p->work_ec);
        retro_eventcount_free(&p->done_ec);
    }

    memset(p, 0, sizeof(*p));
}
