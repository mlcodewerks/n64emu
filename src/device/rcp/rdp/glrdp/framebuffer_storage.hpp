#pragma once
#include <cstdint>

// API-independent storage for GLRDP's image layout, aliases and CPU coherence.
// Reads/uploads use OpenGL row order: row zero is the bottom of the image.
// Implementations rasterize on the GPU; this interface only transfers storage.
class FramebufferStorage {
  public:
    virtual ~FramebufferStorage() = default;
    virtual int maximum_texture_size() const = 0;
    virtual void allocate(unsigned &color, unsigned &framebuffer, int width, int height) = 0;
    virtual void release(unsigned color, unsigned framebuffer) = 0;
    virtual void upload(unsigned color, int width, int height, const std::uint8_t *rgba) = 0;
    virtual void read(unsigned framebuffer, int x, int y, int width, int height, std::uint8_t *rgba) = 0;
};
