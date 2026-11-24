#pragma once
#include "common.h"
struct vp { unsigned x=0, y=0, width=0, height=0; };
#include <memory>
class Video {
  public:
    Video();
    ~Video();
    void init(HWND window);
    void shutdown();
    void start_core(const retro_game_geometry &geometry);
    void stop_core();
    void geometry(const retro_game_geometry &geometry, bool allocate = false);
    void refresh(const void *data, unsigned width, unsigned height, size_t pitch);
    void new_frame();
    void draw(unsigned width, unsigned height, unsigned top = 0);
    void present();
    void capture(const fs::path &path);
    void verify_software(const void *data, unsigned width, unsigned height, size_t pitch);
    bool set_format(retro_pixel_format value);
    unsigned width() const;
    unsigned height() const;
    float aspect() const;
    vp viewport(unsigned width, unsigned height, unsigned top = 0) const;
    bool integer_scale = false;
    bool square_pixels = false;
    bool verify_pixels = false;
    uint64_t verified_frames = 0;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
