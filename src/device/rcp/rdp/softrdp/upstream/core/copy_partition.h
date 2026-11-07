#ifndef SR_COPY_PARTITION_H
#define SR_COPY_PARTITION_H

#include "rdp_state.h"

/* Copy fetches advance texture coordinates once per native copy group. A row
 * boundary can split a draw without changing those groups only at this phase.
 * Byte targets keep the command-ordered hidden-bit latch path. */
static inline bool sr_copy_rows_partitionable(uint32_t width, uint32_t first_x,
                                              rdp_texture_size size)
{
    if (!width || size < RDP_SIZE_16BPP || size > RDP_SIZE_32BPP) return false;
    const uint32_t group = size == RDP_SIZE_16BPP ? 4u : 2u;
    return width % group == 0u && first_x % group == 0u;
}

#endif
