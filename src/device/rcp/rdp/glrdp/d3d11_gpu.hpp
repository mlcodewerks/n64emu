#pragma once
#include <libretro.h>
#ifdef HAVE_D3D11
#include <d3d11.h>
bool glrdp_dx11_initialize(retro_environment_t environment);
extern "C" void glrdp_dx11_negotiate(retro_environment_t environment);
bool glrdp_dx11_initialize_device(ID3D11Device *device, ID3D11DeviceContext *context);
void glrdp_dx11_shutdown();
void glrdp_dx11_begin();
void glrdp_dx11_end();
void glrdp_dx11_prepare_frame();
void glrdp_dx11_prepare_present(bool updated);
void glrdp_dx11_present();
ID3D11Texture2D *glrdp_dx11_output();
#endif
extern bool glrdp_dx11;
