
#ifndef PARALLAX_CONTROLLER_H
#define PARALLAX_CONTROLLER_H

#include "core/controller_input.h"
#include "core/rumble.h"
#include "core/joybus.h"

#include <stdint.h>

struct frontend_controller
{
    int control_id;

    struct game_controller* cont;
    struct transferpak* tpk;

    uint32_t last_input;
    int last_pak_type;
    void (*main_switch_pak)(int control_id);
    unsigned int pak_switch_delay;
    unsigned int gb_switch_delay;

    unsigned int gb_cart_switch_enabled;

};

extern const struct controller_input_backend_interface
    g_libretro_controller_backend;

/* Rumble backend interface */

extern const struct rumble_backend_interface
    g_libretro_rumble_backend;

/* PIF data processing functions */

extern const struct joybus_device_interface
    g_libretro_joybus_backend;

#endif
