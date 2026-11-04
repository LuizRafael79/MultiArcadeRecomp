# Provisional architecture

This document records the current implementation direction. It is intentionally provisional: board behavior and chip variants must be established from primary technical references and validated against MAME before being generalized.

## Shared implementation layout

Reusable execution engines and chip models live under `core/`, independently
of any board. The shared CPU set is `core/cpus/m68000/`,
`core/cpus/z80/`, and `core/cpus/mcs48/`; reusable chip models live in
`core/chips/`. Imported sources remain grouped under `core/third_party/`.

Each board module under `src/` selects only the shared CPUs and chips it needs,
then implements its own memory map, timing, interrupts, device wiring, video
register interpretation, and game/board quirks. CPS1, Neo Geo, System 16, and
System 18 must not copy or depend on one another's board implementation merely
because they share a CPU family or imported ABI.

There is one 68000 decode/discovery/emission frontend for all boards:
`m68krecomp_frontend`. The prototype-only local decoder and its `translate`
command have been removed. Board modules keep their own bus adapters,
interrupts, timing, and device maps. Musashi remains the interpreter fallback
for instructions the recompiler does not yet cover, including code reached at
runtime in RAM; do not remove it until that coverage exists elsewhere.

## Execution model

- **Motorola 68000:** translate guest instructions into native C, then compile that generated C with the host compiler. The board runtime supplies the System 16 address map, interrupt timing, and peripheral behavior.
- **Zilog Z80:** use one shared CPU implementation and native AOT recompiler across boards. Its interpreter remains the required fallback for uncovered instructions and runtime code. Per-game ROMs produce separate generated tables; board adapters provide memory/I/O maps, banking, interrupts, clock conversion, and device scheduling. Do not fork CPU implementations by board (for example, `cps1_z80` or `system16_z80`). System 16A/B and System 18 now link the shared Z80 interpreter engine; generated AOT tables are not yet connected to those sound modules.
- **NEC uPD7751 / MCS-48 family:** model the chip's board interface and timing. If recompilation becomes useful, investigate a reusable MCS-48-family translator rather than a uPD7751-only translator. Confirm the exact embedded core, instruction subset, and chip-specific memory/I/O behavior first.
- **Video tile/sprite hardware:** interpret the board's video RAM, registers, graphics ROM formats, and composition rules in a software model. Keep guest timing renderer-backend-neutral; the indexed CPU framebuffer remains the reference for GPU parity.
- **FM synthesis:** interpret register writes and chip clocks in an accurate synthesizer core. An FM chip consumes register commands and advances operators/envelopes/phase; it does not execute a CPU instruction stream, so a CPU-style recompilation is not applicable. If profiling justifies it, specialize or JIT portions of the synthesis model as a separate optimization.

## Protected 68000 variants

Treat Sega FD1094 as a reusable encrypted-CPU/protection module, not a per-game protection bypass. MAME’s reference uses a key ROM, decrypts the opcode-fetch view according to a live state, and switches state on documented CPU events such as the FD1094 `CMP.L` command, interrupt entry, and `RTE`. Program-data reads remain a distinct path from decrypted opcode fetches. A static recompilation path therefore needs the key and protection state in its decode/cache identity (or equivalent state-specific translated entries), and must preserve those state transitions. Other FD-family parts are separate models until their algorithms and interfaces are verified. This is planned hardware emulation; it is not implemented in the current Shinobi set-6 executable.

## System 16A scope

Shinobi System 16A is the first board target. The 68000 core can be reused across boards, while address maps, clocks, interrupt wiring, graphics ROM wiring, video registers, and sound devices remain board/profile-specific. The current compositor is an early software implementation for the 16A page tilemaps and text layer. It does not imply that System 16A physically contains the System 16B custom video ICs.

System 16B uses three distinct ASICs in this path: 315-5195 for address mapping, 315-5196 for sprite generation, and 315-5197 for tilemap generation. None should be attributed to 16A. Shared MAME software helpers or shared graphics bitplane layouts are implementation reuse evidence only, not proof of identical physical hardware or board behavior.

