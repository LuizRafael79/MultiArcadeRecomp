# ZUni80: Z80 AoT backend and board adapters

**ZUni80** é o nome do backend AoT Z80 próprio do MameRecomp. Seu frontend
experimental em `tools/z80_aot/` já decodifica instruções, descobre blocos e
analisa agrupamentos de rotinas/entradas de retomada. Ainda não emite o código
de produção nem substitui o gerador legado; integração, verificação diferencial,
temporização e paridade de desempenho continuam pendentes.

## Shared CPU rule

There is one MameRecomp Z80 CPU/recompiler implementation for every board.
An arcade board does not change the Z80 instruction set. CPS1, CPS2, Neo Geo,
and any System 16/18 sound CPU should use the same decoder, generated-code
ABI, instruction helpers, and selected interpreter. A board supplies its memory
and I/O bus, ROM banking, interrupt lines, clock ratio, and synchronization
with its sound devices.

Generated code tables remain per game because they come from each game's ROM;
they plug into the shared `mamerecomp_z80_aot` backend. The common code is in
`tools/z80/`, `core/third_party/z80-aot/`, and `core/cpus/z80/`. It is under the
generic `mamerecomp::z80` namespace. `Mode::NativeStrict` is diagnostic only
and must not replace the normal hybrid mode in board runners.

## Current integration

- The `sf2ce` and `dino` CPS1 commands use the local shared code generator and
  compile generated tables into `libmamerecomp_z80_aot.a`. Dino's fixed opcode
  view is decoded with its MAME Kabuki key; its data view remains raw ROM bytes.
  Current generation reports zero unsupported instructions for both fixed
  sound programs. Their runtime board buses and chip scheduling are still
  being connected.
- Neo Geo/KOF98 uses the shared C++ Z80 CPU state/runtime and generated tables.
  Its historic game-facing wrappers now use `mamerecomp::z80`.
- System 16A, System 16B, and System 18 sound adapters use the common
  `mamerecomp::z80::Z80` interpreter through board-owned memory and port
  callbacks. Board-specific banking, YM synchronization, IRQ and NMI wiring
  stay in each adapter. Their generated per-game AOT tables are not connected
  yet, so those boards currently execute entirely through the shared
  interpreter.
- SuperZazu remains available as the MIT execution reference selected in Neo
  Geo with `NG_Z80_MODE=legacy`. The regular shared AoT path is still the
  GPL-3.0-only CPS2-derived engine. The migration goal covers both the
  generated-code frontend/runtime and the interpreter on every Z80 board; no
  production board has switched to a replacement AoT yet.

## Interpreter comparison (2026-11-12)

There is no unconditional winner; the evidence favors a different core on each
axis:

| Criterion | SuperZazu (`core/third_party/superzazu-z80/`) | Local `mamerecomp::z80` (`core/third_party/z80-aot/`) |
|---|---|---|
| Instruction semantics | Upstream README for the pinned `d64fe10` revision reports passing both ZEXDOC and ZEXALL. This is stronger published conformance evidence than we currently have for the local core. | Broad opcode implementation is present, including undocumented/indexed behavior, but this repository has no Z80 CPU conformance target or recorded ZEX results. Do not infer correctness from KOF98 gameplay alone. |
| Timing | Counts T-states per instruction; memory/I/O callbacks do not expose an intra-instruction bus-cycle timestamp. | Also uses instruction stepping, but `Z80Bus::InstructionBoundary` feeds the current guest timestamp to board devices and can yield at I/O/interrupt boundaries. This is more integrated with the current YM/audio scheduler. Neither is a bus-pin-accurate Z80 model. |
| Interrupts and board hooks | Has NMI/INT support, interrupt mode/state and callback buses; the board adapter still needs to map its interrupt acknowledge and synchronization rules. | IRQ/NMI, EI delay, HALT, bank mapping, dynamic-code checks, fallback logging, and generated-block scheduling already use the MameRecomp board contract. |
| AoT | Interpreter only. | Integrated with the project's AoT generator/runtime, but that implementation is GPL-3.0-only and cannot remain the project's MIT-distributable core under the current licensing requirement. |
| Runtime speed | No comparative benchmark recorded. | AoT can skip interpreter dispatch for generated blocks; no controlled SuperZazu-vs-local interpreter benchmark recorded. |

**Current conclusion:** The pinned SuperZazu revision is the better-supported
correctness reference based on ZEXDOC/ZEXALL. Its upstream test harness was
compiled against the vendored `z80.c` and passed `prelim.com`, ZEXDOC and
ZEXALL; the copyrighted test images and harness remain in the external
upstream checkout and were not copied into MameRecomp.

