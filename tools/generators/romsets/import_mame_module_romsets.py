#!/usr/bin/env python3
"""Import CPS-1, CPS-2 and Neo Geo game ROM data from MAME -listxml."""
from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from driver_layout import parse_driver
from manifest_io import load_manifest_file, write_manifest_file


MODULES = {
    "capcom/cps1.cpp": ("capcom", "cps1", "cps1"),
    "capcom/cps2.cpp": ("capcom", "cps2", "cps2"),
    "snk/neogeo.cpp": ("snk", "neogeo", "neogeo_mvs"),
    "williams/midtunit.cpp": ("midway", "midway", "tunit"),
}
RUNTIME_ONLY_FIELDS = ("bios_candidates", "system_roms", "game_transforms")


def mame_rom(entry: ET.Element) -> dict:
    attrs = entry.attrib
    rom = {
        "file": attrs["name"],
        "size": int(attrs["size"], 10),
        # MAME's -listxml offsets are hexadecimal digits, without a 0x prefix.
        "offset": int(attrs.get("offset", "0"), 16),
        "status": attrs.get("status", "good"),
        "optional": attrs.get("optional", "no") == "yes",
    }
    for source, dest in (("crc", "crc32"), ("sha1", "sha1"), ("merge", "merge"), ("bios", "bios")):
        if attrs.get(source):
            rom[dest] = attrs[source].lower() if source in {"crc", "sha1"} else attrs[source]
    return rom


def make_manifest(machine: ET.Element, sourcefile: str,
                  driver_layouts: dict[str, dict], aliases: dict[str, str]) -> dict:
    _publisher, module, board = MODULES[sourcefile]
    game = machine.get("name", "")
    description = machine.findtext("description") or game
    regions: dict[str, dict] = {}
    for entry in machine.findall("rom"):
        region_name = entry.get("region", "unknown")
        regions.setdefault(region_name, {"roms": []})["roms"].append(mame_rom(entry))

    properties = {"protection": "none"}
    if module == "cps2":
        # The per-set key region records the MAME security key. Decryption is
        # handled by the CPS-2 backend, so this is descriptive, not a run block.
        properties["protection"] = "cps2-cipher"

    driver_layout = driver_layouts.get(game)
    if driver_layout is None:
        target = aliases.get(game)
        driver_layout = {"source": Path(sourcefile).name, "romset_alias": target} if target else {
            "source": Path(sourcefile).name, "status": "ROM_START not found"
        }
    return {
        "module": module,
        "board": board,
        "game": game,
        "gamename": description,
        "parent": machine.get("cloneof"),
        "romof": machine.get("romof"),
        "mame": {
            "name": game,
            "description": description,
            "cloneof": machine.get("cloneof"),
            "romof": machine.get("romof"),
            "year": machine.findtext("year"),
            "manufacturer": machine.findtext("manufacturer"),
            "sourcefile": sourcefile,
            "isbios": machine.get("isbios") == "yes",
        },
        "romset_source": "MAME -listxml",
        "driver_layout": driver_layout,
        "regions": regions,
        "properties": properties,
    }


