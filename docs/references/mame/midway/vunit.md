# MAME V-Unit runtime references

Reference copies from the MAME repository:

- Board and video: <https://github.com/mamedev/mame/tree/master/src/mame/williams/midvunit.cpp>
- DCS Audio 2K runtime: <https://github.com/mamedev/mame/tree/master/src/mame/shared/dcs.cpp>
- ADSP-2100/2105 CPU: <https://github.com/mamedev/mame/tree/master/src/devices/cpu/adsp2100>
- AD1851 DAC: <https://github.com/mamedev/mame/tree/master/src/devices/sound/dac.cpp>

Each copied source keeps its MAME BSD-3-Clause license and copyright headers.
The references are not compiled as part of MameRecomp. Adapted board code is
maintained in the parent module; CPU semantics live in `core/cpus/`, and the
shared DCS Audio 2K device lives in `core/chips/dcs/`.
