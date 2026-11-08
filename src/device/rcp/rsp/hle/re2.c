#include "hle_timing.h"
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - re2.c                                           *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2016 Gilles Siberlin                                    *
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"
#include "hle_simd.h"
#include "hle_timing_video.h"

static void video_instruction_timing(struct hle_t *hle, enum hle_video_timing_kind kind)
{
    uint32_t p = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t code = *dmem_u32(hle, TASK_UCODE);
    struct hle_video_timing_input input;
    uint32_t cycles;
    input.program = dram_u32(hle, code);
    input.boot = (const uint32_t *)hle->imem;
    input.boot_bytes = *dmem_u32(hle, TASK_UCODE_BOOT_SIZE);
    input.flags = *dmem_u32(hle, TASK_FLAGS);
    input.data_bytes = *dmem_u32(hle, TASK_UCODE_DATA_SIZE);
    input.status = *hle->sp_status;
    input.io_busy = *hle->sp_dma_full | *hle->sp_dma_busy | *hle->sp_semaphore;
    input.unaligned = (p | code | *dram_u32(hle, p) | *dram_u32(hle, p + 4)) & 7u;
    input.kind = kind;
    if (kind == HLE_VIDEO_DECODE) {
        input.width = *dram_u32(hle, p + 16);
        input.height = *dram_u32(hle, p + 20);
        input.rows_per_dma = *dram_u32(hle, p + 24);
        input.row_pairs = *dram_u32(hle, p + 28);
        input.descriptor = *dram_u32(hle, p + 32);
        input.stride_pair = *dram_u32(hle, p + 36);
        input.unaligned |= (*dram_u32(hle, p + 8) | *dram_u32(hle, p + 12)) & 7u;
    } else {
        uint32_t bytes = *dram_u32(hle, p + 8);
        input.width = bytes / 8u;
        input.row_pairs = *dram_u32(hle, p + 16);
        input.height = input.row_pairs * 2u;
        input.rows_per_dma = *dram_u32(hle, p + 12);
        input.descriptor = *dram_u32(hle, p + 24);
        input.stride_pair = *dram_u32(hle, p + 28);
        input.unaligned |= bytes & 7u;
    }
    cycles = hle_timing_video(&input);
    if (cycles) hle_timing_override(cycles);
}

#define SATURATE8(x) ((unsigned int) x <= 255 ? x : (x < 0 ? 0: 255))

/**************************************************************************
 * Resident evil 2 ucodes
 **************************************************************************/
/* Transcribed from the resize microcode.
 *
 * The ucode upscales a 24-bit source image (bytes ordered B,G,R per
 * pixel, 320 pixels / 960 bytes per line) to an RGBA5551 frame, walking
 * a 16.16 fixed-point lattice.  Each output row DMAs 0x780 bytes -- the
 * two source lines the row interpolates between -- to DMEM 0, and the
 * packed output row is staged right behind that window at DMEM 0x780,
 * which matters: for the right-most source column the bottom-right tap
 * address (xint * 3 + 3 + 0x3c0) runs past the window into the staging
 * area, so the tap reads bytes of this row's own first packed pixels.
 * The window below reproduces that feedback.
 *
 * All four bilinear corner weights are built from Q15 x/y fractions
 * with a truncation after every stage, exactly like the ucode:
 * weight = ((frac_a * frac_b) >> 16) * 4 in Q16, and each tap is
 * (sample * weight) >> 16, truncated before the four taps are summed.
 *
 * The ucode also carries a serial error-diffusion pipeline (per-lane
 * accumulators rotated through DMEM, three-channel VABS/VLT flag
 * AND-merge selecting between the accumulator and a constant).  Its
 * comparison constant in the ucode data is zero, and VLT can only pass
 * for |diff| < 0 (never) or a VCO-flagged tie, with VCO provably clear
 * at that point, so the selected error term is the constant 4 on every
 * pixel: the quantizer reduces to min(sum + 4, 255) & 0xf8.  The 16-bit
 * accumulate carries (VADDC into VADD) can never fire at byte scale
 * either.  Both mechanisms are therefore left out of the transcription. */
