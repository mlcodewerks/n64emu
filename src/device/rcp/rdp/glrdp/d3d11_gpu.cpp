#include "d3d11_gpu.hpp"
bool glrdp_dx11 = false;
#ifdef HAVE_D3D11
#include "d3d11_shaders.hpp"
#include "gl_loader.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <d3d11_1.h>
#include <libretro_d3d.h>
#include <memory>
#include <stdexcept>
#include <unordered_map>

// This private command adapter shares the RDP implementation with GL. All GPU
// resources and commands here are native D3D11; no OpenGL context is created.
namespace {
using namespace glrdp::dx11;
struct Texture {
    ComPtr<ID3D11Texture2D> image;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    unsigned width = 0, height = 0, bytes = 4;
    GLenum min_filter = GL_NEAREST, mag_filter = GL_NEAREST;
    GLenum wrap_s = GL_CLAMP_TO_EDGE, wrap_t = GL_CLAMP_TO_EDGE;
};
struct Framebuffer {
    std::array<GLuint, 8> colors{};
    GLuint depth = 0;
    std::array<GLenum, 8> draw{GL_COLOR_ATTACHMENT0};
    GLenum read = GL_COLOR_ATTACHMENT0;
};
struct Shader {
    GLenum type = 0;
    std::string source;
};
struct LinkedProgram : Program {
    std::vector<GLuint> shaders;
};
struct StreamBuffer {
    ComPtr<ID3D11Buffer> resource;
    size_t bytes = 0;
    bool discard = true;
};
struct Backend {
    HMODULE compiler_library = nullptr;
    pD3DCompile compiler = nullptr;
    ~Backend() {
        if (compiler_library)
            FreeLibrary(compiler_library);
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> core_state, frontend_state;
    ComPtr<ID3D11Buffer> batch_constants;
    ComPtr<ID3D11RasterizerState> raster_scissor, raster_full;
    ComPtr<ID3D11DepthStencilState> no_depth;
    ComPtr<ID3D11BlendState> write_all, write_none;
    std::unordered_map<unsigned, Texture> textures;
    std::unordered_map<unsigned, Framebuffer> framebuffers;
    std::unordered_map<unsigned, std::vector<unsigned char>> buffers;
    std::unordered_map<unsigned, StreamBuffer> stream_buffers;
    bool range_constants = false;
    uint64_t ranged_draws = 0, copied_draws = 0;
    std::unordered_map<unsigned, Shader> shaders;
    std::unordered_map<unsigned, LinkedProgram> programs;
    std::unordered_map<std::string, Program> compiled_programs;
    std::unordered_map<unsigned, ComPtr<ID3D11SamplerState>> samplers;
    std::unordered_map<GLenum, GLuint> buffer_bindings;
    std::array<GLuint, 16> units{};
    GLuint next = 1, active = 0, program = 0, draw = 0, read = 0;
    GLuint uniform_buffer = 0;
    size_t uniform_offset = 0, uniform_size = 0;
    std::array<int, 4> viewport{0, 0, 640, 480}, scissor{0, 0, 640, 480};
    std::array<float, 4> clear_color{};
    bool scissor_enabled = false, color_write = true;
    int pack = 4, unpack = 4;
    unsigned guards = 0;
    retro_hw_render_interface_d3d11 frontend{};
    std::unordered_map<unsigned, GLuint> present_textures;
    GLuint last_present = 0;
    unsigned pending_sync_index = 0;
    bool prepare_target = false;
};
std::unique_ptr<Backend> backend;
Backend &b() {
    if (!backend)
        throw std::runtime_error("DX11 context unavailable");
    return *backend;
}
Texture &texture() { return b().textures.at(b().units[b().active]); }
Texture &attachment(Framebuffer &f, GLenum point) {
    return b().textures.at(point == GL_DEPTH_ATTACHMENT ? f.depth
                                                        : f.colors.at(point - GL_COLOR_ATTACHMENT0));
}
void unbind() {
    std::array<ID3D11ShaderResourceView *, 16> empty{};
    b().context->PSSetShaderResources(0, UINT(empty.size()), empty.data());
    b().context->VSSetShaderResources(0, UINT(empty.size()), empty.data());
    b().context->OMSetRenderTargets(0, nullptr, nullptr);
}
unsigned row_size(unsigned bytes, unsigned alignment) { return (bytes + alignment - 1) & ~(alignment - 1); }
DXGI_FORMAT format(GLint internal, unsigned &bytes) {
    switch (internal) {
    case GL_RGBA:
    case GL_RGBA8:
        bytes = 4;
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case GL_RGBA8UI:
        bytes = 4;
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case GL_RGBA32UI:
        bytes = 16;
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case GL_R8UI:
        bytes = 1;
        return DXGI_FORMAT_R8_UINT;
    case GL_R32UI:
        bytes = 4;
        return DXGI_FORMAT_R32_UINT;
    // RDP depth tests use the integer Z/DZ texture. The GL compatibility depth
    // attachment is never sampled and its fixed-function compare is ALWAYS.
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
        bytes = 4;
        return DXGI_FORMAT_R32_FLOAT;
    default:
        throw std::runtime_error("DX11 unsupported RDP texture format");
    }
}
void allocate(Texture &t, unsigned width, unsigned height, GLint internal, const void *pixels,
              unsigned alignment) {
    unbind();
    t.rtv.Reset();
    t.srv.Reset();
    t.image.Reset();
    t.format = format(internal, t.bytes);
    t.width = width;
    t.height = height;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = t.format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels;
    data.SysMemPitch = row_size(width * t.bytes, alignment);
    check(b().device->CreateTexture2D(&desc, pixels ? &data : nullptr, &t.image), "create texture");
    check(b().device->CreateShaderResourceView(t.image.Get(), nullptr, &t.srv), "create texture view");
    check(b().device->CreateRenderTargetView(t.image.Get(), nullptr, &t.rtv), "create render target");
}
ID3D11SamplerState *sampler(const Texture &t) {
    const unsigned key = unsigned(t.min_filter == GL_LINEAR) | (unsigned(t.mag_filter == GL_LINEAR) << 1) |
                         (unsigned(t.wrap_s == GL_REPEAT) << 2) | (unsigned(t.wrap_t == GL_REPEAT) << 3);
    auto &s = b().samplers[key];
    if (!s) {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER(
            key & 2 ? (key & 1 ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
                               : D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT)
                    : (key & 1 ? D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT));
        desc.AddressU = key & 4 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = key & 8 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        check(b().device->CreateSamplerState(&desc, &s), "create sampler");
    }
    return s.Get();
}
void upload_constants(ID3D11Buffer *buffer, const void *data, size_t size) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(b().context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "map constants");
    std::memcpy(mapped.pData, data, size);
    b().context->Unmap(buffer, 0);
}
void draw(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    auto &g = b();
    auto &p = g.programs.at(g.program);
    if (!p.linked)
        throw std::runtime_error(p.error);
    unbind();
    auto &f = g.framebuffers[g.draw];
    std::array<ID3D11RenderTargetView *, 8> targets{};
    unsigned target_count = 0;
    for (unsigned i = 0; i < f.draw.size(); ++i)
        if (f.draw[i] != GL_NONE) {
            auto &t = attachment(f, f.draw[i]);
            targets[i] = t.rtv.Get();
            target_count = i + 1;
        }
    g.context->OMSetRenderTargets(target_count, targets.data(), nullptr);
    g.context->OMSetDepthStencilState(g.no_depth.Get(), 0);
    g.context->OMSetBlendState(g.color_write ? g.write_all.Get() : g.write_none.Get(), nullptr, ~0u);
    g.context->RSSetState(g.scissor_enabled ? g.raster_scissor.Get() : g.raster_full.Get());
    const auto &v = g.viewport;
    const D3D11_VIEWPORT viewport{float(v[0]), float(v[1]), float(v[2]), float(v[3]), 0, 1};
    g.context->RSSetViewports(1, &viewport);
    const auto &s = g.scissor;
    const D3D11_RECT scissor{s[0], s[1], s[0] + s[2], s[1] + s[3]};
    g.context->RSSetScissorRects(1, &scissor);
    g.context->IASetInputLayout(nullptr);
    g.context->IASetPrimitiveTopology(mode == GL_TRIANGLE_STRIP ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                                                                : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.context->VSSetShader(g.draw ? p.vertex.Get() : p.present_vertex.Get(), nullptr, 0);
    g.context->PSSetShader(p.pixel.Get(), nullptr, 0);
    g.context->GSSetShader(nullptr, nullptr, 0);
    g.context->HSSetShader(nullptr, nullptr, 0);
    g.context->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer *primitive_constants = g.batch_constants.Get();
    const auto stream = g.stream_buffers.find(g.uniform_buffer);
    const bool ranged = g.range_constants && stream != g.stream_buffers.end() && stream->second.resource;
    UINT first_constant = UINT(g.uniform_offset / 16);
    UINT constant_count = UINT((g.uniform_size + 255) / 256 * 16);
    if (g.uniform_buffer && g.uniform_size) {
        const auto &bytes = g.buffers.at(g.uniform_buffer);
        if (g.uniform_size > 65536 || g.uniform_offset + g.uniform_size > bytes.size())
            throw std::runtime_error("DX11 primitive constant range outside buffer");
        if (ranged) {
            if ((g.uniform_offset & 255) || constant_count > 4096 ||
                g.uniform_offset + size_t(constant_count) * 16 > stream->second.bytes)
                throw std::runtime_error("DX11 invalid constant window");
            primitive_constants = stream->second.resource.Get();
            ++g.ranged_draws;
        } else {
            upload_constants(g.batch_constants.Get(), bytes.data() + g.uniform_offset, g.uniform_size);
            ++g.copied_draws;
        }
    }
    for (unsigned stage_index = 0; stage_index < 2; ++stage_index) {
        auto &stage = p.stages[stage_index];
        if (stage.constants && *stage.uploaded != stage.globals) {
            upload_constants(stage.constants.Get(), stage.globals.data(), stage.globals.size());
            *stage.uploaded = stage.globals;
        }
        ID3D11Buffer *constants[] = {primitive_constants, stage.constants.Get()};
        std::array<ID3D11ShaderResourceView *, 16> views{};
        std::array<ID3D11SamplerState *, 16> samplers{};
        for (const auto &binding : stage.samples)
            for (unsigned i = 0; i < binding.count; ++i) {
                auto name = binding.name;
                if (binding.count > 1)
                    name += "[" + std::to_string(i) + "]";
                const unsigned unit = p.texture_units[name];
                if (unit >= g.units.size())
                    throw std::runtime_error("DX11 invalid texture unit");
                const GLuint id = g.units[unit];
                if (!id || !g.textures.count(id))
                    continue;
                // GL permits an unused sampler to name an attachment. D3D11
                // validates even unused views, so leave those bindings empty.
                bool output = false;
                for (auto point : f.draw)
                    if (point != GL_NONE && f.colors[point - GL_COLOR_ATTACHMENT0] == id)
                        output = true;
                if (output)
                    continue;
                const auto &t = g.textures.at(id);
                const bool integer = t.format == DXGI_FORMAT_R8_UINT || t.format == DXGI_FORMAT_R32_UINT ||
                                     t.format == DXGI_FORMAT_R8G8B8A8_UINT ||
                                     t.format == DXGI_FORMAT_R32G32B32A32_UINT;
                // Unused GL samplers can retain views of another numeric type
                // (for example mip samplers while using the atlas). D3D11
                // validates every bound view, so keep those slots unbound.
                if (integer != binding.integer)
                    continue;
                views[binding.slot + i] = t.srv.Get();
                samplers[binding.slot + i] = sampler(t);
            }
        if (stage_index) {
            if (ranged) {
                g.context->PSSetConstantBuffers1(0, 1, constants, &first_constant, &constant_count);
                g.context->PSSetConstantBuffers(1, 1, constants + 1);
            } else
                g.context->PSSetConstantBuffers(0, 2, constants);
            g.context->PSSetShaderResources(0, 16, views.data());
            g.context->PSSetSamplers(0, 16, samplers.data());
        } else {
            if (ranged) {
                g.context->VSSetConstantBuffers1(0, 1, constants, &first_constant, &constant_count);
                g.context->VSSetConstantBuffers(1, 1, constants + 1);
            } else
                g.context->VSSetConstantBuffers(0, 2, constants);
            g.context->VSSetShaderResources(0, 16, views.data());
            g.context->VSSetSamplers(0, 16, samplers.data());
        }
    }
    g.context->DrawInstanced(count, instances, first, 0);
    check(g.device->GetDeviceRemovedReason(), "draw/device status");
}
void clear(GLenum point, const float *value) {
    auto &g = b();
    if (!g.color_write)
        return;
    auto &t = attachment(g.framebuffers[g.draw], point);
    unbind();
    if (g.scissor_enabled) {
        const auto &s = g.scissor;
        const D3D11_RECT rect{std::clamp(s[0], 0, int(t.width)), std::clamp(s[1], 0, int(t.height)),
                              std::clamp(s[0] + s[2], 0, int(t.width)),
                              std::clamp(s[1] + s[3], 0, int(t.height))};
        if (rect.right > rect.left && rect.bottom > rect.top)
            g.context->ClearView(t.rtv.Get(), value, &rect, 1);
    } else
        g.context->ClearRenderTargetView(t.rtv.Get(), value);
}
void copy(Texture &dst, int dx, int dy, Texture &src, int sx, int sy, unsigned width, unsigned height) {
    if (!width || !height)
        return;
    unbind();
    const D3D11_BOX box{UINT(sx), UINT(sy), 0, UINT(sx) + width, UINT(sy) + height, 1};
    b().context->CopySubresourceRegion(dst.image.Get(), 0, dx, dy, 0, src.image.Get(), 0, &box);
}
void uniform(GLint location, const void *value, size_t size, bool integer = false) {
    if (location < 0)
        return;
    auto &p = b().programs.at(b().program);
    const auto &name = p.locations.at(location);
    if (integer && size == 4)
        p.texture_units[name] = *static_cast<const unsigned *>(value);
    for (auto &stage : p.stages)
        for (const auto &m : stage.members)
            if (m.name == name)
                std::memcpy(stage.globals.data() + m.offset, value, std::min(size, size_t(m.size)));
}

void APIENTRY ActiveTexture(GLenum unit) { b().active = unit - GL_TEXTURE0; }
void APIENTRY BindTexture(GLenum, GLuint id) { b().units.at(b().active) = id; }
void APIENTRY GenTextures(GLsizei count, GLuint *ids) {
    while (count--) {
        *ids = b().next++;
        b().textures[*ids++] = {};
    }
}
void APIENTRY DeleteTextures(GLsizei count, const GLuint *ids) {
    while (count--)
        b().textures.erase(*ids++);
}
void APIENTRY TexImage2D(GLenum, GLint, GLint internal, GLsizei width, GLsizei height, GLint, GLenum, GLenum,
                         const void *pixels) {
    allocate(texture(), width, height, internal, pixels, b().unpack);
}
void APIENTRY TexSubImage2D(GLenum, GLint, GLint x, GLint y, GLsizei width, GLsizei height, GLenum, GLenum,
                            const void *pixels) {
    auto &t = texture();
    unbind();
    D3D11_BOX box{UINT(x), UINT(y), 0, UINT(x + width), UINT(y + height), 1};
    b().context->UpdateSubresource(t.image.Get(), 0, &box, pixels, row_size(width * t.bytes, b().unpack), 0);
}
void APIENTRY TexParameteri(GLenum, GLenum name, GLint value) {
    auto &t = texture();
    if (name == GL_TEXTURE_MIN_FILTER)
        t.min_filter = value;
    else if (name == GL_TEXTURE_MAG_FILTER)
        t.mag_filter = value;
    else if (name == GL_TEXTURE_WRAP_S)
        t.wrap_s = value;
    else if (name == GL_TEXTURE_WRAP_T)
        t.wrap_t = value;
}
void APIENTRY PixelStorei(GLenum name, GLint value) {
    (name == GL_PACK_ALIGNMENT ? b().pack : b().unpack) = value;
}
void APIENTRY GenFramebuffers(GLsizei count, GLuint *ids) {
    while (count--) {
        *ids = b().next++;
        b().framebuffers[*ids++] = {};
    }
}
void APIENTRY DeleteFramebuffers(GLsizei count, const GLuint *ids) {
    while (count--)
        b().framebuffers.erase(*ids++);
}
void APIENTRY BindFramebuffer(GLenum target, GLuint id) {
    if (target != GL_READ_FRAMEBUFFER)
        b().draw = id;
    if (target != GL_DRAW_FRAMEBUFFER)
        b().read = id;
}
void APIENTRY FramebufferTexture2D(GLenum target, GLenum point, GLenum, GLuint id, GLint) {
    auto &f = b().framebuffers[target == GL_READ_FRAMEBUFFER ? b().read : b().draw];
    if (point == GL_DEPTH_ATTACHMENT)
        f.depth = id;
    else
        f.colors.at(point - GL_COLOR_ATTACHMENT0) = id;
}
GLenum APIENTRY CheckFramebufferStatus(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
void APIENTRY DrawBuffers(GLsizei count, const GLenum *points) {
    auto &f = b().framebuffers[b().draw];
    f.draw.fill(GL_NONE);
    std::copy(points, points + count, f.draw.begin());
}
void APIENTRY DrawBuffer(GLenum point) {
    if (point == GL_BACK)
        point = GL_COLOR_ATTACHMENT0;
    DrawBuffers(1, &point);
}
void APIENTRY ReadBuffer(GLenum point) {
    b().framebuffers[b().read].read = point == GL_BACK ? GL_COLOR_ATTACHMENT0 : point;
}
void APIENTRY CopyTexSubImage2D(GLenum, GLint, GLint dx, GLint dy, GLint x, GLint y, GLsizei width,
                                GLsizei height) {
    auto &f = b().framebuffers[b().read];
    copy(texture(), dx, dy, attachment(f, f.read), x, y, width, height);
}
void APIENTRY BlitFramebuffer(GLint sx, GLint sy, GLint ex, GLint ey, GLint dx, GLint dy, GLint dex,
                              GLint dey, GLbitfield mask, GLenum) {
    if (ex - sx != dex - dx || ey - sy != dey - dy)
        throw std::runtime_error("DX11 RDP copy extents differ");
    auto &src = b().framebuffers[b().read];
    auto &dst = b().framebuffers[b().draw];
    if (mask & GL_COLOR_BUFFER_BIT)
        for (auto point : dst.draw)
            if (point != GL_NONE)
                copy(attachment(dst, point), dx, dy, attachment(src, src.read), sx, sy, ex - sx, ey - sy);
    if ((mask & GL_DEPTH_BUFFER_BIT) && src.depth && dst.depth)
        copy(attachment(dst, GL_DEPTH_ATTACHMENT), dx, dy, attachment(src, GL_DEPTH_ATTACHMENT), sx, sy,
             ex - sx, ey - sy);
}
void APIENTRY ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum channels, GLenum type,
                         void *out) {
    auto &f = b().framebuffers[b().read];
    auto &t = attachment(f, f.read);
    D3D11_TEXTURE2D_DESC desc{};
    t.image->GetDesc(&desc);
    desc.BindFlags = desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(b().device->CreateTexture2D(&desc, nullptr, &staging), "create readback");
    unbind();
    b().context->CopyResource(staging.Get(), t.image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(b().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "read texture");
    const unsigned components = channels == GL_RED_INTEGER ? 1 : 4;
    const unsigned component_bytes = type == GL_UNSIGNED_INT ? 4 : 1;
    const unsigned pitch = row_size(width * components * component_bytes, b().pack);
    for (int row = 0; row < height; ++row) {
        const int source_row = b().read ? y + row : int(t.height) - 1 - y - row;
        const auto *src =
            static_cast<const unsigned char *>(mapped.pData) + source_row * mapped.RowPitch + x * t.bytes;
        auto *dst = static_cast<unsigned char *>(out) + row * pitch;
        if (components * component_bytes == t.bytes && channels != GL_BGRA)
            std::memcpy(dst, src, width * t.bytes);
        else
            for (int col = 0; col < width; ++col) {
                std::array<unsigned, 4> pixel{0, 0, 0, 1};
                const unsigned source_components =
                    (t.format == DXGI_FORMAT_R32_UINT || t.format == DXGI_FORMAT_R8_UINT) ? 1 : 4;
                for (unsigned c = 0; c < source_components; ++c)
                    if (t.format == DXGI_FORMAT_R32G32B32A32_UINT || t.format == DXGI_FORMAT_R32_UINT)
                        std::memcpy(&pixel[c], src + col * t.bytes + c * 4, 4);
                    else
                        pixel[c] = src[col * t.bytes + c];
                if (channels == GL_BGRA)
                    std::swap(pixel[0], pixel[2]);
                for (unsigned c = 0; c < components; ++c)
                    std::memcpy(dst + (col * components + c) * component_bytes, &pixel[c], component_bytes);
            }
    }
    b().context->Unmap(staging.Get(), 0);
}
void APIENTRY GenBuffers(GLsizei count, GLuint *ids) {
    while (count--) {
        *ids = b().next++;
        b().buffers[*ids++] = {};
    }
}
void APIENTRY DeleteBuffers(GLsizei count, const GLuint *ids) {
    while (count--) {
        const auto id = *ids++;
        b().buffers.erase(id);
        b().stream_buffers.erase(id);
        if (b().uniform_buffer == id) {
            b().uniform_buffer = 0;
            b().uniform_size = 0;
        }
        for (auto &[target, binding] : b().buffer_bindings)
            if (binding == id)
                binding = 0;
    }
}
void APIENTRY BindBuffer(GLenum target, GLuint id) { b().buffer_bindings[target] = id; }
void APIENTRY BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum) {
    auto &bytes = b().buffers.at(b().buffer_bindings[target]);
    bytes.resize(size);
    if (data)
        std::memcpy(bytes.data(), data, size);
    if (target == GL_UNIFORM_BUFFER && b().range_constants) {
        auto &stream = b().stream_buffers[b().buffer_bindings[target]];
        if (!stream.resource || stream.bytes != size_t(size)) {
            stream.resource.Reset();
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = UINT(size);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT created = b().device->CreateBuffer(&desc, nullptr, &stream.resource);
            if (FAILED(created)) {
                b().range_constants = false;
                return;
            }
            stream.bytes = size;
        }
        stream.discard = true;
        if (data) {
            upload_constants(stream.resource.Get(), data, size);
            stream.discard = false;
        }
    }
}
void APIENTRY BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    auto &bytes = b().buffers.at(b().buffer_bindings[target]);
    if (offset < 0 || size < 0 || size_t(offset + size) > bytes.size())
        throw std::runtime_error("DX11 buffer update out of range");
    std::memcpy(bytes.data() + offset, data, size);
    const auto stream = b().stream_buffers.find(b().buffer_bindings[target]);
    if (b().range_constants && stream != b().stream_buffers.end() && stream->second.resource) {
        auto &gpu = stream->second;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(b().context->Map(gpu.resource.Get(), 0,
                               gpu.discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0,
                               &mapped),
              "stream primitive constants");
        std::memcpy(static_cast<unsigned char *>(mapped.pData) + offset, data, size);
        b().context->Unmap(gpu.resource.Get(), 0);
        gpu.discard = false;
    }
}
void APIENTRY BindBufferRange(GLenum, GLuint, GLuint id, GLintptr offset, GLsizeiptr size) {
    b().uniform_buffer = id;
    b().uniform_offset = offset;
    b().uniform_size = size;
}
void APIENTRY BindBufferBase(GLenum target, GLuint index, GLuint id) {
    BindBufferRange(target, index, id, 0, id ? b().buffers.at(id).size() : 0);
}
GLuint APIENTRY CreateShader(GLenum type) {
    auto id = b().next++;
    b().shaders[id].type = type;
    return id;
}
void APIENTRY ShaderSource(GLuint id, GLsizei count, const GLchar *const *strings, const GLint *lengths) {
    auto &s = b().shaders.at(id).source;
    s.clear();
    for (int i = 0; i < count; ++i)
        s.append(strings[i], lengths && lengths[i] >= 0 ? size_t(lengths[i]) : std::strlen(strings[i]));
}
void APIENTRY CompileShader(GLuint) {}
void APIENTRY GetShaderiv(GLuint, GLenum name, GLint *value) {
    *value = name == GL_COMPILE_STATUS ? GL_TRUE : 1;
}
void APIENTRY GetShaderInfoLog(GLuint, GLsizei size, GLsizei *length, GLchar *out) {
    if (length)
        *length = 0;
    if (size)
        *out = 0;
}
void APIENTRY DeleteShader(GLuint id) { b().shaders.erase(id); }
GLuint APIENTRY CreateProgram() {
    auto id = b().next++;
    b().programs[id] = {};
    return id;
}
void APIENTRY AttachShader(GLuint program, GLuint shader) {
    b().programs.at(program).shaders.push_back(shader);
}
void APIENTRY LinkProgram(GLuint id) {
    auto &p = b().programs.at(id);
    try {
        std::string vs, ps;
        for (auto shader : p.shaders) {
            auto &s = b().shaders.at(shader);
            (s.type == GL_VERTEX_SHADER ? vs : ps) = s.source;
        }
        const auto key = vs + '\0' + ps;
        const auto cached = b().compiled_programs.find(key);
        if (cached != b().compiled_programs.end())
            static_cast<Program &>(p) = cached->second;
        else {
            compile_program(b().device.Get(), b().compiler, p, vs, ps);
            b().compiled_programs.emplace(key, static_cast<const Program &>(p));
        }
    } catch (const std::exception &e) {
        p.error = e.what();
        p.linked = false;
        std::fprintf(stderr, "DX11 program link: %s\n", p.error.c_str());
    }
}
void APIENTRY GetProgramiv(GLuint id, GLenum name, GLint *value) {
    const auto &p = b().programs.at(id);
    *value = name == GL_LINK_STATUS ? p.linked : GLint(p.error.size() + 1);
}
void APIENTRY GetProgramInfoLog(GLuint id, GLsizei size, GLsizei *length, GLchar *out) {
    const auto &e = b().programs.at(id).error;
    const unsigned count = size > 0 ? std::min(size_t(size - 1), e.size()) : 0;
    if (size > 0) {
        std::memcpy(out, e.data(), count);
        out[count] = 0;
    }
    if (length)
        *length = count;
}
void APIENTRY DeleteProgram(GLuint id) { b().programs.erase(id); }
void APIENTRY UseProgram(GLuint id) { b().program = id; }
GLint APIENTRY GetUniformLocation(GLuint id, const GLchar *name) {
    auto &v = b().programs.at(id).locations;
    auto it = std::find(v.begin(), v.end(), name);
    if (it != v.end())
        return GLint(it - v.begin());
    v.push_back(name);
    return GLint(v.size() - 1);
}
GLuint APIENTRY GetUniformBlockIndex(GLuint, const GLchar *) { return 0; }
void APIENTRY UniformBlockBinding(GLuint, GLuint, GLuint) {}
void APIENTRY Uniform1i(GLint l, GLint x) { uniform(l, &x, sizeof(x), true); }
void APIENTRY Uniform1ui(GLint l, GLuint x) { uniform(l, &x, sizeof(x), true); }
void APIENTRY Uniform1f(GLint l, GLfloat x) { uniform(l, &x, sizeof(x)); }
void APIENTRY Uniform2f(GLint l, GLfloat x, GLfloat y) {
    const float v[]{x, y};
    uniform(l, v, sizeof(v));
}
void APIENTRY Uniform4f(GLint l, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const float v[]{x, y, z, w};
    uniform(l, v, sizeof(v));
}
void APIENTRY Uniform4i(GLint l, GLint x, GLint y, GLint z, GLint w) {
    const int v[]{x, y, z, w};
    uniform(l, v, sizeof(v));
}
void APIENTRY DrawArrays(GLenum mode, GLint first, GLsizei count) { draw(mode, first, count, 1); }
void APIENTRY DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    draw(mode, first, count, instances);
}
void APIENTRY Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { b().viewport = {x, y, w, h}; }
void APIENTRY Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { b().scissor = {x, y, w, h}; }
void APIENTRY Enable(GLenum cap) {
    if (cap == GL_SCISSOR_TEST)
        b().scissor_enabled = true;
}
void APIENTRY Disable(GLenum cap) {
    if (cap == GL_SCISSOR_TEST)
        b().scissor_enabled = false;
}
void APIENTRY ColorMask(GLboolean r, GLboolean g, GLboolean blue, GLboolean a) {
    if (!(r == g && g == blue && blue == a))
        throw std::runtime_error("DX11 RDP requires a uniform channel mask");
    b().color_write = r;
}
void APIENTRY ClearColor(GLfloat r, GLfloat g, GLfloat blue, GLfloat a) { b().clear_color = {r, g, blue, a}; }
void APIENTRY Clear(GLbitfield mask) {
    auto &f = b().framebuffers[b().draw];
    if (mask & GL_COLOR_BUFFER_BIT)
        for (auto p : f.draw)
            if (p != GL_NONE)
                clear(p, b().clear_color.data());
}
void APIENTRY ClearBufferuiv(GLenum, GLint index, const GLuint *value) {
    const float converted[]{float(value[0]), float(value[1]), float(value[2]), float(value[3])};
    const auto point = b().framebuffers[b().draw].draw.at(index);
    if (point != GL_NONE)
        clear(point, converted);
}
void APIENTRY ClearDepth(GLdouble) {}
void APIENTRY DepthFunc(GLenum func) {
    if (func != GL_ALWAYS)
        throw std::runtime_error("DX11 unexpected hardware depth compare");
}
void APIENTRY DepthMask(GLboolean) {}
void APIENTRY PolygonMode(GLenum, GLenum) {}
void APIENTRY GenVertexArrays(GLsizei count, GLuint *ids) {
    while (count--)
        *ids++ = b().next++;
}
void APIENTRY DeleteVertexArrays(GLsizei, const GLuint *) {}
void APIENTRY BindVertexArray(GLuint) {}
void APIENTRY VertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) {}
void APIENTRY EnableVertexAttribArray(GLuint) {}
void APIENTRY DisableVertexAttribArray(GLuint) {}
void APIENTRY BindAttribLocation(GLuint, GLuint, const GLchar *) {}
void APIENTRY BindSampler(GLuint, GLuint) {}
void APIENTRY BlendColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void APIENTRY BlendEquationSeparate(GLenum, GLenum) {}
void APIENTRY BlendFuncSeparate(GLenum, GLenum, GLenum, GLenum) {}
void APIENTRY GetIntegerv(GLenum name, GLint *value) {
    switch (name) {
    case GL_MAX_TEXTURE_SIZE:
        *value = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        break;
    case GL_MAX_UNIFORM_BLOCK_SIZE:
        *value = 65536;
        break;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
        *value = 256;
        break;
    case GL_MAX_DRAW_BUFFERS:
        *value = 8;
        break;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
        *value = 16;
        break;
    case GL_FRAMEBUFFER_BINDING:
        *value = b().draw;
        break;
    case GL_READ_FRAMEBUFFER_BINDING:
        *value = b().read;
        break;
    case GL_PACK_ALIGNMENT:
        *value = b().pack;
        break;
    case GL_UNPACK_ALIGNMENT:
        *value = b().unpack;
        break;
    default:
        *value = 0;
        break;
    }
}
const GLubyte *APIENTRY GetString(GLenum) {
    return reinterpret_cast<const GLubyte *>("Native Direct3D 11 / Shader Model 5.0");
}
void install() {
#define SET(name) glad_gl##name = name
    SET(ActiveTexture);
    SET(BindTexture);
    SET(GenTextures);
    SET(DeleteTextures);
    SET(TexImage2D);
    SET(TexSubImage2D);
    SET(TexParameteri);
    SET(PixelStorei);
    SET(GenFramebuffers);
    SET(DeleteFramebuffers);
    SET(BindFramebuffer);
    SET(FramebufferTexture2D);
    SET(CheckFramebufferStatus);
    SET(DrawBuffers);
    SET(DrawBuffer);
    SET(ReadBuffer);
    SET(CopyTexSubImage2D);
    SET(BlitFramebuffer);
    SET(ReadPixels);
    SET(GenBuffers);
    SET(DeleteBuffers);
    SET(BindBuffer);
    SET(BufferData);
    SET(BufferSubData);
    SET(BindBufferRange);
    SET(BindBufferBase);
    SET(CreateShader);
    SET(ShaderSource);
    SET(CompileShader);
    SET(GetShaderiv);
    SET(GetShaderInfoLog);
    SET(DeleteShader);
    SET(CreateProgram);
    SET(AttachShader);
    SET(LinkProgram);
    SET(GetProgramiv);
    SET(GetProgramInfoLog);
    SET(DeleteProgram);
    SET(UseProgram);
    SET(GetUniformLocation);
    SET(GetUniformBlockIndex);
    SET(UniformBlockBinding);
    SET(Uniform1i);
    SET(Uniform1ui);
    SET(Uniform1f);
    SET(Uniform2f);
    SET(Uniform4f);
    SET(Uniform4i);
    SET(DrawArrays);
    SET(DrawArraysInstanced);
    SET(Viewport);
    SET(Scissor);
    SET(Enable);
    SET(Disable);
    SET(ColorMask);
    SET(ClearColor);
    SET(Clear);
    SET(ClearBufferuiv);
    SET(ClearDepth);
    SET(DepthFunc);
    SET(DepthMask);
    SET(PolygonMode);
    SET(GenVertexArrays);
    SET(DeleteVertexArrays);
    SET(BindVertexArray);
    SET(VertexAttribPointer);
    SET(EnableVertexAttribArray);
    SET(DisableVertexAttribArray);
    SET(BindAttribLocation);
    SET(BindSampler);
    SET(BlendColor);
    SET(BlendEquationSeparate);
    SET(BlendFuncSeparate);
    SET(GetIntegerv);
    SET(GetString);
#undef SET
    glrdp_gles3 = glrdp_image_load_store = glrdp_fragment_interlock = false;
    glrdp_texture_barrier = nullptr;
    glrdp_precision_query = nullptr;
}
} // namespace

