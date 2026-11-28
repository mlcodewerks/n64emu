#ifndef N64EMU_CHEATS_H
#define N64EMU_CHEATS_H

#include <stdbool.h>

struct device;
bool cheats_validate(const char *codes);
bool cheats_set(unsigned index, bool enabled, const char *codes);
void cheats_clear(void);
bool cheats_use_button(void);
void cheats_press_button(void);
void cheats_apply(struct device *dev, bool boot);

#endif
