#include "rdp_emit_simd.h"
#include "hle_timing.h"
/* SGI ZSortp screen-space object walker. Grammar reference:
 * https://github.com/gonetz/GLideN64/blob/master/src/uCodes/ZSort.cpp
 * Uses the shared integer frontend/RDP encoder, not the OpenGL renderer.
 * Tasks are validated without guest writes or output before HLE dispatch.
 *
 * See benchmarks/hle-calibration/FOUR_FAMILY_COMPLETION.md for validation.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zsort_dsp.h"
#include "rdp_emit_zsort.h"
#include "rdp_emit_rsp.h"
extern void rdp_fifo_append(RdpFifo *, const int32_t *, int);
extern void rdp_fifo_fullsync_note(void);
#ifdef MSB_FIRST
#define ZBYTE 0u
#else
#define ZBYTE 3u
#endif

#define ZS_STATE_ONLY 0
#define ZS_SHADE_TRI  1
#define ZS_TEX_TRI    2
#define ZS_SHADE_QUAD 3
#define ZS_TEX_QUAD   4

typedef struct ZSortWrite
{
    struct ZSortWrite *previous;
    unsigned address, bytes;
    unsigned char old[1];
} ZSortWrite;

typedef struct ZSortState
{
    unsigned char *rdram, *dmem;
    unsigned rdram_size;
    unsigned segments[16];
    unsigned othermode_h, othermode_l;
    unsigned command_budget;
    int trace;
    ZSortWrite *writes;
    void *dsp;
    int32_t matrix[3][4][4];
    GSPState *gsp;
    RdpFifo *fifo;
    unsigned *status, *interrupt;
    unsigned pc, wait, yielded, window_start, window_pos, packet_bytes, flush_link;
} ZSortState;

/* Pointer-free continuation. DMEM includes the two native return stacks and
 * the cached command window, so CPU edits affect the next DMA, not commands
 * that the RSP has already fetched. */
static struct {
    unsigned active, pc, wait, window_start, window_pos, packet_bytes;
    unsigned char dmem[4096], dsp[ZSORT_DSP_SNAPSHOT_BYTES];
} zs_stream;
static unsigned zs_vco;

int zsort_stream_active(void) { return zs_stream.active != 0; }
unsigned zsort_result_vco(void) { return zs_vco; }
unsigned zsort_state_size(void) { return sizeof(zs_stream); }
void zsort_state_save(void *out) { memcpy(out, &zs_stream, sizeof(zs_stream)); }
void zsort_state_load(const void *in)
{
    if (in) memcpy(&zs_stream, in, sizeof(zs_stream));
    else memset(&zs_stream, 0, sizeof(zs_stream));
}
/* ---- Guest memory and task validation ---- */

static unsigned zs_get32(const unsigned char *p, unsigned a)
{
    return (unsigned)p[a ^ ZBYTE] << 24 | (unsigned)p[(a + 1) ^ ZBYTE] << 16 |
           (unsigned)p[(a + 2) ^ ZBYTE] << 8 | p[(a + 3) ^ ZBYTE];
}

static int zs_get16(const unsigned char *p, unsigned a)
{
    return (int16_t)((unsigned)p[a ^ ZBYTE] << 8 | p[(a + 1) ^ ZBYTE]);
}

static void zs_put16(unsigned char *p, unsigned a, int v)
{
    p[a ^ ZBYTE] = (unsigned char)((unsigned)v >> 8);
    p[(a + 1) ^ ZBYTE] = (unsigned char)v;
}

static void zs_put32(unsigned char *p, unsigned a, unsigned v)
{
    zs_put16(p, a, v >> 16);
    zs_put16(p, a + 2, v);
}

static int zs_range(unsigned a, unsigned n, unsigned size)
{
    return a <= size && n <= size - a;
}

static int zs_ram_range(const ZSortState *z, unsigned a, unsigned n)
{
    return zs_range(a, n, z->rdram_size);
}

/* Validate against the writes the task actually observes, then undo them in
 * reverse order. A save followed by a load may intentionally overlap. */
static int zs_record_write(ZSortState *z, unsigned address, unsigned bytes)
{
    ZSortWrite *w;
    if (z->fifo) return 1;
    w = (ZSortWrite *)malloc(sizeof(*w) + bytes);
    if (!w) return 0;
    w->previous = z->writes;
    w->address = address;
    w->bytes = bytes;
    memcpy(w->old, z->rdram + address, bytes);
    z->writes = w;
    return 1;
}

static void zs_undo_writes(ZSortState *z)
{
    while (z->writes) {
        ZSortWrite *w = z->writes;
        memcpy(z->rdram + w->address, w->old, w->bytes);
        z->writes = w->previous;
        free(w);
    }
}

