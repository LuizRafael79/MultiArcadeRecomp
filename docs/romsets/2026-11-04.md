# ROM sets added on 2026-11-04

Inventory from `/home/luiz/git/mame/roms`. MAME source/configuration data is
from the local MAME 0.289 tree and `-listxml`; all eight new archives passed
`mame -rompath /home/luiz/git/mame/roms -verifyroms`. SFEX2PA and BLDYROR2 are
reported by MAME as the best available sets because their parent security ROM
`78081g503.ic655` has no good dump; it is not present in either supplied ZIP.

## Implementation progress (2026-11-04)

- The PSXRecomp compiler bridge, R3000A interpreter fallback and common PSX memory/dispatch layer now compile. BIOS tracing exposed MAME-map mismatches and they were fixed: writes to BIOS ROM are ignored, ZN-2 RAM_CONFIG size code 3 maps a 4 MiB address window (matching MAME), and mapped `nopr`/`noprw` addresses return the MAME values instead of raising bus errors. Unmapped instruction fetches now raise IBE instead of leaving the CPU at a stuck PC. The SIO path also gained the MAME-style ZNMCU HLE, DSR signaling, and CAT702/MCU wired-AND data. Current smoke status remains unsuccessful: SFEX2PA reaches RI at RAM PC `0x8000204c` (word `0x00000001`) after 200 million steps; BLDYROR2 reaches RI at PC `0x00000974` (word `0xfffffffe`) after about 2.3 million steps. Fresh BLDYROR2 tracing confirms the BIOS copies and runs code from RAM, writes `0xfffffffe` at `0x974`, and then executes the empty RAM area through that RI. This is evidence of an unresolved board/device or boot-state mismatch, not proof of an AOT coverage defect. Neither title is boot-validated.
- ESWAT/FD1094 runs on the existing System16B runtime and the user confirms it works perfectly. Protection is active: the profile loads the key ROM, opcode fetches use live FD1094 decryption, and Musashi fallback tracks CMPI.L state changes, IRQ mode and RTE. The cipher and opcode masks were ported from MAME and pass all four documented reset-vector checks. The current `eswat.zip` is the encrypted set; `eswatd` is an optional separate bootleg profile if its ZIP is supplied. Video is CPU software, without OpenGL. The ordinary Shinobi AOT path remains separate.
- A GPU parser defect found during this pass is fixed: GP0 `0x80` now consumes its four-word packet (command, source, destination, dimensions), avoiding an out-of-bounds packet access. The PSX interpreter also now writes Cause.CE only for Coprocessor Unusable exceptions, not for Reserved Instruction exceptions. Targeted GPU/interpreter tests pass. Rebuilt 200-million-instruction BLDYROR2 smoke keeps the same RI PC/opcode and instruction count; the CE field is now correctly zero. Neither title is boot-validated.
- Toki's `/tmp/toki_gameplay_soundprobe.png` frame exposed incorrect colors. Comparison with MAME `gfx_element::decode` showed the native pen significance is reversed locally: the first declared gfx plane contributes to the highest pen bit. Fixing that in `graphics_pixel` produces the more coherent `/tmp/toki_planeorder.png`; freeze video work at this checkpoint and continue to the next module. The runtime has MAME-checked ZIP loading, 68000 AOT, TAD bus, vblank sprite buffering, custom tiles/sprites, and SEI80BU opcode/data transforms. Renderer is CPU software, without OpenGL. The 68000 table has 113,053 discovered instructions; a 1200-frame gameplay run executed 17,057,594 AOT instructions and zero Musashi fallback. Static Z80 AOT is linked through the shared generator using separate SEI80BU-decoded opcode/data views: 1,939 fixed-ROM instructions were generated; the same run executed 7,140,695 AOT instructions and 528,519 interpreter fallbacks. A YM3812 OPL2 engine from local MAME's BSD-3 YMFM source is connected to the Z80 clock, 0x4008/9 registers and SDL sample stream. It generated 1,000,482 samples at 49,715 Hz, peak 18,980, with 2,271 key-ons. A shared four-voice OKIM6295 is mapped at sound address 0x6000 and clocked at the MAME board's 1 MHz input/PIN7-high rate; it received 9 writes and had no active voice at the end of the run. This confirms chip traffic and generated YM samples, not audible correctness or sample playback. MAME's `umask16(0x00ff)` sound-latch semantics and RST18 IRQ acknowledge match `src/mame/shared/seibusound.cpp`. The driver has no EEPROM/NVRAM. Both PPM capture and a shared SDL software window are now wired; no OpenGL.
- MAME-derived per-game ROM manifests now exist for all eight sets under `src/romsets/`. `tools/generators/romsets/import_requested_romsets.py` regenerates them from the local MAME source tree and `-listxml`, preserving both file metadata and source `ROM_START` load geometry. All eight loaded through the generic ZIP loader with CRC verification and correct ROM interleave in a 5.8-second batch. The loader now resolves alternate member names by ZIP directory CRC/size, which is needed for one renamed Tekken 3 sound ROM.
- Direct ZIP loaders and first board address decoders exist for Capcom ZN-2 (`sfex2pa`) and Raizing ZN-1 (`bldyror2`). Smoke runs against both supplied archives verify the ROM regions, BIOS mapping, program reads, bank selection and sound latches. The ZN-2 Z80 now runs through the shared interpreter, gets the 250 Hz IRQ and latch NMI, and clocks the shared QSound HLE with its sample ROM; audio samples are currently discarded and this path is not game-validated.
- A first `ZnMachineRuntime` now composes the BIOS, shared PSX CPU/memory devices, and either board decoder into one interpreter-capable machine object. It accepts a generated AOT entry table when supplied, but no ZN game table or CLI frontend is wired yet. PSX SIO0 is scaled to the configured board CPU clock (67.7376 MHz on ZN-1, 100 MHz on ZN-2); both board adapters wire CAT702 and the ZNMCU serial protocol, including DSR pulses and digital input response. The ZN AT28C16 window reads and writes the set's initial 2 KiB image; EEPROM busy timing, ID mode and persisted save data are not implemented. Serial traffic is observed, but CAT702 exchange correctness and game boot remain unvalidated.
- These are CPU and board foundations, **not playable systems**. Initial PSX DMA block/OTC/linked-list transfers, GP0/GP1 VRAM transfer/fill, flat polygons, affine textured triangles/quads (4/8/15bpp), Gouraud triangles, flat rectangles and textured rectangles, CPU-clock root counters, SIO0/CAT702/ZNMCU, basic AT28C16 storage and MDEC command/status plus channel 0/1 DMA paths are connected. The nine PSX/ZN test targets pass with assertions enabled. GPU/timers remain incomplete and synchronous; exact raster edge rules, textured/Gouraud edge cases, display timing and timer sync modes, SPU, EEPROM timing/persistence, ZN audio CPUs/chips and a run frontend still need implementation and boot validation. The MDEC algorithm is adapted from MAME's BSD-3-Clause source and is not yet validated against game streams.

