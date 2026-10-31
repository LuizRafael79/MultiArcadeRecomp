#!/usr/bin/env python3
"""System-selecting entry point for the MameRecomp build backends."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a game with the selected arcade-system recompilation backend"
    )
    parser.add_argument(
        "--system", required=True,
        choices=("system16", "system16a", "system16b", "system18", "cps1", "cps2", "neogeo"),
    )
    parser.add_argument("--game", required=True, help="game/set short name, such as kof98, dino, or sfa2")
    parser.add_argument("--rom-dir", type=Path, help="ROM set directory or zip for the selected backend")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--source-dir", type=Path, help="override the source toolkit checkout for CPS backends")
    args, backend_args = parser.parse_known_args()

    if args.system == "neogeo" and args.game.lower() in {"kof98", "kof98p", "kof98n"}:
        # Keep the frozen Neo Geo prototype's established entry point intact.
        command = [sys.executable, str(ROOT / "tools/neogeo/build_kof98_prototype.py"),
                   "--jobs", str(args.jobs), *backend_args]
    elif args.system in {"cps1", "cps2"}:
        backend = ROOT / "tools" / args.system / "build.py"
        command = [sys.executable, str(backend), "--game", args.game, "--jobs", str(args.jobs)]
        if args.rom_dir:
            command.extend(["--rom-dir", str(args.rom_dir)])
        if args.source_dir:
            command.extend(["--source-dir", str(args.source_dir)])
        command.extend(backend_args)
    elif args.system in {"system16", "system16a", "system16b", "system18"}:
        parser.error(
            f"{args.system}/{args.game}: the board runtime exists, but a per-game AOT build adapter "
            "is not wired into tools/recompile.py yet"
        )
    else:
        parser.error(f"backend {args.system}/{args.game} is not wired into this checkout yet")

    print("+", " ".join(command), flush=True)
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
