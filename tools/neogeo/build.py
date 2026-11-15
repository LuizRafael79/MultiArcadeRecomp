#!/usr/bin/env python3
"""Generate and build the Neo Geo KOF98 game module using local MultiArcadeRecomp tools."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import zipfile
import subprocess
import sys


def run(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def extract_member(archive: Path, member_name: str, destination: Path) -> Path:
    with zipfile.ZipFile(archive) as zf:
        matches = [name for name in zf.namelist() if Path(name).name == member_name]
        if len(matches) != 1:
            raise RuntimeError(f"expected one {member_name} in {archive}, found {len(matches)}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(zf.read(matches[0]))
    return destination


def combine_entries(paths: list[Path], destination: Path) -> Path | None:
    values: list[str] = []
    seen: set[str] = set()
    for path in paths:
        if not path.is_file():
            continue
        for line in path.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            pc_match = re.search(r"\bPC\s*=\s*\$?([0-9a-fA-F]+)", line)
            value = pc_match.group(1) if pc_match else line.replace("$", "")
            if value and value.lower().removeprefix("0x") not in seen:
                value = value.lower().removeprefix("0x")
                seen.add(value)
                values.append(value)
    if not values:
        return None
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(values) + "\n")
    return destination


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--game", default="kof98", choices=("kof98",))
    parser.add_argument("--rom-dir", type=Path,
        help="ROM directory or game ZIP; defaults to ../mame/roms")
    parser.add_argument("--rom-zip", type=Path,
        help=argparse.SUPPRESS)
    parser.add_argument("--bios-zip", type=Path)
    parser.add_argument("--hardware-mode", choices=("aes", "mvs"), default="aes",
        help="hardware mode and mode-specific AoT module to build (default: aes)")
    parser.add_argument("--bios", help="system BIOS ROM to include in this mode's AoT build")
    parser.add_argument("--build-dir", type=Path, default=Path("build"),
        help="shared MultiArcadeRecomp build root (default: build)")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--rebuild", action="store_true",
        help="regenerate the selected game's assets and AoT module (the default build already does this)")
    parser.add_argument("--entries", type=Path, action="append", default=[])
    parser.add_argument("--z80-bios-entries", type=Path, action="append", default=[])
    parser.add_argument("--z80-cart-entries", type=Path, action="append", default=[])
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    rom_source = args.rom_zip or args.rom_dir
    if rom_source is None:
        rom_source = root.parent / "mame/roms"
    if rom_source.is_dir():
        game_zip = rom_source / f"{args.game}.zip"
        bios_default = rom_source / "neogeo.zip"
    elif rom_source.suffix.lower() == ".zip":
        game_zip = rom_source
        bios_default = rom_source.parent / "neogeo.zip"
    else:
        raise RuntimeError("--rom-dir must name a ROM directory or a .zip file")
    if not game_zip.is_file():
        raise RuntimeError(f"KOF98 ROM ZIP not found: {game_zip}")
    build=(root/args.build_dir).resolve() if not args.build_dir.is_absolute() else args.build_dir
    build_tmp = build / "neogeo/kof98/tmp"
    build_tmp.mkdir(parents=True, exist_ok=True)
    build_env = os.environ.copy()
    build_env["TMPDIR"] = str(build_tmp)
    bios=args.bios_zip or (bios_default.parent / ("aes.zip" if args.hardware_mode == "aes" else "neogeo.zip"))
    if not bios.is_file():
        raise RuntimeError(f"Neo Geo BIOS ZIP not found: {bios}")
    default_bios_name = "neo-po.bin" if args.hardware_mode == "aes" else "sp-s2.sp1"
    bios_name = args.bios or default_bios_name
    bios_suffix = "" if bios_name == default_bios_name else "_" + re.sub(r"[^a-z0-9]+", "_", Path(bios_name).stem.lower()).strip("_")
    module_id = args.hardware_mode + bios_suffix
    mode_build=build/"neogeo/kof98"/module_id
    generated=mode_build/"generated"
    z80_generated=build/"neogeo/kof98/z80_generated"
    module_target=f"multiarcaderecomp_game_kof98_{module_id}"
    module_path=build/f"multiarcaderecomp_game_kof98_{module_id}.so"

    # The generator executable is part of the normal project build graph.
    # Generated shard sources are discovered by the second CMake configure
    # after ROM analysis emits them into the deterministic build directories.
    run([args.cmake,"-S",str(root),"-B",str(build),
         f"-DMULTIARCADERECOMP_NEOGEO_HARDWARE_MODE={args.hardware_mode}",
         f"-DMULTIARCADERECOMP_NEOGEO_MODULE_ID={module_id}",
         f"-DMULTIARCADERECOMP_NEOGEO_GENERATED_DIR={generated}"],root,build_env)
    run([args.cmake,"--build",str(build),"--target","kof98_aot","--parallel",str(args.jobs)],root,build_env)
    command=[str(build/"kof98_aot"),"--rom-zip",str(game_zip.resolve()),
             "--bios-zip",str(bios.resolve()),"--bios",bios_name,
             "--out",str(mode_build.resolve())]
    # Keep runtime-confirmed MVS BIOS entries in the default static table.
    runtime_roots=root/"tools/neogeo/kof98-runtime-roots.txt"
    if runtime_roots.is_file():
        command.extend(["--entries",str(runtime_roots)])
    for entry_file in args.entries:
        command.extend(["--entries",str(entry_file.resolve())])
    run(command,root,build_env)

    # Use the shared MultiArcadeRecomp Z80 frontend for the KOF98 static sound-CPU pass.
    # Generated output stays in the build tree and is compiled into the same
    # per-game module as the 68000 table.
    z80_inputs=build/"neogeo/kof98/z80_inputs"
    m1=extract_member(game_zip.resolve(),"242-m1.m1",z80_inputs/"242-m1.m1")
    # AES BIOS archives contain the home-system BIOS ROMs, while the shared
    # sound BIOS (SM1) remains in the common Neo Geo archive.
    sm1=extract_member(bios_default.resolve(),"sm1.sm1",z80_inputs/"sm1.sm1")
    z80_generator=root/"tools/z80/recomp.py"
    bios_roots=combine_entries(
        [root/"tools/neogeo/kof98-z80-bios-roots.txt",
         Path(str(build/"kof98-z80-misses")+".bios.txt"), *args.z80_bios_entries],
        z80_inputs/"bios_entries.txt")
    cart_roots=combine_entries(
        [root/"tools/neogeo/kof98-z80-cart-roots.txt",
         Path(str(build/"kof98-z80-misses")+".cart.txt"), *args.z80_cart_entries],
        z80_inputs/"cart_entries.txt")
    bios_command=[sys.executable,str(z80_generator),"--program",str(sm1),"--out",
                  str(z80_generated/"bios"),"--symbol-prefix","bios_","--precise-cycle-yield"]
    cart_command=[sys.executable,str(z80_generator),"--program",str(m1),"--out",
                  str(z80_generated/"cart"),"--symbol-prefix","kof98_","--precise-cycle-yield"]
    if bios_roots: bios_command.extend(["--entries",str(bios_roots)])
    if cart_roots: cart_command.extend(["--entries",str(cart_roots)])
    run(bios_command,root,build_env)
    run(cart_command,root,build_env)

    # Refresh the graph after code generation so CMake can add the emitted
    # shards. The directories are the deterministic defaults from CMake; this
    # adapter no longer clears or injects cache paths itself.
    run([args.cmake,"-S",str(root),"-B",str(build),
         f"-DMULTIARCADERECOMP_NEOGEO_HARDWARE_MODE={args.hardware_mode}",
         f"-DMULTIARCADERECOMP_NEOGEO_MODULE_ID={module_id}",
         f"-DMULTIARCADERECOMP_NEOGEO_GENERATED_DIR={generated}"],root,build_env)
    run([args.cmake,"--build",str(build),"--target","multiarcaderecomp",
         "multiarcaderecomp_neogeo_runtime",module_target,
         "--parallel",str(args.jobs)],root,build_env)
    print(f"Built Neo Geo runtime: {build/'multiarcaderecomp_neogeo_runtime.so'}")
    print(f"Built KOF98 {args.hardware_mode.upper()} game module: {module_path}")
    print(f"Run with: {build/'multiarcaderecomp'} run --machine=neogeo --board={args.hardware_mode} --game=kof98 --rom-dir {game_zip} --headless --frames 300")
    return 0


if __name__=="__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"neogeo build: command exited {error.returncode}",file=sys.stderr)
        raise SystemExit(error.returncode)
