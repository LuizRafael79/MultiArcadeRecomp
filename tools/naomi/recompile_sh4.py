#!/usr/bin/env python3
"""Translate declared SH-4 function ranges from a decrypted Naomi code image.

Manifest example: {"set_id":"mvsc2u", "base_address":"0x0c000000",
"entry":"0x0c021000", "functions":[{"address":"0x0c021000", "length":64}]}.
The explicit ranges are temporary until an SH-4 code/data analyzer is ready.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "core" / "third_party" / "initiald_sh4recomp"))
import codegen  # noqa: E402
from sh4recomp import decode  # noqa: E402


def number(value: object, label: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"{label} must be an integer or 0x-prefixed string")


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def emit(image: bytes, manifest: dict, symbol_prefix: str = "") -> str:
    set_id = str(manifest.get("set_id", "naomi")).lower()
    if not re.fullmatch(r"[a-z0-9_]+", set_id):
        raise ValueError("set_id may contain lowercase letters, digits, and underscores")
    if symbol_prefix and not re.fullmatch(r"[a-z0-9_]+", symbol_prefix):
        raise ValueError("symbol_prefix may contain lowercase letters, digits, and underscores")
    def symbol(address: int) -> str:
        prefix = f"{symbol_prefix}_" if symbol_prefix else ""
        return f"FUN_{prefix}{address:08X}"
    image_base = number(manifest.get("base_address", 0), "base_address")
    entry = number(manifest["entry"], "entry")
    specs = manifest.get("functions")
    if not isinstance(specs, list) or not specs:
        raise ValueError("manifest needs a non-empty functions array")

    translated_addresses = {number(spec["address"], "function address") for spec in specs}
    external_functions = {
        number(value, "external function address")
        for value in manifest.get("external_functions", [])
    }
    resolved_calls = {}
    for site, target in manifest.get("resolved_calls", {}).items():
        target_text = str(target)
        target_address = int(target_text, 0) if target_text.lower().startswith("0x") else int(target_text, 16)
        if target_address in translated_addresses:
            resolved_calls[str(site).lower().removeprefix("0x")] = f"{target_address:08x}"

    functions: dict[int, str] = {}
    code_guards: dict[int, tuple[int, int, int]] = {}
    aliases: dict[int, int] = {}
    entry_candidates: dict[int, list[int]] = {}
    entry_candidate_scores: dict[int, int] = {}
    for spec in specs:
        address = number(spec["address"], "function address")
        code_base = number(spec.get("base_address", address), "function base_address")
        length = number(spec["length"], "function length")
        # P1/P2 aliases name the same physical RAM, but guest code can observe
        # the alias in PC-relative addresses. Keep the guest address for code
        # generation while normalizing only the image lookup.
        offset = (code_base & 0x1fffffff) - (image_base & 0x1fffffff)
        if address & 1 or code_base & 1 or length <= 0 or length & 1:
            raise ValueError(f"unaligned or empty function range at 0x{address:08x}")
        if offset < 0 or offset + length > len(image):
            raise ValueError(f"function 0x{address:08x} lies outside the image")
        if address in functions:
            raise ValueError(f"duplicate function address 0x{address:08x}")
        raw = image[offset:offset + length]
        reachable = spec.get("reachable_addrs")
        reachable_addrs = None if reachable is None else {
            number(value, "reachable address") for value in reachable
        }
        if reachable_addrs is not None and (address not in reachable_addrs or
                                             any(value < code_base or value >= code_base + length
                                                 for value in reachable_addrs)):
            raise ValueError(f"reachable instruction set is outside function 0x{address:08x}")
        for i in range(0, length, 2):
            opcode = raw[i] | (raw[i + 1] << 8)
            mnemonic, _args = decode(opcode)
            guest_pc = code_base + i
            if mnemonic == "unk" and (reachable_addrs is None or guest_pc in reachable_addrs):
                raise ValueError(f"unknown opcode 0x{opcode:04x} at 0x{guest_pc:08x}")
        # SH-4 interrupts are checked before each non-delay-slot instruction.
        # RTE can therefore resume at any reachable instruction boundary, even
        # an ordinary fallthrough address. Export those boundaries as entries;
        # a delay-slot instruction is not interruptible and must remain paired
        # with its branch. Explicit branch targets are added below as well.
        entry_addrs = {address}
        if reachable_addrs is not None:
            delay_slot_addrs = set()
            for offset in range(0, length, 2):
                guest_pc = code_base + offset
                if guest_pc not in reachable_addrs:
                    continue
                opcode = raw[offset] | (raw[offset + 1] << 8)
                mnemonic, fields = decode(opcode)
                if mnemonic in {"bra", "bsr", "bt_s", "bf_s", "jmp", "jsr",
                                "braf", "bsrf", "rts", "rte"}:
                    delay_slot_addrs.add(guest_pc + 2)
                if mnemonic in {"bra", "bsr", "bt", "bf", "bt_s", "bf_s"}:
                    target = guest_pc + fields["disp"]
                    if target in reachable_addrs:
                        entry_addrs.add(target)
                if mnemonic in {"bsr", "bsrf", "jsr", "jsrf"}:
                    return_pc = guest_pc + 4
                    if return_pc in reachable_addrs:
                        entry_addrs.add(return_pc)
                if mnemonic in {"bt", "bf"}:
                    fallthrough = guest_pc + 2
                    if fallthrough in reachable_addrs:
                        entry_addrs.add(fallthrough)
                elif mnemonic in {"bt_s", "bf_s"}:
                    fallthrough = guest_pc + 4
                    if fallthrough in reachable_addrs:
                        entry_addrs.add(fallthrough)
            entry_addrs.update(reachable_addrs - delay_slot_addrs)
        text = codegen.recompile_function(
            symbol(address), code_base, raw,
            resolved_calls=resolved_calls,
            reachable_addrs=reachable_addrs,
            entry_addr=address,
            entry_addrs=entry_addrs if entry_addrs is not None else {address})
        if "UNHANDLED:" in text:
            raise ValueError(f"function 0x{address:08x} has an opcode without codegen")
        functions[address] = text
        code_guards[address] = (code_base, len(raw) // 2, fnv1a64(raw))
        entry_candidate_scores[address] = len(reachable_addrs or {address})
        for reachable_address in (entry_addrs or {address}):
            entry_candidates.setdefault(reachable_address, []).append(address)
        for alias in spec.get("aliases", []):
            alias_address = number(alias, "function alias")
            if alias_address in aliases or alias_address in functions:
                raise ValueError(f"duplicate function alias 0x{alias_address:08x}")
            aliases[alias_address] = address

    if entry not in functions:
        raise ValueError("entry must be included in functions")
    body = "\n\n".join(functions[a] for a in sorted(functions))
    if symbol_prefix:
        def replace_symbol(match: re.Match[str]) -> str:
            address = int(match.group(1), 16)
            return symbol(address) if address in translated_addresses else match.group(0)

        # Apply the prefix in one pass. Re-scanning the full generated body
        # once per translated function made large Naomi ROM manifests
        # effectively quadratic in output size.
        body = re.sub(r"\bFUN_([0-9A-Fa-f]{8})\b", replace_symbol, body)
    helpers = set(re.findall(r"\b((?:host_[A-Za-z0-9_]+|load_sr|shim_[A-Za-z0-9_]+))\s*\(", body))
    implemented_helpers = {
        "host_indirect_call", "load_sr", "host_rte", "host_div1", "host_load_fpscr",
        "host_frchg", "host_fschg", "host_fldi", "host_fsts_fpul",
        "host_flds_fpul", "host_fmov_reg", "host_fmov_load",
        "host_fmov_store", "host_fmov_load_postinc", "host_fmov_store_predec",
        "host_fadd", "host_fsub", "host_fmul", "host_fdiv",
        "host_fcmp_eq", "host_fcmp_gt", "host_fneg", "host_fabs",
        "host_fsqrt", "host_float_fpul", "host_ftrc_fpul", "host_fcnvsd",
        "host_fcnvds", "host_fmac", "host_fipr", "host_ftrv", "host_fsrra",
        "host_fsca", "host_pref", "host_trapa", "host_store_sr",
    }
    unsupported = helpers - implemented_helpers
    if unsupported:
        raise ValueError("runtime helpers not implemented: " + ", ".join(sorted(unsupported)))
    calls = {int(x, 16) for x in re.findall(
        r"\bFUN_(?:[a-z0-9_]+_)?([0-9A-Fa-f]{8})\s*\(", body)}
    missing = calls - functions.keys() - external_functions
    if missing:
        raise ValueError("direct call target absent from manifest: " + ", ".join(
            f"0x{x:08x}" for x in sorted(missing)))

    lines = ["// Generated by tools/naomi/recompile_sh4.py; do not edit.",
             '#include "naomi/naomi_sh4_context.hpp"',
             '#include "naomi/naomi_sh4_helpers.hpp"', "#include <cstdint>",
             "", "namespace mamerecomp::naomi {"]
    for address in sorted(functions.keys() | external_functions):
        function_name = symbol(address) if address in functions else f"FUN_{address:08X}"
        lines.append(f"void {function_name}(Sh4Context& ctx);")
    lines += ["", body, "", f"void mamerecomp_register_sh4_{set_id}(Sh4Dispatcher& d) {{"]
    for address in sorted(functions):
        base, word_count, code_hash = code_guards[address]
        lines.append(
            f"    d.add(0x{address:08X}u, &{symbol(address)}, "
            f"0x{base:08X}u, {word_count}u, 0x{code_hash:016X}ull);")
    # Interrupt/RTE can resume at any translated instruction boundary, not
    # only at function roots. Map every statically reachable instruction to a
    # single generated function that can enter at that PC. Prefer the function
    # rooted at the PC itself; otherwise choose the candidate with the broadest
    # reachable CFG so its next branch/call is less likely to fall off AOT.
    for guest_pc, candidates in sorted(entry_candidates.items()):
        owner = guest_pc if guest_pc in functions else max(
            candidates,
            key=entry_candidate_scores.__getitem__,
        )
        base, word_count, code_hash = code_guards[owner]
        lines.append(
            f"    d.add(0x{guest_pc:08X}u, &{symbol(owner)}, "
            f"0x{base:08X}u, {word_count}u, 0x{code_hash:016X}ull);")
    for alias, address in sorted(aliases.items()):
        base, word_count, code_hash = code_guards[address]
        lines.append(
            f"    d.add(0x{alias:08X}u, &{symbol(address)}, "
            f"0x{base:08X}u, {word_count}u, 0x{code_hash:016X}ull);")
    lines += ["}", f"std::uint32_t mamerecomp_sh4_entry_{set_id}() {{ return 0x{entry:08X}u; }}",
              "} // namespace mamerecomp::naomi", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path, help="decrypted guest code image")
    parser.add_argument("--manifest", required=True, type=Path, help="JSON function ranges")
    parser.add_argument("--output", required=True, type=Path, help="generated C++ output")
    parser.add_argument("--symbol-prefix", default="",
                        help="optional unique prefix for generated function symbols")
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        output = emit(args.image.read_bytes(), manifest, args.symbol_prefix)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Avoid touching an identical generated translation so incremental
        # builds can reuse its existing object file.
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
            args.output.write_text(output, encoding="utf-8")
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        print(f"SH-4 translation failed: {exc}", file=sys.stderr)
        return 2
    print(f"Generated {args.output} for {manifest.get('set_id', 'naomi')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
