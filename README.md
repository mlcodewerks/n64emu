# parallax

![parallax](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

**Use other libretro cores and N64 emulators for any use for gaming.** 

**Please**

## What?

Personal N64 emulator code playset for anything with:

* **x86-64 CPUs with SSE2, or Linux AArch64 CPUs with NEON**.
* **Vulkan 1.1//DX9/DX11/GL2/GL3.3/GL4/GLES2/GLES 3.0**.
* **Win7 and above/modern Linux**
* **Modern GCC/G++ with C++20 support**

## How?

Making a garbage fire by hacking on parallel-n64 to see what the heck happens, if any, until almost nothing of the original core remains.

This includes:
- Complete bit-exact HLE with cycle-timing for **all** commercial game RSP tasks, based on disassembly of the entire NTSC N64  game library's tasks.
- GL2+GL3.3+GL4/GLES2/GLES3/DX9+DX11 based raster LLE RDP.
- Vectorised+threaded LLE RDP (SoftRDP).
- Compute shader-based LLE RDP (parallel-RDP). 
- Cycle-based JITs for both RSP and main CPU.
- Cycle-based timing of all components like all RCP interfaces.
- RDP timing stall/delay simulation based on countless RDP tests and commercial game hardware recordings.

Many bits of info was gleaned from CEN64, Simple64, Ares and MAME. Many thanks to the authors of these emulators. 

The `gles2` renderer uses the [legacy GPU GLES2 backend](src/device/rcp/rdp/legacygpu/DESIGN.md).
Build with `HAVE_GLES2_RDP=1`; GLES targets enable it automatically. A GLES2-only
build can set `HAVE_GLRDP=0 HAVE_GL2_RDP=0 HAVE_PARALLEL_RDP=0`. Fragment shaders
require `highp` with at least 23 precision bits and byte-preserving RGBA targets.

The **HLE level** option selects **Off** or **RSP HLE** (default), independently of CPU cache timing. 
Off executes all RSP programs natively in cycle-based LLE; RSP HLE accelerates supported graphics, audio and task microcode 
with RSP LLE fallback for those ucodes not supported.

**CPU cache timing** selects **Off** (only counts base CPU cycles), **Fast** (base cycles + tags-only cache emulation for performance), or **Accurate** (base cycles + complete cache/pipeline emulation)

This emulator fork is based on  **very** extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination

[As well as countless tests with reference NTSC N64 hardware game recordings](https://www.youtube.com/playlist?list=PLILjRAt8_i888z_8QISC6EE7fcaqCPnPF) 

