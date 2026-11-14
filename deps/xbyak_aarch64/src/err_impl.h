#pragma once
/*
 * Copyright 2020-2023 FUJITSU LIMITED
 * Copyright 2026 Arm Ltd. and affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

Error::Error(int err) : err_(err), msg_("") {
  if (err_ <= 0)
    return;
  fprintf(stderr, "bad err=%d in Xbyak::Error\n", err_);
  static const char *tbl[] = {
      "none",
      "code is too big",
      "label is redefined",
      "label is too far",
      "label is not found",
      "bad parameter",
      "can't protect",
      "offset is too big",
      "can't alloc",
      "label is not set by L()",
      "label is already set by L()",
      "internal error",
      "illegal register index (can not encoding register index)",
      "illegal register element index (can not encoding element index)",
      "illegal predicate register type",
      "illegal immediate parameter (range error)",
      "illegal immediate parameter (unavailable value error)",
      "illegal immediate parameter (condition error)",
      "illegal shift-mode paramater",
      "illegal extend-mode parameter",
      "illegal condition parameter",
      "illegal barrier option",
      "illegal const parameter (range error)",
      "illegal const parameter (unavailable error)",
      "illegal const parameter (condition error)",
      "illegal type",
      "bad align",
      "bad addressing",
      "bad scale",
      "munmap failed",
      "register is not in use",
      "register already in use",
      "no unallocated register available",
      "illegal register family",
      "register is not tracked by manager",
      "register is already tracked by manager",
      "bad scoped register",
  };
  if ((size_t)err_ >= sizeof(tbl) / sizeof(tbl[0])) {
    msg_ = "bad err num";
  } else {
    msg_ = tbl[err_];
  }
}