A 120-frame KOF98 comparison was run with `NG_PROFILE=1`, `--audio`, separate
NVRAM files and a YM2610 write trace in each mode. The default AoT path and
`NG_Z80_MODE=legacy` emitted 1,148 writes with identical ports and values. The
first write timestamp differed by -11 Z80 T-states and the final write by -28;
the SuperZazu run ended with a -4-cycle clock skew against the scheduler. A
separate 30-frame legacy trace recorded 54,660 SuperZazu instruction steps in
the first 500,000 Z80 cycles. The 68000 verifier passed in both launches.
The longer 1,200-frame capture produced 13,755 writes in each mode, with all
port/value pairs identical. The first event was 11 cycles earlier and the last
event 34 cycles earlier in the SuperZazu trace; final scheduler skew was -15
cycles for SuperZazu and +8 for AoT. This supports KOF98 YM command
compatibility, but does not prove identical per-instruction state, sample
output, or compatibility with every board's bus timing and interrupt
acknowledge.

The migration remains incomplete: the default path and other Z80 boards still
link the GPL-3.0-only frontend/runtime. The shared replacement must keep board
bus/clock/IRQ hooks while moving instruction execution to SuperZazu and
providing a new project-owned, license-compatible AoT frontend. No production
board has switched to that complete replacement yet.

## LLVM-Z80 evaluation

