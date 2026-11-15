# SPDX-License-Identifier: MIT
"""Typed, ROM-independent Z80 instruction decoding for the AoT frontend.

This module models instruction boundaries and control flow only. It is kept
separate from the MIT SuperZazu execution core and from board/runtime APIs so
the decoder can be tested without ROMs, generated C++, or a host CPU core.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto

ADDRESS_MASK = 0xFFFF


class DecodeError(ValueError):
    """The requested instruction crosses bytes absent from the supplied image."""


class OpcodeSpace(Enum):
    BASE = auto()
    CB = auto()
    ED = auto()
    INDEX_CB = auto()


class Flow(Enum):
    NEXT = auto()
    CONDITIONAL_BRANCH = auto()
    BRANCH = auto()
    INDIRECT_BRANCH = auto()
    CONDITIONAL_CALL = auto()
    CALL = auto()
    CONDITIONAL_RETURN = auto()
    RETURN = auto()
    REPEAT = auto()


@dataclass(frozen=True, slots=True)
class Instruction:
    address: int
    end: int
    raw: bytes
    opcode: int
    space: OpcodeSpace
    index: int | None
    prefixes: tuple[int, ...]
    flow: Flow
    target: int | None = None
    immediate8: int | None = None
    immediate16: int | None = None
    index_displacement: int | None = None
    relative_displacement: int | None = None

    @property
    def length(self) -> int:
        return len(self.raw)

    @property
    def x(self) -> int:
        return self.opcode >> 6

    @property
    def y(self) -> int:
        return (self.opcode >> 3) & 7

    @property
    def z(self) -> int:
        return self.opcode & 7


def _image_byte(image: bytes, address: int, what: str) -> int:
    address &= ADDRESS_MASK
    if address >= len(image):
        raise DecodeError(f"missing {what} byte at 0x{address:04x}")
    return image[address]


def decode_one(
    data_image: bytes,
    address: int,
    opcode_image: bytes | None = None,
) -> Instruction:
    """Decode one instruction from data/opcode views of a 64 KiB Z80 space.

    `opcode_image` can differ from `data_image` for boards that decrypt M1
    opcode fetches. Immediate bytes and displacements always come from the data
    view. A DDCB/FDCB operation reads its final operation byte from the data
    view; ordinary CB and ED second bytes are opcode fetches.
    """
    if opcode_image is None:
        opcode_image = data_image
    start = address & ADDRESS_MASK
    pc = start
    raw = bytearray()

    def take_data() -> int:
        nonlocal pc
        value = _image_byte(data_image, pc, "data")
        raw.append(value)
        pc = (pc + 1) & ADDRESS_MASK
        return value

    def take_opcode() -> int:
        nonlocal pc
        value = _image_byte(opcode_image, pc, "opcode")
        raw.append(value)
        pc = (pc + 1) & ADDRESS_MASK
        return value

    def take_word() -> int:
        low = take_data()
        return low | (take_data() << 8)

    prefixes: list[int] = []
    index: int | None = None
    op = take_opcode()
    while op in (0xDD, 0xFD):
        prefixes.append(op)
        index = 1 if op == 0xDD else 2
        op = take_opcode()

    if op == 0xED:
        # Index prefixes before ED are ignored, but remain part of the bytes.
        second = take_opcode()
        space = OpcodeSpace.ED
        index = None
        x, y, z = second >> 6, (second >> 3) & 7, second & 7
        flow = Flow.NEXT
        target = None
        immediate16 = None
        if x == 1 and z == 5:
            flow = Flow.RETURN
        elif x == 2 and y >= 6 and z <= 3:
            flow = Flow.REPEAT
            target = start
        elif x == 1 and z == 3:
            immediate16 = take_word()
        return Instruction(start, pc, bytes(raw), second, space, index,
                           tuple(prefixes), flow, target, immediate16=immediate16)

    if op == 0xCB:
        if index is None:
            second = take_opcode()
            space = OpcodeSpace.CB
        else:
            index_displacement = take_data()  # signed displacement
            second = take_data()
            space = OpcodeSpace.INDEX_CB
            return Instruction(start, pc, bytes(raw), second, space, index,
                               tuple(prefixes), Flow.NEXT,
                               index_displacement=index_displacement)
        return Instruction(start, pc, bytes(raw), second, space, index,
                           tuple(prefixes), Flow.NEXT)

    flow = Flow.NEXT
    target: int | None = None
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    index_displacement: int | None = None
    relative_displacement: int | None = None
    immediate8: int | None = None
    immediate_word: int | None = None

    # Determine displacement bytes used by indexed register/memory forms.
    if index is not None and (
        (x == 1 and op != 0x76 and (y == 6 or z == 6))
        or (x == 2 and z == 6)
        or (x == 0 and z in (4, 5, 6) and y == 6)
    ):
        index_displacement = take_data()

    # The x/y/z pattern describes instruction length independently of meaning.
    if x == 0:
        if z == 0 and y >= 2:
            relative_displacement = take_data()
        elif z == 1 and not q:
            immediate_word = take_word()
        elif z == 2 and y >= 4:
            immediate_word = take_word()
        elif z == 6:
            immediate8 = take_data()
    elif x == 3:
        if z == 2:
            immediate_word = take_word()
        elif z == 3 and y == 0:
            immediate_word = take_word()
        elif z == 3 and y in (2, 3):
            immediate8 = take_data()
        elif z == 4:
            immediate_word = take_word()
        elif z == 5 and q and p == 0:
            immediate_word = take_word()
        elif z == 6:
            immediate8 = take_data()

    # Relative branch target is relative to the byte following the displacement.
    if x == 0 and z == 0 and y in range(2, 8):
        assert relative_displacement is not None
        target = (pc + (relative_displacement - 0x100 if relative_displacement & 0x80 else relative_displacement)) & ADDRESS_MASK
        flow = Flow.BRANCH if y == 3 else Flow.CONDITIONAL_BRANCH
    elif x == 3 and z == 0:
        flow = Flow.CONDITIONAL_RETURN
    elif x == 3 and z == 1 and q:
        if p == 0:
            flow = Flow.RETURN
        elif p == 2:
            flow = Flow.INDIRECT_BRANCH
    elif x == 3 and z == 2:
        flow = Flow.CONDITIONAL_BRANCH
        target = immediate_word
    elif x == 3 and z == 3 and y == 0:
        flow = Flow.BRANCH
        target = immediate_word
    elif x == 3 and z == 4:
        flow = Flow.CONDITIONAL_CALL
        target = immediate_word
    elif x == 3 and z == 5 and q and p == 0:
        flow = Flow.CALL
        target = immediate_word
    elif x == 3 and z == 7:
        flow = Flow.CALL
        target = y * 8

    return Instruction(start, pc, bytes(raw), op, OpcodeSpace.BASE, index,
                       tuple(prefixes), flow, target, immediate8, immediate_word,
                       index_displacement, relative_displacement)