def merge_existing(path: Path, imported: dict, source_path: Path | None = None) -> None:
    old = load_manifest_file(source_path or path)
    runtime = old.get("runtime", {})
    old_regions = old.get("regions", {})
    if any("segments" in rom for region in old_regions.values() for rom in region.get("roms", [])):
        runtime["regions"] = old_regions

    old_properties = old.get("properties", {})
    runtime_properties = runtime.setdefault("properties", {})
    # These are per-game execution transforms, rather than MAME ROM metadata.
    for key in ("sound_opcode_decode", "prom_transform", "protection_decrypt"):
        if key in old_properties:
            runtime_properties[key] = old_properties.pop(key)
    if not old_properties:
        old.pop("properties", None)

    # Keep Neo Geo runtime-only dependency/transform data attached to the game.
    for key in RUNTIME_ONLY_FIELDS:
        if key in old:
            runtime[key] = old.pop(key)
    if "dependencies" in old and imported["module"] == "neogeo":
        runtime["dependencies"] = old.pop("dependencies")

    old_properties = old.get("properties", {})
    old.update(imported)
    if old_properties:
        old["properties"] = {**imported.get("properties", {}), **old_properties}
    if runtime:
        old["runtime"] = runtime
    else:
        old.pop("runtime", None)
    write_manifest_file(path, old)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mame-source", type=Path, required=True)
    parser.add_argument("--xml", type=Path, help="existing output from mame -listxml")
    parser.add_argument("--mame-bin", help="MAME executable used to run -listxml")
    parser.add_argument("--output", type=Path, required=True, help="src/romsets directory")
    args = parser.parse_args()
    if bool(args.xml) == bool(args.mame_bin):
        parser.error("provide exactly one of --xml or --mame-bin")
    driver_data = {}
    for sourcefile in MODULES:
        source_path = args.mame_source / "src/mame" / sourcefile
        if not source_path.is_file():
            parser.error(f"MAME source file not found: {source_path}")
        driver_data[sourcefile] = parse_driver(source_path)

    process = None
    if args.xml:
        stream = args.xml.open("rb")
    else:
        process = subprocess.Popen([args.mame_bin, "-listxml"], stdout=subprocess.PIPE)
        stream = process.stdout
    if stream is None:
        raise SystemExit("could not read MAME -listxml output")
    manifests = []
    machine_profiles: dict[str, tuple[str, str]] = {}
    try:
        for _event, machine in ET.iterparse(stream, events=("end",)):
            if machine.tag != "machine":
                continue
            sourcefile = machine.get("sourcefile", "")
            if sourcefile in MODULES:
                layouts, aliases = driver_data[sourcefile]
                manifest = make_manifest(machine, sourcefile, layouts, aliases)
                manifests.append(manifest)
                machine_profiles[manifest["game"]] = (manifest["module"], manifest["board"])
            machine.clear()
    finally:
        stream.close()

    if process and process.wait():
        raise SystemExit("MAME -listxml failed")
    for manifest in manifests:
        for relation in ("parent", "romof"):
            related = manifest.get(relation)
            related_profile = machine_profiles.get(related) if related else None
            if related_profile:
                related_module, related_board = related_profile
                if related_module != manifest["module"]:
                    manifest[f"{relation}_module"] = related_module
                if related_board != manifest["board"]:
                    manifest[f"{relation}_board"] = related_board

    written = 0
    missing = []
    for manifest in manifests:
        publisher, module, board = next(
            values for values in MODULES.values()
            if values[1] == manifest["module"] and values[2] == manifest["board"]
        )
        directory = args.output / publisher / module / board
        destination = directory / f"{manifest['game']}.toml"
        directory.mkdir(parents=True, exist_ok=True)
        module_dir = args.output / publisher / module
        other_paths = [path for path in module_dir.rglob(f"{manifest['game']}.toml")
                       if path != destination]
        legacy_path = other_paths[0] if other_paths else None
        if destination.exists() or legacy_path:
            merge_existing(destination, manifest, source_path=legacy_path or destination)
            if legacy_path and legacy_path.exists() and legacy_path != destination:
                legacy_path.unlink()
        else:
            write_manifest_file(destination, manifest)
        written += 1

    if not written:
        print("No CPS-1, CPS-2 or Neo Geo registrations found in MAME XML", file=sys.stderr)
        return 2
    missing_layouts = [manifest["game"] for manifest in manifests
                       if manifest["driver_layout"].get("status") == "ROM_START not found"]
    if missing_layouts:
        print("Driver layout missing for: " + ", ".join(missing_layouts), file=sys.stderr)
    print(f"Imported {written} ROM-set entries from configured MAME drivers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
