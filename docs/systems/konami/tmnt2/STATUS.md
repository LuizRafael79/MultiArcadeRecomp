# Konami TMNT2 / Sunset Riders board status

The later TMNT2-family board is implemented separately from original TMNT.
Its board CPU adapters are in `src/konami/tmnt2_machine.cpp`,
`src/konami/tmnt2_m68k.cpp`, and `src/konami/tmnt2_z80.cpp`. ROM manifests for
TMNT2 and Sunset Riders are under `src/romsets/konami/tmnt2/`. CMake builds
`multiarcaderecomp_konami_tmnt2`; generated AOT sources are selected separately by
game identifier.

The 68000 AOT bridge and Musashi fallback are shared CPU modules. The board
owns its memory map, interrupts, EEPROM, and sound wiring. It no longer links
to System 16 runtime code. The Z80 and OPM implementations are shared hardware
components, with board callbacks and synchronization kept in this adapter.

The board library compiled in the focused Linux x64 Release build during the
68000 runtime separation. This was not a full application link or game boot.
