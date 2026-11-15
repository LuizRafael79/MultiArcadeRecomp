# SPDX-License-Identifier: MIT
from __future__ import annotations

import unittest

from tools.z80_aot.isa import Flow, OpcodeSpace, decode_one
from tools.z80_aot.discover import discover


class Z80InstructionDecodeTests(unittest.TestCase):
    def test_all_opcode_spaces_have_valid_instruction_boundaries(self) -> None:
        for opcode in range(256):
            with self.subTest(space="base", opcode=opcode):
                image = bytearray(0x10000)
                image[0x100] = opcode
                insn = decode_one(bytes(image), 0x100)
                self.assertEqual(insn.address, 0x100)
                self.assertEqual(insn.end, 0x100 + insn.length)
                self.assertGreaterEqual(insn.length, 1)

            with self.subTest(space="cb", opcode=opcode):
                image = bytearray(0x10000)
                image[0x100:0x102] = bytes((0xCB, opcode))
                self.assertEqual(decode_one(bytes(image), 0x100).space, OpcodeSpace.CB)

            with self.subTest(space="ed", opcode=opcode):
                image = bytearray(0x10000)
                image[0x100:0x102] = bytes((0xED, opcode))
                insn = decode_one(bytes(image), 0x100)
                self.assertEqual(insn.space, OpcodeSpace.ED)
                self.assertEqual(insn.end, 0x100 + insn.length)

            with self.subTest(space="index-cb", opcode=opcode):
                image = bytearray(0x10000)
                image[0x100:0x104] = bytes((0xDD, 0xCB, 0xFE, opcode))
                insn = decode_one(bytes(image), 0x100)
                self.assertEqual(insn.space, OpcodeSpace.INDEX_CB)
                self.assertEqual(insn.length, 4)

    def test_separate_opcode_and_data_views(self) -> None:
        data = bytearray(0x10000)
        opcodes = bytearray(data)
        opcodes[0x200:0x202] = bytes((0xDD, 0x36))
        data[0x200:0x202] = bytes((0x00, 0x00))
        data[0x202:0x204] = bytes((0xFE, 0x7A))
        insn = decode_one(bytes(data), 0x200, bytes(opcodes))
        self.assertEqual(insn.raw, bytes((0xDD, 0x36, 0xFE, 0x7A)))
        self.assertEqual(insn.length, 4)
        self.assertEqual(insn.index, 1)
        self.assertEqual(insn.index_displacement, 0xFE)
        self.assertEqual(insn.immediate8, 0x7A)
        self.assertEqual((insn.x, insn.y, insn.z), (0, 6, 6))

    def test_prefix_rules_and_indexed_cb_fetch_order(self) -> None:
        image = bytearray(0x10000)
        image[0x300:0x307] = bytes((0xDD, 0xFD, 0xED, 0x4D, 0, 0, 0))
        retn = decode_one(bytes(image), 0x300)
        self.assertEqual(retn.space, OpcodeSpace.ED)
        self.assertIsNone(retn.index)
        self.assertEqual(retn.flow, Flow.RETURN)
        self.assertEqual(retn.length, 4)

        image[0x320:0x324] = bytes((0xFD, 0xCB, 0x80, 0x46))
        indexed_bit = decode_one(bytes(image), 0x320)
        self.assertEqual(indexed_bit.space, OpcodeSpace.INDEX_CB)
        self.assertEqual(indexed_bit.index, 2)
        self.assertEqual(indexed_bit.raw, bytes((0xFD, 0xCB, 0x80, 0x46)))

    def test_control_flow_targets_and_wrap(self) -> None:
        image = bytearray(0x10000)
        image[0x400:0x40E] = bytes((
            0x20, 0x02,       # JR NZ,+2 -> 0x404
            0xCD, 0x34, 0x12, # CALL 0x1234
            0xC4, 0x78, 0x56, # CALL NZ,0x5678
            0xC9,             # RET
            0xC3, 0x00, 0x00, # JP 0
            0xED, 0xB0,       # LDIR repeats at ED prefix
        ))
        cases = (
            (0x400, Flow.CONDITIONAL_BRANCH, 0x404),
            (0x402, Flow.CALL, 0x1234),
            (0x405, Flow.CONDITIONAL_CALL, 0x5678),
            (0x408, Flow.RETURN, None),
            (0x409, Flow.BRANCH, 0),
            (0x40C, Flow.REPEAT, 0x40C),
        )
        for address, flow, target in cases:
            with self.subTest(address=address):
                insn = decode_one(bytes(image), address)
                self.assertEqual(insn.flow, flow)
                self.assertEqual(insn.target, target)
        self.assertEqual(decode_one(bytes(image), 0x400).relative_displacement, 2)

        image[0xFFFF] = 0x18  # JR -2 wraps back to the opcode at $ffff.
        image[0x0000] = 0xFE
        wrapped = decode_one(bytes(image), 0xFFFF)
        self.assertEqual(wrapped.end, 1)
        self.assertEqual(wrapped.target, 0xFFFF)

    def test_static_roots_calls_and_block_edges(self) -> None:
        image = bytearray(0x10000)
        image[0:9] = bytes((
            0xCD, 0x06, 0x00,  # CALL 0006
            0x20, 0x01,        # JR NZ,+1 -> 0006
            0xC9,              # RET (fallthrough of conditional branch)
            0xC9,              # RET (called routine)
            0x00, 0x00,
        ))
        result = discover(bytes(image), [0x0000, 0x0038])
        self.assertEqual(result.roots, (0x0000, 0x0038))
        self.assertEqual(set(result.blocks), {0x0000, 0x0003, 0x0005, 0x0006, 0x0038})
        self.assertEqual(
            [(edge.flow, edge.target) for edge in result.blocks[0x0000].edges],
            [(Flow.CALL, 0x0006), (Flow.NEXT, 0x0003)],
        )
        self.assertEqual(
            [(edge.flow, edge.target) for edge in result.blocks[0x0003].edges],
            [(Flow.CONDITIONAL_BRANCH, 0x0006), (Flow.NEXT, 0x0005)],
        )

    def test_indirect_control_flow_is_not_guessed(self) -> None:
        image = bytearray(0x10000)
        image[0:3] = bytes((0x21, 0x00, 0x20))  # LD HL,2000
        image[3] = 0xE9                      # JP (HL)
        result = discover(bytes(image), [0])
        self.assertEqual(tuple(result.blocks), (0,))
        self.assertEqual(result.blocks[0].instructions[-1].flow, Flow.INDIRECT_BRANCH)
        self.assertEqual(result.blocks[0].edges, ())

    def test_block_repeat_stays_inside_block_until_fallthrough(self) -> None:
        image = bytearray(0x10000)
        image[0:3] = bytes((0xED, 0xB0, 0xC9))  # LDIR; RET
        result = discover(bytes(image), [0])
        self.assertEqual(tuple(result.blocks), (0,))
        self.assertEqual(
            tuple(instruction.flow for instruction in result.blocks[0].instructions),
            (Flow.REPEAT, Flow.RETURN),
        )

    def test_rst_discovers_vector_and_return_address(self) -> None:
        image = bytearray(0x10000)
        image[0:2] = bytes((0xCF, 0xC9))  # RST 08; RET at return address
        image[0x08] = 0xC9
        result = discover(bytes(image), [0])
        self.assertEqual(set(result.blocks), {0x0000, 0x0001, 0x0008})
        self.assertEqual(result.blocks[0].edges[0].flow, Flow.CALL)
        self.assertEqual(result.blocks[0].edges[0].target, 0x0008)
        self.assertEqual(result.blocks[0].edges[1].flow, Flow.NEXT)
        self.assertEqual(result.blocks[0].edges[1].target, 0x0001)


if __name__ == "__main__":
    unittest.main()
