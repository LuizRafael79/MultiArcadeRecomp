# Naomi recompilation module

## Target and references

The first target is **Marvel vs. Capcom 2: New Age of Heroes**, on the original
Naomi platform. Initial D Arcade Stage 3 is a useful SH-4/Naomi 2 reference, but
it is a GD-ROM title with a security PIC; MvC2 is a cartridge title with a
large encrypted ROM board. Their shared foundation is SH-4, PowerVR2/Holly,
AICA, and Naomi system I/O. The MAME Naomi driver is the board-level source of
truth for MvC2's ROM maps and security variants; Flycast is an additional
behavioral reference for the complete platform.

Additional primary-reference index: [luckyseoul/dreamcast-docs](https://github.com/luckyseoul/dreamcast-docs)
collects Sega's Dev.Box System Architecture manual (TA/PVR registers and buses),
the Dreamcast Hardware Specification Outline, SH-4 manuals, and Comstedt's PVR
reverse-engineering notes. These documents now inform the PVR/Holly and SH-4
work; Naomi-specific behavior is still checked against MAME/Flycast because
the manuals describe Dreamcast hardware.

The user also supplied [Neperos' Dreamcast C-code collection](https://www.neperos.com/article/phojqq6461db3169),
which republishes Lars Olsson's 2004 PVR notes alongside older memory-map and
AICA notes. Its PVR register descriptions, reset scan timings, raster IRQ lines,
TA list commands, and tile-buffer fields are useful implementation clues. The
author explicitly labels the PVR document unofficial and not guaranteed
correct, so runtime behavior is cross-checked against Flycast and MAME before
being treated as hardware fact.

MvC2 uses the original Naomi topology: one SH-4, one 315-6267/Holly PowerVR2,
the ARM7/AICA sound subsystem, and the platform's Maple/JVS I/O path. Maple/JVS
is shared Naomi infrastructure, not an Initial D-only feature; host XInput is
converted to input state behind the 315-6149/JVS bridge. MvC2 does not use
ELAN or GD-ROM. The Naomi 2
board instead adds a second 315-6267/Holly and a 315-6289 ELAN while retaining
one SH-4. These per-game components are recorded in each profile; do not load
Naomi 2 devices for MvC2.

Initial D supplies the SH-4 translator/code generator and a Maple-to-JVS input
path. Its versioned runtime snapshot does not contain a standalone PVR2: it has
an ELAN/Naomi 2 diagnostic framebuffer and game-specific PVR wait/init shims.
That renderer consumes ELAN-finalized scenes, so it cannot render MvC2's Naomi 1
PVR output. Its game-facing Maple shim and ELAN texture DMA are not the same thing as
Holly's Maple command-table DMA. The latter is implemented as a board-level
component using Flycast/MAME behavior, with the 315-6149/837-13551 HLE attached
through a device endpoint. Reused Initial D files retain their source notices;
Flycast-derived files retain GPL-2.0 notices.

| MAME set | Region | ROM board | Security key | ROM region |
| --- | --- | --- | --- | ---: |
| `mvsc2u` | USA Rev A | 171-7919A / Sega M2 crypt | `0002c840` | 120 MiB |
| `mvsc2` | Export/Korea Rev A | 171-7978B | `c18b6e7c` | 136 MiB |

Both sets are registered by MAME as `naomi_state`; the `naomim1`/`naomim2`
machine configuration labels describe ROM-board paths, not the Naomi versus
Naomi 2 mainboard generation. The driver currently marks these games
`MACHINE_NOT_WORKING`, `MACHINE_IMPERFECT_GRAPHICS`, and
`MACHINE_IMPERFECT_SOUND`.

## Implementation boundary

The Naomi module must have a distinct SH-4 code ABI and runtime from the
68000/System 16-family modules. Its first implementation stages are:

1. Keep Naomi game profiles and cartridge-board metadata in
   `mamerecomp_naomi.so`; validate ROM filenames, lengths, CRCs, region gaps,
   and clone/parent merging against the MAME ROM declarations.
2. Connect the staged Initial-D SH-4 decoder/emitter to the new
   `naomi_sh4_context.*` ABI and a static dispatch build step. The context now
   defines the emitter's integer/FPU state and callback bus; an address-to-
   function dispatcher supports indirect calls and guest instruction budgets.
   FPU helper semantics, exceptions, generated MvC2 functions, and instruction
   behavior still need implementation and validation.
3. Implement the MvC2 ROM-board address/decryption path, Naomi BIOS/EEPROM
   inputs, and the SH-4 memory map.
4. Finish PowerVR TA command submission and Holly DMA/interrupt routing to
   one PVR2 for MvC2, then integrate AICA/ARM7 and the Naomi I/O path. ELAN,
   a second PVR2, and GD-ROM are outside this game's device set.

Initial D's `sh4recomp.py` and `codegen.py` are staged in
`core/third_party/initiald_sh4recomp/`. `tools/naomi/analyze_sh4.py` follows reachable SH-4 control flow from the game entry and BIOS reset entry, preserves P1/P2 guest addresses, tracks a conservative set of register constants through delay slots, and emits function manifests.
`tools/naomi/recompile_sh4.py` consumes that manifest and emits C++ plus
registration for `Sh4Dispatcher`; it rejects unknown opcodes, missing direct
call targets, and helpers that lack runtime implementations. Static analysis
of the local `mvsc2u` program ROM (`epr-23062a.ic22`) found 69,054
instruction-in-function visits (40,884 distinct guest PCs) across 1,479
function entries. The generated function bodies total about 5.54 million
characters of C++ before includes/registration, with no reachable opcode left
unknown to the decoder/code generator in this image. The generated translation
unit is 5,682,234 bytes; it compiles and links into `mamerecomp_naomi.so`.
The module exports set-specific registration and a
bounded SH-4 probe. An earlier run appeared to reach `0xac021092`, but
that was a false idle-loop result: when a translated indirect target was
missing, generated C++ kept running after the dispatcher returned. The
dispatcher now unwinds the generated call stack and reports the unresolved
target. Without a BIOS image, MvC2 reaches a guest `JMP @R1` at `0x0c1f808c`
with a zero function pointer at `0xac018028`. The game had initialized its
pointer object at `0x0c3452e0` to `0xac018000` and conditionally copies `0x1c00`
bytes from `0xa0060000` (Naomi BIOS physical `0x00060000`) into that RAM table.

The local Initial D recomp source offers a useful pattern, but not a drop-in
Naomi BIOS image: its SH-4 code generator recognizes statically resolved calls
into `0x0f000000..0x0f001bff` and emits a `shim_bios_hle(ctx, target)` host
bridge instead of requiring translated firmware code. The bridge body itself
is not present in the checked-in source found here. Flycast's REIOS is also an
internal, primitive HLE BIOS replacement, but the Naomi arcade path loads the
real Naomi BIOS; MAME's MvC2 sets likewise include the `NAOMI_BIOS` region.
The probe now loads a user-supplied regional BIOS from `naomi.zip`, validating
against Flycast/MAME CRCs and mapping raw bytes read-only at physical
`0x00000000..0x001fffff`. The local USA dump `epr-21577h.ic27` (CRC
`fdf17452`) has the eight expected `0x0c01xxxx` pointers at BIOS offset
`0x60000` with the guest's little-endian reads. The build pipeline now analyzes
and recompiles both observed BIOS entry points (`0x0c0183a4` and `0x0c0183bc`)
from the copied code fragment; these are four translated functions and 24
instruction visits total. With them registered, a 10-million-instruction run
reaches `0xac021092` and consumes the full budget with no unresolved target,
confirming the self-branch is the real post-initialization wait loop. At that
point SPG is at scanline 260 and Holly has `ISTNRM=0x18`, but IML2/4/6 are zero
and no IRQ is accepted. An optional `MAMERECOMP_NAOMI_TRACE_SYSCTRL=1` bus trace
showed that this REIOS boot path reads only `SB_SBREV` at `0x005f689c` and does
not write any Holly interrupt masks. The read-only revision register now returns
the MAME/Flycast reset value `0x0b`; this did not move execution beyond the
self-loop or produce PVR submissions. Treat the zero masks as a symptom, not a
proven cause. The current runtime now enters through the real Naomi BIOS reset vector. The BIOS AOT now follows a copied 32-byte trampoline at `0x8c0000e0` and exits at `0x8c0001a2` after 136 instructions; determine how Naomi hardware supplies that next RAM stage.
A no-dump REIOS mode can later provide proven services as host callbacks,
following the Initial D pattern rather than copying a guessed BIOS blob.

The probe advances PVR2 SPG from the SH-4 instruction budget, raises
VBlank-in/out in Holly at the reset scanlines, models Holly status/masks/priority,
and accepts SH-4 IRLn levels 2/4/6 with exception vectors and `RTE` delay-slot
return. The post-BIOS 10-million-instruction probe confirms VBlank-in/out at
scanline 260 (`ISTNRM=0x18`), but masks are zero and no IRQ is accepted. SPG
cadence presently uses one approximate cycle per translated
instruction; instruction-specific SH-4 timing and cycle-visible bus ordering
are not implemented. The report still has 523 dynamic indirect sites
(514 `JSR @Rn`, 7 `JMP @Rn`, 2 `BRAF`) and 26 direct targets outside the
supplied image; these are routed through the dispatcher and may stop at runtime
if no translated entry/service is registered. Scalar FPU helpers and basic
double-pair operations are present, but FPU status/exception precision, TRAPA
services, and PVR list parsing/rasterization remain incomplete. The CPU probe is
not connected to normal game runtime scheduling, and no PVR frame or game audio
is produced.
`naomi_reios_boot.*` remains historical reference code and is excluded from the Naomi runtime build; `run-naomi-sh4` starts from the real BIOS reset vector. The BIOS reset AOT is generated from the locally supplied BIOS, and the SH-4 P4 reset state currently follows MAME (`EXPEVT=0`, Naomi CPU version `0x040206c8`, PRR=0). MAME remains
the primary board/map reference. The Maple/JVS HLE is in
`src/naomi/naomi_jvs_837_13551.hpp` and adapts to the Holly endpoint. Per the
user's clarification, use the applicable source license and preserve notices.
Flycast is GPL-2.0; its ported files must retain attribution and applicable
notices. MvC2's cartridge, security, SH-4 boot, and single-PVR wiring remain
game/board-specific integration work.

## Current state

`src/naomi/naomi_module.cpp` records the two MAME MvC2 sets and their ROM-board
metadata. The MAME ROM ZIPs are present in `/home/luiz/git/mame/roms`; the
BIOS-free `inspect-naomi-roms` path assembles the cartridge region from those
sets, validates ROM CRC/length, fills open space with `0xff`, handles the
Export interleave and `ROM_COPY`, and can source shared files from a parent set.
`src/naomi/naomi_pvr2.*` is connected to the probe's Naomi device bus. SH-4
stores into the E-area populate its two 32-byte store queues; `PREF` submits
the selected queue through the Naomi Area 4 split for TA polygon data, YUV, or
direct VRAM writes. The PVR register window and Area 1 VRAM aliases are also
mapped, with basic register storage and an 8 MiB backing store. TA submissions
are retained as a byte stream. This establishes the hardware-facing data path,
not a renderer: TA packet parsing, list allocation, tile binning, polygon
rasterization, texture formats, framebuffer timing, and PVR interrupts remain
unimplemented. Flycast's `core/hw/pvr/pvr_mem.cpp::TAWriteSQ` and MAME's
`src/mame/sega/naomi.cpp::naomi_map` informed the bus split and address windows.
`src/naomi/naomi_holly.*` now contains a standalone Holly interrupt
controller (normal/external/error pending and masks at SH-4 IRL 2/4/6), the
read-only `SB_SBREV=0x0b` register, and a Maple DMA descriptor engine. An environment-gated sysctrl trace is available for Holly register accesses. The interrupt controller is mapped into the probe;
PVR reset timing raises VBlank status through it. `naomi_jvs_837_13551.*` adapts its endpoint to the
315-6149/837-13551 Maple/JVS HLE. The JVS endpoint is ready to connect, but
runtime input state, Maple DMA scheduling, and cycle timing remain. The MAME ROM loader
has now assembled and validated all 15 `mvsc2u`
ROM loads. The actual Naomi BIOS reset AOT now feeds the translated SH-4 probe. It reaches `0x8c0001a2` (unresolved) after the BIOS trampoline, where the next RAM-stage source/loading is still unknown; the probe still produces no game frame or audio.
MAME's auxiliary firmware
sets are `mie.zip` (315-6146 MIE/Z80) and `jvs13551.zip` (837-13551/315-6215).
The local archives now match the MAME sets: `mie.zip` has 315-6146.bin CRC
`9b197e35`; `jvs13551.zip` has 315-6215.bin CRC `98202738` plus four TMP90PH44
variants. They are separate I/O-device references, not the Naomi BIOS, and
the current path does not execute their firmware images. The real BIOS reset AOT currently analyzes the 0xa0000000 vector plus the indirect reset helper at 0xa0000080. It reaches 0x8c0001a2 after 136 instructions; this next RAM stage is not yet explained. The REIOS helper is no longer part of the executable runtime path. The SH-4 translator uses reachable-code discovery and alias-aware dispatch.
`naomi_m2crypt.*`
contains the Flycast 315-5881 stream algorithm behind a ciphered-word callback;
`naomi_m1crypt.*` follows MAME's M1 decrypt/decompress stream and models DMA
mode selection. Neither stream is connected to a Naomi board register map or
SH-4 runtime yet. `naomi_sh4_context.*` provides the register/FPU and
memory-callback ABI, and `naomi_address_space.*` maps MAME's Naomi 1 SRAM,
sound RAM, texture/frame RAM and 32 MiB SH-4 RAM while delegating device windows
to callbacks. Compiled MvC2 functions are available, but the real BIOS boot has not reached them yet. The PVR2
register, Area 1, Area 4 and store-queue paths now reach `naomi_pvr2.*`, but its
TA stream is only captured; it does not yet render. AICA/ARM7 and Maple DMA are
not connected to the running probe. JVS firmware/device integration is deferred;
the existing JVS bridge stays provisional and outside the boot path.


## Current real-BIOS boot experiment

`tools/naomi/build_mvsc2u_aot.py` now analyzes the complete local Naomi BIOS image at reset vector `0xa0000000`, plus the computed helper at `0xa0000080`, and emits C++ for those reachable instructions. `mamerecomp_naomi.so` registers that AOT alongside the MvC2 and copied BIOS service entries. `run-naomi-sh4` initializes SH-4 state and PC at the real BIOS reset vector; it no longer calls Flycast REIOS or seeds the game's entry registers. The EXPEVT reset value is 0 per MAME `sh4_base_device::device_reset()`, avoiding the BIOS's error branch that appeared when we used the SH-4 manual's power-on-held value.

An initial validation executed 136 translated instructions and reported `0x8c0001a2`, but that result came from translating the source ROM slice as if it were the copied code. The trace shows the BIOS copies 16 halfwords from `0xa00000e0` to `0x8c0000e0` using a pre-decrementing destination, reversing their order. The AOT generator now translates the resulting RAM bytes instead. The prior hypothesis that the full BIOS is shadowed into RAM was experimental and has been removed. Re-run the real BIOS path and follow its actual PC/memory effects before making further mapping assumptions.


The `JMP @Rn` fix is validated on the BIOS reset path: before the fix, the reset helper executed `add #32,R3` in the delay slot and the generated C++ incorrectly jumped to the updated `0x8c000100`. SH-4 samples the jump target before the delay slot, so the corrected AOT reaches `0x8c0000e0`. The RAM bytes there are the source halfwords in reverse order; translate those exact bytes rather than the original BIOS slice.

### Boot progress (2026-10-30)

The reset probe zero-initialized `Sh4Context` in `mamerecomp_naomi_sh4_probe_start()`
after installing its callbacks. That silently removed both the SH-4 cycle sink
and `PREF` store-queue handler. `Sh4Probe::wireCpu()` now restores the bus,
cycle sink, and store-queue path after every context reset. The cycle sink
advances the MAME-referenced 128 Hz BSC refresh counter; with the BIOS's MCR
settings, RFCR advances and the BIOS passes its `RFCR > 16` polling loop.

The reset path then copies BIOS bytes `0x800..0x37ff` to main RAM at
`0x0c001000` and jumps through P2 alias `0xac00101a`. The AOT build extracts
those exact bytes and analyzes the copied code at its RAM alias. The SH-4
decoder/codegen now covers `LDC Rm,VBR` (`0x402e`) and `LDC Rm,GBR` (`0x403e`)
encountered in this path. Additional indirect entries are added from observed
guest targets. Flycast's Naomi `core/hw/sh4/modules/tmu.cpp` confirmed the
SH-4 TMU behavior: the BIOS sets TCOR0/TCNT0 to `0xffffffff`, TCR0 to 0, starts
channel 0, then polls TCNT0. A three-channel, cycle-driven TMU model was added
to the SH-4 map, including prescalers and sticky underflow status. The trace
then showed TCNT0 decrementing by two per poll, and BIOS execution advanced
past the former stuck state. With new observed indirect targets added, the
latest 500M probe ends at `0xac0011ac` in the same BIOS callback/event loop,
with 13.4M indirect calls and no unresolved target. The repeated callback
`0xac0023e2` reads SCIF status at `0xffe80010` and tests RDF (receive FIFO data
full); Flycast initializes SCFSR2 to `0x0060`, with RDF clear when the receive
FIFO is empty. The runtime now returns that reset value and models SCFSR2
write-one-to-clear behavior; a bounded probe confirms the callback still sees
RDF clear, so this status correction does not release the BIOS loop.
The SH-4 context-save callback reached through `0xac001afe` uses `STC.L SR/GBR/VBR/SSR/SPC,@-Rn`; the AOT decoder/codegen now implements these stores, with live T/Q/M composed when saving SR. This resolves the previous translation stop at `0xac001d96`. A fresh 120M probe enters and returns from this handler, then remains in the outer callback/event loop. A bounded callback trace shows repeated calls to `0xac0014b0` and `0xac00181c`; the latter dispatches `0xac0023e2`, which reads SCFSR2 `0x0060` (RDF clear) and returns 0. The loop leaves event words at `0xac000370/374` as `{0,1}` and accepts no SH-4 interrupt. The Naomi G1 register map now matches MAME `naomig1.cpp`: `SB_G1SYSM` and GD status reads return zero, and G1 counter writes are no-ops. Before this fix, the unmapped `SB_G1SYSM` read incorrectly returned open bus `0xffffffff`; correcting it changes the exact stop PC but does not start M2 PIO or PVR activity. The remaining unhandled `0x5f7070/74` accesses are the MAME board's default open-bus/no-op ports; `0x5f74e4` is outside Naomi G1's mapped registers. Holly
has VBlank status `0x18` while its interrupt masks remain zero. No serial board-ID access at `0x5f7078/7c` or M2 PIO read occurs before this loop. Flycast implements Naomi's X76F100 board ID in `core/hw/naomi/naomi.cpp` using `core/hw/flashrom/x76f100.cpp`; MAME's first board-ID transaction occurs from game PC `0x0c03ef76`, so this EEPROM remains a later dependency, not the cause of the current BIOS loop. Compare the event/timer path against Flycast and MAME; do not seed the event flag. The M2 ROM image is now
attached to the address space, with the `0002c840` key and PIO/crypt registers,
but the BIOS has not yet read `0x5f7008`; G1 DMA and the board serial-ID EEPROM
are still incomplete. Bounded `MAMERECOMP_NAOMI_TRACE_G1`, `_TMU`, `_BOOT_STATE`,
`_CALLBACKS`, and `_BOOT_LOOP` diagnostics capture the paths without unbounded
logs.

This is still not the complete Naomi bus topology: `NaomiAddressSpace` currently
dispatches Holly interrupt registers and PVR windows directly, and the SH-4
store-queue callback currently forwards to `NaomiPvr2`. Flycast's Naomi path
in `core/hw/sh4/storeq.cpp` also routes QACR area-4 store queues directly to
`TAWriteSQ`; that function classifies the target as TA polygon data, YUV input,
or VRAM. Its Holly path remains responsible for system-bus registers, G1
cartridge PIO/DMA, and interrupt routing. This is evidence for the functional
split we should model, rather than evidence that every PVR packet traverses a
Holly register callback. The Holly register/IRQ
controller is partial; it is not yet the bus bridge that mediates all SH-4↔PVR
transactions. `probe_start()` now attaches `image.bytes` to the M2 PIO model.
The exact cartridge register path is implemented but not yet exercised by the BIOS; DMA transfer
and board serial-ID EEPROM are pending. No framebuffer or first frame exists.
The immediate work is to release the BIOS loop through the actual event/IRQ
path, observe its ROM PIO and DMA setup, then execute MvC2 code and parse and
rasterize the PVR TA lists.
