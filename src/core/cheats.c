#include "cheats.h"

#include "device/device.h"
#include "device/rcp/rdp/fb.h"
#include <ctype.h>
#include <stdlib.h>

struct cheat_code {
    uint32_t address;
    uint16_t value;
};

struct cheat {
    struct cheat *next;
    unsigned index;
    bool enabled, uses_button;
    size_t count;
    struct cheat_code codes[];
};

static struct cheat *cheats;
static bool button_pressed;

static bool repeat_code(uint32_t address) { return (address >> 16) == 0x5000; }
static bool condition_code(unsigned op)
{ return (op >= 0xd0 && op <= 0xd3) || (op >= 0xd8 && op <= 0xdb); }
static unsigned code_bytes(unsigned op)
{ return 1 + ((op & 0xfe) == 0xda ? !(op & 1) : (op & 1)); }
static bool write_code(unsigned op)
{
    switch (op & ~1u) {
    case 0x80: case 0x88: case 0xa0: case 0xa8: case 0xf0: return true;
    default: return false;
    }
}

static bool separator(unsigned char c)
{ return isspace(c) || c == '+' || c == ';' || c == ','; }

static const char *skip_separators(const char *text)
{
    while (separator((unsigned char)*text)) ++text;
    return text;
}

static unsigned read_hex(const char **text, uint32_t *value)
{
    const char *p = skip_separators(*text);
    unsigned digits = 0;
    *value = 0;
    while (isxdigit((unsigned char)*p)) {
        if (++digits > 8) return 0;
        unsigned c = (unsigned char)*p++;
        *value = (*value << 4) | (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    if (*p && !separator((unsigned char)*p)) return 0;
    *text = p;
    return digits;
}

static size_t parse(const char *text, struct cheat_code *out)
{
    if (!text) return 0;
    size_t count = 0;
    bool need_write = false, repeat = false;
    while (*(text = skip_separators(text))) {
        uint32_t address, value;
        if (read_hex(&text, &address) != 8 || !read_hex(&text, &value) || value > UINT16_MAX)
            return 0;
        unsigned op = address >> 24;
        if (write_code(op)) {
            if (code_bytes(op) == 2 && (address & 1)) return 0;
            need_write = repeat = false;
        } else if (!repeat && (condition_code(op) || repeat_code(address))) {
            repeat = repeat_code(address);
            if ((!repeat && code_bytes(op) == 2 && (address & 1)) || (repeat && !(address & 0xff00)))
                return 0;
            need_write = true;
        } else return 0;
        if (out) out[count] = (struct cheat_code){address, (uint16_t)value};
        ++count;
    }
    return need_write ? 0 : count;
}

bool cheats_validate(const char *codes) { return parse(codes, NULL) != 0; }

bool cheats_set(unsigned index, bool enabled, const char *codes)
{
    struct cheat **at = &cheats;
    while (*at && (*at)->index < index) at = &(*at)->next;
    struct cheat *old = *at && (*at)->index == index ? *at : NULL;
    if (!codes) {
        if (old) old->enabled = enabled;
        return old != NULL;
    }

    size_t count = parse(codes, NULL);
    if (!count || count > (SIZE_MAX - sizeof(struct cheat)) / sizeof(struct cheat_code)) return false;
    struct cheat *entry = malloc(sizeof(*entry) + count * sizeof(*entry->codes));
    if (!entry) return false;
    entry->next = old ? old->next : *at;
    entry->index = index;
    entry->enabled = enabled;
    entry->count = count;
    parse(codes, entry->codes);
    entry->uses_button = false;
    for (size_t i = 0; i < count; ++i) entry->uses_button |= (entry->codes[i].address >> 24) & 8;
    *at = entry;
    free(old);
    return true;
}

void cheats_clear(void)
{
    while (cheats) {
        struct cheat *next = cheats->next;
        free(cheats);
        cheats = next;
    }
    button_pressed = false;
}

void cheats_press_button(void) { button_pressed = true; }

bool cheats_use_button(void)
{
    for (const struct cheat *entry = cheats; entry; entry = entry->next)
        if (entry->enabled && entry->uses_button) return true;
    return false;
}

static unsigned value_shift(uint32_t address, unsigned bytes)
{ return (4 - (address & 3) - bytes) * 8; }

static bool read_value(struct device *dev, uint32_t address, unsigned bytes, uint32_t *value)
{
    if ((address & (bytes - 1)) || address + bytes > dev->rdram.dram_size) return false;
    pre_framebuffer_read(&dev->dp.fb, address);
    struct r4300_core *cpu = &dev->r4300;
    uint32_t word = dev->rdram.dram[address >> 2];
    if (cpu->cache_timing) {
        unsigned index = (address >> 4) & 511;
        uint32_t tag = (address & 0x1ffff000) | 1;
        /* Dirty cache data is newer than backing RDRAM. A clean alias can
         * still contain an older DMA snapshot. */
        if (cpu->dcache_tags[index] != (tag | 2)) index ^= 256;
        if (cpu->dcache_tags[index] == (tag | 2))
            word = cpu->dcache_words[index][(address >> 2) & 3];
    }
    *value = (word >> value_shift(address, bytes)) & (bytes == 2 ? 0xffffu : 0xffu);
    return true;
}

static void write_value(struct device *dev, uint32_t address, unsigned bytes, uint16_t value)
{
    if ((address & (bytes - 1)) || address + bytes > dev->rdram.dram_size) return;
    pre_framebuffer_read(&dev->dp.fb, address);
    struct r4300_core *cpu = &dev->r4300;
    unsigned shift = value_shift(address, bytes);
    uint32_t mask = (bytes == 2 ? 0xffffu : 0xffu) << shift;
    uint32_t bits = ((uint32_t)value << shift) & mask;
    uint32_t *ram = &dev->rdram.dram[address >> 2];
    bool changed = (*ram & mask) != bits;
    if (changed) {
        *ram = (*ram & ~mask) | bits;
        post_framebuffer_write(&dev->dp.fb, address, bytes);
    }

    /* Patch resident aliases as well as RAM. Write-back lines can otherwise
     * mask the cheat or undo it on eviction. Preserve unrelated dirty bytes. */
    uint32_t tag = (address & 0x1ffff000) | 1;
    for (unsigned i = (address >> 4) & 255; i < 512; i += 256) {
        if ((cpu->dcache_tags[i] & ~2u) != tag) continue;
        uint32_t *word = &cpu->dcache_words[i][(address >> 2) & 3];
        *word = (*word & ~mask) | bits;
    }
    for (unsigned i = (address >> 5) & 127; i < 512; i += 128) {
        if (cpu->icache_shadow_tags[i] != tag) continue;
        uint32_t *word = &cpu->icache_shadow_words[i][(address >> 2) & 7];
        changed |= (*word & mask) != bits;
        *word = (*word & ~mask) | bits;
    }
    if (cpu->prefetch_valid) {
        uint32_t pc = cpu->prefetch_pc;
        if ((pc & 0xc0000000u) != 0x80000000u) {
            uint32_t page = cpu->cp0.tlb.LUT_r[pc >> 12];
            pc = page ? (page & ~0xfffu) | (pc & 0xfffu) : UINT32_MAX;
        }
        if ((pc & 0x1fffffffu) == (address & ~3u)) {
            changed |= (cpu->prefetch_op & mask) != bits;
            cpu->prefetch_op = (cpu->prefetch_op & ~mask) | bits;
        }
    }
    if (changed) invalidate_r4300_cached_code(cpu, address, bytes);
}

void cheats_apply(struct device *dev, bool boot)
{
    bool button = button_pressed;
    if (!boot) button_pressed = false;
    for (const struct cheat *entry = cheats; entry; entry = entry->next) {
        if (!entry->enabled) continue;
        bool allowed = true;
        for (size_t i = 0; i < entry->count; ++i) {
            struct cheat_code code = entry->codes[i];
            unsigned runs = 1, stride = 0, delta = 0;
            if (repeat_code(code.address)) {
                runs = (code.address >> 8) & 255;
                stride = code.address & 255;
                delta = code.value;
                code = entry->codes[++i];
            }
            unsigned op = code.address >> 24, bytes = code_bytes(op);
            uint32_t address = code.address & 0xffffff;
            if (condition_code(op)) {
                if (!boot && allowed) {
                    uint32_t actual, expected = code.value & (bytes == 2 ? 0xffffu : 0xffu);
                    allowed = (!(op & 8) || button) && read_value(dev, address, bytes, &actual) &&
                        ((actual == expected) == !(op & 2));
                }
                continue;
            }
            if (boot ? (op & ~1u) == 0xf0 : allowed && op < 0xf0 && (!(op & 8) || button)) {
                for (unsigned n = 0; n < runs; ++n)
                    write_value(dev, address + n * stride, bytes, (uint16_t)(code.value + n * delta));
            }
            allowed = true;
        }
    }
}
