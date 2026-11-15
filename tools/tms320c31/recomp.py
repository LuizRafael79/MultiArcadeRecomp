#!/usr/bin/env python3
"""Translate guarded static C31 instructions from the V-Unit reset image.

Supported integer forms lower to generated C++ operations. Other legal
instruction groups retain statically selected handler calls until their
semantics are translated. Every candidate checks the live PC/opcode before
execution, so modified RAM code automatically falls back.
"""
from __future__ import annotations

import argparse
import re
import struct
from collections import deque
from pathlib import Path


CONTROL_PREFIXES = ("br", "call", "ret", "trap", "reti", "rpt", "dbc")
CONTROL_EXACT = {"idle", "maxspeed", "sigi", "swi"}
STORE_PREFIXES = ("st", "push", "pop", "iack")

# TI SPRU031F Appendix A gives the major opcode bits; Chapter 13's individual
# formats define the two additional high bits that select register/immediate
# encodings. This mask/value table recognizes only forms with direct lowering.
# It intentionally does not include parallel encodings (bit 31 = 1).
TI_DIRECT_MASK = 0x7ff
TI_DIRECT_OPCODE_NAMES = {
    0x000: "absf_reg", 0x003: "absf_imm",
    0x00c: "addf_reg", 0x00f: "addf_imm",
    0x010: "addi_reg", 0x013: "addi_imm",
    0x014: "and_reg", 0x017: "and_imm",
    0x020: "cmpf_reg", 0x023: "cmpf_imm",
    0x028: "fix_reg", 0x02b: "fix_imm",
    0x02c: "float_reg", 0x02f: "float_imm",
    0x040: "ldi_reg", 0x043: "ldi_imm",
    0x050: "mpyf_reg", 0x053: "mpyf_imm",
    0x05c: "negf_reg", 0x05f: "negf_imm",
    0x080: "or_reg", 0x083: "or_imm",
    0x0bc: "subf_reg", 0x0bf: "subf_imm",
    0x0c0: "subi_reg", 0x0c3: "subi_imm",
    0x0d4: "xor_reg", 0x0d7: "xor_imm",
    0x104: "addf3_regreg", 0x118: "cmpf3_regreg",
    0x124: "mpyf3_regreg",
    0x134: "subf3_regreg",
}


def decode_ti_direct(opcode: int) -> str | None:
    if opcode & 0x80000000:
        return None
    return TI_DIRECT_OPCODE_NAMES.get((opcode >> 21) & TI_DIRECT_MASK)


