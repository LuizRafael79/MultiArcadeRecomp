# SPDX-License-Identifier: MIT
"""Control-flow discovery for MultiArcadeRecomp's Z80 static recompiler."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

from .isa import DecodeError, Flow, Instruction, decode_one


@dataclass(frozen=True, slots=True)
class Edge:
    flow: Flow
    target: int


@dataclass(frozen=True, slots=True)
class BasicBlock:
    address: int
    instructions: tuple[Instruction, ...]
    edges: tuple[Edge, ...]


@dataclass(frozen=True, slots=True)
class Discovery:
    roots: tuple[int, ...]
    instructions: dict[int, Instruction]
    blocks: dict[int, BasicBlock]
    unresolved: dict[int, str]


def discover(
    data_image: bytes,
    roots: tuple[int, ...] | list[int],
    opcode_image: bytes | None = None,
    address_limit: int = 0x8000,
) -> Discovery:
    """Find fixed-ROM code reachable from `roots`, following direct edges.

    Edges that leave the fixed image remain visible as unresolved targets;
    boards can later compile their bank variants separately. Indirect jumps,
    returns and dynamic addresses terminate a block without guessed targets.
    """
    if address_limit < 0 or address_limit > 0x10000:
        raise ValueError("address_limit must be between 0 and 0x10000")
    entry_roots = tuple(dict.fromkeys(a & 0xFFFF for a in roots))
    starts: set[int] = set(entry_roots)
    pending = deque(entry_roots)
    instructions: dict[int, Instruction] = {}
    unresolved: dict[int, str] = {}

    def schedule(address: int) -> None:
        address &= 0xFFFF
        if address < address_limit and address not in starts:
            starts.add(address)
            pending.append(address)

    while pending:
        block_start = pending.popleft()
        address = block_start
        while address < address_limit:
            if address != block_start and address in starts:
                break
            instruction = instructions.get(address)
            if instruction is None:
                try:
                    instruction = decode_one(data_image, address, opcode_image)
                except DecodeError as exc:
                    unresolved[address] = str(exc)
                    break
                instructions[address] = instruction

            next_address = instruction.end
            flow = instruction.flow
            target = instruction.target

            if flow in (Flow.BRANCH, Flow.CONDITIONAL_BRANCH, Flow.CALL,
                        Flow.CONDITIONAL_CALL):
                if target is not None:
                    schedule(target)
                    if target >= address_limit:
                        unresolved.setdefault(target, "direct edge leaves fixed image")
                if flow in (Flow.CONDITIONAL_BRANCH, Flow.CALL,
                            Flow.CONDITIONAL_CALL, Flow.REPEAT):
                    schedule(next_address)
                    if next_address >= address_limit:
                        unresolved.setdefault(next_address, "fallthrough leaves fixed image")
                break

            if flow in (Flow.INDIRECT_BRANCH, Flow.RETURN, Flow.CONDITIONAL_RETURN):
                if flow == Flow.CONDITIONAL_RETURN:
                    schedule(next_address)
                    if next_address >= address_limit:
                        unresolved.setdefault(next_address, "fallthrough leaves fixed image")
                break

            if next_address >= address_limit:
                unresolved.setdefault(next_address, "fallthrough leaves fixed image")
                break
            if next_address in starts:
                break
            address = next_address

    blocks: dict[int, BasicBlock] = {}
    for block_start in sorted(starts):
        first = instructions.get(block_start)
        if first is None:
            continue
        block_instructions: list[Instruction] = []
        address = block_start
        edges: list[Edge] = []
        while True:
            instruction = instructions.get(address)
            if instruction is None:
                break
            block_instructions.append(instruction)
            next_address = instruction.end
            flow = instruction.flow
            if flow in (Flow.BRANCH, Flow.CONDITIONAL_BRANCH, Flow.CALL,
                        Flow.CONDITIONAL_CALL):
                if instruction.target is not None:
                    edges.append(Edge(flow, instruction.target))
                if flow in (Flow.CONDITIONAL_BRANCH, Flow.CALL,
                            Flow.CONDITIONAL_CALL, Flow.REPEAT):
                    edges.append(Edge(Flow.NEXT, next_address))
                break
            if flow == Flow.CONDITIONAL_RETURN:
                edges.append(Edge(Flow.NEXT, next_address))
                break
            if flow in (Flow.INDIRECT_BRANCH, Flow.RETURN):
                break
            if next_address in starts:
                edges.append(Edge(Flow.NEXT, next_address))
                break
            if next_address >= address_limit:
                break
            if next_address not in instructions:
                break
            address = next_address
        blocks[block_start] = BasicBlock(block_start, tuple(block_instructions), tuple(edges))

    return Discovery(entry_roots, instructions, blocks, unresolved)
