#ifndef PARALLAX_RUMBLE_H
#define PARALLAX_RUMBLE_H

enum rumble_action
{
    RUMBLE_STOP,
    RUMBLE_START
};

struct rumble_backend_interface
{
    /* Start or stop rumbling
     */
    void (*exec)(void* rumble, enum rumble_action action);
};

#endif
