#ifndef N64EMU_NEAD_VALIDATION_H
#define N64EMU_NEAD_VALIDATION_H
#include <stdint.h>
#include <string.h>
#include "hle_timing_nead.h"

static inline int nead_known_signature(uint32_t signature)
{
    switch(signature) {
    case 0x11181350u: case 0x111812e0u: case 0x110412acu: case 0x110412ccu:
    case 0x1cd01250u: case 0x1f08122cu: case 0x1f38122cu: case 0x1f681230u:
    case 0x1f801250u: case 0x109411f8u: case 0x1eac11b8u: case 0x1f701238u:
    case 0x1f4c1230u:return 1;
    default:return 0;
    }
}

/* Reconstruct exactly the IMEM image seeded by the identified NEAD walker.
 * SF Japan and Wave Shindou retain the first 0x80 bytes of their SDK boot. */
static inline uint32_t nead_loaded_profile(const uint32_t task[16],
    const uint8_t *ram, uint32_t ram_size, uint32_t signature)
{
    uint32_t image[1024],text=task[4]&0x7fffffu,boot=task[2]&0x7fffffu;
    uint32_t prefix=signature==0x111812e0u || signature==0x110412acu ? 0x80u : 0u;
    if(ram_size<sizeof(image) || (text&3u) || text>ram_size-(sizeof(image)-prefix))return 0;
    if(prefix) {
        if((boot&3u) || boot>ram_size-prefix)return 0;
        memcpy(image,ram+boot,prefix);
    }
    memcpy((uint8_t*)image+prefix,ram+text,sizeof(image)-prefix);
    return hle_timing_nead_profile(image);
}

/* ADDMIXER's partial vectors depend on scratch registers. Admit only the
 * independently exercised simple lists, preserving tables and command DMA.
 * Other lists select native execution before HLE has any side effects. */
static inline int nead_addmix_hle_supported(const uint32_t task[16],
    const uint8_t *ram,uint32_t ram_size,uint32_t signature)
{
    uint32_t profile=nead_loaded_profile(task,ram,ram_size,signature);
    uint32_t list=task[12]&0x7fffffu,bytes=task[13],data=task[6]&0x7fffffu;
    uint32_t text=task[4]&0x7fffffu,boot=task[2]&0x7fffffu,size,rounded,i;
    if(!profile || task[1] || bytes>((profile>=3u && profile<=5u)?128u:64u))return 0;
    size=profile==4u || profile==5u ? 0x330u : 0x2dfu;rounded=(size+7u)&~7u;
    if(task[7]!=size || (data&7u) || rounded>ram_size || data>ram_size-rounded ||
        hle_timing_word_fingerprint((const uint32_t*)(ram+data),rounded/4u)!=hle_timing_nead_data_hash(profile))return 0;
    if(profile==4u || profile==5u) {
        if(task[3]!=0xd0u || (boot&3u) || ram_size<0xd0u || boot>ram_size-0xd0u ||
            hle_timing_word_fingerprint((const uint32_t*)(ram+boot),52u)!=UINT64_C(0x0f7c8dfaf9f62fc0))return 0;
    }
    for(i=0;i<bytes;i+=8u) {
        uint32_t w1,w2,opcode,count,from,to,extent;
        memcpy(&w1,ram+list+i,4);memcpy(&w2,ram+list+i+4u,4);
        opcode=w1>>24;count=(w1>>12)&0xff0u;
        if(opcode==8u)continue;
        if(opcode==4u) {
            from=w2>>16;to=w2&0xffffu;extent=count?(count+63u)&~63u:64u;
            if(((from|to)&15u) || from<0x400u || to<0x400u ||
                from+extent>0xf90u || to+extent>0xf90u)return 0;
        } else if(opcode==2u) {
            count=w2&0xffffu;to=w1&0xffffu;extent=(count+15u)&~15u;
            if(count && (to<0x400u || to+extent>0xf90u))return 0;
        } else if(opcode==10u) {
            int short_tail=profile==2u || profile==3u || profile==6u;
            count=w2&0xffffu;from=w1&0xffffu;to=w2>>16;
            extent=short_tail?(count+1u)&~1u:(count+15u)&~15u;
            if(!extent && short_tail && profile!=3u)extent=2u;
            if(extent && (from<0x400u || to<0x400u || from+extent>0xf90u || to+extent>0xf90u))return 0;
        } else if(opcode==0x14u || opcode==0x15u) {
            uint32_t address=(w2&0xffffffu)&~7u,at=w1&0xff8u;
            if(!count && (profile==3u || profile==6u))continue;
            if(!count || count>ram_size || address>ram_size-count)return 0;
            if(opcode==0x14u) {
                if((w1&0x1000u) || at<0x400u || at+count>0xf90u)return 0;
            } else if((address<list+bytes && list<address+count) ||
                (address<data+rounded && data<address+count) ||
                (address<text+4096u && text<address+count) ||
                ((profile==4u || profile==5u) && address<boot+0xd0u && boot<address+count))return 0;
        } else return 0;
    }
    return 1;
}

/* An IMEM LOAD changes the program that will execute next. A zero masked
 * length can encode 256 DMA rows and overwrite command/control memory.
 * Select native execution before HLE changes any task state. The two proved
 * zero-return programs can retain HLE after complete executable validation. */
static inline int nead_load_hle_supported(const uint32_t task[16],
    const uint8_t *ram, uint32_t ram_size, uint32_t signature)
{
    uint32_t list = task[12] & 0x7fffffu, bytes = task[13], i;
    uint32_t verified_profile = 0;
    if(!nead_known_signature(signature))return 1;
    if (!bytes || (bytes & 7u) || (list & 7u) || bytes > ram_size ||
        list > ram_size - bytes) return 0;
    for(i=0;i<bytes;i+=8u) {
        uint32_t command;memcpy(&command,ram+list+i,4);
        if((command>>24)==4u)return nead_addmix_hle_supported(task,ram,ram_size,signature);
    }
    for (i = 0; i < bytes; i += 8u) {
        uint32_t command;
        memcpy(&command, ram + list + i, 4);
        if (((command >> 24) & 0x7fu) != 0x14u) continue;
        if (!((command >> 12) & 0xff0u) &&
            (signature == 0x1eac11b8u || signature == 0x109411f8u)) {
            if (!verified_profile) {
                verified_profile=nead_loaded_profile(task,ram,ram_size,signature);
            }
            if (verified_profile != (signature == 0x1eac11b8u ? 3u : 6u)) return 0;
            continue;
        }
        /* Mario Kart adds its 0x450 audio-buffer base before SP_MEM_ADDR
         * selects DMEM/IMEM. A carry across 0x1000 changes the bank. */
        if (!((command >> 12) & 0xff0u) ||
            (((command & 0xffffu) + (signature == 0x11181350u ? 0x450u : 0u)) & 0x1000u)) return 0;
    }
    return 1;
}
#endif
