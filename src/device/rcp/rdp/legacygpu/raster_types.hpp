#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace glrdp::legacy {
enum class ShaderDialect { GLSL110, GLSLES100, HLSL3 };
using Float4 = std::array<float, 4>;
// Resolved command state, not resolved pixels. All sample tests, attribute
// interpolation, texture filtering, combining, Z and blending run in shaders.
struct RasterDraw {
    std::array<Float4, 112> constants{};
    std::vector<std::array<float, 2>> vertices;
    unsigned color = 0, depth = 0;
    std::array<unsigned, 2> tiles{};
    std::array<unsigned, 2> auxiliary{};
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int scale = 1;
};
struct RasterPresent {
    unsigned color = 0;
    std::array<Float4, 112> constants{};
    std::uintptr_t framebuffer = 0;
    unsigned width = 640, height = 480;
};
enum class RasterShader {
    Color,
    Depth,
    Present,
    LoadTMEM,
    DecodeTMEM,
    VIReconstruct,
    Copy,
    Noise,
    Shade,
    Alpha0,
    Texel0,
    Texel1,
    LOD,
    CoverageAlpha,
    Candidate
};
struct TextureDraw {
    RasterShader shader = RasterShader::LoadTMEM;
    unsigned target = 0;
    std::array<unsigned, 2> auxiliary{};
    std::array<unsigned, 3> inputs{};
    std::array<Float4, 112> constants{};
    int x = 0, y = 0, width = 1, height = 1;
};
using RasterKey = std::array<unsigned char, 62>;
RasterKey raster_key(const RasterDraw &);
std::string raster_fragment_shader(RasterShader, bool hlsl, const RasterDraw *specialize = nullptr);
std::string raster_fragment_shader(RasterShader, ShaderDialect, const RasterDraw *specialize = nullptr);
} // namespace glrdp::legacy
