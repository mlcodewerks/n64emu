

#include "rdram.h"

#include "core/types.h"
#include "core/log.h"
#include "device/device.h"
#include "device/memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/rcp.h"

#include <string.h>

#define RDRAM_BCAST_ADDRESS_MASK UINT32_C(0x00080000)


/* XXX: deduce # of RDRAM modules from it's total size
 * Assume only 2Mo RDRAM modules.
 * Proper way of doing it would be to declare in init_rdram
 * what kind of modules we insert and deduce dram_size from
 * that configuration.
 */
static size_t get_modules_count(const struct rdram* rdram)
{
    return (rdram->dram_size) / 0x200000;
}

static uint8_t cc_value(uint32_t mode_reg)
{
    return ((mode_reg & 0x00000040) >>  6)
        |  ((mode_reg & 0x00004000) >> 13)
        |  ((mode_reg & 0x00400000) >> 20)
        |  ((mode_reg & 0x00000080) >>  4)
        |  ((mode_reg & 0x00008000) >> 11)
        |  ((mode_reg & 0x00800000) >> 18);
}


static osal_inline uint16_t idfield_value(uint32_t device_id)
{
    return ((((device_id >> 26) & 0x3f) <<  0)
          | (((device_id >> 23) & 0x01) <<  6)
          | (((device_id >>  8) & 0xff) <<  7)
          | (((device_id >>  7) & 0x01) << 15));
}

static size_t get_module(const struct rdram* rdram, uint32_t address)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    uint16_t id_field = ri_address_to_id_field(address);


    for (module = 0; module < modules; ++module) {
        /* Each supported chip spans two adjacent 1 MiB device addresses. */
        if ((id_field & ~1u) == (idfield_value(rdram->regs[module][RDRAM_DEVICE_ID_REG]) & ~1u)) {
            return module;
        }
    }

    /* can happen during memory detection because
     * it probes potentialy non present RDRAM */
    return RDRAM_MAX_MODULES_COUNT;
}

static void read_rdram_dram_corrupted(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);
    size_t module;

    if ((size_t)addr >= rdram->dram_size / 4) { *value = 0; return; }
    *value = rdram->dram[addr];

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    /* corrupt read value if CC value is not calibrated */
    uint32_t mode = rdram->regs[module][RDRAM_MODE_REG] ^ UINT32_C(0xc0c0c0c0);
    if ((mode & 0x80000000) && (cc_value(mode) == 0)) {
        *value = 0;
    }
}

static void map_corrupt_rdram(struct rdram* rdram, int corrupt)
{
    struct mem_mapping mapping;

    mapping.begin = MM_RDRAM_DRAM;
    mapping.end = MM_RDRAM_DRAM + rdram->dram_size - 1;
    mapping.type = CORE_MEMORY_RDRAM;
    mapping.handler.opaque = rdram;
    mapping.handler.read32 = (corrupt)
        ? read_rdram_dram_corrupted
        : read_rdram_dram;
    mapping.handler.write32 = write_rdram_dram;

    apply_mem_mapping(rdram->r4300->mem, &mapping);
}


void init_rdram(struct rdram* rdram,
                uint32_t* dram,
                size_t dram_size,
                struct r4300_core* r4300)
{
    rdram->dram = dram;
    rdram->dram_size = dram_size;
    rdram->r4300 = r4300;
}

void poweron_rdram(struct rdram* rdram)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    memset(rdram->regs, 0, RDRAM_MAX_MODULES_COUNT*RDRAM_REGS_COUNT*sizeof(uint32_t));
    memset(rdram->dram, 0, rdram->dram_size);

    core_log(CORE_LOG_INFO, "Initializing %u RDRAM modules for a total of %u MB",
        (uint32_t) modules, (uint32_t) rdram->dram_size / (1024*1024));

    for (module = 0; module < modules; ++module) {
        rdram->regs[module][RDRAM_CONFIG_REG] = UINT32_C(0xb4190010);
        rdram->regs[module][RDRAM_DEVICE_ID_REG] = UINT32_C(0x00000000);
        /* Cold chips require a repeated write to change WriteDelay from 4
         * to the RI's one-cycle command timing. */
        rdram->regs[module][RDRAM_DELAY_REG] = UINT32_C(0x00000023);
        rdram->regs[module][RDRAM_MODE_REG] = UINT32_C(0xc0c0c0c0);
        rdram->regs[module][RDRAM_REF_ROW_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_RAS_INTERVAL_REG] = UINT32_C(0x101c0a04);
        rdram->regs[module][RDRAM_MIN_INTERVAL_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_ADDR_SELECT_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_DEVICE_MANUF_REG] = UINT32_C(0x00000500);
    }
}


