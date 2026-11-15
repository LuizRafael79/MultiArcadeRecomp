# Namco System 12 migration status

The clean `multiarcaderecomp` baseline contains a Tekken 3 ROM manifest at
`src/romsets/namco/system12/tekken3.toml` and a reusable H8/300H CPU core at
`core/cpus/h8/`. It does not contain a System 12 board runtime, game module,
H8 board adapter, or launcher integration. The manifest alone is not runtime
support.

System 12 has not yet been migrated from `multiarcaderecomp_dev`. When that work
starts, keep its board runtime and device wiring separate from Capcom ZN1/ZN2;
share only generic PSX-derived components or CPUs through explicit hardware
modules. A source comparison and migration inventory must precede any code
move.
