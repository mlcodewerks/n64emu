#include "rsp_interpreter.hpp"
#include "rsp_jit.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
using namespace RSP;
static void check(bool condition, unsigned trial, const char *what) {
  if (!condition) {
    fprintf(stderr, "Mixed RSP stream %u: %s\n", trial, what);
    exit(1);
  }
}
int main() {
  JIT::CPU cpu;
  Interpreter reference;
  std::mt19937 random(0x5253504d);
  std::array<uint32_t, IMEM_WORDS> imem{};
  std::array<uint32_t, DMEM_WORDS> actual_mem{}, expected_mem{};
  std::array<uint32_t, 16> actual_cr{}, expected_cr{};
  uint32_t actual_irq = 0, expected_irq = 0;
  cpu.set_imem(imem.data());
  cpu.set_dmem(actual_mem.data());
  reference.set_imem(imem.data());
  reference.set_dmem(expected_mem.data());
  auto &a = cpu.get_state();
  auto &b = reference.get_state();
  if (std::getenv("N64EMU_RSP_BENCH")) {
    a = CPUState{};
    a.imem = imem.data();
    a.dmem = actual_mem.data();
    a.cp0.irq = &actual_irq;
    for (unsigned i = 0; i < 16; i++)
      a.cp0.cr[i] = &actual_cr[i];
    for (unsigned i = 0; i < 32; i++)
      for (unsigned j = 0; j < 8; j++)
        a.cp2.regs[i].e[j] = i * 911 + j * 193;
    unsigned n = 1;
    for (unsigned i = 0; i < 64; i++) {
      unsigned op = i % 3 == 0 ? 0 : i % 3 == 1 ? 8 : 16;
      imem[n++] = (0x25u << 25) | ((i % 16) << 21) | ((i % 8) << 16) |
                  (((i + 1) % 8) << 11) | (((i + 2) % 8) << 6) | op;
      imem[n++] = (9u << 26) | ((1 + i % 8) << 21) | ((1 + i % 8) << 16) | 7;
    }
    imem[n] = 13;
    cpu.invalidate_imem();
    a.pc = 0;
    cpu.run();
    auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 500000; i++) {
      a.pc = 0;
      a.cycles = 0;
      a.has_delay_slot = 0;
      actual_cr[4] = 0;
      cpu.run();
    }
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
            .count();
    uint64_t hash = 1469598103934665603ULL;
    const auto bytes = reinterpret_cast<const unsigned char *>(&a.cp2);
    for (unsigned i = 0; i < sizeof(a.cp2); i++) {
      hash ^= bytes[i];
      hash *= 1099511628211ULL;
    }
    printf("Mixed RSP: %.6f seconds / 32000000 vectors; cycles=%u scalar=%u "
           "vector_hash=%016llx\n",
           seconds, a.cycles, a.sr[1], (unsigned long long)hash);
    return 0;
  }
  const unsigned vu[] = {0,  1,  4,  5,  6,  7,  8,  9,  12, 13, 14, 15,
                         16, 17, 19, 20, 21, 29, 32, 33, 34, 35, 36, 37,
                         38, 39, 40, 41, 42, 43, 44, 45, 55, 63};
  const unsigned su[] = {0x21, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2b};
  for (unsigned trial = 0; trial < 2048; trial++) {
    a = CPUState{};
    b = CPUState{};
    a.imem = b.imem = imem.data();
    a.dmem = actual_mem.data();
    b.dmem = expected_mem.data();
    a.cp0.irq = &actual_irq;
    b.cp0.irq = &expected_irq;
    actual_cr.fill(0);
    expected_cr.fill(0);
    for (unsigned i = 0; i < 16; i++) {
      a.cp0.cr[i] = &actual_cr[i];
      b.cp0.cr[i] = &expected_cr[i];
    }
    for (unsigned i = 1; i < 32; i++)
      a.sr[i] = b.sr[i] = random();
    auto bytes = reinterpret_cast<unsigned char *>(&a.cp2);
    for (unsigned i = 0; i < sizeof(a.cp2); i++)
      bytes[i] = random();
    b.cp2 = a.cp2;
    for (unsigned i = 0; i < DMEM_WORDS; i++)
      actual_mem[i] = expected_mem[i] = random();
    imem.fill(0);
    unsigned n = 1;
    for (unsigned i = 0; i < 9; i++) {
      unsigned vd = random() % 8, vs = random() % 8, vt = random() % 8,
               e = random() % 16,
               op = vu[random() % (sizeof(vu) / sizeof(vu[0]))];
      imem[n++] =
          (0x25u << 25) | (e << 21) | (vt << 16) | (vs << 11) | (vd << 6) | op;
      for (unsigned j = 0; j < 1 + trial % 3; j++) {
        unsigned rs = random() % 32, rt = random() % 32, rd = random() % 32,
                 kind = random() % 3;
        imem[n++] =
            kind == 0
                ? (9u << 26) | (rs << 21) | (rt << 16) | (random() & 65535)
            : kind == 1
                ? (rs << 21) | (rt << 16) | (rd << 11) | su[random() % 8]
                : (rt << 16) | (rd << 11) | ((random() % 32) << 6) |
                      (random() % 2 ? 0 : 2);
      }
      if (i == 3 && trial % 2) {
        imem[n++] = (0x12u << 26) | (4u << 21) | ((random() % 32) << 16) |
                    ((random() % 8) << 11) | ((random() % 16) << 7);
      }
      if (i == 6 && trial % 2) {
        imem[n++] = (0x12u << 26) | ((random() % 32) << 16) |
                    ((random() % 8) << 11) | ((random() % 16) << 7);
      }
    }
    imem[n] = 13;
    cpu.invalidate_imem();
    reference.invalidate_imem();
    check(cpu.run() == MODE_BREAK, trial, "native completion");
    check(reference.run() == MODE_BREAK, trial, "reference completion");
    check(!std::memcmp(a.sr, b.sr, sizeof(a.sr)), trial, "scalar state");
    check(!std::memcmp(&a.cp2, &b.cp2, sizeof(a.cp2)), trial,
          "vector/accumulator/control state");
    check(actual_mem == expected_mem, trial, "DMEM");
    check(a.cycles == b.cycles, trial, "cycle count");
    check(a.pc == b.pc, trial, "PC");
  }
  puts("2048 mixed scalar/vector RSP streams match interpreter state and exact "
       "cycles.");
}
