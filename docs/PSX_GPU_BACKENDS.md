# PSX GPU rasterization backends

## Goal

Keep the MAME-derived software rasterizer as the correctness/reference path and
add an optional OpenGL backend that executes PSX GPU drawing work on the host
GPU. This is distinct from presenting an already-rendered RGB framebuffer: the
current SDL `opengl` renderer only scales and presents pixels produced by the
CPU rasterizer.

The GPU device belongs to the shared PSX-derived SoC. ZN-1, ZN-2, System 12,
and future Sony-based boards should use the same GPU implementation; board code
continues to provide its bus mapping, clock, IRQ wiring, and display connection.
Both Sony ZN and Namco System 12 accept the same `--gpu-hw` backend selection;
the renderer itself has no ZN-specific types or board callbacks.

## Current implementation status

- `mamerecomp_psx_gpu` is a reusable PSX GPU target composed by the common PSX
  machine layer. GP0/GP1 decoding and device-visible state stay in `gpu.cpp`;
  raster work is routed through the `GpuHardwareRenderer` backend interface.
- Software rendering remains the default. `--gpu-hw` explicitly selects the
  OpenGL 4.3 backend and fails clearly when the SDL3/OpenGL build support is
  unavailable. ZN-1, ZN-2, and Namco System 12 use this same PSX implementation.
- OpenGL keeps two integer `R16UI` VRAM textures plus the CPU-visible mirror.
  Polygon and rectangle commands are batched in GP0 order through vertex and
  command/scanline SSBOs; fills, lines, texture-cache generation, VRAM copies,
  and scanout use compute shaders. The 64-layer cache stores indexed texture
  pages while CLUT colors are sampled from current VRAM. Fixed-point scanline
  interpolation is used for polygons to match the software reference.
- Batches flush at ordering boundaries, including destination read/write
  overlap, texture/CLUT reads that overlap pending writes, CPU-visible VRAM
  operations, and scanout. GPU-to-GPU GP0 `0x80` copies use a source snapshot.
  The copy stays in the same OpenGL command stream and uses explicit image/
  texture barriers; it does not force a separate `glFlush` submission.
  GP0 A0 uploads update only their rectangle; C0 reads back only the requested
  rectangle. DMA2 linked-list upload/readback and odd-width wrapped transfers
  have automated coverage.
- With `--gpu-hw --window`, the scanout compute shader writes the display
  texture and the GPU presents it directly to the native SDL OpenGL window.
  Headless image captures use synchronous readback for deterministic output.
  The three-slot PBO readback ring remains experimental and is not used by the
  direct-present path.
- Synthetic parity tests cover GPU types 0/1/2, representative flat/Gouraud and
  textured primitives, dithering, clipping, mask and transparency behavior,
  GP1 scanout modes, texture/CLUT coherence, and DMA/GP0 transfers. They pass on
  Mesa Intel UHD 630 and the NVIDIA GTX 1650 with GL error checks enabled.
  llvmpipe and Mesa software rendering have also been used for runtime checks.
- GP0 drawing now follows DuckStation's interlaced 480i field rule: draws skip
  rows belonging to the active displayed field unless GP0(E1) enables drawing
  to that field. The active row parity includes GP1 display-area Y and the
  current field bit. The rule is applied by the software path, OpenGL compute
  rasterizer, immediate draw shader, and batched fragment shader; the synthetic
  parity test covers fills, rectangles, lines, polygons, a textured sprite,
  the E1 override, an odd GP1 display-area Y origin, and an emulated field
  transition at vblank.
- Full-game checkpoints include byte-identical software/OpenGL SFEX2PA captures
  at frames 1074 and 1560 on NVIDIA; ZN-1 Bloody Roar 2 and System 12 Tekken 3
  also have matched checkpoints. These do not prove continuous parity for
  every command stream or every title.
- Two paired 600-frame headless profiles cut CPU time attributed to GP0 by
  about 41%, but total `run_until` time changed by less than 1%; DSP16A is the
  dominant cost in that SFEX2PA profile. Two paired 300-frame windowed runs
  showed near-equal total time for direct-present GPU_HW and the software/SDL
  presenter. These measurements show no material whole-machine speedup yet.

### Remaining work

Broaden parity coverage across command combinations and long-running gameplay,
repeat real-game comparisons on more Sony-derived boards and GPU vendors, and
measure direct-present throughput with controlled repeated runs. The current
hardware path requires OpenGL 4.3 and has been validated on a limited set of
drivers; it is not yet a proof of full GPU_HW feature parity with DuckStation.

## Reference and implementation boundary

DuckStation's `GPU_HW` and shader generator are architectural references for
batched drawing, VRAM texture caching, and synchronization at CPU-visible VRAM
accesses. This project will implement its backend independently and will not
copy DuckStation source files. The MAME software renderer remains the behavioral
reference for the current baseline and for differences in PSX GPU semantics.

