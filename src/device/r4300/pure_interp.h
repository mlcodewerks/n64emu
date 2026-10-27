

#ifndef PARALLAX_DEVICE_R4300_PURE_INTERP_H
#define PARALLAX_DEVICE_R4300_PURE_INTERP_H

#include <stdbool.h>
#include <stdint.h>

struct r4300_core;

typedef void (*r4300_interp_handler)(struct r4300_core* r4300, uint32_t op);

extern bool breakloop;

r4300_interp_handler pure_interp_decode(uint32_t op, int idle);
void pure_interp_execute_one(struct r4300_core* r4300);
/* Shared COP1 bodies; callers have decoded a legal arithmetic/compare opcode. */
void pure_interp_fpu_arithmetic(struct r4300_core* r4300, uint32_t op);
void pure_interp_fpu_compare(struct r4300_core* r4300, uint32_t op);
/* Integer ABI for native COP1 emission; shared validation and result rules. */
int pure_interp_fpu_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3]);
void pure_interp_fpu_finish(struct r4300_core*, uint32_t op, uint64_t bits, unsigned int flags);
int pure_interp_fpu_compare_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3]);
int pure_interp_jit_fpu_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3], uint64_t* commits);
int pure_interp_jit_fpu_prepare_prefetched(struct r4300_core*,uint32_t,uint64_t[3],uint64_t*);
void pure_interp_fpu_compare_finish(struct r4300_core*, int result);

#endif /* PARALLAX_DEVICE_R4300_PURE_INTERP_H */
