# SH-4 recompilation sources

`sh4recomp.py` and `codegen.py` were copied from the user's local
Initial-D-Arcade-Stage-3-Native-SH-4-Recompilation-WIP checkout for reuse in
MultiArcadeRecomp. Their instruction decoder and C++ emitter are staged here without
game-specific translation data. Keep upstream attribution and applicable
license notices with these files and any derivative changes.

`codegen.py` is adapted at the output boundary to emit per-context memory
callbacks (`ctx.read*`/`ctx.write*`) instead of Initial D's global `MEMxx`
macros. The generated C++ therefore targets `naomi_sh4_context.hpp`. The
scripts are not yet wired into a MvC2 ROM analyzer or compiled dispatch table.
Initial D's captured call-site shims and Naomi 2/ELAN-specific behavior must be
reviewed separately before use in a MvC2 translation.
