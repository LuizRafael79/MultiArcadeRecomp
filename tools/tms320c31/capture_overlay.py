#!/usr/bin/env python3
"""Build a deterministic C31 AOT overlay from one or more hot-site logs.

Each input is captured with MULTIARCADERECOMP_C31_HOT_SITES=1. The tool unions PCs
across runs but refuses to emit an address if its observed opcode changes.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


PATTERN = re.compile(r"\[C31 hot opcode\] PC=([0-9a-fA-F]+) OPCODE=([0-9a-fA-F]+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    observed: dict[int, int] = {}
    for path in args.logs:
        local: dict[int, int] = {}
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = PATTERN.search(line)
            if not match:
                continue
            pc, opcode = (int(value, 16) for value in match.groups())
            previous = local.setdefault(pc, opcode)
            if previous != opcode:
                parser.error(f"{path}: code at PC {pc:#08x} changed during capture "
                             f"({previous:#010x} -> {opcode:#010x}); keep it interpreted")
        for pc, opcode in local.items():
            previous = observed.setdefault(pc, opcode)
            if previous != opcode:
                parser.error(f"captured runs disagree at PC {pc:#08x}: "
                             f"{previous:#010x} vs {opcode:#010x}; keep it interpreted")
    if not observed:
        parser.error("no C31 hot opcode records found in input logs")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        output.write("# Stable captured TMS320C31 words, PC OPCODE\n")
        for pc, opcode in sorted(observed.items()):
            output.write(f"0x{pc:06x} 0x{opcode:08x}\n")
    print(f"C31 overlay: {len(observed)} stable instruction words -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
