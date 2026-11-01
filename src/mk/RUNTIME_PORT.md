# MK T-Unit runtime port

This document tracks the **machine runtime** port from MAME to MameRecomp.
The runtime models board devices and their interactions. CPU execution and
translation stay in shared CPU code under `core/cpus`; they are connected to
this runtime through `mamerecomp::tms34010::Bus` or the MC6809 bus interface.
The diagnostic runner in `tools/mk/trace.cpp` is not the machine runtime.

## Source correspondence

| MAME source | MameRecomp location | Ported behavior / current status |
| --- | --- | --- |
| `midtunit_base_state` / `midtunit_adpcm_state` machine composition | `src/mk/mk_machine.*` | Owns the T-Unit and Williams ADPCM devices and wires T-Unit sound-command/reset writes to the sound board. It does not instantiate or implement either CPU. |
| `src/mame/midway/midtunit.cpp` | `src/mk/tunit.cpp` | T-Unit address map, RAM regions, ROM mirrors, input reads, palette, graphics ROM window, DMA/control windows and watchdog write. Base clocks and initial display geometry are represented; dynamic screen reconfiguration is outstanding. |
| `src/mame/midway/midtunit_m.cpp` | `src/mk/tunit.cpp` | CMOS, MK protection sequence, sound command/status latch and reset line. The MK protection table and write-to-select behavior are ported. |
| `src/mame/midway/midtunit_v.cpp` | `src/mk/tunit.cpp` | Video RAM banking, T-Unit graphics ROM banking, DMA register banks, pixel operations, clipping, flips, skip headers, scaling, DMA busy time, scanout, palette conversion and shift-register callbacks. DMA output still needs comparison against real MK traffic. |
| `src/mame/midway/midtunit_v.ipp` | `src/mk/tunit.cpp` | Command-to-pixel-operation selection and 1–8 bpp variants are implemented in the runtime blitter. |
| TMS34010 scanline/I/O behavior in `src/devices/cpu/tms34010/tms34010.cpp` | shared `core/cpus/tms34010` plus machine cycle API in `src/mk/tunit.cpp` | The TMS core owns CPU registers/instructions/interrupt entry. The board owns beam progression, display interrupt latching, DPYADR progression, scanout and framebuffer. The current scheduler is approximate and lacks MAME's dynamic screen timing/event scheduler. |
| `src/mame/shared/williamssound.cpp` | `src/mk/williams_adpcm.cpp` | Williams ADPCM sound-board map, bank selection, YM2151/OKIM6295/DAC behavior and stereo sample generation are represented. The shared MC6809E backend is scheduled by the diagnostic runner, which feeds the generated samples to SDL playback in window mode. |
| ROM declarations in `src/mame/midway/midtunit.cpp` | `tools/mk/build.py` | MK rev. 5 ROM verification, lane interleave and region assembly. This is build input preparation, not runtime hardware emulation. |

## Ownership boundaries

- `core/cpus/tms34010`: shared TMS34010 state, interpreter/recompiler and CPU
  exception semantics. It has no Mortal Kombat memory map or video device.
- `core/cpus/m6809`: shared MC6809E CPU implementation. The current tool-side
  AOT translator is experimental; unsupported entries use the interpreter.
- `src/mk/tunit.*`: MK T-Unit machine bus and board-side devices. It owns the
  video RAM, palette, DMA registers, beam and scanout state, inputs, CMOS and
  MK protection behavior.
- `src/mk/williams_adpcm.*`: Williams sound-board device behavior and chip
  latches/synthesis. `MKMachine` wires the device and exposes its sound CPU bus
  and generated audio without containing MC6809E CPU implementation.
- `tools/mk/trace.cpp`: diagnostic frontend/runner. It invokes a selected CPU
  backend, passes elapsed cycles to the machine, captures fallback traces and
  presents machine output. It must not own hardware state or video emulation.

Runtime routines may remain interpreted where required. Static recompilation
of deterministic board routines can be added later as a separate layer; it
does not move board semantics into CPU code or make the machine runtime itself
an interpreter.

## Remaining machine-port work

1. Replace the approximate fixed beam clock with MAME-compatible dynamic
   horizontal/vertical timing updates and event ordering.
2. Verify the 2x512-word shift-register transfer path against the shared TMS
   pixel access semantics and MK instruction trace.
3. Compare DMA register reads/writes, command timing, source addressing,
   blitter output and interrupt assertion against MAME for captured MK
   commands. Current traces have not yet issued a command with bit 15 set.
4. Finish machine-side interrupt-line scheduling so DMA/display events are
   delivered at the correct CPU boundary without implementing CPU behavior in
   the machine module.
5. Replace the approximate runner scheduling of the MC6809E with machine event
   scheduling and verify YM2151, OKI and DAC output against MAME.
6. Compare inputs, CMOS persistence, palette/scanout and sound behavior with
   MAME after the machine loop advances beyond the current idle loop.

## Current diagnostic state

The bounded MK runner reaches PC `0xffdd19c0` and spends the remaining budget
in a self-loop. In an 8,000,000-cycle run it reported zero DMA commands and a
black framebuffer. This confirms the runner reaches the video reporting path;
it does not establish that the board runtime is complete or that DMA is the
root cause. Interpreter reason logging remains enabled for prototype runs.
