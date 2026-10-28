/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - memory.c                                        *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <string.h>

#include "memory.h"
#include "hle_simd.h"

#if HLE_SIMD_ENABLED
/* Permutation is confined to each native word. Aliased walks keep their
 * original byte/halfword order, including self-overlap during endian swaps. */
static int memory_vector_safe(const void *dst,const void *src,size_t n)
{
    uintptr_t d=(uintptr_t)dst,s=(uintptr_t)src;
    return d+n<=s || s+n<=d;
}
static __m128i memory_swap(__m128i v,int halfwords)
{
#ifndef MSB_FIRST
    if (!halfwords)
        v = _mm_or_si128(_mm_srli_epi16(v,8), _mm_slli_epi16(v,8));
    v = _mm_shufflelo_epi16(v, _MM_SHUFFLE(2,3,0,1));
    return _mm_shufflehi_epi16(v, _MM_SHUFFLE(2,3,0,1));
#else
    return v;
#endif
}
#endif

/* Global functions */
void load_u8(uint8_t* dst, const unsigned char* buffer, unsigned address, size_t count)
{
#if HLE_SIMD_ENABLED
    if (!(address&3) && memory_vector_safe(dst,buffer+address,(count+3)&~(size_t)3))
        while(count>=16) {
            _mm_storeu_si128((__m128i *)dst,memory_swap(_mm_loadu_si128((const __m128i *)(buffer+address)),0));
            dst+=16;address+=16;count-=16;
        }
#endif
    while (count != 0) {
        *(dst++) = *u8(buffer, address);
        address += 1;
        --count;
    }
}

void load_u16(uint16_t* dst, const unsigned char* buffer, unsigned address, size_t count)
{
#if HLE_SIMD_ENABLED
    if (!(address&3) && memory_vector_safe(dst,buffer+address,(count*2+3)&~(size_t)3))
        while(count>=8) {
            _mm_storeu_si128((__m128i *)dst,memory_swap(_mm_loadu_si128((const __m128i *)(buffer+address)),1));
            dst+=8;address+=16;count-=8;
        }
#endif
    while (count != 0) {
        *(dst++) = *u16(buffer, address);
        address += 2;
        --count;
    }
}

void load_u32(uint32_t* dst, const unsigned char* buffer, unsigned address, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(dst, u32(buffer, address), count * sizeof(uint32_t));
}

void store_u8(unsigned char* buffer, unsigned address, const uint8_t* src, size_t count)
{
#if HLE_SIMD_ENABLED
    if (!(address&3) && memory_vector_safe(buffer+address,src,(count+3)&~(size_t)3))
        while(count>=16) {
            _mm_storeu_si128((__m128i *)(buffer+address),memory_swap(_mm_loadu_si128((const __m128i *)src),0));
            src+=16;address+=16;count-=16;
        }
#endif
    while (count != 0) {
        *u8(buffer, address) = *(src++);
        address += 1;
        --count;
    }
}

void store_u16(unsigned char* buffer, unsigned address, const uint16_t* src, size_t count)
{
#if HLE_SIMD_ENABLED
    if (!(address&3) && memory_vector_safe(buffer+address,src,(count*2+3)&~(size_t)3))
        while(count>=8) {
            _mm_storeu_si128((__m128i *)(buffer+address),memory_swap(_mm_loadu_si128((const __m128i *)src),1));
            src+=8;address+=16;count-=8;
        }
#endif
    while (count != 0) {
        *u16(buffer, address) = *(src++);
        address += 2;
        --count;
    }
}

void store_u32(unsigned char* buffer, unsigned address, const uint32_t* src, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(u32(buffer, address), src, count * sizeof(uint32_t));
}

