#ifndef PARALLAX_DEVICE_CONTROLLERS_H
#define PARALLAX_DEVICE_CONTROLLERS_H

#include "core/joybus.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct game_controller;
struct controller_input_backend_interface;
struct storage_backend_interface;
struct rumble_backend_interface;
struct gb_cart;

struct game_controller_flavor
{
    const char* name;
    uint16_t type;

    /* controller reset procedure */
    void (*reset)(struct game_controller* cont);
};

struct pak_interface
{
    const char* name;
    void (*plug)(void* pak);
    void (*unplug)(void* pak);
    void (*read)(void* pak, uint16_t address, uint8_t* data, size_t size);
    void (*write)(void* pak, uint16_t address, const uint8_t* data, size_t size);
};

struct game_controller
{
    uint8_t status;

    const struct game_controller_flavor* flavor;

    void* cin;
    const struct controller_input_backend_interface* icin;

    void* pak;
    const struct pak_interface* ipak;
};

void init_game_controller(struct game_controller* cont,
    const struct game_controller_flavor* flavor,
    void* cin, const struct controller_input_backend_interface* icin,
    void* pak, const struct pak_interface* ipak);

void change_pak(struct game_controller* cont,
                void* pak, const struct pak_interface* ipak);

/* Controller Joybus interface */
extern const struct joybus_device_interface
    g_ijoybus_device_controller;

/* Controller flavors */
extern const struct game_controller_flavor g_standard_controller_flavor;
extern const struct game_controller_flavor g_mouse_controller_flavor;

struct biopak
{
    unsigned int bpm;
};

void init_biopak(struct biopak* bpk,
    unsigned int bpm);

extern const struct pak_interface g_ibiopak;

struct mempak
{
    void* storage;
    const struct storage_backend_interface* istorage;
};

enum { MEMPAK_SIZE = 0x8000 };

void format_mempak(uint8_t* mem);

void init_mempak(struct mempak* mpk,
                 void* storage,
                 const struct storage_backend_interface* istorage);

extern const struct pak_interface g_imempak;

struct rumblepak
{
    uint8_t state;
    void* rumble;
    const struct rumble_backend_interface* irumble;
};

void init_rumblepak(struct rumblepak* rpk,
    void* rumble, const struct rumble_backend_interface* irumble);

void poweron_rumblepak(struct rumblepak* rpk);

void set_rumble_reg(struct rumblepak* rpk, uint8_t value);

extern const struct pak_interface g_irumblepak;

enum cart_access_mode
{
    CART_NOT_INSERTED = 0x40,
    CART_ACCESS_MODE_0 = 0x80,
    CART_ACCESS_MODE_1 = 0x89
};

struct transferpak
{
    unsigned int enabled;
    unsigned int bank;
    unsigned int access_mode;
    unsigned int access_mode_changed;

    struct gb_cart* gb_cart;
};

void init_transferpak(struct transferpak* tpk, struct gb_cart* gb_cart);
void poweron_transferpak(struct transferpak* tpk);

void change_gb_cart(struct transferpak* tpk, struct gb_cart* gb_cart);

extern const struct pak_interface g_itransferpak;

#ifdef __cplusplus
}
#endif

#endif
