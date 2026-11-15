# Neo Geo / KOF98 modularization audit (2026-11-12)

## Current-state verification (2026-11-13)

The launcher and local build pipeline now default to the original AES BIOS
`neo-po.bin` with AES hardware signals; MVS defaults to `sp-s2.sp1`. UniBIOS
remains selectable with an explicit `--bios`. A 120-frame headless launch
through `multiarcaderecomp` previously captured the UniBIOS 4.0 screen reporting
`Region: EUROPE Mode: CONSOLE`; that was before BIOS-specific AoT modules were
split. The current native AES and MVS modules are generated from `neo-po.bin`
and `sp-s2.sp1`, respectively. UniBIOS now gets its own optional AoT variant
when built explicitly, because using the native-AES AoT with UniBIOS produces
68000 verifier mismatches.

That Release build generated 29,450 M68K routines / 152,986 dispatch entries
(1,077,425 emitted instruction occurrences), 20 BIOS and 59 cart Z80 roots,
and linked the original combined-mode `multiarcaderecomp_game_kof98.so` at
114,297,096 bytes. These are pre-split build measurements. The build pipeline
now writes the BIOS-dependent M68K AoT to separate `aes/generated/` and
`mvs/generated/` directories and produces `multiarcaderecomp_game_kof98_aes.so` and
`multiarcaderecomp_game_kof98_mvs.so` with separate CMake targets/object directories.
The sound CPU AoT stays shared because both modes use the same sound program
ROMs. AES selects the original `neo-po.bin` by default; MVS selects
`sp-s2.sp1`. UniBIOS is a separate optional AoT variant, selected with
`--bios` at both build and run time. The M68K
sources occupied 269 MiB; object files occupied 182 MiB. Compilation used
`-O3 -DNDEBUG -fPIC` (C) and `-O3 -DNDEBUG -std=c++20 -fPIC` (C++), without
`-g`.

No build input requires an external `/home/luiz/git/neogeorecomp` checkout:
the generator, runtime snapshot, and required Z80 sources are in this
repository. Remaining NeoGeoRecomp-shaped include paths and ABI names are
internal compatibility/API coupling, not an external repository dependency.
The KOF98 build is still orchestrated by `tools/neogeo/build.py` and currently
performs two CMake configure passes around ROM-derived source generation; this
is not yet a shared manifest-driven build pipeline. Also, the active Z80 AoT
translator/runtime remains marked GPL-3.0-only and is linked into the game
module, so the stated MIT-only distribution goal is not met.

### Later 68000 verifier check (2026-11-13)

After restoring the reference-style Neo Geo dispatch policy, the default AES
KOF98 run was checked with
`build/multiarcaderecomp run --machine=neogeo --board=mvs --game=kof98 --headless
--frames=3600 --verify`. It completed 1,547,511 verified blocks with zero
register/flag/PC/bus mismatches; 8,518,982 instructions still ran through
Musashi and 80 entry points were uncovered. `--board=mvs` selects the board
module; hardware mode defaults to AES unless `--system=mvs` is provided. At
the same frame count, explicit MVS mode completed 2,075,369 verified blocks
with zero mismatches, 205,348 interpreted instructions, and 79 uncovered
entries. A 1,200-frame default-AES run checked 301,956 blocks with zero
mismatches but still interpreted 8,364,258 instructions. This is the
previously known zero-filled work-RAM loop around `$1008AC/$1008B0`, not
evidence that the verifier's zero mismatch implies broad native coverage. The
earlier multi-million
mismatch result was caused by verification replay fetching instruction bytes
through KOF98's page-zero protection overlay. `bus_code_peek16()` now reads
the underlying P-ROM for instruction fetches during verification, while
ordinary data reads continue to use the recorded board bus. This establishes
AoT/Musashi parity for executed blocks in this run, not full static coverage.

