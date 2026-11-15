#!/usr/bin/env python3
"""Apply the Golden Axe PC Edition BPS chip patches to a goldnaxeud ZIP."""

from __future__ import annotations

import copy
import hashlib
import struct
import sys
import zlib
from pathlib import Path
from zipfile import ZipFile


PATCH_DIR = Path(__file__).resolve().parent / "patches"
CHIPS = {
    "bootleg_epr-12542.ic1": {
        "source_sha1": "46515ff47db08cd7d01c1857c1533bb278bdd89a",
        "target_sha1": "60535ef8b2751161ff6082769d4fa889295552d1",
        "size": 0x40000,
    },
    "bootleg_epr-12543.ic2": {
        "source_sha1": "4e46c1a84f4d599e9f43b3009f24a2856e24615d",
        "target_sha1": "f3a8063732179efb4fb85272d1a185f1bcdca705",
        "size": 0x40000,
    },
}


def bps_number(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    shift = 1
    while True:
        if pos >= len(data):
            raise ValueError("truncated BPS number")
        byte = data[pos]
        pos += 1
        value += (byte & 0x7F) * shift
        if byte & 0x80:
            return value, pos
        shift <<= 7
        value += shift


def bps_signed(data: bytes, pos: int) -> tuple[int, int]:
    encoded, pos = bps_number(data, pos)
    magnitude = encoded >> 1
    return (-magnitude if encoded & 1 else magnitude), pos


def apply_bps(source: bytes, patch: bytes) -> bytes:
    if len(patch) < 16 or patch[:4] != b"BPS1":
        raise ValueError("not a BPS1 patch")
    stored_patch_crc = struct.unpack_from("<I", patch, len(patch) - 4)[0]
    if zlib.crc32(patch[:-4]) & 0xFFFFFFFF != stored_patch_crc:
        raise ValueError("BPS patch CRC mismatch")

    pos = 4
    source_size, pos = bps_number(patch, pos)
    target_size, pos = bps_number(patch, pos)
    metadata_size, pos = bps_number(patch, pos)
    if source_size != len(source):
        raise ValueError(f"BPS source size mismatch: {len(source)} != {source_size}")
    pos += metadata_size
    action_end = len(patch) - 12
    target = bytearray()
    source_relative = 0
    target_relative = 0

    while pos < action_end and len(target) < target_size:
        action, pos = bps_number(patch, pos)
        length = (action >> 2) + 1
        kind = action & 3
        if kind == 0:  # SourceRead: same offset in source and target
            start = len(target)
            target.extend(source[start:start + length])
        elif kind == 1:  # TargetRead: literal bytes in patch
            if pos + length > action_end:
                raise ValueError("truncated BPS TargetRead")
            target.extend(patch[pos:pos + length])
            pos += length
        elif kind == 2:  # SourceCopy
            delta, pos = bps_signed(patch, pos)
            source_relative += delta
            if source_relative < 0 or source_relative + length > len(source):
                raise ValueError("BPS SourceCopy outside source")
            target.extend(source[source_relative:source_relative + length])
            source_relative += length
        else:  # TargetCopy; overlapping copies are permitted
            delta, pos = bps_signed(patch, pos)
            target_relative += delta
            if target_relative < 0 or target_relative >= len(target):
                raise ValueError("BPS TargetCopy outside target")
            for _ in range(length):
                target.append(target[target_relative])
                target_relative += 1

    if pos != action_end or len(target) != target_size:
        raise ValueError("BPS action stream did not end at the declared target size")
    source_crc, target_crc = struct.unpack_from("<II", patch, action_end)
    if zlib.crc32(source) & 0xFFFFFFFF != source_crc:
        raise ValueError("BPS source CRC mismatch")
    if zlib.crc32(target) & 0xFFFFFFFF != target_crc:
        raise ValueError("BPS target CRC mismatch")
    return bytes(target)


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {Path(sys.argv[0]).name} ORIGINAL_GOLDNAXEUD.zip OUTPUT.zip", file=sys.stderr)
        return 2
    source_path = Path(sys.argv[1]).expanduser().resolve()
    output_path = Path(sys.argv[2]).expanduser().resolve()
    if not source_path.is_file():
        raise SystemExit(f"Input archive not found: {source_path}")
    if source_path == output_path:
        raise SystemExit("Refusing to overwrite the original ROM archive")
    if output_path.exists():
        raise SystemExit(f"Output already exists; choose a new path: {output_path}")

    with ZipFile(source_path, "r") as src:
        infos = src.infolist()
        names = [item.filename for item in infos]
        for name, spec in CHIPS.items():
            if names.count(name) != 1:
                raise SystemExit(f"Expected exactly one ZIP member named {name}")
            original = src.read(name)
            if len(original) != spec["size"] or sha1(original) != spec["source_sha1"]:
                raise SystemExit(f"Source chip does not match the expected goldnaxeud ROM: {name}")

        output_path.parent.mkdir(parents=True, exist_ok=True)
        with ZipFile(output_path, "w") as dst:
            dst.comment = src.comment
            for info in infos:
                data = src.read(info)
                if info.filename in CHIPS:
                    patch = (PATCH_DIR / f"{info.filename}.bps").read_bytes()
                    data = apply_bps(data, patch)
                    spec = CHIPS[info.filename]
                    if len(data) != spec["size"] or sha1(data) != spec["target_sha1"]:
                        raise SystemExit(f"Patched chip hash mismatch: {info.filename}")
                dst.writestr(copy.copy(info), data)
    print(f"Created patched archive: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
