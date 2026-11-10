/* ZSortp 0.33 walker and read-only preflight for the Angrylion frontend. */
#ifndef RDP_EMIT_ZSORT_H
#define RDP_EMIT_ZSORT_H

#include "rdp_emit_frontend.h"
#include "rdp_emit_f3dex2.h"

int zsort_ucode_match(const unsigned char *rdram, unsigned rdram_size,
                     unsigned ucode_data, unsigned ucode_data_size);
int zsort_validate(unsigned char *rdram, unsigned rdram_size,
                   const unsigned char *dmem, unsigned dl);
int zsort_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned char *rdram,
                 unsigned rdram_size, unsigned char *dmem, unsigned dl);
int zsort_run_stream(GSPState *gsp, RdpFifo *fifo, unsigned char *rdram,
                     unsigned rdram_size, unsigned char *dmem, unsigned dl,
                     int resume, unsigned *status, unsigned *interrupt);
int zsort_stream_active(void);
unsigned zsort_result_vco(void);
unsigned zsort_state_size(void);
void zsort_state_save(void *out);
void zsort_state_load(const void *in);
int zsort_yield_supported(const unsigned char *ram, unsigned size,
                          const unsigned char *dmem);

#endif
