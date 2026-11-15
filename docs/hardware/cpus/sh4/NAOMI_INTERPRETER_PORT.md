# Naomi SH-4 interpreter port

## Direction

### Active porting direction

The goal is a complete Naomi runtime that runs Marvel vs. Capcom 2 from the
official Naomi BIOS through the existing SH-4 AOT plus interpreter pipeline.
REIOS is outside the target path. Port the complete SH-4 interpreter and the
Naomi device/runtime dependencies from Flycast, using MAME's Naomi devices and
software PowerVR2 implementation where they provide the needed behavior or a
more direct framebuffer path. Keep upstream provenance and license notices
for every imported component and adapt machine-global services to Naomi-owned
state.

The provisional triangle rasterizer and post-run VRAM viewer are not the
target rendering architecture. The PVR2/TA scene pipeline must produce the
framebuffer consumed by the presenter, including the scene lists, textures,
sprites, tile ordering, depth/blending/fog, framebuffer layout and scanout
behavior required by MvC2. The renderer should be built from the upstream
runtime, not expanded by patching the provisional rasterizer.

Maintain the boundary by component:

| Component | Active implementation | External reference role |
| --- | --- | --- |
| SH-4 instruction interpreter | `core/cpus/sh4/flycast_interpreter.*` adapted from Flycast | MAME opcode/behavior comparison only |
| SH-4 AOT translator | MameRecomp's existing translator | independent of both emulators |
| Naomi bus, Holly and devices | `src/naomi`, adapted from Flycast | MAME cross-checks only, not runtime code |
| PVR2, TA and framebuffer renderer | `src/naomi`, port from Flycast; MAME software PowerVR2 as an implementation source where useful | Cross-check Flycast/MAME behavior and the game's framebuffer output |

The staged upstream trees are source material. When a component is ported, add
its upstream file mapping and status to the corresponding `UPSTREAM.md`; do not
leave an active provisional or duplicate implementation beside the port under
an ambiguous `common` or `runtime` name.

Keep the existing SH-4 AOT translator and its generated entry points as the
normal execution path. Use the Flycast SH-4 interpreter only when the live PC
has no registered AOT entry, and return to AOT dispatch as soon as execution
reaches a registered entry again. Naomi devices and memory decoding stay in
`src/naomi`; generic instruction semantics belong in `core/cpus/sh4`.

Keep the CPU interpreter distinct from the Naomi runtime. The SH-4 and ARM7
interpreters execute CPU instructions when AOT has no entry; the runtime
models the bus, memory map, scheduler, interrupts, PVR2, AICA and other
devices those instructions access. Porting a CPU interpreter does not fill in
those device models, and device execution must not depend on whether the
current instruction came from AOT or the interpreter.

Flycast's **Interpreter** is the SH-4 instruction-semantics source because the
selected Naomi game runs there with the available BIOS. Do not bring its
Dynarec: MameRecomp already has its own SH-4 AOT translator, and a second
host-code generator would obscure which compiler is under evaluation. MAME
remains useful for cross-checking Naomi hardware behavior; it is separate from
the Flycast-derived runtime being ported and from the SH-4 instruction
interpreter. The staged source
snapshot and upstream revision/license are recorded in
`docs/third_party/flycast-sh4/UPSTREAM.md`.

## Current implementation checkpoint

### Naomi BIOS versus REIOS boundary

The active Naomi path boots the official Naomi BIOS. In Flycast standalone,
the `UseReios || !nvmem::loadFiles()` fallback is inside the Dreamcast console
content branch in `core/emulator.cpp`. Naomi arcade startup instead loads the
cartridge, NVRAM and then calls `naomi_cart_LoadBios()`. That loader searches
the game/parent archive and `naomi.zip`, and throws if no usable default or
game-specific BIOS was loaded. The RetroArch `LoadHle(new_system_dir)` snippet
is therefore not the standalone Naomi arcade startup path.

