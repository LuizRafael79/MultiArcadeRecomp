# Upstream MAME TMS320C3x source

Copied from MAME `src/devices/cpu/tms320c3x/` for the MameRecomp C31 port.
Upstream repository: <https://github.com/mamedev/mame/tree/master/src/devices/cpu/tms320c3x>

Files retained here:

- `tms320c3x.cpp`, `tms320c3x.h`: CPU family state, device behavior, reset,
  interrupts, register and custom-float conversion, memory addressing.
- `320c3x_ops.ipp`: opcode semantics, addressing mode helpers and dispatch
  tables. The MameRecomp adapter includes these handlers directly.
- `tms320c3x_dasm.cpp`, `tms320c3x_dasm.h`: disassembler reference.

These files retain their upstream notices and are licensed under the
BSD 3-Clause License as indicated in each source header. The MameRecomp
adapter is in the neighboring directory; board behavior is in
`src/midway/vunit/`.
