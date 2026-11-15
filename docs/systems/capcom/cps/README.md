# CPS-1 module layout

The CPS-1 runtime separates shared hardware behavior from MAME-derived
per-game configuration and ROM wiring:

```text
src/cps1/
  core/                 shared CPS-1 bus, video, CPS-A/B, inputs, RAM and sound
  machine_profiles/     shared clock, timing and sound-board profiles
  games/<game>/         that game's driver, inputs and ROM load operations
```

`games/<game>/driver.cpp` is the adapter point for the MAME registration. It
selects the MAME machine configuration, input definition, CPS-B device/PAL
profile, initialization and ROM set. Board-wide behavior belongs in `core/`;
game-specific values and load geometry belong in the game's directory. New
drivers should be checked against both the relevant `GAME` registration and
the per-game CPS configuration in MAME's `cps1_v.cpp`.

The supported SF2CE ROM driver describes the `ROM_LOAD*` and `ROM_CONTINUE`
operations, including interleave stride, word swapping, expected byte counts
and CRC32. `cps1_romset_export` verifies and assembles those operations into
regions for the existing static 68000 and Z80 translation pipeline. This
exporter is a bridge into the current pipeline, not a replacement for the
per-game C++ data model.

## Runtime and executable graphics RAM

MAME maps `$900000-$92ffff` as shared graphics RAM and comments that SF2CE
executes code from it. The first pass runs unknown RAM code through Musashi,
records entry PCs, and saves the mapped 192 KiB. The next build pass can feed
that snapshot to `m68krecomp` as an additional region. The generated CPS-1
RAM routines yield after each guest instruction; dispatch compares that
instruction's live bytes with the embedded snapshot before using native code.
Writes to executable RAM use the bus slow path, and any changed instruction
falls back to Musashi. This keeps self-modifying code correct while allowing
unchanged RAM routines to become AOT code. The current SF2CE boot budget has
not reached this address range yet, so this path is structurally implemented
but not exercised by the title-screen run.

The CPS-B profile carries SF2CE's MAME values instead of relying on the
renderer or bus to guess them: self-test register `$32` returns `$0036`,
multiply ports and layer-control/priority/palette registers use the declared
offsets, and tile/star enable masks match `CPS_B_21_DEF`. CPS-B priority masks
are inverted when generating the tilemap foreground mask, matching MAME's
`cps1_update_transmasks` behavior: the register is inverted to form a
transparent-pen mask, so set bits in the register become opaque foreground
pens. Pen 15 remains transparent in the back layer. MAME scans forward to find
the final object marker, then its normal data pointer draws entries from 0
forward; some bootlegs reverse this order. These details are shared by SF2CE
and Dino and need gameplay validation.

For a gameplay run, set `MULTIARCADERECOMP_CPS1_TRACE_COMPOSITION=1` to log changes
to the CPS-B layer-control word, decoded order/enables, CPS-A video control and
the four active priority registers. `MULTIARCADERECOMP_CPS1_DUMP_LAYERS=build/cps1/layers`
also writes one PPM per source layer at each framebuffer capture.

Scroll 2 row-scroll indexing follows MAME's `screen_update_cps1`: MAME assigns
the tilemap scroll to row `(i + scrolly) & 0x3ff`, but reads the value from
`OTHER[(i + ROWSCROLL_OFFS) & 0x3ff]`. For an output scanline `i`, the
corresponding OTHER-RAM index is therefore `screen_y + ROWSCROLL_OFFS`; the
Scroll 2 Y offset selects the tilemap row and must not be subtracted again from
the OTHER-RAM index. This distinction fixed the scrolling aircraft in SF2CE.

## Board coverage and MAME parity

MAME documents CPS-1 as A, B and C boards, with some early sets combining B
and C. A-board revisions are mostly interchangeable, except for clock
differences such as SF2CE's 12 MHz A-board. Video behavior is largely shared,
while B-board/PAL configurations change CPS-B register locations, layer
enables, priority masks, palette control and graphics banking; the C board
also supplies game-specific I/O/protection behavior. The runtime currently
implements the generic tile/sprite path and one concrete video profile:
SF2CE's `CPS_B_21_DEF` with `mapper_S9263B`. Other MAME configurations must be
added as data and validated individually; the presence of a generic renderer
does not imply support for all board/PAL variants.