static int zs_dma(ZSortState *z, unsigned address, unsigned memory,
                  unsigned bytes, int save)
{
    unsigned i;
    /* The translated profile models DMEM transfers. Selecting the IMEM
     * bank changes or inspects program storage and needs native admission. */
    if (memory & 0x1000u) return 0;
    address &= 0x7ffff8u;
    memory &= 0xff8u;
    bytes = (bytes + 7u) & ~7u;
    if (!zs_ram_range(z, address, bytes)) return 0;
    if (save && !zs_record_write(z, address, bytes)) return 0;
    for (i = 0; i < bytes; ++i)
        if (save) z->rdram[(address + i) ^ ZBYTE] = z->dmem[((memory + i) & 4095u) ^ ZBYTE];
        else z->dmem[((memory + i) & 4095u) ^ ZBYTE] = z->rdram[(address + i) ^ ZBYTE];
    return 1;
}

static unsigned zs_phys(const ZSortState *z, unsigned a)
{
    return ((a & 0xffffffu) + z->segments[(a >> 24) & 15]) & 0xffffffu;
}

static unsigned zs_address(const ZSortState *z, unsigned a)
{
    return a + z->segments[(a >> 24) & 15u];
}

static int zs_step(ZSortState *z)
{
    return z->command_budget && --z->command_budget;
}

static int zs_reject(const char *kind, unsigned a, unsigned value)
{
    static unsigned count;
    if (getenv("HLE_ZSORT_TRACE") && count++ < 12)
        fprintf(stderr, "ZSortp rejected %s at %06x: %08x\n", kind, a, value);
    return 0;
}

static int zs_dmfield(unsigned field, unsigned len, unsigned *out)
{
    if (field < 0x400 || !zs_range(field - 0x400, len, 4096))
        return 0;
    *out = field - 0x400;
    return 1;
}

int zsort_ucode_match(const unsigned char *ram, unsigned size, unsigned data,
                      unsigned bytes)
{
    static const char prefix[] = "RSP Gfx ucode ";
    unsigned i, j;
    if (!ram || !data || data >= size)
        return 0;
    if (!bytes || bytes > 0x1000)
        bytes = 0x1000;
    if (bytes > size - data)
        return 0;
    for (i = 0; i + sizeof(prefix) - 1 + 6 <= bytes; ++i)
    {
        for (j = 0; j < sizeof(prefix) - 1; ++j)
            if (ram[(data + i + j) ^ ZBYTE] != (unsigned char)prefix[j])
                break;
        if (j != sizeof(prefix) - 1)
            continue;
        /* The first signature names this task, not a bundled overlay. */
        for (j = 0; j < 6; ++j)
            if (ram[(data + i + sizeof(prefix) - 1 + j) ^ ZBYTE] !=
                (unsigned char)"ZSortp"[j])
                return 0;
        return 1;
    }
    return 0;
}

int zsort_yield_supported(const unsigned char *ram, unsigned size,
                          const unsigned char *dm)
{
    unsigned text = zs_get32(dm, 0xfd0u) & 0xffffffu;
    unsigned data = zs_get32(dm, 0xfd8u) & 0xffffffu;
    if ((text & 3u) || (data & 7u) || zs_get32(dm, 0xfdcu) != 0xc00u ||
        !zs_range(text, 0x10a4u, size) || !zs_range(data, 0xc00u, size) ||
        !zsort_dsp_supported((const uint32_t*)(ram + text)) ||
        ram[(data + 0x210u) ^ ZBYTE] > 1u ||
        zs_get32(ram, data + 0xbfcu) != zs_get32(dm, 0xfd0u)) return 0;
    return zs_range(zs_get32(ram, data + 0x218u) & 0x7ffff8u, 160u, size);
}
/* ---- Embedded RDP lists and screen-space objects ---- */

static void zs_append(ZSortState *z, const int32_t *w, int n)
{
    if (z->dsp) {
        unsigned i;
        for (i = 0; i < (unsigned)n; ++i)
            zs_put32(z->dmem, 0xd20u + z->packet_bytes + i * 4u, (unsigned)w[i]);
        z->packet_bytes += (unsigned)n * 4u;
    }
    else if (z->fifo)
        rdp_fifo_append(z->fifo, w, n);
}

static int zs_flush(ZSortState *z, int force)
{
    unsigned address, bytes = z->packet_bytes, i;
    int32_t words[(4096u - 0xd20u) / 4u];
    if (!bytes || (!force && bytes <= 0x150u)) return 1;
    if (bytes > 4096u - 0xd20u) return 0;
    zs_put32(z->dmem, 0x224u, z->flush_link);
    address = zs_get32(z->dmem, 0xf4u) & 0xffffffu;
    if (address + bytes > zs_get32(z->dmem, 0xfecu))
        address = zs_get32(z->dmem, 0xfe8u) & 0xffffffu;
    if (!zs_dma(z, address, 0xd20u, bytes, 1)) return 0;
    zs_put32(z->dmem, 0xf4u, address + bytes);
    z->packet_bytes = 0;
    if (z->fifo) {
        for (i = 0; i < bytes / 4u; ++i) words[i] = (int32_t)zs_get32(z->dmem, 0xd20u + i * 4u);
        z->fifo->base = address;
        rdp_fifo_append(z->fifo, words, (int)(bytes / 4u));
        if (z->fifo->flush) z->fifo->flush(z->fifo);
    }
    return 1;
}

static int zs_dsp_dma(void *context, unsigned ram, unsigned dm, unsigned bytes, int save)
{
    ZSortState *z = (ZSortState*)context;
    return zs_step(z) && zs_dma(z, ram, dm, bytes, save);
}

