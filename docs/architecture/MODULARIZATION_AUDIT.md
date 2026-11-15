# Source modularization audit

This is a working audit of the clean `multiarcaderecomp` checkout. Update it after
moving a module or resolving an ownership question. A ROM manifest is not proof
that a board runtime or game is implemented.

## Corrected boundary: shared 68000 core versus board runtime

The old CMake target `multiarcaderecomp_system16_runtime` held a callback bus bridge
and generated-code dispatcher that were consumed by unrelated Konami and
Seibu boards. The Musashi fallback target also had a System 16 name, and
TMNT/TMNT2/Toki included headers from `src/system16/`.

The bridge, dispatcher, and Musashi fallback now live in
`core/cpus/m68000/runtime/` and build as `multiarcaderecomp_m68k_recomp_runtime` and
`multiarcaderecomp_m68k_musashi`. System 16, TMNT, TMNT2/Sunset Riders, and Toki bind
their own memory callbacks to that generic CPU runtime. System 16 keeps
FD1094 and its own address map. No Konami or Seibu source includes
`src/system16/` now.

Focused Release compilation succeeded for the two shared targets, the TMNT,
TMNT2, and Toki board libraries, plus the host `main.cpp` and System 16 debugger
translation units. The full application was not linked and no game was booted.

## Current source ownership and remaining debt

| Area | Current location | Audit result |
|---|---|---|
| Shared CPUs and chips | `core/cpus/`, `core/chips/`, `core/audio/` | Architectural rule: CPU implementations provide processor semantics behind generic memory/IO and interrupt interfaces. Clock rates/dividers, bus maps, IRQ wiring, and board-specific quirks belong to board adapters/runtimes; silicon variants with different CPU behavior get distinct CPU models. A chip module owns the IC behavior and internal state/timers when the hardware has them, and exposes the required clock/advance or signal interfaces. The board chooses the IC instance, supplies its clock, and wires its signals to other devices; the chip never branches on board identity. A source scan found no Sega/System16/System18 references in `core/cpus/`. The repository-wide audit is not complete: `core/cpus/sh4/flycast_interpreter.*` and `flycast_cycles.*` are Naomi-bound, and `core/cpus/psx/machine_memory.*` includes ZN/System 12 memory configurations and SoC devices; these need a follow-up ownership review to distinguish CPU semantics from SoC/runtime adapters. MCS-48, Yamaha YMFM, DAC/DC blocker, RF5C68, and Sega ASICs have explicit module targets. Device wrappers stay board-specific when clocks, IRQs, or routing differ. |
| Sega System16A/B and System18 | `src/runtime/sega/`, `src/boards/sega/`, `src/games/sega/`, `src/romsets/sega/` | Parent CMake modules aggregate each board and runtime family. The shared address-space contract, frame type, board IDs, and timing registrations live under Sega common/runtime modules. Independent runtime targets under `src/runtime/sega/system16a/`, `system16b/`, and `system18/` construct board buses, video sessions, board defaults, mapper/audio wiring, System18 scanline timing, and System18 state export. Board audio mixers emit PCM through `SegaAudioSink`; SDL playback belongs to the presentation module. Each runtime is built as a separate `.so` with a versioned factory and selected by the runner; generated game `.so` files remain separate. The board profiles remain typed as `System16AConfiguration`, `System16BConfiguration`, or `System18Configuration`. 315-5011, 315-5012, 315-5049, 315-5195/5196/5197, System18 ASICs, System16A 8255, System16B 315-5248 multiplier, and 315-5250 compare/timer have separate modules. System16A and System16B OpenGL renderer sources/targets are separate; System18 currently uses software rendering. `main.cpp` dispatches Sega ROM/build/run commands; the generic runner owns 68000/vblank scheduling and loads only the selected board runtime. Memory-map inspection and graphics decoding are exported by that runtime, and board-specific video diagnostics are callbacks supplied by the runtime. The host target no longer links a board library or board-specific ASIC. Runtime/gameplay validation remains profile-specific and requires ROM availability.
| Konami TMNT boards | `src/konami/`, `src/romsets/konami/` | The board source is separate by generation; shared CPU/chip targets are generic after this correction. |
| Seibu Toki | `src/seibu/`, `src/romsets/seibu/` | Board adapter is local; CPU cores are shared. |
| Neo Geo KOF98 | `src/neogeo/` plus `core/third_party/neogeo-runtime/` | Game host, ROM metadata/protection, AOT exports, and upstream board runtime are split across layers. No source move was made pending a component-by-component map. |
| Capcom CPS1 | `src/cps1/` | Already has a core/game split, but module CMake ownership and CPU/fallback integration remain incomplete. |
| Naomi | `src/naomi/`, shared SH-4/PVR/AICA components | Board devices and game loader are still grouped in one source directory. |
| Sony ZN | `src/zn/`, shared PSX GPU/CPU | ZN1 and ZN2 use separate board/audio adapters; preserve the common PSX GPU as hardware shared on demand. |
| Namco System 12 | `src/zn/` plus shared PSX/H8 cores | Tekken 3 works through the shared PSX-derived runtime, but System 12 board/game wiring is still mixed into the ZN sources. Extract it while keeping only the generic PSX/H8 cores shared. The existing Tekken 3 AOT artifact must be connected to the separated runtime. |
| Midway Seattle, T-Unit, V-Unit | `src/ki/`, `src/mk/`, `src/mk2/`, `src/midway/vunit/` | Board runtimes are separated by family, but sources and per-game profiles still need the agreed `hardware/boards/runtime/games` layout. |

