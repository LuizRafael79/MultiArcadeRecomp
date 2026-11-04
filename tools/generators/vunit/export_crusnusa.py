#!/usr/bin/env python3
"""Export the MAME crusnusa parent set into V-Unit byte-addressed regions."""
from __future__ import annotations

import argparse
import pathlib
import struct
import zlib
import zipfile

MAIN = [
    ("v4.5_4-11-95_cruisn_usa_u10_86b3.u10", 0x000000, 0xB623EAB9),
    ("v4.5_4-11-95_cruisn_usa_u11_6d73.u11", 0x000001, 0x2E47EE94),
    ("v4.5_4-11-95_cruisn_usa_u12_4b32.u12", 0x000002, 0xE9197942),
    ("v4.5_4-11-95_cruisn_usa_u13_430e.u13", 0x000003, 0x4A7C434C),
    ("l1_cruisin_u.s.a._game_rom_u14.u14", 0x200000, 0x6A4AE622),
    ("l1_cruisin_u.s.a._game_rom_u15.u15", 0x200001, 0x1A0AD3B7),
    ("l1_cruisin_u.s.a._game_rom_u16.u16", 0x200002, 0x799D4DD6),
    ("l1_cruisin_u.s.a._game_rom_u17.u17", 0x200003, 0x3D68B660),
    ("l1_cruisin_u.s.a._game_rom_u18.u18", 0x400000, 0x9E8193FB),
    ("l1_cruisin_u.s.a._game_rom_u19.u19", 0x400001, 0x0BF60CDE),
    ("l1_cruisin_u.s.a._game_rom_u20.u20", 0x400002, 0xC07F68F0),
    ("l1_cruisin_u.s.a._game_rom_u21.u21", 0x400003, 0xB0264AED),
    ("l1_cruisin_u.s.a._game_rom_u22.u22", 0x600000, 0xAD137193),
    ("l1_cruisin_u.s.a._game_rom_u23.u23", 0x600001, 0x842449B0),
    ("l1_cruisin_u.s.a._game_rom_u24.u24", 0x600002, 0x0B2275BE),
    ("l1_cruisin_u.s.a._game_rom_u25.u25", 0x600003, 0x2B9FE68F),
    ("l1_cruisin_u.s.a._game_rom_u26.u26", 0x800000, 0xAE56B871),
    ("l1_cruisin_u.s.a._game_rom_u27.u27", 0x800001, 0x2D977A8E),
    ("l1_cruisin_u.s.a._game_rom_u28.u28", 0x800002, 0xCFFA5FB1),
    ("l1_cruisin_u.s.a._game_rom_u29.u29", 0x800003, 0xCBE52C60),
]
DCS = [
    ("l1_cruisin_u.s.a._sound_rom_u2.u2", 0x000000, 0xB9338332),
    ("l1_cruisin_u.s.a._sound_rom_u3.u3", 0x200000, 0xCD8325D6),
    ("l1_cruisin_u.s.a._sound_rom_u4.u4", 0x400000, 0xFAB457F3),
    ("l1_cruisin_u.s.a._sound_rom_u5.u5", 0x600000, 0xBECC92F4),
    ("l1_cruisin_u.s.a._sound_rom_u6.u6", 0x800000, 0xA9F915D3),
    ("l1_cruisin_u.s.a._sound_rom_u7.u7", 0xA00000, 0x424F0BBC),
    ("l1_cruisin_u.s.a._sound_rom_u8.u8", 0xC00000, 0x03C28199),
    ("l1_cruisin_u.s.a._sound_rom_u9.u9", 0xE00000, 0x24BA6371),
]


def load_member(archive: zipfile.ZipFile, name: str, crc: int) -> bytes:
    by_lower = {pathlib.PurePosixPath(n).name.lower(): n for n in archive.namelist()}
    member = by_lower.get(name.lower())
    if member is None:
        raise SystemExit(f"missing MAME parent ROM: {name}")
    data = archive.read(member)
    actual = zlib.crc32(data) & 0xFFFFFFFF
    if actual != crc:
        raise SystemExit(f"CRC mismatch for {name}: expected {crc:08x}, got {actual:08x}")
    if len(data) != 0x80000:
        raise SystemExit(f"unexpected size for {name}: {len(data):#x}")
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("zip", type=pathlib.Path)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    main_region = bytearray(0x1000000)
    dcs_region = bytearray([0xFF]) * 0x1000000
    with zipfile.ZipFile(args.zip) as archive:
        for name, offset, crc in MAIN:
            data = load_member(archive, name, crc)
            main_region[offset:offset + len(data) * 4:4] = data
        for name, offset, crc in DCS:
            data = load_member(archive, name, crc)
            dcs_region[offset:offset + len(data) * 2:2] = data
    (args.out_dir / "main.bin").write_bytes(main_region)
    (args.out_dir / "dcs.bin").write_bytes(dcs_region)
    print(f"MAME crusnusa parent regions written to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
