#include "legacy_gpu.hpp"
#include <stdexcept>

namespace glrdp::legacy {
void validate_images(const Image &a, const Image &b, unsigned limit) {
    if (!a.width || !a.height || a.width != b.width || a.height != b.height || a.width > limit ||
        a.height > limit || a.rgba.size() != std::size_t(a.width) * a.height * 4 ||
        a.rgba.size() != b.rgba.size())
        throw std::runtime_error("legacy GPU invalid input images");
}
unsigned storage_extent(unsigned n, bool pot) {
    if (!pot)
        return n;
    unsigned result = 1;
    while (result < n) {
        if (result > 0x40000000u)
            throw std::runtime_error("legacy GPU texture size overflow");
        result *= 2;
    }
    return result;
}
std::string fragment_shader(Kernel kernel, bool hlsl) {
    std::string s = hlsl ? R"(
#define V2 float2
#define V4 float4
#define SAMPLE0(p) tex2D(plane0,p)
#define SAMPLE1(p) tex2D(plane1,p)
#define LOOP [loop]
sampler2D plane0 : register(s0);
sampler2D plane1 : register(s1);
float4 dimensions : register(c0);
)"
                         : R"(#version 110
#define V2 vec2
#define V4 vec4
#define SAMPLE0(p) texture2D(plane0,p)
#define SAMPLE1(p) texture2D(plane1,p)
#define LOOP
uniform sampler2D plane0, plane1;
uniform vec4 dimensions;
varying vec2 uv;
)";
    s += R"(
