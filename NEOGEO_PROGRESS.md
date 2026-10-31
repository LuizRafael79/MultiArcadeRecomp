# Neo Geo KOF98 static recompilation prototype

KOF98 is the first game-specific module for extending the existing
NeoGeoRecomp MVS runtime. This is a static recompilation prototype, not a new
68000 emulator: the 68000 frontend translates the BIOS and cartridge ROM to C,
the generated C is compiled into `kof98recomp`, and the board runtime executes
those native functions. At present, Musashi is retained only for addresses
without generated entries. The longer-term goal is to eliminate this fallback
by improving static discovery and translating uncovered code.

## Reproduce the build

From the repository root, with the user's MAME ROM archives available:

```sh
python3 tools/neogeo/build_kof98_prototype.py \
  --rom-zip /path/to/kof98.zip \
  --bios-zip /path/to/neogeo.zip \
  --build-dir build/neogeo-kof98 --jobs 2
```

The script performs these steps:

1. Configure and build `kof98_aot`, which uses the KOF98 profile and existing
   68000 frontend.
2. Validate ROM names, lengths and CRCs, extract the BIOS and game assets, and
   apply the KOF98 PROGSF1 transform adapted from MAME's
   `src/devices/bus/neogeo/prot_kof98.cpp`.
3. Discover code in the MVS BIOS and transformed KOF98 P ROM, then emit C
   shards and a dispatch table under `build/neogeo-kof98/neogeo/kof98/generated`.
4. Configure the generated sources into `kof98recomp` and compile them as
   ordinary native C code.

Run a headless framebuffer capture with:

```sh
build/neogeo-kof98/kof98recomp \
  --rom-zip /path/to/kof98.zip --bios-zip /path/to/neogeo.zip \
  --headless --frames 300 --screenshot 299:/tmp/kof98.png
```

Generated sources and extracted ROM data are derived from copyrighted ROMs;
they stay under `build/` and must not be committed or distributed.

## Current prototype result

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

The input loader now extracts and CRC-checks UniBIOS 4.0, and the runner accepts
`--bios uni-bios_4_0.rom`. MAME's `neogeo.cpp` documents the hardware-type bit
as AES=0/MVS=1; the BIOS/game code reads this board signal and follows the
corresponding startup path. `--system` selects which hardware signals the
runtime presents for comparison. AES mode remains available, while the default
MVS mode now boots the protected KOF98 set without forcing a protection
response or patching the P-ROM.

For the current MVS boot, use the standard static build, default BIOS and
default MVS board signals:

```sh
build/neogeo-kof98/kof98recomp \
  --rom-zip /home/luiz/git/mame/roms/kof98.zip \
  --bios-zip /home/luiz/git/mame/roms/neogeo.zip \
  --headless --frames 1200
```

The launcher preserves an explicitly supplied `--bios`; its default remains
`sp-s2.sp1`. It creates or loads the MVS NVRAM file automatically. Window
controls include F1/F2 for the service/test input. The runtime uses the
nominal 12 MHz 68000 clock, with no overclock option.

The test initially ran under Musashi because the existing AOT table covers
`sp-s2.sp1`, not UniBIOS. The build script now accepts `--bios` so a separate
static table can include UniBIOS 4.0; build it with:

