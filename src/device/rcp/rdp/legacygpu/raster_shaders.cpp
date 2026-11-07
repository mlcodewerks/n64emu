#include "../glrdp/vi_filter_shader.hpp"
#include "legacy_gpu.hpp"
#include <cstdlib>
#include <string>
#include <regex>
namespace glrdp::legacy {
RasterKey raster_key(const RasterDraw &draw) {
    RasterKey key{};
    unsigned at = 0;
    for (float v : draw.constants[1])
        key[at++] = static_cast<unsigned char>(v);
    for (unsigned i = 19; i <= 28; i++)
        for (float v : draw.constants[i])
            key[at++] = static_cast<unsigned char>(v);
    for (unsigned i = 0; i < 4; i++)
        key[at++] = static_cast<unsigned char>(draw.constants[41][i]);
    key[at++] = static_cast<unsigned char>(draw.constants[45][0]);
    for (unsigned i = 88; i <= 90; i++)
        for (float v : draw.constants[i])
            key[at++] = static_cast<unsigned char>(v);
    key[at++] = static_cast<unsigned char>(draw.scale);
    return key;
}
std::string raster_fragment_shader(RasterShader pass, bool hlsl, const RasterDraw *specialize) {
    std::string s = hlsl ? R"(
#define V2 float2
#define V3 float3
#define V4 float4
#define SAMPLE(t,p) tex2Dlod(t,float4(p,0.0,0.0))
#define LOOP [loop]
float4 c[112] : register(c0);
sampler2D memory_color:register(s0), tile0:register(s1), tile1:register(s2), old_depth:register(s3), new_depth:register(s4), noise_tex:register(s5), reciprocal_tex:register(s6), combined_color:register(s7), first_alpha:register(s8), cached0:register(s9), cached1:register(s10), cached_lod:register(s11);
)"
                         : R"(#version 110
#define V2 vec2
#define V3 vec3
#define V4 vec4
#define SAMPLE(t,p) texture2D(t,p)
#define LOOP
uniform vec4 c[112];
uniform sampler2D memory_color,tile0,tile1,old_depth,new_depth,noise_tex,reciprocal_tex,combined_color,first_alpha,cached0,cached1,cached_lod;
)";
    s += hlsl ? "#define D3D_ROWS 1\n" : "#define D3D_ROWS 0\n";
    // RGBA8 intermediate passes bound the instruction count and preserve the same
    // quantization on GLSL 1.10 and ps_3_0 drivers.
    s += "#define SPLIT_SHADING 1\n#define SPLIT_TEXTURES 1\n#define SPLIT_LOD 1\n#define SPLIT_ALPHA 1\n";
    s += "#define LOD_PASS " + std::to_string(pass == RasterShader::LOD) + "\n";
    s += "#define TEXEL0_PASS " + std::to_string(pass == RasterShader::Texel0) + "\n";
    s += "#define TEXEL1_PASS " + std::to_string(pass == RasterShader::Texel1) + "\n";
    s += "#define SHADE_PASS " + std::to_string(pass == RasterShader::Shade || pass == RasterShader::Candidate) + "\n";
    s += "#define COVERAGE_ALPHA_PASS " + std::to_string(pass == RasterShader::CoverageAlpha) + "\n";
    s += "#ifndef GLES2_PIPELINE\n#define GLES2_PIPELINE 0\n#endif\n";
    s += "#define ALPHA0_PASS " + std::to_string(pass == RasterShader::Alpha0) + "\n";
    s += "#define DEPTH_PASS " + std::to_string(pass == RasterShader::Depth) + "\n";
    s += "#define VI_PASS " + std::to_string(pass == RasterShader::Present) + "\n";
    s += "#define VI_RECONSTRUCT_PASS " + std::to_string(pass == RasterShader::VIReconstruct) + "\n";
    s += "#define NOISE_PASS " + std::to_string(pass == RasterShader::Noise) + "\n";
    s += "#define COPY_PASS " + std::to_string(pass == RasterShader::Copy) + "\n";
    s += "#define TMEM_LOAD_PASS " + std::to_string(pass == RasterShader::LoadTMEM) + "\n";
    s += "#define TMEM_DECODE_PASS " + std::to_string(pass == RasterShader::DecodeTMEM) + "\n";
    s += R"(
