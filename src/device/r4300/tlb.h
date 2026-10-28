

#ifndef PARALLAX_DEVICE_R4300_TLB_H
#define PARALLAX_DEVICE_R4300_TLB_H

#include <stddef.h>
#include <stdint.h>

struct r4300_core;
struct r4300_fastmem;

struct tlb_entry
{
   short mask;
   unsigned int vpn2;
   char g;
   unsigned char asid;
   unsigned int pfn_even;
   char c_even;
   char d_even;
   char v_even;
   unsigned int pfn_odd;
   char c_odd;
   char d_odd;
   char v_odd;
   char r;
   //int check_parity_mask;

   unsigned int start_even;
   unsigned int end_even;
   unsigned int phys_even;
   unsigned int start_odd;
   unsigned int end_odd;
   unsigned int phys_odd;
};

struct tlb
{
    struct r4300_fastmem* fastmem;
    struct tlb_entry entries[32];
    uint32_t LUT_r[0x100000];
    uint32_t LUT_w[0x100000];
};

void poweron_tlb(struct tlb* tlb);

void tlb_unmap(struct tlb* tlb, size_t entry);
void tlb_map(struct tlb* tlb, size_t entry);

uint32_t virtual_to_physical_address(struct r4300_core* r4300, uint32_t address, int w);

#endif /* PARALLAX_DEVICE_R4300_TLB_H */
