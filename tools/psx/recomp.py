#!/usr/bin/env python3
"""Static AOT translator for little-endian PlayStation R3000A program images.

The emitted module uses the PSX-specific ABI in core/cpus/psx/psx_aot_abi.h.
Instructions outside the implemented integer subset remain at their original
PC and are delegated to the R3000A interpreter at runtime.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


FUNCTIONS_PER_SOURCE = 512
MAX_BLOCK_INSTRUCTIONS = 16


def s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def branch_target(pc: int, word: int) -> int:
    return (pc + 4 + (s16(word & 0xffff) << 2)) & 0xffffffff


def flow(pc: int, word: int):
    """Return (kind, target, has_fallthrough) for supported static flow."""
    op = word >> 26
    rt, fn = (word >> 16) & 31, word & 63
    if op == 0 and fn in (8, 9):
        return ("jalr" if fn == 9 else "jr", None, fn == 9)
    if op in (2, 3):
        target = ((pc + 4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
        return ("jal" if op == 3 else "j", target, op == 3)
    if op in (4, 5) or (op in (6, 7) and rt == 0):
        return ("branch", branch_target(pc, word), True)
    if op == 1 and rt in (0, 1, 16, 17):
        return ("branch", branch_target(pc, word), True)
    return (None, None, True)


def is_native_instruction(word: int) -> bool:
    """Whether emit_instruction produces native semantics for this opcode."""
    op = word >> 26
    rt, fn = (word >> 16) & 31, word & 63
    if op == 0:
        return fn in {
            0x00, 0x02, 0x03, 0x04, 0x06, 0x07,
            0x08, 0x09, 0x10, 0x11, 0x12, 0x13,
            0x18, 0x19, 0x1a, 0x1b, 0x20, 0x21, 0x22, 0x23,
            0x24, 0x25, 0x26, 0x27, 0x2a, 0x2b,
        }
    if op in (2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15):
        return True
    if op in (6, 7):
        return rt == 0
    if op == 1:
        return rt in (0, 1, 16, 17)
    if op in (32, 33, 35, 36, 37, 40, 41, 43):
        return True
    return False


def is_memory_store(word: int) -> bool:
    """Stores terminate blocks so self-modifying code is revalidated on entry."""
    return (word >> 26) in (40, 41, 43)


def is_memory_access(word: int) -> bool:
    """Memory callbacks may move device deadlines during a generated block."""
    return (word >> 26) in (32, 33, 35, 36, 37, 40, 41, 43)


class Program:
    def __init__(self, image: bytes, base: int):
        if len(image) % 4:
            raise ValueError("program image size must be divisible by four")
        self.base = base & 0xffffffff
        self.words = struct.unpack("<%dI" % (len(image) // 4), image)
        self.code: dict[int, int] = {}
        self.entries: set[int] = set()

    def word(self, pc: int):
        offset = (pc - self.base) & 0xffffffff
        if pc & 3 or offset >= len(self.words) * 4:
            return None
        return self.words[offset // 4]

    def discover(self, seeds: set[int]):
        pending = list(seeds)
        while pending:
            pc = pending.pop()
            while (word := self.word(pc)) is not None and pc not in self.code:
                self.entries.add(pc)
                self.code[pc] = word
                kind, target, fallthrough = flow(pc, word)
                if kind is None:
                    pc = (pc + 4) & 0xffffffff
                    continue

                # The delay slot is always retained as its own AOT entry.
                slot = (pc + 4) & 0xffffffff
                slot_word = self.word(slot)
                if slot_word is not None:
                    self.code.setdefault(slot, slot_word)
                    self.entries.add(slot)
                if target is not None and self.word(target) is not None:
                    self.entries.add(target)
                    pending.append(target)
                after_slot = (pc + 8) & 0xffffffff
                if fallthrough and self.word(after_slot) is not None:
                    self.entries.add(after_slot)
                    pending.append(after_slot)
                break


def build_aot_blocks(program: Program) -> list[list[tuple[int, int]]]:
    """Group contiguous native instructions without crossing control-flow edges."""
    code = program.code
    pcs = sorted(code)
    code_pcs = set(pcs)
    leaders = {pc for pc in pcs if pc - 4 not in code_pcs}
    for pc in pcs:
        word = code[pc]
        if not is_native_instruction(word):
            leaders.add(pc)
        if pc - 4 in code and not is_native_instruction(code[pc - 4]):
            leaders.add(pc)
        if is_memory_store(word) and pc + 4 in code_pcs:
            leaders.add(pc + 4)
        kind, target, fallthrough = flow(pc, word)
        if kind is None:
            continue
        for candidate in (pc + 4, pc + 8, target):
            if candidate is not None and candidate in code_pcs:
                leaders.add(candidate)

    blocks: list[list[tuple[int, int]]] = []
    covered: set[int] = set()
    for start in pcs:
        if start in covered or start not in leaders:
            continue
        block: list[tuple[int, int]] = []
        pc = start
        while pc in code and is_native_instruction(code[pc]):
            block.append((pc, code[pc]))
            covered.add(pc)
            kind, _, _ = flow(pc, code[pc])
            next_pc = pc + 4
            if kind is not None:
                # Keep a supported branch and its supported delay slot in one
                # AOT call. The slot still has its own entry_offset, so a
                # scheduler boundary between the two resumes at the right PC.
                if (len(block) < MAX_BLOCK_INSTRUCTIONS and
                        next_pc in code and is_native_instruction(code[next_pc])):
                    block.append((next_pc, code[next_pc]))
                    covered.add(next_pc)
                break
            if is_memory_store(code[pc]):
                break
            if len(block) >= MAX_BLOCK_INSTRUCTIONS:
                if kind is None and next_pc in code_pcs:
                    leaders.add(next_pc)
                break
            if next_pc not in code_pcs or next_pc in leaders:
                break
            pc = next_pc
        if not block:
            block = [(start, code[start])]
            covered.add(start)
        blocks.append(block)

    # Any profiled PC not reached from a discovered leader is retained as a
    # one-instruction block; an indirect jump may target it at runtime.
    for pc in pcs:
        if pc not in covered:
            blocks.append([(pc, code[pc])])
            covered.add(pc)
    blocks.sort(key=lambda block: block[0][0])
    return blocks


def reg(n: int) -> str:
    return f"s->r[{n}]"


def emit_instruction(pc: int, word: int) -> str:
    """Emit one native R3000A instruction or an interpreter handoff."""
    op = word >> 26
    rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    shamt, fn = (word >> 6) & 31, word & 63
    imm = word & 0xffff
    simm = s16(imm)
    head = f"    const uint32_t pc = 0x{pc:08x}u;\n"

    def write_gpr(destination: int, expression: str) -> str:
        if destination == 0:
            return f"    psx_aot_advance(s);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n    return PSX_AOT_DISPATCH;\n"
        return (f"    const uint32_t value = (uint32_t)({expression});\n"
                f"    psx_aot_advance(s);\n    {reg(destination)} = value;\n"
                f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                "    return PSX_AOT_DISPATCH;\n")

    def write_checked_gpr(destination: int) -> str:
        assign = f"    {reg(destination)} = value;\n" if destination else ""
        return (f"    const uint32_t value = (uint32_t)(int32_t)wide;\n"
                f"    psx_aot_advance(s);\n{assign}"
                f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                "    return PSX_AOT_DISPATCH;\n")

    if op in (2, 3):
        target = ((pc + 4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
        link = "    s->r[31] = pc + 8u;\n" if op == 3 else ""
        return (head + f"    psx_aot_advance(s);\n{link}"
                f"    psx_aot_schedule_branch(s, 0x{target:08x}u, true);\n"
                f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                "    return PSX_AOT_DISPATCH;\n")

    if op == 0:
        if fn == 0x00: expr = f"{reg(rt)} << {shamt}"
        elif fn == 0x02: expr = f"{reg(rt)} >> {shamt}"
        elif fn == 0x03: expr = f"(int32_t){reg(rt)} >> {shamt}"
        elif fn == 0x04: expr = f"{reg(rt)} << ({reg(rs)} & 31u)"
        elif fn == 0x06: expr = f"{reg(rt)} >> ({reg(rs)} & 31u)"
        elif fn == 0x07: expr = f"(int32_t){reg(rt)} >> ({reg(rs)} & 31u)"
        elif fn == 0x10:
            assign = f"    {reg(rd)} = value;\n" if rd else ""
            return (head + "    const uint32_t value = psx_aot_muldiv_result(s, true);\n"
                    f"    psx_aot_advance(s);\n{assign}"
                    f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                    "    return PSX_AOT_DISPATCH;\n")
        elif fn == 0x11:
            return (head + f"    psx_aot_write_hilo(s, true, {reg(rs)});\n"
                    f"    psx_aot_advance(s);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                    "    return PSX_AOT_DISPATCH;\n")
        elif fn == 0x12:
            assign = f"    {reg(rd)} = value;\n" if rd else ""
            return (head + "    const uint32_t value = psx_aot_muldiv_result(s, false);\n"
                    f"    psx_aot_advance(s);\n{assign}"
                    f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                    "    return PSX_AOT_DISPATCH;\n")
        elif fn == 0x13:
            return (head + f"    psx_aot_write_hilo(s, false, {reg(rs)});\n"
                    f"    psx_aot_advance(s);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                    "    return PSX_AOT_DISPATCH;\n")
        elif fn in (0x18, 0x19, 0x1a, 0x1b):
            operation = {
                0x18: "PSX_MULDIV_SIGNED",
                0x19: "PSX_MULDIV_UNSIGNED",
                0x1a: "PSX_DIV_SIGNED",
                0x1b: "PSX_DIV_UNSIGNED",
            }[fn]
            return (head + f"    psx_aot_muldiv_start(s, {operation}, {reg(rs)}, {reg(rt)});\n"
                    f"    psx_aot_advance(s);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                    "    return PSX_AOT_DISPATCH;\n")
        elif fn == 0x20:
            return (head + f"    const int64_t wide = (int64_t)(int32_t){reg(rs)} + (int64_t)(int32_t){reg(rt)};\n"
                    f"    if (wide < INT32_MIN || wide > INT32_MAX) return psx_aot_raise_exception(s, 12u, pc, 0x{word:08x}u);\n"
                    + write_checked_gpr(rd))
        elif fn == 0x21: expr = f"{reg(rs)} + {reg(rt)}"
        elif fn == 0x22:
            return (head + f"    const int64_t wide = (int64_t)(int32_t){reg(rs)} - (int64_t)(int32_t){reg(rt)};\n"
                    f"    if (wide < INT32_MIN || wide > INT32_MAX) return psx_aot_raise_exception(s, 12u, pc, 0x{word:08x}u);\n"
                    + write_checked_gpr(rd))
        elif fn == 0x23: expr = f"{reg(rs)} - {reg(rt)}"
        elif fn == 0x24: expr = f"{reg(rs)} & {reg(rt)}"
        elif fn == 0x25: expr = f"{reg(rs)} | {reg(rt)}"
        elif fn == 0x26: expr = f"{reg(rs)} ^ {reg(rt)}"
        elif fn == 0x27: expr = f"~({reg(rs)} | {reg(rt)})"
        elif fn == 0x2a: expr = f"((int32_t){reg(rs)} < (int32_t){reg(rt)})"
        elif fn == 0x2b: expr = f"({reg(rs)} < {reg(rt)})"
        elif fn == 0x08:
            return head + f"    const uint32_t target = {reg(rs)};\n    psx_aot_advance(s);\n    psx_aot_schedule_branch(s, target, true);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n    return PSX_AOT_DISPATCH;\n"
        elif fn == 0x09:
            link = f"    {reg(rd)} = pc + 8u;\n" if rd else ""
            return (head + f"    const uint32_t target = {reg(rs)};\n    psx_aot_advance(s);\n"
                    + link + f"    psx_aot_schedule_branch(s, target, true);\n"
                    f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n    return PSX_AOT_DISPATCH;\n")
        else:
            return head + f"    return psx_aot_interpret_one(s, bus, 0x{word:08x}u, pc);\n"
        return head + write_gpr(rd, expr)

    if op == 8:
        return (head + f"    const int64_t wide = (int64_t)(int32_t){reg(rs)} + (int64_t){simm};\n"
                f"    if (wide < INT32_MIN || wide > INT32_MAX) return psx_aot_raise_exception(s, 12u, pc, 0x{word:08x}u);\n"
                + write_checked_gpr(rt))
    if op == 9: return head + write_gpr(rt, f"{reg(rs)} + (uint32_t){simm}")
    if op == 10: return head + write_gpr(rt, f"((int32_t){reg(rs)} < {simm})")
    if op == 11: return head + write_gpr(rt, f"({reg(rs)} < (uint32_t){simm})")
    if op == 12: return head + write_gpr(rt, f"{reg(rs)} & 0x{imm:04x}u")
    if op == 13: return head + write_gpr(rt, f"{reg(rs)} | 0x{imm:04x}u")
    if op == 14: return head + write_gpr(rt, f"{reg(rs)} ^ 0x{imm:04x}u")
    if op == 15: return head + write_gpr(rt, f"0x{imm:04x}u << 16")

    branch = None
    if op == 4: branch = f"({reg(rs)} == {reg(rt)})"
    elif op == 5: branch = f"({reg(rs)} != {reg(rt)})"
    elif op == 6 and rt == 0: branch = f"((int32_t){reg(rs)} <= 0)"
    elif op == 7 and rt == 0: branch = f"((int32_t){reg(rs)} > 0)"
    elif op == 1 and rt in (0, 1, 16, 17):
        branch = f"((int32_t){reg(rs)} {'< 0' if rt in (0, 16) else '>= 0'})"
    if branch is not None:
        link = rt in (16, 17) and op == 1
        link_code = f"    if ({branch}) {reg(31)} = pc + 8u;\n" if link else ""
        return (head + f"    const bool take = {branch};\n    psx_aot_advance(s);\n"
                f"    psx_aot_schedule_branch(s, 0x{branch_target(pc, word):08x}u, take);\n"
                + link_code + f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n"
                "    return PSX_AOT_DISPATCH;\n")

    loads = {32: (1, "int8_t"), 33: (2, "int16_t"), 35: (4, "uint32_t"),
             36: (1, "uint8_t"), 37: (2, "uint16_t")}
    if op in loads:
        size, cast = loads[op]
        return (head + f"    uint32_t value = 0;\n"
                f"    if (!psx_aot_memory_read(s, bus, {reg(rs)} + (uint32_t){simm}, {size}u, pc, 0x{word:08x}u, &value)) return PSX_AOT_EXCEPTION;\n"
                f"    psx_aot_schedule_load(s, {rt}u, (uint32_t)({cast})value);\n"
                f"    psx_aot_retire(s, 0x{word:08x}u, pc);\n    return PSX_AOT_DISPATCH;\n")
    stores = {40: 1, 41: 2, 43: 4}
    if op in stores:
        return (head + f"    if (!psx_aot_memory_write(s, bus, {reg(rs)} + (uint32_t){simm}, {stores[op]}u, {reg(rt)}, pc, 0x{word:08x}u)) return PSX_AOT_EXCEPTION;\n"
                f"    psx_aot_advance(s);\n    psx_aot_retire(s, 0x{word:08x}u, pc);\n    return PSX_AOT_DISPATCH;\n")
    return head + f"    return psx_aot_interpret_one(s, bus, 0x{word:08x}u, pc);\n"


def emit_block(block: list[tuple[int, int]]) -> str:
    """Emit one bounded straight-line function, preserving per-op boundaries."""
    lines: list[str] = []
    chain_block = len(block) > 1 and not any(is_memory_access(word)
                                              for _, word in block)
    lines.append("    if (entry_offset == 0) goto instruction_0;\n")
    if chain_block:
        lines.append("    switch (entry_offset) {\n")
        for index in range(1, len(block)):
            lines.append(f"    case {index}: goto instruction_{index};\n")
        lines.append("    default: return PSX_AOT_INTERPRETER;\n    }\n")
    else:
        lines.append("    return PSX_AOT_INTERPRETER;\n")
    for index, (pc, word) in enumerate(block):
        lines.append(f"instruction_{index}:\n    {{\n")
        if index:
            lines.append(f"    if (entry_offset < {index} && s->cycles >= cycle_limit) return PSX_AOT_DISPATCH;\n")
            lines.append(
                f"    const bool event_due_{index} = entry_offset < {index} && s->cycles >= s->next_event_cycle;\n"
                f"    if (event_due_{index} && bus && bus->check_interrupts)\n"
                f"        bus->check_interrupts(bus->opaque, s, 0x{pc:08x}u);\n"
                f"    if (entry_offset < {index} && (s->cp0[12] & 1u) && (s->cp0[12] & s->cp0[13] & 0x0000ff00u))\n"
                f"        return psx_aot_raise_exception(s, 0u, 0x{pc:08x}u, 0);\n"
                f"    if (event_due_{index}) {{\n"
                "        uint32_t actual_opcode = 0;\n"
                f"        const uint32_t physical_pc = 0x{pc:08x}u & 0x1fffffffu;\n"
                "        const size_t ram_size = bus ? bus->direct_ram_size : 0;\n"
                "        const size_t ram_offset = bus && bus->direct_ram_mask != SIZE_MAX\n"
                "            ? (physical_pc & bus->direct_ram_mask) : SIZE_MAX;\n"
                "        const bool direct_ram_fetch = bus && bus->direct_ram &&\n"
                "            bus->direct_ram_visible_bytes &&\n"
                "            physical_pc < *bus->direct_ram_visible_bytes && ram_size &&\n"
                "            ram_offset != SIZE_MAX && ram_offset + 4 <= ram_size;\n"
                "        bool source_fetched = false;\n"
                "        if (direct_ram_fetch) {\n"
                "            const uint8_t *bytes = bus->direct_ram + ram_offset;\n"
                "            actual_opcode = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |\n"
                "                ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);\n"
                "            source_fetched = true;\n"
                "        } else if (bus && bus->read) {\n"
                f"            source_fetched = bus->read(bus->opaque, 0x{pc:08x}u, 4, s->pc, &actual_opcode);\n"
                "        }\n"
                f"        if (!source_fetched || actual_opcode != 0x{word:08x}u)\n"
                "            return PSX_AOT_INTERPRETER;\n"
                "    }\n")
        emitted = emit_instruction(pc, word)
        if index + 1 < len(block):
            terminal = "    return PSX_AOT_DISPATCH;\n"
            if not emitted.endswith(terminal):
                raise ValueError(f"cannot chain non-native instruction at {pc:#x}")
            emitted = emitted[:-len(terminal)]
        lines.append(emitted)
        lines.append("    }\n")
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path, help="flat little-endian R3000A image")
    parser.add_argument("--base", required=True, type=lambda x: int(x, 0), help="image virtual load address")
    parser.add_argument("--entry", action="append", required=True, type=lambda x: int(x, 0), help="entry PC; repeat for vectors/known roots")
    parser.add_argument("--entries-file", type=Path, help="additional entry PCs, one hexadecimal value per line")
    parser.add_argument("--entries-only", action="store_true",
                        help="translate only supplied/profiled PCs and form bounded blocks")
    parser.add_argument("--out", required=True, type=Path, help="generated C++ module path")
    args = parser.parse_args()
    try:
        image = args.program.read_bytes()
        program = Program(image, args.base)
        if not image:
            raise ValueError("program image is empty")
        entries = list(args.entry)
        if args.entries_file:
            entries.extend(int(line.split("#", 1)[0].strip(), 0)
                           for line in args.entries_file.read_text().splitlines()
                           if line.split("#", 1)[0].strip())
        if not any(program.word(entry) is not None for entry in entries):
            raise ValueError("none of the entry PCs lies inside the program image")
        if args.entries_only:
            for pc in set(entries):
                word = program.word(pc)
                if word is not None:
                    program.entries.add(pc)
                    program.code[pc] = word
        else:
            program.discover(set(entries))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    blocks = build_aot_blocks(program)
    block_entries = [
        (pc, word, block[0][0], offset)
        for block in blocks
        for offset, (pc, word) in enumerate(block)
    ]
    for old_shard in args.out.parent.glob(f"{args.out.stem}_*.cpp"):
        old_shard.unlink()
    for shard_index, start in enumerate(range(0, len(blocks), FUNCTIONS_PER_SOURCE)):
        shard = args.out.with_name(f"{args.out.stem}_{shard_index:04d}.cpp")
        shard_lines = ["// Generated by tools/psx/recomp.py. Do not edit.\n",
                       '#include "cpus/psx/psx_aot_abi.h"\n',
                       "// Inline the per-op pipeline helpers; generated modules do not use LTO.\n",
                       "#define psx_aot_advance psx_aot_advance_inline\n",
                       "#define psx_aot_retire psx_aot_retire_inline\n\n"]
        for block in blocks[start:start + FUNCTIONS_PER_SOURCE]:
            pc = block[0][0]
            shard_lines.append(f"PsxAotExit psx_block_{pc:08x}(PsxCpuState *s, const PsxBus *bus, uint64_t cycle_limit, uint8_t entry_offset)\n{{\n")
            shard_lines.append(emit_block(block))
            shard_lines.append("}\n\n")
        shard.write_text("".join(shard_lines))

    lines = ["// Generated by tools/psx/recomp.py. Do not edit.\n",
             '#include "cpus/psx/psx_aot_abi.h"\n\n']
    for block in blocks:
        pc = block[0][0]
        lines.append(f"PsxAotExit psx_block_{pc:08x}(PsxCpuState *s, const PsxBus *bus, uint64_t cycle_limit, uint8_t entry_offset);\n")
    lines.append("\n")
    lines.append("const PsxAotEntry psx_aot_entries[] = {\n")
    for pc, opcode, block_pc, offset in block_entries:
        native = 1 if is_native_instruction(opcode) else 0
        lines.append(f"    {{0x{pc:08x}u, psx_block_{block_pc:08x}, 0x{opcode:08x}u, 1, {native}, {offset}}},\n")
    lines.append("};\nconst size_t psx_aot_entry_count = sizeof(psx_aot_entries) / sizeof(psx_aot_entries[0]);\n\n")
    lines.append("const PsxAotSourceWord psx_aot_source_words[] = {\n")
    for pc in sorted(program.code):
        lines.append(f"    {{0x{pc:08x}u, 0x{program.code[pc]:08x}u}},\n")
    lines.append("};\nconst size_t psx_aot_source_word_count = sizeof(psx_aot_source_words) / sizeof(psx_aot_source_words[0]);\n")
    args.out.write_text("".join(lines))


if __name__ == "__main__":
    main()
