#!/usr/bin/env python3
"""Invoke the external CPS-2 static-recompilation toolkit in an isolated build tree."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = Path("/home/luiz/git/cps2recomp")


def link_once(path: Path, target: Path) -> None:
    if path.is_symlink():
        if path.resolve() != target.resolve():
            raise RuntimeError(f"refusing to replace unexpected source link: {path}")
        return
    if path.exists():
        raise RuntimeError(f"refusing to replace existing source path: {path}")
    path.symlink_to(target, target_is_directory=target.is_dir())


def custom_profile_source(source: Path, game: str, profile: Path, base: Path) -> Path:
    """Make a source-tree overlay so upstream tools can consume a local game profile."""
    overlay = base / "toolkit-source"
    overlay.mkdir(parents=True, exist_ok=True)
    (base / "work").mkdir(exist_ok=True)
    link_once(overlay / "work", base / "work")
    skipped = {".git", "build", "work", "rom", "games"}
    for entry in source.iterdir():
        if entry.name not in skipped:
            link_once(overlay / entry.name, entry)

    games = overlay / "games"
    games.mkdir(exist_ok=True)
    for entry in (source / "games").iterdir():
        if entry.is_dir():
            link_once(games / entry.name, entry)

    custom = games / game
    custom.mkdir(exist_ok=True)
    link_once(custom / "game.cfg", profile / "game.cfg")
    # This profile shares SFA2's readable game logic, whose family traits are in the parent profile.
    if game == "sfz2a":
        link_once(custom / "family.h", source / "games" / "sfa2" / "family.h")
    return overlay


def main() -> int:
    parser = argparse.ArgumentParser(description="Build one CPS-2 game with the cps2recomp backend")
    parser.add_argument("--game", required=True)
    parser.add_argument("--rom-dir", type=Path, help="ROM zip or folder (defaults to SOURCE/rom)")
    parser.add_argument("--source-dir", type=Path,
                        default=Path(os.environ.get("MAMERECOMP_CPS2_SOURCE", DEFAULT_SOURCE)))
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--rebuild", action="store_true", help="remove this game's build and generated work before rebuilding")
    args = parser.parse_args()

    source = args.source_dir.expanduser().resolve()
    profile = ROOT / "tools" / "cps2" / "profiles" / args.game
    upstream_game = source / "games" / args.game
    if not (source / "Makefile").is_file() or not (upstream_game / "game.cfg").is_file() and not (profile / "game.cfg").is_file():
        parser.error(f"CPS-2 toolkit/game not found: {source} (expected upstream game or local profile for {args.game})")
    if (upstream_game / "game.cfg").is_file() and (profile / "game.cfg").is_file():
        parser.error(f"game id exists both upstream and locally: {args.game}")

    build = ROOT / "build" / "cps2" / args.game
    work = ROOT / "build" / "cps2" / "work" / args.game
    module_build = ROOT / "build" / "cps2" / f"module-{args.game}"
    module = ROOT / "build" / "cps2" / f"libmamerecomp_cps2_{args.game}.so"
    if args.rebuild:
        shutil.rmtree(build, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)
        shutil.rmtree(module_build, ignore_errors=True)
        module.unlink(missing_ok=True)
    build_root = ROOT / "build" / "cps2"
    build_root.mkdir(parents=True, exist_ok=True)
    if (profile / "game.cfg").is_file():
        source_root = custom_profile_source(source, args.game, profile, build_root)
        game_dir = source_root / "games" / args.game
        game_config_root = source_root
    else:
        source_root = source
        game_dir = upstream_game
        game_config_root = build_root
        game_link = build_root / "games"
        if game_link.is_symlink():
            if game_link.resolve() != (source / "games").resolve():
                parser.error(f"refusing to replace unexpected game-source link: {game_link}")
        elif game_link.exists():
            parser.error(f"refusing to replace non-symlink game-source path: {game_link}")
        else:
            game_link.symlink_to(source / "games", target_is_directory=True)
    rom = args.rom_dir.expanduser().resolve() if args.rom_dir else source / "rom"
    command = ["make", "-C", str(source_root), f"GAME={args.game}", f"ROM={rom}",
               f"B={build}", f"WORK={work}", f"CURDIR={game_config_root}", f"-j{args.jobs}"]
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode:
        return result.returncode

    result = subprocess.run([sys.executable, str(ROOT / "tools/cps2/instrument_coverage.py"),
                             "--source", str(source), "--work", str(work)], cwd=ROOT)
    if result.returncode:
        return result.returncode

    include_game_sources = True
    module_config = profile / "module.cfg"
    if module_config.is_file():
        for line in module_config.read_text().splitlines():
            key, separator, value = line.partition("=")
            if separator and key.strip() == "include_game_sources":
                include_game_sources = value.strip().lower() not in {"false", "0", "no", "off"}

    configure = ["cmake", "-S", str(ROOT / "tools/cps2"), "-B", str(module_build),
                 f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={build_root}",
                 "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCPS2RECOMP_SOURCE_DIR={source}", f"-DCPS2RECOMP_WORK_DIR={work}",
                 f"-DCPS2RECOMP_GAME={args.game}", f"-DCPS2RECOMP_GAME_DIR={game_dir}",
                 f"-DCPS2RECOMP_LOCAL_PROFILE_DIR={profile}",
                 f"-DCPS2RECOMP_INCLUDE_GAME_SOURCES={'ON' if include_game_sources else 'OFF'}"]
    result = subprocess.run(configure, cwd=ROOT)
    if result.returncode:
        return result.returncode
    print("Building the CPS-2 board runtime module.", flush=True)
    return subprocess.run(["cmake", "--build", str(module_build), "--target",
                           f"mamerecomp_cps2_{args.game}", "--parallel", str(args.jobs)],
                          cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
