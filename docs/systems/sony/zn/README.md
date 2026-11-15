# Sony ZN modules

Sony ZN runs through the single `multiarcaderecomp` executable. It owns the CLI and
runtime dispatch, and loads game CPU translations from board/game `.so`
modules.

SFEX2PA uses `multiarcaderecomp_zn2_sfex2pa.so` for its Z80 and R3000A AOT tables.
The ZN build recipe generates the Z80 translation first, profiles live RAM
code through `multiarcaderecomp run`, then adds the captured R3000A translation to
the same game module. It builds that module without rebuilding the all-system
launcher.

ZN-1 `bldyror2` and ZN-2 `sfex2pa` run through the same executable and shared
board runtime. A ZN-1 AOT build recipe is not integrated yet.