static unsigned zs_dsp_flush(void *context, unsigned cursor, int force, unsigned link)
{
    ZSortState *z = (ZSortState*)context;
    if (cursor < 0xd20u || cursor > 4096u || !zs_step(z)) return 0;
    z->packet_bytes = cursor - 0xd20u;
    z->flush_link = link;
    return zs_flush(z, force) ? 0xd20u + z->packet_bytes : 0;
}

static unsigned zs_dsp_status(void *context)
{
    return *((ZSortState*)context)->status;
}

static int zs_rdp(ZSortState *z, unsigned op, unsigned w0, unsigned w1)
{
    int32_t w[2];
    if (op == 0xe2 || op == 0xe3)
    {
        unsigned shift = (w0 >> 8) & 255, len = w0 & 255, mask;
        unsigned *mode = op == 0xe3 ? &z->othermode_h : &z->othermode_l;
        if (shift >= 32 || !len || len > 32 - shift)
            return 0;
        mask = len == 32 ? ~0u : ((1u << len) - 1) << shift;
        *mode = (*mode & ~mask) | w1;
        w0 = 0xef000000u | (z->othermode_h & 0xffffff);
        w1 = z->othermode_l;
    }
    else if (op == 0xef)
    {
        z->othermode_h = w0 & 0xffffff;
        z->othermode_l = w1;
    }
    else if (op == 0xfd || op == 0xfe || op == 0xff)
        w1 = (w1 & 0xffffffu) + z->segments[(w1 >> 24) & 15u];
    if (op == 0xe9 || op == 0x29)
    {
        if (z->fifo)
            rdp_fifo_fullsync_append(z->fifo, w0, w1);
        return 1;
    }
    w[0] = (int32_t)w0;
    w[1] = (int32_t)w1;
    zs_append(z, w, 2);
    return 1;
}

static int zs_rdplist(ZSortState *z, unsigned pointer)
{
    unsigned a = zs_phys(z, pointer);
    if (!a)
        return 1;
    while (zs_step(z))
    {
        unsigned w0, w1, op, n = 2, i;
        int32_t w[44];
        if ((a & 3) || !zs_ram_range(z, a, 8))
            return 0;
        w0 = zs_get32(z->rdram, a);
        w1 = zs_get32(z->rdram, a + 4);
        op = w0 >> 24;
        hle_timing_graphics_family("gfx_zsort");
        hle_timing_command(op);
        a += 8;
        if (op == 0xdf) {
            if (z->fifo && z->fifo->drain) z->fifo->drain();
            return 1;
        }
        if (op == 0)
            continue; /* Embedded SPNoOp/padding pairs. */
        if (op == 0xe4 || op == 0xe5)
        {
            if (!zs_ram_range(z, a, 16))
                return 0;
            w[0] = w0;
            w[1] = w1;
            w[2] = zs_get32(z->rdram, a + 4);
            w[3] = zs_get32(z->rdram, a + 12);
            a += 16;
            zs_append(z, w, 4);
            continue;
        }
        if ((op & 63) >= 8 && (op & 63) <= 15)
        {
            static const unsigned char lengths[8] = {8,  12, 24, 28,
                                                     24, 28, 40, 44};
            n = lengths[op & 7];
            if (!zs_ram_range(z, a, (n - 2) * 4))
                return 0;
            w[0] = w0;
            w[1] = w1;
            for (i = 2; i < n; ++i, a += 4)
                w[i] = zs_get32(z->rdram, a);
            zs_append(z, w, n);
            continue;
        }
        if (op < 0xe2 || !zs_rdp(z, op, w0, w1))
            return zs_reject("RDP", a - 8, w0);
    }
    return 0;
}

