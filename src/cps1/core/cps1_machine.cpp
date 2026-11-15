#include "cps1_state.hpp"
#include "cps1_inputs.hpp"

#include <stdexcept>
#include <string>

namespace multiarcaderecomp::cps1::detail {

void configure_game(const Cps1GameDriver& driver) {
    if (!driver.machine || !driver.inputs || !driver.cps_b)
        throw std::runtime_error("incomplete CPS-1 machine/input profile for " + std::string(driver.id));
    if (!driver.machine->main_clock_hz || !driver.machine->pixel_clock_hz ||
        !driver.machine->htotal || !driver.machine->vtotal ||
        driver.machine->vblank_start >= driver.machine->vtotal)
        throw std::runtime_error("invalid timing profile for CPS-1 game " + std::string(driver.id));

    g_main_clock_hz = driver.machine->main_clock_hz;
    g_sound_clock_hz = driver.machine->sound_clock_hz;
    g_pixel_clock_hz = driver.machine->pixel_clock_hz;
    g_htotal = driver.machine->htotal;
    g_vtotal = driver.machine->vtotal;
    g_vblank_start = driver.machine->vblank_start;
    g_active_cps_b_profile = driver.cps_b;
    g_gfxram_is_executable = driver.graphics_ram_is_executable;
    g_reverse_sprite_order = driver.reverse_sprite_order;
    g_next_vblank = cps1_vblank_cycles();
    configure_inputs(driver.inputs->player_port_default, driver.inputs->dip_port_defaults,
        driver.inputs->extra_buttons_port_default, driver.inputs->input2_port_default,
        driver.inputs->input3_port_default);
}

std::int64_t cps1_frame_cycles() {
    return static_cast<std::int64_t>(g_main_clock_hz) * g_htotal * g_vtotal / g_pixel_clock_hz;
}

std::int64_t cps1_vblank_cycles() {
    return static_cast<std::int64_t>(g_main_clock_hz) * g_htotal * g_vblank_start / g_pixel_clock_hz;
}

} // namespace multiarcaderecomp::cps1::detail
