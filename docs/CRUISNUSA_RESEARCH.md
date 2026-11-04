# Cruis'n USA — V-Unit research and port status

Cruis'n USA is a separate target from Mortal Kombat/T-Unit. Its main device is
the **TMS320C31**, a 32-bit floating-point DSP running at a 50 MHz input clock;
it is not the TMS34010 used by the MK1/MK2 T-Unit prototypes. The board's
AD1851 is the audio PCM DAC, while the ADSP-2105 at 10 MHz runs the DCS audio
system. Do not confuse those devices or reuse the TMS34010 CPU backend for the
main game CPU.

MAME's V-Unit driver is `~/git/mame/src/mame/williams/midvunit.cpp`, with video
in `midvunit_v.cpp` and board declarations in `midvunit.h`. The board notes
identify the custom LSI LIA7968 and a Midway 5410-1346500 ASIC. MAME represents
the graphics path through TMS320C31-written video/DMA registers, a queued DMA
command list and host-side polygon rasterization (`poly_manager<float>`). The
precise division of work between those physical ASICs and the software model
still needs to be mapped before implementation.

The TMS320C31 port should start as a new CPU backend, using the MAME
`src/devices/cpu/tms320c3x/` implementation as the opcode/architectural
reference. Start with an interpreter and categorized instruction logging, then
build the static translator against observed Cruis'n USA code. The TMS34010
backend under `core/cpus/tms34010/` is a different ISA and is not a starting
point for instruction semantics.

The MAME driver supports parent set `crusnusa` and revisions `crusnusa44`,
`crusnusa41`, `crusnusa40`, `crusnusa21`, `crusnusa20`, and `crusnusa11`.
Select and audit the installed parent ROM set before designing the ROM profile.

## Source map

- MAME board/runtime: `src/mame/williams/midvunit.cpp`
- MAME polygon/DMA video model: `src/mame/williams/midvunit_v.cpp`
- MAME TMS320C3x core: `src/devices/cpu/tms320c3x/`
- Board notes, clocks, and ROM declarations: comments and machine config in
  `midvunit.cpp`

# Current MameRecomp port

The new `mamerecomp_vunit` shared library is wired as a board module. Its
interpreter path consists of the MAME-derived C31 opcode handlers in
`core/cpus/tms320c31/`, the V-Unit map/control runtime in
`src/midway/vunit/vunit_machine.*`, and the DMA/video path in
`src/midway/vunit/core/vunit_video.*`. C31 is configured for 25 million
instruction cycles per second from its 50 MHz input clock, consistent with
MAME's two input clocks per execution cycle.

The ADSP-2105 implementation from KI is reused through the generic
`core/cpus/adsp2105/` backend. The DCS Audio 2K board runtime was moved from the
MK2-specific directory to `core/chips/dcs/`; MK2 now links that shared library,
and V-Unit uses it with its own command/reset wiring. This DCS change was
compiled for both the shared library and MK2 module. KI1 and KI2 remain
explicitly tracked DCS consumers on their working UltraCombo-specific runtime;
see `docs/DCS_MODULARIZATION.md` before changing the shared interface.

The ADC0844 conversion/40 microsecond completion and IRQ behavior is now a
shared chip component in `core/chips/adc0844/`. V-Unit owns input values and
connects its completion IRQ to C31 IRQ3. The board runtime also has the MAME
low-RAM ROM copy, C31 memory map, page/scanline/DMA registers, palette/texture
RAM, CMOS protection, and DCS host writes.

The MAME DMA command decoding is implemented, with flat, opaque texture,
transparent texture, transparent-mask and dither paths. Polygon fill currently
uses a two-triangle software rasterizer, so exact `poly_manager` edge and
interpolation parity remains open. The V-Unit module builds, but the interpreter
runtime has not yet been validated against a Cruis'n USA frame. Keep this game
in prototype status; implement and test the interpreter-backed game pipeline
before starting C31 AOT translation.

## Interpreter-only startup checkpoint (2026-11-02)

At this startup checkpoint the game did not require a static recompilation to
execute: the V-Unit runtime dispatched main-program instructions through the
shared MAME-derived TMS320C31 interpreter, and ran DCS audio through the shared
ADSP-2105 interpreter/runtime. The board module supplies memory mapping,
interrupts, I/O and video/DMA behavior. A small C31 AOT translator was added
later; its current frozen state is recorded below.

While comparing the first C31 startup instructions against MAME, a missing
mask in `sext(value, bits)` was found. Seven-bit shift amounts retained upper
bits, so the startup `LSH $0010,AR0` at `PC=0x4afd` generated an incorrect
address. The helper now masks the operand before sign extension; the first
instructions and the following `RPTS` memory clear match MAME. This removes
the previously documented `PC=0x9fef` loop.

The POST initially appeared stuck at “Motion setup testing”. Two runtime gaps
caused this: the `0x995000` wheel/GALIL port was unmapped, and the runner set
the active-high motion status bits in IN1 to one. The wheel/GALIL protocol is
now ported from MAME's `wheel_board_r/w`, and the motion bits default low as in
MAME's input declarations. The 1200-frame capture now reports `MOTION SYSTEM
OK`, completes the local link setup and reaches `GET USA`; it still reports no
DMA commands or DCS host commands and is not yet a verified gameplay frame.
The live runner maps arrows to wheel/pedals and F1/F2/Enter to the test switch.
Next advance through configuration, then validate DMA, audio and the first
rendered frame against MAME.

## C31 direct-translator checkpoint (frozen 2026-11-02)

The C31 translator now has an experimental direct native path for scalar
integer and floating-point forms. It uses a TI-derived mask/value table for
those direct forms, validates the live PC/opcode, and leaves unsupported code
on the existing guarded fallback. The source and opcode scope are documented
in `core/cpus/tms320c31/MAME_PORT.md`; the TI manuals are archived in
`docs/references/tms320c31/`.

Latest generator figures for the 131,072-word program image are 61,422 guarded
candidates, 14,639 directly translated words, and 14,041 generated blocks.
These counts include data that decodes as an opcode and cannot be read as
unique instruction coverage or runtime execution. Parallel instruction words
are deliberately excluded from scalar lowering and still use fallback.
Fallback classification is not fully TI-derived yet.

The current binary compiled successfully, but no instruction-by-instruction
equivalence or gameplay performance improvement has been established for the
new native operations. The user reports the game works and that the remaining
work is optimization; interpreter-only performance fell to about 88% at some
points, without a critical slowdown. The C31 translator is frozen at this
prototype until explicitly resumed. FPS console logging and extra timing
collection are now opt-in (`--fps` / `--frame-stats`) so ordinary runs do not
collect those per-frame metrics.