## Machines and reusable hardware

| ZIP / set | MAME machine config and source | Main hardware | Sound / auxiliary hardware | MameRecomp status |
|---|---|---|---|---|
| `eswat.zip` / `eswat` | `system16b_fd1094_5797`, `sega/segas16b.cpp` | Sega System 16B; encrypted FD1094 68000 at 10 MHz, 315-5195 mapper, 315-5248 multiplier, 315-5250 compare timers; 320×224 | Z80 5 MHz, YM2151, uPD7759, discrete netlist | User confirms the game runs perfectly. FD1094 key loading, live opcode decryption and runtime state transitions are connected in the existing System16B runtime. Renderer is CPU software without OpenGL. `eswatd` is a separate optional unencrypted bootleg set, not supplied here. |
| `tmnt.zip` / `tmnt` | `tmnt`, `konami/tmnt.cpp` | Konami original TMNT board; 68000 8 MHz, K052109 tilemap, K051960 sprites; 320×224 | Z80 3.579545 MHz, YM2151, K007232, uPD7759 | First coherent 320×224 frame captured in `build/tmnt-current.png` after 1,200 frames; freeze video interpretation at this checkpoint. The runtime has separate original-board K052109/K051960 decoding, YM2151/K007232/uPD7759 paths and AOT+fallback CPU wiring. SDL window/audio path is present. Keyboard now follows shared convention: P1 arrows + keypad 1/2/3/start 1; P2 WASD + J/K/L/start 2. Audio output quality and gameplay input still need validation. |
| `tmnt2.zip` / `tmnt2` | `tmnt2`, `konami/tmnt2.cpp` | Konami TMNT2 board; 68000 16 MHz, K052109 tilemap, K053244/246 sprites, K053251 priority; 304×224 | Z80 8 MHz, YM2151, K053260, EEPROM, watchdog | Runtime and coherent character-select frame are present; freeze interpretation at this checkpoint. A fresh 3,600-frame run using coin/start/attack `--button-event`s produced `build/tmnt2-inputs-current.png` at the character-select screen. Shared RGB framebuffer feeds PPM output and an SDL software window at native board dimensions. Keyboard layout is now consistent across shared SDL board modules: coin 5–8; P1 arrows/keypad 1–3/start 1; P2 WASD/J-K-L/start 2; F2 service. The command-line event test confirms the runtime input path; physical keyboard input still needs a direct interactive check. Shares its board runtime with `ssriders`; per-game ROM loading, inputs, and visible area differ. |
| `ssriders.zip` / `ssriders` | `ssriders`, `konami/tmnt2.cpp` | Same Konami TMNT2 board family; 68000 16 MHz, K052109, K053244/246, K053251; 288×224 | Z80 8 MHz, YM2151, K053260, EEPROM, watchdog | Shared module, PPM framebuffer, and SDL software window at native board dimensions. Same keyboard controls as TMNT2. The 4,200-frame idle capture is a coherent title logo, but no gameplay frame. A run with longer coin/start inputs reached the known 68000 `BAD_PC` at `0x70004ed1`; interactive/gameplay controls and video still need debugging. Music is the only user-confirmed good subsystem. |
| `toki.zip` / `toki` | `toki`, `seibu/toki.cpp` | TAD board; 68000 10 MHz, custom foreground/background tilemaps and sprites; 256×224 | Z80 3.579545 MHz with Seibu encrypted sound interface, YM3812, OKI6295; no EEPROM/NVRAM | Gameplay capture at `/tmp/toki_planeorder.png` after matching MAME's graphics pen-bit significance; freeze video work and continue to the next module. A fresh 1,200-frame run using coin/start/attack events generated `build/toki-inputs-current.png`, a gameplay frame, and audio chip output statistics. Direct ZIP loading, 68000 and static Z80 AOT, fallback, Seibu sound map, YM3812 sample generation and OKI writes are connected. SDL keyboard input: P1 arrows + KP1/KP2/start 1, P2 WASD + J/K/start 2, coins 5/6, F2 service. Audio correctness is not validated by listening. PPM framebuffer and SDL software window are wired; no OpenGL. |
| `sfex2pa.zip` / `sfex2pa` | `coh3002c`, `sony/zn.cpp` | Capcom ZN-2; Sony CXD8661R / PlayStation-derived CPU at 100 MHz, PSX GPU/VRAM, DMA, MDEC and security devices; 640×480 | Z80 8 MHz, PSX SPU and QSound HLE | Share the planned PSX-derived CPU/SoC layer with ZN-1; add Capcom ZN-2 bank/security/I/O wiring, Z80 sound board and QSound. |
| `tekken3.zip` / `tekken3` | `coh700`, `namco/namcos12.cpp` | Namco System 12 with Sony CXD8661R at 100 MHz and PlayStation-derived graphics; 640×480 | H8/3002 at 16.9344 MHz for I/O, Namco C352 configured at 25.4016 MHz in MAME | Direct ZIP geometry, main board map, DMA ROM source and shared PSX-derived runtime are wired. A 480-frame run reaches RAM code and displays the JAMMA/JVS initialization screen. A shared C352 PCM core is now attached to the board profile; H8/3002 scheduling, C352 register writes/audio output, inputs/JVS and gameplay remain pending. |
| `bldyror2.zip` / `bldyror2` | `coh1002e`, `sony/zn.cpp` | Bloody Roar 2 on ZN-1; Sony CXD8530CQ / PlayStation-derived CPU at 67.7376 MHz, PSX GPU/VRAM, DMA, MDEC and security devices; 640×480 | PSX SPU, plus MC68000 at 12 MHz and Yamaha YMF271 at 16.9344 MHz | Local interpreter run reaches the BIOS legal warning at 180 frames (`/tmp/bldyror2-frames180.png`); freeze graphics interpretation at this first recognizable frame. MC68000/YMF271 and SDL PCM output are connected. The MAME ZN-1 input ports now accept replayed coin/start/service and P1/P2 events. At 360 frames PCM reached peak 8,022 with 267,679 generated samples, but the image remained at the warning and no voice slot was active at end; validate control acceptance, sound commands during gameplay, and audible fidelity. Complete PSX SPU/mixer and dynamic AOT remain pending. |