The current generated AoT has 60,832 dispatch entries, 1,706,046 emitted
instruction occurrences and about 300 MiB of generated C. Compared with the
KOF98 reference emitter, MultiArcadeRecomp adds an `NG_DEBUG_INSN()` hook at every
instruction. That is an evidenced code-size/runtime-hook cost difference to
measure and potentially remove for Neo Geo; no performance gain is claimed
yet. The current worktree also contains the early ZUni80 decoder/discovery/
routine-grouping code and a Pacrecomp emitter probe, but no production ZUni80
emitter/runtime. The GPL backend therefore remains active in all current
production Z80 AoT integrations.

### Z80 replacement compatibility boundary

`tools/z80/recomp.py` is currently invoked by both
`tools/neogeo/build.py` and `tools/generators/cps1/build.py`; its generated
tables are consumed through the existing `multiarcaderecomp::z80::Z80Code` ABI.
Runtime consumers also include System 16A/B, System 18, CPS1, ZN2, TMNT,
Toki and KOF98. The replacement AoT generator must therefore keep the current
board-facing contract (`Z80Bus`, `Z80::Run`, `SetZ80Code`, generated table and
function signatures, and cycle-yield behavior) stable while its output is
validated. Migrate generator selection behind that boundary first; do not
change the shared interpreter or board adapters as part of the first switch.

SuperZazu is an MIT-licensed instruction-step core, not a translator. It is the
chosen interpreter basis, while a project-owned frontend must independently
decode ROM images and emit native code. The replacement scope is shared across
all supported Z80 boards; a KOF98-only legacy switch is diagnostic evidence,
not the requested end state.

### SuperZazu execution evidence (2026-11-13)

The upstream SuperZazu test harness was compiled against the vendored local
`z80.c` and run with its `prelim.com`, ZEXDOC and ZEXALL fixtures. All passed.
The copyrighted fixtures and harness remained in the external upstream
checkout.

KOF98 was then run for 120 headless audio frames using the default AoT path and
`NG_Z80_MODE=legacy`, each with separate NVRAM and YM2610 write traces. Both
produced 1,148 YM writes with matching port/value sequences. SuperZazu's first
write was 11 Z80 cycles earlier; its last write was 28 cycles earlier, with a
final scheduler skew of -4 cycles. A separate 30-frame SuperZazu trace
recorded 54,660 instruction steps through the first 500,000 Z80 cycles. The
68000 verifier reported 32,926 blocks and zero mismatches on both runs. This
supports KOF98 sound-command compatibility, but does not yet compare every Z80
register state or establish compatibility with other boards.

A 1,200-frame rerun in the current worktree generated 13,755 YM2610 writes in
each mode, with every port/value pair matching. The first timestamp differed
by 11 Z80 cycles and the final SuperZazu write was 34 cycles earlier. At
shutdown the native path's scheduler skew was +8 cycles and the SuperZazu
path's was -15. An older status entry reported a write-value mismatch at write
152 over 1,200 frames; that result was not reproduced with these commands and
artifacts. Sample-level audio and per-instruction register-state equivalence
remain unverified.

## Executive status

At the time of the initial audit, KOF98's MultiArcadeRecomp application path launched
the generated modules: a 60-frame headless run through
`multiarcaderecomp run --machine=neogeo --board=mvs --game=kof98` reported 15,988
native 68000 blocks, zero interpreted instructions and zero uncovered entry
points. The integration exposed ELF interposition between Neo Geo and generic
68000 symbols. Later symbol namespacing and local symbol binding addressed the
runtime collision; the current 2026-11-13 AES startup verification is recorded
below. Neither run validates interactive gameplay, audio and video as a whole.

At the initial audit, a clean CMake configuration did not have generated
source directories. CMake now defines deterministic defaults under
`build/neogeo/kof98/`; `tools/neogeo/build.py` still generates both tables and
performs a second configure so CMake can discover the generated shards. The
common command does not yet own a shared game-independent generation pipeline.
The active build contains those generated tables and
`multiarcaderecomp_game_kof98.so`.

