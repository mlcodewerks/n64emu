#include "core/input.h"
#include "core/backend.h"
#include "device/controllers.h"
#include <libretro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* snprintf not available in MSVC 2010 and earlier */
#include "core/msvc_compat.h"

extern retro_environment_t environ_cb;
extern retro_input_state_t input_cb;
extern struct retro_rumble_interface rumble;
extern int pad_pak_types[4];
extern int pad_present[4];
static bool libretro_supports_bitmasks = false;

extern core_rom_header ROM_HEADER;

// Some stuff from n-rage plugin
#define RD_GETSTATUS        0x00        // get status
#define RD_READKEYS         0x01        // read button values
#define RD_READPAK          0x02        // read from controllerpack
#define RD_WRITEPAK         0x03        // write to controllerpack
#define RD_RESETCONTROLLER  0xff        // reset controller
#define RD_READEEPROM       0x04        // read eeprom
#define RD_WRITEEPROM       0x05        // write eeprom

#define PAK_IO_RUMBLE       0xC000      // the address where rumble-commands are sent to

/* global data definitions */
struct
{
    core_controller *control;               // pointer to core_controller struct in Core library
    core_buttons buttons;
} controller[4];

void core_input_keys( int Control, core_buttons *Keys );

static void set_input_descriptors(void)
{

      #define independent_cbuttons_map(PAD) \
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "A Button" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "B Button" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "C-Right" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "C-Left" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "C-Down" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "C-Up" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Z Trigger" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "R Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "L Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Control Stick X" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Control Stick Y" },\

      static struct retro_input_descriptor desc[] = {
         independent_cbuttons_map(0)
         independent_cbuttons_map(1)
         independent_cbuttons_map(2)
         independent_cbuttons_map(3)
         0
      };
      environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (struct retro_input_descriptor*)desc);
}

/* Controller protocol and libretro input translation. */

static unsigned char DataCRC( unsigned char *Data, int iLenght )
{
    unsigned char Remainder = Data[0];

    int iByte = 1;
    unsigned char bBit = 0;

    while( iByte <= iLenght )
    {
        int HighBit = ((Remainder & 0x80) != 0);
        Remainder = Remainder << 1;

        Remainder += ( iByte < iLenght && Data[iByte] & (0x80 >> bBit )) ? 1 : 0;

        Remainder ^= (HighBit) ? 0x85 : 0;

        bBit++;
        iByte += bBit/8;
        bBit %= 8;
    }

    return Remainder;
}

void core_input_command(int Control, unsigned char *Command)
{
    unsigned char *Data = &Command[5];

    if (Control == -1)
        return;

    switch (Command[2])
    {
        case RD_GETSTATUS:
            break;
        case RD_READKEYS:
            break;
        case RD_READPAK:
            if (controller[Control].control->accessory == ACCESSORY_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);

                if(( dwAddress >= 0x8000 ) && ( dwAddress < 0x9000 ) )
                    memset( Data, 0x80, 32 );
                else
                    memset( Data, 0x00, 32 );

                Data[32] = DataCRC( Data, 32 );
            }
            break;
        case RD_WRITEPAK:
            if (controller[Control].control->accessory == ACCESSORY_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);
                Data[32] = DataCRC( Data, 32 );

                if ((dwAddress == PAK_IO_RUMBLE) && (rumble.set_rumble_state))
                {
                    if (*Data)
                    {
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_WEAK, 0xFFFF);
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_STRONG, 0xFFFF);
                    }
                    else
                    {
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_WEAK, 0);
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_STRONG, 0);
                    }
                }
            }

            break;
        case RD_RESETCONTROLLER:
            break;
        case RD_READEEPROM:
            break;
        case RD_WRITEEPROM:
            break;
        }
}