void resize_bilinear_task(struct hle_t* hle)
{
    uint32_t data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    uint32_t src_addr   = *dram_u32(hle, data_ptr)      & 0x7fffff;
    uint32_t dst_addr   = *dram_u32(hle, data_ptr + 4)  & 0x7fffff;
    uint32_t dst_width  = *dram_u32(hle, data_ptr + 8);
    uint32_t dst_height = *dram_u32(hle, data_ptr + 12);
    uint32_t x_step     = *dram_u32(hle, data_ptr + 16);
    uint32_t y_step     = *dram_u32(hle, data_ptr + 20);
    uint32_t dst_stride = *dram_u32(hle, data_ptr + 24);
    uint32_t x0         = *dram_u32(hle, data_ptr + 32);
    uint32_t y          = *dram_u32(hle, data_ptr + 36);

    uint8_t win[0x1000];
    uint32_t i, j, k;

    memset(win, 0, sizeof(win));

    for (i = 0; i < dst_height; ++i) {
        uint32_t row_src = (src_addr + (y >> 16) * 960) & 0x7fffff;
        uint32_t out = dst_addr;
        uint32_t yf = (y & 0xffff) >> 1;
        uint32_t ny = 0x7fff - yf;
        uint32_t x = x0;

        for (k = 0; k < 0x780; ++k)
            win[k] = *dram_u8(hle, row_src + k);

        for (j = 0; j < dst_width; ++j) {
            if (!(j & 7u)) hle_timing_add(HLE_WORK_PIXEL, 1);
            uint32_t xi = (x >> 16) * 3;
            uint32_t xf = (x & 0xffff) >> 1;
            uint32_t nx = 0x7fff - xf;
            uint32_t w_tl = ((nx * ny) >> 16) * 4;
            uint32_t w_tr = ((xf * ny) >> 16) * 4;
            uint32_t w_bl = ((nx * yf) >> 16) * 4;
            uint32_t w_br = ((xf * yf) >> 16) * 4;
            uint16_t pixel;
            unsigned c;
            uint32_t chan[4];

#if HLE_SIMD_ENABLED
            {
                __m128i v=_mm_set1_epi32(4),p;
#define HLE_RE2_CORNER(off,weight) do { \
    p=_mm_setr_epi32(win[((off)+0)&0xfff],win[((off)+1)&0xfff],win[((off)+2)&0xfff],0); \
    v=_mm_add_epi32(v,_mm_srli_epi32(hle_mullo32(p,_mm_set1_epi32(weight)),16)); \
} while(0)
                HLE_RE2_CORNER(xi,w_tl);HLE_RE2_CORNER(xi+3,w_tr);
                HLE_RE2_CORNER(xi+0x3c0,w_bl);HLE_RE2_CORNER(xi+0x3c0+3,w_br);
#undef HLE_RE2_CORNER
                _mm_storeu_si128((__m128i *)chan,_mm_and_si128(hle_minu32(v,_mm_set1_epi32(255)),_mm_set1_epi32(0xf8)));
            }
#else
            for (c = 0; c < 3; ++c) {
                uint32_t tl = win[(xi + c)                 & 0xfff];
                uint32_t tr = win[(xi + 3 + c)             & 0xfff];
                uint32_t bl = win[(xi + 0x3c0 + c)         & 0xfff];
                uint32_t br = win[(xi + 0x3c0 + 3 + c)     & 0xfff];
                uint32_t v = ((tl * w_tl) >> 16) + ((tr * w_tr) >> 16)
                           + ((bl * w_bl) >> 16) + ((br * w_br) >> 16)
                           + 4;
                if (v > 255)
                    v = 255;
                chan[c] = v & 0xf8;
            }
#endif

            pixel = (uint16_t)((chan[2] << 8) | (chan[1] << 3) | (chan[0] >> 2) | 1);
            win[0x780 + 2 * j]     = (uint8_t)(pixel >> 8);
            win[0x780 + 2 * j + 1] = (uint8_t)pixel;
            dram_store_u16(hle, &pixel, out, 1);
            out += 2;
            x += x_step;
        }

        dst_addr = (dst_addr + dst_stride) & 0x7fffff;
        y += y_step;
    }

    rsp_break(hle, SP_STATUS_TASKDONE);
}