There is no `.gitmodules` entry or required source path into a sibling
NeoGeoRecomp checkout. The 68000 frontend/runtime snapshot and Neo Geo board
runtime are already copied into this repository under `core/third_party/`;
the Z80 frontend and runtime are also local. The remaining dependency is
architectural and naming-level coupling to NeoGeoRecomp's ABI and product
layout, not a build-time requirement to clone that repository. Provenance and
licenses must remain attached to imported sources.

## Changes made during this audit

- Renamed the local source directories to `core/third_party/m68krecomp/` and
  `core/third_party/neogeo-runtime/`; source provenance stays in
  `docs/third_party/` and license texts stay in `docs/license/`. Build files
  no longer need the old NeoGeoRecomp repository checkout or its directory
  names.
- Renamed the local 68000 frontend target to
  `multiarcaderecomp_m68k_aot_frontend` (with a temporary CMake alias for existing
  users) and changed the board library to the shared target/output
  `multiarcaderecomp_neogeo_runtime.so`.
- Replaced the KOF98 generator's call to NeoGeoRecomp-specific
  `ng_recomp_main()` with the common `rc_raw_main()` m68krecomp frontend.
  The KOF98 profile validates/assembles the TOML ROM image, applies its
  game-owned PROGSF1 transform, then supplies the Neo Geo fixed 1 MiB program
  window, first 1 MiB bank window at `0x200000`, and BIOS at `0xC00000`.
  The shared `--board=neogeo` discovery policy and the BIOS, cart-header, and
  runtime-observed roots are preserved. Generation from the external
  `kof98.zip`/`neogeo.zip` produced 27,600 routines and 142,144 dispatch
  entries, matching the historical KOF98 baseline recorded in `STATUS.md`.
- Moved `kof98_game.c` out of the shared ROM/profile library and into
  `multiarcaderecomp_game_kof98.so`. The game module now exports the descriptor and
  reset hook. The shared ROM validator no longer embeds KOF98's decryption;
  the game profile applies that transform in the board runtime.
- Renamed the KOF98 builder entry point to `tools/neogeo/build.py` and wired
  the common `tools/recompile.py --system neogeo --game kof98 --rom-dir ...`
  form to it. The builder still has a KOF98-specific two-pass generation
  implementation; this is not yet the final shared build pipeline. Its
  default build root is now the repository-wide `build/`, placing KOF98
  products below `build/neogeo/kof98/` instead of a separate top-level tree.
- A clean generation built from external `kof98.zip` and `neogeo.zip` emitted
  27,600 68000 routines / 142,144 dispatch entries and Z80 BIOS/cart packages
  (20/59 roots; 3,842/3,696 instructions). The app, board runtime and game
  module compiled, and a 60-frame headless run through the app reported
  15,988 native blocks, zero interpreted instructions and zero uncovered
  entries. This verifies loading and AoT execution, not interactive gameplay,
  audio or video fidelity.
- The KOF98 builder now uses deterministic generated directories inside the
  selected build tree as CMake defaults. It no longer clears or injects those
  cache paths; it performs the required second configure after generation so
  CMake can add the emitted shards to the graph. A complete
  `tools/neogeo/build.py --rom-dir /home/luiz/git/mame/roms --jobs 4` run
  rebuilt the generator, emitted the M68K and Z80 shards, and linked the runtime
  and KOF98 game module using those defaults.
- The first module run exposed symbol interposition and a hidden dependency on
  `ng_protection_active`; the Neo Geo ABI is now namespaced and the Neo Geo bus
  defines its own protection flag. `nm -D` confirms the runtime exports and
  KOF98 module imports resolve as `multiarcaderecomp_neogeo_*`.

## Current KOF98 path

