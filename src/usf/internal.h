#ifndef N64EMU_USF_INTERNAL_H
#define N64EMU_USF_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int n64emu_usf_prepare(void);
void n64emu_usf_step(void);
int n64emu_usf_compare_enabled(void);
int n64emu_usf_fifo_enabled(void);
unsigned n64emu_usf_rdram_size(void);
int n64emu_usf_yield(void);
int n64emu_usf_load_pj64(const uint8_t *, size_t);
#ifdef __cplusplus
}
#endif
#endif
