

#ifndef PARALLAX_DEVICE_RCP_RI_RDRAM_H
#define PARALLAX_DEVICE_RCP_RI_RDRAM_H

#include <stddef.h>
#include <stdint.h>

#include "osal/preproc.h"

struct r4300_core;

enum rdram_registers
{
    RDRAM_CONFIG_REG,
    RDRAM_DEVICE_ID_REG,
    RDRAM_DELAY_REG,
    RDRAM_MODE_REG,
    RDRAM_REF_INTERVAL_REG,
    RDRAM_REF_ROW_REG,
    RDRAM_RAS_INTERVAL_REG,
    RDRAM_MIN_INTERVAL_REG,
    RDRAM_ADDR_SELECT_REG,
    RDRAM_DEVICE_MANUF_REG,
    RDRAM_REGS_COUNT
};

/* IPL3 rdram initialization accepts up to 8 RDRAM modules */
enum { RDRAM_MAX_MODULES_COUNT = 8 };

struct rdram
{
    uint32_t regs[RDRAM_MAX_MODULES_COUNT][RDRAM_REGS_COUNT];

    uint32_t* dram;
    size_t dram_size;

    struct r4300_core* r4300;
};

static osal_inline uint32_t rdram_reg(uint32_t address)
{
    return (address & 0x3ff) >> 2;
}

static osal_inline uint32_t rdram_dram_address(uint32_t address)
{
    return (address & 0x03fffffc) >> 2;
}

void init_rdram(struct rdram* rdram,
                uint32_t* dram,
                size_t dram_size,
                struct r4300_core* r4300);

void poweron_rdram(struct rdram* rdram);

void read_rdram_regs(void* opaque, uint32_t address, uint32_t* value);
void write_rdram_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

void read_rdram_dram(void* opaque, uint32_t address, uint32_t* value);
void write_rdram_dram(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

#endif
