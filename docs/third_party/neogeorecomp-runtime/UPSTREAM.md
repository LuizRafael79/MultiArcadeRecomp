# Neo Geo runtime source

Copied from the local `neogeorecomp` checkout at upstream revision
`af96b2b4b39b42b392b17bcaa6a74a5f27088202`. The original NeoGeoRecomp
portions retain MIT terms (see [`LICENSE`](../../license/neogeorecomp-runtime/LICENSE)
and [`NOTICE`](../../license/neogeorecomp-runtime/NOTICE)). MAME-derived Neo
Geo board/raster portions retain their separate BSD-3-Clause terms; see
[`NOTICE-MAME.md`](../../../core/third_party/neogeo-runtime/NOTICE-MAME.md)
and [`BSD-3-Clause`](../../license/mame-neogeo/BSD-3-Clause).
The original MVS board model supplies the 68000 address map, LSPC, raster
renderer, I/O, Z80/YM2610 runtime, BIOS boot, frame loop, screenshot output,
and AOT dispatcher with Musashi fallback.

MultiArcadeRecomp changes also include module integration and KOF98 profile hooks,
the PROGSF1 protection callbacks, and the KOF98 profile's ROM loader transform.
MAME is not linked into the runtime. The BSD-derived sections are identified
file by file in `NOTICE-MAME.md`; KOF98 protection is separately guided by
`src/devices/bus/neogeo/prot_kof98.cpp` in the local MAME source.
