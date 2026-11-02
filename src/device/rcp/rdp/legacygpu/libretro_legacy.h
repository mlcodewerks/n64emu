#pragma once
#include <libretro.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum legacygpu_backend { LEGACYGPU_GL2, LEGACYGPU_D3D9, LEGACYGPU_GLES2 };
bool legacygpu_request_context(retro_environment_t environment, enum legacygpu_backend backend);
bool legacygpu_context_ready(void);
bool legacygpu_open(uint8_t *rdram, unsigned bytes);
void legacygpu_close(void);
void legacygpu_reset(void);
bool legacygpu_materialize(void);
void legacygpu_process_commands(void);
void legacygpu_scanout(void);
void legacygpu_begin_frame(void);
void legacygpu_end_frame(void);
void legacygpu_framebuffer_read(uint32_t address, uint32_t bytes);
void legacygpu_framebuffer_write(uint32_t address, uint32_t bytes);
bool legacygpu_framebuffer_pages(uint8_t *pages, unsigned count);
void legacygpu_framebuffer_info(void *info);
unsigned legacygpu_state_size(void);
void legacygpu_save_state(void *data);
void legacygpu_load_state(const void *data);
uint8_t *legacygpu_hidden_store(size_t *bytes);
#ifdef __cplusplus
}
#endif
