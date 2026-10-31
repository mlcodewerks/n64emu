#ifndef PARALLAX_USF_INTERNAL_H
#define PARALLAX_USF_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int parallax_usf_prepare(void);
void parallax_usf_step(void);
int parallax_usf_compare_enabled(void);
int parallax_usf_fifo_enabled(void);
unsigned parallax_usf_rdram_size(void);
int parallax_usf_yield(void);
int parallax_usf_load_pj64(const uint8_t *, size_t);
#ifdef __cplusplus
}
#endif
#endif
