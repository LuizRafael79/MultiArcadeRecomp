#!/usr/bin/env python3
"""Emit the legacy System 16/18 runtime profile ABI from JSON manifests."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


BOARD_FAMILY = {
    "system16a": "BoardFamily::System16A",
    "system16b": "BoardFamily::System16B",
    "system18": "BoardFamily::System18",
}
MAPPER = {
    "sega5358": "System16BBoard::Sega5358",
    "sega5521": "System16BBoard::Sega5521",
    "sega5797": "System16BBoard::Sega5797",
}
ROM_BOARD = {
    "shadow_dancer_171": "System18RomBoard::ShadowDancer171",
    "sega5874": "System18RomBoard::Sega5874",
}
REGION_KEYS = (
    ("maincpu", "maincpu", "RomLoad16Byte"),
    ("tiles", "tiles", "RomRegionLoad"),
    ("sprites", "sprites", "RomRegionLoad"),
    ("soundcpu", "soundcpu", "RomRegionLoad"),
    ("upd7751", "upd7751", "RomRegionLoad"),
    ("upd7751_data", "upd7751_data", "RomRegionLoad"),
    ("mcu", "mcu", "RomRegionLoad"),
)


def cpp_string(value: str | None) -> str:
    if value is None:
        return "{}"
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def hx(value: int) -> str:
    return f"0x{value:x}"


def emit(manifests: list[dict]) -> str:
    lines = [
        '#include "system16/system16.hpp"',
        "#include <algorithm>",
        "#include <array>",
        "namespace mamerecomp {",
        "namespace {",
    ]

    profile_items = []
    for manifest in manifests:
        game = manifest["game"]
        regions = manifest.get("runtime", {}).get("regions", {})
        arrays = {}
        for region_key, field, typ in REGION_KEYS:
            region = regions.get(region_key, {})
            roms = region.get("roms", [])
            array_name = f"k_{game}_{field}"
            rows = []
            for rom in roms:
                segments = rom.get("segments", [])
                if typ == "RomLoad16Byte":
                    if len(segments) != 1 or segments[0].get("stride") != 2:
                        raise ValueError(f"{game}/{region_key}: expected one stride-2 segment per maincpu ROM")
                    segment = segments[0]
                    rows.append(
                        "{" + ", ".join((cpp_string(rom["file"]), hx(segment["destination"]),
                                          hx(rom["size"]), hx(int(rom["crc32"], 16)),
                                          cpp_string(rom.get("merge")))) + "}"
                    )
                else:
                    for segment in segments:
                        rows.append(
                            "{" + ", ".join((cpp_string(rom["file"]), hx(rom["size"]),
                                              hx(int(rom["crc32"], 16)), hx(segment["source_offset"]),
                                              hx(segment["destination"]), hx(segment["length"]),
                                              str(segment["stride"]), cpp_string(rom.get("merge")))) + "}"
                        )
            lines.append(f"constexpr std::array<{typ}, {len(rows)}> {array_name}{{{{")
            lines.extend(f"    {row}," for row in rows)
            lines.append("}};")
            arrays[region_key] = (array_name, region.get("size", 0))

        mapper = manifest.get("properties", {}).get("mapper", "sega5797")
        rom_board = manifest.get("properties", {}).get("rom_board", "shadow_dancer_171")
        if mapper not in MAPPER or rom_board not in ROM_BOARD:
            raise ValueError(f"{game}: unknown board-specific mapper/ROM board")
        values = [cpp_string(game), cpp_string(manifest["gamename"]), BOARD_FAMILY[manifest["board"]]]
        for region_key, _field, _typ in REGION_KEYS:
            name, size = arrays[region_key]
            values.extend((hx(size), name))
        values.extend((MAPPER[mapper], cpp_string(manifest.get("romof")),
                       str(manifest.get("properties", {}).get("soundcpu_fill", 0)), ROM_BOARD[rom_board]))
        profile_name = f"profile_{game}"
        lines.extend((f"const GameProfile& {profile_name}() {{",
                      f"    static constexpr GameProfile value{{{', '.join(values)}}};",
                      "    return value;", "}"))
        profile_items.append(f"{profile_name}()")

    lines.append(f"const std::array<GameProfile, {len(profile_items)}> profiles{{{{")
    lines.extend(f"    {item}," for item in profile_items)
    lines.extend(("}};", "} // namespace", "std::span<const GameProfile> game_profiles() { return profiles; }",
                  "const GameProfile* find_game_profile(std::string_view id) {",
                  "    const auto found = std::find_if(profiles.begin(), profiles.end(), [id](const GameProfile& p) { return p.id == id; });",
                  "    return found == profiles.end() ? nullptr : &*found;", "}", "} // namespace mamerecomp", ""))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifests = [json.loads(path.read_text()) for path in sorted(args.manifest_dir.rglob("*.json"))]
    for manifest in manifests:
        if manifest.get("module") not in {"system16", "system18"}:
            raise SystemExit(f"unsupported module in {manifest.get('game')}: {manifest.get('module')}")
    # Catalog entries describe MAME ROM placement but do not necessarily have a
    # runtime-tested load layout. Only explicitly enabled runtime profiles enter
    # the legacy C++ GameProfile table.
    manifests = [manifest for manifest in manifests
                 if manifest.get("runtime_profile", False) and manifest.get("runnable", True)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(emit(manifests))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
