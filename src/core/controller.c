
#include "core/controller.h"

#include "core/input.h"

#include "main/main.h"

#include <stdint.h>
#include <core/frontend.h>

/* XXX: this is an abuse of the Zilmar Spec normally this value is reserved */
enum {
    PAK_SWITCH_BUTTON = 0x4000,
    GB_CART_SWITCH_BUTTON = 0x8000
};

/* Pak switching delay
 * If you put too low value,
 * some games (for instance Perfect Dark) won't be able to detect the pak change
 * causing incorrect pak accesses */
enum { PAK_SWITCH_DELAY = 20 };
enum { GB_CART_SWITCH_DELAY = 20 };

static int is_button_released(uint32_t input, uint32_t last_input, uint32_t mask)
{
    return ((input & mask) == 0)
        && ((last_input & mask) != 0);
}

void controller_request_pak_switch(struct frontend_controller* controller,
    void (*switch_pak)(int control_id))
{
    /* Leave the slot empty for subsequent Joybus polls so games see removal. */
    if (controller->cont->ipak == &g_itransferpak) {
        core_input_rumble(controller->control_id, 0);
    }
    change_pak(controller->cont, NULL, NULL);
    controller->main_switch_pak = switch_pak;
    controller->pak_switch_delay = PAK_SWITCH_DELAY;
    controller->gb_cart_switch_enabled = 0;
    controller->gb_switch_delay = 0;
}

static core_result controller_get_input(void* opaque, uint32_t* input_)
{
    struct frontend_controller* cin_compat = (struct frontend_controller*)opaque;
    core_buttons keys = { 0 };

    /* first poll controller */
        if (core_input_keys)
            core_input_keys(cin_compat->control_id, &keys);


    /* return an error if controller is not plugged */
    if (!Controls[cin_compat->control_id].Present) {
        return CORE_SYSTEM_ERROR;
    }

    /* If the PAK_SWITCH_BUTTON was released, cycle to the next accessory. */
    if (is_button_released(keys.Value, cin_compat->last_input, PAK_SWITCH_BUTTON)) {
        controller_request_pak_switch(cin_compat, main_switch_next_pak);
    }

    /* switch to next/selected pak after switch delay has expired */
    if (cin_compat->pak_switch_delay > 0 && --cin_compat->pak_switch_delay == 0) {
        cin_compat->main_switch_pak(cin_compat->control_id);
        cin_compat->main_switch_pak = NULL;
        // If switching to Transfer Pak and if a rom path is set, Switch it
        if (cin_compat->cont->ipak == &g_itransferpak && retro_transferpak_rom_path)
        {
            cin_compat->gb_cart_switch_enabled = 1;
        }
    }

    if (cin_compat->gb_cart_switch_enabled) {
        // disconnect current GB cart (if any) immediately after "GB cart switch" button is released
        if (!cin_compat->gb_switch_delay) {
            change_gb_cart(cin_compat->tpk, NULL);
            cin_compat->gb_switch_delay = GB_CART_SWITCH_DELAY;
        }

        // switch to new GB cart after switch delay has expired
        if (cin_compat->gb_switch_delay > 0 && --cin_compat->gb_switch_delay == 0) {
            main_change_gb_cart(cin_compat->control_id);
            cin_compat->gb_cart_switch_enabled = 0;
            cin_compat->gb_switch_delay = 0;
        }
    }

    cin_compat->last_input = keys.Value;

    *input_ = keys.Value;
    return CORE_OK;
}

const struct controller_input_backend_interface
    g_libretro_controller_backend =
{
    controller_get_input
};


static void controller_rumble(void* opaque, enum rumble_action action)
{
    core_input_rumble(*(const int*)opaque, action == RUMBLE_START ? UINT16_MAX : 0);
}

const struct rumble_backend_interface
    g_libretro_rumble_backend =
{
    controller_rumble
};


static void controller_read(void* opaque,
    const uint8_t* tx, const uint8_t* tx_buf,
    uint8_t* rx, uint8_t* rx_buf)
{
    int control_id = *(int*)opaque;

    if (core_input_read == NULL) {
        return;
    }

    //This is for netplay, -1 means there is no local controller controlling this player
    if (control_id == -1) {
        return;
    }

    /* UGLY: use negative offsets to get access to non-const tx pointer */
    core_input_read(control_id, rx - 1);
}

void controller_command(void* opaque,
    uint8_t* tx, const uint8_t* tx_buf,
    const uint8_t* rx, const uint8_t* rx_buf)
{
    int control_id = *(int*)opaque;

    if (core_input_command == NULL) {
        return;
    }

    //This is for netplay, -1 means there is no local controller controlling this player
    if (control_id == -1) {
        return;
    }

    core_input_command(control_id, tx);
}

const struct joybus_device_interface
    g_libretro_joybus_backend =
{
    NULL,
    controller_read,
    controller_command,
};