void read_rdram_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    if (reg >= RDRAM_REGS_COUNT) { *value = 0; return; }
    /* Odd registers occupy the upper half of the RDRAM register bus. */
    if ((reg & 1) && !(rdram->r4300->mi->regs[MI_INIT_MODE_REG] & 0x200)) {
        *value = 0;
        return;
    }
    size_t module;

    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        *value = 0;
        core_log(CORE_LOG_WARNING, "Reading from broadcast address is unsupported %08x", address);
        return;
    }

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    *value = rdram->regs[module][reg];
    if (reg == RDRAM_DELAY_REG) {
        /* The low bits in each timing byte are fixed on register reads. */
        *value = (*value & UINT32_C(0x38381838)) | UINT32_C(0x03030203);
    }

    /* some bits are inverted when read */
    if (reg == RDRAM_MODE_REG) {
        *value ^= UINT32_C(0xc0c0c0c0);
    }
}

static void write_rdram_module(struct rdram* rdram, size_t module, uint32_t reg,
                               uint32_t value, uint32_t mask)
{
    uint32_t mi_mode = rdram->r4300->mi->regs[MI_INIT_MODE_REG];
    if (((rdram->regs[module][RDRAM_DELAY_REG] >> 3) & 7) != 1) {
        if (!(mi_mode & 0x80) || (mi_mode & 0x7f) < 15) return;
        /* MI repeat supplies the initial command at the cold chip's phase. */
        value = (value << 16) | (value >> 16);
        mask = (mask << 16) | (mask >> 16);
    }
    if (reg == RDRAM_DELAY_REG) mask &= UINT32_C(0x38381838);
    masked_write(&rdram->regs[module][reg], value, mask);
}

void write_rdram_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    if (reg >= RDRAM_REGS_COUNT) return;
    size_t module;
    size_t modules = get_modules_count(rdram);

    if (reg == RDRAM_CONFIG_REG || reg == RDRAM_DEVICE_MANUF_REG) return;

    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        for (module = 0; module < modules; ++module) {
            write_rdram_module(rdram, module, reg, value, mask);
        }
    }
    else {
        module = get_module(rdram, address);
        if (module != RDRAM_MAX_MODULES_COUNT) {
            write_rdram_module(rdram, module, reg, value, mask);
        }
    }
    rdram->r4300->mi->regs[MI_INIT_MODE_REG] &= ~UINT32_C(0x80);

    /* Calibration can alternate manual and automatic current control on
     * individual chips. A broadcast MODE write is not necessarily its end. */
    if (reg == RDRAM_MODE_REG) {
        int corrupt = 0;
        for (module = 0; module < modules; ++module) {
            uint32_t mode = rdram->regs[module][RDRAM_MODE_REG] ^ UINT32_C(0xc0c0c0c0);
            corrupt |= (mode & UINT32_C(0x80000000)) && cc_value(mode) == 0;
        }
        int was_corrupt = mem_get_handler(rdram->r4300->mem, MM_RDRAM_DRAM)->read32 == read_rdram_dram_corrupted;
        if (corrupt != was_corrupt) map_corrupt_rdram(rdram, corrupt);
    }
}


void read_rdram_dram(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    *value = (size_t)addr < rdram->dram_size / 4 ? rdram->dram[addr] : 0;
}

void write_rdram_dram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    if ((size_t)addr < rdram->dram_size / 4)
        masked_write(&rdram->dram[addr], value, mask);
}
