# MK2 DMA comparison

The trace compares the T-Unit DMA register snapshot at each triggered command.
It helps locate the first command that diverges before comparing rendered
pixels. Keep the game revision, CMOS NVRAM and input sequence the same in both
runs. The diagnostic runner now loads/saves a 16 KiB image at
`build/mk2/work/mk2.nvram` by default; use `--nvram=PATH` to isolate a run.

## MultiArcadeRecomp

Build and run with a command log enabled:

```sh
cmake --build build --target multiarcaderecomp_mk2_trace -j4
build/multiarcaderecomp_mk2_trace build/mk2/work --window \
  --dma-command-log=build/mk2/work/mk2-recomp-dma.log
```

Close the window after the corruption appears. The command log is written at
exit and contains the DMA command plus source, destination, size, palette,
scale, configuration, and clip registers.

## MAME

From the MultiArcadeRecomp repository, launch the parent MK2 set with the trace script:

```sh
mame mk2 -rompath /home/luiz/git/mame/roms -autoboot_delay 0 \
  -autoboot_script "$PWD/tools/mk2/mame_dma_trace.lua"
```

Run it from the MultiArcadeRecomp repository root. The script writes
`build/mk2/work/mk2-mame-dma.log` by default; set `MULTIARCADERECOMP_MK2_DMA_LOG` to
choose another path. It captures the same register snapshot through a write
tap on the TMS34010 program space. Close MAME after reaching the same point as
the MultiArcadeRecomp run.

## Compare

```sh
python3 tools/mk2/compare_dma.py /path/to/mk2-mame-dma.log \
  build/mk2/work/mk2-recomp-dma.log
```

The comparator reports the first differing command and fields. If the command
snapshots match through the corruption point, the next step is a pixel-level
comparison of the blitter's writes and scanout VRAM addresses.

The trace includes the triggering TMS PC, execution time, and general
registers as diagnostic metadata. These are not compared: MAME's write tap
samples the CPU during the bus write, while MultiArcadeRecomp currently records the
state before the instruction starts. MAME logs emulated seconds and
MultiArcadeRecomp logs TMS execution cycles after the DMA fields, so command timing
can still be compared directly.
