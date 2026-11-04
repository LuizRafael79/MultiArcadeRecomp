#!/usr/bin/env python3
"""Generate direct C++ wrappers for the MAME-derived C3x opcode handlers."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def handlers_from_source(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    table = text.split("s_tms320c3x_ops[]", 1)[1].split("{", 1)[1].split("\n};", 1)[0]
    handlers = re.findall(r"&tms320c3x_device::([A-Za-z0-9_]+)", table)
    if len(handlers) != 2048:
        raise ValueError(f"expected 2048 C3x dispatch entries, found {len(handlers)}")
    return sorted(set(handlers) - {"illegal", "unimplemented"})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ops_source", type=Path)
    parser.add_argument("declarations", type=Path)
    parser.add_argument("implementations", type=Path)
    args = parser.parse_args()
    try:
        handlers = handlers_from_source(args.ops_source)
    except (OSError, ValueError, IndexError) as exc:
        parser.error(str(exc))
    args.declarations.parent.mkdir(parents=True, exist_ok=True)
    args.implementations.parent.mkdir(parents=True, exist_ok=True)
    args.declarations.write_text("\n".join(
        f"bool aot_execute_{name}(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept;"
        for name in handlers) + "\n", encoding="utf-8")
    args.implementations.write_text("\n".join(
        f"bool Interpreter::aot_execute_{name}(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {{\n"
        f"    if (!aot_begin(expected_pc, expected_opcode)) return false;\n"
        f"    {name}(expected_opcode);\n"
        f"    return aot_end(expected_pc);\n"
        f"}}"
        for name in handlers) + "\n", encoding="utf-8")
    print(f"Generated {len(handlers)} direct C31 handler wrappers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
