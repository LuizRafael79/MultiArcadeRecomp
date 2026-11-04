# ROM audit data and runtime drivers

The JSON records under `src/romsets/<publisher>/<module>/<board>/<game>.json`
are a compact catalog copied from MAME `-listxml`. They answer audit questions:
which set is this, which set is its parent/ROM source, and which filenames,
sizes, offsets and hashes MAME expects. `gamename` is MAME's display name.
They are not load plans and are not consumed by a game's runtime loader.

MAME `-listxml` does not describe physical ROM wiring. It does not preserve
interleave width, bus stride, word swaps, `ROM_CONTINUE` source offsets, or
board-specific transforms. Runtime loading therefore comes from game-specific
C++ ROM drivers, where a supported game is implemented. Those drivers encode
load geometry, verify CRC32, and construct the board's ROM regions.

## Runtime ownership

- CPS-1 supported game data lives under `src/cps1/games/<game>/`. The game
  driver selects machine, input and CPS-B profiles and its ROM-load table.
  Hardware shared by CPS-1 games lives under `src/cps1/core/`.
- System 16/18 game ROM profiles currently use the existing per-game C++
  files under `src/romsets/` and the shared System 16/18 runtime. Their JSON
  catalog is audit-only.
- Neo Geo KOF98 still uses a generated C++ table from its existing JSON as a
  temporary path. It is scheduled for conversion to a per-game C++ driver.
- ROM archives stay separate from source and generated binaries. Runtime
  loaders consume the selected set's ZIP and validate expected CRC32 values.

## Updating the audit catalog

Use the saved MAME XML dump so import is reproducible and does not depend on
an installed MAME executable:

```sh
python3 tools/generators/romsets/import_mame_module_romsets.py \
  --mame-source /home/luiz/git/mame --xml /tmp/mamerecomp-listxml.xml \
  --output src/romsets

python3 tools/generators/romsets/import_sega_mame.py \
  --mame-source /home/luiz/git/mame --xml /tmp/mamerecomp-listxml.xml \
  --output src/romsets
```

The importers write only audit metadata. To implement runtime support, add or
update the game's C++ driver and wire it to its shared module/core profile.
Do not infer ROM wiring from the XML offsets alone.
