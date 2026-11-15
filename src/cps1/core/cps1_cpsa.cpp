#include "cps1_cpsa.hpp"
#include "cps1_state.hpp"
#include "cps1_video.hpp"

namespace multiarcaderecomp::cps1::detail {

void cpsa_write(unsigned offset, std::uint16_t value) {
    if (offset >= g_cps_a.size()) return;
    g_cps_a[offset] = value;
    if (offset == 5) copy_cps1_palette();
}

void cpsa_reset() { g_cps_a.fill(0); }

} // namespace multiarcaderecomp::cps1::detail
