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
4. Lines, VRAM copy, and transfer interaction. GP0 `0x80` is GPU-resident;
   CPU/DMA transfers still need to order against pending GPU draws.
5. Display scanout and interlace modes, preserving GP1 display start/ranges and
   the physical aspect ratio selected by the board/presenter.

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
