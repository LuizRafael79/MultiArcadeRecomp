# MultiArcadeRecomp repository restart and source reorganization

This document records the new repository's origin and the staged migration of
supported games. Update it after each completed migration and validation.

## Goal

Preserve the former working repository as `multiarcaderecomp_dev`, create a clean
`multiarcaderecomp`, then migrate supported games incrementally, by hardware and game.
Start with Sega System 16A, System 16B, and System 18. Build and validate after
each small step so the new tree does not become a second tangled source tree.

The source model is still being decided. Current working concepts are:

- shared CPU and chip implementations;
- board hardware and its runtime;
- game ROM metadata, inputs, configuration, and game-specific AOT output;
- the running session / application lifecycle.

Do not force the word `machine` into the directory model until its meaning is
settled. The current CLI's `machine=sega` naming is not itself an architectural
boundary.

## Repository setup completed — 2026-11-09

1. Confirmed `/home/luiz/git/MultiArcadeRecomp` was the official repository. The
   directory `/home/luiz/.codex/worktrees/7c98/MultiArcadeRecomp` is a linked,
   detached Codex worktree, not a second official source checkout.
2. Renamed the official checkout to `/home/luiz/git/multiarcaderecomp_dev`. This was a
   filesystem rename, preserving tracked edits and untracked files there.
3. Repaired the Codex worktree's Git metadata to point to
   `/home/luiz/git/multiarcaderecomp_dev/.git/worktrees/MultiArcadeRecomp`. Its pre-existing
   local edits remain in that worktree.
4. Created `/home/luiz/git/multiarcaderecomp` as a separate local clone of the last
   committed revision, `9b695046a8decf84fbe50b8441922af626a606a0`
   (`Reuse PSX texture cache across ABR modes`). The new checkout starts clean;
   uncommitted work from `multiarcaderecomp_dev` was deliberately not copied into it.
5. Removed the clone's local `origin` pointing at `multiarcaderecomp_dev`; no hosted
   remote was supplied. The new repo retains history through the baseline
   commit, but future changes are independent of the preservation checkout.

## Migration rules

- Keep `multiarcaderecomp_dev` as the reference for the former working tree. Do not
  build there or silently copy its dirty state into the new repository.
- Establish a clean Release build and test baseline in `multiarcaderecomp` before
  moving source. After each small source move, rebuild and run relevant tests
  before continuing.
- Migrate one board/game slice at a time. Record source ownership, dependencies,
  build result, game boot/play checkpoint, and known gaps before starting the
  next slice. A ROM manifest is not proof that a game runs.
- Move only source and metadata required by the selected slice. Do not add ROM
  dumps, save data, screenshots, PCM captures, or generated AOT artifacts.
- AOT generated from user-provided ROMs belongs in a local cache keyed by ROM
  identity, recompiler version, and target ABI. Distributed sources contain
  the recompiler and generic runtimes, not per-game generated code.
- Keep board maps, scheduling, IRQ wiring, and device connections in board
  runtime code. Keep reusable CPU/chip behavior in shared core modules. Keep
  game identity, ROM manifest, inputs, and proven game-specific differences
  with the game profile.
- Preserve behavior first. Make architectural cleanup and emulation changes in
  separate, reviewable steps where practical.

## First migration scope: Sega System 16 / 18

The baseline registers these Sega game profiles:

| Game/profile | Board family |
|---|---|
| `shinobi` | System 16A |
| `shinobi5` | System 16B |
| `goldnaxe` | System 16B |
| `goldnaxeud` | System 16B |
| `eswat` | System 16B |
| `shdancer` | System 18 |
| `mwalkd` | System 18 |

These are candidates, not a claim that each passes a clean build or gameplay
validation. Confirm the supported set from the launcher and profiles before
migration and correct this list if needed.

Current implementation is spread across `src/system16/`, `src/system18/`,
`src/romsets/`, `src/main.cpp`, and CMake declarations. Move a small, coherent
slice at a time after mapping dependencies; avoid a blind directory-wide move.