static int32_t sat16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}

static uint32_t YCbCr_to_RGBA(uint8_t Y, uint8_t Cb, uint8_t Cr)
{
#if HLE_SIMD_ENABLED
    return hle_re2_rgba(Y,Cb,Cr);
#else
    /* The RSP has no FPU: the video ucode does this with truncating VMUDM and
     * 16-bit coefficients held in DMEM. Those coefficients (read straight from
     * the RE2 ucode_data) are 38155, 45941, 23401, 11277, 58065 over 1<<16, so
     * the divide is >>16. The previous double form only approximated those
     * coefficients (e.g. 0.582199097 vs the exact 38155/65536) and disagreed
     * with the hardware in a handful of cases; this is the exact integer form. */
    /* The microcode works in a <<7 domain: LUV/LHV load the samples as
     * value<<7, each vmudm truncates its product to (x<<7)*coeff >> 16
     * individually, the terms combine through saturating vadd/vsub,
     * and the store takes bits 14:7 after a [0, 255<<7] clamp.  The
     * per-term truncation loses up to one output LSB per term relative
     * to summing full products, so it must be modeled exactly. */
    int32_t y  = ((int32_t)(Y << 7) * 38155) >> 16;
    int32_t cr = (int32_t)(uint32_t)((Cr - 128) << 7);
    int32_t cb = (int32_t)(uint32_t)((Cb - 128) << 7);
    int32_t cr_r = (int32_t)((int64_t)(int16_t)cr * 45941 >> 16);
    int32_t cr_g = (int32_t)((int64_t)(int16_t)cr * 23401 >> 16);
    int32_t cb_g = (int32_t)((int64_t)(int16_t)cb * 11277 >> 16);
    int32_t cb_b = (int32_t)((int64_t)(int16_t)cb * 58065 >> 16);
    int32_t r7 = sat16(y + cr_r);
    int32_t g7 = sat16(sat16(y - cr_g) - cb_g);
    int32_t b7 = sat16(y + cb_b);

    r7 = (r7 < 0) ? 0 : (r7 > 0x7f80) ? 0x7f80 : r7;
    g7 = (g7 < 0) ? 0 : (g7 > 0x7f80) ? 0x7f80 : g7;
    b7 = (b7 < 0) ? 0 : (b7 > 0x7f80) ? 0x7f80 : b7;

    return ((uint32_t)(r7 >> 7) << 24) | ((uint32_t)(g7 >> 7) << 16) |
           ((uint32_t)(b7 >> 7) << 8) | 0;
#endif
}

/* SP DMA packs rows contiguously in DMEM, wraps at 4 KiB, and applies
 * skip only to RDRAM. Wide video frames overlap the fixed native banks;
 * retaining this window also preserves the loop's read-after-write feedback. */
static void video_window_dma(struct hle_t *hle, uint8_t window[4096],
                             uint32_t cache, uint32_t dram, uint32_t descriptor, int write)
{
    uint32_t bytes = (descriptor & 0xfffu) + 1u;
    uint32_t rows = ((descriptor >> 12) & 255u) + 1u;
    uint32_t skip = descriptor >> 20, row, k;
    bytes = (bytes + 7u) & ~7u;
    cache &= 0xff8u; dram &= 0x7ffff8u;
    for (row = 0; row < rows; ++row) {
        for (k = 0; k < bytes; ++k) {
            uint8_t *ram = dram_u8(hle, (dram + k) & 0x7fffffu);
            if (write) *ram = window[(cache + k) & 0xfffu];
            else window[(cache + k) & 0xfffu] = *ram;
        }
        cache += bytes; dram += bytes + skip;
    }
}