```sh
python3 tools/neogeo/build_kof98_prototype.py \
  --rom-zip /path/to/kof98.zip --bios-zip /path/to/neogeo.zip \
  --bios uni-bios_4_0.rom --build-dir build/neogeo-kof98-aes
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
  --bios sp-s2.sp1 --build-dir build/neogeo-kof98 --jobs 2
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

The sound runtime has diagnostic-only `NG_Z80_MODE=legacy` (the former
SuperZazu stepper), `NG_Z80_MODE=interpreter` (the new interpreter one
instruction at a time), and `NG_Z80_MODE=blocks` (the new interpreter using
AOT block boundaries). `NG_YM_TRACE=PATH` records every YM2610 port/value
write; `NG_Z80_STATE_TRACE=PATH` records Z80 state at run boundaries. The
default remains native/hybrid.

A deterministic 1,200-frame comparison showed native/hybrid and the new
interpreter produce the same 13,559 YM register writes when generated blocks
yield at instruction boundaries. The former SuperZazu diagnostic produces
13,461 writes and diverges at write 152. Since generated code matches its own
interpreter, this points to a behavior/timing difference in the replacement
Z80 core or its interrupt/command handling, rather than an AOT emitter
mismatch.

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

The KOF98 build and 12,000-frame MVS run now pass with the generated 68000
dispatch table loaded from `mamerecomp_game_kof98.so`; frame 11,999 shows a
live match. That run reports 7,715,041 native 68000 blocks and 8,369,210
Musashi instructions, with zero uncovered entry points. The remaining
interpreted work is 68000 execution from mutable work RAM, not Z80 coverage.

The next KOF98 milestone is hybrid static Z80 recompilation. Start with
native code for the instructions the frontend covers and retain the existing
interpreter for uncovered instructions, with fallback PCs counted. If a
fallback causes a KOF98 failure or blocks useful coverage, add its native
semantics and rebuild. The existing NeoGeo runtime currently calls
`z80_step()` in `core/third_party/neogeorecomp-runtime/src/audio.c`.

Local source inventory:

- `/home/luiz/git/neogeorecomp` contains the SuperZazu Z80 interpreter and
  YMFM's YM2610 chip model, but no Z80 static-recompiler frontend.
- `/home/luiz/git/segagenesisrecomp` contains the scheduler adapter and the
  shared `z80-recomp-core` state/semantic headers. Its `SmsRecomp --flat-step`
  decoder/emitter source is maintained in the separate `smsggrecomp` project
  and is not present in the local checkout. The Genesis adapter itself has
  Genesis-specific bus and interrupt behavior, so it cannot be linked as the
  Neo Geo implementation.
- `/home/luiz/git/cps2recomp/tools/recompz80.py` and `tools/z80.py` contain a
  real recursive-descent Z80-to-C++ frontend. Running it on KOF98's
  `242-m1.m1` in `/tmp` found 3,026 instructions, 547 blocks, and 72 routines;
  it left 39 reachable instructions to its interpreter, all `IN`/`OUT` port
  instructions. Its runtime only indexes the fixed `$0000-$7FFF` image and
  its generated code is coupled to CPS2's `Z80` class and QSound bus, so the
  frontend is a useful coverage/workflow reference but needs a Neo Geo address
  map, bank handling, and YM2610 port adapter.
- The CPS2 frontend/runtime files are marked GPL-3.0-only, while this project
  is GPL-2.0-only. Keep its source out of this tree; implement the Neo Geo
  adapter and any missing semantics here, or use separately generated output
  only after its licensing is checked. GNU's compatibility guidance says
  GPL-3.0-only code cannot be combined with GPL-2.0-only code.
- The YM2610 model is already compiled into native C++ through YMFM. Its chip
  implementation is not interpreted bytecode. The static-recompilation work
  here targets the Z80 sound driver; generated instructions will call the
  existing Neo Geo port and timing interface to program YM2610.

The KOF98 M1 ROM is the first Z80 image. Its 256 KiB physical bank data must
be translated with the Neo Geo's fixed and switchable M1 windows in mind; the
runtime remains responsible for bank selection, Z80 RAM, YM2610 ports, cycle
accounting, and NMI/IRQ delivery. Start from the CPS2 frontend's proven
instruction grouping and coverage result, but keep the implementation and
runtime glue Neo Geo-specific. Initially retain the current interpreter for
reachable instructions the generated code does not yet cover, while tracking
fallback PCs; add those instruction semantics if they cause a KOF98 failure
or prevent useful native coverage. Generated Z80 code belongs to the KOF98
game module and must follow the module ABI and packaging rules in
`ARCHITECTURE.md`. After this path boots and audio is validated, apply the same
process to the 68000 and remove Musashi.