Provisional destination, subject to dependency review:

```text
src/machine/                     # generic machine/session composition, if needed
src/hardware/
  cpus/<cpu>/                    # reusable CPU semantics, interpreter, AOT
  chips/<chip>/                  # reusable chip implementations
src/runtime/
  sega/system16/                 # family-level scheduler/dispatch shared by A/B
  sega/system18/                 # only if it has reusable family-level runtime
src/boards/sega/
  system16/common/               # only code proven common to A/B
  system16/a/                    # board map, wiring, board-specific adapters
  system16/b/
  system18/
src/games/sega/
  system16/shinobi/
  system16/shinobi5/
  system16/goldnaxe/
  system16/goldnaxeud/
  system16/eswat/
  system18/shdancer/
  system18/mwalkd/
```

`hardware` owns reusable CPU/chip cores; it should not be nested under Sega or
System 16. A board owns its memory map, timing, interrupts, and connections to
those cores. `runtime/sega/system16` is justified only for execution/scheduling
code actually shared by the A/B boards; board-specific behavior stays in each
board module. Game manifests and game-specific profiles belong with their game.

Use a `CMakeLists.txt` at each module boundary, with small parent aggregators
calling `add_subdirectory()`. The root CMake file should set project-wide
options/dependencies and compose targets, not enumerate every board/game source.
A module's own CMake file does not prescribe static vs dynamic linking.

`machine` remains an optional name for generic assembly/session code. Do not put
CPU/chip implementations under `machine/sega/system16`, because that would make
reusable hardware look Sega/System16-specific.

ROM manifests may stay in a shared catalog if the loader needs one. Game-owned
C++ profile and behavior should still have one obvious game owner; do not
multiply definitions solely for visual directory symmetry.

## Work log

### 2026-11-09 — repository creation and first System 16/18 slice

- Preserved the official working tree as `~/git/multiarcaderecomp_dev`.
- Created a clean `~/git/multiarcaderecomp` clone at baseline commit `9b69504`.
- Removed the clone's local origin; the new repository has no remote configured.
- Added `CMakePresets.json` with distinct Linux x64 and Windows x64 `dev`,
  `debug`, and `release` build/test presets. Linux uses Ninja + GCC; Windows
  uses Visual Studio 2022 x64 + MSVC. Each preset has its own build directory.
  Linux dev/debug/release configurations all configure successfully.
- The first full Linux Release build exposed a pre-existing malformed branch in
  the PSX GPU command decoder. Removed only that unreachable orphan branch; no
  GPU behavior was changed. Removed `-march=native` so artifacts use the
  compiler's portable x64 baseline.
- Linux x64 `dev`, `debug`, and `release` configure presets all configure
  successfully into separate directories. A complete application build was
  inadvertently started once and manually interrupted at 114/366 build steps
  after the user clarified that this stage should stay scoped to Sega. Do not
  treat that interrupted build as a build pass. During cleanup, the parent
  `build/` directory was removed, which also removed the preset artifacts
  created earlier; all three Linux preset configurations were then recreated,
  and the targeted Sega build/test was rerun successfully. Only generated build
  artifacts were removed; no source or ROM data was affected. Use presets only.
- Instead, the 23 System 16/18 translation units compiled individually in the
  Release preset. `system16_fd1094_test` built and passed with the local
  `eswat.zip`. This validates compilation and that focused test, not full game
  execution. The new executable has not been built yet.
- Windows x64 presets are defined for Visual Studio 2022/MSVC but cannot be
  built in this Linux environment. The current source still has POSIX dynamic
  loading/process-spawn code and fixed `.so` paths, which must be ported before
  Windows is a supported build target.
- Graphics target matrix: Linux x64 OpenGL now, Vulkan later; Windows x64
  OpenGL now, DirectX later. macOS is intentionally deferred and unverified.
  No Vulkan or DirectX backend is claimed by these presets.
