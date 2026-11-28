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
- Cacheless CPU timing in the optional Cycle timing Off profile.
- Cartridge ROM images up to 252 MiB.
- USF playback support.

Many bits of info was gleaned from CEN64 and Ares. \
SoftRDP is used for the RDP renderer. RSP emulator derived from parallel-rsp. \
Many thanks to the authors of these emulators. \
Thanks to [LongplayArchive](https://www.youtube.com/user/Longplayarchive) for the useful NTSC N64 hardware reference behaviour recordings.

This emulator fork is based on  **very** extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination
- Various demoscene demos which exercise RDP/RSP/main CPU timing, and weird Nintendo 64 hardware quirks.

## Building

The compiled ROM database uses save, controller-pak and region metadata from
the [MiSTer N64 ROM Database](https://github.com/MiSTer-devel/N64_ROM_Database).
The [source data and its GPL-3.0 license](data/mister) accompany the generated
header. Run `python tools/update_mister_rom_database.py --check` to verify it.

### Windows x64 standalone

The standalone build uses MSYS2 with UCRT64 GCC/G++, Python, Meson, Ninja,
and GNU Make. From the repository root, run `standalone\build.bat` to build
`standalone\build\n64emu.exe`. Add `-Test` to run the standalone tests after
building. If MSYS2 is installed somewhere other than `C:\msys64`, pass
`-Msys2Root "D:\msys64"` with the installation path.

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

### Compatibility hacks

Open **Settings → Core → Hacks** in standalone or **Core Options → Hacks**
in RetroArch. All three settings take effect when content restarts.

**CPU clock multiplier** (`n64emu_cpu_overclock`) supports `1x`,
`2x`, and `4x`. `1x` is the stock clock. All multipliers support the native
CPU JIT in either Cycle Timing mode. At `2x` and `4x`, the 64-bit JITs use
shared CPU timing for their generated integer code and precise handlers for
other instructions. Peripheral clock rates and modeled bus service times
keep their stock scale. Game behavior and host performance can change.
Load save states at the multiplier used to create them.

**Ignore TLB exceptions** (`n64emu_ignore_tlb_exceptions`) skips faulting TLB
accesses for ROM hacks that rely on that behavior. Failed loads leave the
destination register as it was. Address and alignment exceptions still apply.

**Disable Expansion Pak** (`n64emu_disable_expansion_pak`) boots with 4 MiB of
RDRAM. The default is 8 MiB. Load save states with the same memory size.

### Controller accessories

In libretro, use the **Player 1–4 controller pak** core options. In the
standalone app, open **Settings → Input → Player 1–4 → Controller Pak**.
Switch between a **Memory Pak** (Controller Pak) and a **Rumble Pak** while
the game is running. The game sees the old Pak come out before the new one
goes in. Memory Pak saves stay with the port. Rumble uses the libretro
frontend's rumble interface or the standalone app's XInput support.

### Nintendo 64 cheats

Load a RetroArch `.cht` list or paste GameShark codes from **Emulation → Cheats**
in standalone. You can toggle individual entries and press the virtual
GameShark button there. In RetroArch, load and apply codes through the Cheats
menu. Player 1's L3 input is the GameShark button.

The native API in `src/core/api.h` validates and sets the same address/value
pairs by index. Supported codes include 8- and 16-bit writes, conditional
writes, repeaters, boot writes, and GameShark-button codes. Boot codes run on
the first frame or when the game is reset. Disabling a cheat stops future
writes, and unloading the ROM clears the list. Standalone imports `.cht`
entries that use RetroArch's emulator handler and shows unsupported entries
as unavailable.

## foobar2000 USF component

`src/foo_input_usf` builds the foobar2000 component against the USF player.
PSF loading is integrated in `src/usf/usf.cpp` and uses bundled miniz.
The foobar2000 SDK and WTL must be supplied manually; see the
[component build instructions](src/foo_input_usf/README.md).
