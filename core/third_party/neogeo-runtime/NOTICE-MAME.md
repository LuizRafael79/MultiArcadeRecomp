# MAME-derived Neo Geo code

The following portions of this runtime are ports/adaptations of MAME Neo Geo
driver code and retain its BSD-3-Clause terms. MAME marks the source files
with `// license:BSD-3-Clause` and lists Bryan McPhail, Ernesto Corvi, Andrew
Prime, and Zsolt Vasvari as copyright holders.

- `src/bus.c`: Neo Geo address-map behavior from MAME `src/mame/snk/neogeo.cpp`.
- `src/io.c`: board I/O, coin, watchdog and RTC behavior from
  `src/mame/snk/neogeo.cpp`.
- `src/lspc.c`: LSPC VRAM ports, sprite animation and timer behavior from
  `src/mame/snk/neogeo_spr.cpp` and `src/mame/snk/neogeo.cpp`.
- `src/video.c`: sprite and fixed-layer rasterization behavior from
  `src/mame/snk/neogeo_spr.cpp` and `src/mame/snk/neogeo_v.cpp`.

The corresponding MAME source files are available at `/home/luiz/git/mame` in
this development environment. MAME is not linked into MameRecomp. The complete
BSD-3-Clause text is in [`docs/license/mame-neogeo/BSD-3-Clause`](../../../docs/license/mame-neogeo/BSD-3-Clause).

These portions are not relicensed under the NeoGeoRecomp MIT license. Preserve
this notice and the BSD-3-Clause license with source and binary distributions.
