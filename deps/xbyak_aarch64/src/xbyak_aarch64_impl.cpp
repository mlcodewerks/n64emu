/*
 * Copyright 2020-2023 FUJITSU LIMITED
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */
#define XBYAK_AARCH64_MAKE_INSTANCE
#include "xbyak_aarch64.h"
#include <memory.h>
#include <stdio.h>
#ifdef _WIN32
#include <intrin.h>
#include <processthreadsapi.h> // FlushInstructionCache
#endif

namespace Xbyak_aarch64 {

#include "err_impl.h"
#include "xbyak_aarch64_impl.h"
#include "xbyak_aarch64_mnemonic.h"

} // namespace Xbyak_aarch64
