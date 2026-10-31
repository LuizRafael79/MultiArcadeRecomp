#pragma once

#include "naomi_address_space.hpp"
#include "naomi_sh4_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mamerecomp::naomi {

struct NaomiBootResult {
    bool success = false;
    std::uint32_t entry = 0;
    std::size_t copied_bytes = 0;
    std::string error;
};

// Flycast REIOS-style Naomi cartridge cold boot. The BIOS ROM is already
// mapped in NaomiAddressSpace; this copies the game image to main RAM and
// initializes the SH-4 registers as REIOS does.
NaomiBootResult reios_boot_naomi(std::span<const std::uint8_t> rom_board,
                                 NaomiAddressSpace& memory,
                                 Sh4Context& cpu);

} // namespace mamerecomp::naomi
