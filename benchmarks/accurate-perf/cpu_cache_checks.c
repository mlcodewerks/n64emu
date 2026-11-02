#define main archived_cpu_timing_main
#include "../cpu_timing.c"
#undef main
#include "device/r4300/fastmem.h"
#include "device/r4300/jit/n64emujit.h"
static void verify(int ok, const char *what, unsigned seed, unsigned mode) {
  if (!ok) {
    fprintf(stderr, "accurate JIT mismatch %s seed=%u mode=%u\n", what, seed,
            mode);
    exit(1);
  }
}
static int precise_exit(struct r4300_core *r, void *block, unsigned index,
                        unsigned phase) {
  (void)r;
  (void)block;
  (void)index;
  (void)phase;
  fputs("unexpected native side exit\n", stderr);
  exit(2);
}
struct snapshot {
  int64_t regs[32], hi, lo;
  uint32_t cp0[32], phase, pc, store, cc;
  int64_t clock, cycle;
  uint64_t load, fpu, fill_end, wb[4], retired;
  uint32_t fill, wb_count, tags[512], dtags[512], words[512][4], shadows[512],
      generations[32];
  cp1_reg fregs[32];
};
static struct snapshot take(void) {
  struct snapshot s = {0};
  memcpy(s.regs, cpu.regs, sizeof(s.regs));
  s.hi = cpu.hi;
  s.lo = cpu.lo;
  memcpy(s.cp0, cpu.cp0.regs, sizeof(s.cp0));
  s.phase = cpu.cp0.count_phase;
  s.pc = cpu.instruction_pc.addr;
  s.clock = cpu.cp0.count_clock;
  s.cycle = cpu.cp0.cycle_count;
  s.load = cpu.pipeline_load_mask;
  s.fpu = cpu.pipeline_fpu_result_mask;
  s.store = cpu.pipeline_cached_store;
  s.cc = cpu.pipeline_fpu_cc;
  s.fill = cpu.icache_fill_index;
  s.fill_end = cpu.icache_fill_end;
  s.wb_count = cpu.write_buffer_count;
  memcpy(s.wb, cpu.write_buffer_end, sizeof(s.wb));
  memcpy(s.tags, cpu.icache_tags, sizeof(s.tags));
  s.retired = cpu.retired_ops;
  memcpy(s.dtags, cpu.dcache_tags, sizeof(s.dtags));
  memcpy(s.words, cpu.dcache_words, sizeof(s.words));
  memcpy(s.fregs, cpu.cp1.regs, sizeof(s.fregs));
  memcpy(s.shadows, cpu.icache_shadow_tags, sizeof(s.shadows));
  if (cpu.code_page_generation)
    memcpy(s.generations, cpu.code_page_generation, sizeof(s.generations));
  return s;
}
static const uint32_t ops[] = {0x24210001, 0x00011040, 0x00411826, 0x24040005,
                               0x00222021, 0x34451234, 0x000330c3, 0};