/* Control-stick mapping adapted from kev4cards' ares algorithm.
 * https://github.com/ares-emulator/ares/blob/master/ares/n64/controller/gamepad/gamepad.cpp
 *
 * Copyright (c) 2004-2025 ares team, Near et al
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
static double stick_axis_response(int16_t input, double saturation, double deadzone)
{
   double position = (double)input * saturation / 32767.0;
   double magnitude = fabs(position);
   if (magnitude <= deadzone)
      return 0.0;
   return copysign((magnitude - deadzone) * saturation / (saturation - deadzone), position);
}

static void translate_analog_axes(int Control, core_buttons* Keys)
{
   const double cardinal = 85.0;
   const double diagonal = 69.0;
   const double deadzone = 7.0;
   const double sum = deadzone + diagonal;
   const double saturation = (sum + sqrt(sum * sum - 2.0 * sqrt(2.0) * diagonal * deadzone)) / sqrt(2.0);
   int16_t raw_x = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   int16_t raw_y = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
   double x = stick_axis_response(raw_x, saturation, deadzone);
   double y = stick_axis_response(raw_y, saturation, deadzone);
   double length = hypot(x, y);

   /* Restrict square stick reports to the same radius as circular sticks. */
   if (length > saturation)
   {
      x *= saturation / length;
      y *= saturation / length;
   }

   /* The two sloping edges in each quadrant join (85,0), (69,69), (0,85).
    * Scaling by their half-plane limits is equivalent to ares' ray/gate
    * intersection, without division by an axis close to zero. */
   {
      double gate_slope = (cardinal - diagonal) / diagonal;
      double edge = fmax(fabs(x) + gate_slope * fabs(y), fabs(y) + gate_slope * fabs(x));
      if (edge > cardinal)
      {
         x *= cardinal / edge;
         y *= cardinal / edge;
      }
   }

   /* Match ares' truncation, correcting floating-point boundary noise. */
   Keys->X_AXIS = (int)copysign(fabs(x) + 1e-9, x);
   Keys->Y_AXIS = (int)copysign(fabs(y) + 1e-9, -y);
}

void core_input_keys( int Control, core_buttons *Keys )
{
   unsigned i;
   int16_t ret        = 0;
   Keys->Value        = 0;

   if (libretro_supports_bitmasks)
      ret = input_cb(Control, RETRO_DEVICE_JOYPAD,
            0, RETRO_DEVICE_ID_JOYPAD_MASK);
   else
   {
      for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
      {
         if (input_cb(Control, RETRO_DEVICE_JOYPAD, 0, i))
            ret |= (1 << i);
      }
   }

   Keys->R_DPAD       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)));
   Keys->L_DPAD       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)));
   Keys->D_DPAD       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)));
   Keys->U_DPAD       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP)));
   Keys->START_BUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_START)));
   Keys->Z_TRIG       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)));

      Keys->A_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)));
      Keys->B_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)));
      Keys->R_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)));
      Keys->L_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)));
      Keys->D_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)));
      Keys->U_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)));
      Keys->R_TRIG    = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)));
   Keys->L_TRIG    = !!((ret & (1 <<RETRO_DEVICE_ID_JOYPAD_SELECT)));

   translate_analog_axes(Control, Keys);
}

void core_input_init(void)
{
    int i;
    libretro_supports_bitmasks = environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
    for( i = 0; i < 4; i++ )
    {
       controller[i].control = Controls + i;
       controller[i].control->Present = pad_present[i];
       controller[i].control->RawData = 0;

       if (pad_pak_types[i] == ACCESSORY_MEMPAK)
          controller[i].control->accessory = ACCESSORY_MEMPAK;
       else if (pad_pak_types[i] == ACCESSORY_RAW)
          controller[i].control->accessory = ACCESSORY_RAW;
       else if (pad_pak_types[i] == ACCESSORY_TRANSFER_PAK)
          controller[i].control->accessory = ACCESSORY_TRANSFER_PAK;
       else
          controller[i].control->accessory = ACCESSORY_NONE;
    }
   set_input_descriptors();
}


void core_input_read(int Control, unsigned char *Command)
{
   core_input_command(Control, Command);
}

