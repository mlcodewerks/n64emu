# n64emu

![parallax](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

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

Run GNU Make from the repository root. Use a C/C++ compiler targeting the
architecture being built; setting `ARCH` alone does not provide a cross compiler.
The CPU and RSP JITs are selected automatically for x86, x86-64, ARMv7 with NEON,
and AArch64. Windows supports x86 and x86-64; ARM builds target Linux.

| Target | Build command | Libretro output |
| --- | --- | --- |
| Windows x64, MSYS2 MINGW64 or UCRT64 shell | `make -j8 platform=win ARCH=x86_64` | `parallax_libretro.dll` |
| Windows x86, MSYS2 MINGW32 shell | `make -j8 platform=win ARCH=x86` | `parallax_libretro.dll` |
| Linux x86-64 | `make -j8 platform=unix ARCH=x86_64` | `parallax_libretro.so` |
| Linux x86, with multilib and 32-bit development libraries | `make -j8 platform=unix ARCH=x86` | `parallax_libretro.so` |
| Linux ARMv7 hard-float, NEON | `make -j8 platform=unix ARCH=arm` | `parallax_libretro.so` |
| Linux AArch64 | `make -j8 platform=unix ARCH=aarch64` | `parallax_libretro.so` |
| Raspberry Pi 4, 64-bit Raspberry Pi OS | `make -j8 platform=rpi4` | `parallax_libretro.so` |
| Raspberry Pi 5, 64-bit Raspberry Pi OS | `make -j8 platform=rpi5` | `parallax_libretro.so` |

On [MSYS2](https://www.msys2.org/docs/environments/), install `make` and the
matching toolchain, for example:

```sh
# MSYS2 MINGW64
pacman -S --needed make mingw-w64-x86_64-toolchain
# MSYS2 MINGW32
pacman -S --needed make mingw-w64-i686-toolchain
```

Linux builds need GNU Make, a C/C++ toolchain, and OpenGL development libraries
for the desktop renderers. For Debian/Ubuntu, `build-essential libgl-dev
libx11-dev` supplies the usual desktop prerequisites. GLES/Pi builds use Mesa
EGL/GLES development libraries (`libegl-dev libgles-dev`). CPU/RSP assembler
dependencies and Vulkan headers are included in the repository.

Cross compile Linux ARM builds with the corresponding compiler prefix:

```sh
make -j8 platform=unix ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
make -j8 platform=unix ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu-
```

The sysroot must contain the target's graphics development libraries. Use
`FORCE_GLES3=1` for a generic Linux GLES 3 build. `DEBUG=1` includes debug symbols.
`CPU_BACKEND=interpreter` and `RSP_BACKEND=interpreter` independently select the
portable engines. The build records architecture/backend changes and rebuilds
affected objects; do not build different configurations concurrently in one tree.

### USF audio decoder

Add `USF=1`, or use `make usf`, to build the lazyusf2-compatible decoder API in
[src/usf/usf.h](src/usf/usf.h). It includes no RDP rasterizer, graphics display-list
walker, image/video decoder, presentation code, or renderer worker pool. Minimal
register/framebuffer stubs acknowledge startup graphics tasks and preserve the
CPU/RSP interfaces needed by music rips. Audio HLE and native RSP audio remain
available. Legacy USF instruction/device timing is independent of the video core.

```sh
# Matching MSYS2 toolchain shells
make -j8 platform=win ARCH=x86_64 USF=1
make -j8 platform=win ARCH=x86 USF=1

# Linux, using a native compiler or the appropriate CROSS_COMPILE prefix
make -j8 platform=unix ARCH=x86_64 USF=1
make -j8 platform=unix ARCH=x86 USF=1
make -j8 platform=unix ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- USF=1
make -j8 platform=unix ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu- USF=1
```

Windows outputs are `build/usf/x64/parallax_usf.dll` and
`build/usf/Win32/parallax_usf.dll`, with GNU import archives (`.dll.a`) and export
definitions (`.def`) alongside them. `USF_BUILD_DIR=path` overrides that directory.
Linux outputs `parallax_usf.so`; USF builds need no graphics development libraries.

For an MSVC consumer, including foobar2000's delay-loaded decoder, create an MSVC
import library after building the DLL. A GNU import archive cannot provide MSVC
delay-load support. Install Visual Studio C++ Build Tools, then run in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./tools/build_usf_import_library.ps1 -Architecture x64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./tools/build_usf_import_library.ps1 -Architecture Win32
```

The script finds `lib.exe` through PATH or Visual Studio Installer. Pass
`-BuildDirectory` when using a custom output directory, or `-LibraryManager` to
select `lib.exe` explicitly. The foobar2000 project can import
[src/usf/foobar_usf.props](src/usf/foobar_usf.props); its architecture and DLL
directory must match the decoder. The decoder also needs each miniUSF's referenced
`.usflib` files in their relative locations.

For a synthetic API/playback check in the matching MSYS2 shell:

```sh
gcc -O2 benchmarks/usf_checks.c -o build/usf_checks.exe
./build/usf_checks.exe build/usf/x64/parallax_usf.dll
# Use build/usf/Win32/parallax_usf.dll with a 32-bit test executable.
```

On Linux:

```sh
mkdir -p build
cc -O2 benchmarks/usf_checks.c -ldl -o build/usf_checks
./build/usf_checks ./parallax_usf.so
```

These checks need no game files.

### JIT checks

The 32-bit backends use paired 32-bit CPU values, the shared precise CPU fallback
protocol, executable-code publication and validated tail links. The RSP backends
share the existing scalar compiler, vector SSA optimizer and register allocator;
vector instructions lower to SSE2 on x86 and NEON on ARMv7. Instructions outside
the CPU's native integer lowering retain the shared precise handlers.

```sh
# In MINGW32, or on a native 32-bit Linux host
make -f benchmarks/jit32/checks.mk CXX=g++ check

# Build the same checks for ARMv7 (run the outputs on an ARM Linux machine)
make -f benchmarks/jit32/checks.mk OUT=build/jit32-arm \
    CXX='arm-linux-gnueabihf-g++ -march=armv7-a -mfpu=neon -mfloat-abi=hard' all
```

See [benchmarks/jit32/README.md](benchmarks/jit32/README.md) for the differential
checks and ARM machine-code validation tools.
