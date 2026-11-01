# Third-party source references

The T-Unit board runtime in this directory is a local adaptation of hardware
behavior documented and implemented by MAME. The MAME emulator framework and
its device classes are not linked into MameRecomp.

Adapted/reference files:

- [MAME `midtunit.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit.cpp)
  — T-Unit address map, input layout, clocks and machine wiring.
- [MAME `midtunit_m.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit_m.cpp)
  — MK protection sequence and sound command/reset behavior.
- [MAME `midtunit_v.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit_v.cpp)
  and [`midtunit_v.ipp`](https://github.com/mamedev/mame/blob/master/src/mame/williams/midtunit_v.ipp)
  — graphics ROM banking, VRAM lanes, scanout and DMA.
- [MAME `williamssound.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/shared/williamssound.cpp)
  — ADPCM board map/banking and YM2151, DAC and OKIM6295 wiring.
- [MAME TMS34010 core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/tms34010)
  — upstream CPU implementation retained under
  [`core/cpus/tms34010/mame_source`](../../core/cpus/tms34010/mame_source/README.md)
  as the source for the ongoing portable interpreter port.
- [MAME MC6809 core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/m6809)
  — upstream MC6809/MC6809E interpreter and opcode dispatch retained under
  [`core/cpus/m6809/mame_source`](../../core/cpus/m6809/mame_source/README.md)
  for the Williams ADPCM sound CPU port.

The relevant MAME files identify themselves as BSD-3-Clause and list the
copyright holders in their headers. The original CPU source files have been
copied with those headers intact. The license text below is retained for the
T-Unit board adaptations; the source tree and upstream links are recorded
above.

## BSD 3-Clause License

Copyright (c) Alex Pasadyn, Zsolt Vasvari, Ernesto Corvi, Aaron Giles
Copyright (c) Aaron Giles

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
