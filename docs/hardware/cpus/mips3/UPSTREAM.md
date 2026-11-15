# MIPS III recompiler source

`recomp.py` and the initial R4600 runtime slice under `core/cpus/mips3/` are
adapted from [UltraCombo](https://github.com/thefixinhixon/UltraCombo),
`tools/recomp.py`, `src/cpu.c`, `src/cpu.h`, `src/disasm.c`, `src/rec.c`, and
the generated-code headers (MIT, copyright 2026 thefixinhixon; inspected
checkout at `b7ae26e7605efbe53cb60433ade26d5579900d8e`). The translator keeps
its recursive-descent discovery, delay-slot handling, pointer scan,
computed-jump discovery and C emitter. The runtime port removes direct DCS
and game-profile dependencies from the CPU dispatcher. Preserve the upstream
MIT notice with this code.

[`ki-recomp`](https://github.com/kilnan/ki-recomp) (inspected checkout
`5800e733fd00a0e8892301723e1d924444dca9fa`) was reviewed as a secondary reference.
Its MIPS III translator provides useful richer diagnostics and function-level
capture tooling, but its emitted C++ calls the KI1-specific `ki::Context`
runtime. Both KI1 and KI2 avoid a post-load MAME snapshot: their U98 boot EPROMs
contain compressed game images decoded offline by the pinned
`ki-rom`/`kipack` tool (`-t ki1` and `-t ki2`). KI2's executable image and
pointer table were compared byte for byte with a MAME RAM capture before
switching its generator to the offline path; its data image is mutable and
should not be used as a byte-equality oracle after boot.

The emitter produces intermediate `rec_*.c`/`rec_tab.c` files using the
runtime ABI now ported under `core/cpus/mips3/`. Configure
`MULTIARCADERECOMP_KI_MIPS_AOT_DIR` to compile a generated directory into the shared
backend. The Seattle board runtime and `multiarcaderecomp run` integration remain
unfinished. No game-derived output belongs in the repository.
