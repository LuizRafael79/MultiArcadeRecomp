# SPDX-License-Identifier: MIT
"""Group discovered Z80 basic blocks into dispatcher-sized routines.

The routine boundary policy follows MameRecomp's shared 68000 AoT shape:
calls and unconditional transfers cross the dispatcher, while conditional
branches and their loop edges stay as labels inside a generated routine.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

from .discover import Discovery
from .isa import Flow


@dataclass(frozen=True, slots=True)
class Routine:
    address: int
    blocks: tuple[int, ...]
    entries: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class RoutineLayout:
    routines: tuple[Routine, ...]
    dispatch: dict[int, int]


def group_routines(discovery: Discovery) -> RoutineLayout:
    """Build routine groups and their legal dispatcher/resume addresses.

    A direct CALL target, RST vector, and unconditional branch target starts a
    dispatcher routine. Conditional branches, their fallthroughs, and ED block
    repeat edges remain internal. A call's fallthrough is both part of the
    caller's routine and a resume entry because the board dispatcher regains
    control after the callee returns. Back-edge targets are also resume entries
    so generated code can yield to a due board event and later re-enter safely.

    Blocks may appear in more than one group when control-flow regions overlap,
    matching the 68000 frontend's correctness-first duplication rule.
    """
    blocks = discovery.blocks

    def is_jp_tail(address: int) -> bool:
        block = blocks[address]
        if not block.instructions or block.instructions[-1].flow != Flow.BRANCH:
            return False
        # Relative JR is the Z80 equivalent of a local BRA edge. Absolute JP
        # leaves the current generated routine and gets its own dispatch entry.
        return block.instructions[-1].opcode != 0x18

    heads = {root for root in discovery.roots if root in blocks}
    for block in blocks.values():
        for edge in block.edges:
            if (edge.flow in (Flow.CALL, Flow.CONDITIONAL_CALL) or
                    (edge.flow == Flow.BRANCH and is_jp_tail(block.address))):
                if edge.target in blocks:
                    heads.add(edge.target)

    pending = deque(sorted(heads))
    queued = set(heads)
    routines: list[Routine] = []
    covered: set[int] = set()

    while pending:
        head = pending.popleft()
        if head not in blocks:
            continue
        members: set[int] = set()
        work = [head]
        resume_entries = {head}

        while work:
            address = work.pop()
            if address in members or address not in blocks:
                continue
            members.add(address)
            block = blocks[address]

            has_call = any(edge.flow in (Flow.CALL, Flow.CONDITIONAL_CALL)
                           for edge in block.edges)
            for edge in block.edges:
                target = edge.target
                if edge.flow in (Flow.CALL, Flow.CONDITIONAL_CALL):
                    # The callee/tail target is dispatched separately.
                    continue
                if edge.flow == Flow.BRANCH and is_jp_tail(address):
                    continue
                if edge.flow in (Flow.NEXT, Flow.BRANCH, Flow.CONDITIONAL_BRANCH, Flow.REPEAT):
                    if target in blocks:
                        work.append(target)
                        if edge.flow in (Flow.BRANCH, Flow.CONDITIONAL_BRANCH, Flow.REPEAT) and target <= address:
                            resume_entries.add(target)
                        if has_call and edge.flow == Flow.NEXT:
                            resume_entries.add(target)

        ordered_blocks = tuple(sorted(members))
        ordered_entries = tuple(sorted(resume_entries & members))
        routines.append(Routine(head, ordered_blocks, ordered_entries))
        covered.update(members)

        # Be total even if an unusual/partial CFG contains a block that was not
        # reachable from the supplied roots or a statically described edge.
        for orphan in sorted(set(blocks) - covered - queued):
            pending.append(orphan)
            queued.add(orphan)

    routines.sort(key=lambda routine: routine.address)
    dispatch: dict[int, int] = {}
    for routine in routines:
        for entry in routine.entries:
            dispatch.setdefault(entry, routine.address)
    return RoutineLayout(tuple(routines), dispatch)
