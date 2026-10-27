#include "dps_model.h"
#include <string.h>
#define RDP_DPS_WIN 40u
#define RDP_DPS_ROWS 1024u
#define PIXEL_SIZE_16BIT 2
#define DPS_WORKERS 12u
struct dps_model {
    uint32_t valid,total,base,pend_pos1,pend_alive;
    int cap_on;
    uint8_t sched[RDP_DPS_WIN];
    uint32_t val[RDP_DPS_WIN];
    uint8_t val_set[RDP_DPS_WIN];
    uint32_t seed_val[4];
    uint8_t seed_set[4];
    uint32_t nrows;
    int16_t row_x0[RDP_DPS_ROWS],row_x1[RDP_DPS_ROWS],row_span[RDP_DPS_ROWS];
};
struct span {
    uint8_t dps_cap,dps_seed_n;
    int16_t dps_cap_x0,dps_cap_x1;
    int32_t dps_voff;
    int16_t dps_seed_x[4];
    uint8_t dps_seed_k[4];
};
static struct {
    struct dps_model dps;
    struct span span[RDP_DPS_ROWS];
    struct { int32_t xh,yh,xl,yl; } clip;
    unsigned fb_size,scfield,sckeepodd;
    struct { uint8_t r,g,b; } pixel_color;
} state[DPS_WORKERS];
static _Thread_local unsigned current_worker;
int softrdp_dps_armed;
/* Reuse only the licensed cen64 span-buffer model, never the old rasterizer. */
#include "../angrylion/n64video/rdp/dps.c"

void softrdp_dps_reset(void)
{
    memset(state,0,sizeof(state));
    softrdp_dps_armed=0;
    current_worker=0;
}
void softrdp_dps_begin(unsigned worker,const softrdp_dps_setup *s)
{
    current_worker=worker<DPS_WORKERS?worker:0;
    state[current_worker].dps.cap_on=0;
    if (!softrdp_dps_armed || !s || (s->size!=2 && s->size!=3)) return;
    state[current_worker].clip.xh=s->x0;
    state[current_worker].clip.yh=s->y0;
    state[current_worker].clip.xl=s->x1;
    state[current_worker].clip.yl=s->y1;
    state[current_worker].fb_size=s->size;
    state[current_worker].scfield=s->field;
    state[current_worker].sckeepodd=s->keep_odd;
    dps_prepass(current_worker,s->flip,s->yh,s->ym,s->yl,s->xh,s->xm,s->xl,s->dxhdy,s->dxmdy,s->dxldy);
}
int softrdp_dps_row(unsigned y)
{
    return y<RDP_DPS_ROWS && state[current_worker].dps.cap_on && state[current_worker].span[y].dps_cap;
}
void softrdp_dps_capture(unsigned y,int x,uint8_t r,uint8_t g,uint8_t b,unsigned coverage)
{
    if (!softrdp_dps_row(y)) return;
    state[current_worker].pixel_color.r=r;
    state[current_worker].pixel_color.g=g;
    state[current_worker].pixel_color.b=b;
    dps_capture(current_worker,&state[current_worker].span[y],x,coverage);
}
int softrdp_dps_take(unsigned workers,uint32_t words[32])
{
    if (!softrdp_dps_armed || !words) return 0;
    return dps_take(workers>DPS_WORKERS?DPS_WORKERS:workers,words);
}
