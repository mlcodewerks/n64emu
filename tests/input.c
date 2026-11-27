/* Libretro-to-N64 control-stick and button translation checks. */
#include "core/input.h"
#include <libretro.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

core_controller Controls[4];
int pad_pak_types[4];
int pad_present[4] = {1, 1, 1, 1};
struct retro_rumble_interface rumble;
static int16_t axes[4][2];
static uint16_t buttons[4];
static bool bitmasks;
static unsigned expected_port;
static uint16_t motor_strength[2];
static unsigned rumble_calls;

static bool set_rumble_state(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
   assert(port == expected_port);
   assert(effect == RETRO_RUMBLE_WEAK || effect == RETRO_RUMBLE_STRONG);
   motor_strength[effect] = strength;
   ++rumble_calls;
   return true;
}

static bool environment(unsigned command, void *data)
{
   (void)data;
   if (command == RETRO_ENVIRONMENT_GET_INPUT_BITMASKS)
      return bitmasks;
   return command == RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS;
}

static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
   assert(port == expected_port);
   if (device == RETRO_DEVICE_ANALOG)
   {
      assert(index == RETRO_DEVICE_INDEX_ANALOG_LEFT && id <= RETRO_DEVICE_ID_ANALOG_Y);
      return axes[port][id];
   }
   assert(device == RETRO_DEVICE_JOYPAD && index == 0);
   return id == RETRO_DEVICE_ID_JOYPAD_MASK ? (int16_t)buttons[port] : !!(buttons[port] & (1u << id));
}

retro_environment_t environ_cb = environment;
retro_input_state_t input_cb = input;

static core_buttons read_axes(int x, int y)
{
   core_buttons result;
   axes[expected_port][0] = (int16_t)x;
   axes[expected_port][1] = (int16_t)y;
   result.Value = UINT32_MAX; /* Every poll must replace stale input. */
   core_input_keys((int)expected_port, &result);
   return result;
}

int main(void)
{
   /* Golden outputs from ares' ray/octagon implementation, including circular
    * full diagonals, square corners, axial drift and gradual movement. */
   static const int vectors[][4] = {
      {0, 0, 0, 0}, {2000, 2000, 0, 0}, {2300, 0, 0, 0},
      {3000, 0, 2, 0}, {4096, 0, 6, 0}, {8192, 0, 19, 0},
      {16384, 0, 46, 0}, {16384, 16384, 46, -46},
      {23170, 23170, 69, -69}, {32767, 32767, 69, -69},
      {-32768, -32768, -69, 69}, {32767, 0, 85, 0},
      {0, -32768, 0, 85}, {30000, 10000, 79, -22},
      {10000, 30000, 22, -79}, {32767, 1000, 85, 0}
   };
   for (unsigned mode = 0; mode < 2; ++mode)
   {
      bitmasks = mode != 0;
      core_input_init();
      for (expected_port = 0; expected_port < 4; ++expected_port)
      {
         for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i)
         {
            core_buttons result = read_axes(vectors[i][0], vectors[i][1]);
            assert(result.X_AXIS == vectors[i][2] && result.Y_AXIS == vectors[i][3]);
            assert((result.Value & 0xffff) == 0);
         }
         buttons[expected_port] = UINT16_MAX;
         core_buttons result = read_axes(32767, -32767);
         assert((result.Value & 0xffff) == 0x3fff);
         assert(result.X_AXIS == 69 && result.Y_AXIS == 69);
         buttons[expected_port] = 1u << RETRO_DEVICE_ID_JOYPAD_B;
         result = read_axes(0, 0);
         assert(result.Value == 0x80); /* RetroPad B is N64 A. */
         buttons[expected_port] = 0;
      }
   }
   expected_port = 0;
   /* An exhaustive single-axis sweep detects wraparound, sign asymmetry,
    * discontinuities and accidental integer overflow at int16 extremes. */
   int previous = -85;
   for (int raw = INT16_MIN; raw <= INT16_MAX; ++raw)
   {
      core_buttons x = read_axes(raw, 0);
      core_buttons y = read_axes(0, raw);
      assert(x.X_AXIS >= previous && x.X_AXIS >= -85 && x.X_AXIS <= 85);
      assert(x.Y_AXIS == 0 && y.X_AXIS == 0 && y.Y_AXIS == -x.X_AXIS);
      previous = x.X_AXIS;
      if (raw != INT16_MIN)
      {
         core_buttons mirror = read_axes(-raw, 0);
         assert(mirror.X_AXIS == -x.X_AXIS);
      }
   }
   /* The hardware gate is an octagon, rather than independent axis clamps. */
   for (int x = INT16_MIN; x <= INT16_MAX; x += 511)
      for (int y = INT16_MIN; y <= INT16_MAX; y += 511)
      {
         core_buttons result = read_axes(x, y);
         int ax = abs(result.X_AXIS), ay = abs(result.Y_AXIS);
         assert(69 * ax + 16 * ay <= 85 * 69);
         assert(16 * ax + 69 * ay <= 85 * 69);
      }
   expected_port = 2;
   core_input_rumble(expected_port, UINT16_MAX);
   rumble.set_rumble_state = set_rumble_state;
   core_input_rumble(expected_port, UINT16_MAX);
   assert(rumble_calls == 2 && motor_strength[0] == UINT16_MAX && motor_strength[1] == UINT16_MAX);
   Controls[expected_port].Present = 0;
   core_input_rumble(expected_port, 0);
   core_input_rumble(expected_port, UINT16_MAX);
   assert(rumble_calls == 4 && motor_strength[0] == 0 && motor_strength[1] == 0);
   puts("Input: ares vectors, four ports, buttons, int16 sweep and octagonal gate passed.");
   return 0;
}