| Stage | Current implementation | Finding |
|---|---|---|
| ROM profile | `src/romsets/snk/neogeo/neogeo_mvs/kof98.toml`, emitted by `emit_neogeo_runtime.py` | TOML validation and generated ROM metadata are already local. |
| Game behavior | `src/neogeo/kof98_game.c` | KOF98 owns PROGSF1 transform, protection callbacks, and the `ng_game_t` descriptor; these now compile into the KOF98 game module. Shared ZIP/ROM validation leaves program bytes raw. |
| 68000 generation | `kof98_aot` prepares manifest-backed regions and calls the shared `rc_raw_main()` frontend with `--board neogeo`; `tools/neogeo/build.py` orchestrates generation | KOF98 no longer invokes the NeoGeoRecomp-specific ROM-profile generator. The frontend's emitted ABI and headers still use `ng_func_entry_t`, `CPU` and `neogeorecomp` names. |
| Z80 generation | Local `tools/z80/recomp.py` emits C++ from SM1 and M1 ROMs | This is already a MultiArcadeRecomp-local generator; KOF98 roots and ROM extraction are wired by its bespoke build script. Runtime fallback remains active. |
| Z80 execution | `src/neogeo/kof98_z80_host.cpp` selects the BIOS/cart table and calls `multiarcaderecomp::z80::Z80::Run()`; bus, clocks and IRQ callbacks live in `core/third_party/neogeo-runtime/src/audio.c` | Default is native translated blocks. Missing entries, RAM execution and interrupt boundaries use the GPL-derived `Execute()` interpreter in `core/third_party/z80-aot/sound/z80.cpp`; this is neither Musashi nor SuperZazu. `NG_Z80_MODE=interpreter` selects the same interpreter deliberately. |
| AOT packaging | CMake combines 68000 C shards, Z80 C++ shards and `kof98_module.cpp` into `multiarcaderecomp_game_kof98.so` | Both CPUs are packaged together per game. The module API is KOF98/NeoGeo-specific, not the common game-module contract used by Sega. |
| Board execution | `multiarcaderecomp` dynamically loads `multiarcaderecomp_neogeo_runtime.so`; the board module loads `multiarcaderecomp_game_kof98.so` | The 60-frame headless app run reports 15,988 native blocks, zero interpreted instructions and zero uncovered entries. CPU/dispatcher implementation is still duplicated inside the board runtime; board loop, map, LSPC/video, I/O, Z80 scheduling/audio and platform code remain grouped there. |
| Build entry point | `tools/recompile.py --system neogeo --game kof98` delegates to `tools/neogeo/build.py` | The wrapper accepts the common `--rom-dir` contract. The game adapter still owns ROM extraction and generation, then reconfigures CMake using the deterministic build-tree defaults so it can add emitted sources. |

## AoT coverage and evidence

The checked-in status records a 68000 table of roughly 27.5k routines and
142k dispatch entries after long-run roots were added. It also records a
12,000-frame run with about 7.7m native blocks, 8.4m Musashi instructions and
zero uncovered ROM entries. The remaining 68000 interpreter workload was
reported as execution from mutable work RAM; it is not evidence that all
guest execution is statically translated.

For Z80, the status records generated BIOS/cart blocks and runs with
substantial native coverage, but interpreter fallback remains for dynamic RAM
execution and previously uncovered roots. One run reported about 704k native
blocks and 65k interpreted instructions; these figures belong to different
captures and must not be compared as a performance benchmark. The generated
BIOS and cart tables are correctly separate because the Neo Geo sound CPU
switches between those ROM images, while both tables are exported by the same
KOF98 game module.

The output-size audit in `STATUS.md` also remains relevant: overlapping 68000
routine bodies duplicate guest instruction labels heavily, and an unoptimized
generated build produced a roughly 99 MiB game module. Release optimization
and shared-block emission are separate follow-up items; neither is solved by
changing the module name or build wrapper.

## Upstream comparison and license audit

The historical NeoGeoRecomp comparison used revision
`af96b2b4b39b42b392b17bcaa6a74a5f27088202` (`v0.2.0`). This matches the
recorded upstream revision for the locally vendored 68000 frontend/runtime.
That comparison found no newer 68000 recompiler commit to import. Upstream
documents complete 68000 opcode
translation, runtime-profile roots, and a Musashi comparison mode; the local
snapshot already contains that frontend, but KOF98 does not yet use the
upstream verifier as a gate in the standard MultiArcadeRecomp pipeline. NeoGeoRecomp
has no Z80 AoT translator: its Z80 is the separately licensed SuperZazu
interpreter, already vendored locally under MIT.

