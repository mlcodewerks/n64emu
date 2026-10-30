# parallax

![parallax](https://user-images.githubusercontent.com/56025978/170892431-f2e70f44-1a40-4dc6-975d-0a89d00a8589.png)

**Use other libretro cores and N64 emulators for any use for gaming.** 

**Please**

## What?

Personal N64 emulator code playset for anything with:

* **x86-64 CPUs with SSE2, or Linux AArch64 CPUs with NEON**.
* **Vulkan 1.1/GL3.3/GLES 3.0**.
* **Win7 and above/modern Linux**
* **Modern GCC/G++ with C++20 support**

## How?

Making a garbage fire by hacking on parallel-n64 to see what the heck happens, if any, until almost nothing of the original core remains. This includes complete HLE for commercial game RSP tasks, 
GL3.3/GLES3/DX based LLE RDP, per-cycle level timing, etc.

Many bits of info was gleaned from CEN64, Ares and MAME. Many thanks to the authors of these emulators.

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

This is in no way intended to be a "daily driver" or even maintained as such. This is worked on **entirely** when I feel like to mess around with N64 things.

