# Neo Geo KOF98 static recompilation prototype

## Current verified state (2026-11-13)

The launcher selects hardware directly with `--board=aes` or `--board=mvs`.
AES uses the original Japanese BIOS (`neo-po.bin`); MVS uses `sp-s2.sp1`.
UniBIOS remains available through an explicit `--bios uni-bios_4_0.rom` after
building its matching optional AoT module. KOF98 is loaded by
the common `mamerecomp` host through `mamerecomp_neogeo_runtime.so` and the
mode-specific `mamerecomp_game_kof98_aes.so` or
`mamerecomp_game_kof98_mvs.so`; it is no longer a separate
`kof98recomp` runner.

The local 68000 frontend prepares the transformed program image and emits
native C into the build tree. Musashi remains the runtime fallback and is also
used by `--verify` for instruction-state comparison. The command below selects
MVS hardware.

```sh
build/mamerecomp run --machine=neogeo --board=mvs --game=kof98 \
  --headless --frames=3600 --verify
```

The BIOS-specific modules are now distinct and both were verified for 120
headless frames using their standard BIOSes. MVS (`sp-s2.sp1`) checked 31,925
blocks with zero mismatches, zero interpreted instructions and zero uncovered
entries. AES (`neo-po.bin`) checked 7,687 blocks with zero mismatches, 2,005,898
interpreted instructions and zero uncovered entries. A later AES log showed
8,192,038 interpreted instructions, with 4,440,982 starting at `$1008B0` and
3,700,173 at `$1008AC`; these PCs are in Neo Geo work RAM. The disassembly
printed by the old profile was read at shutdown and can therefore show stale
RAM bytes; capture opcode/disassembly during fallback execution when
investigating this path. The matching MVS run had 51,757 interpreted
instructions and 800,902 native blocks. This AES/MVS comparison is diagnostic
evidence, not yet a confirmed explanation of the boot difference.

Neo Geo runs accept `--dump-fallbacks FILE` to write a text inventory of unique
Musashi-executed PCs with execution counts, the opcode and disassembly captured
on first execution. It includes work RAM, unlike `--dump-misses`, which remains
the ROM entry list for the recompiler. The fallback inventory is diagnostic
only and is not fed into AoT generation automatically; select useful ROM roots
manually with the build command's `--entries` option. Work-RAM code needs a
captured image and a separate design before it can become an AoT input.

AES audio mapping was corrected to match MAME: BIOS/SM1 selects only the fixed
Z80 window `$0000-$7FFF`; all four banked windows read cart M1, with the MAME
`$10000` base and bank mask. The build passes, but AES playback still needs a
listening check.

Historical combined-module runs checked **1,547,511 blocks with zero state
mismatches**, but reported 8,518,982 interpreted instructions and 80 uncovered entry points. At
1,200 AES frames, the same command shape reported 301,956 native blocks,
8,364,258 interpreted instructions and 11 uncovered entry points. Explicit
MVS hardware mode (`--board=mvs`) at 3,600 frames checked 2,075,369 blocks
with zero mismatches, 205,348 interpreted instructions and 79 uncovered entry
points. The large AES fallback workload is the repeated execution of work RAM
around `$1008AC/$1008B0`; it is not explained by the number of ROM entry points
alone. Zero verifier mismatches only covers the
native blocks that ran; it does not validate the millions of interpreter
instructions. The earlier mismatch flood was a separate verifier-fetch bug:
KOF98's protection overlay hid the underlying P-ROM from Musashi's opcode
fetch during replay. The verifier now reads instruction bytes from the P-ROM
beneath that overlay, while data accesses still use the logged board bus.

The generated KOF98 68000 module currently has 60,832 dispatch entries and
1,706,046 instruction occurrences across its routines; generated C occupies
about 300 MiB. The local emitter adds `NG_DEBUG_INSN` to every occurrence,
unlike the compared `kof98recomp` emitter. That is a concrete source-size and
runtime-hook cost candidate, but it has not yet been removed or benchmarked.

