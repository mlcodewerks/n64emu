#include "legacy_gpu.hpp"
#include <GL/gl.h>
#include <GL/glext.h>
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
        throw std::runtime_error(std::string("GL2 missing function: ") + name);
    return p;
}
bool extension(const char *name) {
    const auto *list = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    const auto length = std::strlen(name);
    for (auto p = list; p && (p = std::strstr(p, name)); p += length)
        if ((p == list || p[-1] == ' ') && (p[length] == ' ' || p[length] == 0))
            return true;
    return false;
}
class GL2 final : public Device {
    Capabilities caps_;
#define FUNCTION(type, name) type name;
    FUNCTION(PFNGLCREATESHADERPROC, CreateShader)
    FUNCTION(PFNGLSHADERSOURCEPROC, ShaderSource)
    FUNCTION(PFNGLCOMPILESHADERPROC, CompileShader)
    FUNCTION(PFNGLGETSHADERIVPROC, GetShaderiv)
    FUNCTION(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)
    FUNCTION(PFNGLDELETESHADERPROC, DeleteShader)
    FUNCTION(PFNGLCREATEPROGRAMPROC, CreateProgram)
    FUNCTION(PFNGLATTACHSHADERPROC, AttachShader)
    FUNCTION(PFNGLLINKPROGRAMPROC, LinkProgram)
    FUNCTION(PFNGLGETPROGRAMIVPROC, GetProgramiv)
    FUNCTION(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)
    FUNCTION(PFNGLDELETEPROGRAMPROC, DeleteProgram)
    FUNCTION(PFNGLUSEPROGRAMPROC, UseProgram)
    FUNCTION(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)
    FUNCTION(PFNGLUNIFORM1IPROC, Uniform1i)
    FUNCTION(PFNGLUNIFORM4FPROC, Uniform4f)
    FUNCTION(PFNGLUNIFORM4FVPROC, Uniform4fv)
    FUNCTION(PFNGLACTIVETEXTUREPROC, ActiveTexture)
    FUNCTION(PFNGLBINDBUFFERPROC, BindBuffer)
    FUNCTION(PFNGLGENFRAMEBUFFERSEXTPROC, GenFramebuffersEXT)
    FUNCTION(PFNGLBINDFRAMEBUFFEREXTPROC, BindFramebufferEXT)
    FUNCTION(PFNGLDELETEFRAMEBUFFERSEXTPROC, DeleteFramebuffersEXT)
    FUNCTION(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, FramebufferTexture2DEXT)
    FUNCTION(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, CheckFramebufferStatusEXT)
#undef FUNCTION
    std::array<GLuint, 6> programs_{};
    std::array<GLuint, 3> textures_{};
    GLuint framebuffer_ = 0;
    unsigned width_ = 0, height_ = 0;
    unsigned target_width_ = 0, target_height_ = 0;
    bool pbo_ = false, srgb_ = false;
    PFNGLBINDSAMPLERPROC BindSamplerOptional = nullptr;
    PFNGLBINDFRAMEBUFFERPROC BindFramebufferSplit = nullptr;
    bool presenting_ = false;
    GLuint present_fbo_ = 0;
    unsigned present_width_ = 0, present_height_ = 0;
    struct Guard {
        GL2 &g;
        GLint program = 0, framebuffer = 0, read_framebuffer = 0, active = 0, pack = 0, unpack = 0;
        std::array<GLint, 12> samplers{};
        Guard(GL2 &gpu) : g(gpu) {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
            if (g.BindFramebufferSplit)
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glPushAttrib(GL_ALL_ATTRIB_BITS);
            glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
            if (g.BindSamplerOptional)
                for (unsigned i = 0; i < 12; ++i) {
                    g.ActiveTexture(GL_TEXTURE0 + i);
                    glGetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
                    g.BindSamplerOptional(i, 0);
                }
            if (g.pbo_) {
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
                g.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                g.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            }
            for (GLenum p : {GL_PACK_ALIGNMENT, GL_UNPACK_ALIGNMENT})
                glPixelStorei(p, 1);
            for (GLenum p : {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_UNPACK_ROW_LENGTH,
                             GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS})
                glPixelStorei(p, 0);
            for (GLenum scale : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE})
                glPixelTransferf(scale, 1);
            for (GLenum bias : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS})
                glPixelTransferf(bias, 0);
            glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
        }
        ~Guard() {
            g.UseProgram(program);
            if (g.BindFramebufferSplit) {
                g.BindFramebufferSplit(GL_DRAW_FRAMEBUFFER, framebuffer);
                g.BindFramebufferSplit(GL_READ_FRAMEBUFFER, read_framebuffer);
            } else
                g.BindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
            glPopClientAttrib();
            glPopAttrib();
            if (g.BindSamplerOptional)
                for (unsigned i = 0; i < 12; ++i)
                    g.BindSamplerOptional(i, samplers[i]);
            if (g.pbo_) {
                g.BindBuffer(GL_PIXEL_PACK_BUFFER, pack);
                g.BindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
            }
            g.ActiveTexture(active);
        }
    };
    struct RasterTarget {
        GLuint color = 0, feedback = 0, fbo = 0;
        int width = 0, height = 0, tw = 0, th = 0;
    };
    unsigned combined_ = 0, alpha0_ = 0, texel0_ = 0, texel1_ = 0, lod_ = 0;
    std::unordered_map<unsigned, RasterTarget> raster_targets_;
    std::array<GLuint, 13> raster_programs_{};
    std::map<std::pair<RasterShader, RasterKey>, GLuint> raster_specialized_;
    std::unordered_map<GLuint, std::array<GLint, 13>> raster_locations_;
    std::unique_ptr<Guard> raster_guard_;
    GLuint raster_program(RasterShader pass, const RasterDraw *specialize = nullptr) {
        auto &id = specialize ? raster_specialized_[{pass, raster_key(*specialize)}]
                              : raster_programs_[unsigned(pass)];
        if (id)
            return id;
        const auto source = raster_fragment_shader(pass, false, specialize);
        GLuint vs =
            shader(GL_VERTEX_SHADER,
                   "#version 110\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}");
        GLuint fs = shader(GL_FRAGMENT_SHADER, source);
        id = CreateProgram();
        AttachShader(id, vs);
        AttachShader(id, fs);
        LinkProgram(id);
        DeleteShader(vs);
        DeleteShader(fs);
        GLint ok = 0;
        GetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char message[8192]{};
            GetProgramInfoLog(id, sizeof(message), nullptr, message);
            throw std::runtime_error(std::string("GL2 RDP link: ") + message);
        }
        return id;
    }
    void raster_state() {
        for (GLenum v :
             {GL_BLEND, GL_ALPHA_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST,
              GL_DITHER, GL_COLOR_LOGIC_OP, GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE})
            glDisable(v);
        for (unsigned i = 0; i < 6; i++)
            glDisable(GL_CLIP_PLANE0 + i);
        if (srgb_)
            glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    void snapshot(RasterTarget &t, const RasterDraw &draw) {
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, t.fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        ActiveTexture(GL_TEXTURE0);
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
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(x0, y0);
        glVertex2f(left, top);
        glTexCoord2f(x1, y0);
        glVertex2f(right, top);
        glTexCoord2f(x0, y1);
        glVertex2f(left, bottom);
        glTexCoord2f(x1, y1);
        glVertex2f(right, bottom);
        glEnd();
    }
    void raster_inputs(GLuint p, const std::array<GLuint, 12> &textures,
                       const std::array<Float4, 112> &constants) {
        auto [it, inserted] = raster_locations_.try_emplace(p);
        if (inserted) {
            it->second[0] = GetUniformLocation(p, "c[0]");
            const char *names[] = {"memory_color", "tile0",     "tile1",          "old_depth",
                                   "new_depth",    "noise_tex", "reciprocal_tex", "combined_color",
                                   "first_alpha",  "cached0",   "cached1",        "cached_lod"};
            for (unsigned i = 0; i < 12; i++)
                it->second[i + 1] = GetUniformLocation(p, names[i]);
        }
        const auto &locations = it->second;
        UseProgram(p);
        Uniform4fv(locations[0], 112, constants[0].data());
        for (unsigned i = 0; i < 12; i++) {
            ActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            Uniform1i(locations[i + 1], i);
        }
    }
    GLuint shader(GLenum type, const std::string &source) {
        GLuint s = CreateShader(type);
        const char *text = source.c_str();
        ShaderSource(s, 1, &text, nullptr);
        CompileShader(s);
        GLint ok = 0;
        GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char message[4096]{};
            GetShaderInfoLog(s, sizeof(message), nullptr, message);
            DeleteShader(s);
            throw std::runtime_error(std::string("GL2 shader: ") + message);
        }
        return s;
    }
    GLuint program(Kernel k) {
        auto &id = programs_.at(unsigned(k));
        if (id)
            return id;
        const GLuint vs = shader(
            GL_VERTEX_SHADER,
            "#version 110\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}\n");
        GLuint fs = 0, p = 0;
        try {
            fs = shader(GL_FRAGMENT_SHADER, fragment_shader(k, false));
            p = CreateProgram();
            AttachShader(p, vs);
            AttachShader(p, fs);
            LinkProgram(p);
            GLint ok = 0;
            GetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) {
                char message[4096]{};
                GetProgramInfoLog(p, sizeof(message), nullptr, message);
                throw std::runtime_error(std::string("GL2 shader link: ") + message);
            }
        } catch (...) {
            DeleteShader(vs);
            if (fs)
                DeleteShader(fs);
            if (p)
                DeleteProgram(p);
            throw;
        }
        DeleteShader(vs);
        DeleteShader(fs);
        id = p;
        return id;
    }

  public:
    GL2(Resolver resolver, bool pot) {
#define LOAD(name) name = resolve<decltype(name)>(resolver, "gl" #name)
        LOAD(CreateShader);
        LOAD(ShaderSource);
        LOAD(CompileShader);
        LOAD(GetShaderiv);
        LOAD(GetShaderInfoLog);
        LOAD(DeleteShader);
        LOAD(CreateProgram);
        LOAD(AttachShader);
        LOAD(LinkProgram);
        LOAD(GetProgramiv);
        LOAD(GetProgramInfoLog);
        LOAD(DeleteProgram);
        LOAD(UseProgram);
        LOAD(GetUniformLocation);
        LOAD(Uniform1i);
        LOAD(Uniform4f);
        LOAD(ActiveTexture);
        LOAD(BindBuffer);
        LOAD(GenFramebuffersEXT);
        LOAD(BindFramebufferEXT);
        LOAD(DeleteFramebuffersEXT);
        LOAD(FramebufferTexture2DEXT);
        LOAD(CheckFramebufferStatusEXT);
#undef LOAD
        if (!extension("GL_EXT_framebuffer_object"))
            throw std::runtime_error("GL2 requires EXT_framebuffer_object");
        caps_.device = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        caps_.device += " / ";
        caps_.device += reinterpret_cast<const char *>(glGetString(GL_VERSION));
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        Uniform4fv = resolve<PFNGLUNIFORM4FVPROC>(resolver, "glUniform4fv");
        caps_.max_texture = limit;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &limit);
        caps_.fragment_samplers = limit;
        caps_.separate_shading = true;
        if (limit < 12)
            throw std::runtime_error("GL2 RDP requires twelve fragment samplers");
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &limit);
        if (limit < 448)
            throw std::runtime_error("GL2 RDP requires 112 fragment uniform vectors");
        caps_.padded_power_of_two = pot || !extension("GL_ARB_texture_non_power_of_two");
        // PBOs are optional in 2.0 and core in 2.1. Use the extension bit rather
        // than calling unsupported buffer targets on 2.0-only implementations.
        pbo_ = extension("GL_ARB_pixel_buffer_object") || extension("GL_EXT_pixel_buffer_object");
        srgb_ = extension("GL_ARB_framebuffer_sRGB") || extension("GL_EXT_framebuffer_sRGB");
        if (extension("GL_ARB_sampler_objects"))
            BindSamplerOptional = resolve<PFNGLBINDSAMPLERPROC>(resolver, "glBindSampler");
        if (extension("GL_ARB_framebuffer_object"))
            BindFramebufferSplit = resolve<PFNGLBINDFRAMEBUFFERPROC>(resolver, "glBindFramebuffer");
        else if (extension("GL_EXT_framebuffer_blit"))
            BindFramebufferSplit = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(BindFramebufferEXT);
    }
    ~GL2() override { release_resources(); }
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
        if (t.tw > int(caps_.max_texture) || t.th > int(caps_.max_texture))
            throw std::runtime_error("GL2 RDP target exceeds caps");
        glGenTextures(1, &t.color);
        glGenTextures(1, &t.feedback);
        GenFramebuffersEXT(1, &t.fbo);
        ActiveTexture(GL_TEXTURE0);
        for (auto id : {t.color, t.feedback}) {
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.tw, t.th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, t.fbo);
        FramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, t.color, 0);
        if (CheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
            throw std::runtime_error("GL2 RDP framebuffer incomplete");
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
        DeleteFramebuffersEXT(1, &t.fbo);
        raster_targets_.erase(it);
    }
    void upload(unsigned color, int width, int height, const std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        ActiveTexture(GL_TEXTURE0);
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
        ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t.color);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, t.height - y - height, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                        bytes.data());
    }
    void read(unsigned fbo, int x, int y, int width, int height, std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, raster_targets_.at(fbo).fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    void draw_raster(const RasterDraw &draw) override { draw_batch({&draw}); }
    void draw_batch(const std::vector<const RasterDraw *> &draws) override {
        if (draws.empty())
            return;
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto &first = *draws.front();
        auto &color = raster_targets_.at(first.color), &z = raster_targets_.at(first.depth);
        const bool depth_pass = first.constants[1][0] < 2 || first.constants[45][0] > .5f;
        bool shading = false;
        for (auto *d : draws)
            shading |= d->constants[1][0] < 2;
        const bool split = caps_.separate_shading && shading;
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
                          RasterShader::Alpha0, RasterShader::Depth, RasterShader::Color}) {
            if (pass == RasterShader::LOD && !lod)
                continue;
            if (pass == RasterShader::Texel0 && !textured)
                continue;
            if (pass == RasterShader::Texel1 && !second)
                continue;
            if (pass == RasterShader::Shade && !split)
                continue;
            if (pass == RasterShader::Alpha0 && !alpha)
                continue;
            if (pass == RasterShader::Depth && !depth_pass)
                continue;
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
                case RasterShader::Shade:
                    return raster_targets_.at(combined_);
                case RasterShader::Depth:
                    return z;
                default:
                    return color;
                }
            }();
            BindFramebufferEXT(GL_FRAMEBUFFER_EXT, target.fbo);
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
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
                    split && pass != RasterShader::Shade ? raster_targets_.at(combined_).color : 0,
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
                raster_inputs(raster_program(pass, &draw), inputs, constants);
                static const bool boxes = std::getenv("GLRDP_LEGACY_BOUNDING_BOX") != nullptr;
                if (draw.vertices.empty() || boxes || guarded)
                    raster_quad(left, top, right, bottom, draw.scale, target.width, target.height);
                else {
                    glBegin(GL_TRIANGLES);
                    for (auto v : draw.vertices)
                        glVertex2f(v[0] * draw.scale * 2.0f / target.width - 1.0f,
                                   1.0f - v[1] * draw.scale * 2.0f / target.height);
                    glEnd();
                }
            }
        }
        GLenum error = glGetError();
        if (error)
            throw std::runtime_error("GL2 GPU raster error " + std::to_string(error));
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
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, t.fbo);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
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
            throw std::runtime_error("GL2 GPU TMEM error " + std::to_string(e));
    }
    void present_raster(const RasterPresent &present) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto &t = raster_targets_.at(present.color);
        auto constants = present.constants;
        constants[0] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, GLuint(present.framebuffer));
        glDrawBuffer(present.framebuffer ? GL_COLOR_ATTACHMENT0_EXT : GL_BACK);
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
                DeleteProgram(p);
        raster_specialized_.clear();
        raster_locations_.clear();
        while (!raster_targets_.empty())
            release(raster_targets_.begin()->first, 0);
        combined_ = alpha0_ = texel0_ = texel1_ = lod_ = 0;
        for (auto &p : raster_programs_) {
            if (p)
                DeleteProgram(p);
            p = 0;
        }

        for (auto &p : programs_) {
            if (p)
                DeleteProgram(p);
            p = 0;
        }
        if (framebuffer_)
            DeleteFramebuffersEXT(1, &framebuffer_);
        framebuffer_ = 0;
        glDeleteTextures(3, textures_.data());
        textures_ = {};
        width_ = height_ = 0;
        target_width_ = target_height_ = 0;
    }
    Image execute(Kernel kernel, const Image &a, const Image &b) override {
        validate_images(a, b, caps_.max_texture);
        const unsigned rows = kernel == Kernel::ColorPipeline ? a.height / 8 : a.height;
        if (!rows || (kernel == Kernel::ColorPipeline && a.height % 8))
            throw std::runtime_error("GL2 invalid packet atlas");
        Guard guard(*this);
        unsigned w = storage_extent(a.width, caps_.padded_power_of_two),
                 h = storage_extent(a.height, caps_.padded_power_of_two);
        w = std::max(w, width_);
        h = std::max(h, height_);
        if (w > caps_.max_texture || h > caps_.max_texture)
            throw std::runtime_error("GL2 padded texture exceeds caps");
        ActiveTexture(GL_TEXTURE0);
        if (!framebuffer_)
            GenFramebuffersEXT(1, &framebuffer_);
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
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            width_ = w;
            height_ = h;
        }
        if (!presenting_ && (target_width_ != w || target_height_ < rows)) {
            target_width_ = w;
            target_height_ = std::max(target_height_, storage_extent(rows, caps_.padded_power_of_two));
            glBindTexture(GL_TEXTURE_2D, textures_[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, target_width_, target_height_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        for (unsigned i = 0; i < 2; ++i) {
            ActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, a.width, a.height, GL_RGBA, GL_UNSIGNED_BYTE,
                            (i ? b : a).rgba.data());
        }
        BindFramebufferEXT(GL_FRAMEBUFFER_EXT, presenting_ ? present_fbo_ : framebuffer_);
        if (!presenting_)
            FramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, textures_[2],
                                    0);
        if (CheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
            throw std::runtime_error("GL2 incomplete RGBA8 framebuffer");
        glDrawBuffer(presenting_ && !present_fbo_ ? GL_BACK : GL_COLOR_ATTACHMENT0_EXT);
        glReadBuffer(presenting_ && !present_fbo_ ? GL_BACK : GL_COLOR_ATTACHMENT0_EXT);
        for (GLenum s :
             {GL_BLEND, GL_ALPHA_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST,
              GL_DITHER, GL_COLOR_LOGIC_OP, GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE})
            glDisable(s);
        for (unsigned i = 0; i < 6; ++i)
            glDisable(GL_CLIP_PLANE0 + i);
        if (srgb_)
            glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glViewport(0, 0, presenting_ ? present_width_ : a.width, presenting_ ? present_height_ : rows);
        GLuint p = program(kernel);
        UseProgram(p);
        Uniform1i(GetUniformLocation(p, "plane0"), 0);
        Uniform1i(GetUniformLocation(p, "plane1"), 1);
        Uniform4f(GetUniformLocation(p, "dimensions"), 1.0f / w, 1.0f / h, float(a.width), float(rows));
        float u = float(a.width) / w, v = float(rows) / h;
        float v0 = presenting_ ? v : 0.0f, v1 = presenting_ ? 0.0f : v;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, v0);
        glVertex2f(-1, -1);
        glTexCoord2f(u, v0);
        glVertex2f(1, -1);
        glTexCoord2f(0, v1);
        glVertex2f(-1, 1);
        glTexCoord2f(u, v1);
        glVertex2f(1, 1);
        glEnd();
        Image out{a.width, rows, {}};
        if (!presenting_) {
            out.rgba.resize(std::size_t(a.width) * rows * 4);
            glReadPixels(0, 0, a.width, rows, GL_RGBA, GL_UNSIGNED_BYTE, out.rgba.data());
        }
        GLenum error = glGetError();
        if (error)
            throw std::runtime_error("GL2 draw/readback error " + std::to_string(error));
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
std::unique_ptr<Device> make_gl2(Resolver resolver, bool pot) { return std::make_unique<GL2>(resolver, pot); }
} // namespace glrdp::legacy
