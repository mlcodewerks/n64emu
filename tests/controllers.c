#include "support.h"
#include "device/controllers.h"
#include "core/controller_input.h"
#include "core/rumble.h"
static core_result input(void* p,uint32_t* value){*value=*(uint32_t*)p;return CORE_OK;}
static core_result absent(void* p,uint32_t* value){(void)p;(void)value;return CORE_BACKEND_ERROR;}
static void rumble(void* p,enum rumble_action action){*(int*)p=action;}
/* Polynomial long division, separate from the controller's shift-register CRC. */
static uint8_t crc(const uint8_t* p){unsigned r=0;for(unsigned i=0;i<=32;++i){r=(r<<8)|(i<32?p[i]:0);for(int bit=15;bit>=8;--bit)if(r&(1u<<bit))r^=0x185u<<(bit-8);}return (uint8_t)r;}
int main(void)
{
    unit_headless();
    uint8_t bytes[MEMPAK_SIZE+32],payload[35]={JCMD_PAK_WRITE,0x7f,0xe0},reply[36]={0};
    struct test_storage s={bytes,MEMPAK_SIZE,0,0,0};struct mempak m;
    memset(bytes,0,sizeof(bytes));memset(bytes+MEMPAK_SIZE,0xa5,32);init_mempak(&m,&s,&test_storage_api);
    uint32_t pad=0x44332211;const struct controller_input_backend_interface backend={input};struct game_controller c;
    init_game_controller(&c,&g_standard_controller_flavor,&pad,&backend,&m,&g_imempak);
    g_ijoybus_device_controller.poweron(&c);uint8_t tx=1,rx=3,cmd=JCMD_STATUS;
    g_ijoybus_device_controller.process(&c,&tx,&cmd,&rx,reply);assert(reply[0]==5 && reply[1]==0 && (reply[2]&1));
    cmd=JCMD_CONTROLLER_READ;rx=4;uint32_t state=0;g_ijoybus_device_controller.process(&c,&tx,&cmd,&rx,(uint8_t*)&state);assert(state==pad);
    for(unsigned i=0;i<32;++i)payload[i+3]=(uint8_t)(i*7+0x55);
    tx=35;rx=1;g_ijoybus_device_controller.process(&c,&tx,payload,&rx,reply);
    assert(reply[0]==crc(payload+3) && s.saves==1 && s.start==0x7fe0 && s.saved==32);
    payload[0]=JCMD_PAK_READ;tx=3;rx=33;g_ijoybus_device_controller.process(&c,&tx,payload,&rx,reply);
    assert(!memcmp(reply,payload+3,32) && reply[32]==crc(reply));
    for(unsigned i=0;i<32;++i)assert(bytes[MEMPAK_SIZE+i]==0xa5);
    uint8_t block[32]={1};g_imempak.write(&m,0x8000,block,32);assert(s.saves==1);
    g_imempak.read(&m,0x8000,block,32);for(unsigned i=0;i<32;++i)assert(block[i]==0);
    cmd=JCMD_CONTROLLER_READ;tx=1;rx=3;memset(reply,0xaa,sizeof(reply));
    g_ijoybus_device_controller.process(&c,&tx,&cmd,&rx,reply);assert(rx&0x40);assert(reply[0]==0xaa);
    const struct controller_input_backend_interface missing={absent};c.icin=&missing;rx=4;
    g_ijoybus_device_controller.process(&c,&tx,&cmd,&rx,reply);assert(rx&0x80);
    int motor=-1;const struct rumble_backend_interface motor_api={rumble};struct rumblepak r;
    init_rumblepak(&r,&motor,&motor_api);poweron_rumblepak(&r);assert(motor==RUMBLE_STOP);
    g_irumblepak.read(&r,0x8000,block,32);for(unsigned i=0;i<32;++i)assert(block[i]==0x80);
    memset(block,1,32);g_irumblepak.write(&r,0xc000,block,32);assert(motor==RUMBLE_START);
    g_irumblepak.unplug(&r);assert(motor==RUMBLE_STOP);
    struct transferpak t;init_transferpak(&t,NULL);poweron_transferpak(&t);
    g_itransferpak.read(&t,0x8000,block,32);assert(block[0]==0);
    memset(block,0x84,32);g_itransferpak.write(&t,0x8000,block,32);g_itransferpak.read(&t,0x8000,block,32);assert(block[0]==0x84);
    g_itransferpak.read(&t,0xb000,block,32);assert(block[0]==CART_NOT_INSERTED);
    memset(block,0xfe,32);g_itransferpak.write(&t,0x8000,block,32);assert(!t.enabled);
    puts("Controllers: input/status/errors, pak CRC/boundaries, rumble and Transfer Pak passed.");return 0;
}