The active Z80 production path remains the GPL-3.0-only legacy AoT/runtime.
SuperZazu is an MIT instruction interpreter and is currently diagnostic; it is
not an AoT translator. ZUni80 has instruction decoding, static control-flow
discovery and routine grouping, but no functional native-code emitter/runtime
or production integration yet. Thus the requested MIT-only Z80 production
replacement is not complete. Detailed historical experiments below are kept
as dated evidence; where they conflict with this current-state section, this
section records the latest verified state.

## Reproduce the build

From the repository root, with the user's MAME ROM archives available:

```sh
build/mamerecomp build --machine=neogeo --board=aes --game=kof98 \
  --rom-dir /path/to/kof98.zip --bios-zip /path/to/aes.zip --jobs 2
```

Build the MVS variant separately with `--board=mvs` and
`--bios-zip /path/to/neogeo.zip`. Each build generates
the 68000 AoT under `build/neogeo/kof98/aes/` or `.../mvs/`, compiles into a
distinct CMake target/object directory, and writes its matching game module.
AES uses `neo-po.bin` by default; MVS uses `sp-s2.sp1`. An explicit
`--bios` overrides that mode's default. The shared sound BIOS/cart AoT remains
in `build/neogeo/kof98/z80_generated/`, since the sound ROM inputs are shared.

The 68000 BIOS code is part of each generated AoT. To use UniBIOS, build its
matching AES module and then run with the same `--bios` selection:

```sh
build/mamerecomp build --machine=neogeo --board=aes --game=kof98 \
  --bios=uni-bios_4_0.rom --rom-dir /path/to/kof98.zip
build/mamerecomp run --machine=neogeo --board=aes --game=kof98 \
  --bios=uni-bios_4_0.rom --rom-dir /path/to/kof98.zip
```

That builds and selects `mamerecomp_game_kof98_aes_uni_bios_4_0.so` instead of
reusing the native-AES AoT. This optional module is generated on demand. The
BIOS archive defaults to `aes.zip`; SM1 and other shared system ROMs still come
from `neogeo.zip`.

This common app command dispatches through `tools/recompile.py` to the local
Neo Geo adapter. That adapter currently performs two CMake configure passes
around ROM-derived source generation; it is not yet the generic manifest-driven
generation pipeline used by every board. The stages are:

1. Build `kof98_aot`; validate the TOML ROM profile and prepare the transformed
   68000 image using KOF98's PROGSF1 transform.
2. Generate local 68000 C shards and Z80 BIOS/cart tables under
   `build/neogeo/kof98/`.
3. Configure the generated sources into the build graph, then compile the
   shared Neo Geo runtime and the selected mode's game module.

Run a verified headless session with:

```sh
build/mamerecomp run --machine=neogeo --board=mvs --game=kof98 \
  --headless --frames=3600 --verify
```

Generated sources and extracted ROM data are derived from copyrighted ROMs;
they stay under `build/` and must not be committed or distributed.

## Historical MVS baseline (2026-10)

The local KOF98 and Neo Geo BIOS archives generate **27,226 routines**,
**141,170 dispatch entries**, and about **976,607 translated 68000
instructions**. With MVS watchdog resets and backup RAM persistence enabled,
the native runner now reaches KOF98 attract mode: frame 600 shows the title
screen with “INSERT COIN”, and frame 1,199 shows the attract animation. The
startup warning no longer blocks startup.

The 1,200-frame MVS profile reported 594,306 native blocks and 39 interpreted
instructions. A longer 12,000-frame profile reached 7.87 million native
blocks, 8.52 million interpreted instructions and 283 uncovered ROM entries.
The top fallback volume came from repeated zero-filled work RAM at
`$1008AC/$1008B0`; the ROM entry points are separately recoverable by AOT.
Those 283 roots are now recorded in `tools/neogeo/kof98-runtime-roots.txt` for
the next KOF98-only build.

### Musashi fallback profile (2026-10-31)

