/* Private shared runtime interface; legacy types stay inside the core. */
#ifndef N64EMU_RUNTIME_H
#define N64EMU_RUNTIME_H
#include <libretro.h>
#ifdef __cplusplus
extern "C" {
#endif
void  core_runtime_init(void);
void  core_runtime_deinit(void);
unsigned core_runtime_api_version(void);
void  core_runtime_set_environment(retro_environment_t cb);
void  core_runtime_set_video_refresh(retro_video_refresh_t cb);
void  core_runtime_set_audio_sample(retro_audio_sample_t cb);
void  core_runtime_set_audio_sample_batch(retro_audio_sample_batch_t cb);
void  core_runtime_set_input_poll(retro_input_poll_t cb);
void  core_runtime_set_input_state(retro_input_state_t cb);
void  core_runtime_get_system_info(struct retro_system_info *info);
void  core_runtime_get_system_av_info(struct retro_system_av_info *info);
unsigned core_runtime_get_region(void);
bool core_runtime_load_game(const struct retro_game_info *game);
bool core_runtime_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
void  core_runtime_unload_game(void);
void  core_runtime_run(void);
void  core_runtime_reset(void);
void * core_runtime_get_memory_data(unsigned type);
size_t core_runtime_get_memory_size(unsigned type);
size_t core_runtime_serialize_size(void);
bool core_runtime_serialize(void *data, size_t size);
bool core_runtime_unserialize(const void *data, size_t size);
void  core_runtime_set_controller_port_device(unsigned in_port, unsigned device);
void  core_runtime_cheat_reset(void);
void  core_runtime_cheat_set(unsigned index, bool enabled, const char *codeLine);
#ifdef __cplusplus
}
#endif
#endif
