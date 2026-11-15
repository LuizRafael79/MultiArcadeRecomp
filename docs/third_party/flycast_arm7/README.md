# ARM7 interpreter reference

`arm-new.h` is vendored from Flycast at commit
`1b63c8a3677cc8eb1292863c4fe8fc8f94674fc8` (`core/hw/arm7/arm-new.h`).
Its header retains the original VisualBoyAdvance GPL-2.0-or-later notice.
The Naomi adapter in `src/naomi/naomi_arm7.*` supplies the CPU state and AICA
memory callbacks; no Flycast runtime or audio mixer is linked.

The local Initial D checkout documents a production ARM7/AICA path, but its
tracked public tree contains only validation probes and does not include that
implementation source.
