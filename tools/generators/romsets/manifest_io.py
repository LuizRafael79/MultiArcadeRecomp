"""TOML I/O for ROM-set source manifests."""
from __future__ import annotations

import re
import tomllib
from pathlib import Path
from typing import Any


_BARE_KEY = re.compile(r"^[A-Za-z0-9_-]+$")


def load_manifest_file(path: Path) -> dict[str, Any]:
    try:
        text = path.read_text(encoding="utf-8")
        if path.suffix.lower() != ".toml":
            raise ValueError(f"unsupported manifest extension: {path.suffix}")
        value = tomllib.loads(text)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ValueError(f"cannot read ROM-set manifest {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"ROM-set manifest root must be a table: {path}")
    return value


def _key(value: str) -> str:
    if _BARE_KEY.fullmatch(value):
        return value
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return f'"{escaped}"'


def _value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
        return f'"{escaped}"'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            raise ValueError("non-finite numbers are not valid ROM manifest values")
        return repr(value)
    if isinstance(value, list):
        if any(isinstance(item, (dict, list)) for item in value):
            raise ValueError("nested arrays/tables must be emitted as TOML tables")
        return "[" + ", ".join(_value(item) for item in value) + "]"
    raise ValueError(f"unsupported TOML manifest value: {type(value).__name__}")


def dumps_toml(document: dict[str, Any]) -> str:
    """Serialize manifest-shaped mappings, including nested arrays of tables."""
    output: list[str] = []

    def emit_table(table: dict[str, Any], path: tuple[str, ...], *, array: bool = False) -> None:
        if path:
            if output and output[-1] != "":
                output.append("")
            prefix = "[[" if array else "["
            suffix = "]]" if array else "]"
            output.append(prefix + ".".join(_key(part) for part in path) + suffix)

        child_tables: list[tuple[str, dict[str, Any]]] = []
        child_arrays: list[tuple[str, list[dict[str, Any]]]] = []
        for name, value in table.items():
            if value is None:
                # TOML has no null value; omission preserves the manifest's
                # existing .get(key) / optional-field semantics.
                continue
            if isinstance(value, dict):
                child_tables.append((name, value))
            elif isinstance(value, list) and value and all(isinstance(item, dict) for item in value):
                child_arrays.append((name, value))
            else:
                output.append(f"{_key(name)} = {_value(value)}")

        for name, child in child_tables:
            emit_table(child, path + (name,))
        for name, rows in child_arrays:
            for row in rows:
                emit_table(row, path + (name,), array=True)

    emit_table(document, ())
    return "\n".join(output).rstrip() + "\n"


def write_manifest_file(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dumps_toml(document), encoding="utf-8")