Flycast does contain optional Naomi REIOS HLE code: `core/reios/reios.cpp`
has `reios_boot()` and `reios_setup_naomi()`, which can seed a Naomi cartridge
image and SH-4 state when explicitly invoked. This is separate from
`reios_init()` in `core/reios/gdrom_hle.cpp`, which only registers the generic
GD-ROM HLE scheduler callback. MameRecomp's Naomi BIOS runtime must not call
`reios_init()` or use REIOS to replace the Naomi BIOS. The Naomi REIOS helper
and its diagnostic AOT roots remain an explicitly selected test path only;
they are not part of normal Naomi startup. The official-BIOS path is the
source of truth for Naomi bring-up.

### Scope correction: CPU interpreter is not the PVR2 runtime

The current work is **not** a complete Flycast runtime port, and it does not
include the Naomi 3D video pipeline. The SH-4 adapter covers the opcode
families in Flycast's table, but exact CPU/runtime behavior remains
incomplete. `NaomiPvr2` has register storage, SPG scanline timing, VRAM
access, TA FIFO parsing and decoded scene primitives. There is no active TA
rasterizer: `STARTRENDER` decodes the submitted scene and schedules the
render-done interrupt, but it does not draw into `FB_W` or a host framebuffer.
`captureFramebuffer()` reads the guest scanout selected by `FB_R_*` from PVR
VRAM; it does not return rasterized TA output. Keep this real guest scanout
readback as the framebuffer interface and port Flycast's scene renderer into
it instead of reviving the removed bring-up triangle rasterizer. The latest
500M-instruction official-BIOS run wrote VRAM but submitted zero TA bytes and
issued zero `STARTRENDER` commands, so it did not produce a game frame.

These are separate implementation layers. Flycast SH-4 instruction semantics
belong in `core/cpus/sh4`; Naomi memory, Holly, PVR2 registers and TA ingress
belong in `src/naomi`; scene-list parsing and rasterization are PVR2 runtime
work, not CPU interpreter work. Flycast's PVR references are in
`/home/luiz/git/flycast/core/hw/pvr/{pvr.cpp,pvr_regs.cpp,pvr_mem.cpp,ta.cpp,ta_vtx.cpp,ta_ctx.cpp,ta_util.cpp,spg.cpp}`.
MAME's Naomi map and software PVR2 reference are
`/home/luiz/git/mame/src/mame/sega/naomi.cpp` and
`/home/luiz/git/mame/src/mame/sega/powervr2.{cpp,h}`. MAME's `powervr2.cpp`
contains a software TA/ISP/TSP renderer; it is a useful behavioral reference,
but its MAME framework dependencies need adapting.

Flycast's PVR implementation has now also been snapshotted at
`core/third_party/flycast-pvr/` (same upstream revision as the SH-4 snapshot,
GPL-2.0). This is source for the port, not a linked backend. It includes the
TA parser and frontend but expects Flycast's global machine and renderer
services. The active PVR parser was corrected to stop treating reserved PCW
type 6 as a list header: polygon/modifier (4) and sprite (5) select the list,
vertex packets (7) belong to that list, and EOL (0) ends it. It now also
follows Flycast's 32-byte FIFO framing for 64-byte polygon headers and vertex
parameters, skipping the second half instead of decoding its bytes as a new
PCW. PVR dumps and the Naomi probe summary expose PCW type counts and per-list
packet counts. Completed lists now retain byte ranges into the original TA
capture (`.ta-lists.txt` alongside `.ta.bin`), giving the renderer a concrete
input without duplicating the potentially large FIFO payload. This also lets
the next bring-up run distinguish “FIFO received data” from “TA completed a
scene list.” `TA_LIST_CONT` follows Flycast's `ta_vtx_ListInit(true)` behavior:
every write starts another TA render pass, ends the active-list context, and
keeps already submitted FIFO bytes available for the later renderer.
The PVR soft-reset register follows Flycast's `ta_vtx_SoftReset()` and resets
the TA parser state without erasing the captured stream or finished lists.

The active TA packet-size decoder now ports Flycast's `TaTypeLut` rules from
`ta.cpp` and `ta_const_df.h`: polygon header lengths and vertex lengths are
selected from the exact object-control encoding, including invalid combinations,
instead of the previous broad texture/colour heuristic. Sprite headers remain
32 bytes and sprite vertices are tracked as A/B 32-byte halves. The `.ta-lists`
diagnostic now also records each decoded polygon/sprite header and its vertex
byte span, so captures can be compared with Flycast's parser. This adapter is
not the upstream TA context/region-list renderer and does not produce pixels.
Port Flycast's TA context, region sorting, texture pipeline and renderer
lifecycle before connecting completed frames to the presenter.

