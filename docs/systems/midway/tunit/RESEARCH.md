# Mortal Kombat arcade (Midway T-Unit): initial research

## Scope

The first target is MAME's parent set `mk`, revision 5.0 T-Unit, registered as
`midtunit_adpcm_state`, machine `tunit_adpcm`, input profile `mk`, and init
routine `init_mktunit`. `mkr4` is a T-Unit revision clone. Keep this work
independent from the frozen CPS-1 prototype and other board runtimes.

## Main finding: this is a new CPU backend

The game has a single TMS34010 as its main CPU. It is not a 68000 board and
does not use the project's existing 68000/Z80 AOT pipeline. MAME configures a
50 MHz TMS34010, 4 MHz pixel clock (`8 MHz / 2`) and two pixels per clock. The
CPU is also the programmable graphics processor: game code draws into video
memory using its graphics instructions, and MAME calls into the Midway T-Unit
video device for scanline output and shift-register transfers.

The TMS34010 is a 32-bit graphics CPU with **bit-addressed** memory. MAME's
implementation has distinct opcode fetch, word/long memory operations,
bit-field helpers, graphics operations, internal I/O registers, interrupts,
and a disassembler. Treating its addresses as ordinary byte addresses would
break instruction fetch, fields, PIXBLT and the T-Unit memory map. The right
project structure is one shared TMS34010 core/recompiler, then a T-Unit board
runtime and per-game ROM/profile adapters.

## T-Unit map and devices from MAME

The base map in `src/mame/williams/midtunit.cpp` includes:

| Address range | Device/function |
| --- | --- |
| `0x00000000–0x003fffff` | Midway video RAM read/write handlers |
| `0x01000000–0x013fffff` | Main RAM |
| `0x01400000–0x0141ffff` | CMOS/NVRAM |
| `0x01600000–0x0160003f` | Inputs, system controls and DIP switches |
| `0x01800000–0x0187ffff` | Palette RAM, xRGB555, 32K colors |
| `0x01a80000–0x01a800ff` | Video DMA/blitter register window |
| `0x01b00000–0x01b0001f` | Video control registers |
| `0x02000000–0x07ffffff` | Graphics ROM read window through the video device |
| `0x1f800000–0x1fffffff`, `0xff800000–0xffffffff` | Main program ROM and mirror |

MK's map adds sound status/data/control at `0x01d00000` and `0x01d01020`.
The T-Unit video device has local video RAM, a DMA graphics blitter, graphics
ROM bank selection, per-scanline output and TMS34010 shift-register callbacks.
This is not a CPS-style tilemap/sprite compositor: the TMS34010 draws pixels
into its framebuffer and the custom blitter expands graphics ROM data into
video RAM.

## MK ROM organization

MAME defines four regions in `ROM_START(mk)`:

- `maincpu`: 1 MiB region from two 512 KiB byte-lane ROMs using
  `ROM_LOAD16_BYTE`.
- `video`: 12 MiB region, populated from 512 KiB `ROM_LOAD32_BYTE` lanes in
  three 2 MiB groups (0, 2 and 4 MiB); the final group completes the 6 MiB
  data payload with lane interleave.
- `adpcm:cpu`: 0x50000 region; the U3 sound ROM is loaded at 0x10000 for
  0x40000 bytes.
- `adpcm:oki`: 1 MiB region; two 256 KiB sample ROMs are each mirrored into
  paired 256 KiB banks.

The ROM exporter will need lane-aware assembly and explicit reloads; treating
these files as flat concatenated blobs will scramble both program and video
regions.

## Sound and protection are not optional details

MAME's Williams ADPCM board contains:

- MC6809E at 2 MHz (`8 MHz / 4`);
- YM2151 at 3,579,545 Hz;
- AD7524 DAC;
- OKIM6295 at 1 MHz (`8 MHz / 8`), PIN7 high (clock/pin configuration marked
  unverified in MAME).

The main TMS34010 communicates through the sound status/data/control windows.
MAME's `init_mktunit` also installs MK protection handlers over
`0x01b00000–0x01b6ffff`, and provides 43 bytes of hidden RAM to the sound CPU
at `0xfb9c–0xfbc6`. That hidden RAM is part of startup protection behavior;
omit it and a correct TMS34010 core can still fail boot. Sound CPU, sound chips,
protection, and inter-CPU timing each need their own evidence and validation.

## TMS34010 implementation references

Primary implementation reference is MAME's CPU device:

- `src/devices/cpu/tms34010/tms34010.cpp` — reset, bus, execution loop,
  internal registers, interrupts and CPU timing.
