# Flycast SH-4 interpreter source snapshot

This directory stages the SH-4 interpreter and the adjacent CPU state, opcode,
exception, memory, MMU, cache and scheduling source from Flycast for the Naomi
SH-4 fallback port. The raw upstream files remain a reference snapshot and
are not compiled unchanged; MameRecomp compiles its adapter at
`core/cpus/sh4/flycast_interpreter.cpp`.

- Upstream: https://github.com/flycast/flycast
- Local checkout: `/home/luiz/git/flycast`
- Upstream revision: `1b63c8a3677cc8eb1292863c4fe8fc8f94674fc8`
- License: GNU GPL version 2; see [`LICENSE`](LICENSE), copied from the
  upstream checkout.
- Interpreter implementation: `interpr/sh4_interpreter.cpp`,
  `interpr/sh4_opcodes.cpp`, and `interpr/sh4_fpu.cpp`.
- FSCA sine coefficients: `sh4/fsca-table.h`, consumed by Flycast's
  `sh4_rom.cpp`; the interpreter adapter includes the exact coefficient words
  and applies Flycast's quadrant and sine/cosine indexing rules.

The imported sources expect Flycast's global CPU context, memory/MMU services,
exception controller, cache, scheduler, logging, and initialization framework.
They must not be linked unchanged into `mamerecomp_naomi.so`. The selected
source is Flycast's **Interpreter**, not its Dynarec. Adapt instruction
execution to `mamerecomp::naomi::Sh4Context` and its callback bus, preserving
this project's AOT translator as the fast path. At a missing AOT entry, execute
guest instructions through the interpreter and return to AOT dispatch when a
registered, valid translated entry is reached. Board/device behavior remains
in MameRecomp's Naomi runtime, where MAME's LLE implementations are useful
references.

Do not use Flycast's SH-4 Dynarec as the Naomi fallback. The staged
`dyna/decoder.h` and `dyna/decoder_opcodes.h` are reference-only dependencies
of Flycast's combined opcode metadata header; they are not part of the runtime
port and must not be compiled into MameRecomp.
