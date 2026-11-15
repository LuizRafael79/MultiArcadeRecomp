#include "adsp2105.hpp"

namespace multiarcaderecomp::adsp2105 {

Block lookup(std::uint16_t) noexcept { return nullptr; }
Block lookup(std::uint16_t, std::uint32_t) noexcept { return nullptr; }
std::uint16_t entry_pc() noexcept { return 0; }

} // namespace multiarcaderecomp::adsp2105
