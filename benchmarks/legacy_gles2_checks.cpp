#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "../src/device/rcp/rdp/legacygpu/legacy_gpu.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace glrdp::legacy;
static void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static GLuint compile(GLenum type, const std::string &source) {
    auto id = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(id, 1, &text, nullptr);
    glCompileShader(id);
    GLint ok=0; glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[8192]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);
        glDeleteShader(id);throw std::runtime_error(log);
    }
    return id;
}
static void check_shader(const std::string &source) {
    const auto vs=compile(GL_VERTEX_SHADER,R"(#version 100
precision highp float;
attribute vec2 position;
varying highp vec2 pixel_position;
void main(){gl_Position=vec4(position,0.0,1.0);pixel_position=position;}
)");
    const auto fs=compile(GL_FRAGMENT_SHADER,source);
    auto p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[8192]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(log);}
    GLint count=0;glGetProgramiv(p,GL_ACTIVE_UNIFORMS,&count);
    unsigned samplers=0,vectors=0;
    for(GLint i=0;i<count;++i){char name[256];GLint size;GLenum type;
        glGetActiveUniform(p,i,sizeof(name),nullptr,&size,&type,name);
        vectors+=size; if(type==GL_SAMPLER_2D)samplers+=size;
    }
    require(samplers<=8,"shader exceeds eight samplers");
    require(vectors<=16,"shader exceeds sixteen uniform vectors");
    glDeleteProgram(p);
}
static Proc resolver(const char *name) {return reinterpret_cast<Proc>(eglGetProcAddress(name));}
int main(int argc, char **argv) {
    try {
        auto display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major=0,minor=0;
        require(eglInitialize(display,&major,&minor),"EGL initialization failed");
        require(eglBindAPI(EGL_OPENGL_ES_API),"EGL ES binding failed");
        const EGLint config_attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config;EGLint count;
        require(eglChooseConfig(display,config_attributes,&config,1,&count)&&count,"EGL config unavailable");
        const EGLint surface_attributes[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
        auto surface=eglCreatePbufferSurface(display,config,surface_attributes);
        const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);
        require(eglMakeCurrent(display,surface,surface,context),"EGL make current failed");
        std::printf("%s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
        const bool device_only=argc>1&&std::string(argv[1])=="--device-only";
        for(unsigned i=0;!device_only&&i<15;++i){
            std::printf("shader %u\n",i);std::fflush(stdout);
            RasterDraw copy;copy.constants[1][0]=2;
            check_shader(raster_fragment_shader(RasterShader(i),ShaderDialect::GLSLES100,
                         i==unsigned(RasterShader::Candidate)?&copy:nullptr));
        }
        auto device=make_gles2(resolver);
        Image a{256,1,{}};a.rgba.resize(1024);
        for(unsigned i=0;i<1024;++i)a.rgba[i]=std::uint8_t(i*37);
        require(device->execute(Kernel::Copy,a,a).rgba==a.rgba,"copy mismatch");
        const bool es3=std::strstr(reinterpret_cast<const char*>(glGetString(GL_VERSION)),"OpenGL ES 3.")!=nullptr;
        GLuint pbos[2]{},sampler=0;
        using Samplers=void(GL_APIENTRY *)(GLsizei,GLuint*);
        using BindSampler=void(GL_APIENTRY *)(GLuint,GLuint);
        auto gen_samplers=reinterpret_cast<Samplers>(eglGetProcAddress("glGenSamplers"));
        auto delete_samplers=reinterpret_cast<Samplers>(eglGetProcAddress("glDeleteSamplers"));
        auto bind_sampler=reinterpret_cast<BindSampler>(eglGetProcAddress("glBindSampler"));
        if(es3){
            glGenBuffers(2,pbos);
            for(unsigned i=0;i<2;++i){glBindBuffer(i?0x88ec:0x88eb,pbos[i]);glBufferData(i?0x88ec:0x88eb,64,nullptr,GL_DYNAMIC_DRAW);}
            glPixelStorei(0x0d02,23);glPixelStorei(0x0cf2,23);
            gen_samplers(1,&sampler);bind_sampler(0,sampler);
        }
        glViewport(3,4,17,19);glEnable(GL_BLEND);glActiveTexture(GL_TEXTURE3);
        unsigned color=0,fbo=0;device->allocate(color,fbo,16,16);
        std::vector<std::uint8_t> pixels(16*16*4,0);
        device->upload(color,16,16,pixels.data());
        std::vector<std::uint8_t> readback(pixels.size(),255);
        device->read(fbo,0,0,16,16,readback.data());
        require(readback==pixels,"storage mismatch");
        GLint viewport[4],active;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        require(viewport[0]==3&&viewport[1]==4&&viewport[2]==17&&viewport[3]==19&&
                glIsEnabled(GL_BLEND)&&active==GL_TEXTURE3,"frontend state changed");
        if(es3){
            GLint value;glGetIntegerv(0x88ed,&value);require(GLuint(value)==pbos[0],"pack PBO changed");
            glGetIntegerv(0x88ef,&value);require(GLuint(value)==pbos[1],"unpack PBO changed");
            glGetIntegerv(0x0d02,&value);require(value==23,"pack row length changed");
            glGetIntegerv(0x0cf2,&value);require(value==23,"unpack row length changed");
            glActiveTexture(GL_TEXTURE0);glGetIntegerv(0x8919,&value);require(GLuint(value)==sampler,"sampler changed");
            bind_sampler(0,0);delete_samplers(1,&sampler);
            glBindBuffer(0x88eb,0);glBindBuffer(0x88ec,0);glDeleteBuffers(2,pbos);
            glPixelStorei(0x0d02,0);glPixelStorei(0x0cf2,0);
        }
        unsigned depth=0,depth_fbo=0,white=0,white_fbo=0;
        device->allocate(depth,depth_fbo,16,16);
        device->allocate(white,white_fbo,1,1);
        const std::uint8_t white_pixel[]={255,255,255,255};device->upload(white,1,1,white_pixel);
        RasterDraw draw;draw.color=color;draw.depth=depth;draw.tiles={white,white};
        draw.x1=draw.y1=16;draw.constants[1]={3,0,0,0};
        draw.constants[2]={0,0,16,16};draw.constants[29]={0,0,1,3};
        draw.constants[30]=draw.constants[31]={71,83,99,255};
        device->draw_raster(draw);device->read(fbo,0,0,16,16,readback.data());
        for(unsigned i=0;i<readback.size();++i)
            require(readback[i]==std::uint8_t(draw.constants[30][i%4]),"fill raster mismatch");
        const auto filled=readback;
        draw.constants[1][0]=0;draw.constants[15]={19,31,47,64};
        draw.constants[18][3]=128;draw.constants[21]=draw.constants[22]={0,0,0,3};
        draw.constants[26][0]=1;draw.constants[27][3]=1;draw.constants[28]={3,3,0,0};
        device->draw_raster(draw);device->read(fbo,0,0,16,16,readback.data());
        require(readback==filled,"alpha-rejected draw changed color");
        draw.constants[15][3]=255;
        device->draw_raster(draw);device->read(fbo,0,0,16,16,readback.data());
        for(unsigned i=0;i<readback.size();++i)
            require(readback[i]==(i%4==3?224:std::uint8_t(draw.constants[15][i%4])),"one-cycle raster mismatch");
        // Reject after an accepted primitive to catch stale scratch acceptance.
        draw.constants[15]={200,201,202,0};device->draw_raster(draw);
        std::vector<std::uint8_t> after(readback.size());device->read(fbo,0,0,16,16,after.data());
        require(after==readback,"stale alpha metadata accepted a rejected draw");
        draw.constants[1][0]=1;draw.constants[15]={19,31,47,255};
        draw.constants[19]=draw.constants[20]={0,0,0,3};
        device->draw_raster(draw);device->read(fbo,0,0,16,16,after.data());
        require(after==readback,"two-cycle/neighbor alpha raster mismatch");
        draw.constants[1]={2,0,0,1};draw.constants[27][3]=0;
        draw.constants[11]={0,0,0,0};draw.constants[12]={0,0,0,0};draw.constants[13]={1,0,0,0};
        device->draw_raster(draw);device->read(fbo,0,0,16,16,after.data());
        for(unsigned i=0;i<after.size();++i)
            require(after[i]==(i%4==3?224:255),"copy-cycle raster mismatch");
        for(int scale:{2,4}){
            unsigned scaled=0,scaled_fbo=0,scaled_depth=0,scaled_depth_fbo=0;
            device->allocate(scaled,scaled_fbo,16*scale,16*scale);
            device->allocate(scaled_depth,scaled_depth_fbo,16*scale,16*scale);
            draw.color=scaled;draw.depth=scaled_depth;draw.scale=scale;
            draw.constants[1]={3,0,0,0};draw.constants[29][2]=float(scale);
            device->draw_raster(draw);
            std::vector<std::uint8_t> scaled_pixels(16*scale*16*scale*4);
            device->read(scaled_fbo,0,0,16*scale,16*scale,scaled_pixels.data());
            for(unsigned i=0;i<scaled_pixels.size();++i)
                require(scaled_pixels[i]==std::uint8_t(draw.constants[30][i%4]),"upscaled fill mismatch");
            device->release(scaled,scaled_fbo);device->release(scaled_depth,scaled_depth_fbo);
        }
        device->release(color,fbo);device->release(depth,depth_fbo);device->release(white,white_fbo);device.reset();
        require(glGetError()==GL_NO_ERROR,"GL error after device checks");
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
        std::puts("GLES2 checks passed");return 0;
    } catch(const std::exception &e){std::fprintf(stderr,"GLES2 checks: %s\n",e.what());return 1;}
}
