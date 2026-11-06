import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.psx.recomp import (
    Program, branch_target, build_aot_blocks, emit_block, emit_instruction,
    flow, is_native_instruction,
)


class R3000ARecompilerTests(unittest.TestCase):
    def test_native_coverage_classifies_emitted_interpreter_fallback(self):
        addiu = (9 << 26) | (8 << 16) | 1
        cop2 = (18 << 26) | (16 << 21) | (2 << 11) | 0x30
        self.assertTrue(is_native_instruction(addiu))
        self.assertFalse(is_native_instruction(cop2))

    def test_aot_blocks_stop_at_branch_and_keep_delay_slot_entry(self):
        base = 0x80010000
        addiu = (9 << 26) | (8 << 16) | 1
        beq = (4 << 26) | 1
        program = Program(bytes(0x100), base)
        program.code = {
            base: addiu,
            base + 4: addiu,
            base + 8: beq,
            base + 12: 0,
            base + 16: addiu,
        }
        program.entries = set(program.code)
        blocks = build_aot_blocks(program)
        self.assertEqual(blocks[0], [
            (base, addiu), (base + 4, addiu), (base + 8, beq),
            (base + 12, 0),
        ])
        self.assertEqual(blocks[1], [(base + 16, addiu)])
        self.assertIn("cycle_limit", emit_block(blocks[0]))
        self.assertIn("actual_opcode !=", emit_block(blocks[0]))
        self.assertIn("case 3: goto instruction_3", emit_block(blocks[0]))

    def test_branch_target_is_relative_to_delay_slot(self):
        pc = 0x80010000
        beq_zero_zero_plus_three = (4 << 26) | (3 & 0xffff)
        self.assertEqual(branch_target(pc, beq_zero_zero_plus_three), pc + 16)

    def test_jalr_discovery_continues_after_delay_slot(self):
        pc = 0x80010000
        jalr_ra = (8 << 21) | (31 << 11) | 9
        self.assertEqual(flow(pc, jalr_ra), ("jalr", None, True))

    def test_discovery_keeps_branch_and_delay_slot_entries(self):
        base = 0x80010000
        words = [
            (9 << 26) | (8 << 16) | 1,              # addiu t0, zero, 1
            (4 << 26) | 1,                           # beq zero, zero, base+12
            (0 << 6) | 0x00,                         # delay slot: nop
            (2 << 26) | ((base >> 2) & 0x03ffffff), # j base
        ]
        image = b"".join(word.to_bytes(4, "little") for word in words)
        program = Program(image, base)
        program.discover({base})
        self.assertEqual(program.entries, {base, base + 4, base + 8, base + 12})

    def test_native_branch_preserves_untaken_delay_slot(self):
        pc = 0x80010000
        beq = (4 << 26) | (1 << 21) | (2 << 16) | 2
        emitted = emit_instruction(pc, beq)
        self.assertIn("0x8001000cu, take", emitted)
        self.assertIn("psx_aot_schedule_branch", emitted)

    def test_native_load_uses_pipeline_aware_helper(self):
        pc = 0x80010000
        lw = (0x23 << 26) | (4 << 21) | (2 << 16) | 8
        emitted = emit_instruction(pc, lw)
        self.assertIn("psx_aot_schedule_load(s, 2u", emitted)
        self.assertNotIn("psx_aot_advance(s)", emitted)

    def test_signed_add_routes_overflow_to_exception(self):
        pc = 0x80010000
        add = (1 << 21) | (2 << 16) | (3 << 11) | 0x20
        emitted = emit_instruction(pc, add)
        self.assertIn("psx_aot_raise_exception(s, 12u", emitted)

    def test_native_multiply_defers_result_until_mfhi_mflo(self):
        pc = 0x80010000
        mult = (1 << 21) | (2 << 16) | 0x18
        mflo = (8 << 11) | 0x12
        self.assertIn("psx_aot_muldiv_start(s, PSX_MULDIV_SIGNED", emit_instruction(pc, mult))
        self.assertIn("psx_aot_muldiv_result(s, false)", emit_instruction(pc + 4, mflo))

    def test_native_mthi_cancels_pending_operation_through_shared_helper(self):
        pc = 0x80010000
        mthi = (1 << 21) | 0x11
        self.assertIn("psx_aot_write_hilo(s, true", emit_instruction(pc, mthi))


if __name__ == "__main__":
    unittest.main()
