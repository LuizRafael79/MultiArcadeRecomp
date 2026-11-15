#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace multiarcaderecomp {
// FD1094 separates encrypted opcode reads from plain data reads.
class Fd1094 {
public:
    explicit Fd1094(std::span<const uint8_t> key);
    uint8_t state() const noexcept;
    void change_state(uint16_t value) noexcept;
    void enter_irq() noexcept { irq_mode_ = true; }
    void leave_irq() noexcept { irq_mode_ = false; }
    uint16_t decrypt_opcode(uint32_t byte_address, uint16_t encrypted, bool vector_fetch = false) const;
    void decrypt_region(std::span<const uint8_t> encrypted, std::span<uint8_t> output, uint8_t state) const;
private:
    std::array<uint8_t, 0x2000> key_{};
    uint8_t state_ = 0;
    bool irq_mode_ = false;
};
} // namespace multiarcaderecomp