def decode_handlers(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    table = text.split("s_tms320c3x_ops[]", 1)[1].split("{", 1)[1].split("\n};", 1)[0]
    handlers = re.findall(r"&tms320c3x_device::([A-Za-z0-9_]+)", table)
    if len(handlers) != 2048:
        raise ValueError(f"expected 2048 opcode groups, found {len(handlers)}")
    return handlers


def is_control(name: str) -> bool:
    return name in CONTROL_EXACT or name.startswith(CONTROL_PREFIXES)


def is_native_safe(name: str) -> bool:
    # Fixed-handler code can preserve bus and side effects because it invokes
    # the exact MAME-derived semantic routine. Control flow gets a one-word
    # block; idle and unsupported groups remain interpreted.
    return name not in {"illegal", "unimplemented", "idle"}


def emit_direct_instruction(pc: int, opcode: int, name: str | None) -> list[str] | None:
    """Emit translated integer and common floating-point ops without handlers.

    This intentionally covers only general-register destinations. Special
    registers have write side effects in MAME's update_special() path and stay
    on the existing guarded handler path until translated explicitly.
    """
    # TI Appendix A assigns parallel instruction families to encodings with
    # bit 31 set. They represent paired operations with shared source
    # snapshots and deferred writes, so scalar lowering must reject them.
    if opcode & 0x80000000:
        return None

    dreg = (opcode >> 16) & 31

    float_ops = {
        "addf_reg": "addf", "addf3_regreg": "addf",
        "subf_reg": "subf", "subf3_regreg": "subf",
        "mpyf_reg": "mpyf", "mpyf3_regreg": "mpyf",
        "cmpf3_regreg": "subf",
    }
    float_immediate = {"addf_imm": "addf", "subf_imm": "subf",
                       "mpyf_imm": "mpyf", "absf_imm": "absf",
                       "cmpf_imm": "subf"}
    if name in float_ops:
        if name == "mpyf_reg":
            # MPYF register form can address the full CPU register file.
            # Until special-register writes are lowered, restrict its
            # destination to R0-R7 and preserve the full source field.
            if dreg >= 8:
                return None
            src2 = opcode & 31
        else:
            dreg &= 7
            src2 = opcode & 7
        if name.endswith("3_regreg"):
            src1, src2 = (opcode >> 8) & 7, opcode & 7
        else:
            src1 = dreg
        result_reg = 34 if name == "cmpf3_regreg" else dreg
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();",
            f"        const Register lhs = state.r[{src1}];",
            f"        const Register rhs = state.r[{src2}];",
            f"        native_ops::{float_ops[name]}(state, state.r[{result_reg}], lhs, rhs);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in {"absf_reg", "absf_imm"}:
        src = f"state.r[{opcode & 7}]" if name == "absf_reg" else "immediate_value"
        setup = ([] if name == "absf_reg" else [
            f"        Register immediate_value{{}}; native_ops::short_to_float(immediate_value, 0x{opcode & 0xffff:04x}u);"])
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();", *setup,
            f"        const Register source = {src};",
            f"        native_ops::absf(state, state.r[{(opcode >> 16) & 7}], source);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in float_immediate:
        dreg = (opcode >> 16) & (31 if name == "mpyf_imm" else 7)
        if name == "mpyf_imm" and dreg >= 8:
            return None
        result_reg = 34 if name.startswith("cmpf") else dreg
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();",
            "        Register immediate_value{}; native_ops::short_to_float(immediate_value, 0x%04xu);" % (opcode & 0xffff),
            f"        const Register lhs = state.r[{dreg}];",
            f"        native_ops::{float_immediate[name]}(state, state.r[{result_reg}], lhs, immediate_value);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in {"cmpf_reg"}:
        dreg = (opcode >> 16) & 7
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();",
            f"        const Register lhs = state.r[{dreg}];",
            f"        const Register rhs = state.r[{opcode & 7}];",
            "        native_ops::subf(state, state.r[34], lhs, rhs);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in {"negf_reg", "negf_imm"}:
        dreg = (opcode >> 16) & 7
        setup = [] if name == "negf_reg" else [
            f"        Register immediate_value{{}}; native_ops::short_to_float(immediate_value, 0x{opcode & 0xffff:04x}u);"]
        src = f"state.r[{opcode & 7}]" if name == "negf_reg" else "immediate_value"
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();", *setup,
            f"        const Register source = {src};",
            f"        native_ops::negf(state, state.r[{dreg}], source);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in {"float_reg", "float_imm"}:
        dreg = (opcode >> 16) & 7
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();",
            (f"        state.r[{dreg}].i32[0] = state.r[{opcode & 31}].i32[0];"
             if name == "float_reg" else
             f"        state.r[{dreg}].i32[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(0x{opcode & 0xffff:04x}u)));"),
            f"        native_ops::int_to_float(state, state.r[{dreg}]);",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]
    if name in {"fix_reg", "fix_imm"}:
        dreg = (opcode >> 16) & 31
        if dreg >= 8:
            return None
        source_setup = ([f"        Register source = state.r[{opcode & 7}];"] if name == "fix_reg" else [
            f"        Register source{{}}; native_ops::short_to_float(source, 0x{opcode & 0xffff:04x}u);"])
        return [
            f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
            "        auto& state = cpu.state();", *source_setup,
            "        native_ops::float_to_int(state, source, true);",
            f"        state.r[{dreg}].i32[0] = static_cast<std::uint32_t>(source.mantissa());",
            f"        const bool sequential = cpu.aot_end(0x{pc:06x}u);",
            "        if (state.native_instructions != before) executed = true;",
            "        if (!sequential || state.delayed || (state.r[21].i32[0] & 0x100u)) return executed;",
        ]

    if dreg >= 8:
        return None

    binary = {
        "addi_reg": ("add", False), "addi_imm": ("add", True),
        "subi_reg": ("sub", False), "subi_imm": ("sub", True),
        "and_reg": ("and", False), "and_imm": ("and", True),
        "or_reg": ("or", False), "or_imm": ("or", True),
        "xor_reg": ("xor", False), "xor_imm": ("xor", True),
    }
    lines = [
        f"        if (!cpu.aot_begin(0x{pc:06x}u, 0x{opcode:08x}u)) return executed;",
        "        auto& state = cpu.state();",
    ]

    if name in {"ldi_reg", "ldi_imm"}:
        src = ((f"state.r[{opcode & 31}].i32[0]") if name == "ldi_reg" else
               f"static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(0x{opcode & 0xffff:04x}u)))")
        lines += [
            f"        const std::uint32_t value = {src};",
            f"        state.r[{dreg}].i32[0] = value;",
            "        constexpr std::uint32_t n_flag=0x08u, v_flag=0x02u, z_flag=0x04u, u_flag=0x10u;",
            "        auto& st = state.r[21].i32[0];",
            "        st &= ~(n_flag | v_flag | z_flag | u_flag);",
            "        st |= ((value >> 28) & n_flag) | (value == 0 ? z_flag : 0u);",
        ]
    elif name in binary:
        op, immediate = binary[name]
        src = (f"static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(0x{opcode & 0xffff:04x}u)))"
               if immediate and op in {"add", "sub"} else
               f"static_cast<std::uint32_t>(0x{opcode & 0xffff:04x}u)" if immediate else
               f"state.r[{opcode & 31}].i32[0]")
        lines += [
            f"        const std::uint32_t lhs = state.r[{dreg}].i32[0];",
            f"        const std::uint32_t rhs = {src};",
            f"        const std::uint32_t result = lhs { {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^'}[op] } rhs;",
        ]
        if op in {"add", "sub"}:
            overflow = ("(((lhs ^ rhs) & (lhs ^ result)) >> 30) & 0x02u"
                        if op == "sub" else
                        "(((lhs ^ result) & (rhs ^ result)) >> 30) & 0x02u")
            carry = "(rhs > lhs ? 1u : 0u)" if op == "sub" else "(lhs > result ? 1u : 0u)"
            # OVM only saturates the result; flags continue to reflect the
            # wrapped arithmetic result, matching the source macros.
            if op == "sub":
                sat_overflow = "(((lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) != 0"
            else:
                sat_overflow = "(((lhs ^ result) & (rhs ^ result)) & 0x80000000u) != 0"
            lines += [
                f"        const bool overflow = {sat_overflow};",
                "        std::uint32_t stored = result;",
                f"        if ((state.r[21].i32[0] & 0x80u) && overflow) stored = (lhs & 0x80000000u) ? 0x80000000u : 0x7fffffffu;",
                f"        state.r[{dreg}].i32[0] = stored;",
                "        constexpr std::uint32_t c_flag=0x01u, v_flag=0x02u, z_flag=0x04u, n_flag=0x08u, u_flag=0x10u;",
                "        auto& st = state.r[21].i32[0];",
                "        st &= ~(c_flag | v_flag | z_flag | n_flag | u_flag);",
                f"        st |= {carry};",
                f"        st |= {overflow};",
                "        st |= ((result >> 28) & n_flag) | (result == 0 ? z_flag : 0u);",
            ]
        else:
            lines += [
                f"        state.r[{dreg}].i32[0] = result;",
                "        constexpr std::uint32_t v_flag=0x02u, z_flag=0x04u, n_flag=0x08u, u_flag=0x10u;",
                "        auto& st = state.r[21].i32[0];",
                "        st &= ~(n_flag | v_flag | z_flag | u_flag);",
                "        st |= ((result >> 28) & n_flag) | (result == 0 ? z_flag : 0u);",
            ]
    else:
        return None

    lines += [
        "        const bool sequential = cpu.aot_end(0x%06xu);" % pc,
        "        if (state.native_instructions != before) executed = true;",
        "        if (!sequential) return executed;",
        "        if (state.delayed) return executed;",
        "        if (state.r[21].i32[0] & 0x100u) return executed;",
        "        ",
    ]
    return lines


