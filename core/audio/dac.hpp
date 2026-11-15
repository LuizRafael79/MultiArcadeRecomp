#pragma once

#include <cstdint>

namespace multiarcaderecomp::audio {

// Sample-and-hold 8-bit DAC output, centered around the unsigned midpoint.
class Dac8 {
public:
    explicit Dac8(std::uint8_t initial = 0x80) : m_value(initial) {}

    void write(std::uint8_t value) noexcept { m_value = value; }
    std::uint8_t value() const noexcept { return m_value; }
    std::int32_t signed_sample() const noexcept {
        return (static_cast<std::int32_t>(m_value) - 128) * 256;
    }

private:
    std::uint8_t m_value;
};

// First-order DC blocker used by board-level analog output paths.
class DcBlocker {
public:
    DcBlocker(std::int32_t feedback_numerator, std::int32_t feedback_denominator,
        std::int32_t initial_input = 0) noexcept;

    std::int32_t process(std::int32_t input) noexcept;
    void reset(std::int32_t initial_input = 0) noexcept;

private:
    std::int32_t m_feedback_numerator;
    std::int32_t m_feedback_denominator;
    std::int32_t m_previous_input;
    std::int32_t m_previous_output = 0;
};

} // namespace multiarcaderecomp::audio