### ZN architecture clarified from MAME

The ZN boards use a PlayStation-derived main SoC, so the PSXRecomp R3000A
compiler and dynamic-code discovery are relevant to both
`sfex2pa` (ZN-2) and `bldyror2` (ZN-1). The project now has a common CPU state,
memory bus and PSXRecomp bridge, plus initial DMA/GPU devices and board maps,
but they do not yet form a runnable machine. The GTE is adapted from MAME's
BSD-3-Clause implementation and connected to COP2 transfers and commands.
Initial CPU-clock root counters are mapped and raise timer IRQs; sync-to-blank
and external-clock behavior remain approximations.
PSXRecomp's checked-in `gte.h` is only a placeholder, not a GTE implementation;
the CPU/device set still needs game-level validation and substantial remaining
devices (MDEC, SPU, security, audio CPUs/chips) before boot.

The two supplied ZN games have different sound boards, according to the MAME
machine configs in `src/mame/sony/zn.cpp`:

| Set | Main board additions | Sound path |
|---|---|---|
| `sfex2pa` / Capcom ZN-2 (`coh3002c`) | 4 MiB fixed + 4 MiB banked program window; bank register at `0x1fb00000`; country ROM at `0x1fb80000`; QSound latch at `0x1fb60000`; CAT702 security | Z80 at 8 MHz, 250 Hz periodic IRQ, shared latch/NMI, banked Z80 ROM, QSound registers at `0xd000-0xd003` and response at `0xd007`; PSX SPU remains part of the base SoC |
| `bldyror2` / Raizing ZN-1 (`coh1002e`) | four 8 MiB ROM banks across `0x1f000000-0x1f7fffff`, selected through security/bank control; sound latch and IRQ at `0x1fb00000/4`; CAT702 security | MC68000 at 12 MHz with 512 KiB ROM and 512 KiB RAM, YMF271 at 16.9344 MHz, latch-driven IRQ; PSX SPU also remains part of the base SoC |

So the reusable ZN work is the PSX-derived CPU/SoC foundation, not one
identical full board runtime. SFEX2PA needs QSound plus its Z80 path; Bloody
Roar 2 needs YMF271 plus its 68000 path. The project already has a QSound HLE
used by CPS1/CPS2 and reusable Z80/68000 backends. I found no YMF271 or PSX SPU
implementation in the current MameRecomp runtime, so those are chip/runtime
work still to do. System 12's C352 is separate and does not belong to ZN.

## CPU-first implementation order

Existing reusable CPU paths are the shared 68000 and Z80 backends. These games
do not need a new generic 68000/Z80 core; FD1094 and Seibu encrypted-Z80
behavior are separate bus/decryption layers. The missing CPU families are the
PlayStation-derived R3000A and H8/3002.

1. **Integrate the PSXRecomp R3000A compiler and dynamic-code pipeline** for
   the PlayStation-derived R3000A/CXD85xx CPU; bridge its generated-code ABI to
   MameRecomp and compare behavior against MAME. Keep the in-tree prototype
   only where it supplies useful tests or ABI glue.
   This CPU appears in ZN-1 (`bldyror2`), ZN-2 (`sfex2pa`) and System 12
   (`tekken3`).
