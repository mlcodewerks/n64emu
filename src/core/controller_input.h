
#ifndef PARALLAX_CONTROLLER_INPUT_H
#define PARALLAX_CONTROLLER_INPUT_H

#include "core/types.h"

#include <stdint.h>

enum standard_controller_input {
    CI_STD_R_DPAD = 0x0001,
    CI_STD_L_DPAD = 0x0002,
    CI_STD_D_DPAD = 0x0004,
    CI_STD_U_DPAD = 0x0008,
    CI_STD_START  = 0x0010,
    CI_STD_Z      = 0x0020,
    CI_STD_B      = 0x0040,
    CI_STD_A      = 0x0080,
    CI_STD_R_CBTN = 0x0100,
    CI_STD_L_CBTN = 0x0200,
    CI_STD_D_CBTN = 0x0400,
    CI_STD_U_CBTN = 0x0800,
    CI_STD_R      = 0x1000,
    CI_STD_L      = 0x2000,
    /* bits 14 and 15 are reserved */
    /* bits 23-16 are for X-axis */
    /* bits 31-24 are for Y-axis */
};

enum mouse_controller_input {
    CI_MOUSE_RIGHT = 0x0040,
    CI_MOUSE_LEFT  = 0x0080,
    /* bits 23-16 are for X-axis */
    /* bits 31-24 are for Y-axis */
};


struct controller_input_backend_interface
{
    /* Get emulated controller input status (32-bit)
     * Encoding of the input status depends on the emulated controller flavor.
     * Returns CORE_OK on success
     */
    core_result (*get_input)(void* cin, uint32_t* input);
};

#endif
