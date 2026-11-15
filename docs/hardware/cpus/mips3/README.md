# Shared MIPS III/R4600 CPU backend (prototype)

This directory contains the shared MIPS III/R4600 interpreter, disassembler,
exception/delay-slot machinery, and AOT dispatcher/ABI adapted from
UltraCombo. KI1 and KI2 use the same CPU implementation. The generated C from
`tools/mips3/recomp.py` can be compiled into this backend by setting
`MULTIARCADERECOMP_KI_MIPS_AOT_DIR` at CMake configure time; generated tables are
adapted by `linked_module.c`.

Keep CPU instruction semantics, delay slots, exceptions, and compiled block
dispatch here. Seattle address map, RAM/EPROM layout, IDE, video and DCS
communication belong under `src/ki/runtime/`; game ROM/CHD declarations belong
under `src/ki/games/`. The current imported CPU ABI still assumes Seattle's
RAM layout for fast translated loads/stores, so it is a shared KI-family
backend prototype rather than a board-independent MIPS III library. Generalize
that memory interface before reusing it on a different MIPS board.

Source provenance and the upstream revision are recorded in
[`docs/hardware/cpus/mips3/UPSTREAM.md`](UPSTREAM.md). The upstream MIT
license is in [`docs/license/ultracombo-mips3/LICENSE.UltraCombo`](../../../license/ultracombo-mips3/LICENSE.UltraCombo).
