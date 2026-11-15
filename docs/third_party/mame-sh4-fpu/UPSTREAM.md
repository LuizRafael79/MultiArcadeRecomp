# MAME SH-4 vector FPU algorithms

The sh4fpu.cpp implementation is ported from MAME's SH-4 software FPU
algorithms:

- Upstream checkout: /home/luiz/git/mame
- Upstream revision: b2a182f0b761030db051210494ed7c6b7e412eb6
- Upstream files: src/devices/cpu/sh/sh4fpu.cpp and sh4fpu.h
- Upstream license: MIT, retained in the imported source headers

The port removes MAME's emu.h utility dependencies and keeps the numerical
implementation independent of the MAME device framework. MultiArcadeRecomp's SH-4
interpreter calls it for FIPR, FTRV, FSCA and FSRRA; the Naomi-owned CPU state
and FPSCR remain in Sh4Context. The source's MIT license is in [`docs/license/mame-sh4-fpu/MIT`](../../license/mame-sh4-fpu/MIT); retain it and the source-header copyright notices with redistributions.