## Repository-wide items still to audit

- Convert root CMake source lists into small module `CMakeLists.txt` files with
  parent aggregation; preserve one executable and per-game generated-code
  boundaries.
- Inventory every cross-board library dependency and confirm that each one is
  a CPU/chip/runtime component with a neutral contract, not a board adapter.
- Map game ROM metadata, inputs, and protection to one game owner without
  confusing catalog manifests with implemented runtime support.
- Compare the shared Z80 interpreter with Superzazu. Neo Geo `legacy` mode
  selects Superzazu for the whole Z80, while the normal backend has its own
  interpreter/AOT path. No interpreter winner has been selected.
- Audit device and I/O boundaries (DMA, IRQ, input/output, audio/video) per
  board; share code only after confirming identical hardware behavior.
- Keep generated AOT ROM output in the user build/cache. The repository stores
  generator scripts and generic runtime interfaces only.

## Sega board boundaries after the current split

- `SegaAddressSpace`, common bus results, Sega input/control state, and board
  family IDs live under `src/boards/sega/common/`. The System 16A address map
  is implemented in `src/boards/sega/system16a/`; System 16B and System 18
  implement their own derived address spaces in their board modules.
- System 18 no longer includes System 16 bus headers or links the System 16
  runtime target. Its board module links the shared Sega address-space module
  and the chips it actually uses.
- `GameProfile` still contains shared ROM metadata, but its typed board
  configuration is now the family discriminator. The registry and TOML
  generation live in `src/runtime/sega/profiles/`; separate per-board profile
  APIs after session runners no longer consume this shared ROM record.
- The shared Sega runner still owns common game-session boot, module selection,
  68000/vblank scheduling, and frame capture. Keep it limited to generic
  orchestration and move any remaining board behavior into the registered
  board runtimes.
- System16A and System16B OpenGL implementations now live in separate
  `system16a_video_gl.cpp` and `system16b_video_gl.cpp` sources and targets.
  Each board builds only its own renderer backend; common presentation remains
  in the Sega presentation module.
- The System16A/B and System18 sound adapters now emit mixed PCM through the
  board-neutral `SegaAudioSink`. SDL device creation and queueing belong to the
  Sega presentation target; the board archives no longer reference SDL audio.
- Continue game migration individually. Shinobi is the first game-owned module;
  keep its DIP behavior with that game and move the next title's unique runtime
  behavior only after confirming it is truly game-specific.
- Naomi remains an experimental loadable module in the production source/build
  graph. Its removal from production is a future cleanup once production and
  experimental targets are split; it was not part of this Sega module change.
- Apply the same CPU boundary to every architecture: no CPU implementation may
  inspect a board identity to decide its behavior. A board supplies generic
  buses, clocking, reset/interrupt lines, and peripheral adapters. If behavior
  differs because the processor silicon differs, represent it as a distinct
  CPU model and select it in the board configuration.

## Documentation and licenses

Project documentation is centralized in `docs/`; vendored source directories
no longer contain project notes, upstream records, or license texts. License
and notice files are under `docs/license/`, upstream provenance is under
`docs/third_party/`, and [`docs/README.md`](../README.md) is the navigation
index. License contents are retained, including the project GPL-2.0 text moved
from the deleted root path to `docs/license/multiarcaderecomp/LICENSE.md`.
