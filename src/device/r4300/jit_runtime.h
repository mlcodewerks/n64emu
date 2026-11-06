

#ifndef N64EMU_DEVICE_R4300_JIT_RUNTIME_H
#define N64EMU_DEVICE_R4300_JIT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

struct r4300_core;
struct r4300_jit_state;

int r4300_jit_init(struct r4300_core* r4300);
void r4300_jit_destroy(struct r4300_core* r4300);
void r4300_jit_invalidate(struct r4300_core* r4300, uint32_t address, size_t size);
void r4300_jit_tlb_changed(struct r4300_core* r4300);
int r4300_jit_mapped_cached(struct r4300_core*, uint32_t address);
void r4300_jit_cache_stats(struct r4300_core*, uint64_t stats[2]);
void r4300_jit_host_fault_stats(struct r4300_core*, uint64_t stats[2]);
#ifdef HAVE_N64EMUJIT
void r4300_jit_execute_one(struct r4300_core* r4300);
void r4300_jit_run(struct r4300_core* r4300);
void r4300_jit_stats(struct r4300_core* r4300, uint64_t stats[4]);
void r4300_jit_optimization_stats(struct r4300_core* r4300, uint64_t stats[2]);
uint64_t r4300_jit_direct_links(struct r4300_core* r4300);
void r4300_jit_code_stats(struct r4300_core*, uint64_t stats[3]);
#endif

#endif /* N64EMU_DEVICE_R4300_JIT_RUNTIME_H */
