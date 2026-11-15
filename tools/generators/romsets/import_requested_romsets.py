#!/usr/bin/env python3
"""Create per-game ROM recipes for the eight ROM sets requested on 2026-11-04."""
from __future__ import annotations

import argparse
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from driver_layout import parse_driver
from manifest_io import write_manifest_file

SETS = {
    "eswat": ("sega/segas16b.cpp", "sega", "system16b"),
    "tmnt": ("konami/tmnt.cpp", "konami", "tmnt"),
    "tmnt2": ("konami/tmnt2.cpp", "konami", "tmnt2"),
    "ssriders": ("konami/tmnt2.cpp", "konami", "tmnt2"),
    "toki": ("seibu/toki.cpp", "seibu", "toki"),
    "sfex2pa": ("sony/zn.cpp", "sony", "zn2"),
    "tekken3": ("namco/namcos12.cpp", "namco", "system12"),
    "bldyror2": ("sony/zn.cpp", "sony", "zn1"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mame-source", type=Path, required=True)
    parser.add_argument("--mame-bin", default="mame")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("games", nargs="*", choices=SETS)
    args = parser.parse_args()
    games = args.games or list(SETS)
    cache = {}
    for source, _, _ in {SETS[g] for g in games}:
        source_path = args.mame_source / "src/mame" / source
        if not source_path.is_file():
            parser.error(f"MAME source not found: {source_path}")
        cache[source] = parse_driver(source_path)

    for game in games:
        source, module, board = SETS[game]
        try:
            xml = subprocess.run([args.mame_bin, "-listxml", game], check=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        except subprocess.CalledProcessError as exc:
            raise SystemExit(f"MAME could not describe {game}: {exc.stderr.decode(errors='replace')}")
        root = ET.fromstring(xml)
        machine = root.find("machine")
        if machine is None or machine.get("name") != game:
            raise SystemExit(f"MAME -listxml did not return exactly the requested game {game}")
        regions = {}
        for item in machine.findall("rom"):
            name = item.get("region", "unknown")
            rom = {"file": item.get("name"), "size": int(item.get("size", "0")),
                   "offset": int(item.get("offset", "0"), 16),
                   "status": item.get("status", "good"),
                   "optional": item.get("optional", "no") == "yes"}
            for key in ("crc", "sha1", "merge", "bios"):
                if item.get(key): rom[{"crc": "crc32"}.get(key, key)] = item.get(key)
            regions.setdefault(name, {"roms": []})["roms"].append(rom)
        layouts, aliases = cache[source]
        layout = layouts.get(game)
        if layout is None:
            alias = aliases.get(game)
            layout = {"source": Path(source).name, "romset_alias": alias} if alias else {
                "source": Path(source).name, "status": "ROM_START not found"}
        rom_by_name = {rom["file"]: rom for region in regions.values() for rom in region["roms"]}
        runtime_regions = {}
        for region in layout.get("regions", []):
            if region.get("size") is None:
                continue
            flags = region.get("flags_expression", "") or ""
            runtime_regions[region["name"]] = {
                "size": region["size"],
                "fill": 0xff if "ROMREGION_ERASEFF" in flags else 0,
                "roms": [],
            }
        for operation in layout.get("operations", []):
            name = operation.get("file")
            region_name = operation.get("region")
            if not name or region_name not in runtime_regions or name not in rom_by_name:
                continue
            rom = rom_by_name[name]
            if not rom.get("crc32") or not operation.get("segments"):
                continue  # NO_DUMP files belong in metadata, not the ZIP loader.
            item = {"file": name, "size": rom["size"], "crc32": rom["crc32"],
                    "segments": operation["segments"]}
            if operation.get("transform"):
                item["transform"] = operation["transform"]
            runtime_regions[region_name]["roms"].append(item)
        manifest = {
            "module": module, "board": board, "game": game,
            "gamename": machine.findtext("description") or game,
            "parent": machine.get("cloneof"), "romof": machine.get("romof"),
            "mame": {"name": game, "description": machine.findtext("description") or game,
                     "year": machine.findtext("year"), "manufacturer": machine.findtext("manufacturer"),
                     "sourcefile": source, "machine": machine.get("runnable")},
            "romset_source": "MAME -listxml + local ROM_START parser",
            "driver_layout": layout, "regions": regions,
            "runtime": {"regions": runtime_regions},
            "properties": {"protection": "fd1094" if game == "eswat" else "none"},
        }
        out = args.output / module / board / f"{game}.toml"
        out.parent.mkdir(parents=True, exist_ok=True)
        write_manifest_file(out, manifest)
        print(f"{game}: {sum(len(r['roms']) for r in regions.values())} ROM files, {len(layout.get('operations', []))} MAME layout operations -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
