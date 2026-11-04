# Midway V-Unit / Cruis'n USA

`mamerecomp_vunit` is the board/runtime module. It uses the shared
`core/cpus/tms320c31/` interpreter for the 50 MHz-input main DSP and the shared
`core/chips/dcs/` DCS Audio 2K runtime, which in turn uses the generic
`core/cpus/adsp2105/` interpreter. Cruis'n USA's board notes identify no
populated PIC; the PIC16C57 in the schematic is noted as absent for this game.

## Port layout

- `vunit_machine.*`: MAME-derived V-Unit address map, C31 reset copy, IRQ and
  cycle schedule, ADC, DCS host command/reset, inputs, CMOS and NVRAM.
- `core/vunit_video.*`: V-Unit video RAM, palette, texture RAM, page select,
  16-word DMA command buffer and quad raster path.
- `core/cpus/tms320c31/`: C31 CPU-only interpreter and future static translator.
- `core/chips/dcs/`: shared DCS Audio 2K board-side protocol and DSP link.
- `core/chips/adc0844/`: shared ADC0844 conversion and IRQ behavior.
- `mame_source/`: uncompiled MAME references with upstream notices.
- `games/crusnusa/`: parent set metadata and game-specific wiring belong here.

The parent MAME set is `crusnusa` v4.5. MAME's ROM declaration, map, machine
configuration, timer/IRQ behavior and DMA command decoding are the reference;
see `mame_source/README.md` and `docs/CRUISNUSA_RESEARCH.md`.

## Current implementation limits

The game reaches gameplay, but the current interpreter-backed runtime remains
slow and produces no audible output in the user's run. MAME's `poly_manager`
scan conversion is approximated by a two-triangle software rasterizer; pixel
coverage, texture interpolation, display timing and DMA behavior still need
comparison against MAME. MAME's own C31 source still marks some operations
unimplemented; runtime traces will establish whether Cruis'n uses those paths.

`mamerecomp_vunit_trace` supports `--window` on SDL3 builds and writes the most
recent image as a PPM. Pass `--audio=PATH.wav` to capture the exact mono signed
16-bit, 31.25 kHz sample stream sent by the DCS runtime to SDL. Its default run
is limited to 600 frames; pass `--frames=0 --window` to continue until the
window is closed (or the CPU stops on a fatal diagnostic). In the live runner,
arrow keys control wheel and pedals; F1, F2 or Enter activates the test/enter
switch, and `1` is Start.
Use `--interpreter-only` to disable AOT dispatch for both the C31 and the
ADSP-2105, or use `--c31-interpreter-only` / `--adsp-interpreter-only` to
isolate one CPU. The ROMs, board runtime, video backend, inputs, and NVRAM stay
the same, so these modes help attribute performance changes to a CPU path.
Per-instruction categorized profiling is disabled in normal runs; pass
`--profile` to collect and print fallback-site summaries while debugging. The
C31 prints up to 64 transition examples for `AOT_OPCODE_MISMATCH` and
`UNCOVERED_ENTRY`, plus full counts and the last PC/opcode in its final
summary. Other interpreter entries such as ordinary cycle-budget boundaries
are not logged as AOT fallbacks.

### GCC profile-guided optimization

Release already uses `-O3`; no `-march=native` is added, so the binary remains
portable to other CPUs supported by the build target. For a local PGO build,
use a separate build directory and a fresh profile directory:

```sh
cmake -S . -B build/vunit/pgo-generate \
  -DCMAKE_BUILD_TYPE=Release \
  -DMAMERECOMP_ENABLE_WINDOW=ON \
  -DMAMERECOMP_ENABLE_VUNIT_OPENGL=ON \
  -DMAMERECOMP_CRUSNUSA_C31_ROM_IMAGE="$PWD/build/vunit/work/crusnusa/main.bin" \
  -DMAMERECOMP_CRUSNUSA_C31_OVERLAY="$PWD/build/vunit/work/crusnusa/c31-hot-overlay.txt" \
  -DMAMERECOMP_VUNIT_PGO_MODE=GENERATE \
  -DMAMERECOMP_VUNIT_PGO_DIR="$PWD/build/vunit/pgo-data"
cmake --build build/vunit/pgo-generate --target mamerecomp_vunit_trace -j2
```

