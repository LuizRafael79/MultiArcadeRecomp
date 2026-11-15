# CPS-1/2 progress

## Status: prototipagem

`sf2ce` permanece congelado como protótipo. O usuário retomou explicitamente
Cadillacs and Dinosaurs (`dino`) para revisar o runtime CPS1/QSound e Kabuki;
os resultados abaixo não representam suporte completo. A CPU 68000 continua
no frontend compartilhado atual, sem retrabalho nesta etapa. Pendências em
[TODO.md](../../../TODO.md).

## Separation and current state

CPS builds use `tools/recompile.py --system cps1|cps2 --game ...`; generated code,
work files, and binaries stay under `build/cps1/` or `build/cps2/`. Neo Geo is
frozen at its current KOF98 prototype. CPS-1 and CPS-2 share CPU recompiler
work where it fits, but each board and its audio/video devices have separate
runtime profiles.

### CPS-2 / per-game module checkpoint (2026-11-04)

The public command now accepts `multiarcaderecomp build/run --machine=cps
--board=cps2 --game=sfa3`; the build delegates to the external CPS2Recomp
checkout and the run launches its standalone executable. SFA3 was built from
`/home/luiz/git/mame/roms/sfa3.zip` and the public headless command completed
120 frames. CPS2Recomp's `check-blocks` also completed a 120-frame comparison
of translated M68K and Z80 blocks against its interpreter.

The shared local M68K generator was compared against CPS2Recomp's SFA3 output.
With the `cps2` board profile it covered 106,850 of 106,946 CPS2Recomp
instruction PCs, while also emitting about 21.5K additional dispatch PCs and
more duplicated routine instructions. This is promising coverage, not yet
proof of equivalent game execution: the local output has not been connected
to the CPS-2 runner and validated through boot/gameplay. In the matched Z80
comparison, the local generator covered all 349 CPS2Recomp instruction
addresses and emitted three additional boundaries for precise cycle yielding.

SFA3, SFZ2A and SFZ2AL now build as separate loaded board modules through
`multiarcaderecomp build --machine=cps --board=cps2 --game=<game>`, and run in-process
through the same host entry point. Each game has its own build/work tree and
`.so`; shared CPS-2 runtime and game-family sources are reused. `sfz2a` is a
local profile that shares SFA2 gameplay sources, while `sfz2al` uses the
upstream profile. MAME identifies `sfz2a` as Street Fighter Zero 2 (Asia
960227) and `sfz2al` as Street Fighter Zero 2 Alpha (Asia 960826), so they are
distinct sets rather than duplicate region labels. The upstream SDL frontend
and F1 menu remain available.

HSF2 (Hyper Street Fighter II: The Anniversary Edition, USA 040202) now uses
the recipe added to the current CPS2Recomp checkout. MAME verifies the local
`hsf2.zip` set and its key CRC `fc9b18c9`; this is HSF2, not Super Street
Fighter II Turbo. The MultiArcadeRecomp module includes the shared CPS2 board runtime
and generated program-ROM AOT, while `runtime/no_game.cpp` registers no
CPS2Recomp game-specific rewrites or modes. The generator reported 115,646
68000 instructions in 80 C++ units and 1,358 Z80 instructions. The resulting
`libmultiarcaderecomp_cps2_hsf2.so` is 15,714,264 bytes. A headless 1,800-frame run
completed: 68000 AOT 42,140,651 / fallback 4,804,835; Z80 AOT 34,512,997 /
fallback 196,061. The extra entries found during the run were saved to HSF2's
own work tree. This validates execution for that run length, not full-game
playability or visual/audio parity.