def branch_targets(pc: int, opcode: int, name: str) -> tuple[set[int], bool]:
    """Return static branch/call destinations and whether fallthrough exists."""
    fallthrough = True
    targets: set[int] = set()
    if name in {"br_imm", "brd_imm"}:
        targets.add(opcode & 0xffffff)
        fallthrough = name == "brd_imm"  # delayed control returns to pipeline code
    elif name in {"call_imm", "callc_imm"}:
        if name == "call_imm":
            targets.add(opcode & 0xffffff)
        else:
            disp = opcode & 0xffff
            if disp & 0x8000: disp -= 0x10000
            targets.add((pc + 1 + disp) & 0xffffff)
    elif name in {"brc_imm", "brcd_imm"}:
        disp = opcode & 0xffff
        if disp & 0x8000: disp -= 0x10000
        targets.add((pc + 1 + disp) & 0xffffff)
    elif name == "rptb_imm":
        targets.add(opcode & 0xffffff)
    return targets, fallthrough


def reachable(words: list[int], handlers: list[str]) -> tuple[set[int], set[int]]:
    start = words[0] & 0xffffff
    todo = deque([start])
    seen: set[int] = set()
    targets: set[int] = {start}
    while todo:
        pc = todo.popleft()
        if pc >= len(words) or pc in seen:
            continue
        opcode = words[pc]
        name = handlers[opcode >> 21]
        if name == "illegal":
            continue
        seen.add(pc)
        branches, fallthrough = branch_targets(pc, opcode, name)
        for target in branches:
            if target < len(words):
                targets.add(target)
                todo.append(target)
        if fallthrough and pc + 1 < len(words):
            todo.append(pc + 1)
        # Unknown/dynamic branches still leave a safe interpreter return edge.
        if is_control(name) and name not in {
            "br_imm", "brd_imm", "brc_imm", "brcd_imm", "call_imm",
            "callc_imm", "rptb_imm"
        } and pc + 1 < len(words):
            todo.append(pc + 1)
    return seen, targets


