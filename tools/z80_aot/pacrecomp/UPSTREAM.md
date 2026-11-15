# Pacrecomp Z80 frontend

The files in this directory are copied from the MIT-licensed
[`sp00nznet/pacrecomp`](https://github.com/sp00nznet/pacrecomp) project:

- `decode.c` / `decode.h`
- `analyze.c` / `analyze.h`
- `emit.c` / `emit.h`

The original MIT license and copyright notice are preserved in `LICENSE`.
These files are used as a locally built code-generation reference while the
MameRecomp Z80 emitter is adapted to the project's runtime ABI. Pacrecomp's
Pac-Man CPU, memory map, I/O, and dispatcher runtime are not included here and
must not be treated as the KOF98 runtime.

Upstream source was reviewed from the public repository on 2026-11-13. The
review copy did not contain Git metadata, so this import is not pinned to a
commit hash.
