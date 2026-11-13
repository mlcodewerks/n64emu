#include "support.h"
#include "device/disk.h"
#include "device/dd_controller.h"
int main(void)
{
    unit_headless();struct dd_sys_data sys={0};struct test_storage s={(uint8_t*)&sys,sizeof(sys),0,0,0};
    struct dd_disk disk={0};disk.storage=&s;disk.istorage=&test_storage_api;disk.format=DISK_FORMAT_SDK;
    unsigned cases=0;
    for(unsigned type=0;type<7;++type){sys.type=type;GenerateLBAToPhysTable(&disk);
        assert(LBAToByte(&sys,0,1)==19720 && LBAToVZone(&sys,0)==0);
        uint64_t total=0;
        for(unsigned lba=0;lba<=MAX_LBA;++lba){uint16_t phys=LBAToPhys(&sys,lba);
            assert(disk.lba_phys_table[lba]==phys);
            assert(PhysToLBA(&disk,(phys>>12)&1,phys&0xfff,(phys>>13)&1)==lba);
            unsigned size=LBAToByte(&sys,lba,1);assert(size>=9520 && size<=19720 && size%85==0);
            total+=size;++cases;
        }
        assert(total==LBAToByte(&sys,0,MAX_LBA+1));
    }
    uint32_t rom[]={0x12345678,0x9abcdef0};time_t now=1000;struct dd_controller dd;
    memset(&dd,0xa5,sizeof(dd));init_dd(&dd,&now,&test_clock_api,rom,sizeof(rom),NULL,NULL,NULL);poweron_dd(&dd);
    assert(dd.regs[DD_ASIC_ID_REG]==0x00030000 && !dd.bm_write && !dd.bm_reset_held && !dd.bm_zone);
    for(unsigned i=0;i<sizeof(dd.c2s_buf);++i)assert(!dd.c2s_buf[i]);
    for(unsigned i=0;i<sizeof(dd.ds_buf);++i)assert(!dd.ds_buf[i]);
    for(unsigned i=0;i<sizeof(dd.ms_ram);++i)assert(!dd.ms_ram[i]);
    for(unsigned i=0;i<2;++i){uint32_t v;read_dd_rom(&dd,0x06000000+i*4,&v);assert(v==rom[i]);}
    printf("64DD disks: %u logical/physical round trips and seven geometry size totals passed.\n",cases);return 0;
}
