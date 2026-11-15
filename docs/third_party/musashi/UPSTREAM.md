# Musashi 68000 reference core

Source: https://github.com/kstenerud/Musashi
Imported from the NeoGeoRecomp submodule revision recorded in its checkout (`313ebf1`).
License text: `docs/license/musashi/LICENSE-MUSASHI.txt`.

For MultiArcadeRecomp this is intended as an optional correctness oracle/fallback and supplies base cycle data to the imported code generator. It is not the generated native execution path. Do not silently use it to mask missing native coverage in final coverage claims.
