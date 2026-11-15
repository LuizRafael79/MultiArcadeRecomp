# superzazu/z80

Source: https://github.com/superzazu/z80
Imported from the NeoGeoRecomp submodule revision `d64fe10`.
License: MIT; see [`LICENSE`](../../license/superzazu-z80/LICENSE).

This is an instruction-level Z80 interpreter, not a Z80 static recompiler. Its memory and I/O callbacks and T-state counter are suitable for adapting the System 16A sound CPU. System 16 must provide its own sound ROM/RAM maps, ports, latches, interrupt scheduling, and YM2151/uPD7751 connections.
