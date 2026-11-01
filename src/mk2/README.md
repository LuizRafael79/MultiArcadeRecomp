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

MK2 uses MAME's DSW default `0xfc7d` (CMOS coinage, Powerup Test off). Unlike
MK1, its MAME input table has no `Skip Post Test` DIP; the diagnostic runner
can hold active-low Start 1 at a selected TMS cycle, letting the POST appear
before it is skipped.

## Build and bounded run

From the repository root:

```sh
python3 tools/mk2/build.py \
  --romset /home/luiz/git/mame/roms/mk2.zip \
  --out build/mk2/work
cmake -S . -B build \
  -DMAMERECOMP_MK2_TMS34010_AOT_SOURCE="$PWD/build/mk2/work/tms34010_aot.cpp" \
  -DMAMERECOMP_ADSP2105_AOT_SOURCE="$PWD/build/mk2/work/adsp2105_aot.cpp"
cmake --build build --target mamerecomp_mk2_trace -j4
./build/mamerecomp_mk2_trace build/mk2/work 30000000 \
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
scene; dynamic timing changes still need coverage.

The DCS data RAM mirrors all four `0x0000-0x1fff` windows, matching MAME's
`0x0000-0x07ff` map mirrored by `0x1800`. The DCS runtime owns host handshake,
banked ROM and boot loading, local/shared RAM, SPORT1 autobuffer transfers,
wrap interrupts, and mono DAC samples; ADSP instruction semantics remain in the
shared CPU backend. ADSP ISA coverage and exact DCS reset/SPORT/DAC timing remain
incomplete, so MK2 is still a prototype runtime.
