#!/usr/bin/env python3
"""Generate and compile CPS-1 sound Z80 AOT code using MameRecomp's backend.

The other CPS recompiler checkouts are not used as Z80 runtimes or generators
here. Their sound implementation is a reference only. ROM metadata is taken
from MAME's CPS-1 driver.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
SF2CE_ROMS = {
    "s92_09.11a": (0x10000, 0x08F6B60E),
    "s92_18.11c": (0x20000, 0x7F162009),
    "s92_19.12c": (0x20000, 0xBEADE53F),
}
SF2CE_MAIN_ROMS = {
    "s92e_23b.8f": (0x80000, 0x0AAA1A3A),
    "s92_22b.7f": (0x80000, 0x2BBE15ED),
    "s92_21a.6f": (0x80000, 0x925A7877),
}
SF2CE_GFX_ROMS = (
    ("s92-1m.3a", 0x03B0D852), ("s92-3m.5a", 0x840289EC),
    ("s92-2m.4a", 0xCDB5F027), ("s92-4m.6a", 0xE2799472),
    ("s92-5m.7a", 0xBA8A2761), ("s92-7m.9a", 0xE584BFB5),
    ("s92-6m.8a", 0x21E3F87D), ("s92-8m.10a", 0xBEFC47DF),
    ("s92-10m.3c", 0x960687D5), ("s92-12m.5c", 0x978ECD18),
    ("s92-11m.4c", 0xD6EC9A0A), ("s92-13m.6c", 0xED2C67F6),
)
DINO_SOUND_ROM = ("cd_q.5k", 0x20000, 0x605FDB0B)


def dino_opcode_image(sound: bytes) -> bytes:
    """Decode Dino's fixed Kabuki opcode window using MAME's published key."""
    swap_key1, swap_key2, address_key, xor_key = 0x76543210, 0x24601357, 0x4343, 0x43

    def swap_pairs(value: int, pair_mask: int) -> int:
        for pair in range(4):
            if pair_mask & (1 << pair):
                low = 1 << (pair * 2)
                high = low << 1
                value ^= ((value >> 1) & low) | ((value & low) << 1)
        return value & 0xFF

    def bitswap1(value: int, key: int, select: int) -> int:
        mask = sum(((select >> ((key >> (4 * pair)) & 7)) & 1) << pair for pair in range(4))
        return swap_pairs(value, mask)

    def bitswap2(value: int, key: int, select: int) -> int:
        mask = sum(((select >> ((key >> (12 - 4 * pair)) & 7)) & 1) << pair for pair in range(4))
        return swap_pairs(value, mask)

    def rol(value: int) -> int:
        return ((value << 1) | (value >> 7)) & 0xFF

    out = bytearray(0x8000)
    for address, byte in enumerate(sound[:0x8000]):
        select = (address + address_key) & 0xFFFF
        byte = bitswap1(byte, swap_key1 & 0xFFFF, select & 0xFF)
        byte = rol(byte)
        byte = bitswap2(byte, swap_key1 >> 16, select & 0xFF)
        byte = rol(byte ^ xor_key)
        byte = bitswap2(byte, swap_key2 & 0xFFFF, select >> 8)
        byte = rol(byte)
        out[address] = bitswap1(byte, swap_key2 >> 16, select >> 8)
    return bytes(out)


def checked_zip_regions(game: str, rom_zip: Path, work: Path) -> tuple[bytes, bytes | None, bytes | None, bytes | None]:
    with zipfile.ZipFile(rom_zip) as archive:
        if game == "sf2ce":
            loaded = {}
            for name, (size, crc_expected) in SF2CE_ROMS.items():
                data = archive.read(name)
                crc = zlib.crc32(data) & 0xFFFFFFFF
                if len(data) != size or crc != crc_expected:
                    raise SystemExit(f"{name}: expected {size:#x}/{crc_expected:08x}, got {len(data):#x}/{crc:08x}")
                loaded[name] = data
            (work / "oki_samples.bin").write_bytes(loaded["s92_18.11c"] + loaded["s92_19.12c"])
            sound = loaded["s92_09.11a"]
            program = bytearray()
            for name, (size, crc_expected) in SF2CE_MAIN_ROMS.items():
                data = archive.read(name)
                crc = zlib.crc32(data) & 0xFFFFFFFF
                if len(data) != size or crc != crc_expected:
                    raise SystemExit(f"{name}: expected {size:#x}/{crc_expected:08x}, got {len(data):#x}/{crc:08x}")
                # MAME declares ROM_LOAD16_WORD_SWAP for each 68000 lane pair.
                swapped = bytearray(data)
                swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
                program.extend(swapped)
            gfx = bytearray(0x600000)
            for index, (name, crc_expected) in enumerate(SF2CE_GFX_ROMS):
                data = archive.read(name)
                crc = zlib.crc32(data) & 0xFFFFFFFF
                if len(data) != 0x80000 or crc != crc_expected:
                    raise SystemExit(f"{name}: expected 0x80000/{crc_expected:08x}, got {len(data):#x}/{crc:08x}")
                bank = (index // 4) * 0x200000
                lane = (index % 4) * 2
                for offset in range(0, len(data), 2):
                    destination = bank + (offset // 2) * 8 + lane
                    gfx[destination:destination + 2] = data[offset:offset + 2]
            return sound, None, bytes(program), bytes(gfx)

        name, size, crc_expected = DINO_SOUND_ROM
        sound = archive.read(name)
        crc = zlib.crc32(sound) & 0xFFFFFFFF
        if len(sound) != size or crc != crc_expected:
            raise SystemExit(f"{name}: expected {size:#x}/{crc_expected:08x}, got {len(sound):#x}/{crc:08x}")
        return sound, dino_opcode_image(sound), None, None


def generate(game: str, rom_zip: Path, jobs: int, boot_cycles: int, until_video: bool = False) -> int:
    if not rom_zip.is_file():
        raise SystemExit(f"{game} ROM zip not found: {rom_zip}")
    work = ROOT / "build" / "cps1" / "work" / game
    gen = work / "gen" / "z80"
    z80_dynamic_entries = work / "z80_dynamic_entries.txt"
    work.mkdir(parents=True, exist_ok=True)
    try:
        sound, opcodes, program, gfx = checked_zip_regions(game, rom_zip, work)
    except (KeyError, zipfile.BadZipFile) as exc:
        raise SystemExit(f"invalid {game} ROM zip {rom_zip}: {exc}") from exc

    (work / "z80_program.bin").write_bytes(sound[:0x8000])
    (work / "z80_banked.bin").write_bytes(sound[0x8000:])
    if gfx is not None:
        (work / "gfx.bin").write_bytes(gfx)
    if program is not None:
        program_path = work / "program.bin"
        program_path.write_bytes(program)
        initial_pc = int.from_bytes(program[4:8], "big")
        (work / "m68k_entry.txt").write_text(f"initial_pc=0x{initial_pc:06x}\n")
    command = [sys.executable, str(ROOT / "tools/z80/recomp.py"),
               "--program", str(work / "z80_program.bin"), "--out", str(gen),
               "--list", str(work / "z80.lst"), "--precise-cycle-yield"]
    if opcodes is not None:
        opcode_path = work / "z80_opcodes.bin"
        opcode_path.write_bytes(opcodes)
        command.extend(["--opcodes", str(opcode_path)])
    if z80_dynamic_entries.exists():
        command.extend(["--entries", str(z80_dynamic_entries)])
    print(f"Verified {game} sound ROMs against MAME.", flush=True)
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode:
        return result.returncode

    build = ROOT / "build" / "cps1" / "mamerecomp-host"
    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                 f"-DMAMERECOMP_Z80_AOT_GENERATED_DIR={gen}"]
    result = subprocess.run(configure, cwd=ROOT)
    if result.returncode:
        return result.returncode
    if program is not None:
        build_m68k = ["cmake", "--build", str(build), "--target", "m68krecomp", "--parallel", str(jobs)]
        result = subprocess.run(build_m68k, cwd=ROOT)
        if result.returncode:
            return result.returncode
        m68k_gen = work / "gen" / "m68k"
        if m68k_gen.exists():
            shutil.rmtree(m68k_gen)
        m68k_gen.mkdir(parents=True, exist_ok=True)
        dynamic_entries = work / "m68k_dynamic_entries.txt"
        misses = work / "m68k_misses.txt"
        z80_misses = work / "z80_misses.txt"
        z80_fallbacks = work / "z80_fallbacks.log"
        for round_index in range(4):
            command = [str(build / "m68krecomp"), "--raw", str(program_path), "--base", "0",
                       "--entry", hex(initial_pc), "--prefix", game, "--out", str(m68k_gen),
                       "--files", str(jobs), "--no-pointer-scan"]
            if dynamic_entries.exists():
                command.extend(["--entries", str(dynamic_entries)])
            print(f"68000 translation pass {round_index + 1}: ROM seeds plus previously observed entries.", flush=True)
            print("+", " ".join(command), flush=True)
            result = subprocess.run(command, cwd=ROOT)
            if result.returncode:
                return result.returncode
            configure_native = ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                                f"-DMAMERECOMP_Z80_AOT_GENERATED_DIR={gen}",
                                f"-DMAMERECOMP_CPS1_M68K_GENERATED_DIR={m68k_gen}"]
            result = subprocess.run(configure_native, cwd=ROOT)
            if result.returncode:
                return result.returncode
            compile_m68k = ["cmake", "--build", str(build), "--target", "cps1_boot", "--parallel", str(jobs)]
            result = subprocess.run(compile_m68k, cwd=ROOT)
            if result.returncode:
                return result.returncode
            command = [str(build / "cps1_boot"), str(program_path), str(boot_cycles), str(misses),
                       str(work / "z80_program.bin"), str(work / "z80_banked.bin"),
                       str(z80_misses), str(z80_fallbacks), str(work / "gfx.bin"),
                       str(work / "sf2ce-frame.ppm")]
            if until_video:
                command.append("--until-video")
            print("+", " ".join(command), flush=True)
            result = subprocess.run(command, cwd=ROOT)
            if result.returncode:
                return result.returncode
            seen = set(dynamic_entries.read_text().splitlines()) if dynamic_entries.exists() else set()
            observed = set(misses.read_text().splitlines()) if misses.exists() else set()
            new_entries = observed - seen
            if new_entries:
                dynamic_entries.write_text("\n".join(sorted(seen | observed)) + "\n")
                print(f"Observed {len(new_entries)} new ROM entry points; carrying them into the next native pass.", flush=True)
            old_z80_entries = set(z80_dynamic_entries.read_text().splitlines()) if z80_dynamic_entries.exists() else set()
            observed_z80_entries = set(z80_misses.read_text().splitlines()) if z80_misses.exists() else set()
            new_z80_entries = observed_z80_entries - old_z80_entries
            if new_z80_entries:
                z80_dynamic_entries.write_text("\n".join(sorted(old_z80_entries | observed_z80_entries)) + "\n")
                print(f"Z80 runtime discovered {len(new_z80_entries)} new fixed-ROM entries; rebuilding its shared AOT table.", flush=True)
                z80_command = [sys.executable, str(ROOT / "tools/z80/recomp.py"),
                               "--program", str(work / "z80_program.bin"), "--out", str(gen),
                               "--list", str(work / "z80.lst"), "--precise-cycle-yield",
                               "--entries", str(z80_dynamic_entries)]
                if opcodes is not None:
                    z80_command.extend(["--opcodes", str(work / "z80_opcodes.bin")])
                result = subprocess.run(z80_command, cwd=ROOT)
                if result.returncode:
                    return result.returncode
                result = subprocess.run(configure_native, cwd=ROOT)
                if result.returncode:
                    return result.returncode
                result = subprocess.run(compile_m68k, cwd=ROOT)
                if result.returncode:
                    return result.returncode
            if not new_entries and not new_z80_entries:
                print("Both CPU entry-discovery lists reached a fixed point for this run budget.", flush=True)
                break
    compile_native = ["cmake", "--build", str(build), "--target", "mamerecomp_z80_aot", "--parallel", str(jobs)]
    result = subprocess.run(compile_native, cwd=ROOT)
    if result.returncode == 0:
        print(f"Shared native Z80 AOT library compiled: {build / 'libmamerecomp_z80_aot.a'}")
        print("No CPS1Recomp/CPS2Recomp Z80 CPU or interpreter is used in this path.")
        if program is not None:
            print("CPU boot probe runs native 68000 and shared Z80 blocks with Musashi/Z80 interpreter fallbacks; SDL video is opt-in with --window.")
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate CPS-1 Z80 AOT code with the MameRecomp backend")
    parser.add_argument("--game", required=True, choices=("sf2ce", "dino"))
    parser.add_argument("--rom-dir", type=Path, help="ROM zip file")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--boot-cycles", type=int, default=120_000_000,
                        help="68000 cycles per CPS1 entry-discovery boot pass")
    parser.add_argument("--until-video", action="store_true",
                        help="write the PPM only after real non-transparent video appears")
    args = parser.parse_args()
    rom = args.rom_dir.expanduser().resolve() if args.rom_dir else Path(f"/home/luiz/git/mame/roms/{args.game}.zip")
    return generate(args.game, rom, args.jobs, args.boot_cycles, args.until_video)


if __name__ == "__main__":
    raise SystemExit(main())
