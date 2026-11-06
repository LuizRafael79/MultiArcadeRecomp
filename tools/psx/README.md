# PlayStation R3000A AOT frontend

`recomp.py` is the first CPU-backend stage for the ZN work. It reads a flat,
little-endian code image plus explicit virtual entry points and emits a C++
translation unit against `core/cpus/psx/psx_aot_abi.h`. It performs recursive
control-flow discovery, includes delay-slot instructions as independent
dispatch entries, and records the source words so the eventual runtime can
detect modified code.

The native subset currently covers common R3000 integer shifts, arithmetic,
logic, comparisons, immediate operations, conditional/direct/register
branches, byte/halfword/word loads and stores, and MULT/DIV with HI/LO access.
Signed ADD/ADDI/SUB overflow routes through the PSX exception helper.
Unimplemented instructions remain at their original PC and return to
`psx_aot_interpret_one`. A partial interpreter and pipeline helpers cover common
MIPS I integer, branch, memory, COP0 and COP2 register-transfer operations. A
shared PSX bus, partial GPU/DMA, CPU-clock root timers, MAME-derived GTE/MDEC
and a ZN board composition layer now exist, but the machine is not complete:
SPU, exact raster/timer behavior, security/audio completion, launch integration
and game validation remain outstanding.

The current MIPS III backend is not directly linked here. Its Seattle memory
assumptions, 64-bit register file, and R4600 pipeline do not match the PSX
R3000A. Its decoder/emitter patterns can inform shared implementation, while
PSX execution state and exception/delay-load behavior remain in this backend.

PSXRecomp revision `060ef0ab` is vendored under
`core/third_party/psxrecomp/`; CMake builds it as `mamerecomp_psxrecomp`.
The command-line compiler successfully translated a synthetic PS-X EXE into
function-discovered C shards in this project build. The generated C also
compiled against the upstream runtime headers, confirming the executable
target is usable as a generator. It is not yet linked to a board module.

A compile-only compatibility header is at
`core/cpus/psx/psxrecomp_abi/`. A synthetic shard containing memory loads and
stores plus an RTPS command compiled against it, exposing the helper ABI the
runtime bridge must implement. The header contains declarations only; it does
not emulate the PSXRecomp runtime.

PSXRecomp provides R3000A decoding, function discovery and dynamic overlays;
its checked-in GTE header is only a placeholder. The
compiler's emitted functions use PSXRecomp's `CPUState` (`gpr[]`, `cop0[]`,
`gte_data[]`, `gte_ctrl[]`, memory callbacks) and global runtime helpers such
as `psx_check_interrupts_at` and `gte_execute`; that is not the in-tree
`PsxCpuState` ABI (`r[]`, bus callbacks, delayed-load/branch pipeline). The
integration therefore needs a deliberate runtime/ABI bridge, not just a
different opcode decoder. The compatibility header and bridge establish a
compile-time ABI smoke only; they do not yet adapt upstream game output into
`ZnMachineRuntime`. ZN's bank selection must participate in dynamic-code
identity so code at a reused guest address in a different ROM bank cannot
dispatch to a stale translation.

The in-tree GTE currently uses MAME's BSD-3-Clause implementation instead.
Compare guest-visible outputs and flags against MAME. If PSXRecomp source is
copied later, preserve the PolyForm
Noncommercial 1.0.0 license, copyright notice and the project's non-commercial
use condition in distribution notices. No H8/3002 recompiler was found in that
repo; that CPU needs a separate implementation, with MAME as the behavior
reference.

This frontend does not yet parse PS-X EXE/ELF/disc layouts, discover dynamic
overlays, or connect generated entries to a launched ZN/System 12 game. The
current `ZnMachineRuntime` interprets from the board BIOS and can accept a table
provided by a caller. Game profiles still need image mapping and roots for
recompilation. Library builds and synthetic tests do not imply full CPU or
game compatibility.
