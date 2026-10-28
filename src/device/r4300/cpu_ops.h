

#ifndef PARALLAX_DEVICE_R4300_CPU_OPS_H
#define PARALLAX_DEVICE_R4300_CPU_OPS_H

/* Precise opcode semantics for native JIT helpers, delay slots and faults.
 * This module does not provide a continuous interpreter execution mode. */

#include <stdbool.h>
#include <stdint.h>

struct r4300_core;

typedef void (*r4300_op_handler)(struct r4300_core* r4300, uint32_t op);

extern bool breakloop;

r4300_op_handler r4300_ops_decode(uint32_t op, int idle);
void r4300_ops_execute_one(struct r4300_core* r4300);
/* Shared COP1 bodies; callers have decoded a legal arithmetic/compare opcode. */
void r4300_ops_fpu_arithmetic(struct r4300_core* r4300, uint32_t op);
void r4300_ops_fpu_compare(struct r4300_core* r4300, uint32_t op);
/* Integer ABI for native COP1 emission; shared validation and result rules. */
int r4300_ops_fpu_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3]);
void r4300_ops_fpu_finish(struct r4300_core*, uint32_t op, uint64_t bits, unsigned int flags);
int r4300_ops_fpu_compare_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3]);
int r4300_ops_jit_fpu_prepare(struct r4300_core*, uint32_t op, uint64_t inputs[3], uint64_t* commits);
int r4300_ops_jit_fpu_prepare_prefetched(struct r4300_core*,uint32_t,uint64_t[3],uint64_t*);
void r4300_ops_fpu_compare_finish(struct r4300_core*, int result);

#endif /* PARALLAX_DEVICE_R4300_CPU_OPS_H */
