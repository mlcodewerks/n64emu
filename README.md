# n64emu

![n64emu](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

## What?

Personal N64 emulator code playset for anything with:

* **x86/x86-64 CPUs with SSE2, or ARMv7/AArch64 CPUs with NEON**.
* **Modern Windows/modern Linux**
* **GCC/G++ 9 or newer with C++17 support**
* CPU with 2 or more cores.

## How?

Making a garbage fire by hacking on parallel-n64 until almost nothing of the original core remains.

This includes:
- Native RSP execution for graphics, audio and custom microcode.
- Vectorised+threaded LLE RDP (SoftRDP).
- Cycle-based JITs for both RSP and main CPU.
- Cycle-based timing of all components, including RDP stalls/delays.
- USF playback support.

Many bits of info was gleaned from CEN64 and Ares. \
SoftRDP is used for the RDP renderer. RSP emulator derived from parallel-rsp. \
Many thanks to the authors of these emulators. \
Thanks to [LongplayArchive](https://www.youtube.com/user/Longplayarchive) for the useful NTSC N64 hardware reference behaviour recordings.

**Cycle timing** selects **On** (default) or **Off**. On always enables full CPU
cache and pipeline timing. Off uses Fast timing for known, untagged cartridges.
Explicit Compatible tags and titles absent from the database use Compatible.
Cycle timing overrides every database tag. RSP/RDP clocks remain active in all
profiles, and changing the setting requires restarting content.

This emulator fork is based on  **very** extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination
- Various demoscene demos which exercise RDP/RSP/main CPU timing, and weird Nintendo 64 hardware quirks.

## Building

### Libretro and USF libraries

Linux builds require GNU Make and GCC/G++ 9 or newer. On Debian/Ubuntu, install
`build-essential`.
For an i686 build on an x86-64 host, also install `gcc-multilib g++-multilib`
and the 32-bit C/C++ development libraries. Select GCC 9 explicitly with
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

## foobar2000 USF component

`src/foo_input_usf` builds the foobar2000 component against the USF player.
PSF loading is integrated in `src/usf/usf.cpp` and uses bundled miniz.
The foobar2000 SDK and WTL must be supplied manually; see the
[component build instructions](src/foo_input_usf/README.md).