`NG_PROFILE=1` reports where interpreted instructions start, and
`--dump-misses FILE` records ROM/RAM targets that have no AOT entry. On a
1,300-frame MVS run using the standard `sp-s2.sp1` AOT build, Musashi executed
18 instructions from work RAM at `$10FC00` (`move.b D0,($3,A1)`), with zero
uncovered entries. Longer runs also reach `$00AE7A` and other BIOS/cart copy,
state and attract routines. A temporary AOT generation with all 283 long-run
roots emitted 27,511 routines, 141,921 dispatch entries and 984,043 translated
instructions; no shared System 16/18 compiler logic or generated build was
changed. The standard KOF98 build script now includes those Neo Geo-specific
roots. The millions of later zero-filled work-RAM instructions are not ROM
roots and still need separate RAM-flow diagnosis.

The UniBIOS AES experiment is a different case: it spent about 8.35 million
interpreted instructions at `$1008AC/$1008B0`, where RAM decodes as repeated
`ori.b #$0,D0` (zero-filled words), and reported one uncovered ROM target
`$00AE7A`. Feeding that target back through `--entries` did not materially
change execution: the large count comes from dispatching into mutable work RAM
that contains no valid initialized code in the current AES path. Treat that as
a runtime/RAM control-flow or boot-state issue, not evidence that the 68000
frontend lacks ORI support. AOTing the single ROM target alone cannot fix the
RAM loop; first compare the path and RAM contents against MAME before adding
dynamic-code handling.

## KOF98 game profile

`src/neogeo/kof98_game.c` owns game-specific behavior: the PROGSF1 program
transform and the initial protection callbacks. The Neo Geo runtime owns the
MVS address map, BIOS startup, LSPC/video raster path, I/O, Z80 audio CPU and
YM2610 sound. Shared profile and ROM loading live in
`src/neogeo/neogeo_romset.cpp`. This keeps later game modules separate from the
System 16/18 board code.

### MVS protection behavior checked against MAME

MAME does not force-accept the cartridge or suppress its board check. Its
`kof98` slot selects `NEOGEO_KOF98`, whose cart device calls the shared
PROGSF1 decryption and exposes the protection chip. The driver routes reads at
`0x100–0x103` and writes at `0x20AAAA–0x20AAAB` to that chip. With no command
written, it returns the original decrypted P-ROM header words (`NEO-`). A
write of `0x0090` changes the header response to `C2FD`; `0x00F0` changes it to
`NEO-` via the second state. These are the chip's expected emulated responses,
not a general bypass routine.

