#include "support.h"
#include "device/memory.h"
static unsigned notifications;
static uint16_t regions[4];
void r4300_fastmem_region_changed(struct memory* m,uint16_t r){assert(m->cpu_fastmem);assert(notifications<4);regions[notifications++]=r;}
struct port {uint32_t data,address;unsigned reads,writes;};
static void read_port(void* p,uint32_t a,uint32_t* v){struct port* s=p;s->address=a;++s->reads;*v=s->data;}
static void write_port(void* p,uint32_t a,uint32_t v,uint32_t mask){struct port* s=p;s->address=a;++s->writes;masked_write(&s->data,v,mask);}
int main(void)
{
    unit_headless();static struct memory m;struct port a={0x12345678},b={0xaabbccdd};
    struct mem_mapping map={0x120000,0x13ffff,CORE_MEMORY_ROM,{&a,read_port,write_port}};
    init_memory(&m,&map,1,&a,NULL);assert(m.base==&a && !m.cpu_fastmem && !notifications);
    for(unsigned address=0x120000;address<=0x13ffff;address+=0xffff){uint32_t v;const struct mem_handler* h=mem_get_handler(&m,address);mem_read32(h,address,&v);assert(v==a.data && a.address==address);}
    mem_write32(mem_get_handler(&m,0x130004),0x130004,0xabcd0000,0xffff0000);assert(a.data==0xabcd5678 && a.writes==1);
    map.begin=0x130000;map.handler.opaque=&b;m.cpu_fastmem=(void*)&m;apply_mem_mapping(&m,&map);
    assert(notifications==1 && regions[0]==0x13 && mem_get_handler(&m,0x12ffff)->opaque==&a);
    uint32_t v;mem_read32(mem_get_handler(&m,0x130000),0x130000,&v);assert(v==0xaabbccdd && b.reads==1);
    map.begin=map.end=UINT32_MAX;apply_mem_mapping(&m,&map);assert(regions[1]==0xffff && notifications==2);
    puts("Memory: inclusive region boundaries, dispatch, masks, remapping and fastmem invalidation passed.");return 0;
}