bool initialize_device(ID3D11Device *device, ID3D11DeviceContext *context,
                       const retro_hw_render_interface_d3d11 *frontend) {
    if (!device || !context || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return false;
    auto candidate = std::make_unique<Backend>();
    if (frontend)
        candidate->compiler = frontend->D3DCompile;
    if (!candidate->compiler) {
        for (const auto *name : {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"}) {
            auto library = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library)
                continue;
            candidate->compiler = reinterpret_cast<pD3DCompile>(GetProcAddress(library, "D3DCompile"));
            if (candidate->compiler) {
                candidate->compiler_library = library;
                break;
            }
            FreeLibrary(library);
        }
    }
    if (!candidate->compiler)
        throw std::runtime_error("DX11 requires a D3DCompile implementation");
    if (frontend && frontend->interface_version == 2)
        candidate->frontend = *frontend;
    candidate->device = device;
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    candidate->range_constants =
        !std::getenv("GLRDP_DX11_NO_CB_OFFSETS") &&
        SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
        options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer;
    ComPtr<ID3D11Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) ||
        FAILED(context->QueryInterface(IID_PPV_ARGS(&candidate->context))))
        return false;
    const D3D_FEATURE_LEVEL levels[]{device->GetFeatureLevel()};
    const UINT flags = device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED
                           ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                           : 0;
    check(device1->CreateDeviceContextState(flags, levels, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device),
                                            nullptr, &candidate->core_state),
          "create isolated pipeline state");
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 65536;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check(device->CreateBuffer(&cb, nullptr, &candidate->batch_constants), "create primitive constants");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = true;
    check(device->CreateRasterizerState(&raster, &candidate->raster_full), "create rasterizer");
    raster.ScissorEnable = true;
    check(device->CreateRasterizerState(&raster, &candidate->raster_scissor), "create scissor rasterizer");
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp =
        D3D11_STENCIL_OP_KEEP;
    depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    depth.BackFace = depth.FrontFace;
    check(device->CreateDepthStencilState(&depth, &candidate->no_depth), "create depth state");
    D3D11_BLEND_DESC blend{};
    for (auto &rt : blend.RenderTarget) {
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    check(device->CreateBlendState(&blend, &candidate->write_all), "create write state");
    for (auto &rt : blend.RenderTarget)
        rt.RenderTargetWriteMask = 0;
    check(device->CreateBlendState(&blend, &candidate->write_none), "create mask state");
    candidate->framebuffers[0] = {};
    backend = std::move(candidate);
    glrdp_dx11 = true;
    install();
    glrdp_dx11_begin();
    try {
        const auto id = b().next++;
        allocate(b().textures[id], 640, 480, GL_RGBA8, nullptr, 4);
        b().framebuffers[0].colors[0] = id;
        b().present_textures[0] = id;
        b().last_present = id;
        const float black[]{0, 0, 0, 1};
        b().context->ClearRenderTargetView(b().textures[id].rtv.Get(), black);
    } catch (...) {
        glrdp_dx11_end();
        backend.reset();
        glrdp_dx11 = false;
        throw;
    }
    glrdp_dx11_end();
    return true;
}
bool glrdp_dx11_initialize_device(ID3D11Device *device, ID3D11DeviceContext *context) {
    return initialize_device(device, context, nullptr);
}
void glrdp_dx11_negotiate(retro_environment_t environment) {
    retro_hw_render_context_negotiation_interface probe{RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11,
                                                        0};
    static const retro_hw_render_context_negotiation_interface_d3d11 negotiation{
        RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11, 1, 2};
    if (environment(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT, &probe) &&
        probe.interface_version >= 1)
        environment(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&negotiation);
}
bool glrdp_dx11_initialize(retro_environment_t environment) {
    const char *save_directory = nullptr;
    configure_shader_cache(environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory) &&
                                   save_directory
                               ? std::string(save_directory) + "/GLRDP-DX11-shaders"
                               : "");
    const retro_hw_render_interface_d3d11 *render_interface = nullptr;
    if (!environment(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &render_interface) || !render_interface ||
        render_interface->interface_type != RETRO_HW_RENDER_INTERFACE_D3D11 ||
        (render_interface->interface_version != 1 && render_interface->interface_version != 2))
        return false;
    if (render_interface->interface_version == 2 &&
        (!render_interface->lock_context || !render_interface->unlock_context ||
         !render_interface->set_texture || !render_interface->get_sync_index ||
         !render_interface->get_sync_index_mask || !render_interface->wait_sync_index))
        return false;
    return initialize_device(render_interface->device, render_interface->context, render_interface);
}
void glrdp_dx11_begin() {
    if (b().guards++ == 0) {
        auto &g = b();
        if (g.frontend.interface_version == 2)
            g.frontend.lock_context(g.frontend.handle);
        b().context->SwapDeviceContextState(b().core_state.Get(),
                                            b().frontend_state.ReleaseAndGetAddressOf());
        try {
            if (g.prepare_target) {
                auto &id = g.present_textures[g.pending_sync_index];
                if (!id) {
                    id = g.next++;
                    allocate(g.textures[id], 640, 480, GL_RGBA8, nullptr, 4);
                    const float black[]{0, 0, 0, 1};
                    g.context->ClearRenderTargetView(g.textures[id].rtv.Get(), black);
                }
                g.framebuffers[0].colors[0] = id;
                g.prepare_target = false;
            }
        } catch (...) {
            glrdp_dx11_end();
            throw;
        }
    }
}
void glrdp_dx11_end() {
    if (!b().guards)
        throw std::runtime_error("DX11 unbalanced context guard");
    if (--b().guards == 0) {
        b().context->SwapDeviceContextState(b().frontend_state.Get(),
                                            b().core_state.ReleaseAndGetAddressOf());
        b().frontend_state.Reset();
        if (b().frontend.interface_version == 2)
            b().frontend.unlock_context(b().frontend.handle);
    }
}
ID3D11Texture2D *glrdp_dx11_output() {
    return attachment(b().framebuffers[0], GL_COLOR_ATTACHMENT0).image.Get();
}
void glrdp_dx11_prepare_frame() {
    auto &g = b();
    if (g.frontend.interface_version != 2)
        return;
    if (g.guards)
        throw std::runtime_error("DX11 sync wait inside context lock");
    g.pending_sync_index = g.frontend.get_sync_index(g.frontend.handle);
    if (g.pending_sync_index >= 32 ||
        !(g.frontend.get_sync_index_mask(g.frontend.handle) & (1u << g.pending_sync_index)))
        throw std::runtime_error("DX11 invalid frontend sync index");
    g.frontend.wait_sync_index(g.frontend.handle);
    g.prepare_target = true;
}
void glrdp_dx11_prepare_present(bool updated) {
    auto &g = b();
    if (g.frontend.interface_version != 2)
        return;
    if (!updated)
        return;
    if (!g.guards)
        throw std::runtime_error("DX11 texture handoff outside context lock");
    if (updated)
        g.last_present = g.framebuffers[0].colors[0];
    g.frontend.set_texture(g.frontend.handle, g.textures.at(g.last_present).image.Get());
}
void glrdp_dx11_present() {
    if (b().frontend.interface_version == 2)
        return;
    auto &t = attachment(b().framebuffers[0], GL_COLOR_ATTACHMENT0);
    auto *view = t.srv.Get();
    b().context->PSSetShaderResources(0, 1, &view);
}
void glrdp_dx11_shutdown() {
    if (backend && b().frontend.interface_version == 2) {
        glrdp_dx11_begin();
        b().frontend.set_texture(b().frontend.handle, nullptr);
        glrdp_dx11_end();
    }
    if (backend)
        std::fprintf(stderr, "DX11 constants = offsets=%u ranged_draws=%llu copied_draws=%llu\n",
                     unsigned(b().range_constants), (unsigned long long)b().ranged_draws,
                     (unsigned long long)b().copied_draws);
    backend.reset();
    glrdp_dx11 = false;
}
#endif
