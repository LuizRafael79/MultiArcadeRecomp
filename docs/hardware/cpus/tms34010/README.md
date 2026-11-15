# TMS34010 shared CPU backend

This directory contains the shared TMS34010 AOT backend and the beginning of
its interpreter fallback. The current interpreter is still a partial
prototype. The full MAME-core port scope and the boundary between CPU and
T-Unit responsibilities are documented in [MAME_PORT.md](MAME_PORT.md).
Board memory mapping remains with the machine runtime.

## Recompiler prototype

`tools/tms34010/recomp.py` discovers statically reachable blocks from an entry
PC, translates the currently implemented instruction subset into native C++,
and writes an instruction-address lookup table. Each discovered instruction
gets a native dispatch entry, so a computed PC transfer can rejoin at an
instruction inside an already discovered block. It accepts an already assembled byte-addressed
`maincpu` region. For MK, ROM lane assembly is still done by the game ROM
pipeline; the CPU compiler does not parse MAME ZIP manifests.

After checking all 4,096 opcode slots against MAME's TMS34010 dispatch table,
the current static pass reaches 895 translated instruction visits across 214
blocks before computed PC transfers stop discovery. It follows literal targets
for `EXGPC`, `JUMP`, and indirect calls only when the value is established
inside the same straight-line block. Five computed-PC exits remain unresolved
in this pass: one `JUMP` and four `EXGPC`s. An earlier 14,514 / 5,030 count included illegal encodings decoded as
neighboring operations and was invalid. These are static compiler visits, not
an instruction census or runtime-frequency profile. Register-address PIXT,
`CVXYL`, `DRAV`, and absolute field copies lower through board bus callbacks;
other XY pixel forms, PIXBLT, line/fill, and shift-register transfers remain
outside the subset.

Unsupported instructions produce a block returning `BlockExit::NeedsInterpreter`
with PC unchanged. `runtime.hpp` supplies the shared AOT dispatcher, an
optional hook reporting the untranslated bit-PC/opcode, and a one-instruction
fallback hook. `interpreter.cpp` is an incremental interpreter adapted from
MAME's TMS34010 opcode and graphics implementations. It currently covers
several boot-path operations (including register/immediate arithmetic, moves,
branches, `RETI`, and `PIXBLT L,XY`), but it is not a complete TMS34010
interpreter. When an instruction is still unsupported, dispatch stops at that
exact PC rather than guessing its behavior.
MAME's `unimpl()` slots are emitted as one-cycle reserved operations because
its implementation explicitly logs and advances. MAME's `illop()` slots now
push PC/ST, restore ST to `0x10`, load the exception vector at `0xfffffc20`,
and dispatch; static discovery follows that vector when it is present in the
assembled image.
Graphics operations need the board to supply pixel callbacks. The TMS core
deliberately does not own PSIZE, raster operation, transparency, or shift
register state; the callbacks must implement those using the T-Unit's I/O
registers and memory map. The T-Unit runtime supplies these callbacks. The
current PIXBLT fallback is experimental: timing, clipping edge cases, reverse
traversal, and shift-register transfers still need comparison against MAME.
`RETS` uses the emulated stack and statically known direct-call continuations
where available. A bounded runner at `tools/mk/trace.cpp` combines generated
blocks with the fallback and stops at the first unsupported opcode; it is a
diagnostic harness, not a complete MK runtime or proof of game compatibility.
The runner always writes categorized fallback totals to
`build/mk/work/tms34010_fallbacks.log` and a bounded instruction-by-instruction
trace to `build/mk/work/tms34010_interpreter_steps.log`. The detailed log is
capped at one million steps by default; set `MULTIARCADERECOMP_INTERPRETER_STEP_LIMIT`
to change the cap. `--interpreter-steps=PATH` changes its output path. The
summary uses the shared fallback categories documented in
`core/cpus/interpreter_trace.h`.

Example for an assembled MK `maincpu` region:

```sh
python3 tools/tms34010/recomp.py \
  --rom build/mk/maincpu.bin \
  --base-byte-address 0x1f800000 \
  --map-size-bytes 0x800000 \
  --output build/mk/tms34010_aot.cpp
```

Compile the generated source with the project root on the include path. It
exports `lookup(bit_pc)` and `entry_pc()` in `multiarcaderecomp::tms34010`; a board
dispatcher must provide `State`, `Bus`, and cycle deadlines when calling blocks.

The compiler can also take known runtime-selected ROM PCs as extra AOT roots.
Use `--extra-entry-bits 0x...` repeatedly or pass a text file with one
bit-addressed PC per line via `--entry-list`. This is intended for PCs captured
by a future board trace hook: add only observed, aligned addresses inside the
mapped ROM window, then regenerate. It does not guess targets for `JUMP` or
`EXGPC`; those instructions already dispatch through the native block lookup,
but an unregistered target has no generated block and currently stops at the
untranslated-opcode hook.
