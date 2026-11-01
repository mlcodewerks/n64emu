#ifndef N64EMU_DEVICE_PIF_H
#define N64EMU_DEVICE_PIF_H

#include <stddef.h>
#include <stdint.h>
#include "osal/preproc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct r4300_core;
struct joybus_device_interface;
struct si_controller;

enum cic_version
{
    CIC_X101,
    CIC_X102,
    CIC_X103,
    CIC_X105,
    CIC_X106,
    CIC_5101,
    CIC_5167,
    CIC_8303,
    CIC_8401,
    CIC_8501
};

struct cic
{
    const char* name;
    enum cic_version version;
    unsigned int seed;
};

void init_cic_using_ipl3(struct cic* cic, const void* ipl3);

#define CHL_LEN 0x20

void n64_cic_nus_6105(char chl[], char rsp[], int len);

/* Simulates end result of PIFBootROM execution.
 * This bypasses entirely the execution of real PIFBootROM */
void pif_bootrom_hle_execute(struct r4300_core* r4300);


enum { PIF_ROM_SIZE = 0x7c0 };
enum { PIF_RAM_SIZE = 0x40 };
enum { PIF_CHANNELS_COUNT = 5 };

struct pif_channel
{
    void* jbd;
    const struct joybus_device_interface* ijbd;

    uint8_t* tx;
    uint8_t* tx_buf;
    uint8_t* rx;
    uint8_t* rx_buf;
};

void disable_pif_channel(struct pif_channel* channel);
size_t setup_pif_channel(struct pif_channel* channel, uint8_t* buf);

struct pif
{
    uint8_t* base;
    uint8_t* ram;
    struct pif_channel channels[PIF_CHANNELS_COUNT];

    struct cic cic;

    struct r4300_core* r4300;
    struct si_controller* si;
};

static osal_inline uint32_t pif_address(uint32_t address)
{
    return (address & 0xfffc);
}

void init_pif(struct pif* pif,
    uint8_t* pif_base,
    void* jbds[PIF_CHANNELS_COUNT],
    const struct joybus_device_interface* ijbds[PIF_CHANNELS_COUNT],
    const uint8_t* ipl3,
    struct r4300_core* r4300,
    struct si_controller* si);

void poweron_pif(struct pif* pif);

void reset_pif(struct pif* pif, unsigned int reset_type);

void setup_channels_format(struct pif* pif);

void read_pif_mem(void* opaque, uint32_t address, uint32_t* value);
void write_pif_mem(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

void process_pif_ram(struct pif* pif);
void update_pif_ram(struct pif* pif);

void hw2_int_handler(void* opaque);

#ifdef __cplusplus
}
#endif

#endif
