# NeoGeoRecomp 68000 recompiler sources

Source: https://github.com/sp00nznet/neogeorecomp/tree/master/tools/m68krecomp
Upstream revision inspected: `af96b2b` (2026-10-20, v0.2.0)
License: MIT; see [`LICENSE`](../../license/neogeorecomp-m68krecomp/LICENSE) and [`NOTICE`](../../license/neogeorecomp-m68krecomp/NOTICE).

The copied files are the upstream decoder, C emitter, discovery frontend, and raw-image CLI. The upstream emitter targets the Neo Geo runtime ABI (`CPU`, `ng_*`, `NG_INSN`, and Neo Geo dispatch entry types); it is not yet wired into MameRecomp. System 16 must supply its own CPU scheduling, bus, exception, IRQ, and device integration. Do not copy Neo Geo board code into the System 16 runtime.

The raw-image feasibility run against the local Shinobi main CPU image used reset PC `0x400`, discovered 1,510 pointer-scan seeds, and emitted 117,159 instruction occurrences across 1,811 routines / 3,833 dispatch entries. The generated C was about 17 MB and was not compiled or executed. These counts are not directly comparable to MameRecomp's basic-block count because NeoGeoRecomp's routine discovery intentionally duplicates shared code and scans candidate pointers.

Do not commit game ROMs or generated game C. Preserve the upstream MIT notice when redistributing these sources.

The board-independent register/CCR helpers and normal exception-entry code from NeoGeoRecomp are also staged in `include/neogeorecomp/` and `runtime/cpu.c`. The bus header is present only because its inline stack helpers are part of that ABI; its Neo Geo page-map implementation is not being used. `m68k_cpu_runtime` compiles the CPU helper object, but final System 16 bus binding and scheduling are still pending.

The sibling Z80 dependency is separately staged in `core/third_party/superzazu-z80/`; see `docs/third_party/superzazu-z80/UPSTREAM.md`. Its interpreter is architecture-generic, while Neo Geo's sound memory/port map and YM2610 integration are not reused.

A System 16 bus ABI bridge now forwards generated 68000 memory accesses through `System16AddressSpace` callbacks and latches the first failure. It deliberately leaves the Neo Geo page maps unused. The current executable does not yet dispatch generated blocks through this bridge.

MameRecomp adaptation: the vendored C emitter prefixes each guest instruction with `NG_DEBUG_INSN(address)`. The runtime leaves this hook null unless `run-full --debugger` is selected, enabling instruction-level breakpoints and stepping in the System 16 monitor.
