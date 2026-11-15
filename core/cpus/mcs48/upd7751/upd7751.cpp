#include "upd7751.hpp"

#include <utility>

namespace multiarcaderecomp {

Upd7751::Upd7751(std::span<const std::uint8_t> program, Bus bus)
    : m_cpu(program, std::move(bus)) {}

} // namespace multiarcaderecomp
