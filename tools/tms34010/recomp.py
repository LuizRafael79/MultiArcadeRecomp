#!/usr/bin/env python3
"""Prototype static TMS34010 block translator.

Input is an assembled byte-addressed maincpu region. Instruction and PC
addresses used internally are TMS34010 bit addresses. Unsupported instructions
end native translation at their own PC so a future interpreter can take over
without the AOT translator guessing the instruction length.
"""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass, field
from pathlib import Path
import sys


MASK32 = 0xFFFFFFFF

# Opcode slots from MAME's 34010tbl.hxx (opcode >> 4; low nibble is a
# register operand). Separate these holes before pattern decoding so reserved
# and illegal encodings cannot alias a nearby valid instruction.
ILLOP_RANGES = (
    (0x020, 0x02F), (0x040, 0x04F), (0x080, 0x08F), (0x0A0, 0x0AF),
    (0x0C0, 0x0CF), (0x0E0, 0x0EF), (0x340, 0x37F), (0x700, 0x7FF),
    (0x9E0, 0x9FF), (0xBE0, 0xBFF), (0xD80, 0xDEF), (0xFE0, 0xFFF),
)
UNIMPL_RANGES = (
    (0x000, 0x001), (0x004, 0x00F), (0x011, 0x011), (0x01D, 0x01D),
    (0x01F, 0x01F), (0x031, 0x031), (0x033, 0x033), (0x035, 0x035),
    (0x037, 0x037), (0x05D, 0x05D), (0x060, 0x06F), (0x07D, 0x07D),
    (0x095, 0x095), (0x0D2, 0x0D2), (0x0D4, 0x0D4), (0x0D7, 0x0D7),
    (0x0DF, 0x0DF), (0x0F1, 0x0F1), (0x0F3, 0x0F3), (0x0F5, 0x0F5),
    (0x0F7, 0x0F7), (0x0F9, 0x0F9), (0x0FB, 0x0FB), (0x0FD, 0x0FD),
    (0x0FF, 0x0FF), (0xD42, 0xD4F), (0xD52, 0xD5F), (0xD62, 0xD6F),
    (0xD72, 0xD7F), (0xDF0, 0xDF0), (0xDF2, 0xDF8), (0xDFA, 0xDFF),
    (0xEA0, 0xEBF),
)


def opcode_slot(index: int) -> str | None:
    if any(first <= index <= last for first, last in ILLOP_RANGES):
        return "illop"
    if any(first <= index <= last for first, last in UNIMPL_RANGES):
        return "unimpl"
    return None


@dataclass(frozen=True)
class Instruction:
    pc: int
    next_pc: int
    opcode: int
    name: str
    bank: str = "A"
    src: int = 0
    dst: int = 0
    immediate: int = 0
    condition: int = 0
    target: int = 0
    fallthrough: int = 0
    branch_cycles_taken: int = 0
    branch_cycles_not_taken: int = 0
    field: int = 0


@dataclass
class BlockInfo:
    pc: int
    instructions: list[Instruction]
    unsupported_opcode: int | None = None
    dynamic_exits: list[tuple[int, str]] = field(default_factory=list)


class Rom:
    def __init__(self, data: bytes, base_byte_address: int, map_size: int):
        if not data:
            raise ValueError("empty ROM image")
        if map_size <= 0:
            raise ValueError("ROM map size must be positive")
        self.data = data
        self.base = base_byte_address & MASK32
        self.map_size = map_size

    def index_for_bit_pc(self, pc: int) -> int:
        byte_address = (pc & MASK32) >> 3
        offset = (byte_address - self.base) & MASK32
        if offset >= self.map_size:
            raise ValueError(f"PC {pc & MASK32:#010x} is outside the mapped ROM window")
        return offset % len(self.data)

    def word(self, bit_pc: int) -> int:
        i = self.index_for_bit_pc(bit_pc)
        j = (i + 1) % len(self.data)
        return self.data[i] | (self.data[j] << 8)

    def long(self, bit_pc: int) -> int:
        lo = self.word(bit_pc)
        hi = self.word((bit_pc + 16) & MASK32)
        return lo | (hi << 16)


