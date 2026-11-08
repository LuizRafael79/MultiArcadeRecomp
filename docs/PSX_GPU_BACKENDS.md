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
3. Add an optional OpenGL implementation in that same module. It owns a device
   VRAM image and submits drawing in GP0 order. The emulated PSX VRAM remains
   1024x512 16-bit; host output resolution and window aspect do not change the
   emulated drawing surface.
4. Synchronize only at observable boundaries: CPU/DMA reads of VRAM, CPU/DMA
   writes that overlap pending GPU work, scanout capture, and state save/load.
   Insert the required OpenGL memory barriers between ordered commands; avoid
   reading the whole VRAM back after every primitive.
5. Expose an explicit backend choice (`software` or `opengl`). If OpenGL is
   unavailable, report that clearly when requested; automatic mode may fall
   back to software. Keep headless deterministic capture on the software path
   unless an OpenGL context is explicitly available.

The CLI switch is `--gpu-hw`; omitting it selects the software rasterizer.
When the build includes SDL3 and OpenGL, this switch currently creates a hidden
OpenGL 4.3 context and dispatches GP0 work through
`core/cpus/psx/gpu_rasterize.comp`. A build without those dependencies reports
an error if `--gpu-hw` is requested. The shader keeps emulated VRAM as a
1024x512 `R16UI` image and handles the currently connected fill, flat/Gouraud
polygon, line, solid rectangle, and textured rectangle/polygon paths. VRAM
readback still occurs when the CPU-visible VRAM or scanout is requested, and
GP0 `0x80` currently synchronizes through the CPU implementation. Thus this is
an active hardware raster backend, but not yet a fully GPU-resident scanout or
transfer path.

## Raster coverage order

Implement and compare each GP0 family against the retained software path:

1. Fill rectangle and untextured flat triangles/quads.
2. Textured rectangles and polygons for 4-bit, 8-bit, and 15-bit texels,
   including CLUT, texture page/window, transparency, and texture STP.
3. Gouraud interpolation, dithering, draw-area clipping/offset, mask test/set,
   and the four semi-transparency equations.
4. Lines, VRAM copy, and transfer interaction. Transfers remain device
   operations, but must order against pending GPU draws.
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

The OpenGL path has been built and exercised on llvmpipe. SFEX2PA's 900-frame
logo capture is recognizable, but its RGB output still differs from the
software path; the current image comparison is not a parity pass. Hardware
tests on a physical GPU and final 16-bit VRAM comparisons for all GP0 families
remain pending. In particular, textured semi-transparency and wrapped fill
rectangles have explicit shader handling, but still need a deterministic
command-stream comparison against the software implementation.
