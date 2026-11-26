#include <cassert>
#include <cstdio>

#include "rsp_op.hpp"

namespace RSP
{
short MFC0_count[32];
int SP_STATUS_TIMEOUT;
}

extern "C" void renderer_process_commands(void) {}

int main()
{
    using namespace RSP;
    CPUState sp;
    uint32_t registers[16] = {}, irq = 0;
    uint32_t dmem[DMEM_WORDS] = {}, imem[IMEM_WORDS] = {}, rdram[2048] = {};
    for (unsigned i = 0; i < 16; ++i) sp.cp0.cr[i] = registers + i;
    sp.cp0.irq = &irq;
    sp.dmem = dmem;
    sp.imem = imem;
    sp.rdram = rdram;
    dmem[0] = 0x12345678;

    const auto write = [&](uint32_t address, uint32_t length_register, unsigned expected, uint32_t cache = 0) {
        registers[CP0_REGISTER_DMA_DRAM] = address;
        registers[CP0_REGISTER_DMA_CACHE] = cache;
        sp.sr[1] = length_register;
        const uint32_t before = sp.cycles;
        assert(RSP_MTC0(&sp, CP0_REGISTER_DMA_WRITE_LENGTH, 1) == MODE_CONTINUE);
        assert(sp.cycles - before == expected);
        assert(rdram[address / 4] == (cache ? imem[0] : dmem[0]));
    };

    write(0x100, 7, 25);
    sp.cycles += 128;
    write(0x108, 7, 24);
    sp.cycles += 129;
    write(0x110, 7, 25);
    write(0x120, 7, 25);
    write(0x128, 255, 32);

    write(0x7e8, 7, 25);
    write(0x7f0, 31, 25); // The current block crosses a row.
    write(0x7f8, 7, 25);
    write(0x800, 7, 25);  // The address starts at a row boundary.
    write(0x808, 0x1007, 50); // A two-block request does not carry setup across requests.
    write(0x818, 7, 25);
    write(0x820, 7, 25, 0x1000); // Fetching from IMEM occupies its instruction port.
    write(0x828, 7, 25);

    // One setup for a strided read, which also ends the write sequence.
    registers[CP0_REGISTER_DMA_DRAM] = 0x100;
    registers[CP0_REGISTER_DMA_CACHE] = 0;
    sp.sr[1] = (8u << 20) | (1u << 12) | 15;
    const uint32_t before = sp.cycles;
    assert(RSP_MTC0(&sp, CP0_REGISTER_DMA_READ_LENGTH, 1) == MODE_CONTINUE);
    assert(sp.cycles - before == 12);
    assert(dmem[0] == rdram[0x100 / 4] && dmem[4] == rdram[0x118 / 4]);
    write(0x830, 7, 25);

    std::puts("RSP DMA: consecutive writes, row boundaries, expiry, direction changes and strided reads passed.");
}
