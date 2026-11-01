
#ifndef N64EMU_N64EMUJIT_H
#define N64EMU_N64EMUJIT_H
#ifndef N64EMUJIT_BACKEND_NAME
#if defined(__i386__) || defined(_M_IX86)
#define N64EMUJIT_BACKEND_NAME "n64emujit-x86"
#elif defined(__arm__)
#define N64EMUJIT_BACKEND_NAME "n64emujit-arm32"
#elif defined(__aarch64__)
#define N64EMUJIT_BACKEND_NAME "n64emujit-arm64"
#else
#define N64EMUJIT_BACKEND_NAME "n64emujit-x64"
#endif
#endif
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct r4300_core;
struct r4300_fastmem;
struct n64emujit;
typedef void (*n64emujit_entry)(struct r4300_core*);
/* 0: begin native instruction, 1: commit native instruction,
 * 2: shared precise instruction; 5 | (count << 8): precise region;
 * 11: begin COP1 transfer with its unusable check. Zero exits the block. */
typedef int (*n64emujit_step)(struct r4300_core*, void*, unsigned, unsigned);
typedef n64emujit_entry (*n64emujit_link)(struct r4300_core*);
typedef int (*n64emujit_fpu_prepare)(struct r4300_core*, uint32_t, uint64_t inputs[3], uint64_t* commits);
typedef int (*n64emujit_fpu_finish)(struct r4300_core*, uint32_t, uint64_t, unsigned);
/* Filled only by the validated C linker, consumed by emitted tail jumps.
 * Entry identity catches decoded-slot replacement; page/epoch/segment stamps
 * catch code writes, remapping and arena recycling. */
struct n64emujit_link_cache {
    uint32_t pc, epoch, page_generation;
    n64emujit_entry expected_entry, body;
    n64emujit_entry* current_entry;
    const uint32_t* current_page_generation;
    const uint32_t* current_mapping;
    uint32_t mapping;
    const uint64_t* current_segment_generation;
    uint64_t segment_generation;
    void* target_block;
    uint64_t* profile_entries;
};
struct n64emujit_fetch_cache {
    uint64_t generation;
    uint32_t tag, cached;
};
struct n64emujit_config {
    uint32_t virtual_pc, physical_page;
    unsigned cache_timing;
    n64emujit_link link;
    uint32_t delay_op;
    unsigned has_delay, idle_end;
    uint64_t* commits;
    uint64_t* fast_commits;
    n64emujit_fpu_prepare fpu_prepare;
    n64emujit_fpu_finish fpu_finish;
    n64emujit_entry* chain_entry;
    struct n64emujit_link_cache* links;
    void* previous_block;
    const uint32_t* epoch;
    uint64_t* entries;
    uint64_t* linked_entries;
    uint64_t* direct_links;
    unsigned profile;
    unsigned compat_issue_clock;
    struct n64emujit_fetch_cache* fetch_cache;
    const uint64_t* fetch_generation;
    struct r4300_fastmem* fastmem;
};
struct n64emujit* n64emujit_create(void);
void n64emujit_destroy(struct n64emujit*);
void* n64emujit_fault_guard(const struct n64emujit*);
void* n64emujit_fault_enter(struct n64emujit*);
void n64emujit_fault_leave(void* previous);
uint64_t n64emujit_fault_count(const struct n64emujit*);
void n64emujit_fault_resolve(struct n64emujit*, struct r4300_core*, uint32_t op);
uint64_t n64emujit_generation(const struct n64emujit*);
int n64emujit_entry_valid(const struct n64emujit*, n64emujit_entry, uint64_t generation);
const uint64_t* n64emujit_entry_generation_ptr(const struct n64emujit*, n64emujit_entry);
int n64emujit_has_native(const uint32_t* ops, unsigned count, uint32_t delay_op, unsigned has_delay, unsigned idle);
n64emujit_entry n64emujit_compile(struct n64emujit*, void* block,
    const uint32_t* ops, unsigned count, n64emujit_step step);
/* Emit host code with shared precise instruction boundaries. Native coverage
 * is host-specific (see README.md). Optional linking returns a published,
 * validated successor that must not grow the host stack. */
n64emujit_entry n64emujit_compile_optimized(struct n64emujit*, void* block,
    const uint32_t* ops, unsigned count, n64emujit_step step, const struct n64emujit_config*);
void n64emujit_stats(const struct n64emujit*, uint64_t* blocks, uint64_t* native);
void n64emujit_code_stats(const struct n64emujit*, uint64_t stats[3]);
#ifdef __cplusplus
}
#endif
#endif