The current external CPS2Recomp checkout is at `b51892d` (already up to date
after the user's pull). Comparison with the previous local checkout showed no
changes to its core M68K/Z80 generator or interpreter files; recent changes
are concentrated in runtime/build plumbing and game profiles, including HSF2.
Thus the updated HSF2 recipe is being used, but there is no evidence in this
update that the underlying CPU recompilers became more mature.

Headless 1,800-frame runs completed for both new sets. The first SFZ2A run
logged opcode `0xffe4` at odd PC `0x001991`. MAME's ROM declaration confirms
all configured ROM CRCs and key CRC `777b7358`; MAME reports the set as good
and its driver as good. The cause was an SFA2 `FrameRegisters` rewrite attached
at SFA2 address `0x13CE`, where SFZ2A has an unrelated `moveq`; the matching
SFZ2A routine begins at `0x13D2`. `--check-rewrites` exposed the mismatch in
all 199,363 calls. SFZ2A's profile now excludes only the shared `input.cpp`
rewrite and runs the original ROM routine through AOT. A fresh 1,800-frame
MultiArcadeRecomp run completed without the illegal-opcode message: 48.94M native
68000 instructions, 29K fallback; 32.82M native Z80 instructions, 229K
fallback. This confirms the immediate boot-loop cause, not full gameplay
parity. The per-game `family.h` mapping is still a follow-up: CPS2Recomp's
`kPlaces` maps code/data addresses shared by source files, while opcode
decoding lives in `tools/m68k.py` and discovery/emission in `tools/recomp.py`.

SFZ2AL's 1,800-frame run reached about 43.83M native and 4.48M fallback
68000 instructions, plus 32.81M native and 233K fallback Z80 instructions.
It collected additional dynamic entries during the run, so another build can
improve static coverage. All instruction totals are runtime counters, not
proof of full gameplay parity.

The upstream runtime already supplies `Machine`, the 68000 bus/map and frame
timing, Z80/QSound, video, EEPROM, ROM loading/decryption and interrupt
wiring. It currently runs with CPS2Recomp's own M68K and Z80 AOT runtimes and
generators. Although the local M68K and Z80 generators compare competitively
in static coverage, neither is connected to this runtime's state/dispatch ABI
yet. Adapting and validating the shared local 68000 generator is still open;
do not claim the cross-board CPU-sharing goal is met for CPS2 until then.

The runtime is not wholesale MAME code. CPS2Recomp marks most machine/board
code GPL-3.0-only and credits MAME-derived portions such as the CPS-2 cipher
and QSound rewrite under BSD-3-Clause. The project license at `docs/license/multiarcaderecomp/LICENSE.md` is
GPL-2.0-only, so the combined module must not be treated as distributable
under the current license. The user has authorized local integration and will
provide a license; preserve upstream attribution and resolve the final license
before redistribution. Details are in [docs/systems/capcom/cps/UPSTREAM.md](UPSTREAM.md).

Cadillacs and Dinosaurs (`dino`) is a CPS-1 board with QSound and a Kabuki
encrypted sound Z80. Its sound-ROM extraction, Kabuki opcode view, and native
Z80 generation are now handled by the local CPS1 build path; the old CPS2Recomp
board execution path is not used. Its sound path is QSound, not YM2151/OKI.

### Dino runtime checkpoint (2026-11-02)

The MAME `dino_decode(0x76543210, 0x24601357, 0x4343, 0x43)` keys are applied
before translation to the fixed 0x8000-byte Z80 ROM. Generation supplies the
decoded opcode view to instruction discovery and AOT, and the decoded data
view for immediates/ordinary reads. The runtime Z80 bus uses those same two
views in native execution and interpreter fallback. A 12M-cycle forced-
interpreter comparison matched AOT's framebuffer SHA-256 and QSound writes
(360 writes, 24,037 generated samples); the interpreter executed 792,945 Z80
instructions, while the AOT run used 63,207 native blocks. At 600M cycles the
native run reached the Capcom logo with 46,512,211 native 68000 instructions,
zero Musashi instructions, 8,742,380 native Z80 blocks, zero interpreted Z80
instructions, 1,008 QSound writes and 1,201,899 generated samples.

The QSound main map now handles the MAME IN2/IN3 reads, coin control writes,
EEPROM serial lines and the 93C46 8-bit serial storage (optional persisted
image via `--eeprom`). The framebuffer is available in PPM and SDL window mode
(`--window`). The EEPROM serial timing/persistence has not yet been compared
against MAME, and the game has not been confirmed through attract/gameplay.

The first post-wiring captures still showed isolated sprites on black because
the renderer ignored the driver's `mapper_CD63B` and applied the SF2CE
`S9263B` mapping ranges. The MAME CD63B mapper exposes two 0x8000 banks to all
three scroll layers and sprites, with per-type address shifts. That mapping is
now selected from the game profile. A fresh 600M-cycle capture displays the
green background, Capcom logo and disclaimer; `video-pens` rose from 20,061 to
215,732 in the comparable run. This fixes the missing tilemap banks, not the
separate priority/composition concerns also seen in SF2CE. The shared
compositor still needs pixel-level comparison with MAME for both games.

### Shared sprite/tile composition follow-up (2026-11-02)

The attempt to invert CPS-B priority polarity was wrong and was reverted: MAME
passes `priority_reg ^ 0xffff` as a *transparent-pen* mask to `set_transmask`,
so set bits in the register become opaque layer-0 pens. The user's gameplay
capture showed the inverse made the player disappear behind the foreground.
The normal sprite-list traversal (entry 0 forward) also matches MAME, but the
pixel write rule was incomplete: MAME's `prio_transpen` marks each opaque
sprite pixel with priority 31, and its implicit mask bit 31 prevents later
objects from overwriting it. MultiArcadeRecomp previously overwrote those pixels,
which put the 1P marker behind Hannah. The CPS1 compositor now preserves the
first opaque sprite pixel, while still honoring the preceding tilemap's high
priority mask.

An automated MAME run (one coin + 1P start + one right tap) captured the Dino
character-select screen at frames 400 and 500. The object list is identical in
MAME and MultiArcadeRecomp at frame 500, as are `control=06d6`, order `3,2,1,0`, video
control `000e` and priority words `0000,7e01,00ff,01ff`. With the per-pixel
sprite priority behavior restored, the 1P marker appears over Hannah's arm as
in MAME. References are `/tmp/dino-mame-select-0500.png` and
`/tmp/dino-recomp-select-hannah-fixed.png`; the MAME trace helper is
`tools/cps1/mame_dino_composition_trace.lua`. Dino gameplay and the separate
SF2CE composition mismatch still need validation.

Earlier MAME select capture frame 400 reports
`control=06d6`, order `3,2,1,0`, video control `000e` and priority words
`0000,7e01,00ff,01ff`; MultiArcadeRecomp reports the same values.

## Street Fighter II: Champion Edition (`sf2ce`)

### Current prototype status (2026-10-31)

`cps1_boot` now accepts the extracted 6 MiB graphics region and writes a
384x224 PPM framebuffer after the requested CPU cycle budget. The initial
renderer decodes CPS-1 8x8/16x16/32x32 graphics, tile maps, palette RAM and
buffered sprite RAM. Its first output is currently black: CPU execution reaches
the expected startup loop and writes CPS-A/CPS-B registers, gfx RAM, YM2151
registers and sound commands, but the renderer's palette/layer interpretation
does not yet produce visible pixels. This is a visible integration checkpoint,
not a successful game boot.

The latest 120M-cycle run had 14,746,223 native 68000 instructions, 466,103
native 68000 blocks, zero Musashi instructions, 466,300 native Z80 blocks and
zero interpreted Z80 instructions for the paths reached in that run. The bus
reported 67 nonzero YM register values and sound command `$f0`. Those writes
are captured only: YMFM generation and an OKI MSM6295 core are not yet wired to
the CPS-1 runner, despite the shared project already having a YM2151 engine.
The renderer also needs validation against MAME's CPS-A palette copy timing,
tile priorities and sprite buffering. Current generated image:
`build/cps1/work/sf2ce/sf2ce-frame.ppm` (converted preview:
`build/cps1/work/sf2ce/sf2ce-frame.png`).

The runner uses the full 0x40000-byte CPS-A gfx-RAM address aperture for video
base registers while exposing the mapped 0x30000-byte CPU window, matching the
masking behavior in MAME's `cps1_base`.

The startup graphics-RAM diagnostic stays on the real Musashi fallback. No
CPU-side shortcut is used for that test; palette timing, sprite priorities and
the exact RAM test still need the full hardware implementation in the next
video pass.

The current pass now follows MAME's CPS-B-21 layer-control masks for SF2CE
(`0x02`, `0x04`, `0x08`, `0x30`) and its four priority slots, and applies the
CPS-A video-control bits when enabling the 16x16 and 32x32 tilemaps. CPU
fallback behavior is unchanged and still executes individual missing 68000 or
Z80 instructions through the respective interpreter.

The CPS-1 tilemap details were cross-checked against the local `cps1recomp`
video implementation: SF2CE Scroll 1 `$40xx` entries are 16x16 glyphs built
from four 8x8 graphics tiles, and the title/test text uses an unscrolled
Scroll-1 view. That behavior is now implemented in the MultiArcadeRecomp renderer;
the board bus and CPU execution remain MultiArcadeRecomp code.

`cps1_boot` has an optional SDL3 live presenter. Passing `--window` after the
frame path updates a 768x448 window from the same 384x224 framebuffer used for
PPM capture at each VBlank. The normal build driver leaves the window off while
the CPU/video pipeline is still being debugged. Headless runs remain the
reference path while CPS-A/CPS-B is being completed.

The CPS-1 pipeline source is `tools/generators/cps1/build.py`; `tools/recompile.py`
dispatches to it, while the old `tools/cps1/build.py` path remains a thin
compatibility entry point. It validates/extracts the main and audio ROM regions, generates
an entry-reachable 68000 prototype, and statically translates the fixed Z80 program using the shared MultiArcadeRecomp Z80
recompiler (`tools/z80/recomp.py`). It does not use a CPS1Recomp/CPS2Recomp Z80
runtime. ROM sizes and CRC32 values are checked against MAME before generation.

The 68000 translator consumes the MAME program ROMs and starts from the reset
PC. Pointer scanning remains disabled because broad scanning can interpret data
as code. Dynamic entries discovered by the runner are fed into subsequent
generation passes. Generated 68000 routines/table and the generated Z80 code
are compiled into `libmultiarcaderecomp_game_<game>.so`, with a small game ABI adapter.
The board runtime is `libmultiarcaderecomp_cps1.so`; both depend on shared CPU runtime
libraries. `multiarcaderecomp_<game>` remains a small runner that uses those modules.
ROM bytes stay external. The CPS-1 runtime adapts the imported `ng_*` ABI
without linking the Neo Geo board runtime. Musashi remains the fallback for
uncovered instructions and code reached dynamically in RAM.

The first full SF2CE translation emitted 82,931 instructions across 5,174
routines and 52,090 dispatch addresses. This was excessive: `--no-pointer-scan`
only disables a full-ROM scan, while the shared Neo Geo frontend still inferred
code pointers from PC-relative data references and scanned indirect jump
tables. The raw recompiler now accepts `--board cps1`; its conservative policy
uses direct control flow and runtime-observed entry addresses. The `neogeo`
policy keeps its indirect pointer/jump-table scans, and raw builds default to
the conservative `generic` policy. Starting with 49 observed entries already
in the worklist, SF2CE emitted 1,180 instructions, 51 routines and 325 dispatch
entries. One discovery pass added observed PCs and reached 2,185 instructions,
97 routines and 533 dispatch entries; a 12M-cycle run then had zero
Musashi/Z80 interpreter instructions and zero uncovered ROM entries. Its
framebuffer hash matched the previous oversized build, so reducing false code
discovery did not itself fix the independent layer-order issue.

The first run generated 1,448 Z80 instructions (2,898 bytes) in 244 blocks and
37 routines, with zero instructions delegated to the Z80 interpreter. Sources
are under `build/cps1/work/sf2ce/gen/z80/`; the shared target is
`multiarcaderecomp_z80_aot`, with no system-specific CPU target. The Z80 ROM's second 0x8000 bytes
are bank data mapped through the CPS-1 sound program's bank window; they are
preserved separately at `build/cps1/work/sf2ce/z80_banked.bin`. The two 0x20000
sample ROMs are concatenated in board order at `build/cps1/work/sf2ce/oki_samples.bin`.

Reproduce the generation step:

```sh
python3 tools/recompile.py --system cps1 --game sf2ce \
  --rom-dir /home/luiz/git/mame/roms/sf2ce.zip
```

### Clock profile and audio status

MAME's SF2CE machine configuration is `cps1_12MHz`: main 68000 at
12 MHz, Z80 at 3.579545 MHz, YM2151 at 3.579545 MHz, OKI MSM6295 at 1 MHz
(16 MHz / 4 / 4), and CPS pixel clock at 8 MHz. The raw screen timing is
512x262, giving 59.637 Hz, 201,216 68000 cycles per frame, and VBlank beginning
at 184,320 cycles. Other CPS1 boards can differ; QSound configurations in MAME,
for example, use a 12 MHz 68000 and 8 MHz Z80.

The runner had mistakenly used 10 MHz for SF2CE's 68000 and for the Z80/main
clock ratio. Both now use the standard SF2CE values above. Window presentation
is paced to the CPS pixel-derived frame period; a 2M-cycle window probe took
0.256 seconds including startup, close to the expected 0.2 seconds of guest
time. The Z80 translator runs natively at the correct ratio. YM2151 and OKI are
still bus stubs here: register writes are counted, but neither audio device is
currently clocked or producing sound in this CPS1 runner.

The earlier 10 MHz screenshots and cycle labels in this note predate the clock
correction and must not be used for frame-for-frame timing comparisons.

### Work remaining for a solid `sf2ce` prototype

- Continue dynamic 68000 discovery for the graphics-RAM/runtime entry points;
  Musashi still handles a small set of instructions/entries during gameplay.
- Compare CPS-A/CPS-B composition, per-layer alignment, sprite priorities, and
  attract timing against MAME beyond the title logo.
- Implement and connect YM2151 and OKI devices, then synchronize writes and
  sample playback to sound-Z80 timestamps.
- Verify sustained attract/gameplay, input handling, and audio against MAME.

The shared CPU/fallback contract and the System 16/18 migration status are in
[Z80 backend](../../../hardware/cpus/z80/README.md).

## MAME findings

MAME's `sf2ce` profile uses the normal CPS-1 68000 map and has no main-program
cipher. It declares three 0x80000 program ROMs as word-swapped data, an audio
region with 0x8000 fixed Z80 bytes and 0x8000 banked bytes loaded at region
offset 0x10000, and two 0x20000 OKI sample ROMs. Its 68000 code can execute from
graphics RAM at `0x900000-0x92ffff`; a ROM-only static scan cannot discover all
such runtime code, so dynamic entry discovery or an explicit fallback is
needed for the main CPU.

MAME source: `/home/luiz/git/mame/src/mame/capcom/cps1.cpp`, `ROM_START(sf2ce)`,
`machine_start`, and the CPS-1 sound/memory maps.

### Current SF2CE POST behavior

The native runner is now reaching the CPS-1 RAM POST with zero Musashi
instructions and zero uncovered ROM entries. SF2CE writes and verifies several
large regions before it initializes the tilemaps; therefore an early PPM can be
black even though the CPU is progressing correctly. MAME's `cps1_v.cpp` calls
these checks board self-tests and notes that some variants take effectively
forever. The runner lets native routines run up to the next exact VBlank boundary while preserving the
exact VBlank boundary, reducing dispatch overhead without skipping instructions.
For visual probing, pass `--until-video` after the PPM path. The runner probes
at VBlank boundaries but writes the PPM only after non-transparent tile pens
are produced; a POST-only budget reports that no video target was reached
instead of leaving a misleading black capture.

The renderer audit against MAME also removed a CPS1Recomp-only Scroll-1 glyph
special case (MAME treats `$4000-$4fff` as normal banked Scroll-1 codes),
corrected Scroll-2/Scroll-3 video-control gating to bits 2/3, and made
horizontal offsets profile-specific. SF2CE has zero scroll kludge offsets in
MAME; the `-0x10` values apply to selected conversion/bootleg configurations.

### SF2CE boot and CPS-B compositor (in progress)

The POST restart cause was an incorrect VBlank interrupt level. MAME disables
the 68000 interrupt mixer for CPS1; its asserted `M68K_IRQ_IPL1` input is thus
priority level 2. The runner previously delivered level 1/vector 25. It now
delivers level 2/vector 26 and uses MAME's `autovector(offset + 1)` mapping for
CPU-space acknowledge reads. The repeating reset stopped: a 120M-cycle run now
reports one `TRAP #$e`, zero uncovered ROM entries, zero Musashi instructions,
and reaches the disclaimer.

The compositor now clears with CPS1 backdrop pen `$bff`, treats tile pen 15 as
transparent, follows the CPS-B layer order and enable bits, uses the SF2CE
CPS-B-21 priority registers to mark high-priority tile pixels that mask sprites,
and walks the buffered CPS-A object table in MAME order with 9-bit coordinate
wrap. It maps each sprite base through the S9263B PAL before expanding `nx × ny`
tile blocks, and applies CPS-A OTHER-RAM row scroll to Scroll2 when video-control
bit 0 is set. MAME writes zero to SF2CE's four priority-mask registers, so the
priority bitmap does not hide tiles in this set; sprite order and CPS-B layer
order remain active.

At 300M 68000 cycles (30 seconds), the AOT runner produces the full SF2CE title
logo; the frame differs from MAME only at the blinking `INSERT COIN` message.
The interleaved logo during its animation is normal, as confirmed by the user.
At 500M cycles the runner reaches the Air Force attract round. In the 50-second
MAME comparison, the sky and aircraft pixels match exactly; remaining frame
differences are concentrated around the animated fighters, crowd, and temporary
`INSERT COIN` overlay. At 600M cycles it reaches the fight; 34 new 68000 entry
points were uncovered and 30,406 instructions used Musashi. The user's title
camera/building transition and reported fighter/crowd depth still need a direct
frame-by-frame comparison. `MULTIARCADERECOMP_CPS1_DUMP_LAYERS=/tmp/prefix` writes the
four intermediate compositor layers for diagnosis. SDL window pacing is tied
to the 59.637 Hz hardware frame cadence rather than host speed.

The title/building transition diagnostic shows three visible depth groups
split across the CPS planes: the crowd is in the buffered sprite list, while
MAME draws the foreground fighter artwork on Scroll 1. Scroll 2 and
Scroll 3 carry the sky/building/bush scenery behind them. This produces the
three visible depth groups the user pointed out (scenery, crowd, fighters),
with the scenery itself split across two tilemaps.

The MAME `render_layers` sequence for the title animation is CPS-B `$26 =
$12ce`: Scroll 3, Scroll 2, sprites, then Scroll 1. The last Scroll-1 pass
puts the fighters in front of the crowd. The native run later writes `$078e`,
which decodes as Scroll 2, Scroll 3, Scroll 1, then sprites and therefore puts
the crowd in front. The ROM does this by storing `$078e` at work RAM `$ff804e`
from code at `$671a`; subsequent code copies that word to CPS-B `$26`. MAME's
frame-900 sample remains at `$12ce`, so the next investigation is why the
native CPU path reaches the alternate layer setup. The normal MAME sprite-list
walk itself is forward in both implementations. Set
`MULTIARCADERECOMP_CPS1_TRACE_LAYER_CONTROL=1` to log CPS-B `$26` writes and the
work-RAM writes that supply the layer-control word.

The CPS-1 board renderer exposes three tilemaps plus sprites, so the Russia and
Guile stage comparison should use the same four layer dumps and log each
game's CPS-B order register. That stage-by-stage capture is still pending; the
title animation already confirms why the order must be taken from the game's
register writes rather than hard-coded by stage name.

#### Layer-order diagnosis

The local CPS compositor extracts the four two-bit layer selectors from
CPS-B `$26` and executes them in that order. This matches MAME's
`cps_state::render_layers` in `src/mame/capcom/cps1_v.cpp`, including preparing
the preceding tile layer's high-priority mask immediately before a sprite pass.
For SF2CE, MAME's `CPS_B_21_DEF` also places the layer-control register at `$26`
and uses enable masks `$02/$04/$08`, matching this runner's `$800166` mapping.

The title depth change is therefore driven by guest-visible state: MAME's
reference frame uses `$12ce` (Scroll 3, Scroll 2, sprites, Scroll 1), while the
native run later writes `$078e` (Scroll 2, Scroll 3, Scroll 1, sprites). The ROM
stores that second value at work RAM `$ff804e` from code at `$671a`, then copies
it to CPS-B `$26`. This is evidence of a CPU execution/state divergence or a
frame/timing mismatch relative to the MAME capture; it is not evidence that the
compositor is randomly reordering layers. Compare both emulators at the same
guest frame and inspect the registers and flags at `$671a` before changing the
layer renderer.

The local `cps1recomp` renderer is useful for tile scan and graphics-layout
cross-checks, but it is not authoritative for layer order: its `video.c` uses a
fixed Scroll 3/Scroll 2/sprites/Scroll 1 sequence and contains an SF2-specific
Scroll-1 unscrolling workaround that MAME does not use. Keep that prototype
separate from this CPS1 board implementation. The remaining NeoGeoRecomp ABI
dependency in `cps1_boot` is an integration debt in CPU dispatch only; its
Neo Geo board runtime, bus, video, and audio are not used by CPS1.

The direct `cps1_boot` command currently exposes all extracted ROM artifacts
because it is an internal harness. For a normal build-and-run pass, use the
system entry point documented above (`tools/recompile.py`); the large internal
argument list should not be presented as the regular game launch interface.