2. **AOT compiler for H8/3002**, used as the System 12 I/O CPU in `tekken3`.
3. **Interpreters/fallbacks for those CPU families**, ported from MAME and
   kept behind the same CPU/bus interfaces as AOT.
4. **TMNT original (`tmnt`), TMNT2 family (`tmnt2`, `ssriders`), and Toki** —
   now have coherent first-frame checkpoints; freeze their graphics. Remaining
   validation is limited to audio/gameplay and the Sunset Riders long-run
   `BAD_PC`, without blocking work on other boards.
5. **Capcom ZN-2 (`sfex2pa`)** — current next board. Resolve the BIOS/SIO data
   mismatch against MAME, then attach its profile/AOT and continue the board
   runtime. Do not infer a compiler defect from the current interpreter-only
   boot failure.
6. **Raizing ZN-1 (`bldyror2`)** — continue after ZN-2: resolve the RAM boot
   sentinel mismatch, then complete its separate 68000/YMF271 audio path.
7. **Namco System 12 (`tekken3`)** — reuse the verified PSX-derived CPU/SoC
   components where applicable; add H8/3002 I/O, C352 and board-specific video.
8. **ESWAT** — complete and user-confirmed working in System16B with FD1094
   active; no further implementation is pending for this set.

## Machine dependency roots to follow in MAME

- Konami TMNT: `src/mame/konami/tmnt.cpp` plus devices `k052109`, `k051960`,
  `k007232`, `upd7759`, `ymopm` and Z80/68000 cores.
- Konami TMNT2/Sunset Riders: `src/mame/konami/tmnt2.cpp` plus devices
  `k052109`, `k053246_k053244` (K053244/246), `k053251`, `k053260`, EEPROM,
  YM2151 and Z80/68000 cores.
- Toki: `src/mame/seibu/toki.cpp`, Seibu sound devices and the custom video
  callbacks in that driver.
- ESWAT: `src/mame/sega/segas16b.cpp`, Sega System16B video/sound devices,
  FD1094 and the 315-5195 mapper.
- SFEX2PA: `src/mame/sony/zn.cpp` plus PSX CPU/GPU/IRQ/DMA/MDEC/RCNT/SIO/SPU,
  QSound HLE and ZN security devices.
- Bloody Roar 2: `src/mame/sony/zn.cpp` plus the ZN-1 PSX CPU/GPU/IRQ/DMA/MDEC/
  RCNT/SIO/SPU and security devices; the game adds its own 68000/YMF271 sound
  path.
- Tekken 3: `src/mame/namco/namcos12.cpp` plus PSX CPU/GPU/DMA devices, H8 I/O,
  Namco C352 and System 12 video/ROM handling.

The MAME driver configuration and device callback graph must be followed for
each board before implementing its adapter. A ROM manifest is not considered
game support: support means the shared board runtime is connected, the game
has its own ROM/profile recipe, and the set boots through the common launcher.

## Implementation checkpoint

The first shared runtime slice for TMNT2 and Sunset Riders is in
`src/konami/`: direct ZIP loading with MAME CRC/size checks and ROM lane
geometry, the common 68000 address decoder, a Musashi 68000 interpreter
adapter, and the project's shared Z80 interpreter connected to the sound ROM
and RAM bus. The main-board sound control write now follows MAME's held Z80
IM1 IRQ behavior; it is not modeled as a sound command latch. Targeted
board/CPU/ROM tests pass, and the ROM test validated both local archives.
The runtime now decodes K052109 8x8 graphics and composites its three indexed
tile layers with MAME-derived scroll registers, layer priorities, palette
bases, visible areas and global flip. The K053244/245 path now models the
scattered CPU sprite RAM, device RAM/buffer latch, basic 16x16 graphics decode,
zoom, ordering, flips, transparency and priority masks. K053251 register and
palette-index behavior is implemented. K053260 now models its main/Z80
communication ports, four voice registers, PCM playback, KADPCM decode,
loop/reverse, pitch, volume and stereo pan. YM2151 is now connected through
the shared ymfm engine, and both chips are advanced from Z80 cycle boundaries
into one interleaved stereo sample queue. The YM2151 timer/busy/IRQ callbacks
are wired to the board's audio interrupt line; the K053260's SH1 NMI and
the four-cycle `0xfc00` acknowledge mask are also modeled. YM2151 reads now
select the mapped register offset, so `0xf801` returns status (including busy)
instead of reading the data port at `0xf800`. This moved the Z80 past its
initial YM busy-poll loop, from PC `0x1186` to `0x4d02` after 3600 frames. The
main CPU frame
budget now follows the 6 MHz 384x264 screen timing (270336 68000 clocks), and
IRQ4 is asserted at line 240 before the remainder of that frame executes, as
in MAME's K052109 vblank callback. The audio queue is now sent to the SDL3
output from the runner with `--audio`; device startup has been exercised, but
audible output has not been validated. Raster-accurate register latching
remains. Sprite mirroring,
shadows and palette dimming are implemented in the indexed-frame conversion.
The TMNT2 053990 protection command now follows MAME's 0x8200 sprite-list
transform, including source/modifier reads from ROM, work RAM and sprite RAM,
zoom adjustment and writes to the scattered K053245 buffer. The tile/sprite
compositor, protection transform, PCM/ADPCM, clock conversion, vblank IRQ and
NMI acknowledge tests pass. The TMNT2 renderer now produces a coherent
character-select frame in a 3600-frame run; the renderer is frozen at that
checkpoint. Sunset Riders has a separately generated 68000/Z80 AOT module and
uses the same board runtime. Its K052109 character-bank registers now extract
the byte from the active 68000 lane, fixing the all-zero bank values caused by
reading the wrong half of the write value. The YM2151 read at `0xf801` now
returns status instead of the data port, advancing the Z80 past its initial
busy-poll loop. Its Z80 NMI is connected to K053260 SH1, not TIM2, matching
MAME; the `0xfc00` handler implements the acknowledge and four-cycle inhibit.
A 120-frame capture shows the ROM/RAM checks passing, including `EEPROM 153
OK`; the ZIP's `ssriders_eac.nv` is 128 bytes with the expected MAME CRC. The
shared ER5911 handler implements LOCK, UNLOCK, ERASE, ERASE ALL, WRITE, WRITE
ALL and READ decoding. An input event path now maps coin/start to the Konami
ports. At 4200 frames, capture
`/tmp/ssriders_4200_bankfix.png` shows a legible Sunset Riders title logo and
Konami mark; freeze video interpretation at this first coherent checkpoint.
Runs of 5000 and 7200 frames without inputs fail with 68000 `BAD_PC` at odd PC
`0x70004ed1` (dispatch status 2); this later CPU failure and audio validation
remain open, but do not block moving to the next board. Current runs with
input events confirm TMNT2 reaches character select and Toki reaches gameplay.
The keyboard layout was standardized across the shared SDL presenter and TMNT
presenter: P1 uses arrows/keypad and P2 uses WASD/J/K/L. The next unvalidated
runtime is Capcom ZN-2 (`sfex2pa`).
The 68000 unit test also accounts for Musashi's reset-cycle budget before
asserting the first instruction, matching the API's behavior.

