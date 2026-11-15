# Mortal Kombat / Midway T-Unit

`TUnit` is the machine runtime for the MK T-Unit profile. It owns the board
map, RAM and device behavior, beam timing, scanout and framebuffer. It does
not contain a TMS34010 or MC6809E interpreter/recompiler. Those CPUs live in
the shared `core/cpus` layer and communicate with the board through buses.
The implementation is adapted from these MAME references:

- `src/mame/williams/midtunit.cpp`: memory map, clocks, input layout, ROM
  regions and video device wiring.
- `src/mame/williams/midtunit_m.cpp`: MK protection sequence and main-to-sound
  command/reset behavior.
- `src/mame/williams/midtunit_v.cpp` and `midtunit_v.ipp`: T-Unit video RAM
  banking, graphics ROM window, DMA register banking and blitter rules.
- `src/mame/shared/williamssound.cpp`: Williams ADPCM board, MC6809E map,
  sample banking, YM2151, AD7524 and OKIM6295 wiring.

The MAME implementation carries BSD-3-Clause attribution. The local runtime is
a rework of the interfaces and behavior, not a direct build of MAME's emulator
sources. Preserve the upstream source links and license notice with this board
module; the source links and license text are recorded in
[`THIRD_PARTY.md`](THIRD_PARTY.md).

See [`RUNTIME_PORT.md`](RUNTIME_PORT.md) for the MAME-to-MultiArcadeRecomp source map,
CPU/runtime ownership boundaries and explicit remaining port work.

The common launcher uses the MAME set names and Midway's T-Unit board family:

```sh
multiarcaderecomp build --machine=midway --board=tunit --game=mk
multiarcaderecomp run --machine=midway --board=tunit --game=mk --frames 600
```

Headless runs can select `--cycles` and save a frame with `--frame-out`; input
replays and isolated CMOS are available with `--input-replay` and `--nvram`.

## TMS AOT optimization hypothesis

- [ ] Investigate whether the UltraCombo ADSP translator's larger generated
  chunks and internal label dispatch can improve the TMS34010 recompilers.
  Measure dispatch overhead and preserve TMS PC/opcode validation, mutable-code
  invalidation, and interrupt/DMA boundaries. This is an unverified hypothesis;
  the ADSP result does not establish that the same change is correct for TMS.

## Runtime coverage

- TMS34010 bit-addressed maps for program mirrors, work RAM, VRAM, CMOS,
  palette RAM, input ports, DMA registers and graphics ROM.
- Internal TMS34010 I/O register storage, pixel-size state, CONVSP/CONVDP
  scaling values and window-check mode.
- T-Unit VRAM bank selection, xRGB555 palette RAM, one-shot CMOS write enable,
  graphics bank selection, MK's protection sequence and the main sound-command
  latch/status behavior.
- Separate MK1/MK2 graphics-ROM profiles. MAME's `mk2()` enables
  `set_gfx_rom_large(true)`: MK2 control bit 7 selects the 8 MiB graphics bank,
  and its DMA source addresses are decoded without the normal T-Unit
  `0x02000000` window-base subtraction. MK1 keeps the normal profile.
- Initial graphics DMA implementation for the command's 1–8 bpp data modes,
  flips, clipping, skip headers and fixed-point scaling, based on MAME's
  `dma_draw` path. DMA busy time now follows MAME's 41 ns per output pixel,
  and completion raises external interrupt 1 for the TMS34010 runtime.
- Scanline display parameters and DPYADR/VCOUNT progression, following the
  TMS34010 callbacks in MAME's CPU core and Midway scanline copy path.
- T-Unit's DPYCTL shift-register pixel mode now transfers the same 2x512-word
  VRAM window as MAME's `to_shiftreg` / `from_shiftreg` callbacks.
- Williams ADPCM sound-board memory map, program/OKI banking, command latch,
  hidden MK sound RAM, YM2151 (`ymfm`), AD7524 sample latch, and an initial
  OKIM6295-compatible four-voice ADPCM playback path.

