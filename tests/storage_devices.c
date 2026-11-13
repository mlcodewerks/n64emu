#include "support.h"
#include "device/cart.h"
#include "core/types.h"
int main(void)
{
    unit_headless();
    uint8_t bytes[FLASHRAM_SIZE],ram[256],out[16],pattern[8]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
    unsigned cases=0;
    for(unsigned large=0;large<2;++large){
        struct test_storage s={bytes,large?2048:512,0,0,0};struct eeprom e;
        memset(bytes,0xcc,sizeof(bytes));init_eeprom(&e,large?JDT_EEPROM_16K:JDT_EEPROM_4K,&s,&test_storage_api);
        uint8_t ack=0xff;unsigned block=s.length/8-1;eeprom_write_block(&e,block,pattern,&ack);
        assert(ack==0 && s.saves==1 && s.start==s.length-8 && s.saved==8);
        memset(out,0,sizeof(out));eeprom_read_block(&e,block,out);assert(!memcmp(out,pattern,8));
        assert(bytes[s.length]==0xcc);++cases;
        if(!large){ack=0xaa;eeprom_write_block(&e,64,pattern,&ack);assert(s.saves==1 && bytes[512]==0xcc);++cases;}
    }
    struct test_storage s={bytes,sizeof(bytes),0,0,0};struct flashram f;
    format_flashram(bytes);init_flashram(&f,MX29L0000_ID,&s,&test_storage_api);poweron_flashram(&f);
    for(unsigned i=0;i<128;++i)ram[i^S8]=(uint8_t)(i*3+7);
    write_flashram(&f,0x08010000,0xb4000000,UINT32_MAX);
    flashram_dma_read(&f,ram,0,0x08000000,128);assert(s.saves==0 && bytes[384]==0xff);
    write_flashram(&f,0x08010000,0xa5000003,UINT32_MAX);
    assert(s.saves==1 && s.start==384 && s.saved==128 && (f.status&4));
    for(unsigned i=0;i<128;++i)assert(bytes[(384+i)^S8]==(uint8_t)(i*3+7));
    assert(bytes[383]==0xff && bytes[512]==0xff);++cases;
    write_flashram(&f,0x08010000,0xe1000000,UINT32_MAX);uint32_t value;
    flashram_dma_write(&f,ram,0,0x08000000,8);
    assert(((uint32_t*)ram)[0]==FLASHRAM_TYPE_ID && ((uint32_t*)ram)[1]==MX29L0000_ID);++cases;
    memset(bytes,0,sizeof(bytes));write_flashram(&f,0x08010000,0x4b000080,UINT32_MAX);
    write_flashram(&f,0x08010000,0x78000000,UINT32_MAX);
    assert(s.start==16384 && s.saved==16384 && (f.status&8));
    for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==(i>=16384 && i<32768?0xff:0));++cases;
    struct sram sr;s.length=SRAM_SIZE;s.saves=0;memset(bytes,0,SRAM_SIZE);init_sram(&sr,&s,&test_storage_api);
    write_sram(&sr,0x08000004,0x12345678,UINT32_MAX);write_sram(&sr,0x08000004,0xabcd0000,0xffff0000);
    read_sram(&sr,0x08000004,&value);assert(value==0xabcd5678 && s.saves==2);++cases;
    printf("Cartridge saves: %u EEPROM/FlashRAM/SRAM boundary and persistence cases passed.\n",cases);return 0;
}