Cruis'n USA's V-Unit module already has an in-project GLSL 4.30 compute
rasterizer in `src/midway/vunit/core/vunit_video_gl.cpp`. The PSX backend uses
the same general approach—compute dispatch, resident device memory, and shader
compilation—but V-Unit's shader cannot be reused directly: its command format,
texture RAM, palette rules, and 512x400 targets differ from PSX GP0, CLUT,
texture pages, 1024x512 16-bit VRAM, and blending. The PSX-specific GLSL shader
is `core/cpus/psx/gpu_rasterize.comp`.

## Historical implementation notes

The dated entries below record intermediate states of the renderer. Some
describe bugs and gaps that later entries in this file resolve; use the current
implementation summary above as the authoritative status.

The path builds and has been exercised on the host's Mesa Intel UHD 630
hardware renderer (not llvmpipe). A matched 1,800-frame SFEX2PA sequence was
compared against the default software path with fresh temporary NVRAM. Frames
300, 600, 1200, and 1800 were pixel-identical. The QSound logo at frame 900
still differs in 7,805 pixels, and the ARIKA intro at frame 1500 differs in
3,067 pixels; the large triangle artifacts are gone, but these texture/edge
differences remain under investigation. A single rectangle probe verified
FBO coordinates after fixing the Y orientation. This is not yet parity across
all GP0 families: final 16-bit VRAM comparisons, Gouraud cases, semi-
transparency, wrapped fills, line edge rules, and GP0 transfer interactions
still need coverage. Hardware remains selected explicitly with `--gpu-hw`;
the no-flag software path remains the comparison baseline.

An in-game SFEX2PA capture on the Intel UHD 630 exposed a Gouraud-texture
modulation bug: the shader packed interpolated vertex color to RGB555, then
incorrectly treated that packed value as RGB888 for texture modulation. The
shader now modulates from the interpolated 8-bit vertex color. At frame 1560,
the fighter models render with the expected white gi and natural skin tones;
the red/green color corruption is gone. Pixel-level parity with the software
path is still incomplete (the matched frame differs in 37,590 pixels, mostly
small rasterization/color-edge differences), so broader GPU validation remains
open.

GP0 `0x80` was verified on the Intel UHD 630 with wrapped, overlapping, and
mask-tested copy probes against a CPU snapshot reference. An end-to-end GP0
`0x80` followed by GP0 `0xc0` readback also matched all copied words.

The OpenGL line compute shader now follows the fixed-point DDA step and
endpoint rule used by the software renderer. A sequence containing clipped,
horizontal, vertical, steep, reversed, zero-length, flat, Gouraud, and
semi-transparent lines matched the software VRAM pixels exactly on the Intel
UHD 630. Fills and lines update the current integer VRAM image in place; their
compute operations only read or write the pixel owned by each invocation, so
they do not need a ping-pong copy. Polygon sequences still use ping-pong and
matched the software VRAM image in an overlapping-triangle probe without a
per-primitive `glFlush`. The command SSBO uses a fenced ring with one fence per
256 commands, and per-command GL error polling is opt-in via
`MAMERECOMP_PSX_GPU_GL_CHECK`.

Hardware-mode display scanout converts the selected PSX VRAM window to RGB in
an OpenGL compute shader. A windowed run blits that output texture directly to
the SDL OpenGL window; headless capture reads back only the final RGB frame.
Ordinary vblank no longer copies the complete emulated VRAM into the CPU
shadow. CPU-visible GPUREAD and other operations that require VRAM synchronize
the needed data on demand.
The scanout probe matched the software reference across tested horizontal
modes, RGB15/RGB24, display-start wrapping, and interlaced-height configurations.
An SFEX2PA capture at frame 1560 matched the prior hardware scanout byte for
byte. With `--gpu-hw`, the 1560-frame headless run took 43.2 seconds on the
UHD 630 after removing per-primitive flushes; the GP0 profiling total fell from
24.7 seconds to 3.1 seconds. The same run through the software rasterizer took
45.6 seconds. The final 60-frame gameplay window measured about 45.9 ms per
frame before the direct-present path was added. A comparable throughput
measurement after that change remains outstanding; GPU command submission and
CPU/GPU synchronization are still performance work.

Interactive `--gpu-hw --window` now creates one native SDL OpenGL window and
shares its context with the PSX GPU module. The scanout compute shader writes
the output texture, then the GPU blits that texture directly to the window's
backbuffer with aspect-preserving scaling; this avoids mapping the frame to
CPU memory and uploading it through `SDL_UpdateTexture`. GP0 scissor state is
reset before the blit so the last primitive's clip rectangle cannot crop
presentation. Headless `--frame-out` and frame-sequence captures retain
synchronous readback for deterministic output. The three-slot PBO ring remains
available for asynchronous CPU readback experiments, but the interactive
presenter no longer uses it.