The 2026-11-02 official-BIOS interpreter run at 500M instructions ended at
`PC=0x0c035c60`. PVR counters were `TA=0`, `YUV=0`, `VRAM=9,648,512 bytes`,
356 register writes, and `STARTRENDER=0`; 57 Holly interrupts were accepted.
This confirms that the current boot does not reach scene submission, so the
rasterizer remains unexercised until the BIOS/cart handoff is corrected.

Parser verification against the saved authentic 224-byte Flycast capture
(`artifacts/naomi-mvsc2-20261031/from-build-tmp/mvsc2-pvr-1500m.ta.bin`)
decoded two type-4 control/polygon packets and one type-5 sprite with a 64-byte
A+B vertex span, followed by the two completed list spans. This validates the
new packet-boundary and sprite-pair accounting on captured input, not the
rasterizer or the game boot. A fresh MameRecomp run at 1.25B instructions
ended at `PC=0x0c045fe0`; its final TA buffer held only two type-4 packets and
EOLs (128 bytes), `STARTRENDER=0`, and `FB_R_CTRL.fb_enable=0`. Thus that run
still shows no game scene reaching the renderer.

The Naomi CLI currently constructs the optional SDL viewer only after the
entire requested SH-4 instruction budget has run and the final framebuffer
readback has been copied. `--window` is therefore a post-run diagnostic window,
not a live display; a multi-billion-instruction run will take a long time
before SDL opens. This is separate from the SH-4 boot/runtime correctness.

### PVR DMA checkpoint

The PVR system-bus DMA window (`0x005f7c00`) is now connected to the Naomi
address space. Its execution path follows Flycast's
`core/hw/pvr/pvr_sb_regs.cpp::do_pvr_dma`: it checks SH-4 DMAOR, copies the
programmed dword range in the selected direction, updates SAR0/DMATCR0/CHCR0,
clears the start bit, and raises Holly's PVR-DMA interrupt. This is a narrow
device path, not a claim that the overall Naomi runtime is complete. PVR sort
DMA, the full SH-4 DMAC, complete TA scene construction and rasterization are
still outstanding.

PVR command-register behavior was also checked against Flycast's register
handler: `STARTRENDER` and `SOFTRESET` are commands rather than latched values,
`TA_LIST_INIT` is not stored, and `TA_LIST_CONT` starts a pass on every write
while retaining the submitted stream. `STARTRENDER` is now counted/traced,
but no renderer consumes the lists yet. The soft-reset address is `0x005f8008`.

The core PVR and Area 4 writes have been traced, including SH-4 `PREF`/QACR
store-queue routing. The video path still needs complete packet/list parsing,
tile sorting, texture/palette reads and framebuffer output. This video-runtime
work remains independent from the SH-4 interpreter and AOT translator.

### G1 cartridge DMA checkpoint (2026-11-02)

The M2 cartridge's programmed G1 DMA path is connected in
`NaomiAddressSpace`. `GDEN` gates a rising `GDST` request; `GDSTAR/GDLEN`
provide the RAM destination and transfer length; the M2 board's `DMA_OFFSET`
provides the ROM source. Transfers advance in Flycast-sized chunks (up to
1 KiB, 10 SH-4 cycles per byte), respect the M2 4 MiB bank mapping and cart
boundary, update `GDSTARD/GDLEND`, and raise Holly normal interrupt bit 14 on
completion. Clearing `GDEN` aborts an active transfer. Set
`MAMERECOMP_NAOMI_TRACE_G1_DMA=1` to log starts and completion/progress.

This closes one missing board operation; it does not establish that MvC2's
observed BIOS handoff uses G1 DMA. Existing synchronized traces show the BIOS
only reading the 0x500-byte ROM boot header and no G1 start before the earlier
probe ended. The guest-visible cart-to-RAM handoff remains unresolved, the TA
scene/raster path is incomplete, and AICA synthesis is not connected. A
visible Naomi frame has not yet been verified in MameRecomp.

