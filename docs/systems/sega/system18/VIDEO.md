# System 18 video progress — Shadow Dancer

> **Current status (2026-11-12):** The Moonwalker corruption and hostage/HELP ME VDP issues described in the historical captures below were fixed in later changes. The user confirmed the in-game image and effects now render correctly. Those captures remain useful for the debugging history, but do not describe the current runtime.

## Runtime observation — 2026-10-30

The recomp now runs the native generated 68000 table for `shdancer` and captures real frames from its System 18 board runtime. The dog title frame at 10,000,000 cycles is produced by the current 315-5197 tilemap / 315-5196 sprite path; it is not a MAME capture. At 50,000,000 cycles the dog/title image is clean, while the 100,000,000-cycle capture shows the Shadow Dancer title and later captures reach the Stage 1-1 select screen. These images are direct `run-full --frame-out-at-vblank` outputs.

The 315-5313 register and memory state is now included in `--video-state-out` as `vdp_regs.bin`, `vdp_vram_be.bin`, `vdp_cram_be.bin`, and `vdp_vsram_be.bin`; `state.txt` also records VDP enable and mixing latch values. Captures at physical vblanks 60, 300, 600, 780, 1200, 1620, 1800, and 2400 show `vdp_enabled=0`, `vdp_mixing=0`, VDP register 1=`0x05` (display disabled), empty CRAM/VSRAM, and just one initialized VRAM word (two nonzero bytes). Thus the observed dog/title/stage-select/gameplay images do not exercise VDP raster output. The 10M image's scattered pixels were absent in the 50M capture, so they are a transient earlier guest video state, not evidence of VDP output. This covers those observed states only; it does not rule out a brief or effect-specific VDP use.

## Current implementation boundary

- The current System 18 renderer is software only. The `opengl` backend option
  is available for System16A/B; System18 has no OpenGL VDP/tile/sprite path yet.
- The interpreted 315-5313 bus model handles its command/data ports, registers, VRAM/CRAM/VSRAM, basic DMA, status, and interrupt state.
- The System 18 CPU compositor accepts the VDP at the same four insertion points as MAME (`vdp_mixing` layer 0–3), applies the latch priority bit, and maps its 64 CRAM colors through the Mega Drive nonlinear palette curve. The interpreted scanline path has an initial Genesis Mode 5 implementation for scrolling Plane A/B, the Window name table, and linked-list sprites with basic priority/transparency. Mode selection follows the register-1 Genesis bit; register-0 special-palette bit is not used as a mode gate, and register-0 display-disable plus register-1 DISP blank output. This is not yet full VDP parity: exact sprite evaluation/overflow, shadow/highlight, alternate/interlace modes, display-width variants, priority edge cases, and dot-accurate beam timing remain incomplete.
- The runtime reaches actual Stage 1-1 gameplay with scripted coin/start/button events. The game frame is verified; VDP rendering is not exercised because the guest keeps CNT2 low.

## Next work

1. Add an executable Moonwalker profile/translation and validate the active VDP compositor, including its CNT2 and `vdp_mixing` layer 2/3 behavior; the local MAME probe is reference evidence only, not a MultiArcadeRecomp run.
2. Compare Mode 5 Window, sprite order/limits and priority output against MAME captures or device-level state, then implement the missing shadow/highlight and display-mode cases that the game exercises.
3. Validate HBLANK and active VDP composition against an actual System 18 game that raises CNT2; the beam now follows guest cycles but pixel-level/IRQ edge timing remains approximate.

## VDP beam scheduling — 2026-10-30

The `run-full` System 18 loop initially advanced the interpreted VDP beam at 262 scanlines per 60 Hz frame. This has been corrected in the timing update below: MAME configures the board screen at 57.23 Hz and the VDP uses alternate timing from screen vpos. Scanline boundaries still interrupt CPU dispatch for IRQ/state advancement, while each VDP MMIO access now derives H/V beam position from the current guest cycle.

## Gameplay state — 2026-10-30

A scripted run with a coin, start, then start + player button 1 reached the actual Stage 1-1 gameplay. The capture at 400,000,004 68000 cycles is `/tmp/shdancer_stage_confirm.png` (generated from the corresponding real `run-full` PPM). It shows the gameplay scene, player, enemy, status, and HUD. At vblank 2400, the VDP is still disabled, mixing latch is 0, CRAM/VSRAM remain empty, and VRAM still contains only the one initialization word seen in the title captures.

