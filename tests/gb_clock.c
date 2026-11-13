#include "support.h"
#include "device/gb.h"
static struct test_storage rom,ram;
static void rom_init(void* p,void** s,const struct storage_backend_interface** i){*s=p;*i=&test_storage_api;}
static void rom_release(void* p){(void)p;}
static void ram_init(void* p,size_t size,void** s,const struct storage_backend_interface** i){struct test_storage* m=p;assert(size<=m->length);*s=m;*i=&test_storage_api;}
static void ram_release(void* p){(void)p;}
int main(void)
{
    unit_headless();
    uint8_t rom_bytes[8*0x4000],ram_bytes[0x8000],value;time_t now=1000;
    for(unsigned bank=0;bank<8;++bank)memset(rom_bytes+bank*0x4000,bank,0x4000);
    rom_bytes[0x147]=3;rom_bytes[0x148]=2;rom_bytes[0x149]=3;
    rom=(struct test_storage){rom_bytes,sizeof(rom_bytes),0,0,0};ram=(struct test_storage){ram_bytes,sizeof(ram_bytes),0,0,0};memset(ram_bytes,0,sizeof(ram_bytes));
    struct gb_cart g;init_gb_cart(&g,&rom,rom_init,rom_release,&ram,ram_init,ram_release,&now,&test_clock_api,NULL,NULL);poweron_gb_cart(&g);
    read_gb_cart(&g,0x4000,&value,1);assert(value==1);
    value=2;write_gb_cart(&g,0x2000,&value,1);read_gb_cart(&g,0x4000,&value,1);assert(value==2);
    value=0;write_gb_cart(&g,0x2000,&value,1);read_gb_cart(&g,0x4000,&value,1);assert(value==1);
    value=0x5a;write_gb_cart(&g,0xa123,&value,1);assert(ram.saves==0);
    value=0x0a;write_gb_cart(&g,0x0000,&value,1);value=0x5a;write_gb_cart(&g,0xa123,&value,1);read_gb_cart(&g,0xa123,&value,1);assert(value==0x5a && ram.saves==1);
    value=0;write_gb_cart(&g,0x0000,&value,1);value=0xff;write_gb_cart(&g,0xa123,&value,1);assert(ram.saves==1 && ram_bytes[0x123]==0x5a);
    struct mbc3_rtc rtc;init_mbc3_rtc(&rtc,&now,&test_clock_api);poweron_mbc3_rtc(&rtc);rtc.last_time=now;
    rtc.regs[0]=59;rtc.regs[1]=59;rtc.regs[2]=23;rtc.regs[3]=255;rtc.regs[4]=1;
    ++now;latch_mbc3_rtc_regs(&rtc,1);
    assert(read_mbc3_rtc_regs(&rtc,0)==0 && read_mbc3_rtc_regs(&rtc,1)==0 && read_mbc3_rtc_regs(&rtc,2)==0);
    assert(read_mbc3_rtc_regs(&rtc,3)==0 && (read_mbc3_rtc_regs(&rtc,4)&0x81)==0x80);
    now+=63;assert(read_mbc3_rtc_regs(&rtc,0)==0);latch_mbc3_rtc_regs(&rtc,0);
    assert(read_mbc3_rtc_regs(&rtc,0)==3 && read_mbc3_rtc_regs(&rtc,1)==1);
    /* The low-byte rollover must propagate into day bit 8, including when
     * more than a day elapses between reads. Carry remains sticky. */
    for(unsigned elapsed=1;elapsed<=3;++elapsed){
        poweron_mbc3_rtc(&rtc);rtc.last_time=now;
        rtc.regs[0]=59;rtc.regs[1]=59;rtc.regs[2]=23;rtc.regs[3]=255;
        now+=1+(elapsed-1)*86400;
        assert(read_mbc3_rtc_regs(&rtc,3)==elapsed-1 && read_mbc3_rtc_regs(&rtc,4)==1);
    }
    puts("Game Boy: MBC1 bank/RAM protection/persistence and RTC rollover/latch passed.");return 0;
}
