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

## Current structure

- `core/cpus/psx/gpu.cpp` contains GP0/GP1 command handling, device state,
  software rasterization, VRAM transfers, and scanout capture in one class.
- `mamerecomp_psx_gpu` is now a separate CMake target from
  `mamerecomp_psx_cpu`; `MachineMemory` composes both through the common PSX
  device API. This is the first structural seam for a reusable backend.
- `core/video/sdl_video_presenter.cpp` creates an SDL `opengl` presenter, but
  its API receives CPU RGB frames. It does not rasterize PSX commands.
- `Gpu` writes directly into the host VRAM vector. GPUREAD, DMA readback,
  texture sampling, and frame capture can observe those writes, so a hardware
  backend must define explicit ordering and coherence before keeping VRAM on
  the device.

## Target architecture

1. Keep GP0/GP1 decoding, status, timing, transfer registers, and board-visible
   behavior in the shared PSX GPU device.
2. Move draw operations behind a backend interface owned by the independent
   PSX GPU module. The software implementation remains the default/reference
   backend and continues to operate on the existing VRAM storage.
3. Add an optional OpenGL implementation in that same module. It owns device
   VRAM images and submits drawing in GP0 order. The texture dimensions follow
   the VRAM allocation supplied by the PSX-derived machine (1024 pixels wide;
   512 or 1024 rows in the current ZN configurations); host output resolution
   and window aspect do not change the emulated drawing surface.
4. Synchronize only at observable boundaries: CPU/DMA reads of VRAM, CPU/DMA
   writes that overlap pending GPU work, scanout capture, and state save/load.
   Insert the required OpenGL memory barriers between ordered commands; avoid
   reading the whole VRAM back after every primitive. GP0 `0x80` VRAM copies
   now snapshot and copy through GPU textures/compute dispatches.
5. Expose an explicit backend choice (`software` or `opengl`). If OpenGL is
   unavailable, report that clearly when requested; automatic mode may fall
   back to software. Keep headless deterministic capture on the software path
   unless an OpenGL context is explicitly available.

The CLI switch is `--gpu-hw`; omitting it selects the software rasterizer.
When the build includes SDL3 and OpenGL, `--gpu-hw` creates an OpenGL 4.3
context. GP0 flat/Gouraud polygons and solid/textured rectangles are rasterized
by the OpenGL triangle pipeline in `gpu_draw.vert` and `gpu_draw.frag`; fills
and lines use the OpenGL compute shader in `gpu_rasterize.comp`. Both paths
write the same integer VRAM textures in GP0 order. The software rasterizer
remains the default when the flag is omitted. A build without SDL3/OpenGL
reports an error if `--gpu-hw` is requested.

OpenGL VRAM is dynamically sized from the machine's allocation. Coordinate
mapping was verified on the Intel UHD 630: a test rectangle requested at
`(10,20)` is read back at `(10,20)..(19,29)`. The FBO Y mapping and rectangle
quad vertex order must stay consistent with the PSX VRAM convention and the
GP0 strip order (`TL, TR, BL, BR`). CPU-visible VRAM and scanout still trigger
readback; the backend is not yet fully GPU-resident for scanout and CPU/DMA
transfers.

## Raster coverage order

Implement and compare each GP0 family against the retained software path:

1. Fill rectangle and untextured flat triangles/quads.
2. Textured rectangles and polygons for 4-bit, 8-bit, and 15-bit texels,
   including CLUT, texture page/window, transparency, and texture STP.
3. Gouraud interpolation, dithering, draw-area clipping/offset, mask test/set,
   and the four semi-transparency equations.
4. Lines, VRAM copy, and transfer interaction. Flat and Gouraud lines now use
   the software path's fixed-point DDA and omit the terminal endpoint. GP0
   `0x80` is GPU-resident; CPU/DMA transfers still need ordering coverage.
5. Display scanout and interlace modes, preserving GP1 display start/ranges and
   the physical aspect ratio selected by the board/presenter. The GPU compute
   scanout is implemented; direct texture presentation and remaining mode
   parity still need work.

For every stage, compare identical command streams and initial VRAM contents
between software and OpenGL, including the final 16-bit VRAM image and the RGB
scanout. Keep the software renderer selectable for regression diagnosis.

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

## Current validation status

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

Hardware-mode display scanout now converts the selected PSX VRAM window to RGB
in an OpenGL compute shader. Only the final RGB frame is read back for the
existing SDL presenter/capture interfaces; ordinary vblank capture no longer
copies the complete emulated VRAM into the CPU shadow. CPU-visible GPUREAD and
other operations that require the full VRAM mirror still synchronize on demand.
The scanout probe matched the software reference across tested horizontal
modes, RGB15/RGB24, display-start wrapping, and interlaced-height configurations.
An SFEX2PA capture at frame 1560 matched the prior hardware scanout byte for
byte. With `--gpu-hw`, the 1560-frame headless run took 43.2 seconds on the
UHD 630 after removing per-primitive flushes; the GP0 profiling total fell from
24.7 seconds to 3.1 seconds. The same run through the software rasterizer took
45.6 seconds. The final 60-frame gameplay window still measured about 45.9 ms
per frame, so GPU command submission and CPU-mediated presentation remain
performance work; the window does not yet present the shared GPU texture
directly.

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

The direct path completed a 457-frame SFEX2PA run on Wayland with the GTX 1650
when `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` was set for diagnosis, and a
900-frame run on X11. During direct-OpenGL debugging, the PSX window may remain
undecorated; restoring its normal decoration is deferred until the renderer is
stable. Other SDL windows keep their normal decoration/backend choice. Runtime
completion is smoke-tested, and pixel output is compared against the software
path below.

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
