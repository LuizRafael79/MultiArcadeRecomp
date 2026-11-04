#!/usr/bin/env python3
"""Assemble MAME's Mortal Kombat rev 5 T-Unit regions and run TMS34010 AOT."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import sys
import zipfile
import zlib


PROGRAM_ROM_LANES = (
    ("l5_mortal_kombat_t-unit_uj12_game_rom.uj12", 0, 0, 0x80000, 0xf4990bf2),
    ("l5_mortal_kombat_t-unit_ug12_game_rom.ug12", 0, 1, 0x80000, 0xb06aeac1),
)
GRAPHICS_ROM_LANES = (
    ("l1_mortal_kombat_t-unit_ug14_game_rom.ug14", 0x000000, 0, 0x80000, 0x9e00834e),
    ("l1_mortal_kombat_t-unit_uj14_game_rom.uj14", 0x000000, 1, 0x80000, 0xf4b0aaa7),
    ("l1_mortal_kombat_t-unit_ug19_game_rom.ug19", 0x000000, 2, 0x80000, 0x2d8c7ba1),
    ("l1_mortal_kombat_t-unit_uj19_game_rom.uj19", 0x000000, 3, 0x80000, 0x33b9b7a4),
    ("l1_mortal_kombat_t-unit_ug16_game_rom.ug16", 0x200000, 0, 0x80000, 0x52c9d1e5),
    ("l1_mortal_kombat_t-unit_uj16_game_rom.uj16", 0x200000, 1, 0x80000, 0xc94c58cf),
    ("l1_mortal_kombat_t-unit_ug20_game_rom.ug20", 0x200000, 2, 0x80000, 0x2f7e55d3),
    ("l1_mortal_kombat_t-unit_uj20_game_rom.uj20", 0x200000, 3, 0x80000, 0xeae96df0),
    ("l1_mortal_kombat_t-unit_ug17_game_rom.ug17", 0x400000, 0, 0x80000, 0xe34fe253),
    ("l1_mortal_kombat_t-unit_uj17_game_rom.uj17", 0x400000, 1, 0x80000, 0xa56e12f5),
    ("l1_mortal_kombat_t-unit_ug22_game_rom.ug22", 0x400000, 2, 0x80000, 0xb537bb4e),
    ("l1_mortal_kombat_t-unit_uj22_game_rom.uj22", 0x400000, 3, 0x80000, 0x5e12523b),
)
SOUND_CPU_ROM = ("sl1_mortal_kombat_u3_sound_rom.u3", 0x40000, 0xc615844c)
# Observed at runtime as the target of an indexed JSR in the fixed sound ROM.
# It is not reachable from reset/vector control flow, so seed it explicitly for
# AOT discovery until dynamic-call target discovery is part of the compiler.
SOUND_OBSERVED_ENTRIES = (0xf69b, 0xf752, 0xfcde, 0xfd09, 0xf6fe, 0xf7f6,
                          0xde7d)
OKI_ROMS = (
    ("sl1_mortal_kombat_u12_sound_rom.u12", 0x40000, 0x258bd7f9),
    ("sl1_mortal_kombat_u13_sound_rom.u13", 0x40000, 0x7b7ec3b6),
)


def read_verified(archive: zipfile.ZipFile, name: str, size: int,
                  crc: int) -> bytes:
    names = {item.lower(): item for item in archive.namelist()}
    actual_name = names.get(name.lower())
    if actual_name is None:
        raise ValueError(f"{archive.filename}: missing MAME ROM {name}")
    payload = archive.read(actual_name)
    actual_crc = zlib.crc32(payload) & 0xffffffff
    if len(payload) != size or actual_crc != crc:
        raise ValueError(
            f"{name}: got size={len(payload):#x} CRC={actual_crc:08x}; "
            f"expected size={size:#x} CRC={crc:08x}")
    return payload


def interleave_lanes(archive: zipfile.ZipFile, roms: tuple,
                     region_size: int, lane_width: int) -> bytearray:
    region = bytearray(region_size)
    for name, base, lane, size, crc in roms:
        payload = read_verified(archive, name, size, crc)
        start = base + lane
        end = start + (size - 1) * lane_width + 1
        if end > len(region):
            raise ValueError(f"{name}: lane load exceeds its MAME region")
        region[start:end:lane_width] = payload
    return region


def load_recompiler():
    path = Path(__file__).resolve().parents[1] / "tms34010" / "recomp.py"
    spec = importlib.util.spec_from_file_location("mamerecomp_tms34010_recomp", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load TMS34010 compiler at {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def load_m6809_recompiler():
    path = Path(__file__).resolve().parents[1] / "m6809" / "recomp.py"
    spec = importlib.util.spec_from_file_location("mamerecomp_m6809_recomp", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load MC6809 compiler at {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romset", type=Path, default=Path("roms/mk.zip"),
                        help="MAME mk.zip, revision 5 T-Unit parent set")
    parser.add_argument("--out", type=Path, default=Path("build/mk/work"),
                        help="output directory for assembled ROM regions and AOT")
    parser.add_argument("--max-instructions", type=int, default=250000)
    parser.add_argument("--entry-bits", action="append", type=lambda x: int(x, 0),
                        default=[], metavar="PC",
                        help="extra observed TMS34010 bit-address entry (repeatable)")
    parser.add_argument("--sound-entry", action="append", type=lambda x: int(x, 0),
                        default=[], metavar="PC",
                        help="extra observed MC6809E address entry (repeatable)")
    parser.add_argument("--entry-list", type=Path,
                        default=Path(__file__).resolve().with_name("aot_entries_mk.txt"),
                        help="text file with observed TMS34010 bit-address PCs "
                             f"(default: {Path(__file__).resolve().with_name('aot_entries_mk.txt')})")
    args = parser.parse_args()

    try:
        with zipfile.ZipFile(args.romset) as archive:
            program = interleave_lanes(archive, PROGRAM_ROM_LANES, 0x100000, 2)
            graphics = interleave_lanes(archive, GRAPHICS_ROM_LANES, 0xc00000, 4)
            sound_name, sound_size, sound_crc = SOUND_CPU_ROM
            sound = bytearray([0xff]) * 0x50000
            sound[0x10000:0x10000 + sound_size] = read_verified(
                archive, sound_name, sound_size, sound_crc)
            oki = bytearray([0xff]) * 0x100000
            for (name, size, crc), destination in zip(OKI_ROMS, (0x00000, 0x80000)):
                payload = read_verified(archive, name, size, crc)
                oki[destination:destination + size] = payload
                oki[destination + size:destination + size * 2] = payload
    except (OSError, zipfile.BadZipFile, ValueError) as exc:
        print(f"mk-build: {exc}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    regions = {
        "maincpu.bin": program,
        "video.bin": graphics,
        "soundcpu.bin": sound,
        "oki.bin": oki,
    }
    for filename, payload in regions.items():
        (args.out / filename).write_bytes(payload)

    sound_recomp = load_m6809_recompiler()
    sound_rom = sound_recomp.SoundRom(bytes(sound))
    sound_reset = sound_rom.word(0xfffe, 0)
    sound_vector_targets = [
        sound_rom.word(vector, 0)
        for vector in (0xfff2, 0xfff4, 0xfff6, 0xfff8, 0xfffa, 0xfffc)
    ]
    sound_entries = list(dict.fromkeys([
        sound_reset,
        *(target for target in sound_vector_targets if 0x4000 <= target <= 0xffff),
        *SOUND_OBSERVED_ENTRIES,
        *args.sound_entry,
    ]))
    sound_by_bank = {
        bank: sound_recomp.translate(sound_rom, sound_entries, bank)
        for bank in range(8)
    }
    sound_aot = args.out / "mc6809e_aot.cpp"
    sound_aot.write_text(
        sound_recomp.emit_source(sound_by_bank, sound_reset), encoding="utf-8")
    sound_inventory = args.out / "mc6809e_opcode_inventory.txt"
    inventory_lines = [
        "MC6809E AOT discovery inventory",
        "Generated from the selected ROM set by tools/m6809/recomp.py.",
        "Each mnemonic lists the statically discovered PCs in each ROM bank.",
        "",
    ]
    for bank, instructions in sorted(sound_by_bank.items()):
        inventory_lines.append(f"bank {bank:#x}:")
        by_mnemonic: dict[str, list[int]] = {}
        for pc, instruction in sorted(instructions.items()):
            by_mnemonic.setdefault(instruction.opcode, []).append(pc)
        for mnemonic, pcs in sorted(by_mnemonic.items()):
            rendered = " ".join(f"{pc:04x}" for pc in pcs)
            inventory_lines.append(
                f"  {mnemonic}: {len(pcs)} site(s) [{rendered}]")
        inventory_lines.append("")
    sound_inventory.write_text("\n".join(inventory_lines), encoding="utf-8")

    recomp = load_recompiler()
    rom = recomp.Rom(bytes(program), 0x1f800000, 0x800000)
    entry = rom.long(0xffffffE0) & 0xfffffff0
    listed_roots = []
    if args.entry_list:
        try:
            for line_number, line in enumerate(
                    args.entry_list.read_text(encoding="utf-8").splitlines(), 1):
                value = line.split("#", 1)[0].strip()
                if value:
                    try:
                        listed_roots.append(int(value, 0))
                    except ValueError as exc:
                        raise ValueError(
                            f"{args.entry_list}:{line_number}: invalid PC {value!r}") from exc
        except OSError as exc:
            print(f"mk-build: cannot read entry list {args.entry_list}: {exc}",
                  file=sys.stderr)
            return 2
    roots = list(dict.fromkeys([entry, *listed_roots, *args.entry_bits]))
    blocks, counts, unmapped = recomp.compile_reachable(
        rom, roots, 128, args.max_instructions)
    source = recomp.emit_source(blocks, entry)
    (args.out / "tms34010_aot.cpp").write_text(source, encoding="utf-8")
    translated = sum(len(block.instructions) for block in blocks.values())
    dynamic = [(pc, name) for block in blocks.values()
               for pc, name in block.dynamic_exits]
    print(f"MAME mk rev 5 regions verified: maincpu={len(program):#x}, "
          f"video={len(graphics):#x}, soundcpu={len(sound):#x}, oki={len(oki):#x}")
    print(f"TMS34010 entry={entry:#010x}, roots={len(roots)}, discovered={translated} instructions "
          f"in {len(blocks)} blocks, opcode forms={len(counts)}, "
          f"computed-PC exits={len(dynamic)}, unmapped={len(unmapped)}")
    print(f"generated artifacts: {args.out}")
    sound_instruction_count = sum(len(items) for items in sound_by_bank.values())
    sound_opcode_forms = len({
        instruction.opcode
        for instructions in sound_by_bank.values()
        for instruction in instructions.values()
    })
    print(f"MC6809E reset={sound_reset:#06x}, banks=8, discovered native instructions="
          f"{sound_instruction_count}, opcode forms={sound_opcode_forms}, "
          f"AOT source={sound_aot}, inventory={sound_inventory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