The scanout shader's uniform locations are resolved once when its program is
created and reused for each vblank; the frame loop no longer asks the GL driver
to look them up repeatedly. The existing PSX GPU parity, multi-mode scanout,
and transfer tests pass on the NVIDIA GTX 1650 after this change. This removes
per-frame driver queries, but a separate windowed benchmark is still needed to
measure its total-runtime effect.

The direct path completed a 457-frame SFEX2PA run on Wayland with the GTX 1650
when `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` was set for diagnosis, and a
900-frame run on X11. After making the PSX native-OpenGL window borderless for
debugging, a 600-frame X11 run completed all 600 scanout updates using the
direct blit path. Wayland was not revalidated in this run. Restoring normal
decoration is deferred until the renderer is stable; other SDL windows keep
their normal decoration/backend choice. Runtime completion is smoke-tested,
and pixel output is compared against the software path below.

The GP0 batching path submits opaque flat/Gouraud triangles and quads, textured
and untextured, plus rectangles, through a streaming vertex buffer. Per-vertex
attributes carry UVs and the primitive's texture page, CLUT, texture depth,
window, color, and mask state. A 64-layer R16UI texture array caches 256x256
texture-index pages; a compute shader fills cache layers while indexed texels
read their CLUT from the current VRAM texture in the draw shader. This avoids
duplicating identical index pages for different CLUTs. VRAM writes invalidate
entries whose source texture intersects the write. Wrapped/interleaved layouts
conservatively invalidate globally. A temporary depth attachment assigns ordered
depths to the two triangles of a quad and to each successive primitive, so
overlap preserves PSX last-draw-wins behavior within a GL submission. Flat
colors remain in their native packed format; Gouraud and texture modulation
colors are converted from RGB888. Semitransparent primitives apply all four
PSX ABR equations in the fragment shader, and mask-test primitives read the
old STP bit from source VRAM. A destination-reading primitive flushes if its
draw bounds overlap pending writes; disjoint blend/mask commands remain in the
same batch. Draw-area changes, CPU-visible VRAM operations, and scanout also
flush the queue. Before queuing a textured primitive, the renderer checks
whether its source page or CLUT intersects pending writes; if so it flushes
first so the texture fetch sees the updated VRAM snapshot. Interleaved and
wrapped source regions conservatively force that flush.
`MAMERECOMP_PSX_GPU_BATCHING=0` disables the path;
`MAMERECOMP_PSX_GPU_BATCH_LIMIT` caps primitives per submission, and
`MAMERECOMP_PSX_GPU_BATCH_STATS=1` prints aggregate counts.

Validation on an NVIDIA GTX 1650 ran SFEX2PA for 1,800 frames through gameplay
with fresh NVRAM. The batch and non-batch frame captures were byte-identical
(0 differing pixels), including with OpenGL error checks enabled. The scene
submitted 501,234 opaque primitives in 8,846 draws (56.7 primitives per draw)
with VRAM read-after-write barriers enabled. An earlier opaque-only batch run
submitted 501,234 primitives in 8,846 draws. After adding safe batching for
semitransparent and mask-test commands, the same script submitted 512,479
primitives in 12,950 draws (39.6 primitives per draw); the extra 11,245
primitives were commands that previously used individual draws. The capture
remained byte-identical to the opaque-only result. A repeated run with OpenGL
error checks enabled took 44.3 seconds and produced the same SHA-256 as both
the earlier batch capture and the cached direct-VRAM capture. These timing
samples are too few to establish a stable speedup.
The CLUT-independent cache produced the same 1,800-frame capture byte-for-byte
as the previous direct-VRAM sampling path (zero differing pixels). It generated
7,477 layers, recorded 466,666 hits, 7,468 invalidations, and no evictions;
compared with the earlier CLUT-inclusive cache, generations fell from 31,822
and evictions from 24,563. The single matched 1,800-frame software run took
47.6 seconds; GPU timings are listed above and remain initial measurements,
not a stable benchmark. After changing GP0 `A0` to upload only its destination
rectangle, a repeat GPU run remained byte-identical to the prior GPU capture.
A separate 600-frame run
with `MAMERECOMP_PSX_GPU_GL_CHECK=1` completed without OpenGL errors. Remaining
work includes reducing cache invalidations and broadening GP0/VRAM parity
coverage. CPU-visible VRAM operations and scanout remain explicit
synchronization points. GP0 `A0` reads back the full
VRAM first only when stale host data must be consulted for check-mask behavior;
the 1,800-frame capture remained unchanged after this transfer-path change.
GP0 `C0` now reads only its requested rectangle into the host mirror. A VRAM
write generation forces that rectangle to refresh if GPU work occurs between
successive GPUREADs; querying the generation flushes pending raster batches
first. An end-to-end GP0 `C0` probe read pixels across both VRAM edges and
observed a queued GPU draw inserted between consecutive GPUREAD words.