static void initial(unsigned seed, unsigned mode) {
  setup(0);
  memcpy(code, ops, sizeof(ops));
  cpu.cache_timing = 1;
  cpu.compat_issue_clock = 0;
  cpu.cp0.regs[CP0_CONFIG_REG] = 3;
  cpu.cp0.regs[CP0_STATUS_REG] = 0;
  cpu.cp0.count_clock = 1000;
  cpu.cp0.count_phase = seed & 1;
  cpu.cp0.cycle_count = -100000;
  cpu.instruction_pc.addr = mode ? 0xa0000000 : 0x80000000;
  for (unsigned i = 0; i < 512; i++)
    cpu.icache_tags[i] = 1;
  cpu.regs[1] = (int32_t)(seed * 0x91e10da5u);
  cpu.pipeline_load_mask = seed % 3 == 0   ? 1
                           : seed % 3 == 1 ? UINT64_C(1) << 34
                                           : (UINT64_C(1) << 35) | 2;
  cpu.pipeline_fpu_result_mask = UINT64_C(1) << seed % 32;
  cpu.pipeline_fpu_cc = seed & 1;
  cpu.pipeline_cached_store = seed & 1;
  uint64_t now = 2000 + (seed & 1);
  cpu.icache_fill_index = seed % 4;
  cpu.icache_fill_end = now + seed % 97;
  cpu.write_buffer_count = seed % 5;
  for (unsigned i = 0; i < 4; i++)
    cpu.write_buffer_end[i] = now + i * 20 + seed % 31;
}
static unsigned memory_checks(struct n64emujit *jit);
static void memory_benchmark(struct n64emujit *jit);
static unsigned branch_checks(struct n64emujit *jit);
static unsigned ownership_checks(struct n64emujit *jit);
int main(void) {
#ifdef _WIN32
  _putenv("N64EMU_CPU_HOST_FAULTS=1");
#else
  setenv("N64EMU_CPU_HOST_FAULTS", "1", 1);
#endif
  struct n64emujit *jit = n64emujit_create();
  verify(jit != NULL, "allocation", 0, 0);
  if (getenv("N64EMU_CPU_BENCH")) {
    memory_benchmark(jit);
    n64emujit_destroy(jit);
    return 0;
  }
  unsigned cases = 0;
  for (unsigned mode = 0; mode < 2; mode++) {
    struct n64emujit_config c = {0};
    c.virtual_pc = mode ? 0xa0000000 : 0x80000000;
    c.physical_page = 0;
    c.cache_timing = 1;
    c.commits = &cpu.retired_ops;
    n64emujit_entry fn =
        n64emujit_compile_optimized(jit, NULL, ops, 8, precise_exit, &c);
    verify(fn != NULL, "compile", 0, mode);
    for (unsigned seed = 0; seed < 256; seed++) {
      initial(seed, mode);
      for (unsigned i = 0; i < 8; i++)
        r4300_ops_execute_one(&cpu);
      struct snapshot expected = take();
      initial(seed, mode);
      fn(&cpu);
      struct snapshot actual = take();
      if (memcmp(&expected, &actual, sizeof(actual))) {
        verify(!memcmp(expected.regs, actual.regs, sizeof(actual.regs)), "GPRs",
               seed, mode);
        verify(expected.pc == actual.pc, "PC", seed, mode);
        verify(expected.phase == actual.phase &&
                   expected.clock == actual.clock &&
                   expected.cycle == actual.cycle,
               "clocks", seed, mode);
        verify(expected.fill == actual.fill &&
                   expected.fill_end == actual.fill_end,
               "I-cache fill", seed, mode);
        verify(expected.wb_count == actual.wb_count, "write buffer", seed,
               mode);
        verify(expected.load == actual.load && expected.fpu == actual.fpu &&
                   expected.cc == actual.cc && expected.store == actual.store,
               "pipeline", seed, mode);
        verify(0, "state", seed, mode);
      }
      cases++;
    }
  }
  cases += memory_checks(jit);
  cases += branch_checks(jit);
  cases += ownership_checks(jit);
  n64emujit_destroy(jit);
  r4300_jit_destroy(&cpu);
  printf("Accurate x64 JIT: %u pipeline/cache/write-buffer differential cases "
         "passed.\n",
         cases);
  return 0;
}

/* Compare every load/store width and both FPR layouts, with stale backing
 * memory. The backing handlers abort if called: resident hits must not consult
 * them. Randomized dirty bits and preceding load/store history exercise
 * the full data and pipeline state, not just the loaded result. */
