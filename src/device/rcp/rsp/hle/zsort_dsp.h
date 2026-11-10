#ifndef N64EMU_ZSORT_DSP_H
#define N64EMU_ZSORT_DSP_H
#include <stdint.h>
/* Self-contained C89 DSP implementation. Callers provide DMEM and optional
 * transport callbacks; no native RSP state, vector library or JIT is needed. */
#ifdef __cplusplus
extern "C" {
#endif
int zsort_dsp_supported(const uint32_t *text);
unsigned zsort_dsp_size(void);
void zsort_dsp_init(void *storage, unsigned char *dmem,
                    const uint16_t vectors[32][8], unsigned vco);
int zsort_dsp_run(void *storage, unsigned w0, unsigned w1, unsigned entry);
void zsort_dsp_vectors(void *storage, uint16_t vectors[32][8]);
unsigned zsort_dsp_vco(void *storage);
typedef struct ZSortDspIO {
    void *user;
    int (*dma)(void *user, unsigned ram, unsigned dmem, unsigned bytes, int save);
    unsigned (*flush)(void *user, unsigned cursor, int force, unsigned link);
    unsigned (*status)(void *user);
} ZSortDspIO;
int zsort_dsp_graphics(void *storage, unsigned w0, unsigned w1,
                        unsigned entry, unsigned cursor, ZSortDspIO *io);
int zsort_dsp_resume_graphics(void *storage, unsigned object,
                             unsigned cursor, ZSortDspIO *io);
#define ZSORT_DSP_SNAPSHOT_BYTES 1024u
void zsort_dsp_save(const void *storage, void *snapshot);
void zsort_dsp_restore(void *storage, unsigned char *dmem, const void *snapshot);
#ifdef __cplusplus
}
#endif
#endif
