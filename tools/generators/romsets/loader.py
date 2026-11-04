from __future__ import annotations

import json
from pathlib import Path
import zipfile
import zlib


def load_manifest(path: Path, *, module: str | None = None, board: str | None = None,
                  game: str | None = None) -> dict:
    try:
        manifest = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read ROM-set manifest {path}: {exc}") from exc
    for key, expected in (("module", module), ("board", board), ("game", game)):
        if expected is not None and manifest.get(key) != expected:
            raise ValueError(f"{path}: expected {key}={expected!r}, got {manifest.get(key)!r}")
    return manifest


def _manifest_catalog(root: Path, module: str | None) -> dict[str, dict]:
    result = {}
    for path in root.rglob("*.json"):
        try:
            manifest = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        if manifest.get("game") and (module is None or manifest.get("module") == module):
            result[manifest["game"]] = manifest
    return result


def _archive_path(input_path: Path, game: str) -> Path:
    return input_path if input_path.is_file() else input_path / f"{game}.zip"


def _family_ids(game: str, catalog: dict[str, dict]) -> tuple[list[str], set[str]]:
    parents = []
    current = game
    seen = {game}
    while current in catalog:
        parent = catalog[current].get("parent") or catalog[current].get("romof")
        if not parent or parent in seen:
            break
        parents.append(parent)
        seen.add(parent)
        current = parent
    root = current

    def family_root(game_id: str) -> str:
        current_id = game_id
        chain_seen = set()
        while current_id in catalog and current_id not in chain_seen:
            chain_seen.add(current_id)
            parent_id = catalog[current_id].get("parent") or catalog[current_id].get("romof")
            if not parent_id:
                return current_id
            current_id = parent_id
        return current_id

    related = {game_id for game_id in catalog if family_root(game_id) == root}
    return parents, related


def _candidate_archives(game: str, manifest: dict, input_path: Path,
                        manifest_root: Path, mode: str) -> list[Path]:
    if mode not in {"auto", "non-merged", "split", "merged"}:
        raise ValueError(f"unknown ROM-set packaging mode: {mode}")
    selected = _archive_path(input_path, game)
    if mode == "non-merged":
        return [selected]
    catalog = _manifest_catalog(manifest_root, manifest.get("module"))
    catalog.setdefault(game, manifest)
    parents, related = _family_ids(game, catalog)
    ids = [game]
    if mode in {"auto", "split", "merged"}:
        ids.extend(parents)
    if mode == "auto":
        ids.extend(sorted(related - set(ids)))

    base_dir = input_path.parent if input_path.is_file() else input_path
    candidates = [selected]
    seen = {selected.resolve()}
    for game_id in ids[1:]:
        path = base_dir / f"{game_id}.zip"
        if path.resolve() not in seen:
            candidates.append(path)
            seen.add(path.resolve())
    return candidates


def _read_checked_member(candidates: list[Path], rom: dict, cache: dict) -> bytes:
    expected_size = int(rom["size"])
    expected_crc = int(rom["crc32"], 16)
    names = list(dict.fromkeys(name for name in (rom["file"], rom.get("merge"), *rom.get("family_aliases", [])) if name))
    for archive_path in candidates:
        if not archive_path.is_file():
            continue
        try:
            key = archive_path.resolve()
            if key not in cache:
                cache[key] = zipfile.ZipFile(archive_path)
            archive = cache[key]
        except (OSError, zipfile.BadZipFile):
            continue
        members = {Path(name).name: name for name in archive.namelist()}
        for name in names:
            member = members.get(name)
            if member is None:
                continue
            data = archive.read(member)
            if len(data) == expected_size and (zlib.crc32(data) & 0xFFFFFFFF) == expected_crc:
                return data
    raise ValueError(f"missing compatible ROM {rom['file']} ({expected_size:#x}/{expected_crc:08x})")


def _transform(data: bytes, name: str | None) -> bytes:
    if name is None or name == "identity":
        return data
    if name == "word_swap":
        if len(data) & 1:
            raise ValueError("word_swap ROM has odd length")
        out = bytearray(data)
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out)
    raise ValueError(f"unsupported ROM transform: {name}")


def load_regions(manifest: dict, input_path: Path, manifest_root: Path,
                 *, mode: str = "auto") -> dict[str, bytes]:
    candidates = _candidate_archives(manifest["game"], manifest, input_path, manifest_root, mode)
    catalog = _manifest_catalog(manifest_root, manifest.get("module"))
    catalog.setdefault(manifest["game"], manifest)
    _parents, related = _family_ids(manifest["game"], catalog)
    related_rom_names: dict[tuple[int, str], set[str]] = {}
    for related_game in related - {manifest["game"]}:
        other = catalog[related_game]
        for region in other.get("regions", {}).values():
            for other_rom in region.get("roms", []):
                key = (int(other_rom["size"]), other_rom["crc32"].lower())
                related_rom_names.setdefault(key, set()).add(other_rom["file"])
    cache = {}
    output = {}
    try:
        runtime_regions = manifest.get("runtime", {}).get("regions")
        regions = runtime_regions if runtime_regions is not None else manifest.get("regions", {})
        for region_name, region in regions.items():
            size = int(region["size"])
            fill = int(region.get("fill", 0))
            target = bytearray([fill]) * size
            for rom in region.get("roms", []):
                alias_key = (int(rom["size"]), rom["crc32"].lower())
                rom["family_aliases"] = sorted(related_rom_names.get(alias_key, set()) - {rom["file"]})
                data = _transform(_read_checked_member(candidates, rom, cache), rom.get("transform"))
                for segment in rom.get("segments", []):
                    src = int(segment.get("source_offset", 0))
                    dest = int(segment["destination"])
                    length = int(segment["length"])
                    stride = int(segment.get("stride", 1))
                    group = int(segment.get("group_bytes", 1))
                    if group < 1 or stride < group or length < 0 or length % group:
                        raise ValueError(f"{manifest['game']}/{region_name}: invalid segment geometry")
                    if src < 0 or src + length > len(data) or dest < 0:
                        raise ValueError(f"{manifest['game']}/{region_name}: segment exceeds source ROM")
                    final = dest + ((length - group) // group) * stride + group if length else dest
                    if final > size:
                        raise ValueError(f"{manifest['game']}/{region_name}: segment exceeds target region")
                    for offset in range(0, length, group):
                        target[dest + (offset // group) * stride:dest + (offset // group) * stride + group] = \
                            data[src + offset:src + offset + group]
            output[region_name] = bytes(target)
        return output
    finally:
        for archive in cache.values():
            archive.close()