The [`llvm-z80` project](https://github.com/llvm-z80/llvm-z80) is an LLVM
backend that compiles C/C++/LLVM IR *to Z80 machine code*. It is not a binary
Z80-to-host recompiler, so it cannot replace `tools/z80/recomp.py` or serve as
the direct AoT generator for emulated sound ROMs. It is still useful as a
reference for instruction selection, optimization, and compiler test design;
the relevant ideas can inform our own Z80 guest-to-host frontend without
pulling in the LLVM fork as a build dependency.

Its distribution identifies the backend as Apache-2.0 with LLVM exceptions.
That is compatible with using it alongside this MIT project, provided its
license, exception text, notices, and applicable source attribution remain
intact. Compatibility does not make upstream files relicensable as MIT. For
our implementation, independently authored code can remain MIT; any copied
upstream material must retain its original license and notices.

## Pacrecomp evaluation

[`pacrecomp`](https://github.com/sp00nznet/pacrecomp) is a closer reference:
it is an MIT-licensed Z80 ROM-to-C recompiler, with a decoder, recursive
control-flow analyzer, emitter, and verification interpreter. Its emitter
documents the main, CB, ED, DD/FD, and DDCB/FDCB instruction planes and
self-modifying immediate handling. I downloaded the public source to `/tmp`,
reviewed the decoder/analyzer/emitter and license, and built it with GCC 16.2
Release; both included tests (`test_decode`, `test_rt`) passed. The license is
the standard MIT license and retains the author's `sp00nz` copyright notice.

It is a valuable implementation reference and potential source of audited
MIT-licensed components, but it is not yet a drop-in backend for MameRecomp:
its runtime uses one global Pac-Man CPU/memory/I/O API, and every basic block
returns to a host dispatcher. That output shape does not establish a speedup
over the current KOF98 AoT, which groups blocks into routines with direct
intra-routine gotos. Its two focused tests also do not establish complete
opcode conformance, KOF98 compatibility, board timing, or performance. The
new MameRecomp frontend should reuse or adapt only after those boundaries are
audited; in particular, retain the M68K-style direct block linking, the
per-board callback contract, and benchmark output against the existing AoT.

As a KOF98-specific feasibility probe, the relevant MIT decoder, analyzer, and
emitter are now vendored under `tools/z80_aot/pacrecomp/`, with the upstream
license and origin recorded alongside them. The excluded CMake target
`mamerecomp_z80_pacrecomp_probe` analyzes the fixed BIOS (`sm1.sm1`) and
cartridge (`242-m1.m1`) images using the current runtime-observed roots. It
discovered 3,832 BIOS instructions in 947 blocks and 3,686 cartridge
instructions in 966 blocks. The emitter produced both code files without
reporting an unimplemented reachable opcode. This confirms that its instruction
lowering covers the current static KOF98 corpus; it does not prove instruction
semantics or timing parity. The emitted runtime still lacks MameRecomp's
cycle-budget yields, interrupt deadlines, board I/O timestamps, and
dynamic-code/fallback hooks, so those need an adapter before its output can run
in the KOF98 sound scheduler.

## 68000 recompiler methodology reference

The user-provided [`kof98recomp`](https://github.com/danilopaulinodasilva/kof98recomp)
repository is a game profile and launcher around the NeoGeoRecomp toolkit: its
68000 generator entry point delegates to `ng_recomp_main()`. MameRecomp's
68000 frontend is itself based on the same NeoGeoRecomp `m68krecomp` sources,
vendored and integrated locally under `core/third_party/m68krecomp/`. The
toolkit's
[`m68krecomp` design](https://github.com/danilopaulinodasilva/neogeorecomp/blob/a47ee3fe09dc46d1f2b86c36ec972b43b35ebf62/docs/recompiler.md),
and the matching discovery/emission implementation is
[`recomp.c`](https://github.com/danilopaulinodasilva/neogeorecomp/blob/a47ee3fe09dc46d1f2b86c36ec972b43b35ebf62/tools/m68krecomp/recomp.c).
This is a methodology reference; no code from this repository is being copied
into the Z80 frontend.

The useful pattern for the project-owned Z80 AoT is:

1. Decode and cache instructions once, then discover code from reset, interrupt,
   direct control-flow edges, and supplied roots. For Z80, start with reset,
   IM 1 vector, fixed BIOS/cart roots, and runtime-observed misses. Add
   table/code-pointer discovery where the sound ROM uses computed dispatch.
2. Emit a host routine for a connected guest-code region. Give the routine a
   switch for valid entry/resume addresses, then use labels and direct `goto`
   edges internally. Calls, returns, indirect jumps, and required scheduler
   yields return to the board dispatcher. This avoids a host dispatch per
   guest basic block while preserving the Z80 stack and runtime control flow.
3. Add accurate guest cycles to emitted instructions, including dynamic branch
   and repeat costs. Do not transplant the 68000 event policy verbatim: Z80
   interrupt enable delay, sound-latch I/O timestamps, and YM timer deadlines
   define where the Neo Geo runtime must regain control.
4. When execution reaches code outside the discovered table, use SuperZazu as
   fallback and record the address. Replay scripted/soak sessions, feed new
   entry addresses into the next build, and compare native execution against
   SuperZazu from identical CPU/bus snapshots, including register state and
   ordered bus writes.

The current local CPS2-derived generator already groups blocks into routines
and uses direct intra-routine branches. ZUni80's MIT `tools/z80_aot/` frontend
has decoding, static block discovery, and a prototype analysis for grouping
blocks and identifying entry/resume points. It does not yet emit those grouped
routines, instrument runtime misses, or provide differential verification.
The Pacrecomp probe currently emits one function per block and is a useful
source of instruction-lowering logic, but its dispatcher shape and untimed
runtime are not the final MameRecomp design.

The ZUni80 CFG was cross-checked against that MIT analyzer on the current
KOF98 fixed-ROM inputs, using reset (`0x0000`), IM 1 (`0x0038`), and the
checked-in BIOS/cart entry lists. ZUni80 finds 3,842 instructions / 951 blocks
for SM1 and 3,696 instructions / 969 blocks for M1. Pacrecomp finds 3,832 /
947 and 3,686 / 966 respectively. Every Pacrecomp block address is also
present in the ZUni80 result; the extra ZUni80 blocks are reachable through
RST vectors (`0x0008`/`0x0010`), which Pacrecomp's analyzer does not follow.
The ZUni80 discovery keeps ED block-repeat instructions such as LDIR inside
their containing block so execution can continue at the following instruction
after the repeat completes. Synthetic tests cover this rule and the routine
grouping model. This is CFG coverage evidence only, not an emitted-code or
instruction-semantics comparison.

### Required parity with the project's 68000 AoT

“Parity” means the Z80 frontend must offer equivalent pipeline capabilities;
it does not mean copying 68000 instruction rules or blindly scanning 16-bit
data as code. The local 68000 frontend in
`core/third_party/m68krecomp/recomp.c` and `emit.c` provides this reference:

| Capability | Z80 production path today | Project-owned Z80 path |
|---|---|---|
| Instruction decode and direct-edge discovery | Present in `tools/z80/recomp.py` | Present in `tools/z80_aot/isa.py` and `discover.py` |
| Jump-table / code-pointer discovery | Mostly supplied as manually maintained or runtime-observed roots | Missing |
| Routine grouping, entry/resume switch, direct internal labels | Present in the legacy generator | Grouping/entry analysis prototype exists; grouped-code emitter missing |
| Runtime miss capture and feed-back into the next generation | Runtime fallback logging exists; automated profile/rebuild flow is incomplete | Missing |
| Timing-aware yields at board event boundaries | Present through the current instruction-boundary path, with measurable per-instruction overhead | Missing |
| Differential execution against an interpreter, including ordered bus effects | No integrated Z80 block verifier | Missing |
| CPU conformance gate in the normal build | SuperZazu's upstream ZEX suites were run externally; no local Z80 gate | Missing |
| Standard per-game build integration | KOF98-specific generator orchestration remains | Missing |

The project-owned Z80 AoT is not at parity yet. Close each gap with Z80-aware
logic: table patterns and bank mappings differ from 68000, runtime yields must
respect YM2610 and sound-latch timing, and the reference executor is SuperZazu.
Keep the current production generator in place until the new frontend can
generate KOF98's BIOS and cart tables, pass equivalent coverage and state/bus
verification, and compare performance against the integrated baseline.

## AoT performance direction

Inspection of the current KOF98 generated module found that it already
translates each instruction into host code and removes the per-instruction
opcode dispatcher. Its generated bodies still call out-of-line runtime methods
such as `Z80::Alu`, `SetPair`, and `Add16` for ordinary register operations;
these appear as unresolved imports in the game module and are implemented in
the separately linked runtime. Therefore the next AoT must preserve direct
block control flow while making common register and flag semantics visible to
the host optimizer, rather than only replacing the decoder or dispatch table.

Disassembly of the Release KOF98 module confirms this cost at a hot BIOS
routine: the generated body checks the PC, checks the cycle budget at
instruction boundaries, updates an instruction counter, constructs the timing
boundary, and calls the out-of-line `Z80::Alu` even for `XOR A`. The generic
cycle-boundary callback path also has a per-instruction branch. These are
specific optimization targets to measure independently; board-visible memory
and I/O synchronization must remain exact.

A temporary compiler probe against the MIT SuperZazu implementation tested
constant opcode specialization. Marking its internal `exec_opcode` switch
always-inline and emitting a block with constant opcodes made GCC `-O3 -flto`
remove the opcode switch and inline instruction semantics; the resulting
three-instruction probe had no calls or per-instruction returns. This is
promising evidence, not yet a production solution: the probe used a temporary
source copy, and an actual emitter still needs full opcode, interrupt, timing,
bus-callback, and state-equivalence coverage.

The transfer from the M68K frontend is its code-generation shape: discover
basic blocks, emit direct host operations, connect intra-routine edges with
labels/gotos, and yield only where board deadlines or dynamic control flow
require it. For Z80, specialize each decoded opcode at generation time, inline
hot register/flag updates, and retain callbacks for memory-mapped I/O and
scheduling. Performance claims require a controlled KOF98 run against the
current AoT with identical ROMs, audio/device events, host compiler flags, and
generated-code coverage. The CPS2-derived AoT remains available until the new
output has parity and passes that comparison.

### KOF98 runtime baseline (2026-11-13)

**Benchmark interpretation:** the tested executable and game module are built
and run by MameRecomp. The AoT source generator in `tools/z80/recomp.py` is a
locally adapted implementation derived from CPS2Recomp, paired with the local
`mamerecomp::z80` runtime; this is not an external CPS2Recomp module loaded at
runtime. The 1,200-frame comparison below measures MameRecomp's current
integrated AoT path against SuperZazu's interpreter. It does not measure the
new project-owned generator frontend under `tools/z80_aot/`, whose emitter and
production integration are still unfinished. A direct comparison between that
new generator and the current integrated AoT can happen after the new emitter
produces equivalent KOF98 coverage and uses the same runtime contract.

An alternating, headless 1,200-frame MVS run used the same Release build,
`kof98.zip`, `neogeo.zip`, `sp-s2.sp1`, `--audio`, and one `/tmp` NVRAM file.
With the current generated Z80 path, elapsed times were 1.791 s and 1.861 s;
with `NG_Z80_MODE=legacy` (SuperZazu instruction stepping), they were 2.085 s
and 2.042 s. The medians are 1.826 s and 2.064 s respectively, about 13%
slower end-to-end in the legacy run. The generated path reported about 1.66
million native Z80 blocks and 7.8k interpreted instructions; the legacy path
does not currently increment those same counters. Both modes ended within 12
guest Z80 cycles of the expected clock, and the 68000 path reported zero
interpreted instructions.

This is a reproducible whole-run baseline, not an isolated Z80 benchmark: it
includes ROM loading, audio-device work, the 68000 and the board scheduler, and
the small 68000 block count varied on the first run. Keep it as context for
later measurements, and do not attribute the full 13% difference to Z80
execution without per-core timing instrumentation.

## Timing contract

The shared CPU receives a board-provided cycle budget and memory bus. Bus
callbacks report the current Z80 cycle timestamp so YM2151, OKI MSM6295,
QSound, YM2610, or other board devices can be advanced before each register
access. Per-board scheduler code converts the main CPU/master clock to Z80
cycles, applies board-specific IRQ/NMI timing, and synchronizes audio chips.
Instruction semantics and the selected interpreter/AOT policy stay in the
common Z80 core.

## Build artifact example

```sh
python3 tools/recompile.py --system cps1 --game sf2ce \
  --rom-dir /home/luiz/git/mame/roms/sf2ce.zip
```

This generates a game-specific table under `build/cps1/work/sf2ce/gen/z80/`
and builds the shared backend target. It does not yet claim that the CPS1
sound bus or full game runner is connected.
