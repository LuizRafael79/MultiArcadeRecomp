# Killer Instinct build-side sources

- [UltraCombo](https://github.com/thefixinhixon/UltraCombo): MIPS III static
  translator adapted into `../mips3/recomp.py`; MIT notice is in
  [`docs/license/ultracombo-mips3/LICENSE.UltraCombo`](../../../license/ultracombo-mips3/LICENSE.UltraCombo).
- [ki-recomp](https://github.com/kilnan/ki-recomp): inspected for KI1's
  boot-ROM extraction pipeline and verification against MAME DRAM. Its emitted
  CPU/runtime code was not copied.
- [ki-rom](https://github.com/nekuz0r/ki-rom): `kipack` is used as a separate,
  build-time utility to decode the custom compressed MIPS images in KI1's U98
  EPROM. The builder checks out commit
  `8e4344cfd2b039e2ca97c9f115bb8f1acaff804b` under ignored
  `build/ki/work/ki1/tooldeps/`; it is not linked into or copied into
  MultiArcadeRecomp. Its AGPL-3.0 license remains with that external checkout. A
  prebuilt executable can be supplied with `--kipack`.
- MAME `src/mame/rare/kinst.cpp`: parent-set ROM checksums, CHD identities,
  memory map and Seattle hardware declarations.
