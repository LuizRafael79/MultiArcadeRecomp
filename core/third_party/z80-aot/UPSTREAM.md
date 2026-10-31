# Shared Z80 CPU source and provenance

This is the single Z80 CPU/AOT runtime vendored for use by all MameRecomp
systems. Board-specific buses, clocks, interrupt wiring and sound-device
synchronization live outside this directory.

The initial implementation was copied from `/home/luiz/git/cps2recomp` at
commit `e6142dc` (2026-10-29), then moved into a platform-neutral path and
namespace. It remains GPL-3.0-only with its original notices and license.
The decoder/emitter is in `tools/z80/`; game-derived generated sources stay
under `build/<system>/work/<game>/`.

Before publishing the eventual MIT implementation, rewrite these files or
obtain compatible relicensing from the rightsholder. `CPS_PROGRESS.md` and
`Z80_PROGRESS.md` track the current integration status and known limitations.
