# Konami Teenage Mutant Ninja Turtles (1989) status

The original TMNT board is implemented separately from the later TMNT2/Sunset
Riders board. Its board code is in `src/konami/tmnt_machine.cpp` and
`src/konami/tmnt_z80.cpp`; game ROM wiring is in `src/romsets/konami/tmnt/`.
The CMake target is `mamerecomp_konami_tmnt`.

The 68000 AOT bridge and Musashi interpreter fallback are shared CPU modules
under `core/cpus/m68000/runtime/`. TMNT supplies its own board memory callbacks;
it does not depend on System 16 board runtime code. Sound CPU execution uses
the common Z80 backend with board-owned memory and port callbacks.

This target compiled in the focused Linux x64 Release build during the 68000
runtime separation. No full application link or gameplay validation was run in
that step. Track remaining board and game validation here as it is completed.
