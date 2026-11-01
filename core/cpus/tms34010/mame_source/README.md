# Vendored MAME TMS34010 source

This directory preserves the upstream TMS34010/TMS34020 sources from
`~/git/mame/src/devices/cpu/tms34010`. The upstream headers and BSD-3-Clause
notices remain with each source file.

Upstream repository and source directory:
<https://github.com/mamedev/mame/tree/master/src/devices/cpu/tms34010>

The portable adapter and current port status are documented in
[`../MAME_PORT.md`](../MAME_PORT.md). The MAME device implementation is a
reference and source for the adapted CPU handlers; MAME's framework-dependent
device, scheduler, address-space and screen code is not compiled into
MameRecomp.
