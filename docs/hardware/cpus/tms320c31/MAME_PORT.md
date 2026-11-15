# TMS320C31 interpreter port

## ISA authority

Use Texas Instruments' [TMS320C3x User's Guide, SPRU031F](https://www.ti.com/lit/ug/spru031e/spru031e.pdf)
as the primary specification for instruction formats, operations, and status
flags. Chapter 13 contains the 113-instruction ISA summary, addressing-mode
encodings, condition codes, and per-instruction descriptions. The MAME source
is useful for cross-checking edge cases and cycle behavior; generated native
instructions must not invoke MAME opcode handlers.

The same guide's Appendix A (PDF pages 724–729) is the opcode mask/value
index, including dedicated encodings for parallel operations. Appendix B
(PDF pages 730 onward) contains the C31/VC33 boot-loader source. That source
and Chapter 11 are relevant to the V-Unit RAM image: the boot loader copies
program blocks to their destination addresses and branches into the loaded
image, so AOT discovery must follow live RAM contents and invalidate changed
words instead of assuming the reset image is the whole program.

The 1994 SPRU031D edition is also archived locally for historical comparison.
Its revision notes call out improved repeat/interrupt descriptions and updated
serial-port/DMA material. Use SPRU031F as the primary decoding reference; use
SPRU031D to compare how the C31-specific architecture and timing were
documented closer to the game's development era.

Complementary TI references:

- [TMS320C31 Embedded Control Technical Brief, SPRU083](https://www.ti.com/jp/lit/an/spru083/spru083.pdf): device/application context.
- [TMS320C3x General-Purpose Applications User's Guide, SPRU194](https://www.ti.com/lit/ug/spru194/spru194.pdf): application examples and assembly routines.
- [TMS320C3x/C4x Assembly Language Tools User's Guide, SPRU035](https://www.ti.com/lit/pdf/spru035): assembler, linker, directives, and object-image details.

Treat SPRU031F as the ISA authority. The other guides help explain real code
and image layout, but do not replace the per-instruction encoding descriptions.

The current interpreter's instruction handlers and indirect-addressing
routines are adapted from MAME's TMS320C3x core. `mame_source/` retains the
upstream files unchanged as a comparison/reference; the adapter binds the C31
register state and 24-bit word-addressed bus to MameRecomp callbacks. The AOT
translator is being separated from those handlers, with the TI guide as its
ISA reference.

The active interpreter includes MAME's complete opcode dispatch table and
handler body. MAME's explicit `unimplemented()` paths and `norm` fatal path
stop with a diagnostic reason instead of terminating the host process. They
must be checked against Cruis'n USA traces before claiming complete game
coverage. This describes limitations in MAME's CPU core, not defects in
Cruis'n USA or its ROM set.

Opcode fetches use a shared direct-page cache modeled on MAME's
`memory_access_cache`: board adapters expose contiguous code pages, while
handler-backed and unmapped pages fall back to the normal bus callback. The
cache stores pointers to live ROM/RAM, not copied opcode data, so self-modifying
RAM code remains visible. A board that changes mappings must call
`Interpreter::invalidate_code_cache()`. `read_code_word()` gives runtime code
discovery and recompilation the same live code view without advancing CPU
state or instruction counters.

## Runtime boundary

- CPU state, C3x custom floating-point representation, instruction dispatch,
  delayed branches, repeat blocks, and IRQ entry live in
  `core/cpus/tms320c31/`.
- Board address decoding, low-RAM reset copy, scanline IRQ, DMA queue, VRAM,
  texture RAM, palette, controls, CMOS, and the DCS host link live under
  `src/midway/vunit/`.
- The C31's program/data bus uses 32-bit words addressed by 24-bit word
  addresses. MAME configures its C31 at a 50 MHz input clock and divides raw
  clocks by two for execution cycles.
- DCS Audio 2K host protocol is shared under `core/chips/dcs/`; its ADSP-2105
  instruction backend remains the generic `core/cpus/adsp2105/` implementation.

`tools/tms320c31/generate_mame_declarations.py` regenerates the handler
declarations used by the adapter from the vendored opcode source. Do not edit
the generated declarations by hand.

## Static recompilation

`tools/tms320c31/recomp.py` reads the V-Unit `main.bin` low-RAM/reset image and
emits guarded C++ blocks. Direct-lowering selection now uses a local TI-derived
mask/value table (`TI_DIRECT_MASK` / `TI_DIRECT_OPCODE_NAMES`) based on
Appendix A plus the Chapter 13 instruction formats; those direct instructions
are not classified by MAME's handler table and do not call MAME handlers.
MAME's opcode-group table still classifies the remaining legal words so the
generator can emit guarded fallback wrappers. Replacing that remaining
build-time classification with TI-derived decoding is still in progress.
Generated instructions validate the expected PC and live opcode, then perform
the translated operation in generated C++. The interpreter remains the
fallback for instructions outside the translated subset, changed code, and
unmapped PCs.

The first directly translated families are `LDI` register/immediate,
`ADDI`/`SUBI` register/immediate, and `AND`/`OR`/`XOR` register/immediate,
restricted to general integer destinations R0–R7. The emitter includes the
integer flag and OVM saturation effects for these forms. The generator also
translates the register forms of `ADDF`, `SUBF`, and `MPYF`, including their
three-register variants, `CMPF3` register/register, their immediate forms, `ABSF`/`NEGF` register and
immediate forms, `CMPF` register/immediate, and `FLOAT`/`FIX` register and
immediate conversion forms. Mantissa/exponent arithmetic and flags are
implemented in `native_ops.hpp`; generated code calls these inline CPU
primitives without entering a MAME opcode handler or converting through host
`float`/`double`.

Destinations in special registers and every other instruction family still
use the guarded MAME-derived handler wrapper. Control-flow instructions remain
single-op blocks; repeat-mode execution still dispatches at each instruction
boundary so the repeat counter and interrupts are checked there.

The code-image scan can encounter data that decodes as an instruction, so
`guarded-candidates` is not a count of reachable or executed opcodes. Runtime
native/interpreted totals and hot-PC logs remain the evidence for actual game
coverage. The generator reports how many words were emitted as direct native
operations separately from the candidates that use an instruction handler.
Scalar direct translations reject encodings with bit 31 set, which Appendix A
assigns to parallel-operation forms. These forms are not two ordinary
sequential instructions: the guide specifies that operand reads are observed
at the start of the cycle and register/memory writes are committed at the end,
with defined behavior for read/write overlap. They remain on the guarded
handler path until the translator can lower both operations as one atomic
instruction, preserve source snapshots, perform all memory accesses in the
documented order, and commit both outputs and flags correctly. A future
mask/value decoder must classify these encodings before scalar opcode-family
matching.

The generator still uses MAME's opcode-group table to classify words for the
fallback wrapper. The direct operations listed here are emitted as native C++
and do not call MAME handlers; replacing build-time fallback classification
with a complete TI-derived decoder remains work in progress.

## Comparison with the Killer Instinct MIPS AOT path

Use `core/cpus/mips3/` and `tools/mips3/recomp.py` as an architectural
reference, not as reusable ISA code. The KI translator emits common MIPS
operations directly into C, groups control-flow regions into functions with
labels for valid entry PCs, and calls `rec_op()` only for instructions that
remain interpreted. Its runtime maps PCs to function pointers and validates a
whole function against mutable RAM once per code generation, then caches the
result until a write invalidates that generation.

Some C31 opcode families now emit direct C++ rather than call
`aot_execute_*()`. The remaining families still use that guarded handler path
until their arithmetic, special-register effects, addressing modes, and cycle
behavior are translated explicitly. Do not port MIPS delay-slot, TLB,
register, or memory semantics to the C31.

Rollback marker for the current direct-emitter set: `emit_direct_instruction()`
in `tools/tms320c31/recomp.py`, the floating-point primitives in
`core/cpus/tms320c31/native_ops.hpp`, and this section. Supported names are
`LDI_REG/IMM`, `ADDI_REG/IMM`, `SUBI_REG/IMM`, `AND_REG/IMM`, `OR_REG/IMM`,
`XOR_REG/IMM`, `ADDF_REG`, `ADDF3_REGREG`, `SUBF_REG`, `SUBF3_REGREG`,
`CMPF3_REGREG`, `MPYF_REG`, `MPYF3_REGREG`, `ADDF/SUBF/MPYF_IMM`, `ABSF/NEGF_REG/IMM`,
`CMPF_REG/IMM`, and `FLOAT/FIX_REG/IMM`. Integer operations restrict
destinations to R0–R7; floating-point forms use the instruction-defined R0–R7
operands, and `FIX` currently only translates R0–R7 destinations. Expand this
set with instruction-level equivalence evidence against the interpreter.

The generic pattern worth sharing across CPU backends is: generated entry map,
direct host operations for supported instructions, explicit helper calls for
bus-visible effects, code-write invalidation, and a precise interpreter exit
when the next native operation is unsupported or changes control flow. The
ADSP-2105 generator also needs larger native blocks; it currently dispatches
per PC/opcode and often delegates an instruction to `native_execute_instruction()`.

## Frozen AOT checkpoint — 2026-11-02

The initial C31 direct-lowering prototype is intentionally frozen here. Do not
add opcode families or change its semantics until this checkpoint is resumed.
The current state is:

- Direct recognition uses the TI-derived 11-bit mask/value entries in
  `tools/tms320c31/recomp.py`; the remaining fallback classification still
  comes from the MAME opcode-group table.
- Native scalar lowering covers `LDI`, `ADDI`, `SUBI`, `AND`, `OR`, `XOR`,
  `ADDF`, `SUBF`, `MPYF`, `CMPF3` register/register, `ABSF`, `NEGF`, `CMPF`,
  `FLOAT`, and `FIX`, limited to the documented forms and register handling
  listed above. FP primitives use the C31 mantissa/exponent state, not host
  `float`/`double`.
- Encodings with bit 31 set remain on the guarded fallback. Parallel C3x
  operations need one atomic lowering with source snapshots and end-of-cycle
  commits; splitting them into scalar operations would be incorrect.
- The 131,072-word image produced 61,422 guarded candidates, 14,639 direct
  translated words, and 14,041 generated blocks. These are image-word counts;
  data can decode as opcodes, so they say nothing by themselves about distinct
  instructions, reachable code, runtime execution, or coverage.
- The Release target `mamerecomp_vunit_trace` compiled successfully after this
  checkpoint. No instruction-by-instruction equivalence run or gameplay
  performance gain has been established for the new direct emitter.
- The TI references used for this work are archived in
  `docs/hardware/cpus/tms320c31/REFERENCES.md`. SPRU031F is the ISA authority; SPRU031D (1994)
  is retained for historical comparison. Read the README there before using
  the supplementary assembler/application guides.

When resumed, first compare native operations against the interpreter on the
same instruction/state inputs. Then expand the TI decoder, parallel-operation
model, and RAM-code discovery based on those results.

The runtime follows three paths: a known PC with matching code enters AOT; a
known PC whose live opcode differs falls back and reports
`AOT_OPCODE_MISMATCH`; an unknown PC runs in the interpreter and reports
`UNCOVERED_ENTRY`. The runner prints category totals and the last PC/opcode,
and emits a bounded set of detailed fallback lines so logs remain usable.
After an AOT word matches once, its validation is cached. The V-Unit bus
invalidates that exact PC on every write, forcing a fresh live-opcode check
before the next AOT execution. Mapping changes clear both the direct-page and
validation caches.

The generated source is split into 4K-word PC shards. The dispatcher selects a
low-memory shard directly and checks the sparse captured-RAM overlay separately;
this avoids compiling the full generated table as one enormous C++ unit while
preserving direct lookup for the reset image.

Enable generation by configuring with
`-DMAMERECOMP_CRUSNUSA_C31_ROM_IMAGE=/path/to/main.bin`. The generated source
is written into the selected CMake build directory. Leave the setting empty
to build the same CPU with interpreter-only dispatch. Captured stable RAM code
can be added with `-DMAMERECOMP_CRUSNUSA_C31_OVERLAY=/path/to/overlay.txt`;
every overlay word is checked on first use and again after a write to its PC. Use
`MAMERECOMP_C31_HOT_SITES=1` during a run to capture hot PC/opcode pairs, then
`tools/tms320c31/capture_overlay.py run1.log run2.log --output overlay.txt` to
union repeated captures. The tool rejects addresses whose opcode differs
within or between runs, leaving those PCs on the interpreter path. Runtime
totals separate native/AOT from interpreted instructions.

The V-Unit runner can also generate an initial ADSP-2105 table from the DCS
region with `-DMAMERECOMP_CRUSNUSA_DCS_ROM_IMAGE=/path/to/dcs.bin`. It mirrors
the DCS 2K bank-zero boot transfer and feeds its packed 24-bit words into the
shared `tools/adsp2105/recomp.py` generator. Dynamic program writes and banked
code without a matching AOT entry retain the ADSP interpreter fallback.

## Reference documents

- TI [TMS320C3x User's Guide, SPRU031F](https://www.ti.com/lit/pdf/spru031)
- [MAME source map and upstream license](../../../references/mame/cpus/tms320c31.md)
