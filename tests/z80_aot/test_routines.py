# SPDX-License-Identifier: MIT
from __future__ import annotations

import unittest

from tools.z80_aot.discover import discover
from tools.z80_aot.routines import group_routines


class Z80RoutineGroupingTests(unittest.TestCase):
    def test_call_resume_and_loop_head_become_dispatch_entries(self) -> None:
        image = bytearray(0x10000)
        image[0:6] = bytes((
            0xCD, 0x10, 0x00,  # CALL 0010
            0x20, 0xFE,        # JR NZ back to 0003
            0xC3,              # JP 0020 follows below
        ))
        image[5:8] = bytes((0xC3, 0x20, 0x00))
        image[0x10] = 0xC9    # RET
        image[0x20] = 0xC9    # RET

        layout = group_routines(discover(bytes(image), [0]))
        by_address = {routine.address: routine for routine in layout.routines}

        self.assertEqual(by_address[0].blocks, (0, 3, 5))
        self.assertEqual(by_address[0].entries, (0, 3))
        self.assertEqual(by_address[0x10].blocks, (0x10,))
        self.assertEqual(by_address[0x20].blocks, (0x20,))
        self.assertEqual(layout.dispatch[3], 0)
        self.assertEqual(layout.dispatch[0x10], 0x10)
        self.assertEqual(layout.dispatch[0x20], 0x20)

    def test_conditional_call_has_callee_and_resume_routines(self) -> None:
        image = bytearray(0x10000)
        image[0:3] = bytes((0xCC, 0x10, 0x00))  # CALL Z,0010
        image[3] = 0xC9                       # RET after the call
        image[0x10] = 0xC9                    # callee RET

        layout = group_routines(discover(bytes(image), [0]))
        by_address = {routine.address: routine for routine in layout.routines}

        self.assertEqual(by_address[0].blocks, (0, 3))
        self.assertEqual(by_address[0].entries, (0, 3))
        self.assertEqual(by_address[0x10].blocks, (0x10,))

    def test_indirect_jump_ends_routine_without_guessing_target(self) -> None:
        image = bytearray(0x10000)
        image[0:4] = bytes((0x21, 0x00, 0x20, 0xE9))  # LD HL,2000; JP (HL)

        result = discover(bytes(image), [0])
        layout = group_routines(result)

        self.assertEqual(layout.routines[0].blocks, (0,))
        self.assertEqual(layout.routines[0].entries, (0,))
        self.assertNotIn(0x2000, layout.dispatch)

    def test_relative_jr_stays_inside_its_routine(self) -> None:
        image = bytearray(0x10000)
        image[0:4] = bytes((
            0x18, 0x02,  # JR to 0004
            0xC9,         # skipped RET
            0xC9,         # target RET
        ))

        layout = group_routines(discover(bytes(image), [0]))

        self.assertEqual(len(layout.routines), 1)
        self.assertEqual(layout.routines[0].blocks, (0, 4))
        self.assertEqual(layout.routines[0].entries, (0,))


if __name__ == "__main__":
    unittest.main()