Cross-board validation on ZN-1 ran Bloody Roar 2 for 900 frames: the
OpenGL/rasterizer and software frame captures were byte-identical, including
after semitransparency/mask batching. The GPU path submitted 134,046
primitives in 717 batches, with 13 texture generations and no cache evictions.
A separate 900-frame native-window run completed through
the direct-present path; its 15.7-second wall time includes presentation
pacing and is not a throughput benchmark. The headless capture run was slower
than software in this single sample (5.72 s versus 4.71 s), which is a reminder
that readback-heavy validation does not measure the intended direct-present
interactive path.

For the SFEX2PA character-select frame (frame 1100, coin at 300 and start at
950), the NVIDIA GTX 1650 `--gpu-hw` capture still differs from software in the
top 136 rows; the Ryu portrait is missing/deformed. Batch-on and batch-off GPU
captures have the same visible defect (their remaining difference is 244
pixels near rows 134–135), so the fault is in the shared raster path, not the
opaque batching or texture-page cache. A DuckStation-style native-resolution
pixel-center convention and round-to-even UV conversion were tested in both
triangle paths. They changed the capture but did not restore the portrait, so
those unverified sampling changes were reverted. A same-frame
software-textured-polygon fallback probe also left the portrait missing, so
the defect is not isolated to that path. The GP0 packet trace for this scene
is available with `MAMERECOMP_PSX_GPU_COMPARE_TRACE_FRAME`; the next diagnosis
needs to follow the relevant draw and its VRAM source data against the software
reference. That trace showed the flipped `GP0 0x64` rectangle (`tpage=0x18b4`)
was sampling its first texel one position too early. The rectangle path now
uses DuckStation's `tex_start = tex_left + 1` convention when flipped, with the
extent subtracted from that start. This removed that command's new pixel
mismatches and reduced the frame's software/GPU difference from about 35,800
to 29,300 pixels, but did not visibly restore the Ryu portrait. After that
correction, the first new mismatches in the traced frame come from textured
polygon commands (`GP0 0x2e`), which remain to be compared against MAME's
scanline interpolation.

On 2026-11-08, the missing/deformed character portrait was traced to the VRAM
upload path rather than polygon sampling. `upload_rect()` passed packed 16-bit
VRAM rows to `glTexSubImage2D()` while OpenGL's default `GL_UNPACK_ALIGNMENT`
was 4 bytes. Odd-width transfer rows are only 2-byte aligned in the packed
buffer, so each subsequent row could start two bytes off. Setting unpack
alignment to 2 for these uploads fixes the character-select portrait. A
matched SFEX2PA capture at frame 1074 on the NVIDIA GTX 1650 now shows the
portrait intact; compared with the software frame, 1,842 of 122,880 RGB pixels
differ, concentrated above the character grid. This visual capture validates
the portrait fix but does not establish full VRAM parity; the remaining
pixel-level difference needs separate analysis.

Also on 2026-11-08, the selected frame's first remaining polygon differences
were traced to using barycentric interpolation in the OpenGL compute shader,
while MAME and the software reference step polygon edges and UV/color values
in signed 16.16 fixed point once per scanline and pixel. The compute shader now
uses that fixed-point scanline walk for GP0 triangles and quads, including
MAME's degenerate-quad culling/order. Textured polygons bypass the native
varying-interpolation batch shader and use this compute path; untextured
polygons remain batched. With the default batching setting, frame 1074's
selected `0x2e` and `0x2c` polygon commands had zero VRAM differences against
the software reference, and the complete 512×240 capture was pixel-identical.
This is a targeted SFEX2PA frame check, not full polygon-family parity.

One unprofiled 1,074-frame headless capture took 36.05 s on the GTX 1650 with
`--gpu-hw`, compared with 33.92 s through the software rasterizer using the
same inputs (about 6% slower for this run). These single samples are not a
stable benchmark. Textured-polygon batching is currently bypassed to preserve
fixed-point semantics; reproducing the scanline interpolation in the batch
shader, then measuring interactive direct presentation and validating ZN-1
and System 12, remain open work.