### DMA writes and SH-4 AOT invalidation (2026-11-02)

All Naomi bus-master writes into writable memory now use the same SH-4 code
line epoch guard as CPU stores: M2 G1 cart DMA, PVR system-bus DMA, Holly
channel 2, Maple responses, and SH-4 store-queue writes that land in ordinary
RAM. PVR store queues remain routed to PVR registers/TA and are not reported as
RAM writes. The address-space observer filters out device/unmapped destinations
so DMA traffic does not grow the AOT epoch map for non-memory ranges.

This closes the invalidation contract for device writes; it does not prove that
BIOS code is copied correctly or that the game reaches TA rendering. The
official BIOS boot and first visible frame remain the validation targets.

### M2 encrypted-word byte order (2026-11-02)

The M2 cipher reader is now aligned with Flycast's
`M2Cartridge::ReadCipheredData`: for both the ROM-board stream and M2 RAM, the
byte at the lower byte address supplies bits 15:8 of the cipher input word.
The previous adapter assembled these words in the opposite order. This could
corrupt BIOS copies/decompression from the M2 board before the game reached
PVR scene submission. Build succeeds after the correction; the official-BIOS
boot and first-frame check still need to be repeated to establish whether this
was the cause of the callback loop.

- `Sh4Context` has an optional one-instruction `interpreter_step` callback.
- `Sh4Dispatcher` keeps its existing AOT lookup and, on a miss, repeatedly
  steps the interpreter until an AOT entry, interrupt, stop, or instruction
  budget boundary is reached.
- Generated SH-4 entries now carry an FNV-1a 64-bit signature and source
  range for their complete translated image. Before first use, the dispatcher
  compares live guest bytes against the translator input; a mismatch fails
  closed to the interpreter. It caches that decision by physical 32-byte
  lines and rechecks after writes through `Sh4Context`, preventing a captured
  RAM block from being used after its code bytes change. ICBI continues to
  invalidate its line. Device DMA writers report writable-memory changes via
  the Naomi address-space observer.
- Native and interpreted guest-instruction counters are tracked separately
  and included in the Naomi probe summary.
- The connected adapter currently covers common integer arithmetic and
  logical operations, shifts/rotates/division steps, MAC/MUL operations,
  scalar loads/stores, PC-relative and GBR addressing, system-register
  transfers, delayed/direct branches, and the single/double precision FPU
  operation families represented by the existing Naomi FPU helpers.
- FPU-disabled instructions stop at the instruction boundary with
  `EXCEPTION_BOUNDARY` and enter the SH-4 exception vector; TRAPA records TRA
  and enters the same vector. The interpreter's RTE path fetches the delay-slot
  opcode under the old MD/MMU mode, restores SSR, executes the prefetched opcode
  under the restored register bank and privilege state, then resumes at SPC.
  The generated AOT RTE helper remains separate and its delay-slot fetch path
  still needs equivalent old-mode handling.
  Store-queue PREF delegates to the Naomi board callback. ICBI invalidates the
  matching translated 32-byte physical line, so subsequent execution there
  goes through the interpreter. OCB operations are no-ops in the current
  write-through Naomi memory model, with a callback for board-specific cache
  effects. Illegal opcodes enter Flycast's IllegalInstr/SlotIllegalInstr
  vector; known unsupported variants and rejected memory/cache operations
  stop with their own categorized reason.
- `LDTLB` transfers CCN PTEH/PTEL/PTEA into the UTLB slot selected by
  MMUCR.URC. Data accesses and interpreted instruction fetches consume these
  entries for translation; full CCN side effects and exception/cycle details
  remain incomplete.
- External SH-4 interrupts are sampled at Flycast's 448-cycle scheduler slice
  boundary rather than on every instruction. Timing is still provisional:
  the AOT emitter and interpreter currently charge one cycle per instruction
  and do not model Flycast's opcode issue-unit overlap or external bus wait
  states. Upgrade cycle accounting before treating video/IRQ timing as
  hardware-accurate.
