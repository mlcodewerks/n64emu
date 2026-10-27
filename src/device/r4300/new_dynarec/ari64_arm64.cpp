/* Xbyak_aarch64 host emitter for the shared Ari64 frontend.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <xbyak_aarch64/xbyak_aarch64.h>
#include <map>
#include <memory>
extern "C" {
#include "ari64.h"
#include "core/log.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/idec.h"
#include <assert.h>
#include "device/r4300/timing.h"
#include "device/r4300/pure_interp.h"
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

#if !defined(__aarch64__) && !defined(ARI64_CROSS_ASSEMBLE)
#error Ari64 ARM64 requires an AArch64 compiler
#endif
#ifndef ARI64_ARM64_SEGMENT_BYTES
#define ARI64_ARM64_SEGMENT_BYTES (512 * 1024)
#endif
#define SEGMENTS 16
#define ARENA_BYTES (SEGMENTS * ARI64_ARM64_SEGMENT_BYTES)
struct ari64 {
    unsigned char* code;
    size_t used, page;
    uint64_t generation, stamps[SEGMENTS], blocks, native, bytes, calls;
};

struct ari64* ari64_create(void)
{
    struct ari64* s = (struct ari64*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    #ifdef _WIN32
    SYSTEM_INFO info; GetSystemInfo(&info);
    long page=info.dwPageSize;
#else
    long page=sysconf(_SC_PAGESIZE);
#endif
    if (page <= 0 || ARI64_ARM64_SEGMENT_BYTES % page) { free(s); return NULL; }
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
void ari64_destroy(struct ari64* s)
{
    if(!s) return;
#ifdef _WIN32
    VirtualFree(s->code,0,MEM_RELEASE);
#else
    munmap(s->code,ARENA_BYTES);
#endif
    free(s);
}
uint64_t ari64_generation(const struct ari64* s) { return s->generation; }
const uint64_t* ari64_entry_generation_ptr(const struct ari64* s, ari64_entry entry)
{
    uintptr_t offset = (uintptr_t)entry - (uintptr_t)(s ? s->code : NULL);
    return s && entry && offset < ARENA_BYTES ? &s->stamps[offset / ARI64_ARM64_SEGMENT_BYTES] : NULL;
}
int ari64_entry_valid(const struct ari64* s, ari64_entry entry, uint64_t generation)
{
    const uint64_t* stamp = ari64_entry_generation_ptr(s, entry);
    return stamp && generation && *stamp == generation;
}
void ari64_stats(const struct ari64* s, uint64_t* blocks, uint64_t* native)
{
    *blocks = s ? s->blocks : 0; *native = s ? s->native : 0;
}
void ari64_code_stats(const struct ari64* s, uint64_t stats[3])
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
