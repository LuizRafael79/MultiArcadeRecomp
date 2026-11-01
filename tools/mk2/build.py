#!/usr/bin/env python3
"""Assemble MAME's MK2 parent ROM set and generate TMS/ADSP AOT sources."""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import sys
import zipfile
import zlib

PROGRAM = (
    ("l3.1_mortal_kombat_ii_game_rom_uj12.uj12", 0, 0, 0x80000, 0xcf100a75),
    ("l3.1_mortal_kombat_ii_game_rom_ug12.ug12", 0, 1, 0x80000, 0x582c7dfd),
)
GRAPHICS = (
    ("l1_mortal_kombat_ii_game_rom_ug14.ug14", 0x000000, 0, 0x100000, 0x01e73af6),
    ("l1_mortal_kombat_ii_game_rom_uj14.uj14", 0x000000, 1, 0x100000, 0xd4985cbb),
    ("l1_mortal_kombat_ii_game_rom_ug19.ug19", 0x000000, 2, 0x100000, 0xfec137be),
    ("l1_mortal_kombat_ii_game_rom_uj19.uj19", 0x000000, 3, 0x100000, 0x2d763156),
    ("l1_mortal_kombat_ii_game_rom_ug16.ug16", 0x400000, 0, 0x100000, 0x8ba6ae18),
    ("l1_mortal_kombat_ii_game_rom_uj16.uj16", 0x400000, 1, 0x100000, 0x39d885b4),
    ("l1_mortal_kombat_ii_game_rom_ug20.ug20", 0x400000, 2, 0x100000, 0x809118c1),
    ("l1_mortal_kombat_ii_game_rom_uj20.uj20", 0x400000, 3, 0x100000, 0xb96824f0),
    ("l1_mortal_kombat_ii_game_rom_ug17.ug17", 0x800000, 0, 0x100000, 0x937d8620),
    ("l1_mortal_kombat_ii_game_rom_uj17.uj17", 0x800000, 1, 0x100000, 0x218de160),
    ("l1_mortal_kombat_ii_game_rom_ug22.ug22", 0x800000, 2, 0x100000, 0x154d53b1),
    ("l1_mortal_kombat_ii_game_rom_uj22.uj22", 0x800000, 3, 0x100000, 0x8891d785),
)
SOUND = (
    ("l1_mortal_kombat_ii_sound_rom_u2.u2", 0x000000, 0x100000, 0x5f23d71d),
    ("l1_mortal_kombat_ii_sound_rom_u3.u3", 0x200000, 0x300000, 0xd6d92bf9),
    ("l1_mortal_kombat_ii_sound_rom_u4.u4", 0x400000, 0x500000, 0xeebc8e0f),
    ("l1_mortal_kombat_ii_sound_rom_u5.u5", 0x600000, 0x700000, 0x2b0b7961),
    ("l1_mortal_kombat_ii_sound_rom_u6.u6", 0x800000, 0x900000, 0xf694b27f),
    ("l1_mortal_kombat_ii_sound_rom_u7.u7", 0xa00000, 0xb00000, 0x20387e0a),
)


def verified(archive: zipfile.ZipFile, name: str, size: int, crc: int) -> bytes:
    members = {item.lower(): item for item in archive.namelist()}
    actual_name = members.get(name.lower())
    if actual_name is None:
        raise ValueError(f"{archive.filename}: missing ROM {name}")
    payload = archive.read(actual_name)
    actual_crc = zlib.crc32(payload) & 0xffffffff
    if len(payload) != size or actual_crc != crc:
        raise ValueError(f"{name}: got {len(payload):#x}/{actual_crc:08x}, expected {size:#x}/{crc:08x}")
    return payload


def interleave(archive: zipfile.ZipFile, entries: tuple, region_size: int,
               lane_width: int) -> bytearray:
    region = bytearray([0xff]) * region_size
    for name, base, lane, size, crc in entries:
        payload = verified(archive, name, size, crc)
        end = base + lane + (size - 1) * lane_width + 1
        if end > region_size:
            raise ValueError(f"{name}: ROM load exceeds region")
        region[base + lane:end:lane_width] = payload
    return region


def import_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load compiler {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def decode_dcs_boot(sound_region: bytes) -> bytes:
    # MAME DCS 2K boot: low byte of 0x1000 16-bit words, 4-byte records,
    # first three bytes are an ADSP opcode and byte 3 gives page length.
    page = bytes(sound_region[i * 2] for i in range(0x1000))
    instruction_count = (page[3] + 1) * 8
    if instruction_count * 4 > len(page):
        raise ValueError(f"invalid DCS boot page length byte {page[3]:#04x}")
    decoded = bytearray()
    for index in range(instruction_count):
        decoded.extend(page[index * 4:index * 4 + 3])
    return bytes(decoded)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romset", type=Path, default=Path("roms/mk2.zip"),
                        help="MAME mk2.zip parent set (MK2 rev L3.1)")
    parser.add_argument("--out", type=Path, default=Path("build/mk2/work"))
    parser.add_argument("--max-tms-instructions", type=int, default=300000)
    parser.add_argument("--entry-bits", action="append", type=lambda x: int(x, 0), default=[])
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.romset) as archive:
            main_region = interleave(archive, PROGRAM, 0x100000, 2)
            graphics = interleave(archive, GRAPHICS, 0xc00000, 4)
            sound = bytearray([0xff]) * 0xc00000
            for name, first, second, crc in SOUND:
                payload = verified(archive, name, 0x80000, crc)
                for base in (first, second):
                    end = base + (len(payload) - 1) * 2 + 1
                    sound[base:end:2] = payload
    except (OSError, zipfile.BadZipFile, ValueError) as exc:
        print(f"mk2-build: {exc}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    for name, data in (("maincpu.bin", main_region), ("video.bin", graphics),
                       ("dcs_sound.bin", sound)):
        (args.out / name).write_bytes(data)

    tms = import_module("mk2_tms_recomp", Path(__file__).parents[1] / "tms34010" / "recomp.py")
    rom = tms.Rom(bytes(main_region), 0x1f800000, 0x800000)
    entry = rom.long(0xffffffe0) & 0xfffffff0
    roots = list(dict.fromkeys([entry, *args.entry_bits]))
    blocks, counts, unmapped = tms.compile_reachable(rom, roots, 128,
                                                       args.max_tms_instructions)
    tms_source = args.out / "tms34010_aot.cpp"
    tms_source.write_text(tms.emit_source(blocks, entry), encoding="utf-8")

    decoded = decode_dcs_boot(bytes(sound))
    decoded_path = args.out / "adsp2105_boot_program.bin"
    decoded_path.write_bytes(decoded)
    adsp = import_module("mk2_adsp_recomp", Path(__file__).parents[1] / "adsp2105" / "recomp.py")
    adsp_source = args.out / "adsp2105_aot.cpp"
    adsp.emit_aot(decoded, adsp_source, entry_pc=0)

    translated = sum(len(block.instructions) for block in blocks.values())
    print(f"MK2 regions: maincpu={len(main_region):#x} graphics={len(graphics):#x} DCS={len(sound):#x}")
    print(f"TMS34010 entry={entry:#010x} roots={len(roots)} translated={translated} blocks={len(blocks)} unmapped={len(unmapped)} AOT={tms_source}")
    print(f"DCS boot program={len(decoded)//3} ADSP words, AOT={adsp_source}")
    print(f"artifacts: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
