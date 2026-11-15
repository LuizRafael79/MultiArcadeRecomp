# Game manifests and driver modules

ROM-set source manifests under `src/romsets/` use TOML. ROM-set tools require
Python 3.11 or newer for the standard-library TOML parser. The migration preserves
their existing fields and hierarchy: `module`, `board`, `game`, `parent`, MAME
registration metadata, regions, ROM files, load segments, runtime properties,
and per-game transforms. ROM-set tools accept TOML only. JSON remains in
unrelated generated reports and AOT metadata where it is part of those tools'
interfaces.

## Parent and clone semantics

`parent` records the MAME parent/clone relationship. It is used to locate
related archives and identify shared ROM files. Each ROM-set manifest remains
self-contained for its region layout: child ROM lists are not implicitly
concatenated with parent lists. This avoids silently loading a parent's ROM
into a clone when that clone changes region geometry or board wiring.

Game-driver configuration should be a separate concern from ROM inventory.
Board defaults belong to a board profile. A game's TOML selects that profile
and names only its verified overrides or optional code hooks. Parent data
inheritance needs explicit per-field merge rules before it is enabled; arrays
such as ROM loads and input definitions must never be concatenated by default.

## Runtime boundary

`core/abi/game_driver_v1.h` defines a C ABI for optional game-specific modules.
It contains fixed-width version and size fields, stable identifiers, and an
optional configuration callback. The host supplies board services through a
versioned C API. The game module sees that API as opaque until it includes the
corresponding board header. CPU implementations and board runtime code are not
linked into a game's driver merely to expose this interface.

Use the module only for behavior that cannot be represented as manifest data:
for example a game-specific protection setup or a board register wiring
override. Different program ROMs, gameplay rules, and balance changes remain
in the user's ROM and do not require a C++ reimplementation. Shared hooks
should live in a reusable board extension when more than one game needs them.

## Migration status

- All 1,119 checked-in ROM-set manifests were converted from JSON to TOML and
  round-trip parsed with Python's standard TOML reader.
- ROM archive loading and ROM-set import/generation tools now accept and write
  TOML only.
- System 16/18 runtime profiles are still partly hand-authored in C++.
  `emit_system_profiles.py` is not yet the active profile source in CMake, and
  some legacy games do not yet have complete runtime load geometry in their
  manifests. Do not remove those C++ profiles until the manifest projection
  reproduces the active profile table.
- Existing game `.so` modules have not all adopted the generic driver ABI.
  Migration should proceed board by board with a typed board API and a
  parent/clone family that can be compared against MAME.
