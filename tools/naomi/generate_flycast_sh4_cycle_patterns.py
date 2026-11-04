#!/usr/bin/env python3
"""Generate compact SH-4 timing descriptors from Flycast's opcode table."""

from pathlib import Path
import re
import sys


def entries(source: str):
    marker = "static sh4_opcodelistentry opcodes[]="
    start = source.index("{", source.index(marker)) + 1
    end = source.index("\n};", start)
    body = source[start:end]
    rows, depth, row_start = [], 0, None
    in_string = escaped = False
    for i, ch in enumerate(body):
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            if depth == 0:
                row_start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and row_start is not None:
                rows.append(body[row_start + 1:i])
                row_start = None

    def split_fields(row):
        fields, depth, start = [], 0, 0
        in_string = escaped = False
        for i, ch in enumerate(row):
            if in_string:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    in_string = False
                continue
            if ch == '"':
                in_string = True
            elif ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif ch == "," and depth == 0:
                fields.append(row[start:i].strip())
                start = i + 1
        fields.append(row[start:].strip())
        return fields

    result = []
    masks = {
        "Mask_none": "0xffff", "Mask_n": "0xf0ff",
        "Mask_n_m": "0xf00f", "Mask_n_m_imm4": "0xf000",
        "Mask_n_imm8": "0xf000",
        "Mask_imm8": "0xff00", "Mask_n_ml3bit": "0xf08f",
        "Mask_nh3bit": "0xf1ff", "Mask_nh2bit": "0xf3ff",
    }
    for row in rows:
        f = split_fields(row)
        if len(f) < 10 or f[1] == "0":
            continue
        # Flycast prepends a REIOS pseudo-op (not a SH-4 encoding).
        if not re.fullmatch(r"0[xX][0-9a-fA-F]+", f[3]):
            continue
        if f[2] not in masks:
            raise ValueError(f"unknown opcode mask {f[2]}")
        if f[8] not in {"MT", "EX", "BR", "LS", "FE", "CO"}:
            raise ValueError(f"unknown execution unit {f[8]}")
        if not re.fullmatch(r"\d+", f[6]) or not re.fullmatch(r"\d+", f[9]):
            raise ValueError(f"non-literal timing fields: {f}")
        result.append((masks[f[2]], f[3], f[6], f[8], f[9]))
    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_flycast_sh4_cycle_patterns.py sh4_opcode_list.cpp output.inc")
    patterns = entries(Path(sys.argv[1]).read_text())
    lines = [
        "// Generated from Flycast sh4_opcode_list.cpp; do not edit by hand.",
        "// Source snapshot: core/third_party/flycast-sh4/sh4_opcode_list.cpp",
    ]
    for mask, value, issue, unit, extype in patterns:
        lines.append(f"{{{mask}, {value}, {issue}, Unit::{unit}, {extype}}},")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
    print(f"generated {len(patterns)} SH-4 timing patterns")


if __name__ == "__main__":
    main()
