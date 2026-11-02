/* Integer-conversion regressions, including nemu64's CVT.D.L trap input.
 * With FPU_REFERENCE, compare the portable implementation against x64 SSE2. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "device/r4300/fpu_native.h"

#ifdef FPU_REFERENCE
unsigned fpu_reference_eval(unsigned, unsigned, uint64_t, uint64_t, unsigned, uint64_t *);
#endif
static unsigned checks;
static int check(unsigned fmt, unsigned fn, uint64_t input, unsigned rm,
                 uint64_t expected, unsigned expected_flags)
{
    uint64_t actual = 0;
    unsigned flags = fpu_native_eval(fmt, fn, input, 0, rm, &actual);
    ++checks;
    if (actual == expected && flags == expected_flags) return 0;
    fprintf(stderr, "CVT fmt=%u fn=%u input=%016" PRIx64 " rm=%u: %016" PRIx64
            "/%u expected %016" PRIx64 "/%u\n", fmt, fn, input, rm,
            actual, flags, expected, expected_flags);
    return 1;
}
int main(void)
{
    static const struct {
        unsigned fmt, fn;
        uint64_t input, result[4];
        unsigned flags;
    } cases[] = {
        {21,33,UINT64_C(0x7ffffffffffffd),{UINT64_C(0x435fffffffffffff),UINT64_C(0x435fffffffffffff),UINT64_C(0x4360000000000000),UINT64_C(0x435fffffffffffff)},1},
        {21,33,INT64_MAX,{UINT64_C(0x43e0000000000000),UINT64_C(0x43dfffffffffffff),UINT64_C(0x43e0000000000000),UINT64_C(0x43dfffffffffffff)},1},
        {21,33,UINT64_C(0x8000000000000000),{UINT64_C(0xc3e0000000000000),UINT64_C(0xc3e0000000000000),UINT64_C(0xc3e0000000000000),UINT64_C(0xc3e0000000000000)},0},
        {20,32,16777217,{0x4b800000,0x4b800000,0x4b800001,0x4b800000},1},
        {20,32,UINT64_C(0xfffffffffeffffff),{0xcb800000,0xcb800000,0xcb800000,0xcb800001},1},
        {20,32,16777219,{0x4b800002,0x4b800001,0x4b800002,0x4b800001},1},
        {20,33,INT32_MAX,{UINT64_C(0x41dfffffffc00000),UINT64_C(0x41dfffffffc00000),UINT64_C(0x41dfffffffc00000),UINT64_C(0x41dfffffffc00000)},0},
        {21,32,0,{0,0,0,0},0},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        for (unsigned rm=0;rm<4;++rm)
            if (check(cases[i].fmt,cases[i].fn,cases[i].input,rm,cases[i].result[rm],cases[i].flags)) return 1;
#ifdef FPU_REFERENCE
    uint64_t seed = UINT64_C(0x1983011700000001);
    for (unsigned i=0;i<100000;++i) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        for (unsigned fmt=20;fmt<=21;++fmt) for (unsigned fn=32;fn<=33;++fn)
            for (unsigned rm=0;rm<4;++rm) {
                uint64_t expected;
                unsigned flags=fpu_reference_eval(fmt,fn,seed,0,rm,&expected);
                if(check(fmt,fn,seed,rm,expected,flags)) return 1;
            }
    }
#endif
    printf("%u integer conversion result/rounding/exception checks passed.\n",checks);
    return 0;
}
