# Seibu / TAD Toki status

Toki board code is in `src/seibu/toki_machine.cpp`, `toki_m68k.cpp`, and
`toki_z80.cpp`, with its ROM manifest at `src/romsets/seibu/toki/toki.toml`.
CMake builds the board as `multiarcaderecomp_seibu_toki` and accepts per-game 68000
and Z80 generated sources.

Toki owns its address map, encrypted Z80 setup, video, interrupts, and sound
wiring. It uses the shared 68000 AOT bridge and Musashi fallback under
`core/cpus/m68000/runtime/`, plus common Z80/YMFM/OKI components. It does not
use System 16 board runtime code.

The board library compiled in the focused Linux x64 Release build during the
68000 runtime separation. No full application link or gameplay validation was
run in that step.
