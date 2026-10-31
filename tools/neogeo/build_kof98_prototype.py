#!/usr/bin/env python3
"""Generate and build the KOF98 BIOS+cart 68000 AOT prototype."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import zipfile
import subprocess
import sys


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


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
    parser.add_argument("--rom-zip", type=Path, required=True)
    parser.add_argument("--bios-zip", type=Path)
    parser.add_argument("--bios", default="sp-s2.sp1", help="system BIOS ROM to include in the AOT build")
    parser.add_argument("--build-dir", type=Path, default=Path("build/neogeo-kof98"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--entries", type=Path, action="append", default=[])
    parser.add_argument("--z80-bios-entries", type=Path, action="append", default=[])
    parser.add_argument("--z80-cart-entries", type=Path, action="append", default=[])
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    build=(root/args.build_dir).resolve() if not args.build_dir.is_absolute() else args.build_dir
    bios=args.bios_zip or args.rom_zip.parent/"neogeo.zip"
    generated=build/"neogeo/kof98/generated"
    z80_generated=build/"neogeo/kof98/z80_generated"

    # First build the ROM-driven generator without wiring a generated module.
    run([args.cmake,"-S",str(root),"-B",str(build),
         "-DMAMERECOMP_NEOGEO_GENERATED_DIR=",
         "-DMAMERECOMP_NEOGEO_Z80_GENERATED_DIR="],root)
    run([args.cmake,"--build",str(build),"--target","kof98_aot","--parallel",str(args.jobs)],root)
    command=[str(build/"kof98_aot"),"--rom-zip",str(args.rom_zip.resolve()),
             "--bios-zip",str(bios.resolve()),"--bios",args.bios,
             "--out",str((build/"neogeo/kof98").resolve())]
    # Keep runtime-confirmed MVS BIOS entries in the default static table.
    runtime_roots=root/"tools/neogeo/kof98-runtime-roots.txt"
    if runtime_roots.is_file():
        command.extend(["--entries",str(runtime_roots)])
    for entry_file in args.entries:
        command.extend(["--entries",str(entry_file.resolve())])
    run(command,root)

    # Use the shared MameRecomp Z80 frontend for the KOF98 static sound-CPU pass.
    # Generated output stays in the build tree and is compiled into the same
    # per-game module as the 68000 table.
    z80_inputs=build/"neogeo/kof98/z80_inputs"
    m1=extract_member(args.rom_zip.resolve(),"242-m1.m1",z80_inputs/"242-m1.m1")
    sm1=extract_member(bios.resolve(),"sm1.sm1",z80_inputs/"sm1.sm1")
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
    run(bios_command,root)
    run(cart_command,root)

    # Reconfigure so CMake discovers the emitted C shards and static table.
    run([args.cmake,"-S",str(root),"-B",str(build),
         f"-DMAMERECOMP_NEOGEO_GENERATED_DIR={generated}",
         f"-DMAMERECOMP_NEOGEO_Z80_GENERATED_DIR={z80_generated}"],root)
    run([args.cmake,"--build",str(build),"--target","kof98recomp","--parallel",str(args.jobs)],root)
    print(f"Built static KOF98 runner: {build/'kof98recomp'}")
    print(f"Run with: kof98recomp --rom-zip kof98.zip --bios {args.bios} --headless --frames 300")
    return 0


if __name__=="__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"build_kof98_prototype: command exited {error.returncode}",file=sys.stderr)
        raise SystemExit(error.returncode)