The follow-up batch implementation stores each original GP0 polygon in an
SSBO and draws its bounded screen rectangle. The fragment shader applies the
same 16.16 edge and attribute stepping used by the compute rasterizer, then
does texture-cache lookup, CLUT, mask, and blending in the normal ordered
batch. This restores batching for textured triangles and quads without relying
on OpenGL's varying interpolation. On 2026-11-08, SFEX2PA frame 1074 rendered
pixel-identically to the software capture; traced textured commands `0x2e` and
`0x2c` each had zero differing VRAM pixels. The portrait remains intact. A
matched 600-frame headless run took 19.56 s with `--gpu-hw` and 19.64 s with
software rendering, with identical guest instruction/cycle totals. This
sample shows parity and removes the earlier measured GPU penalty, but does
not demonstrate a meaningful speedup; CPU-side guest work still dominates
this headless measurement. The earlier statements that textured polygons
bypass batching and that the 1,074-frame result was slower describe the
previous implementation and are superseded by this checkpoint.

The same OpenGL path was checked on 2026-11-08 with
`MAMERECOMP_PSX_GPU_GL_CHECK=1` and per-frame software comparison. ZN-1
`bldyror2` completed 600 vblanks with 552 visible captures at 512×480, and
Namco System 12 `tekken3` completed 600 vblanks with 600 visible captures at
640×240. Both used the NVIDIA GTX 1650 renderer; neither reported a GL error
or a differing visible pixel. The ZN-1 run included coin/start inputs. These
checks establish scanout parity for these captured runs, not full game
correctness or 3D gameplay coverage.

A paired 600-vblank headless capture of System 12 `tekken3` was then run on
the NVIDIA GTX 1650 with and without `--gpu-hw`. Both 640×240 PPMs were
byte-identical (SHA-256
`de1cae4b28f306cf50ef96d72a79959d9fa58e93c42176737c16a17cdb28e690`), and
both runs reported 250,726,699 R3000A instructions/cycles and 62,681,653 H8
cycles. This confirms shared-backend image parity at that boot checkpoint on
System 12; it is not a gameplay parity or performance result.

The direct-present window path completed 300 SFEX2PA frames in 10.91 s; the
software rasterizer completed the same input sequence in 10.70 s after
disabling SDL's Wayland libdecor plugin. Both runs had identical guest cycle
and instruction totals. This does not show a performance gain from GPU
rasterization in the measured attract/early-game workload. Before disabling
the plugin, the software window reproducibly crashed in
`libdecor-gtk.so`/`libglycin-2.so` while creating the Wayland frame; setting
`SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` made it complete without changing the
video driver. The presenter now sets that SDL hint before initialization on
Linux unless the user overrides it, allowing SDL/compositor decorations where
xdg-decoration is available.

On 2026-11-08, profiling showed that the batch polygon fragment shader was
repeating polygon edge selection, fixed-point slope calculation, and divisions
for every covered pixel. The CPU now computes each polygon's scanline bounds
and starting UV/RGB values and horizontal steps once, uploads those records in
a second SSBO, and leaves per-pixel work to integer interpolation and shading
on the GPU. The SSBO is flushed before its scanline data exceeds one million
rows. This keeps the original GP0 order, fixed-point interpolation, texture
cache, and VRAM barriers while removing repeated edge setup from the fragment
shader.

The optimized shader was checked against the software backend on SFEX2PA frame
1074: both textured polygon commands traced (`0x2e` and `0x2c`) had zero
differing VRAM pixels, and the complete 512×240 RGB capture had zero differing
pixels. With the NVIDIA GTX 1650, one clean-host 600-frame headless run took
14.53 s in `--gpu-hw` versus 15.31 s in software. A 300-frame direct-present
window run took 8.51 s versus 9.27 s in software; guest cycle and instruction
totals matched. These are single-run samples, not stable benchmark results,
but they show the precomputed path avoiding the prior GPU batch slowdown on
these workloads. A short Valgrind Callgrind run of the software configuration
still attributes most simulated CPU work to the DSP16A and R3000A paths; it
does not justify changing those CPU cores as part of the GPU work.

A fresh 2026-11-08 run repeated the frame-1074 comparison on the Mesa Intel
UHD 630, with `MAMERECOMP_PSX_GPU_GL_CHECK=1`, the software reference enabled,
and per-command tracing. The process completed all 1,074 vblanks with no GL
error or scanout difference reported; both textured polygon commands in the
portrait scene again had zero differing VRAM pixels. This supplements the
GTX 1650 comparison and catches driver-specific behavior on the integrated
GPU.

After the scanline precomputation change, the Mesa UHD 630 smoke comparisons
were repeated for ZN-1 `bldyror2` and Namco System 12 `tekken3`, each for 600
vblanks with `MAMERECOMP_PSX_GPU_GL_CHECK=1` and the software reference
enabled. They completed at 512×480 and 640×240 respectively, with no GL error
or differing scanout reported. These are boot/scanout checks and do not claim
full gameplay parity for either title.

