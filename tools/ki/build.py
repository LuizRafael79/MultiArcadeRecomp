#!/usr/bin/env python3
"""Capture and statically translate a KI1/KI2 MIPS RAM image.

This is an offline discovery/build stage. It does not produce a runnable KI
executable yet: MameRecomp still needs the shared R4600 runtime and generated
code ABI described in docs/hardware/cpus/mips3/UPSTREAM.md.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import zlib

from romsets import GAMES


ROOT = Path(__file__).resolve().parents[2]
MIPS_RECOMPILER = ROOT / "tools/mips3/recomp.py"
ADSP_UPSTREAM = ROOT / "core/third_party/ultracombo-adsp"
LUA_TEMPLATE = ROOT / "tools/ki/capture_ram.lua.in"
KI_ROM_URL = "https://github.com/nekuz0r/ki-rom.git"
KI_ROM_COMMIT = "8e4344cfd2b039e2ca97c9f115bb8f1acaff804b"
KI1_IMAGES = {
    "rom-0": (0x32CB0, "ed875b9f2a0d1bccf79ff63189c37488ee718659"),
    "rom-1": (0x51100, None),
    "rom-2": (0x80, None),
}
OFFLINE_IMAGES = {
    "ki1": KI1_IMAGES,
    "ki2": {
        "rom-0": (0x2FA50, "e67a556c22f902a6ab6cc9227f6621933815908a"),
        "rom-1": (0x6AD00, "188dfebe7ec6fa215cae2efd1cdb0c6e258a689d"),
        "rom-2": (0x40, "b43a7c4a9a1810a43ff834903e90e545471fda8c"),
    },
}
def generate_adsp_aot(archive_path: Path, work: Path, game_id: str, game) -> Path:
    """Run UltraCombo's ADSP translator over the selected game's DCS images."""
    with zipfile.ZipFile(archive_path) as archive:
        files = {name.lower(): name for name in archive.namelist()}
        u10 = files.get(game.dcs[0].name.lower())
        if u10 is None:
            raise ValueError(f"{archive_path}: missing DCS ROM {game.dcs[0].name}")
        rom = archive.read(u10)
    if len(rom) != 0x80000:
        raise ValueError(f"{game_id} U10 DCS ROM has unexpected size: {len(rom):#x}")

    images: list[tuple[int, list[int]]] = []
    for source, base, words, boot in game.adsp_images:
        if boot:
            words = (rom[source + 3] + 1) * 8
        words = min(words, 0x400 if boot else 0x800)
        opcodes = []
        for i in range(words):
            at = source + i * 4
            opcodes.append((rom[at] << 16) | (rom[at + 1] << 8) | rom[at + 2])
        images.append((base, opcodes))

    work.mkdir(parents=True, exist_ok=True)
    image_list = work / "adsp_images.bin"
    with image_list.open("wb") as out:
        out.write(len(images).to_bytes(4, "little"))
        for base, opcodes in images:
            out.write(base.to_bytes(2, "little"))
            out.write(len(opcodes).to_bytes(2, "little"))
            for opcode in opcodes:
                out.write(opcode.to_bytes(4, "little"))
    generator = work / "adsprec_build"
    command = [os.environ.get("CC", "cc"), "-O2", "-std=c11",
               "-I", str(ADSP_UPSTREAM),
               str(ROOT / "tools/ki/adsprec_build.c"),
               str(ADSP_UPSTREAM / "adsprec.c"), "-o", str(generator)]
    subprocess.run(command, cwd=ROOT, check=True)
    output = work / "adsp_aot"
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(generator), str(image_list), str(output)], cwd=ROOT, check=True)
    print(f"{game_id.upper()} UltraCombo ADSP AOT: {sum(len(words) for _, words in images)} ROM words -> {output}")
    return output


def find_set_archive(rom_dir: Path, setname: str, explicit: Path | None) -> Path:
    if explicit:
        return explicit.expanduser().resolve()
    if rom_dir.is_file() and rom_dir.suffix.lower() == ".zip":
        return rom_dir.resolve()
    candidates = (rom_dir / f"{setname}.zip", rom_dir / setname / f"{setname}.zip")
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f"cannot find {setname}.zip under {rom_dir}")


def validate_zip(archive_path: Path, game) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        files = {name.lower(): name for name in archive.namelist() if not name.endswith("/")}
        for rom in (game.boot, *game.dcs):
            name = files.get(rom.name.lower())
            if name is None:
                raise ValueError(f"{archive_path}: missing {rom.name}")
            data = archive.read(name)
            crc = zlib.crc32(data) & 0xffffffff
            if len(data) != rom.size or crc != rom.crc32:
                raise ValueError(
                    f"{rom.name}: got size={len(data):#x} CRC={crc:08x}; "
                    f"expected size={rom.size:#x} CRC={rom.crc32:08x}"
                )


def prepare_chd(rom_dir: Path, game, archive: Path, work: Path) -> tuple[Path, str]:
    candidates = (
        rom_dir / game.setname / game.chd,
        archive.parent / game.chd,
        rom_dir / game.chd,
    )
    for path in candidates:
        if path.is_file():
            return path.resolve(), str(rom_dir)

    # Some ROM collections wrap the CHD in a second ZIP under the set folder.
    # Extract the CHD container (not its raw disk contents) into build/ and add
    # that directory to MAME's media search path.
    nested_archives = (
        rom_dir / game.setname / f"{game.setname}.zip",
        archive.parent / game.setname / f"{game.setname}.zip",
    )
    for nested in nested_archives:
        if not nested.is_file():
            continue
        with zipfile.ZipFile(nested) as packed:
            member = next((name for name in packed.namelist()
                           if name.lower().endswith("/" + game.chd.lower())), None)
            if member is None:
                continue
            info = packed.getinfo(member)
            chd_root = work / "mame_chd_roms"
            output = chd_root / game.setname / game.chd
            output.parent.mkdir(parents=True, exist_ok=True)
            if (not output.is_file() or output.stat().st_size != info.file_size
                    or output.stat().st_mtime < nested.stat().st_mtime):
                with packed.open(info) as source, output.open("wb") as target:
                    shutil.copyfileobj(source, target)
            return output.resolve(), f"{rom_dir};{chd_root}"

    raise FileNotFoundError(
        f"missing {game.chd} (or a nested {game.setname}.zip containing it) "
        f"under {rom_dir / game.setname}; expected MAME SHA1 "
        f"{game.chd_sha1}"
    )


def lua_quote(path: Path) -> str:
    return '"' + str(path.resolve()).replace("\\", "\\\\").replace('"', '\\"') + '"'


def capture_ram(mame: str, rom_path: str, game, work: Path, frames: int) -> tuple[Path, Path]:
    ram0 = work / "ram0.bin"
    ram2 = work / "ram2.bin"
    for snapshot in (ram0, ram2):
        snapshot.unlink(missing_ok=True)
    diff_dir = work / "diff"
    diff_dir.mkdir(parents=True, exist_ok=True)
    script = work / "capture_ram.lua"
    source = LUA_TEMPLATE.read_text()
    source = source.replace("@@FRAME@@", str(frames))
    source = source.replace("@@RAM0@@", lua_quote(ram0))
    source = source.replace("@@RAM2@@", lua_quote(ram2))
    script.write_text(source)

    command = [
        mame, game.setname, "-rompath", rom_path,
        "-autoboot_script", str(script), "-skip_gameinfo", "-nothrottle",
        "-video", "none", "-sound", "none", "-noplugin", "data",
        "-diff_directory", str(diff_dir),
    ]
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode:
        raise RuntimeError(f"MAME capture exited with status {result.returncode}")
    if not ram0.is_file() or not ram2.is_file():
        raise RuntimeError(
            "MAME did not produce both RAM snapshots; confirm the set boots and "
            f"the Lua API is supported. Expected {ram0} and {ram2}."
        )
    if ram0.stat().st_size != 0x80000 or ram2.stat().st_size != 0x800000:
        raise RuntimeError("MAME produced RAM snapshots with unexpected sizes")
    return ram0, ram2


def ensure_kipack(work: Path, override: Path | None) -> Path:
    if override:
        executable = override.expanduser().resolve()
        if not executable.is_file():
            raise FileNotFoundError(f"kipack executable not found: {executable}")
        return executable

    tools_root = work / "tooldeps"
    checkout = tools_root / "ki-rom"
    executable = checkout / "tools/kipack/kipack"
    git_env = os.environ.copy()
    # This checkout has a global HTTPS->SSH rewrite, but this public
    # build-time dependency is fetched over HTTPS and must not read SSH config.
    git_env["GIT_CONFIG_GLOBAL"] = os.devnull
    tools_root.mkdir(parents=True, exist_ok=True)
    if not (checkout / ".git").is_dir():
        subprocess.run(["git", "clone", "--no-checkout", KI_ROM_URL, str(checkout)],
                       cwd=ROOT, check=True, env=git_env)
    subprocess.run(["git", "-C", str(checkout), "fetch", "--depth", "1", "origin", KI_ROM_COMMIT],
                   cwd=ROOT, check=True, env=git_env)
    subprocess.run(["git", "-C", str(checkout), "checkout", "--detach", KI_ROM_COMMIT],
                   cwd=ROOT, check=True, env=git_env)
    subprocess.run(["make", "-s", "-C", str(checkout / "tools/kipack"),
                    f"GCC={os.environ.get('CC', 'cc')}"], cwd=ROOT, check=True)
    if not executable.is_file():
        raise FileNotFoundError(f"kipack build did not produce {executable}")
    return executable


def offline_snapshot(archive_path: Path, work: Path, game_id: str, game,
                     kipack_override: Path | None) -> tuple[Path, Path]:
    """Decode game images from U98 with ki-rom/kipack, without booting MAME."""
    with zipfile.ZipFile(archive_path) as archive:
        files = {name.lower(): name for name in archive.namelist()}
        member = files[game.boot.name.lower()]
        bootrom = work / game.boot.name
        bootrom.write_bytes(archive.read(member))

    kipack = ensure_kipack(work, kipack_override)
    image_dir = work / "bootrom_images"
    image_dir.mkdir(parents=True, exist_ok=True)
    variant = game_id
    subprocess.run([str(kipack), "unpack", "-t", variant,
                    str(bootrom), str(image_dir)], cwd=ROOT, check=True)

    ram0 = bytearray(0x80000)
    ram2 = bytearray(0x800000)
    for stem, (expected_size, expected_sha1) in OFFLINE_IMAGES[game_id].items():
        data_path = image_dir / f"{stem}.bin"
        addr_path = image_dir / f"{stem}.addr"
        data = data_path.read_bytes()
        if len(data) != expected_size:
            raise ValueError(f"{data_path}: expected {expected_size:#x} bytes, got {len(data):#x}")
        if expected_sha1 and hashlib.sha1(data).hexdigest() != expected_sha1:
            raise ValueError(f"{data_path}: unexpected SHA1; check the selected boot ROM")
        address_bytes = addr_path.read_bytes()
        if len(address_bytes) != 4:
            raise ValueError(f"{addr_path}: expected a 32-bit physical load address")
        physical = int.from_bytes(address_bytes, "little")
        start = physical - 0x08000000
        end = start + len(data)
        if start < 0 or end > len(ram2):
            raise ValueError(f"{stem} load address {physical:#010x} is outside Seattle DRAM")
        ram2[start:end] = data

    ram0_path, ram2_path = work / "ram0.bin", work / "ram2.bin"
    ram0_path.write_bytes(ram0)
    ram2_path.write_bytes(ram2)
    print(f"{game_id.upper()} code images decoded directly from U98 with kipack; MAME boot/capture skipped.")
    return ram0_path, ram2_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, choices=tuple(GAMES))
    parser.add_argument("--rom-dir", type=Path,
                        default=Path.home() / "git/mame/roms",
                        help="MAME ROM root (default: ~/git/mame/roms)")
    parser.add_argument("--romset", type=Path, help="override the parent set ZIP")
    parser.add_argument("--mame", default="mame", help="MAME executable")
    parser.add_argument("--kipack", type=Path,
                        help="use an existing kipack executable instead of fetching its pinned tool source")
    parser.add_argument("--frames", type=int, default=900,
                        help="MAME frames before a --capture-mame snapshot (default: 900)")
    parser.add_argument("--capture-mame", action="store_true",
                        help="use a live MAME RAM snapshot instead of U98 offline extraction")
    parser.add_argument("--ram0", type=Path, help="use an existing 512 KiB RAM0 snapshot")
    parser.add_argument("--ram2", type=Path, help="use an existing 8 MiB RAM2 snapshot")
    parser.add_argument("--coverage", type=Path,
                        help="optional coverage bitmap from an instrumented MIPS trace")
    parser.add_argument("--out", type=Path,
                        help="generated source directory (default: build/ki/work/<game>/mips_aot)")
    parser.add_argument("--jobs", type=int, default=2,
                        help="reserved for the later compiled runtime stage")
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    if (args.ram0 is None) != (args.ram2 is None):
        parser.error("--ram0 and --ram2 must be supplied together")
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    game = GAMES[args.game]
    rom_dir = args.rom_dir.expanduser().resolve()
    try:
        archive = find_set_archive(rom_dir, game.setname, args.romset)
        rom_root = archive.parent if args.romset or not rom_dir.is_dir() else rom_dir
        validate_zip(archive, game)
        work = ROOT / "build/ki/work" / args.game
        work.mkdir(parents=True, exist_ok=True)
        chd, mame_rom_path = prepare_chd(rom_root, game, archive, work)
        print(f"KI set {game.setname}: ROM ZIP={archive}; CHD={chd}")
        if args.ram0:
            ram0, ram2 = args.ram0.expanduser().resolve(), args.ram2.expanduser().resolve()
        elif args.capture_mame:
            mame = shutil.which(args.mame)
            if not mame:
                raise FileNotFoundError(f"MAME executable not found: {args.mame}")
            ram0, ram2 = capture_ram(mame, mame_rom_path, game, work, args.frames)
        else:
            ram0, ram2 = offline_snapshot(archive, work, args.game, game, args.kipack)

        out = args.out.expanduser().resolve() if args.out else work / "mips_aot"
        command = [sys.executable, str(MIPS_RECOMPILER), "--game", args.game,
                   "--ram0", str(ram0), "--ram2", str(ram2), "--out", str(out)]
        if args.coverage:
            command.extend(["--coverage", str(args.coverage.expanduser().resolve())])
        print("+", " ".join(command), flush=True)
        status = subprocess.run(command, cwd=ROOT).returncode
        if status != 0:
            return status
        generate_adsp_aot(archive, work, args.game, game)
        return 0
    except (OSError, zipfile.BadZipFile, ValueError, RuntimeError,
            subprocess.CalledProcessError) as exc:
        print(f"ki-build: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
