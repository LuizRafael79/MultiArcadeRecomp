# Mortal Kombat II T-Unit prototype

MK2 is a separate board module. It reuses the common T-Unit/TMS34010 runtime
from `src/mk/`, while `src/mk2/` owns the MK2-specific protection and DCS Audio
2K adapter. The DCS adapter connects the TMS host latches to the shared
ADSP-2105 CPU backend in `core/cpus/adsp2105/`; it does not use the MK1
Williams/MC6809 audio board.

The ROM pipeline verifies the MAME parent set's filenames, sizes, and CRC32s,
interleaves the TMS program and graphics regions, reconstructs the DCS sound
region, and emits per-ROMset TMS and ADSP AOT sources. The ADSP AOT currently
includes bootstrap loads, immediate DAG1/DAG2 writes, shifts, data-register
moves, ALU/MAC operations (including the ALU-to-AF move op used by MK2),
absolute and DAG memory transfers, and conditional branches/calls. AOT entries
verify the live program word before execution, so
unchanged instructions copied into writable ADSP RAM can run natively while
rewritten instructions safely fall back to the shared interpreter. The
firmware's unconditional wait loop also has a scheduler-bounded fast path.
Remaining instructions use the shared ADSP interpreter where implemented;
unsupported opcodes remain visible as categorized stops.

MK2's TMS AOT build includes extra ROM entries observed while following
indirect control flow: `0xffa5a150`, `0xffa5a190`, `0xffa5a200`, `0xffa5a2a0`,
`0xffa5a750`, `0xffa5a7f0`, `0xffa5aa30`, `0xffa5aad0`, `0xffa5a630`,
`0xffa5a6d0`, and `0xffa5ad30` (observed during the strict-AOT checkpoint
below).
The `CALLA` at `0xffa5a7f0`
targets `0xffa52fb0`, which is then discovered from that root. The reset-root-only CFG did not reach these ROM blocks even
though `MOVI.L B` (`0x09f6`), `EXGPC B6` (`0x0136`), `ADDI.L` (`0x0b21`), and
`CALLA` are supported by the decoder. The generator keeps these addresses as
default roots; additional per-run roots can still be supplied with
`--entry-bits`.

Latest ADSP AOT additions:

- Immediate-register load group `0x30-0x3f`, including opcode `0x340008` at
  boot-program PC `0x0026`.
- `DO UNTIL` loop setup group `0x14-0x17`, including opcode `0x14039e` at
  boot-program PC `0x0037`. It advances PC before saving the loop-body entry;
  general loop-end handling remains interpreter-assisted so loop conditions
  and stack updates retain the shared interpreter semantics, with the native
  store endpoint below handled explicitly.
- DAG2 program-space store with MAC function zero (`0x58-0x59`), including
  opcode `0x5800a2` at boot-program PC `0x0039`; data write, PX byte, and DAG
  post-modify are handled by the shared native backend.
- Conditional ALU-to-AF group `0x26-0x27`, including opcode `0x267a0f` at
  boot-program PC `0x0046`.
- Conditional MAC-to-MR group `0x20-0x21`, including opcode `0x20980f` at
  boot-program PC `0x006a`; `0x21600f` at `0x0072` is also supported at a
  `DO UNTIL` loop boundary.
- Mode-control opcode `0x0c3000` at boot-program PC `0x006d` now shares the
  interpreter's MSTAT update helper, including bank-switch side effects.
- Conditional return `0x0a000f` at boot-program PC `0x0125` now has an AOT
  entry and uses the interpreter's condition and PC/status-stack behavior.
- DAG address modify `0x090001` at boot-program PC `0x0093` now uses the same
  I/M-register selection and circular-address helper as the interpreter,
  including its `DO UNTIL` loop-end ordering.
- DAG2 program read `0x5000b1` at boot-program PC `0x00ae` now has a native
  helper and loop-end handling; it reads through DAG2, writes the selected
  core register, then preserves the loop-updated PC.
- DAG1 register store (`0x68-0x69`) can execute natively at a `DO UNTIL`
  loop boundary when MAC function is zero. Added after `0x680001` at
  boot-program PC `0x004e` was rejected at the end of the loop started by
  `0x1404ee` at `0x004d`; loop-PC and stack transitions match the shared
  interpreter order.
- NOP (`0x000000`) now runs natively both normally and at a loop boundary;
  counter-only loops retain their fast path, with other loop boundaries using
  the shared loop state transition.
- DAG1 core-register store (`0x68-0x69`) can now execute natively at a
  `DO UNTIL` loop boundary when MAC function is zero. Added after `0x680001` at
  ADSP boot PC `0x004e` was rejected at the end of the loop started by
  `0x1404ee` at `0x004d`; PC advance/loop-stack handling happens before the
  store, matching the interpreter's instruction order.
- `PIXBLT B,XY` (`0x0fa0` family), first observed at ROM PC `0xffa592b0`, is
  emitted as an AOT entry that calls the shared native pixel-transfer helper
  through the board's pixel and XY-conversion callbacks.

Keep this checkpoint updated when adding ADSP opcode families so a regression
can be bisected or reverted by opcode group.

MK2 uses MAME's DSW default `0xfc7d` (CMOS coinage, Powerup Test off). The
diagnostic runner persists the T-Unit's 16 KiB CMOS image in
`build/mk2/work/mk2.nvram` by default; pass `--nvram=PATH` to choose another
file. Its CMOS write semantics follow MAME, which currently accepts writes
without gating them on the separate CMOS-enable address. Unlike MK1, MK2 has
no `Skip Post Test` DIP, so the runner supports timed active-low input pulses:

```sh
build/multiarcaderecomp_mk2_trace build/mk2/work 100000000 \
  --press-continue-at=25000000 \
  --press-coin-at=62500000 --press-coin-at=68750000 \
  --press-start-at=81250000
```

The TMS execution-cycle clock is 6.25 MHz (the MAME-configured 50 MHz input
clock divided by eight). The example holds POST continue at 4 s, inserts two
credits at 10 and 11 s, then presses Start at 13 s. Each pulse lasts 0.4 s.
CMOS changes are saved when the runner exits and loaded on the next run.

## Build and bounded run

From the repository root:

```sh
python3 tools/mk2/build.py \
  --romset /path/to/roms/mk2.zip \
  --out build/mk2/work
cmake -S . -B build \
  -DMULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE="$PWD/build/mk2/work/tms34010_aot.cpp" \
  -DMULTIARCADERECOMP_ADSP2105_AOT_SOURCE="$PWD/build/mk2/work/adsp2105_aot.cpp"
cmake --build build --target multiarcaderecomp_mk2_trace -j4
./build/multiarcaderecomp_mk2_trace build/mk2/work 30000000 \
  build/mk2/work/mk2-frame.ppm --press-start-at=5000000
```

The trace executable runs a fixed TMS cycle budget, saves a PPM, and reports
native/interpreted instruction counts for both TMS34010 and ADSP-2105, plus DCS
latch/SPORT state. On exit it writes categorized interpreter totals and CPU
instruction totals to `build/mk2/work/mk2-runtime-summary.log` by default; pass
`--summary-log=PATH` to choose another file. It uses the
ported MAME TMS interpreter on AOT misses before trying the local subset.
`--window` presents the T-Unit framebuffer and queues the DCS SPORT1 mono
stream to SDL at the DCS board's 31.25 kHz external sample clock. The PPM's
400x254 active display area was compared pixel-for-pixel with a MAME MK2 story
frame. The T-Unit display profile and framebuffer path therefore work for that
scene; dynamic HCOUNT and gameplay timing still need comparison.

The DCS data RAM mirrors all four `0x0000-0x1fff` windows, matching MAME's
`0x0000-0x07ff` map mirrored by `0x1800`. The DCS runtime owns host handshake,
banked ROM and boot loading, local/shared RAM, SPORT1 autobuffer transfers,
wrap interrupts, and mono DAC samples; ADSP instruction semantics remain in the
shared CPU backend. ADSP ISA coverage and exact DCS reset/SPORT/DAC timing remain
incomplete, so MK2 is still a prototype runtime.

When the TMS reads an empty DCS output latch, MAME's DCS runtime charges 4444
main-CPU cycles to the read. The MK2 dispatcher now applies that stall to the
TMS/T-Unit clock and reports empty-latch reads and total host stall cycles in
the DCS summary. This is board-runtime scheduling behavior, independent of the
TMS AOT backend.

## Historical strict-AOT checkpoint — 2026-11-01

MK2 is frozen at the current experimental state. Do not treat the following
strict-AOT run as a boot or visual success: it executed 227,705 TMS and 689,393
ADSP instructions natively, then stopped at TMS PC `0xffa5add0`, opcode
`0x07a0`, with no AOT block. The saved frame had no completed visible frame,
the T-Unit DMA had issued no commands, and the DCS had produced no samples.
The run advanced past an earlier TMS indirect target `0xffa5ad30` after that
target was added as an extra root; the next target remains uncovered. Artifacts
from this checkpoint are the generated files
`build/diagnostics/mk2-overlay-strict-v3.log`,
`build/diagnostics/mk2-overlay-strict-v3.ppm`, and
`build/diagnostics/mk2-overlay-strict-v3.bin`.

The ADSP generator also accepts offline program-image variants captured by
`--adsp-overlay`. Each record is 5 bytes: 16-bit PC followed by a 24-bit opcode,
both big-endian. The trace snapshot includes words loaded by DCS boot pages and
words written to program RAM. Generated AOT dispatch is keyed by both PC and
the live opcode, so a different bank/image at the same PC selects its own
variant. This is offline AOT generation, not runtime compilation. A captured
`PC=0x0001` had both `0x4d6f72` and `0x000000`; adding that observed image
variant removed the ADSP stop and exposed the current TMS boundary above.

Current ADSP rollback marker remains opcode `0x5000b1` in
`docs/hardware/cpus/adsp2105/MAME_PORT.md`; this checkpoint added no ADSP instruction
semantics. Later runs passed this old TMS boundary, so this section records
history rather than the current state.

## Current gameplay investigation — 2026-11-04

The common launcher accepts MAME shortnames and runs this board with:

```sh
multiarcaderecomp build --machine=midway --board=tunit --game=mk2
multiarcaderecomp run --machine=midway --board=tunit --game=mk2 --frames 600
```

For deterministic headless diagnosis, `run` also accepts `--input-replay`
with cycle-counted active-low port values, `--nvram` for isolated CMOS, and
`--dma-command-log` for the blitter register stream. With a controlled replay,
the first 145 blitter commands matched MAME. The TMS AOT run diverged on
command 146 around 7.17 seconds; TMS interpreter-only continued matching to
command 323 and diverged at command 324 around 7.19 seconds. The earlier AOT
divergence means the TMS translation path needs investigation in addition to
the remaining board/scheduler mismatch. Current logs are recorded in
`TODO.md`; no video or DMA behavior change has been made based on an unproven
cause.

The live runner preserves SDL coin input unless scripted coin events were
requested. DCS SPORT1 now aligns the selected ADSP I register directly as MAME
does in `sound_tx_callback`; it must not apply a second DAG post-modify while
setting the audio buffer origin.
