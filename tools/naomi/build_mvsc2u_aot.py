#!/usr/bin/env python3
"""Extract, analyze, translate, and compile the local MvC2 USA SH-4 program."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import zlib
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]
PROGRAM_ROM = "epr-23062a.ic22"
BIOS_ROM = "epr-21577h.ic27"
BIOS_CRC = 0xFDF17452
BIOS_FRAGMENT_OFFSET = 0x60000
BIOS_FRAGMENT_SIZE = 0x1C00
BIOS_FRAGMENT_BASE = 0x0C018000
BIOS_FIRST_ENTRY = 0x0C0183A4
# The BIOS PIO copier was observed in a saved Flycast trace whose internal game
# ID is Japan, not USA. Keep that as cross-set BIOS evidence only. The exact USA
# BIOS image is CRC-validated below. Its pointer table at 0x0c018040 names the
# P0 entry 0x0c0195e8; the P2 root covers the same code alias used by the saved
# trace at 0xac0195fe/0xac019640.
BIOS_ENTRIES = (0x0C018398, 0x0C01839E, 0x0C0183A4, 0x0C0183BC,
                0x0C01840A, 0x0C018436, 0x0C0185DC, 0x0C01862C,
                0x0C01874E, 0x0C0195E8, 0x0C019A38, 0xAC0195E8,
                0xAC0195FE, 0xAC019602, 0xAC019640)
BIOS_RESET_ENTRY = 0xA0000000
BIOS_RESET_INDIRECT_ENTRY = 0xA0000080
BIOS_RESET_INDIRECT_P1_ENTRY = 0x80000080
# The reset routine reaches this BIOS continuation after copying its startup
# tables into SH-4 internal RAM and returning through the on-chip stack.
BIOS_RESET_CONTINUATION = 0xA0000420
BIOS_RESET_CONTINUATION_2 = 0xA00005BC
BIOS_BOOTSTRAP_OFFSET = 0xE0
BIOS_BOOTSTRAP_SIZE = 0x20
BIOS_BOOTSTRAP_ENTRY = 0x8C0000E0
BIOS_RAM_COPY_OFFSET = 0x100
BIOS_RAM_COPY_BASE = 0x8C000100
BIOS_RAM_COPY_ENTRY = 0x8C000120
BIOS_COPIED_CODE_SOURCE = 0x800
BIOS_COPIED_CODE_SIZE = 0x3000
BIOS_COPIED_CODE_BASE = 0xAC001000
BIOS_COPIED_CODE_ENTRY = 0xAC00101A
BIOS_COPIED_CODE_ENTRY_2 = 0xAC002078
BIOS_COPIED_CODE_ENTRY_3 = 0xAC001AE4
BIOS_COPIED_CODE_ENTRY_4 = 0xAC002054
BIOS_COPIED_CODE_ENTRY_5 = 0xAC00114E
BIOS_COPIED_CODE_ENTRY_6 = 0xAC001B2C
BIOS_COPIED_CODE_ENTRY_7 = 0xAC001978
BIOS_COPIED_CODE_ENTRY_8 = 0xAC001284
BIOS_COPIED_CODE_ENTRY_9 = 0xAC001552
BIOS_COPIED_CODE_ENTRY_10 = 0xAC0014B0
BIOS_COPIED_CODE_ENTRY_11 = 0xAC00181C
BIOS_COPIED_CODE_ENTRY_12 = 0xAC001AFE
BIOS_COPIED_CODE_ENTRY_13 = 0xAC001216
# The BIOS copies this exception-return trampoline from its ROM image into
# main RAM at 0x0c000620 before dispatching through that vector.
BIOS_RAM_VECTOR_SOURCE = 0x1474
BIOS_RAM_VECTOR_BASE = 0x0C000620
BIOS_RAM_VECTOR_SIZE = 0x10
# The BIOS installs a small runtime vector at 0x0c000600 which jumps to this
# handler in its RAM copy. Compile the handler and its static callees separately
# at the P0 alias because interrupts enter through VBR=0x0c000000.
BIOS_RAM_IRQ_VECTOR_ENTRY = 0x0C000600
BIOS_RAM_IRQ_SECONDARY_ENTRY = 0x0C001000
# The BIOS dispatches the level-6/VBlank INTEVT (0x320) through its RAM
# vector table to this MvC2 handler. It is a dynamic interrupt target, so the
# normal game entry analyzer does not necessarily discover it.
GAME_VBLANK_IRQ_ENTRY = 0x0C055512
# The VBlank handler's dispatch path can indirectly call this guest function.
# The boot RAM image shows it is an actual two-instruction RTS/NOP function,
# so register it as code instead of treating it as a missing implementation.
GAME_DYNAMIC_BOOT_ENTRIES = (
    (GAME_VBLANK_IRQ_ENTRY, "vblank"),
    (0x0C04B83C, "vblank_callback"),
    (0x0C055918, "vblank_return"),
    (0x0C05408C, "vblank_resume"),
    (0x0C05477A, "vblank_resume_tail"),
    (0x0C04BB70, "vblank_resume_worker"),
    (0x0C02AD1A, "vblank_game_entry"),
    (0x0C04854A, "game_indirect_04854a"),
    (0x0C048606, "game_indirect_048606"),
    (0x0C048544, "game_indirect_048544"),
    (0x0C0485E8, "game_indirect_0485e8"),
    (0x0C05324E, "second_vblank_resume"),
    (0x0C05327E, "second_vblank_tail"),
    (0x0C053252, "third_vblank_resume"),
    (0x0C05323C, "fourth_vblank_resume"),
    (0x0C053236, "vblank_resume_053236"),
    (0x0C05323E, "vblank_resume_05323e"),
    (0x0C053242, "vblank_resume_053242"),
    (0x0C053254, "vblank_resume_053254"),
    (0x0C055A14, "callback_return_055a14"),
    (0x0C055A50, "callback_epilogue_055a50"),
    # Observed at runtime: JSR @R3 from 0x0c04c36e resolves to this aligned
    # function entry. The static analyzer cannot infer the loaded function
    # pointer, but the captured RAM bytes decode as a valid function body.
    (0x0C055D00, "runtime_indirect_055d00"),
    # Observed as the computed target of the BRAF jump table at 0x0c035f78.
    # The guest bounds the selector to 0..21; decoding the inline signed-word
    # table at 0x0c035f84 yields 13 unique, aligned targets. Each target was
    # independently analyzed from the captured RAM image with no unknown ops,
    # out-of-image edges, or unresolved static transfers.
    (0x0C035FB0, "runtime_braf_035fb0"),
    (0x0C035FB4, "runtime_braf_035fb4"),
    (0x0C035FF8, "runtime_braf_035ff8"),
    (0x0C03600C, "runtime_braf_03600c"),
    (0x0C036020, "runtime_braf_036020"),
    (0x0C036034, "runtime_braf_036034"),
    (0x0C036040, "runtime_braf_036040"),
    (0x0C036064, "runtime_braf_036064"),
    (0x0C03606A, "runtime_braf_03606a"),
    (0x0C03607E, "runtime_braf_03607e"),
    (0x0C036084, "runtime_braf_036084"),
    (0x0C0360A4, "runtime_braf_0360a4"),
    (0x0C0360A8, "runtime_braf_0360a8"),
    # Runtime-observed JSR @R3 at 0x0c0353fa (delay slot 0x0c0353fc),
    # R10=0x80 (table selector 32). The target analyzes as 69 valid instructions.
    (0x0C03652E, "runtime_jsr_03652e"),
    # A second runtime-observed JSR @R3 from 0x0c0353fa, with R10=0x84
    # (table selector 33). Other captured pointer-table values remain
    # candidates only; they are not registered without runtime/write evidence.
    (0x0C0365B8, "runtime_jsr_0365b8"),
    # Runtime-observed JSR @R2 at 0x0c045fdc returned through the delay slot
    # 0x0c045fde; the target is an aligned RTS/NOP stub in the captured RAM.
    (0x0C04B88C, "runtime_jsr_04b88c"),
    # Runtime-observed indirect dispatch from delay-slot PC 0x0c02098c. The
    # target is aligned and analyzes to 80 reachable instructions without an
    # unknown instruction or unresolved static transfer.
    (0x0C02860C, "runtime_indirect_02860c"),
    (0x0C02913C, "game_callback_02913c"),
    (0x0C0201EA, "game_init_resume_0201ea"),
    (0x0C02F3A6, "game_resume_02f3a6"),
)
# The captured pre-payload boot-RAM image has RTS/NOP at 0x0c04b88c, so keep
# that observed entry in the boot-RAM manifest. A later live Flycast capture
# with the real BIOS running shows the USA EPR bytes at the same guest address
# (EPR offset 0x2b88c), proving the mapping changes across boot phases. Do not
# register the ROM AOT unconditionally: runtime dispatch must first establish
# that guest RAM contains the corresponding translated bytes.
GAME_DYNAMIC_ROM_ENTRIES = (
    (0x0C207ADC, "draw_pref_207adc"),
    # Observed during the bounded REIOS AOT smoke: JSR @Rn from the delay
    # slot at 0x0c217f02 resolved to this EPR-backed function entry. Starting
    # the static analyzer here found three functions and 57 reachable
    # instructions without unknown opcodes or unresolved static transfers.
    (0x0C224E3C, "reios_indirect_224e3c"),
    # The newly translated wrapper reached this callback through JSR @R2 at
    # 0x0c224e68. Analysis from the observed EPR target found two functions
    # and 22 reachable instructions without unresolved static transfers.
    (0x0C2171C6, "reios_indirect_2171c6"),
    # The second callback from the same setup wrapper is stored in the EPR
    # literal at 0x0c217f80, copied to the runtime task record at 0x0c3b2728,
    # then observed as the JSR @R2 target at 0x0c224e68. It analyzes to nine
    # valid instructions ending in RTS/NOP.
    (0x0C217ED2, "reios_indirect_217ed2"),
    # Runtime observed through the shared callback wrapper at 0x0c224e68;
    # its EPR pointer literal is at 0x0c21544c. Static analysis finds 24
    # reachable instructions and no unresolved static transfers.
    (0x0C2153E8, "reios_indirect_2153e8"),
    # Runtime observed from the shared callback wrapper after the EPR-backed
    # initialization above. Its pointer literal is at 0x0c215760; analysis
    # finds four functions / 144 reachable instructions with no unresolved
    # static transfers.
    (0x0C2156A0, "reios_indirect_2156a0"),
    # Runtime observed as the callback target at the shared REIOS task wrapper;
    # the EPR bytes decode to 31 reachable instructions ending in RTS.
    (0x0C218400, "reios_indirect_218400"),
    # Runtime observed as the next callback target after 0x0c218400. The EPR
    # contains executable SH-4 code at this address; translation retains its
    # indirect JSR as a runtime dispatch (the static analyzer reports that edge
    # separately rather than inventing a destination).
    (0x0C215880, "reios_indirect_215880"),
    # Runtime observed as the next callback after 0x0c215880; static analysis
    # reaches three functions / 242 instructions without unresolved edges.
    (0x0C217A40, "reios_indirect_217a40"),
    # Runtime observed after the callbacks above; EPR analysis reaches two
    # functions / 17 instructions without unresolved static edges.
    (0x0C224A6C, "reios_indirect_224a6c"),
    # Distinct runtime-observed entry immediately following the previous EPR
    # routine; independent analysis reaches four functions / 54 instructions.
    (0x0C224A80, "reios_indirect_224a80"),
    # Runtime-observed task callback; static analysis reaches three functions
    # / 205 instructions with no unresolved edges.
    (0x0C2266A0, "reios_indirect_2266a0"),
)
GAME_STATIC_ROM_FUNCTION_ROOTS = frozenset({0x0C207ADC})
BOUNDED_BRAF_TABLE_ROOTS = frozenset({
    0x0C035FB0, 0x0C035FB4, 0x0C035FF8, 0x0C03600C, 0x0C036020,
    0x0C036034, 0x0C036040, 0x0C036064, 0x0C03606A, 0x0C03607E,
    0x0C036084, 0x0C0360A4, 0x0C0360A8,
})

def run(command: list[str]) -> None:
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def verify_generated_boot_roots(manifest_path: Path, source_path: Path) -> str:
    """Fail the build if any declared dynamic root is missing from AOT output."""
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    source = source_path.read_text(encoding="utf-8")
    entries = {int(value, 16) for value in re.findall(
        r"d\.add\(0x([0-9A-Fa-f]{8})u,", source)}
    cases = {int(value, 16) for value in re.findall(
        r"case 0x([0-9A-Fa-f]{8})u:", source)}
    functions = {int(value, 16) for value in re.findall(
        r"void FUN_(?:[a-z0-9_]+_)?([0-9A-Fa-f]{8})\(", source)}
    missing = [entry for entry, _name in GAME_DYNAMIC_BOOT_ENTRIES
               if entry not in entries or (entry not in cases and entry not in functions)]
    if missing:
        formatted = ", ".join(f"0x{entry:08x}" for entry in missing)
        raise SystemExit(
            f"stale/incomplete SH-4 AOT output: dynamic root(s) absent from "
            f"generated body or dispatcher: {formatted}")
    digest = hashlib.sha256(manifest_bytes).hexdigest()
    print(f"Verified {len(GAME_DYNAMIC_BOOT_ENTRIES)} dynamic AOT roots; "
          f"boot-RAM manifest SHA-256={digest}", flush=True)
    return digest


def verify_generated_rom_roots(manifest_path: Path, source_path: Path,
                               expected_entry: int) -> None:
    """Require observed ROM-backed dispatch roots in both body and dispatcher."""
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    source = source_path.read_text(encoding="utf-8")
    entries = {int(value, 16) for value in re.findall(
        r"d\.add\(0x([0-9A-Fa-f]{8})u,", source)}
    cases = {int(value, 16) for value in re.findall(
        r"case 0x([0-9A-Fa-f]{8})u:", source)}
    functions = {int(value, 16) for value in re.findall(
        r"void FUN_(?:[a-z0-9_]+_)?([0-9A-Fa-f]{8})\(", source)}
    manifest_roots = {int(item["address"], 0)
                      for item in manifest.get("functions", [])}
    if (expected_entry not in entries or
            (expected_entry not in cases and expected_entry not in functions) or
            expected_entry not in manifest_roots):
        formatted = f"0x{expected_entry:08x}"
        raise SystemExit(
            f"stale/incomplete SH-4 ROM AOT output: observed root absent from "
            f"manifest, generated body, or dispatcher: {formatted}")
    print(f"Verified observed ROM AOT root 0x{expected_entry:08x}; "
          f"instructions_analyzed={manifest.get('instructions_analyzed', 0)}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom_zip", type=Path, help="owner-supplied mvsc2u.zip")
    parser.add_argument("--bios-zip", type=Path,
                        help="owner-supplied naomi.zip (defaults beside rom_zip)")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/release")
    parser.add_argument("--max-instructions", type=int, default=250000)
    parser.add_argument("--smoke-instructions", type=int, default=0,
                        help="after building, boot MvC2 for this many guest instructions")
    parser.add_argument("--boot-ram-image", type=Path,
                        help="optional 4 MiB RAM snapshot captured after the real BIOS/cart boot stage")
    parser.add_argument("--irq-handler-image", type=Path,
                        help="optional 256 KiB low-RAM snapshot from a real BIOS boot, including its patched IRQ handler")
    args = parser.parse_args()

    rom_zip = args.rom_zip.expanduser().resolve()
    bios_zip = (args.bios_zip.expanduser().resolve() if args.bios_zip else
                rom_zip.parent / "naomi.zip")
    build_dir = args.build_dir.expanduser().resolve()
    generated_dir = build_dir / "naomi-generated/mvsc2u"
    generated_dir.mkdir(parents=True, exist_ok=True)
    image_path = generated_dir / "epr-23062a.ic22"
    game_manifest_path = generated_dir / "functions.json"
    source_path = generated_dir / "mvsc2u_translated.cpp"
    reios_manifest_path = generated_dir / "mvsc2u_reios_functions.json"
    reios_source_path = generated_dir / "mvsc2u_reios_translated.cpp"
    rom_runtime_manifest_paths = {
        entry: generated_dir / f"mvsc2u_{name}_functions.json"
        for entry, name in GAME_DYNAMIC_ROM_ENTRIES}
    rom_runtime_source_paths = {
        entry: generated_dir / f"mvsc2u_{name}_translated.cpp"
        for entry, name in GAME_DYNAMIC_ROM_ENTRIES}
    bios_fragment_path = generated_dir / "naomi_bios_fragment.bin"
    bios_manifest_path = generated_dir / "naomi_bios_functions.json"
    bios_source_path = generated_dir / "naomi_bios_translated.cpp"
    bios_reset_manifest_path = generated_dir / "naomi_bios_reset_functions.json"
    bios_reset_source_path = generated_dir / "naomi_bios_reset_translated.cpp"
    bios_reset_p1_manifest_path = generated_dir / "naomi_bios_reset_p1_functions.json"
    bios_reset_p1_source_path = generated_dir / "naomi_bios_reset_p1_translated.cpp"
    bootstrap_path = generated_dir / "naomi_bootstrap.bin"
    bootstrap_manifest_path = generated_dir / "naomi_bootstrap_functions.json"
    bootstrap_source_path = generated_dir / "naomi_bootstrap_translated.cpp"
    bios_ram_path = generated_dir / "naomi_bios_ram_shadow.bin"
    bios_ram_manifest_path = generated_dir / "naomi_bios_ram_shadow_functions.json"
    bios_ram_source_path = generated_dir / "naomi_bios_ram_shadow_translated.cpp"
    bios_copied_code_path = generated_dir / "naomi_bios_copied_code.bin"
    bios_copied_code_manifest_path = generated_dir / "naomi_bios_copied_code_functions.json"
    bios_copied_code_source_path = generated_dir / "naomi_bios_copied_code_translated.cpp"
    bios_ram_vector_path = generated_dir / "naomi_bios_ram_vector.bin"
    bios_ram_vector_manifest_path = generated_dir / "naomi_bios_ram_vector_functions.json"
    bios_ram_vector_source_path = generated_dir / "naomi_bios_ram_vector_translated.cpp"
    bios_ram_irq_manifest_path = generated_dir / "naomi_bios_ram_irq_functions.json"
    bios_ram_irq_secondary_manifest_path = generated_dir / "naomi_bios_ram_irq_secondary_functions.json"
    bios_ram_irq_source_path = generated_dir / "naomi_bios_ram_irq_translated.cpp"
    bios_ram_irq_image_path = generated_dir / "naomi_bios_irq_runtime.bin"
    boot_ram_image_path = generated_dir / "mvsc2u_boot_ram.bin"
    boot_ram_manifest_path = generated_dir / "mvsc2u_boot_ram_functions.json"
    boot_ram_source_path = generated_dir / "mvsc2u_boot_ram_translated.cpp"

    with ZipFile(rom_zip) as archive:
        matches = [name for name in archive.namelist()
                   if Path(name).name.lower() == PROGRAM_ROM.lower()]
        if len(matches) != 1:
            raise SystemExit(f"expected exactly one {PROGRAM_ROM} in {rom_zip}; found {len(matches)}")
        image_path.write_bytes(archive.read(matches[0]))
    print(f"Extracted {matches[0]} ({image_path.stat().st_size} bytes)")

    with ZipFile(bios_zip) as archive:
        matches = [name for name in archive.namelist()
                   if Path(name).name.lower() == BIOS_ROM.lower()]
        if len(matches) != 1:
            raise SystemExit(f"expected exactly one {BIOS_ROM} in {bios_zip}; found {len(matches)}")
        bios = archive.read(matches[0])
    actual_crc = zlib.crc32(bios) & 0xffffffff
    if len(bios) != 0x200000 or actual_crc != BIOS_CRC:
        raise SystemExit(f"unexpected {BIOS_ROM}: size={len(bios)} CRC={actual_crc:08x}; "
                         f"expected 2097152 bytes CRC={BIOS_CRC:08x}")
    bios_image_path = generated_dir / "naomi_bios.bin"
    bios_image_path.write_bytes(bios)
    # Reset code copies this BIOS block from 0x800 to 0x0c001000 before
    # jumping through the P2 alias at 0xac00101a.
    bios_copied_code_path.write_bytes(
        bios[BIOS_COPIED_CODE_SOURCE:BIOS_COPIED_CODE_SOURCE + BIOS_COPIED_CODE_SIZE])
    bios_ram_vector_path.write_bytes(
        bios[BIOS_RAM_VECTOR_SOURCE:BIOS_RAM_VECTOR_SOURCE + BIOS_RAM_VECTOR_SIZE])
    # The code reached at 0x8c0000e0 copies BIOS words from A0000100 onward
    # to RAM starting at 8c000100. This image lets the analyzer recompile the
    # bytes at their actual post-copy guest addresses (including PC-relative
    # references), while runtime still performs the copy on the SH-4 bus.
    bios_ram_path.write_bytes(bios[BIOS_RAM_COPY_OFFSET:])
    bootstrap_source = bios[BIOS_BOOTSTRAP_OFFSET:BIOS_BOOTSTRAP_OFFSET + BIOS_BOOTSTRAP_SIZE]
    if len(bootstrap_source) != BIOS_BOOTSTRAP_SIZE:
        raise SystemExit("Naomi BIOS does not contain the expected reset trampoline")
    # The reset helper copies 16 halfwords with a pre-decrementing destination
    # pointer. The bytes executed from RAM are therefore in reverse halfword
    # order; translating the source ROM slice verbatim invents a false branch.
    bootstrap = b"".join(
        bootstrap_source[offset:offset + 2]
        for offset in range(BIOS_BOOTSTRAP_SIZE - 2, -1, -2)
    )
    bootstrap_path.write_bytes(bootstrap)
    fragment = bios[BIOS_FRAGMENT_OFFSET:BIOS_FRAGMENT_OFFSET + BIOS_FRAGMENT_SIZE]
    bios_fragment_path.write_bytes(fragment)
    print(f"Extracted {matches[0]} BIOS fragment at 0x{BIOS_FRAGMENT_OFFSET:x} "
          f"({len(fragment)} bytes, CRC={actual_crc:08x})")

    run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
         "--image", str(image_path), "--base", "0x0c020000",
         "--entry", "0x0c021000", "--set-id", "mvsc2u",
         "--output", str(game_manifest_path), "--max-instructions",
         str(args.max_instructions)])
    for entry, name in GAME_DYNAMIC_ROM_ENTRIES:
        manifest_path = rom_runtime_manifest_paths[entry]
        if entry in GAME_STATIC_ROM_FUNCTION_ROOTS:
            game_manifest = json.loads(game_manifest_path.read_text(encoding="utf-8"))
            function = next((item for item in game_manifest["functions"]
                             if int(item["address"], 0) == entry), None)
            if function is None:
                raise SystemExit(f"main ROM manifest lacks observed function 0x{entry:08x}")
            root_manifest = {
                "set_id": f"mvsc2u_{name}",
                "base_address": game_manifest["base_address"],
                "entry": f"0x{entry:08x}",
                "functions": [function],
                "resolved_calls": game_manifest.get("resolved_calls", {}),
                "instructions_analyzed": len(function.get("reachable_addrs", [])),
                "root_evidence": (
                    "same-set Flycast executed PREF at 0x0c207ace; main ROM CFG "
                    "places that instruction in function 0x0c207adc"),
            }
            manifest_path.write_text(json.dumps(root_manifest, indent=2) + "\n",
                                     encoding="utf-8")
        else:
            run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
                 "--image", str(image_path), "--base", "0x0c020000",
                 "--entry", hex(entry), "--set-id", f"mvsc2u_{name}",
                 "--output", str(manifest_path),
                 "--max-instructions", str(args.max_instructions)])
    bios_manifests = []
    for entry in BIOS_ENTRIES:
        bios_entry_manifest_path = generated_dir / f"naomi_bios_{entry:08x}.json"
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(bios_fragment_path), "--base", hex(BIOS_FRAGMENT_BASE),
             "--entry", hex(entry), "--set-id", "mvsc2u_bios",
             "--output", str(bios_entry_manifest_path), "--max-instructions", "100000"])
        bios_manifests.append(json.loads(bios_entry_manifest_path.read_text(encoding="utf-8")))
    reset_manifests = []
    for entry in (BIOS_RESET_ENTRY, BIOS_RESET_INDIRECT_ENTRY,
                  BIOS_RESET_CONTINUATION, BIOS_RESET_CONTINUATION_2):
        reset_entry_path = generated_dir / f"naomi_bios_reset_{entry:08x}.json"
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(bios_image_path), "--base", hex(BIOS_RESET_ENTRY),
             "--entry", hex(entry), "--set-id", "naomi_bios_reset",
             "--output", str(reset_entry_path), "--max-instructions", "100000"])
        reset_manifests.append(json.loads(reset_entry_path.read_text(encoding="utf-8")))
    # The reset code reaches the BIOS routine at physical offset 0x80 through
    # the P1 alias (0x80000080). Keep that guest PC in a separate translation
    # so PC-relative semantics and dispatcher lookups preserve the alias.
    run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
         "--image", str(bios_image_path), "--base", hex(BIOS_RESET_ENTRY),
         "--entry", hex(BIOS_RESET_INDIRECT_P1_ENTRY), "--set-id", "naomi_bios_reset_p1",
         "--output", str(bios_reset_p1_manifest_path), "--max-instructions", "100000"])
    reset_merged = {
        "set_id": "naomi_bios_reset",
        "base_address": hex(BIOS_RESET_ENTRY),
        "entry": hex(BIOS_RESET_ENTRY),
        "functions": [], "resolved_calls": {}, "analysis": {},
        "unresolved": [], "instructions_analyzed": 0,
        "truncated_by_limit": False,
    }
    reset_seen = set()
    for manifest in reset_manifests:
        for function in manifest["functions"]:
            if function["address"] not in reset_seen:
                reset_merged["functions"].append(function)
                reset_seen.add(function["address"])
        reset_merged["resolved_calls"].update(manifest.get("resolved_calls", {}))
        reset_merged["analysis"].update(manifest.get("analysis", {}))
        reset_merged["unresolved"].extend(manifest.get("unresolved", []))
        reset_merged["instructions_analyzed"] += manifest.get("instructions_analyzed", 0)
        reset_merged["truncated_by_limit"] |= manifest.get("truncated_by_limit", False)
    bios_reset_manifest_path.write_text(json.dumps(reset_merged, indent=2) + "\n", encoding="utf-8")
    run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
         "--image", str(bootstrap_path), "--base", hex(BIOS_BOOTSTRAP_ENTRY),
         "--entry", hex(BIOS_BOOTSTRAP_ENTRY), "--set-id", "naomi_bootstrap",
         "--output", str(bootstrap_manifest_path), "--max-instructions", "10000"])
    run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
         "--image", str(bios_ram_path), "--base", hex(BIOS_RAM_COPY_BASE),
         "--entry", hex(BIOS_RAM_COPY_ENTRY), "--set-id", "naomi_bios_ram_shadow",
         "--output", str(bios_ram_manifest_path), "--max-instructions", "100000"])
    run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
         "--image", str(bios_ram_vector_path), "--base", hex(BIOS_RAM_VECTOR_BASE),
         "--entry", hex(BIOS_RAM_VECTOR_BASE), "--set-id", "naomi_bios_ram_vector",
         "--output", str(bios_ram_vector_manifest_path), "--max-instructions", "10000"])
    if args.irq_handler_image:
        irq_image = args.irq_handler_image.expanduser().resolve()
        if irq_image.stat().st_size != 0x40000:
            raise SystemExit(f"IRQ handler RAM image must be exactly 256 KiB: {irq_image}")
        if irq_image != bios_ram_irq_image_path:
            shutil.copyfile(irq_image, bios_ram_irq_image_path)
    elif not bios_ram_irq_image_path.exists():
        # The BIOS patches this handler in RAM at boot, so the ROM shadow is
        # not its executable form. Keep the registration symbol available
        # until a real runtime snapshot is supplied.
        bios_ram_irq_source_path.write_text(
            '#include "naomi/naomi_sh4_context.hpp"\n'
            'namespace multiarcaderecomp::naomi {\n'
            'void multiarcaderecomp_register_sh4_naomi_bios_ram_irq(Sh4Dispatcher&) {}\n'
            '}\n', encoding="utf-8")
    if bios_ram_irq_image_path.exists():
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(bios_ram_irq_image_path), "--base", "0x0c000000",
             "--entry", hex(BIOS_RAM_IRQ_VECTOR_ENTRY), "--set-id", "naomi_bios_ram_irq",
             "--output", str(bios_ram_irq_manifest_path), "--max-instructions", "10000"])
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(bios_ram_irq_image_path), "--base", "0x0c000000",
             "--entry", hex(BIOS_RAM_IRQ_SECONDARY_ENTRY), "--set-id", "naomi_bios_ram_irq",
             "--output", str(bios_ram_irq_secondary_manifest_path), "--max-instructions", "10000"])
        irq_manifest = json.loads(bios_ram_irq_manifest_path.read_text(encoding="utf-8"))
        irq_secondary = json.loads(bios_ram_irq_secondary_manifest_path.read_text(encoding="utf-8"))
        known_irq_functions = {
            function.get("address", "").lower()
            for function in irq_manifest.get("functions", [])}
        irq_manifest["functions"].extend(
            function for function in irq_secondary.get("functions", [])
            if function.get("address", "").lower() not in known_irq_functions)
        irq_manifest.setdefault("analysis", {}).update(irq_secondary.get("analysis", {}))
        # c000600 is a BIOS-installed vector stub; c001cba/c001cc0/c001004
        # are its runtime-patched handler. Its callees ac001284 and c000620
        # are already compiled in the BIOS translation.
        irq_internal = {"0x0c000600", "0x0c001000", "0x0c001cba", "0x0c001cc0", "0x0c001004"}
        irq_external = ["0xac001284", "0x0c000620"]
        irq_manifest["functions"] = [
            function for function in irq_manifest.get("functions", [])
            if function.get("address", "").lower() in irq_internal]
        irq_manifest["analysis"] = {
            key: value for key, value in irq_manifest.get("analysis", {}).items()
            if key.lower() in irq_internal}
        irq_manifest["external_functions"] = irq_external
        irq_manifest["instructions_analyzed"] = sum(
            function.get("length", 0) // 2 for function in irq_manifest["functions"])
        bios_ram_irq_manifest_path.write_text(json.dumps(irq_manifest, indent=2) + "\n",
                                              encoding="utf-8")
    copied_code_manifests = []
    for index, entry in enumerate((BIOS_COPIED_CODE_ENTRY, BIOS_COPIED_CODE_ENTRY_2,
                                   BIOS_COPIED_CODE_ENTRY_3, BIOS_COPIED_CODE_ENTRY_4,
                                   BIOS_COPIED_CODE_ENTRY_5, BIOS_COPIED_CODE_ENTRY_6,
                                   BIOS_COPIED_CODE_ENTRY_7, BIOS_COPIED_CODE_ENTRY_8,
                                   BIOS_COPIED_CODE_ENTRY_9, BIOS_COPIED_CODE_ENTRY_10,
                                   BIOS_COPIED_CODE_ENTRY_11, BIOS_COPIED_CODE_ENTRY_12,
                                   BIOS_COPIED_CODE_ENTRY_13)):
        manifest_path = generated_dir / f"naomi_bios_copied_code_{index}.json"
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(bios_copied_code_path), "--base", hex(BIOS_COPIED_CODE_BASE),
             "--entry", hex(entry), "--set-id", "naomi_bios_copied_code",
             "--output", str(manifest_path), "--max-instructions", "100000"])
        copied_code_manifests.append(json.loads(manifest_path.read_text(encoding="utf-8")))
    copied_code_merged = {
        "set_id": "naomi_bios_copied_code",
        "base_address": hex(BIOS_COPIED_CODE_BASE),
        "entry": hex(BIOS_COPIED_CODE_ENTRY),
        "functions": [], "resolved_calls": {}, "analysis": {},
        "unresolved": [], "instructions_analyzed": 0,
        "truncated_by_limit": False,
    }
    copied_seen_functions = set()
    for manifest in copied_code_manifests:
        for function in manifest["functions"]:
            if function["address"] not in copied_seen_functions:
                copied_code_merged["functions"].append(function)
                copied_seen_functions.add(function["address"])
        copied_code_merged["resolved_calls"].update(manifest.get("resolved_calls", {}))
        copied_code_merged["analysis"].update(manifest.get("analysis", {}))
        copied_code_merged["unresolved"].extend(manifest.get("unresolved", []))
        copied_code_merged["instructions_analyzed"] += manifest.get("instructions_analyzed", 0)
        copied_code_merged["truncated_by_limit"] |= manifest.get("truncated_by_limit", False)
    bios_copied_code_manifest_path.write_text(json.dumps(copied_code_merged, indent=2) + "\n", encoding="utf-8")
    merged = {
        "set_id": "mvsc2u_bios",
        "base_address": hex(BIOS_FRAGMENT_BASE),
        "entry": hex(BIOS_FIRST_ENTRY),
        "functions": [],
        "resolved_calls": {},
        "analysis": {},
        "unresolved": [],
        "instructions_analyzed": 0,
        "truncated_by_limit": False,
    }
    seen_functions = set()
    for manifest in bios_manifests:
        for function in manifest["functions"]:
            if function["address"] not in seen_functions:
                merged["functions"].append(function)
                seen_functions.add(function["address"])
        merged["resolved_calls"].update(manifest.get("resolved_calls", {}))
        merged["analysis"].update(manifest.get("analysis", {}))
        merged["unresolved"].extend(manifest.get("unresolved", []))
        merged["instructions_analyzed"] += manifest.get("instructions_analyzed", 0)
        merged["truncated_by_limit"] |= manifest.get("truncated_by_limit", False)
    bios_manifest_path.write_text(json.dumps(merged, indent=2) + "\n", encoding="utf-8")
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(image_path), "--manifest", str(game_manifest_path),
         "--output", str(source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_fragment_path), "--manifest", str(bios_manifest_path),
         "--output", str(bios_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_image_path), "--manifest", str(bios_reset_manifest_path),
         "--output", str(bios_reset_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_image_path), "--manifest", str(bios_reset_p1_manifest_path),
         "--output", str(bios_reset_p1_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bootstrap_path), "--manifest", str(bootstrap_manifest_path),
         "--output", str(bootstrap_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_ram_path), "--manifest", str(bios_ram_manifest_path),
         "--output", str(bios_ram_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_ram_vector_path), "--manifest", str(bios_ram_vector_manifest_path),
         "--output", str(bios_ram_vector_source_path)])
    if bios_ram_irq_image_path.exists():
        run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
             "--image", str(bios_ram_irq_image_path), "--manifest", str(bios_ram_irq_manifest_path),
             "--output", str(bios_ram_irq_source_path)])
    run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
         "--image", str(bios_copied_code_path), "--manifest", str(bios_copied_code_manifest_path),
         "--output", str(bios_copied_code_source_path)])
    translated_sources = [bios_source_path, bios_reset_source_path,
                          bios_reset_p1_source_path,
                          bootstrap_source_path, bios_ram_source_path,
                          bios_ram_vector_source_path, bios_ram_irq_source_path,
                          bios_copied_code_source_path]
    if args.boot_ram_image:
        boot_ram_image = args.boot_ram_image.expanduser().resolve()
        if boot_ram_image.stat().st_size != 0x400000:
            raise SystemExit(f"boot RAM image must be exactly 4 MiB: {boot_ram_image}")
        if boot_ram_image != boot_ram_image_path:
            shutil.copyfile(boot_ram_image, boot_ram_image_path)
        run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
             "--image", str(boot_ram_image_path), "--base", "0x0c020000",
             "--entry", "0x0c020000", "--set-id", "mvsc2u_boot_ram",
             "--output", str(boot_ram_manifest_path), "--max-instructions",
             str(args.max_instructions)])
        # The BIOS reaches game code from dynamically patched interrupt and
        # callback paths, so analyze observed guest entries as extra roots.
        # Merge their reachable code into the same generated module.
        boot_ram_extra_manifests = []
        for entry, entry_name in GAME_DYNAMIC_BOOT_ENTRIES:
            extra_manifest_path = generated_dir / f"mvsc2u_boot_ram_{entry_name}_functions.json"
            run([sys.executable, str(ROOT / "tools/naomi/analyze_sh4.py"),
                 "--image", str(boot_ram_image_path), "--base", "0x0c020000",
                 "--entry", hex(entry), "--set-id", "mvsc2u_boot_ram",
                 "--output", str(extra_manifest_path), "--max-instructions",
                 str(args.max_instructions)])
            boot_ram_extra_manifests.append(json.loads(
                extra_manifest_path.read_text(encoding="utf-8")))
        boot_ram_manifest = json.loads(boot_ram_manifest_path.read_text(encoding="utf-8"))
        boot_ram_functions = {
            function["address"].lower(): function
            for function in boot_ram_manifest.get("functions", [])}
        unresolved = boot_ram_manifest.setdefault("unresolved", [])
        unresolved_keys = {
            (item.get("source"), item.get("target"), item.get("kind"))
            for item in unresolved}
        for extra_manifest in boot_ram_extra_manifests:
            for function in extra_manifest.get("functions", []):
                key = function["address"].lower()
                existing = boot_ram_functions.get(key)
                if existing is None:
                    boot_ram_manifest["functions"].append(function)
                    boot_ram_functions[key] = function
                    continue
                if existing["length"] != function["length"]:
                    raise SystemExit(f"conflicting AOT ranges for SH-4 function {key}")
                reachable = set(existing.get("reachable_addrs", []))
                reachable.update(function.get("reachable_addrs", []))
                existing["reachable_addrs"] = sorted(
                    reachable, key=lambda value: int(value, 0))
            boot_ram_manifest.setdefault("resolved_calls", {}).update(
                extra_manifest.get("resolved_calls", {}))
            boot_ram_manifest.setdefault("analysis", {}).update(
                extra_manifest.get("analysis", {}))
            for item in extra_manifest.get("unresolved", []):
                key = (item.get("source"), item.get("target"), item.get("kind"))
                if key not in unresolved_keys:
                    unresolved.append(item)
                    unresolved_keys.add(key)
            boot_ram_manifest["truncated_by_limit"] = (
                boot_ram_manifest.get("truncated_by_limit", False) or
                extra_manifest.get("truncated_by_limit", False))
        boot_ram_manifest["instructions_analyzed"] = len({
            address.lower()
            for function in boot_ram_manifest["functions"]
            for address in function.get("reachable_addrs", [])})
        boot_ram_manifest["dynamic_root_evidence"] = [
            {
                "address": f"0x{entry:08x}",
                "name": entry_name,
                "origin": ("bounded-static-braf-table" if entry in BOUNDED_BRAF_TABLE_ROOTS
                           else "runtime-observed-target"),
                "source": ("BRAF at 0x0c035f78; CMP/HS bounds selector to 0..21; "
                           "signed halfword entries at 0x0c035f84"
                           if entry in BOUNDED_BRAF_TABLE_ROOTS else
                           "guest indirect target observed in a prior bounded smoke"),
            }
            for entry, entry_name in GAME_DYNAMIC_BOOT_ENTRIES
        ]
        boot_ram_manifest_path.write_text(
            json.dumps(boot_ram_manifest, indent=2) + "\n", encoding="utf-8")
        run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
             "--image", str(boot_ram_image_path), "--manifest", str(boot_ram_manifest_path),
             "--output", str(boot_ram_source_path)])
        verify_generated_boot_roots(boot_ram_manifest_path, boot_ram_source_path)
        translated_sources.append(boot_ram_source_path)
        # Keep the real-BIOS path's RAM snapshot AOT separate from a prefixed
        # ROM AOT used only by the optional REIOS diagnostic boot.
        reios_manifest = json.loads(game_manifest_path.read_text(encoding="utf-8"))
        reios_manifest["set_id"] = "mvsc2u_reios"
        reios_manifest_path.write_text(
            json.dumps(reios_manifest, indent=2) + "\n", encoding="utf-8")
        run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
             "--image", str(image_path), "--manifest", str(reios_manifest_path),
             "--output", str(reios_source_path), "--symbol-prefix", "reios"])
        translated_sources.append(reios_source_path)
    else:
        translated_sources.insert(0, source_path)
    for entry, name in GAME_DYNAMIC_ROM_ENTRIES:
        manifest_path = rom_runtime_manifest_paths[entry]
        source_path_for_root = rom_runtime_source_paths[entry]
        run([sys.executable, str(ROOT / "tools/naomi/recompile_sh4.py"),
             "--image", str(image_path), "--manifest", str(manifest_path),
             "--output", str(source_path_for_root),
             "--symbol-prefix", name])
        verify_generated_rom_roots(manifest_path, source_path_for_root, entry)
        translated_sources.append(source_path_for_root)
    run(["cmake", "-S", str(ROOT), "-B", str(build_dir),
         f"-DMULTIARCADERECOMP_NAOMI_TRANSLATED_SOURCES={';'.join(map(str, translated_sources))}",
         f"-DMULTIARCADERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION={'ON' if args.boot_ram_image else 'OFF'}",
         f"-DMULTIARCADERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION={'ON' if args.boot_ram_image else 'OFF'}",
         "-DMULTIARCADERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS=ON"])
    run(["nice", "-n", "19", "cmake", "--build", str(build_dir), "-j4", "--target",
         "multiarcaderecomp_naomi", "multiarcaderecomp"])
    print(f"Built {build_dir / 'multiarcaderecomp_naomi.so'}")
    print("The game, Naomi BIOS reset entry, and BIOS service entries are translated. Hardware startup and PVR rendering remain runtime work.")
    if args.smoke_instructions:
        if args.smoke_instructions < 1:
            raise SystemExit("--smoke-instructions must be positive")
        try:
            run([sys.executable, str(ROOT / "tools/naomi/smoke_mvsc2_boot.py"),
                 str(rom_zip), str(bios_zip), "--binary", str(build_dir / "multiarcaderecomp"),
                 "--manifest", str(boot_ram_manifest_path), "--instructions",
                 str(args.smoke_instructions)])
        except subprocess.CalledProcessError as exc:
            return exc.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
