/*
 * Naomi REIOS startup state follows Flycast's implementation:
 * core/reios/reios.cpp::reios_boot and reios_setup_naomi.
 * Flycast is distributed under GPL-2.0-or-later; see its LICENSE and retain
 * this attribution for this derived startup behavior.
 */
#include "naomi_reios_boot.hpp"

#include <algorithm>
#include <stdexcept>

namespace mamerecomp::naomi {
namespace {
constexpr std::uint32_t kProgramLengthOffset = 0x368;
constexpr std::uint32_t kProgramLoadAddress = 0x0c020000;
constexpr std::uint32_t kGameEntry = 0x0c021000;
constexpr std::uint32_t kReiosOpcode = 0x085b;

void install_syscall_stub(NaomiAddressSpace& memory, std::uint32_t hook_address,
                          std::uint32_t syscall_vector) {
    memory.write32(syscall_vector, hook_address);
    memory.write16(hook_address, kReiosOpcode);
}

void apply_reios_boot_prelude(NaomiAddressSpace& memory) {
    // Mirror core/reios/reios.cpp::reios_boot before the Naomi cart handoff.
    auto& main_ram = memory.mainRam();
    if (main_ram.size() < 0x10000u)
        throw std::runtime_error("Naomi main RAM is too small for the REIOS boot prelude");
    std::fill_n(main_ram.begin(), 0x10000u, 0xffu);

    install_syscall_stub(memory, 0x8c001000u, 0x8c0000b0u);
    install_syscall_stub(memory, 0x8c001002u, 0x8c0000b4u);
    install_syscall_stub(memory, 0x8c001004u, 0x8c0000b8u);
    install_syscall_stub(memory, 0x8c001006u, 0x8c0000bcu);
    install_syscall_stub(memory, 0x8c0010f0u, 0x8c0000c0u);
    install_syscall_stub(memory, 0x8c001008u, 0x8c0000e0u);

    // Flycast holds the ARM7 in its REIOS idle loop while the SH-4 boots.
    memory.write32(0x80800000u, 0xeafffffeu);
}

std::uint32_t load_le32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
}

NaomiBootResult reios_boot_naomi(std::span<const std::uint8_t> rom_board,
                                 NaomiAddressSpace& memory,
                                 Sh4Context& cpu) {
    if (rom_board.size() < kProgramLengthOffset + sizeof(std::uint32_t))
        return {false, 0, 0, "cartridge is too small to contain the Naomi boot header"};

    const auto program_size = load_le32(rom_board, kProgramLengthOffset);
    const auto ram_offset = static_cast<std::size_t>(kProgramLoadAddress - 0x0c000000);
    auto& main_ram = memory.mainRam();
    if (program_size == 0 || program_size > rom_board.size())
        return {false, 0, 0, "cartridge boot program length is outside the ROM board region"};
    if (ram_offset > main_ram.size() || program_size > main_ram.size() - ram_offset)
        return {false, 0, 0, "cartridge boot program does not fit in Naomi main RAM"};

    apply_reios_boot_prelude(memory);
    std::copy_n(rom_board.begin(), program_size, main_ram.begin() + ram_offset);

    // Observed REIOS Naomi initial register state. Keep these values together
    // so they can be compared directly with the HLE source and MAME boot traces.
    cpu.r = {kGameEntry, 0x0c01f820, 0xa0710004, 0x0c01f130,
             0x5bfccd08, 0xa05f7000, 0xa05f7008, 0x00000007,
             0x00000000, 0x00002000, 0xffffffff, 0x0c0e0000,
             0x00000000, 0x00000000, 0x00000000, 0x0cc00000};
    cpu.r_bank.fill(0);
    cpu.gbr = 0x0c2abcc0;
    cpu.sr = cpu.ssr = 0x60000000;
    cpu.spc = 0x0c041738;
    cpu.sgr = 0x0cbfffb0;
    cpu.dbr = 0x00000fff;
    cpu.vbr = 0x0c000000;
    cpu.pr = 0xac0195ee;
    cpu.fpul = 0x000001e0;
    cpu.macl = 0x00002000;
    cpu.t = true;
    cpu.q = false;
    cpu.m_bit = false;
    cpu.fpscr = 0x00040001;
    cpu.fpscr_pr = false; // FPSCR.PR is clear in the REIOS value 0x00040001.
    cpu.xf.fill(0.0f);
    cpu.frf.fill(0.0f);
    cpu.xf[0] = cpu.xf[5] = cpu.xf[10] = cpu.xf[15] = 1.0f;
    cpu.frf[0] = 1.0f;
    cpu.frf[1] = 2147483648.0f;
    cpu.frf[3] = 480.0f;
    cpu.frf[4] = 0.00001f;
    cpu.frf[6] = 0.00208333321f;
    cpu.frf[9] = 2147483648.0f;
    cpu.frf[10] = 1.0f;
    cpu.frf[11] = -1.0f;
    cpu.pc = kGameEntry;
    cpu.execution_stopped = false;
    cpu.unresolved_target = 0;

    return {true, kGameEntry, program_size, {}};
}

} // namespace mamerecomp::naomi