static int zs_object(ZSortState *z, unsigned header, unsigned *cache,
                     unsigned *next)
{
    unsigned type = header & 7, a = header & 0x00fffff8u, nv, stride, prefix, i;
    RspTriVtx v[4];
    int32_t cmd[44];
    if (type > ZS_TEX_QUAD)
        return zs_reject("object type", a, type);
    prefix = (type == ZS_SHADE_TRI || type == ZS_SHADE_QUAD) ? 8 : 16;
    nv = type == ZS_STATE_ONLY ? 0 : type >= ZS_SHADE_QUAD ? 4 : 3;
    stride = (type == ZS_SHADE_TRI || type == ZS_SHADE_QUAD) ? 8 : 16;
    if (!zs_ram_range(z, a, prefix + nv * stride))
        return zs_reject("object bounds", a, type);
    *next = zs_phys(z, zs_get32(z->rdram, a));
    for (i = 0; i < prefix / 4 - 1; ++i)
    {
        unsigned p = zs_get32(z->rdram, a + 4 + i * 4);
        /* Unlike state/textured objects, the shaded handler has no null
         * guard. A null transition would read an RDP list at address zero. */
        if (stride == 8 && !p && cache[i])
            return 0;
        if (p && p != cache[i])
        {
            if (!zs_rdplist(z, p))
                return 0;
            cache[i] = p;
        }
    }
    if (!z->fifo || !nv)
        return 1;
    memset(v, 0, sizeof(v));
    for (i = 0; i < nv; ++i)
    {
        unsigned p = a + prefix + i * stride;
        v[i].x = zs_get16(z->rdram, p);
        v[i].y = zs_get16(z->rdram, p + 2);
        v[i].r = z->rdram[(p + 4) ^ ZBYTE];
        v[i].g = z->rdram[(p + 5) ^ ZBYTE];
        v[i].b = z->rdram[(p + 6) ^ ZBYTE];
        v[i].a = z->rdram[(p + 7) ^ ZBYTE];
        if (stride == 16)
        {
            v[i].s = zs_get16(z->rdram, p + 8);
            v[i].t = zs_get16(z->rdram, p + 10);
            v[i].invw = (int32_t)zs_get32(z->rdram, p + 12);
            v[i].pw = v[i].invw ? INT32_MAX / v[i].invw : INT32_MAX;
        }
    }
    rsp_tri_set_zboss_attr(1);
    /* ZSortp premultiplies S/T/W even when RDP perspective is disabled. */
    rsp_set_affine_tex(0);
    for (i = 0; i < nv - 2; ++i)
    {
        const RspTriVtx *v0, *v1, *v2;
        int n;
        /* The shaded network compares B/C first, then A (or D on the
         * second quad triangle). The textured network compares A/B first
         * and replaces A with D for the second triangle. Equal Y values
         * make these input permutations observable in the edge words. */
        if (stride == 8)
        {
            v0 = &v[1];
            v1 = &v[2];
            v2 = &v[i ? 3 : 0];
        }
        else
        {
            v0 = &v[i ? 3 : 0];
            v1 = &v[1];
            v2 = &v[2];
        }
        n = rsp_tri_write(cmd, v0, v1, v2, stride == 16, 0, 1, 1, 0, 0, 0x4000,
                          8, -1, 0x7fff);
        if (n > 0)
        {
            /* Both native writers explicitly store v0 into the shade-base
             * fraction pair (0x5ec / 0x914), rather than a half-unit bias. */
            cmd[12] = 0;
            cmd[13] = 0;
            zs_append(z, cmd, n);
        }
    }
    rsp_tri_set_zboss_attr(0);
    rsp_set_affine_tex(0);
    return 1;
}
/* ---- Matrix, viewport and scratch-memory commands ---- */

static int zs_movemem(ZSortState *z, unsigned w0, unsigned w1)
{
    unsigned idx = w0 & 0x3e, ofs = ((w0 >> 6) & 511) * 8,
             len = (1 + ((w0 >> 15) & 511)) * 8;
    unsigned a = zs_phys(z, w1), i, j;
    if (z->dsp)
    {
        ofs += (unsigned)(uint16_t)zs_get16(z->dmem, 0x258u + idx);
        if (!zs_dma(z, a, ofs, len, (w0 & 1u) != 0)) return 0;
        if (!(w0 & 1u)) {
            z->othermode_h = zs_get32(z->dmem, 0xc8u) & 0xffffffu;
            z->othermode_l = zs_get32(z->dmem, 0xccu);
            if (idx == 12u && !(w0 & 0x7fc0u)) {
                gsp_set_viewport(z->gsp, z->rdram, a & ~7u);
                z->gsp->fog_m = zs_get16(z->dmem, 0xe6u);
                z->gsp->fog_o = zs_get16(z->dmem, 0xeeu);
            }
        }
        return 1;
    }
    if (idx == 0 || idx == 2)
    {
        /* DMEM base table at 0x258: slot 0 = 0x400, slot 2 = 0x440. */
        ofs += idx == 2 ? 0x40u : 0u;
        if (!zs_range(ofs, len, 4096) || !zs_range(a, len, z->rdram_size))
            return 0;
        if (!(w0 & 1) && !zs_ram_range(z, a, len))
            return 0;
        if ((w0 & 1) && !zs_record_write(z, a, len)) return 0;
        /* Copy by logical byte lane; unaligned lengths need not be a word. */
        for (i = 0; i < len; ++i)
            if (!(w0 & 1))
                z->dmem[(ofs + i) ^ ZBYTE] = z->rdram[(a + i) ^ ZBYTE];
            else
                z->rdram[(a + i) ^ ZBYTE] = z->dmem[(ofs + i) ^ ZBYTE];
        return 1;
    }
    if (idx == 4 || idx == 6 || idx == 8)
    {
        if ((w0 & 1) || ofs || len != 64)
            return 0;
        if (!zs_ram_range(z, a, 64))
            return 0;
        for (i = 0; i < 4; ++i)
            for (j = 0; j < 4; ++j)
            {
                unsigned p = (i * 4 + j) * 2;
                z->matrix[(idx - 4) / 2][i][j] =
                    (int32_t)((unsigned)zs_get16(z->rdram, a + p) << 16 |
                              (unsigned)(uint16_t)zs_get16(z->rdram,
                                                           a + 32 + p));
            }
        return 1;
    }
    if (idx == 12)
    {
        if ((w0 & 1) || ofs || len != 16)
            return 0;
        if (!zs_ram_range(z, a, 16))
            return 0;
        gsp_set_viewport(z->gsp, z->rdram, a);
        z->gsp->fog_m = zs_get16(z->rdram, a + 6);
        z->gsp->fog_o = zs_get16(z->rdram, a + 14);
        return 1;
    }
    return 0;
}