def decode(rom: Rom, pc: int) -> Instruction | None:
    """Decode the initial TMS34010 AOT subset; None means interpreter needed."""
    pc &= MASK32
    op = rom.word(pc)
    end = (pc + 16) & MASK32
    slot = opcode_slot(op >> 4)
    if slot == "illop":
        return Instruction(pc, end, op, "illop")
    if slot == "unimpl":
        return Instruction(pc, end, op, "reserved")
    if (op & 0xfff0) == 0x0fa0:
        return Instruction(pc, end, op, "pixblt_b_xy")
    bank = "B" if op & 0x10 else "A"
    src = (op >> 5) & 0x0F
    dst = op & 0x0F
    top = op & 0xFE00
    sub = op & 0x01E0

    # NOP: the otherwise-unused register bits do not affect the operation.
    if top == 0x0200 and sub == 0x0100:
        return Instruction(pc, end, op, "nop")
    if top == 0x0200 and sub == 0x0160:
        return Instruction(pc, end, op, "dint")
    if top == 0x0200 and sub == 0x0120:
        return Instruction(pc, end, op, "clrc")
    if top == 0x0000 and sub == 0x0140:
        return Instruction(pc, end, op, "getpc", bank, dst=dst)
    if top == 0x0000 and sub == 0x0180:
        return Instruction(pc, end, op, "getst", bank, dst=dst)
    if top == 0x0000 and sub == 0x0160:
        return Instruction(pc, end, op, "jump", bank, dst=dst)
    if top == 0x0000 and sub == 0x0120:
        return Instruction(pc, end, op, "exgpc", bank, dst=dst)
    # MAME's opcode table maps these slots to unimpl(), which consumes one
    # cycle without trapping. REV-A/REV-B are adjacent slots and both write 8.
    if top == 0x0000 and sub in (0x0000, 0x00A0, 0x00C0, 0x00D0, 0x00F0):
        return Instruction(pc, end, op, "reserved")
    if top == 0x0000 and sub == 0x0020:
        return Instruction(pc, end, op, "rev", bank, dst=dst)
    if top == 0x0000 and sub == 0x0040:
        return Instruction(pc, end, op, "reserved")
    if top == 0x0000 and sub == 0x0080:
        return Instruction(pc, end, op, "reserved")
    if top == 0x0000 and sub == 0x0100:
        return Instruction(pc, end, op, "emu")
    if top == 0x0000 and sub == 0x01A0:
        return Instruction(pc, end, op, "putst", bank, dst=dst)
    if top == 0x0000 and sub == 0x01C0:
        return Instruction(pc, end, op, "popst")
    if top == 0x0000 and sub == 0x01E0:
        return Instruction(pc, end, op, "pushst")
    if top == 0xF800:
        return Instruction(pc, end, op, "pixt_ri", bank, src=src, dst=dst)
    if top == 0xFA00:
        return Instruction(pc, end, op, "pixt_ir", bank, src=src, dst=dst)
    if top == 0xFC00:
        return Instruction(pc, end, op, "pixt_ii", bank, src=src, dst=dst)
    if top == 0x0200 and sub == 0x01E0:
        return Instruction(pc, end, op, "not", bank, dst=dst)
    if top == 0x0200 and sub == 0x01A0:
        return Instruction(pc, end, op, "neg", bank, dst=dst)
    if top == 0x0200 and sub == 0x0180:
        return Instruction(pc, end, op, "abs", bank, dst=dst)
    if top in (0x0400, 0x0600) and sub in (0x0140, 0x0160):
        return Instruction(pc, end, op, "setf", immediate=op & 0x3F,
                           condition=1 if top == 0x0600 else 0)
    if top in (0x0400, 0x0600) and sub == 0x01C0:
        src_addr = rom.long(end)
        dst_addr = rom.long((end + 32) & MASK32)
        return Instruction(pc, (end + 64) & MASK32, op, "move_abs_copy",
                           immediate=src_addr, target=dst_addr,
                           field=1 if top == 0x0600 else 0)
    if top in (0x0400, 0x0600) and sub == 0x0180:
        return Instruction(pc, (end + 32) & MASK32, op, "move_ra", bank,
                           dst=dst, immediate=rom.long(end),
                           field=1 if top == 0x0600 else 0)
    if top in (0x0400, 0x0600) and sub == 0x01A0:
        return Instruction(pc, (end + 32) & MASK32, op, "move_ar", bank,
                           dst=dst, immediate=rom.long(end),
                           field=1 if top == 0x0600 else 0)
    if top in (0x0400, 0x0600) and sub == 0x01E0:
        return Instruction(pc, (end + 32) & MASK32, op,
                           "move_byte_abs_store" if top == 0x0400 else "move_byte_abs_load",
                           bank, dst=dst, immediate=rom.long(end))
    if top in (0x0400, 0x0600) and sub in (0x0100, 0x0120):
        return Instruction(pc, end, op, "sext" if sub == 0x0100 else "zext",
                           bank, dst=dst, field=1 if top == 0x0600 else 0)

    # Immediate long/word loads occupy 3/2 instruction words respectively.
    if top == 0x0800 and sub == 0x01C0:
        value = rom.word(end)
        return Instruction(pc, (end + 16) & MASK32, op, "movi_w", bank, dst=dst,
                           immediate=value if value < 0x8000 else value - 0x10000)
    if top == 0x0800 and sub == 0x01E0:
        return Instruction(pc, (end + 32) & MASK32, op, "movi_l", bank, dst=dst,
                           immediate=rom.long(end))
    if top == 0x0800 and sub == 0x0120:
        return Instruction(pc, end, op, "call_indirect", bank, dst=dst)
    if top == 0x0800 and sub in (0x0180, 0x01A0):
        return Instruction(pc, (end + 16) & MASK32, op,
                           "mmtm" if sub == 0x0180 else "mmfm", bank,
                           dst=dst, immediate=rom.word(end))

    # TMS34010 immediate ALU encodings (MAME's 0x0a00 group).
    immediate_forms = {
        0x0100: ("addi_w", 16, False), 0x0120: ("addi_l", 32, False),
        0x0140: ("cmpi_w", 16, True),  0x0160: ("cmpi_l", 32, True),
        0x0180: ("andi_l", 32, True),  0x01A0: ("ori_l", 32, False),
        0x01C0: ("xori_l", 32, False), 0x01E0: ("subi_w", 16, True),
    }
    if top == 0x0A00 and sub in immediate_forms:
        name, width, complement = immediate_forms[sub]
        raw = rom.word(end) if width == 16 else rom.long(end)
        value = (~raw) & (0xFFFF if width == 16 else MASK32) if complement else raw
        if width == 16 and value & 0x8000:
            value -= 0x10000
        return Instruction(pc, (end + width) & MASK32, op, name, bank,
                           dst=dst, immediate=value)
    if top == 0x0C00 and sub == 0x0100:
        raw = rom.long(end)
        return Instruction(pc, (end + 32) & MASK32, op, "subi_l", bank,
                           dst=dst, immediate=(~raw) & MASK32)

    # Displacement based field moves: MOVE r,@(disp,rd) / @(disp,rs),r.
    if top in (0xB000, 0xB200):
        disp = rom.word(end)
        disp = disp if disp < 0x8000 else disp - 0x10000
        return Instruction(pc, (end + 16) & MASK32, op, "move_disp_store", bank,
                           src=src, dst=dst, immediate=disp,
                           field=1 if top == 0xB200 else 0)
    if top in (0xB400, 0xB600):
        disp = rom.word(end)
        disp = disp if disp < 0x8000 else disp - 0x10000
        return Instruction(pc, (end + 16) & MASK32, op, "move_disp_load", bank,
                           src=src, dst=dst, immediate=disp,
                           field=1 if top == 0xB600 else 0)
    if top in (0xB800, 0xBA00):
        src_disp = rom.word(end)
        dst_disp = rom.word((end + 16) & MASK32)
        src_disp = src_disp if src_disp < 0x8000 else src_disp - 0x10000
        dst_disp = dst_disp if dst_disp < 0x8000 else dst_disp - 0x10000
        return Instruction(pc, (end + 32) & MASK32, op, "move_disp_copy", bank,
                           src=src, dst=dst, immediate=src_disp, target=dst_disp,
                           field=1 if top == 0xBA00 else 0)
    if top in (0xAC00, 0xAE00):
        disp = rom.word(end)
        disp = disp if disp < 0x8000 else disp - 0x10000
        return Instruction(pc, (end + 16) & MASK32, op,
                           "move_byte_disp_store" if top == 0xAC00 else "move_byte_disp_load",
                           bank, src=src, dst=dst, immediate=disp)
    if top in (0x8C00, 0x8E00):
        return Instruction(pc, end, op,
                           "move_byte_store" if top == 0x8C00 else "move_byte_load",
                           bank, src=src, dst=dst)
    if top in (0x9C00, 0x9D00):
        return Instruction(pc, end, op, "move_byte_copy", bank, src=src, dst=dst)

    memory_moves = {
        0x8000: "move_store", 0x8400: "move_load", 0x8800: "move_copy",
        0x9000: "move_store_post", 0x9400: "move_load_post",
        0x9800: "move_copy_post", 0xA000: "move_store_pre",
        0xA400: "move_load_pre", 0xA800: "move_copy_pre",
        0x8200: "move_store", 0x8600: "move_load", 0x8A00: "move_copy",
        0x9200: "move_store_post", 0x9600: "move_load_post",
        0x9A00: "move_copy_post", 0xA200: "move_store_pre",
        0xA600: "move_load_pre", 0xAA00: "move_copy_pre",
    }
    if top in memory_moves:
        return Instruction(pc, end, op, memory_moves[top], bank, src=src,
                           dst=dst, field=1 if top & 0x0200 else 0)

    # Constant arithmetic uses the five-bit K field; K=0 encodes 32.
    if top in (0x1000, 0x1200):
        k = (op >> 5) & 0x1F
        return Instruction(pc, end, op, "addk", bank, dst=dst, immediate=k or 32)
    if top in (0x1400, 0x1600):
        k = (op >> 5) & 0x1F
        return Instruction(pc, end, op, "subk", bank, dst=dst, immediate=k or 32)
    if top in (0x1800, 0x1A00):
        k = (op >> 5) & 0x1F
        return Instruction(pc, end, op, "movk", bank, dst=dst, immediate=k or 32)
    if top in (0x1C00, 0x1E00):
        return Instruction(pc, end, op, "btst_k", bank, dst=dst,
                           immediate=(op >> 5) & 0x1F)
    if top in (0x2000, 0x2200, 0x2400, 0x2600,
               0x2800, 0x2A00, 0x2C00, 0x2E00):
        group = top & 0xFC00
        name = {0x2000: "sla_k", 0x2400: "sll_k",
                0x2800: "sra_k", 0x2C00: "srl_k"}[group]
        return Instruction(pc, end, op, name, bank, dst=dst,
                           immediate=(op >> 5) & 0x1F)
    if top in (0x3000, 0x3200, 0x3400, 0x3600):
        return Instruction(pc, end, op, "rl_k", bank, dst=dst,
                           immediate=(op >> 5) & 0x1F)
    if top in (0x6000, 0x6200, 0x6400, 0x6600):
        name = {0x6000: "sla_r", 0x6200: "sll_r",
                0x6400: "sra_r", 0x6600: "srl_r"}[top]
        return Instruction(pc, end, op, name, bank, src=src, dst=dst)
    if top == 0x6800:
        return Instruction(pc, end, op, "rl_r", bank, src=src, dst=dst)
    if top == 0x6A00:
        return Instruction(pc, end, op, "lmo", bank, src=src, dst=dst)
    if top == 0x4A00:
        return Instruction(pc, end, op, "btst_r", bank, src=src, dst=dst)
    if top == 0x5200:
        return Instruction(pc, end, op, "andn", bank, src=src, dst=dst)
    if top in (0x5C00, 0x5E00):
        return Instruction(pc, end, op, "mpys" if top == 0x5C00 else "mpyu",
                           bank, src=src, dst=dst)
    if top == 0x4200:
        return Instruction(pc, end, op, "addc", bank, src=src, dst=dst)
    if top == 0x4600:
        return Instruction(pc, end, op, "subb", bank, src=src, dst=dst)
    if top in (0xE000, 0xE200, 0xE400):
        return Instruction(pc, end, op,
                           {0xE000: "add_xy", 0xE200: "sub_xy", 0xE400: "cmp_xy"}[top],
                           bank, src=src, dst=dst)
    if top == 0xE800:
        return Instruction(pc, end, op, "cvxyl", bank, src=src, dst=dst)
    if top == 0xF600:
        return Instruction(pc, end, op, "drav", bank, src=src, dst=dst)
    if top in (0x5A00, 0x5B00):
        return Instruction(pc, end, op, "divu", bank, src=src, dst=dst)
    if top == 0x5800:
        return Instruction(pc, end, op, "divs", bank, src=src, dst=dst)
    if top == 0xEC00:
        return Instruction(pc, end, op, "movx", bank, src=src, dst=dst)
    if top == 0xEE00:
        return Instruction(pc, end, op, "movy", bank, src=src, dst=dst)
    if top in (0xD400, 0xD600) and (op & 0x0100):
        return Instruction(pc, end, op, "exgf", bank, dst=dst,
                           field=1 if top == 0xD600 else 0)
    if top in (0x3800, 0x3A00, 0x3C00, 0x3E00):
        displacement = (op >> 5) & 0x1F
        direction = -1 if op & 0x0400 else 1
        target = (end + direction * displacement * 16) & MASK32
        target &= 0xFFFFFFF0
        return Instruction(pc, end, op, "dsjs", bank, dst=dst,
                           target=target, fallthrough=end,
                           branch_cycles_taken=2, branch_cycles_not_taken=3)
    if top == 0x0C00 and sub == 0x0120:
        disp = rom.word(end)
        disp = disp if disp < 0x8000 else disp - 0x10000
        fallthrough = (end + 16) & MASK32
        target = (fallthrough + (disp << 4)) & MASK32
        target &= 0xFFFFFFF0
        return Instruction(pc, fallthrough, op, "callr", target=target,
                           fallthrough=fallthrough)
    if top == 0x0C00 and sub == 0x0140:
        fallthrough = (end + 32) & MASK32
        target = rom.long(end) & 0xFFFFFFF0
        return Instruction(pc, fallthrough, op, "calla", target=target,
                           fallthrough=fallthrough)
    if top == 0x0C00 and sub in (0x0180, 0x01A0, 0x01C0):
        disp = rom.word(end)
        disp = disp if disp < 0x8000 else disp - 0x10000
        fallthrough = (end + 16) & MASK32
        target = (fallthrough + (disp << 4)) & 0xFFFFFFF0
        cc = {0x0180: 0, 0x01A0: 1, 0x01C0: 2}[sub]
        return Instruction(pc, fallthrough, op, "dsj", bank, dst=dst,
                           target=target, fallthrough=fallthrough, condition=cc,
                           branch_cycles_taken=3, branch_cycles_not_taken=2)
    if top == 0x0800 and sub == 0x0160:
        return Instruction(pc, end, op, "rets", immediate=op & 0x1F)
    if top == 0x0C00 and sub == 0x0160:
        # EINT only consumes its opcode word; MAME's handler advances no
        # operand cursor, so the next instruction is immediately after it.
        return Instruction(pc, end, op, "eint")
    if top == 0x0C00 and sub == 0x01E0:
        return Instruction(pc, end, op, "setc")

    # Core register-to-register arithmetic and logical operations.
    arithmetic = {
        0x4000: "add",
        0x4400: "sub",
        0x4800: "cmp",
        0x4C00: "move",
        0x5000: "and",
        0x5400: "or",
        0x5600: "xor",
    }
    if top in (0x4E00, 0x4F00):
        source_bank = bank
        destination_bank = "A" if source_bank == "B" else "B"
        return Instruction(pc, end, op, "move_cross", source_bank,
                           src=src, dst=dst, condition=1 if destination_bank == "B" else 0)
    name = arithmetic.get(top)
    if name:
        return Instruction(pc, end, op, name, bank, src=src, dst=dst)

    # Conditional JR/JA. The short displacement is in 16-bit words; the
    # extended relative displacement is a signed 16-bit word; JA carries a
    # 32-bit bit-address. All PCs are normalized to the TMS instruction
    # boundary (16 bits) as MAME does after control transfers.
    if (op & 0xF000) == 0xC000:
        cc = (op >> 8) & 0x0F
        low = op & 0xFF
        if low == 0:
            disp = rom.word(end)
            disp = disp if disp < 0x8000 else disp - 0x10000
            fallthrough = (end + 16) & MASK32
            target = (fallthrough + (disp << 4)) & MASK32
            target &= 0xFFFFFFF0
            return Instruction(pc, fallthrough, op, "jr", condition=cc,
                               target=target, fallthrough=fallthrough,
                               branch_cycles_taken=3, branch_cycles_not_taken=2)
        if low == 0x80:
            target = rom.long(end) & 0xFFFFFFF0
            fallthrough = (end + 32) & MASK32
            return Instruction(pc, fallthrough, op, "ja", condition=cc,
                               target=target, fallthrough=fallthrough,
                               branch_cycles_taken=3, branch_cycles_not_taken=4)
        disp = low if low < 0x80 else low - 0x100
        fallthrough = end
        target = (fallthrough + (disp << 4)) & MASK32
        target &= 0xFFFFFFF0
        return Instruction(pc, fallthrough, op, "jrs", condition=cc,
                           target=target, fallthrough=fallthrough,
                           branch_cycles_taken=2, branch_cycles_not_taken=1)

    return None