- Targeted Release validation: compiled the 23 System16A/System16B/System18
  board/profile translation units; built and passed `system16_fd1094_test`
  using `/home/luiz/git/mame/roms/eswat.zip`.
- First game-owned source move: moved Shinobi's C++ profile from
  `src/romsets/shinobi.cpp` to `src/games/sega/system16a/shinobi/profile.cpp`
  and updated CMake. Its object compiles in the Release preset. The proposed
  final layout groups games under `src/games/sega/system16/<game>/`; the current
  `system16a` subdirectory is an initial staging location and can be normalized
  when the game-family split is reviewed. The JSON ROM
  manifest remains at the legacy catalog path until its loader dependencies are
  traced. No game executable or boot test has run yet.
- The whole application was not built. Next: isolate Sega/System16/18 sources
  behind explicit CMake targets so focused board/game checks avoid unrelated
  targets, then validate Shinobi boot before moving the next game.

## Build configurations and platform targets

Use CMake presets so development, debug, and release outputs never share a
build directory:

```sh
cmake --preset linux-x64-dev
cmake --build --preset linux-x64-dev
ctest --preset linux-x64-dev

cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug

cmake --preset linux-x64-release
cmake --build --preset linux-x64-release
ctest --preset linux-x64-release
```

On a Windows host with Visual Studio 2022 and the C++ workload installed, use
the matching `windows-x64-dev`, `windows-x64-debug`, or
`windows-x64-release` configure/build/test presets. Their build outputs are
separate under `build/windows-x64/`. These presets establish compiler/generator
selection; a Windows build is not considered supported until POSIX dynamic
library loading, subprocess creation, module filenames, and graphics dependencies
are made portable and the presets pass on Windows.

Linux x64 is the first validation target. Its desired graphics backends are
OpenGL initially and Vulkan later. Windows x64 targets OpenGL initially and
DirectX later. macOS support is deferred; do not claim or ship a macOS target
until it can be built and exercised on macOS.

## Source layout decision checkpoint

### 2026-11-09 — provisional vocabulary

- Prefer `hardware` for reusable CPU and chip implementations.
- Use `boards` for physical-board-specific maps, timing, IRQ wiring, and
  connections to generic hardware cores. System 16A and 16B should remain
  separate board modules; share code only when the source/behavior demonstrates
  that both boards use it.
- Use `runtime` for family-level execution/scheduling only when code is truly
  shared across board variants (for example a System 16 A/B dispatcher). Keep
  board-specific runtime behavior next to its board.
- Use `games` for each game's profile, ROM manifest, inputs, and verified
  exceptions. Generated AOT remains a per-user cache, not repository content.
- Reserve `machine` for generic session/composition code if such a module is
  needed. It is not the owner for Sega/System 16 CPU cores.
- Give each real module its own `CMakeLists.txt`; root CMake aggregates modules.
  This does not yet choose `.a` versus `.so`/`.dll`.
- This is a proposed layout only. The current code migration has not moved the
  board sources or introduced per-module CMake files yet.

### 2026-11-09 — Z80 interpreter decision kept open

- Moved the project-authored Z80 integration/status note to
  `docs/hardware/cpus/z80/README.md` and updated its README/CPS progress links.
- Source inspection confirms Neo Geo `NG_Z80_MODE=legacy` runs Superzazu's
  `z80_step` interpreter for the whole sound CPU. Normal mode runs the shared
  `multiarcaderecomp::z80` backend, which has its own interpreter mode and AOT/native
  execution modes. Superzazu is therefore not currently the AOT fallback.
- The user wants the shared interpreter choice evaluated between the current
  implementation and Superzazu. No winner has been selected and no Neo Geo
  runtime source was changed. Compare correctness coverage, instruction and
  interrupt semantics, cycle accounting, bus behavior, and measured cost before
  deciding whether either implementation should be removed or integrated as the
  fallback.

### 2026-11-09 — documentation grouped outside the repository root

