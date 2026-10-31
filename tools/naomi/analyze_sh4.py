#!/usr/bin/env python3
"""Discover reachable SH-4 code and emit an explicit C++ generator manifest.

This is an initial static analyzer: it follows direct control flow, recursively
queues direct calls, resolves a small set of register-constant operations for
indirect transfers, and preserves the guest P1/P2 address used at each entry.
It stops at unknown instructions/indirect targets and reports them instead of
pretending that data bytes are code.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "core" / "third_party" / "initiald_sh4recomp"))
from sh4recomp import decode  # noqa: E402


UNKNOWN = None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path, help="decrypted code image")
    parser.add_argument("--base", required=True, help="physical SH-4 address of image offset zero")
    parser.add_argument("--entry", required=True, help="guest entry address")
    parser.add_argument("--set-id", default="naomi")
    parser.add_argument("--output", required=True, type=Path, help="translation manifest JSON")
    parser.add_argument("--max-instructions", type=int, default=250000,
                        help="global reachable instruction limit")
    args = parser.parse_args()

    image = args.image.read_bytes()
    base = int(args.base, 0) & 0x1fffffff
    entry = int(args.entry, 0) & 0xffffffff
    functions: dict[int, dict] = {}
    unresolved: list[dict] = []
    calls: dict[str, str] = {}
    # REIOS state from naomi_reios_boot.cpp. Seeding these constants matters:
    # the entry trampoline computes its first P2 alias and dispatch targets
    # from the registers established by the BIOS-free boot path.
    reios_regs = (0x0c021000, 0x0c01f820, 0xa0710004, 0x0c01f130,
                  0x5bfccd08, 0xa05f7000, 0xa05f7008, 0x00000007,
                  0x00000000, 0x00002000, 0xffffffff, 0x0c0e0000,
                  0x00000000, 0x00000000, 0x00000000, 0x0cc00000)
    function_queue = deque([(entry, reios_regs)])
    queued = {entry}
    total_insns = 0

    def image_offset(address: int) -> int | None:
        offset = (address & 0x1fffffff) - base
        return offset if 0 <= offset <= len(image) - 2 else None

    def op_at(address: int):
        offset = image_offset(address)
        if offset is None:
            return None, None
        opcode = struct.unpack_from("<H", image, offset)[0]
        return opcode, decode(opcode)

    def word_at(address: int) -> int | None:
        offset = image_offset(address)
        if offset is None or offset > len(image) - 4:
            return None
        return struct.unpack_from("<I", image, offset)[0]

    def half_at(address: int) -> int | None:
        offset = image_offset(address)
        if offset is None or offset > len(image) - 2:
            return None
        value = struct.unpack_from("<H", image, offset)[0]
        return value - 0x10000 if value & 0x8000 else value

    def byte_at(address: int) -> int | None:
        offset = image_offset(address)
        if offset is None:
            return None
        value = image[offset]
        return value - 0x100 if value & 0x80 else value

    def queue_function(target: int, reason: str, source: int,
                       incoming_regs: tuple | None = None) -> bool:
        nonlocal unresolved
        target &= 0xffffffff
        if image_offset(target) is None:
            unresolved.append({"source": f"0x{source:08x}", "target": f"0x{target:08x}",
                               "kind": reason, "why": "outside supplied code image"})
            return False
        if target not in queued:
            queued.add(target)
            function_queue.append((target, (None,) * 16 if incoming_regs is None else incoming_regs))
        return True

    def simulate(mnemonic: str, fields: dict, pc: int, regs: tuple) -> tuple:
        r = list(regs)
        n, m = fields.get("n"), fields.get("m")
        def set_n(value):
            if n is not None: r[n] = None if value is None else value & 0xffffffff
        if mnemonic == "movi": set_n(fields["imm"])
        elif mnemonic == "mova": r[0] = ((pc & ~3) + 4 + fields["disp"]) & 0xffffffff
        elif mnemonic == "mov" and n is not None and m is not None: r[n] = r[m]
        elif mnemonic == "movl_pcrel": set_n(word_at(((pc & ~3) + 4 + fields["disp"]) & 0xffffffff))
        elif mnemonic == "movw_pcrel": set_n(half_at(((pc & ~3) + 4 + fields["disp"]) & 0xffffffff))
        elif mnemonic == "movl_ld" and m is not None and r[m] is not None: set_n(word_at(r[m]))
        elif mnemonic == "movl_ld_disp" and m is not None and r[m] is not None:
            set_n(word_at((r[m] + fields["disp"]) & 0xffffffff))
        elif mnemonic in {"movl_ld_postinc", "movw_ld_postinc", "movb_ld_postinc"} and m is not None:
            width = 4 if mnemonic == "movl_ld_postinc" else 2 if mnemonic == "movw_ld_postinc" else 1
            value = None if r[m] is None else (
                word_at(r[m]) if width == 4 else half_at(r[m]) if width == 2 else byte_at(r[m]))
            set_n(value)
            if r[m] is not None and n != m:
                r[m] = (r[m] + width) & 0xffffffff
        elif mnemonic in {"movw_ld", "movb_ld"} and m is not None and r[m] is not None:
            set_n(half_at(r[m]) if mnemonic == "movw_ld" else byte_at(r[m]))
        elif mnemonic in {"movw_ld_r0", "movb_ld_r0"} and m is not None and \
                r[0] is not None and r[m] is not None:
            address = (r[0] + r[m]) & 0xffffffff
            set_n(half_at(address) if mnemonic == "movw_ld_r0" else byte_at(address))
        elif mnemonic == "movl_ld_r0" and m is not None and r[0] is not None and r[m] is not None:
            set_n(word_at((r[0] + r[m]) & 0xffffffff))
        elif mnemonic == "and" and n is not None and m is not None:
            set_n(None if r[n] is None or r[m] is None else r[n] & r[m])
        elif mnemonic == "or" and n is not None and m is not None:
            set_n(None if r[n] is None or r[m] is None else r[n] | r[m])
        elif mnemonic == "xor" and n is not None and m is not None:
            set_n(None if r[n] is None or r[m] is None else r[n] ^ r[m])
        elif mnemonic == "add_imm" and n is not None:
            set_n(None if r[n] is None else r[n] + fields["imm"])
        elif mnemonic == "add" and n is not None and m is not None:
            set_n(None if r[n] is None or r[m] is None else r[n] + r[m])
        elif mnemonic.startswith("f"):
            # SH-4 floating-point instructions update FR/XF/FPUL state, not Rn.
            pass
        elif mnemonic in {"movl_st", "movl_st_predec", "movl_st_disp", "movl_st_r0",
                          "movb_st", "movb_st_predec", "movb_st_disp", "movb_st_r0",
                          "movw_st", "movw_st_predec", "movw_st_disp", "movw_st_r0",
                          "ldc_dbr", "ldcvbr", "ldcl_gbr", "ldcl_vbr", "ldcl_ssr", "ldcl_spc",
                          "nop", "cmp_eq_imm", "cmp_hs", "cmp_ge", "cmp_hi", "cmp_gt",
                          "tst", "cmp_str", "dt", "pref", "tas", "clrt", "sett"}:
            pass
        elif n is not None:
            # Conservatively invalidate writes not covered by the constant pass.
            if mnemonic not in {"cmp_eq", "cmp_hs", "cmp_ge", "cmp_hi", "cmp_gt", "tst", "cmp_str"}:
                set_n(None)
        return tuple(r)

    while function_queue and total_insns < args.max_instructions:
        function_entry, entry_regs = function_queue.popleft()
        if function_entry in functions:
            continue
        work = deque([(function_entry, entry_regs)])
        states_at: dict[int, tuple] = {}
        reachable: set[int] = set()
        local_calls: set[int] = set()
        static_jumps: set[int] = set()
        dynamic: list[dict] = []
        unknown: list[dict] = []
        low = function_entry
        high = function_entry

        while work and total_insns < args.max_instructions:
            pc, regs = work.popleft()
            if pc & 1:
                unknown.append({"pc": f"0x{pc:08x}", "reason": "odd instruction address"})
                continue
            if pc in states_at:
                previous = states_at[pc]
                merged = tuple(a if a == b else None for a, b in zip(previous, regs))
                if merged == previous:
                    continue
                states_at[pc] = merged
                regs = merged
            else:
                states_at[pc] = regs
            opcode, decoded = op_at(pc)
            if decoded is None:
                dynamic.append({"pc": f"0x{pc:08x}", "reason": "control flow left image"})
                continue
            word, (mnemonic, fields) = opcode, decoded
            if mnemonic == "unk":
                unknown.append({"pc": f"0x{pc:08x}", "opcode": f"0x{word:04x}"})
                continue
            if pc not in reachable:
                total_insns += 1
                reachable.add(pc)
            low, high = min(low, pc), max(high, pc)
            next_regs = simulate(mnemonic, fields, pc, regs)
            if mnemonic in {"lds_mem_dbr", "ldcl_gbr", "ldcl_vbr", "ldcl_ssr", "ldcl_spc"} and fields.get("n") is not None:
                regs_after = list(next_regs)
                if regs_after[fields["n"]] is not None:
                    regs_after[fields["n"]] = (regs_after[fields["n"]] + 4) & 0xffffffff
                next_regs = tuple(regs_after)

            def delay_state() -> tuple:
                nonlocal low, high, total_insns
                slot_pc = (pc + 2) & 0xffffffff
                slot_op, slot_decoded = op_at(slot_pc)
                if slot_pc not in reachable:
                    reachable.add(slot_pc)
                    total_insns += 1
                low, high = min(low, slot_pc), max(high, slot_pc)
                if slot_decoded is None or slot_decoded[0] == "unk":
                    unknown.append({"pc": f"0x{slot_pc:08x}",
                                    "opcode": None if slot_op is None else f"0x{slot_op:04x}",
                                    "reason": "unsupported delay-slot instruction"})
                    return next_regs
                return simulate(slot_decoded[0], slot_decoded[1], slot_pc, next_regs)

            if mnemonic == "rts":
                _ = delay_state()
                continue
            if mnemonic in {"bra", "bsr", "bt_s", "bf_s", "jmp", "jsr", "braf", "bsrf"}:
                after_delay = delay_state()
                fallthrough = (pc + 4) & 0xffffffff
                target = None
                if mnemonic in {"bra", "bsr"}:
                    target = (pc + fields["disp"]) & 0xffffffff
                elif mnemonic in {"jmp", "jsr"}:
                    reg = regs[fields["n"]] # target is sampled before the delay slot
                    target = reg
                elif mnemonic in {"braf", "bsrf"}:
                    reg = regs[fields["n"]]
                    target = None if reg is None else (pc + 4 + reg) & 0xffffffff
                elif mnemonic in {"bt_s", "bf_s"}:
                    target = (pc + fields["disp"]) & 0xffffffff
                if mnemonic in {"bra", "bt_s", "bf_s", "jmp", "braf"}:
                    if mnemonic in {"bt_s", "bf_s"}:
                        work.append((target, after_delay))
                        work.append((fallthrough, after_delay))
                    elif target is not None:
                        static_jumps.add(target)
                        queue_function(target, "tail-jump", pc, after_delay)
                    else:
                        dynamic.append({"pc": f"0x{pc:08x}", "kind": mnemonic,
                                        "register": fields.get("n"),
                                        "register_value": None if fields.get("n") is None or regs[fields["n"]] is None else f"0x{regs[fields['n']]:08x}",
                                        "reason": "target not constant"})
                    continue
                if mnemonic in {"bsr", "bsrf", "jsr"}:
                    if target is not None:
                        local_calls.add(target)
                        if mnemonic == "jsr": calls[f"{pc:08x}"] = f"{target:08x}"
                        queue_function(target, "call", pc, after_delay)
                    else:
                        dynamic.append({"pc": f"0x{pc:08x}", "kind": mnemonic,
                                        "register": fields.get("n"),
                                        "register_value": None if fields.get("n") is None or regs[fields["n"]] is None else f"0x{regs[fields['n']]:08x}",
                                        "reason": "target not constant"})
                    work.append((fallthrough, (None,) * 16))
                    continue
            if mnemonic in {"bt", "bf"}:
                target = (pc + fields["disp"]) & 0xffffffff
                work.append((target, next_regs))
                work.append(((pc + 2) & 0xffffffff, next_regs))
            else:
                work.append(((pc + 2) & 0xffffffff, next_regs))

        if reachable:
            functions[function_entry] = {
                "address": f"0x{low:08x}",
                "entry": f"0x{function_entry:08x}",
                "length": high - low + 2,
                "reachable_addrs": [f"0x{x:08x}" for x in sorted(reachable)],
                "calls": [f"0x{x:08x}" for x in sorted(local_calls)],
                "static_jumps": [f"0x{x:08x}" for x in sorted(static_jumps)],
                "dynamic_targets": dynamic,
                "unknown_opcodes": unknown,
            }

    manifest_functions = []
    for function_entry, info in sorted(functions.items()):
        manifest_functions.append({
            "address": info["entry"],
            "base_address": info["address"],
            "length": info["length"],
            "reachable_addrs": info["reachable_addrs"],
        })
    result = {
        "set_id": args.set_id,
        "base_address": f"0x{base:08x}",
        "entry": f"0x{entry:08x}",
        "functions": manifest_functions,
        "resolved_calls": calls,
        "analysis": {f"0x{k:08x}": v for k, v in functions.items()},
        "unresolved": unresolved,
        "instructions_analyzed": total_insns,
        "truncated_by_limit": bool(function_queue or total_insns >= args.max_instructions),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Analyzed {total_insns} SH-4 instructions across {len(functions)} functions; wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
