#!/usr/bin/env python3
"""Import System 16/18 ROM-set metadata from MAME's -listxml output."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


SOURCE_FILES = {
    "sega/segas16a.cpp": "system16",
    "sega/segas16b.cpp": "system16",
    "sega/segas18.cpp": "system18",
    "sega/system16.cpp": "system16",
}
REGISTRATION = re.compile(r"\bGAME[A-Z]*\s*\(")
PROTECTED_CONFIG = re.compile(r"(?:fd1089|fd1094|mc8123|i8751)", re.IGNORECASE)
PROTECTED_DESCRIPTION = re.compile(r"(?:FD1089|FD1094|MC-8123|8751|(?<!un)protected|encrypted)", re.IGNORECASE)


def split_macro_args(text: str, start: int) -> list[str]:
    """Split a one-line MAME GAME macro call, respecting nested parentheses/quotes."""
    args: list[str] = []
    token: list[str] = []
    depth = 0
    quoted = False
    escaped = False
    for char in text[start:]:
        if quoted:
            token.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == '"':
            quoted = True
            token.append(char)
        elif char == "(":
            depth += 1
            token.append(char)
        elif char == ")":
            if depth == 0:
                args.append("".join(token).strip())
                break
            depth -= 1
            token.append(char)
        elif char == "," and depth == 0:
            args.append("".join(token).strip())
            token = []
        else:
            token.append(char)
    return args


def game_configs(mame_source: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for relative in SOURCE_FILES:
        source = mame_source / "src/mame" / relative
        if not source.is_file():
            raise FileNotFoundError(source)
        for line in source.read_text(errors="replace").splitlines():
            match = REGISTRATION.search(line)
            if not match:
                continue
            args = split_macro_args(line, match.end())
            if len(args) >= 4:
                name = args[1].strip()
                config = args[3].strip()
                result[name] = config
    return result


def config_boards(mame_source: Path) -> dict[str, str]:
    """Resolve wrapper machine configs to the board setup they call."""
    result: dict[str, str] = {}
    for relative in SOURCE_FILES:
        source = (mame_source / "src/mame" / relative).read_text(errors="replace")
        pattern = re.compile(
            r"void\s+[\w:]+::(\w+)\s*\(machine_config\s*&config\)\s*\{(.*?)^}",
            re.MULTILINE | re.DOTALL,
        )
        for match in pattern.finditer(source):
            calls = re.findall(r"\b(\w+)\s*\(config\s*\)", match.group(2))
            result[match.group(1)] = " ".join(calls)
    return result


def classify_board(
    machine: ET.Element,
    configs: dict[str, str],
    wrappers: dict[str, str],
    machines: dict[str, ET.Element],
    visiting: set[str] | None = None,
) -> tuple[str, str]:
    name = machine.get("name", "")
    config = configs.get(name, "")
    text = f"{machine.findtext('description') or ''} {config}"
    if re.search(r"system18", text, re.IGNORECASE):
        return "system18", config
    if re.search(r"system16a", text, re.IGNORECASE):
        return "system16a", config
    if re.search(r"system16b", text, re.IGNORECASE):
        return "system16b", config

    source = machine.get("sourcefile", "")
    if source.endswith("segas18.cpp"):
        return "system18", config

    visiting = visiting or set()
    if name not in visiting:
        visiting.add(name)
        parent = machine.get("cloneof") or machine.get("romof")
        if parent in machines:
            inherited, _ = classify_board(machines[parent], configs, wrappers, machines, visiting)
            if inherited:
                return inherited, config

    # Config wrappers such as shdancbl call system18(config), while some older
    # home-grown bootlegs use the generic System 16 base machine.
    for called in wrappers.get(config, "").split():
        if called.lower() == "system16_base":
            return "system16", config
        if called.lower() == "system18":
            return "system18", config
        if called.lower() in {"system16a", "system16b"}:
            return called.lower(), config
        if called in wrappers:
            called_lower = wrappers[called].lower()
            if "system16_base" in called_lower:
                return "system16", config
            if "system18(" in called_lower:
                return "system18", config
            if "system16a(" in called_lower:
                return "system16a", config
            if "system16b(" in called_lower:
                return "system16b", config

    # The source file is a reliable fallback for the ordinary Sega registrations.
    if source.endswith("segas16a.cpp"):
        return "system16a", config
    if source.endswith("segas16b.cpp"):
        return "system16b", config
    return "", config


def mame_rom(rom: ET.Element) -> dict:
    attrs = rom.attrib
    result = {
        "file": attrs["name"],
        "size": int(attrs["size"], 0),
        # MAME writes offsets as hexadecimal digits without a 0x prefix.
        "offset": int(attrs.get("offset", "0"), 16),
        "status": attrs.get("status", "good"),
        "optional": attrs.get("optional", "no") == "yes",
    }
    if attrs.get("crc"):
        result["crc32"] = attrs["crc"].lower()
    if attrs.get("sha1"):
        result["sha1"] = attrs["sha1"].lower()
    if attrs.get("merge"):
        result["merge"] = attrs["merge"]
    if attrs.get("bios"):
        result["bios"] = attrs["bios"]
    return result


def build_manifest(machine: ET.Element, module: str, board: str, config: str) -> dict:
    name = machine.get("name", "")
    description = machine.findtext("description") or name
    sourcefile = machine.get("sourcefile", "")
    rom_regions: dict[str, dict] = {}
    for rom in machine.findall("rom"):
        region_name = rom.get("region", "unknown")
        rom_regions.setdefault(region_name, {"roms": []})["roms"].append(mame_rom(rom))

    protection = "none"
    match = PROTECTED_CONFIG.search(config)
    if match:
        protection = match.group(0).lower()
    elif any(region.endswith(":key") for region in rom_regions):
        protection = "decryption-key"
    elif config in {"beautyb", "iqpipe", "bloxeedbl"}:
        protection = "mame-identified"
    elif PROTECTED_DESCRIPTION.search(description) and "bootleg of" not in description.lower():
        protection = "mame-identified"

    manifest = {
        "module": module,
        "board": board,
        "game": name,
        "gamename": description,
        "parent": machine.get("cloneof"),
        "romof": machine.get("romof"),
        "mame": {
            "name": name,
            "description": description,
            "cloneof": machine.get("cloneof"),
            "romof": machine.get("romof"),
            "year": machine.findtext("year"),
            "manufacturer": machine.findtext("manufacturer"),
            "sourcefile": sourcefile,
            "machine_config": config,
        },
        "romset_source": "MAME -listxml",
        "regions": rom_regions,
        "properties": {"protection": protection},
    }
    if protection != "none":
        manifest["runnable"] = False
    return manifest


def read_machines(stream) -> list[ET.Element]:
    machines = []
    for _event, element in ET.iterparse(stream, events=("end",)):
        if element.tag != "machine":
            continue
        source = element.get("sourcefile", "")
        module = SOURCE_FILES.get(source)
        if module:
            machines.append(element)
        else:
            element.clear()
    return machines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mame-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--xml", type=Path, help="existing output from mame -listxml")
    source.add_argument("--mame-bin", help="MAME executable used to run -listxml")
    args = parser.parse_args()

    configs = game_configs(args.mame_source)
    wrappers = config_boards(args.mame_source)
    if args.xml:
        stream = args.xml.open("rb")
        process = None
    else:
        process = subprocess.Popen([args.mame_bin, "-listxml"], stdout=subprocess.PIPE)
        assert process.stdout is not None
        stream = process.stdout
    try:
        machines = read_machines(stream)
    finally:
        stream.close()
    if process and process.wait():
        raise SystemExit("MAME -listxml failed")

    manifests = []
    unresolved = []
    machine_index = {machine.get("name", ""): machine for machine in machines}
    for machine in machines:
        board, config = classify_board(machine, configs, wrappers, machine_index)
        if not board:
            unresolved.append(machine.get("name", ""))
            continue
        module = "system18" if board == "system18" else "system16"
        manifests.append(build_manifest(machine, module, board, config))

    if unresolved:
        print("Could not infer board for: " + ", ".join(unresolved), file=sys.stderr)
        return 2

    for manifest in manifests:
        existing = next(args.output.rglob(f"{manifest['game']}.json"), None) if args.output.exists() else None
        if existing:
            old = json.loads(existing.read_text())
            # Keep one canonical MAME layout at the top level. The runtime's
            # hand-authored transforms are an optional, separate projection.
            runtime = old.get("runtime", {})
            if "regions" in old and any(
                "segments" in rom
                for region in old["regions"].values()
                for rom in region.get("roms", [])
            ):
                runtime["regions"] = old["regions"]
            old.update({key: value for key, value in manifest.items()
                        if key not in {"properties", "runnable"}})
            if runtime.get("regions"):
                old["runtime"] = runtime
            else:
                old.pop("runtime", None)
            old.pop("mame_roms", None)
            protection = manifest["properties"]["protection"]
            old.setdefault("properties", {})["protection"] = protection
            if protection != "none":
                old["runnable"] = False
            elif old.get("runnable") is False and old.get("romset_source") == "MAME -listxml":
                old.pop("runnable", None)
            existing.write_text(json.dumps(old, indent=2, ensure_ascii=False) + "\n")
        else:
            directory = args.output / "sega" / manifest["module"] / manifest["board"]
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f"{manifest['game']}.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

    print(f"Imported {len(manifests)} System 16/18 game entries (MAME machine registrations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
