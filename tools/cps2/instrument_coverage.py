#!/usr/bin/env python3
"""Add low-overhead, per-block instruction counters to generated CPS2 code."""
from __future__ import annotations

import argparse
from pathlib import Path
import re


BLOCK = re.compile(r"^\s*(?:BLOCK|ZBLOCK)\s*\(")
INSN = re.compile(r"^\s*//\s+[0-9A-Fa-f]{4,6}:\s+\S")
INCLUDE = '#include "mamerecomp_cps2_coverage.h"'


def instrument(path: Path, kind: str) -> None:
    source = path.read_text()
    if INCLUDE in source:
        return
    lines = source.splitlines()
    counter = "m68k_aot_instructions" if kind == "m68k" else "z80_aot_instructions"
    output: list[str] = []
    marker_index: int | None = None
    instruction_count = 0

    def finish_block() -> None:
        nonlocal marker_index, instruction_count
        if marker_index is not None and instruction_count:
            output.insert(marker_index + 1,
                f"    mamerecomp_cps2_coverage::{counter} += {instruction_count}ULL;")
        marker_index = None
        instruction_count = 0

    for line in lines:
        if BLOCK.match(line):
            finish_block()
            marker_index = len(output)
            output.append(line)
        else:
            output.append(line)
            if marker_index is not None and INSN.match(line):
                instruction_count += 1
    finish_block()

    try:
        native_include = output.index('#include "native.h"')
    except ValueError:
        native_include = output.index('#include "native_z80.h"')
    output.insert(native_include + 1, INCLUDE)
    path.write_text("\n".join(output) + "\n")


def patch_runtime(source: Path, destination: Path, kind: str) -> None:
    text = source.read_text()
    marker = "            interpreted++;"
    if text.count(marker) != 1:
        raise SystemExit(f"expected one interpreter counter in {source}")
    counter = "m68k_fallback_instructions" if kind == "m68k" else "z80_fallback_instructions"
    text = text.replace(marker, marker + f"\n            mamerecomp_cps2_coverage::{counter}++;", 1)
    first_include_end = text.find("\n", text.find("#include"))
    text = text[:first_include_end + 1] + INCLUDE + "\n" + text[first_include_end + 1:]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    generated = args.work / "gen"
    m68k = sorted(generated.glob("native_[0-9]*.cpp"))
    z80 = sorted(generated.glob("z80_[0-9]*.cpp"))
    if not m68k or not z80:
        raise SystemExit(f"missing generated CPS2 code under {generated}")
    for path in m68k:
        instrument(path, "m68k")
    for path in z80:
        instrument(path, "z80")

    runtime = args.work / "mamerecomp_runtime"
    patch_runtime(args.source / "runtime/recomp.cpp", runtime / "recomp.cpp", "m68k")
    patch_runtime(args.source / "runtime/recomp_z80.cpp", runtime / "recomp_z80.cpp", "z80")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
