#!/usr/bin/env python3
"""Generate the SFEX2PA QSound Z80 AOT image from its ROM ZIP."""

import argparse
import subprocess
import sys
import zipfile
from pathlib import Path


SOUND_ROMS = ("x2p_02.2e", "x2p_03.3e")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--ram-snapshot", type=Path)
    parser.add_argument("--pc-list", type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    # A first-stage build is Z80-only. Remove a prior captured RAM overlay so
    # CMake does not accidentally keep compiling and linking stale R3000A code.
    if not args.ram_snapshot:
        psx_dir = args.out / "psx"
        for stale in (psx_dir / "psx_program.cpp", psx_dir / "sfex2pa_ram.bin"):
            stale.unlink(missing_ok=True)
        for stale in psx_dir.glob("psx_program_*.cpp"):
            stale.unlink()
    with zipfile.ZipFile(args.rom) as archive:
        audio = b"".join(archive.read(name) for name in SOUND_ROMS)
    if len(audio) != 0x40000:
        parser.error(f"SFEX2PA QSound ROM size is {len(audio):#x}, expected 0x40000")
    program = args.out / "sfex2pa_qsound.bin"
    program.write_bytes(audio)
    status = subprocess.run([
        args.python, str(Path(__file__).resolve().parents[1] / "z80" / "recomp.py"),
        "--program", str(program), "--out", str(args.out / "z80"),
        "--list", str(args.out / "sfex2pa_qsound.lst"),
        "--precise-cycle-yield",
    ], check=False).returncode
    if status or not args.ram_snapshot:
        return status
    if not args.pc_list or not args.pc_list.is_file():
        parser.error("--ram-snapshot requires --pc-list from an interpreter profiling run")
    image = args.ram_snapshot.read_bytes()
    if len(image) != 0x400000:
        parser.error(f"ZN-2 RAM snapshot size is {len(image):#x}, expected 0x400000")
    psx_dir = args.out / "psx"
    psx_dir.mkdir(parents=True, exist_ok=True)
    program = psx_dir / "sfex2pa_ram.bin"
    program.write_bytes(image)
    psx_output = psx_dir / "psx_program.cpp"
    return subprocess.run([
        args.python, str(Path(__file__).resolve().parent / "recomp.py"),
        "--program", str(program), "--base", "0x80000000",
        "--entry", "0x80000000", "--entries-file", str(args.pc_list),
        "--entries-only", "--validate-source-every-instruction",
        "--out", str(psx_output),
    ], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