Moved the project-authored root documents into explicit shared-hardware,
system, game, ROM-catalog, architecture, and project-status folders under
`docs/`. Updated local Markdown links in the root README and moved notes. The
Z80 decision note remains under `docs/hardware/cpus/z80/` because it describes
a shared CPU backend rather than a single board.

Project-authored notes under `core/cpus/`, `src/`, and `tools/` moved under
`docs/` by CPU, system, or board. MAME source notes are under
`docs/references/mame/`. License texts and notices are under `docs/license/`,
and all vendored provenance and package documentation are under
`docs/third_party/`. The deleted root GPL-2.0 text was preserved as
`docs/license/multiarcaderecomp/LICENSE.md`. Link checks report no broken relative
Markdown links. No Neo Geo source/runtime was changed.


### 2026-11-09 — remove System 16 ownership from shared 68000 execution

- Moved the generated-code bus bridge, dispatcher, and Musashi fallback from
  `src/system16/` into `core/cpus/m68000/runtime/` and renamed their targets
  to `multiarcaderecomp_m68k_recomp_runtime` and `multiarcaderecomp_m68k_musashi`.
- Updated TMNT, TMNT2/Sunset Riders, and Toki to bind their own board callbacks
  to the shared 68000 runtime. Their source no longer includes System 16
  headers or links System 16 runtime targets. System 16 still provides its own
  board bus and FD1094 hooks.
- Removed the unreferenced System 16 address-space wrapper after `main.cpp`
  began binding System 16 callbacks directly to the generic bridge.
- Release build succeeded for the common bridge/fallback and the TMNT, TMNT2,
  and Toki board libraries. The `main.cpp` and System 16 debugger objects also
  compiled. The full application was not linked and no games were booted. The
  build emitted existing warnings in YMFM timer indexing and default Z80 bus
  callback parameters.
- Added per-system documentation for System 16, both Konami boards, Toki, and
  the unstarted System 12 migration. The current clean checkout contains a
  Tekken 3 ROM manifest and a reusable H8 core, but no System 12 board runtime
  or game integration.
- The full repository modularization remains in progress. Neo Geo source stays
  untouched while its runtime/game ownership and Z80 interpreter choice are
  audited; current source directories have not all moved into the final
  `hardware/boards/runtime/games` layout or acquired local CMake files.

### 2026-11-09 — ROM manifests and driver-module boundary

- Converted all 1,119 checked-in ROM-set source manifests from JSON to TOML.
  The conversion round-tripped every manifest through Python's standard
  `tomllib` parser before deleting its JSON source file.
- Added TOML-only I/O in `tools/generators/romsets/manifest_io.py`. Archive
  loading and the MAME ROM importers now read and write TOML. KOF98's CMake ROM-data
  generator now reads its TOML manifest.
- Added the versioned C module contract in `core/abi/game_driver_v1.h` and
  documented ROM inventory, parent/clone handling, and driver hooks in
  `docs/architecture/GAME_MANIFESTS_AND_DRIVER_ABI.md`.
- The manifests remain self-contained per game for ROM-region layouts; parent
  currently expresses lineage/archive lookup, not implicit merging of ROM
  arrays. This avoids accidentally inheriting changed bootleg wiring or ROM
  geometry. Parent-profile inheritance and explicit merge policy remain to be
  implemented before game-driver TOML fields depend on it.
- The new C driver ABI is defined but not yet adopted by the existing board/game
  modules. System 16/18's active C++ profile registry is still partly manual;
  generated TOML profile data must be compared against all active runtime
  profiles before replacing it.
- No build or game launch was run in this step. Existing staged deletions and
  unrelated working-tree changes were preserved.

### 2026-11-11 — Sega module aggregation

- Added parent CMake modules at `src/boards/sega/` and `src/runtime/sega/`.
  The root now adds those parents and links the `multiarcaderecomp_sega_runtime`
  aggregate instead of enumerating each Sega board target.
- Moved Sega ROM-profile generation into the System 16 runtime module and
  assigned FD1094, the M68000 debugger, and System 16 presentation/video
  backend selection to that module. Sega board targets own their optional
  SDL3 device adapters through the board aggregator.
