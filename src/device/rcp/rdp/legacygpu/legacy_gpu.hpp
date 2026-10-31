#pragma once
#include "../glrdp/framebuffer_storage.hpp"
#include "raster_types.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
struct IDirect3DDevice9;
#endif

namespace glrdp::legacy {
// Native GPU RDP devices using RGBA8 targets and float-only legacy shaders.
// GLRDP supplies command state, triangle setup and CPU/framebuffer coherence.
// Rasterization, interpolation, coverage, Z, color and VI execute on the GPU.
enum class Kernel { Copy, PackDepth, Combine9, Filter3Point, ColorPipeline, Divider };
struct Image {
    unsigned width, height;
    std::vector<std::uint8_t> rgba;
};
struct Capabilities {
    std::string device;
    unsigned max_texture = 0, fragment_samplers = 0;
    bool padded_power_of_two = false;
    bool square_textures = false;
    bool separate_shading = false;
};
class Device : public FramebufferStorage {
  public:
    virtual ~Device() = default;
    virtual const Capabilities &capabilities() const = 0;
    // One RGBA8 output per pass: no integer textures, MRT, float render targets,
    // texture barriers, vertex textures, UBOs or shader bit operators required.
    virtual Image execute(Kernel kernel, const Image &a, const Image &b) = 0;
    virtual void present(const Image &image, std::uintptr_t framebuffer, unsigned width, unsigned height) = 0;
    // Release resources that must not survive a D3D9 Reset/context destruction.
    virtual void release_resources() = 0;
    virtual void begin_raster() = 0;
    virtual void end_raster() = 0;
    virtual void draw_raster(const RasterDraw &) = 0;
    virtual void draw_batch(const std::vector<const RasterDraw *> &) = 0;
    virtual void present_raster(const RasterPresent &) = 0;
    virtual void upload_region(unsigned color, int x, int y, int width, int height,
                               const std::uint8_t *rgba) = 0;
    virtual void texture_pass(const TextureDraw &) = 0;
};
using Proc = void (*)();
using Resolver = Proc (*)(const char *);
std::unique_ptr<Device> make_gl2(Resolver resolver, bool force_power_of_two = false);
std::unique_ptr<Device> make_gles2(Resolver resolver);
#ifdef _WIN32
std::unique_ptr<Device> make_d3d9(IDirect3DDevice9 *device, bool force_power_of_two = false,
                                  bool force_square = false, Proc compiler = nullptr);
#endif
std::string fragment_shader(Kernel kernel, bool hlsl);
std::string fragment_shader(Kernel kernel, ShaderDialect dialect);
void validate_images(const Image &a, const Image &b, unsigned limit);
unsigned storage_extent(unsigned logical, bool power_of_two);
} // namespace glrdp::legacy