float rem(float x,float d){return x-floor(x/d)*d;}
V4 octets(V4 x){return floor(x*255.0+0.5);}
float s9(float x){x=rem(x,512.0);return x>=256.0?x-512.0:x;}
float abd9(float x){x=rem(x,512.0);return x>=384.0?x-512.0:x;}
float combine9(float a,float b,float cc,float d){
    float diff=abd9(a)-abd9(b),v=s9(cc);
    float ah=floor(diff/16.0),al=diff-ah*16.0,ch=floor(v/16.0),cl=v-ch*16.0;
    float carry=floor((al*cl+128.0)/16.0);
    float q=rem(ah*ch+abd9(d)+floor((ah*cl+al*ch+carry)/16.0),512.0);
    return q<256.0?q:(q<384.0?255.0:0.0);
}
V3 divide_rgb(V3 numerator,float d){
    V3 n=floor(numerator/4.0);n=n-floor(n/2048.0)*2048.0;
    float inv=15.0-d,weight=128.0;
    V3 partial=V3(inv,inv,inv)+floor(n/256.0)+V3(1.0,1.0,1.0);
    partial=partial-floor(partial/8.0)*8.0;
    V3 result=V3(0.0,0.0,0.0),previous=result;
    LOOP for(int k=0;k<8;k++){
        V3 bit=floor(n/weight);bit=bit-floor(bit/2.0)*2.0;
        V3 temp=partial*2.0+bit+V3(d,d,d)+previous*(inv+1.0-d);
        partial=temp-floor(temp/8.0)*8.0;
        previous=floor(temp/16.0);previous=previous-floor(previous/2.0)*2.0;
        result+=previous*weight;weight*=0.5;
    }
    return result;
}
V2 frame_uv(V2 p,V4 info){
    p=clamp(floor(p),V2(0.0,0.0),info.xy-V2(1.0,1.0));
#if !D3D_ROWS
    p.y=info.y-1.0-p.y;
#endif
    return (p+V2(0.5,0.5))/info.zw;
}
V4 frame(sampler2D t,V2 p,V4 info){return octets(SAMPLE(t,frame_uv(p,info)));}
// 32-bit RDP fixed-point operations carried as two exact 16-bit float limbs.
// Products use base-256 carries; converting a signed coefficient to one float
// would discard the low bits before GLRDP's Z18.13 span correction.
V2 word_add(V2 a,V2 b){float lo=a.x+b.x;return V2(rem(lo,65536.0),rem(a.y+b.y+floor(lo/65536.0),65536.0));}
V2 word_neg(V2 a){return word_add(V2(65535.0-a.x,65535.0-a.y),V2(1.0,0.0));}
V2 word_scalar(float a){return V2(rem(a,65536.0),rem(floor(a/65536.0),65536.0));}
V2 word_mul(V2 a,V2 b){
    V4 x=V4(rem(a.x,256.0),floor(a.x/256.0),rem(a.y,256.0),floor(a.y/256.0));
    V4 y=V4(rem(b.x,256.0),floor(b.x/256.0),rem(b.y,256.0),floor(b.y/256.0));
    float r0=x.x*y.x,r1=x.x*y.y+x.y*y.x+floor(r0/256.0);
    float r2=x.x*y.z+x.y*y.y+floor(r1/256.0),carry=floor(r2/256.0);
    r2=rem(r2,256.0)+x.z*y.x;carry+=floor(r2/256.0);
    float r3=rem(x.x*y.w,256.0)+rem(x.y*y.z,256.0)+rem(x.z*y.y,256.0)+rem(x.w*y.x,256.0)+carry;
    return V2(rem(r0,256.0)+rem(r1,256.0)*256.0,rem(r2,256.0)+rem(r3,256.0)*256.0);
}
// Signed pixel/subpixel multipliers fit sixteen bits. Splitting the multiplier
// into bytes keeps each product below 2^24 and avoids the full 32x32 path.
V2 word_mul_small(V2 a,float b){
    bool negative=b<0.0;b=abs(b);float lo=rem(b,256.0),hi=floor(b/256.0);
    float p0=a.x*lo,p1=a.x*hi,v=rem(p0,65536.0)+rem(p1,256.0)*256.0;
    V2 result=V2(rem(v,65536.0),rem(rem(a.y*lo,65536.0)+rem(a.y*hi,256.0)*256.0+floor(p0/65536.0)+floor(p1/256.0)+floor(v/65536.0),65536.0));
    return negative?word_neg(result):result;
}
V2 word_shr(V2 a,float bits,bool arithmetic){
    if(bits>=16.0){float low=floor(a.y/exp2(bits-16.0));if(arithmetic&&a.y>=32768.0)low+=65536.0-exp2(32.0-bits);return V2(low,arithmetic&&a.y>=32768.0?65535.0:0.0);}
    float power=exp2(bits),high=floor(a.y/power);
    if(arithmetic&&a.y>=32768.0)high+=65536.0-65536.0/power;
    return V2(floor(a.x/power)+rem(a.y,power)*(65536.0/power),high);
}
bool word_less(V2 a,V2 b){float ah=a.y>=32768.0?a.y-65536.0:a.y,bh=b.y>=32768.0?b.y-65536.0:b.y;return ah<bh||(ah==bh&&a.x<b.x);}
V2 edge_word(float which,float y){
    V4 e=which<0.5?c[106]:(which<1.5?c[107]:c[108]);
    float origin=which>1.5?c[3].y:floor(c[3].x/4.0)*4.0;
    return word_add(e.xy,word_mul_small(e.zw,y-origin));
}
V2 fixed_span(V4 a,V4 b,V2 pixel,bool mask_dx){
    bool offset=(c[5].x<0.0)==(c[3].w>0.5);V2 major=edge_word(0.0,pixel.y*4.0+(offset?3.0:0.0));
    float origin=rem(major.y,4096.0);if(origin>=2048.0)origin-=4096.0;
    float xfrac=floor(major.x/256.0);
    V2 row=word_add(a.xy,word_mul_small(b.xy,pixel.y-floor(c[3].x/4.0)));
    V2 de=V2(floor(b.x/512.0)*512.0,b.y),dy=V2(floor(b.z/512.0)*512.0,b.w);
    V2 correction=offset?word_add(word_add(de,word_neg(word_shr(de,2.0,true))),word_add(word_neg(dy),word_shr(dy,2.0,true))):V2(0.0,0.0);
    V2 dx8=word_shr(a.zw,8.0,true);dx8.x=floor(dx8.x/2.0)*2.0;
    V2 value=word_add(word_add(V2(floor(row.x/512.0)*512.0,row.y),correction),word_neg(word_mul_small(dx8,xfrac)));
    value.x=floor(value.x/1024.0)*1024.0;V2 dx=a.zw;if(mask_dx)dx.x=floor(dx.x/32.0)*32.0;
    return word_add(value,word_mul_small(dx,pixel.x-origin));
}
float correct_shade(V2 value,V2 raw_dx,V2 raw_dy,V2 first,float cvg){
    float result=value.y;
    if(cvg<7.5){
        float v=floor(value.x/16384.0)+rem(value.y,512.0)*4.0;
        float dx=floor(raw_dx.x/16384.0)+rem(raw_dx.y,2048.0)*4.0,dy=floor(raw_dy.x/16384.0)+rem(raw_dy.y,2048.0)*4.0;
        if(dx>=4096.0)dx-=8192.0;if(dy>=4096.0)dy-=8192.0;
        result=floor((v*4.0+first.x*dx+first.y*dy)/16.0);
    }
    result=rem(result,512.0);return result>=384.0?0.0:min(result,255.0);
}
float float_attribute(V4 a,V2 p){float row=floor(p.y)-c[6].y,major=c[4].x+row*c[5].x*4.0;return a.x+row*a.z+(p.x-major)*a.y+rem(p.y,1.0)*a.w;}
V4 shade_values(V2 position_value,V2 anchor,float cvg){
    if(c[41].x<0.5)return V4(0.0,0.0,0.0,0.0);
    if(c[29].z>1.0)return clamp(floor(V4(float_attribute(c[7],position_value),float_attribute(c[8],position_value),float_attribute(c[9],position_value),float_attribute(c[10],position_value))+V4(0.5,0.5,0.5,0.5)),0.0,255.0);
    if(c[41].w>0.5){
        V2 pixel=floor(position_value);V2 first=floor((anchor-pixel)*4.0+V2(0.5,0.5));
        V2 r=fixed_span(c[80],c[81],pixel,true),g=fixed_span(c[82],c[83],pixel,true),b=fixed_span(c[84],c[85],pixel,true),a=fixed_span(c[86],c[87],pixel,true);
        return V4(correct_shade(r,c[80].zw,c[81].zw,first,cvg),correct_shade(g,c[82].zw,c[83].zw,first,cvg),correct_shade(b,c[84].zw,c[85].zw,first,cvg),correct_shade(a,c[86].zw,c[87].zw,first,cvg));
    }
    return V4(0.0,0.0,0.0,0.0);
}
float tc_axis(float coordinate,float reciprocal,float shift,bool carry){
    V2 product=word_mul_small(word_scalar(coordinate),reciprocal);float k=29.0-shift;
    float segment=k>=16.0?floor(product.y/exp2(k-16.0)):floor(product.x/32768.0)+product.y*2.0;
    segment=rem(segment,exp2(30.0-k));V2 value=shift==14.0?word_mul(product,V2(2.0,0.0)):word_shr(product,13.0-shift,true);
    if(carry)return 32767.0;
    if(segment!=0.0&&segment!=exp2(30.0-k)-1.0){float signbit=rem(floor((shift==14.0?product.y:value.y)/8192.0),2.0);return signbit<0.5?32767.0:-32768.0;}
    float bits=rem(value.y,2.0)*65536.0+floor(value.x/32768.0)*32768.0;
    if(bits==32768.0)return 32767.0;if(bits==65536.0)return -32768.0;return value.x>=32768.0?value.x-65536.0:value.x;
}
V3 fixed_texture(V2 p){
    V2 a=fixed_span(c[96],c[97],floor(p),true),b=fixed_span(c[98],c[99],floor(p),true),d=fixed_span(c[100],c[101],floor(p),true);
    V3 raw=V3(a.y>=32768.0?a.y-65536.0:a.y,b.y>=32768.0?b.y-65536.0:b.y,d.y>=32768.0?d.y-65536.0:d.y);
    if(c[1].z<0.5)return V3(raw.xy/32.0,1.0);
    float w=rem(raw.z,32768.0),leading=0.0,v=w;
    if(v>=256.0){v=floor(v/256.0);leading+=8.0;}if(v>=16.0){v=floor(v/16.0);leading+=4.0;}if(v>=4.0){v=floor(v/4.0);leading+=2.0;}if(v>=2.0)leading+=1.0;
    float shift=14.0-leading,norm=rem(w*exp2(shift),16384.0);V4 rom=frame(reciprocal_tex,V2(floor(norm/256.0),0.0),c[102]);
    float slope=rem(rom.b+rom.a*256.0,1024.0)-1023.0;
    float reciprocal=rem(floor(slope*rem(norm,256.0)*4.0/1024.0)+rom.r+rom.g*256.0,32768.0);
    return V3(tc_axis(raw.x,reciprocal,shift,raw.z<=0.0)/32.0,tc_axis(raw.y,reciprocal,shift,raw.z<=0.0)/32.0,raw.z<=0.0?-1.0:1.0);
}
float axis(float p,float size,float mask,float flags){
    if(rem(flags,2.0)>0.5||mask<0.5)p=clamp(p,0.0,size-1.0);
    if(mask>0.5){float period=exp2(mask);if(flags>=2.0&&rem(floor(p/period),2.0)>0.5)p=-1.0-p;p=rem(p,period);}
    return clamp(p,0.0,size-1.0);
}
V4 texel(sampler2D t,V2 p,V4 info,V4 wrap){
    p=V2(axis(p.x,info.x,wrap.x,wrap.z),axis(p.y,info.y,wrap.y,wrap.w));
    return frame(t,p,info);
}
V4 sample_tile(sampler2D t,V2 st,V4 info,V4 transform,V4 wrap){
    st=st*transform.xy-transform.zw;
    V2 base=floor(st),f=floor((st-base)*32.0)/32.0;
    V4 a=texel(t,base,info,wrap);
    if(c[28].z<1.5||c[1].x>=2.0)return a;
    V4 b=texel(t,base+V2(1.0,0.0),info,wrap),d=texel(t,base+V2(0.0,1.0),info,wrap);
    if(f.x+f.y<=1.0)return floor(a+f.x*(b-a)+f.y*(d-a)+V4(0.5,0.5,0.5,0.5));
    V4 e=texel(t,base+V2(1.0,1.0),info,wrap);
    return floor(e+(1.0-f.x)*(d-e)+(1.0-f.y)*(b-e)+V4(0.5,0.5,0.5,0.5));
}
V2 word_xor(V2 a,V2 b){
    V2 result=V2(0.0,0.0);float weight=1.0;
    LOOP for(int i=0;i<16;i++){V2 aa=floor(a*0.5),bb=floor(b*0.5);result+=abs((a-aa*2.0)-(b-bb*2.0))*weight;a=aa;b=bb;weight*=2.0;}
    return result;
}
V4 noise_pixel(V2 p){
    V2 value=word_xor(word_mul(word_scalar(floor(p.x)),V2(15285.0,7954.0)),word_mul(word_scalar(floor(p.y)),V2(25749.0,24373.0)));
    value=word_xor(value,V2(31161.0,40503.0));value=word_xor(value,word_shr(value,16.0,false));value=word_mul(value,V2(13613.0,32747.0));
    value=word_xor(value,word_shr(value,15.0,false));value=word_mul(value,V2(42635.0,33900.0));value=word_xor(value,word_shr(value,16.0,false));
    return V4(rem(value.x,256.0)/255.0,0.0,0.0,1.0);
}
float hash_noise(V2 p){
#if !VI_PASS && !VI_RECONSTRUCT_PASS
    p.y=c[38].y-1.0-floor(p.y);
#endif
    return frame(noise_tex,p,c[104]).r;
}
float matrix_value(V2 p,bool bayer){
    float x=rem(p.x,4.0),y=rem(p.y,4.0);
    if(bayer){
        if(y<0.5)return x<0.5?0.0:(x<1.5?4.0:(x<2.5?1.0:5.0));
        if(y<1.5)return x<0.5?4.0:(x<1.5?0.0:(x<2.5?5.0:1.0));
        if(y<2.5)return x<0.5?3.0:(x<1.5?7.0:(x<2.5?2.0:6.0));
        return x<0.5?7.0:(x<1.5?3.0:(x<2.5?6.0:2.0));
    }
    if(y<0.5)return x<0.5?0.0:(x<1.5?6.0:(x<2.5?1.0:7.0));
    if(y<1.5)return x<0.5?4.0:(x<1.5?2.0:(x<2.5?5.0:3.0));
    if(y<2.5)return x<0.5?3.0:(x<1.5?5.0:(x<2.5?2.0:4.0));
    return x<0.5?7.0:(x<1.5?1.0:(x<2.5?6.0:0.0));
}
// Eight RDP samples, with the same staggered rows and no-AA write enable
// as GLRDP. Geometry submission is conservative; this is the rasterizer.
float sample_coverage(V2 p,out V2 anchor,out bool first){
    V2 ip=floor(p);anchor=ip;first=false;
    if(c[105].z>c[105].x&&c[105].w>c[105].y&&(ip.x<c[105].x||ip.y<c[105].y||ip.x>=c[105].z||ip.y>=c[105].w))return 0.0;
    float count=0.0;bool found=false;
    if(c[1].y<0.5&&ip.x>=c[2].x&&ip.x+1.0<=c[2].z&&ip.y>=c[2].y&&ip.y+1.0<=c[2].w){first=true;return 8.0;}
    V2 major_word=V2(0.0,0.0),minor_word=major_word;
    if(c[1].y>0.5&&c[41].w>0.5){
        float y=ip.y*4.0;major_word=edge_word(0.0,y);minor_word=edge_word(y<c[3].y?1.0:2.0,y);
        if(y>=c[3].x&&y+3.0<c[3].z&&(c[3].y<=y||c[3].y>y+3.0)&&abs(c[5].x)<1024.0&&abs(y<c[3].y?c[5].y:c[5].z)<1024.0){
            V2 ms=c[106].zw,ns=y<c[3].y?c[107].zw:c[108].zw;
            V2 last_major=word_add(major_word,word_add(word_add(ms,ms),ms)),last_minor=word_add(minor_word,word_add(word_add(ns,ns),ns));
            V2 left=c[3].w>0.5?major_word:minor_word,right=c[3].w>0.5?minor_word:major_word,last_left=c[3].w>0.5?last_major:last_minor,last_right=c[3].w>0.5?last_minor:last_major;
            V2 px=V2(0.0,rem(ip.x,65536.0)),last_px=V2(49152.0,px.y);
            if(!word_less(px,left)&&!word_less(px,last_left)&&word_less(last_px,right)&&word_less(last_px,last_right)){first=true;return 8.0;}
        }
    }
    LOOP for(int row=0;row<4;row++){
        float y=ip.y*4.0+float(row),left=0.0,right=0.0;
        if(c[1].y>0.5&&c[41].w>0.5&&y==c[3].y)minor_word=c[108].xy;
        bool active=c[1].y<0.5||(y>=c[3].x&&y<c[3].z);
        if(active){
            if(c[1].y>0.5&&c[41].w<0.5){
                float major=c[4].x+c[5].x*(y-floor(c[3].x/4.0)*4.0);
                float minor=y<c[3].y?c[4].y+c[5].y*(y-floor(c[3].x/4.0)*4.0):c[4].z+c[5].z*(y-c[3].y);
                left=c[3].w>0.5?major:minor;right=c[3].w>0.5?minor:major;
            }
            LOOP for(int col=0;col<2;col++){
                float q=rem(float(row),2.0)+float(col)*2.0,x=ip.x+q*0.25;
                bool inside=c[1].y>0.5?(x>=left&&x<right):(x>=c[2].x&&x<c[2].z&&y>=c[2].y*4.0&&y<c[2].w*4.0);
                if(c[1].y>0.5&&c[41].w>0.5){V2 test_x=V2(q*16384.0,rem(ip.x,65536.0)),lo=c[3].w>0.5?major_word:minor_word,hi=c[3].w>0.5?minor_word:major_word;inside=!word_less(test_x,lo)&&word_less(test_x,hi);}
                if(inside){count+=1.0;if(!found){anchor=ip+V2(q,float(row))*0.25;found=true;}if(row==0&&col==0)first=true;}
            }
        }
        if(c[1].y>0.5&&c[41].w>0.5){major_word=word_add(major_word,c[106].zw);minor_word=word_add(minor_word,y<c[3].y?c[107].zw:c[108].zw);}
    }
    return count;
}
float coverage(V2 p,out V2 anchor){
    bool first;float count=sample_coverage(p,anchor,first);
    if(count<0.5||(c[26].x<0.5&&c[1].x<2.0&&!first))discard;
    return count;
}
float span_value(V4 a,V2 p){
    if(c[1].y<0.5)return a.x+(p.x-c[6].x)*a.y+(p.y-c[6].y)*a.w;
    float row=floor(p.y)-c[6].y,major=c[4].x+row*c[5].x*4.0;
    return a.x+row*a.z+(p.x-major)*a.y+rem(p.y,1.0)*a.w;
}
V4 mip_region(float level){
    if(level<0.5)return c[48];
    if(level<1.5)return c[49];
    if(level<2.5)return c[50];
    if(level<3.5)return c[51];
    if(level<4.5)return c[52];
    if(level<5.5)return c[53];
    if(level<6.5)return c[54];
    return c[55];
}
V4 mip_transform(float level){
    if(level<0.5)return c[56];
    if(level<1.5)return c[57];
    if(level<2.5)return c[58];
    if(level<3.5)return c[59];
    if(level<4.5)return c[60];
    if(level<5.5)return c[61];
    if(level<6.5)return c[62];
    return c[63];
}
V4 mip_wrap(float level){
    if(level<0.5)return c[64];
    if(level<1.5)return c[65];
    if(level<2.5)return c[66];
    if(level<3.5)return c[67];
    if(level<4.5)return c[68];
    if(level<5.5)return c[69];
    if(level<6.5)return c[70];
    return c[71];
}
V4 mip_storage(float level){
    if(level<0.5)return c[72];
    if(level<1.5)return c[73];
    if(level<2.5)return c[74];
    if(level<3.5)return c[75];
    if(level<4.5)return c[76];
    if(level<5.5)return c[77];
    if(level<6.5)return c[78];
    return c[79];
}
#if D3D_ROWS
static float copy_lane=0.0;
#else
float copy_lane=0.0;
#endif
V4 atlas_fetch(V2 p,V4 region,V4 wrap,V4 storage){
    if(storage.w<0.0){
        float sx=wrap.x>0.5?axis(p.x,65536.0,wrap.x,rem(wrap.z,4.0)):p.x;
        float sy=wrap.y>0.5?axis(p.y,65536.0,wrap.y,rem(wrap.w,4.0)):p.y;
        float index=rem(sx+sy*(-storage.w-1.0),2048.0);
        if(rem(sy,2.0)>0.5)index=floor(index/4.0)*4.0+rem(index+2.0,4.0);
        p=V2(rem(index,512.0),floor(index/512.0));
    }else{
        p=V2(axis(p.x,storage.x,wrap.x,rem(wrap.z,4.0)),axis(p.y,storage.y,wrap.y,rem(wrap.w,4.0)));
        if(storage.z>0.5){
            float index=p.x+p.y*storage.w,bit=exp2(storage.z-1.0);
            if(rem(p.y,2.0)>0.5)index=floor(index/(bit*2.0))*(bit*2.0)+rem(index+bit,bit*2.0);
            index=rem(index,region.z*region.w);p=V2(rem(index,512.0),floor(index/512.0));
        }
    }
    p=V2(rem(p.x,region.z),rem(p.y,region.w));
    return frame(tile0,p+region.xy,c[91]);
}
V4 atlas_sample(float level,V2 st,float texture_cycle){
    V4 region=mip_region(level),transform=mip_transform(level),wrap=mip_wrap(level),storage=mip_storage(level);
    V2 p=st*transform.xy-transform.zw;
    if(c[1].x==2.0)p.x=floor(p.x)+copy_lane;
    V2 base=floor(p),f=floor((p-base)*32.0)/32.0;
    V4 a=atlas_fetch(base,region,wrap,storage);
    if(c[28].z<2.0||c[1].x==2.0)return a;
    bool yuv=rem(floor(c[90].z/exp2(level)),2.0)>0.5;
    bool yuv_palette=rem(floor(c[90].w/exp2(level)),2.0)>0.5;
    if(yuv||yuv_palette){
        V4 b=atlas_fetch(base+V2(2.0,0.0),region,wrap,storage),d=atlas_fetch(base+V2(0.0,1.0),region,wrap,storage),e=atlas_fetch(base+V2(2.0,1.0),region,wrap,storage);
        V2 rg=V2(floor(f.x*16.0)/32.0+rem(base.x,2.0)*0.5,f.y);
        float convert=texture_cycle<0.5?c[94].x:c[94].y;
        if(yuv&&convert<0.5)return V4(rg.x+rg.y>=1.0?e.rg:a.rg,f.x+f.y>=1.0?e.ba:a.ba);
        V4 v=f.x+f.y<1.0?a+(b-a)*f.x+(d-a)*f.y:e+(d-e)*(1.0-f.x)+(b-e)*(1.0-f.y);
        v.rg=rg.x+rg.y<1.0?a.rg+(b.rg-a.rg)*rg.x+(d.rg-a.rg)*rg.y:e.rg+(d.rg-e.rg)*(1.0-rg.x)+(b.rg-e.rg)*(1.0-rg.y);
        if(c[28].z==3.0){if(f.x==0.5&&f.y==0.5)v.ba=(a.ba+b.ba+d.ba+e.ba)*0.25;if(rg.x==0.5&&rg.y==0.5)v.rg=(a.rg+b.rg+d.rg+e.rg)*0.25;}
        return floor(v+V4(0.5,0.5,0.5,0.5));
    }
    V4 b=atlas_fetch(base+V2(1.0,0.0),region,wrap,storage),d=atlas_fetch(base+V2(0.0,1.0),region,wrap,storage);
    V4 v;
    if(c[28].z==3.0&&f.x==0.5&&f.y==0.5)v=(a+b+d+atlas_fetch(base+V2(1.0,1.0),region,wrap,storage))*0.25;
    else if(f.x+f.y<1.0)v=a+(b-a)*f.x+(d-a)*f.y;
    else{V4 e=atlas_fetch(base+V2(1.0,1.0),region,wrap,storage);v=e+(d-e)*(1.0-f.x)+(b-e)*(1.0-f.y);}
    return floor(v+V4(0.5,0.5,0.5,0.5));
}
V3 texture_q(V2 p){
    if(c[1].y>0.5&&c[41].w>0.5&&c[29].z==1.0)return fixed_texture(p);
    V3 q=V3(span_value(c[11],p),span_value(c[12],p),span_value(c[13],p));
    if(c[1].x==2.0)q-=V3(c[11].y,c[12].y,c[13].y)*copy_lane;
    if(c[1].z<0.5)q.z=1.0;return q;
}
V2 texture_st(V3 q){return c[1].y>0.5&&c[41].w>0.5&&c[29].z==1.0?q.xy:q.xy/max(q.z,0.00000001);}
void texture_lod(V3 q,V3 qx,V3 qy,out float first,out float second,out float lod_fraction){
    V2 st=texture_st(q),sx=texture_st(qx),sy=texture_st(qy);
    V2 here=floor(st*32.0),dx=floor(sx*32.0)-here,dy=floor(sy*32.0)-here;
    dx=V2(dx.x<0.0?-dx.x-1.0:dx.x,dx.y<0.0?-dx.y-1.0:dx.y);
    dy=V2(dy.x<0.0?-dy.x-1.0:dy.x,dy.y<0.0?-dy.y-1.0:dy.y);
    float lod=max(max(dx.x,dx.y),max(dy.x,dy.y)),level=0.0;
    bool magnify=false,distant=false;
    if(lod>=16384.0||q.z<=0.0||qx.z<=0.0||qy.z<=0.0){distant=true;lod_fraction=255.0;}
    else if(lod<32.0||lod<c[89].w){
        magnify=true;distant=c[88].y==0.0;
        if(c[89].y<0.5&&c[89].z<0.5)lod_fraction=distant?255.0:0.0;
        else{lod_fraction=max(lod,c[89].w)*8.0;if(c[89].z>0.5)lod_fraction+=256.0;}
    }else{
        float input_value=rem(floor(lod/32.0),256.0);level=max(floor(log2(max(input_value,1.0))),0.0);
        distant=c[88].y==0.0||lod>=8192.0||level>=c[88].y;
        lod_fraction=c[89].y<0.5&&c[89].z<0.5&&distant?255.0:rem(floor(lod*8.0/exp2(level)),256.0);
    }
    if(distant)level=c[88].y;
    if(c[89].y<0.5){first=level;second=distant||(c[89].z<0.5&&magnify)?first:rem(first+1.0,8.0);}
    else{first=rem(level+(magnify?0.0:1.0),8.0);second=rem(level+(!distant&&!magnify?2.0:1.0),8.0);}
}
V4 yuv_convert(V4 value){
    value.rg-=V2(128.0,128.0);
    V3 rgb=value.b+V3(floor((c[93].x*value.g+128.0)/256.0),floor((c[93].y*value.r+c[93].z*value.g+128.0)/256.0),floor((c[93].w*value.r+128.0)/256.0));
    rgb=rgb-floor(rgb/512.0)*512.0;
    return V4(rgb.r>=384.0?rgb.r-512.0:rgb.r,rgb.g>=384.0?rgb.g-512.0:rgb.g,rgb.b>=384.0?rgb.b-512.0:rgb.b,value.b);
}
V4 source(float mux,V4 combined,V4 t0,V4 t1,V4 shade){
    if(mux<0.5)return combined;
    if(mux<1.5)return t0;
    if(mux<2.5)return t1;
    if(mux<3.5)return c[15];
    if(mux<4.5)return shade;
    if(mux<5.5)return c[16];
    if(mux<6.5)return V4(255.0,255.0,255.0,255.0);
    return V4(0.0,0.0,0.0,0.0);
}
V3 rgb_c(float mux,V4 combined,V4 t0,V4 t1,V4 shade,float lod_fraction){
    if(mux<5.5)return source(mux,combined,t0,t1,shade).rgb;
    if(mux<6.5)return c[43].rgb;
    float a=0.0;
    if(mux<7.5)a=combined.a;else if(mux<8.5)a=t0.a;else if(mux<9.5)a=t1.a;
    else if(mux<10.5)a=c[15].a;else if(mux<11.5)a=shade.a;else if(mux<12.5)a=c[16].a;
    else if(mux<13.5)a=lod_fraction;else if(mux<14.5)a=c[29].y;else if(mux<15.5)a=c[44].y;
    return V3(a,a,a);
}
float alpha_c(float mux,V4 combined,V4 t0,V4 t1,V4 shade,float lod_fraction){
    if(mux<0.5)return lod_fraction;
    if(mux<5.5)return source(mux,combined,t0,t1,shade).a;
    return mux<6.5?c[29].y:0.0;
}
V4 combine_cycle(V4 rgb,V4 am,V4 combined,V4 t0,V4 t1,V4 shade,V2 p,float lod_fraction){
    V3 a=source(rgb.x,combined,t0,t1,shade).rgb;
    if(rgb.x>6.5&&rgb.x<7.5)a=V3(hash_noise(p),hash_noise(p),hash_noise(p));
    V3 b=source(rgb.y,combined,t0,t1,shade).rgb;
    if(rgb.y>5.5&&rgb.y<6.5)b=c[42].rgb;
    else if(rgb.y>6.5&&rgb.y<7.5)b=V3(c[44].x,c[44].x,c[44].x);
    V3 cc=rgb_c(rgb.z,combined,t0,t1,shade,lod_fraction),d=source(rgb.w,combined,t0,t1,shade).rgb;
    float aa=source(am.x,combined,t0,t1,shade).a,ab=source(am.y,combined,t0,t1,shade).a;
    float ac=alpha_c(am.z,combined,t0,t1,shade,lod_fraction),ad=source(am.w,combined,t0,t1,shade).a;
    return V4(combine9(a.r,b.r,cc.r,d.r),combine9(a.g,b.g,cc.g,d.g),combine9(a.b,b.b,cc.b,d.b),
              clamp(floor(((aa-ab)*s9(ac)+ad*256.0+128.0)/256.0),0.0,255.0));
}
void texture_pair(V2 p,out V4 t0,out V4 t1,out float lod_fraction){
    V2 tp=c[1].y>0.5&&c[29].z>1.0?p:floor(p);copy_lane=c[1].x==2.0?rem(tp.x-c[6].x,4.0):0.0;
    V3 q=texture_q(tp);V2 st=texture_st(q);
    t0=V4(255.0,255.0,255.0,255.0);t1=t0;
    lod_fraction=0.0;
    if(c[1].w>0.5){
        if(c[88].x>0.5){
            float first=0.0,second=c[1].x==1.0?1.0:0.0;
            if(c[1].x<2.0&&(c[89].x>0.5||c[90].x>0.5)){
                float lf,ls;
#if SPLIT_LOD && !LOD_PASS
                V4 metadata=frame(cached_lod,(p+V2(1.0,0.0))*max(c[29].z,1.0),c[109]);lf=metadata.r;ls=metadata.g;lod_fraction=metadata.b+metadata.a*256.0;
#else
                texture_lod(q,texture_q(tp+V2(1.0,0.0)),texture_q(tp+V2(0.0,1.0)),lf,ls,lod_fraction);
#endif
                if(c[89].x>0.5){first=lf;second=c[1].x==0.0?first:ls;}
            }
#if SPLIT_TEXTURES && !TEXEL0_PASS && !TEXEL1_PASS
            t0=frame(cached0,(p+V2(1.0,0.0))*max(c[29].z,1.0),c[109]);t1=c[1].x>0.5?frame(cached1,(p+V2(1.0,0.0))*max(c[29].z,1.0),c[109]):t0;
#else
#if !TEXEL1_PASS
            t0=atlas_sample(first,st,c[1].x==0.0?1.0:0.0);
#endif
#if !TEXEL0_PASS
            t1=atlas_sample(second,st,1.0);
#endif
#endif
#if !TEXEL0_PASS && !TEXEL1_PASS
            bool y0=rem(floor(c[90].z/exp2(first)),2.0)>0.5,y1=rem(floor(c[90].z/exp2(second)),2.0)>0.5;
            if(y0){if((c[1].x==0.0?c[94].y:c[94].x)<0.5)t0=yuv_convert(t0);else t0.rg-=V2(128.0,128.0);}
            if(y1){if(c[94].y<0.5)t1=yuv_convert(t1);else t1.rg-=V2(128.0,128.0);}
#endif
        }else{
#if SPLIT_TEXTURES && !TEXEL0_PASS && !TEXEL1_PASS
            t0=frame(cached0,(p+V2(1.0,0.0))*max(c[29].z,1.0),c[109]);t1=c[1].x>0.5?frame(cached1,(p+V2(1.0,0.0))*max(c[29].z,1.0),c[109]):t0;
#else
#if !TEXEL1_PASS
            t0=sample_tile(tile0,st,c[32],c[34],c[36]);
#endif
#if !TEXEL0_PASS
            t1=sample_tile(tile1,st,c[33],c[35],c[37]);
#endif
#endif
        }
    }
}
V4 shade_pixel(V2 p,V2 anchor,float geometric,bool first_only){
    if(c[1].x>2.5){
        float index=floor(p.x);
        if(c[29].w==1.0){float lane=rem(index,4.0),raw=lane<0.5?c[30].r:(lane<1.5?c[30].g:(lane<2.5?c[30].b:c[30].a));return V4(raw,raw,raw,255.0);}
        return rem(index,2.0)<0.5?c[30]:c[31];
    }
    V4 shade=V4(0.0,0.0,0.0,0.0);
    if(c[41].w>0.5)shade=shade_values(p,anchor,geometric);
    else if(c[41].x>0.5)shade=clamp(floor(V4(span_value(c[7],anchor),span_value(c[8],anchor),span_value(c[9],anchor),span_value(c[10],anchor))),0.0,255.0);
    V4 t0,t1;float lod_fraction;texture_pair(p,t0,t1,lod_fraction);
    if(c[1].x>1.5)return t0;
    V4 combined=V4(0.0,0.0,0.0,0.0);
    if(c[1].x>0.5){combined=combine_cycle(c[19],c[20],combined,t0,t1,shade,p,lod_fraction);if(first_only)return combined;}
    return combine_cycle(c[21],c[22],combined,c[1].x>0.5?t1:t0,t0,shade,p,lod_fraction);
}
float zvalue(V2 position_value,V2 anchor,float cvg){
    if(c[25].w>0.5)return c[6].z;
    if(c[41].y<0.5)return 0.0;
    V2 pixel=floor(position_value),value;
    if(c[41].w>0.5)value=fixed_span(c[46],c[47],pixel,false);
    else{
        float major=c[4].x+c[5].x*(pixel.y*4.0-floor(c[3].x/4.0)*4.0);float origin=floor(major);
        value=word_add(c[46].xy,word_mul(c[46].zw,word_scalar(pixel.x-origin)));
    }
    V2 bits=word_shr(value,10.0,false);float raw=bits.x+bits.y*65536.0;
    V2 first=floor((anchor-pixel)*4.0+V2(0.5,0.5));
    V2 result=word_add(word_mul(word_scalar(raw),V2(4.0,0.0)),word_add(word_mul(word_scalar(first.x),word_shr(c[46].zw,10.0,true)),word_mul(word_scalar(first.y),word_shr(c[47].zw,10.0,true))));
    result=word_shr(result,5.0,true);
    // Full coverage uses the pixel origin, not the first covered sub-sample.
    if(cvg>7.5)result=word_shr(value,13.0,false);
    float z=result.x+rem(result.y,8.0)*65536.0,overflow=floor(z/131072.0);
    return overflow==2.0?262143.0:(overflow==3.0?0.0:rem(z,262144.0));
}
float zprecision(float z){
    float top=floor(z/2048.0);
    if(top<64.0)return 0.0;if(top<96.0)return 1.0;if(top<112.0)return 2.0;
    if(top<120.0)return 3.0;if(top<124.0)return 4.0;if(top<126.0)return 5.0;
    return top<127.0?6.0:7.0;
}
V4 pack_z(float z,float dz,float flags){return V4(rem(z,256.0),rem(floor(z/256.0),256.0),floor(z/65536.0)+dz*4.0,flags);}
// GLRDP Z modes and precision-dependent DZ tolerance, using RGBA8 byte limbs.
float memory_coverage(V4 value){
    if(c[26].z<0.5||c[29].w<2.0)return 7.0;
    if(c[29].w>2.5)return floor(value.a/32.0);
    if(c[38].w>0.5)return rem(floor(value.g/8.0),4.0)*2.0+floor(value.b/128.0);
    return clamp(floor(value.a*8.0/255.0+0.5)-1.0,0.0,7.0);
}
V4 depth_result(V2 p,V2 anchor,float incoming,V4 memory,float geometric){
    V4 old=frame(old_depth,floor(p)*max(c[29].z,1.0)+V2(0.5,0.5)*max(c[29].z,1.0),c[40]);float stored=old.r+old.g*256.0+rem(old.b,4.0)*65536.0;
    float exponent=rem(floor(old.b/4.0),16.0),dzmem=exp2(exponent),dz=c[6].w;
    float zprec=zprecision(stored);bool coplanar=false;
    if(zprec<3.0){if(dzmem!=32768.0)dzmem=max(dzmem*2.0,exp2(4.0-zprec));else{coplanar=true;dzmem=65535.0;}}
    float base=exp2(floor(log2(max(dz,dzmem)))),tol=base*8.0;
    float z=zvalue(p,anchor,geometric),mem=memory_coverage(memory);
    bool overflow=mem+incoming>=8.0,infront=z<stored,maxz=stored>=262143.0;
    bool farther=c[25].x<0.5||coplanar||z+tol>=stored,nearer=coplanar||z-tol<=stored,accept=true;
    float reduced=incoming;
    if(c[25].x>0.5){
        if(c[25].z<0.5)accept=maxz||(overflow?infront:nearer);
        else if(c[25].z<1.5){accept=maxz||(overflow?infront:nearer);
            if(infront&&farther&&overflow){accept=true;float e=floor(log2(max(base,1.0)));float q=rem(floor(stored/exp2(e))-floor(z/exp2(e)),16.0);reduced=min(floor(q*incoming/8.0),8.0);}}
        else if(c[25].z<2.5)accept=infront||maxz;
        else accept=farther&&nearer&&!maxz;
    }
    bool blend=c[26].y>0.5||(c[26].x>0.5&&farther&&!overflow);
    float cv=mem;
    if(c[27].x<0.5)cv=blend?min(mem+reduced,7.0):rem(reduced-1.0,8.0);
    else if(c[27].x<1.5)cv=rem(mem+reduced,8.0);
    else if(c[27].x<2.5)cv=7.0;
    float flags=cv+(farther?8.0:0.0)+(blend?16.0:0.0)+(accept?128.0:0.0);
    V4 value=c[25].y>0.5&&accept?pack_z(z,min(floor(log2(max(dz,1.0))),15.0),flags):old;
    value.b=rem(value.b,64.0)+floor(reduced/4.0)*64.0;value.a=flags+rem(reduced,4.0)*32.0;return value;
}
V3 blend_color(float mux,V4 pixel,V4 memory){
    if(mux<0.5)return pixel.rgb;if(mux<1.5)return memory.rgb;if(mux<2.5)return c[18].rgb;return c[17].rgb;
}
V4 blend_cycle(V4 mux,V4 pixel,V4 memory,float shade_alpha,bool final_cycle,float old_dz){
    V3 x=blend_color(mux.x,pixel,memory),y=blend_color(mux.z,pixel,memory);
    float a=mux.y<0.5?pixel.a:(mux.y<1.5?c[17].a:(mux.y<2.5?shade_alpha:0.0));
    if(final_cycle&&mux.y<0.5&&mux.w<0.5&&pixel.a>=255.0)return V4(x,pixel.a);
    float b=mux.w<0.5?255.0-a:(mux.w<1.5?memory.a:(mux.w<2.5?255.0:0.0));
    a=floor(a/8.0);b=floor(b/8.0);
    if(mux.w>0.5&&mux.w<1.5){
        float de=floor(log2(max(c[6].w,1.0))),ds=min(max(de-old_dz,0.0),4.0),dt=min(max(old_dz-de,0.0),4.0);
        a=floor(a/exp2(ds));b=floor(b/exp2(dt));a=floor(a/4.0)*4.0;b=floor(b/4.0)*4.0+3.0;
    }
    V3 n=x*a+y*(b+1.0),rgb;
    if(!final_cycle||c[26].y>0.5){rgb=floor(n/32.0);rgb=rgb-floor(rgb/256.0)*256.0;}
    else {float d=floor(a/4.0)+floor(b/4.0)+1.0;rgb=d==8.0&&a+b==31.0?floor(n/32.0):divide_rgb(n,d);}
    return V4(rgb,pixel.a);
}
#if D3D_ROWS
static V2 vi_center;
#else
V2 vi_center;
#endif
float vi_scale(){return max(c[29].z,1.0);}
float vi_aa(){return c[106].x;}
bool vi_aa_full(){return true;}
bool vi_restore(){return c[106].y>0.5;}
bool vi_divot(){return c[106].z>0.5;}
bool vi_resampling(){return c[106].w>0.5;}
bool vi_precomputed(){
#if VI_PASS
return true;
#else
return false;
#endif
}
V4 vi_fetch(V2 p){
    float scale=vi_scale();V2 np=floor(p/scale),sub=p-np*scale,sz=max(floor(c[91].xy/scale),V2(1.0,1.0));
    float address=c[109].y+np.y*c[109].x+np.x;
#if VI_RECONSTRUCT_PASS
    address=vi_center.y*sz.x+vi_center.x+(np.y-vi_center.y)*c[109].x+np.x-vi_center.x;
#endif
    address=clamp(address,0.0,sz.x*sz.y-1.0);
    V4 value=frame(memory_color,V2(rem(address,sz.x),floor(address/sz.x))*scale+sub,c[91]);
    if(!vi_precomputed()){
        if(c[107].y==2.0)value.rgb=floor(value.rgb/8.0)*8.0;
        else if(c[107].y==3.0)value.a=floor((floor(value.a/32.0)+1.0)*255.0/8.0+0.5);
    }
    return value/255.0;
}
V2 vi_make2(float v){return V2(v,v);}
V2 vi_make2(float x,float y){return V2(x,y);}
V2 vi_make2(V2 v){return v;}
V3 vi_make3(float v){return V3(v,v,v);}
V3 vi_make3(float x,float y,float z){return V3(x,y,z);}
V3 vi_make3(V3 v){return v;}
V3 vi_mod(V3 x,float d){return x-floor(x/d)*d;}
// GLRDP_SHARED_VI_FILTER
V4 vi_pixel(V2 p){
#if VI_RECONSTRUCT_PASS
    vi_center=floor(p/vi_scale());return vi_filter_tap(floor(p));
#else
    V2 scan=p*c[108].xy/V2(640.0,480.0);
    if(c[42].x>0.5){float row=floor(scan.y*2.0);if(c[42].z>0.5&&c[42].w<0.5&&rem(row,2.0)!=c[42].y)return V4(frame(tile0,p,c[92]).rgb/255.0,1.0);scan.y=floor(row*0.5)+0.5;}
    V2 start=V2(c[110].x-c[108].z,floor((c[110].z-c[108].w)*0.5)),local=scan-start;
    float aw=c[110].y-c[110].x,ah=floor((c[110].w-c[110].z)*0.5),lg=start.x<0.0?0.0:8.0,rg=start.x+aw>c[108].x?0.0:7.0;
    if(local.x<lg||local.x>=aw-rg||local.y<0.0||local.y>=ah)return V4(0.0,0.0,0.0,1.0);
    if(vi_scale()==1.0)local=floor(local);
    V2 st=(c[2].xy+local*c[2].zw+V2(0.5,0.5))*vi_scale()-V2(0.5,0.5);
    V3 rgb=vi_resample(st).rgb;
    if(c[107].w>0.5)rgb+=(hash_noise(V2(floor(p.x),479.0-floor(p.y)))/255.0-0.5)/255.0;
    if(c[107].z>0.5){V3 lo=sqrt(max(rgb,V3(0.0,0.0,0.0))),hi=pow(max(rgb,V3(0.0,0.0,0.0)),V3(1.0/2.2,1.0/2.2,1.0/2.2));rgb=lo+(hi-lo)*step(V3(0.25,0.25,0.25),rgb);}
    return V4(clamp(rgb,V3(0.0,0.0,0.0),V3(1.0,1.0,1.0)),1.0);
#endif
}
float channel(V4 v,float i){if(i<0.5)return v.r;if(i<1.5)return v.g;if(i<2.5)return v.b;return v.a;}
float raw_byte(float address){
    address=rem(address,4096.0);float texel=floor(address/4.0);
    return channel(frame(memory_color,V2(rem(texel,64.0),floor(texel/64.0)),c[91]),rem(address,4.0));
}
float bank_byte(float address,float row){
    if(rem(row,2.0)>0.5)address=floor(address/8.0)*8.0+rem(address+4.0,8.0);
    return raw_byte(address);
}
float expand5(float v){return v*8.0+floor(v/4.0);}
V4 rgba16(float value){return V4(expand5(floor(value/2048.0)),expand5(rem(floor(value/64.0),32.0)),expand5(rem(floor(value/2.0),32.0)),rem(value,2.0)*255.0);}
V4 ia16(float value){float i=floor(value/256.0);return V4(i,i,i,rem(value,256.0));}
V4 palette(float index){float address=2048.0+index*8.0,v=raw_byte(address)*256.0+raw_byte(address+1.0);return c[3].z>0.5?ia16(v):rgba16(v);}
V4 decode_tmem(float x,float y){
    float format=c[2].x,size=c[2].y,base=c[2].w*8.0+y*c[2].z*8.0;
    if(c[3].y>0.5&&format==1.0){float index=bank_byte(rem(base+x,2048.0),y);return palette(size==0.0?c[3].x*16.0+floor(index/16.0):index);}
    if(size==0.0){
        float a=base+floor(x/2.0);if(c[3].y>0.5)a=rem(a,2048.0);
        float packed_byte=bank_byte(a,y),v=rem(x,2.0)>0.5?rem(packed_byte,16.0):floor(packed_byte/16.0);
        if(c[3].y>0.5)return palette(c[3].x*16.0+v);
        if(format==0.0||format==2.0||format==4.0)return V4(v,v,v,v)*17.0;
        if(format==3.0){float i=floor((floor(v/2.0)*255.0+3.0)/7.0);return V4(i,i,i,rem(v,2.0)*255.0);}
    }else if(size==1.0){
        float a=base+x;if(c[3].y>0.5)a=rem(a,2048.0);float v=bank_byte(a,y);
        if(c[3].y>0.5)return palette(v);
        if(format==0.0||format==2.0||format==4.0)return V4(v,v,v,v);
        if(format==3.0){float i=floor(v/16.0)*17.0;return V4(i,i,i,rem(v,16.0)*17.0);}
    }else if(size==2.0){
        if(c[3].y>0.5)return palette(bank_byte(rem(base+x*2.0,2048.0),y));
        if(format==1.0){float pair=rem(base+floor(x/2.0)*2.0,2048.0),luma=bank_byte(rem(base+x,2048.0)+2048.0,y);return V4(bank_byte(pair,y),bank_byte(pair+1.0,y),luma,luma);}
        float v=bank_byte(base+x*2.0,y)*256.0+bank_byte(base+x*2.0+1.0,y);
        if(format==0.0)return rgba16(v);if(format==3.0)return ia16(v);
    }else{
        float lo=rem(base+x*2.0,2048.0);if(c[3].y>0.5)return palette(bank_byte(lo,y));
        return V4(bank_byte(lo,y),bank_byte(lo+1.0,y),bank_byte(lo+2048.0,y),bank_byte(lo+2049.0,y));
    }
    return V4(255.0,0.0,255.0,255.0);
}
float framebuffer_byte(float offset){
    float size=c[2].z,bpp=size==3.0?4.0:(size==2.0?2.0:1.0),index=floor(offset/bpp);
    V2 pixel=V2(rem(index,c[2].x),floor(index/c[2].x))*c[2].w+V2(0.5,0.5)*c[2].w;
    V4 rgba=frame(tile1,pixel,c[95]);
    if(size==3.0)return channel(rgba,rem(offset,4.0));
    if(size==2.0){float word=floor(rgba.r/8.0)*2048.0+floor(rgba.g/8.0)*64.0+floor(rgba.b/8.0)*2.0+(rgba.a>128.0?1.0:0.0);return rem(offset,2.0)<0.5?floor(word/256.0):rem(word,256.0);}
    return rem(offset+c[6].x,2.0)<0.5?rgba.r:rgba.g;
}
float load_byte(float index){
    V4 mapping=frame(tile0,V2(rem(index,64.0),floor(index/64.0)),c[92]);
    if(mapping.a>1.5)return raw_byte(index);
    if(mapping.a>0.5)return framebuffer_byte(mapping.r+mapping.g*256.0+mapping.b*65536.0);
    return mapping.r;
}
V4 tmem_pixel(V2 p){
#if TMEM_LOAD_PASS
    float index=(floor(p.y)*64.0+floor(p.x))*4.0;
    return V4(load_byte(index),load_byte(index+1.0),load_byte(index+2.0),load_byte(index+3.0))/255.0;
#else
    p=floor(p)-c[5].xy;float index=p.y*c[4].x+p.x;
    if(c[3].w>0.5){float a=c[2].w*8.0+index*2.0;return rgba16(raw_byte(a)*256.0+raw_byte(a+1.0))/255.0;}
    return (c[4].z>0.5?decode_tmem(index,0.0):decode_tmem(p.x,p.y))/255.0;
#endif
}
V4 raster_pixel(V2 p){
#if NOISE_PASS
    return noise_pixel(p);
#elif COPY_PASS
    return V4(frame(memory_color,p,c[0]).rgb/255.0,1.0);
#elif TMEM_LOAD_PASS || TMEM_DECODE_PASS
    return tmem_pixel(p);
#elif VI_PASS || VI_RECONSTRUCT_PASS
    return vi_pixel(p);
#elif LOD_PASS
    V2 tp=c[1].y>0.5&&c[29].z>1.0?p:floor(p);copy_lane=0.0;float lod_fraction=0.0;
    V3 q=texture_q(tp);float first,second;texture_lod(q,texture_q(tp+V2(1.0,0.0)),texture_q(tp+V2(0.0,1.0)),first,second,lod_fraction);
    return V4(first,second,rem(lod_fraction,256.0),floor(lod_fraction/256.0))/255.0;
#elif TEXEL0_PASS || TEXEL1_PASS
    V4 t0,t1;float lod_fraction;texture_pair(p,t0,t1,lod_fraction);return TEXEL0_PASS!=0?t0/255.0:t1/255.0;
#elif SHADE_PASS
    V2 anchor;float geometric=coverage(p,anchor);return shade_pixel(p,anchor,geometric,false)/255.0;
#elif ALPHA0_PASS
    V2 anchor;bool first;float geometric=sample_coverage(p,anchor,first);return V4(0.0,0.0,0.0,shade_pixel(p,anchor,geometric,true).a/255.0);
#else
    V2 anchor;float cvg=coverage(p,anchor),geometric=cvg;
    V4 pixel;
#if GLES2_PIPELINE
    pixel=frame(combined_color,p*max(c[29].z,1.0),c[0]);
#else
#if DEPTH_PASS
    if(c[1].x<2.0&&c[27].y<0.5&&c[27].w<0.5&&c[28].w<0.5)pixel=V4(0.0,0.0,0.0,255.0);
    else
#endif
#if SPLIT_SHADING
    if(c[1].x<2.0)pixel=frame(combined_color,p*max(c[29].z,1.0),c[0]);else
#endif
    pixel=shade_pixel(p,anchor,geometric,false);
#endif
    V4 memory=c[41].z>0.5?frame(memory_color,floor(p)*max(c[29].z,1.0)+V2(0.5,0.5)*max(c[29].z,1.0),c[0]):V4(0.0,0.0,0.0,255.0);
#if GLES2_PIPELINE && !COVERAGE_ALPHA_PASS
    V4 alpha_metadata=frame(coverage_alpha,p*max(c[29].z,1.0),c[0]);
    if(alpha_metadata.a<128.0)discard;
    cvg=alpha_metadata.r;pixel.a=alpha_metadata.g;
#else
    if(c[1].x<2.0){
        if(c[28].w>0.5){V3 distance=abs(pixel.rgb-c[42].rgb),range=max(c[43].rgb,V3(1.0,1.0,1.0));if(distance.r<=range.r&&distance.g<=range.g&&distance.b<=range.b)discard;}
        float alpha_value=pixel.a==255.0?256.0:pixel.a;
        float alpha_coverage=floor((alpha_value*cvg+4.0)/8.0);
        if(c[27].y>0.5)cvg=rem(floor(alpha_coverage/32.0),16.0);
        if(c[27].z>0.5)pixel.a=min(c[27].y>0.5?alpha_coverage:cvg*32.0,255.0);
        else{
            float dither_matrix=matrix_value(floor(p),c[28].x==1.0||c[28].x==3.0);
            float dither=c[28].y==3.0?0.0:(c[28].y==2.0?rem(hash_noise(floor(p)),8.0):(c[28].y==1.0?7.0-dither_matrix:dither_matrix));
            if(c[28].w<0.5)pixel.a=min(alpha_value+dither,255.0);
        }
        float compare_alpha=pixel.a;
        if(c[1].x==1.0&&(c[27].w==1.0||c[27].w==3.0)){
            V2 np=(c[29].z>1.0?p:floor(p))+V2(c[1].y>0.5?(c[3].w>0.5?1.0:-1.0):1.0,0.0),na;bool nf;
            float nc=sample_coverage(np,na,nf),next_alpha;
#if SPLIT_ALPHA
            next_alpha=frame(first_alpha,(np+V2(1.0,0.0))*max(c[29].z,1.0),c[109]).a;
#else
            next_alpha=shade_pixel(np,na,nc,true).a;
#endif
            float av=next_alpha==255.0?256.0:next_alpha;
            if(c[27].z>0.5)compare_alpha=min(c[27].y>0.5?floor((av*nc+4.0)/8.0):nc*32.0,255.0);
            else{float dm=matrix_value(floor(p),c[28].x==1.0||c[28].x==3.0),da=c[28].y==3.0?0.0:(c[28].y==2.0?rem(hash_noise(floor(p)),8.0):(c[28].y==1.0?7.0-dm:dm));compare_alpha=min(av+da,255.0);}
        }
        float alpha=c[27].w==3.0?hash_noise(floor(p)):c[18].a;
        if((c[27].w==1.0||c[27].w==3.0)&&compare_alpha<alpha)discard;
        if(cvg<0.5&&c[26].x>0.5)discard;
    }else if(c[1].x<3.0){if(c[29].w==2.0)pixel.a=rem(pixel.a,2.0)>0.5?255.0:0.0;if(c[27].w>0.0&&pixel.a<1.0)discard;}
#endif
#if COVERAGE_ALPHA_PASS
    float sa=c[41].w>0.5?shade_values(p,anchor,geometric).a:clamp(floor(span_value(c[10],anchor)),0.0,255.0);
    float dm=matrix_value(floor(p),c[28].x==1.0||c[28].x==3.0),da=c[28].y==3.0?0.0:(c[28].y==2.0?rem(hash_noise(floor(p)),8.0):(c[28].y==1.0?7.0-dm:dm));
    return V4(cvg,pixel.a,min(sa+da,255.0),255.0)/255.0;
#else
#if !DEPTH_PASS
    if(c[1].x>=2.0&&c[45].x<0.5){
        if(c[29].w==2.0)pixel.a=pixel.a>128.0?255.0:32.0;
        else if(c[1].x<3.0)pixel.a=c[29].w==3.0?rem(cvg-1.0,8.0)*32.0:floor((rem(cvg-1.0,8.0)+1.0)*255.0/8.0+0.5);
        return pixel/255.0;
    }
#endif
    if(c[29].w==2.0&&c[38].w<0.5)memory.rgb=floor(memory.rgb/8.0)*8.0;
    V4 depth=depth_result(p,anchor,cvg,memory,geometric);
#if DEPTH_PASS
    if(c[45].x>0.5)return pack_z(c[45].y,c[45].z,128.0+7.0)/255.0;
    return depth/255.0;
#else
    if(c[45].x>0.5)return pixel/255.0;
    V4 accepted=frame(new_depth,floor(p)*max(c[29].z,1.0)+V2(0.5,0.5)*max(c[29].z,1.0),c[40]);if(accepted.a<128.0)discard;
    float cv=rem(accepted.a,8.0);
#if GLES2_PIPELINE
    float shade_alpha=alpha_metadata.b;
#else
    float shade_alpha=c[41].w>0.5?shade_values(p,anchor,geometric).a:clamp(floor(span_value(c[10],anchor)),0.0,255.0);
    float amatrix=matrix_value(floor(p),c[28].x==1.0||c[28].x==3.0),adither=c[28].y==3.0?0.0:(c[28].y==2.0?rem(hash_noise(floor(p)),8.0):(c[28].y==1.0?7.0-amatrix:amatrix));shade_alpha=min(shade_alpha+adither,255.0);
#endif
    if(c[1].x<2.0){
        cvg=min(cvg,floor(accepted.b/64.0)*4.0+rem(floor(accepted.a/32.0),4.0));
        if(cvg<0.5&&c[26].x>0.5)discard;
        float mem=memory_coverage(memory);bool wrapped=mem+cvg>=8.0,blend=c[26].y>0.5||(c[26].x>0.5&&rem(floor(accepted.a/8.0),2.0)>0.5&&!wrapped);
        cv=c[27].x<0.5?(blend?min(mem+cvg,7.0):rem(cvg-1.0,8.0)):(c[27].x<1.5?rem(mem+cvg,8.0):(c[27].x<2.5?7.0:mem));
        if(c[29].w==2.0&&c[38].w>0.5){float intensity=floor(memory.r/8.0)*8.0+floor(memory.g/32.0);memory.rgb=V3(intensity,intensity,intensity);}
        memory.a=mem*32.0;
        float old_dz=c[25].x>0.5?rem(floor(frame(old_depth,floor(p)*max(c[29].z,1.0)+V2(0.5,0.5)*max(c[29].z,1.0),c[40]).b/4.0),16.0):15.0;
        if(c[1].x>0.5)pixel=blend_cycle(c[23],pixel,memory,shade_alpha,false,old_dz);
        V4 mux=c[24];
        if(c[26].w>0.5&&!wrapped)pixel.rgb=blend_color(mux.z,pixel,memory);
        else if(!blend)pixel.rgb=blend_color(mux.x,pixel,memory);
        else pixel=blend_cycle(mux,pixel,memory,shade_alpha,true,old_dz);
        if(c[28].x<3.0){
            float d=c[28].x==2.0?hash_noise(floor(p)):matrix_value(floor(p),c[28].x==1.0);V3 threshold=c[28].x==2.0?V3(rem(d,8.0),rem(floor(d/8.0),8.0),floor(d/64.0)):V3(d,d,d);
            V3 lo=pixel.rgb-floor(pixel.rgb/8.0)*8.0,up=min(floor(pixel.rgb/8.0)*8.0+V3(8.0,8.0,8.0),V3(255.0,255.0,255.0));
            pixel.r=lo.r>threshold.r?up.r:pixel.r;pixel.g=lo.g>threshold.g?up.g:pixel.g;pixel.b=lo.b>threshold.b?up.b:pixel.b;
        }
        if(c[29].w==2.0&&c[38].w>0.5){float r=pixel.r;pixel=V4(floor(r/8.0)*8.0,(rem(r,8.0)*4.0+floor(cv/2.0))*8.0,rem(cv,2.0)*128.0,32.0);}
        else pixel.a=c[29].w>2.5?cv*32.0:floor((cv+1.0)*255.0/8.0+0.5);
    }else if(c[1].x<3.0)pixel.a=255.0;
    return pixel/255.0;
#endif
#endif
#endif
}
)";
    s += hlsl ? "float4 main(float2 position:TEXCOORD0,float2 pixel:VPOS):COLOR0{return "
                "raster_pixel((pixel+float2(0.5,0.5))/"
                "max(c[103].z,1.0)+((ALPHA0_PASS||TEXEL0_PASS||TEXEL1_PASS||LOD_PASS)?c[110].xy:float2(0.0,0."
                "0)));}\n"
              : "void "
                "main(){gl_FragColor=raster_pixel(vec2(gl_FragCoord.x,c[103].y-gl_FragCoord.y)/"
                "max(c[103].z,1.0)+((ALPHA0_PASS!=0||TEXEL0_PASS!=0||TEXEL1_PASS!=0||LOD_PASS!=0)?c[110].xy:"
                "vec2(0.0,0.0)));}\n";
    {
        std::string filter = glrdp_vi::filter;
        auto replace = [&](const std::string &from, const std::string &to) {
            std::size_t at = 0;
            while ((at = filter.find(from, at)) != std::string::npos) {
                filter.replace(at, from.size(), to);
                at += to.size();
            }
        };
        replace("vec2(", "vi_make2(");
        replace("vec3(", "vi_make3(");
        replace("vec2", "V2");
        replace("vec3", "V3");
        replace("vec4", "V4");
        if (hlsl) {
            replace("mix(", "lerp(");
            replace("fract(", "frac(");
            replace("mod(", "vi_mod(");
        }
        const std::string marker = "// GLRDP_SHARED_VI_FILTER";
        s.replace(s.find(marker), marker.size(), filter);
    }
    if (specialize) {
        auto replace = [&](const std::string &from, const std::string &to) {
            std::size_t at = 0;
            while ((at = s.find(from, at)) != std::string::npos) {
                s.replace(at, from.size(), to);
                at += to.size();
            }
        };
        for (unsigned i = 1; i <= 28; i++)
            if (i == 1 || i >= 19) {
                const auto &v = specialize->constants[i];
                replace("c[" + std::to_string(i) + "]",
                        "(V4(" + std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
                            std::to_string(v[2]) + "," + std::to_string(v[3]) + "))");
            }
        for (unsigned i = 0; i < 4; i++)
            replace(std::string("c[41].") + "xyzw"[i], std::to_string(specialize->constants[41][i]));
        replace("c[29].z", std::to_string(float(specialize->scale)));
        replace("c[45].x", std::to_string(specialize->constants[45][0]));
        for (unsigned i = 88; i <= 90; i++) {
            const auto &v = specialize->constants[i];
            replace("c[" + std::to_string(i) + "]", "(V4(" + std::to_string(v[0]) + "," +
                                                        std::to_string(v[1]) + "," + std::to_string(v[2]) +
                                                        "," + std::to_string(v[3]) + "))");
        }
    }
    return s;
}

