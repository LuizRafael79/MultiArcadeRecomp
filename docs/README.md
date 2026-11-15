# Documentation Index

This directory contains project notes, system descriptions, hardware
integration status, provenance, and license texts. Documentation is kept
separate from implementation sources.

## System documentation

- Capcom CPS: [`README`](systems/capcom/cps/README.md), [`status`](systems/capcom/cps/STATUS.md), [`upstream notes`](systems/capcom/cps/UPSTREAM.md)
- Midway Seattle / Killer Instinct: [`runtime`](systems/midway/seattle/README.md), [`pipeline`](systems/midway/seattle/KILLER_INSTINCT_PIPELINE.md)
- Midway T-Unit / MK: [`runtime`](systems/midway/tunit/README.md), [`port map`](systems/midway/tunit/RUNTIME_PORT.md), [`MK2`](systems/midway/tunit/mk2/README.md)
- Midway V-Unit / Cruis’n USA: [`runtime`](systems/midway/vunit/README.md), [`research`](systems/midway/vunit/CRUISNUSA_RESEARCH.md)
- Sega Naomi: [`architecture`](systems/sega/naomi/ARCHITECTURE.md), [`runtime audit`](systems/sega/naomi/RUNTIME_PORT_AUDIT.md)
- Sega System 16: [`board and CPU status`](systems/sega/system16/STATUS.md), [`Golden Axe`](games/sega/golden_axe/STATUS.md)
- Sega System 18: [`video status`](systems/sega/system18/VIDEO.md)
- Sega System 16 game notes: [`Golden Axe`](games/sega/golden_axe/STATUS.md)
- Konami TMNT: [`original board`](systems/konami/tmnt/STATUS.md), [`TMNT2/Sunset Riders board`](systems/konami/tmnt2/STATUS.md)
- Namco System 12: [`migration status`](systems/namco/system12/MIGRATION_STATUS.md)
- Seibu/TAD Toki: [`board status`](systems/seibu/toki/STATUS.md)
- SNK Neo Geo: [`KOF98 status`](systems/snk/neogeo/STATUS.md)
- Sony ZN: [`module overview`](systems/sony/zn/README.md)

## Shared hardware

- CPUs: [`status`](hardware/cpus/STATUS.md), [`Z80 backend and interpreter decision`](hardware/cpus/z80/README.md), [`M68000`](hardware/cpus/m68000/README.md), [`TMS34010`](hardware/cpus/tms34010/README.md), [`SH-4`](hardware/cpus/sh4/README.md)
- Video: [`status`](hardware/video/STATUS.md), [`PSX GPU backends`](hardware/video/psx/BACKENDS.md)
- Audio: [`status`](hardware/audio/STATUS.md), [`DCS modularization`](hardware/audio/dcs/MODULARIZATION.md), [`QSound source notes`](hardware/audio/qsound/MAME_SOURCE_DIFF.md)

## Project and references

- [`Architecture overview`](architecture/OVERVIEW.md)
- [`Dynamic runtime modules and per-game F1 configuration`](architecture/OVERVIEW.md#application-and-loadable-modules)
- [`Repository and modularization audit log`](REPOSITORY_REORGANIZATION.md)
- [`Source modularization audit`](architecture/MODULARIZATION_AUDIT.md)
- [`TODO`](TODO.md)
- [`ROM catalog`](romsets/README.md)
- [`Third-party provenance`](third_party/README.md)
- [`Licenses and notices`](license/README.md)

The index lists documentation already present for migrated prototypes. A ROM
manifest alone does not establish that a board or game runtime is complete.