No newer 68000 implementation was found to port in this comparison. The
remaining useful upstream work is integration: make generation, profiling,
and verification stages part of the generic MultiArcadeRecomp build interface rather
than KOF98-only orchestration. There is no upstream Z80 AoT feature to port;
the active Z80 AoT implementation was derived from CPS2Recomp under GPL-3.0.
Given the project's MIT distribution goal, it must be replaced with an
independent MIT implementation before KOF98's AoT module can be distributed.

The MAME-derived portions of the Neo Geo board runtime are BSD-3-Clause, not
GPL. Their file-level provenance is recorded in
`core/third_party/neogeo-runtime/NOTICE-MAME.md`; keep that license and notice
with distributions. Other portions retain their NeoGeoRecomp MIT terms. These
licenses can coexist; the MAME-derived code must not be relabeled MIT or GPL.

Separately, the active KOF98 Z80 translator/runtime at `tools/z80/` and
`core/third_party/z80-aot/` carries `GPL-3.0-only` provenance from
CPS2Recomp. Since MultiArcadeRecomp is MIT, this code cannot remain in the distributed
pipeline under the requested project license. It must be replaced by a clean
MIT implementation; preserve the GPL license and provenance only with the
retired source snapshot if it remains in the repository, and ensure it is not
compiled or imported by production targets. This GPL issue is independent of
the BSD-licensed MAME portions of the Neo Geo board and is not a reason to
relicense those portions. The 68000 frontend, Musashi, remaining
NeoGeoRecomp-derived runtime code, SuperZazu Z80 interpreter and YMFM retain
their respective permissive licenses and notices.

This Z80 license issue is repository-wide, not isolated to Neo Geo: CMake
builds `multiarcaderecomp_z80_core` from the GPL files and links it into multiple
production runtimes and generated game modules. The Neo Geo board contains an
MIT SuperZazu stepper (`NG_Z80_MODE=legacy`). In this mode `audio_sync()` calls
`z80_step()` one instruction at a time and feeds its T-state count into board
clock and YM2610 timing. It is currently a diagnostic/reference path: it does
not consume AoT tables, and the KOF98 module still compiles and links the GPL
AoT engine. No captured run currently establishes functional equivalence or
comparative speed between the paths. Do not advertise the overall
application binary as MIT-only until this shared Z80 dependency has been
reimplemented or replaced and the dependent boards have been checked.

### Z80 engine comparison (KOF98)

| Path | License/provenance | Execution model | Current KOF98 role |
|---|---|---|---|
| `multiarcaderecomp::z80::Z80` (`core/third_party/z80-aot/`) | GPL-3.0-only, derived from CPS2Recomp | Generated native blocks with instruction interpreter fallback; guest-cycle/interrupt boundaries and fallback profiling | Still the production/default path across current AoT integrations; must be removed from the distributed build. |
| SuperZazu `z80_step()` (`core/third_party/superzazu-z80/`) | MIT | Compact C instruction-step interpreter; memory/I/O callbacks, interrupt state and T-state counter | Optional Neo Geo diagnostic path; KOF98 YM write port/value parity evidence exists. Not AoT. |

Current evidence shows matching KOF98 YM port/value behavior over a 1,200-frame
run, with small but measurable timing differences. It does not establish
register-by-register parity, full-game behavior, performance parity, or
compatibility with other boards. SuperZazu remains the chosen MIT interpreter
basis; the project-owned AoT frontend and common board adapter are still
required before production migration is complete.

## What is and is not decoupled

- The KOF98 game module owns game transforms/protection. Its Neo Geo board
  still embeds a separate 68000 dispatcher and Z80 execution host instead of
  composing the generic MultiArcadeRecomp CPU runtimes end to end. The Z80 has two
  selectable engines (GPL AoT/interpreter and MIT SuperZazu), with only the
  former as the production default.
- KOF98's decryption and protection behavior are game-owned.
- 68000 and Z80 generated code are separate generated artifacts but are
  compiled into one per-game shared object.
