// IA-32 lowering of the shared precise CPU block protocol.
// SPDX-License-Identifier: GPL-2.0-or-later
#if !defined(__i386__) && !defined(_M_IX86)
#error n64emuJIT x86 requires a 32-bit x86 compiler.
#endif
#include "pair32/frontend.inc"
