#include "support.h"
#include "device/pif.h"
#include "core/joybus.h"
static unsigned setups,processed,notifications;
static uint8_t* ram_start;
void core_input_command(int control,unsigned char* cmd){assert(control==-1 && !cmd);++notifications;}
void core_input_read(int control,unsigned char* cmd){assert(control==-1 && !cmd);}
static void bounds(void* p,uint8_t* tx,const uint8_t* tb,const uint8_t* rx,const uint8_t* rb)
{(void)p;assert(tx>=ram_start && tx<ram_start+64 && rx==tx+1);assert(tb==tx+2 && rb==tb+(*tx&63));assert(rb+(*rx&63)<=ram_start+64);++setups;}
static void process(void* p,const uint8_t* tx,const uint8_t* tb,uint8_t* rx,uint8_t* rb)
{(void)p;assert(*tx==1 && *rx==3 && tb[0]==0);rb[0]=0x05;rb[1]=0;rb[2]=1;++processed;}
static const struct joybus_device_interface joy={.process=process,.post_setup=bounds};
int main(void)
{
    unit_headless();uint8_t guarded[66];struct pif p={0};p.ram=guarded+1;ram_start=p.ram;
    for(unsigned k=0;k<5;++k)p.channels[k].ijbd=&joy;
    memset(guarded,0xa5,sizeof(guarded));memset(p.ram,0,64);
    const uint8_t command[]={0xff,0,1,3,0,0,0,0,0xfe};memcpy(p.ram,command,sizeof(command));
    setup_channels_format(&p);assert(setups==1 && !p.channels[0].tx && p.channels[1].tx==p.ram+2 && !p.channels[2].tx);
    update_pif_ram(&p);assert(processed==1 && p.ram[5]==5 && p.ram[7]==1);
    /* A malformed transfer must never expose a response outside PIF RAM.
     * Exhaust every remaining length and offset, including stale channels. */
    unsigned malformed=0;
    for(unsigned start=0;start<64;++start)for(unsigned tx=1;tx<64;++tx){
        if(start+2+tx+3<=64)continue;
        for(unsigned k=0;k<5;++k)p.channels[k].tx=p.ram+2;
        memset(p.ram,0xff,64);p.ram[start]=(uint8_t)tx;if(start<63)p.ram[start+1]=3;
        unsigned before=setups;setup_channels_format(&p);assert(setups==before);
        for(unsigned k=0;k<5;++k)assert(!p.channels[k].tx);
        assert(guarded[0]==0xa5 && guarded[65]==0xa5);++malformed;
    }
    assert(notifications==malformed+1);
    printf("PIF: valid Joybus processing and %u truncated packet/stale-channel cases passed.\n",malformed);return 0;
}
