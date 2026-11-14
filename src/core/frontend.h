/*
 *   Mupen64plus-Next - core/frontend.h
 *   Copyright (C) 2020 M4xw <m4x@m4xw.net>
 *   Copyright (C) 2020 Daniel De Matteis
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../LICENSE for the license terms.
 */

#ifndef N64EMU_FRONTEND_H
#define N64EMU_FRONTEND_H
#include <stdbool.h>
#include <libretro.h>
#include "core/backend.h"
#ifdef __cplusplus
extern "C" {
#endif
extern retro_environment_t environ_cb;
extern bool libretro_swap_buffer;
extern bool retro_savestate_complete;
extern int retro_savestate_result;
extern char *retro_dd_path_img, *retro_dd_path_rom;
extern char *retro_transferpak_rom_path, *retro_transferpak_ram_path;
extern uint32_t EnableFrameDuping, EnableFullspeed, EnableOverscan, AspectRatio;
extern uint32_t IgnoreTLBExceptions;
uint32_t get_retro_screen_width(void);
uint32_t get_retro_screen_height(void);
#define RETRO_MEMORY_DD (0x100 + 1)
#define RETRO_GAME_TYPE_DD 1
#define RETRO_MEMORY_TRANSFERPAK (0x100 + 2)
#define RETRO_GAME_TYPE_TRANSFERPAK 2
#define CORE_NAME "n64emu"
#ifdef __cplusplus
}
#endif
#endif