- Updated the repository layout and modularization audit to match the source
  tree. The System 16 runner remains in `main.cpp`, and the System 16A/16B
  video implementation is still combined. These are explicit ownership items
  for a later code split, not hidden by the CMake aggregation.
- `cmake --preset linux-x64-release` configured successfully, and
  `cmake --build --preset linux-x64-release --target multiarcaderecomp -j2` linked
  the application successfully. No tests or game boot were run.

### 2026-11-11 — shared Sega address-space contract

- Moved the common Sega address-space abstraction, bus result types, shared
  controls, and board-family IDs under `src/boards/sega/common/`.
- Moved System 16A's I/O and memory map into its board module. System 16B and
  System 18 derive from the neutral Sega address-space contract. System 18 no
  longer includes System 16 bus headers or links the System 16 runtime target.
- `cmake --preset linux-x64-release` configured successfully, and
  `cmake --build --preset linux-x64-release --target multiarcaderecomp -j2` linked
  the application successfully. No tests or game boot were run.

### 2026-11-11 — neutral Sega profile module

- Moved the Sega ROM profile registry and archive loader from the System 16
  runtime directory to `src/runtime/sega/profiles/`, with its own CMake target.
  TOML profile generation now belongs to that module.
- Moved System 16A's sound ROM assembly function into the System 16A board
  module. The common `SegaVideoSignals` now carries only display and flip;
  row/column-scroll state is part of the System 16A adapter contract.
- System 18 and System 16B keep only their board identity/configuration types
  from `src/boards/sega/common/`; neither board target depends on the System 16
  runtime library.
- The shared profile record still has fields for all three families. Next
  ownership steps are typed board configuration, separate 16A/16B video
  modules, and moving the system runners and their family selection out of
  `main.cpp`.
- `cmake --preset linux-x64-release` configured successfully, and
  `cmake --build --preset linux-x64-release --target multiarcaderecomp -j2` linked
  the application successfully. No tests or game boot were run.

### 2026-11-11 — shared 315-5197 chip and CPU boundary

- Extracted Sega 315-5197 tile attribute decoding and frame-boundary register
  latching from the System 16 video implementation into
  `core/chips/sega_315_5197/`. System 16B, System 18, and the shared System 16
  video implementation now link the chip module explicitly.
- Reaffirmed the CPU rule across architectures: a CPU core contains processor
  behavior only. Boards provide generic bus/clock/interrupt/reset connections;
  board-specific CPU quirks stay in the adapter/runtime, while different
  processor silicon is modeled as a separate CPU type. A source scan found no
  Sega/System16/System18 references under `core/cpus/`.
- The broader CPU ownership scan found follow-up candidates outside Sega:
  SH-4 interpreter/cycle files currently depend on Naomi context/configuration,
  and the PSX `machine_memory` component combines CPU-facing memory with ZN and
  System 12 SoC configuration/devices. These need separate review before
  claiming all CPU modules are fully board-neutral.
- Split the System 16A and System 16B CPU video renderers into their owning
  board targets. The planar graphics decoder and OpenGL backend remain shared
  targets; System 18 continues to compose the shared 315-5197 tilemaps with its
  own VDP and board signals.
- Started the game-by-game layer with
  `src/games/sega/system16a/shinobi/`, which now owns Shinobi's lives DIP
  choices and display name. The remaining ROM profiles still come from the
  per-game TOML manifests; game-specific behavior will move one title at a
  time when identified.
- Naomi remains built as an experimental loadable module while it is not
  bootable. Removing experimental systems from the production target is a
  future production/build split, separate from the Sega work in this step.
- The Release configure and `multiarcaderecomp` build completed successfully after
  this extraction. No tests or game boot were run.
- Remaining Sega ownership work: separate OpenGL backend ownership where it
  can be done cleanly, move each board session runner and family selection out
  of `main.cpp`, replace the wide shared `GameProfile` with typed per-board
  configuration, and continue the game modules title by title.
