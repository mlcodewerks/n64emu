# n64emu

![n64emu](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

**Use other libretro cores and N64 emulators for any use for gaming.** 

**Please**

## What?

Personal N64 emulator code playset for anything with:

* **x86/x86-64 CPUs with SSE2, or Linux ARMv7/AArch64 CPUs with NEON**.
* **Vulkan 1.1//DX9/DX11/GL2/GL3.3/GL4/GLES2/GLES 3.0**.
* **Win7 and above/modern Linux**
* **Modern GCC/G++ with C++20 support**

## How?

Making a garbage fire by hacking on parallel-n64 until almost nothing of the original core remains.

This includes:
- Complete bit-exact HLE with cycle based-timing for **all** commercial game RSP tasks, based on disassembly of the entire NTSC N64 game library's tasks. Measurements have been done to count how long each RSP task takes to execute to fit in with a cycle-based timing system.
- GL2+GL3.3+GL4/GLES2/GLES3/DX9+DX11 raster-based LLE RDP.
- Vectorised+threaded LLE RDP (SoftRDP).
- Compute shader-based LLE RDP (parallel-RDP). 
- Cycle-based JITs for both RSP and main CPU.
- Cycle-based timing of all components, including RDP stalls/delays.
- USF playback support.

Many bits of info was gleaned from CEN64, Simple64, Ares and MAME. \
Many thanks to the authors of these emulators. \
Thanks to [LongplayArchive](https://www.youtube.com/user/Longplayarchive) for the useful NTSC N64 hardware reference behaviour recordings.

The **HLE level** option selects **Off** or **RSP HLE** (default), independently of CPU cache timing. 
Off executes all RSP programs natively in cycle-based LLE; RSP HLE accelerates supported graphics, audio and task microcode 
with RSP LLE fallback for those ucodes not supported.

**CPU cache timing** selects **Off** (only counts base CPU cycles), **Fast** (base cycles + tags-only cache emulation for performance), or **Accurate** (base cycles + complete cache/pipeline emulation)

This emulator fork is based on  **very** extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination

## Building

| Target | Build command | Libretro output | USF playback library output (`USF=1`) |
| --- | --- | --- | --- |
| Windows x64, MSYS2 MINGW64 or UCRT64 shell | `make -j8 platform=win ARCH=x86_64` | `n64emu_libretro.dll` | `build/usf/x64/n64emu_usf.dll` |
| Windows x86, MSYS2 MINGW32 shell | `make -j8 platform=win ARCH=x86` | `n64emu_libretro.dll` | `build/usf/Win32/n64emu_usf.dll` |
| Linux x86-64 | `make -j8 platform=unix ARCH=x86_64` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux x86, with multilib and 32-bit development libraries | `make -j8 platform=unix ARCH=x86` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux ARMv7 hard-float, NEON | `make -j8 platform=unix ARCH=arm` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux AArch64 | `make -j8 platform=unix ARCH=aarch64` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Raspberry Pi 4, 64-bit Raspberry Pi OS | `make -j8 platform=rpi4` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Raspberry Pi 5, 64-bit Raspberry Pi OS | `make -j8 platform=rpi5` | `n64emu_libretro.so` | `n64emu_usf.so` |