- `run-naomi-sh4 --interpreter-only` disables AOT lookup for a controlled
  runtime comparison while keeping the same Naomi memory map, interrupts,
  devices, video and BIOS boot setup. This mode can be built without generated
  SH-4 sources; the interpreter starts at the official BIOS reset vector. The
  active Naomi runner has no REIOS boot mode. Normal hybrid boot registers
  generated entries when those sources are configured.
- Latest boot-path opcode fix: `0x421c` is `SHAD R1,R2`. The SHAD/SHLD
  decoder now masks only the fixed opcode bits (`0xf00f`); the previous mask
  incorrectly treated the variable `Rm` field as fixed. The adjacent `0x8nmd`
  displacement load/store family also uses `0xff00` matching and the encoded
  `Rm` base register. Preserve this entry in the rollback notes while adding
  subsequent opcodes.
- This remains an adapter using Flycast semantics, not yet a complete
  source-level port of every Flycast interpreter operation. Basic UTLB/ITLB
  translation and MMU exceptions are now wired through SH-4 data accesses and
  interpreted instruction fetches. Full cache behavior, address-dependent
  waits, and the complete scheduler contract still need work.
  Neither interpreter-only nor the hybrid build has produced a verified
  playable boot/frame.

### Basic MMU translation and fault unwind (2026-11-02)

Added Flycast-derived UTLB matching (page sizes, ASID/shared matching,
permissions, first-write and physical P4 remap), ITLB fill/replacement using
`MMUCR.LRUI`, URC updates, store-queue permission/alignment checks, and
MMUCR.TI invalidation. TLB misses, multi-hits, protection faults, first writes
and address errors now write TEA/PTEH, enter the appropriate exception vector
and unwind the in-flight instruction. A focused build of `mamerecomp_naomi`
passed. Short official-BIOS runs in interpreter-only and hybrid modes both
retired 5 million instructions to the budget at `PC=0x800000a2`, with no
exception or unresolved target. They did not reach cartridge activity, TA
submission or a visible frame; the full MMU and BIOS/game path remain to be
validated.

### Hybrid run checkpoint (2026-11-02)

An isolated `build/naomi-hybrid` configuration links the existing generated
SH-4 entry points with the Flycast-derived interpreter/runtime. The official
BIOS run reached 1.2 billion instructions without an interpreter fallback;
that run predates live AOT code guards and must be repeated before treating
its callback loop as valid execution.
Holly accepted 275 interrupts; PVR counters showed 1,024 total TA bytes,
9,648,512 VRAM bytes written, 440 register writes and two completed lists.
The final captured TA stream was only 128 bytes: two type-4 headers and two
EOL packets. `STARTRENDER` was never written, and `FB_R_CTRL=0x00800004` has
`fb_enable=0`; the direct VRAM readback is therefore black even though the
registers describe 640x480 RGB565. Execution was still in guest code at PC
`0x0c04b88c` when the instruction budget expired, with all 1.2 billion
instructions handled by AOT and zero interpreted. This is progress through
the BIOS boot, but not a verified visible frame or playable boot. A trace
showed repeated calls to the RTS stub at `0x0c04b88c` from a task dispatch
loop. The RAM capture contains RTS/NOP there, but the BIOS can later expose
different EPR bytes at the same address. Earlier AOT registration used only
the PC and could select stale translated code. The new byte guard addresses
that case; rerun the official-BIOS boot to see whether execution leaves the
loop and submits a scene. A rasterizer cannot fix a missing `STARTRENDER`. The normal
CLI summary now reports render-start count and framebuffer enable state. The
separate REIOS diagnostic run reached a different state and must not be used
to judge official-BIOS startup. Keep Flycast as the implementation path and
MAME as a hardware cross-check only.

### Live AOT guard boot check (2026-11-02)

Regenerated the complete MVSC2 AOT source set with live-byte hashes, rebuilt
`mamerecomp_naomi.so` and the host target, then ran the official BIOS. At 100M
instructions the run stayed in the BIOS with 99,999,994 native and 6
interpreted instructions. At a 1.2B budget, execution stopped earlier at
`0x0c001dc8` after 195,722,255 instructions (195,722,151 native, 104
interpreted), with one accepted Holly interrupt. The guard exposed a
previously hidden AOT/interpreter boundary; the top-level dispatcher now
passes unregistered return PCs through the interpreter path. The remaining
unresolved target is reached after interpreted `CMP/EQ #0,R0` (`0x8800`) and
`BT 0x0c001dc8` (`0x8939`) at `0x0c001d50/0x0c001d52`. PVR had 8,388,688 VRAM
bytes and 173 register writes, but no TA/YUV submissions, no `STARTRENDER`,
and scanout remained disabled. No visible frame has been verified.