For the SF2CE video path, tilemap dimensions and scan mappers, graphics layouts,
palette conversion, priority-mask polarity, sprite table latch, and normal
multi-tile sprite expansion follow `cps1_v.cpp`. Remaining known gaps include
starfield ROM rendering and MAME's per-game bootleg kludges. Full-screen flip
is currently applied as a final 180-degree framebuffer transform; MAME applies
flip state inside tilemap and sprite coordinate handling, so the flipped case
still needs a dedicated comparison before it can be called equivalent.

Sprite overlap follows MAME's `prio_transpen` behavior: an opaque pixel sets
the priority bitmap to 31, so the first opaque object in normal list order owns
that pixel and later sprites cannot overwrite it. This fixes the Dino
character-select 1P marker being hidden behind Hannah; the frame-500 MAME and
MultiArcadeRecomp captures now agree on that overlap.

An equal-cycle comparison against MAME frame 3240 currently has about 1.55%
different pixels overall. The static background and aircraft are nearly
aligned after the row-scroll correction; remaining mismatches are concentrated
in sprites and HUD. This is close visual agreement for this capture, not a
claim of exact frame parity or general CPS-1 compatibility.

The QSound profile now has a runtime path for the shared Z80, two shared-RAM
windows, banked sound ROM and QSound sample device. Dino's `init_dino` Kabuki
decode is represented as separate opcode and data views: the shared Z80 AOT
translator decodes instructions from the opcode view and immediate/ordinary
data reads from the data view. This matches MAME's `AS_OPCODES` mapping and
`kabuki_decode`, which decrypts both views. Kabuki does not add Z80
instructions. The common Z80 interpreter uses the same opcode/data bus views
when instructions fall back from AOT.

This path is still experimental. A 600-million-cycle Dino run clears the
68000 shared-Q-RAM handshake after correcting Kabuki data decryption; the
decoded data prefix matches MAME's Z80 program-space bytes, the QSound CPU
issues writes, and the captured frame shows the Cadillacs and Dinosaurs logo.
The reached 68000 path ran through the shared AOT backend without Musashi
fallback, and the observed Z80 path ran through the shared native backend
without interpreted instructions. A short forced-interpreter comparison
matched the AOT run's framebuffer hash and QSound counters, but its frame was
still black at that early point. The QSound map now includes IN2/IN3, coin
control outputs and a serial 93C46 8-bit EEPROM with optional persistent image
(`--eeprom PATH`). The window maps the third player's controls to the keypad.
The EEPROM protocol and persistence still need comparison with MAME, and the
game has not been confirmed through attract/gameplay. Full video/audio and
in-game behavior remain unverified.

The renderer selects the graphics mapper from the game profile. Dino's
`mapper_CD63B` uses two 0x8000 banks for sprites and all scroll layers; using
SF2CE's `mapper_S9263B` ranges had made most Dino tile codes invalid. After
selecting the CD63B ranges, a fresh 600M-cycle capture contains the green
background, logo and disclaimer. This does not resolve the shared layer
priority/composition issues, which remain open for both Dino and SF2CE.

SF2CE's sound runtime clocks the Z80 against the 68000, runs the YM2151 through
the shared YMFM OPM core, and implements the OKIM6295 command/status path, four
ADPCM voices and pin-7 rate selection. The mixer follows MAME's mono routes
(YM2151 outputs at 0.35 each and OKI at 0.30). Z80 native execution and its
interpretive fallback remain enabled. Pass `--window --oki
build/cps1/work/sf2ce/roms/oki.bin` to the generated runner to hear sound;
headless frame generation still clocks both devices without opening an audio
device. QSound is implemented as an experimental runtime path; Dino boot and
audio remain unverified.

## TODO: deep video parity audit

The renderer is a useful SF2CE prototype, not a verified reproduction of every
stage in MAME's CPS-1 video pipeline. Audit each item against `cps1_v.cpp`,
recording the game, board/PAL profile, frame and source register/RAM state:

- [ ] CPS-A video-base register decode, alignment, aperture wrapping and live
  base changes for object, scroll, OTHER and palette RAM.
- [ ] CPS-A video-control bits: layer enables, row scroll, star enables and
  flip, including flip effects on tilemap and sprite coordinates.
- [ ] CPS-B profile selection and layer-control decode for supported board/PAL
  variants, including CPS-C I/O and protection interactions.
- [ ] Graphics ROM bank mapping for every layer and sprites; graphics layouts,
  tile code/attribute decoding and out-of-range tile behavior.
- [ ] Scroll 1/2/3 scan mappers, clipping, offsets and row-scroll indexing in
  static screens and during gameplay motion.