The CPU-to-VRAM upload path now passes the existing little-endian VRAM mirror
directly to `glTexSubImage2D()` with a 1024-pixel row length, splitting only at
the VRAM X/Y wrap boundaries. This removes the per-transfer pixel-by-pixel
repack allocation and preserves the odd-width row alignment fix; GL unpack
state is restored after each upload and big-endian hosts request component
byte swapping. A matched 600-frame SFEX2PA run on the Mesa UHD 630 took 13.43 s
with `--gpu-hw` and 13.23 s in software, with identical guest instruction and
cycle totals. This change removes a copy but did not produce a measurable
overall speedup in that CPU-dominated run. The frame-1074 portrait comparison
after the change reported no scanout or textured-command pixel differences.

`psx_gpu_hw_transfer_test` is built only when the OpenGL backend is available;
it skips when the host cannot create an OpenGL 4.3 context. On the development
host it passed on the Mesa UHD 630 through the real MachineMemory DMA channel
2 callbacks. It sends an odd-width A0 upload across both VRAM edges and a GP0
`0x80` copy through linked-list RAM-to-GPU DMA, then reads the pixels through
block GPU-to-RAM DMA and GP0 C0. It also checks a GPU fill followed by DMA C0
readback across X=1023 and Y=511. This exercises the upload, copy, readback,
and DMA synchronization paths rather than relying only on a game capture. A
second sequence interleaves A0, separate draws, two 0x80 copies, and C0 to
verify that disjoint VRAM regions survive successive ping-pong writes. The
test also draws an indexed texel to populate the texture-page cache, changes
that source texel using a later A0 DMA upload, and draws again with the same
cache key. C0 readback observes the new CLUT color, verifying that CPU writes
invalidate cached GPU texture data before the next draw. It then modifies the
same cached source page with a GPU fill and confirms the subsequent textured
draw sees that update as well, covering GPU-write invalidation and the
render-target-to-texture ordering boundary.

`psx_gpu_hw_parity_test` runs the same GP0 stream through separate software
and OpenGL GPU instances, then reads back and compares all 524,288 VRAM
pixels. The stream covers 15bpp, indexed 4bpp/CLUT and 8bpp/CLUT texturing,
texture-window source addressing, Gouraud-textured triangles, Gouraud
triangles/quads, flat/Gouraud lines, overlapping VRAM copy, all four ABR
semi-transparency equations with mask disabled, mask set/check, draw-area
clipping, and fills. It passed on Mesa Intel UHD
630 with exact VRAM parity; together with the DMA test, this gives repeatable
driver-backed coverage of draw ordering, texture coherence, and CPU-visible
transfers. It does not replace full-game checks on discrete GPUs or other
driver implementations. A second software/OpenGL pair uses GPU type 0 and
compares the readback of an interleaved 8bpp texture crossing from UV row 0 to
row 1; it passes with exact pixel parity. This exercises the older PSX texture
addressing path separately from the ZN/System 12 GPU type 2 path. A GPU type 1
case also draws a horizontally flipped raw 15bpp sprite from U=0 across the
255→0 boundary. That test found a real OpenGL mismatch: interpolation reached
U=-0.5 at the second pixel and GLSL integer conversion truncated it to zero,
repeating the first texel. Both the draw and batch fragment shaders now floor
interpolated UVs before the 8-bit wrap; the test now matches the software
pixels `[0x001f, 0x03e0]` exactly. A fresh SFEX2PA frame-1074 capture with
coin at 300 and start at 950 was then rendered through both backends; `cmp`
and SHA-256 confirmed the complete 512x240 PPMs are byte-identical, and guest
instruction/cycle totals matched. This verifies the shader change against the
real ZN-2 attract/character-select sequence as well as the synthetic wrap case.

That parity test also compares the complete RGB output from software and
OpenGL scanout after configuring GP1 modes/ranges independently on each device.
The exact-pixel checks cover 256x240 RGB15, 320x240 RGB24 with a partial beam
window and a VRAM start near both wrap edges, 384-pixel horizontal mode,
512x480 interlaced RGB15, and 320x256 PAL RGB15. These passed on the Mesa UHD
630. Direct-present uses the same scanout compute result, but window swap and
compositor behavior are not part of this offscreen pixel comparison.

The previous 37,590-pixel difference reported for SFEX2PA gameplay frame 1560
was from non-batched polygons going through the OpenGL triangle-varying path.
Those polygons now use the compute rasterizer's fixed-point scanline path,
matching the MAME-derived software rasterizer. A fresh frame-1560 run on Mesa
UHD 630 had zero command-level VRAM transitions for the traced textured
polygons and sprites, and the final RGB comparison reported no differing
pixels. On a separate 180-frame headless profile, GP0 dispatch took 0.70 ms
with OpenGL and 4.91 ms with software across 57 packets; the complete run took
4,019 ms and the DSP16A run accounted for 2,832 ms. This explains why reducing
raster CPU work has little effect on total ZN-2 runtime in that test; it does
not measure GPU execution latency or guarantee a gameplay frame rate.

