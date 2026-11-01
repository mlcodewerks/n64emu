

#ifndef N64EMU_DEVICE_MEMORY_MEMORY_H
#define N64EMU_DEVICE_MEMORY_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "osal/preproc.h"

enum { RDRAM_MAX_SIZE = 0x800000 };
enum { CART_ROM_MAX_SIZE = 0x4000000 };
enum { DD_ROM_MAX_SIZE = 0x400000 };

typedef void (*read32fn)(void*,uint32_t,uint32_t*);
typedef void (*write32fn)(void*,uint32_t,uint32_t,uint32_t);

enum core_memory_type { CORE_MEMORY_NOMEM, CORE_MEMORY_NOTHING, CORE_MEMORY_RDRAM, CORE_MEMORY_RDRAMREG, CORE_MEMORY_RSPMEM, CORE_MEMORY_RSPREG, CORE_MEMORY_RSP, CORE_MEMORY_DP, CORE_MEMORY_DPS, CORE_MEMORY_VI, CORE_MEMORY_AI, CORE_MEMORY_PI, CORE_MEMORY_RI, CORE_MEMORY_SI, CORE_MEMORY_FLASHRAMSTAT, CORE_MEMORY_ROM, CORE_MEMORY_PIF, CORE_MEMORY_MI, CORE_MEMORY_BREAKPOINT };

struct mem_handler
{
    void* opaque;
    read32fn read32;
    write32fn write32;
};

struct mem_mapping
{
    uint32_t begin;
    uint32_t end;       /* inclusive */
    int type;
    struct mem_handler handler;
};

enum memory_notification_query {
    MEMORY_NOTIFICATION_FAST_READ_PAGE,
    MEMORY_NOTIFICATION_FAST_WRITE_PAGE,
    MEMORY_NOTIFICATION_READ_RELEASES_PAGE
};

struct memory
{
    struct r4300_fastmem* cpu_fastmem;
    /* A notification handler may expose clean 4 KiB pages to guarded RAM.
     * Queries take a physical byte address and memory_notification_query.
     * Read-release queries test the exact address; fast-page queries promise
     * the whole page is ordinary RAM. Never called in the host fault handler. */
    struct mem_handler cpu_notification;
    int (*cpu_notification_query)(void*, uint32_t, int);
    struct mem_handler handlers[0x10000];
    void* base;

};

static osal_force_inline void masked_write(uint32_t* dst, uint32_t value, uint32_t mask)
{
    *dst = (*dst & ~mask) | (value & mask);
}

void init_memory(struct memory* mem,
                 struct mem_mapping* mappings, size_t mappings_count,
                 void* base,
                 struct mem_handler* dbg_handler);

static osal_force_inline const struct mem_handler* mem_get_handler(const struct memory* mem, uint32_t address)
{
    return &mem->handlers[address >> 16];
}

static osal_force_inline void mem_read32(const struct mem_handler* handler, uint32_t address, uint32_t* value)
{
    handler->read32(handler->opaque, address, value);
}

static osal_force_inline void mem_write32(const struct mem_handler* handler, uint32_t address, uint32_t value, uint32_t mask)
{
    handler->write32(handler->opaque, address, value, mask);
}

void apply_mem_mapping(struct memory* mem, const struct mem_mapping* mapping);

void* init_mem_base(void);
void release_mem_base(void* mem_base);
uint32_t* mem_base_u32(void* mem_base, uint32_t address);

void read_with_bp_checks(void* opaque, uint32_t address, uint32_t* value);
void write_with_bp_checks(void* opaque, uint32_t address, uint32_t value, uint32_t mask);


#endif

