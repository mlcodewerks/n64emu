#pragma once
#include "shader_library.h"
#include "shader_chain.h"
#include <memory>
#include "native.h"

// All methods require the owning D3D9 device.
class shader_runtime {
public:
    struct chain {
        size_t index = 0;
        bool normalizes_input = false;
        libra_d3d9_filter_chain_t d3d9 = nullptr;
        std::map<std::string, float> parameters;
        ~chain();
    };
    std::vector<std::unique_ptr<chain>> chains;
    uint64_t attempted = 0, applied = 0;
    size_t frame = 0;
    uint64_t timestamp = 0;
    template<class Options> Options frame_options() {
        Options options{};
        // New filter chains initialize their own history.
        options.version = LIBRASHADER_CURRENT_VERSION;
        options.frame_direction = 1; options.total_subframes = options.current_subframe = 1;
        options.aspect_ratio = video_shaders().source_aspect; options.frames_per_second = video_shaders().source_fps;
        const auto now = native_nanoseconds();
        options.frametime_delta = timestamp ? static_cast<uint32_t>((now - timestamp) / 1000000) : 0;
        timestamp = now; return options;
    }
    bool prepare(IDirect3DDevice9 *device);
    void update_parameters();
    void clear();
};
