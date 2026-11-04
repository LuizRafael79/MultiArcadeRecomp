#!/usr/bin/env python3
"""Emit the small C++ KOF98 runtime table from its JSON manifest."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def cpp(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(manifest: dict) -> str:
    if manifest.get("module") != "neogeo" or manifest.get("game") != "kof98":
        raise ValueError("expected the Neo Geo KOF98 manifest")
    runtime = manifest["runtime"]
    regions = runtime["regions"]
    lines = [
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "namespace mamerecomp::neogeo::romset_data {",
        "struct Load { const char* name; const char* region; std::uint32_t source_offset, offset, length, stride, group_bytes, rom_size, crc; bool word_swap; };",
        "struct Region { const char* name; std::uint32_t size; };",
        "struct Bios { const char* name; std::uint32_t size, crc; bool word_swap; };",
        "struct SystemRom { const char* name; std::uint32_t size, crc; };",
    ]

    loads = []
    for region_name, region in regions.items():
        for rom in region.get("roms", []):
            segments = rom.get("segments", [])
            for segment in segments:
                loads.append((rom, region_name, segment))
    lines.append(f"inline constexpr std::array<Load, {len(loads)}> loads{{{{")
    for rom, region_name, segment in loads:
        lines.append("    {" + ", ".join((cpp(rom["file"]), cpp(region_name),
                      hex(int(segment.get("source_offset", 0))), hex(int(segment["destination"])),
                      hex(int(segment["length"])), hex(int(segment.get("stride", 1))),
                      hex(int(segment.get("group_bytes", 1))), hex(int(rom["size"])), "0x" + rom["crc32"].lower(),
                      "true" if rom.get("transform") == "word_swap" else "false")) + "},")
    lines.append("}};")

    lines.append(f"inline constexpr std::array<Region, {len(regions)}> regions{{{{")
    for region_name, region in regions.items():
        lines.append(f"    {{{cpp(region_name)}, {hex(int(region['size']))}}},")
    lines.append("}};")

    bios = runtime.get("bios_candidates", [])
    lines.append(f"inline constexpr std::array<Bios, {len(bios)}> bios_candidates{{{{")
    for rom in bios:
        lines.append("    {" + ", ".join((cpp(rom["file"]), hex(int(rom["size"])),
                      "0x" + rom["crc32"].lower(),
                      "true" if rom.get("transform") == "word_swap" else "false")) + "},")
    lines.append("}};")

    system_roms = runtime.get("system_roms", [])
    lines.append(f"inline constexpr std::array<SystemRom, {len(system_roms)}> system_roms{{{{")
    for rom in system_roms:
        lines.append("    {" + ", ".join((cpp(rom["file"]), hex(int(rom["size"])),
                      "0x" + rom["crc32"].lower())) + "},")
    lines.extend(("}};", "} // namespace mamerecomp::neogeo::romset_data", ""))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(emit(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
