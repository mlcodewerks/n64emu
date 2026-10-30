/* Xbyak_aarch64 host emitter for the shared ParallaxJIT frontend.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <xbyak_aarch64/xbyak_aarch64.h>
#include <map>
#include <memory>
extern "C" {
#include "parallaxjit.h"
#include "core/log.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/idec.h"
#include <assert.h>
#include "device/r4300/timing.h"
#include "device/r4300/cpu_ops.h"
#include "device/memory.h"
#include "device/rdram.h"
}
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if !defined(__aarch64__) && !defined(PARALLAXJIT_CROSS_ASSEMBLE)
#error ParallaxJIT ARM64 requires an AArch64 compiler
#endif
#ifndef PARALLAXJIT_ARM64_SEGMENT_BYTES
#define PARALLAXJIT_ARM64_SEGMENT_BYTES (512 * 1024)
#endif
#define SEGMENTS 16
#define ARENA_BYTES (SEGMENTS * PARALLAXJIT_ARM64_SEGMENT_BYTES)
struct parallaxjit {
    unsigned char* code;
    size_t used, page;
    uint64_t generation, stamps[SEGMENTS], blocks, native, bytes, calls;
};
/* The fault-assisted memory emitter is currently x86-64 only. */
void* parallaxjit_fault_guard(const struct parallaxjit*) { return NULL; }
void* parallaxjit_fault_enter(struct parallaxjit*) { return NULL; }
void parallaxjit_fault_leave(void*) {}
uint64_t parallaxjit_fault_count(const struct parallaxjit*) { return 0; }
void parallaxjit_fault_resolve(struct parallaxjit*, struct r4300_core*, uint32_t) {}

struct parallaxjit* parallaxjit_create(void)
{
    struct parallaxjit* s = (struct parallaxjit*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    #ifdef _WIN32
    SYSTEM_INFO info; GetSystemInfo(&info);
    long page=info.dwPageSize;
#else
    long page=sysconf(_SC_PAGESIZE);
#endif
    if (page <= 0 || PARALLAXJIT_ARM64_SEGMENT_BYTES % page) { free(s); return NULL; }
    s->page = (size_t)page;
    #ifdef _WIN32
    s->code=(unsigned char*)VirtualAlloc(NULL,ARENA_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    s->code=(unsigned char*)mmap(NULL,ARENA_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(s->code==MAP_FAILED) s->code=NULL;
#endif
    if(!s->code) { free(s); return NULL; }
    s->generation = s->stamps[0] = 1;
    return s;
}
void parallaxjit_destroy(struct parallaxjit* s)
{
    if(!s) return;
#ifdef _WIN32
    VirtualFree(s->code,0,MEM_RELEASE);
#else
    munmap(s->code,ARENA_BYTES);
#endif
    free(s);
}
uint64_t parallaxjit_generation(const struct parallaxjit* s) { return s->generation; }
const uint64_t* parallaxjit_entry_generation_ptr(const struct parallaxjit* s, parallaxjit_entry entry)
{
    uintptr_t offset = (uintptr_t)entry - (uintptr_t)(s ? s->code : NULL);
    return s && entry && offset < ARENA_BYTES ? &s->stamps[offset / PARALLAXJIT_ARM64_SEGMENT_BYTES] : NULL;
}
int parallaxjit_entry_valid(const struct parallaxjit* s, parallaxjit_entry entry, uint64_t generation)
{
    const uint64_t* stamp = parallaxjit_entry_generation_ptr(s, entry);
    return stamp && generation && *stamp == generation;
}
void parallaxjit_stats(const struct parallaxjit* s, uint64_t* blocks, uint64_t* native)
{
    *blocks = s ? s->blocks : 0; *native = s ? s->native : 0;
}
void parallaxjit_code_stats(const struct parallaxjit* s, uint64_t stats[3])
{
    stats[0] = s ? s->bytes : 0; stats[1] = 0; stats[2] = s ? s->calls : 0;
}


#include "arm64/xbyak_emit.h"
static int code_protect(void* p,size_t size,int execute) {
#ifdef _WIN32
    DWORD previous; return VirtualProtect(p,size,execute?PAGE_EXECUTE_READ:PAGE_READWRITE,&previous)!=0;
#else
    return !mprotect(p,size,execute?PROT_READ|PROT_EXEC:PROT_READ|PROT_WRITE);
#endif
}
#include "code_write_scope.h"
#include "arm64/regalloc_arm64.inc"
#include "arm64/memory_arm64.inc"
#include "arm64/muldiv_arm64.inc"
#include "arm64/integer_arm64.inc"
#include "arm64/alu_arm64.inc"
#define OPT_TEMP0 T0
#define OPT_TEMP1 T1
#define OPT_TEMP2 T2
#include "optimizer_alu.inc"
#undef OPT_TEMP0
#undef OPT_TEMP1
#undef OPT_TEMP2
#include "arm64/branch_arm64.inc"
#include "arm64/timing_arm64.inc"
#include "arm64/fpu_arm64.inc"
#include "arm64/link_arm64.inc"
#include "arm64/frontend_arm64.inc"