A fresh paired headless benchmark on the Mesa UHD 630 ran SFEX2PA for 600
frames twice per backend with distinct temporary NVRAM files and the same ROM
and inputs. OpenGL took 11.73 s and 11.86 s (11.79 s mean); software took
11.88 s and 11.67 s (11.78 s mean). Each run executed exactly 250,726,699
guest instructions/cycles and updated 600 frames. With no window or capture
requested, this isolates raster command processing from scanout/readback and
shows no total-runtime speedup on this CPU-dominated workload. It must not be
used to judge direct-present throughput, which needs a separate windowed
benchmark.

Cross-board 600-frame headless timing on the same UHD 630 was also repeated
twice. Bloody Roar 2 (ZN-1) took 1.63/1.63 s with OpenGL and 1.57/1.57 s with
software, with identical 169,836,249 guest cycles/instructions. Its OpenGL run
submitted 124,852 primitives in 518 batches and reported one texture-cache
generation, 101,700 hits, and no invalidations/evictions. Tekken 3 (System 12)
took 5.18/5.17 s with OpenGL and 5.18/5.15 s with software, with identical
250,726,699 guest cycles/instructions and 62,681,653 H8 cycles. Its OpenGL run
submitted 51,767 primitives in 313 batches, with one texture-cache generation,
51,669 hits, and no invalidations/evictions. These boot/attract workloads show
the renderer is active on ZN-1 and System 12, but their total runtime remains
dominated by CPU/device execution and does not show an overall speedup.

The direct-present route was separately timed once on a windowed 300-frame
SFEX2PA run: 6.949 s with the PSX OpenGL blit and 6.950 s with the SDL
presenter receiving the software-rendered RGB frame. Guest cycles and
instructions matched. This establishes that direct presentation avoids a
visible CPU-side penalty in this sample, but is not a repeated throughput
result and does not show a material total-runtime gain.

The earlier frame-900 QSound-logo and frame-1500 ARIKA-intro mismatches are
also superseded: fresh Mesa UHD 630 runs with `MAMERECOMP_PSX_GPU_GL_CHECK=1`
and software comparison completed 900 and 1,500 vblanks respectively, with no
GL error or RGB pixel difference reported at their final captures. Together
with the frame-1074 portrait and frame-1560 gameplay checks, these cover the
previously reported logo, intro, character-select, and fight scenes. They are
specific SFEX2PA checkpoints, not proof of every GP0 family or every ZN title.

On 2026-11-08, the synthetic OpenGL parity and DMA/transfer tests were also run
directly with NVIDIA PRIME offload. Both reported
`NVIDIA GeForce GTX 1650/PCIe/SSE2` as the active renderer and passed: parity
covered GPU types 0, 1, and 2 plus scanout modes, while transfer coverage
exercised odd-width CPU uploads, GP0 copies, DMA readback, and texture-cache
coherence. This confirms those paths on a second vendor's driver; it does not
replace a full-game NVIDIA capture or broader hardware validation.

The DMA test now also primes an indexed-texture cache entry, changes its CLUT
with a GPU fill, and redraws before a DMA A0 upload changes the same CLUT again.
The subsequent texture draws observe both new colors. This passed on Mesa
llvmpipe and the NVIDIA GTX 1650, exercising palette coherence across queued
GPU writes and CPU-to-VRAM transfers.

A paired SFEX2PA frame-1074 capture was then run on the GTX 1650 with the same
ROM, coin/start events, and independent fresh temporary NVRAM files. The
OpenGL and software PPM outputs had the same SHA-256
(`70e72d25918fd97b896e3c03c0b371810dfc2dcc49c895e42c2d24c74bccbb0c`) and
`cmp` reported byte-for-byte identity, including the Ryu portrait. Both runs
reported 448,800,791 guest instructions/cycles and 1,074 frame updates. This
confirms the portrait fix and this real ZN-2 scene on NVIDIA; it remains a
single-frame gameplay validation, not a performance benchmark or full-title
parity proof.

The matched SFEX2PA gameplay frame 1560 was also captured on the GTX 1650 with
the same coin/start/attack events and independent fresh NVRAM. The OpenGL and
software PPM files were byte-identical (SHA-256
`48f8ff6ef121e4ef4ee4bb37f6e4d6d0e201e9c71e4c81fcf59943e8097ea453`), and
both runs reported 651,889,417 guest instructions/cycles and 1,560 frame
updates. This checks the 3D fight scene on the discrete driver in addition to
the portrait frame; it is still a selected-frame comparison, not a continuous
full-title image-parity or performance result.

