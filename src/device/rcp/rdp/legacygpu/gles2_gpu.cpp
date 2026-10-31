#include "legacy_gpu.hpp"
#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace glrdp::legacy {
namespace {
template <class T> T resolve(Resolver resolver, const char *name) {
    auto p = reinterpret_cast<T>(resolver(name));
    if (!p)
        throw std::runtime_error(std::string("GLES2 missing function: ") + name);
    return p;
}
class GLES2 final : public Device {
    Capabilities caps_;
    // A frontend may supply an ES3 context for an ES2 request. Neutralize the
    // additional state that can affect ES2 texture transfers and sampling.
    bool es3_ = false;
    using BindSampler = void (GL_APIENTRY *)(GLuint, GLuint);
    BindSampler bind_sampler_ = nullptr;
    static constexpr GLenum pack_buffer = 0x88eb, unpack_buffer = 0x88ec,
        pack_binding = 0x88ed, unpack_binding = 0x88ef, sampler_binding = 0x8919,
        read_framebuffer_binding = 0x8caa, draw_framebuffer = 0x8ca9, read_framebuffer = 0x8ca8;
    static constexpr std::array<GLenum,6> row_parameters = {0x0d02,0x0d03,0x0d04,0x0cf2,0x0cf3,0x0cf4};
    decltype(&::glActiveTexture) glActiveTexture = nullptr;
    decltype(&::glAttachShader) glAttachShader = nullptr;
    decltype(&::glBindAttribLocation) glBindAttribLocation = nullptr;
    decltype(&::glBindBuffer) glBindBuffer = nullptr;
    decltype(&::glBindFramebuffer) glBindFramebuffer = nullptr;
    decltype(&::glBindTexture) glBindTexture = nullptr;
    decltype(&::glBufferData) glBufferData = nullptr;
    decltype(&::glCheckFramebufferStatus) glCheckFramebufferStatus = nullptr;
    decltype(&::glClear) glClear = nullptr;
    decltype(&::glClearColor) glClearColor = nullptr;
    decltype(&::glColorMask) glColorMask = nullptr;
    decltype(&::glCompileShader) glCompileShader = nullptr;
    decltype(&::glCopyTexSubImage2D) glCopyTexSubImage2D = nullptr;
    decltype(&::glCreateProgram) glCreateProgram = nullptr;
    decltype(&::glCreateShader) glCreateShader = nullptr;
    decltype(&::glDeleteBuffers) glDeleteBuffers = nullptr;
    decltype(&::glDeleteFramebuffers) glDeleteFramebuffers = nullptr;
    decltype(&::glDeleteProgram) glDeleteProgram = nullptr;
    decltype(&::glDeleteShader) glDeleteShader = nullptr;
    decltype(&::glDeleteTextures) glDeleteTextures = nullptr;
    decltype(&::glDisable) glDisable = nullptr;
    decltype(&::glDisableVertexAttribArray) glDisableVertexAttribArray = nullptr;
    decltype(&::glDrawArrays) glDrawArrays = nullptr;
    decltype(&::glEnable) glEnable = nullptr;
    decltype(&::glEnableVertexAttribArray) glEnableVertexAttribArray = nullptr;
    decltype(&::glFramebufferTexture2D) glFramebufferTexture2D = nullptr;
    decltype(&::glGenBuffers) glGenBuffers = nullptr;
    decltype(&::glGenFramebuffers) glGenFramebuffers = nullptr;
    decltype(&::glGenTextures) glGenTextures = nullptr;
    decltype(&::glGetBooleanv) glGetBooleanv = nullptr;
    decltype(&::glGetError) glGetError = nullptr;
    decltype(&::glGetFloatv) glGetFloatv = nullptr;
    decltype(&::glGetFramebufferAttachmentParameteriv) glGetFramebufferAttachmentParameteriv = nullptr;
    decltype(&::glGetIntegerv) glGetIntegerv = nullptr;
    decltype(&::glGetProgramInfoLog) glGetProgramInfoLog = nullptr;
    decltype(&::glGetProgramiv) glGetProgramiv = nullptr;
    decltype(&::glGetShaderInfoLog) glGetShaderInfoLog = nullptr;
    decltype(&::glGetShaderPrecisionFormat) glGetShaderPrecisionFormat = nullptr;
    decltype(&::glGetShaderiv) glGetShaderiv = nullptr;
    decltype(&::glGetString) glGetString = nullptr;
    decltype(&::glGetUniformLocation) glGetUniformLocation = nullptr;
    decltype(&::glGetVertexAttribPointerv) glGetVertexAttribPointerv = nullptr;
    decltype(&::glGetVertexAttribiv) glGetVertexAttribiv = nullptr;
    decltype(&::glIsEnabled) glIsEnabled = nullptr;
    decltype(&::glLinkProgram) glLinkProgram = nullptr;
    decltype(&::glPixelStorei) glPixelStorei = nullptr;
    decltype(&::glReadPixels) glReadPixels = nullptr;
    decltype(&::glScissor) glScissor = nullptr;
    decltype(&::glShaderSource) glShaderSource = nullptr;
    decltype(&::glTexImage2D) glTexImage2D = nullptr;
    decltype(&::glTexParameteri) glTexParameteri = nullptr;
    decltype(&::glTexSubImage2D) glTexSubImage2D = nullptr;
    decltype(&::glUniform1i) glUniform1i = nullptr;
    decltype(&::glUniform2f) glUniform2f = nullptr;
    decltype(&::glUniform4f) glUniform4f = nullptr;
    decltype(&::glUseProgram) glUseProgram = nullptr;
    decltype(&::glVertexAttribPointer) glVertexAttribPointer = nullptr;
    decltype(&::glViewport) glViewport = nullptr;
    std::array<GLuint, 6> programs_{};
    std::array<GLuint, 3> textures_{};
    GLuint framebuffer_ = 0;
    unsigned width_ = 0, height_ = 0;
    unsigned target_width_ = 0, target_height_ = 0;
    GLuint vertex_buffer_ = 0, state_texture_ = 0;
    bool presenting_ = false;
    GLuint present_fbo_ = 0;
    unsigned present_width_ = 0, present_height_ = 0;
    struct Guard {
        GLES2 &g;
        GLint program=0, framebuffer=0, active=0, buffer=0, pack=0, unpack=0;
        GLint read_fbo=0, pack_pbo=0, unpack_pbo=0;
        std::array<GLint,6> rows{};
        std::array<GLint,8> samplers{};
        GLint viewport[4]{}, scissor[4]{};
        GLboolean mask[4]{}; GLfloat clear[4]{};
        std::array<GLint,8> textures{};
        static constexpr std::array<GLenum,8> enables = {GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,
            GL_CULL_FACE,GL_SCISSOR_TEST,GL_DITHER,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE};
        std::array<GLboolean,8> enabled{};
        struct Attribute { GLint enabled, size, type, normalized, stride, buffer; void *pointer; };
        std::array<Attribute,2> attributes{};
        Guard(GLES2 &gpu) : g(gpu) {
            g.glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            g.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
            if(g.es3_){
                g.glGetIntegerv(read_framebuffer_binding,&read_fbo);
                g.glGetIntegerv(pack_binding,&pack_pbo);g.glGetIntegerv(unpack_binding,&unpack_pbo);
                g.glBindBuffer(pack_buffer,0);g.glBindBuffer(unpack_buffer,0);
                for(unsigned i=0;i<rows.size();++i){g.glGetIntegerv(row_parameters[i],&rows[i]);g.glPixelStorei(row_parameters[i],0);}
            }
            g.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            g.glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
            g.glGetIntegerv(GL_VIEWPORT,viewport); g.glGetIntegerv(GL_SCISSOR_BOX,scissor);
            g.glGetBooleanv(GL_COLOR_WRITEMASK,mask);g.glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
            g.glGetIntegerv(GL_PACK_ALIGNMENT,&pack);g.glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
            for(unsigned i=0;i<textures.size();++i){
                g.glActiveTexture(GL_TEXTURE0+i);g.glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
                if(g.bind_sampler_){g.glGetIntegerv(sampler_binding,&samplers[i]);g.bind_sampler_(i,0);}
            }
            for(unsigned i=0;i<enables.size();++i)enabled[i]=g.glIsEnabled(enables[i]);
            for(unsigned i=0;i<attributes.size();++i){auto &a=attributes[i];
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&a.enabled);
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_SIZE,&a.size);
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_TYPE,&a.type);
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&a.normalized);
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&a.stride);
                g.glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&a.buffer);
                g.glGetVertexAttribPointerv(i,GL_VERTEX_ATTRIB_ARRAY_POINTER,&a.pointer);
            }
            g.glPixelStorei(GL_PACK_ALIGNMENT,1);g.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        }
        ~Guard(){
            g.glUseProgram(program);g.glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
            if(g.es3_){
                g.glBindFramebuffer(read_framebuffer,read_fbo);
                g.glBindBuffer(pack_buffer,pack_pbo);g.glBindBuffer(unpack_buffer,unpack_pbo);
                for(unsigned i=0;i<rows.size();++i)g.glPixelStorei(row_parameters[i],rows[i]);
            }
            g.glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            g.glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
            g.glColorMask(mask[0],mask[1],mask[2],mask[3]);g.glClearColor(clear[0],clear[1],clear[2],clear[3]);
            for(unsigned i=0;i<textures.size();++i){
                g.glActiveTexture(GL_TEXTURE0+i);g.glBindTexture(GL_TEXTURE_2D,textures[i]);
                if(g.bind_sampler_)g.bind_sampler_(i,samplers[i]);
            }
            for(unsigned i=0;i<enables.size();++i){if(enabled[i])g.glEnable(enables[i]);else g.glDisable(enables[i]);}
            for(unsigned i=0;i<attributes.size();++i){auto &a=attributes[i];
                g.glBindBuffer(GL_ARRAY_BUFFER,a.buffer);
                g.glVertexAttribPointer(i,a.size,a.type,a.normalized,a.stride,a.pointer);
                if(a.enabled)g.glEnableVertexAttribArray(i);else g.glDisableVertexAttribArray(i);
            }
            g.glBindBuffer(GL_ARRAY_BUFFER,buffer);g.glActiveTexture(active);
            g.glPixelStorei(GL_PACK_ALIGNMENT,pack);g.glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
        }
    };
    struct RasterTarget {
        GLuint color = 0, feedback = 0, fbo = 0;
        int width = 0, height = 0, tw = 0, th = 0;
    };
    unsigned combined_ = 0, alpha0_ = 0, texel0_ = 0, texel1_ = 0, lod_ = 0, coverage_alpha_ = 0;
    std::unordered_map<unsigned, RasterTarget> raster_targets_;
    std::array<GLuint, 15> raster_programs_{};
    std::map<std::pair<RasterShader, RasterKey>, GLuint> raster_specialized_;
    std::unordered_map<GLuint, std::array<GLint, 14>> raster_locations_;
    std::unique_ptr<Guard> raster_guard_;
    GLuint raster_program(RasterShader pass, const RasterDraw *specialize = nullptr) {
        auto &id = specialize ? raster_specialized_[{pass, raster_key(*specialize)}]
                              : raster_programs_[unsigned(pass)];
        if (id)
            return id;
        const auto source = raster_fragment_shader(pass, ShaderDialect::GLSLES100, specialize);
        GLuint vs = shader(GL_VERTEX_SHADER, vertex_shader());
        GLuint fs = 0;
        try { fs = shader(GL_FRAGMENT_SHADER, source); }
        catch (...) { glDeleteShader(vs); throw; }
        id = glCreateProgram();
        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glBindAttribLocation(id,0,"position");
        glBindAttribLocation(id,1,"texcoord");
        glLinkProgram(id);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char message[8192]{};
            glGetProgramInfoLog(id, sizeof(message), nullptr, message);
            glDeleteProgram(id); id = 0;
            throw std::runtime_error(std::string("GLES2 RDP link: ") + message);
        }
        return id;
    }
    void raster_state() {
        for(auto v:Guard::enables)glDisable(v);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    static const char *vertex_shader() { return R"(#version 100
precision highp float;
attribute vec2 position,texcoord;
uniform vec2 vertex_size;
varying highp vec2 uv;
varying highp vec2 pixel_position;
void main(){gl_Position=vec4(position,0.0,1.0);uv=texcoord;
    pixel_position=vec2(position.x+1.0,1.0-position.y)*vertex_size*0.5;}
)"; }
    void vertices(GLenum mode,const std::vector<float> &data) {
        if(!vertex_buffer_)glGenBuffers(1,&vertex_buffer_);
        glBindBuffer(GL_ARRAY_BUFFER,vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(float),data.data(),GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
        glDrawArrays(mode,0,GLsizei(data.size()/4));
    }
    void snapshot(RasterTarget &t, const RasterDraw &draw) {
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t.feedback);
        int x0 = std::clamp(int(std::floor(draw.x0 * draw.scale)), 0, t.width),
            x1 = std::clamp(int(std::ceil(draw.x1 * draw.scale)), x0, t.width);
        int y0 = std::clamp(int(std::floor(draw.y0 * draw.scale)), 0, t.height),
            y1 = std::clamp(int(std::ceil(draw.y1 * draw.scale)), y0, t.height);
        if (x1 > x0 && y1 > y0)
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x0, t.height - y1, x0, t.height - y1, x1 - x0, y1 - y0);
    }
    void raster_quad(float x0, float y0, float x1, float y1, int scale, int w, int h) {
        const float left = x0 * scale * 2.0f / w - 1.0f, right = x1 * scale * 2.0f / w - 1.0f;
        const float top = 1.0f - y0 * scale * 2.0f / h, bottom = 1.0f - y1 * scale * 2.0f / h;
        vertices(GL_TRIANGLE_STRIP,{left,top,x0,y0,right,top,x1,y0,
                                    left,bottom,x0,y1,right,bottom,x1,y1});
    }
    void raster_inputs(GLuint p, const std::array<GLuint, 12> &textures,
                       const std::array<Float4, 112> &constants) {
        auto [it, inserted] = raster_locations_.try_emplace(p);
        static constexpr const char *names[] = {"memory_color","tile0","tile1","old_depth",
            "new_depth","noise_tex","reciprocal_tex","combined_color","first_alpha",
            "cached0","cached1","cached_lod","coverage_alpha"};
        if(inserted){it->second[0]=glGetUniformLocation(p,"draw_state");
            for(unsigned i=0;i<13;++i)it->second[i+1]=glGetUniformLocation(p,names[i]);}
        glUseProgram(p);
        if(!state_texture_)glGenTextures(1,&state_texture_);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,state_texture_);
        std::array<std::uint8_t,16*32*4> bytes{};
        for(unsigned i=0;i<112;++i)for(unsigned j=0;j<4;++j){
            const float v=constants[i][j];std::uint32_t bits;std::memcpy(&bits,&v,4);
            if(!std::isfinite(v)||(v!=0 && std::fpclassify(v)==FP_SUBNORMAL))
                throw std::runtime_error("GLES2 unsupported draw-state value");
            for(unsigned b=0;b<4;++b)bytes[(i*4+j)*4+b]=std::uint8_t(bits>>(b*8));
        }
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,32,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
        texture_parameters();glUniform1i(it->second[0],0);
        unsigned unit=1;
        for(unsigned i=0;i<13;++i)if(it->second[i+1]>=0){
            if(unit>=8)throw std::runtime_error("GLES2 pass exceeds eight sampler budget");
            glActiveTexture(GL_TEXTURE0+unit);
            const auto texture=i==12?coverage_texture_:textures[i];
            GLint attached=0,fbo=0,type=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
            if(fbo){
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attached);
            }
            if(texture && type==GL_TEXTURE && GLuint(attached)==texture)throw std::runtime_error("GLES2 texture feedback loop");
            glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(it->second[i+1],unit++);
        }
        glUniform2f(glGetUniformLocation(p,"vertex_size"),constants[103][0],constants[103][1]);
    }
    GLuint coverage_texture_ = 0;
    void texture_parameters(){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    GLuint shader(GLenum type, const std::string &source) {
        GLuint s = glCreateShader(type);
        const char *text = source.c_str();
        glShaderSource(s, 1, &text, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char message[4096]{};
            glGetShaderInfoLog(s, sizeof(message), nullptr, message);
            glDeleteShader(s);
            throw std::runtime_error(std::string("GLES2 shader: ") + message);
        }
        return s;
    }
    GLuint program(Kernel k) {
        auto &id = programs_.at(unsigned(k));
        if (id)
            return id;
        const GLuint vs=shader(GL_VERTEX_SHADER,vertex_shader());
        GLuint fs = 0, p = 0;
        try {
            fs = shader(GL_FRAGMENT_SHADER, fragment_shader(k, ShaderDialect::GLSLES100));
            p = glCreateProgram();
            glAttachShader(p, vs);
            glAttachShader(p, fs);
            glBindAttribLocation(p,0,"position");
            glBindAttribLocation(p,1,"texcoord");
            glLinkProgram(p);
            GLint ok = 0;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) {
                char message[4096]{};
                glGetProgramInfoLog(p, sizeof(message), nullptr, message);
                throw std::runtime_error(std::string("GLES2 shader link: ") + message);
            }
        } catch (...) {
            glDeleteShader(vs);
            if (fs)
                glDeleteShader(fs);
            if (p)
                glDeleteProgram(p);
            throw;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        id = p;
        return id;
    }

    void validate_storage() {
        Image input{256,1,{}};input.rgba.resize(1024);
        for(unsigned i=0;i<256;++i){
            input.rgba[i*4]=std::uint8_t(i);input.rgba[i*4+1]=std::uint8_t(i*73+19);
            input.rgba[i*4+2]=std::uint8_t(i*151+29);input.rgba[i*4+3]=std::uint8_t(255-i);
        }
        if(execute(Kernel::Copy,input,input).rgba!=input.rgba)
            throw std::runtime_error("GLES2 RGBA byte round-trip failed");
        // Require exact highp carry and 16-bit limbs, independent of reported precision.
        auto packed=execute(Kernel::PackDepth,input,input);
        for(unsigned i=0;i<256;++i)
            if(packed.rgba[i*4+2]!=(input.rgba[i*4+2]%4)+(i%16)*4 ||
               packed.rgba[i*4+3]!=input.rgba[i*4+3]%8)
                throw std::runtime_error("GLES2 packed arithmetic probe failed");
        Image operands{256,1,std::vector<std::uint8_t>(1024)},high=operands;
        std::vector<std::uint8_t> expected(1024);
        for(unsigned i=0;i<256;++i){
            int v[]={int((i*71+133)&511),int((i*151+79)&511),int((i*199+241)&511),int((i*83+83)&511)};
            for(unsigned j=0;j<4;++j){operands.rgba[i*4+j]=std::uint8_t(v[j]);high.rgba[i*4]|=std::uint8_t((v[j]>>8)<<j);if(v[j]>=256)v[j]-=512;}
            const int n=(v[0]-v[1])*v[2]+v[3]*256+128;
            const unsigned q=unsigned(n>=0?n/256:-((-n+255)/256))&511;
            expected[i*4]=std::uint8_t(q);expected[i*4+1]=std::uint8_t(q>>8);
            expected[i*4+2]=std::uint8_t(q<256?q:(q<384?255:0));expected[i*4+3]=255;
        }
        if(execute(Kernel::Combine9,operands,high).rgba!=expected)
            throw std::runtime_error("GLES2 signed combiner arithmetic probe failed");
    }

  public:
    GLES2(Resolver resolver) {
        glActiveTexture=resolve<decltype(glActiveTexture)>(resolver,"glActiveTexture");
        glAttachShader=resolve<decltype(glAttachShader)>(resolver,"glAttachShader");
        glBindAttribLocation=resolve<decltype(glBindAttribLocation)>(resolver,"glBindAttribLocation");
        glBindBuffer=resolve<decltype(glBindBuffer)>(resolver,"glBindBuffer");
        glBindFramebuffer=resolve<decltype(glBindFramebuffer)>(resolver,"glBindFramebuffer");
        glBindTexture=resolve<decltype(glBindTexture)>(resolver,"glBindTexture");
        glBufferData=resolve<decltype(glBufferData)>(resolver,"glBufferData");
        glCheckFramebufferStatus=resolve<decltype(glCheckFramebufferStatus)>(resolver,"glCheckFramebufferStatus");
        glClear=resolve<decltype(glClear)>(resolver,"glClear");
        glClearColor=resolve<decltype(glClearColor)>(resolver,"glClearColor");
        glColorMask=resolve<decltype(glColorMask)>(resolver,"glColorMask");
        glCompileShader=resolve<decltype(glCompileShader)>(resolver,"glCompileShader");
        glCopyTexSubImage2D=resolve<decltype(glCopyTexSubImage2D)>(resolver,"glCopyTexSubImage2D");
        glCreateProgram=resolve<decltype(glCreateProgram)>(resolver,"glCreateProgram");
        glCreateShader=resolve<decltype(glCreateShader)>(resolver,"glCreateShader");
        glDeleteBuffers=resolve<decltype(glDeleteBuffers)>(resolver,"glDeleteBuffers");
        glDeleteFramebuffers=resolve<decltype(glDeleteFramebuffers)>(resolver,"glDeleteFramebuffers");
        glDeleteProgram=resolve<decltype(glDeleteProgram)>(resolver,"glDeleteProgram");
        glDeleteShader=resolve<decltype(glDeleteShader)>(resolver,"glDeleteShader");
        glDeleteTextures=resolve<decltype(glDeleteTextures)>(resolver,"glDeleteTextures");
        glDisable=resolve<decltype(glDisable)>(resolver,"glDisable");
        glDisableVertexAttribArray=resolve<decltype(glDisableVertexAttribArray)>(resolver,"glDisableVertexAttribArray");
        glDrawArrays=resolve<decltype(glDrawArrays)>(resolver,"glDrawArrays");
        glEnable=resolve<decltype(glEnable)>(resolver,"glEnable");
        glEnableVertexAttribArray=resolve<decltype(glEnableVertexAttribArray)>(resolver,"glEnableVertexAttribArray");
        glFramebufferTexture2D=resolve<decltype(glFramebufferTexture2D)>(resolver,"glFramebufferTexture2D");
        glGenBuffers=resolve<decltype(glGenBuffers)>(resolver,"glGenBuffers");
        glGenFramebuffers=resolve<decltype(glGenFramebuffers)>(resolver,"glGenFramebuffers");
        glGenTextures=resolve<decltype(glGenTextures)>(resolver,"glGenTextures");
        glGetBooleanv=resolve<decltype(glGetBooleanv)>(resolver,"glGetBooleanv");
        glGetError=resolve<decltype(glGetError)>(resolver,"glGetError");
        glGetFloatv=resolve<decltype(glGetFloatv)>(resolver,"glGetFloatv");
        glGetFramebufferAttachmentParameteriv=resolve<decltype(glGetFramebufferAttachmentParameteriv)>(resolver,"glGetFramebufferAttachmentParameteriv");
        glGetIntegerv=resolve<decltype(glGetIntegerv)>(resolver,"glGetIntegerv");
        glGetProgramInfoLog=resolve<decltype(glGetProgramInfoLog)>(resolver,"glGetProgramInfoLog");
        glGetProgramiv=resolve<decltype(glGetProgramiv)>(resolver,"glGetProgramiv");
        glGetShaderInfoLog=resolve<decltype(glGetShaderInfoLog)>(resolver,"glGetShaderInfoLog");
        glGetShaderPrecisionFormat=resolve<decltype(glGetShaderPrecisionFormat)>(resolver,"glGetShaderPrecisionFormat");
        glGetShaderiv=resolve<decltype(glGetShaderiv)>(resolver,"glGetShaderiv");
        glGetString=resolve<decltype(glGetString)>(resolver,"glGetString");
        glGetUniformLocation=resolve<decltype(glGetUniformLocation)>(resolver,"glGetUniformLocation");
        glGetVertexAttribPointerv=resolve<decltype(glGetVertexAttribPointerv)>(resolver,"glGetVertexAttribPointerv");
        glGetVertexAttribiv=resolve<decltype(glGetVertexAttribiv)>(resolver,"glGetVertexAttribiv");
        glIsEnabled=resolve<decltype(glIsEnabled)>(resolver,"glIsEnabled");
        glLinkProgram=resolve<decltype(glLinkProgram)>(resolver,"glLinkProgram");
        glPixelStorei=resolve<decltype(glPixelStorei)>(resolver,"glPixelStorei");
        glReadPixels=resolve<decltype(glReadPixels)>(resolver,"glReadPixels");
        glScissor=resolve<decltype(glScissor)>(resolver,"glScissor");
        glShaderSource=resolve<decltype(glShaderSource)>(resolver,"glShaderSource");
        glTexImage2D=resolve<decltype(glTexImage2D)>(resolver,"glTexImage2D");
        glTexParameteri=resolve<decltype(glTexParameteri)>(resolver,"glTexParameteri");
        glTexSubImage2D=resolve<decltype(glTexSubImage2D)>(resolver,"glTexSubImage2D");
        glUniform1i=resolve<decltype(glUniform1i)>(resolver,"glUniform1i");
        glUniform2f=resolve<decltype(glUniform2f)>(resolver,"glUniform2f");
        glUniform4f=resolve<decltype(glUniform4f)>(resolver,"glUniform4f");
        glUseProgram=resolve<decltype(glUseProgram)>(resolver,"glUseProgram");
        glVertexAttribPointer=resolve<decltype(glVertexAttribPointer)>(resolver,"glVertexAttribPointer");
        glViewport=resolve<decltype(glViewport)>(resolver,"glViewport");
        es3_ = std::strstr(reinterpret_cast<const char*>(glGetString(GL_VERSION)),"OpenGL ES 3.") != nullptr;
        if(es3_)bind_sampler_=resolve<BindSampler>(resolver,"glBindSampler");
        Guard guard(*this);
        caps_.device=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        caps_.device+=" / ";caps_.device+=reinterpret_cast<const char*>(glGetString(GL_VERSION));
        GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);caps_.max_texture=limit;
        GLint viewport[2]{};glGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewport);
        caps_.max_texture=std::min(caps_.max_texture,unsigned(std::min(viewport[0],viewport[1])));
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&limit);caps_.fragment_samplers=limit;
        if(limit<8)throw std::runtime_error("GLES2 requires eight fragment samplers");
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS,&limit);
        if(limit<16)throw std::runtime_error("GLES2 requires sixteen fragment uniform vectors");
        GLint range[2]{},precision=0;glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
        if(precision<23||range[0]<62||range[1]<62)
            throw std::runtime_error("GLES2 requires fragment highp with 23 precision bits and exponent range 62");
        caps_.separate_shading=true;
        // Restricted NPOT is core: all textures are nearest/clamped and have no mipmaps.
        caps_.padded_power_of_two=false;
        try { validate_storage(); } catch(...) { release_resources(); throw; }
    }
    ~GLES2() override { release_resources(); }
    const Capabilities &capabilities() const override { return caps_; }
    int maximum_texture_size() const override { return int(caps_.max_texture); }
    void begin_raster() override {
        if (!raster_guard_)
            raster_guard_ = std::make_unique<Guard>(*this);
    }
    void end_raster() override { raster_guard_.reset(); }
    void allocate(unsigned &color, unsigned &framebuffer, int width, int height) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        RasterTarget t;
        t.width = width;
        t.height = height;
        t.tw = storage_extent(width, caps_.padded_power_of_two);
        t.th = storage_extent(height, caps_.padded_power_of_two);
        if (width <= 0 || height <= 0 || t.tw > int(caps_.max_texture) || t.th > int(caps_.max_texture))
            throw std::runtime_error("GLES2 RDP target exceeds caps");
        glGenTextures(1, &t.color);
        glGenTextures(1, &t.feedback);
        glGenFramebuffers(1, &t.fbo);
        glActiveTexture(GL_TEXTURE0);
        for (auto id : {t.color, t.feedback}) {
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.tw, t.th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteTextures(1,&t.color);glDeleteTextures(1,&t.feedback);glDeleteFramebuffers(1,&t.fbo);
            throw std::runtime_error("GLES2 RDP framebuffer incomplete");
        }
        color = framebuffer = t.color;
        raster_targets_.emplace(color, t);
    }
    void release(unsigned color, unsigned) override {
        auto it = raster_targets_.find(color);
        if (it == raster_targets_.end())
            return;
        auto &t = it->second;
        glDeleteTextures(1, &t.color);
        glDeleteTextures(1, &t.feedback);
        glDeleteFramebuffers(1, &t.fbo);
        raster_targets_.erase(it);
    }
    void upload(unsigned color, int width, int height, const std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, raster_targets_.at(color).color);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    void upload_region(unsigned color, int x, int y, int width, int height,
                       const std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        auto &t = raster_targets_.at(color);
        std::vector<std::uint8_t> bytes(std::size_t(width) * height * 4);
        for (int row = 0; row < height; row++)
            std::memcpy(bytes.data() + std::size_t(height - 1 - row) * width * 4,
                        rgba + std::size_t(row) * width * 4, width * 4);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t.color);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, t.height - y - height, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                        bytes.data());
    }
    void read(unsigned fbo, int x, int y, int width, int height, std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        glBindFramebuffer(GL_FRAMEBUFFER, raster_targets_.at(fbo).fbo);

        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    void draw_raster(const RasterDraw &draw) override { draw_batch({&draw}); }
    void draw_batch(const std::vector<const RasterDraw *> &draws) override {
        if (draws.empty())return;
        if(draws.size()>1){for(auto *d:draws)draw_batch({d});return;}
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto &first = *draws.front();
        auto &color = raster_targets_.at(first.color), &z = raster_targets_.at(first.depth);
        const bool depth_pass = first.constants[1][0] < 2 || first.constants[45][0] > .5f;
        bool shading = false;
        for (auto *d : draws)
            shading |= d->constants[1][0] < 2;
        const bool split = true;
        const bool split_tex = split;
        bool alpha = false, textured = false, second = false, lod = false;
        for (auto *d : draws) {
            alpha |= d->constants[1][0] == 1 && (unsigned(d->constants[27][3]) & 1);
            const bool tex = d->constants[1][0] < 2 && d->constants[1][3] > .5f;
            textured |= tex;
            second |= tex && d->constants[1][0] == 1;
            lod |= tex && (d->constants[89][0] > .5f || d->constants[90][0] > .5f);
        }
        alpha &= split;
        textured &= split_tex;
        second &= split_tex;
        lod &= split_tex;
        auto ensure = [&](unsigned &id, int w, int h) {
            if (id) {
                auto &t = raster_targets_.at(id);
                if (t.width == w && t.height == h)
                    return;
                release(id, id);
                id = 0;
            }
            unsigned fbo = 0;
            allocate(id, fbo, w, h);
        };
        ensure(coverage_alpha_,color.width,color.height);
        if (split)
            ensure(combined_, color.width, color.height);
        if (alpha)
            ensure(alpha0_, color.width + 2 * first.scale, color.height);
        if (textured)
            ensure(texel0_, color.width + 2 * first.scale, color.height);
        if (second)
            ensure(texel1_, color.width + 2 * first.scale, color.height);
        if (lod)
            ensure(lod_, color.width + 2 * first.scale, color.height);
        RasterDraw bounds = first;
        bool memory = false;
        for (auto *d : draws) {
            bounds.x0 = std::min(bounds.x0, d->x0);
            bounds.y0 = std::min(bounds.y0, d->y0);
            bounds.x1 = std::max(bounds.x1, d->x1);
            bounds.y1 = std::max(bounds.y1, d->y1);
            memory |= d->constants[41][2] > .5f;
        }
        if (memory)
            snapshot(color, bounds);
        if (depth_pass)
            snapshot(z, bounds);
        for (auto pass : {RasterShader::LOD, RasterShader::Texel0, RasterShader::Texel1, RasterShader::Shade,
                          RasterShader::Alpha0, RasterShader::Candidate, RasterShader::CoverageAlpha, RasterShader::Depth, RasterShader::Color}) {
            if (pass == RasterShader::LOD && !lod)
                continue;
            if (pass == RasterShader::Texel0 && !textured)
                continue;
            if (pass == RasterShader::Texel1 && !second)
                continue;
            if (pass == RasterShader::Shade && !shading)
                continue;
            if (pass == RasterShader::Alpha0 && !alpha)
                continue;
            if (pass == RasterShader::Depth && !depth_pass)
                continue;
            if(pass==RasterShader::Candidate && shading)continue;
            const bool guarded = pass == RasterShader::Alpha0 || pass == RasterShader::Texel0 ||
                                 pass == RasterShader::Texel1 || pass == RasterShader::LOD;
            auto &target = [&]() -> RasterTarget & {
                switch (pass) {
                case RasterShader::LOD:
                    return raster_targets_.at(lod_);
                case RasterShader::Texel0:
                    return raster_targets_.at(texel0_);
                case RasterShader::Texel1:
                    return raster_targets_.at(texel1_);
                case RasterShader::Alpha0:
                    return raster_targets_.at(alpha0_);
                case RasterShader::CoverageAlpha:
                    return raster_targets_.at(coverage_alpha_);
                case RasterShader::Candidate:
                case RasterShader::Shade:
                    return raster_targets_.at(combined_);
                case RasterShader::Depth:
                    return z;
                default:
                    return color;
                }
            }();
            glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

            glViewport(0, 0, target.width, target.height);
            glEnable(GL_SCISSOR_TEST);
            for (auto *packet : draws) {
                const auto &draw = *packet;
                if (pass == RasterShader::LOD &&
                    !(draw.constants[1][0] < 2 && draw.constants[1][3] > .5f &&
                      (draw.constants[89][0] > .5f || draw.constants[90][0] > .5f)))
                    continue;
                if ((pass == RasterShader::Texel0 || pass == RasterShader::Texel1) &&
                    (draw.constants[1][0] >= 2 || draw.constants[1][3] < .5f ||
                     (pass == RasterShader::Texel1 && draw.constants[1][0] != 1)))
                    continue;
                if (pass == RasterShader::Shade && draw.constants[1][0] >= 2)
                    continue;
                if (pass == RasterShader::Alpha0 &&
                    !(draw.constants[1][0] == 1 && (unsigned(draw.constants[27][3]) & 1)))
                    continue;
                auto constants = draw.constants;
                constants[0] = {float(color.width), float(color.height), float(color.tw), float(color.th)};
                constants[40] = {float(z.width), float(z.height), float(z.tw), float(z.th)};
                if (draw.constants[88][0] > .5f) {
                    auto &t = raster_targets_.at(draw.tiles[0]);
                    constants[91] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
                } else
                    for (unsigned i = 0; i < 2; i++) {
                        auto &t = raster_targets_.at(draw.tiles[i]);
                        constants[32 + i] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
                    }
                for (unsigned i = 0; i < 2; i++)
                    if (draw.auxiliary[i]) {
                        auto &t = raster_targets_.at(draw.auxiliary[i]);
                        constants[i ? 102 : 104] = {float(t.width), float(t.height), float(t.tw),
                                                    float(t.th)};
                    }
                if (textured || alpha) {
                    auto &t = raster_targets_.at(textured ? texel0_ : alpha0_);
                    constants[109] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
                }
                const std::array<GLuint, 12> inputs = {
                    color.feedback,
                    raster_targets_.at(draw.tiles[0]).color,
                    raster_targets_.at(draw.tiles[1]).color,
                    z.feedback,
                    pass == RasterShader::Depth ? 0 : z.color,
                    draw.auxiliary[0] ? raster_targets_.at(draw.auxiliary[0]).color : 0,
                    draw.auxiliary[1] ? raster_targets_.at(draw.auxiliary[1]).color : 0,
                    split && pass != RasterShader::Shade && pass != RasterShader::Candidate ? raster_targets_.at(combined_).color : 0,
                    alpha0_ && pass != RasterShader::Alpha0 ? raster_targets_.at(alpha0_).color : 0,
                    texel0_ && pass != RasterShader::Texel0 ? raster_targets_.at(texel0_).color : 0,
                    (pass == RasterShader::Texel0 || pass == RasterShader::Texel1)
                        ? 0
                        : (draw.constants[1][0] == 1 && texel1_
                               ? raster_targets_.at(texel1_).color
                               : (texel0_ ? raster_targets_.at(texel0_).color : 0)),
                    lod_ && pass != RasterShader::LOD ? raster_targets_.at(lod_).color : 0};
                constants[103] = {float(target.width), float(target.height), float(draw.scale), 0};
                constants[110] = {guarded ? -1.0f : 0.0f, 0, 0, 0};
                // Coverage is evaluated on the native RDP grid. Submit every
                // subpixel of each touched cell, including fractional AA edges.
                const float left = std::floor(draw.x0), top = std::floor(draw.y0),
                            right = std::ceil(draw.x1) + (guarded ? 2.0f : 0.0f), bottom = std::ceil(draw.y1);
                const int x0 = int(left * draw.scale), y0 = int(top * draw.scale),
                          x1 = int(right * draw.scale), y1 = int(bottom * draw.scale);
                glScissor(x0, target.height - y1, std::max(0, x1 - x0), std::max(0, y1 - y0));
                coverage_texture_=pass==RasterShader::CoverageAlpha?0:raster_targets_.at(coverage_alpha_).color;
                // Clear scratch acceptance and candidate data, including rejected fragments.
                if(pass!=RasterShader::Depth && pass!=RasterShader::Color){
                    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
                }
                raster_inputs(raster_program(pass, &draw), inputs, constants);
                raster_quad(left,top,right,bottom,draw.scale,target.width,target.height);
            }
        }
        GLenum error = glGetError();
        if (error)
            throw std::runtime_error("GLES2 GPU raster error " + std::to_string(error));
    }
    void texture_pass(const TextureDraw &draw) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto &t = raster_targets_.at(draw.target);
        auto constants = draw.constants;
        const std::array<unsigned, 3> info = {91, 92, 95};
        for (unsigned i = 0; i < 3; i++) {
            auto &s = raster_targets_.at(draw.inputs[i]);
            constants[info[i]] = {float(s.width), float(s.height), float(s.tw), float(s.th)};
        }
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);

        glViewport(0, 0, t.width, t.height);
        constants[103] = {float(t.width), float(t.height), 1, 0};
        for (unsigned i = 0; i < 2; i++)
            if (draw.auxiliary[i]) {
                auto &t = raster_targets_.at(draw.auxiliary[i]);
                constants[i ? 102 : 104] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
            }
        raster_inputs(raster_program(draw.shader),
                      {raster_targets_.at(draw.inputs[0]).color, raster_targets_.at(draw.inputs[1]).color,
                       raster_targets_.at(draw.inputs[2]).color, 0, 0,
                       draw.auxiliary[0] ? raster_targets_.at(draw.auxiliary[0]).color : 0,
                       draw.auxiliary[1] ? raster_targets_.at(draw.auxiliary[1]).color : 0},
                      constants);
        raster_quad(float(draw.x), float(draw.y), float(draw.x + draw.width), float(draw.y + draw.height), 1,
                    t.width, t.height);
        GLenum e = glGetError();
        if (e)
            throw std::runtime_error("GLES2 GPU TMEM error " + std::to_string(e));
    }
    void present_raster(const RasterPresent &present) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto &t = raster_targets_.at(present.color);
        auto constants = present.constants;
        constants[0] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(present.framebuffer));

        glViewport(0, 0, present.width, present.height);
        constants[103] = {float(present.width), float(present.height), 1, 0};
        raster_inputs(raster_program(RasterShader::Copy), {t.color, t.color, t.color, t.color, t.color},
                      constants);
        raster_quad(0, 0, float(present.width), float(present.height), 1, present.width, present.height);
    }
    void release_resources() override {
        end_raster();
        for (auto &[_, p] : raster_specialized_)
            if (p)
                glDeleteProgram(p);
        raster_specialized_.clear();
        raster_locations_.clear();
        while (!raster_targets_.empty())
            release(raster_targets_.begin()->first, 0);
        combined_ = alpha0_ = texel0_ = texel1_ = lod_ = coverage_alpha_ = 0;
        for (auto &p : raster_programs_) {
            if (p)
                glDeleteProgram(p);
            p = 0;
        }

        for (auto &p : programs_) {
            if (p)
                glDeleteProgram(p);
            p = 0;
        }
        if (framebuffer_)
            glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
        glDeleteTextures(1,&state_texture_);state_texture_=0;
        glDeleteBuffers(1,&vertex_buffer_);vertex_buffer_=0;
        glDeleteTextures(3, textures_.data());
        textures_ = {};
        width_ = height_ = 0;
        target_width_ = target_height_ = 0;
    }
    Image execute(Kernel kernel, const Image &a, const Image &b) override {
        validate_images(a, b, caps_.max_texture);
        const unsigned rows = kernel == Kernel::ColorPipeline ? a.height / 8 : a.height;
        if (!rows || (kernel == Kernel::ColorPipeline && a.height % 8))
            throw std::runtime_error("GLES2 invalid packet atlas");
        Guard guard(*this);
        unsigned w = storage_extent(a.width, caps_.padded_power_of_two),
                 h = storage_extent(a.height, caps_.padded_power_of_two);
        w = std::max(w, width_);
        h = std::max(h, height_);
        if (w > caps_.max_texture || h > caps_.max_texture)
            throw std::runtime_error("GLES2 padded texture exceeds caps");
        glActiveTexture(GL_TEXTURE0);
        if (!framebuffer_)
            glGenFramebuffers(1, &framebuffer_);
        if (!textures_[0])
            glGenTextures(3, textures_.data());
        if (width_ != w || height_ != h) {
            for (unsigned i = 0; i < 2; ++i) {
                GLuint texture = textures_[i];
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            width_ = w;
            height_ = h;
        }
        if (!presenting_ && (target_width_ != w || target_height_ < rows)) {
            target_width_ = w;
            target_height_ = std::max(target_height_, storage_extent(rows, caps_.padded_power_of_two));
            glBindTexture(GL_TEXTURE_2D, textures_[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_width_, target_height_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        for (unsigned i = 0; i < 2; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, a.width, a.height, GL_RGBA, GL_UNSIGNED_BYTE,
                            (i ? b : a).rgba.data());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, presenting_ ? present_fbo_ : framebuffer_);
        if (!presenting_)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[2],
                                    0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("GLES2 incomplete RGBA8 framebuffer");


        raster_state();
        glViewport(0, 0, presenting_ ? present_width_ : a.width, presenting_ ? present_height_ : rows);
        GLuint p = program(kernel);
        glUseProgram(p);
        glUniform1i(glGetUniformLocation(p, "plane0"), 0);
        glUniform1i(glGetUniformLocation(p, "plane1"), 1);
        glUniform4f(glGetUniformLocation(p, "dimensions"), 1.0f / w, 1.0f / h, float(a.width), float(rows));
        float u = float(a.width) / w, v = float(rows) / h;
        float v0 = presenting_ ? v : 0.0f, v1 = presenting_ ? 0.0f : v;
        vertices(GL_TRIANGLE_STRIP,{-1,-1,0,v0,1,-1,u,v0,-1,1,0,v1,1,1,u,v1});
        Image out{a.width, rows, {}};
        if (!presenting_) {
            out.rgba.resize(std::size_t(a.width) * rows * 4);
            glReadPixels(0, 0, a.width, rows, GL_RGBA, GL_UNSIGNED_BYTE, out.rgba.data());
        }
        GLenum error = glGetError();
        if (error)
            throw std::runtime_error("GLES2 draw/readback error " + std::to_string(error));
        return out;
    }
    void present(const Image &image, std::uintptr_t framebuffer, unsigned width, unsigned height) override {
        presenting_ = true;
        present_fbo_ = GLuint(framebuffer);
        present_width_ = width;
        present_height_ = height;
        try {
            execute(Kernel::Copy, image, image);
        } catch (...) {
            presenting_ = false;
            throw;
        }
        presenting_ = false;
    }
};
} // namespace
std::unique_ptr<Device> make_gles2(Resolver resolver) { return std::make_unique<GLES2>(resolver); }
} // namespace glrdp::legacy
