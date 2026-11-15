#!/usr/bin/env python3
"""Report the first differing triggered DMA register snapshot."""
import argparse
from pathlib import Path


def load(path: Path):
    records = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        values = line.split()
        if len(values) < 16:
            raise ValueError(f"{path}:{number}: expected at least 16 columns, found {len(values)}")
        try:
            fields = [int(value, 0) for value in values[1:16]]
            metadata = values[16:]
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid DMA field: {exc}") from exc
        # Sequence numbering is emitted as decimal by MAME and historically
        # as hexadecimal by MultiArcadeRecomp. Compare stream position instead.
        records.append((values[0], fields, metadata))
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mame", type=Path)
    parser.add_argument("recomp", type=Path)
    args = parser.parse_args()
    try:
        mame, recomp = load(args.mame), load(args.recomp)
    except (OSError, ValueError) as exc:
        print(f"DMA trace error: {exc}")
        return 2
    columns = ("cmd", "config", "source", "x", "y", "width", "height",
               "palette", "color", "xstep", "ystep", "r12", "r13", "r16", "r17")
    for index, (left, right) in enumerate(zip(mame, recomp), 1):
        if left[1] != right[1]:
            print(f"first DMA snapshot divergence at command #{index}")
            for name, a, b in zip(columns, left[1], right[1]):
                if a != b:
                    print(f"  {name}: MAME={a:#x} MultiArcadeRecomp={b:#x}")
            print(f"  MAME seq={left[0]}:      {' '.join(f'{value:#x}' for value in left[1])}")
            print(f"  MultiArcadeRecomp seq={right[0]}: {' '.join(f'{value:#x}' for value in right[1])}")
            if left[2] or right[2]:
                print(f"  trigger PC: MAME={left[2][0] if left[2] else 'n/a'} "
                      f"MultiArcadeRecomp={right[2][0] if right[2] else 'n/a'}")
                if len(left[2]) > 1 or len(right[2]) > 1:
                    print(f"  trigger time/cycle: MAME={left[2][1] if len(left[2]) > 1 else 'n/a'}s "
                          f"MultiArcadeRecomp={right[2][1] if len(right[2]) > 1 else 'n/a'} cycles")
            print("  CPU register metadata is included in each row; it is not compared because MAME samples during the bus write while MultiArcadeRecomp records the pre-instruction state.")
            return 1
    if len(mame) != len(recomp):
        common = min(len(mame), len(recomp))
        print(f"Common prefix matches: {common} commands; trace lengths differ "
              f"(MAME={len(mame)}, MultiArcadeRecomp={len(recomp)}).")
        return 2
    print(f"DMA register snapshots match: {len(mame)} commands")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