For repeatable ZN boot diagnostics, `build/zn_boot_smoke <set> <zip> <budget>`
now runs the BIOS with the shared interpreter and prints exception PCs,
opcodes, CP0 state and AOT/fallback counts. New interpreter-only runs
confirmed the existing blockers: `bldyror2` reaches RAM PC `0x974` with
opcode `0xfffffffe` and repeats RI exceptions; `sfex2pa` reaches an unaligned
return target `0x2fd0547e` and raises ADEL. A bounded 5M-step trace shows the
bad target is written byte-by-byte by BIOS code at `0xbfc0acd4` into the saved
RA slot before the exception; it is not introduced by AOT dispatch. A direct
MAME GDB-stub comparison at that BIOS loop shows matching register state and
serial data at the first sampled bytes of both copy blocks (`s2=0x30` and
`s2=0x10`, including the `0x7e` byte). This disproves the initial hypothesis
that those sampled SIO response bytes differ. The runtime then repeats RAM PC
`0x1000` with A0=`0x974`, growing SP by `0x18` each pass before the bad return.
MAME reached `0x1000` in another state (A0=1, SP=`0xa000cbe8`, RA=`0x10c8`)
and did not reach the local condition during a 9-second emulated run. Next,
locate the first control-flow divergence after the SIO copy and check stack
state/key-ROM availability before changing SIO or CPU semantics. The new
optional `MAMERECOMP_ZN_TRACE_POINTER_WRITES=1` plus
`MAMERECOMP_ZN_WATCH_WRITE_ADDRESS=<address>` traces writes across RAM aliases
and DMA to a watched word. These runs had no generated AOT entries and do not
validate an integrated game. The optional
`MAMERECOMP_ZN_TRACE_BOOT=1` flag emits a small instruction/register trace for
the initial low-RAM path.

## MiSTer FPGA references

The user-provided checkouts in `/home/luiz/git` are the primary local
references:

- [XelaNotPu/ZN1_MiSTer](https://github.com/XelaNotPu/ZN1_MiSTer), commit
  `69a30d8`: non-Capcom ZN-1 board variants (Tecmo, Taito/FX-1, Visco, Atlus,
  Raizing). Useful for comparing per-licensee boot ROM, CAT702, ROM banking,
  NVRAM/FRAM and board I/O behavior.
- [XelaNotPu/ZN1-Capcom_MiSTer](https://github.com/XelaNotPu/ZN1-Capcom_MiSTer),
  local checkout `/home/luiz/git/ZN1-Capcom_MiSTer`, commit `42afa7b`: Capcom
  ZN-1 and QSound wiring; source includes a
  PlayStation-derived CPU/GPU/GTE foundation.
- [XelaNotPu/ZN2-Capcom_MiSTer](https://github.com/XelaNotPu/ZN2-Capcom_MiSTer),
  local checkout `/home/luiz/git/ZN2-Capcom_MiSTer`, commit `d16528f`: Capcom
  ZN-2 (`coh3002c`), CAT702, banked ROM, EEPROM/NVRAM,
  QSound and PSX-derived CPU/GPU/GTE integration.

An additional non-Capcom ZN-1 reference is in `/tmp/zn1-mister-reference`,
commit `69a30d8`; it covers Tecmo, Taito/FX-1, Visco, Atlus and Raizing board
variants.

These FPGA projects are valuable independent behavioral references and may
provide concrete test cases for the ZN board layer. Their HDL is not directly
an AOT compiler or software interpreter. The repositories describe a GPLv3-or-
later combined source tree (including PSX_MiSTer and other components); consult
per-file notices before copying anything. For now they are research checkouts,
not source dependencies of MameRecomp. Prioritize using them to cross-check the
MAME-derived board maps and to derive observable tests for the R3000A/GTE and
board peripherals.

**SFEX2PA MAME cache/fetch comparison (2026-11-05):** the previous `0x2fd0547e` ADEL came from treating BIOS cache-isolation stores as physical RAM writes and routing opcode fetch through the I-cache. MAME's PSX CPU uses `m_instruction.read_dword()` for opcode fetch; CP0.SR IsC redirects data reads/writes to `cache_readword/cache_writeword` according to BIU. MameRecomp now keeps that split and models BIU tag/lock/invalidate plus its I-cache and D-cache storage for isolated data accesses. At the BIOS `SYSCALL` PC `0x8001afc0`, MAME GDB and local interpreter report the same complete GPR state and HI/LO. The local 50M-instruction smoke no longer hits the old ADEL and continues in RAM, but this remains interpreter progress, not a game boot or video validation. The CPS2-style ROM/AOT path is still pending for ZN-2.

**SFEX2PA GP1/vblank comparison (2026-11-05):** MAME debugger confirms the game’s 512-wide GP1 mode (`GP1(08,2)`) and recurring `GP1(03,1)`/`GP1(03,0)` display blank/enable commands. MAME `zn2_spu_hack_r()` toggles bit 3 at `0x1fa60000`; the game polls it using `lw`, so the ZN-2 board handler now preserves the toggle on 32-bit reads. MAME GP1(04) also sets GPUSTAT bit 25 when DMA mode is 1 or 2; this is now mirrored. MAME wires GPU vblank to PSX IRQ input 0; the local GPU timing now raises that IRQ. The local launcher builds and runs through 500M cycles, but remains at a 512x240 black/disabled frame and has not yet reached the recurring GP1 display-enable sequence seen in MAME. At `0x80252354`, the local game repeatedly tests BIOS event 0 (`v0=0`); the first MAME and local event-structure snapshots match, but later event progression still differs or remains pending. The screenshot/frame is therefore still unvalidated; continue comparing post-vblank event delivery, GPU command path, and scanout window before moving on.

**ZN scanout e primeiro quadro local de BLDYROR2 (2026-11-05):** a comparação com `src/devices/video/psx.cpp` do MAME confirmou o vblank padrão de 2500 µs. O launcher agora agenda a apresentação após esse intervalo e `--frame-out` grava o último quadro apresentado. Em SFEX2PA, `--frames=480` gera 480 atualizações e a captura `/tmp/sfex2pa-frames480.png` mostra o aviso amarelo de boot; a tela preta anterior era amostragem durante a troca da página de exibição. Para BLDYROR2, `zn_boot_smoke` já ultrapassa o antigo endereço de sentinela `0x974` e um run de 50M instruções alcança PC de código do jogo em RAM; `--frames=180` produz `/tmp/bldyror2-frames180.png`, com texto de aviso reconhecível. A captura correspondente do MAME é `/tmp/bldyror2-mame-180.png`; a versão local ainda tem diferença visível de cor e de pixels de borda, então congelar essa interpretação como checkpoint de primeiro quadro, não como fidelidade final. Build do launcher passou. BLDYROR2 continua sem gameplay validado e sem áudio 68000/YMF271 conectado. Próximo na sequência: Namco System 12/Tekken 3; consultar a configuração `coh700` e o mapa de `namcos12.cpp`, com H8/3002 e C352 além do runtime PSX compartilhável.

**SFEX2PA clock do SoC alinhado ao MAME e checkpoint gráfico congelado (2026-11-05):** `zn.cpp` configura CXD8661R a 100 MHz e `psx.cpp::execute_run()` consome um tick de execução por instrução (`m_icount--`); nosso interpreter também avança um tick por instrução. O runtime local estava usando 25 MHz, o que deixava o vblank quatro vezes mais lento em relação à execução e produzia capturas em fases erradas. Alinhei os clocks base ZN-1/ZN-2 aos valores do `machine_config` MAME (67.7376/100 MHz), removendo a hipótese de divisão por quatro. Build passou. Aos 300 quadros/500M instruções, SFEX2PA produz o aviso de boot coerente em `/tmp/sfex2pa-clock-300.png`, `display=enabled`, 512×240; congele a interpretação gráfica nesse checkpoint. Ainda não é gameplay validado: AOT dinâmico, SPU/mixer e áudio permanecem pendentes. GP1 escolhe 512×240 no jogo; essa proporção wide vem do modo de vídeo configurado pela máquina, não de `display=disabled`. Próximo módulo: continuar BLDYROR2, começando pelo MC68000 de áudio e YMF271 conforme `raizing_zn_state::coh1002e`/`psarc_sound_map` no MAME. Todos os módulos novos observados continuam no presenter SDL software, sem OpenGL; o TODO geral de OpenGL/Vulkan/reuso continua ativo.

**BLDYROR2 MC68000 de áudio ligado ao mapa MAME (2026-11-05):** criei um Musashi 68000 board-independent com contexto guardado por CPU, callbacks de byte-bus, autovector/IRQ-ack e contador real por instruction hook (`core/cpus/m68000/musashi_runtime.*`). Ele está isolado em `libmamerecomp_musashi_68000.so`, evitando colisão com as instâncias Musashi do host, e serve como runtime 68000 compartilhável. `Zn1SoundCpu` já consome a ROM intercalada, RAM 512 KiB, latch em `0x180009`, IRQ nível 2 e portas YMF271 `0x100000–0x10001f`, seguindo `psarc_sound_map` do MAME. Relógio relativo é agendado a 12 MHz contra 67.7376 MHz do CXD8530CQ. Build `mamerecomp` passou; execução de 180 quadros alcançou 3.578.747 instruções MC68000 fallback e 482 escritas YMF271 (`/tmp/bldyror2-zn1-audio-cpu-180.ppm`), mantendo o checkpoint de aviso. Ainda não há geração de áudio: falta portar/adaptar o núcleo BSD YMF271 do MAME com leitura da ROM externa de 4 MiB, e encaminhar a saída estéreo ao SDL; SPU do SoC também permanece pendente. Não chamar esse áudio de funcional ainda.

**SFEX2PA checkpoint gráfico revalidado e congelado (2026-11-05):** executei o binário atual por 600 quadros/1.000.000.000 ciclos com `sfex2pa.zip`. O GPU termina `display=enabled`, modo 512×240, e o frame `/tmp/sfex2pa-investigate-600.png` mostra o logo Virtual Audio/QSound e Capcom. A captura preta antiga não se reproduziu; `display=disabled` é o estado transitório do GP1 reset antes do comando de enable. A tela larga vem do modo 512×240 selecionado por GP1(08), conforme `updatevisiblearea()` do MAME. Congelar a interpretação gráfica neste checkpoint e avançar sem ajustes no renderer; isso não valida gameplay, AOT dinâmico ou áudio/SPU.

**BLDYROR2 saída YMF271 integrada — verificação pendente (2026-11-05):** o núcleo BSD do MAME está agora em `core/chips/ymf271.*`, com callback da ROM sample, timers/slots, geração estéreo a 44,1 kHz e conexão ao SDL para execução com janela. A build completa do `mamerecomp` passou após a integração. Run de 180 quadros em curso para confirmar nível de áudio não-zero; não declarar o YMF271 funcional até inspecionar os contadores/escutar a saída. O SPU base do SoC ainda não está conectado ao mixer host.

**BLDYROR2 IRQ do 68000 de áudio corrigido conforme Musashi/MAME (2026-11-05):** trace de 180 quadros encontrou a causa da repetição de comando: dois writes de IRQ2 do MIPS resultavam em 128 acknowledges e 128 leituras repetidas do latch `0x7e`, pois o adapter limpava o IRQ da placa mas deixava a linha externa ativa no Musashi. O README do Musashi exige `m68k_set_irq(0)` quando o dispositivo reconhece o IRQ; o callback genérico agora limpa a linha após o acknowledge, e a placa reapresenta IRQ se ainda houver evento pendente. Rebuild completo passou. No mesmo checkpoint, os contadores caíram para dois writes IRQ2, dois acknowledges e duas leituras de latch; o 68000 avançou de 2.127.038 para 3.536.188 instruções e o YMF271 recebeu 1.190 writes. Foram produzidas 133.752 amostras estéreo/44,1 kHz, mas `peak=0` e `active_slots=0`: ainda não há voz comprovada durante o aviso de boot. Trace opcional disponível em `MAMERECOMP_ZN_TRACE_SOUND_COMMANDS=1` e `MAMERECOMP_ZN_TRACE_YMF271=1`. A saída YMF/SDL está ligada, porém precisa de um estado de jogo que efetivamente acione voz; o SPU do SoC permanece pendente.

**SFEX2PA scanout reconfirmado e caminho QSound/SDL ligado (2026-11-05):** o run local de 600 frames/1.000.000.000 instruções termina com `display=enabled`, modo 512×240; o PPM atual tem pixels idênticos ao checkpoint conhecido do logo QSound/Capcom. Em 60 frames, o jogo ainda envia GP1 reset/blank explicitamente, então esse frame preto é transitório. O código MAME usa os mesmos bits de reset/blank e dimensões GP1 configuradas, sem evidência de regressão no presenter. O Z80 do QSound agora segue a razão de clock MAME 8 MHz/100 MHz do CXD8661R (antes o denominador local era 25 MHz), gera PCM estéreo a 24.038 Hz, acumula amostras e pode entregá-las ao SDL com `--audio` ou janela. Build `mamerecomp` passou; execução dummy SDL de 2 frames abriu/consumiu o fluxo e gerou 801 amostras, pico 0. A execução headless de 120 frames gerou 48.076 amostras e também pico 0, logo o caminho está conectado, mas ainda não há som audível validado. As entradas de P1/P2, SERVICE/SYSTEM, P3/P4 e kick harness foram ligadas no mapa MAME, com teclado ao vivo; replay headless de inputs ainda falta. Continua pendente o comando que ativa voz durante gameplay, o SPU do SoC e AOT dinâmico. Os módulos novos ZN continuam no presenter SDL de software, sem OpenGL; os TODOs gerais de OpenGL/Vulkan/reuso permanecem.

**BLDYROR2 entradas ZN-1 e PCM YMF271 não silencioso (2026-11-05):** o decoder ZN-1 agora expõe os ports `P1`, `P2`, `SERVICE`, `SYSTEM`, `P3` e `P4` como no mapa base de `zn.cpp`; o runner aplica teclas/eventos em nível baixo e respeita o perfil `bldyror2`, que associa os botões extras 4/5 aos ports `P3`/`SERVICE`. Build `mamerecomp` passou. Replay com coin no frame 200, start no 230 e ataque no 300 até o frame 360 terminou em PC `0x802d080c`, com 406.425.600 instruções R3000A interpretadas, 6.513.934 do MC68000 de áudio, 1.956 escritas YMF271 e 267.679 amostras a 44,1 kHz; pico PCM 8.022. A captura `build/bldyror2-input-audio-360.png` continua no aviso legal reconhecível; MAME também mostra o aviso aos 360 frames (`/tmp/bldyror2-mame-360.png`). Há PCM não silencioso no percurso, mas não validei escuta nem que coin/start tenham feito o jogo sair do aviso. Congelar o frame/checkpoint; SPU do SoC e AOT dinâmico seguem pendentes.

**Tekken 3 H8/3002 runtime (2026-11-05):** `mamerecomp_h8` compila os handlers MAME gerados (`h8_base_generated.inc`/`h8_advanced_generated.inc`) e helpers aritméticos BSD-3-Clause. O wrapper replica o loop `execute_run` do MAME, com estado parcial e carry de ciclos entre fatias; o H8 está conectado ao scheduler System 12 na razão de clocks MAME de 16,9344/67,7376 MHz. O mapa da sub-CPU inclui RAM interna `0xfffd10–0xffff0f`, registradores básicos das portas 4/6/7/8/9/A/B com DDRs/máscaras padrão do H83002 e SYSCR. Em 60 frames, `--audio` gerou 44.100 amostras estéreo C352 a 44,1 kHz; foram observadas 66 escritas de inicialização, zero vozes ativas e pico PCM 0. Em 240 frames o vídeo sai das barras e mostra texto de inicialização legível (“INITIALIZING / JAMMA VIDEO STANDARD / C-STEP 1-2”), display ativo; checkpoint salvo em `snap/tekken3/0000.png`. Congelar a interpretação gráfica nesse ponto e seguir com áudio/dispositivos sem alterar o decodificador. DMA, timers, SCI, ADC/INTC, JVS/RTC/settings e inputs continuam pendentes; não há áudio audível nem gameplay validados.

**PSX SPU PCM inicial (2026-11-05):** o core comum `core/cpus/psx/spu.*` decodifica blocos ADPCM de 16 bytes/28 amostras, aplica pitch com interpolação e volumes de voz/master, e gera PCM estéreo. As fases attack/decay/sustain/release usam constantes das tabelas de `src/devices/sound/spu_tables.cpp` do MAME; a forma do envelope ainda não foi comparada em execução. A amostragem avança pelo mesmo contador de ciclos do SoC; a fila PCM 44,1 kHz segue por um stream SDL dedicado para ZN-1/ZN-2/System 12, independente do QSound/YMF271/C352 de cada placa. `mamerecomp` compila e o smoke de SFEX2PA com SDL dummy terminou 2 frames com display habilitado e 801 amostras QSound. Esse smoke não demonstra key-on audível do SPU. Reverb, modulação/noise, timing de IRQ e fidelidade auditiva continuam pendentes. A primeira execução longa revelou que `Spu::advance` recebia o contador cumulativo de ciclos a cada instrução e reprocessava todo o histórico; o SPU agora avança somente pelo delta desde a chamada anterior. Com a correção, replay headless de 1.200 frames com SDL dummy e coin/start levou 5m10s, executou 2 bilhões de instruções e produziu 480.760 amostras QSound, pico 16.570. A captura pós-start ainda ficou quase preta, então permanece congelado apenas o checkpoint do logo. O presenter SDL software do projeto só apresenta o framebuffer do runtime; isso é separado do renderer OSD SDL software do MAME, que rasteriza primitivas MAME. Esse raster interno é distinto do backend SDL de janela, que nesta instalação reportou OpenGL; não atribuir o raster do jogo ao driver de apresentação.

**PSX GPU textured polygon scanline (2026-11-05):** `core/cpus/psx/gpu.cpp` agora percorre os edge chains esquerdo/direito do GP0 para polígonos texturizados de 3/4 vértices, interpolando posição, UV e canais Gouraud em 16.16 no eixo Y e por scanline em X. Quadriláteros são rasterizados como um único polígono, sem a divisão local anterior em dois triângulos. A implementação segue `psxgpu_device::FlatTexturedPolygon` de MAME `src/devices/video/psx.cpp`; o caminho OSD `drawsdl3soft.cpp` continua separado e não foi transplantado. `mamerecomp` compilou, `psx_gpu_test` passou e um probe GP0 temporário desenhou um quad raw com textura 4×4 conhecida; amostras no interior e na borda exclusiva coincidiram com os texels esperados. O smoke SFEX2PA de 2 frames terminou com display ativo e PPM 256×240, mas não chega ao logo/cena 3D; ainda precisa comparar uma captura de jogo contra MAME mantendo o checkpoint conhecido congelado.


**SFEX2PA transparência GPU comparada com MAME (2026-11-05):** a leitura de `psxgpu_device::FlatTexturedRectangle` e `TRANSPARENTPIXEL` revelou que o caminho semitransparente local não preservava o bit STP ao escrever o pixel resultante; alinhei-o ao MAME usando o blend RGB555 compartilhado, que preserva esse bit. `mamerecomp` compilou, `psx_gpu_test` passou e o replay coin frame 300/start frame 950 até 1200 terminou. O screenshot permaneceu quase preto, enquanto `/tmp/sfex2pa-input-mame-1200.png` mostra “Choose Your Fighter”; logo STP é uma correção de fidelidade independente, não a causa principal da divergência. Confirmei também no código MAME e com `mame sfex2pa -video soft -verbose`: as primitivas são rasterizadas por `software_renderer` em CPU antes da apresentação SDL; o SDL2 instalado seleciona OpenGL para apresentar a textura da janela. Isso não significa que o raster do jogo seja executado por LLVMpipe. SFEX2PA segue sem checkpoint gameplay validado; manter a investigação no estado GP0/VRAM e na execução da placa.
