# Z80 backend and board adapters

## Shared CPU rule

There is one MameRecomp Z80 CPU/recompiler implementation for every board.
An arcade board does not change the Z80 instruction set. CPS1, CPS2, Neo Geo,
and any System 16/18 sound CPU must use the same decoder, generated-code ABI,
instruction helpers, and optional fallback. A board supplies only its memory
and I/O bus, ROM banking, interrupt lines, clock ratio, and synchronization
with its sound devices.

Generated code tables remain per game because they come from each game's ROM;
they plug into the shared `mamerecomp_z80_aot` backend. The common code is in
`tools/z80/`, `core/third_party/z80-aot/`, and `core/cpus/z80/`. It is under the generic
`mamerecomp::z80` namespace. The shared interpreter is a required fallback for uncovered instructions,
untranslated blocks, and runtime code. `Mode::NativeStrict` is only a diagnostic
mode and must not replace the normal hybrid mode in board runners.

## Current integration

- The `sf2ce` and `dino` CPS1 commands use the local shared code generator and
  compile the generated tables into `libmamerecomp_z80_aot.a`. Dino's fixed
  opcode view is decoded with its MAME Kabuki key; its data view remains the
  raw ROM bytes. Current generation reports zero unsupported instructions for
  both fixed sound programs. Their runtime board buses and chip scheduling are
  still being connected.
- Neo Geo/KOF98 already uses the same C++ Z80 CPU state/runtime and generated
  tables. Its historic game-facing wrappers now use `mamerecomp::z80`.
- System 16A, System 16B, and System 18 sound adapters now use the common
  `mamerecomp::z80::Z80` interpreter through board-owned memory and port
  callbacks. Board-specific banking, YM synchronization, IRQ and NMI wiring
  stay in each adapter. Their generated per-game AOT tables are not connected
  yet, so those boards currently execute entirely through the shared
  interpreter.
- The older `core/third_party/superzazu-z80` implementation remains built for
  the Neo Geo runtime's legacy mode. Keep that route available until Neo Geo's
  interpreter fallback is also migrated and verified. Do not add new
  `cps1_z80`, `cps2_z80`, or `system16_z80` CPU implementations.

## Timing contract

The shared CPU receives a board-provided cycle budget and memory bus. Bus
callbacks report the current Z80 cycle timestamp so YM2151, OKI MSM6295,
QSound, YM2610, or other board devices can be advanced before each register
access. Per-board scheduler code converts the main CPU/master clock to Z80
cycles, applies board-specific IRQ/NMI timing, and synchronizes audio chips.
Instruction semantics and native/fallback policy stay in the common Z80 core.

## Build artifact example

```sh
python3 tools/recompile.py --system cps1 --game sf2ce \
  --rom-dir /home/luiz/git/mame/roms/sf2ce.zip
```

This generates a game-specific table under `build/cps1/work/sf2ce/gen/z80/`
and builds the shared backend target. It does not yet claim that the CPS1
sound bus or full game runner is connected.