static void video_window_init(struct hle_t *hle, uint8_t window[4096])
{
    uint32_t k, data = *dmem_u32(hle, TASK_UCODE_DATA);
    for (k = 0; k < 4096; ++k) window[k] = *dmem_u8(hle, k);
    video_window_dma(hle, window, 0, data, *dmem_u32(hle, TASK_UCODE_DATA_SIZE) - 1u, 0);
}

static void decode_video_window(struct hle_t *hle, uint32_t p)
{
    uint8_t window[4096];
    uint32_t y = *dram_u32(hle, p), cb = *dram_u32(hle, p + 4);
    uint32_t cr = *dram_u32(hle, p + 8), dest = *dram_u32(hle, p + 12);
    uint32_t width = *dram_u32(hle, p + 16), pairs = *dram_u32(hle, p + 28);
    uint32_t rows = *dram_u32(hle, p + 24), descriptor = *dram_u32(hle, p + 32);
    uint32_t stride = *dram_u32(hle, p + 36), pair, r, x, k;
    video_window_init(hle, window);
    memset(window + 0x300, 0, 0x800);
    for (pair = 0; pair < pairs; ++pair) {
        uint32_t chroma = 0, first = 0, second = width, out = 0x300;
        video_window_dma(hle, window, 0, y, 2u * width - 1u, 0);
        video_window_dma(hle, window, 0x200, cb, width - 1u, 0);
        video_window_dma(hle, window, 0x280, cr, width - 1u, 0);
        for (r = 0; r < rows; ++r) {
            for (x = 0; x < width; x += 16u) {
                uint32_t pixels[2][16];
                /* LUV/LHV latch both rows and chroma before any SFV stores. */
                for (k = 0; k < 16u; ++k) {
                    uint8_t c = window[(0x200u + chroma + k / 2u) & 0xfffu];
                    uint8_t v = window[(0x280u + chroma + k / 2u) & 0xfffu];
                    pixels[0][k] = YCbCr_to_RGBA(window[(first + k) & 0xfffu], c, v);
                    pixels[1][k] = YCbCr_to_RGBA(window[(second + k) & 0xfffu], c, v);
                }
                /* SHV writes planar RGB through the fixed 0xf80 scratch
                 * area. The second row's LUVs happen after first-row SFV;
                 * a wide first row can overwrite that scratch in between. */
                for (k = 0; k < 16u; ++k) {
                    uint32_t row;
                    for (row = 0; row < 2u; ++row) {
                        uint32_t at = 0xf80u + row * 64u;
                        window[at + k] = pixels[row][k] >> 24;
                        window[at + 16u + k] = pixels[row][k] >> 16;
                        window[at + 32u + k] = pixels[row][k] >> 8;
                    }
                }
                for (k = 0; k < 16u; ++k) {
                    uint32_t at = (out + k * 4u) & 0xfffu, color = pixels[0][k];
                    window[at] = color >> 24; window[(at + 1u) & 0xfffu] = color >> 16;
                    window[(at + 2u) & 0xfffu] = color >> 8;
                }
                for (k = 0; k < 16u; ++k)
                    pixels[1][k] = (uint32_t)window[0xfc0u + k] << 24 |
                        (uint32_t)window[0xfd0u + k] << 16 | (uint32_t)window[0xfe0u + k] << 8;
                /* Only the second output row is cleared before its RGB writes. */
                for (k = 0; k < 64u; ++k) window[(out + width * 4u + k) & 0xfffu] = 0;
                for (k = 0; k < 16u; ++k) {
                    uint32_t at = (out + width * 4u + k * 4u) & 0xfffu, color = pixels[1][k];
                    window[at] = color >> 24; window[(at + 1u) & 0xfffu] = color >> 16;
                    window[(at + 2u) & 0xfffu] = color >> 8;
                }
                first += 16u; second += 16u; chroma += 8u; out += 64u;
                hle_timing_add(HLE_WORK_PIXEL, 4);
            }
            first += width; second += width; out += width * 4u;
        }
        video_window_dma(hle, window, 0x300, dest, descriptor, 1);
        y += width * 2u; cb += width / 2u; cr += width / 2u; dest += stride;
    }
}

