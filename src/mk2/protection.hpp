#pragma once

#include "mk/tunit.hpp"

#include <cstdint>

namespace mamerecomp::mk2 {

// MK2's protection register wiring from mk2_state::init_mk2. This is distinct
// from the MK1 protection sequence and remains a machine/profile component.
class Protection final : public mamerecomp::mk::ProtectionInterface {
public:
    bool read(std::uint32_t address, std::uint16_t& value) const noexcept override;
    bool write(std::uint32_t address, std::uint16_t value) noexcept override;
private:
    std::uint16_t m_data{};
};

} // namespace mamerecomp::mk2