def c_u32(value: int) -> str:
    return f"0x{value & MASK32:08x}u"


def emit_instruction(ins: Instruction) -> list[str]:
    s = "s"
    reg = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
    out = [f"    if ({s}.cycles >= stop_cycle) return BlockExit::Budget;"]
    if ins.name == "nop":
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "dint":
        out += [f"    {s}.st &= ~(1u << 21);", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 3u;"]
    elif ins.name == "clrc":
        out += [f"    {s}.st &= ~flag_c;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "eint":
        out += [f"    {s}.st |= (1u << 21);", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 3u;"]
    elif ins.name == "setc":
        out += [f"    {s}.st |= flag_c;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "emu":
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 6u;"]
    elif ins.name == "reserved":
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "illop":
        out += [f"    {s}.sp -= 32u;",
                f"    if (!write_long(bus, {s}.sp, {c_u32(ins.next_pc)}))",
                "        return BlockExit::BusFault;",
                f"    {s}.sp -= 32u;",
                f"    if (!write_long(bus, {s}.sp, {s}.st))",
                "        return BlockExit::BusFault;",
                f"    {s}.st = 0x10u;",
                f"    if (!read_long(bus, 0xfffffc20u, {s}.pc))",
                "        return BlockExit::BusFault;",
                f"    {s}.cycles += 16u;", "    return BlockExit::Dispatch;"]
    elif ins.name == "rev":
        out += [f"    {reg} = 8u;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "pixblt_b_xy":
        out += ["    if (!execute_pixblt_b_xy_native(s, bus))",
                "        return BlockExit::BusFault;"]
    elif ins.name == "pixt_ri":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    if (!write_pixel_operation(bus, {reg}, {src}))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 2u;"]
    elif ins.name == "pixt_ir":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += ["    if (!bus.read_pixel) return BlockExit::BusFault;",
                f"    const std::uint32_t pixel_{ins.pc:x} = bus.read_pixel(bus.opaque, {src});",
                f"    {reg} = pixel_{ins.pc:x};",
                f"    {s}.st = ({s}.st & ~flag_v) | (pixel_{ins.pc:x} ? flag_v : 0u);",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 4u;"]
    elif ins.name == "pixt_ii":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += ["    if (!bus.read_pixel || !bus.write_pixel) return BlockExit::BusFault;",
                f"    const std::uint32_t pixel_{ins.pc:x} = bus.read_pixel(bus.opaque, {src});",
                f"    if (!write_pixel_operation(bus, {reg}, pixel_{ins.pc:x}))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 4u;"]
    elif ins.name == "cvxyl":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += ["    if (!bus.convert_dst_xy) return BlockExit::BusFault;",
                f"    if (!bus.convert_dst_xy(bus.opaque, {src}, reg({s}, Bank::B, 4), &{reg}))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 3u;"]
    elif ins.name == "drav":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += ["    if (!bus.window_checking || !bus.convert_dst_xy || !bus.write_pixel)",
                "        return BlockExit::BusFault;",
                "    const std::uint8_t window_mode = bus.window_checking(bus.opaque);",
                f"    const std::uint32_t draw_xy_{ins.pc:x} = {reg};",
                f"    const std::uint32_t draw_delta_{ins.pc:x} = {src};",
                "    bool draw_pixel = true;",
                "    if (window_mode != 0) {",
                f"        {s}.st &= ~flag_v;",
                f"        const std::int16_t draw_x_{ins.pc:x} = static_cast<std::int16_t>(draw_xy_{ins.pc:x});",
                f"        const std::int16_t draw_y_{ins.pc:x} = static_cast<std::int16_t>(draw_xy_{ins.pc:x} >> 16);",
                f"        const std::int16_t win_start_x_{ins.pc:x} = static_cast<std::int16_t>(reg({s}, Bank::B, 5));",
                f"        const std::int16_t win_start_y_{ins.pc:x} = static_cast<std::int16_t>(reg({s}, Bank::B, 5) >> 16);",
                f"        const std::int16_t win_end_x_{ins.pc:x} = static_cast<std::int16_t>(reg({s}, Bank::B, 6));",
                f"        const std::int16_t win_end_y_{ins.pc:x} = static_cast<std::int16_t>(reg({s}, Bank::B, 6) >> 16);",
                f"        if (draw_x_{ins.pc:x} < win_start_x_{ins.pc:x} || draw_x_{ins.pc:x} > win_end_x_{ins.pc:x} ||",
                f"            draw_y_{ins.pc:x} < win_start_y_{ins.pc:x} || draw_y_{ins.pc:x} > win_end_y_{ins.pc:x}) {{",
                f"            {s}.st |= flag_v; draw_pixel = false;",
                "        } else if (window_mode == 1) draw_pixel = false;",
                "    }",
                f"    if (draw_pixel) {{ std::uint32_t draw_address_{ins.pc:x} = 0;",
                f"        if (!bus.convert_dst_xy(bus.opaque, draw_xy_{ins.pc:x}, reg({s}, Bank::B, 4), &draw_address_{ins.pc:x}) ||",
                f"            !write_pixel_operation(bus, draw_address_{ins.pc:x}, reg({s}, Bank::B, 9)))",
                "            return BlockExit::BusFault;",
                "    }",
                f"    {reg} = (((draw_xy_{ins.pc:x} & 0xffffu) + (draw_delta_{ins.pc:x} & 0xffffu)) & 0xffffu) |",
                f"        (((((draw_xy_{ins.pc:x} >> 16) & 0xffffu) + ((draw_delta_{ins.pc:x} >> 16) & 0xffffu)) & 0xffffu) << 16);",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 4u;"]
    elif ins.name == "move_abs_copy":
        out += [f"    std::uint32_t field_value_{ins.pc:x} = 0;",
                f"    if (!read_field(bus, {c_u32(ins.immediate)}, {s}.st, {ins.field}u, field_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;",
                f"    if (!write_field(bus, {c_u32(ins.target)}, field_value_{ins.pc:x}, {s}.st, {ins.field}u))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 7u;"]
    elif ins.name == "exgf":
        shift = 6 if ins.field else 0
        out += [f"    const std::uint32_t exchanged_field_{ins.pc:x} = ({s}.st >> {shift}) & 0x3fu;",
                f"    {s}.st = ({s}.st & ~(0x3fu << {shift})) | (({reg} & 0x3fu) << {shift});",
                f"    {reg} = exchanged_field_{ins.pc:x};", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "putst":
        out += [f"    {s}.st = {reg};", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 3u;"]
    elif ins.name == "pushst":
        out += [f"    {s}.sp -= 32u;",
                f"    if (!write_long(bus, {s}.sp, {s}.st)) return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 2u;"]
    elif ins.name == "popst":
        out += [f"    if (!read_long(bus, {s}.sp, {s}.st)) return BlockExit::BusFault;",
                f"    {s}.sp += 32u;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 8u;"]
    elif ins.name == "not":
        out += [f"    {reg} = ~{reg};", f"    set_logic_z({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "neg":
        out += [f"    const std::uint32_t neg_lhs_{ins.pc:x} = {reg};",
                f"    const std::uint32_t neg_result_{ins.pc:x} = 0u - neg_lhs_{ins.pc:x};",
                f"    set_sub_flags({s}, 0u, neg_lhs_{ins.pc:x}, neg_result_{ins.pc:x});",
                f"    {reg} = neg_result_{ins.pc:x};",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "abs":
        out += [f"    const std::uint32_t abs_lhs_{ins.pc:x} = {reg};",
                f"    const std::uint32_t abs_result_{ins.pc:x} = 0u - abs_lhs_{ins.pc:x};",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                f"    if (static_cast<std::int32_t>(abs_result_{ins.pc:x}) > 0) {reg} = abs_result_{ins.pc:x};",
                f"    set_nz({s}, abs_result_{ins.pc:x});",
                f"    if (abs_result_{ins.pc:x} == 0x80000000u) {s}.st |= flag_v;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "lmo":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    std::uint32_t lmo_source_{ins.pc:x} = {src};",
                f"    std::uint32_t lmo_result_{ins.pc:x} = 0;",
                f"    {s}.st &= ~flag_z;",
                f"    if (lmo_source_{ins.pc:x} == 0) {s}.st |= flag_z;",
                f"    else while (!(lmo_source_{ins.pc:x} & flag_n)) {{ ++lmo_result_{ins.pc:x}; lmo_source_{ins.pc:x} <<= 1; }}",
                f"    reg({s}, Bank::{ins.bank}, {ins.dst}) = lmo_result_{ins.pc:x};",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "btst_r":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    {s}.st &= ~flag_z;",
                f"    if (!({dst} & (1u << ({src} & 0x1fu)))) {s}.st |= flag_z;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 2u;"]
    elif ins.name == "btst_k":
        bit = 31 - ins.immediate
        out += [f"    {s}.st &= ~flag_z;",
                f"    if (!({reg} & (1u << {bit}u))) {s}.st |= flag_z;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "andn":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    {reg} &= ~{src};", f"    set_logic_z({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "divu":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    {s}.st &= ~(flag_z | flag_v);",
                f"    if (!{src}) {s}.st |= flag_v;",
                f"    else if ({ins.dst}u & 1u) {{ {dst} /= {src}; if (!{dst}) {s}.st |= flag_z; }}",
                f"    else {{ const std::uint64_t dividend_{ins.pc:x} = (static_cast<std::uint64_t>({dst}) << 32) | reg({s}, Bank::{ins.bank}, {ins.dst + 1});",
                f"        const std::uint64_t quotient_{ins.pc:x} = dividend_{ins.pc:x} / {src};",
                f"        if (quotient_{ins.pc:x} > 0xffffffffull) {s}.st |= flag_v;",
                f"        else {{ {dst} = static_cast<std::uint32_t>(quotient_{ins.pc:x}); reg({s}, Bank::{ins.bank}, {ins.dst + 1}) = static_cast<std::uint32_t>(dividend_{ins.pc:x} % {src}); if (!{dst}) {s}.st |= flag_z; }} }}",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 37u;"]
    elif ins.name == "divs":
        src = f"static_cast<std::int32_t>(reg({s}, Bank::{ins.bank}, {ins.src}))"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    {s}.st &= ~(flag_z | flag_v);",
                f"    if (!{src}) {s}.st |= flag_v;"]
        if ins.dst & 1:
            out += [f"    else if ({dst} == 0x80000000u && {src} == -1) {s}.st |= flag_v;",
                    f"    else {{ {dst} = static_cast<std::uint32_t>(static_cast<std::int32_t>({dst}) / {src}); set_nz({s}, {dst}); }}"]
        else:
            out += [f"    else {{ const std::uint64_t dividend_bits_{ins.pc:x} = (static_cast<std::uint64_t>({dst}) << 32) | reg({s}, Bank::{ins.bank}, {ins.dst + 1});",
                    f"        const std::int64_t dividend_{ins.pc:x} = static_cast<std::int64_t>(dividend_bits_{ins.pc:x});",
                    f"        if (dividend_{ins.pc:x} == (-9223372036854775807ll - 1ll) && {src} == -1) {s}.st |= flag_v;",
                    f"        else {{ const std::int64_t quotient_{ins.pc:x} = dividend_{ins.pc:x} / {src};",
                    f"            if (quotient_{ins.pc:x} < -2147483648ll || quotient_{ins.pc:x} > 2147483647ll) {s}.st |= flag_v;",
                    f"            else {{ {dst} = static_cast<std::uint32_t>(static_cast<std::int32_t>(quotient_{ins.pc:x}));",
                    f"                reg({s}, Bank::{ins.bank}, {ins.dst + 1}) = static_cast<std::uint32_t>(dividend_{ins.pc:x} % {src}); set_nz({s}, {dst}); }} }} }}"]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += {39 if ins.dst & 1 else 40}u;"]
    elif ins.name in ("add_xy", "sub_xy", "cmp_xy"):
        lhs = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        rhs = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        if ins.name == "add_xy":
            out += [f"    const std::uint32_t xy_dst_{ins.pc:x} = {lhs};",
                    f"    const std::uint32_t xy_src_{ins.pc:x} = {rhs};",
                    f"    const std::uint32_t xy_x_{ins.pc:x} = (((xy_dst_{ins.pc:x} & 0xffffu) + (xy_src_{ins.pc:x} & 0xffffu)) & 0xffffu);",
                    f"    const std::uint32_t xy_y_{ins.pc:x} = ((((xy_dst_{ins.pc:x} >> 16) & 0xffffu) + ((xy_src_{ins.pc:x} >> 16) & 0xffffu)) & 0xffffu);",
                    f"    {lhs} = xy_x_{ins.pc:x} | (xy_y_{ins.pc:x} << 16);",
                    f"    {s}.st &= ~(flag_n | flag_c | flag_z | flag_v);",
                    f"    if (!xy_x_{ins.pc:x}) {s}.st |= flag_n;",
                    f"    if (xy_y_{ins.pc:x} & 0x8000u) {s}.st |= flag_c;",
                    f"    if (!xy_y_{ins.pc:x}) {s}.st |= flag_z;",
                    f"    if (xy_x_{ins.pc:x} & 0x8000u) {s}.st |= flag_v;"]
        else:
            out += [f"    const std::uint32_t xy_dst_{ins.pc:x} = {lhs};",
                    f"    const std::uint32_t xy_src_{ins.pc:x} = {rhs};",
                    f"    const std::uint32_t xy_x_{ins.pc:x} = ((xy_dst_{ins.pc:x} - xy_src_{ins.pc:x}) & 0xffffu);",
                    f"    const std::uint32_t xy_y_{ins.pc:x} = (((xy_dst_{ins.pc:x} >> 16) - (xy_src_{ins.pc:x} >> 16)) & 0xffffu);",
                    f"    {s}.st &= ~(flag_n | flag_c | flag_z | flag_v);"]
            if ins.name == "sub_xy":
                out += [f"    if ((xy_src_{ins.pc:x} & 0xffffu) == (xy_dst_{ins.pc:x} & 0xffffu)) {s}.st |= flag_n;",
                        f"    if ((xy_src_{ins.pc:x} >> 16) > (xy_dst_{ins.pc:x} >> 16)) {s}.st |= flag_c;",
                        f"    if ((xy_src_{ins.pc:x} >> 16) == (xy_dst_{ins.pc:x} >> 16)) {s}.st |= flag_z;",
                        f"    if ((xy_src_{ins.pc:x} & 0xffffu) > (xy_dst_{ins.pc:x} & 0xffffu)) {s}.st |= flag_v;",
                        f"    {lhs} = xy_x_{ins.pc:x} | (xy_y_{ins.pc:x} << 16);"]
            elif ins.name == "cmp_xy":
                out += [f"    if (!xy_x_{ins.pc:x}) {s}.st |= flag_n;",
                        f"    if (xy_x_{ins.pc:x} & 0x8000u) {s}.st |= flag_v;",
                        f"    if (!xy_y_{ins.pc:x}) {s}.st |= flag_z;",
                        f"    if (xy_y_{ins.pc:x} & 0x8000u) {s}.st |= flag_c;"]
            else:
                out += [f"    if (!xy_x_{ins.pc:x}) {s}.st |= flag_n;",
                        f"    if (xy_y_{ins.pc:x} & 0x8000u) {s}.st |= flag_c;",
                        f"    if (!xy_y_{ins.pc:x}) {s}.st |= flag_z;",
                        f"    if (xy_x_{ins.pc:x} & 0x8000u) {s}.st |= flag_v;"]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("addc", "subb"):
        lhs = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        rhs = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        carry = f"(({s}.st & flag_c) ? 1u : 0u)"
        out += [f"    const std::uint32_t carry_lhs_{ins.pc:x} = {lhs};",
                f"    const std::uint32_t carry_rhs_{ins.pc:x} = {rhs};",
                f"    const std::uint32_t carry_result_{ins.pc:x} = "
                f"carry_lhs_{ins.pc:x} {'+' if ins.name == 'addc' else '-'} carry_rhs_{ins.pc:x} "
                f"{'+' if ins.name == 'addc' else '-'} {carry};",
                (f"    set_add_flags({s}, carry_lhs_{ins.pc:x}, carry_rhs_{ins.pc:x}, carry_result_{ins.pc:x});"
                 if ins.name == "addc" else
                 f"    set_sub_flags({s}, carry_lhs_{ins.pc:x}, carry_rhs_{ins.pc:x}, carry_result_{ins.pc:x});"),
                f"    {lhs} = carry_result_{ins.pc:x};", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name in ("mpys", "mpyu"):
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        signed = ins.name == "mpys"
        out += [f"    unsigned mul_width_{ins.pc:x} = ({s}.st >> 6) & 0x1fu;",
                f"    if (!mul_width_{ins.pc:x}) mul_width_{ins.pc:x} = 32u;",
                f"    std::uint32_t mul_src_{ins.pc:x} = {src};",
                f"    if (mul_width_{ins.pc:x} < 32u) {{",
                f"        const std::uint32_t mul_mask_{ins.pc:x} = (1u << mul_width_{ins.pc:x}) - 1u;",
                f"        mul_src_{ins.pc:x} &= mul_mask_{ins.pc:x};",
                *([f"        if (mul_src_{ins.pc:x} & (1u << (mul_width_{ins.pc:x} - 1u))) mul_src_{ins.pc:x} |= ~mul_mask_{ins.pc:x};"] if signed else []),
                "    }"]
        if signed:
            out += [f"    const std::int64_t product_signed_{ins.pc:x} = static_cast<std::int64_t>(static_cast<std::int32_t>(mul_src_{ins.pc:x})) * static_cast<std::int64_t>(static_cast<std::int32_t>({dst}));",
                    f"    const std::uint64_t product_{ins.pc:x} = static_cast<std::uint64_t>(product_signed_{ins.pc:x});"]
        else:
            out += [f"    const std::uint64_t product_{ins.pc:x} = static_cast<std::uint64_t>(mul_src_{ins.pc:x}) * static_cast<std::uint64_t>({dst});"]
        out += [f"    {s}.st &= ~flag_z;",
                f"    if (!product_{ins.pc:x}) {s}.st |= flag_z;"]
        if signed:
            out += [f"    {s}.st &= ~flag_n;",
                    f"    if ((product_{ins.pc:x} >> 63) & 1u) {s}.st |= flag_n;"]
        out += [f"    {dst} = static_cast<std::uint32_t>(product_{ins.pc:x} >> 32);",
                f"    reg({s}, Bank::{ins.bank}, {ins.dst | 1}) = static_cast<std::uint32_t>(product_{ins.pc:x});",
                f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {20 if signed else 21}u;"]
    elif ins.name == "getpc":
        out += [f"    {reg} = {c_u32(ins.next_pc)};", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "getst":
        out += [f"    {reg} = {s}.st;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "exgpc":
        out += [f"    const std::uint32_t exchanged_pc_{ins.pc:x} = {reg};",
                f"    {reg} = {c_u32(ins.next_pc)};",
                f"    {s}.pc = exchanged_pc_{ins.pc:x} & 0xfffffff0u;",
                f"    {s}.cycles += 2u;", "    return BlockExit::Dispatch;"]
    elif ins.name == "setf":
        shift = 6 if ins.condition else 0
        out += [f"    {s}.st = ({s}.st & ~(0x3fu << {shift})) | "
                f"({ins.immediate}u << {shift});",
                f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {1 + ins.condition}u;"]
    elif ins.name == "jump":
        out += [f"    {s}.pc = reg({s}, Bank::{ins.bank}, {ins.dst}) & 0xfffffff0u;",
                f"    {s}.cycles += 2u;", "    return BlockExit::Dispatch;"]
    elif ins.name in ("movi_w", "movi_l"):
        out += [f"    {reg} = {c_u32(ins.immediate)};",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                f"    set_nz({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {2 if ins.name == 'movi_w' else 3}u;"]
    elif ins.name in ("addi_w", "addi_l", "subi_w", "subi_l", "cmpi_w", "cmpi_l"):
        lhs = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        subtract = ins.name.startswith(("subi", "cmpi"))
        out += [f"    const std::uint32_t lhs_{ins.pc:x} = {lhs};",
                f"    const std::uint32_t rhs_{ins.pc:x} = {c_u32(ins.immediate)};",
                f"    const std::uint32_t result_{ins.pc:x} = "
                f"lhs_{ins.pc:x} {'-' if subtract else '+'} rhs_{ins.pc:x};",
                (f"    set_sub_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"
                 if subtract else
                 f"    set_add_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"),
                *([] if ins.name.startswith("cmpi") else [f"    {lhs} = result_{ins.pc:x};"]),
                f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {2 if ins.name.endswith('_w') else 3}u;"]
    elif ins.name in ("andi_l", "ori_l", "xori_l"):
        op = {"andi_l": "&=", "ori_l": "|=", "xori_l": "^="}[ins.name]
        out += [f"    {reg} {op} {c_u32(ins.immediate)};",
                f"    set_logic_z({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 3u;"]
    elif ins.name == "movk":
        out += [f"    {reg} = {c_u32(ins.immediate)};",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("addk", "subk"):
        imm = c_u32(ins.immediate)
        out += [f"    const std::uint32_t lhs_{ins.pc:x} = {reg};",
                f"    const std::uint32_t rhs_{ins.pc:x} = {imm};",
                f"    const std::uint32_t result_{ins.pc:x} = "
                f"lhs_{ins.pc:x} {'+' if ins.name == 'addk' else '-'} rhs_{ins.pc:x};",
                (f"    set_add_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"
                 if ins.name == "addk" else
                 f"    set_sub_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"),
                f"    {reg} = result_{ins.pc:x};",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("add", "sub", "cmp"):
        lhs = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        rhs = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    const std::uint32_t lhs_{ins.pc:x} = {lhs};",
                f"    const std::uint32_t rhs_{ins.pc:x} = {rhs};",
                f"    const std::uint32_t result_{ins.pc:x} = "
                f"lhs_{ins.pc:x} {'+' if ins.name == 'add' else '-'} rhs_{ins.pc:x};",
                (f"    set_add_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"
                 if ins.name == "add" else
                 f"    set_sub_flags({s}, lhs_{ins.pc:x}, rhs_{ins.pc:x}, result_{ins.pc:x});"),
                *([] if ins.name == "cmp" else [f"    {lhs} = result_{ins.pc:x};"]),
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("and", "or", "xor"):
        rhs = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        op = {"and": "&", "or": "|", "xor": "^"}[ins.name]
        out += [f"    {reg} {op}= {rhs};", f"    set_logic_z({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "move":
        rhs = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    {reg} = {rhs};",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                f"    set_nz({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "move_ra":
        out += [f"    if (!write_field(bus, {c_u32(ins.immediate)}, {reg}, {s}.st, {ins.field}u))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 3u;"]
    elif ins.name == "move_ar":
        out += [f"    std::uint32_t absolute_value_{ins.pc:x} = 0;",
                f"    if (!read_field(bus, {c_u32(ins.immediate)}, {s}.st, {ins.field}u, absolute_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                f"    {reg} = absolute_value_{ins.pc:x};", f"    set_nz({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 5u;"]
    elif ins.name == "move_byte_abs_store":
        out += [f"    if (!write_byte(bus, {c_u32(ins.immediate)}, static_cast<std::uint8_t>({reg})))",
                "        return BlockExit::BusFault;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 1u;"]
    elif ins.name == "move_byte_abs_load":
        out += [f"    std::uint8_t byte_value_{ins.pc:x} = 0;",
                f"    if (!read_byte(bus, {c_u32(ins.immediate)}, byte_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;", f"    {reg} = static_cast<std::int8_t>(byte_value_{ins.pc:x});",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);", f"    set_nz({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 5u;"]
    elif ins.name == "move_byte_store":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    if (!write_byte(bus, {dst}, static_cast<std::uint8_t>({src})))",
                "        return BlockExit::BusFault;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "move_byte_load":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    std::uint8_t byte_value_{ins.pc:x} = 0;",
                f"    if (!read_byte(bus, {src}, byte_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;", f"    {dst} = static_cast<std::int8_t>(byte_value_{ins.pc:x});",
                f"    {s}.st &= ~(flag_n | flag_z | flag_v);", f"    set_nz({s}, {dst});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 3u;"]
    elif ins.name == "move_byte_copy":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    std::uint8_t byte_value_{ins.pc:x} = 0;",
                f"    if (!read_byte(bus, {src}, byte_value_{ins.pc:x}) || !write_byte(bus, {dst}, byte_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 3u;"]
    elif ins.name in ("move_byte_disp_store", "move_byte_disp_load"):
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        if ins.name == "move_byte_disp_store":
            out += [f"    if (!write_byte(bus, {dst} + {ins.immediate}, static_cast<std::uint8_t>({src})))",
                    "        return BlockExit::BusFault;", f"    {s}.cycles += 3u;"]
        else:
            out += [f"    std::uint8_t disp_byte_{ins.pc:x} = 0;",
                    f"    if (!read_byte(bus, {src} + {ins.immediate}, disp_byte_{ins.pc:x}))",
                    "        return BlockExit::BusFault;",
                    f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                    f"    {dst} = static_cast<std::int8_t>(disp_byte_{ins.pc:x});",
                    f"    set_nz({s}, {dst});", f"    {s}.cycles += 5u;"]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};"]
    elif ins.name in ("move_disp_store", "move_disp_load"):
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        if ins.name == "move_disp_store":
            out += [f"    if (!write_field(bus, {dst} + {ins.immediate}, {src}, {s}.st, {ins.field}u))",
                    "        return BlockExit::BusFault;", f"    {s}.cycles += 3u;"]
        else:
            out += [f"    std::uint32_t disp_value_{ins.pc:x} = 0;",
                    f"    if (!read_field(bus, {src} + {ins.immediate}, {s}.st, {ins.field}u, disp_value_{ins.pc:x}))",
                    "        return BlockExit::BusFault;",
                    f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                    f"    {dst} = disp_value_{ins.pc:x};", f"    set_nz({s}, {dst});",
                    f"    {s}.cycles += 5u;"]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};"]
    elif ins.name == "move_disp_copy":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        out += [f"    std::uint32_t disp_copy_value_{ins.pc:x} = 0;",
                f"    if (!read_field(bus, {src} + {ins.immediate}, {s}.st, {ins.field}u, disp_copy_value_{ins.pc:x}))",
                "        return BlockExit::BusFault;",
                f"    if (!write_field(bus, {dst} + {ins.target}, disp_copy_value_{ins.pc:x}, {s}.st, {ins.field}u))",
                "        return BlockExit::BusFault;", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += 5u;"]
    elif ins.name == "move_cross":
        destination_bank = "B" if ins.condition else "A"
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{destination_bank}, {ins.dst})"
        out += [f"    {dst} = {src};", f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                f"    set_nz({s}, {dst});", f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    ++{s}.cycles;"]
    elif ins.name == "call_indirect":
        out += [f"    {s}.sp -= 32u;",
                f"    if (!write_long(bus, {s}.sp, {c_u32(ins.next_pc)})) return BlockExit::BusFault;",
                f"    {s}.pc = reg({s}, Bank::{ins.bank}, {ins.dst}) & 0xfffffff0u;",
                f"    {s}.cycles += 3u;", "    return BlockExit::Dispatch;"]
    elif ins.name in ("sext", "zext"):
        width = f"field_width_{ins.pc:x}"
        out += [f"    unsigned {width} = ({s}.st >> {6 if ins.field else 0}) & 0x1fu;",
                f"    if (!{width}) {width} = 32u;"]
        if ins.name == "sext":
            out += [f"    if ({width} < 32u) {{ const std::uint32_t field_mask_{ins.pc:x} = (1u << {width}) - 1u;",
                    f"        {reg} &= field_mask_{ins.pc:x};",
                    f"        if ({reg} & (1u << ({width} - 1u))) {reg} |= ~field_mask_{ins.pc:x};", "    }"]
        else:
            out += [f"    if ({width} < 32u) {reg} &= (1u << {width}) - 1u;"]
        out += [f"    {s}.st &= ~(flag_n | flag_z);", f"    set_nz({s}, {reg});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += 3u;"]
    elif ins.name == "movx":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    {reg} = ({reg} & 0xffff0000u) | ({src} & 0xffffu);",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "movy":
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        out += [f"    {reg} = ({src} & 0xffff0000u) | ({reg} & 0xffffu);",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("sla_k", "sll_k", "sra_k", "srl_k"):
        if ins.name in ("sla_k", "sll_k"):
            out += [f"    std::uint32_t shift_value_{ins.pc:x} = {reg};"]
            if ins.name == "sla_k" and ins.immediate == 0:
                out += [f"    {s}.st &= ~(flag_n | flag_c | flag_z | flag_v);",
                        f"    set_nz({s}, shift_value_{ins.pc:x});"]
            elif ins.name == "sll_k" and ins.immediate == 0:
                out += [f"    {s}.st &= ~(flag_c | flag_z);",
                        f"    set_logic_z({s}, shift_value_{ins.pc:x});"]
            elif ins.name == "sla_k":
                out += [f"    {s}.st &= ~(flag_n | flag_c | flag_z | flag_v);",
                        f"    if ({ins.immediate}u) {{",
                        f"        const std::uint32_t mask_{ins.pc:x} = (0xffffffffu << (31u - {ins.immediate}u)) & 0x7fffffffu;",
                        f"        const std::uint32_t sign_adjusted_{ins.pc:x} = (shift_value_{ins.pc:x} & flag_n) ? (shift_value_{ins.pc:x} ^ mask_{ins.pc:x}) : shift_value_{ins.pc:x};",
                        f"        if (sign_adjusted_{ins.pc:x} & mask_{ins.pc:x}) {s}.st |= flag_v;",
                        f"        shift_value_{ins.pc:x} <<= ({ins.immediate}u - 1u);",
                        f"        if (shift_value_{ins.pc:x} & flag_n) {s}.st |= flag_c;",
                        f"        shift_value_{ins.pc:x} <<= 1u; {reg} = shift_value_{ins.pc:x};", "    }",
                        f"    set_nz({s}, shift_value_{ins.pc:x});"]
            else:
                out += [f"    {s}.st &= ~(flag_c | flag_z);",
                        f"    if ({ins.immediate}u) {{",
                        f"        shift_value_{ins.pc:x} <<= ({ins.immediate}u - 1u);",
                        f"        if (shift_value_{ins.pc:x} & flag_n) {s}.st |= flag_c;",
                        f"        shift_value_{ins.pc:x} <<= 1u; {reg} = shift_value_{ins.pc:x};", "    }",
                        f"    set_logic_z({s}, shift_value_{ins.pc:x});"]
        else:
            shift = (-ins.immediate) & 0x1F
            op = ">>" if ins.name == "srl_k" else ">>"
            out += [f"    std::uint32_t shift_value_{ins.pc:x} = {reg};",
                    (f"    {s}.st &= ~(flag_n | flag_c | flag_z);"
                     if ins.name == "sra_k" else f"    {s}.st &= ~(flag_c | flag_z);"),
                    f"    if ({shift}u) {{",
                    (f"        shift_value_{ins.pc:x} = static_cast<std::uint32_t>(static_cast<std::int32_t>(shift_value_{ins.pc:x}) {op} ({shift}u - 1u));"
                     if ins.name == "sra_k" else
                     f"        shift_value_{ins.pc:x} {op}= ({shift}u - 1u);"),
                    f"        if (shift_value_{ins.pc:x} & 1u) {s}.st |= flag_c;",
                    (f"        shift_value_{ins.pc:x} = static_cast<std::uint32_t>(static_cast<std::int32_t>(shift_value_{ins.pc:x}) >> 1u);"
                     if ins.name == "sra_k" else f"        shift_value_{ins.pc:x} >>= 1u;"),
                    f"        {reg} = shift_value_{ins.pc:x};", "    }",
                    (f"    set_nz({s}, shift_value_{ins.pc:x});"
                     if ins.name == "sra_k" else
                     f"    set_logic_z({s}, shift_value_{ins.pc:x});")]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {3 if ins.name == 'sla_k' else 1}u;"]
    elif ins.name == "rl_k":
        out += [f"    std::uint32_t rotate_value_{ins.pc:x} = {reg};",
                f"    {s}.st &= ~(flag_c | flag_z);",
                *([f"    rotate_value_{ins.pc:x} <<= ({ins.immediate}u - 1u);",
                   f"    if (rotate_value_{ins.pc:x} & flag_n) {s}.st |= flag_c;",
                   f"    rotate_value_{ins.pc:x} <<= 1u;",
                   f"    rotate_value_{ins.pc:x} |= {reg} >> ((32u - {ins.immediate}u) & 0x1fu);",
                   f"    {reg} = rotate_value_{ins.pc:x};"] if ins.immediate else []),
                f"    set_logic_z({s}, rotate_value_{ins.pc:x});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name in ("sla_r", "sll_r", "sra_r", "srl_r"):
        count = f"(reg({s}, Bank::{ins.bank}, {ins.src}) & 0x1fu)"
        value = f"shift_reg_value_{ins.pc:x}"
        if ins.name in ("sla_r", "sll_r"):
            out += [f"    std::uint32_t {value} = {reg};"]
            if ins.name == "sla_r":
                out += [f"    {s}.st &= ~(flag_n | flag_c | flag_z | flag_v);",
                        f"    if ({count}) {{ const unsigned shift_count_{ins.pc:x} = {count};",
                        f"        const std::uint32_t shift_mask_{ins.pc:x} = (0xffffffffu << (31u - shift_count_{ins.pc:x})) & 0x7fffffffu;",
                        f"        const std::uint32_t sign_adjusted_{ins.pc:x} = ({value} & flag_n) ? ({value} ^ shift_mask_{ins.pc:x}) : {value};",
                        f"        if (sign_adjusted_{ins.pc:x} & shift_mask_{ins.pc:x}) {s}.st |= flag_v;",
                        f"        {value} <<= (shift_count_{ins.pc:x} - 1u); if ({value} & flag_n) {s}.st |= flag_c;",
                        f"        {value} <<= 1u; {reg} = {value}; }}", f"    set_nz({s}, {value});"]
            else:
                out += [f"    {s}.st &= ~(flag_c | flag_z);",
                        f"    if ({count}) {{ {value} <<= ({count} - 1u); if ({value} & flag_n) {s}.st |= flag_c; {value} <<= 1u; {reg} = {value}; }}",
                        f"    set_logic_z({s}, {value});"]
        else:
            shifted = f"((32u - {count}) & 0x1fu)"
            out += [f"    std::uint32_t {value} = {reg};",
                    (f"    {s}.st &= ~(flag_n | flag_c | flag_z);"
                     if ins.name == "sra_r" else f"    {s}.st &= ~(flag_c | flag_z);"),
                    f"    if ({shifted}) {{",
                    (f"        {value} = static_cast<std::uint32_t>(static_cast<std::int32_t>({value}) >> ({shifted} - 1u));"
                     if ins.name == "sra_r" else f"        {value} >>= ({shifted} - 1u);"),
                    f"        if ({value} & 1u) {s}.st |= flag_c;",
                    (f"        {value} = static_cast<std::uint32_t>(static_cast<std::int32_t>({value}) >> 1u);"
                     if ins.name == "sra_r" else f"        {value} >>= 1u;"),
                    f"        {reg} = {value};", "    }",
                    (f"    set_nz({s}, {value});" if ins.name == "sra_r"
                     else f"    set_logic_z({s}, {value});")]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    ++{s}.cycles;"]
    elif ins.name == "rl_r":
        count = f"(reg({s}, Bank::{ins.bank}, {ins.src}) & 0x1fu)"
        value = f"rotate_reg_value_{ins.pc:x}"
        out += [f"    std::uint32_t {value} = {reg};", f"    {s}.st &= ~(flag_c | flag_z);",
                f"    if ({count}) {{ const unsigned rotate_count_{ins.pc:x} = {count};",
                f"        {value} <<= (rotate_count_{ins.pc:x} - 1u);",
                f"        if ({value} & flag_n) {s}.st |= flag_c;",
                f"        {value} <<= 1u; {value} |= {reg} >> ((32u - rotate_count_{ins.pc:x}) & 0x1fu);",
                f"        {reg} = {value};", "    }", f"    set_logic_z({s}, {value});",
                f"    {s}.pc = {c_u32(ins.next_pc)};", "    ++s.cycles;"]
    elif ins.name in ("mmtm", "mmfm"):
        bank = f"Bank::{ins.bank}"
        selected = f"selected_{ins.pc:x}"
        out += [f"    unsigned {selected} = 0;",
                (f"    for (int i_{ins.pc:x} = 0; i_{ins.pc:x} < 16; ++i_{ins.pc:x}) {{"
                 if ins.name == "mmtm" else
                 f"    for (int i_{ins.pc:x} = 15; i_{ins.pc:x} >= 0; --i_{ins.pc:x}) {{"),
                (f"        if ({ins.immediate}u & (0x8000u >> i_{ins.pc:x})) {{"
                 if ins.name == "mmtm" else
                 f"        if ({ins.immediate}u & (1u << i_{ins.pc:x})) {{"),
                f"            ++{selected};"]
        if ins.name == "mmtm":
            out += [f"            {reg} -= 32u;",
                    f"            if (!write_long(bus, {reg}, reg({s}, {bank}, i_{ins.pc:x})))",
                    "                return BlockExit::BusFault;"]
        else:
            out += [f"            std::uint32_t value_{ins.pc:x} = 0;",
                    f"            if (!read_long(bus, {reg}, value_{ins.pc:x}))",
                    "                return BlockExit::BusFault;",
                    f"            reg({s}, {bank}, i_{ins.pc:x}) = value_{ins.pc:x};",
                    f"            {reg} += 32u;"]
        out += ["        }", "    }",
                f"    {s}.pc = {c_u32(ins.next_pc)};",
                f"    {s}.cycles += {2 if ins.name == 'mmtm' else 3}u + 4u * {selected};"]
    elif ins.name.startswith("move_") and ins.name in (
        "move_store", "move_store_post", "move_store_pre",
        "move_load", "move_load_post", "move_load_pre",
        "move_copy", "move_copy_post", "move_copy_pre",
    ):
        src = f"reg({s}, Bank::{ins.bank}, {ins.src})"
        dst = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        inc = f"field_increment({s}.st, {ins.field}u)"
        addr = f"memory_address_{ins.pc:x}"
        value = f"memory_value_{ins.pc:x}"
        load = ins.name.startswith("move_load")
        copy = ins.name.startswith("move_copy")
        pre = ins.name.endswith("_pre")
        post = ins.name.endswith("_post")
        if pre and (load or copy):
            out += [f"    {src} -= {inc};"]
        if pre and (not load):
            out += [f"    {dst} -= {inc};"]
        if copy or load:
            out += [f"    std::uint32_t {value} = 0;",
                    f"    if (!read_field(bus, {src}, {s}.st, {ins.field}u, {value}))",
                    "        return BlockExit::BusFault;"]
        if post and load:
            out += [f"    {src} += {inc};"]
        if post and copy:
            out += [f"    {src} += {inc};"]
        if load:
            out += [f"    {s}.st &= ~(flag_n | flag_z | flag_v);",
                    f"    {dst} = {value};", f"    set_nz({s}, {dst});"]
        else:
            out += [f"    const std::uint32_t {addr} = {dst};"]
            if not copy:
                out += [f"    const std::uint32_t {value} = {src};"]
            out += [f"    if (!write_field(bus, {addr}, {value}, {s}.st, {ins.field}u))",
                    "        return BlockExit::BusFault;"]
        if post and not load:
            out += [f"    {dst} += {inc};"]
        cycles = {"move_store": 1, "move_store_post": 1, "move_store_pre": 2,
                  "move_load": 3, "move_load_post": 3, "move_load_pre": 4,
                  "move_copy": 3, "move_copy_post": 4, "move_copy_pre": 4}[ins.name]
        out += [f"    {s}.pc = {c_u32(ins.next_pc)};", f"    {s}.cycles += {cycles}u;"]
    elif ins.name in ("callr", "calla"):
        return_address = ins.fallthrough
        out += [f"    {s}.sp -= 32u;",
                f"    if (!write_long(bus, {s}.sp, {c_u32(return_address)}))",
                "        return BlockExit::BusFault;",
                f"    {s}.pc = {c_u32(ins.target)};",
                f"    {s}.cycles += {3 if ins.name == 'callr' else 4}u;",
                "    return BlockExit::Dispatch;"]
    elif ins.name == "rets":
        out += [f"    std::uint32_t return_{ins.pc:x} = 0;",
                f"    if (!read_long(bus, {s}.sp, return_{ins.pc:x}))",
                "        return BlockExit::BusFault;",
                f"    {s}.sp += 32u + ({ins.immediate}u << 4);",
                f"    {s}.pc = return_{ins.pc:x} & 0xfffffff0u;",
                f"    {s}.cycles += 7u;", "    return BlockExit::Dispatch;"]
    elif ins.name == "dsj":
        counter = f"reg({s}, Bank::{ins.bank}, {ins.dst})"
        condition_test = ("true" if ins.condition == 0 else
                          f"({s}.st & flag_z)" if ins.condition == 1 else
                          f"!({s}.st & flag_z)")
        out += [f"    if ({condition_test}) {{", f"        --{counter};",
                f"        if ({counter} != 0) {{ {s}.pc = {c_u32(ins.target)}; {s}.cycles += 3u; }}",
                f"        else {{ {s}.pc = {c_u32(ins.fallthrough)}; {s}.cycles += 2u; }}",
                "    } else {", f"        {s}.pc = {c_u32(ins.fallthrough)}; {s}.cycles += 2u;", "    }",
                "    return BlockExit::Dispatch;"]
    elif ins.name == "dsjs":
        out += [f"    std::uint32_t& counter_{ins.pc:x} = {reg};",
                f"    counter_{ins.pc:x} -= 1u;",
                f"    if (counter_{ins.pc:x} != 0) {{",
                f"        {s}.pc = {c_u32(ins.target)};",
                f"        {s}.cycles += {ins.branch_cycles_taken}u;",
                "    } else {",
                f"        {s}.pc = {c_u32(ins.fallthrough)};",
                f"        {s}.cycles += {ins.branch_cycles_not_taken}u;",
                "    }",
                "    return BlockExit::Dispatch;"]
    elif ins.name in ("jr", "ja", "jrs"):
        out += [f"    if (condition({s}.st, {ins.condition}u)) {{",
                f"        {s}.pc = {c_u32(ins.target)};",
                f"        {s}.cycles += {ins.branch_cycles_taken}u;",
                "    } else {",
                f"        {s}.pc = {c_u32(ins.fallthrough)};",
                f"        {s}.cycles += {ins.branch_cycles_not_taken}u;",
                "    }",
                "    return BlockExit::Dispatch;"]
    return out


def compile_reachable(rom: Rom, entries: int | list[int], block_limit: int,
                      instruction_limit: int) -> tuple[dict[int, BlockInfo], Counter, list[tuple[int, int]]]:
    if isinstance(entries, int):
        entries = [entries]
    roots = list(dict.fromkeys(pc & 0xFFFFFFF0 for pc in entries))
    pending = deque(roots)
    queued = set(pending)
    blocks: dict[int, BlockInfo] = {}
    counts: Counter = Counter()
    unmapped: list[tuple[int, int]] = []
    total = 0

    while pending and total < instruction_limit:
        start = pending.popleft()
        if start in blocks:
            continue
        block = BlockInfo(start, [])
        blocks[start] = block
        pc = start
        constants: dict[tuple[str, int], int] = {}
        for _ in range(block_limit):
            ins = decode(rom, pc)
            if ins is None:
                if not block.instructions:
                    block.unsupported_opcode = rom.word(pc)
                elif pc not in queued:
                    queued.add(pc)
                    pending.append(pc)
                break
            block.instructions.append(ins)
            counts[ins.name] += 1
            total += 1
            indirect_target = None
            if ins.name in ("exgpc", "jump", "call_indirect"):
                key = ("SP" if ins.dst == 15 else ins.bank, ins.dst)
                indirect_target = constants.get(key)

            if ins.name in ("jr", "ja", "jrs", "dsjs", "dsj", "callr", "calla", "call_indirect", "rets", "jump", "exgpc", "illop"):
                if ins.name == "illop":
                    try:
                        successors = (rom.long(0xFFFFFC20) & MASK32,)
                    except ValueError:
                        successors = ()
                elif ins.name in ("exgpc", "jump"):
                    successors = (() if indirect_target is None else
                                  (indirect_target & 0xFFFFFFF0,))
                elif ins.name == "call_indirect":
                    successors = ((ins.next_pc,) if indirect_target is None else
                                  (ins.next_pc, indirect_target & 0xFFFFFFF0))
                else:
                    successors = (() if ins.name == "rets" else
                                  (ins.target, ins.fallthrough))
                if ins.name in ("exgpc", "jump", "call_indirect") and indirect_target is None:
                    block.dynamic_exits.append((ins.pc, ins.name))
                for target in successors:
                    if target not in queued:
                        try:
                            rom.word(target)
                        except Exception:
                            unmapped.append((pc, target))
                            continue
                        queued.add(target)
                        pending.append(target)
                break

            # Resolve only straight-line literal-to-register chains. Any
            # other instruction invalidates the local facts; this conservative
            # pass never carries guessed register state across block edges.
            if ins.name in ("movi_w", "movi_l", "movk"):
                constants.clear()
                key = ("SP" if ins.dst == 15 else ins.bank, ins.dst)
                constants[key] = ins.immediate & MASK32
            elif ins.name == "getpc":
                constants.clear()
                key = ("SP" if ins.dst == 15 else ins.bank, ins.dst)
                constants[key] = ins.next_pc
            elif ins.name == "move":
                value = constants.get(("SP" if ins.src == 15 else ins.bank, ins.src))
                constants.clear()
                if value is not None:
                    key = ("SP" if ins.dst == 15 else ins.bank, ins.dst)
                    constants[key] = value
            elif ins.name not in ("nop", "reserved"):
                constants.clear()
            pc = ins.next_pc
        else:
            if pc not in queued:
                queued.add(pc)
                pending.append(pc)
        if not block.instructions and block.unsupported_opcode is None:
            block.unsupported_opcode = rom.word(start)

    return blocks, counts, unmapped


def emit_source(blocks: dict[int, BlockInfo], entry: int) -> str:
    lines = [
        "// Generated by tools/tms34010/recomp.py; do not edit.",
        '#include "core/cpus/tms34010/tms34010.hpp"',
        "#include <cstdint>",
        "namespace multiarcaderecomp::tms34010 {",
    ]
    instruction_entries: dict[int, Instruction] = {}
    interpreter_entries: set[int] = set()
    for pc, block in sorted(blocks.items()):
        if block.unsupported_opcode is not None and not block.instructions:
            interpreter_entries.add(pc)
        for ins in block.instructions:
            instruction_entries[ins.pc] = ins

    # Keep a native entry for every translated instruction, not just each
    # discovered basic-block head. TMS code uses register-computed PC changes;
    # this lets those transfers rejoin any instruction already discovered by
    # static analysis. It also makes runtime-captured targets easy to seed as
    # additional roots without having to recover their containing block.
    for pc, ins in sorted(instruction_entries.items()):
        lines += [f"static BlockExit ins_{pc:08x}(State& s, Bus& bus, std::uint64_t stop_cycle) {{",
                  f"    if (s.pc != {c_u32(pc)}) return BlockExit::Dispatch;",
                  "    (void)bus;"]
        lines.extend(emit_instruction(ins))
        lines += ["    return BlockExit::Dispatch;", "}"]
    for pc in sorted(interpreter_entries):
        lines += [f"static BlockExit ins_{pc:08x}(State& s, Bus&, std::uint64_t) {{",
                  f"    if (s.pc != {c_u32(pc)}) return BlockExit::Dispatch;",
                  "    return BlockExit::NeedsInterpreter;", "}"]
    lines += [
        "Block lookup(std::uint32_t pc) noexcept {",
        "    switch (pc & 0xfffffff0u) {",
    ]
    for pc in sorted(instruction_entries.keys() | interpreter_entries):
        lines.append(f"    case {c_u32(pc)}: return &ins_{pc:08x};")
    lines += ["    default: return nullptr;", "    }", "}",
              f"std::uint32_t entry_pc() noexcept {{ return {c_u32(entry)}; }}",
              "} // namespace multiarcaderecomp::tms34010", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path,
                        help="assembled byte-addressed maincpu ROM region")
    parser.add_argument("--base-byte-address", type=lambda x: int(x, 0),
                        default=0x1F800000,
                        help="CPU byte address where the region is mapped (default: MK mirror)")
    parser.add_argument("--map-size-bytes", type=lambda x: int(x, 0),
                        default=0x800000,
                        help="board ROM window size in bytes (default: MK mirror window)")
    parser.add_argument("--entry-bits", type=lambda x: int(x, 0),
                        help="entry PC in TMS bit addresses; default reads reset vector at FFFFFFE0h")
    parser.add_argument("--extra-entry-bits", action="append", type=lambda x: int(x, 0),
                        default=[], metavar="PC",
                        help="additional statically observed entry PC (repeatable)")
    parser.add_argument("--entry-list", type=Path,
                        help="text file of additional bit-address entry PCs (hex; # comments allowed)")
    parser.add_argument("--output", required=True, type=Path,
                        help="generated C++ source path")
    parser.add_argument("--max-block-length", type=int, default=128)
    parser.add_argument("--max-instructions", type=int, default=250000)
    args = parser.parse_args()

    try:
        rom = Rom(args.rom.read_bytes(), args.base_byte_address, args.map_size_bytes)
        entry = args.entry_bits
        if entry is None:
            entry = rom.long(0xFFFFFFE0) & MASK32
            entry &= 0xFFFFFFF0
        extra_entries = list(args.extra_entry_bits)
        if args.entry_list:
            for line_number, line in enumerate(args.entry_list.read_text(encoding="utf-8").splitlines(), 1):
                value = line.split("#", 1)[0].strip()
                if not value:
                    continue
                try:
                    extra_entries.append(int(value, 0))
                except ValueError as exc:
                    raise ValueError(f"invalid entry on {args.entry_list}:{line_number}: {value!r}") from exc
        roots = list(dict.fromkeys([entry, *extra_entries]))
        blocks, counts, unmapped = compile_reachable(
            rom, roots, args.max_block_length, args.max_instructions)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(emit_source(blocks, entry), encoding="utf-8")
    except (OSError, ValueError) as exc:
        print(f"tms34010-recomp: {exc}", file=sys.stderr)
        return 2

    translated = sum(len(block.instructions) for block in blocks.values())
    unsupported = [(pc, b.unsupported_opcode) for pc, b in sorted(blocks.items())
                   if b.unsupported_opcode is not None and not b.instructions]
    dynamic_exits = sorted((pc, name) for block in blocks.values()
                           for pc, name in block.dynamic_exits)
    print(f"entry PC(bit)={entry:#010x}; roots={len(roots)}; AOT blocks={len(blocks)}; "
          f"translated instructions={translated}; opcode forms={len(counts)}")
    if counts:
        print("forms: " + ", ".join(f"{name}={count}" for name, count in sorted(counts.items())))
    print(f"first unsupported blocks={len(unsupported)}")
    for pc, op in unsupported[:20]:
        print(f"  PC={pc:#010x} opcode={op:#06x}")
    if len(unsupported) > 20:
        print(f"  ... {len(unsupported) - 20} more")
    print(f"unresolved computed-PC exits={len(dynamic_exits)}")
    for pc, name in dynamic_exits[:20]:
        print(f"  PC={pc:#010x} instruction={name}")
    if len(dynamic_exits) > 20:
        print(f"  ... {len(dynamic_exits) - 20} more")
    if unmapped:
        print(f"branch targets outside mapped ROM={len(unmapped)}")
    print(f"generated: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
