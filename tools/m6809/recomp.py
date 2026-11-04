#!/usr/bin/env python3
"""First MC6809(E) AOT pass for the Williams ADPCM sound CPU.

The ROM view models MAME's eight 0x8000-byte banks at $4000-$bfff and the
fixed ROM at $c000-$ffff. This first pass emits native blocks for the boot
and control-flow subset listed in SUPPORTED; every other reachable opcode is
left as an explicit interpreter entry for the forthcoming MAME fallback.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path
import sys

ROM_BANK_BASE = 0x10000
ROM_BANK_SIZE = 0x8000
ROM_FIXED_BASE = 0x4c000
ROM_IMAGE_SIZE = 0x50000


class SoundRom:
    def __init__(self, image: bytes):
        if len(image) < ROM_IMAGE_SIZE:
            raise ValueError(f"sound ROM region must be at least {ROM_IMAGE_SIZE:#x} bytes")
        self.image = image

    def byte(self, address: int, bank: int) -> int:
        address &= 0xffff
        if 0x4000 <= address < 0xc000:
            offset = ROM_BANK_BASE + bank * ROM_BANK_SIZE + address - 0x4000
        elif address >= 0xc000:
            offset = ROM_FIXED_BASE + address - 0xc000
        else:
            raise ValueError(f"address {address:#06x} is not in the sound ROM map")
        return self.image[offset]

    def word(self, address: int, bank: int) -> int:
        return (self.byte(address, bank) << 8) | self.byte(address + 1, bank)


@dataclass(frozen=True)
class Instruction:
    pc: int
    size: int
    opcode: str
    operands: tuple[int, ...] = ()
    target: int | None = None
    flow: str = "next"


def decode(rom: SoundRom, pc: int, bank: int) -> Instruction | None:
    op = rom.byte(pc, bank)
    b = lambda offset: rom.byte((pc + offset) & 0xffff, bank)
    w = lambda offset: (b(offset) << 8) | b(offset + 1)

    if op == 0x12:
        return Instruction(pc, 1, "nop")
    if op in (0x1a, 0x1c):
        return Instruction(pc, 2, "orcc" if op == 0x1a else "andcc", (b(1),))
    if op == 0x1f:
        return Instruction(pc, 2, "tfr", (b(1),))
    if op == 0x39:
        return Instruction(pc, 1, "rts", flow="return")
    if op == 0x3b:
        return Instruction(pc, 1, "rti", flow="return")
    if op == 0x3b:
        return Instruction(pc, 1, "rti", flow="return")
    if op in (0x34, 0x35, 0x36, 0x37):
        mask = b(1)
        name = {0x34: "pshs", 0x35: "puls", 0x36: "pshu", 0x37: "pulu"}[op]
        return Instruction(pc, 2, name, (mask,),
                           flow="jump" if name in ("puls", "pulu") and mask & 0x80 else "next")
    if op == 0x16:
        displacement = w(1)
        target = (pc + 3 + (displacement - 0x10000 if displacement & 0x8000 else displacement)) & 0xffff
        return Instruction(pc, 3, "lbra", target=target, flow="branch")
    if 0x20 <= op <= 0x2f:
        displacement = b(1)
        target = (pc + 2 + (displacement - 0x100 if displacement & 0x80 else displacement)) & 0xffff
        return Instruction(pc, 2, "branch", (op & 0x0f,), target, "cond")
    if op == 0x8d:
        displacement = b(1)
        target = (pc + 2 + (displacement - 0x100 if displacement & 0x80 else displacement)) & 0xffff
        return Instruction(pc, 2, "bsr", target=target, flow="call")
    if op in (0x86, 0x89, 0x85, 0x84, 0x8a, 0xc5, 0xc6):
        name = {0x86: "lda_imm", 0x89: "adca_imm", 0x85: "bita_imm",
                0x84: "anda_imm",
                0x8a: "ora_a_imm", 0xc5: "bitb_imm", 0xc6: "ldb_imm"}[op]
        return Instruction(pc, 2, name, (b(1),))
    if op == 0x3d:
        return Instruction(pc, 1, "mul")
    if op == 0xcc:
        return Instruction(pc, 3, "ldd_imm", (w(1),))
    if op in (0x83, 0x8c, 0xc3):
        name = {0x83: "subd_imm", 0x8c: "cmpx_imm", 0xc3: "addd_imm"}[op]
        return Instruction(pc, 3, name, (w(1),))
    if op in (0x8e, 0xce):
        return Instruction(pc, 3, "ldx_imm" if op == 0x8e else "ldu_imm", (w(1),))
    if op in (0x30, 0x31, 0x32, 0x33):
        postbyte = b(1)
        mode = postbyte & 0x1f
        if not (postbyte & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        return Instruction(pc, 2 + extra, "lea_indexed",
                           (op, postbyte, extension))
    if 0x40 <= op <= 0x5f and (op & 0x0f) in {
            0x0, 0x1, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
            0x9, 0xa, 0xc, 0xd, 0xf}:
        return Instruction(pc, 1, "acc_unary", (op,))
    if 0x60 <= op <= 0x6f and (op & 0x0f) in {
            0x0, 0x1, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
            0x9, 0xa, 0xc, 0xd, 0xf}:
        post = b(1)
        mode = post & 0x1f
        if not (post & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        return Instruction(pc, 2 + extra, "indexed_unary",
                           (op, post, extension))
    if op == 0x10 and b(1) == 0x83:
        return Instruction(pc, 4, "cmpd_imm", (w(2),))
    if op == 0x10 and b(1) == 0x93:
        return Instruction(pc, 3, "cmpd_dir", (b(2),))
    if op == 0x10 and b(1) in (0x8e, 0xce):
        sub = b(1)
        return Instruction(pc, 4, "ldy_imm" if sub == 0x8e else "lds_imm", (w(2),))
    if op in (0x93, 0x96, 0x97, 0x9a, 0xd6, 0xd7):
        return Instruction(pc, 2, {0x96: "lda_dir", 0x97: "sta_dir",
                                   0x9a: "ora_a_dir", 0xd6: "ldb_dir",
                                   0xd7: "stb_dir", 0x93: "subd_dir"}[op], (b(1),))
    if op in (0x9e, 0xbe, 0xde, 0xfe):
        name = {0x9e: "ldx_dir", 0xbe: "ldx_ext",
                0xde: "ldu_dir", 0xfe: "ldu_ext"}[op]
        return Instruction(pc, 2 if op in (0x9e, 0xde) else 3, name,
                           (b(1),) if op in (0x9e, 0xde) else (w(1),))
    if op in (0xdc, 0xfc):
        return Instruction(pc, 2 if op == 0xdc else 3,
                           "ldd_dir" if op == 0xdc else "ldd_ext",
                           (b(1),) if op == 0xdc else (w(1),))
    if op == 0x9c:
        return Instruction(pc, 2, "cmpx_dir", (b(1),))
    if op == 0xbc:
        return Instruction(pc, 3, "cmpx_ext", (w(1),))
    if op == 0xf1:
        return Instruction(pc, 3, "cmpb_ext", (w(1),))
    if op <= 0x0f and op != 0x0e and (op & 0x0f) in {
            0x0, 0x1, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
            0x9, 0xa, 0xc, 0xd, 0xf}:
        return Instruction(pc, 2, "direct_unary", (op, b(1)))
    if op == 0x9f:
        return Instruction(pc, 2, "stx_dir", (b(1),))
    if op == 0xbf:
        return Instruction(pc, 3, "stx_ext", (w(1),))
    if op == 0xdd:
        return Instruction(pc, 2, "std_dir", (b(1),))
    if op == 0xfd:
        return Instruction(pc, 3, "std_ext", (w(1),))
    if op in (0xb6, 0xb7, 0xf6, 0xf7):
        return Instruction(pc, 3, {0xb6: "lda_ext", 0xb7: "sta_ext", 0xf6: "ldb_ext", 0xf7: "stb_ext"}[op], (w(1),))
    if op in (0xad, 0xaf, 0xed, 0xef):
        post = b(1)
        mode = post & 0x1f
        if not (post & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        kind = "jsr_indexed" if op == 0xad else "indexed_store16"
        return Instruction(pc, 2 + extra, kind, (op, post, extension))
    if op in (0xa6, 0xa7, 0xe6, 0xe7):
        post = b(1)
        mode = post & 0x1f
        if not (post & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        size = 2 + extra
        return Instruction(pc, size, "indexed_load_store",
                           (op, post, extension))
    if op in (0xae, 0xee, 0xec):
        post = b(1)
        mode = post & 0x1f
        if not (post & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        return Instruction(pc, 2 + extra, "indexed_load16",
                           (op, post, extension))
    if op in (0xaa, 0xe4):
        post = b(1)
        mode = post & 0x1f
        if not (post & 0x80):
            extra = 0
        elif mode in (0x08, 0x0c, 0x18, 0x1c):
            extra = 1
        elif mode in (0x09, 0x0d, 0x0f, 0x19, 0x1d, 0x1f):
            extra = 2
        elif mode in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x0b, 0x11, 0x13, 0x14, 0x15, 0x16, 0x1b):
            extra = 0
        else:
            return None
        extension = b(2) if extra == 1 else w(2) if extra == 2 else 0
        return Instruction(pc, 2 + extra, "indexed_logic",
                           (op, post, extension))
    if op == 0x7e:
        address = w(1)
        return Instruction(pc, 3, "jmp", (address,), address, "jump")
    if op == 0xbd:
        address = w(1)
        return Instruction(pc, 3, "jsr", (address,), address, "call")
    return None


def translate(rom: SoundRom, entries: list[int], bank: int) -> dict[int, Instruction]:
    found: dict[int, Instruction] = {}
    pending = deque(entries)
    while pending:
        pc = pending.popleft() & 0xffff
        while pc not in found and pc >= 0x4000:
            try:
                ins = decode(rom, pc, bank)
            except (IndexError, ValueError):
                break
            if ins is None:
                break
            found[pc] = ins
            following = (pc + ins.size) & 0xffff
            if ins.flow in ("branch", "cond", "call") and ins.target is not None:
                pending.append(ins.target)
            if ins.flow == "jump" or ins.flow == "return":
                break
            if ins.flow == "call":
                pending.append(following)
                break
            if ins.flow == "cond":
                pending.append(following)
                break
            pc = following
    return found


def emit_instruction(ins: Instruction) -> list[str]:
    pc = ins.pc
    nxt = (pc + ins.size) & 0xffff
    op, args = ins.opcode, ins.operands
    code = [f"    s.pc = 0x{nxt:04x};"]
    if op == "nop":
        code += ["    ++s.cycles;"]
    elif op in ("orcc", "andcc"):
        code += [f"    s.cc {'|=' if op == 'orcc' else '&='} 0x{args[0]:02x};", "    s.cycles += 3;"]
    elif op == "lda_imm":
        code += [f"    s.a = 0x{args[0]:02x};", "    set_nz8(s, s.a);", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", "    s.cycles += 2;"]
    elif op == "ldb_imm":
        code += [f"    s.b = 0x{args[0]:02x};", "    set_nz8(s, s.b);", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", "    s.cycles += 2;"]
    elif op == "adca_imm":
        code += [f"    s.a = aot_add8(s, s.a, 0x{args[0]:02x}, s.cc & cc_c);", "    s.cycles += 2;"]
    elif op in ("bita_imm", "bitb_imm"):
        register = "a" if op == "bita_imm" else "b"
        code += [f"    set_nz8(s, static_cast<std::uint8_t>(s.{register} & 0x{args[0]:02x}));",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 "    s.cycles += 2;"]
    elif op == "anda_imm":
        code += [f"    s.a &= 0x{args[0]:02x};", "    set_nz8(s, s.a);",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 "    s.cycles += 2;"]
    elif op in ("ora_a_imm", "ora_a_dir"):
        if op.endswith("_imm"):
            rhs = f"0x{args[0]:02x}"
            cycles = 2
        else:
            rhs = f"bus.read8(bus.opaque, static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x}))"
            code += ["    if (!bus.read8) return BlockExit::BusFault;"]
            cycles = 4
        code += [f"    s.a |= {rhs};", "    set_nz8(s, s.a);",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 f"    s.cycles += {cycles};"]
    elif op == "ldd_imm":
        code += [f"    s.a = 0x{args[0] >> 8:02x}; s.b = 0x{args[0] & 0xff:02x};", "    set_nz16(s, static_cast<std::uint16_t>((s.a << 8) | s.b));", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", "    s.cycles += 3;"]
    elif op == "subd_imm":
        code += [f"    aot_set_d(s, aot_sub16(s, aot_d(s), 0x{args[0]:04x}));",
                 "    s.cycles += 3;"]
    elif op == "subd_dir":
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x});",
                 "    const auto high = bus.read8(bus.opaque, address);",
                 "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));",
                 "    aot_set_d(s, aot_sub16(s, aot_d(s), static_cast<std::uint16_t>((high << 8) | low)));",
                 "    s.cycles += 5;"]
    elif op == "addd_imm":
        code += [f"    aot_set_d(s, aot_add16(s, aot_d(s), 0x{args[0]:04x}));",
                 "    s.cycles += 3;"]
    elif op == "mul":
        code += ["    const auto product = static_cast<std::uint16_t>(s.a * s.b);",
                 "    aot_set_d(s, product);",
                 "    s.cc &= static_cast<std::uint8_t>(~(cc_z | cc_c));",
                 "    if (!product) s.cc |= cc_z;",
                 "    if (product & 0x0080) s.cc |= cc_c;",
                 "    s.cycles += 11;"]
    elif op == "cmpx_imm":
        code += [f"    (void)aot_sub16(s, s.x, 0x{args[0]:04x});",
                 "    s.cycles += 3;"]
    elif op in ("cmpd_imm", "cmpd_dir"):
        if op == "cmpd_imm":
            rhs = f"0x{args[0]:04x}"
            cycles = 5
        else:
            code += ["    if (!bus.read8) return BlockExit::BusFault;",
                     f"    const auto address = static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x});",
                     "    const auto high = bus.read8(bus.opaque, address);",
                     "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));"]
            rhs = "static_cast<std::uint16_t>((high << 8) | low)"
            cycles = 7
        code += [f"    (void)aot_sub16(s, aot_d(s), {rhs});",
                 f"    s.cycles += {cycles};"]
    elif op in ("ldx_imm", "ldy_imm", "ldu_imm", "lds_imm"):
        reg = {"ldx_imm": "x", "ldy_imm": "y", "ldu_imm": "u", "lds_imm": "s"}[op]
        cycles = 4 if op in ("ldy_imm", "lds_imm") else 3
        code += [f"    s.{reg} = 0x{args[0]:04x};", f"    set_nz16(s, s.{reg});", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", f"    s.cycles += {cycles};"]
    elif op == "lea5":
        operation, index_reg, displacement = args
        base = ("s.x", "s.y", "s.u", "s.s")[index_reg]
        destination = {0x30: "s.x", 0x31: "s.y", 0x32: "s.s", 0x33: "s.u"}[operation]
        code += [f"    {destination} = static_cast<std::uint16_t>({base} + ({displacement}));"]
        if operation in (0x30, 0x31):
            code += [f"    s.cc = static_cast<std::uint8_t>((s.cc & ~cc_z) | ({destination} == 0 ? cc_z : 0));"]
        code += ["    s.cycles += 4;"]
    elif op == "lea_indexed":
        operation, post, extension = args
        destination = {0x30: "s.x", 0x31: "s.y", 0x32: "s.s", 0x33: "s.u"}[operation]
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code += [f"    {destination} = {ea};"]
        if operation in (0x30, 0x31):
            code += [f"    s.cc = static_cast<std::uint8_t>((s.cc & ~cc_z) | ({destination} == 0 ? cc_z : 0));"]
        code += ["    s.cycles += 4;"]
    elif op == "acc_unary":
        opcode = args[0]
        register = "b" if opcode & 0x10 else "a"
        code += [f"    s.{register} = aot_unary(s, 0x{opcode & 0x0f:x}, s.{register});",
                 "    s.cycles += 2;"]
    elif op == "indexed_unary":
        opcode, post, extension = args
        operation = opcode & 0x0f
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto ea = {ea};",
                 "    const auto value = bus.read8(bus.opaque, ea);",
                 f"    const auto result = aot_unary(s, 0x{operation:x}, value);"]
        if operation != 0xd:
            code += ["    if (!bus.write8) return BlockExit::BusFault;",
                     "    bus.write8(bus.opaque, ea, result);"]
        code += ["    s.cycles += 6;"]
    elif op == "direct_unary":
        opcode, offset = args
        operation = opcode & 0x0f
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = static_cast<std::uint16_t>((s.dp << 8) | 0x{offset:02x});",
                 "    const auto value = bus.read8(bus.opaque, address);",
                 f"    const auto result = aot_unary(s, 0x{operation:x}, value);"]
        if operation != 0xd:
            code += ["    if (!bus.write8) return BlockExit::BusFault;",
                     "    bus.write8(bus.opaque, address, result);"]
        code += [f"    s.cycles += {4 if operation == 0xd else 6};"]
    elif op == "tfr":
        src, dst = args[0] >> 4, args[0] & 0x0f
        source = {
            0: "aot_d(s)", 1: "s.x", 2: "s.y", 3: "s.u", 4: "s.s",
            5: "s.pc", 8: "s.a", 9: "s.b", 10: "s.cc", 11: "s.dp",
        }.get(src)
        destination = {
            0: "d", 1: "x", 2: "y", 3: "u", 4: "s", 5: "pc",
            8: "a", 9: "b", 10: "cc", 11: "dp",
        }.get(dst)
        if source is None or destination is None:
            code = ["    return BlockExit::NeedsInterpreter;"]
        elif dst == 0:
            code += [f"    aot_set_d(s, static_cast<std::uint16_t>({source}));",
                     "    s.cycles += 6;"]
        elif dst < 8:
            code += [f"    s.{destination} = static_cast<std::uint16_t>({source});",
                     "    s.cycles += 6;"]
        else:
            code += [f"    s.{destination} = static_cast<std::uint8_t>({source});",
                     "    s.cycles += 6;"]
    elif op in ("pshs", "pshu", "puls", "pulu"):
        mask = args[0]
        stack = "s" if op.endswith("s") else "u"
        other = "u" if stack == "s" else "s"
        if op.startswith("ps"):
            if not mask:
                code += ["    s.cycles += 5;", "    return BlockExit::Dispatch;"]
            else:
                code = [f"    s.pc = 0x{nxt:04x};",
                        "    if (!bus.write8) return BlockExit::BusFault;"]
                for bit, register in ((0x80, "pc"), (0x40, other),
                                      (0x20, "y"), (0x10, "x")):
                    if mask & bit:
                        expr = f"0x{nxt:04x}" if register == "pc" else f"s.{register}"
                        code += [f"    --s.{stack}; bus.write8(bus.opaque, s.{stack}, static_cast<std::uint8_t>({expr}));",
                                 f"    --s.{stack}; bus.write8(bus.opaque, s.{stack}, static_cast<std::uint8_t>(({expr}) >> 8));"]
                for bit, expr in ((0x08, "s.dp"), (0x04, "s.b"),
                                  (0x02, "s.a"), (0x01, "s.cc")):
                    if mask & bit:
                        code += [f"    --s.{stack}; bus.write8(bus.opaque, s.{stack}, {expr});"]
                byte_count = sum(2 if mask & bit else 0 for bit in (0x80, 0x40, 0x20, 0x10)) + sum(1 if mask & bit else 0 for bit in (0x08, 0x04, 0x02, 0x01))
                code += [f"    s.cycles += {5 + byte_count};"]
        else:
            if not mask:
                code += ["    s.cycles += 5;", "    return BlockExit::Dispatch;"]
            else:
                code = [f"    s.pc = 0x{nxt:04x};",
                        "    if (!bus.read8) return BlockExit::BusFault;"]
                for bit, register in ((0x01, "cc"), (0x02, "a"), (0x04, "b"), (0x08, "dp")):
                    if mask & bit:
                        code += [f"    s.{register} = bus.read8(bus.opaque, s.{stack}++);"]
                for bit, register in ((0x10, "x"), (0x20, "y"), (0x40, other), (0x80, "pc")):
                    if mask & bit:
                        code += [f"    {{ const auto hi = bus.read8(bus.opaque, s.{stack}++);",
                                 f"      const auto lo = bus.read8(bus.opaque, s.{stack}++);",
                                 f"      s.{register} = static_cast<std::uint16_t>((hi << 8) | lo); }}"]
                byte_count = sum(2 if mask & bit else 0 for bit in (0x10, 0x20, 0x40, 0x80)) + sum(1 if mask & bit else 0 for bit in (0x01, 0x02, 0x04, 0x08))
                code += [f"    s.cycles += {5 + byte_count};"]
                if mask & 0x80:
                    code += ["    return BlockExit::Dispatch;"]
    elif op.startswith(("lda_dir", "ldb_dir", "lda_ext", "ldb_ext")):
        address = args[0] if op.endswith("_ext") else "static_cast<std::uint16_t>((s.dp << 8) | 0x%02x)" % args[0]
        target_reg = "a" if op.startswith("lda") else "b"
        code += [f"    if (!bus.read8) return BlockExit::BusFault;", f"    s.{target_reg} = bus.read8(bus.opaque, {address});", f"    set_nz8(s, s.{target_reg});", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", f"    s.cycles += {4 if op.endswith('_dir') else 5};"]
    elif op.startswith(("sta_dir", "stb_dir", "sta_ext", "stb_ext")):
        address = args[0] if op.endswith("_ext") else "static_cast<std::uint16_t>((s.dp << 8) | 0x%02x)" % args[0]
        source_reg = "a" if op.startswith("sta") else "b"
        code += ["    if (!bus.write8) return BlockExit::BusFault;", f"    bus.write8(bus.opaque, {address}, s.{source_reg});", f"    set_nz8(s, s.{source_reg});", "    s.cc &= static_cast<std::uint8_t>(~cc_v);", f"    s.cycles += {4 if op.endswith('_dir') else 5};"]
    elif op in ("ldx_dir", "ldx_ext", "ldu_dir", "ldu_ext"):
        address = (f"static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x})"
                   if op.endswith("_dir") else f"0x{args[0]:04x}")
        register = "x" if op.startswith("ldx") else "u"
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = {address};",
                 "    const auto high = bus.read8(bus.opaque, address);",
                 "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));",
                 f"    s.{register} = static_cast<std::uint16_t>((high << 8) | low);",
                 f"    set_nz16(s, s.{register});",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 f"    s.cycles += {5 if op.endswith('_dir') else 6};"]
    elif op in ("ldd_dir", "ldd_ext"):
        address = (f"static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x})"
                   if op.endswith("_dir") else f"0x{args[0]:04x}")
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = {address};",
                 "    const auto high = bus.read8(bus.opaque, address);",
                 "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));",
                 "    aot_set_d(s, static_cast<std::uint16_t>((high << 8) | low));",
                 "    set_nz16(s, aot_d(s));",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 f"    s.cycles += {5 if op.endswith('_dir') else 6};"]
    elif op in ("cmpx_dir", "cmpx_ext"):
        address = (f"static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x})"
                   if op.endswith("_dir") else f"0x{args[0]:04x}")
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = {address};",
                 "    const auto high = bus.read8(bus.opaque, address);",
                 "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));",
                 "    (void)aot_sub16(s, s.x, static_cast<std::uint16_t>((high << 8) | low));",
                 f"    s.cycles += {5 if op.endswith('_dir') else 6};"]
    elif op == "cmpb_ext":
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    (void)aot_sub8(s, s.b, bus.read8(bus.opaque, 0x{args[0]:04x}));",
                 "    s.cycles += 5;"]
    elif op in ("stx_dir", "stx_ext"):
        address = (f"static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x})"
                   if op.endswith("_dir") else f"0x{args[0]:04x}")
        code += ["    if (!bus.write8) return BlockExit::BusFault;",
                 f"    const auto address = {address};",
                 "    bus.write8(bus.opaque, address, static_cast<std::uint8_t>(s.x >> 8));",
                 "    bus.write8(bus.opaque, static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(s.x));",
                 "    set_nz16(s, s.x);",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 f"    s.cycles += {5 if op.endswith('_dir') else 6};"]
    elif op in ("std_dir", "std_ext"):
        address = (f"static_cast<std::uint16_t>((s.dp << 8) | 0x{args[0]:02x})"
                   if op.endswith("_dir") else f"0x{args[0]:04x}")
        code += ["    if (!bus.write8) return BlockExit::BusFault;",
                 f"    const auto address = {address};",
                 "    const auto value = aot_d(s);",
                 "    bus.write8(bus.opaque, address, static_cast<std::uint8_t>(value >> 8));",
                 "    bus.write8(bus.opaque, static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value));",
                 "    set_nz16(s, value);",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 f"    s.cycles += {5 if op.endswith('_dir') else 6};"]
    elif op == "indexed_load_store":
        opcode, post, extension = args
        target_reg = "a" if opcode in (0xa6, 0xa7) else "b"
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        if opcode in (0xa6, 0xe6):
            code += ["    if (!bus.read8) return BlockExit::BusFault;",
                     f"    s.{target_reg} = bus.read8(bus.opaque, {ea});",
                     f"    set_nz8(s, s.{target_reg});",
                     "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                     "    s.cycles += 4;"]
        else:
            code += ["    if (!bus.write8) return BlockExit::BusFault;",
                     f"    bus.write8(bus.opaque, {ea}, s.{target_reg});",
                     f"    set_nz8(s, s.{target_reg});",
                     "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                     "    s.cycles += 4;"]
    elif op == "indexed_load16":
        opcode, post, extension = args
        register = {0xae: "x", 0xee: "u", 0xec: "d"}[opcode]
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    const auto address = {ea};",
                 "    const auto high = bus.read8(bus.opaque, address);",
                 "    const auto low = bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1));",
                 ("    aot_set_d(s, static_cast<std::uint16_t>((high << 8) | low));"
                  if register == "d" else
                  f"    s.{register} = static_cast<std::uint16_t>((high << 8) | low);"),
                 ("    set_nz16(s, aot_d(s));" if register == "d" else
                  f"    set_nz16(s, s.{register});"),
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 "    s.cycles += 5;"]
    elif op == "indexed_logic":
        opcode, post, extension = args
        register = "a" if opcode == 0xaa else "b"
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code += ["    if (!bus.read8) return BlockExit::BusFault;",
                 f"    s.{register} {'|=' if opcode == 0xaa else '&='} bus.read8(bus.opaque, {ea});",
                 f"    set_nz8(s, s.{register});",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 "    s.cycles += 4;"]
    elif op == "indexed_store16":
        opcode, post, extension = args
        value = {0xaf: "s.x", 0xed: "aot_d(s)", 0xef: "s.u"}[opcode]
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code += ["    if (!bus.write8) return BlockExit::BusFault;",
                 f"    const auto address = {ea};",
                 f"    const auto value = {value};",
                 "    bus.write8(bus.opaque, address, static_cast<std::uint8_t>(value >> 8));",
                 "    bus.write8(bus.opaque, static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value));",
                 "    set_nz16(s, value);",
                 "    s.cc &= static_cast<std::uint8_t>(~cc_v);",
                 "    s.cycles += 5;"]
    elif op == "jmp":
        code = [f"    s.pc = 0x{args[0]:04x};", "    s.cycles += 4;", "    return BlockExit::Dispatch;"]
    elif op == "jsr":
        code = ["    if (!bus.write8) return BlockExit::BusFault;", f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt:04x}));", f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt >> 8:04x}));", f"    s.pc = 0x{args[0]:04x};", "    s.cycles += 8;", "    return BlockExit::Dispatch;"]
    elif op == "jsr_indexed":
        _, post, extension = args
        ea = (f"indexed_address(s, bus, 0x{post:02x}, "
              f"static_cast<std::int16_t>(0x{extension:04x}), 0x{nxt:04x})")
        code = ["    if (!bus.write8) return BlockExit::BusFault;",
                f"    const auto target = {ea};",
                f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt:04x}));",
                f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt >> 8:04x}));",
                "    s.pc = target;", "    s.cycles += 7;",
                "    return BlockExit::Dispatch;"]
    elif op in ("branch", "lbra"):
        cond = args[0] if op == "branch" else 0
        if op == "lbra":
            code = [f"    s.pc = 0x{ins.target:04x};", "    s.cycles += 5;", "    return BlockExit::Dispatch;"]
        else:
            pred = ["true", "false", "!((s.cc & (cc_c|cc_z)) != 0)", "(s.cc & (cc_c|cc_z)) != 0", "!(s.cc & cc_c)", "(s.cc & cc_c) != 0", "!(s.cc & cc_z)", "(s.cc & cc_z) != 0", "!(s.cc & cc_v)", "(s.cc & cc_v) != 0", "!(s.cc & cc_n)", "(s.cc & cc_n) != 0", "((s.cc & cc_n) != 0) == ((s.cc & cc_v) != 0)", "((s.cc & cc_n) != 0) != ((s.cc & cc_v) != 0)", "!(s.cc & cc_z) && (((s.cc & cc_n) != 0) == ((s.cc & cc_v) != 0))", "(s.cc & cc_z) || (((s.cc & cc_n) != 0) != ((s.cc & cc_v) != 0))"]
            code = [f"    s.pc = 0x{nxt:04x};",
                    f"    if ({pred[cond]}) s.pc = 0x{ins.target:04x};",
                    "    s.cycles += 3;", "    return BlockExit::Dispatch;"]
    elif op in ("bsr",):
        code = ["    if (!bus.write8) return BlockExit::BusFault;", f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt:04x}));", f"    --s.s; bus.write8(bus.opaque, s.s, static_cast<std::uint8_t>(0x{nxt >> 8:04x}));", f"    s.pc = 0x{ins.target:04x};", "    s.cycles += 7;", "    return BlockExit::Dispatch;"]
    elif op == "rts":
        code = ["    if (!bus.read8) return BlockExit::BusFault;", "    const auto hi = bus.read8(bus.opaque, s.s++);", "    const auto lo = bus.read8(bus.opaque, s.s++);", "    s.pc = static_cast<std::uint16_t>((hi << 8) | lo);", "    s.cycles += 5;", "    return BlockExit::Dispatch;"]
    elif op == "rti":
        code = ["    if (!bus.read8) return BlockExit::BusFault;",
                "    s.cc = bus.read8(bus.opaque, s.s++);",
                "    unsigned cycles = 6;",
                "    if (s.cc & cc_e) {",
                "        s.a = bus.read8(bus.opaque, s.s++);",
                "        s.b = bus.read8(bus.opaque, s.s++);",
                "        s.dp = bus.read8(bus.opaque, s.s++);",
                "        { const auto hi = bus.read8(bus.opaque, s.s++);",
                "          const auto lo = bus.read8(bus.opaque, s.s++);",
                "          s.x = static_cast<std::uint16_t>((hi << 8) | lo); }",
                "        { const auto hi = bus.read8(bus.opaque, s.s++);",
                "          const auto lo = bus.read8(bus.opaque, s.s++);",
                "          s.y = static_cast<std::uint16_t>((hi << 8) | lo); }",
                "        { const auto hi = bus.read8(bus.opaque, s.s++);",
                "          const auto lo = bus.read8(bus.opaque, s.s++);",
                "          s.u = static_cast<std::uint16_t>((hi << 8) | lo); }",
                "        cycles = 15;",
                "    }",
                "    { const auto hi = bus.read8(bus.opaque, s.s++);",
                "      const auto lo = bus.read8(bus.opaque, s.s++);",
                "      s.pc = static_cast<std::uint16_t>((hi << 8) | lo); }",
                "    s.cycles += cycles;",
                "    return BlockExit::Dispatch;"]
    else:
        return ["    return BlockExit::NeedsInterpreter;"]
    code += ["    return BlockExit::Dispatch;"]
    return code


def emit_source(by_bank: dict[int, dict[int, Instruction]], reset: int) -> str:
    lines = [
        "// Generated by tools/m6809/recomp.py; do not edit.",
        '#include "core/cpus/m6809/m6809.hpp"',
        "namespace mamerecomp::m6809 {",
    ]
    for bank, instructions in sorted(by_bank.items()):
        for pc, ins in sorted(instructions.items()):
            lines += [f"static BlockExit b{bank}_{pc:04x}(State& s, Bus& bus, std::uint64_t stop) {{",
                      f"    if (s.pc != 0x{pc:04x}) return BlockExit::Dispatch;",
                      "    if (s.cycles >= stop) return BlockExit::Budget;"]
            lines.extend(emit_instruction(ins))
            lines += ["}"]
    lines += ["Block lookup(std::uint16_t pc, std::uint8_t bank) noexcept {", "    switch (bank & 7) {"]
    for bank, instructions in sorted(by_bank.items()):
        lines += [f"    case {bank}: switch (pc) {{"]
        lines += [f"        case 0x{pc:04x}: return &b{bank}_{pc:04x};" for pc in sorted(instructions)]
        lines += ["        default: return nullptr;", "    }"]
    lines += ["    default: return nullptr;", "    }", "}"]
    lines += [f"std::uint16_t reset_vector(const Bus& bus) noexcept {{ return bus.read8 ? static_cast<std::uint16_t>((bus.read8(bus.opaque, 0xfffe) << 8) | bus.read8(bus.opaque, 0xffff)) : 0x{reset:04x}; }}", "} // namespace mamerecomp::m6809", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True, help="MAME-layout 0x50000-byte sound CPU region")
    parser.add_argument("--output", type=Path, required=True, help="generated AOT C++ source")
    parser.add_argument("--entry", action="append", type=lambda value: int(value, 0), default=[], help="additional 16-bit code entry")
    parser.add_argument("--banks", default="0,1,2,3,4,5,6,7", help="comma-separated ROM banks to compile")
    args = parser.parse_args()
    try:
        rom = SoundRom(args.rom.read_bytes())
        reset = rom.word(0xfffe, 0)
        if not 0x4000 <= reset <= 0xffff:
            raise ValueError(f"reset vector {reset:#06x} is outside ROM")
        banks = [int(item, 0) for item in args.banks.split(",")]
        entries = list(dict.fromkeys([reset, *args.entry]))
        by_bank = {bank: translate(rom, entries, bank) for bank in banks}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(emit_source(by_bank, reset), encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"m6809-recomp: {error}", file=sys.stderr)
        return 2
    total = sum(len(instructions) for instructions in by_bank.values())
    print(f"MC6809(E) reset={reset:#06x}; banks={len(banks)}; AOT instructions={total}; "
          f"interpreter entries remain explicit; generated={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
