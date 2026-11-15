# Shared SH-4 CPU backend (work in progress)

Naomi currently owns the SH-4 context and AOT dispatcher in
`src/naomi/naomi_sh4_context.*`; moving those generic pieces into this shared
CPU backend is a follow-up. The active AOT path is derived from Initial D and
is not being replaced.

The Flycast interpreter source snapshot is staged at
[`core/third_party/flycast-sh4/`](../../../../core/third_party/flycast-sh4). Its opcode
and FPU handlers are the semantic source for the fallback adapter in
`flycast_interpreter.cpp`. The Naomi dispatcher keeps AOT first, then executes
the adapter one instruction at a time on a miss and resumes AOT at a known
entry. Native and interpreted instruction counts are separate. A static audit
matched the adapter's opcode masks against all 212 entries in Flycast's opcode
table; remaining CPU work is in exact variant behavior and the supporting
MMU/cache/exception/scheduler runtime, rather than missing top-level opcode
families. It recognizes Flycast's `NOP0` alias (`0x0000`) as a no-op alongside
standard `NOP` (`0x0009`). Naomi's
`run-naomi-sh4 --interpreter-only`
switch bypasses translated entries while preserving the board runtime, for
direct AOT-versus-interpreter boot comparisons.

The source is not compiled unchanged because Flycast handlers depend on its
global CPU context, memory/MMU, exception, cache and scheduler services. Adapt
remaining pieces to `Sh4Context` and its callback bus before claiming full
interpreter coverage. Preserve delay-slot behavior and stop/rejoin at
instruction boundaries. Keep board maps, BIOS, Holly/PVR, AICA and Maple
outside this CPU backend.

## Keep the three layers distinct

- `flycast_interpreter.*`: runtime semantic fallback adapted from Flycast; it
  does not generate host code.
- Naomi's existing SH-4 translator/dispatcher: MultiArcadeRecomp's AOT path. Future
  decoder and opcode coverage belongs to the translator, not the interpreter.
- Naomi runtime: memory map, exceptions/interrupt delivery, cache/MMU policy,
  timing, and devices. These must not be folded into the generic CPU decoder.

Flycast's SH-4 interpreter is the source of truth for this runtime port,
including opcode semantics, variants, and execution behavior. Use the local
MAME SH-4 sources at `~/git/mame/src/devices/cpu/sh/` only as a secondary
reference for documentation and opcode coverage. Do not use MAME runtime
behavior to replace or extend Flycast semantics in the Naomi port.

Instruction issue timing is now sourced from Flycast's `sh4_opcode_list.cpp`
through `tools/naomi/generate_flycast_sh4_cycle_patterns.py`. AOT and
interpreted instructions both retire through that timing table, including the
Flycast execution-unit overlap and the SH-4 core/external cycle ratio. The
address-dependent external bus wait-state model, MMU translation/permissions,
cache behavior, full exception sampling, and Flycast scheduler contract remain
incomplete; this is a shared timing foundation, not a complete CPU port.

The interpreter's FIPR, FTRV, FSCA and FSRRA handlers now use Flycast's
single-precision behavior, vector accumulation order and exact FSCA coefficient
table via `flycast_fpu.*`. The existing `host_f*` functions remain separate
for already-generated AOT code and are not changed by this interpreter port.
`core/third_party/mame-sh4-fpu/` remains linked for those AOT helpers; it is
not the interpreter's semantic source.

Basic FADD/FSUB/FMUL/FDIV, FCMP, FNEG/FABS, FSQRT and FMAC also have dedicated
Flycast-derived handlers in `flycast_fpu.*`. They preserve single-precision
operations as `float`, use Flycast's even-register DR pair mapping in PR mode,
and perform FMAC with a fused operation. A temporary focused check covered
single precision, DR multiplication/square root and a value that distinguishes
fused FMAC from separate multiply/add. The Naomi module builds with these
handlers; FPU cause/exception details and remaining variants still need audit.
The interpreter also adapts Flycast's FPSCR update contract: FPSCR loads clear
the cause field, FRCHG/FSCHG update their mode bits, and each interpreted FPU
instruction applies RM's host rounding mode for its duration before restoring
the caller's mode. A focused state check covers cause clearing and FR/SZ/PR
tracking. DN now flushes subnormal inputs and results for the arithmetic,
compare, square-root, FMAC and vector operations covered by the dedicated
handlers. A focused check confirmed a subnormal input becomes signed zero with
DN enabled. Floating-point cause/exception behavior and DN coverage for the
remaining conversion/transfer instructions still need audit.