- [ ] Palette page copy/skip semantics, color conversion, background pen and
  palette changes during a frame.
- [ ] CPS-B priority groups and interaction with each layer order. Capture
  intermediate output and sprite-priority bitmap around each high-layer pass.
- [ ] Sprite latch timing, end marker, iteration direction, bank mapping,
  nx/ny expansion, wrapping, flips, clipping and per-game kludges.
- [ ] Starfield ROM decoding/rendering and board-specific star enables.
- [ ] Frame timing: VBlank latch, raster position, partial updates, and writes
  that happen between screen updates.
- [ ] Compare MAME/runtime stages at title, attract/demo, gameplay, scrolling,
  flip-screen and transition states; compare final composition after each
  intermediate stage agrees.

## Build and run

```sh
build/multiarcaderecomp build --machine=cps --board=cps1 --game=sf2ce \
  --rom-dir /path/to/sf2ce.zip --jobs 4
build/multiarcaderecomp run --machine=cps --board=cps1 --game=sf2ce \
  --rom-dir /path/to/sf2ce.zip
```

`build` creates or updates the static translation; `run` only loads the built
game and refuses to run if the source or selected ROM ZIP made the build stale.
Add `--rebuild` to `build` to force a fresh translation.

For a direct live Dino run, use the generated `multiarcaderecomp_dino` runner with
`--window`; the SDL window keeps running until Escape or the window close event.
The positional cycle budget is ignored while the live window is active.

Generated translations, ROM regions, runtime frames and RAM diagnostics live
under `build/cps1/work/sf2ce/`. A ROM export succeeding only proves that the
ROM driver matched; visual correctness must be checked from the runtime
framebuffer and the layer/register traces. The runner reads graphics and OKI
samples directly from the CRC-validated `roms/gfx.bin` and `roms/oki.bin`
regions; the Z80 fixed and banked images are split from `roms/audiocpu.bin` by
the build pipeline. Region sizes are checked before CPU recompilation starts.

### SF2CE controls

When launching the runner with `--window`, the keyboard maps to the CPS-1
active-low input ports as follows:

| Action | Key |
| --- | --- |
| Coin 1 / Coin 2 | `5` / `6` |
| Start 1 / Start 2 | `1` / `2` |
| Player 1 movement | `W` `A` `S` `D` |
| Player 1 punches | `J` `K` `L` |
| Player 1 kicks | `U` `I` `O` |
| Player 2 movement | Arrow keys |
| Player 2 punches / kicks | Numpad `1`–`6` |
| Close the game window | `Esc` |

Coin keys send a one-frame pulse and remain active while held; start and action
keys stay active while held. The SF2 six-button layout follows the MAME input
definition; the extra kick buttons are read from the CPS-B C-board input port.
Set `MULTIARCADERECOMP_CPS1_TRACE_INPUT=1` to log detected keyboard changes and input
port values observed by the 68000.

The positional `frame.ppm` argument writes one still image when the run ends.
For a sequence of snapshots, set `MULTIARCADERECOMP_CPS1_FRAME_DUMP_DIR` to an output
directory. The runner writes one PPM every 60 VBlanks by default; override the
interval with `MULTIARCADERECOMP_CPS1_FRAME_DUMP_EVERY`. For example:

```sh
MULTIARCADERECOMP_CPS1_FRAME_DUMP_DIR=build/cps1/work/sf2ce/frames \
MULTIARCADERECOMP_CPS1_FRAME_DUMP_EVERY=60 \
build/cps1/multiarcaderecomp-host/multiarcaderecomp_sf2ce \
  build/cps1/work/sf2ce/program.bin 1200000000 \
  build/cps1/work/sf2ce/m68k_dynamic_entries.txt \
  build/cps1/work/sf2ce/z80_program.bin \
  build/cps1/work/sf2ce/z80_banked.bin \
  build/cps1/work/sf2ce/z80_misses.txt \
  build/cps1/work/sf2ce/z80_fallbacks.log \
  build/cps1/work/sf2ce/roms/gfx.bin \
  build/cps1/work/sf2ce/frame.ppm \
  --window --oki build/cps1/work/sf2ce/roms/oki.bin
```

For live-window pacing diagnostics, set `MULTIARCADERECOMP_CPS1_TRACE_PACING=1`.
The runner reports measured presentation FPS in 120-frame windows alongside
the CPS1 refresh target computed from the machine profile.
