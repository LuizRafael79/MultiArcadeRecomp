# Flycast PVR runtime reference snapshot

This directory is a source snapshot of Flycast's PowerVR2 hardware runtime
(`core/hw/pvr`) for the Naomi PVR/TA port. It is reference/source material;
MameRecomp does not compile these files unchanged yet.

- Upstream: https://github.com/flycast/flycast
- Source checkout: `/home/luiz/git/flycast/core/hw/pvr`
- Revision: `1b63c8a3677cc8eb1292863c4fe8fc8f94674fc8`
- License: GNU GPL version 2; see [`LICENSE`](../../license/flycast-pvr/LICENSE).
- Included path: PowerVR2 registers, SPG, VRAM, TA packet parsing and renderer
  interfaces. `renderer-reference/` adds Flycast texture conversion/cache and
  triangle attribute helpers (`TexCache`, `texconv`, and `sorter`) needed to
  carry scene primitives through the graphics pipeline. The Flycast Dynarec
  is not included or used by this snapshot.

The snapshot depends on Flycast's global machine state, memory services,
settings, and renderer backend interfaces. The port must adapt those seams to
MameRecomp's Naomi bus and graphics presenter. Keep the stages separate:
SH-4 CPU execution in `core/cpus/sh4`, Naomi/Holly device routing in
`src/naomi`, and PVR TA/scene rendering in the Naomi video runtime. Do not
mistake this reference snapshot for an active or complete PVR implementation.
Naomi's active TA path decodes scene primitives, preserves continuation pass
indices, seeds the guest region-array OPB pointers, and reads the region-array
tile bounds at STARTRENDER. A software geometry pass currently rasterizes
non-textured strips and sprites with region clipping and depth comparison, then
uses the Flycast framebuffer writer. Textured primitives are skipped while
texture decoding/sampling is ported; tile sorting, blending, fog and modifier
volumes also remain. `captureFramebuffer()` scans out the guest VRAM contents.
