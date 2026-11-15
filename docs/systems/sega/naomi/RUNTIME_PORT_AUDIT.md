# Naomi runtime port audit

This is the active implementation checklist for the Naomi runtime. The target
is Marvel vs. Capcom 2 booting from the official Naomi BIOS with MultiArcadeRecomp's
SH-4 AOT path and a complete Flycast-derived interpreter fallback. REIOS is
excluded. Do not treat an isolated CPU probe, the REIOS diagnostic path, or a
post-run VRAM dump as a successful system boot or rendered frame.

Flycast `1b63c8a3677cc8eb1292863c4fe8fc8f94674fc8` is the source of truth for
Naomi runtime behavior and the implementation reference. Use the local MAME
checkout for documentation and targeted opcode/device comparisons; prefer it
for runtime behavior only when evidence shows it is more accurate than
Flycast. Keep imported code's original license and record upstream file
mappings.

## Current layers and missing work

### CPU interpreter fallback contract

For every Naomi CPU that has an AOT path in MultiArcadeRecomp, the Flycast
interpreter is the semantic fallback at an unregistered or invalid AOT entry.
The handoff is instruction-boundary transparent: interpret until control reaches
a valid AOT entry, then resume AOT without changing guest state or the Naomi
device timeline. This is CPU execution behavior, not Naomi device runtime.
Do not add Flycast Dynarec/recompiler code to either side of this contract.

The SH-4 interpreter adapter is currently wired into the existing AOT
dispatcher, but matching opcode-table masks is only an opcode-family coverage
check. It does not establish complete Flycast behavior: register variants,
exceptions, MMU/cache effects, cycle accounting and scheduler interaction still
need source-by-source parity work. Port Flycast interpreter semantics into the
SH-4 fallback and keep the existing MultiArcadeRecomp AOT path intact.

The fallback's basic floating-point arithmetic, comparisons, sign/absolute,
square root and FMAC now use a dedicated Flycast-derived path. Single-precision
operations execute as `float`, double-register operations use Flycast's DR
pair mapping, and FMAC uses one fused operation as Flycast does. The AOT
backend continues to use its existing helpers. Other FPU corner cases and
FPSCR exception behavior remain part of the parity audit.

FPSCR loads and FRCHG/FSCHG now follow Flycast's `UpdateFPSCR` behavior for
cause-field clearing and bank/mode bits. The interpreter applies the selected
RM rounding mode around each FPU instruction and restores the host mode
afterward. DN flushes subnormal inputs and results in the arithmetic, compare,
square-root, FMAC and vector handlers currently ported. Conversion/transfer DN
coverage and detailed floating-point exception state remain incomplete.

The Naomi ARM7 has no MultiArcadeRecomp AOT dispatcher today. Its Flycast
ARM7TDMI interpreter runs directly as the AICA auxiliary CPU; it must remain a
Flycast interpreter port, not be replaced with an AOT or Dynarec backend. The
AICA DSP microprogram interpreter is another processor interpreter and is
already adapted from Flycast, while AICA voice generation, mixing, timing and
register behavior remain runtime work. Neither the ARM7 CPU nor AICA DSP
interpreter makes the rest of the AICA runtime complete.

