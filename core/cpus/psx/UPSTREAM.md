# PSX GPU reference

`gpu.cpp` ports the flat-polygon scan conversion from MAME's
`src/devices/video/psx.cpp` (`psxgpu_device::FlatPolygon`, MAME BSD-3-Clause,
copyright smf). It now also ports the textured and Gouraud-textured polygon
edge-chain scan conversion from `FlatTexturedPolygon`, adapting MAME's
left/right edge interpolation of UV and color to the local VRAM interface.
Textured quads are drawn as one polygon, matching MAME's path instead of being
split into two local triangles. This GPU remains incomplete; consult MAME's
`psx.cpp` and `psx.h` before extending command and rasterization behavior.

MAME source: <https://github.com/mamedev/mame/tree/master/src/devices/video>

## PSX SPU

`spu_adsr_rates.hpp` transcribes the envelope duration tables from MAME's
`src/devices/sound/spu_tables.cpp` (copyright pSXAuthor and R. Belmont,
BSD-3-Clause). The local SPU implementation uses these tables in its voice
envelope stages; it is not a full port of MAME's stream, reverb, noise, or
modulation implementation.

MAME source: <https://github.com/mamedev/mame/blob/master/src/devices/sound/spu_tables.cpp>
