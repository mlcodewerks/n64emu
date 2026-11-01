
#ifndef PARALLAX_PARALLAXJIT_H
#define PARALLAX_PARALLAXJIT_H
#ifndef PARALLAXJIT_BACKEND_NAME
#if defined(__i386__) || defined(_M_IX86)
#define PARALLAXJIT_BACKEND_NAME "parallaxjit-x86"
#elif defined(__arm__)
#define PARALLAXJIT_BACKEND_NAME "parallaxjit-arm32"
#elif defined(__aarch64__)
#define PARALLAXJIT_BACKEND_NAME "parallaxjit-arm64"
#else
#define PARALLAXJIT_BACKEND_NAME "parallaxjit-x64"
#endif
#endif
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct r4300_core;
struct r4300_fastmem;
struct parallaxjit;
typedef void (*parallaxjit_entry)(struct r4300_core*);
/* 0: begin native instruction, 1: commit native instruction,
 * 2: shared precise instruction; 5 | (count << 8): precise region;
 * 11: begin COP1 transfer with its unusable check. Zero exits the block. */
typedef int (*parallaxjit_step)(struct r4300_core*, void*, unsigned, unsigned);
typedef parallaxjit_entry (*parallaxjit_link)(struct r4300_core*);
typedef int (*parallaxjit_fpu_prepare)(struct r4300_core*, uint32_t, uint64_t inputs[3], uint64_t* commits);
typedef int (*parallaxjit_fpu_finish)(struct r4300_core*, uint32_t, uint64_t, unsigned);
/* Filled only by the validated C linker, consumed by emitted tail jumps.
 * Entry identity catches decoded-slot replacement; page/epoch/segment stamps
 * catch code writes, remapping and arena recycling. */
struct parallaxjit_link_cache {
    uint32_t pc, epoch, page_generation;
    parallaxjit_entry expected_entry, body;
    parallaxjit_entry* current_entry;
    const uint32_t* current_page_generation;
    const uint32_t* current_mapping;
    uint32_t mapping;
    const uint64_t* current_segment_generation;
    uint64_t segment_generation;
    void* target_block;
    uint64_t* profile_entries;
};
struct parallaxjit_fetch_cache {
    uint64_t generation;
    uint32_t tag, cached;
};
struct parallaxjit_config {
    uint32_t virtual_pc, physical_page;
    unsigned cache_timing;
    parallaxjit_link link;
    uint32_t delay_op;
    unsigned has_delay, idle_end;
    uint64_t* commits;
    uint64_t* fast_commits;
    parallaxjit_fpu_prepare fpu_prepare;
    parallaxjit_fpu_finish fpu_finish;
    parallaxjit_entry* chain_entry;
    struct parallaxjit_link_cache* links;
    void* previous_block;
    const uint32_t* epoch;
    uint64_t* entries;
    uint64_t* linked_entries;
    uint64_t* direct_links;
    unsigned profile;
    unsigned compat_issue_clock;
    struct parallaxjit_fetch_cache* fetch_cache;
    const uint64_t* fetch_generation;
    struct r4300_fastmem* fastmem;
};
struct parallaxjit* parallaxjit_create(void);
void parallaxjit_destroy(struct parallaxjit*);
void* parallaxjit_fault_guard(const struct parallaxjit*);
void* parallaxjit_fault_enter(struct parallaxjit*);
void parallaxjit_fault_leave(void* previous);
uint64_t parallaxjit_fault_count(const struct parallaxjit*);
void parallaxjit_fault_resolve(struct parallaxjit*, struct r4300_core*, uint32_t op);
uint64_t parallaxjit_generation(const struct parallaxjit*);
int parallaxjit_entry_valid(const struct parallaxjit*, parallaxjit_entry, uint64_t generation);
const uint64_t* parallaxjit_entry_generation_ptr(const struct parallaxjit*, parallaxjit_entry);
int parallaxjit_has_native(const uint32_t* ops, unsigned count, uint32_t delay_op, unsigned has_delay, unsigned idle);
parallaxjit_entry parallaxjit_compile(struct parallaxjit*, void* block,
    const uint32_t* ops, unsigned count, parallaxjit_step step);
/* Emit host code with shared precise instruction boundaries. Native coverage
 * is host-specific (see README.md). Optional linking returns a published,
 * validated successor that must not grow the host stack. */
parallaxjit_entry parallaxjit_compile_optimized(struct parallaxjit*, void* block,
    const uint32_t* ops, unsigned count, parallaxjit_step step, const struct parallaxjit_config*);
void parallaxjit_stats(const struct parallaxjit*, uint64_t* blocks, uint64_t* native);
void parallaxjit_code_stats(const struct parallaxjit*, uint64_t stats[3]);
#ifdef __cplusplus
}
#endif
#endif
