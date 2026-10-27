# RSP Xbyak backend

The RSP frontend retains block discovery, IMEM hashes/invalidation, local branch
labels, direct/indirect dispatch, delay slots (including branches in delay slots),
cycle accounting and timeslice exits. Its scalar register cache retains locking,
LRU eviction, dirty writeback, sign/zero extension and caller-save flushing.
`jit_emitter.hpp` replaces GNU Lightning with the vendored Xbyak backends;
`jit_vector.cpp` adds native SIMD and COP2 data/control transfers. GNU Lightning
is no longer built or linked. The migration also fixes `SRAV rd=0` marking zero
dirty and tripping the register cache's writeback assertion.

| Host | State / DMEM / indirect PC | Volatile scalar cache | Preserved scalar cache | Scratch |
| --- | --- | --- | --- | --- |
| x64 Windows | rbx / rdi / rsi | rax, rdx, r8-r10 | r12-r15 | r11, rcx |
| x64 System V | rbx / r13 / r14 | rax, rdx, r8-r10 | r12, r15 | r11, rcx |
| AArch64 | x19 / x20 / x21 | x0-x7 | x22-x28 | x16, x17 |

Entry thunks establish a shared frame and preserve the ABI's nonvolatile integer
registers. Windows also preserves all 128 bits of XMM6-XMM15; NEON uses v0-v7
and v16-v31, leaving v8-v15 untouched. Helper arguments use parallel register
moves with cycle breaking and outgoing stack arguments. Local branches use
Xbyak labels. Cross-block x64 jumps are relocated within a reserved 1 GiB arena;
AArch64 dispatch uses absolute addresses. Code moves from a writable scratch
buffer to allocated pages, flushes the instruction cache and becomes executable.

## Native vector emission

All 64 VU opcodes emit machine code, including reserved-op zeroing, VNOP,
48-bit accumulation, signed/unsigned multiply variants, saturation, clipping,
comparisons, VCO/VCC/VCE, logic, VSAR, VMOV and reciprocal/square-root ROM and
latch behavior. MFC2/MTC2/CFC2/CTC2 emit inline code, including odd-byte transfers,
byte-15 truncation and signed flag extraction. Generated code never calls the
C++ intrinsic vector handlers; they remain independent execution oracles.

The existing `arch/simd/rsp` algorithms compile against symbolic SIMD values to
build SSA programs. Dead values are removed; physical SIMD allocation uses
last-use reuse and farthest-next-use eviction. Stack spill homes are reserved
only when pressure exceeds the available registers. Lowering uses SSE4.2-compatible
instructions (including SSSE3 PSHUFB and SSE4.1 blends) or native NEON. NEON
high-half products use widening multiplication and narrowing; selector shuffles
use TBL. Both SSE2 and SSE4.1 algorithm profiles preserve their reference behavior
for raw flag masks. No compiler-generated intrinsic body is copied or invoked.

Small operations emit inline. Large arithmetic/comparison operations share native
JIT leaves cached by opcode. Callers preload VS and selected VT and pass the VD
address: XMM1/XMM2/rcx on x64, v0/v1/x17 on AArch64. Leaves use the fixed JIT
state register and preserve the scalar register cache, avoiding C++ argument
setup and caller-save writeback. The four divider-normalization operations use
instruction-specialized native leaves and flush volatile scalar registers before
using them as scratch. VMOV and divider-high operations need no scalar flush.

Native leaves occupy compact, 16-byte-aligned slabs, reducing instruction-cache
and TLB pressure. Slabs hold no dispatcher thunks or compiler calls. While the
dispatcher compiles, it can reopen a slab as writable and return it to executable
protection without reopening executing thunks. Protection ranges use the host's
page size. Vector stack temporaries do not overlap branch-delay slots, helper
arguments or the entry thunk's saved SIMD registers.

Vector DMEM load/store routines retain their scalar C++ loops. CP0 scheduling
and I/O helpers also retain their existing paths.

## Automated validation

```sh
cmake -S src/device/rcp/rsp/p_rsp -B build-rsp \
  -DCMAKE_BUILD_TYPE=Release -DPARALLEL_RSP_DEBUG_JIT=OFF
cmake --build build-rsp --parallel
ctest --test-dir build-rsp -E rsp-vu-fuzz --output-on-failure
```