The generated ROM translation contains writes to the 315-5296 CNT register at `0xe4001d`; the decoded values in the game code are `0x00` and `0x02`. CNT2 is bit 2, so these values leave the MAME `set_vdp_enable` output low. MAME's `segas18_state::screen_update` inserts VDP pixels only when this output is high. This confirms that Shadow Dancer's observed gameplay image is composed by the board tilemap/sprite path and that the onboard 315-5313 is not an active visual layer in these states. Do not force-enable the VDP or inject display data to make an artificial VDP frame.

The game frame objective is demonstrated through actual gameplay output. MAME's `segas18_state::screen_update` reads pixels generated by the VDP and inserts them between the board's tilemap passes according to `vdp_mixing`; it does not pass a completed drawing to another graphics ASIC. The VDP output is a separate pixel layer consumed by the System 18 compositor. The recomp follows those insertion points for its current Mode 5 plane/window/sprite output. Re-running Shadow Dancer with the VDP disabled produces byte-identical output to the prior capture with the same input schedule.

## Ninjutsu/VDP signal probe — 2026-10-30

Added a transition history for the board's VDP-enable CNT output and VDP-mixing latch to `state.txt`. Also fixed the System 18 I/O read counters so scripted input probes can confirm that the guest sampled controls. A run with player-1 button 3 held over vblanks 2100–2130 recorded 30 pressed P1 reads, but at vblank 2130 the VDP enable remained low, mixing remained zero, the transition history was empty, and VDP register 1 still had display-enable clear (`0x05`). This confirms the input reached the game, but the captured image does not independently prove the ninjutsu animation was triggered.

## Moonwalker VDP confirmation — 2026-10-30

Ran the local MAME 0.289 `mwalkd` decrypted bootleg for 15 emulated seconds with a Lua frame callback reading the mapped 315-5296 CNT register at `0xe4001d`. It alternated between `0x00` and `0x06`; the latter sets CNT2 (bit 2), which `segas18.cpp` wires to `segas18_state::set_vdp_enable`. MAME's `screen_update` comments specifically identify Moonwalker VDP mixing at layers 2/3. This is direct runtime evidence that Moonwalker uses the VDP in attract mode; it is a better live validation target than Shadow Dancer.

The probe above used MAME itself. The active VDP renderer has since been exercised in MultiArcadeRecomp with Moonwalker; see the 2026-11-11 capture below. Pixel-level parity with MAME remains unverified.

## Moonwalker active VDP runtime capture — 2026-11-11

After persisting the observed dynamic code roots (`0x1580c`, `0x1f7a4`, `0x1cbdc`) in `mwalkd.toml`, rebuilt the independent `multiarcaderecomp_system18_mwalkd.so` module and ran 500,000,000 guest cycles. The 68000 reached physical vblank 2,861 and the I8751 executed 21,736,945 instructions. The runner captured gameplay with `vdp_enabled=1` and `vdp_mixing=0x04`; the corresponding write occurred at cycle 477,051,564. The PPM is `/tmp/mwalkd-aot4.ppm`, with raw VDP and board state in `/tmp/mwalkd-aot4-state`.

At the time of this capture, this confirmed that the System 18 runtime reached gameplay with the active VDP mix latch selected; it did not provide pixel parity against MAME. The same intermediate build still logged zero YM/FM/PCM peaks. Later gameplay fixes corrected the image and sound issues reported by the user; see the current-status note at the top of this file. The timing and pixel comparisons described here remain useful historical evidence, not a statement about current audio behavior.

## Shadow Dancer mapper correction — 2026-10-30

The physical ROM board is 171-5873B, while MAME selects its special `ROM_BOARD_171_SHADOW` mapper behavior. `segas18_state::memory_mapper` leaves region 2 unused and maps the VDP's 16-byte local register window on region 1. The board documentation places that region at `0xC00000–0xDFFFFF`. A local MAME Lua probe read a changing VDP status value at `0xC00004`; `0xFF0004` did not show the same VDP behavior. MultiArcadeRecomp's fixed mapper setup was incorrectly putting region 1 at `0xFF0000`, so it now sets the region-1 size/base to mirror the VDP across the 2 MiB C/D range and leaves region 2 unmapped. Region 3 still mirrors work RAM into the wrapped stack area.

After this correction, the 400-million-cycle Shadow Dancer run still reached 2400 vblanks and its gameplay PPM matched the prior capture byte-for-byte. This validates that the corrected VDP mapping did not disturb the existing gameplay path; Shadow Dancer still keeps CNT2 low in the captured states, so it is not proof of active VDP pixels.