static int zs_transform(ZSortState *z, unsigned w1)
{
    unsigned n = 1 + (w1 >> 24), src, dst, i, k;
    if (!zs_dmfield((w1 >> 12) & 4095, n * 6, &src) ||
        !zs_dmfield(w1 & 4095, n * 16, &dst))
        return 0;
    for (i = 0; i < n; ++i, src += 6, dst += 16)
    {
        int x = zs_get16(z->dmem, src), y = zs_get16(z->dmem, src + 2),
            zz = zs_get16(z->dmem, src + 4);
        int64_t p[4];
        int sx, sy, fog, cc = 0;
        hle_transform4(p,&z->matrix[2][0][0],x,y,zz);
        sx = z->gsp->viewport.vtrans_x / 16384;
        sy = z->gsp->viewport.vtrans_y / 16384;
        if (p[3])
        {
            sx += (int)(p[0] * (z->gsp->viewport.vscale_x / 16384) / p[3]);
            sy += (int)(p[1] * (z->gsp->viewport.vscale_y / 16384) / p[3]);
        }
        zs_put16(z->dmem, dst, sx);
        zs_put16(z->dmem, dst + 2, sy);
        {
            int64_t w = p[3] * 31 / 65536;
            zs_put32(z->dmem, dst + 4,
                     w ? (unsigned)(INT32_MAX / w) : INT32_MAX);
        }
        zs_put16(z->dmem, dst + 8, (int)(p[0] / 65536));
        zs_put16(z->dmem, dst + 10, (int)(p[1] / 65536));
        fog = p[3] > 0 ? (int)(p[2] * z->gsp->fog_m / p[3]) + z->gsp->fog_o : 0;
        if (fog < 0)
            fog = 0;
        if (fog > 255)
            fog = 255;
        if (p[0] < -p[3])
            cc |= 16;
        if (p[0] > p[3])
            cc |= 1;
        if (p[1] < -p[3])
            cc |= 32;
        if (p[1] > p[3])
            cc |= 2;
        if (p[3] < 6554)
            cc |= 4;
        z->dmem[(dst + 12) ^ ZBYTE] = cc;
        z->dmem[(dst + 13) ^ ZBYTE] = fog;
        zs_put16(z->dmem, dst + 14, (int)(p[3] / 65536));
    }
    return 1;
}
/* ---- Display-list dispatch ---- */

static void zs_status_write(ZSortState *z, unsigned value)
{
    unsigned bit;
    for (bit = 0; bit < 8; ++bit) {
        unsigned pair = (value >> (9u + bit * 2u)) & 3u;
        if (pair == 1u) *z->status &= ~(0x80u << bit);
        if (pair == 2u) *z->status |= 0x80u << bit;
    }
    if ((value & 3u) == 1u) *z->status &= ~1u;
    if ((value & 3u) == 2u) *z->status |= 1u;
    if (value & 4u) *z->status &= ~2u;
    if ((value & 0x60u) == 0x20u) *z->status &= ~0x20u;
    if ((value & 0x60u) == 0x40u) *z->status |= 0x20u;
    if ((value & 0x180u) == 0x80u) *z->status &= ~0x40u;
    if ((value & 0x180u) == 0x100u) *z->status |= 0x40u;
    if (z->interrupt) {
        if ((value & 0x18u) == 8u) *z->interrupt &= ~1u;
        if ((value & 0x18u) == 16u) *z->interrupt |= 1u;
    }
}

static void zs_sync_dmem(ZSortState *z)
{
    unsigned i;
    for (i = 0; i < 16u; ++i) z->segments[i] = zs_get32(z->dmem, 0xf8u + i * 4u);
    z->othermode_h = zs_get32(z->dmem, 0xc8u) & 0xffffffu;
    z->othermode_l = zs_get32(z->dmem, 0xccu);
}

static void zs_branch(ZSortState *z, unsigned pc)
{
    z->pc = pc;
    z->window_pos = 160u;
}

