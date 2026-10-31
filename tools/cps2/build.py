#!/usr/bin/env python3
"""Invoke the external CPS-2 static-recompilation toolkit in an isolated build tree."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = Path("/home/luiz/git/cps2recomp")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build one CPS-2 game with the cps2recomp backend")
    parser.add_argument("--game", required=True)
    parser.add_argument("--rom-dir", type=Path, help="ROM zip or folder (defaults to SOURCE/rom)")
    parser.add_argument("--source-dir", type=Path,
                        default=Path(os.environ.get("MAMERECOMP_CPS2_SOURCE", DEFAULT_SOURCE)))
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()

    source = args.source_dir.expanduser().resolve()
    if not (source / "Makefile").is_file() or not (source / "games" / args.game / "game.cfg").is_file():
        parser.error(f"CPS-2 toolkit/game not found: {source} (expected Makefile and games/{args.game}/game.cfg)")

    build = ROOT / "build" / "cps2" / args.game
    work = ROOT / "build" / "cps2" / "work" / args.game
    overlay = ROOT / "build" / "cps2"
    game_link = overlay / "games"
    overlay.mkdir(parents=True, exist_ok=True)
    if game_link.is_symlink():
        if game_link.resolve() != (source / "games").resolve():
            parser.error(f"refusing to replace unexpected game-source link: {game_link}")
    elif game_link.exists():
        parser.error(f"refusing to replace non-symlink game-source path: {game_link}")
    else:
        game_link.symlink_to(source / "games", target_is_directory=True)
    rom = args.rom_dir.expanduser().resolve() if args.rom_dir else source / "rom"
    command = ["make", "-C", str(source), f"GAME={args.game}", f"ROM={rom}",
               f"B={build}", f"WORK={work}", f"CURDIR={overlay}", f"-j{args.jobs}"]
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
