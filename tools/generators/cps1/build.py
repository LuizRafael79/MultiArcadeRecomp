#!/usr/bin/env python3
"""Generate and compile CPS-1 sound Z80 AOT code using MameRecomp's backend.

The other CPS recompiler checkouts are not used as Z80 runtimes or generators
here. Their sound implementation is a reference only. ROM metadata is taken
from MAME's CPS-1 driver.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def export_driver_roms(game: str, rom_zip: Path, work: Path, jobs: int) -> tuple[bytes, bytes, bytes, bytes]:
    build = ROOT / "build" / "cps1" / "mamerecomp-host"
    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"]
    result = subprocess.run(configure, cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    result = subprocess.run(["cmake", "--build", str(build), "--target", "cps1_romset_export",
                             "--parallel", str(jobs)], cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    rom_output = work / "roms"
    command = [str(build / "cps1_romset_export"), game, str(rom_zip), str(rom_output)]
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    maincpu = (rom_output / "maincpu.bin").read_bytes()
    gfx = (rom_output / "gfx.bin").read_bytes()
    sound = (rom_output / "audiocpu.bin").read_bytes()
    sample_region = "qsound" if game == "dino" else "oki"
    samples = (rom_output / f"{sample_region}.bin").read_bytes()
    expected_sizes = {"maincpu": 0x180000, "gfx": 0x400000 if game == "dino" else 0x600000,
                      "audiocpu": 0x28000 if game == "dino" else 0x18000,
                      sample_region: 0x200000 if game == "dino" else 0x40000}
    actual_sizes = {"maincpu": len(maincpu), "gfx": len(gfx),
                    "audiocpu": len(sound), sample_region: len(samples)}
    for region, expected in expected_sizes.items():
        if actual_sizes[region] != expected:
            raise SystemExit(f"{game} {region} ROM region has {actual_sizes[region]:#x} bytes; expected {expected:#x}")
    # The region exports remain the runtime's source of truth.
    bank_end = 0x28000 if game == "dino" else 0x18000
    return maincpu, sound[:0x8000], sound[0x10000:bank_end], samples


def kabuki_dino_views(program: bytes) -> tuple[bytes, bytes]:
    """Return MAME dino_decode opcode and data views for the fixed Z80 ROM."""
    def swap1(value: int, key: int, select: int) -> int:
        for bit, pair in ((0, (0, 1)), (4, (2, 3)), (8, (4, 5)), (12, (6, 7))):
            if select & (1 << ((key >> bit) & 7)):
                a, b = pair
                value = (value & ~(1 << a | 1 << b)) | (((value >> a) & 1) << b) | (((value >> b) & 1) << a)
        return value

    def swap2(value: int, key: int, select: int) -> int:
        for bit, pair in ((12, (0, 1)), (8, (2, 3)), (4, (4, 5)), (0, (6, 7))):
            if select & (1 << ((key >> bit) & 7)):
                a, b = pair
                value = (value & ~(1 << a | 1 << b)) | (((value >> a) & 1) << b) | (((value >> b) & 1) << a)
        return value

    def decode(value: int, select: int) -> int:
        value = swap1(value, 0x76543210 & 0xffff, select & 0xff)
        value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7)
        value = swap2(value, 0x76543210 >> 16, select & 0xff)
        value ^= 0x43
        value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7)
        value = swap2(value, 0x24601357 & 0xffff, select >> 8)
        value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7)
        return swap1(value, 0x24601357 >> 16, select >> 8)

    if len(program) < 0x8000:
        raise SystemExit("dino Kabuki fixed Z80 ROM is smaller than 0x8000 bytes")
    opcodes = bytes(decode(program[address], address + 0x4343) for address in range(0x8000))
    data = bytes(decode(program[address], ((address ^ 0x1FC0) + 0x4343 + 1) & 0xFFFF)
                 for address in range(0x8000))
    return opcodes, data


def generate(game: str, rom_zip: Path, jobs: int, boot_cycles: int, until_video: bool = False,
    ) -> int:
    if not rom_zip.is_file():
        raise SystemExit(f"{game} ROM zip not found: {rom_zip}")
    work = ROOT / "build" / "cps1" / "work" / game
    build = ROOT / "build" / "cps1" / "mamerecomp-host"
    gen = work / "gen" / "z80"
    z80_dynamic_entries = work / "z80_dynamic_entries.txt"
    work.mkdir(parents=True, exist_ok=True)
    program, fixed_sound, banked_sound, samples = export_driver_roms(game, rom_zip, work, jobs)
    (work / "z80_program.bin").write_bytes(fixed_sound)
    (work / "z80_banked.bin").write_bytes(banked_sound)
    program_path = work / "program.bin"
    program_path.write_bytes(program)
    initial_pc = int.from_bytes(program[4:8], "big")
    (work / "m68k_entry.txt").write_text(f"initial_pc=0x{initial_pc:06x}\n")
    z80_args = ["-DMAMERECOMP_Z80_AOT_GENERATED_DIR="]
    z80_command = [sys.executable, str(ROOT / "tools/z80/recomp.py"),
                   "--program", str(work / "z80_program.bin"), "--out", str(gen),
                   "--list", str(work / "z80.lst"), "--precise-cycle-yield"]
    if game == "dino":
        opcodes_path = work / "z80_opcodes.bin"
        opcodes, data = kabuki_dino_views((work / "roms" / "audiocpu.bin").read_bytes())
        opcodes_path.write_bytes(opcodes)
        data_path = work / "z80_data.bin"
        data_path.write_bytes(data)
        z80_command.extend(["--opcodes", str(opcodes_path)])
        z80_command.extend(["--data", str(data_path)])
    if z80_dynamic_entries.exists():
        z80_command.extend(["--entries", str(z80_dynamic_entries)])
    result = subprocess.run(z80_command, cwd=ROOT)
    if result.returncode:
        return result.returncode
    z80_args = [f"-DMAMERECOMP_Z80_AOT_GENERATED_DIR={gen}"]
    print(f"Verified {game} sound ROMs against MAME.", flush=True)

    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                 *z80_args]
    result = subprocess.run(configure, cwd=ROOT)
    if result.returncode:
        return result.returncode
    if program:
        build_m68k = ["cmake", "--build", str(build), "--target", "m68krecomp", "--parallel", str(jobs)]
        result = subprocess.run(build_m68k, cwd=ROOT)
        if result.returncode:
            return result.returncode
        m68k_gen = work / "gen" / "m68k"
        if m68k_gen.exists():
            import shutil
            shutil.rmtree(m68k_gen)
        m68k_gen.mkdir(parents=True, exist_ok=True)
        dynamic_entries = work / "m68k_dynamic_entries.txt"
        ram_entries = work / "m68k_ram_entries.txt"
        ram_snapshot = work / "m68k_gfxram_snapshot.bin"
        misses = work / "m68k_misses.txt"
        z80_misses = work / "z80_misses.txt"
        z80_fallbacks = work / "z80_fallbacks.log"
        for round_index in range(4):
            # Keep generated C translation units small enough for cc1 on large
            # CPS-1 ROMs (notably dino), independently of compile parallelism.
            # `jobs` limits concurrent compiler processes; it must not also
            # force a giant number of instructions into each source file.
            m68k_source_files = max(8, jobs * 4)
            command = [str(build / "m68krecomp"), "--raw", str(program_path), "--base", "0",
                       "--entry", hex(initial_pc), "--prefix", game, "--out", str(m68k_gen),
                       "--files", str(m68k_source_files), "--board", "cps1"]
            if dynamic_entries.exists():
                command.extend(["--entries", str(dynamic_entries)])
            ram_roots = [line.strip() for line in ram_entries.read_text().splitlines()
                         if line.strip()] if ram_entries.exists() else []
            snapshot_before = ram_snapshot.read_bytes() if ram_snapshot.exists() else None
            if ram_roots and snapshot_before is not None:
                command.extend(["--region", "0x900000", str(ram_snapshot),
                                "--entries", str(ram_entries)])
            print(f"68000 translation pass {round_index + 1}: ROM seeds plus previously observed entries.", flush=True)
            print("+", " ".join(command), flush=True)
            result = subprocess.run(command, cwd=ROOT)
            if result.returncode:
                return result.returncode
            configure_native = ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                                *z80_args, f"-DMAMERECOMP_CPS1_M68K_GENERATED_DIR={m68k_gen}",
                                f"-DMAMERECOMP_CPS1_GAME_ID={game}"]
            result = subprocess.run(configure_native, cwd=ROOT)
            if result.returncode:
                return result.returncode
            compile_m68k = ["cmake", "--build", str(build), "--target", "cps1_boot", "--parallel", str(jobs)]
            result = subprocess.run(compile_m68k, cwd=ROOT)
            if result.returncode:
                return result.returncode
            command = [str(build / f"mamerecomp_{game}"), str(program_path), str(boot_cycles), str(misses),
                       str(work / "z80_program.bin"), str(work / "z80_banked.bin"),
                       str(z80_misses), str(z80_fallbacks), str(work / "roms" / "gfx.bin"),
                       str(work / f"{game}-frame.ppm"),
                       "--m68k-ram-entries", str(work / "m68k_ram_entries.txt"),
                       "--gfxram-snapshot", str(work / "m68k_gfxram_snapshot.bin")]
            if game == "dino":
                command.extend(["--qsound-program", str(work / "roms" / "audiocpu.bin"),
                                "--qsound-samples", str(work / "roms" / "qsound.bin"),
                                "--eeprom", str(work / "dino-eeprom.bin")])
            else:
                command.extend(["--oki", str(work / "roms" / "oki.bin")])
            if until_video:
                command.extend(["--until-video"])
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
            observed_ram = set(ram_entries.read_text().splitlines()) if ram_entries.exists() else set()
            new_ram_entries = observed_ram - set(ram_roots)
            snapshot_after = ram_snapshot.read_bytes() if ram_snapshot.exists() else None
            ram_snapshot_changed = snapshot_before != snapshot_after
            if new_ram_entries:
                print(f"Observed {len(new_ram_entries)} new executable gfx-RAM entry points; translating from a captured snapshot next pass.", flush=True)
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
                if game == "dino":
                    z80_command.extend(["--opcodes", str(work / "z80_opcodes.bin"),
                                        "--data", str(work / "z80_data.bin")])
                result = subprocess.run(z80_command, cwd=ROOT)
                if result.returncode:
                    return result.returncode
                result = subprocess.run(configure_native, cwd=ROOT)
                if result.returncode:
                    return result.returncode
                result = subprocess.run(compile_m68k, cwd=ROOT)
                if result.returncode:
                    return result.returncode
            if not new_entries and not new_z80_entries and not new_ram_entries and not ram_snapshot_changed:
                print("Both CPU entry-discovery lists reached a fixed point for this run budget.", flush=True)
                break
    compile_native = ["cmake", "--build", str(build), "--target", "mamerecomp_z80_aot", "--parallel", str(jobs)]
    result = subprocess.run(compile_native, cwd=ROOT)
    if result.returncode == 0:
        print(f"Shared native Z80 AOT library compiled: {build / 'libmamerecomp_z80_aot.a'}")
        print("The sound CPU uses MameRecomp's shared Z80 AOT runtime.")
    if result.returncode == 0 and program:
        board_sound = "QSound" if game == "dino" else "YM2151/OKI"
        print(f"CPU boot probe uses native 68000 and {board_sound}; --window enables live video and audio output.")
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