/* Native main-thread stack uses a byte offset, not a host nesting limit. */
static int zs_control(ZSortState *z, unsigned w0, unsigned w1)
{
    unsigned op = w0 >> 24, i;
    if (op == 0xdbu) {
        unsigned at = ((unsigned)(uint16_t)zs_get16(z->dmem, 0x260u + (w0 & 255u)) +
                       (w0 >> 8)) & 4095u;
        for (i = 0; i < 4u; ++i)
            z->dmem[((at + i) & 4095u) ^ ZBYTE] = (unsigned char)(w1 >> (24u - i * 8u));
        zs_sync_dmem(z);
    }
    else if (op == 0xdeu) {
        if (!(w0 & 0x10000u)) {
            unsigned depth = z->dmem[0xf0u ^ ZBYTE];
            zs_put32(z->dmem, 0x138u + depth, z->pc);
            z->dmem[0xf0u ^ ZBYTE] = (unsigned char)(depth + 4u);
        }
        zs_branch(z, zs_address(z, w1));
    }
    else if (op == 0xdfu) {
        unsigned depth = z->dmem[0xf0u ^ ZBYTE];
        if (depth == z->dmem[0x246u ^ ZBYTE]) return 1;
        depth = (depth - 4u) & 255u;
        z->dmem[0xf0u ^ ZBYTE] = (unsigned char)depth;
        zs_branch(z, zs_get32(z->dmem, 0x138u + depth));
    }
    else if (op == 0xd8u) {
        zs_put16(z->dmem, 0x244u, 0);
        zs_put32(z->dmem, 0x23cu, zs_address(z, w1));
    }
    else if (op == 0xd7u) {
        int remaining = zs_get16(z->dmem, 0x244u);
        if (zs_get32(z->dmem, 0x23cu)) {
            unsigned depth = z->dmem[0xf0u ^ ZBYTE];
            unsigned subdepth = z->dmem[0xf1u ^ ZBYTE];
            unsigned base = z->dmem[0x247u ^ ZBYTE];
            unsigned ret = z->pc;
            zs_branch(z, zs_get32(z->dmem, 0x23cu) + remaining);
            z->dmem[0xf1u ^ ZBYTE] = (unsigned char)base;
            for (;;) {
                depth += 4u;
                zs_put32(z->dmem, 0x134u + depth, ret);
                if (subdepth == base) break;
                ret = zs_get32(z->dmem, 0x138u + base);
                base += 4u;
                if (base > 255u) return -1;
            }
            z->dmem[0xf0u ^ ZBYTE] = (unsigned char)depth;
            zs_put32(z->dmem, 0x23cu, 0);
        }
    }
    else if (op == 0xd9u) {
        if (*z->status & 0x800u) {
            *z->status &= ~0x800u;
            if (!zs_dma(z, w0, 0x1c8u, 8u, 0)) return -1;
        }
        if (zs_get32(z->dmem, 0x1c8u) < w1) {
            z->pc -= 8u;
            z->window_pos -= 8u;
            return 3;
        }
    }
    else if (op == 0xdau) {
        zs_status_write(z, w1);
        zs_put32(z->dmem, 0xdcu, w1);
        if ((w0 & 0x400u) && (*z->status & 0x400u)) {
            z->wait = 1u;
            return 3;
        }
    }
    else if (op == 0xddu) {
        /* f84/f88 reload the following cached pair. It is the destination
         * program's load descriptor, and remains its first command. */
        unsigned descriptor = zs_get32(z->dmem, 0xc80u + z->window_pos);
        unsigned code = zs_get32(z->dmem, 0xc84u + z->window_pos);
        unsigned text = code & 0x7fffffu;
        unsigned data = zs_get32(z->dmem, 0xdcu);
        unsigned bytes = (descriptor & 65535u) + 1u;
        uint16_t vectors[32][8];
        unsigned vco;
        if ((text & 3u) || !zs_range(text, 0x10a4u, z->rdram_size) ||
            !zsort_dsp_supported((const uint32_t*)(z->rdram + text))) return -1;
        z->flush_link = 0xf84u;
        if (!zs_flush(z, 1)) return -1;
        zs_put32(z->dmem, 0x10cu, data);
        zs_put32(z->dmem, 0xfd0u, code);
        zs_put32(z->dmem, 0xff0u, z->pc);
        if (!zs_dma(z, data, 0, bytes, 0)) return -1;
        zsort_dsp_vectors(z->dsp, vectors);
        vco = zsort_dsp_vco(z->dsp);
        zsort_dsp_init(z->dsp, z->dmem, vectors, vco);
        zs_sync_dmem(z);
        zs_branch(z, z->pc);
        if (!(zs_get32(z->dmem, 0xfc4u) & 8u))
            zs_put32(z->dmem, 0xf4u, zs_get32(z->dmem, 0xfecu));
        zs_status_write(z, 0x42800u);
    }
    else if (op == 0xe0u || op == 0xe1u) zs_put32(z->dmem, 0xdcu, w1);
    else return -1;
    return 0;
}

