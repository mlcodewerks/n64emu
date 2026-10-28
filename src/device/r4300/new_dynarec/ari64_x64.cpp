/* Xbyak host emitter for Ari64's shared precise frontend.
 * Derived from Ari64's emitter, Copyright (C) 2009-2011 Ari64.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>
#include <map>
#include <memory>
extern "C" {
#include "ari64.h"
#include "core/log.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/idec.h"
#include "device/r4300/timing.h"
#include "device/r4300/cpu_ops.h"
#include "device/memory.h"
#include "device/rdram.h"
#include "device/r4300/fastmem.h"
}
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__x86_64__) && !defined(_M_X64)
#error The Ari64 backend supports x64 only
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "x64/assem_x64.h"
#include "host_fault.h"
#include "x64/xbyak_emit.h"
#include "x64/frontend_x64.inc"
