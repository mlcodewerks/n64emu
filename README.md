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

Making a garbage fire by hacking on parallel-n64 to see what the heck happens, if any. This includes HLE for LLE RDP components, 
GL3.3/GLES3 based LLE RDP, per-cycle level timing, etc.

This is based on extensive tests such as:
- Lemmy's nemu64-test
- Wiseguy's cache timing tests
- Thar0's RDP timing/CPU instruction tests
- Dillonb's rsp-ruination

This is in no way intended to be a "daily driver" or even maintained as such. This is worked on **entirely** when I feel like to mess around with N64 things.

