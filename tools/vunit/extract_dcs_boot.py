#!/usr/bin/env python3
"""Extract the first DCS 2K ADSP boot stream in MultiArcadeRecomp packed format.

The board's DCS ROM is a 16-bit interleaved region. MAME's DCS boot routine
selects every even byte into a 0x1000-byte transfer buffer, then loads packed
24-bit program words from bytes 0..2 of each four-byte slot. This script
reproduces that extraction for the static boot program only; later banked or
self-modified program words safely remain on the ADSP interpreter path.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def extract(region: bytes) -> bytes:
    if len(region) < 0x2000:
        raise ValueError("DCS sound region is too small for one 0x1000-byte boot bank")
    transfer = region[0:0x2000:2]
    if len(transfer) != 0x1000:
        raise ValueError("could not construct DCS boot transfer buffer")
    count = (transfer[3] + 1) * 8
    packed = bytearray()
    for index in range(min(count, 0x400)):
        start = index * 4
        packed.extend(transfer[start:start + 3])
    return bytes(packed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dcs_rom", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        packed = extract(args.dcs_rom.read_bytes())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(packed)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"DCS boot extraction: {len(packed) // 3} ADSP words, {len(packed)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
