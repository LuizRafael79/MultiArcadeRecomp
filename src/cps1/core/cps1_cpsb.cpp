#include "cps1_cpsb.hpp"
#include "cps1_inputs.hpp"
#include "cps1_state.hpp"

#include <cstdlib>
#include <iostream>

#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>

extern "C" std::uint32_t g_current_guest_pc;

namespace multiarcaderecomp::cps1::detail {

std::uint16_t cpsb_read(unsigned offset) {
    const auto* profile = g_active_cps_b_profile;
    if (!profile) return 0xffff;
    if (offset == profile->signature_register) return profile->board_signature;
    if (offset == profile->multiply_result_low_register || offset == profile->multiply_result_high_register) {
        const std::uint32_t product = static_cast<std::uint32_t>(g_cps_b[profile->multiply_factor1_register])
            * g_cps_b[profile->multiply_factor2_register];
        return offset == profile->multiply_result_low_register ? static_cast<std::uint16_t>(product)
                           : static_cast<std::uint16_t>(product >> 16);
    }
    if (profile->input2_register != 0xff && offset == profile->input2_register) return read_extra_buttons();
    if (offset == profile->input3_register) return 0xff;
    return 0xffff;
}

void cpsb_write(unsigned offset, std::uint16_t value) {
    if (offset >= g_cps_b.size()) return;
    g_cps_b[offset] = value;
    if (!g_active_cps_b_profile || offset != g_active_cps_b_profile->layer_control_register
        || !std::getenv("MULTIARCADERECOMP_CPS1_TRACE_LAYER_CONTROL")) return;
    static std::uint16_t previous = 0xffff;
    if (value == previous) return;
    std::cerr << "[cps1] layer-control cycle=" << ng_cycles << " PC=$" << std::hex
              << g_current_guest_pc << " value=$" << value << " A5=$" << ng_cpu.a[5]
              << " source=$" << ng_r16(ng_cpu.a[5] + 0x4e) << std::dec << '\n';
    previous = value;
}

void cpsb_reset() { g_cps_b.fill(0); }

} // namespace multiarcaderecomp::cps1::detail