def emit_aot(rom: bytes, ops_source: Path, output_path: Path,
             max_words: int = 0x20000, max_block: int = 32,
             overlay_words: tuple[tuple[int, int], ...] = ()) -> tuple[int, int, int, int]:
    if len(rom) % 4:
        raise ValueError("C31 ROM image must contain complete little-endian 32-bit words")
    raw_words = list(struct.unpack(f"<{len(rom) // 4}I", rom))
    words = raw_words[:max_words]
    handlers = decode_handlers(ops_source)
    # The reset copy maps the entire first 0x20000-word window as live RAM.
    # C31 uses computed branches, so static reachability is not authoritative.
    # Every candidate is guarded by exact live PC/opcode and executes the same
    # handler as the interpreter, making dead-data candidates inert.
    candidates = {pc: opcode for pc, opcode in enumerate(words)
                  if is_native_safe(handlers[opcode >> 21])}
    for pc, opcode in overlay_words:
        if not 0 <= pc < (1 << 24) or not 0 <= opcode <= 0xffffffff:
            raise ValueError(f"invalid overlay word PC={pc:#x} opcode={opcode:#x}")
        if is_native_safe(handlers[(opcode & 0xffffffff) >> 21]):
            candidates[pc] = opcode & 0xffffffff
        else:
            candidates.pop(pc, None)
    page_words = 0x1000
    direct_count = sum(emit_direct_instruction(pc, opcode, decode_ti_direct(opcode)) is not None
                       for pc, opcode in candidates.items())
    # Keep CMake's generated source list stable even for a short reset image.
    # The default C31 image window is 0x20000 words, i.e. 32 pages.
    page_count = max(32, (len(words) + page_words - 1) // page_words)
    shard_candidates: list[dict[int, int]] = [
        {pc: op for pc, op in candidates.items()
         if page * page_words <= pc < min((page + 1) * page_words, len(words))}
        for page in range(page_count)
    ]
    shard_candidates.append({pc: op for pc, op in candidates.items() if pc >= len(words)})

    def make_blocks(shard: dict[int, int]) -> list[list[tuple[int, int, str]]]:
        blocks: list[list[tuple[int, int, str]]] = []
        pcs = sorted(shard)
        index = 0
        while index < len(pcs):
            block: list[tuple[int, int, str]] = []
            previous = pcs[index] - 1
            while index < len(pcs) and len(block) < max_block:
                pc = pcs[index]
                if block and pc != previous + 1:
                    break
                opcode = shard[pc]
                name = handlers[opcode >> 21]
                block.append((pc, opcode, name))
                index += 1
                previous = pc
                if is_control(name):
                    break
            blocks.append(block)
        return blocks

    all_blocks = 0
    output_path.parent.mkdir(parents=True, exist_ok=True)
    shard_names = [output_path.stem + f"_shard_{i:02d}.cpp"
                   for i in range(len(shard_candidates) - 1)]
    extra_name = output_path.stem + "_extra.cpp"

    for shard_index, shard in enumerate(shard_candidates):
        blocks = make_blocks(shard)
        all_blocks += len(blocks)
        is_extra = shard_index == page_count
        source_name = extra_name if is_extra else shard_names[shard_index]
        lines = [
            "// Generated by tools/tms320c31/recomp.py; do not edit.",
            '#include "core/cpus/tms320c31/tms320c31.hpp"',
            '#include "core/cpus/tms320c31/native_ops.hpp"',
            "#include <algorithm>", "#include <array>", "#include <iterator>",
            "namespace multiarcaderecomp::tms320c31 {",
        ]
        for block_index, block in enumerate(blocks):
            lines += [f"static bool block_{block_index}(Interpreter& cpu, std::uint32_t offset) {{",
                      "    bool executed = false;", "    switch (offset) {"]
            for offset, _ in enumerate(block):
                lines.append(f"    case {offset}: goto instruction_{offset};")
            lines += ["    default: return false;", "    }"]
            for offset, (pc, opcode, name) in enumerate(block):
                lines += [
                    f"instruction_{offset}:", "    {",
                    "        const auto before = cpu.state().native_instructions;",
                ]
                direct = emit_direct_instruction(pc, opcode, decode_ti_direct(opcode))
                if direct is not None:
                    lines.extend(direct)
                else:
                    lines += [
                        f"        const bool sequential = cpu.aot_execute_{name}(0x{pc:06x}u, 0x{opcode:08x}u);",
                        "        if (cpu.state().native_instructions != before) executed = true;",
                        "        if (!sequential) return executed;",
                        "        if (cpu.state().delayed) return executed;",
                        "        if (cpu.state().r[21].i32[0] & 0x100u) return executed;",
                    ]
                lines.append("    }")
            lines += ["    return executed;", "}"]

        lines.append("struct Entry { std::uint32_t pc; AotBlock block; std::uint32_t offset; };")
        if is_extra:
            lines.append(f"static const std::array<Entry, {len(shard)}> entries{{{{")
            for block_index, block in enumerate(blocks):
                for offset, (pc, _, _) in enumerate(block):
                    lines.append(f"    {{0x{pc:06x}u, &block_{block_index}, {offset}u}},")
            lines += ["}};", "AotEntry lookup_aot_extra(std::uint32_t pc) noexcept {",
                      "    const auto it = std::lower_bound(entries.begin(), entries.end(), pc,",
                      "        [](const Entry& entry, std::uint32_t value) { return entry.pc < value; });",
                      "    return it != entries.end() && it->pc == pc ? AotEntry{it->block, it->offset} : AotEntry{};",
                      "}"]
        else:
            page_base = shard_index * page_words
            page_length = max(0, min(page_words, len(words) - page_base))
            lines.append(f"static const std::array<AotEntry, {page_length}> dense_lookup = [] {{")
            lines.append(f"    std::array<AotEntry, {page_length}> result{{}};")
            for block_index, block in enumerate(blocks):
                for offset, (pc, _, _) in enumerate(block):
                    lines.append(f"    result[{pc - page_base}] = {{&block_{block_index}, {offset}u}};")
            lines += ["    return result;", "}();",
                      f"AotEntry lookup_aot_shard_{shard_index:02d}(std::uint32_t pc) noexcept {{",
                      f"    if (pc < {page_base}u || pc >= {page_base + page_length}u) return {{}};",
                      f"    return dense_lookup[pc - {page_base}u];", "}"]
        lines += ["} // namespace multiarcaderecomp::tms320c31", ""]
        (output_path.parent / source_name).write_text("\n".join(lines), encoding="utf-8")

    dispatcher = [
        "// Generated by tools/tms320c31/recomp.py; do not edit.",
        '#include "core/cpus/tms320c31/tms320c31.hpp"',
        "namespace multiarcaderecomp::tms320c31 {",
    ]
    dispatcher.extend(f"AotEntry lookup_aot_shard_{i:02d}(std::uint32_t) noexcept;"
                      for i in range(page_count))
    dispatcher.append("AotEntry lookup_aot_extra(std::uint32_t) noexcept;")
    dispatcher.append("AotEntry lookup_aot(std::uint32_t pc) noexcept {")
    dispatcher.append("    switch (pc >> 12) {")
    dispatcher.extend(f"    case {i}: return lookup_aot_shard_{i:02d}(pc);"
                      for i in range(page_count))
    dispatcher += ["    default: return lookup_aot_extra(pc);", "    }", "}",
                   "} // namespace multiarcaderecomp::tms320c31", ""]
    output_path.write_text("\n".join(dispatcher), encoding="utf-8")
    return len(words), len(candidates), all_blocks, direct_count


def read_overlay(path: Path | None) -> tuple[tuple[int, int], ...]:
    if path is None:
        return ()
    words = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 2:
            raise ValueError(f"{path}:{line_number}: expected PC OPCODE")
        words.append((int(fields[0], 0), int(fields[1], 0)))
    return tuple(words)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="V-Unit main.bin reset/low-RAM image")
    parser.add_argument("output", type=Path, help="generated C++ source")
    parser.add_argument("--ops-source", type=Path,
                        default=Path("core/cpus/tms320c31/mame_source/320c3x_ops.ipp"))
    parser.add_argument("--max-words", type=lambda s: int(s, 0), default=0x20000)
    parser.add_argument("--max-block", type=int, default=32)
    parser.add_argument("--overlay", type=Path,
                        help="optional live RAM opcode samples, one 'PC OPCODE' pair per line")
    args = parser.parse_args()
    try:
        overlay = read_overlay(args.overlay)
        reached, candidates, blocks, direct = emit_aot(
            args.rom.read_bytes(), args.ops_source, args.output, args.max_words,
            args.max_block, overlay)
    except (OSError, ValueError, IndexError, struct.error) as exc:
        parser.error(str(exc))
    print(f"TMS320C31 AOT: image-words={reached} guarded-candidates={candidates} "
          f"direct-translated={direct} blocks={blocks}; fallback=interpreter")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
