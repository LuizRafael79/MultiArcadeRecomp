# TMS34010 MAME core port

The MK runtime now has a MAME-derived TMS34010 interpreter fallback. Native
translated blocks remain the first execution path. When dispatch reaches an
untranslated entry, one instruction is executed through the MAME opcode table;
graphics handlers that are still incomplete in the adapter are explicitly
handed to the existing incremental MultiArcadeRecomp implementation.

## Imported source and generated adapter

Upstream reference: [MAME TMS34010 source](https://github.com/mamedev/mame/tree/master/src/devices/cpu/tms34010)

| Source | Current use |
| --- | --- |
| `mame_source/34010tbl.hxx` | Source for the 4096-entry TMS34010 opcode dispatch table. |
| `mame_source/34010ops.hxx` | MAME CPU/control/branch/field/XY/pixel handlers, adapted as member definitions in `mame_ops.inc`. |
| `mame_source/34010fld.hxx` | Field read/write handlers, adapted in `mame_fields.inc`. |
| `mame_source/34010ops.h` | MAME operation macros used by the adapted handlers. |
| `mame_source/34010gfx.hxx` | Retained reference. PIXBLT/FILL/LINE still use the existing partial graphics fallback; these are not fully ported. |
| `mame_source/tms34010.cpp/.h` | Architectural and device reference. MAME scheduler, address-space, display-timing and interrupt framework are not linked. |
| `mame_source/34010dsm.cpp/.h` | MAME disassembler reference. Runtime logging currently reports the dispatch handler name, not full formatted assembly. |

`tools/tms34010/import_mame_opcode_table.py` regenerates the adapter header,
opcode table and adapted `.inc` files from the vendored source. Keep upstream
copyright/license notices in the source files and maintain this generator
when changing the adaptation.

## Runtime boundary and known gaps

`mame_interpreter.cpp` adapts MAME handlers to the shared `State` and
bit-addressed `Bus`. The T-Unit remains in `src/mk/` and owns the board memory
map, I/O side effects, interrupts, VRAM and scanout. The MAME interpreter does
not link MAME's device tree or scheduler. `check_interrupt()` is currently a
no-op because the board run loop owns interrupt delivery; exact exception and
interrupt timing still needs comparison against MAME.

The following are still incomplete and the current build must not be described
as a complete/verified TMS34010 emulator:

- MAME graphics instruction implementations in `34010gfx.hxx`; several
  handlers delegate to the older experimental graphics code.
- Full TMS I/O register side effects, raster operations, plane masking and
  shift-register callbacks.
- Exact exception, interrupt, display timing and cycle accounting.
- Differential instruction/state validation against MAME.

The latest 130-million-cycle MK trace reaches the Revision 5/CMOS screen but
the MAME-first fallback changes the POST chip drawing compared with the prior
partial fallback. Treat this as an integration regression to investigate,
not as a successful POST. Per-instruction traces now name the MAME dispatch
handler and distinguish MAME CPU execution from the partial graphics fallback.
