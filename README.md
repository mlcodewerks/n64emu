# n64emu

![n64emu](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

**Use other libretro cores and N64 emulators for any use for gaming.** 

**Please**

## What?

Personal N64 emulator code playset for anything with:

* **x86/x86-64 CPUs with SSE2, or ARMv7/AArch64 CPUs with NEON**.
* **Vulkan 1.1/DX9/DX11/GL2/GL3.3/GL4/GLES2/GLES 3.0**.
* **Win7 and above/modern Linux**
* **GCC/G++ 9 or newer with C++17 support**

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

The **RSP HLE** option selects **Off** (default) or **On**, independently of cycle-based CPU timing.
Off executes all RSP programs natively in cycle-based LLE; On accelerates supported graphics, audio and task microcode
with RSP LLE fallback for those ucodes not supported.

**Cycle-based timing** selects **On** (default; complete CPU cache and pipeline timing) or **Off** (simplified CPU timing for higher performance). Component clocks and work-based RDP timing remain active in both settings.

Both timing modes are included in the standard core build. **Off** uses seven-clock RAM access estimates and operand-dependent floating-point latency without simulating CPU caches or pipeline interlocks. Floating-point results and exception flags remain precise. **On** retains Accurate timing and is recommended for timing-sensitive demos and homebrew. Restart content after changing the option. USF builds retain their separate ripper compatibility timing.

This emulator fork is based on  **very** extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination

## Building

Linux builds require GNU Make, GCC/G++ 9 or newer, and the OpenGL development
headers/libraries. On Debian/Ubuntu, install `build-essential libgl-dev`.
For an i686 build on an x86-64 host, also install `gcc-multilib g++-multilib`
and the 32-bit OpenGL development libraries. Select GCC 9 explicitly with
`CC=gcc-9 CXX=g++-9` when multiple compiler versions are installed.
The build tracks both compiler versions and target triples and rebuilds objects
when the selected toolchain changes.

| Target | Build command | Libretro output | USF playback library output (`USF=1`) |
| --- | --- | --- | --- |
| Windows x64, MinGW-w64 | `make -j8 platform=win64` | `n64emu_libretro.dll` | `build/usf/x64/n64emu_usf.dll` |
| Windows x86, MinGW-w64 | `make -j8 platform=win32` | `n64emu_libretro.dll` | `build/usf/Win32/n64emu_usf.dll` |
| Linux x86-64 | `make -j8 platform=linux-x86_64` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux i686, with multilib and 32-bit development libraries | `make -j8 platform=linux-i686` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux ARMv7 hard-float, NEON | `make -j8 platform=unix ARCH=arm` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Linux AArch64 | `make -j8 platform=unix ARCH=aarch64` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Raspberry Pi 4, 64-bit Raspberry Pi OS | `make -j8 platform=rpi4` | `n64emu_libretro.so` | `n64emu_usf.so` |
| Raspberry Pi 5, 64-bit Raspberry Pi OS | `make -j8 platform=rpi5` | `n64emu_libretro.so` | `n64emu_usf.so` |

The Windows aliases select both the architecture and matching MinGW C/C++ tools.
They use MSYS2's `/mingw32/bin` or `/mingw64/bin` when installed; otherwise the
`i686-w64-mingw32-` or `x86_64-w64-mingw32-` tools must be on `PATH`.
Explicit `CC`, `CXX` and `CROSS_COMPILE` overrides remain supported.
`platform=win ARCH=x86` and `platform=win ARCH=x86_64` remain available.
The Linux aliases use the host compiler with `-m64` or `-m32`; the existing
`platform=unix ARCH=x86_64` and `platform=unix ARCH=i686` forms also work.
