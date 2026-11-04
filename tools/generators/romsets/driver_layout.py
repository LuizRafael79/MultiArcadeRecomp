"""Extract ROM layout directives from MAME ROM_START blocks."""
from __future__ import annotations

import re
from pathlib import Path


CALL = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(")
ROM_START = re.compile(r"\bROM_START\s*\(\s*([A-Za-z0-9_]+)\s*\)")
ROM_ALIAS = re.compile(r"^\s*#\s*define\s+rom_([A-Za-z0-9_]+)\s+rom_([A-Za-z0-9_]+)\s*$", re.MULTILINE)
NEO_MACRO_REGION = {
    "NEO_SFIX_32K": "fixed", "NEO_SFIX_64K": "fixed",
    "NEO_SFIX_128K": "fixed", "NEO_SFIX_256K": "fixed",
    "NEO_BIOS_AUDIO_64K": "audiocpu", "NEO_BIOS_AUDIO_128K": "audiocpu",
    "NEO_BIOS_AUDIO_256K": "audiocpu", "NEO_BIOS_AUDIO_512K": "audiocpu",
}


def strip_comments(text: str) -> str:
    out = list(text)
    i = 0
    state = "normal"
    while i < len(text):
        char = text[i]
        next_char = text[i + 1] if i + 1 < len(text) else ""
        if state == "normal":
            if char == '"':
                state = "string"
            elif char == "/" and next_char == "/":
                out[i] = out[i + 1] = " "
                state = "line"
                i += 1
            elif char == "/" and next_char == "*":
                out[i] = out[i + 1] = " "
                state = "block"
                i += 1
        elif state == "string":
            if char == "\\":
                i += 1
            elif char == '"':
                state = "normal"
        elif state == "line":
            if char == "\n":
                state = "normal"
            else:
                out[i] = " "
        elif state == "block":
            if char == "*" and next_char == "/":
                out[i] = out[i + 1] = " "
                state = "normal"
                i += 1
            elif char != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def call_end(text: str, open_paren: int) -> int | None:
    depth = 0
    quoted = False
    escaped = False
    for index in range(open_paren, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
    return None


def split_args(text: str) -> list[str]:
    args: list[str] = []
    start = 0
    depth = 0
    quoted = False
    escaped = False
    for index, char in enumerate(text):
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            args.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        args.append(tail)
    return args


def integer_expression(value: str) -> int | None:
    value = value.strip()
    try:
        return int(value, 0)
    except ValueError:
        return None


def local_macros(text: str) -> dict[str, tuple[list[str] | None, str]]:
    """Read source-local C macros so ROM lists factored into helpers survive."""
    joined = re.sub(r"\\\s*\n", " ", text)
    macros = {}
    define = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)(\(([^)]*)\))?\s*(.*?)\s*$", re.MULTILINE)
    for match in define.finditer(joined):
        params = None if match.group(2) is None else [p.strip() for p in match.group(3).split(",") if p.strip()]
        macros[match.group(1)] = (params, match.group(4))
    return macros


def expand_local_macros(text: str, macros: dict[str, tuple[list[str] | None, str]]) -> str:
    """Expand simple source-local object/function macros inside ROM_START."""
    for _ in range(16):
        changed = False
        for name, (params, body) in macros.items():
            if params is None:
                updated, count = re.subn(rf"\b{re.escape(name)}\b", body, text)
            else:
                pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
                pieces = []
                cursor = 0
                count = 0
                for match in pattern.finditer(text):
                    close = call_end(text, match.end() - 1)
                    if close is None:
                        continue
                    values = split_args(text[match.end():close])
                    if len(values) != len(params):
                        continue
                    expanded = body
                    for param, value in zip(params, values):
                        expanded = re.sub(rf"\b{re.escape(param)}\b", value, expanded)
                    pieces.extend((text[cursor:match.start()], expanded))
                    cursor = close + 1
                    count += 1
                if count:
                    pieces.append(text[cursor:])
                    updated = "".join(pieces)
                else:
                    updated = text
            if count:
                text = updated
                changed = True
        if not changed:
            break
    return text


def macro_calls(text: str, start: int, end: int):
    pos = start
    while pos < end:
        match = CALL.search(text, pos, end)
        if not match:
            return
        open_paren = match.end() - 1
        close = call_end(text, open_paren)
        if close is None or close > end:
            return
        line = text.count("\n", 0, match.start()) + 1
        yield match.group(1), split_args(text[open_paren + 1:close]), line
        pos = close + 1


