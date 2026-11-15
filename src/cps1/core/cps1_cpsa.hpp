#pragma once

#include <cstdint>

namespace multiarcaderecomp::cps1::detail {

void cpsa_write(unsigned offset, std::uint16_t value);
void cpsa_reset();

} // namespace multiarcaderecomp::cps1::detail
