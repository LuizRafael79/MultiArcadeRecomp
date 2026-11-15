#!/usr/bin/env python3
"""Emit the legacy System 16/18 runtime profile ABI from TOML manifests."""
from __future__ import annotations

import argparse
from pathlib import Path
from manifest_io import load_manifest_file


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
    ("maincpu:key", "fd1094_key", "RomRegionLoad"),
    ("plds", "plds", "RomRegionLoad"),
)


def cpp_string(value: str | None) -> str:
    if value is None:
        return "{}"
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def hx(value: int) -> str:
    return f"0x{value:x}"


def emit(manifests: list[dict]) -> str:
    lines = [
        '#include "runtime/sega/profiles/sega_profiles.hpp"',
        "#include <algorithm>",
        "#include <array>",
        "namespace mamerecomp::romsets {",
    ]

    profile_items = []
    for manifest in manifests:
        game = manifest["game"]
        regions = manifest.get("runtime", {}).get("regions", {})
        canonical_roms = {
            (rom.get("file"), rom.get("crc32", "").lower()): rom
            for region in manifest.get("regions", {}).values()
            for rom in region.get("roms", [])
        }

        def hash_fields(rom: dict) -> tuple[str, str]:
            canonical = canonical_roms.get((rom.get("file"), rom.get("crc32", "").lower()), {})
            sha1 = rom.get("sha1") or canonical.get("sha1")
            merge = rom.get("merge") or canonical.get("merge")
            status = rom.get("status", canonical.get("status"))
            if not sha1:
                raise ValueError(f"{game}/{rom.get('file')}: required ROM has no SHA-1 in the manifest")
            if sha1 and (len(sha1) != 40 or any(c not in "0123456789abcdefABCDEF" for c in sha1)):
                raise ValueError(f"{game}/{rom.get('file')}: invalid SHA-1 value")
            return cpp_string(sha1), cpp_string(merge)

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
                    sha1, merge = hash_fields(rom)
                    rows.append(
                        "{" + ", ".join((cpp_string(rom["file"]), hx(segment["destination"]),
                                          hx(rom["size"]), hx(int(rom["crc32"], 16)),
                                          sha1, merge)) + "}"
                    )
                else:
                    for segment in segments:
                        sha1, merge = hash_fields(rom)
                        rows.append(
                            "{" + ", ".join((cpp_string(rom["file"]), hx(rom["size"]),
                                              hx(int(rom["crc32"], 16)), sha1, hx(segment["source_offset"]),
                                              hx(segment["destination"]), hx(segment["length"]),
                                              str(segment["stride"]), merge)) + "}"
                        )
            lines.append(f"constexpr std::array<{typ}, {len(rows)}> {array_name}{{{{")
            lines.extend(f"    {row}," for row in rows)
            lines.append("}};")
            arrays[region_key] = (array_name, region.get("size", 0))

        mapper = manifest.get("properties", {}).get("mapper", "sega5797")
        rom_board = manifest.get("properties", {}).get("rom_board", "shadow_dancer_171")
        if mapper not in MAPPER or rom_board not in ROM_BOARD:
            raise ValueError(f"{game}: unknown board-specific mapper/ROM board")
        board = manifest["board"]
        if board == "system16a":
            board_configuration = "System16AConfiguration{}"
        elif board == "system16b":
            board_configuration = f"System16BConfiguration{{{MAPPER[mapper]}}}"
        elif board == "system18":
            board_configuration = f"System18Configuration{{{ROM_BOARD[rom_board]}}}"
        else:
            raise ValueError(f"{game}: unsupported Sega board family {board!r}")
        interactive_configuration = bool(manifest.get("runtime", {}).get("interactive_configuration", False))
        runtime = manifest.get("runtime", {})
        values = [cpp_string(game), cpp_string(manifest["gamename"]), board_configuration,
                  "true" if interactive_configuration else "false",
                  f"std::optional<std::uint8_t>{{{hx(runtime['default_dsw1'])}}}" if "default_dsw1" in runtime else "std::nullopt",
                  f"std::optional<std::uint8_t>{{{hx(runtime['default_dsw2'])}}}" if "default_dsw2" in runtime else "std::nullopt"]
        for region_key, _field, _typ in REGION_KEYS:
            if region_key in {"maincpu:key", "plds"}:
                continue
            name, size = arrays[region_key]
            values.extend((hx(size), name))
        key_name, key_size = arrays["maincpu:key"]
        pld_name, pld_size = arrays["plds"]
        values.extend((cpp_string(manifest.get("romof")),
                       str(manifest.get("properties", {}).get("soundcpu_fill", 0)),
                       hx(key_size), key_name, hx(pld_size), pld_name))
        entry_points = manifest.get("runtime", {}).get("m68k_entry_points", [])
        if entry_points:
            entries_name = f"k_{game}_m68k_entry_points"
            lines.append(f"constexpr std::array<std::uint32_t, {len(entry_points)}> {entries_name}{{{{")
            lines.extend(f"    {hx(address)}," for address in entry_points)
            lines.append("}};")
            values.append(entries_name)
        profile_name = f"profile_{game}"
        lines.extend((f"const GameProfile& {profile_name}() {{",
                      f"    static constexpr GameProfile value{{{', '.join(values)}}};",
                      "    return value;", "}"))
        profile_items.append(f"{profile_name}()")

    lines.extend((
        "} // namespace mamerecomp::romsets",
        "namespace mamerecomp {",
        "namespace {",
        f"const std::array<GameProfile, {len(profile_items)}> profiles{{{{",
    ))
    lines.extend(f"    romsets::{item}," for item in profile_items)
    lines.extend((
        "}};",
        "}",
        "std::span<const GameProfile> game_profiles() { return profiles; }",
        "const GameProfile* find_game_profile(std::string_view id) {",
        "    const auto found = std::find_if(profiles.begin(), profiles.end(), [id](const GameProfile& p) { return p.id == id; });",
        "    return found == profiles.end() ? nullptr : &*found;",
        "}",
        "} // namespace mamerecomp",
        "",
    ))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    paths = sorted(args.manifest_dir.rglob("*.toml"))
    manifests = [load_manifest_file(path) for path in paths]
    for manifest in manifests:
        if manifest.get("module") not in {"system16", "system18"}:
            raise SystemExit(f"unsupported module in {manifest.get('game')}: {manifest.get('module')}")
    # A runtime profile means the TOML contains the layout consumed by the
    # emulator. `runnable` describes boot status and must not hide a profile
    # from inspect/map commands.
    manifests = [manifest for manifest in manifests
                 if manifest.get("runtime_profile", False)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(emit(manifests))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
