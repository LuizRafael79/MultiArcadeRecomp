# ROM audit data and runtime drivers

The TOML records under `src/romsets/<publisher>/<module>/<board>/<game>.toml`
contain catalog metadata from MAME `-listxml` and, where available, a separate
`runtime` projection with explicit load segments and board properties. They
answer audit questions and can drive generated runtime tables when the board
has an implemented projection. `gamename` is MAME's display name.

MAME `-listxml` alone does not describe physical ROM wiring. It does not
preserve interleave width, bus stride, word swaps, `ROM_CONTINUE` source
offsets, or board-specific transforms. Runtime loading still uses existing
C++ profiles for System 16/18, while KOF98 generates its ROM table from the
TOML `runtime` projection. A runtime projection must contain verified geometry,
transforms, and hashes; the migration does not infer these from audit metadata.

## Runtime ownership

- CPS-1 supported game data lives under `src/cps1/games/<game>/`. The game
  driver selects machine, input and CPS-B profiles and its ROM-load table.
  Hardware shared by CPS-1 games lives under `src/cps1/core/`.
- System 16/18 runtime profiles still use existing per-game C++ tables and the
  shared System 16/18 runtime. TOML manifests have not yet replaced those
  active tables.
- Neo Geo KOF98 generates a C++ ROM table from its TOML runtime projection.
  Its board/game integration remains a separate migration task.
- ROM archives stay separate from source and generated binaries. Runtime
  loaders consume the selected set's ZIP and validate expected CRC32 values.

## Updating the audit catalog

Use the saved MAME XML dump so import is reproducible and does not depend on
an installed MAME executable:

```sh
python3 tools/generators/romsets/import_mame_module_romsets.py \
  --mame-source /path/to/mame --xml build/multiarcaderecomp-listxml.xml \
  --output src/romsets

python3 tools/generators/romsets/import_sega_mame.py \
  --mame-source /path/to/mame --xml build/multiarcaderecomp-listxml.xml \
  --output src/romsets
```

The importers write MAME catalog metadata. Runtime load geometry must be added
or verified separately; do not infer wiring from XML offsets alone.
