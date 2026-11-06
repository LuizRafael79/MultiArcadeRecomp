# MAME H8 CPU code

The generated instruction handlers and arithmetic helpers in this directory
are adapted from MAME's `src/devices/cpu/h8/` implementation. Copyright
holders include Olivier Galibert and the MAME H8 contributors. They are
distributed under the BSD 3-Clause license; see the MAME repository's
`LICENSE` file for the complete license text.

The handlers depend on MameRecomp's standalone CPU state and callbacks. MAME
peripherals, scheduler, debugger, DMA controller, and interrupt controller are
not included by these files and must be implemented or connected separately.
