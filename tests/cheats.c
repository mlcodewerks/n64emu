#include "core/cheats.h"
#include "device/device.h"
#include <assert.h>
#include <stdio.h>

static struct device dev;
static uint32_t ram[1024];
static unsigned reads, writes, invalidations;

void pre_framebuffer_read(struct fb *fb, uint32_t address)
{ assert(fb == &dev.dp.fb && address < sizeof(ram)); ++reads; }
void post_framebuffer_write(struct fb *fb, uint32_t address, uint32_t bytes)
{ assert(fb == &dev.dp.fb && address + bytes <= sizeof(ram)); ++writes; }
void invalidate_r4300_cached_code(struct r4300_core *cpu, uint32_t address, size_t bytes)
{ assert(cpu == &dev.r4300 && address + bytes <= sizeof(ram)); ++invalidations; }

int main(void)
{
    dev.rdram.dram = ram;
    dev.rdram.dram_size = sizeof(ram);
    assert(!cheats_validate(NULL));
    assert(!cheats_validate("80000000 XXXX"));
    assert(!cheats_validate("81000003 0001"));
    assert(!cheats_validate("D0000000 0001"));
    assert(!cheats_validate("50000001 0000;80000000 0001"));
    assert(!cheats_validate("50000301 0000;D0000000 0001;80000000 0000"));
    assert(!cheats_validate("80000000 0001;C0000000 0001"));
    assert(cheats_set(3, true, "F0000001 005a;80000000+0012,81000002 3456\n"
        "D0000000 0012 D1000002 3456 50000301 0001 80000004 0020;"
        "D2000000 0012 80000007 00fe;88000008 00ab;DA000002 0000 8900000a deed"));
    assert(!cheats_set(3, true, "invalid replacement"));
    assert(cheats_use_button());
    cheats_apply(&dev, true);
    assert(ram[0] == 0x005a0000 && !ram[1] && !ram[2]);
    cheats_apply(&dev, false);
    assert(ram[0] == 0x125a3456 && ram[1] == 0x20212200 && !ram[2]);
    cheats_press_button();
    cheats_apply(&dev, false);
    assert(ram[2] == 0xab00deed);
    ram[2] = 0;
    cheats_apply(&dev, false);
    assert(!ram[2]);
    assert(cheats_set(3, false, NULL) && !cheats_use_button());
    ram[0] = 0;
    cheats_apply(&dev, false);
    assert(!ram[0]);

    cheats_clear();
    dev.r4300.cache_timing = 1;
    dev.r4300.dcache_tags[0] = 1;
    dev.r4300.dcache_tags[256] = 3;
    dev.r4300.dcache_words[256][0] = 0xff102030;
    dev.r4300.icache_shadow_tags[0] = dev.r4300.icache_shadow_tags[128] = 1;
    dev.r4300.icache_shadow_words[0][0] = 0x10000000;
    dev.r4300.icache_shadow_words[128][0] = 0x20000000;
    dev.r4300.prefetch_valid = 1;
    dev.r4300.prefetch_pc = 0x80000000;
    dev.r4300.prefetch_op = 0x30000000;
    assert(cheats_set(5, true, "D0000000 00ff;A1000002 5678;807fffff 00ff"));
    assert(cheats_set(1, true, "80000005 0001"));
    assert(cheats_set(2, true, "80000005 0002"));
    cheats_apply(&dev, false);
    assert(ram[0] == 0x00005678 && (ram[1] & 0x00ff0000) == 0x00020000);
    assert(dev.r4300.dcache_words[0][0] == 0x00005678);
    assert(dev.r4300.dcache_words[256][0] == 0xff105678);
    assert(dev.r4300.icache_shadow_words[0][0] == 0x10005678);
    assert(dev.r4300.icache_shadow_words[128][0] == 0x20005678);
    assert(dev.r4300.prefetch_op == 0x30005678);
    assert(reads > writes && writes && invalidations == writes);
    cheats_clear();
    puts("GameShark parsing, boot, conditions, repeaters, toggles, cache aliases and RAM bounds passed.");
}
