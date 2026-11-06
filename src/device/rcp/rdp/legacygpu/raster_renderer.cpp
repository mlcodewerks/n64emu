#include "raster_renderer.hpp"
#include "../glrdp/combiner_decode.hpp"
#include "../glrdp/rdp_profile.hpp"
#include "../glrdp/depth_codec.hpp"
#include "../glrdp/rdp_combiner_usage.hpp"
#include "../glrdp/texture_decode_cache.hpp"
#include "../glrdp/triangle_geometry.hpp"
#include "renderer_options.h"
#include "tc_reciprocal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace glrdp::legacy {
namespace {
Float4 color(RDPColor c) { return {float(c.r), float(c.g), float(c.b), float(c.a)}; }
unsigned bytes_per_pixel(RDPImageSize s) { return (4u << unsigned(s)) / 8; }
bool overlaps(std::uint32_t a, std::uint64_t n, std::uint32_t b, std::uint64_t m) {
    return a < std::uint64_t(b) + m && b < std::uint64_t(a) + n;
}
std::vector<std::uint8_t> flip(const DecodedTexture &t) {
    std::vector<std::uint8_t> out(t.rgba.size());
    for (unsigned y = 0; y < t.height; y++)
        std::memcpy(out.data() + std::size_t(t.height - 1 - y) * t.width * 4,
                    t.rgba.data() + std::size_t(y) * t.width * 4, t.width * 4);
    return out;
}
} // namespace
RasterRenderer::RasterRenderer(Device &d, N64Memory &m, int scale)
    : device_(d), memory_(m), scale_(std::clamp(scale, 1, 4)) {
    fb_.set_storage(&d);
    fb_.set_memory(&m);
    atlas_.size = std::min(2048, device_.maximum_texture_size());
    unsigned atlas_fbo = 0;
    device_.allocate(atlas_texture_, atlas_fbo, atlas_.size, atlas_.size);
    device_.allocate(tmem_map_, atlas_fbo, 64, 64);
    device_.allocate(tmem_cpu_, atlas_fbo, 64, 16);
    unsigned f = 0;
    device_.allocate(reciprocal_, f, 64, 1);
    std::array<std::uint8_t, 256> rom{};
    for (unsigned i = 0; i < 64; i++) {
        rom[i * 4] = std::uint8_t(tc_points[i]);
        rom[i * 4 + 1] = std::uint8_t(tc_points[i] >> 8);
        rom[i * 4 + 2] = std::uint8_t(tc_slopes[i]);
        rom[i * 4 + 3] = std::uint8_t(tc_slopes[i] >> 8);
    }
    device_.upload_region(reciprocal_, 0, 0, 64, 1, rom.data());
    device_.allocate(white_, f, 1, 1);
    device_.allocate(black_, f, 1, 1);
    const std::uint8_t white[] = {255, 255, 255, 255}, black[] = {0, 0, 0, 255};
    device_.upload(white_, 1, 1, white);
    device_.upload(black_, 1, 1, black);
}
RasterRenderer::~RasterRenderer() {
    queued_.clear();
    footprints_.clear();
    scheduler_.clear();
    reset();
    device_.release(reciprocal_, reciprocal_);
    device_.release(white_, white_);
    device_.release(black_, black_);
    device_.release(atlas_texture_, atlas_texture_);
    device_.release(tmem_map_, tmem_map_);
    device_.release(tmem_cpu_, tmem_cpu_);
}
void RasterRenderer::reset() {
    drain();
    for (auto &snapshot : snapshots_)
        device_.release(snapshot->texture, snapshot->fbo);
    snapshots_.clear();
    if (noise_)
        device_.release(noise_, noise_);
    noise_ = 0;
    noise_width_ = noise_height_ = 0;
    if (vi_filter_)
        device_.release(vi_filter_, vi_filter_);
    vi_filter_ = 0;
    vi_width_ = vi_height_ = 0;
    for (auto &f : vi_history_) {
        if (f)
            device_.release(f, f);
        f = 0;
    }
    vi_index_ = 0;
    fb_.clear();
    if (coverage_.color)
        device_.release(coverage_.color, coverage_.fbo);
    coverage_ = {};
    for (auto &[_, z] : depth_)
        device_.release(z.color, z.fbo);
    depth_.clear();
    for (auto &t : tiles_) {
        if (t.color)
            device_.release(t.color, t.color);
        t = {};
    }
    fields_.reset();
    atlas_.clear();
    decode_cache_.clear();
    ++atlas_epoch_;
}
void RasterRenderer::ensure_noise(int width, int height) {
    width = std::max({width, 640, noise_width_});
    height = std::max({height, 480, noise_height_});
    if (noise_ && width == noise_width_ && height == noise_height_)
        return;
    drain();
    if (noise_)
        device_.release(noise_, noise_);
    unsigned id = 0;
    device_.allocate(noise_, id, width, height);
    noise_width_ = width;
    noise_height_ = height;
    TextureDraw draw;
    draw.shader = RasterShader::Noise;
    draw.target = noise_;
    draw.inputs = {white_, white_, white_};
    draw.width = width;
    draw.height = height;
    device_.texture_pass(draw);
}
GLFramebuffer *RasterRenderer::target(const RDPState &s, float bottom) {
    const auto &image = s.color_image;
    if (image.address >= memory_.size())
        return nullptr;
    const auto row = (std::uint64_t(image.width) * (4u << unsigned(image.size)) + 7) / 8;
    if (!row)
        return nullptr;
    const int available = int(std::min<std::uint64_t>((memory_.size() - image.address) / row, 4096));
    if (available < 1)
        return nullptr;
    int height = std::max(int(std::ceil(bottom)), int((s.scissor_yl + 3) / 4));
    height = fb_.drawing_height(image, height);
    height = std::clamp(height, 1, available);
    if (!queued_.empty() &&
        (image.address != queued_image_.address || image.width != queued_image_.width ||
         image.size != queued_image_.size || image.format != queued_image_.format || height > queued_height_))
        drain();
    auto &f = fb_.bind_color_image(image, height, s.z_image_address, scale_, false);
    if (f.cpu_dirty) {
        if (f.gpu_dirty && !f.raw_z)
            fb_.download_to_rdram(f, memory_);
        fb_.upload_from_rdram(f, memory_);
    }
    return f.color ? &f : nullptr;
}
void RasterRenderer::import_depth(Depth &z) {
    std::vector<std::uint8_t> pixels(std::size_t(z.width * z.scale) * z.height * z.scale * 4);
    const std::size_t bytes = std::size_t(z.width) * z.height * 2;
    z.snapshot.resize(bytes);
    memory_.copy_be(z.address, z.snapshot.data(), bytes);
    for (int y = 0; y < z.height * z.scale; y++)
        for (int x = 0; x < z.width * z.scale; x++) {
            const unsigned at = ((y / z.scale) * z.width + x / z.scale) * 2;
            const auto word = std::uint16_t((z.snapshot[at] << 8) | z.snapshot[at + 1]);
            const auto depth = glrdp_z_decompress(word);
            unsigned dz = ((word & 3) << 2) | memory_.read_hidden16(z.address + at);
            auto *p = pixels.data() + (std::size_t(z.height * z.scale - 1 - y) * z.width * z.scale + x) * 4;
            p[0] = depth;
            p[1] = depth >> 8;
            p[2] = (depth >> 16) | (dz << 2);
            p[3] = 0;
        }
    device_.upload(z.color, z.width * z.scale, z.height * z.scale, pixels.data());
    z.cpu_dirty = z.dirty = false;
}
RasterRenderer::Depth &RasterRenderer::depth(const RDPState &s, GLFramebuffer &f) {
    auto &z = depth_[s.z_image_address];
    int height = std::max(
        1, std::min<int>(f.native_height,
                         int((memory_.size() - std::min<std::size_t>(s.z_image_address, memory_.size())) /
                             (std::size_t(f.native_width) * 2))));
    if (!z.color || z.width != f.native_width || z.height != height || z.scale != f.internal_scale) {
        if (z.color) {
            drain();
            export_depth(z);
            device_.release(z.color, z.fbo);
        }
        z = {};
        z.address = std::min<std::uint32_t>(s.z_image_address, unsigned(memory_.size() - 2));
        z.width = f.native_width;
        z.height = height;
        z.scale = f.internal_scale;
        if (std::uint64_t(z.address) + std::uint64_t(z.width) * z.height * 2 > memory_.size()) {
            z.address = 0;
            z.width = f.native_width;
            z.height = f.native_height;
        }
        device_.allocate(z.color, z.fbo, z.width * z.scale, z.height * z.scale);
        import_depth(z);
    } else if (z.cpu_dirty || !memory_.matches_be(z.address, z.snapshot.data(), z.snapshot.size())) {
        drain();
        export_depth(z);
        import_depth(z);
    }
    return z;
}
void RasterRenderer::export_depth(Depth &z) {
    if (!z.dirty)
        return;
    const int width = z.width * z.scale, height = z.height * z.scale;
    std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4);
    device_.read(z.fbo, 0, 0, width, height, rgba.data());
    for (int y = 0; y < z.height; y++)
        for (int x = 0; x < z.width; x++) {
            const auto at = unsigned((y * z.width + x) * 2), address = z.address + at;
            if (memory_.read8(address) != z.snapshot[at] || memory_.read8(address + 1) != z.snapshot[at + 1])
                continue;
            const auto *p = rgba.data() + (std::size_t(height - 1 - (y * z.scale + z.scale / 2)) * width +
                                           x * z.scale + z.scale / 2) *
                                              4;
            const auto depth =
                std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2] & 3) << 16);
            const auto dz = (p[2] >> 2) & 15;
            memory_.write16(address, glrdp_z_compress(depth, dz));
            memory_.write_hidden16(address, dz & 3);
        }
    memory_.copy_be(z.address, z.snapshot.data(), z.snapshot.size());
    z.dirty = false;
    fb_.depth_written_to_ram(z.address, unsigned(z.snapshot.size()));
}
void RasterRenderer::depth_range(std::uint32_t address, std::uint32_t bytes) {
    for (auto &[_, z] : depth_)
        if (overlaps(address, bytes, z.address, z.snapshot.size()))
            export_depth(z);
}
TextureAtlas::Region RasterRenderer::atlas_tile(const RDPState &s, unsigned index) {
    const auto key = TextureDecodeCache::key(s, index);
    if (s.gpu_tmem) {
        auto &snapshot = *s.gpu_tmem;
        if (snapshot.atlas_epoch != atlas_epoch_) {
            snapshot.tiles.clear();
            snapshot.atlas_epoch = atlas_epoch_;
        }
        for (auto &entry : snapshot.tiles)
            if (entry.parameters == key.parameters)
                return entry.region;
        const auto storage = rdp_tile_storage(s, index, true);
        auto region = atlas_.allocate(storage.width, storage.height);
        if (!region[2]) {
            drain();
            atlas_.clear();
            decode_cache_.clear();
            ++atlas_epoch_;
            snapshot.tiles.clear();
            snapshot.atlas_epoch = atlas_epoch_;
            region = atlas_.allocate(storage.width, storage.height);
        }
        if (!region[2])
            throw std::runtime_error("GPU TMEM tile exceeds atlas");
        const auto &tile = s.tiles[index & 7];
        TextureDraw draw;
        draw.shader = RasterShader::DecodeTMEM;
        draw.target = atlas_texture_;
        draw.inputs = {snapshot.texture, white_, white_};
        draw.x = region[0];
        draw.y = region[1];
        draw.width = region[2];
        draw.height = region[3];
        draw.constants[2] = {float(tile.format), float(tile.size), float(tile.line), float(tile.tmem)};
        draw.constants[3] = {float(tile.palette), float((s.other_modes >> 47) & 1),
                             float((s.other_modes >> 46) & 1), float(rdp_linear_copy_tile(s, index))};
        draw.constants[4] = {float(storage.width), float(storage.height), float(storage.texel_shift != 0), 0};
        draw.constants[5] = {float(region[0]), float(region[1]), 0, 0};
        device_.texture_pass(draw);
        snapshot.tiles.push_back({key.parameters, region});
        return region;
    }
    if (auto *region = decode_cache_.find(key, s.tmem))
        return *region;
    auto decoded = rdp_decode_tile(s, std::uint8_t(index), true);
    if (!decoded.width || !decoded.height)
        return {};
    const auto pixel_key = TextureAtlas::key(decoded.width, decoded.height, decoded.rgba);
    TextureAtlas::Region region;
    if (auto *entry = atlas_.find(pixel_key, decoded.width, decoded.height, decoded.rgba))
        region = entry->region;
    else {
        region = atlas_.allocate(decoded.width, decoded.height);
        if (!region[2]) {
            drain();
            atlas_.clear();
            decode_cache_.clear();
            ++atlas_epoch_;
            region = atlas_.allocate(decoded.width, decoded.height);
        }
        if (!region[2])
            throw std::runtime_error("Legacy GPU tile does not fit texture atlas");
        device_.upload_region(atlas_texture_, region[0], region[1], region[2], region[3],
                              decoded.rgba.data());
        atlas_.insert(pixel_key, region, std::move(decoded.rgba));
    }
    decode_cache_.insert(key, s.tmem, region);
    return region;
}
unsigned RasterRenderer::tile(const RDPState &s, unsigned index, Float4 &info, Float4 &transform,
                              Float4 &wrap) {
    auto &cached = tiles_[index & 7];
    auto key = TextureDecodeCache::key(s, index);
    const auto &t = s.tiles[index & 7];
    if (!cached.color || cached.key != key.hash) {
        auto decoded = rdp_decode_tile(s, std::uint8_t(index), true);
        if (!decoded.width || !decoded.height)
            return white_;
        if (cached.color && (cached.width != int(decoded.width) || cached.height != int(decoded.height))) {
            device_.release(cached.color, cached.color);
            cached.color = 0;
        }
        if (!cached.color) {
            unsigned f = 0;
            device_.allocate(cached.color, f, decoded.width, decoded.height);
        }
        cached.width = decoded.width;
        cached.height = decoded.height;
        cached.key = key.hash;
        auto bytes = flip(decoded);
        device_.upload(cached.color, cached.width, cached.height, bytes.data());
    }
    auto shift = [](unsigned n) { return n <= 10 ? 1.0f / float(1u << n) : float(1u << (16 - n)); };
    info = {float(cached.width), float(cached.height), float(cached.width), float(cached.height)};
    transform = {shift(t.shift_s), shift(t.shift_t), float(t.sl) / 4.0f, float(t.tl) / 4.0f};
    wrap = {float(t.mask_s), float(t.mask_t), float(unsigned(t.clamp_s) | (unsigned(t.mirror_s) << 1)),
            float(unsigned(t.clamp_t) | (unsigned(t.mirror_t) << 1))};
    return cached.color;
}
void RasterRenderer::state(RasterDraw &d, const RDPState &s, GLFramebuffer &f, bool textured, unsigned ti) {
    ensure_noise(f.native_width, f.native_height);
    d.auxiliary = {noise_, reciprocal_};
    auto &c = d.constants;
    const auto mode = s.other_modes;
    const unsigned cycle = (mode >> 52) & 3;
    d.color = f.color;
    d.scale = f.internal_scale;
    if (cycle < 2 && ((mode >> 4) & 3))
        d.depth = depth(s, f).color;
    else if (cycle >= 2)
        d.depth = white_;
    else {
        auto &z = coverage_;
        if (!z.color || z.width != f.width || z.height != f.height) {
            drain();
            if (z.color)
                device_.release(z.color, z.fbo);
            z = {};
            z.width = f.width;
            z.height = f.height;
            device_.allocate(z.color, z.fbo, z.width, z.height);
            std::vector<std::uint8_t> empty(std::size_t(z.width) * z.height * 4, 255);
            for (std::size_t at = 2; at < empty.size(); at += 4)
                empty[at] = 63;
            device_.upload(z.color, z.width, z.height, empty.data());
        }
        d.depth = z.color;
    }
    c[0] = {float(f.width), float(f.height), float(f.width), float(f.height)};
    c[38] = {float(f.native_width), float(f.native_height), float(f.internal_scale), float(f.format)};
    const bool clip = s.scissor_xl > s.scissor_xh && s.scissor_yl > s.scissor_yh;
    c[105] = {float(clip ? s.scissor_xh >> 2 : 0), float(clip ? s.scissor_yh >> 2 : 0),
              float(clip ? (s.scissor_xl + 3) >> 2 : f.native_width),
              float(clip ? (s.scissor_yl + 3) >> 2 : f.native_height)};
    c[1][0] = float(cycle);
    c[1][2] = float((mode >> 51) & 1);
    c[1][3] = float(textured);
    c[6][2] = float(s.primitive_z & 0x7fff) * 8.0f;
    c[6][3] = float(std::max(1u, unsigned(s.primitive_dz)));
    c[15] = color(s.primitive);
    c[16] = color(s.environment);
    c[17] = color(s.fog);
    c[18] = color(s.blend);
    int rgb0[4], a0[4], rgb1[4], a1[4];
    glrdp_decode_combine(s.combine, rgb0, a0, rgb1, a1);
    for (unsigned i = 0; i < 4; i++) {
        c[19][i] = float(rgb0[i]);
        c[20][i] = float(a0[i]);
        c[21][i] = float(rgb1[i]);
        c[22][i] = float(a1[i]);
    }
    c[23] = {float((mode >> 30) & 3), float((mode >> 26) & 3), float((mode >> 22) & 3),
             float((mode >> 18) & 3)};
    c[24] = cycle == 1 ? Float4{float((mode >> 28) & 3), float((mode >> 24) & 3), float((mode >> 20) & 3),
                                float((mode >> 16) & 3)}
                       : c[23];
    c[25] = {float(cycle < 2 ? ((mode >> 4) & 1) : 0), float(cycle < 2 ? ((mode >> 5) & 1) : 0),
             float((mode >> 10) & 3), float((mode >> 2) & 1)};
    c[26] = {float((mode >> 3) & 1), float((mode >> 14) & 1), float((mode >> 6) & 1), float((mode >> 7) & 1)};
    c[27] = {float((mode >> 8) & 3), float((mode >> 12) & 1), float((mode >> 13) & 1), float(mode & 3)};
    c[28] = {float((mode >> 38) & 3), float((mode >> 36) & 3), float((mode >> 44) & 3),
             float((mode >> 40) & 1)};
    c[41][2] = float(rdp_reads_color_memory(s));
    c[29] = {0, float(s.primitive_lod_fraction), float(f.internal_scale), float(s.color_image.size)};
    c[42] = {float(s.key.center_r), float(s.key.center_g), float(s.key.center_b), 0};
    c[43] = {float(s.key.scale_r), float(s.key.scale_g), float(s.key.scale_b), 0};
    c[44] = {float(s.convert.k[4]), float(s.convert.k[5]), 0, 0};
    d.tiles = {white_, white_};
    if (textured) {
        c[88][0] = 1;
        c[89] = {float((mode >> 48) & 1), float((mode >> 50) & 1), float((mode >> 49) & 1),
                 float(s.primitive_min_lod)};
        c[90][0] = float(rdp_uses_lod_fraction(s));
        c[93] = {float(s.convert.k[0]), float(s.convert.k[1]), float(s.convert.k[2]), float(s.convert.k[3])};
        c[94] = {float((mode >> 43) & 1), float((mode >> 42) & 1), float((mode >> 41) & 1), 0};
        d.tiles = {atlas_texture_, atlas_texture_};
        const unsigned levels = c[89][0] > .5f ? 8 : (rdp_uses_second_texel(s) ? 2 : 1);
        for (unsigned retry = 0; retry < 3; retry++) {
            const auto epoch = atlas_epoch_;
            c[90][2] = c[90][3] = 0;
            for (unsigned i = 0; i < levels; i++) {
                const auto index = (ti + i) & 7;
                if (s.tiles[index].format == RDPImageFormat::YUV) {
                    if ((mode >> 47) & 1)
                        c[90][3] += float(1u << i);
                    else
                        c[90][2] += float(1u << i);
                }
                const auto region = atlas_tile(s, index);
                const auto &t = s.tiles[index];
                const auto logical = rdp_tile_storage(s, index, false),
                           storage = rdp_tile_storage(s, index, true);
                c[48 + i] = {float(region[0]), float(region[1]), float(region[2]), float(region[3])};
                auto shift = [](unsigned n) {
                    return n <= 10 ? 1.0f / float(1u << n) : float(1u << (16 - n));
                };
                c[56 + i] = {shift(t.shift_s), shift(t.shift_t), float(t.sl) / 4, float(t.tl) / 4};
                c[64 + i] = {float(t.mask_s), float(t.mask_t),
                             float(unsigned(t.clamp_s) | (unsigned(t.mirror_s) << 1)),
                             float(unsigned(t.clamp_t) | (unsigned(t.mirror_t) << 1))};
                c[72 + i] = {float(logical.width), float(logical.height), float(storage.texel_shift),
                             rdp_linear_copy_tile(s, index) ? -float(t.line * 4 + 1)
                                                            : float(t.line * (1u << storage.texel_shift))};
            }
            if (epoch == atlas_epoch_)
                break;
        }
        if (levels == 1) {
            c[49] = c[48];
            c[57] = c[56];
            c[65] = c[64];
            c[73] = c[72];
        }
    }
}
void RasterRenderer::submit(RasterDraw &d, const RDPState &s, GLFramebuffer &f) {
    const float sx0 = float(s.scissor_xh >> 2), sy0 = float(s.scissor_yh >> 2);
    const float sx1 = s.scissor_xl ? float((s.scissor_xl + 3) >> 2) : float(f.native_width),
                sy1 = s.scissor_yl ? float((s.scissor_yl + 3) >> 2) : float(f.native_height);
    d.x0 = std::max({d.x0, sx0, 0.0f});
    d.y0 = std::max({d.y0, sy0, 0.0f});
    d.x1 = std::min({d.x1, sx1, float(f.native_width)});
    d.y1 = std::min({d.y1, sy1, float(f.native_height)});
    if (d.x1 <= d.x0 || d.y1 <= d.y0)
        return;
    const auto depth_pass = [](const RasterDraw &p) {
        return p.constants[1][0] < 2 || p.constants[45][0] > .5f;
    };
    if (!queued_.empty() &&
        (queued_.size() == PrimitiveScheduler::max_queued || d.color != queued_[0].color ||
         d.depth != queued_[0].depth || depth_pass(d) != depth_pass(queued_[0])))
        drain();
    PrimitiveFootprint footprint;
    footprint.bounds = {int(std::floor(d.x0)), int(std::floor(d.y0)), int(std::ceil(d.x1) - std::floor(d.x0)),
                        int(std::ceil(d.y1) - std::floor(d.y0))};
    // Full eight-sample coverage uses conservative bounds; GLRDP's fast
    // center-sample triangle footprint must not hide shared AA edge samples.
    if ((device_.capabilities().separate_shading && d.constants[1][3] > .5f) ||
        (d.constants[1][0] == 1 && (unsigned(d.constants[27][3]) & 1))) {
        --footprint.bounds.x;
        footprint.bounds.w += 2;
    }
    scheduler_.append(footprints_, footprint, PrimitiveScheduler::max_queued);
    footprints_.push_back(footprint);
    if (queued_.empty()) {
        queued_image_ = s.color_image;
        queued_height_ = f.native_height;
    }
    f.mark_gpu_written(int(std::floor(d.x0)), int(std::floor(d.y0)), int(std::ceil(d.x1) - std::floor(d.x0)),
                       int(std::ceil(d.y1) - std::floor(d.y0)));
    if (auto it = depth_.find(s.z_image_address); it != depth_.end() && d.depth == it->second.color)
        it->second.dirty |= d.constants[25][1] > 0.5f || d.constants[45][0] > 0.5f;
    queued_.push_back(std::move(d));
}
void RasterRenderer::drain() {
    GLRDP_PROFILE_SCOPE(flush);
    if (queued_.empty())
        return;
    static const bool serial = std::getenv("GLRDP_LEGACY_SERIAL") != nullptr;
    try {
        if (serial)
            for (auto &draw : queued_)
                device_.draw_raster(draw);
        else
            for (const auto &group : scheduler_.groups) {
                std::vector<const RasterDraw *> draws;
                draws.reserve(group.size());
                for (auto index : group)
                    draws.push_back(&queued_[index]);
                device_.draw_batch(draws);
            }
    } catch (...) {
        queued_.clear();
        footprints_.clear();
        scheduler_.clear();
        throw;
    }
    queued_.clear();
    footprints_.clear();
    scheduler_.clear();
}
void RasterRenderer::triangle(const RDPTriangle &t, const RDPState &s) {
    const auto geometry = glrdp_triangle_geometry(t);
    if (geometry.empty())
        return;
    RasterDraw d;
    d.vertices = glrdp_conservative_triangle(geometry);
    d.x0 = d.y0 = std::numeric_limits<float>::max();
    d.x1 = d.y1 = std::numeric_limits<float>::lowest();
    for (auto &v : geometry) {
        d.x0 = std::min(d.x0, v.x);
        d.x1 = std::max(d.x1, v.x);
        d.y0 = std::min(d.y0, v.y);
        d.y1 = std::max(d.y1, v.y);
    }
    d.x0 = std::floor(d.x0);
    d.y0 = std::floor(d.y0);
    d.x1 = std::ceil(d.x1);
    d.y1 = std::ceil(d.y1);
    auto *f = target(s, d.y1);
    if (!f)
        return;
    state(d, s, *f, t.texture_enabled, t.edge.tile);
    auto &c = d.constants;
    const auto &e = t.edge;
    c[88][1] = float(t.edge.level);
    c[1][1] = 1;
    c[3] = {float(e.yh), float(e.ym), float(e.yl), float(e.right_major)};
    c[4] = {float(double(e.xh & ~1) / 65536.0), float(double(e.xm & ~1) / 65536.0),
            float(double(e.xl & ~1) / 65536.0), 0};
    c[5] = {float(double((e.dxhdy >> 2) & ~1) / 65536.0), float(double((e.dxmdy >> 2) & ~1) / 65536.0),
            float(double((e.dxldy >> 2) & ~1) / 65536.0), 0};
    c[41][1] = float(t.z_enabled);
    auto word = [](std::int32_t x) {
        const auto v = std::uint32_t(x);
        return std::array<float, 2>{float(v & 65535), float(v >> 16)};
    };
    auto z0 = word(t.depth.z), zx = word(t.depth.dzdx), ze = word(t.depth.dzde), zy = word(t.depth.dzdy);
    c[46] = {z0[0], z0[1], zx[0], zx[1]};
    c[47] = {ze[0], ze[1], zy[0], zy[1]};
    c[41][3] = 1;
    auto coeff = [&](unsigned n, std::int32_t value, std::int32_t dx, std::int32_t de, std::int32_t dy) {
        auto a = word(value), b = word(dx), d = word(de), e = word(dy);
        c[n] = {a[0], a[1], b[0], b[1]};
        c[n + 1] = {d[0], d[1], e[0], e[1]};
    };
    for (unsigned i = 0; i < 4; i++)
        coeff(80 + i * 2, t.shade.value[i], t.shade.dx[i], t.shade.de[i], t.shade.dy[i]);
    for (unsigned i = 0; i < 3; i++)
        coeff(96 + i * 2, t.texture.value[i], t.texture.dx[i], t.texture.de[i], t.texture.dy[i]);
    const std::int32_t ex[] = {e.xh, e.xm, e.xl}, es[] = {e.dxhdy, e.dxmdy, e.dxldy};
    for (unsigned i = 0; i < 3; i++) {
        auto a = word(ex[i] & ~1), b = word((es[i] >> 2) & ~1);
        c[106 + i] = {a[0], a[1], b[0], b[1]};
    }

    c[6][0] = float(double(e.xh) / 65536);
    c[6][1] = float(e.yh & ~3) / 4;
    c[41][0] = float(t.shade_enabled);
    for (unsigned i = 0; i < 4; i++)
        c[7 + i] = {float(double(t.shade.value[i]) / 65536), float(double(t.shade.dx[i]) / 65536),
                    float(double(t.shade.de[i]) / 65536), float(double(t.shade.dy[i]) / 65536)};
    const bool perspective = ((s.other_modes >> 51) & 1) != 0;
    for (unsigned i = 0; i < 3; i++) {
        double div = perspective ? (i == 2 ? 65536.0 : 64.0) : 2097152.0;
        c[11 + i] = i == 2 && !perspective
                        ? Float4{1, 0, 0, 0}
                        : Float4{float(t.texture.value[i] / div), float(t.texture.dx[i] / div),
                                 float(t.texture.de[i] / div), float(t.texture.dy[i] / div)};
    }
    c[14] = {float(double(std::uint32_t(t.depth.z)) / 8192), float(double(t.depth.dzdx) / 8192),
             float(double(t.depth.dzde) / 8192), float(double(t.depth.dzdy) / 8192)};
    auto magnitude = [](int x) {
        unsigned v = (unsigned(x) >> 16) & 65535;
        return v & 32768 ? (~v) & 32767 : v;
    };
    unsigned sum = magnitude(t.depth.dzdx) + magnitude(t.depth.dzdy);
    if (!((s.other_modes >> 2) & 1)) {
        unsigned dz = 1;
        if (sum & 0xc000)
            dz = 32768;
        else if (sum == 1)
            dz = 3;
        else if (sum > 1) {
            for (unsigned b = 8192; b; b >>= 1)
                if (sum & b) {
                    dz = b << 1;
                    break;
                }
        }
        c[6][3] = float(dz);
    }
    submit(d, s, *f);
    ++triangles;
}
void RasterRenderer::rectangle(const RDPTextureRectangle &r, const RDPState &s) {
    RasterDraw d;
    d.x0 = float(r.xh) / 4;
    d.y0 = float(r.yh) / 4;
    d.x1 = float(r.xl) / 4;
    d.y1 = float(r.yl) / 4;
    unsigned cycle = (s.other_modes >> 52) & 3;
    if (cycle == 2) {
        d.x1 = std::floor(d.x1) + 1;
        d.y1 = std::floor(d.y1) + 1;
    }
    auto *f = target(s, d.y1);
    if (!f)
        return;
    state(d, s, *f, true, r.tile);
    auto &c = d.constants;
    c[2] = {d.x0, d.y0, d.x1, d.y1};
    c[6][0] = cycle == 2 ? std::floor(d.x0) : d.x0;
    c[6][1] = std::floor(d.y0);
    float ds = float(r.dsdx) / 1024 * (cycle == 2 ? .25f : 1.0f), dt = float(r.dtdy) / 1024;
    c[11] = {float(r.s) / 32, r.flipped ? 0 : ds, 0, r.flipped ? ds : 0};
    c[12] = {float(r.t) / 32, r.flipped ? dt : 0, 0, r.flipped ? 0 : dt};
    c[13] = {1, 0, 0, 0};
    c[1][2] = 0;
    submit(d, s, *f);
    ++rectangles;
}
void RasterRenderer::fill(const RDPFillRectangle &r, const RDPState &s) {
    RasterDraw d;
    d.x0 = float(r.xh) / 4;
    d.y0 = float(r.yh) / 4;
    d.x1 = float(r.xl) / 4;
    d.y1 = float(r.yl) / 4;
    unsigned cycle = (s.other_modes >> 52) & 3;
    if (cycle >= 2) {
        d.x1 = std::floor(d.x1) + 1;
        d.y1 = std::floor(d.y1) + 1;
    }
    auto *f = target(s, d.y1);
    if (!f)
        return;
    state(d, s, *f, false, 0);
    auto &c = d.constants;
    c[2] = {d.x0, d.y0, d.x1, d.y1};
    auto unpack = [](unsigned w) {
        return Float4{float(((w >> 11) & 31) << 3), float(((w >> 6) & 31) << 3), float(((w >> 1) & 31) << 3),
                      float((w & 1) ? 255 : 0)};
    };
    if (s.color_image.size == RDPImageSize::Bits16) {
        c[30] = unpack(s.fill_color >> 16);
        c[31] = unpack(s.fill_color);
    } else
        c[30] = c[31] = {float(s.fill_color >> 24), float((s.fill_color >> 16) & 255),
                         float((s.fill_color >> 8) & 255), float(s.fill_color & 255)};
    if (cycle == 3 && s.color_image.address == s.z_image_address) {
        auto word = std::uint16_t(s.fill_color);
        unsigned dz = ((word & 3) << 2) | (s.fill_color & 3);
        c[45] = {1, float(glrdp_z_decompress(word)), float(dz), 0};
        d.depth = depth(s, *f).color;
        f->raw_z = true;
    } else
        f->raw_z = false;
    submit(d, s, *f);
    fb_.record_fill32_hidden(r, s);
    ++fills;
}
bool RasterRenderer::load_texture(RDPState &s, RDPOpcode op, std::uint64_t command) {
    std::array<std::uint32_t, 4096> mapping;
    if (!rdp_texture_load_map(s, op, command, mapping))
        return false;
    GLFramebuffer *source = nullptr;
    for (auto &[_, constant] : fb_.buffers()) {
        auto &f = const_cast<GLFramebuffer &>(constant);
        if (!f.gpu_dirty || f.raw_z || f.size == RDPImageSize::Bits4)
            continue;
        bool used = false;
        for (auto address : mapping)
            if (address != UINT32_MAX && f.gpu_pending_range(address, 1)) {
                used = true;
                break;
            }
        if (used && (!source || f.write_serial > source->write_serial))
            source = &f;
    }
    if (!source && !s.gpu_tmem)
        return false;
    if (source)
        drain();
    if (source && source->cpu_dirty) {
        fb_.download_to_rdram(*source, memory_);
        fb_.upload_from_rdram(*source, memory_);
    }
    auto previous = s.gpu_tmem;
    bool all_source = source != nullptr;
    if (source) {
        const auto end =
            std::uint64_t(source->address) +
            (std::uint64_t(source->native_width) * source->native_height * (4u << unsigned(source->size)) +
             7) /
                8;
        for (auto address : mapping)
            if (address != UINT32_MAX && (address < source->address || address >= end)) {
                all_source = false;
                break;
            }
    }
    if (all_source && previous && previous->last_load_valid && previous->last_load_map == mapping &&
        previous->last_load_source == (source ? source->color : 0) &&
        previous->last_load_version == (source ? source->texture_version : 0))
        return true;
    // Materialize other independently owned sources in a mixed load. The
    // common framebuffer path remains a GPU -> GPU TMEM transfer.
    for (auto &[_, constant] : fb_.buffers())
        if (&constant != source && constant.gpu_dirty && !constant.raw_z) {
            bool used = false;
            for (auto address : mapping)
                if (address != UINT32_MAX && constant.gpu_pending_range(address, 1)) {
                    used = true;
                    break;
                }
            if (used)
                fb_.download_to_rdram(const_cast<GLFramebuffer &>(constant), memory_);
        }
    std::array<std::uint8_t, 4096> flags{};
    if (previous)
        flags = previous->gpu_bytes;
    std::vector<std::uint8_t> map(4096 * 4);
    bool dependent = false;
    const auto source_bytes =
        source
            ? (std::uint64_t(source->native_width) * source->native_height * (4u << unsigned(source->size)) +
               7) /
                  8
            : 0;
    for (unsigned i = 0; i < 4096; i++) {
        const auto address = mapping[i];
        auto *out = map.data() + i * 4;
        if (address == UINT32_MAX) {
            out[3] = 2;
        } else if (source && address >= source->address &&
                   address < std::uint64_t(source->address) + source_bytes) {
            const auto offset = address - source->address;
            out[0] = offset;
            out[1] = offset >> 8;
            out[2] = offset >> 16;
            out[3] = 1;
            flags[i] = 1;
        } else {
            const auto value = address < memory_.size() ? memory_.read8(address) : 0;
            out[0] = value;
            s.tmem[i] = value;
            flags[i] = 0;
        }
        dependent |= flags[i] != 0;
    }
    if (!dependent) {
        s.gpu_tmem.reset();
        ++s.tmem_generation;
        return true;
    }
    std::shared_ptr<RDPTMEMBacking> next;
    for (auto &entry : snapshots_)
        if (entry.use_count() == 1) {
            next = entry;
            break;
        }
    if (!next) {
        next = std::make_shared<RDPTMEMBacking>();
        device_.allocate(next->texture, next->fbo, 64, 16);
        snapshots_.push_back(next);
    }
    next->serial = ++tmem_serial_;
    next->gpu_bytes = flags;
    next->tiles.clear();
    next->atlas_epoch = atlas_epoch_;
    next->last_load_map = mapping;
    next->last_load_source = source ? source->color : 0;
    next->last_load_version = source ? source->texture_version : 0;
    next->last_load_valid = true;
    if (!previous)
        device_.upload_region(tmem_cpu_, 0, 0, 64, 16, s.tmem.data());
    device_.upload_region(tmem_map_, 0, 0, 64, 64, map.data());
    TextureDraw draw;
    draw.shader = RasterShader::LoadTMEM;
    draw.target = next->texture;
    draw.width = 64;
    draw.height = 16;
    draw.inputs = {previous ? previous->texture : tmem_cpu_, tmem_map_, source ? source->color : white_};
    if (source) {
        draw.constants[2] = {float(source->native_width), float(source->native_height), float(source->size),
                             float(source->internal_scale)};
        draw.constants[6][0] = float(source->address & 1);
    }
    device_.texture_pass(draw);
    s.gpu_tmem = next;
    ++s.tmem_generation;
    return true;
}
void RasterRenderer::materialize_tmem(RDPState &s) {
    drain();
    if (!s.gpu_tmem)
        return;
    std::array<std::uint8_t, 4096> data{};
    device_.read(s.gpu_tmem->fbo, 0, 0, 64, 16, data.data());
    for (unsigned y = 0; y < 16; y++)
        std::memcpy(s.tmem.data() + y * 256, data.data() + (15 - y) * 256, 256);
    s.gpu_tmem.reset();
}
void RasterRenderer::texture_read(const RDPState &s, RDPOpcode op, std::uint64_t command) {
    std::array<std::uint32_t, 4096> map;
    if (!rdp_texture_load_map(s, op, command, map))
        return;
    std::uint32_t first = UINT32_MAX, last = 0;
    for (auto a : map)
        if (a != UINT32_MAX && a < memory_.size()) {
            first = std::min(first, a);
            last = std::max(last, a);
        }
    if (first != UINT32_MAX) {
        bool pending = false;
        for (const auto &[_, f] : fb_.buffers())
            pending |= f.gpu_pending_range(first, last - first + 1);
        for (const auto &[_, z] : depth_)
            pending |= z.dirty && overlaps(first, last - first + 1, z.address, z.snapshot.size());
        if (pending)
            read(first, last - first + 1);
    }
}
void RasterRenderer::read(std::uint32_t address, std::uint32_t bytes) {
    drain();
    depth_range(address, bytes);
    fb_.commit_color_range(address, bytes, true);
}
void RasterRenderer::write(std::uint32_t address, std::uint32_t bytes) {
    fb_.invalidate_cpu_write(address, bytes);
    for (auto &[_, z] : depth_)
        if (overlaps(address, bytes, z.address, z.snapshot.size()))
            z.cpu_dirty = true;
}
void RasterRenderer::pages(std::uint8_t *pages, unsigned count) const {
    auto mark = [&](std::uint32_t a, std::uint64_t bytes) {
        unsigned first = a >> 12, last = unsigned(std::min<std::uint64_t>(std::uint64_t(a) + bytes + 4095,
                                                                          std::uint64_t(count) * 4096) >>
                                                  12);
        for (unsigned i = first; i < last; i++)
            pages[i] = 1;
    };
    for (auto &[_, f] : fb_.buffers())
        mark(f.address, (std::uint64_t(f.native_width) * f.native_height * (4u << unsigned(f.size)) + 7) / 8);
    for (auto &[_, z] : depth_)
        mark(z.address, z.snapshot.size());
}
void RasterRenderer::flush() {
    drain();
    for (auto &[_, z] : depth_)
        export_depth(z);
    fb_.commit_color_range(0, unsigned(memory_.size()));
}
RasterPresent RasterRenderer::scanout(std::uint32_t status, std::uint32_t origin, std::uint32_t width,
                                      std::uint32_t hstart, std::uint32_t vstart, std::uint32_t xs,
                                      std::uint32_t ys, std::uint32_t vsync, std::uint32_t current) {
    drain();
    RasterPresent out;
    out.color = black_;
    if (!(status & 2) || !width || origin >= memory_.size())
        return out;
    const unsigned bpp = (status & 3) == 3 ? 4 : 2;
    const auto span = vi_source_byte_count(status, width, vstart, ys);
    auto *f = fb_.find_containing(origin, span);
    if (!f) {
        RDPImage image{RDPImageFormat::RGBA, bpp == 4 ? RDPImageSize::Bits32 : RDPImageSize::Bits16,
                       std::uint16_t(width), origin};
        int height = int(std::min<std::uint64_t>((memory_.size() - origin) / (width * bpp),
                                                 std::max(1u, span / (width * bpp))));
        if (height < 1)
            return out;
        f = &fb_.bind_color_image(image, height, 0, scale_, false);
    }
    if (f->cpu_dirty) {
        if (f->gpu_dirty && !f->raw_z)
            fb_.download_to_rdram(*f, memory_);
        fb_.upload_from_rdram(*f, memory_);
    }
    const unsigned offset = (origin - f->address) / bpp;
    ensure_noise(f->native_width, f->native_height);
    TextureDraw filter;
    filter.auxiliary = {noise_, reciprocal_};
    filter.shader = RasterShader::VIReconstruct;
    filter.inputs = {f->color, white_, white_};
    filter.constants[29][2] = float(f->internal_scale);
    filter.constants[106] = {renderer_settings.vi_filter ? float((status >> 8) & 3) : 3.0f,
                             float(renderer_settings.vi_filter && renderer_settings.dedither && bool(status & 65536)),
                             float(renderer_settings.vi_filter && renderer_settings.blur && bool(status & 16)),
                             float(renderer_settings.vi_filter)};
    filter.constants[107] = {0, float(f->size), float(renderer_settings.vi_filter && bool(status & 8)),
                             float(renderer_settings.vi_filter && bool(status & 4))};
    filter.constants[109] = {float(width), float(offset), 0, 0};
    unsigned source = f->color;
    {
        if (!vi_filter_ || vi_width_ != f->width || vi_height_ != f->height) {
            if (vi_filter_)
                device_.release(vi_filter_, vi_filter_);
            unsigned id = 0;
            device_.allocate(vi_filter_, id, f->width, f->height);
            vi_width_ = f->width;
            vi_height_ = f->height;
        }
        filter.target = vi_filter_;
        filter.width = f->width;
        filter.height = f->height;
        device_.texture_pass(filter);
        source = vi_filter_;
    }
    for (auto &f : vi_history_)
        if (!f) {
            unsigned id = 0;
            device_.allocate(f, id, 640, 480);
        }
    const auto next = vi_index_ ^ 1u;
    TextureDraw present;
    present.shader = RasterShader::Present;
    present.target = vi_history_[next];
    present.inputs = {source, vi_history_[vi_index_], white_};
    present.width = 640;
    present.height = 480;
    present.constants = filter.constants;
    present.auxiliary = filter.auxiliary;
    present.constants[2] = {float((xs >> 16) & 4095) / 1024, float((ys >> 16) & 4095) / 1024,
                            float(xs & 4095) / 1024, float(ys & 4095) / 1024};
    const bool pal = (vsync & 1023) > 550;
    present.constants[108] = {640, pal ? 288.0f : 240.0f, pal ? 128.0f : 108.0f, pal ? 44.0f : 34.0f};
    present.constants[110] = {float((hstart >> 16) & 1023), float(hstart & 1023),
                              float((vstart >> 16) & 1023), float(vstart & 1023)};
    present.constants[107][0] = 1;
    const bool preserve =
        fields_.update(status, width, hstart, vstart, xs, ys, vsync, current, renderer_settings.bob);
    present.constants[42] = {float(bool(status & 64)), float(fields_.lower), float(preserve),
                             float(renderer_settings.bob)};
    device_.texture_pass(present);
    vi_index_ = next;
    out.color = vi_history_[next];
    return out;
}
} // namespace glrdp::legacy