Implement these ASICs as behavioral C++ devices in the runtime: the 315-5195 updates address regions and banking from its registers; the 315-5197 consumes tile RAM, text RAM, scroll/page registers and graphics ROMs; the 315-5196 processes sprite descriptors and graphics ROMs. They have no ISA and do not execute guest instructions, so they need behavior models rather than recompilers. Keep each device stateful and board-wired, and use MAME behavior as a reference while validating Golden Axe.

Initial models now exist for all three 16B ASICs. The 315-5197 has separate CPU and OpenGL compute paths for normal tilemap mode. The 315-5196 interpreter rasterizes its sprite plane on CPU; Software mode mixes on CPU, while OpenGL mode applies 16B priority, palette selection, and shadow composition in a dedicated compute pass. The 315-5195 exposes register/latch behavior and standard 171-5797 wiring connected to the revision-selected System 16B bus. The generated `goldnaxeud` build is ready for the first runtime video check, but the ASICs and final output still need user validation. Keep audio disabled through that milestone.

## Board modules and game packages

The intended runtime shape is a small executable/dispatcher loading a board module and a per-game AOT module. The board module owns its bus, timing, interrupts, video, sound, and device wiring. A game module contains only that title's translated routines, dispatch tables, and metadata; it links to the board and shared CPU/chip libraries it needs. The executable selects both modules using the game profile. ROM images remain external inputs, and no module embeds ROM bytes. System 16/18 currently put generated tables for several games inside `mamerecomp_system16a.so`, `mamerecomp_system16b.so`, or `mamerecomp_system18.so`; those tables need extraction into per-game modules, and much of the board runtime still lives in the `mamerecomp` host. CPS-1 now has distinct `libmamerecomp_cps1.so` board runtime and `libmamerecomp_game_<id>.so` AOT module, with `mamerecomp_<game>` as a small runner. Shared CPU runtime libraries are `libmamerecomp_cpu_m68000.so` and `libmamerecomp_cpu_z80.so`; board/game modules link the same runtime instance rather than carrying private CPU state. Neo Geo still builds as `kof98recomp` with a statically linked `neogeorecomp_runtime`; that board runtime needs to move into a loadable board module. A game package such as `.mrom` with exact ROM bytes plus a versioned manifest (set ID, region placement/stride, lengths, CRCs and SHA-256 hashes) is deferred. Never bake ROM images into a shared library.

## Multi-game and multi-board structure

The target is a reusable 68000 recompilation core with board/game profiles, not a separate CPU translator for every title. Keep these boundaries explicit as coverage grows:

Generated 68000 code is produced from a set's program ROM during the development/build pipeline; the game runtime does not recompile ROMs while launching. Package one title per AOT module so adding a title does not rebuild or enlarge a board module. The dispatcher opens the matching board and game modules and passes the game table to the shared CPU runtime. Build each CPU family once as a shared runtime library; share one instance of CPU state/helpers across the board and AOT modules. Device models belong under `core/chips/` and should be shared similarly where their API is stable. The 315-5195 model is extracted to `core/chips/315_5195/`; its current shared library is linked by the host, and a stable device ABI is still needed before on-demand loading. Split generated C into multiple translation units to bound compiler memory.

1. **CPU translation/runtime:** instruction decoding, generated native C, 68000 architectural state, and generic exception mechanics. Reuse this across boards using a compatible 68000 variant; model CPU-specific protection or encryption as a separate layer.
2. **Board profile:** clocks, address map, ROM/RAM regions, interrupt sources, input/DIP wiring, and device connections. A title profile selects a board profile and supplies that game's ROM layout and configuration.
3. **Device models:** reusable chip-family implementations with board-specific wiring and timing adapters. Share a model only when chip behavior and interface match; do not infer equivalence from a common CPU or MAME helper name.
4. **Video path:** reuse ROM decoders, tilemap/sprite engines, and compositor pieces only where graphics formats, registers, priorities, and timing semantics agree. Keep an indexed CPU framebuffer as the per-board reference and make GPU backends consume the same board snapshot.