- `34010ops.hxx` — instruction operations and decode table, including PIXBLT.
- `34010fld.hxx` and `34010gfx.hxx` — field and graphics instruction helpers.
- `34010dsm.cpp` — disassembler, useful for validating instruction lengths and
  operand decode independently from execution.
- Midway board: `src/mame/williams/midtunit.cpp`,
  `midtunit_m.cpp`, `midtunit_v.cpp`, `midtunit_v.h`, and `midtunit_v.ipp`.
- Sound: `src/mame/shared/williamssound.cpp` and `williamssound.h`.

TI's TMS34010 User's Guide (SPVU001A, 664 pages in the 1988 scan) is the
architecture/instruction-set reference. It has dedicated sections for register
files and status flags, internal I/O, bit-addressed memory, instruction decode,
graphics operations, interrupts, and instruction timing. The PIXBLT chapters
are especially useful: they document clipping, transfer order, per-pixel depth,
interruptibility, and the substantial timing differences between forms.

I inspected the SDK archive in `/tmp/tms34010-sdk` (downloaded from the linked
repository; not copied into this project). It contains the 1987 and 1990 tool
disks, assembler and C compiler manuals/reference cards, the TMS340 Family
Graphics Library, sample programs, debugger manuals, and TI production data.
The code-generation tools are for producing TMS34010 binaries; they are not a
TMS34010 emulator or a host-side recompilation backend. They may help assemble
small test programs and understand compiler idioms, subject to the license and
DOS-tool compatibility, but they do not remove the need for our own CPU core.

The manual describes a broad instruction set, but that does **not** mean the
first MultiArcadeRecomp version should implement every documented form. First build a
disassembler/trace path for the actual `mk` program ROM, then report both
distinct instruction forms and dynamic frequency. That gives us an evidence-
based implementation order; a short list of common opcodes can be prioritized
while less-used and graphics-heavy forms remain interpreted. The SDK includes
assemblers and development tools, not an existing game-specific opcode census.

## Suggested implementation sequence

1. Decode the `mk` ROM using the MAME disassembler and the TI instruction
   tables. Produce an opcode/form census and boot trace before estimating the
   required interpreter/AOT coverage.
2. Define CPU state and memory semantics from TI documentation plus MAME;
   validate instruction lengths and operands independently of execution.
3. Implement the frequently reached instruction forms first, with an
   interpreter fallback. Check reset, branches, flags, bit-address reads and
   writes, field operations, interrupts, and graphics operations against MAME.
4. Add execution tracing to discover MK's boot path, interrupt use, graphics
   instructions, and protection accesses before choosing AOT block boundaries.
5. Create the shared TMS34010 AOT translator and retain interpretation for
   unsupported instructions, dynamic code, and bus-visible side effects.
6. Implement T-Unit memory/video/ROM mapping, scanline framebuffer, and DMA
   blitter; validate against MAME screenshots and memory/register traces.
7. Add Williams ADPCM, its MC6809E and MK protection; compare sound command
   traffic and clocks with MAME.
8. Only then wire a Mortal Kombat `build`/`run` path through `multiarcaderecomp`.

## Sources

- [MAME T-Unit driver](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit.cpp)
- [MAME T-Unit machine/protection code](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit_m.cpp)
- [MAME T-Unit video/blitter](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit_v.cpp)
- [MAME TMS34010 core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/tms34010)
- [MAME Williams sound board](https://github.com/mamedev/mame/blob/master/src/mame/shared/williamssound.cpp)
- [Texas Instruments TMS340 Family Graphics Library User's Guide](https://www.ti.com/lit/ug/spvu027/spvu027.pdf) — references the TMS34010 User's Guide and documents the graphics programming model.
- [Texas Instruments TMS340 Family C Source Debugger User's Guide](https://www.ti.com/lit/ug/spvu021a/spvu021a.pdf) — its related-documentation section identifies SPVU001 as the architecture/instruction-set reference and SPVU020 as the assembler/compiler/linker/archiver manual.
- [TMS34010 SDK archive supplied by the user](https://github.com/tms34010guy/tms34010-sdk)
- [TMS34010 User's Guide mirror/catalog entry](https://manx-docs.org/details.php/2%2C18584) (1988, SPVU001).

MAME files are BSD-3-Clause. The SDK repository is an archival collection;
review each included document/tool's license before copying any content into
MultiArcadeRecomp. Prefer implementing behavior from the architecture documentation
and recording source attribution separately from the board's runtime code.
