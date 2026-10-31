# Neo Geo runtime source

Copied from the local `neogeorecomp` checkout at upstream revision
`af96b2b4b39b42b392b17bcaa6a74a5f27088202` (MIT; see `LICENSE` and `NOTICE`).
The original MVS board model supplies the 68000 address map, LSPC, raster
renderer, I/O, Z80/YM2610 runtime, BIOS boot, frame loop, screenshot output,
and AOT dispatcher with Musashi fallback.

MameRecomp changes are limited to KOF98 profile hooks, the PROGSF1 protection
callbacks, and the KOF98 profile's ROM loader transform. The implementation is
guided by the local MAME source files `src/mame/snk/neogeo.cpp` and
`src/devices/bus/neogeo/prot_kof98.cpp`; MAME is not linked into the runtime.