def parse_driver(path: Path) -> tuple[dict[str, dict], dict[str, str]]:
    clean = strip_comments(path.read_text(errors="replace"))
    macros = local_macros(clean)
    aliases = {alias: target for alias, target in ROM_ALIAS.findall(clean)}
    layouts = {}
    starts = list(ROM_START.finditer(clean))
    for index, match in enumerate(starts):
        name = match.group(1)
        block_end_match = re.search(r"\bROM_END\b", clean[match.end():])
        if not block_end_match:
            continue
        start = match.end()
        end = start + block_end_match.start()
        block = expand_local_macros(clean[start:end], macros)
        source_line_base = clean.count("\n", 0, start)
        regions: list[dict] = []
        operations: list[dict] = []
        current_region = None
        last_file = None
        for macro, args, line in macro_calls(block, 0, len(block)):
            line += source_line_base
            if macro.startswith("ROM_REGION"):
                if len(args) >= 2:
                    size_expression = args[0].strip()
                    current_region = {
                        "name": args[1].strip().strip('"'),
                        "size_expression": size_expression,
                        "size": integer_expression(size_expression),
                        "region_macro": macro,
                        "flags_expression": args[2].strip() if len(args) > 2 else "0",
                        "source_line": line,
                    }
                    regions.append(current_region)
                    operations.append({"macro": macro, "region": current_region["name"],
                                       "size_expression": size_expression,
                                       "flags_expression": current_region["flags_expression"],
                                       "source_line": line})
                continue

            if macro in NEO_MACRO_REGION:
                current_region = {"name": NEO_MACRO_REGION[macro], "macro_region": macro}
                regions.append({"name": current_region["name"], "size_expression": None,
                                "size": None, "region_macro": macro,
                                "flags_expression": None, "source_line": line})
                file_match = re.match(r'\s*"([^"\\]*(?:\\.[^"\\]*)*)"', args[0]) if args else None
                operation = {"macro": macro, "region": current_region["name"], "source_line": line,
                             "arguments": args}
                if file_match:
                    operation["file"] = file_match.group(1)
                operations.append(operation)
                last_file = operation if file_match else None
                continue

            if macro.startswith("ROM_LOAD") or macro == "ROMX_LOAD":
                file_match = re.match(r'\s*"([^"\\]*(?:\\.[^"\\]*)*)"', args[0]) if args else None
                if not file_match:
                    operations.append({"macro": macro, "region": current_region.get("name") if current_region else None,
                                       "arguments": args, "source_line": line})
                    continue
                offset_index = 2 if "_BIOS" in macro and len(args) >= 4 else 1
                length_index = offset_index + 1
                offset_expression = args[offset_index].strip() if len(args) > offset_index else "0"
                length_expression = args[length_index].strip() if len(args) > length_index else "0"
                operation = {
                    "macro": macro,
                    "region": current_region.get("name") if current_region else None,
                    "file": file_match.group(1),
                    "offset_expression": offset_expression,
                    "offset": integer_expression(offset_expression),
                    "length_expression": length_expression,
                    "length": integer_expression(length_expression),
                    "source_line": line,
                }
                # -listxml preserves file hashes but loses the ROM macro's
                # bus wiring. Record the geometry implied by common MAME
                # load macros alongside their original arguments.
                geometry = {
                    "ROM_LOAD16_WORD_SWAP": (2, 2),
                    "ROM_LOAD16_WORD": (2, 2),
                    "ROM_LOAD16_BYTE": (1, 2),
                    "ROM_LOAD32_BYTE": (1, 4),
                    "ROM_LOAD32_WORD": (2, 4),
                    "ROM_LOAD32_WORD_SWAP": (2, 4),
                    "ROM_LOAD64_BYTE": (1, 8),
                    "ROM_LOAD64_WORD": (2, 8),
                    "ROM_LOAD64_WORD_SWAP": (2, 8),
                }
                base_macro = macro.removesuffix("_BIOS")
                group_bytes, stride = geometry.get(base_macro, (1, 1))
                operation["group_bytes"] = group_bytes
                operation["stride"] = stride
                operation["source_offset"] = 0
                operation["segments"] = [{
                    "source_offset": 0,
                    "destination": operation["offset"],
                    "length": operation["length"],
                    "group_bytes": group_bytes,
                    "stride": stride,
                }]
                operation["source_bytes_consumed"] = operation["length"] or 0
                if "WORD_SWAP" in macro:
                    operation["transform"] = "word_swap"
                crc = re.search(r"\bCRC\(([0-9a-fA-F]+)\)", " ".join(args))
                sha1 = re.search(r"\bSHA1\(([0-9a-fA-F]+)\)", " ".join(args))
                if crc:
                    operation["crc32"] = crc.group(1).lower()
                if sha1:
                    operation["sha1"] = sha1.group(1).lower()
                operations.append(operation)
                last_file = operation
                continue

            if macro in {"ROM_CONTINUE", "ROM_RELOAD", "ROM_IGNORE"}:
                operation = {"macro": macro, "region": current_region.get("name") if current_region else None,
                             "arguments": args, "source_line": line}
                if len(args) >= 2:
                    operation["offset_expression"] = args[0].strip()
                    operation["offset"] = integer_expression(args[0])
                    operation["length_expression"] = args[1].strip()
                    operation["length"] = integer_expression(args[1])
                elif args:
                    operation["length_expression"] = args[0].strip()
                    operation["length"] = integer_expression(args[0])
                operations.append(operation)
                if last_file is not None:
                    last_file.setdefault("continuations", []).append(operation)
                    if operation["macro"] == "ROM_CONTINUE":
                        operation["source_offset"] = last_file["source_bytes_consumed"]
                        last_file.setdefault("segments", []).append({
                            "source_offset": operation["source_offset"],
                            "destination": operation.get("offset"),
                            "length": operation.get("length"),
                            "group_bytes": last_file["group_bytes"],
                            "stride": last_file["stride"],
                        })
                        last_file["source_bytes_consumed"] += operation.get("length") or 0
                    elif operation["macro"] == "ROM_RELOAD":
                        last_file.setdefault("segments", []).append({
                            "source_offset": 0,
                            "destination": operation.get("offset"),
                            "length": operation.get("length"),
                            "group_bytes": last_file["group_bytes"],
                            "stride": last_file["stride"],
                        })
                continue

            # Preserve any other ROM macros (BIOS selectors, fills, copies,
            # device-specific helpers) verbatim instead of guessing semantics.
            if macro.startswith(("ROM_", "NEO_", "NEOGEO_")):
                operations.append({"macro": macro, "region": current_region.get("name") if current_region else None,
                                   "arguments": args, "source_line": line})
        layouts[name] = {"source": path.name, "regions": regions, "operations": operations}
    return layouts, aliases