An opt-in GPU timer-query probe is available with
`MAMERECOMP_PSX_GPU_TIMING=1`. It times batch draws, immediate draws, raster
compute, texture-cache generation, and scanout dispatches without waiting
during normal submission; results are collected asynchronously and outstanding
queries are drained at shutdown. The first GTX 1650 profile measured only
opaque batch draws. An extended 600-frame headless SFEX2PA boot/attract run
recorded 348 batch draws / 47,441 primitives at 29.23 ms, 698 raster-compute
dispatches at 69.51 ms, one texture-cache dispatch at 0.072 ms, and one scanout
dispatch at 0.158 ms; no query was dropped. The run took 12.27 s and executed
250,726,699 guest instructions/cycles. These categories total about 99 ms
(0.8% of this run's wall time), indicating the measured GPU operations are not
the dominant cost for this workload. This excludes CPU-side command
preparation, GL upload/transfer work, and SDL/compositor presentation; timer
mode also calls `glFinish()` at shutdown, so its wall time is not a baseline
performance measurement. The extended probe passed the parity and DMA tests
on both the GTX 1650 and Mesa Intel UHD 630.

Three paired 600-frame SFEX2PA runtime profiles on the GTX 1650 recorded the
same 52,742 GP0 packets and 250,726,699 guest instructions/cycles. Software
GP0 packet processing took 89.3–91.6 ms; the OpenGL path took 52.7–70.1 ms
(two of those runs also had GPU timer queries enabled). This indicates a
reduction in CPU time spent processing GP0 packets, but only tens of
milliseconds across 600 frames. `run_until` was 11.46–11.67 s with OpenGL and
11.59–12.10 s with software, with overlapping run-to-run variation. The
DSP16A LLE accounted for 9.33–9.52 s and 9.44–9.87 s respectively, so the
available data does not establish an overall runtime speedup attributable to
the renderer. These profiles use instrumentation and are diagnostic, not
baseline benchmarks.

An additional batched-polygon parity fixture now covers a four-vertex textured
quad where GP0's coordinate-wrap rule rejects the first triangle and keeps the
second. The batch scanline edge walk had a different `next` table from the
software GPU's surviving-quad order, so it could walk into the discarded
vertex and stretch the textured region. Matching the software order restored
exact VRAM parity for this case on the OpenGL test path (Mesa llvmpipe). This
synthetic regression test isolates the geometry condition. After commit
`045e70d`, SFEX2PA frame 1074 was recaptured outside the sandbox on the NVIDIA
GTX 1650, with fresh isolated NVRAM and coin/start events at frames 300/950.
The 512x240 software and OpenGL PPMs are byte-identical (SHA-256
`75a664abef6fb51831c87cae08b3ab72acabbae7e613363a7680b1342274d026`); both
runs executed 448,800,791 guest instructions/cycles. The Ryu portrait is intact
in the new capture. This verifies that selected frame on NVIDIA, not continuous
full-game parity.

The GP0 dithering path is checked with E1 bit 9 set and a constant-color
Gouraud triangle and flat line. Both assert the expected RGB555 value `0x3def`
at known matrix positions; a solid rectangle asserts `0x4210` to verify it
stays undithered. The fixture then compares the entire VRAM image between
software and OpenGL. It passes on Mesa llvmpipe and NVIDIA GTX 1650. The dither
matrix and per-primitive enable rules were checked against the local DuckStation
GPU command setup and shader implementation; sprites and raw textures remain
undithered, while Gouraud/modulated textured polygons and lines use the E1 state
as appropriate.

Two fresh-NVRAM 600-frame SFEX2PA headless profile pairs were measured on the
GTX 1650 with `MAMERECOMP_ZN_PROFILE_RUNTIME=1`. Both modes processed 52,742
GP0 packets and 250,726,699 guest instructions/cycles. Mean CPU time attributed
to GP0 packet raster/dispatch was 51.75 ms with `--gpu-hw` and 87.65 ms with
software (about 41% less). Mean `run_until` was 11.55 s and 11.64 s
respectively, under 1% apart and within observed run variation. DSP16A execution
accounted for about 9.4–9.5 s in these runs. This workload confirms that GPU_HW
reduces CPU work inside the renderer but is not the main lever for total ZN-2
runtime performance; the profile is diagnostic, not a stable benchmark.

The native-window direct-present path was also measured on the GTX 1650 in two
matched 300-frame SFEX2PA pairs with fresh NVRAM. `--gpu-hw --window` took
7.144/7.301 s; software rendering with the SDL OpenGL presenter took
7.180/7.331 s. Each run reported 125,363,350 guest instructions/cycles and 300
frame updates. The means (7.22 s GPU_HW, 7.26 s software) are effectively
equal at this sample size, so direct GPU presentation introduces no observed
total-runtime penalty but does not demonstrate a material speedup here.
