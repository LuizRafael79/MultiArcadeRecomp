# CPS-2 runtime integration

Each per-game runtime module is built from the user's separate
`cps2recomp` checkout, selected with `--source-dir` or
`MAMERECOMP_CPS2_SOURCE`. `CMakeLists.txt` compiles the upstream board runtime,
the selected game's generated and shared-family code, and the existing SDL
port into `libmamerecomp_cps2_<game>.so`. `module_entry.cpp` exposes the
upstream game entry point to the host. Each game keeps an isolated build and
work directory; local profiles can add a game without changing the upstream
checkout (the current `sfz2a` profile shares SFA2 sources).

The upstream project identifies its original runtime and game code as
GPL-3.0-only; see its `LICENSE` and `THIRD_PARTY_NOTICES.md`. In particular,
the board implementation in `machine/`, `video/`, and `sound/z80.cpp` is
described there as original CPS2Recomp code based on hardware behavior and
MAME's driver documentation. It is not a wholesale copy of MAME's board
runtime. `machine/cps2crypt.cpp` derives from MAME's CPS-2 cipher implementation
and is BSD-3-Clause; `sound/qsound.cpp` is a rewrite based on MAME's QSound HLE
and is also credited BSD-3-Clause in the upstream notices. Keep those
attributions and licenses with any redistributed source or binary.

The existing upstream SDL port remains in each module build, including
its F1 menu and optional features. MameRecomp's public CLI supplies only the
normal ROM, EEPROM, settings, native CPU, headless, and frame-limit arguments;
the menu is not modified by this integration and can be replaced later by the
project's common frontend.

**Distribution gate:** the project license at [`docs/license/mamerecomp/LICENSE.md`](../../../license/mamerecomp/LICENSE.md) is GPL-2.0-only,
whereas the linked upstream runtime is GPL-3.0-only. Do not treat the combined
module as redistributable under the project license at `docs/license/mamerecomp/LICENSE.md`. The project
license and third-party notices need a deliberate, complete review before
distribution. The current integration is for local development and validation.
