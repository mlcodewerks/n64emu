#include "video.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "native.h"
#include "shader_runtime.h"
#include <d3d9.h>
#include <cmath>
#include <fstream>

struct Video::Impl {
    HWND window = nullptr;
    HMODULE module = nullptr;
    ComPtr<IDirect3D9> d3d;
    ComPtr<IDirect3DDevice9> device;
    ComPtr<IDirect3DTexture9> texture;
    D3DPRESENT_PARAMETERS parameters{};
    D3DCAPS9 caps{};
    bool imgui = false, ready = false, dirty = false;
    retro_game_geometry geom{640, 480, 640, 625, 4.0f / 3};
    unsigned frame_w = 0, frame_h = 0, tex_w = 0, tex_h = 0;
    std::vector<uint32_t> pixels;
    std::vector<fs::path> captures;
    shader_runtime shaders;
    struct Target {
        ComPtr<IDirect3DTexture9> texture;
        unsigned width = 0, height = 0;
    };
    std::vector<Target> targets;

    void release_default_resources() {
        shaders.clear(); targets.clear(); texture.Reset();
        tex_w = tex_h = 0; dirty = !pixels.empty();
    }
    bool reset(unsigned w, unsigned h) {
        ready = false;
        if (imgui) ImGui_ImplDX9_InvalidateDeviceObjects();
        release_default_resources();
        parameters.BackBufferWidth = w; parameters.BackBufferHeight = h;
        HRESULT result = device->Reset(&parameters);
        if (result == D3DERR_DEVICELOST) return false;
        check(result, "Reset Direct3D 9 device");
        if (imgui && !ImGui_ImplDX9_CreateDeviceObjects())
            throw std::runtime_error("Cannot recreate Dear ImGui D3D9 resources");
        return ready = true;
    }
    bool available() {
        int w, h;
        native_client_size(window, w, h);
        if (IsIconic(window) || w <= 0 || h <= 0) return ready = false;
        const auto result = device->TestCooperativeLevel();
        if (result == D3DERR_DEVICELOST) return ready = false;
        if (result == D3DERR_DEVICENOTRESET || parameters.BackBufferWidth != unsigned(w) ||
            parameters.BackBufferHeight != unsigned(h)) return reset(w, h);
        check(result, "Check Direct3D 9 device");
        return ready = true;
    }
    void upload() {
        if (!dirty || pixels.empty()) return;
        if (tex_w != frame_w || tex_h != frame_h) {
            texture.Reset();
            check(device->CreateTexture(frame_w, frame_h, 1, D3DUSAGE_DYNAMIC,
                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture.GetAddressOf(), nullptr), "Create N64 frame texture");
            tex_w = frame_w; tex_h = frame_h;
        }
        D3DLOCKED_RECT lock{};
        check(texture->LockRect(0, &lock, nullptr, D3DLOCK_DISCARD), "Lock N64 frame texture");
        for (unsigned y = 0; y < frame_h; ++y)
            std::memcpy(static_cast<uint8_t *>(lock.pBits) + size_t(y) * lock.Pitch,
                        pixels.data() + size_t(y) * frame_w, size_t(frame_w) * 4);
        check(texture->UnlockRect(0), "Upload N64 frame texture");
        dirty = false;
    }
    IDirect3DTexture9 *filtered(unsigned w, unsigned h) {
        auto &control = video_shaders();
        control.init();
        if (!control.settings.enabled || !librashader().d3d9_available || !shaders.prepare(device.Get()))
            return texture.Get();
        shaders.update_parameters();
        targets.resize(shaders.chains.size());
        ComPtr<IDirect3DSurface9> original;
        check(device->GetRenderTarget(0, original.GetAddressOf()), "Get shader render target");
        D3DVIEWPORT9 viewport{};
        device->GetViewport(&viewport);
        ComPtr<IDirect3DStateBlock9> state;
        check(device->CreateStateBlock(D3DSBT_ALL, state.GetAddressOf()), "Save shader render state");
        check(state->Capture(), "Capture shader render state");
        auto *source = texture.Get();
        const auto options = shaders.frame_options<frame_d3d9_opt_t>();
        bool ok = true;
        try {
            for (size_t i = 0; i < targets.size(); ++i) {
                auto &target = targets[i];
                if (target.width != w || target.height != h) {
                    target.texture.Reset();
                    check(device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                        D3DPOOL_DEFAULT, target.texture.GetAddressOf(), nullptr), "Create shader output texture");
                    target.width = w; target.height = h;
                }
                ComPtr<IDirect3DSurface9> surface;
                check(target.texture->GetSurfaceLevel(0, surface.GetAddressOf()), "Get shader output surface");
                if (!librashader().check(librashader().d3d9_filter_chain_frame(&shaders.chains[i]->d3d9,
                    shaders.frame, source, surface.Get(), nullptr, nullptr, &options), control.error)) {
                    ok = false; break;
                }
                source = target.texture.Get();
            }
        } catch (const std::exception &e) { control.error = e.what(); ok = false; }
        // librashader owns BeginScene/EndScene for each D3D9 filter invocation.
        device->SetRenderTarget(0, original.Get()); state->Apply(); device->SetViewport(&viewport);
        if (!ok) { control.settings.enabled = false; return texture.Get(); }
        ++shaders.frame;
        return source;
    }
    ComPtr<IDirect3DSurface9> readback(IDirect3DSurface9 *source) {
        D3DSURFACE_DESC desc{};
        check(source->GetDesc(&desc), "Describe D3D9 surface");
        ComPtr<IDirect3DSurface9> copy;
        check(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
            D3DPOOL_SYSTEMMEM, copy.GetAddressOf(), nullptr), "Create D3D9 readback surface");
        check(device->GetRenderTargetData(source, copy.Get()), "Read D3D9 render target");
        return copy;
    }
    void capture(const fs::path &path) {
        ComPtr<IDirect3DSurface9> back;
        check(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.GetAddressOf()), "Get D3D9 backbuffer");
        auto copy = readback(back.Get());
        D3DSURFACE_DESC desc{}; copy->GetDesc(&desc);
        D3DLOCKED_RECT lock{};
        check(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "Lock D3D9 screenshot");
        std::vector<uint8_t> bytes(size_t(desc.Width) * desc.Height * 4);
        for (unsigned y = 0; y < desc.Height; ++y)
            std::memcpy(bytes.data() + size_t(y) * desc.Width * 4,
                        static_cast<const uint8_t *>(lock.pBits) + size_t(y) * lock.Pitch, size_t(desc.Width) * 4);
        copy->UnlockRect();
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = DWORD(file.bfOffBits + bytes.size());
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info); info.biWidth = desc.Width; info.biHeight = -LONG(desc.Height);
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<char *>(&file), sizeof(file));
        out.write(reinterpret_cast<char *>(&info), sizeof(info));
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        if (!out) throw std::runtime_error("Screenshot write failed");
    }
};

