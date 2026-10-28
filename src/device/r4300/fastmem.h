/* Experimental fault-assisted CPU RAM lookup. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARALLAX_R4300_FASTMEM_H
#define PARALLAX_R4300_FASTMEM_H
#include <stdint.h>
#include <stddef.h>
struct r4300_core;
struct tlb;
struct memory;
struct r4300_fastmem {
    struct r4300_core* owner;
    uintptr_t guard, ram;
    size_t ram_size;
    const uintptr_t* read[0x100000];
    /* Indirection makes SMC protection apply to every virtual alias at once. */
    const uintptr_t* write[0x100000];
    uintptr_t physical_read[2048];
    uintptr_t physical_write[2048];
};
struct r4300_fastmem* r4300_fastmem_create(struct r4300_core*, void* guard);
void r4300_fastmem_destroy(struct r4300_fastmem*);
void r4300_fastmem_refresh(struct r4300_fastmem*);
void r4300_fastmem_tlb_page(struct tlb*, uint32_t page);
void r4300_fastmem_region_changed(struct memory*, uint16_t region);
#endif