Run the instrumented binary through representative gameplay, including the
heavy 3D track. Keep each execution mode's profile in its own fresh profile
directory so interpreter-only and hybrid measurements remain separable. An
instrumented run's FPS is not a performance result. Then reconfigure the same
build directory with `-DMAMERECOMP_VUNIT_PGO_MODE=USE` and rebuild. Keeping the
build directory, source, compiler, and Release options the same also keeps
GCC's profile-to-object mapping stable. Regenerate the profile after code
changes. PGO builds add `-g` while retaining `-O3` and `-DNDEBUG`, so optimized
profiles can be symbolized without changing runtime assert behavior.
A live window is paced to MAME's raw 666x432 timing at 16.6666665 MHz
(approximately 57.93 Hz); the renderer has no VSync and SDL audio submission is
asynchronous, so neither one provides frame pacing.
Press F11 or Alt+Enter to toggle exclusive fullscreen; `--fullscreen` starts in
that mode. The runner selects the closest exclusive mode to the desktop's
current resolution and refresh. At startup it prints the SDL renderer and its
VSync state; the runner requests VSync off because the machine loop already
paces frames.
The default video backend is the existing CPU rasterizer. An experimental,
optional OpenGL 4.3 compute rasterizer for V-Unit DMA quads can be built with
`-DMAMERECOMP_ENABLE_VUNIT_OPENGL=ON` and selected with
`--video-backend=opengl`; `--video-backend=cpu` explicitly selects the unchanged
reference path. The OpenGL path keeps the same DMA command order and VRAM
interface, then reads the rendered pages back so the rest of the runtime stays
unchanged. Palette lookup and RGB555 expansion also run in a GLSL fragment pass;
the RGB result is read back for the current SDL presenter. It requires an
OpenGL 4.3 device and SDL3, and is a prototype rather
than a verified speedup. On NVIDIA Optimus, launch with
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` to select the
discrete GPU. DMA rasterization and palette conversion run on the GPU. SDL
window presentation still uses the existing path and reads the RGB frame back.
Pass `--scanlines=0.0..1.0` for optional alternating-row attenuation; the
default is `0` (off). The same setting works with the CPU reference renderer.
The generic RGB GLSL filter lives in `core/video/shaders/scanlines.frag` and is
not tied to V-Unit or its palette format. Other board presenters have not yet
been switched to the shared OpenGL post-process path.
For a separate prototype build, configure with
`cmake -S . -B build/vunit/opengl -DCMAKE_BUILD_TYPE=Release -DMAMERECOMP_ENABLE_VUNIT_OPENGL=ON -DMAMERECOMP_ENABLE_OPENGL_VIDEO=OFF`, then run
`cmake --build build/vunit/opengl --target mamerecomp_vunit_trace -j4` and pass
`--video-backend=opengl` when launching it.
All DMA quads are uploaded together and rasterized in one compute dispatch;
ordered tile bins preserve overlap order. Only the page requested by the CPU is
read back, while CPU VRAM reads/writes synchronize that page on demand. If no
DMA page is dirty, frame refreshes skip the OpenGL path entirely. Synthetic
tests on a GTX 1650 matched CPU output for textured/transparent overlapping
quads, both render pages, and a CPU VRAM write after GPU rendering. A 500-quad
synthetic batch took about 13.5 ms in CPU rasterization versus 0.5 ms for the
second OpenGL frame including readback; 50 full-screen quads took 1.8 ms versus
75.8 ms on CPU. The first frame includes texture upload. This is not a whole-game
benchmark, so gameplay still needs an in-game run.
Periodic FPS logging is off by default, so normal runs do not write a line to
the console every second or collect the extra per-frame timing samples. Pass
`--fps` to enable the FPS/speed line and final FPS summary. Use `--frame-stats`
to opt into per-second refresh/content/page-flip/DMA and timing statistics;
that mode also collects per-frame timing data. This helps tell
screen refreshes apart from scene updates; RGB changes are only a proxy, while
page flips are the stronger signal for a newly completed 3D frame.
A live SDL audio stream forwards DCS samples as mono signed 16-bit PCM at
31.25 kHz; headless runs drain and discard them. On window close the runner
reports submitted/nonzero samples and the remaining SDL queue, while its final
summary reports DCS sample peak/nonzero counts. The runner loads and saves the
32 KiB V-Unit NVRAM beside `main.bin` (`--nvram=PATH` overrides it). Missing
files start with all bits set, matching MAME. The C31 runs at 25 MHz effective
cycle rate (50 MHz input / 2 clocks per instruction cycle); frame scheduling
uses the 57.93 Hz rate implied by MAME's 33.333333 MHz pixel clock and 666x432
total timing. An earlier gameplay capture produced 795,120 SPORT samples, all
zero before SDL submission, locating that silence upstream of the output
device. The
shared DCS RAM write path truncated ADSP data words to eight bits; it now
preserves all sixteen bits to match MAME's `dcs_dataram_w`. Direct comparison
with MAME's ADSP register-selector tables also found that the interpreter's MAC
operations incorrectly used the ALU X/Y selectors (for example, MAC X=0 must
read MX0, not AX0; MAC Y=2 must read MF, not AF). Separate `mac_x()`/`mac_y()`
selectors now match MAME in both MAC-to-MR and MAC-to-MF operations. This is a
concrete shared CPU semantic discrepancy that could explain the zero-valued
ADSP audio calculations. A follow-up run restored nonzero audio but sounded
like noise. Its WAV capture is 30.9 seconds: silent for the first 23 seconds,
then full-range samples with repeated 16-bit saturation during gameplay. A
second MAME comparison found MAC signedness/width mismatches; `mac_product()`
now follows the MAME rounded-operation signedness and 32-bit intermediate
before 40-bit accumulation. Recheck the WAV and listening result after rebuild.
A 600-frame
headless POST run took 12.1
seconds and generated no DCS commands, so it did not exercise gameplay audio.
Set `MAMERECOMP_ADSP_TRACE_PROGRAM_WRITE=1` to log writes that patch ADSP
program word `0x014b`, including the writing PC/opcode and DAG/data registers;
this isolates the dynamic sample-writer instruction seen in silent gameplay
captures. The trace is capped at 256 records.
Set `MAMERECOMP_ADSP_TRACE_SAMPLE_WRITES=1` to log the first 128 writes into
the active SPORT sample buffer, including the sample value, source instruction,
and ADSP arithmetic/DAG state. Recent gameplay still wrote only zeros to that
buffer; no program write to `0x014b` was observed, so that earlier dynamic-code
hypothesis is disproven.
Per-frame logs now separate machine execution,
framebuffer conversion and SDL presentation time; SDL prefers an accelerated
renderer with software fallback. Fallback-site collection now avoids the
global mutex on every interpreted instruction while keeping categorized logs.

Compared `core/chips/dcs/audio_2k.*` with `~/git/mame/src/mame/shared/dcs.cpp`:
the DCS 2K memory map, 10 MHz ADSP clock, 31.25 kHz SPORT1 external clock,
half-buffer size/increment, AS_DATA reads and mono DAC transfer match the MAME
path. A scheduling mismatch was found: MameRecomp previously
waited until the whole V-Unit frame's ADSP budget had executed before draining
SPORT, while MAME's timer drains at every half-buffer boundary as the DSP runs.
The runtime now slices ADSP execution at those deadlines and drains there, so
the DSP cannot clear/reuse samples before the host reads them. Buffer diagnostics
also follow the actual AS_DATA map (shared or local RAM) and count writes in the
active range. Build succeeds for V-Unit and MK2; gameplay audio still needs a
windowed run to confirm the result. Headless POST runs with no DCS commands do
not exercise this path.

The next gameplay capture still had silence: 53 host commands, 37 overwritten
before consumption, 24 ADSP input-latch reads, 65,040 writes in the SPORT
buffer range but none nonzero, and 1,028,640 zero samples delivered to SDL.
This points upstream of SDL and the SPORT reader. The runtime catches DCS up
before each write to `0x9a0000`, but the capture exposed that the C31's public
`State::cycles` only advances at the end of a run slice. The bus callback was
therefore synchronizing with a stale timestamp. `Interpreter::current_cycles()`
now reports in-slice progress and the V-Unit callback uses it before command or
reset writes. The follow-up gameplay captures confirmed the effect: zero
overwritten commands, first 61 DSP latch reads for 53 host commands and then
71 reads for 63 commands. Samples were still silent: the latest run had 82,800
writes to the SPORT range, all zero, and SDL received 995,040 zero samples.
The Cruis'n USA driver uses the generic `midvunit` machine config and
`midvunit_base_state::control_w()`, so DCS reset is bit 1. Bit 11 belongs to
`crusnwld_state::crusnwld_control_w()` for Cruis'n World; it was a false lead
and is not used by this runtime. MAME also pulses ADSP2105 IRQ1
for its minimum quantum on SPORT1 buffer wrap. The runtime had only set the
edge latch and kept the IRQ line low. In level-triggered ICNTL mode the ADSP
therefore ignored the wrap interrupt and could fail to refill the SPORT buffer.
The runtime now asserts the line for five ADSP cycles (500 ns at 10 MHz), keeps
the edge latch, and cuts DCS execution at pulse expiry. The trace reports IRQ1
pulses, ICNTL and IMASK. Build passed; this is the next targeted gameplay
validation. The MAME DCS source under `mame_source/` is reference-only; the
active path is the adapted `core/chips/dcs/audio_2k.cpp` plus shared ADSP core.

The next audio check flagged the ADSP timer as a likely source of tempo/stutter.
Inspection found that DCS board-owned writes to `TIMER_SCALE`, `TIMER_COUNT`
and `TIMER_PERIOD` were not mirrored into the ADSP state read by its cycle
timer; the board also returned a stale timer count. The DCS adapter now mirrors
those registers, keeps the MAME-visible scale (`raw + 1`) separate from the
CPU's raw scale value, returns the live counter, and resets the timer phase on
enable/reprogramming. This changes timer scheduling only; it does not change
the 24-bit ADSP instruction/boot-word decoding, which remains three big-endian
bytes per program word. Rollback marker: timer-state synchronization in
`core/cpus/adsp2105/interpreter.cpp` and `core/chips/dcs/audio_2k.cpp`.
The V-Unit trace now prints the timer enable, scale/count/period, current phase
and pending timer IRQ for the next audio comparison.
The live SDL runner also reports negotiated source/device rates and the minimum
input-audio queue depth observed after each frame, in milliseconds. This will
distinguish DSP sample generation from underruns caused by the main loop missing
the ~17.26 ms frame deadline.

The C31 translator is planned as a new implementation, separate from the
TMS34010. First identify the gameplay runtime/audio bottleneck and compare the
V-Unit video/audio paths against MAME.
