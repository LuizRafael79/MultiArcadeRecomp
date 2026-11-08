#!/usr/bin/env python3
"""Generate conservative DSP16A native dispatch entries from an execution manifest.

The manifest must be captured by the DSP16A core while running the real device.
It contains only PC/opcode pairs which reached execute_instruction; program-table
and sample-data reads are never decoded by this tool.
"""

import argparse
from pathlib import Path


def read_manifest(path: Path, min_executions: int = 1) -> list[tuple[int, int, int]]:
    entries: dict[int, tuple[int, int]] = {}
    invalid: list[int] = []
    for line_no, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) not in (2, 3):
            raise ValueError(f"{path}:{line_no}: expected pc,opcode[,executions]")
        pc, opcode = (int(part.strip(), 16) for part in parts[:2])
        executions = int(parts[2].strip(), 10) if len(parts) == 3 else 1
        if not 0 <= pc < 0x1000 or not 0 <= opcode <= 0xffff:
            raise ValueError(f"{path}:{line_no}: value outside DSP16A internal ROM")
        if executions < 1:
            raise ValueError(f"{path}:{line_no}: execution count must be positive")
        if opcode == 0xffff:
            invalid.append(pc)
            continue
        previous = entries.get(pc)
        if previous is not None and previous[0] != opcode:
            raise ValueError(f"{path}:{line_no}: PC 0x{pc:03x} has conflicting opcodes")
        entries[pc] = (opcode, max(executions, previous[1] if previous else 0))
    if invalid:
        pcs = ", ".join(f"0x{pc:03x}" for pc in invalid)
        raise ValueError(f"manifest contains conflicting runtime opcodes at {pcs}")
    if not entries:
        raise ValueError("manifest contains no executed DSP16A instructions")
    selected = [
        (pc, opcode, executions)
        for pc, (opcode, executions) in sorted(entries.items())
        if executions >= min_executions
    ]
    if not selected:
        raise ValueError(
            f"no DSP16A instructions meet the minimum execution count {min_executions}"
        )
    return selected


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--min-executions", type=int, default=1,
        help="include only PCs executed at least this many times (default: 1)",
    )
    args = parser.parse_args()
    if args.min_executions < 1:
        parser.error("--min-executions must be positive")
    entries = read_manifest(args.manifest, args.min_executions)

    lines = [
        '// Generated from an observed DSP16A execution manifest. Do not edit.',
        '#include "dsp16a.hpp"',
        '',
        'namespace mamerecomp::dsp16 {',
    ]
    for opcode in sorted({opcode for _, opcode, _ in entries}):
        lines.append(
            f'[[gnu::noinline]] static unsigned dsp16a_aot_op_{opcode:04x}(Dsp16A& dsp, bool cache) {{ '
            f'return dsp.execute_compiled_opcode<0x{opcode:04x}>(cache); }}'
        )
    lines.extend([
        '',
        'static const Dsp16ACompiledEntry entries[] = {',
    ])
    lines.extend(
        f'    {{0x{pc:03x}, 0x{opcode:04x}, &dsp16a_aot_op_{opcode:04x}}},'
        for pc, opcode, _ in entries
    )
    lines.extend([
        '};',
        'extern "C" const Dsp16ACompiledEntry* mamerecomp_dsp16a_aot_entries(std::size_t* count) {',
        '    *count = sizeof(entries) / sizeof(entries[0]);',
        '    return entries;',
        '}',
        '} // namespace mamerecomp::dsp16',
        '',
    ])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines))
    print(f"generated {len(entries)} guarded DSP16A entries in {args.output}")


if __name__ == "__main__":
    main()