The prototype retains these address routes and responses. The actual boot
block was a missing MVS watchdog, not an unsupported protection opcode: MAME
documents that invalid backup RAM can make protection code initialize SRAM
and wait for a watchdog reset. The runtime previously treated writes to
`$300001` as no-ops. It now models the MAME 3,244,030 master-clock tick period
(1,622,015 of the runtime's 12 MHz 68000 cycles), resets CPU/board state on
expiry, and preserves backup RAM across that reset. The MVS boot now proceeds
past the warning into attract mode.

Before the watchdog fix, interpreted and native runs both stopped at the same
MVS startup warning. The matching behavior pointed to the shared board runtime
rather than the 68000 emitter.

MAME's arcade machine uses a 64 KiB NVRAM-backed `saveram`, defaulting to all
zeroes; writes are accepted only after the system latch unlocks it. The
prototype now loads/saves the same 64 KiB region automatically at
`build/neogeo/kof98/run-assets/kof98/kof98.nvram`, defaults missing files to
zero, and keeps the RAM through watchdog resets. The first successful run wrote
the BIOS marker `BACKUP RAM OK !` into this file. On the next launch with that
file loaded, KOF98 reached the title screen by frame 299. `--nvram FILE` can
select a different save location.

Service/test input is mapped to **F1 or F2** in the window and to `test` in an
input script; `9` remains the service coin input. The service input is read at
the MVS test port, separate from the service coin input.

The upstream NeoGeoRecomp v0.2.0 checkout also explicitly lists protected and
encrypted sets as unsupported in `ROADMAP.md`; its porting guide says to test
the board model interpreted first. It therefore has no existing KOF98 bypass
or protection implementation to reuse. MAME separately defines `kof98h` as an
MVS/AES set using `242-pn1.p1` and `242-mg1.m1`, and registers it on the
generic `neobase` machine path. That is a useful control ROM if available, but
it is a distinct set and not merely a decrypted copy of the protected `kof98`
P-ROM. The current local ROM directory does not contain `kof98h`.

### UniBIOS 4.0 experiment

MAME declares `unibios40` as an alternate 128 KiB `mainbios` ROM
(`uni-bios_4_0.rom`, CRC `a7aab458`). It does not change the `kof98` cart
device selection: `kof98` still installs `NEOGEO_KOF98` and the PROGSF1
protection handlers. The UniBIOS's own menu/check logic is inside that BIOS
binary; MAME's C++ source only registers and loads the ROM, so there is no
MAME-side C++ bypass to copy.

The input loader extracts and CRC-checks UniBIOS 4.0, and the runner accepts
`--bios uni-bios_4_0.rom`. MAME's `neogeo.cpp` documents the hardware-type bit
as AES=0/MVS=1; the BIOS/game code reads this board signal and follows the
corresponding startup path. Since 2026-11-13, the launcher and build default
to UniBIOS 4.0 and AES signals. A 120-frame verified headless run displayed
the UniBIOS screen with `Mode: CONSOLE` and reported zero AoT/interpreter
mismatches. Full AES gameplay has not yet been revalidated.

To explicitly request MVS mode with the MVS BIOS:

```sh
build/mamerecomp run --machine=neogeo --board=mvs --game=kof98 \
  --bios-zip /home/luiz/git/mame/roms/neogeo.zip \
  --bios sp-s2.sp1 --headless --frames 1200
```

The launcher preserves explicitly supplied `--bios` options; choose hardware
mode with `--board=aes|mvs`.
The AES default does not use MVS NVRAM. Window controls include F1/F2 for the
service/test input. The runtime uses the nominal 12 MHz 68000 clock, with no
overclock option.

To build explicitly for another BIOS, pass `--bios`; the normal build now
generates the default UniBIOS 4.0 table automatically:

```sh
python3 tools/neogeo/build.py \
  --rom-zip /path/to/kof98.zip --bios-zip /path/to/neogeo.zip \
  --bios sp-s2.sp1 --build-dir build/neogeo-kof98-mvs
```

Then run the command above. MAME clocks the main 68000 from a 24 MHz master at
`/2` (12 MHz); this runtime also uses nominal 12 MHz guest cycles.

For the next boot milestone, compare the AOT and interpreted CPU state at
frame boundaries, confirm BIOS and cartridge mappings against MAME, then
diagnose the blank later framebuffer across LSPC registers, C-ROM loading and
sprite/palette decoding. Keep `--dump-misses` data with the generated build
artifacts and feed verified roots back into AOT generation.

## Musashi removal plan

1. Track every interpreted PC and classify intentional BIOS/device handoffs
   separately from missed game code.
2. Add and verify roots for reachable untranslated routines, rebuild the
   static table, and inspect fallback counts over deterministic boot captures.
3. Add semantics for any unsupported 68000 instructions discovered in real
   execution, instead of relying on the interpreter for those cases.
4. Retain Musashi only in an optional development comparison harness, then
   remove it from the shipped KOF98 runner when native coverage and state
   comparisons are complete.

## Z80 follow-up

### Multi-system entry point (2026-10-31)

Build selection now starts from a common system/game command:

```sh
python3 tools/recompile.py --system neogeo --game kof98 \
  --rom-zip /path/to/kof98.zip --bios-zip /path/to/neogeo.zip \
  --bios uni-bios_4_0.rom --build-dir build/neogeo-kof98 --jobs 2
```

This dispatches to the Neo Geo/KOF98 backend. The selector also recognizes
`system16`, `system16a`, `system16b`, `system18`, `cps1`, and `cps2`. CPS2 is
forwarded to the local `cps2recomp` make pipeline, with generated work/build
files redirected under this repository's `build/cps2/` tree. Override the
source checkout with `MAMERECOMP_CPS2_SOURCE`; pass its ROM root via
`--rom-dir`. This is an integration bridge while the CPS2 runtime and
frontend are adapted to MameRecomp's common module ABI.

The CPS1 checkout is present at `/home/luiz/git/cps1recomp` and contains an MIT
hardware runtime plus a Z80 interpreter, but it does not contain the ROM
analysis/code-generation scripts described in its README. No clone is needed;
the missing part is the game AOT frontend/driver. System 16/18 already have
board runtimes and game runners but do not yet have a common per-game AOT build
adapter. These backends currently report that boundary explicitly. The next
step is to register ROM loader, CPU frontend, board runtime, and game module
behind this interface as each system is brought into the shared pipeline.

### Categorized Z80 discovery

The KOF98 sound CPU now uses generated native Z80 blocks from separate BIOS
SM1 and cartridge M1 tables, selected by the Neo Geo audio ROM mapping. The
prototype retains the C++ interpreter for uncovered code and dynamic
memory while tracking each interpreted execution by reason. Runtime
reports and `*.fallbacks.log` records include category, BIOS/cart image, PC,
opcode byte, active mapping, and hit count:

- `UNCOVERED_ENTRY`: fixed ROM code reached without a generated block.
- `BANKED_CODE_UNCOVERED`: mapped M1 window code without a generated block.
- `UNSUPPORTED_OPCODE`: a generated block explicitly delegated an instruction
  to the interpreter.
- `DYNAMIC_CODE`: execution from writable memory.
- `INVALID_MAPPING`: the active mapping does not provide a valid code byte.
- `INTERRUPT_BOUNDARY`: interrupt-sensitive instruction boundary requiring
  one-instruction scheduling before checking pending IRQs.

At shutdown, set `NG_Z80_MISSES=build/neogeo-kof98/kof98-z80-misses` to save
BIOS and cart entry PCs plus the categorized report. The next common build
command automatically reads those `.bios.txt` and `.cart.txt` PCs back into
the static frontend, deduplicated with the checked-in roots. Thus each run
feeds newly discovered ROM roots into the next build without copying log lines
by hand. RAM, invalid mappings, and unsupported port instructions remain
categorized for diagnosis and are not blindly promoted to ROM roots.

The latest build generated 3,842 BIOS instructions in 689 blocks and 3,696
cart instructions in 670 blocks. The prior 12,000-frame hybrid run reached a
match with 423,015 native Z80 blocks and 974,832 interpreted instructions;
its new cart roots are now automatically included on rebuild. This confirms
the path remains hybrid, not yet fully native. The 68000 baseline supplied by
the user was 20,785,419 native blocks, 41,950,310 interpreted instructions,
and 222 uncovered entry points; keep that run as the comparison point while
the M68K discovery/fallback taxonomy is added separately.

A fresh 1,200-frame categorized run completed successfully: 693,739 native
68000 blocks, 18 Musashi instructions, and zero uncovered 68000 entries; the
Z80 executed 189,393 native blocks and 213,982 interpreted instructions. The
Z80 breakdown was 176,129 `INTERRUPT_BOUNDARY`, 19,591 `UNSUPPORTED_OPCODE`
(mostly IN/OUT port instructions), 18,127 `DYNAMIC_CODE`, and 135
`UNCOVERED_ENTRY`; BIOS had zero new roots and the cart had 59. This makes the
next sound-CPU tasks measurable: reduce the interrupt-boundary interpreter
path carefully, add native port I/O emission, and decide how mutable RAM code
should be handled.

### Audio regression trace and Neo Geo clock profile

The sound runtime currently has one optional interpreter switch:
`NG_Z80_MODE=legacy` selects the SuperZazu stepper; without it, the runtime
uses generated blocks with the CPS2-derived interpreter fallback.
`NG_YM_TRACE=PATH` records every YM2610 port/value write and
`NG_Z80_STATE_TRACE=PATH` records state at run boundaries. There are no
`interpreter` or `blocks` modes in the current `audio.c` implementation.

A previous 1,200-frame status entry reported 13,559 native/hybrid YM writes,
13,461 SuperZazu writes, and a value mismatch at write 152. This discrepancy
was not reproduced by the 2026-11-13 rerun: the current build generated
13,755 writes in each mode and every port/value pair matched. SuperZazu's
timestamps were 11 cycles earlier at the first write and 34 cycles earlier at
the last; scheduler skew was -15 cycles for SuperZazu and +8 for AoT. The
earlier mismatch remains unexplained, while this current run does not compare
sample output or every Z80 register state.

MAME's Neo Geo profile divides the 24 MHz master clock as follows:

| Device | Clock |
| --- | ---: |
| Master | 24 MHz |
| Motorola 68000 | 12 MHz (`/2`) |
| Zilog Z80 | 4 MHz (`/6`) |
| Yamaha YM2610 | 8 MHz (`/3`) |
| Pixel clock | 6 MHz (`/4`) |

### Audio scheduler and clock audit

The audio runtime derives Z80 time from the shared 68000/master-clock timeline
using the exact 3:1 ratio. It runs the Z80 to the next main-CPU audio-latch
access, frame boundary, or YM2610 timer deadline, whichever comes first. YM
timer status and the Z80 IRQ line are updated at timer deadlines and on timer
control/clear writes. YMFM produces one sample per 144 YM2610 clocks, equivalent
to 72 Z80 clocks (about 55.56 kHz).

MAME uses the same device clocks and requests a 50 µs `perfect_quantum` when
the 68000 writes the sound command latch. The runtime's latch accesses force
an immediate audio catch-up; the frame loop also catches audio up at each
frame boundary. A 1,200-frame KOF98 MVS run ended at 243,302,406 main cycles
and 81,100,805 Z80 cycles, only 3 Z80 cycles apart from the exact 3:1 target.

The Z80 translator counts documented instruction cycle costs and the KOF98
build enables per-instruction budget checks, so an AOT block yields at the
first instruction boundary at or past a device deadline. Native I/O callbacks
are charged the active instruction's cycle cost before the callback, matching
the interpreter's end-of-instruction estimate. This removes the previous
path-dependent timestamp where native callbacks saw committed cycles but
interpreter callbacks did not. It is still not a Z80 T-state bus phase model:
an access may be placed up to one instruction away from its actual I/O phase.
Timer IRQ assertion is surfaced at the next instruction boundary. Compare
against MAME if a remaining audible discrepancy points to sub-instruction
event timing.

The audit run also reported 190,184 native Z80 blocks and 238,398 interpreted
instructions. Thus the oscillator ratio is correct, while Z80 coverage and
instruction-boundary event timing remain active sources of audio differences.

### Z80 I/O coverage and YM write comparison (2026-10-31)

Recent KOF98 miss logs showed that the hottest unsupported Z80 operations were
immediate and ED-prefixed I/O instructions used by the sound driver. The AOT
translator now emits `IN/OUT (n)`, `IN/OUT (C)`, and block-I/O operations.
Their callbacks receive the instruction-end cycle timestamp, matching the
interpreter's callback convention. The delayed instruction after `EI` is also
run through its one-instruction AOT block before IRQ polling, instead of
forcing that common case through the interpreter.

On a 1,200-frame MVS profile, this reduced interpreted Z80 work to 19,735
instructions and `INTERRUPT_BOUNDARY` from roughly 335k events to 2. The run
reported 681,769 native blocks, 19,435 `DYNAMIC_CODE` hits (mostly writable
RAM at PC `$FFFD`), 298 `UNCOVERED_ENTRY` hits, and zero `UNSUPPORTED_OPCODE`
hits; main/Z80 clock skew ended at 5 cycles. The interpreter remains enabled
for dynamic RAM and still-missing roots.

The following longer capture (main CPU 2,237,165,590 cycles; Z80
745,721,871 cycles; skew +8) reported 704,252 native Z80 blocks and 65,260
interpreted instructions. The persisted fallback file sums exactly to that
interpreted count: 56,957 `DYNAMIC_CODE` hits at cart RAM PC `$FFFD`, 8,302
`UNCOVERED_ENTRY` hits in fixed BIOS/cart ROM, and one `INTERRUPT_BOUNDARY`;
there were no unsupported opcodes. These totals are from a longer, different
play/input capture and should not be compared to the 1,200-frame profile as
raw performance rates. The log persisted 74 BIOS and 652 cart roots; the next
build combined them with checked-in roots and generated 763 BIOS and 1,378
cart blocks. The next run should show whether those fixed-ROM fallbacks have
now been absorbed. `$FFFD` remains dynamic RAM and needs a dynamic-code cache
with write invalidation if it is to run natively.

The same capture's 68000 profile shows 8,317,019 of 8,411,500 interpreted
instructions starting at `$1008AC` or `$1008B0`, both in MAME's mirrored work
RAM region `$100000–$1FFFFF`, decoded as repeated `ORI.B #0,D0`. The static
dispatch table intentionally covers fixed ROM, banked ROM and BIOS, not work
RAM, so these executions go to Musashi and are not promotable ROM roots. It
needs a later RAM-code trace to determine whether the game is executing copied
code or whether control has entered zero-filled RAM; dynamic 68000 recompilation
or a fallback is required for this region.

### AOT output size and duplicate code (2026-10-31)

KOF98's program ROM files total 6,291,456 bytes (P1 2 MiB + P2 4 MiB). The
generated 68000 C currently has 142,144 unique instruction-address labels but
987,099 emitted label occurrences: 844,955 copies repeat an address in
overlapping routines, and PC `$011912` appears in 1,554 routine bodies. The
generator's `instructions emitted across routines` count is therefore not a
count of distinct ROM instructions. It is below the ROM byte count, while the
repeated host code is still a significant size cost.

After incorporating the persisted Z80 roots, the linked KOF98 module is about
99 MiB, with approximately 90 MiB in `.text`; the generated C is about 246 MiB
and the current full build tree about 1.1 GiB. The common configuration has an
empty `CMAKE_BUILD_TYPE` and compiles generated C with `-fPIC` but no optimizer
flag. Thus the module being larger than the ROM is partly native instruction
expansion and unoptimized code, and strongly amplified by duplicated guest
instructions across overlapping routines. Next size work should first add a
release/optimized build profile, then restructure code generation to share
overlapping blocks instead of emitting each address in many routine bodies.
Measure runtime and output size after each change; do not treat `strip` as the
main fix (there are no debug sections in this module).

An independent 1,200-frame AOT-vs-core-interpreter comparison produced 12,733
YM2610 port/value writes in each mode, with identical event data/order. The
maximum event timestamp difference was 19 Z80 cycles and final timestamp
difference was -4 cycles. This rules out duplicate or incorrect YM register
writes introduced by the AOT emitter in this capture; it does not yet prove
that the shared Z80 core's event timing matches MAME or hardware.

### YM2610 mixer, precision and clipping audit (2026-10-31)

MAME's raw screen timing is 384×264 total with the visible area bounded by
horizontal blanking 28..347 and vertical blanking 16..239, giving the usual
320×224 Neo Geo game image. The MAME driver uses `NEOGEO_VBSTART=240`; the
16-line vertical blank offset is why the hardware scanline counter and the
224-line visible framebuffer do not start at the same numeric Y. The existing
runtime framebuffer height of 224 is therefore consistent with MAME.

MAME's `neogeo_stereo` routes stream 0 (SSG) to both speakers at 0.84 and
stream 1/2 (FM+ADPCM left/right) at 0.98. YMFM's YM2610 forms the FM stereo
channels, adds ADPCM-A and ADPCM-B to those channels, then clamps those
channels to signed 16-bit in `ymfm_opn.cpp`; output channel 2 is the mono SSG
signal. There are no separate MAME speaker-route gains for ADPCM-A and
ADPCM-B; their levels are controlled by the chip's own register mix.

The old runtime added SSG at unity to the FM+ADPCM channels and clamped the
combined signal directly to int16, omitting MAME's route gains and clipping
before route attenuation. That sum was safe from int32 overflow (only a few
int16-scale values are added), and the final clamp saturated rather than
wrapping, but it did not match MAME's mix. The runtime now applies 0.98 to
FM+ADPCM and 0.84 to SSG independently, accumulates in int64, and saturates only at
the PCM output boundary. This follows MAME's float-route ordering to within
integer quantization. A listening comparison is still needed to judge the
perceptual result.

I also installed a MAME Lua debugger watchpoint on the Z80 I/O space. The
initial headless MAME capture repeatedly reset at the watchdog and yielded
only zero-valued I/O events, so it is not a valid sound-driver comparison.
Use the AOT/interpreter YM trace above as the current evidence for write
integrity; a useful MAME trace still needs a stable MVS boot and live sound
command sequence.

These rates are declared in the Neo Geo runtime's local timing header, and the
Z80 divider and YM sample rate derive from them. A 1,200-frame run ended with
4 cycles of skew in native/hybrid mode and 9 in the legacy diagnostic. Keep
these constants scoped to Neo Geo; System16, CPS1, and CPS2 need their own
board clock profiles in the common multi-target interface.

### Remaining Neo Geo instruction engines and devices

The Z80 and 68000 are the instruction-executing processors in this KOF98
pipeline. YM2610/YMFM, LSPC video, timers, memory mapping, input, backup RAM,
and protection are device models called by the guest CPUs; they do not have
guest opcodes to AOT. The next device audit should inventory the active Neo
Geo board models and identify CPU-like devices (for example, any MCU/MCU
firmware present in other cartridge profiles) separately from these hardware
models. KOF98's current scope includes no additional firmware CPU beyond its
68000 and Z80.

The historical KOF98 build and 12,000-frame MVS run passed with the generated 68000
dispatch table loaded from `mamerecomp_game_kof98.so`; frame 11,999 shows a
live match. That run reports 7,715,041 native 68000 blocks and 8,369,210
Musashi instructions, with zero uncovered entry points. The remaining
interpreted work is 68000 execution from mutable work RAM, not Z80 coverage.

### Z80 toolchain and remaining license work

The current pipeline uses local files: `tools/z80/recomp.py` and
`tools/z80/decoder.py` generate BIOS/cart C++ shards, while
`core/third_party/z80-aot/` provides the native execution runtime and
interpreter fallback. These files carry `GPL-3.0-only` SPDX identifiers, and
the generated Z80 code is linked into `mamerecomp_game_kof98.so`. This does
not meet the user's MIT-only distribution goal; a separately implemented,
license-compatible Z80 frontend/runtime is still required before distribution.
The local SuperZazu `z80_step()` implementation is MIT-licensed and remains a
reference path. Its KOF98 port/value trace currently matches the default path,
but equivalent cycle-by-cycle state and performance have not been established.
YM2610 is a device model provided by local YMFM; the KOF98 Z80 driver programs
it through the board's port, clock and IRQ wiring.

External checkouts of NeoGeoRecomp, GenesisRecomp and CPS2Recomp mentioned in
older research notes are not build inputs. Their hardware-specific adapters
cannot simply be linked as the Neo Geo runtime. KOF98's Z80 path keeps the
runtime responsible for M1 fixed/banked windows, RAM, YM2610 ports, cycle
accounting, and NMI/IRQ delivery. The active per-game package still combines
the 68000 and Z80 generated artifacts in one module; a common manifest-driven
pipeline and the independent MIT Z80 replacement remain unfinished.
