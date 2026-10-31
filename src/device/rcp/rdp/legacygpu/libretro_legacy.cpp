#include "libretro_legacy.h"
#include "raster_renderer.hpp"
#include "renderer_options.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <tuple>
#ifdef HAVE_D3D9
#include <libretro_d3d9.h>
#include <wrl/client.h>
#endif
extern "C" {
#include "core/backend.h"
#include "rdp_core.h"
extern renderer_memory gfx_info;
extern retro_log_printf_t log_cb;
extern bool libretro_swap_buffer;
}
namespace {
using namespace glrdp::legacy;
retro_environment_t environment = nullptr;
retro_hw_render_callback hardware{};
std::unique_ptr<Device> device;
std::unique_ptr<RasterRenderer> renderer;
std::unique_ptr<RDPCommandParser> parser;
N64Memory rdram_memory;
bool d3d = false, ready = false, opened = false;
legacygpu_backend backend = LEGACYGPU_GL2;
std::vector<std::uint8_t> commands;
#ifdef HAVE_D3D9
const retro_hw_render_interface_d3d9 *iface = nullptr;
struct FrameTexture {
    Microsoft::WRL::ComPtr<IDirect3DTexture9> texture;
    unsigned width = 0, height = 0;
};
std::array<FrameTexture, 32> frames;
#endif
void error(const char *where, const std::exception &e) {
    if (log_cb)
        log_cb(RETRO_LOG_ERROR, "Legacy GPU %s: %s\n", where, e.what());
    ready = false;
}
struct Lock {
#ifdef HAVE_D3D9
    const retro_hw_render_interface_d3d9 *held = nullptr;
    Lock() {
        if (d3d && iface) {
            held = iface;
            held->lock_context(held->handle);
        }
    }
    ~Lock() {
        if (held)
            held->unlock_context(held->handle);
    }
#endif
};
struct Session {
    Lock lock;
    Device *held = nullptr;
    Session() {
        if (device) {
            held = device.get();
            held->begin_raster();
        }
    }
    ~Session() {
        if (held)
            held->end_raster();
    }
};
void create_renderer() {
    if (opened && device && !renderer)
        renderer = std::make_unique<RasterRenderer>(*device, rdram_memory, renderer_settings.upscale);
}
void context_destroy() {
    ready = false;
    Lock lock;
    if (device) {
        try {
            device->begin_raster();
            if (renderer) {
                renderer->flush();
                if (parser)
                    renderer->materialize_tmem(parser->state());
            }
            renderer.reset();
            device->end_raster();
        } catch (const std::exception &e) {
            error("preserve context", e);
            renderer.reset();
            device->end_raster();
        }
    }
#ifdef HAVE_D3D9
    if (iface)
        iface->set_texture(iface->handle, nullptr);
    for (auto &f : frames)
        f = {};
#endif
    device.reset();
#ifdef HAVE_D3D9
    iface = nullptr;
#endif
}
void context_reset() {
    try {
        if (device)
            context_destroy();
        if (d3d) {
#ifdef HAVE_D3D9
            const retro_hw_render_interface *base = nullptr;
            if (!environment(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &base) || !base ||
                base->interface_type != RETRO_HW_RENDER_INTERFACE_D3D9 || base->interface_version < 1)
                throw std::runtime_error("D3D9 frontend interface unavailable");
            iface = reinterpret_cast<const retro_hw_render_interface_d3d9 *>(base);
            if (!iface->device || !iface->lock_context || !iface->unlock_context || !iface->set_texture ||
                !iface->get_sync_index || !iface->get_sync_index_mask || !iface->wait_sync_index)
                throw std::runtime_error("D3D9 frontend lacks synchronization callbacks");
            Lock lock;
            device = make_d3d9(iface->device, false, false, reinterpret_cast<Proc>(iface->D3DCompile));
#else
            throw std::runtime_error("D3D9 was not compiled for this platform");
#endif
        } else {
            if (!hardware.get_proc_address || !hardware.get_current_framebuffer)
                throw std::runtime_error("Legacy GL frontend callbacks unavailable");
            if (backend == LEGACYGPU_GLES2) {
#ifdef HAVE_GLES2_RDP
                device = make_gles2(reinterpret_cast<Resolver>(hardware.get_proc_address));
#else
                throw std::runtime_error("GLES2 was not compiled for this platform");
#endif
            } else {
#ifdef HAVE_GL2_RDP
                device = make_gl2(reinterpret_cast<Resolver>(hardware.get_proc_address));
#else
                throw std::runtime_error("GL2 was not compiled for this platform");
#endif
            }
        }
        {
            Session session;
            create_renderer();
        }
        ready = true;
        if (log_cb)
            log_cb(RETRO_LOG_INFO,
                   "Legacy GPU: %s; native GPU RDP rasterization, GLRDP triangle/framebuffer model.\n",
                   device->capabilities().device.c_str());
    } catch (const std::exception &e) {
        error("context reset", e);
    }
}
auto fields(RDPState &s) {
    return std::tie(s.tmem, s.tiles, s.texture_image, s.color_image, s.z_image_address, s.other_modes,
                    s.combine, s.scissor_xh, s.scissor_yh, s.scissor_xl, s.scissor_yl, s.fill_color, s.fog,
                    s.blend, s.primitive, s.environment, s.primitive_min_lod, s.primitive_lod_fraction,
                    s.primitive_z, s.primitive_dz, s.convert, s.key, s.tmem_generation);
}
constexpr std::uint64_t state_magic = UINT64_C(0x324550554744524c);
constexpr unsigned pending_capacity = 512;
unsigned registers_size() {
    RDPState s;
    unsigned n = 0;
    std::apply([&](auto &...x) { ((n += sizeof(x)), ...); }, fields(s));
    return n;
}
} // namespace
extern "C" bool legacygpu_request_context(retro_environment_t env, legacygpu_backend selected) {
    environment = env;
    backend = selected;
    d3d = selected == LEGACYGPU_D3D9;
    hardware = {};
    hardware.context_reset = context_reset;
    hardware.context_destroy = context_destroy;
    hardware.cache_context = false;
    hardware.bottom_left_origin = !d3d;
    hardware.context_type = backend == LEGACYGPU_GLES2 ? RETRO_HW_CONTEXT_OPENGLES2 : RETRO_HW_CONTEXT_OPENGL;
#ifdef HAVE_D3D9
    if (d3d) hardware.context_type = RETRO_HW_CONTEXT_D3D9;
#else
    if (d3d) return false;
#endif
    hardware.version_major = d3d ? 9 : 2;
    if (env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hardware))
        return true;
    if (d3d) {
        hardware.context_type = RETRO_HW_CONTEXT_DIRECT3D;
        return env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hardware);
    }
    return false;
}
extern "C" bool legacygpu_context_ready() { return ready; }
extern "C" bool legacygpu_open(std::uint8_t *rdram, unsigned bytes) {
    try {
        rdram_memory.bind(rdram, bytes, true);
        parser = std::make_unique<RDPCommandParser>();
        parser->set_memory(&rdram_memory);
        opened = true;
        auto &cb = parser->callbacks();
        cb.draw_triangle = [](auto &t, auto &s) {
            if (renderer)
                renderer->triangle(t, s);
        };
        cb.draw_texture_rectangle = [](auto &r, auto &s) {
            if (renderer)
                renderer->rectangle(r, s);
        };
        cb.draw_fill_rectangle = [](auto &r, auto &s) {
            if (renderer)
                renderer->fill(r, s);
        };
        cb.load_texture = [](auto &s, auto op, auto command) {
            return renderer && renderer->load_texture(s, op, command);
        };
        cb.before_texture_load = [](auto &s, auto op, auto command) {
            if (renderer)
                renderer->texture_read(s, op, command);
        };
        cb.sync_full = [] { *gfx_info.MI_INTR_REG |= 0x20; };
        if (ready) {
            Session session;
            create_renderer();
        }
        return true;
    } catch (const std::exception &e) {
        error("content open", e);
        return false;
    }
}
extern "C" void legacygpu_close() {
    try {
        if (device) {
            Session session;
            if (renderer) {
                if (log_cb)
                    log_cb(
                        RETRO_LOG_INFO,
                        "Legacy GPU raster: triangles=%llu rectangles=%llu fills=%llu; software spans=0.\n",
                        (unsigned long long)renderer->triangles, (unsigned long long)renderer->rectangles,
                        (unsigned long long)renderer->fills);
                renderer->flush();
                renderer.reset();
            }
        }
    } catch (const std::exception &e) {
        error("content close", e);
        renderer.reset();
    }
    parser.reset();
    commands.clear();
    opened = false;
}
extern "C" void legacygpu_reset() {
    try {
        if (device) {
            Session session;
            if (renderer)
                renderer->reset();
        }
        if (parser)
            parser->reset();
    } catch (const std::exception &e) {
        error("state reset", e);
    }
}
extern "C" bool legacygpu_materialize() {
    if (!ready || !renderer)
        return false;
    try {
        Session session;
        renderer->flush();
        if (parser)
            renderer->materialize_tmem(parser->state());
        return true;
    } catch (const std::exception &e) {
        error("framebuffer flush", e);
        return false;
    }
}
extern "C" void legacygpu_process_commands() {
    if (!ready || !opened || !parser || !renderer)
        return;
    const auto start = *gfx_info.DPC_CURRENT_REG & 0xfffff8u, end = *gfx_info.DPC_END_REG & 0xfffff8u;
    if ((*gfx_info.DPC_STATUS_REG & 2) || end <= start || end - start > 0x1000000)
        return;
    try {
        Session session;
        commands.resize(end - start);
        const auto *hle = rdp_hle_command_buffer(start);
        if (hle)
            for (unsigned i = 0; i < commands.size(); i++)
                commands[i] = std::uint8_t(hle[i / 4] >> ((3 - (i & 3)) * 8));
        else if (*gfx_info.DPC_STATUS_REG & 1)
            for (unsigned i = 0; i < commands.size(); i++)
                commands[i] = gfx_info.DMEM[((start + i) & 4095) ^ 3];
        else
            rdram_memory.copy_be(start, commands.data(), commands.size());
        parser->feed(commands);
        renderer->drain();
    } catch (const std::exception &e) {
        error("command list", e);
        parser->discard_pending();
    }
    *gfx_info.DPC_CURRENT_REG = end;
}
extern "C" void legacygpu_scanout() {
    if (!ready || !opened || !renderer || !parser)
        return;
    try {
        RasterPresent output;
#ifdef HAVE_D3D9
        unsigned index = 0;
        if (d3d) {
            index = iface->get_sync_index(iface->handle);
            if (index >= 32 || !(iface->get_sync_index_mask(iface->handle) & (1u << index)))
                throw std::runtime_error("D3D9 invalid sync index");
            iface->wait_sync_index(iface->handle);
        }
#endif
        Session session;
        output = renderer->scanout(*gfx_info.VI_STATUS_REG, *gfx_info.VI_ORIGIN_REG & 0xffffff,
                                   *gfx_info.VI_WIDTH_REG, *gfx_info.VI_H_START_REG, *gfx_info.VI_V_START_REG,
                                   *gfx_info.VI_X_SCALE_REG, *gfx_info.VI_Y_SCALE_REG,
                                   *gfx_info.VI_V_SYNC_REG, *gfx_info.VI_V_CURRENT_LINE_REG);
#ifdef HAVE_D3D9
        if (d3d) {
            auto &f = frames[index];
            const auto &caps = device->capabilities();
            unsigned w = storage_extent(output.width, caps.padded_power_of_two),
                     h = storage_extent(output.height, caps.padded_power_of_two);
            if (caps.square_textures)
                w = h = std::max(w, h);
            if (!f.texture || f.width != w || f.height != h) {
                f = {};
                if (FAILED(iface->device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                                        D3DPOOL_DEFAULT, &f.texture, nullptr)))
                    throw std::runtime_error("D3D9 scanout allocation failed");
                f.width = w;
                f.height = h;
            }
            output.framebuffer = reinterpret_cast<std::uintptr_t>(f.texture.Get());
        } else
#endif
            output.framebuffer = hardware.get_current_framebuffer();
        device->present_raster(output);
#ifdef HAVE_D3D9
        if (d3d)
            iface->set_texture(iface->handle, frames[index].texture.Get());
#endif
        libretro_swap_buffer = true;
    } catch (const std::exception &e) {
        error("VI", e);
    }
}
extern "C" void legacygpu_framebuffer_read(std::uint32_t address, std::uint32_t bytes) {
    if (!ready || !renderer)
        return;
    try {
        Session session;
        renderer->read(address, bytes);
    } catch (const std::exception &e) {
        error("framebuffer read", e);
    }
}
extern "C" void legacygpu_framebuffer_write(std::uint32_t address, std::uint32_t bytes) {
    if (renderer)
        renderer->write(address, bytes);
}
extern "C" bool legacygpu_framebuffer_pages(std::uint8_t *pages, unsigned count) {
    if (!renderer)
        return false;
    renderer->pages(pages, count);
    return true;
}
extern "C" void legacygpu_framebuffer_info(void *data) {
    auto *out = static_cast<core_framebuffer_info *>(data);
    std::memset(out, 0, 6 * sizeof(*out));
    if (!renderer)
        return;
    unsigned i = 0;
    for (auto &[_, f] : renderer->framebuffers().buffers()) {
        if (i == 5)
            break;
        out[i++] = {f.address,
                    std::max(1u, 1u << (unsigned(f.size) - unsigned(f.size != RDPImageSize::Bits4))),
                    unsigned(f.native_width), unsigned(f.native_height)};
    }
}
extern "C" unsigned legacygpu_state_size() {
    return 16 + registers_size() + 4 + pending_capacity + unsigned(rdram_memory.hidden_size());
}
extern "C" void legacygpu_save_state(void *data) {
    auto *out = static_cast<std::uint8_t *>(data);
    const std::uint64_t size = legacygpu_state_size();
    std::memcpy(out, &state_magic, 8);
    std::memcpy(out + 8, &size, 8);
    out += 16;
    RDPState empty;
    auto &s = parser ? parser->state() : empty;
    std::apply([&](auto &...x) { ((std::memcpy(out, &x, sizeof(x)), out += sizeof(x)), ...); }, fields(s));
    const auto pending = parser ? parser->pending_bytes() : std::span<const std::uint8_t>{};
    const auto count = std::uint32_t(std::min<std::size_t>(pending.size(), pending_capacity));
    std::memcpy(out, &count, 4);
    out += 4;
    std::memset(out, 0, pending_capacity);
    if (count)
        std::memcpy(out, pending.data(), count);
    out += pending_capacity;
    std::memcpy(out, rdram_memory.hidden_data(), rdram_memory.hidden_size());
}
extern "C" void legacygpu_load_state(const void *data) {
    if (!parser)
        return;
    auto *in = static_cast<const std::uint8_t *>(data);
    std::uint64_t magic = 0, size = 0;
    std::memcpy(&magic, in, 8);
    std::memcpy(&size, in + 8, 8);
    if (magic != state_magic || size != legacygpu_state_size())
        return;
    legacygpu_reset();
    in += 16;
    auto &s = parser->state();
    std::apply([&](auto &...x) { ((std::memcpy(&x, in, sizeof(x)), in += sizeof(x)), ...); }, fields(s));
    std::uint32_t count = 0;
    std::memcpy(&count, in, 4);
    in += 4;
    if (count <= pending_capacity)
        parser->restore_pending({in, count});
    in += pending_capacity;
    std::memcpy(rdram_memory.hidden_data(), in, rdram_memory.hidden_size());
}
extern "C" std::uint8_t *legacygpu_hidden_store(std::size_t *bytes) {
    if (bytes)
        *bytes = rdram_memory.hidden_size();
    return rdram_memory.hidden_data();
}
