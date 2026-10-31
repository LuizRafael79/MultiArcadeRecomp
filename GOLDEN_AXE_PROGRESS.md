# Golden Axe / System 16B progress

## Chosen set

`/home/luiz/git/mame/roms/goldnaxe.zip` is MAME's parent `goldnaxe` set (set 6, US), identified in `segas16b.cpp` as `system16b_i8751_5797` / `init_generic_5797`. It is the 8751 variant, not one of the FD1094 sibling sets (`goldnaxeu`, `goldnaxej`, etc.). Therefore the 68000 program does not need FD1094 decryption; the ROM set includes the 4 KiB 8751 firmware `317-0123a.c2`.

## Initial code checkpoint

- Added `goldnaxe` to `GameProfile` as revision B.
- Added MAME CRC-checked layout for the two interleaved 0x40000 main-program ROMs, three tile ROMs, six split/interleaved sprite ROMs, two Z80 sound ROM loads, and the 8751 firmware.
- Added a generic MCU region loader and sound CPU region loader. `inspect` now validates the main CPU, raw graphics, sound CPU, and MCU regions for profiles that declare them.
- Made graphics extraction select the tile decoder from the profile revision, so System 16B profiles use the 16B path.
- Confirmed that Golden Axe tile pixels use MAME's shared `gfx_8x8x3_planar` layout. The runtime now exposes a common 3bpp pixel decoder while keeping the System 16A and 16B tilemap attribute paths separate.
- Added 315-5197 decoding for normal/alternate tile and text words, plus an initial CPU compositor for normal pages, global/row/column scroll, alternate rows, priority marks and background/foreground/text order. Added a separate 16B OpenGL compute shader reusing only the context, atlas upload and output buffers. The scanline-261 page/scroll register latch is modeled; alternate tilemap composition remains unfinished.
- Added an initial 315-5196 model for descriptor termination/hide, bank selection, signed pitch, horizontal/vertical zoom, flip, pen transparency/terminator and scratch-RAM behavior. The ASIC interpreter rasterizes a sprite plane on CPU; Software mode mixes it with the tilemap on CPU, while OpenGL mode now runs a separate compute pass for priority, sprite palette selection and shadow composition. This keeps 16B mixing separate from 16A.
- Corrected the 315-5196 sprite ROM address to use the full 16-bit word offset inside each 0x20000-byte bank, matching MAME's 0x10000-word bank indexing; the earlier 0x7fff mask aliased the upper half of every bank.
- Kept 315-5196 vertical coordinates signed through screen flip and clipping, so sprites crossing the top/bottom edges retain the MAME coordinate transform instead of wrapping negative positions to unsigned values. Pitch and zoom accumulation still advances on clipped rows.
- Applied the System 16B sprite device's default local origin `(184, 0)` (and `(-184, 0)` when flipped) to visible-frame coordinates. The origin is subtracted from the normal-mode descriptor X; when flipped, subtracting the negative origin adds it. A later attempt to reverse this sign made the rotating title logo disappear; snapshot evidence (e.g. raw X `0x0147`) agrees with the original subtraction and MAME's padded sprite-bitmap mapping, so that attempt was reverted. Runtime/OpenGL confirmation is still pending.
- User confirmed the rotating logo and title screen had already been correct before the sign-reversal regression. Preserve the restored origin mapping as the known-good direction; do not tune this offset again without a like-for-like capture.
- User has now confirmed that all visible elements are in the correct positions and framed correctly. Treat the initial Golden Axe bootleg video path as validated; future visual changes should be compared against the current title/logo output.
- Fixed a CPU/OpenGL inconsistency in the 315-5197 path: the CPU renderer used the scanline-261 page/scroll snapshot, while the compute shader reread those six register words from live text RAM. The shader now receives the same four latched page/X/Y values, retaining live RAM reads for row/column tables.
- Rebuilt and ran `goldnaxeud` headless through physical vblank 720 with OpenGL plus `--video-compare-cpu`. The compute renderer reported 0 differing palette-index, priority, or shadow pixels at the title/logo frame; captured output includes the full rotating GOLDEN AXE logo. The OpenGL implementation ran on Mesa llvmpipe in this check, so GPU-driver/performance validation remains for the user's actual session.
- Captured CPU-rendered frames at vblanks 180, 360, 600, 660, 720, 780, 840 and 960 from the `goldnaxeud` runner. The post-revert vblank-720 capture shows the GOLDEN AXE sprite logo, Japanese emblem and SEGA logo inside the 320x224 frame with the MAME-consistent origin subtraction. User confirmed the title/logo itself had already looked correct before the sign-reversal regression; the new OpenGL/CPU compare now verifies this exact title frame. Vblank 960 is a different scene.
- Added a 315-5195 register/latch model and 171-5797 eight-window wiring decoder, connected to a revision-selected System 16B address space.
- Implemented the 5797 ROM-board arithmetic window using MAME's 315-5248 semantics (two signed operands, high/low product reads, byte-lane writes) and the 315-5250 compare registers (signed bounds/value, status/result, history bit, mirrored register offsets). Sound-command writes and timer/IRQ acknowledgement remain inert until the 16B sound/IRQ wiring is connected; no synthetic timer pulses are introduced.
- `inspect goldnaxe /home/luiz/git/mame/roms/goldnaxe.zip` passed CRC checks for all declared regions. Reset vectors are SP `0xffffff00`, PC `0x00000400`.
- `translate-full` succeeded: 481,339 instructions, 3,691 routines and 46,223 dispatch entries; generated C is about 117 MB. This confirms the shared 68000 frontend can discover/translate this set, but does not establish runtime correctness.
- `translate-full goldnaxeud ... --files 64` generated 514,774 instructions across 3,746 routines and 46,731 dispatch entries in 64 C translation units (about 125 MiB of generated source). This keeps each compile unit small; it does not put that code in the normal Shinobi executable.
- An earlier forced System 16A run was an invalid experiment: it stopped at PC `0x498` on a write to `0xfe0020` because it bypassed the System 16B mapper and graphics ASICs. That experiment has been removed from the runtime path and is not evidence about Golden Axe compatibility.
- `run-full` selects the System 16B bus, tile decoder, tilemap renderer and sprite pass from the profile revision. Golden Axe audio now connects a separate 16B Z80/YM2151/uPD7759 path; the user should validate it in live gameplay. The parent `goldnaxe` set still requires its 8751 firmware/interface.
- The `goldnaxeud` generated build compiles successfully, and `glslangValidator -S comp` accepts both 315-5197 tilemap and 315-5196 mixer shaders. The game has not yet been launched through this runner, so the ASIC models and final rendered image still require runtime validation.
- `--video-compare-cpu` now supports revision B and compares the complete indexed frame (tile priorities, sprites, and shadows) against the CPU reference using the same pre-render sprite RAM snapshot. This remains opt-in because the CPU reference adds rendering work each frame.

## Remaining board work

1. Validate the 315-5195/315-5197/315-5196 models by running `goldnaxeud` and inspecting video behavior. The B paths are selected separately from System 16A; the models are initial and not yet runtime-validated.
2. 8751 MCU firmware and board interface for the parent `goldnaxe` set, including the driver-specific vblank/IRQ handshake.
3. Compare uPD7759 DRQ/NMI/FIFO timing and ADPCM output against live Golden Axe playback and MAME. Shinobi's uPD7751/MCS-48 is a separate System 16A board path.
4. Connect the 315-5250 sound-command and interrupt/timer side effects when the board's 16B Z80/IRQ path is implemented; current MAME driver wiring does not attach the compare timer EXCK output for this board.

## References in the local MAME source

- `src/mame/sega/segas16b.cpp`: `ROM_START(goldnaxe)`, game declaration, 16B driver and `system16b_i8751_5797` configuration.
- `src/mame/sega/segaic16.cpp`: System 16B board/chip family and video behavior.
- `src/mame/sega/segas16b.cpp`: `system16b_map` uses the 315-5195 mapper; `system16b` configures SegaIC16 video, 315-5196 sprites, Z80, YM2151 and uPD7759.