## Mapper and VDP mode-register validation — 2026-10-30

The 171-SHADOW mapping is represented as region 1 at `0xC00000–0xDFFFFF`, with region 2 left unused; the physical ROM board label is 171-5873B, while MAME's internal board selector is `ROM_BOARD_171_SHADOW`. `multiarcaderecomp map shdancer` reports the local 16-byte VDP window at `0xC00000–0xC0000F` and mirror mask `0x1ffff0`. A local MAME Lua bus read found VDP status behavior at `0xC00004`, not at the previously assumed `0xFF0004`.

Comparison with MAME's `vdp_set_register` and mode-register definitions caught a second bug: register 0 bit 2 is special-palette control, while register 1 bit 2 selects Genesis versus SMS/Mode 4. The scanline renderer now requires the Genesis selection bit, register 1 DISP, and register 0 display-disable clear. A temporary bus-programmed device probe produced `pixel0=33 opaque=1 sprite_priority=1 pixel1_opaque=0`; it also confirmed the display-disable and non-Genesis mode each blank the line. The probe is a device-level check, not a game capture.

The H32/H40 renderer now follows register 12's horizontal-resolution select for the 256/320-pixel active area and sprite-table base mask. Window addressing uses two 8x8 names per 16-pixel column with the normal 8-pixel vertical row stride. The extended device probe passed (`window_8px_row_stride=1`, `h32_clips_at_256=1`) alongside Genesis/SMS mode, display blanking, transparency and sprite-priority checks. These are programmed device-state checks, not active game output.

The tree rebuilt successfully, and the 400,000,002-cycle Shadow Dancer capture at vblank 2400 remained byte-identical to the previous real gameplay frame (`cmp` exit 0). The rendered PNG for this real MultiArcadeRecomp gameplay frame is `/tmp/shdancer_system18_regression.png`. This confirms the mapper/mode-gate corrections preserve the observed game output; Shadow Dancer's current captures still leave CNT2 low and the VDP display disabled.

## Initial Mode 5 Window and sprite path — 2026-10-30

Added the Window plane with its register-selected horizontal/vertical region and 16-pixel table addressing, plus linked-list sprite traversal, transparency, flips, palette selection, and basic low/high-priority composition. These outputs flow through the System 18 `vdp_mixing` hook, not through an artificial frame or forced VDP enable. The tree builds successfully with `cmake --build build/release -j2`.

Regression: the same 400,000,002-cycle Shadow Dancer gameplay run with CNT2 low produced a PPM byte-identical to the earlier VDP-off capture (`cmp` exit 0). This confirms that the inactive path leaves the current Shadow Dancer image unchanged; it does not validate active VDP rendering. At the time of this implementation, Moonwalker had no MultiArcadeRecomp profile/ROM translation or board wiring. Those have since been added; the active-run result and remaining parity work are documented below. This implementation should be treated as an initial renderer, not a finished System 18 video pipeline.

## Alternate timing and H/V counters — 2026-10-30

MAME's `segas18.cpp` configures 262 total lines, 224 visible lines, `set_alt_timing(1)`, and a board-verified screen refresh of 57.23 Hz. The runner now schedules both the 68000 vblank and VDP scanline edges at that rational refresh (`5723/100`), leaving System 16 refresh behavior unchanged. The VDP bus receives the live 68000 cycle count; reads/writes update beam line and H position before the device operation. H position is mapped to MAME's 0–480 range, with HBLANK status active above 400 and cleared above 460.

`read_hv_counter()` now returns both bytes: NTSC 224-line V count (`00–EA`, then `E5–FF`), 240-line count (`00–FF`, then `00–05`), horizontal wrap after `F7` by subtracting `0x49`, and the V increment when H exceeds 460. A temporary probe passed the 224/240 V wrap and H wrap cases (`hv_224=1 hv_240=1`), along with prior Mode 5/Window/sprite checks. It is a device-level test, not MAME-vs-game output parity.

After rebuilding, a real MultiArcadeRecomp capture at physical vblank 500 (`/tmp/shdancer_57hz_vblank500.png`, 100,000,007 guest cycles) showed the Shadow Dancer title. At the checkpoint, VDP register 1 remained `0x05`, CNT2 was low, and `vdp_mixing=0`; therefore the capture validates the game frame and the no-forced-VDP path, but does not show VDP-generated pixels. The frame counter reached 572 vblanks in 100 million cycles, consistent with 57.23 Hz rather than the former 60 Hz schedule.

