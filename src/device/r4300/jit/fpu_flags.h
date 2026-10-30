#ifndef PARALLAXJIT_FPU_FLAGS_H
#define PARALLAXJIT_FPU_FLAGS_H
/* Exact finite lookup of host exception/rounding encodings. The existing
 * validation, guest exception handling and host environment restoration stay
 * in place; no floating-point result rule is relaxed. */
struct fpu_flag_maps {
    unsigned char sse[64],neon[32];
    unsigned mxcsr[4];
    constexpr fpu_flag_maps() : sse{},neon{},mxcsr{0x1f80,0x7f80,0x5f80,0x3f80} {
        for(unsigned x=0;x<64;x++)sse[x]=((x>>5)&1)|((x>>3)&2)|((x>>1)&4)|((x<<1)&8)|((x<<4)&16);
        for(unsigned x=0;x<32;x++)neon[x]=((x>>4)&1)|((x>>2)&2)|(x&4)|((x<<2)&8)|((x<<4)&16);
    }
};
alignas(64) static constexpr fpu_flag_maps fpu_maps;
#endif
