# Component tests

From the repository root, with GNU Make, GCC/G++, Python 3 and the project's
dependencies available:

```sh
make test -j6
```

On Windows use the MSYS2 MinGW64 toolchain. In PowerShell, for the installation
used here:

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:PATH
bash -c 'make test -j6 CC=gcc CXX=g++'
```

No game ROMs, disk images, frontend, display, audio device or network access are
needed. Assertions remain enabled even when release compiler flags define
`NDEBUG`. The runner executes an explicit manifest, reports every failure,
limits each executable to 600 seconds and retains per-test output and
`results.json` in `tests/.build/`. Build failures stop before execution.
Tests run serially by default; `TEST_JOBS=4` enables concurrent independent
processes. These are correctness checks, not host performance measurements.
The larger FPU suites can take several minutes each. Serial execution avoids
competition between their page-protection and memory-intensive fixtures.
The test-only root target keeps the core's generated build configuration intact.
The generated AI fixture extracts the production AI implementation on each
source change; it does not maintain a second FIFO implementation.
The suite also verifies that the standalone RDP tests use the production timing
model, allowing only the standalone license annotation to differ.

For just the new device/CPU checks:

```sh
make -f tests/unit.mk check-new -j6
```

| Component | Checks included in `make test` |
| --- | --- |
| CPU integer operations | Independent result vectors in Accurate and Off; shifts, arithmetic, branches/annulled delay slots, links, multiply/divide, zero register, overflow, alignment and privilege exceptions |
| CP0 and interrupts | Queue ordering, Count wrap, save/restore, duplicate events, IE/EXL/ERL and masks, EPC/BD, deferred interrupts and delay slots |
| CPU timing | Instruction and FPU latency, Off's seven-clock RAM estimate, half-clock phase, monotonic clock, Count writes/wrap, MMIO, pipeline hazards and SDK poll shortcut versus ordinary execution; separate game/USF policy builds |
| Caches, TLB and JIT | Existing precise/native state and clock comparisons, cache/write-buffer/coherence, mapped/uncached memory, protection faults, event exits, arena and resume guards |
| Floating point | Existing native/precise value, FCR31, exception, rounding and host FP environment tests; mapped execution and branch/delay-slot paths |
| RDRAM and memory map | RDRAM register/data tests; inclusive region dispatch, masked writes, live remapping and fastmem invalidation |
| MI/RI | Each interrupt source's mask/set/clear commands; derived RI status |
| PI/SI | Actual register/DMA orchestration, transfer data, busy rejection, scheduled completion, IRQ and acknowledgement; CPU/renderer/Joybus boundaries mocked |
| VI | 1,000 exact fractional field periods, Count-register independence, current half-line and renderer change notifications |
| AI | Existing two-entry FIFO, sample/deadline, register and monotonic clock conformance checks |
| PIF/Joybus | Valid command processing, channel skipping/termination and 2,262 malformed packet/bounds/stale-channel cases |
| Controllers/paks | Input/status/error handling, independent pak CRC oracle, final pak block, rumble and Transfer Pak control |
| Cartridge saves | EEPROM sizes and boundary writes, FlashRAM program/erase/ID DMA and persistence, SRAM masked writes; existing dongle and ROM database tests |
| Game Boy cartridge | MBC1 bank switching, RAM write protection/persistence, RTC 255/511-day rollover, elapsed days and latched reads |
| 64DD | All 30,212 logical/physical address round trips across seven geometries, total sizes, controller reset buffers and ROM reads |
| RSP | Existing scalar/vector interpreter and JIT suites, polling, signal and single-step conformance |
| RSP HLE | Work/path saturation, save/restore/reset, refinement admission, override/slice policy and NEAD load bounds/bank admission |
| RDP/rendering | DPC flag commands, start latch/alignment, read-only registers, unfreeze notifications, clock wrap/restore, DPS masks and frozen FIFO serialization/bounds; portable timing/short spans, command parser/TMEM, framebuffer notifications, scanline/fill/copy ownership at 1x/2x/4x and mocked GL state bindings |
| Host scheduling | Worker pool and Windows affinity scope |

This is broad component regression coverage, not exhaustive opcode or hardware
conformance. Native execution is validated on the host architecture; the ARM64
emitter check on x64 only inspects generated instructions. GPU driver integration,
frontend lifecycle/serialization round trips, every cartridge mapper and full
disk-command/RTC behavior need separate integration checks. Game replay and
audio starvation comparisons remain in
[the timing study](../benchmarks/fast-timing-study/README.md).

Accurate CPU timing is not changed to satisfy these tests. The older standalone
`benchmarks/cpu_timing.c` main uses a historical flat-clock fixture; this suite
reuses its architectural checks and verifies current Accurate/Off timing in
`tests/timing.c` and the existing cache/FPU suites instead.