| Layer | Present in MultiArcadeRecomp | Upstream comparison | Port still required |
| --- | --- | --- | --- |
| PVR2 / TA / framebuffer | Register window, SPG counters/field number, 16 MiB VRAM, store queues, TA packet capture/framing, decoded scene primitives for all 15 vertex formats, sprites and modifier triangles. PVR DMA, interleaved VRAM and FB_R scanout are present. `STARTRENDER` latches FB_W control/SOF1, stride from `0x4c`, surface dimensions from `TA_GLOB_TILE_CLIP`/`SCALER_CTL`, FB pixel clipping from `FB_X_CLIP`/`FB_Y_CLIP`, and the Flycast region-array tile bounding rectangle. TA FIFO writes are ignored until `TA_LIST_INIT`/`TA_LIST_CONT` opens a context; list init seeds OPB pointers and initializes `TA_NEXT_OPB`/`TA_ITP_CURRENT`; STARTRENDER resolves context addresses from opaque/translucent/punch-through OPBs using Flycast's first-tile walk. The active parser decodes PCW fields from their Flycast bit positions (`ListType` 24..26, `User_Clip` 16..17, object control 0..7) and follows the TA continuation FSM. A software geometry pass is connected to the Flycast `WriteFramebuffer` adapter: it rasterizes strips and sprites with region scissoring, per-primitive user/tile clipping and PVR depth comparison. Texture sampling reads VRAM and palette RAM for twiddled and planar RGB/YUV, VQ, Pal4/Pal8, 1555/565/4444, nearest, bilinear and mipmapped/trilinear sampling. LOD comes from perspective-correct UV derivatives; mip offsets and square mip dimensions follow Flycast `TexCache.cpp`. Triangle commands retain pass/list ordering, apply region-header Z-clear and use Flycast's translucent autosort rule when requested: each polygon parameter receives its key from the minimum Z word across all its vertices, and stable sorting preserves triangle order inside each strip. This still decodes the FIFO directly into custom scene primitives; it does not yet reproduce Flycast TA contexts and their complete per-list geometry/material state. Remaining gaps include full TA-context construction, OPB/pass-chain semantics, perspective/depth precision, exact shader/fog/blend behavior and modifier-volume operations. | Flycast `core/hw/pvr/`, `core/rend/{sorter,texconv,TexCache}.cpp`, and renderer shaders | Port the full Flycast-derived context/parser path, all render passes and OPB-selected contexts; complete shader semantics, depth/blending, fog and modifier-volume effects. Feed all supported scene passes into FB_W, then scan out via FB_R/SPG. |
| SH-4 AOT + interpreter | `Sh4Dispatcher` uses generated-code byte guards and runs the Flycast adapter on AOT misses, then rejoins at a registered entry. Static audit matched opcode masks against all 212 Flycast opcode-table entries. MMU-enabled data translation consults the UTLB; instruction fetches use the ITLB with Flycast LRUI replacement; TLB/address faults update TEA/PTEH and unwind to the exception vector. Interpreter FIPR/FTRV/FSRRA/FSCA now use Flycast handlers and its exact FSCA coefficient table. | Flycast `core/hw/sh4/interpr/{sh4_interpreter,sh4_opcodes,sh4_fpu}.cpp` and `core/hw/sh4/modules/mmu.cpp`; MAME `src/devices/cpu/sh/` only for targeted comparison | Complete MMU corner cases and register side effects, cache semantics, exception/interrupt entry, address-dependent waits and full scheduler/cycle behavior. Preserve each Flycast handler's exact variant behavior, including `SR.S` restrictions for `MAC.L` and `MAC.W`. Consult MAME only when a specific semantic discrepancy is demonstrated. |
| Naomi boot and cartridge | BIOS mapping, M2 ROM board, encryption, G1 DMA and game profile for MvC2 | Flycast `core/hw/naomi/{naomi,naomi_cart,m2cartridge}` and Naomi BIOS loading path | Complete official BIOS-to-cartridge startup state and all required board behavior. Keep REIOS out of normal construction and dispatch. |
| Naomi bus / Holly | Naomi address-space callbacks, interrupt controller, selected G1/DMAC/PVR DMA paths | Flycast `core/hw/holly/` and Naomi bus mappings; MAME `src/mame/sega/naomi*.cpp` | Complete register map, interrupt source/priority behavior, DMA channels, bus arbitration and unmapped/open-bus behavior needed by the game. |
| ARM7 / AICA | Flycast ARM7 interpreter is connected to shared 8 MiB sound RAM. AICA control includes timer/sample ticks, SCIEB/SCIPD and MCIEB/MCIPD/MCIRE interrupts, internal DMA between wave RAM and registers, Holly external IRQ, and a 128-step-per-sample AICA DSP interpreter with MPRO/COEF/MADRS/EXTS, TEMP/MEMS/MIXS and EFREG. | Flycast `core/hw/arm7/` and `core/hw/aica/` | Port the 64 sample voices, effects, mixer, remaining register/timing behavior and host audio output. The DSP microprogram now executes, but voices do not yet feed it. |
| Maple/JVS and board peripherals | Selected Maple DMA/JVS endpoint and EEPROM/security paths | Flycast Naomi peripheral and Maple implementations; MAME device maps | Audit and connect all MvC2-required inputs, interrupts, reset/state and board peripheral register behavior. |
| Host presentation | SDL presenter consumes one diagnostic RGB readback after the instruction budget | Flycast renderer/presenter lifecycle | Tie PVR framebuffer changes and frame/vblank timing to a live host surface. The PVR device owns guest VRAM/framebuffer semantics; SDL only presents completed guest frames. |

## Required implementation order

1. Finish the Flycast SH-4 interpreter and Naomi CPU runtime semantics while
   preserving AOT-first dispatch and the official BIOS entry path.
2. Complete Naomi cartridge, Holly, DMA, interrupt and peripheral behavior
   used during BIOS initialization and game handoff.
3. Port the full upstream-derived PowerVR2 scene path and connect rendering
   to the guest framebuffer and register-driven scanout/presentation.
4. Port the complete ARM7/AICA runtime, including the 64 voices, DSP-fed
   mixer/effects and host audio output.
5. Validate the official-BIOS MvC2 boot and rendered gameplay frame, then
   validate input and sound.

The Naomi module compiles with the Flycast FPU adapter. Its focused smoke
checks passed. Paired official-BIOS runs at 150 million instructions reached
the same `PC=0x0c031e54` and device counters in hybrid and interpreter-only
modes; RAM still differs by one word because an existing generated AOT body
for opcode `0x403e` writes `GBR` where Flycast interprets it as `SSR`. The AOT
was left untouched. Neither run submitted TA data or started a render. The
cartridge handoff, PVR2 rasterization, AICA DSP/mixer and full runtime remain
unfinished; continue source-driven porting and update each row as those layers
are implemented and connected. The AICA internal-DMA and MCI interrupt bridge
have a focused memory/IRQ harness; the latest 150M hybrid BIOS run remains at
the same PC and PVR state as before.
