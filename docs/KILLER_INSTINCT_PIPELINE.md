# Killer Instinct AOT pipeline prototype

## Scope

The migration targets MAME parent sets `kinst` (KI1) and `kinst2` (KI2). Both
use the Seattle platform with a little-endian MIPS III R4600. UltraCombo boots
the game under its interpreter, snapshots loaded code and translates that
image. This is a convenient discovery strategy, not a hardware requirement.
For KI1, the executable is compressed inside the U98 boot EPROM; `kipack`
extracts the images offline, and `ki-recomp` documents a byte-for-byte match of
the code image against MAME DRAM. The CHD contains disk data/assets, so
decompressing it is not a substitute for decoding U98.

The MAME driver remains the source for set names, BIOS/ROM checksums, disk
identities and memory map. A live MAME RAM capture remains available as a
diagnostic reference: it exposes the post-loader memory state and code entries
that static U98 extraction may not discover. It is not required to build the
current KI1 or KI2 AOT image.

## Flow

```text
MAME parent ZIP (+ CHD for game/runtime)
    -> validate BIOS, DCS ROM checksums and CHD presence
    -> KI1: decode compressed game images from U98 with kipack
       KI2: decode game images offline from U98 with kipack
    -> tools/mips3/recomp.py discovers reachable MIPS III code
    -> generated C translation units + entry/image tables under build/ki/work
    -> shared R4600 CPU/AOT ABI in core/cpus/mips3 (new integration slice)
```

Command line:

```sh
python3 tools/recompile.py --system ki --game ki1 --rom-dir ~/git/mame/roms
python3 tools/recompile.py --system ki --game ki2 --rom-dir ~/git/mame/roms
```

Both KI1 and KI2 decode their MIPS images offline from U98 with the pinned
`ki-rom`/`kipack` utility (`-t ki1` or `-t ki2`); neither needs MAME to create
its initial static translation. KI1 image hashes and KI2 code/table images
are checked. KI2's writable data image can differ from a later MAME snapshot.
Pass `--capture-mame` to build from a live post-loader RAM snapshot instead;
`--frames` selects its capture frame (default 900). This is useful for runtime
initialized pointers/data and for finding code not present in the original
U98 images. The build fetches the pinned `ki-rom` utility into ignored
`build/` on first use; install `kipack` separately or pass
`--kipack /path/to/kipack` to avoid that.
If the CHD is nested in a ZIP under the set folder, the builder extracts the CHD
container (not a raw disk image) into `build/ki/work/<game>/mame_chd_roms/` and
adds that search path for MAME. To skip MAME capture, supply both `--ram0` and
`--ram2`. Optional coverage data is
passed through with `--coverage`. Use `--romset` when the set ZIP is outside
the normal ROM root, and `--mame` to name a non-default MAME executable.

## Current status and limits

- KI1 is connected to `mamerecomp build/run`. The Seattle prototype loads the
  parent ZIP and CHD, decodes the MIPS images from U98 offline, starts the shared
  R4600 AOT backend, drives a frame scheduler/video scanout, maps keyboard
  input, opens persistent NVRAM and connects the DCS/ADSP sample path.
- The live SDL framebuffer is 320x240 presented at 960x720. A headless smoke
  run of 180 frames produced the KI logo (`/tmp/ki1-180.ppm`). This confirms
  boot and visible output only; it does not establish gameplay or parity with
  MAME/UltraCombo.
- KI1 uses the UltraCombo ADSP-2105 static translator plus its interpreter for
  uncovered code. The DCS runtime wiring is active; the user confirmed sound
  in gameplay. A short audio startup gap can occur if coin is inserted during
  POST, and needs an event-timeline comparison. Seattle timing, IDE behavior,
  input, NVRAM and video also need systematic comparison against MAME.
- KI2 is connected to the Seattle run dispatcher and uses its own profile,
  ROM/CHD identities, and KI2-specific I/O/IDE differences. The user tested
  the game and confirmed it works perfectly for this prototype. KI2 is marked
  complete and frozen at this checkpoint; further work requires a concrete
  issue or an explicit decision to resume it.
- A MAME reference capture generated 473 functions, 16,858 entries and 43,703
  reached words. Offline extraction from KI2's U98 generated the same function
  and word counts with 16,851 entries. The code image and pointer table match
  captured MAME RAM byte for byte; mutable data differs after runtime use. The
  seven extra MAME entries remain unexplored. Live RAM capture remains a
  diagnostic option, while offline extraction avoids booting MAME during
  ordinary builds; investigating the extra entries is deferred and does not
  block the completed KI2 prototype.
- A correct MIPS disassembly from one snapshot does not cover code generated or
  modified later in RAM. Runtime validation and dynamic-code policy remain
  necessary before calling the output complete.

Run KI1 with a live window (build first if the dispatcher reports stale
artifacts):

```sh
build/mamerecomp build --machine=ki --board=seattle --game=ki1 \
  --rom-dir ~/git/mame/roms --jobs 4
build/mamerecomp run --machine=ki --board=seattle --game=ki1 \
  --rom-dir ~/git/mame/roms --window
```

For KI2, replace `ki1` with `ki2` in both commands.

Without `--window`, a run with no `--headless`/`--frames` option opens the
window by default. For a finite capture, add `--headless --frames 180
--frame-out /tmp/ki1-frame.ppm`. P1 controls are arrows, A/S/D and Q/W/E;
coin 5 and start 1. Escape closes the window.

## Source notes

- MAME ROM declarations and board map: `~/git/mame/src/mame/rare/kinst.cpp`.
- MIPS snapshot translator adapted from [UltraCombo](https://github.com/thefixinhixon/UltraCombo)
  `tools/recomp.py`; see
  `tools/mips3/UPSTREAM.md` and `tools/mips3/LICENSE.UltraCombo`.
- [`ki-recomp`](https://github.com/kilnan/ki-recomp) was inspected as a secondary reference only. Its KI1-specific
  context/runtime ABI was not copied. Its KI1 `kipack` extraction path is used
  as an external build-time utility; details/license are in `tools/ki/UPSTREAM.md`.
