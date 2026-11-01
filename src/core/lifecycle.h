#ifndef N64EMU_LIFECYCLE_H
#define N64EMU_LIFECYCLE_H
#include <stdbool.h>
bool core_open_content(const void *data, unsigned size);
void core_close_content(void);
#endif