static void fill_video_window(struct hle_t *hle, uint32_t p)
{
    uint8_t window[4096];
    uint32_t src = *dram_u32(hle, p), dest = *dram_u32(hle, p + 4);
    uint32_t bytes = *dram_u32(hle, p + 8), rows = *dram_u32(hle, p + 12);
    uint32_t pairs = *dram_u32(hle, p + 16), descriptor = *dram_u32(hle, p + 24);
    uint32_t stride = *dram_u32(hle, p + 28), pair, k, i;
    descriptor |= (rows - 1u) << 12;
    video_window_init(hle, window);
    for (pair = 0; pair < pairs; ++pair) {
        video_window_dma(hle, window, 0, src, descriptor, 0);
        video_window_dma(hle, window, 0x800, dest, descriptor, 0);
        for (k = 0; k < bytes; k += 8u) {
            uint8_t mixed[8];
            for (i = 0; i < 8u; ++i)
                mixed[i] = (window[(k + i) & 0xfffu] + window[(0x800u + k + i) & 0xfffu]) >> 1;
            for (i = 0; i < 8u; ++i) window[(0x800u + k + i) & 0xfffu] = mixed[i];
            hle_timing_add(HLE_WORK_PIXEL, 1);
        }
        video_window_dma(hle, window, 0x800, dest, descriptor, 1);
        src += stride; dest += stride;
    }
}

