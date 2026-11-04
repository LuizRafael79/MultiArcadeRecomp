#!/usr/bin/env python3
"""Regression check: generated multi-entry SH-4 functions fail loud."""
from __future__ import annotations

import sys
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/naomi"))
import recompile_sh4  # noqa: E402


class MissingLeaderRegression(unittest.TestCase):
    def test_generated_dispatch_default_marks_unresolved(self) -> None:
        # NOP fallthrough, then BRA with a NOP delay slot. The ordinary
        # fallthrough can be an IRQ resume entry; the delay slot cannot.
        image = bytes.fromhex("09 00 09 00 00 a0 09 00")
        manifest = {
            "set_id": "regression",
            "base_address": "0x00001000",
            "entry": "0x00001000",
            "functions": [{
                "address": "0x00001000",
                "length": 8,
                "reachable_addrs": ["0x00001000", "0x00001002",
                                    "0x00001004", "0x00001006"],
            }],
        }
        generated = recompile_sh4.emit(image, manifest)
        self.assertIn("ctx.unresolved_target = ctx.pc;", generated)
        self.assertIn("ctx.execution_stopped = true;", generated)
        self.assertNotIn("default: return;", generated)
        self.assertIn("case 0x00001002u:", generated)
        self.assertNotIn("case 0x00001006u:", generated)
        self.assertRegex(
            generated,
            r"d\.add\(0x00001002u, &FUN_00001000, "
            r"0x00001000u, 4u, 0x[0-9A-F]{16}ull\);",
        )

    def test_p1_alias_reads_physical_image_offset(self) -> None:
        # The image is physically based at A0000000. The P1 guest alias
        # 80000080 must therefore analyze bytes at ROM offset 0x80, not zero.
        image = bytearray(b"\xff\xff" * 0x82)
        image[0x80:0x84] = bytes.fromhex("0b 00 09 00")  # RTS; NOP delay slot
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            image_path = temp / "bios.bin"
            manifest_path = temp / "manifest.json"
            image_path.write_bytes(image)
            subprocess.run([
                sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
                "--image", str(image_path), "--base", "0xa0000000",
                "--entry", "0x80000080", "--set-id", "alias_regression",
                "--output", str(manifest_path), "--max-instructions", "16",
            ], check=True, capture_output=True, text=True)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["instructions_analyzed"], 2)
        self.assertEqual(manifest["functions"][0]["address"], "0x80000080")

    def test_analyzer_resolves_indexed_word_braf_target(self) -> None:
        # MOVA -> MOV.W @(Rm,R0),Rn -> BRAF is a common inline jump-table
        # pattern. The signed table displacement must become a discovered root.
        image = bytearray(b"\x09\x00" * 0x12)
        image[0x00:0x0a] = bytes.fromhex(
            "00 e1 03 c7 1d 00 23 00 09 00")
        image[0x10:0x12] = bytes.fromhex("16 00")
        image[0x20:0x24] = bytes.fromhex("0b 00 09 00")
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            image_path = temp / "jump_table.bin"
            manifest_path = temp / "manifest.json"
            image_path.write_bytes(image)
            subprocess.run([
                sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
                "--image", str(image_path), "--base", "0x00001000",
                "--entry", "0x00001000", "--set-id", "braf_regression",
                "--output", str(manifest_path), "--max-instructions", "32",
            ], check=True, capture_output=True, text=True)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["functions"][-1]["address"], "0x00001020")


if __name__ == "__main__":
    unittest.main()
