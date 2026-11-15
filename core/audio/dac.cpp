#include "dac.hpp"

namespace multiarcaderecomp::audio {

DcBlocker::DcBlocker(std::int32_t feedback_numerator, std::int32_t feedback_denominator,
    std::int32_t initial_input) noexcept
    : m_feedback_numerator(feedback_numerator),
      m_feedback_denominator(feedback_denominator),
      m_previous_input(initial_input) {}

std::int32_t DcBlocker::process(std::int32_t input) noexcept {
    if (m_feedback_denominator == 0) return input;
    m_previous_output = input - m_previous_input
        + (m_previous_output * m_feedback_numerator) / m_feedback_denominator;
    m_previous_input = input;
    return m_previous_output;
}

void DcBlocker::reset(std::int32_t initial_input) noexcept {
    m_previous_input = initial_input;
    m_previous_output = 0;
}

} // namespace multiarcaderecomp::audio
