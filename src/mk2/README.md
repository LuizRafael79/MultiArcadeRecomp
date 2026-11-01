# Mortal Kombat II T-Unit prototype

MK2 is a separate board module. It reuses the common T-Unit/TMS34010 runtime
from `src/mk/`, while `src/mk2/` owns the MK2-specific protection and DCS Audio
2K adapter. The DCS adapter connects the TMS host latches to the shared
ADSP-2105 CPU backend in `core/cpus/adsp2105/`; it does not use the MK1
Williams/MC6809 audio board.

The ROM pipeline verifies the MAME parent set's filenames, sizes, and CRC32s,
interleaves the TMS program and graphics regions, reconstructs the DCS sound
region, and emits per-ROMset TMS and ADSP AOT sources. The generated ADSP source
currently translates NOP, group-0 immediate register loads, and the DCS boot's
no-op MAC plus DAG2 program read. Other instructions remain interpreter
fallback candidates. The interpreter currently covers a small subset of the
ADSP-2105 ISA and stalls with a categorized fallback when it reaches an
unsupported opcode; this is deliberately visible in diagnostics.

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
./build/mamerecomp_mk2_trace build/mk2/work 5000000
```

The trace executable runs a fixed TMS cycle budget and saves `mk2-frame.ppm`,
plus reports native/interpreted instruction counts and DCS latch/ADSP state. It
is a bring-up diagnostic, not an interactive game frontend. SPORT/DAC sample
output, accurate DCS timing, full host command protocol, and complete ADSP
instruction coverage are still pending. The frame output is currently not
validated against MAME and may be corrupted while the board runtime is
incomplete.