Video::Video() : impl(std::make_unique<Impl>()) {}
Video::~Video() { shutdown(); }
void Video::init(HWND window) {
    impl->window = window;
    impl->module = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl->module) throw std::runtime_error("Direct3D 9 runtime is unavailable");
    using Create = IDirect3D9 *(WINAPI *)(UINT);
    auto create = reinterpret_cast<Create>(GetProcAddress(impl->module, "Direct3DCreate9"));
    if (!create) throw std::runtime_error("Direct3DCreate9 entry point is unavailable");
    impl->d3d.Attach(create(D3D_SDK_VERSION));
    if (!impl->d3d) throw std::runtime_error("Cannot initialize Direct3D 9");
    int w, h; native_client_size(window, w, h);
    auto &p = impl->parameters;
    p.Windowed = TRUE; p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = window;
    p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
    p.BackBufferWidth = std::max(1, w); p.BackBufferHeight = std::max(1, h);
    p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT result = impl->d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, impl->device.GetAddressOf());
    if (FAILED(result))
        result = impl->d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, impl->device.GetAddressOf());
    check(result, "Create Direct3D 9 device");
    check(impl->device->GetDeviceCaps(&impl->caps), "Read D3D9 capabilities");
    if (!ImGui_ImplDX9_Init(impl->device.Get())) throw std::runtime_error("Dear ImGui D3D9 initialization failed");
    impl->imgui = impl->ready = true;
}
void Video::shutdown() {
    if (!impl) return;
    stop_core();
    if (impl->imgui) ImGui_ImplDX9_Shutdown();
    impl->imgui = impl->ready = false;
    impl->device.Reset(); impl->d3d.Reset();
    if (impl->module) FreeLibrary(impl->module);
    impl->module = nullptr;
}
void Video::start_core(const retro_game_geometry &geom) { geometry(geom, true); }
void Video::stop_core() {
    impl->release_default_resources();
    impl->pixels.clear(); impl->frame_w = impl->frame_h = 0; impl->dirty = false;
}
void Video::geometry(const retro_game_geometry &geom, bool) {
    if (!geom.base_width || !geom.base_height || !std::isfinite(geom.aspect_ratio))
        throw std::runtime_error("Invalid core geometry");
    impl->geom = geom;
}
bool Video::set_format(retro_pixel_format value) { return value == RETRO_PIXEL_FORMAT_XRGB8888; }
unsigned Video::width() const { return impl->frame_w ? impl->frame_w : impl->geom.base_width; }
unsigned Video::height() const { return impl->frame_h ? impl->frame_h : impl->geom.base_height; }
float Video::aspect() const {
    if (!square_pixels && impl->geom.aspect_ratio > 0)
        return impl->geom.aspect_ratio;
    return float(width()) / height();
}
vp Video::viewport(unsigned w, unsigned h, unsigned top) const {
    top = std::min(top, h);
    h -= top;
    if (!w || !h) return {0, top, 0, 0};

    const double ratio = aspect();
    double fitted_height = std::min(double(h), w / ratio);
    if (integer_scale) {
        // Keep whole source scanlines. With square pixels this also scales
        // both axes by the same integer. Allow rounding width to a screen pixel.
        const double width_limit = (w + 0.5) / (height() * ratio);
        const unsigned scale = unsigned(std::min(double(h / height()), width_limit));
        if (scale) fitted_height = height() * scale;
    }
    const auto dw = unsigned(std::clamp(std::round(fitted_height * ratio), 1.0, double(w)));
    const auto dh = unsigned(std::clamp(std::round(fitted_height), 1.0, double(h)));
    return {(w - dw) / 2, top + (h - dh) / 2, dw, dh};
}
void Video::refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;
    if (!w || !h || w > impl->caps.MaxTextureWidth || h > impl->caps.MaxTextureHeight ||
        pitch < size_t(w) * 4 || pitch > SIZE_MAX / h) throw std::runtime_error("Invalid N64 video frame");
    impl->pixels.resize(size_t(w) * h);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            uint32_t color;
            std::memcpy(&color, static_cast<const uint8_t *>(data) + size_t(y) * pitch + size_t(x) * 4, 4);
            impl->pixels[size_t(y) * w + x] = color | 0xff000000u;
        }
    impl->frame_w = w; impl->frame_h = h; impl->dirty = true;
}
void Video::new_frame() { impl->available(); ImGui_ImplDX9_NewFrame(); }
void Video::draw(unsigned w, unsigned h, unsigned top) {
    if (!impl->ready || impl->pixels.empty()) return;
    const bool uploaded = impl->dirty;
    impl->upload();
    if (verify_pixels && uploaded) verify_software(nullptr, width(), height(), 0);
    const auto view = viewport(w, h, top);
    if (!view.width || !view.height) return;
    video_shaders().source_aspect = aspect();
    auto *texture = impl->filtered(view.width, view.height);
    auto *background = ImGui::GetBackgroundDrawList();
    auto &platform = ImGui::GetPlatformIO();
    background->AddCallback(platform.DrawCallback_SetSamplerNearest, nullptr);
    background->AddImage(ImTextureRef(ImTextureID(reinterpret_cast<uintptr_t>(texture))),
        ImVec2(float(view.x), float(view.y)), ImVec2(float(view.x + view.width), float(view.y + view.height)));
    background->AddCallback(platform.DrawCallback_SetSamplerLinear, nullptr);
}
void Video::present() {
    if (!impl->ready) { impl->captures.clear(); return; }
    impl->device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1, 0);
    HRESULT result = impl->device->BeginScene();
    if (result == D3DERR_DEVICELOST) { impl->ready = false; return; }
    check(result, "Begin D3D9 scene");
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    check(impl->device->EndScene(), "End D3D9 scene");
    for (const auto &path : impl->captures) impl->capture(path);
    impl->captures.clear();
    result = impl->device->Present(nullptr, nullptr, nullptr, nullptr);
    if (result == D3DERR_DEVICELOST) impl->ready = false;
    else check(result, "Present D3D9 frame");
}
void Video::capture(const fs::path &path) { impl->captures.push_back(path); }
void Video::verify_software(const void *, unsigned w, unsigned h, size_t) {
    ComPtr<IDirect3DSurface9> source, target;
    check(impl->texture->GetSurfaceLevel(0, source.GetAddressOf()), "Get N64 frame surface");
    check(impl->device->CreateRenderTarget(w, h, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
        0, FALSE, target.GetAddressOf(), nullptr), "Create verification target");
    check(impl->device->StretchRect(source.Get(), nullptr, target.Get(), nullptr, D3DTEXF_NONE), "Copy GPU frame for verification");
    auto copy = impl->readback(target.Get());
    D3DLOCKED_RECT lock{};
    check(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "Lock GPU verification surface");
    bool equal = true;
    for (unsigned y = 0; y < h && equal; ++y)
        equal = !std::memcmp(static_cast<const uint8_t *>(lock.pBits) + size_t(y) * lock.Pitch,
                            impl->pixels.data() + size_t(y) * w, size_t(w) * 4);
    copy->UnlockRect();
    if (!equal) throw std::runtime_error("D3D9 GPU frame verification failed");
    ++verified_frames;
}