## Current-build capture revalidation — 2026-10-30

Rebuilt the current tree and reran the actual `shdancer.zip` profile with the 57.23 Hz runner. At 100,000,007 guest cycles the runner produced a valid 320x224 title frame at physical vblank 500 (`/tmp/shdancer_goal_final.png`); the frame count was 572 and the CPU completed 8,995,108 translated instructions. The emitted VDP state still reports `vdp_enabled=0`, `vdp_mixing=0`, register 1=`0x05`, and zero VDP signal transitions. This verifies the Shadow Dancer board wiring, the game's real frame output, and the correct inactive VDP behavior together. It does not claim the title frame contains VDP pixels; active VDP raster output remains a separate System 18 validation task because the MAME driver evidence shows Shadow Dancer leaves that layer disabled in the observed states.

## Moonwalker `mwalkd` profile and 171-5874 wiring — 2026-10-30

Added the MAME `ROM_START(mwalkd)` set as a separate System 18 profile. The bootleg's decrypted 68000 ROMs, shared tile/sprite/audio ROMs, and `315-5437.ic4` firmware all pass size/CRC validation from `/home/luiz/git/mame/roms/mwalkd.zip`. The 68000 reset vectors are SP `0xffffff00`, PC `0x00000400`. Translation generated 178,309 instructions, 4,099 routines, and 25,998 dispatch entries into `build/release/generated/mwalkd_split`.

The 171-5874 mapper now follows `segas18_state::memory_mapper`: unlike Shadow Dancer's 171-SHADOW, it places the 315-5313 register window on mapper region 2 at `0xC00000–0xDFFFFF`; region 1 remains ROM bank 1 and is absent for this 512 KiB decrypted bootleg. The local `map mwalkd` output confirms that runtime mapping. MAME's `mwalkd` driver uses `system18_i8751`; the 315-5313 is active in MAME attract mode (CNT `0x00/0x06`, mixing levels 2/3), so this profile provides the real-game validation target Shadow Dancer does not.

The 4 KiB `315-5437.ic4` dump is firmware for the 8 MHz Intel I8751, not an additional executable ROM board. The MultiArcadeRecomp runner executes this firmware in the shared MCS-51 interpreter and connects its XDATA accesses to the 315-5313 mapper, including response IRQ and main-CPU reset wiring. The 30-vblank run recorded here was an early bounded CPU run, not gameplay validation; the longer gameplay/VDP result is documented in the 2026-11-11 section. As configured by MAME, the 68000 receives no IRQ4 on this I8751 machine. The separate `goldnaxe` capture is black and its 68000 stalls in a polling loop; determine whether the shared interrupt/runtime path is responsible before treating the I8751 integration as complete. The MAME System 18 driver only registers I8751 on the six Moonwalker sets (`mwalk`, `mwalku`, `mwalkj`, `mwalkd`, `mwalkud`, `mwalkjd`); the `mwalkjd` dump is flagged BAD_DUMP. Other hardware blocks in this MAME configuration are the same System 18 mapper, I/O ASIC, VDP, 315-5196/315-5197 video path, dual YM3438, Z80, and RF5C68 already represented in this project.

### Follow-up runtime findings — 2026-10-30

Running `mwalkd` with `--audio` for 50 million 68000 cycles reaches the budget without crashing, but the RF5C68 has no enabled channels and the generated sample peak is zero. At 1 billion cycles the earlier generated dispatcher stopped on an unmapped but valid ROM PC. The observed PCs `0x1580c`, `0x1f7a4`, and `0x1cbdc` are now persistent `runtime.m68k_entry_points` in the TOML profile and automatically seed translation. Runtime-observed ROM candidates are also saved and incorporated by the next rebuild. The updated translation/runtime now completes 1 billion cycles with zero 68000 Musashi fallback instructions on the exercised attract/gameplay path. The misses alone do not establish that the MCU caused them.

MAME's 315-5313 device also instantiates a Sega PSG (`SEGAPSG`) at VDP clock/15 and maps its writes at offsets `0x10–0x16`. The local VDP already exposes a `psg_write` callback, but System 18 does not connect that callback to audio yet. Gameplay video is now present; audio remains incomplete until the PSG route is added and the MCU/mapper handshake is compared in gameplay.
