/* Optional libretro ABI adapter. The standalone executable does not link this. */
#include "core/runtime.h"
#include "core/log.h"
void  retro_init(void) { core_runtime_init(); }
void  retro_deinit(void) { core_runtime_deinit(); }
unsigned retro_api_version(void) { return core_runtime_api_version(); }
void  retro_set_environment(retro_environment_t cb) { core_runtime_set_environment(cb); }
void  retro_set_video_refresh(retro_video_refresh_t cb) { core_runtime_set_video_refresh(cb); }
void  retro_set_audio_sample(retro_audio_sample_t cb) { core_runtime_set_audio_sample(cb); }
void  retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { core_runtime_set_audio_sample_batch(cb); }
void  retro_set_input_poll(retro_input_poll_t cb) { core_runtime_set_input_poll(cb); }
void  retro_set_input_state(retro_input_state_t cb) { core_runtime_set_input_state(cb); }
void  retro_get_system_info(struct retro_system_info *info) { core_runtime_get_system_info(info); }
void  retro_get_system_av_info(struct retro_system_av_info *info) { core_runtime_get_system_av_info(info); }
unsigned retro_get_region(void) { return core_runtime_get_region(); }
bool retro_load_game(const struct retro_game_info *game) { return core_runtime_load_game(game); }
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return core_runtime_load_game_special(game_type, info, num_info); }
void  retro_unload_game(void) { core_runtime_unload_game(); }
void  retro_run(void) { core_runtime_run(); }
void  retro_reset(void) { core_runtime_reset(); }
void * retro_get_memory_data(unsigned type) { return core_runtime_get_memory_data(type); }
size_t retro_get_memory_size(unsigned type) { return core_runtime_get_memory_size(type); }
size_t retro_serialize_size(void) { return core_runtime_serialize_size(); }
bool retro_serialize(void *data, size_t size) { return core_runtime_serialize(data, size); }
bool retro_unserialize(const void *data, size_t size) { return core_runtime_unserialize(data, size); }
void  retro_set_controller_port_device(unsigned in_port, unsigned device) { core_runtime_set_controller_port_device(in_port, device); }
void  retro_cheat_reset(void) { core_runtime_cheat_reset(); }
void retro_cheat_set(unsigned index, bool enabled, const char *codes)
{
    if (!core_runtime_cheat_set(index, enabled, codes)) {
        if (enabled) core_log(CORE_LOG_WARNING, "Unsupported N64 cheat at index %u", index);
        else core_runtime_cheat_set(index, false, NULL);
    }
}