The T-Unit machine layer is still a port in progress, not a complete or
verified MK machine. The machine beam currently uses the base MAME clock and
fixed initial screen geometry; MAME's dynamic screen reconfiguration and
cycle-exact scheduler behavior remain to be ported and compared. Shift-register
transfer behavior is implemented at the board pixel-bus boundary but has not
yet been exercised by MK. The DMA path has not yet been validated against
actual MK command traffic. Current bounded runs reach a TMS34010 idle loop
with no active DMA commands and produce a black frame.

The MK machine wires the Williams ADPCM board to T-Unit sound-command/reset
writes. The runner schedules the shared MC6809E backend, and the board models
the YM2151, OKIM6295-compatible voices and DAC, then generates stereo samples.
The SDL window now queues those samples to the default playback device. Audio
is only played with `--window`; bounded headless runs still collect no audible
output. The sound-board implementation remains separate from the CPU core.

Generate the ROM regions/AOT source, then build the module and bounded runner:

```sh
python3 tools/mk/build.py --romset /path/to/roms/mk.zip --out build/mk/work \
  --entry-list tools/mk/aot_entries_mk.txt
cmake -S . -B build/mk/native \
  -DMULTIARCADERECOMP_TMS34010_AOT_SOURCE="$PWD/build/mk/work/tms34010_aot.cpp" \
  -DMULTIARCADERECOMP_M6809_AOT_SOURCE="$PWD/build/mk/work/mc6809e_aot.cpp" \
  -DMULTIARCADERECOMP_ENABLE_WINDOW=ON
cmake --build build/mk/native --target multiarcaderecomp_mk_trace -j2
build/mk/native/multiarcaderecomp_mk_trace build/mk/work --window
```

The runner loads and saves the T-Unit's 16 KiB CMOS image at
`build/mk/work/mk.nvram` by default. Override it with `--nvram=PATH`. The
image is written when the runner exits, so close the window normally with Esc
after changing CMOS coinage to retain the setting on the next run.

During execution, the console reports each distinct PC/opcode pair sent to
the TMS34010 interpreter. The final CPU summary separates successfully
executed native/recompiled instructions from interpreted instructions; the
categorized fallback totals and bounded per-step trace remain available in
their log files.

The diagnostic runner calls the T-Unit machine's cycle-advance API, services
TMS34010 interrupt boundaries, and presents the machine-owned framebuffer and
Williams audio through SDL3. Beam timing and scanout live in `src/mk`, not in
the runner.
Close the window with Esc. On the first input mapping, P1 uses
WASD, J/K/L, U/I/O (Space also acts as P1 button 1); P2 uses arrows and the
numeric keypad. Press 5/6 for coin, 1/Enter for Start 1, 2 for Start 2, and F2
for service. Without `--window`, a bounded run still writes its most recent
scheduled framebuffer to PPM. Runtime-discovered roots are recorded in
`tools/mk/aot_entries_mk.txt`; additional one-off roots can be passed to
`tools/mk/build.py` with repeatable `--entry-bits` options.

The build step also emits `mc6809e_aot.cpp` from the sound ROM region. Its
first native pass covers a small reset/control-flow subset across all eight
MAME ROM banks and is compiled as `multiarcaderecomp_m6809_aot`; other instructions
fall back to the shared MC6809E interpreter in the runner.

CPU blocks are currently scheduled by the diagnostic runner at dispatch
boundaries, and machine IRQ lines are sampled there; this is not MAME's full
event scheduler yet. Shift-register transfers are not validated. A displayed
“ROMs are bad, press any button” prompt may be advanced with Space, J, or
Start 1; reaching it is not proof that the board has passed ROM validation.

MAME's `.cpp` files are not drop-in standalone runtime units: they depend on
MAME's device framework, address-space callbacks, scheduler and screen model.
The local files therefore port board behavior into MultiArcadeRecomp's bus/runtime
interfaces and keep exact upstream file references above; they do not compile
or link MAME's emulator framework into the generated game.
