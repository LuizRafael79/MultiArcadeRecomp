# Killer Instinct runtime

The current Seattle runtime is an integration prototype for KI1 (`kinst`). Its
board services are split by responsibility:

- `seattle/`: MIPS address map, RAM/EPROM, frame scheduler, video scanout,
  keyboard inputs, and IDE/CHD plus persistent NVRAM.
- `dcs/`: DCS sound-board startup, ROM banking, command path and sample output.
- `core/cpus/mips3/`: shared R4600 implementation.
- `core/third_party/ultracombo-adsp/`: current KI1 ADSP interpreter and
  translator, with Seattle-specific DCS wiring kept here.

KI1 uses UltraCombo's ADSP-2105 static translator and interpreter over its DCS
ROM code images. A short audio startup gap can occur when a coin is inserted
during POST; compare the DCS reset, host-command, IRQ and sample event order.
The MultiArcadeRecomp ADSP backend remains in use for MK1/MK2. Consolidating CPU
implementations into generic shared backends is future work.

KI2 uses the same Seattle board services and is connected to the dispatcher.
Its MAME profile is recorded in `games/ki2/profile.c`; `kipack -t ki2` extracts
the U98 images offline for AOT discovery. The optional live MAME capture remains
useful for examining post-loader RAM and finding additional entries. The user
validated KI2 in-game and marked the prototype complete; it is frozen at this
checkpoint. `kinst2_map()` shuffles several Seattle I/O
addresses, and its CHD reports CHS 1463/13/47 with the IDE serial at word offset
11, so those values remain game-profile data.

Build and launch through the main dispatcher:

```sh
build/multiarcaderecomp build --machine=ki --board=seattle --game=ki1 \
  --rom-dir /path/to/roms --jobs 4
build/multiarcaderecomp run --machine=ki --board=seattle --game=ki1 \
  --rom-dir /path/to/roms --window
```

Replace `ki1` with `ki2` to build or launch KI2.

The run command opens a 960x720 SDL window by default when no `--headless` or
finite `--frames` option is supplied. Controls: P1 arrows, A/S/D and Q/W/E;
coin 5, start 1. P2 uses I/K/J/L, U/O/P; coin 6, start 2. Escape closes the
window. For a finite headless capture, pass `--headless --frames N --frame-out
PATH.ppm`.

This is not yet a claim of complete MAME/UltraCombo parity. KI1 boots through
the MIPS AOT path and produces a live framebuffer; runtime timing, input,
video, IDE/NVRAM and DCS behavior still need comparison against MAME. ADSP
execution uses the UltraCombo translator and interpreter fallback for KI.

Keep game ROM declarations in `src/ki/games/<game>/`, board services under
`src/ki/runtime/`, and reusable CPU logic in `core/cpus/`.
