#pragma once
#include "../glrdp/framebuffer_manager.hpp"
#include "../glrdp/primitive_scheduler.hpp"
#include "../glrdp/rdp_parser.hpp"
#include "../glrdp/texture_decode_cache.hpp"
#include "../glrdp/vi_field.hpp"
#include "legacy_gpu.hpp"
#include <map>

namespace glrdp::legacy {
class RasterRenderer {
  public:
    RasterRenderer(Device &, N64Memory &, int scale);
    ~RasterRenderer();
    void triangle(const RDPTriangle &, const RDPState &);
    void rectangle(const RDPTextureRectangle &, const RDPState &);
    void fill(const RDPFillRectangle &, const RDPState &);
    void texture_read(const RDPState &, RDPOpcode, std::uint64_t);
    bool load_texture(RDPState &, RDPOpcode, std::uint64_t);
    void materialize_tmem(RDPState &);
    void read(std::uint32_t, std::uint32_t);
    void write(std::uint32_t, std::uint32_t);
    void pages(std::uint8_t *, unsigned) const;
    void flush();
    void drain();
    void reset();
    RasterPresent scanout(std::uint32_t status, std::uint32_t origin, std::uint32_t width,
                          std::uint32_t hstart, std::uint32_t vstart, std::uint32_t xscale,
                          std::uint32_t yscale, std::uint32_t vsync, std::uint32_t current);
    FramebufferManager &framebuffers() { return fb_; }
    std::uint64_t triangles = 0, rectangles = 0, fills = 0;

  private:
    std::vector<RasterDraw> queued_;
    std::vector<PrimitiveFootprint> footprints_;
    PrimitiveScheduler scheduler_;
    RDPImage queued_image_{};
    int queued_height_ = 0;
    Device &device_;
    N64Memory &memory_;
    FramebufferManager fb_;
    int scale_;
    unsigned white_ = 0, black_ = 0;
    unsigned atlas_texture_ = 0;
    TextureAtlas atlas_;
    TextureDecodeCache decode_cache_;
    std::uint64_t atlas_epoch_ = 0;
    std::uint64_t tmem_serial_ = 0;
    unsigned tmem_map_ = 0, tmem_cpu_ = 0;
    std::vector<std::shared_ptr<RDPTMEMBacking>> snapshots_;
    struct Depth {
        unsigned color = 0, fbo = 0;
        std::uint32_t address = 0;
        int width = 0, height = 0, scale = 1;
        bool dirty = false, cpu_dirty = false;
        std::vector<std::uint8_t> snapshot;
    };
    std::map<std::uint32_t, Depth> depth_;
    Depth coverage_;
    struct Tile {
        std::uint64_t key = 0;
        unsigned color = 0;
        int width = 1, height = 1;
    };
    std::array<Tile, 8> tiles_{};
    VIFieldHistory fields_;
    unsigned vi_filter_ = 0;
    int vi_width_ = 0, vi_height_ = 0;
    std::array<unsigned, 2> vi_history_{};
    unsigned vi_index_ = 0;
    unsigned noise_ = 0, reciprocal_ = 0;
    int noise_width_ = 0, noise_height_ = 0;
    void ensure_noise(int, int);
    GLFramebuffer *target(const RDPState &, float);
    Depth &depth(const RDPState &, GLFramebuffer &);
    void import_depth(Depth &);
    void export_depth(Depth &);
    void depth_range(std::uint32_t, std::uint32_t);
    unsigned tile(const RDPState &, unsigned, Float4 &, Float4 &, Float4 &);
    TextureAtlas::Region atlas_tile(const RDPState &, unsigned);
    void state(RasterDraw &, const RDPState &, GLFramebuffer &, bool, unsigned);
    void submit(RasterDraw &, const RDPState &, GLFramebuffer &);
};
} // namespace glrdp::legacy