## System 18 isolation

System 18 has separate profiles/tables in the `mamerecomp_system18.so` package for `shdancer` and the decrypted bootleg `mwalkd`. `System18AddressSpace` selects board-specific MAME wiring: 171-SHADOW leaves region 2 unused and maps the VDP through region 1, while 171-5874 maps the VDP through region 2 and leaves the 512 KiB bootleg's ROM bank 1 empty. Both mirror the VDP at `0xC00000–0xDFFFFF` and keep tile/text RAM windows separate. The interpreted 315-5313 feeds the System 18 compositor at four `vdp_mixing` insertion points; its current Mode 5 scanline renderer covers Plane A/B, Window, and basic linked-list sprites. Shadow Dancer leaves its VDP disabled in observed scenes, while MAME activates it in Moonwalker attract mode; active `mwalkd` output in MameRecomp remains unverified because its 8 MHz I8751 firmware is only validated by an explicit stub, not yet interpreted. Do not link or load the complete System 16B board module as a runtime dependency. Where MAME confirms identical ASICs, split their model into chip-specific libraries (315-5196 sprite generator and 315-5197 tile generator) and keep their APIs/state independent. The generic 68000 translator/runtime and host infrastructure remain common.

System 18 audio has its own `System18Sound`: the shared interpreter runs the 8 MHz Z80, with the 315-5195 PBF command/reply latch, dual 8 MHz YM3438 chips through YMFM OPN, and an initial RF5C68 model with 8 PCM voices and 64 KiB sample RAM. The YM1 timer IRQ is routed to Z80 IRQ0 and the board's sound-command latch pulses Z80 NMI. The sound map follows MAME's fixed/banked ROM, `0xC000–0xDFFF` RF5C68 window, `0xE000–0xFFFF` RAM, and `0x80/0x90/0xA0/0xC0` ports. Mixer timing is still a first pass and must be checked against actual gameplay audio.

Board packages are responsible for clocks, address maps, interrupts, and physical connections between chip interfaces. Load device libraries on demand from the selected board package; do not load every board family's devices.

Do not add speculative video buffering/optimization as part of this port. First implement the visible board behavior and guest timing, then measure before optimizing.

For example, another System 16A title may reuse the 16A compositor after its ROM wiring and register behavior are checked; System 16B needs its distinct mapper/sprite/tilemap behavior, and Neo Geo or CPS hardware needs separate board/video models despite sharing a 68000. This is an architectural direction, not a claim that the current executable already supports those boards. Add a board profile only alongside a runnable title configuration and a reproducible MAME comparison.


## Shinobi System 16A sound topology

The `shinobi` set 6 MAME definition shows several cooperating sound devices, not one DSP paired with an 8048: a 4 MHz Z80 runs the sound program, a 4 MHz Yamaha YM2151 generates FM audio, and a 6 MHz NEC uPD7751C handles sample playback. MAME identifies the uPD7751C as an 8048/MCS-48 clone; its internal 1 KiB program ROM is `7751.bin`, with a separate `epr-11268.1` sample-data ROM. An NEC D8243 expander provides port lines, and the MCU sends sample bytes to an 8-bit R-2R DAC. The Z80 issues the MCU command and ROM-bank selection; the YM2151 output port controls the MCU reset and interrupt lines. These signals and their timing are board behavior and need to be modeled along with the individual chips.

Listening to the current build confirms the YM2151 supplies the music; the user also reports that some sound effects come from the Yamaha, while character death shouts and some other effects use the uPD7751 sample path. Do not route all effects through the MCU by assumption. The exact effect-to-chip mapping still needs to be recorded while implementing and testing the sample command table.

