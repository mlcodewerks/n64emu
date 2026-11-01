#ifndef N64EMU_JOYBUS_H
#define N64EMU_JOYBUS_H

#include "core/log.h"

#include <stdint.h>

enum joybus_commands
{
    JCMD_STATUS = 0x00,
    JCMD_CONTROLLER_READ = 0x01,
    JCMD_PAK_READ = 0x02,
    JCMD_PAK_WRITE = 0x03,
    JCMD_EEPROM_READ = 0x04,
    JCMD_EEPROM_WRITE = 0x05,
    JCMD_AF_RTC_STATUS = 0x06,
    JCMD_AF_RTC_READ = 0x07,
    JCMD_AF_RTC_WRITE = 0x08,
    JCMD_RESET = 0xff,
};

enum joybus_device_types
{
    JDT_NONE             = 0x0000,
    JDT_JOY_ABS_COUNTERS = 0x0001,  /* joystick with absolute coordinates */
    JDT_JOY_REL_COUNTERS = 0x0002,  /* joystick with relative coordinates (= mouse) */
    JDT_JOY_PORT         = 0x0004,  /* has port for external paks */
    JDT_AF_RTC           = 0x1000,  /* RTC */
    JDT_EEPROM_4K        = 0x8000,  /* 4k EEPROM */
    JDT_EEPROM_16K       = 0xc000,  /* 16k EEPROM */
};

/* snippet which helps validate command format */
#define JOYBUS_CHECK_COMMAND_FORMAT(expected_tx, expected_rx) \
    if (*tx != expected_tx || *rx != expected_rx) { \
        core_log(CORE_LOG_WARNING, "Unexpected command format %02x %02x %02x ", \
            *tx, *rx, cmd); \
        *rx |= 0x40; \
        break; \
    }



struct joybus_device_interface
{
    /* Optional. Called at device poweron.
     * Allow the joybus device to initialize itself.
     */
    void (*poweron)(void* jbd);

    /* Required. Perform command processing.
     */
    void (*process)(void* jbd,
        const uint8_t* tx, const uint8_t* tx_buf,
        uint8_t* rx, uint8_t* rx_buf);

    /* Optional. Called after channel setup.
     */
    void (*post_setup)(void* jbd,
        uint8_t* tx, const uint8_t* tx_buf,
        const uint8_t* rx, const uint8_t* rx_buf);
};

#endif