The 57 assembly images normally require the MIPS GNU tools in
`debug-toolchain/Makefile.mips`. Existing images can be used with
`PARALLEL_RSP_BUILD_TEST_IMAGES=OFF` and `PARALLEL_RSP_TEST_IMAGE_DIR`.

All 59 routine tests passed under both default/SSE2 and SSE4.2 build profiles.
The 57 original tests also matched the saved GNU Lightning runner exactly.
The scalar test checks 12,700 randomized instructions, allocation/spills,
eight-argument ABI cycles, helper calls, timeslices, region delay slots and
IMEM invalidation/cache reuse. Native vector differential coverage per profile:

- 786,432 VU cases: every opcode/selector, eight alias patterns, all divider
  destination lanes, edge values and canonical/raw flags.
- 147,456 COP2 transfers: all rd/selector combinations and three scalar-register
  classes, including aliases and byte boundaries.
- 1,024 full-block cases with 31 dirty scalar registers across vector execution.

The complete SSE4.2 reference VU fuzzer exited successfully: 83,554,432 lines,
3,980,316,940 bytes, SHA-256
`9784d1d192d119592bca40985b6115ae01e6306f449e14bc0a8ce8455bde5502`.
This fuzzer exercises the reference algorithms; the differential tests exercise
the emitted code. Its approximately 4 GB output is best redirected or hashed.

AArch64 machine-code execution under Unicorn passed all 57 assembly tests,
65,536 standalone VU cases and 98,304 transfers. A further 16,384 cases exercised
the final shared-leaf argument convention using the native ARM algorithm profile.
Checks cover callee-saved GPR/SIMD registers, stack restoration and absence of
C++ handler calls. Native AArch64 hardware performance is unmeasured.

## ROM comparison against GNU Lightning

Only the five requested games were tested. DKR, Conker and Indiana Jones came
from `C:\codecrack\personalcode\wtweg\wtftest`; DK64 and Top Gear Rally used
local copies of the originally requested `Z:\ROMS\N64` files.

Each backend ran three sequential 1,800-frame boot/attract sequences with no
controller input, accurate timing, native RSP, cached R4300 interpreter and
Angrylion with two renderer threads. Processes used the same first eight
available logical CPUs, alternating backend order between repeats. No compilation
or other validation ran during timing. Every 60 frames the harness compared RAM,
cumulative audio/video hashes and twelve timing fields. All 15 paired runs
matched exactly and exited normally.

Positive percentages mean slower. These are whole-core elapsed times, including
rendering and initial compilation, rather than isolated RSP throughput:

| Game | Lightning median s | Native vector median s | Ratio of medians change | Median paired change |
| --- | ---: | ---: | ---: | ---: |
| Diddy Kong Racing | 11.793 | 11.498 | -2.50% | -2.50% |
| Conker's Bad Fur Day | 19.972 | 19.861 | -0.56% | +2.17% |
| Indiana Jones | 20.339 | 19.759 | -2.85% | -3.57% |
| Donkey Kong 64 | 14.590 | 14.533 | -0.39% | -0.83% |
| Top Gear Rally | 17.234 | 17.047 | -1.08% | -1.08% |

The paired statistic is the median of each repeat's native/Lightning ratio.
Wall times vary: Conker's paired median shows a 2.17% slowdown even though its
ratio of medians is nearly flat. These runs do not establish a universal speedup
or rule out regressions in extended interactive gameplay. An earlier fully
inlined implementation showed a consistent Conker slowdown around 4%; shared
native routines reduced that cost in this measurement.

The normal, non-benchmark DLL was rebuilt and passed 120-frame smoke checks for
all five games, matching baseline RAM/audio/video hashes. Its symbols contain no
GNU Lightning implementation functions. The comparison DLLs, harnesses, emulator
bridge, input images, logs and JSON results remain in ignored
`benchmarks/rsp-xbyak/`, particularly `vector-final-rom-results.json`,
`vector-lightning-test-comparison.log`, `vector-leaf-ctest.log`,
`neon-leaf-tests.log` and `arm64-leaf-tests.log`.
