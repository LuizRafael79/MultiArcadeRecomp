# Shared MC6809 / MC6809E core

The MC6809E in Mortal Kombat's Williams ADPCM board shares the MC6809
instruction set. The `E` suffix describes the external clock and bus signals;
that electrical timing difference belongs to the sound-board runtime, not a
separate instruction decoder.

`m6809.hpp` defines the CPU state and bus boundary. `tools/m6809/recomp.py`
currently builds a first AOT slice for the sound CPU's reset path. It models
the eight `$4000-$bfff` ROM banks separately and the fixed `$c000-$ffff` ROM
window, so a bank switch changes the AOT lookup context. Unsupported code is
left as an explicit interpreter entry. Opcode coverage and all cycle timings
are still incomplete; the next step is to port MAME's complete MC6809
interpreter behind this same state/bus boundary, then expand native coverage
from the observed execution trace.

The MAME source and generated opcode dispatch used for that port are retained
under [`mame_source`](../../../references/mame/cpus/m6809.md). The AOT implementation is local
code and does not link MAME's device framework.