std::string raster_fragment_shader(RasterShader pass, ShaderDialect dialect, const RasterDraw *draw) {
    if (dialect != ShaderDialect::GLSLES100)
        return raster_fragment_shader(pass, dialect == ShaderDialect::HLSL3, draw);
    auto s = raster_fragment_shader(pass, false, draw);
    s.replace(0, s.find('\n') + 1,
              "#version 100\nprecision highp float;\nprecision highp sampler2D;\n"
              "varying highp vec2 pixel_position;\n#define GLES2_PIPELINE 1\n");
    const auto declaration = s.find("uniform vec4 c[112];");
    s.replace(declaration, std::string("uniform vec4 c[112];").size(), R"(
uniform sampler2D draw_state;
uniform sampler2D coverage_alpha;
float state_scalar(float i) {
    vec2 p=vec2(mod(i,16.0),floor(i/16.0));
    vec4 b=floor(texture2D(draw_state,(p+0.5)/vec2(16.0,32.0))*255.0+0.5);
    float exponent=mod(b.a,128.0)*2.0+floor(b.b/128.0);
    if(exponent==0.0)return 0.0;
    float mantissa=mod(b.b,128.0)*65536.0+b.g*256.0+b.r;
    return (b.a>=128.0?-1.0:1.0)*(1.0+mantissa/8388608.0)*exp2(exponent-127.0);
}
vec4 C(float i) { return vec4(state_scalar(i*4.0),state_scalar(i*4.0+1.0),
                             state_scalar(i*4.0+2.0),state_scalar(i*4.0+3.0)); }
)");
    s = std::regex_replace(s, std::regex(R"(c\[([0-9]+)\])"), "C($1.0)");
    s = std::regex_replace(s, std::regex(R"(\bpacked\b)"), "packed_byte");
    const std::string coordinate = "vec2(gl_FragCoord.x,C(103.0).y-gl_FragCoord.y)";
    s.replace(s.find(coordinate), coordinate.size(), "pixel_position");
    if (pass == RasterShader::Candidate) {
        const auto at = s.find("#define SPLIT_TEXTURES 1");
        s.replace(at, std::string("#define SPLIT_TEXTURES 1").size(), "#define SPLIT_TEXTURES 0");
    }
    // Fetch each packet vector once per fragment, rather than repeating four
    // byte-texture reads at every helper call. Unused globals are eliminated.
    std::string declarations, initialize;
    for (unsigned i = 0; i < 112; ++i) {
        const auto access = "C(" + std::to_string(i) + ".0)";
        const auto name = "state_" + std::to_string(i);
        std::size_t at = 0;
        bool used = false;
        while ((at = s.find(access, at)) != std::string::npos) {
            s.replace(at, access.size(), name);
            at += name.size(); used = true;
        }
        if (used) {
            declarations += "vec4 " + name + ";\n";
            initialize += name + "=" + access + ";\n";
        }
    }
    const auto header = s.find("#define V2");
    s.insert(header, declarations);
    s.insert(s.find("void main(){") + std::string("void main(){").size(), initialize);
    return s;
}
} // namespace glrdp::legacy
