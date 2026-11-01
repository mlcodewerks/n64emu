void Emitter::simd_memory(unsigned reg,unsigned base,int32_t offset,bool store,unsigned bytes)
{
#ifdef N64EMU_JIT32_X86
    auto& code=host.code;auto v=Xbyak::Xmm(reg);auto address=n64emu32::Assembler::mem(0,base,offset);
    if(store){if(bytes==16)code.movdqu(address,v);else if(bytes==8)code.movq(address,v);else code.movd(address,v);}
    else {if(bytes==16)code.movdqu(v,address);else if(bytes==8)code.movq(v,address);else code.movd(v,address);}
#else
    host.mov(12,base);if(offset)host.immediate(n64emu32::ADD,12,uint32_t(offset));
    uint32_t encoding=bytes==16?0xf42c0a0fu:bytes==8?0xf42c070fu:0xf4ac080fu;
    if(store)encoding&=~0x00200000u;
    unsigned d=reg*2;host.word(encoding|((d&15)<<12)|((d&16)<<18));
#endif
}
void Emitter::simd_spill(unsigned reg,unsigned offset)
{
    simd_memory(reg,
#ifdef N64EMU_JIT32_X86
        4,
#else
        13,
#endif
        int32_t(offset),true);
}
void Emitter::simd_reload(unsigned reg,unsigned offset)
{
    simd_memory(reg,
#ifdef N64EMU_JIT32_X86
        4,
#else
        13,
#endif
        int32_t(offset),false);
}
void Emitter::simd_node(unsigned operation,unsigned d,unsigned a,unsigned b,unsigned c,uint32_t immediate)
{
    VOp op=VOp(operation);
#ifdef N64EMU_JIT32_X86
    auto& code=host.code;Xbyak::Xmm dd(d),aa(a),bb(b);
    if(op==VOp::Load){simd_memory(d,STATE,int32_t(immediate),false);return;}
    if(op==VOp::Store){simd_memory(a,STATE,int32_t(immediate),true);return;}
    if(op==VOp::Shuffle) {
        if(immediate<2){if(d!=a)code.movdqa(dd,aa);return;}
        unsigned lane=immediate&(immediate<4?1u:immediate<8?3u:7u);
        unsigned mask=immediate<4?(lane?0xf5:0xa0):(lane&3)*0x55;
        if(immediate<8||lane<4)code.pshuflw(dd,aa,mask);else code.pshufhw(dd,aa,mask);
        if(immediate<8)code.pshufhw(dd,dd,mask);else code.pshufd(dd,dd,lane<4?0:0xaa);
        return;
    }
    if(op!=VOp::Constant&&d!=a)code.movdqa(dd,aa);
    switch(op) {
    case VOp::Constant:
        if(!immediate)code.pxor(dd,dd);else if(immediate==UINT32_MAX)code.pcmpeqd(dd,dd);
        else {code.mov(code.eax,immediate);code.movd(dd,code.eax);code.pshufd(dd,dd,0);}break;
#define SIMD(op,name) case VOp::op:code.name(dd,bb);break;
        SIMD(Add,paddw) SIMD(Sub,psubw) SIMD(Adds,paddsw) SIMD(Subs,psubsw) SIMD(Addu,paddusw) SIMD(Subu,psubusw)
        SIMD(And,pand) SIMD(Or,por) SIMD(Xor,pxor) SIMD(AndNot,pandn) SIMD(Eq,pcmpeqw) SIMD(Gt,pcmpgtw)
        SIMD(Min,pminsw) SIMD(Max,pmaxsw) SIMD(MulLo,pmullw) SIMD(MulHi,pmulhw) SIMD(MulHiU,pmulhuw)
        SIMD(UnpackLo,punpcklwd) SIMD(UnpackHi,punpckhwd) SIMD(Pack,packssdw)
#undef SIMD
    case VOp::Shl:code.psllw(dd,immediate);break;case VOp::Shr:code.psrlw(dd,immediate);break;case VOp::Sar:code.psraw(dd,immediate);break;
    case VOp::Blend:
        code.movdqa(code.xmm0,Xbyak::Xmm(c));
        // SSE2 implementation has the same byte-sign semantics as PBLENDVB.
        code.mov(code.eax,uint32_t(uintptr_t(blend_byte_sign_bits)));
        code.pminub(code.xmm0,code.ptr[code.eax]);code.pcmpeqb(code.xmm0,code.ptr[code.eax]);
        code.pxor(dd,bb);code.pandn(code.xmm0,dd);code.movdqa(dd,code.xmm0);code.pxor(dd,bb);break;
    default:assert(false);break;
    }
#else
    auto binary=[&](uint32_t encoding){neon3(encoding,d*2,a*2,b*2);};
    auto copy=[&](unsigned dest,unsigned source){neon3(0xf2220154,dest*2,source*2,source*2);};
    switch(op) {
    case VOp::Load:simd_memory(d,STATE,int32_t(immediate),false);break;
    case VOp::Store:simd_memory(a,STATE,int32_t(immediate),true);break;
    case VOp::Constant:{host.imm(0,immediate);unsigned reg=d*2;host.word(0xeea00b10u|((reg&15)<<16)|((reg&16)<<3));break;}
    case VOp::Shuffle:
        copy(14,a);host.imm(0,uint32_t(uintptr_t(element_masks[immediate].data())));simd_memory(15,0,0,false);
        neon3(0xf3bc09ae,d*2,28,30);neon3(0xf3bc09ae,d*2+1,28,31);break;
#define SIMD(op,encoding) case VOp::op:binary(encoding);break;
        SIMD(Add,0xf2120844) SIMD(Sub,0xf3120844) SIMD(Adds,0xf2120054) SIMD(Subs,0xf2120254)
        SIMD(Addu,0xf3120054) SIMD(Subu,0xf3120254) SIMD(And,0xf2020154) SIMD(Or,0xf2220154)
        SIMD(Xor,0xf3020154) SIMD(Eq,0xf3120854) SIMD(Gt,0xf2120344) SIMD(Min,0xf2120654)
        SIMD(Max,0xf2120644) SIMD(MulLo,0xf2120954)
#undef SIMD
    case VOp::AndNot:neon3(0xf2120154,d*2,b*2,a*2);break;
    case VOp::MulHi:case VOp::MulHiU:
        neon3(op==VOp::MulHi?0xf2d2cc04:0xf3d2cc04,28,a*2,b*2);
        neon3(op==VOp::MulHi?0xf2d2cc04:0xf3d2cc04,30,a*2+1,b*2+1);
        neon2(0xf290083c,d*2,28);neon2(0xf290083c,d*2+1,30);break;
    case VOp::Shl:case VOp::Shr:case VOp::Sar:
        if(!immediate)copy(d,a);
        else {uint32_t encoding=op==VOp::Shl?0xf2930552:op==VOp::Shr?0xf39d0052:0xf29d0052;
            encoding=(encoding&~0x003f0000u)|((op==VOp::Shl?16+immediate:32-immediate)<<16);neon2(encoding,d*2,a*2);}break;
    case VOp::UnpackLo:case VOp::UnpackHi:
        copy(14,a);copy(15,b);neon2(0xf3f6c1ee,28,30);copy(d,op==VOp::UnpackLo?14:15);break;
    case VOp::Pack:neon2(0xf3b60282,d*2,a*2);neon2(0xf3b60282,d*2+1,b*2);break;
    case VOp::Blend:
        neon2(0xf2c9c052,28,c*2);neon3(0xf352c154,28,b*2,a*2);copy(d,14);break;
    }
#endif
}
void Emitter::vector_memory(unsigned state,unsigned dmem,unsigned address,HostWord vector_offset,bool store,unsigned bytes)
{
    using namespace n64emu32;
    if(bytes<4) {
        loadv(address,1);host.immediate(XOR,1,bytes==1?3:2);host.alu(ADD,1,DMEM);
        if(store){host.load(0,STATE,int32_t(vector_offset),bytes);host.store(0,1,0,bytes);}
        else {host.load(0,1,0,bytes);host.store(0,STATE,int32_t(vector_offset),bytes);}
        return;
    }
    if(store)simd_memory(0,STATE,int32_t(vector_offset),false,bytes);
    loadv(address,0);host.alu(ADD,0,DMEM);host.store(0,FP,-924);
    if(!store)simd_memory(0,0,0,false,bytes);
#ifdef N64EMU_JIT32_X86
    host.code.pshuflw(host.code.xmm0,host.code.xmm0,0xb1);
    if(bytes==16)host.code.pshufhw(host.code.xmm0,host.code.xmm0,0xb1);
#else
    neon2(0xf3b400c2,0,0);
#endif
    if(store){host.load(0,FP,-924);simd_memory(0,0,0,true,bytes);}
    else simd_memory(0,STATE,int32_t(vector_offset),true,bytes);
    (void)state;(void)dmem;
}
void Emitter::vector_packed_load(unsigned state,unsigned dmem,unsigned address,HostWord vector_offset,unsigned shift)
{
    loadv(address,0);host.alu(n64emu32::ADD,0,DMEM);simd_memory(0,0,0,false,8);
#ifdef N64EMU_JIT32_X86
    auto& code=host.code;code.pxor(code.xmm1,code.xmm1);code.punpcklbw(code.xmm0,code.xmm1);
    code.pshuflw(code.xmm0,code.xmm0,0x1b);code.pshufhw(code.xmm0,code.xmm0,0x1b);code.psllw(code.xmm0,shift);
#else
    neon2(0xf3b00081,0,0);neon2(0xf3880a11,0,0);
    neon2((0xf2930552u&~0x003f0000u)|((16+shift)<<16),0,0);
#endif
    simd_memory(0,STATE,int32_t(vector_offset),true);(void)state;(void)dmem;
}
