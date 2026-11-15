# MAME MC6809 reference source

This directory preserves the upstream MC6809/MC6809E implementation used to
port a standalone interpreter and compare the AOT recompiler. It is reference
material; these files are not compiled directly because they depend on MAME's
device, address-space, timer, debugger and callback framework.

The generated `m6809.hxx` was produced from `m6809.lst` with the included
`m6809make.py` generator. The generated dispatch is retained so the portable
core can follow MAME's complete opcode behavior without depending on a MAME
build tree.

Upstream: <https://github.com/mamedev/mame/tree/master/src/devices/cpu/m6809>

The upstream files carry BSD-3-Clause notices in their headers. Copyright
holders are Nathan Woods, Sean Riddle, Tim Lindner, John Butler and other
contributors identified in those headers.