static uint32_t memory_ops[16];
static uint64_t tracked_pages[2048];
static struct r4300_fastmem fastmem;
static uint32_t backing[2 * 1024 * 1024];
static void init_fastmem(void) {
  fastmem.ram = (uintptr_t)backing;
  fastmem.physical_read[16] = (uintptr_t)(backing + 0x10000 / 4);
  fastmem.physical_write[16] = fastmem.physical_read[16];
  fastmem.read[0x80010] = &fastmem.physical_read[16];
  fastmem.write[0x80010] = &fastmem.physical_write[16];
}
static void memory_initial(unsigned seed, unsigned layout) {
  initial(seed, 0);
  memcpy(code, memory_ops, sizeof(memory_ops));
  cpu.cp0.regs[CP0_STATUS_REG] = CP0_STATUS_CU1 | (layout ? CP0_STATUS_FR : 0);
  set_fpr_pointers(&cpu.cp1, cpu.cp0.regs[CP0_STATUS_REG]);
  static struct rdram ram;
  ram.dram = backing;
  ram.dram_size = sizeof(backing);
  memory.handlers[1].write32 = write_rdram_dram;
  memory.handlers[1].read32 = read_rdram_dram;
  memory.handlers[1].opaque = &ram;
  cpu.cached_code_pages = tracked_pages;
  for (unsigned i = 0; i < 32; i++) {
    cpu.regs[i] = i * 31 + seed;
    cpu.cp1.regs[i].dword = UINT64_C(0x891ecb259e3779b9) * (i + seed + 1);
  }
  cpu.regs[0] = 0;
  cpu.regs[1] = (int64_t)(int32_t)(0x80010000 + ((seed * 16) & 0x1ff0));
  for (unsigned i = 0; i < 512; i++) {
    cpu.dcache_tags[i] = 0x10001 | ((i ^ seed) & 1 ? 2 : 0);
    for (unsigned j = 0; j < 4; j++)
      cpu.dcache_words[i][j] =
          (seed + i * 0x9e3779b9u + j * 0x891ecb25u) ^ 0xa5a5a5a5u;
  }
}
static unsigned memory_checks(struct n64emujit *jit) {
  const unsigned majors[] = {0x20, 0x24, 0x21, 0x25, 0x23, 0x27, 0x37, 0x31,
                             0x35, 0x28, 0x29, 0x2b, 0x3f, 0x39, 0x3d};
  init_fastmem();
  unsigned cases = 0;
  for (unsigned fast = 0; fast < 2; fast++)
    for (unsigned layout = 0; layout < 2; layout++)
      for (unsigned kind = 0; kind < sizeof(majors) / sizeof(majors[0]);
           kind++) {
        for (unsigned i = 0; i < 16; i++)
          memory_ops[i] =
              (majors[kind] << 26) | (1 << 21) | ((2 + i) << 16) | (i * 8);
        struct n64emujit_config c = {0};
        c.virtual_pc = 0x80000000;
        c.cache_timing = 1;
        c.commits = &cpu.retired_ops;
        c.fastmem = fast ? &fastmem : NULL;
        n64emujit_entry fn = n64emujit_compile_optimized(jit, NULL, memory_ops,
                                                         16, precise_exit, &c);
        verify(fn != NULL, "memory compile", 0, kind);
        for (unsigned seed = 0; seed < 128; seed++) {
          memory_initial(seed, layout);
          for (unsigned i = 0; i < 16; i++)
            r4300_ops_execute_one(&cpu);
          struct snapshot expected = take();
          memory_initial(seed, layout);
          fn(&cpu);
          struct snapshot actual = take();
          if (memcmp(&expected, &actual, sizeof(actual))) {
            const unsigned char *x = (void *)&expected, *y = (void *)&actual;
            for (unsigned k = 0; k < sizeof(actual); k++)
              if (x[k] != y[k])
                fprintf(stderr, "byte %u expected %02x actual %02x\n", k, x[k],
                        y[k]);
            fprintf(stderr, "fregs at %u, load at %u words at %u\n",
                    (unsigned)offsetof(struct snapshot, fregs),
                    (unsigned)offsetof(struct snapshot, load),
                    (unsigned)offsetof(struct snapshot, words));
            verify(0, "cached load state/timing", seed, kind + layout * 15);
          }
          cases++;
        }
      }
  return cases;
}

static void memory_benchmark(struct n64emujit *jit) {
  init_fastmem();
  unsigned major = strstr(getenv("N64EMU_CPU_BENCH"), "store") ? 0x2bu : 0x23u;
  for (unsigned i = 0; i < 16; i++)
    memory_ops[i] = (major << 26) | (1 << 21) | ((2 + i) << 16) | (i * 8);
  struct n64emujit_config c = {0};
  c.virtual_pc = 0x80000000;
  c.cache_timing = 1;
  c.commits = &cpu.retired_ops;
  c.fastmem = strstr(getenv("N64EMU_CPU_BENCH"), "fastmem") ? &fastmem : NULL;
  n64emujit_entry fn =
      n64emujit_compile_optimized(jit, NULL, memory_ops, 16, precise_exit, &c);
  memory_initial(0, 1);
  const unsigned repeats = 4000000;
  clock_t start = clock();
  for (unsigned i = 0; i < repeats; i++) {
    cpu.cp0.cycle_count = -1000000;
    fn(&cpu);
  }
  double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
  printf("Cached Accurate JIT: %.6f seconds / %u accesses; COUNT=%lld phase=%u "
         "result=%llx\n",
         seconds, repeats * 16, (long long)cpu.cp0.count_clock,
         cpu.cp0.count_phase, (unsigned long long)cpu.regs[17]);
}

