# Multi Arcade Recomp

Multi Arcade Recomp is an experimental, modular arcade game recompiler and
runtime. It translates selected game code ahead of time (AOT) and runs it with
reusable CPU, chip, and board implementations. The project is under active
development; support varies by game and a registered ROM profile does not mean
that the game is fully playable.

## Project status

The most mature path is Sega System 16A/16B and System 18. Neo Geo/KOF98,
Capcom CPS, Midway, Konami, Seibu, Sony ZN, and Sega Naomi work is experimental
and isolated by system or board module. Known gaps and validation evidence are
tracked in the [documentation index](docs/README.md).

The shared 68000 AOT frontend is active. Interpreters remain available as
fallbacks for code that is not translated. The in-house Z80 AOT emitter is
still under development and does not yet replace the legacy production backend.
The MAME project is used as a hardware, ROM-layout, and behavior reference; it
is not a runtime dependency. ROM images are provided by the user and are not
included in this repository.

## Build

Requirements: CMake 3.20 or newer, a C/C++20 compiler, Python 3.11+, libarchive,
and zlib. SDL3 is optional and enables windowed presentation; OpenGL 4.3 enables
optional accelerated video paths.

```sh
cmake -S . -B build
cmake --build build -j1
```

List available game profiles:

```sh
./build/mamerecomp list-games
```

Build and run the CPS1 `sf2ce` profile:

```sh
build/mamerecomp build --machine=cps --board=cps1 --game=sf2ce
build/mamerecomp run --machine=cps --board=cps1 --game=sf2ce \
  --rom-dir /path/to/roms
```

Run the Neo Geo KOF98 recompilation pipeline:

```sh
python3 tools/recompile.py --system neogeo --game kof98 \
  --rom-dir /path/to/roms --jobs 2
```

ROM sets are external inputs. The commands accept ROM paths as documented by
their `--help` output. Generated sources, binaries, caches, and build products
belong under `build/` and are excluded from Git.

## Architecture and repository layout

- `core/cpus/`, `core/chips/`, and `core/audio/` contain reusable hardware
  models and shared CPU implementations.
- `core/third_party/` contains isolated upstream dependency snapshots and
  provenance/license information.
- `src/boards/` contains board-specific maps, wiring, timing, and adapters,
  grouped by manufacturer and board family.
- `src/runtime/` contains reusable system-family runtime and scheduling code.
- `src/games/` contains game profiles and behavior that has been separated
  from board implementations.
- `src/romsets/` contains TOML ROM manifests. See the [ROM-set catalog](docs/romsets/README.md).
- `tools/` contains recompiler frontends, build pipelines, generators, and
  diagnostic utilities.
- `docs/` contains architecture notes, system and hardware status, references,
  third-party provenance, and license texts. Start at [docs/README.md](docs/README.md).
- `build/` is for generated and temporary build output; it is not source.

System modules should own board-specific behavior, while shared CPU and chip
code stays in `core/`. Third-party components remain separate from project
code, with their original notices preserved. See the [architecture overview](docs/architecture/OVERVIEW.md)
and [modularization audit](docs/architecture/MODULARIZATION_AUDIT.md) for the
current boundaries and planned work.

## ROMs, licensing, and contributions

No game ROMs are included. Use only ROM dumps that you are legally entitled to
use. Third-party code may have different license terms; consult
[`docs/license/`](docs/license/README.md) and the relevant upstream notices
before redistributing a build.

This repository is experimental. Changes should preserve known behavior and
document the tested game, board, and runtime path. Build success alone does not
establish correct emulation; validation status and known limitations are
recorded in the system documentation.