For this project, keep the Z80 as the sound-program CPU and interpret it first. A general MCS-48 interpreter is a reusable route for the uPD7751 MCU; a dedicated recompilation backend is not a prerequisite. The YM2151 is a register-driven FM synthesizer, not a CPU/DSP instruction stream to translate. MAME's `segas16a.cpp` and `mcs48` device are the current behavioral references. The runtime now has an initial interpreted MCS-48 core connected to the uPD7751's 1 KiB firmware, D8243 port expander, sample-data bus, YM2151-controlled reset/IRQ, and P1 DAC output. The DAC is a separate sampled-audio source summed with the YM2151 output. This path has passed a headless execution check but still needs audible gameplay validation and instruction/timing audits; it is not yet considered hardware-accurate.

## Guiding constraints

1. Reproduce guest-visible CPU, bus, video, and audio behavior before optimizing.
2. Keep board-specific hardware models separate from reusable CPU and chip-family engines.
3. Prefer verified timing/register behavior over shortcuts such as HLE.
4. Record known approximations and validate output against a matching MAME state before calling a subsystem accurate.

## System 16A video acceleration

Keep the 68000/event scheduler and video backend separate. The CPU advances on guest cycles and raises IRQ4 from the board's vblank schedule. Count physical vblank events separately from IRQ4 acceptance: host input events follow elapsed board frames, while accepted IRQs remain CPU-interrupt diagnostics. A video backend consumes a coherent board-state snapshot at a video event; GPU execution time and presentation cadence must never advance guest cycles or determine IRQ timing.

For System 16A, the GPU experiment accelerates **tilemap/text lookup and composition**; the 449.8M-cycle Shinobi checkpoint currently matches the CPU output exactly for palette indices, priorities, and shadow flags. System 16B has its own tilemap compute shader and a second compute pass for the 315-5196 priority/palette/shadow mixer. The 315-5196 descriptor interpreter still produces the indexed sprite plane on CPU; moving descriptor rasterization to GPU is later work. Both paths share only the OpenGL context, atlas, and buffer infrastructure, not board rules.

The remaining System 16A GPU stage is to parse its sprite descriptors on CPU and submit GPU quads while preserving descriptor order and pen-15 row termination; 16A rules stay separate from the 16B mixer. Palette conversion and shadow/highlight output are also still done on CPU. The compute backend retains a hidden SDL OpenGL context. An optional SDL3 presenter displays the final 320×224 frame in a separate decorated window at the emulated 60 Hz vblank cadence; each compute pass explicitly rebinds its hidden context after SDL presentation. Its first-call timing includes context and shader setup, so performance needs repeated-frame measurements before deciding whether the GPU path is faster overall.


## Golden Axe checkpoint

The available `goldnaxe.zip` is MAME's `goldnaxe` set 6 (US), declared as System 16B with an Intel 8751 protection MCU (not an FD1094-encrypted 68000). The 68000 frontend is reusable, and its two 0x40000 byte ROMs are interleaved into a 0x80000 region. Its board path is not a drop-in System 16A profile: MAME maps the address space through the 315-5195, with separate 315-5197 tilemap and 315-5196 sprite ASICs, and configures the 315-5248/315-5250 multiplier/compare-timer fragment for this ROM board. The audio program is Z80 + YM2151 + uPD7759 sample player, distinct from Shinobi's YM2151 + uPD7751/MCS-48 firmware + external DAC path.

`run-full` selects the bus and video path from the profile revision. Golden Axe revision B uses the 315-5195 mapper, 315-5197 tilemap path and 315-5196 sprite path; it does not route through the System 16A renderer. Use `goldnaxeud` for the first video test. The original `goldnaxe` set still needs its 8751 interface, and the 16B audio path is deferred. An earlier forced-16A experiment was removed and is not a valid compatibility test. The current ASIC models still need runtime/video validation. See [TODO.md](TODO.md) and [GOLDEN_AXE_PROGRESS.md](GOLDEN_AXE_PROGRESS.md) for staged work.