static int zs_walk(ZSortState *z, unsigned start)
{
    unsigned pc[10], depth = 0;
    pc[0] = start;
    while (zs_step(z))
    {
        unsigned w0, w1, op, a = pc[depth];
        if (z->dsp) {
            if (z->wait) {
                if (*z->status & 0x400u) return 3;
                z->wait = 0;
            }
            a = z->pc & 0x7fffffu;
            if (*z->status & 0x80u) {
                z->yielded = 1u;
                return 2;
            }
            if (z->window_pos >= 160u) {
                if (!zs_dma(z, a, 0xc80u, 160u, 0)) return 0;
                z->window_start = a;
                z->window_pos = 0;
            }
        }
        if ((a & 3) || !zs_ram_range(z, a, 8))
            return 0;
        w0 = z->dsp ? zs_get32(z->dmem, 0xc80u + z->window_pos) : zs_get32(z->rdram, a);
        w1 = z->dsp ? zs_get32(z->dmem, 0xc84u + z->window_pos) : zs_get32(z->rdram, a + 4);
        op = w0 >> 24;
        hle_timing_graphics_family("gfx_zsort");
        hle_timing_command(op);
        pc[depth] += 8;
        if (z->dsp) { z->pc += 8u; z->window_pos += 8u; }
        if (z->trace)
        {
            static unsigned char seen[256];
            if (!seen[op])
            {
                seen[op] = 1;
                fprintf(stderr, "ZSortp op %02x: %08x %08x at %06x\n", op, w0,
                        w1, a);
            }
        }
        if (z->dsp && ((op >= 0xd7u && op <= 0xdbu) || op == 0xddu ||
                       op == 0xdeu || op == 0xdfu || op == 0xe0u || op == 0xe1u))
        {
            int control = zs_control(z, w0, w1);
            if (control < 0) return 0;
            if (control) return control;
        }
        else if (op == 0xdf)
        {
            if (!depth)
                return 1;
            --depth;
        }
        else if (op == 0xde)
        {
            if (!((w0 >> 16) & 255))
            {
                if (depth == 9)
                    return 0;
                ++depth;
            }
            pc[depth] = zs_phys(z, w1);
        }
        else if (z->dsp && (op == 0x80u || op == 0x81u))
        {
            ZSortDspIO io;
            unsigned entry = (unsigned)(uint16_t)zs_get16(z->dmem,
                0x2dcu + (unsigned)((int)op - 256) * 2u);
            int r;
            io.user = z; io.dma = zs_dsp_dma; io.flush = zs_dsp_flush; io.status = zs_dsp_status;
            r = zsort_dsp_graphics(z->dsp, w0, w1, entry, 0xd20u + z->packet_bytes, &io);
            if (r != 1) return r;
            zs_sync_dmem(z);
        }
        else if (op == 0x80)
        {
            unsigned cache[3] = {0}, h = zs_phys(z, w0), chain;
            for (chain = 0; chain < 2; ++chain)
            {
                while (h)
                {
                    if (!zs_step(z) || !zs_object(z, h, cache, &h))
                        return 0;
                }
                h = zs_phys(z, w1);
            }
        }
        else if (op == 0x81)
        {
            if (!zs_rdplist(z, w1))
                return 0;
        }
        else if (op == 0xdc)
        {
            if (!zs_movemem(z, w0, w1))
                return 0;
            if (z->dsp) zs_sync_dmem(z);
        }
        else if (op == 0xdb)
        {
            unsigned idx = w0 & 255, off = (w0 >> 8) & 65535;
            if (idx == 6)
            {
                if (off & 3 || off >= 64)
                    return 0;
                z->segments[off / 4] = w1;
            }
            else if (idx == 8)
            {
                z->gsp->fog_m = (int16_t)(w1 >> 16);
                z->gsp->fog_o = (int16_t)w1;
            }
            else if (idx == 0x0e)
                z->gsp->persp_norm = w1 & 65535;
            else
                return 0;
        }
        else if (z->dsp && op >= 0xd0u && op <= 0xd6u)
        {
            /* The resident table and the small first IMEM bank select these
             * shared math kernels. Keep the native data tables and vector
             * residue across commands, including interpolation aliases. */
            unsigned entry = (unsigned)(uint16_t)zs_get16(z->dmem, 0x27cu + (op - 0xd0u) * 2u);
            if (!zsort_dsp_run(z->dsp, w0, w1, entry)) return 0;
        }
        else if (op == 0xd6)
        {
            if (!zs_transform(z, w1))
                return 0;
        }
        else if (op == 0xd5)
        {
            unsigned s = w0 & 15, t = (w1 >> 16) & 15, d = w1 & 15;
            int32_t m[4][4];
            if (s < 4 || s > 8 || s & 1 || d < 4 || d > 8 || d & 1)
                return 0;
            if (t < 4 || t > 8 || t & 1)
                return 0;
            rdp_simd_matrix_wrap4(&m[0][0],&z->matrix[(s-4)/2][0][0],&z->matrix[(t-4)/2][0][0]);
            memcpy(z->matrix[(d - 4) / 2], m, sizeof(m));
        }
        /* The main and embedded-RDP tables have different biases (DMEM
         * 0x2dc versus 0x2e2). Raw RDP commands belong in the 0x81 list;
         * do not interpret unknown main opcodes as F3D geometry commands. */
        else
        {
            if (z->trace)
                zs_reject("opcode", a, op);
            return 0;
        }
    }
    return z->dsp ? 3 : 0;
}
/* ---- Walker entry points ---- */

