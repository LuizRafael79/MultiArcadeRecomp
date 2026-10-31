#!/usr/bin/env python3
"""Bounded BIOS/AOT boot regression using owner-supplied Naomi ROMs."""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom_zip", type=Path)
    parser.add_argument("bios_zip", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/release/mamerecomp")
    parser.add_argument("--manifest", type=Path,
                        help="generated SH-4 manifest whose fingerprint is printed in heartbeats")
    parser.add_argument("--log-file", type=Path,
                        help="save the full captured boot trace (defaults beside the manifest)")
    parser.add_argument("--instructions", type=int, default=230_000_000)
    args = parser.parse_args()

    command = [
        str(args.binary.expanduser().resolve()), "run-naomi-sh4", "mvsc2u",
        str(args.rom_zip.expanduser().resolve()), "--bios",
        str(args.bios_zip.expanduser().resolve()), "--instructions",
        str(args.instructions),
    ]
    environment = os.environ.copy()
    environment["MAMERECOMP_NAOMI_TRACE_PROGRESS"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_AICA"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_AOT_TABLE_WRITES"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_CALLBACK_TABLE"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_VIDEO_SETUP"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_PVR"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_SQ"] = "1"
    environment["MAMERECOMP_NAOMI_TRACE_TRAPA"] = "1"
    if args.manifest:
        manifest = args.manifest.expanduser().resolve()
        digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
        environment["MAMERECOMP_NAOMI_MANIFEST_SHA256"] = digest
        print(f"AOT manifest SHA-256={digest}", flush=True)
    result = subprocess.run(command, text=True, capture_output=True, env=environment)
    output = result.stdout + result.stderr
    log_file = args.log_file.expanduser().resolve() if args.log_file else (
        args.manifest.expanduser().resolve().parent / "latest_smoke.log"
        if args.manifest else None)
    if log_file:
        log_file.parent.mkdir(parents=True, exist_ok=True)
        log_file.write_text(output, encoding="utf-8")

    selected = []
    for line in output.splitlines():
        if ("SH-4 BIOS-reset AOT probe" in line or "unresolved indirect target=" in line or
                "boot regression" in line or "AICA trace summary" in line or
                "AICA trace repeated read" in line or "AICA trace RAM " in line or
                "AICA mailbox access" in line or "SH-4 callback-table dispatch" in line or
                "SH-4 task callback #" in line or
                "SH-4 AOT table write" in line or "SH-4 video setup write" in line or
                "SH-4 TRAPA " in line or "Naomi SH-4 PREF" in line or
                "Naomi PVR SQ" in line):
            selected.append(line)
        elif "SH-4 progress executed=" in line:
            match = re.search(r"executed=(\d+)", line)
            if match and int(match.group(1)) % 100_000_000 == 0:
                selected.append(line)
        elif "AICA trace seq=" in line and "physical=007" in line:
            selected.append(line)
    sys.stdout.write("\n".join(selected) + ("\n" if selected else ""))
    if log_file:
        print(f"Full boot trace saved: {log_file}")
    if result.returncode != 0:
        return result.returncode

    summary = re.search(
        r"instructions=(\d+) budget=(\d+) pc=0x([0-9a-fA-F]+).*?(?:\n|$)", output)
    if not summary:
        print("boot regression: missing SH-4 probe summary", file=sys.stderr)
        return 1
    executed, budget, pc = int(summary.group(1)), int(summary.group(2)), int(summary.group(3), 16)
    if executed < args.instructions or executed < budget:
        print(f"boot regression: stopped early ({executed}/{budget} instructions)", file=sys.stderr)
        return 1
    if not 0x0C020000 <= pc < 0x0E000000:
        print(f"boot regression: expected game RAM PC, got 0x{pc:08x}", file=sys.stderr)
        return 1
    if re.search(r"unresolved=0x[0-9a-fA-F]+", output) or "no guest progress" in output:
        print("boot regression: unresolved AOT entry or dispatcher no-progress stop", file=sys.stderr)
        return 1
    print(f"boot regression passed: executed={executed} pc=0x{pc:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
