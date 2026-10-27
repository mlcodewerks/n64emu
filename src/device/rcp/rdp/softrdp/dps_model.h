#ifndef SOFTRDP_DPS_MODEL_H
#define SOFTRDP_DPS_MODEL_H
#include <stdint.h>
#include <stdbool.h>
/* The diagnostic stream model is shared with the previous backend; it does
 * no rasterization. Call control functions only after the worker barrier. */
extern int softrdp_dps_armed;
typedef struct softrdp_dps_setup {
    int32_t yh,ym,yl,xh,xm,xl,dxhdy,dxmdy,dxldy;
    uint16_t x0,y0,x1,y1;
    bool flip,field,keep_odd;
    unsigned size;
} softrdp_dps_setup;
void softrdp_dps_reset(void);
void softrdp_dps_begin(unsigned worker,const softrdp_dps_setup *setup);
int softrdp_dps_row(unsigned y);
void softrdp_dps_capture(unsigned y,int x,uint8_t r,uint8_t g,uint8_t b,unsigned coverage);
int softrdp_dps_take(unsigned workers,uint32_t words[32]);
#endif