static int zs_run(GSPState *g, RdpFifo *f, unsigned char *ram, unsigned size,
                  unsigned char *dm, unsigned dl, int resume,
                  unsigned *status, unsigned *interrupt)
{
    ZSortState z;
    unsigned i, j;
    int result, resume_object = 0;
    if (!g || !ram || !dm || !size || (size & 3))
        return 0;
    memset(&z, 0, sizeof(z));
    z.rdram = ram;
    z.rdram_size = size;
    z.dmem = dm;
    z.gsp = g;
    z.fifo = f;
    z.command_budget = 200000;
    z.trace = getenv("HLE_ZSORT_TRACE") != NULL;
    z.status = status;
    z.interrupt = interrupt;
    z.pc = dl;
    z.window_pos = 160u;
    {
        unsigned text = zs_get32(dm, 0xfd0u) & 0xffffffu;
        unsigned data = zs_get32(dm, 0xfd8u) & 0xffffffu;
        unsigned bytes = (zs_get32(dm, 0xfdcu) + 7u) & ~7u;
        if (!(text & 3u) && zs_range(text, 0x10a4u, size) &&
            zsort_dsp_supported((const uint32_t*)(ram + text)) &&
            bytes <= 0x1000u && zs_range(data, bytes, size)) {
            uint16_t vectors[32][8];
            if (resume && zs_stream.active) {
                memcpy(dm, zs_stream.dmem, 4096u);
                z.pc = zs_stream.pc;
                z.wait = zs_stream.wait;
                z.window_start = zs_stream.window_start;
                z.window_pos = zs_stream.window_pos;
                z.packet_bytes = zs_stream.packet_bytes;
            }
            else {
                z.pc = zs_get32(dm, 0xff0u);
                for (i = 0; i < bytes; ++i) dm[i ^ ZBYTE] = ram[(data + i) ^ ZBYTE];
                if (zs_get32(dm, 0xfc4u) & 1u) {
                    z.pc = zs_get32(dm, 0x218u);
                    resume_object = dm[0x210u ^ ZBYTE] != 0;
                    zs_put32(dm, 0xfc4u, 0);
                }
                else if (!(zs_get32(dm, 0xfc4u) & 8u))
                    zs_put32(dm, 0xf4u, zs_get32(dm, 0xfecu));
            }
            z.dsp = malloc(zsort_dsp_size());
            if (!z.dsp) return 0;
            if (resume && zs_stream.active)
                zsort_dsp_restore(z.dsp, dm, zs_stream.dsp);
            else {
                rsp_factor5_get(vectors);
                zsort_dsp_init(z.dsp, dm, vectors, rsp_graphics_entry_vco());
            }
            zs_sync_dmem(&z);
        }
    }
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 4; ++j)
            z.matrix[i][j][j] = 65536;
    result = 1;
    if (resume_object && z.dsp) {
        ZSortDspIO io;
        io.user = &z; io.dma = zs_dsp_dma; io.flush = zs_dsp_flush; io.status = zs_dsp_status;
        result = zsort_dsp_resume_graphics(z.dsp, zs_get32(dm, 0x214u),
                                            0xd20u + z.packet_bytes, &io);
    }
    if (result == 1) result = zs_walk(&z, dl);
    z.flush_link = result == 2 ? 0xef4u : 0xf18u;
    if (z.dsp && (result == 1 || result == 2) && !zs_flush(&z, 1)) result = 0;
    if (z.dsp && result == 2) {
        dm[0x210u ^ ZBYTE] = 0;
        zs_put32(dm, 0x218u, z.pc);
        zs_put32(dm, 0xbfcu, zs_get32(dm, 0xfd0u));
        if (!zs_dma(&z, zs_get32(dm, 0xff8u), 0, 0xc00u, 1)) result = 0;
    }
    if (z.dsp) {
        if (f && result) {
            uint16_t vectors[32][8];
            zsort_dsp_vectors(z.dsp, vectors);
            for (i = 0; i < 32u; ++i) rsp_factor5_row(i, vectors[i]);
            zs_vco = zsort_dsp_vco(z.dsp);
        }
        if (f) {
            zs_stream.active = result == 3;
            if (zs_stream.active) {
                zs_stream.pc = z.pc;
                zs_stream.wait = z.wait;
                zs_stream.window_start = z.window_start;
                zs_stream.window_pos = z.window_pos;
                zs_stream.packet_bytes = z.packet_bytes;
                memcpy(zs_stream.dmem, dm, 4096u);
                zsort_dsp_save(z.dsp, zs_stream.dsp);
            }
        }
        free(z.dsp);
    }
    zs_undo_writes(&z);
    return result;
}

int zsort_validate(unsigned char *ram, unsigned size, const unsigned char *dm,
                   unsigned dl)
{
    GSPState g;
    unsigned status = 0x800u;
    unsigned char scratch[4096];
    if (!ram || !dm)
        return 0;
    memset(&g, 0, sizeof(g));
    gsp_init(&g);
    memcpy(scratch, dm, sizeof(scratch));
    return zs_run(&g, NULL, ram, size, scratch, dl, 0, &status, NULL) != 0;
}

int zsort_run_dl(GSPState *g, RdpFifo *f, unsigned char *ram, unsigned size,
                 unsigned char *dm, unsigned dl)
{
    unsigned status = 0x800u;
    return zs_run(g, f, ram, size, dm, dl, 0, &status, NULL) == 1;
}

int zsort_run_stream(GSPState *g, RdpFifo *f, unsigned char *ram, unsigned size,
                     unsigned char *dm, unsigned dl, int resume,
                     unsigned *status, unsigned *interrupt)
{
    int result = zs_run(g, f, ram, size, dm, dl, resume, status, interrupt);
    if (!result) return -1;
    if (result == 1) return 0;
    return result == 2 ? 2 : 1;
}