- The Neo Geo board has a loadable MultiArcadeRecomp runtime module, but its internal
  scheduler/map/LSPC/video/I/O/audio/platform implementation remains grouped
  in one imported runtime and still uses a legacy API shape.
- Video rendering is split into a CPU rasterizer and an SDL presentation
  path. `video.c` decodes graphics and draws the LSPC sprite/scanline output
  into a software framebuffer. `window_sdl.c` requests SDL's accelerated 2D
  renderer, uploads that framebuffer to a streaming texture, then copies it
  to the window. SDL may use an OpenGL renderer underneath, depending on the
  selected driver; the Neo Geo source does not call OpenGL directly and has
  no hardware tile/sprite rasterizer or game-specific shaders.
- YM2610, LSPC, address map, frame scheduler, audio, and platform presentation
  are not yet exposed as independently composed board/chip modules in the
  MultiArcadeRecomp architecture. They need a source-ownership and signal/clock map
  before extraction; avoid moving files mechanically.
- The top-level application routes KOF98 build/run through its common command
  interface and dynamically loaded board/game modules. A 60-frame headless run
  through `multiarcaderecomp run --machine=neogeo --board=mvs --game=kof98` now reaches
  the registered board runtime and reports 15,988 native blocks, zero
  interpreted instructions and zero uncovered entry points.
- The module boundary had an ELF symbol-interposition defect: the Neo Geo
  runtime and generic MultiArcadeRecomp 68000 runtime exported the same `ng_*` CPU and
  bus globals. The local Neo Geo public headers now map that private ABI to
  `multiarcaderecomp_neogeo_*`; its bus also owns the protection-active flag. The AoT
  game module and board runtime therefore bind to the same Neo Geo state while
  other 68000 boards keep their existing ABI. `nm -D` confirms the renamed
  runtime exports and matching game-module imports.

## Recommended order of work

1. Keep all imported sources local and preserve each source's original license.
   NeoGeoRecomp-derived code retains its MIT notices; the MAME-derived Neo Geo
   raster/LSPC portions retain their BSD-3-Clause attribution. Move active
   68000 code behind MultiArcadeRecomp-owned CPU/frontend APIs and make the generated
   table ABI board-neutral. Do not require an external checkout.
2. Replace the current GPL-3.0-only Z80 translator/runtime, which came from
   CPS2Recomp and is still in active targets, with a project-owned compatible
   AoT frontend and SuperZazu-based interpreter adapter. Apply this shared
   CPU/runtime to every supported board with a Z80, then validate each board's
   clock, memory and interrupt wiring. NeoGeoRecomp has no Z80 AoT frontend to
   import. Retain original GPL source/license only as a clearly segregated,
   non-built historical third-party snapshot if it is kept at all.
3. Split the Neo Geo board runtime by ownership: board scheduler/map and
   independent device modules (LSPC/video, YM2610 adapter, I/O, audio timing,
   platform). Keep clock rates, memory mapping and IRQ wiring in the board.
4. Replace the bespoke runner with a Neo Geo board runtime module and make
   `multiarcaderecomp` register/launch it. Keep KOF98 transform, protection and AOT
   exports in a game module.
5. Route KOF98 through the same build orchestration contract as other
   MultiArcadeRecomp games. Keep generated ROM-derived source/assets in `build/`,
   emit 68000 and Z80 tables, and package them into the game's
   `xxx_game.so`. A game-only rebuild should not rebuild unrelated boards.
6. Revalidate a clean generated build, MVS boot, attract/gameplay, video,
   audio, native/fallback counters and module ABI before calling KOF98
   decoupled. Do not infer readiness from a successful compile alone.

## Audit scope and limits

This audit generated ROM-derived AoT sources, compiled the 68000 and Z80 game
module and dedicated Neo Geo runtime, and verified a headless KOF98 launch
through the application module route. It did not perform an interactive
attract/gameplay, video or audio comparison, and did not change the frozen Sega
board/game runtimes. The available KOF98 ROM archives are located in the user's
external MAME ROM directory and remain outside the repository.
