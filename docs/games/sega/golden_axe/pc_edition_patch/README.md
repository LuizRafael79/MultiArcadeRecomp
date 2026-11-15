# Golden Axe PC Edition title patch

This patch uses the `goldnaxeud` ROM set as its source and creates a separate `goldnaxepc` set with an animated `PC EDITION` line. The original `goldnaxeud` archive and profile remain unchanged.

## Apply

Keep the original archive unchanged and write the patched copy elsewhere:

```sh
python3 apply_patch.py ~/git/mame/roms/goldnaxeud.zip /tmp/goldnaxepc.zip
```

The patcher checks the SHA-1 and size of each source program chip before writing. It changes only the two interleaved 68000 program ROMs, preserves their lengths and ZIP member names, and leaves all other ROM data untouched. It refuses to overwrite the input archive.

The `.bps` files are binary diffs for the two interleaved 68000 program ROMs. No complete ROM images are included.

## Changes

- After the original GOLDEN AXE logo has settled, the letters in `PC EDITION` orbit the emblem and fall into place above the title word.
- The line uses the System 16B text tilemap and its blue and white lettering, matching the game's `INSERT COIN` prompt for the orbit, the fall, and the final hold. The line stays while the title is active and is cleared after a 300-frame grace period when the title markers disappear.
- The 68000 hook runs from unused `0xff` space in the program ROM. ROM sizes, interleave, and region offsets remain unchanged.

The `goldnaxepc` profile selects Free Play by default. Build and run it with:

```sh
build/mamerecomp build --machine=sega --board=system16b --game=goldnaxepc --rom-dir ~/git/mame/roms/goldnaxepc.zip
build/mamerecomp run --machine=sega --board=system16b --game=goldnaxepc --rom-dir ~/git/mame/roms/goldnaxepc.zip --audio --arranged-bgm
```

The patch is based on `goldnaxeud`; other Golden Axe revisions have different program ROM layouts and need their own analysis.