void decode_video_frame_task(struct hle_t* hle)
{
    video_instruction_timing(hle, HLE_VIDEO_DECODE);
    int data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    int pLuminance = *dram_u32(hle, data_ptr);
    int pCb = *dram_u32(hle, data_ptr + 4);
    int pCr = *dram_u32(hle, data_ptr + 8);
    int pDestination = *dram_u32(hle, data_ptr + 12);
    int nMovieWidth = *dram_u32(hle, data_ptr + 16);
    int nMovieHeight = *dram_u32(hle, data_ptr + 20);
    int nScreenDMAIncrement = *dram_u32(hle, data_ptr + 36);

    if (nMovieWidth > 256 && nMovieWidth <= 320 && !(nMovieWidth & 15)) {
        decode_video_window(hle, (uint32_t)data_ptr);
        rsp_break(hle, SP_STATUS_TASKDONE);
        return;
    }

    int i, j;
    uint8_t Y, Cb, Cr;
    uint32_t pixel;
    int pY_1st_row, pY_2nd_row, pDest_1st_row, pDest_2nd_row;

    for (i = 0; i < nMovieHeight; i += 2)
    {
        int pCrPair = pCr;
        pY_1st_row = pLuminance;
        pY_2nd_row = pLuminance + nMovieWidth;
        pDest_1st_row = pDestination;
        pDest_2nd_row = pDestination + (nScreenDMAIncrement >> 1);

        for (j = 0; j < nMovieWidth; j += 2)
        {
            if (!(j & 2)) hle_timing_add(HLE_WORK_PIXEL, 1);
            dram_load_u8(hle, (uint8_t*)&Cb, pCb++, 1);
            dram_load_u8(hle, (uint8_t*)&Cr, pCr++, 1);

            /*1st row*/
            /* the chroma DMAs move a full width of bytes into the
             * half-width buffers, and the Cr transfer spills its tail
             * past DMEM 0x300 into the output staging; the first-row
             * stores never touch every fourth staging byte, so those
             * pixels ship the spilled chroma bytes */
            dram_load_u8(hle, (uint8_t*)&Y, pY_1st_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            if (j*4 + 3 < nMovieWidth - 0x80) {
                uint8_t spill;
                dram_load_u8(hle, &spill, pCrPair + 0x80 + j*4 + 3, 1);
                pixel |= spill;
            }
            dram_store_u32(hle, &pixel, pDest_1st_row, 1);
            pDest_1st_row += 4;

            dram_load_u8(hle, (uint8_t*)&Y, pY_1st_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            if (j*4 + 7 < nMovieWidth - 0x80) {
                uint8_t spill;
                dram_load_u8(hle, &spill, pCrPair + 0x80 + j*4 + 7, 1);
                pixel |= spill;
            }
            dram_store_u32(hle, &pixel, pDest_1st_row, 1);
            pDest_1st_row += 4;

            /*2nd row*/
            dram_load_u8(hle, (uint8_t*)&Y, pY_2nd_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            dram_store_u32(hle, &pixel, pDest_2nd_row, 1);
            pDest_2nd_row += 4;

            dram_load_u8(hle, (uint8_t*)&Y, pY_2nd_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            dram_store_u32(hle, &pixel, pDest_2nd_row, 1);
            pDest_2nd_row += 4;
        }

        pLuminance += (nMovieWidth << 1);
        pDestination += nScreenDMAIncrement;
    }

    rsp_break(hle, SP_STATUS_TASKDONE);
}

void fill_video_double_buffer_task(struct hle_t* hle)
{
    video_instruction_timing(hle, HLE_VIDEO_FILL);
    /* The boot entry completes a pre-empted fill without touching pixels. */
    if (*hle->sp_status & SP_STATUS_SIG0) {
        *hle->sp_status &= ~SP_STATUS_SIG0;
        rsp_break(hle, SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
        return;
    }
    int data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    int pSrc = *dram_u32(hle, data_ptr);
    int pDest = *dram_u32(hle, data_ptr + 0x4);
    int width = *dram_u32(hle, data_ptr + 0x8) >> 1;
    int height = *dram_u32(hle, data_ptr + 0x10) << 1;
    int stride = *dram_u32(hle, data_ptr + 0x1c) >> 1;

    if (width > 1024 && width <= 1280 && !(width & 63)) {
        fill_video_window(hle, (uint32_t)data_ptr);
        *hle->sp_status &= ~SP_STATUS_SIG0;
        rsp_break(hle, SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
        return;
    }

    assert((*dram_u32(hle, data_ptr + 0x28) >> 16) == 0x8000);

    int i, j;
    int r, g, b, a;
    uint32_t pixel, pixel1, pixel2;

    for(i = 0; i < height; i++)
    {
      for(j = 0; j < width; j=j+4)
      {
        if (!(j & 28)) hle_timing_add(HLE_WORK_PIXEL, 1);
        pixel1 = *dram_u32(hle, pSrc+j);
        pixel2 = *dram_u32(hle, pDest+j);
      
        r = (((pixel1 >> 24) & 0xff) + ((pixel2 >> 24) & 0xff)) >> 1;
        g = (((pixel1 >> 16) & 0xff) + ((pixel2 >> 16) & 0xff)) >> 1;
        b = (((pixel1 >> 8) & 0xff) + ((pixel2 >> 8) & 0xff)) >> 1;
        /* LUV/SUV process all eight bytes of each two-pixel block.
         * Alpha participates in the average, just like RGB. */
        a = ((pixel1 & 0xff) + (pixel2 & 0xff)) >> 1;
      
        pixel = (r << 24) | (g << 16) | (b << 8) | a;
      
        dram_store_u32(hle, &pixel, pDest+j, 1);
      }
      pSrc += stride;
      pDest += stride;
    }

    /* Native SP_STATUS write 0x5200 clears SIG0 and sets SIG1/SIG2. */
    *hle->sp_status &= ~SP_STATUS_SIG0;
    rsp_break(hle, SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
}
