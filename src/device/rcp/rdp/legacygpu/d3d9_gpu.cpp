#include "legacy_gpu.hpp"
#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <wrl/client.h>

namespace glrdp::legacy {
namespace {
template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
void check(HRESULT hr, const char *what) {
    if (FAILED(hr))
        throw std::runtime_error(std::string("D3D9 ") + what + " (" + std::to_string(unsigned(hr)) + ")");
}
class D3D9 final : public Device {
    ComPtr<IDirect3DDevice9> device_;
    struct CompilerLibrary {
        HMODULE module = nullptr;
        ~CompilerLibrary() {
            if (module)
                FreeLibrary(module);
        }
    } library_;
    pD3DCompile compiler_ = nullptr;
    std::array<ComPtr<IDirect3DPixelShader9>, 6> shaders_;
    std::array<ComPtr<IDirect3DTexture9>, 2> inputs_;
    ComPtr<IDirect3DTexture9> upload_, output_;
    ComPtr<IDirect3DSurface9> target_, readback_;
    Capabilities caps_;
    unsigned width_ = 0, height_ = 0;
    unsigned target_width_ = 0, target_height_ = 0;
    unsigned targets_ = 1;
    IDirect3DTexture9 *present_target_ = nullptr;
    unsigned present_width_ = 0, present_height_ = 0;
    struct Guard {
        D3D9 &g;
        ComPtr<IDirect3DStateBlock9> state;
        std::array<ComPtr<IDirect3DSurface9>, 4> targets;
        ComPtr<IDirect3DSurface9> depth;
        D3DVIEWPORT9 viewport{};
        Guard(D3D9 &gpu) : g(gpu) {
            check(g.device_->CreateStateBlock(D3DSBT_ALL, &state), "capture pipeline");
            for (unsigned i = 0; i < g.targets_; ++i) {
                HRESULT hr = g.device_->GetRenderTarget(i, &targets[i]);
                if (hr != D3DERR_NOTFOUND)
                    check(hr, "get frontend target");
            }
            HRESULT hr = g.device_->GetDepthStencilSurface(&depth);
            if (hr != D3DERR_NOTFOUND)
                check(hr, "get frontend depth");
            check(g.device_->GetViewport(&viewport), "get viewport");
        }
        ~Guard() {
            g.device_->SetRenderTarget(0, targets[0].Get());
            for (unsigned i = 1; i < g.targets_; ++i)
                g.device_->SetRenderTarget(i, targets[i].Get());
            g.device_->SetDepthStencilSurface(depth.Get());
            state->Apply();
            g.device_->SetViewport(&viewport);
        }
    };
    struct RasterTarget {
        ComPtr<IDirect3DTexture9> texture, feedback;
        ComPtr<IDirect3DSurface9> surface, feedback_surface, upload, readback;
        int width = 0, height = 0, tw = 0, th = 0;
    };
    std::unordered_map<unsigned, RasterTarget> raster_targets_;
    unsigned raster_serial_ = 1, combined_ = 0, alpha0_ = 0, texel0_ = 0, texel1_ = 0, lod_ = 0;
    std::array<ComPtr<IDirect3DPixelShader9>, 13> raster_shaders_;
    std::map<std::pair<RasterShader, RasterKey>, ComPtr<IDirect3DPixelShader9>> raster_specialized_;
    std::unique_ptr<Guard> raster_guard_;
    IDirect3DPixelShader9 *raster_shader(RasterShader pass, const RasterDraw *specialize = nullptr) {
        auto &p = specialize ? raster_specialized_[{pass, raster_key(*specialize)}]
                             : raster_shaders_[unsigned(pass)];
        if (p)
            return p.Get();
        auto source = raster_fragment_shader(pass, true, specialize);
        ComPtr<ID3DBlob> code, errors;
        HRESULT hr =
            compiler_(source.data(), source.size(), "GLRDP legacy GPU", nullptr, nullptr, "main", "ps_3_0",
                      D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors);
        if (FAILED(hr))
            throw std::runtime_error(
                errors ? std::string(static_cast<char *>(errors->GetBufferPointer()), errors->GetBufferSize())
                       : "D3D9 RDP shader compile failed");
        hr = device_->CreatePixelShader(static_cast<DWORD *>(code->GetBufferPointer()), &p);
        if (FAILED(hr)) {
            D3DCAPS9 caps{};
            device_->GetDeviceCaps(&caps);
            if (const auto *path = std::getenv("GLRDP_LEGACY_SHADER_DUMP")) {
                const auto base = std::string(path) + "-pass" + std::to_string(unsigned(pass));
                std::ofstream(base + ".hlsl", std::ios::binary) << source;
                std::ofstream bytes(base + ".bin", std::ios::binary);
                bytes.write(static_cast<char *>(code->GetBufferPointer()), code->GetBufferSize());
                using Disassemble = HRESULT(WINAPI *)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob **);
                HMODULE module = library_.module ? library_.module : GetModuleHandleW(L"d3dcompiler_47.dll");
                if (module) {
                    auto disassemble =
                        reinterpret_cast<Disassemble>(GetProcAddress(module, "D3DDisassemble"));
                    ComPtr<ID3DBlob> text;
                    if (disassemble && SUCCEEDED(disassemble(code->GetBufferPointer(), code->GetBufferSize(),
                                                             0, nullptr, &text))) {
                        std::ofstream out(base + ".asm", std::ios::binary);
                        out.write(static_cast<char *>(text->GetBufferPointer()), text->GetBufferSize());
                    }
                }
            }
            const auto message = std::string("create GPU raster shader pass ") +
                                 std::to_string(unsigned(pass)) + " bytes " +
                                 std::to_string(code->GetBufferSize()) + " instruction slots " +
                                 std::to_string(caps.MaxPixelShader30InstructionSlots) + " static flow " +
                                 std::to_string(caps.PS20Caps.StaticFlowControlDepth) + " dynamic flow " +
                                 std::to_string(caps.PS20Caps.DynamicFlowControlDepth);
            check(hr, message.c_str());
        }
        return p.Get();
    }
    void raster_state() {
        for (unsigned i = 1; i < targets_; i++)
            check(device_->SetRenderTarget(i, nullptr), "unbind frontend MRT");
        check(device_->SetDepthStencilSurface(nullptr), "unbind frontend depth");
        for (auto [key, value] :
             std::array<std::pair<D3DRENDERSTATETYPE, DWORD>, 17>{{{D3DRS_ZENABLE, FALSE},
                                                                   {D3DRS_ZWRITEENABLE, FALSE},
                                                                   {D3DRS_ALPHATESTENABLE, FALSE},
                                                                   {D3DRS_ALPHABLENDENABLE, FALSE},
                                                                   {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
                                                                   {D3DRS_STENCILENABLE, FALSE},
                                                                   {D3DRS_SCISSORTESTENABLE, FALSE},
                                                                   {D3DRS_FOGENABLE, FALSE},
                                                                   {D3DRS_LIGHTING, FALSE},
                                                                   {D3DRS_DITHERENABLE, FALSE},
                                                                   {D3DRS_SRGBWRITEENABLE, FALSE},
                                                                   {D3DRS_CULLMODE, D3DCULL_NONE},
                                                                   {D3DRS_FILLMODE, D3DFILL_SOLID},
                                                                   {D3DRS_COLORWRITEENABLE, 15},
                                                                   {D3DRS_CLIPPLANEENABLE, 0},
                                                                   {D3DRS_MULTISAMPLEMASK, 0xffffffffu},
                                                                   {D3DRS_WRAP0, 0}}})
            check(device_->SetRenderState(key, value), "set GPU raster state");
        check(device_->SetVertexShader(nullptr), "set GPU raster vertices");
        check(device_->SetStreamSourceFreq(0, 1), "disable instancing");
        check(device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0), "set coordinate index");
        check(device_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE),
              "disable texture transform");
        check(device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1), "set GPU raster FVF");
        for (unsigned i = 0; i < 12; i++)
            for (auto [key, value] :
                 std::array<std::pair<D3DSAMPLERSTATETYPE, DWORD>, 7>{{{D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP},
                                                                       {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
                                                                       {D3DSAMP_MINFILTER, D3DTEXF_POINT},
                                                                       {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                                                                       {D3DSAMP_MIPFILTER, D3DTEXF_NONE},
                                                                       {D3DSAMP_MAXMIPLEVEL, 0},
                                                                       {D3DSAMP_SRGBTEXTURE, FALSE}}})
                check(device_->SetSamplerState(i, key, value), "set GPU raster sampler");
    }
    void raster_quad(float x0, float y0, float x1, float y1, int scale) {
        struct Vertex {
            float x, y, z, w, u, v;
        };
        const Vertex v[] = {{x0 * scale - .5f, y0 * scale - .5f, 0, 1, x0, y0},
                            {x1 * scale - .5f, y0 * scale - .5f, 0, 1, x1, y0},
                            {x0 * scale - .5f, y1 * scale - .5f, 0, 1, x0, y1},
                            {x1 * scale - .5f, y1 * scale - .5f, 0, 1, x1, y1}};
        check(device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex)), "GPU raster draw");
    }
    IDirect3DPixelShader9 *shader(Kernel kernel) {
        auto &p = shaders_.at(unsigned(kernel));
        if (p)
            return p.Get();
        const auto source = fragment_shader(kernel, true);
        ComPtr<ID3DBlob> code, errors;
        HRESULT hr = compiler_(source.data(), source.size(), "legacy RDP kernel", nullptr, nullptr, "main",
                               "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr))
            throw std::runtime_error(
                errors ? std::string(static_cast<char *>(errors->GetBufferPointer()), errors->GetBufferSize())
                       : "D3D9 shader compile failed");
        check(device_->CreatePixelShader(static_cast<DWORD *>(code->GetBufferPointer()), &p),
              "create SM3 shader");
        return p.Get();
    }
    void allocate(unsigned width, unsigned height, unsigned rows) {
        if (width_ != width || height_ != height) {
            for (auto &t : inputs_)
                t.Reset();
            upload_.Reset();
            for (auto &t : inputs_)
                check(device_->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t,
                                             nullptr),
                      "create input");
            // A system-memory upload works without optional dynamic-texture caps.
            check(device_->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &upload_,
                                         nullptr),
                  "create upload");
            width_ = width;
            height_ = height;
        }
        unsigned target_height = storage_extent(rows, caps_.padded_power_of_two);
        if (caps_.square_textures)
            target_height = std::max(target_height, width);
        target_height = std::max(target_height, target_height_);
        if (!rows || (target_width_ == width && target_height_ == target_height))
            return;
        output_.Reset();
        target_.Reset();
        readback_.Reset();
        check(device_->CreateTexture(width, target_height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &output_, nullptr),
              "create RGBA8 target");
        check(output_->GetSurfaceLevel(0, &target_), "get output surface");
        check(device_->CreateOffscreenPlainSurface(width, target_height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                   &readback_, nullptr),
              "create readback");
        target_width_ = width;
        target_height_ = target_height;
    }
    void upload(const Image &image, unsigned index) {
        D3DLOCKED_RECT locked{};
        RECT rect{0, 0, LONG(image.width), LONG(image.height)};
        check(upload_->LockRect(0, &locked, &rect, 0), "lock upload");
        for (unsigned y = 0; y < image.height; ++y) {
            auto *row = static_cast<std::uint8_t *>(locked.pBits) + std::size_t(y) * locked.Pitch;
            const auto *source = image.rgba.data() + std::size_t(y) * image.width * 4;
            for (unsigned x = 0; x < image.width; ++x) {
                row[x * 4 + 0] = source[x * 4 + 2];
                row[x * 4 + 1] = source[x * 4 + 1];
                row[x * 4 + 2] = source[x * 4 + 0];
                row[x * 4 + 3] = source[x * 4 + 3];
            }
        }
        check(upload_->UnlockRect(0), "unlock upload");
        check(device_->UpdateTexture(upload_.Get(), inputs_.at(index).Get()), "upload texture");
    }

  public:
    D3D9(IDirect3DDevice9 *device, bool pot, bool square, Proc compiler) : device_(device) {
        if (!device)
            throw std::runtime_error("D3D9 null device");
        compiler_ = reinterpret_cast<pD3DCompile>(compiler);
        if (!compiler_) {
            wchar_t directory[MAX_PATH]{};
            unsigned n = GetSystemDirectoryW(directory, MAX_PATH);
            if (n && n < MAX_PATH)
                for (const auto *name :
                     {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"}) {
                    auto path = std::wstring(directory) + L"\\" + name;
                    library_.module = LoadLibraryW(path.c_str());
                    if (!library_.module)
                        continue;
                    compiler_ = reinterpret_cast<pD3DCompile>(GetProcAddress(library_.module, "D3DCompile"));
                    if (compiler_)
                        break;
                    FreeLibrary(library_.module);
                    library_.module = nullptr;
                }
        }
        if (!compiler_)
            throw std::runtime_error("D3D9 needs the frontend or system D3DCompile implementation");
        D3DCAPS9 caps{};
        check(device_->GetDeviceCaps(&caps), "query caps");
        if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0))
            throw std::runtime_error("D3D9 kernels require Shader Model 3.0");
        caps_.max_texture = std::min(caps.MaxTextureWidth, caps.MaxTextureHeight);
        caps_.fragment_samplers = 16;
        caps_.separate_shading = true;
        targets_ = std::min(4u, unsigned(caps.NumSimultaneousRTs));
        caps_.padded_power_of_two =
            pot || (caps.TextureCaps & (D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_NONPOW2CONDITIONAL));
        caps_.square_textures = square || (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY);
        ComPtr<IDirect3D9> api;
        check(device_->GetDirect3D(&api), "get API");
        D3DDEVICE_CREATION_PARAMETERS creation{};
        check(device_->GetCreationParameters(&creation), "get adapter");
        D3DADAPTER_IDENTIFIER9 adapter{};
        check(api->GetAdapterIdentifier(creation.AdapterOrdinal, 0, &adapter), "get adapter name");
        caps_.device = std::string(adapter.Description) + " / Shader Model 3.0";
        D3DDISPLAYMODE mode{};
        check(api->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode), "get adapter format");
        check(api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                                     D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8),
              "require RGBA8 rendering");
    }
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
        if (caps_.square_textures)
            t.tw = t.th = std::max(t.tw, t.th);
        if (t.tw > int(caps_.max_texture) || t.th > int(caps_.max_texture))
            throw std::runtime_error("D3D9 RDP target exceeds caps");
        check(device_->CreateTexture(t.tw, t.th, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                     &t.texture, nullptr),
              "create GPU color");
        check(device_->CreateTexture(t.tw, t.th, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                     &t.feedback, nullptr),
              "create GPU feedback");
        check(t.texture->GetSurfaceLevel(0, &t.surface), "GPU surface");
        check(t.feedback->GetSurfaceLevel(0, &t.feedback_surface), "GPU feedback surface");
        color = framebuffer = raster_serial_++;
        raster_targets_.emplace(color, std::move(t));
    }
    void release(unsigned color, unsigned) override { raster_targets_.erase(color); }
    void upload(unsigned color, int width, int height, const std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        auto &t = raster_targets_.at(color);
        if (!t.upload)
            check(device_->CreateOffscreenPlainSurface(t.width, t.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                       &t.upload, nullptr),
                  "create GPU upload");
        D3DLOCKED_RECT locked{};
        check(t.upload->LockRect(&locked, nullptr, 0), "lock GPU upload");
        for (int y = 0; y < height; y++) {
            auto *dst = static_cast<std::uint8_t *>(locked.pBits) + std::size_t(y) * locked.Pitch;
            auto *src = rgba + std::size_t(height - 1 - y) * width * 4;
            for (int x = 0; x < width; x++) {
                dst[x * 4] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
        check(t.upload->UnlockRect(), "unlock GPU upload");
        RECT rect{0, 0, LONG(width), LONG(height)};
        POINT point{0, 0};
        check(device_->UpdateSurface(t.upload.Get(), &rect, t.surface.Get(), &point), "upload GPU image");
    }
    void upload_region(unsigned color, int x, int y, int width, int height,
                       const std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        auto &t = raster_targets_.at(color);
        if (!t.upload)
            check(device_->CreateOffscreenPlainSurface(t.width, t.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                       &t.upload, nullptr),
                  "create atlas upload");
        RECT source{0, 0, LONG(width), LONG(height)};
        D3DLOCKED_RECT locked{};
        check(t.upload->LockRect(&locked, &source, 0), "lock atlas upload");
        for (int row = 0; row < height; row++) {
            auto *dst = static_cast<std::uint8_t *>(locked.pBits) + std::size_t(row) * locked.Pitch;
            auto *src = rgba + std::size_t(row) * width * 4;
            for (int col = 0; col < width; col++) {
                dst[col * 4] = src[col * 4 + 2];
                dst[col * 4 + 1] = src[col * 4 + 1];
                dst[col * 4 + 2] = src[col * 4];
                dst[col * 4 + 3] = src[col * 4 + 3];
            }
        }
        check(t.upload->UnlockRect(), "unlock atlas upload");
        POINT point{LONG(x), LONG(y)};
        check(device_->UpdateSurface(t.upload.Get(), &source, t.surface.Get(), &point),
              "upload atlas region");
    }
    void read(unsigned fbo, int x, int y, int width, int height, std::uint8_t *rgba) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        auto &t = raster_targets_.at(fbo);
        if (!t.readback)
            check(device_->CreateOffscreenPlainSurface(t.tw, t.th, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                       &t.readback, nullptr),
                  "create GPU readback");
        check(device_->GetRenderTargetData(t.surface.Get(), t.readback.Get()), "GPU framebuffer read");
        D3DLOCKED_RECT locked{};
        check(t.readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "map GPU framebuffer");
        for (int row = 0; row < height; row++) {
            auto *src = static_cast<const std::uint8_t *>(locked.pBits) +
                        std::size_t(t.height - 1 - y - row) * locked.Pitch + x * 4;
            auto *dst = rgba + std::size_t(row) * width * 4;
            for (int col = 0; col < width; col++) {
                dst[col * 4] = src[col * 4 + 2];
                dst[col * 4 + 1] = src[col * 4 + 1];
                dst[col * 4 + 2] = src[col * 4];
                dst[col * 4 + 3] = src[col * 4 + 3];
            }
        }
        check(t.readback->UnlockRect(), "unmap GPU framebuffer");
    }
    void draw_raster(const RasterDraw &draw) override { draw_batch({&draw}); }
    void draw_batch(const std::vector<const RasterDraw *> &draws) override {
        if (draws.empty())
            return;
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        const auto &first = *draws.front();
        auto &color = raster_targets_.at(first.color), &z = raster_targets_.at(first.depth);
        for (unsigned i = 0; i < 12; i++)
            check(device_->SetTexture(i, nullptr), "unbind GPU samplers");
        const bool depth_pass = first.constants[1][0] < 2 || first.constants[45][0] > .5f;
        RasterDraw bounds = first;
        bool memory = false, shading = false, alpha = false, textured = false, second = false, lod = false;
        for (auto *d : draws) {
            bounds.x0 = std::min(bounds.x0, d->x0);
            bounds.y0 = std::min(bounds.y0, d->y0);
            bounds.x1 = std::max(bounds.x1, d->x1);
            bounds.y1 = std::max(bounds.y1, d->y1);
            memory |= d->constants[41][2] > .5f;
            shading |= d->constants[1][0] < 2;
            alpha |= d->constants[1][0] == 1 && (unsigned(d->constants[27][3]) & 1);
            textured |= d->constants[1][0] < 2 && d->constants[1][3] > .5f;
            second |= d->constants[1][0] == 1 && d->constants[1][3] > .5f;
            lod |= d->constants[1][0] < 2 && d->constants[1][3] > .5f &&
                   (d->constants[89][0] > .5f || d->constants[90][0] > .5f);
        }
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
        if (shading)
            ensure(combined_, color.width, color.height);
        if (textured)
            ensure(texel0_, color.width + 2 * first.scale, color.height);
        if (second)
            ensure(texel1_, color.width + 2 * first.scale, color.height);
        if (lod)
            ensure(lod_, color.width + 2 * first.scale, color.height);
        if (alpha)
            ensure(alpha0_, color.width + 2 * first.scale, color.height);
        auto snapshot = [&](RasterTarget &t) {
            RECT rect{LONG(std::clamp(int(std::floor(bounds.x0 * bounds.scale)), 0, t.width)),
                      LONG(std::clamp(int(std::floor(bounds.y0 * bounds.scale)), 0, t.height)),
                      LONG(std::clamp(int(std::ceil(bounds.x1 * bounds.scale)), 0, t.width)),
                      LONG(std::clamp(int(std::ceil(bounds.y1 * bounds.scale)), 0, t.height))};
            if (rect.right > rect.left && rect.bottom > rect.top)
                check(device_->StretchRect(t.surface.Get(), &rect, t.feedback_surface.Get(), &rect,
                                           D3DTEXF_NONE),
                      "snapshot GPU batch");
        };
        if (memory)
            snapshot(color);
        if (depth_pass)
            snapshot(z);
        check(device_->BeginScene(), "begin GPU raster scene");
        try {
            for (auto pass :
                 {RasterShader::LOD, RasterShader::Texel0, RasterShader::Texel1, RasterShader::Shade,
                  RasterShader::Alpha0, RasterShader::Depth, RasterShader::Color}) {
                if (pass == RasterShader::LOD && !lod)
                    continue;
                if ((pass == RasterShader::Texel0 && !textured) || (pass == RasterShader::Texel1 && !second))
                    continue;
                if ((pass == RasterShader::Depth && !depth_pass) ||
                    (pass == RasterShader::Shade && !shading) || (pass == RasterShader::Alpha0 && !alpha))
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
                    case RasterShader::Shade:
                        return raster_targets_.at(combined_);
                    case RasterShader::Alpha0:
                        return raster_targets_.at(alpha0_);
                    case RasterShader::Depth:
                        return z;
                    default:
                        return color;
                    }
                }();
                for (unsigned i : {4u, 7u, 8u, 9u, 10u, 11u})
                    check(device_->SetTexture(i, nullptr), "unbind GPU output");
                check(device_->SetRenderTarget(0, target.surface.Get()), "bind GPU raster target");
                D3DVIEWPORT9 view{0, 0, DWORD(target.width), DWORD(target.height), 0, 1};
                check(device_->SetViewport(&view), "GPU viewport");
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
                    constants[0] = {float(color.width), float(color.height), float(color.tw),
                                    float(color.th)};
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
                    const std::array<IDirect3DTexture9 *, 12> inputs = {
                        color.feedback.Get(),
                        raster_targets_.at(draw.tiles[0]).texture.Get(),
                        raster_targets_.at(draw.tiles[1]).texture.Get(),
                        z.feedback.Get(),
                        pass == RasterShader::Depth ? nullptr : z.texture.Get(),
                        draw.auxiliary[0] ? raster_targets_.at(draw.auxiliary[0]).texture.Get() : nullptr,
                        draw.auxiliary[1] ? raster_targets_.at(draw.auxiliary[1]).texture.Get() : nullptr,
                        combined_ && pass != RasterShader::Shade ? raster_targets_.at(combined_).texture.Get()
                                                                 : nullptr,
                        alpha0_ && pass != RasterShader::Alpha0 ? raster_targets_.at(alpha0_).texture.Get()
                                                                : nullptr,
                        texel0_ && pass != RasterShader::Texel0 ? raster_targets_.at(texel0_).texture.Get()
                                                                : nullptr,
                        (pass == RasterShader::Texel0 || pass == RasterShader::Texel1)
                            ? nullptr
                            : (draw.constants[1][0] == 1 && texel1_
                                   ? raster_targets_.at(texel1_).texture.Get()
                                   : (texel0_ ? raster_targets_.at(texel0_).texture.Get() : nullptr)),
                        lod_ && pass != RasterShader::LOD ? raster_targets_.at(lod_).texture.Get() : nullptr};
                    constants[103] = {float(target.width), float(target.height), float(draw.scale), 0};
                    constants[110] = {guarded ? -1.0f : 0.0f, 0, 0, 0};
                    check(device_->SetPixelShaderConstantF(0, constants[0].data(), 112), "set GPU RDP state");
                    for (unsigned i = 0; i < 12; i++)
                        check(device_->SetTexture(i, inputs[i]), "bind GPU RDP input");
                    check(device_->SetPixelShader(raster_shader(pass, &draw)), "GPU RDP shader");
                    // The shader tests RDP coverage for a whole native cell;
                    // hardware clipping must retain all of its scaled samples.
                    const float x0 = std::floor(draw.x0), y0 = std::floor(draw.y0),
                                x1 = std::ceil(draw.x1) + (guarded ? 2.0f : 0.0f), y1 = std::ceil(draw.y1);
                    RECT clip{LONG(x0 * draw.scale), LONG(y0 * draw.scale), LONG(x1 * draw.scale),
                              LONG(y1 * draw.scale)};
                    check(device_->SetScissorRect(&clip), "GPU RDP scissor");
                    check(device_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable GPU RDP scissor");
                    static const bool boxes = std::getenv("GLRDP_LEGACY_BOUNDING_BOX") != nullptr;
                    if (draw.vertices.empty() || boxes || guarded)
                        raster_quad(x0, y0, x1, y1, draw.scale);
                    else {
                        struct Vertex {
                            float x, y, z, w, u, v;
                        };
                        std::vector<Vertex> vertices;
                        vertices.reserve(draw.vertices.size());
                        for (auto v : draw.vertices)
                            vertices.push_back(
                                {v[0] * draw.scale - .5f, v[1] * draw.scale - .5f, 0, 1, v[0], v[1]});
                        check(device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(vertices.size() / 3),
                                                       vertices.data(), sizeof(Vertex)),
                              "GPU RDP hull");
                    }
                }
            }
        } catch (...) {
            device_->EndScene();
            throw;
        }
        check(device_->EndScene(), "end GPU raster scene");
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
        for (unsigned i = 0; i < 12; i++)
            check(device_->SetTexture(i, nullptr), "unbind TMEM destination");
        check(device_->SetRenderTarget(0, t.surface.Get()), "GPU TMEM target");
        D3DVIEWPORT9 view{0, 0, DWORD(t.width), DWORD(t.height), 0, 1};
        check(device_->SetViewport(&view), "GPU TMEM viewport");
        for (unsigned i = 0; i < 3; i++)
            check(device_->SetTexture(i, raster_targets_.at(draw.inputs[i]).texture.Get()),
                  "GPU TMEM source");
        for (unsigned i = 0; i < 2; i++)
            if (draw.auxiliary[i]) {
                auto &t = raster_targets_.at(draw.auxiliary[i]);
                constants[i ? 102 : 104] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
                check(device_->SetTexture(i + 5, t.texture.Get()), "GPU auxiliary input");
            }
        constants[103] = {float(t.width), float(t.height), 1, 0};
        check(device_->SetPixelShaderConstantF(0, constants[0].data(), 112), "GPU TMEM parameters");
        check(device_->SetPixelShader(raster_shader(draw.shader)), "GPU TMEM shader");
        check(device_->BeginScene(), "begin GPU TMEM");
        try {
            raster_quad(float(draw.x), float(draw.y), float(draw.x + draw.width), float(draw.y + draw.height),
                        1);
        } catch (...) {
            device_->EndScene();
            throw;
        }
        check(device_->EndScene(), "end GPU TMEM");
    }
    void present_raster(const RasterPresent &present) override {
        auto hold = raster_guard_ ? nullptr : std::make_unique<Guard>(*this);
        raster_state();
        auto *output = reinterpret_cast<IDirect3DTexture9 *>(present.framebuffer);
        if (!output)
            throw std::runtime_error("D3D9 missing GPU scanout target");
        ComPtr<IDirect3DSurface9> surface;
        check(output->GetSurfaceLevel(0, &surface), "get GPU scanout surface");
        auto &t = raster_targets_.at(present.color);
        auto constants = present.constants;
        constants[0] = {float(t.width), float(t.height), float(t.tw), float(t.th)};
        check(device_->SetRenderTarget(0, surface.Get()), "bind GPU scanout");
        D3DVIEWPORT9 view{0, 0, present.width, present.height, 0, 1};
        check(device_->SetViewport(&view), "GPU scanout viewport");
        check(device_->SetTexture(0, t.texture.Get()), "bind GPU scanout image");
        constants[103] = {float(present.width), float(present.height), 1, 0};
        check(device_->SetPixelShaderConstantF(0, constants[0].data(), 112), "set GPU VI state");
        check(device_->SetPixelShader(raster_shader(RasterShader::Copy)), "set GPU VI shader");
        check(device_->BeginScene(), "begin GPU VI scene");
        try {
            raster_quad(0, 0, float(present.width), float(present.height), 1);
        } catch (...) {
            device_->EndScene();
            throw;
        }
        check(device_->EndScene(), "end GPU VI scene");
    }
    void release_resources() override {
        end_raster();
        raster_specialized_.clear();
        raster_targets_.clear();
        combined_ = alpha0_ = texel0_ = texel1_ = lod_ = 0;
        for (auto &p : raster_shaders_)
            p.Reset();

        for (auto &t : inputs_)
            t.Reset();
        output_.Reset();
        upload_.Reset();
        target_.Reset();
        readback_.Reset();
        for (auto &s : shaders_)
            s.Reset();
        width_ = height_ = 0;
        target_width_ = target_height_ = 0;
    }
    Image execute(Kernel kernel, const Image &a, const Image &b) override {
        validate_images(a, b, caps_.max_texture);
        const unsigned rows = kernel == Kernel::ColorPipeline ? a.height / 8 : a.height;
        if (!rows || (kernel == Kernel::ColorPipeline && a.height % 8))
            throw std::runtime_error("D3D9 invalid packet atlas");
        Guard guard(*this);
        unsigned w = storage_extent(a.width, caps_.padded_power_of_two),
                 h = storage_extent(a.height, caps_.padded_power_of_two);
        w = std::max(w, width_);
        h = std::max(h, height_);
        if (caps_.square_textures)
            w = h = std::max(w, h);
        if (w > caps_.max_texture || h > caps_.max_texture)
            throw std::runtime_error("D3D9 padded texture exceeds caps");
        allocate(w, h, present_target_ ? 0 : rows);
        upload(a, 0);
        upload(b, 1);
        for (unsigned i = 1; i < targets_; ++i)
            check(device_->SetRenderTarget(i, nullptr), "unbind frontend MRT");
        check(device_->SetDepthStencilSurface(nullptr), "unbind depth");
        ComPtr<IDirect3DSurface9> presentation;
        if (present_target_)
            check(present_target_->GetSurfaceLevel(0, &presentation), "get frontend texture surface");
        check(device_->SetRenderTarget(0, present_target_ ? presentation.Get() : target_.Get()),
              "bind target");
        unsigned output_width = present_target_ ? present_width_ : a.width;
        unsigned output_height = present_target_ ? present_height_ : rows;
        const D3DVIEWPORT9 viewport{0, 0, output_width, output_height, 0, 1};
        check(device_->SetViewport(&viewport), "set viewport");
        for (auto [state, value] :
             std::array<std::pair<D3DRENDERSTATETYPE, DWORD>, 17>{{{D3DRS_ZENABLE, FALSE},
                                                                   {D3DRS_ZWRITEENABLE, FALSE},
                                                                   {D3DRS_ALPHATESTENABLE, FALSE},
                                                                   {D3DRS_ALPHABLENDENABLE, FALSE},
                                                                   {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
                                                                   {D3DRS_STENCILENABLE, FALSE},
                                                                   {D3DRS_SCISSORTESTENABLE, FALSE},
                                                                   {D3DRS_FOGENABLE, FALSE},
                                                                   {D3DRS_LIGHTING, FALSE},
                                                                   {D3DRS_DITHERENABLE, FALSE},
                                                                   {D3DRS_SRGBWRITEENABLE, FALSE},
                                                                   {D3DRS_CULLMODE, D3DCULL_NONE},
                                                                   {D3DRS_FILLMODE, D3DFILL_SOLID},
                                                                   {D3DRS_COLORWRITEENABLE, 15},
                                                                   {D3DRS_CLIPPLANEENABLE, 0},
                                                                   {D3DRS_MULTISAMPLEMASK, 0xffffffffu},
                                                                   {D3DRS_WRAP0, 0}}})
            check(device_->SetRenderState(state, value), "set raster state");
        for (unsigned i = 0; i < 2; ++i) {
            check(device_->SetTexture(i, inputs_[i].Get()), "bind input");
            for (auto [state, value] :
                 std::array<std::pair<D3DSAMPLERSTATETYPE, DWORD>, 7>{{{D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP},
                                                                       {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
                                                                       {D3DSAMP_MINFILTER, D3DTEXF_POINT},
                                                                       {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                                                                       {D3DSAMP_MIPFILTER, D3DTEXF_NONE},
                                                                       {D3DSAMP_MAXMIPLEVEL, 0},
                                                                       {D3DSAMP_SRGBTEXTURE, FALSE}}})
                check(device_->SetSamplerState(i, state, value), "set sampler");
        }
        check(device_->SetVertexShader(nullptr), "set fixed vertices");
        check(device_->SetStreamSourceFreq(0, 1), "disable frontend instancing");
        check(device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0), "set texture coordinates");
        check(device_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE),
              "disable frontend texture transform");
        check(device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1), "set vertex format");
        check(device_->SetPixelShader(shader(kernel)), "set kernel");
        float dimensions[]{1.0f / w, 1.0f / h, float(a.width), float(rows)};
        check(device_->SetPixelShaderConstantF(0, dimensions, 1), "set dimensions");
        struct Vertex {
            float x, y, z, w, u, v;
        };
        // D3D9's integer pixel centers require the half-pixel vertex offset.
        float u = float(a.width) / w, v = float(rows) / h;
        const Vertex vertices[] = {{-.5f, -.5f, 0, 1, 0, 0},
                                   {float(output_width) - .5f, -.5f, 0, 1, u, 0},
                                   {-.5f, float(output_height) - .5f, 0, 1, 0, v},
                                   {float(output_width) - .5f, float(output_height) - .5f, 0, 1, u, v}};
        check(device_->BeginScene(), "begin scene");
        HRESULT hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
        HRESULT end = device_->EndScene();
        check(hr, "draw kernel");
        check(end, "end scene");
        if (present_target_)
            return Image{output_width, output_height, {}};
        check(device_->GetRenderTargetData(target_.Get(), readback_.Get()), "readback");
        D3DLOCKED_RECT locked{};
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "lock readback");
        Image out{a.width, rows, std::vector<std::uint8_t>(std::size_t(a.width) * rows * 4)};
        for (unsigned y = 0; y < rows; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(locked.pBits) + std::size_t(y) * locked.Pitch;
            auto *dst = out.rgba.data() + std::size_t(y) * a.width * 4;
            for (unsigned x = 0; x < a.width; ++x) {
                dst[x * 4] = row[x * 4 + 2];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4];
                dst[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        check(readback_->UnlockRect(), "unlock readback");
        return out;
    }
    void present(const Image &image, std::uintptr_t framebuffer, unsigned width, unsigned height) override {
        present_target_ = reinterpret_cast<IDirect3DTexture9 *>(framebuffer);
        if (!present_target_)
            throw std::runtime_error("D3D9 missing frontend frame texture");
        present_width_ = width;
        present_height_ = height;
        try {
            execute(Kernel::Copy, image, image);
        } catch (...) {
            present_target_ = nullptr;
            throw;
        }
        present_target_ = nullptr;
    }
};
} // namespace
std::unique_ptr<Device> make_d3d9(IDirect3DDevice9 *device, bool pot, bool square, Proc compiler) {
    return std::make_unique<D3D9>(device, pot, square, compiler);
}
} // namespace glrdp::legacy
#endif