static uint32_t page_generations[32];
static uint32_t branch_op;
static void branch_initial(unsigned seed, unsigned target, unsigned config) {
  initial(seed, 0);
  code[0] = branch_op;
  code[1] = 0x24630001;
  cpu.cp0.regs[CP0_CONFIG_REG] = config;
  cpu.regs[1] = seed & 1;
  cpu.regs[2] = (int64_t)(int32_t)target;
  cpu.regs[3] = seed * 41;
  unsigned index = (target >> 5) & 511;
  if (seed & 2)
    cpu.icache_tags[index] = 0;
  if (seed & 4)
    cpu.icache_shadow_tags[index] = 0x10001;
  cpu.code_page_generation = page_generations;
  for (unsigned i = 0; i < 32; i++)
    page_generations[i] = seed + i;
}
static unsigned branch_checks(struct n64emujit *jit) {
  unsigned cases = 0;
  for (unsigned kind = 0; kind < 4; kind++)
    for (unsigned seed = 0; seed < 128; seed++) {
      unsigned target = 0x80000040 + (seed % 100) * 32;
      branch_op =
          kind == 0   ? (4u << 26) | (1 << 21) | ((target - 0x80000004) / 4)
          : kind == 1 ? (5u << 26) | (1 << 21) | ((target - 0x80000004) / 4)
          : kind == 2 ? (3u << 26) | ((target >> 2) & 0x3ffffff)
                      : (2 << 21) | 8;
      struct n64emujit_config c = {0};
      c.virtual_pc = 0x80000000;
      c.cache_timing = 1;
      c.commits = &cpu.retired_ops;
      c.has_delay = 1;
      c.delay_op = 0x24630001;
      n64emujit_entry fn = n64emujit_compile_optimized(jit, NULL, &branch_op, 1,
                                                       precise_exit, &c);
      verify(fn != NULL, "branch compile", seed, kind);
      unsigned config = (seed & 8) ? 2 : 3;
      branch_initial(seed, target, config);
      r4300_ops_execute_one(&cpu);
      struct snapshot expected = take();
      branch_initial(seed, target, config);
      fn(&cpu);
      struct snapshot actual = take();
      verify(!memcmp(&expected, &actual, sizeof(actual)),
             "branch prefetch state/timing", seed, kind);
      cases++;
    }
  return cases;
}

/* Protected GPU backing must not be touched by a resident read. Conversely,
 * tracked-code writes must leave through the precise invalidation boundary
 * before changing cache data, dirty bits, pipeline history or guest clocks. */
static struct snapshot before_side_exit;
static unsigned side_exits;
static int tracked_store_exit(struct r4300_core *r, void *block, unsigned index,
                              unsigned phase) {
  (void)block;
  verify(index == 0 && phase == 2, "tracked store exit kind", index, phase);
  struct snapshot current = take();
  verify(!memcmp(&before_side_exit, &current, sizeof(current)),
         "store guard ordering", 0, 0);
  ++side_exits;
  r4300_ops_execute_one(r);
  return 1;
}
static unsigned ownership_checks(struct n64emujit *jit) {
  unsigned cases = 0;
  init_fastmem();
  for (unsigned store = 0; store < 2; store++) {
    memory_ops[0] = ((store ? 0x2bu : 0x23u) << 26) | (1 << 21) | (2 << 16);
    struct n64emujit_config c = {0};
    c.virtual_pc = 0x80000000;
    c.cache_timing = 1;
    c.commits = &cpu.retired_ops;
    c.fastmem = store ? NULL : &fastmem;
    n64emujit_entry fn = n64emujit_compile_optimized(
        jit, NULL, memory_ops, 1, store ? tracked_store_exit : precise_exit,
        &c);
    verify(fn != NULL, "ownership compile", 0, store);
    for (unsigned seed = 0; seed < 32; seed++) {
      tracked_pages[0] = store ? UINT64_C(1) << 16 : 0;
      memory_initial(seed, 1);
      r4300_ops_execute_one(&cpu);
      struct snapshot expected = take();
      memory_initial(seed, 1);
      before_side_exit = take();
      side_exits = 0;
      fastmem.physical_read[16] = (uintptr_t)n64emujit_fault_guard(jit);
      verify(fastmem.physical_read[16] != 0, "host guard allocation", seed,
             store);
      uint64_t faults = n64emujit_fault_count(jit);
      void *previous = n64emujit_fault_enter(jit);
      fn(&cpu);
      n64emujit_fault_leave(previous);
      struct snapshot actual = take();
      verify(!memcmp(&expected, &actual, sizeof(actual)),
             "ownership state/timing", seed, store);
      verify(side_exits == store, "tracked store precise exit", seed, store);
      verify(n64emujit_fault_count(jit) == faults,
             "resident read probed GPU backing", seed, store);
      ++cases;
    }
  }
  tracked_pages[0] = 0;
  init_fastmem();
  return cases;
}
