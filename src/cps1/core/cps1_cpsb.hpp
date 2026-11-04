#pragma once

#include <cstdint>

namespace mamerecomp::cps1::detail {

std::uint16_t cpsb_read(unsigned offset);
void cpsb_write(unsigned offset, std::uint16_t value);
void cpsb_reset();

} // namespace mamerecomp::cps1::detail
