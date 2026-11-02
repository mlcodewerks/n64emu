# Signed DIV sign-mismatch investigation

**The DIV failure is not fixed. No speculative arithmetic model has been added
to the interpreter or JITs.**

The pinned Thar0 `div_data.bin` contains 16,384 hardware reference records.
Current 32-bit-dividend / 64-bit-divisor arithmetic matches all 8,188 records
where divisor bits 31 and 63 agree, and fails all 8,196 where they disagree.
The first disagreement is the existing byte `0x154` mismatch documented in the
conformance report. Both LO and HI must be verified; a preferred repeating
quotient pattern is not an adequate replacement for the missing behavior.

The [test author's explanation](https://github.com/Thar0/N64-IntegerMulDiv-Test)
explicitly leaves this case unresolved. The behavior is also left unresolved
in [ares's DIV implementation](https://github.com/ares-emulator/ares/blob/master/ares/n64/cpu/interpreter-ipu.cpp).
Ordinary restoring/non-restoring arithmetic, operand truncation, and several
candidate sign/carry models were compared with the reference data. None matched
all malformed-divisor records. Those experiments remain in ignored `out` files.

No physical N64 is available. The existing capture remains the source of truth;
new hardware observations are an optional way to distinguish candidate models,
not an available validation step in this environment.

The follow-up investigation also tested extra setup iterations, independent
32/64-bit sign extension, one’s-complement add/subtract biases, sign-dependent
initial flags and quotient correction. Linear and quadratic flag rules inferred
from partial-remainder sign/carry signals did not fit the corpus. None of these
experiments produced a core change or a conformance pass.

Reproduce the complete current-arithmetic comparison with:

```sh
python benchmarks/conformance/audit_div.py \
  benchmarks/conformance/upstream/N64-CPU-Tests/div_data.bin \
  --output benchmarks/conformance/out/div-corpus-audit.json
```

The audit exits nonzero for the known mismatches. It also checks the hardware
capture’s remainder identity, result sign extension, unchanged operands and
repeated `(RS low 32 bits, RT)` consistency. It audits data and current arithmetic;
it does not replace running the image through each JIT.

## Hardware observation ROM

`prepare_nemu.py --div-probe` builds `nemu-div-probe.z64` from the same pinned
nemu64 source and cached Rust/nust64 tools used by the conformance ROM. It appends
`div_probe.rs.inc` in an isolated source copy and replaces that copy's test
selection with startup, the probe, and teardown. It changes no conformance
assertions in the upstream checkout.

The probe executes actual MIPS DIV instructions, with explicit separation around
HI/LO accesses. It reports 1,120 rows covering zero, overflow, known reference
examples, sign disagreements, single-bit sweeps, and repeated inputs with
different preceding operations. Each row is `DIVPROBE RS RT LO HI`, using four
16-digit hexadecimal words. The terminal marker is `DIVPROBE END rows=1120`.
The log also includes CP0 PRId to identify the processor revision.

This ROM **observes** results. Its framework completion summary does not mean
malformed DIV passed an accuracy test. `parse_nemu.py` explicitly rejects probe
logs as conformance results.

Build with the existing tool paths described in `README.md`, adding:

```sh
--output benchmarks/conformance/out/div-hardware-probe --div-probe
```

Run the ROM on physical N64 hardware and capture its ISViewer output. A loader
that exposes ISViewer logging is needed for the full table; the screen alone
does not retain all 1,120 rows. Extract the observations with:

```sh
python benchmarks/conformance/parse_div_probe.py hardware.log \
  --source physical-n64 --output hardware-div-observations.json
```

The parser checks completeness, canonical DIV cases, sign extension, duplicate
consistency, and `HI = sign_extend_32(RS - RT * LO)`. It records raw observations
and never claims that a malformed-DIV implementation has been validated.
An emulator run is useful for checking probe transport only and must be labeled
`--source emulator`.

The probe's x86-32 emulator transport run captured all 1,120 rows (1,096 unique
input pairs), including 40 canonical cases. Canonical arithmetic, sign extension,
the remainder identity and repeated-pair consistency all passed these transport
checks. This does not validate any malformed-divisor result against hardware.
