#pragma once

#include <cstdint>

namespace mamerecomp::cps1::detail {

void cpsa_write(unsigned offset, std::uint16_t value);
void cpsa_reset();

} // namespace mamerecomp::cps1::detail
