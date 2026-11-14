/*
 *  Copyright  (C) 2010-2020 The RetroArch team
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

#ifndef __LIBRETRO_SDK_HASH_H
#define __LIBRETRO_SDK_HASH_H

#include <stdint.h>
#include <stddef.h>

#include <compat/msvc.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <retro_inline.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/**
 * sha256_hash:
 * @out               : Output.
 * @in                : Input.
 * @size              : Size of @out.
 *
 * Hashes SHA256 and outputs a human readable string.
 **/
void sha256_hash(char *out, const uint8_t *in, size_t size);

int sha1_calculate(const char *path, char *result);

uint32_t djb2_calculate(const char *str);

/* Any 32-bit or wider unsigned integer data type will do */
typedef unsigned int MD5_u32plus;

typedef struct {
	MD5_u32plus lo, hi;
	MD5_u32plus a, b, c, d;
	unsigned char buffer[64];
	MD5_u32plus block[16];
} MD5_CTX;

/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * See ../../../LICENSE for the license terms.
 */

void MD5_Init(MD5_CTX *ctx);
void MD5_Update(MD5_CTX *ctx, const void *data, unsigned long size);
void MD5_Final(unsigned char *result, MD5_CTX *ctx);

RETRO_END_DECLS

#endif