## Port requirements

1. Replace Flycast's singleton/global context and memory callbacks with the
   Naomi-owned context and `Sh4Bus`; do not create a second machine runtime.
2. Preserve SH-4 delay slots, SR/FPSCR bank changes, exceptions, and instruction
   PC semantics at the per-instruction boundary.
3. Report each fallback with CPU/backend, reason, PC, opcode, and next PC using
   the project's categorized interpreter trace.
4. Keep translated execution first. The generated blocks carry a source-byte
   hash, and writes through `Sh4Context` advance physical code-line epochs so
   the dispatcher rechecks changed blocks. Naomi DMA/device writers use the
   address-space observer to advance the same epochs for writable memory.
5. Add the adapted CPU source to the shared SH-4 target, not to the Naomi board
   runtime source list. Naomi-specific bus wiring is the only board adapter;
   the address-space DMA observer is the board's AOT invalidation connection.

## Build note

`build/naomi-hybrid` was configured separately because the existing
`build/release` cache references absent generated directories for unrelated
modules. Both `mamerecomp` and `mamerecomp_naomi` built there with Naomi's
existing generated translations enabled. The run results above are from that
isolated binary; they still do not validate a gameplay boot or rendered frame.

The AOT `TRAPA` helper was found to stop execution as a fatal placeholder,
while the Flycast-derived interpreter already enters the SH-4 trap exception.
It now calls the shared exception-entry routine (`EXPEVT=0x160`, `TRA=imm<<2`,
vector `VBR+0x100`, saved PC after the trap instruction). The Naomi module and
host rebuilt successfully after this change; a runtime boot check is still
needed. This CPU fix does not add PVR polygon rendering.

### P0/P1/P2 AOT alias dispatch check (2026-11-02)

The 195.7M stop was not a missing opcode or a failed trap. The BIOS branched to
`0x0c001dc8`; lookup found the translated entry registered as `0xac001dc8`,
but the generated function's PC switch received the unmatched P0 alias and
returned without executing. The dispatcher now enters the generated function
with the exact registered virtual alias. Rebuilt both Naomi module and host.
At 250M instructions the probe reached its budget (422 interpreted). At 1.2B
it also reached its budget (1,199,971,394 AOT; 28,606 interpreted), with 275
Holly interrupts accepted and 16 MiB VRAM activity. PVR captured 1,024 TA
bytes and two completed lists, but `STARTRENDER` was still zero and scanout
disabled; execution returned to the BIOS task callback loop at `0x0c04b88c`.
This verifies sustained hybrid BIOS execution, not game boot or visible video.

### Shared opcode timing wired through AOT and fallback (2026-11-02)

The adapter previously charged one provisional cycle per instruction. Added a
compact timing-pattern table generated from Flycast's `sh4_opcode_list.cpp`
(`tools/naomi/generate_flycast_sh4_cycle_patterns.py`), preserving issue cycles,
execution unit, and memory-operation class. Both the interpreter and generated
AOT instructions now retire with the same opcode-aware core cycle model;
delay-slot instructions pass their own encoded opcode. `build_mvsc2u_aot.py`
was rerun so generated C++ actually contains those calls, then the Naomi module
and host built successfully.

The 150M hybrid checkpoint with this rebuilt pipeline ended at
`PC=0x0c031e54` (`63,553,823` AOT + `86,446,177` interpreted), no accepted IRQ,
no TA bytes, no `STARTRENDER`, no register writes, and a disabled/zero scanout.
This run used the regenerated source set without a captured boot-RAM overlay,
so its AOT ratio is not directly comparable to the prior 150M RAM-dump run.
The cycle model is still partial: address-dependent external bus wait states,
MMU translation and the whole Flycast scheduler/device timing contract remain
to port. The boot is still in BIOS and there is no BIOS image to display.