float modulo(float x, float d) { return x - floor(x / d) * d; }
V4 load_texel0(V2 p) { return floor(SAMPLE0(p) * 255.0 + 0.5); }
V4 load_texel1(V2 p) { return floor(SAMPLE1(p) * 255.0 + 0.5); }
V4 fetch1(V2 p) {
    // Clamp to the logical rectangle, including on padded/conditional-NPOT GPUs.
    p = clamp(p, V2(0.0, 0.0), dimensions.zw - V2(1.0, 1.0));
    return load_texel1((p + V2(0.5, 0.5)) * dimensions.xy);
}
float sign9(float x) { return x >= 256.0 ? x - 512.0 : x; }
float rdp_sign9(float x) { return x >= 384.0 ? x - 512.0 : x; }
float combine_limb(float av, float bv, float cv, float dv) {
    float diff=rdp_sign9(av)-rdp_sign9(bv), c=sign9(cv);
    float ah=floor(diff/16.0), al=diff-ah*16.0;
    float ch=floor(c/16.0), cl=c-ch*16.0;
    float carry=floor((al*cl+128.0)/16.0);
    float q=modulo(ah*ch+rdp_sign9(dv)+floor((ah*cl+al*ch+carry)/16.0),512.0);
    return q<256.0?q:(q<384.0?255.0:0.0);
}
V4 divide_rgb(V4 numerator, float d) {
    V4 n=modulo(0.0,1.0)+floor(numerator/4.0);
    n=n-floor(n/2048.0)*2048.0;
    float inv=15.0-d, weight=128.0;
    V4 partial=V4(inv,inv,inv,inv)+floor(n/256.0)+V4(1.0,1.0,1.0,1.0);
    partial=partial-floor(partial/8.0)*8.0;
    V4 result=V4(0.0,0.0,0.0,0.0), previous=result;
    LOOP for(int k=0;k<8;k++) {
        V4 bit=floor(n/weight);bit=bit-floor(bit/2.0)*2.0;
        V4 temp=partial*2.0+bit+V4(d,d,d,d)+previous*(inv+1.0-d);
        partial=temp-floor(temp/8.0)*8.0;
        previous=floor(temp/16.0);previous=previous-floor(previous/2.0)*2.0;
        result+=previous*weight;weight*=0.5;
    }
    return result;
}
V4 slice0(V2 p,float i) {return load_texel0(p+V2(0.0,i*dimensions.w)*dimensions.xy);}
V4 slice1(V2 p,float i) {return load_texel1(p+V2(0.0,i*dimensions.w)*dimensions.xy);}
V4 expand_operand(V4 v) {
    return V4(v.r+modulo(v.a,2.0)*256.0,
              v.g+modulo(floor(v.a/2.0),2.0)*256.0,
              v.b+modulo(floor(v.a/4.0),2.0)*256.0,0.0);
}
V4 blend_resolved(V4 pixel,V4 x,V4 y,V4 factors) {
    float live=modulo(factors.a,2.0);
    x=x*(1.0-live)+pixel*live;
    live=modulo(floor(factors.a/2.0),2.0);
    y=y*(1.0-live)+pixel*live;
    if(factors.b<0.5)return x;
    V4 num=x*factors.r+y*(factors.g+1.0);
    if(factors.b<1.5){V4 v=floor(num/32.0);return v-floor(v/256.0)*256.0;}
    float d=floor(factors.r/4.0)+floor(factors.g/4.0)+1.0;
    if(d==8.0)return floor(num/32.0);
    return divide_rgb(num,d);
}
V4 shade(V2 p) {
    V4 a = load_texel0(p), b = load_texel1(p);
)";
    switch (kernel) {
    case Kernel::Copy:
        s += "return a / 255.0;\n";
        break;
    case Kernel::PackDepth:
        // Never reconstruct an 18-bit Z in one float: some GL2 chips use FP24.
        s += "return V4(a.r, a.g, modulo(a.b, 4.0) + modulo(b.r, 16.0) * 4.0, modulo(a.a, 8.0)) / 255.0;\n";
        break;
    case Kernel::Combine9:
        s += R"(
    float av = sign9(a.r + modulo(b.r, 2.0) * 256.0);
    float bv = sign9(a.g + modulo(floor(b.r / 2.0), 2.0) * 256.0);
    float cv = sign9(a.b + modulo(floor(b.r / 4.0), 2.0) * 256.0);
    float dv = sign9(a.a + modulo(floor(b.r / 8.0), 2.0) * 256.0);
    // Base-16 limbs keep every intermediate below 2048. A direct 9-bit
    // product loses low bits on old floating-point fragment hardware.
    float diff = av - bv;
    float ah = floor(diff / 16.0), al = diff - ah * 16.0;
    float ch = floor(cv / 16.0), cl = cv - ch * 16.0;
    float carry = floor((al * cl + 128.0) / 16.0);
    float q = ah * ch + dv + floor((ah * cl + al * ch + carry) / 16.0);
    q = modulo(q, 512.0);
    float saturated = q < 256.0 ? q : (q < 384.0 ? 255.0 : 0.0);
    return V4(modulo(q, 256.0), floor(q / 256.0), saturated, 255.0) / 255.0;
)";
        break;
    case Kernel::Filter3Point:
        s += R"(
    V2 base = a.ba;
    float fs = modulo(a.r, 32.0), ft = modulo(a.g, 32.0);
    V4 c0, cx, cy;
    if (fs + ft >= 32.0) {
        c0 = fetch1(base + V2(1.0, 1.0));
        cx = fetch1(base + V2(0.0, 1.0));
        cy = fetch1(base + V2(1.0, 0.0));
        fs = 32.0 - fs; ft = 32.0 - ft;
    } else {
        c0 = fetch1(base);
        cx = fetch1(base + V2(1.0, 0.0));
        cy = fetch1(base + V2(0.0, 1.0));
    }
    // Products are <= 8160; no hardware linear filtering or mip derivatives.
    return floor((c0 * 32.0 + (cx - c0) * fs + (cy - c0) * ft + 16.0) / 32.0) / 255.0;
)";
        break;
    case Kernel::Divider:
        s += "return "
             "divide_rgb(V4(a.r+a.g*256.0,a.r+a.g*256.0,a.r+a.g*256.0,0.0)*4.0,modulo(b.r,16.0))/255.0;\n";
        break;
    case Kernel::ColorPipeline:
        s += R"(
    V4 av=expand_operand(slice0(p,0.0)), bv=expand_operand(slice0(p,1.0));
    V4 cv=expand_operand(slice0(p,2.0)), dv=expand_operand(slice0(p,3.0));
    V4 flags=slice1(p,2.0);
    V4 color=V4(combine_limb(av.r,bv.r,cv.r,dv.r),combine_limb(av.g,bv.g,cv.g,dv.g),combine_limb(av.b,bv.b,cv.b,dv.b),0.0);
    if(flags.g>0.5)color=V4(av.r<256.0?av.r:(av.r<384.0?255.0:0.0),av.g<256.0?av.g:(av.g<384.0?255.0:0.0),av.b<256.0?av.b:(av.b<384.0?255.0:0.0),0.0);
    if(flags.r>0.5)color=blend_resolved(color,slice0(p,4.0),slice0(p,5.0),slice1(p,0.0));
    if(flags.r>1.5)color=blend_resolved(color,slice0(p,6.0),slice0(p,7.0),slice1(p,1.0));
    color=color-floor(color/256.0)*256.0;
    V4 threshold=slice1(p,3.0);
    V4 remainder=color-floor(color/8.0)*8.0;
    V4 increment=clamp(remainder-threshold, V4(0.0,0.0,0.0,0.0),V4(1.0,1.0,1.0,1.0));
    color=color*(V4(1.0,1.0,1.0,1.0)-increment)+clamp(floor(color/8.0)*8.0+V4(8.0,8.0,8.0,8.0),V4(0.0,0.0,0.0,0.0),V4(255.0,255.0,255.0,255.0))*increment;
    return V4(color.rgb,flags.a)/255.0;
)";
        break;
    }
    s += "}\n";
    s += hlsl ? "float4 main(float2 uv : TEXCOORD0) : COLOR0 { return shade(uv); }\n"
              : "void main() { gl_FragColor = shade(uv); }\n";
    return s;
}
} // namespace glrdp::legacy
