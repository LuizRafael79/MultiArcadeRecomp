#include "zn_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace multiarcaderecomp::zn {

void ZnMachineRuntime::set_input_state(
    const multiarcaderecomp::runtime::ArcadeInput &input)
{
    using Input = multiarcaderecomp::runtime::ArcadeInput;
    const auto active_low_player = [&](unsigned player) {
        const uint16_t pressed = input.players_pressed[player];
        uint8_t port = 0xff;
        if (pressed & Input::up) port &= static_cast<uint8_t>(~0x01u);
        if (pressed & Input::down) port &= static_cast<uint8_t>(~0x02u);
        if (pressed & Input::left) port &= static_cast<uint8_t>(~0x04u);
        if (pressed & Input::right) port &= static_cast<uint8_t>(~0x08u);
        if (pressed & Input::button1) port &= static_cast<uint8_t>(~0x10u);
        if (pressed & Input::button2) port &= static_cast<uint8_t>(~0x20u);
        if (pressed & Input::button3) port &= static_cast<uint8_t>(~0x40u);
        return port;
    };
    const auto kick_port = [&](unsigned player) {
        const uint16_t pressed = input.players_pressed[player];
        uint8_t port = 0xff;
        if (pressed & Input::button4) port &= static_cast<uint8_t>(~0x01u);
        if (pressed & Input::button5) port &= static_cast<uint8_t>(~0x02u);
        if (pressed & Input::button6) port &= static_cast<uint8_t>(~0x04u);
        return port;
    };
    const uint8_t p1 = active_low_player(0);
    const uint8_t p2 = active_low_player(1);
    uint8_t system = static_cast<uint8_t>(0xffu
        & ((input.players_pressed[0] & Input::start) ? 0xfeu : 0xffu)
        & ((input.players_pressed[1] & Input::start) ? 0xfdu : 0xffu)
        & ((input.coins_pressed & 1u) ? 0xefu : 0xffu)
        & ((input.coins_pressed & 2u) ? 0xdfu : 0xffu));
    const uint8_t service_bits = static_cast<uint8_t>(input.service_switches |
        (input.service_pressed ? 0x01u : 0u));
    uint8_t service = static_cast<uint8_t>(~service_bits);

    if (zn2_) {
        const uint8_t kick1 = kick_port(0);
        const uint8_t kick2 = kick_port(1);
        const uint8_t p3 = static_cast<uint8_t>(0x8fu | ((kick1 & 7u) << 4));
        const uint8_t p4 = static_cast<uint8_t>(0x8fu | ((kick2 & 7u) << 4));
        if (std::getenv("MULTIARCADERECOMP_ZN_TRACE_INPUTS") &&
            (input.players_pressed[0] & Input::start || input.coins_pressed))
            std::cerr << "ZN input start=" << bool(input.players_pressed[0] & Input::start)
                      << " coins=" << unsigned(input.coins_pressed)
                      << " system=0x" << std::hex << unsigned(system)
                      << " p1=0x" << unsigned(p1) << std::dec << '\n';
        zn2_->set_input_ports(p1, p2, service, system, p3, p4, kick1, kick2);
    } else if (zn1_) {
        uint8_t extra = 0xff;
        uint8_t extra_service = service;
        if (input.players_pressed[0] & Input::button4) extra &= static_cast<uint8_t>(~0x10u);
        if (input.players_pressed[1] & Input::button4) extra &= static_cast<uint8_t>(~0x20u);
        if (input.players_pressed[0] & Input::button5) extra_service &= static_cast<uint8_t>(~0x10u);
        if (input.players_pressed[1] & Input::button5) extra_service &= static_cast<uint8_t>(~0x20u);
        zn1_->set_input_ports(p1, p2, extra_service, system, extra, 0xff);
    }
}

ZnMachineRuntime::ZnMachineRuntime(std::string_view set,
                                   const std::filesystem::path &zip,
                                   const PsxAotEntry *entries,
                                   size_t entry_count,
                                   const multiarcaderecomp::z80::Z80Code *zn2_z80_code,
                                   multiarcaderecomp::chips::QSoundMode qsound_mode,
                                   std::span<const std::uint16_t> qsound_firmware,
                                   std::span<const multiarcaderecomp::dsp16::Dsp16ACompiledEntry> dsp_aot_program,
                                   multiarcaderecomp::dsp16::Dsp16ARunHandler dsp_aot_run)
    : roms_(load_romset(set, zip)),
      memory_(std::make_unique<multiarcaderecomp::psx::MachineMemory>(
          set == "bldyror2" ? multiarcaderecomp::psx::kZn1MemoryConfig :
          set == "tekken3" ? multiarcaderecomp::psx::kSystem12MemoryConfig :
                             multiarcaderecomp::psx::kZn2MemoryConfig)),
      entries_(entries), entry_count_(entry_count)
{
    profile_timing_ = std::getenv("MULTIARCADERECOMP_ZN_PROFILE_RUNTIME") != nullptr;
    const auto &bios = roms_.at("bios");
    if (!memory_->load_bios(bios.data(), bios.size()))
        throw std::runtime_error("invalid ZN BIOS region");
    if (set == "sfex2pa") {
        zn2_ = std::make_unique<CapcomZn2Board>(roms_);
        zn2_sound_ = std::make_unique<Zn2SoundCpu>(*zn2_, zn2_z80_code,
                                                    qsound_mode, qsound_firmware, dsp_aot_program, dsp_aot_run);
        zn2_->set_sound_command_callback(this, sync_zn2_sound_command);
        board_bus_ = zn2_->make_bus();
    } else if (set == "bldyror2") {
        zn1_ = std::make_unique<RaizingZn1Board>(roms_);
        zn1_sound_ = std::make_unique<Zn1SoundCpu>(*zn1_, roms_);
        board_bus_ = zn1_->make_bus();
    } else if (set == "tekken3") {
        system12_runtime_ = std::make_unique<multiarcaderecomp::namco::system12::System12Runtime>(roms_);
        board_bus_ = system12_runtime_->make_bus();
        memory_->set_dma_device_read(5, [this](uint32_t, size_t words) {
            return system12_runtime_->dma_rom_read(words);
        });
    } else {
        throw std::runtime_error("unsupported PSX-derived set profile");
    }
    memory_->set_board_bus(&board_bus_);
    bus_ = memory_->make_bus();
    psx_aot_reset(&cpu_);
    memory_->attach_cpu(&cpu_);
}

PsxAotExit ZnMachineRuntime::step()
{
    const PsxAotExit result = multiarcaderecomp::psx::execute_one(
        &cpu_, &bus_, entries_, entry_count_, &counters_);
    sync_secondary_processors();
    return result;
}

void ZnMachineRuntime::run_until(uint64_t target_cycle)
{
    const auto profile_start = profile_timing_
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    constexpr uint64_t scheduler_quantum = 4096;
    while (cpu_.cycles < target_cycle) {
        uint64_t slice_end = target_cycle;
        if (zn2_sound_) {
            const uint64_t sound_event = zn2_sound_->next_main_event_cycle();
            if (sound_event > cpu_.cycles)
                slice_end = std::min(slice_end, sound_event);
        } else {
            slice_end = std::min(slice_end, cpu_.cycles + scheduler_quantum);
        }
        (void)multiarcaderecomp::psx::execute_until(
            &cpu_, &bus_, entries_, entry_count_, &counters_, slice_end);
        sync_secondary_processors();
    }
    if (profile_timing_)
        profile_run_until_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - profile_start).count());
}

void ZnMachineRuntime::sync_zn2_sound_command(void *opaque, uint64_t main_cycles)
{
    auto *self = static_cast<ZnMachineRuntime *>(opaque);
    if (!self->zn2_sound_) return;
    self->zn2_sound_->run_to_main_cycles(main_cycles);
    self->zn2_sound_main_cycles_ = main_cycles;
}

void ZnMachineRuntime::sync_secondary_processors()
{
    if (system12_runtime_) system12_runtime_->sync_maincpu_cycles(cpu_.cycles);
    if (zn2_sound_ &&
        (cpu_.cycles - zn2_sound_main_cycles_ >= 4096 || zn2_->sound_pending())) {
        zn2_sound_->run_to_main_cycles(cpu_.cycles);
        zn2_sound_main_cycles_ = cpu_.cycles;
    }
    if (zn1_sound_ && cpu_.cycles - zn1_sound_main_cycles_ >= 4096) {
        const uint64_t elapsed = cpu_.cycles - zn1_sound_main_cycles_;
        // MAME's coh1002e configuration uses a 67.7376 MHz CXD8530CQ, which
        // yields 16.9344 MHz PSX execute cycles, and a 12 MHz MC68000.
        zn1_sound_cycle_remainder_ += elapsed * 12'000'000ull;
        const unsigned budget = static_cast<unsigned>(
            zn1_sound_cycle_remainder_ / 16'934'400ull);
        zn1_sound_cycle_remainder_ %= 16'934'400ull;
        if (budget) zn1_sound_->run(budget);
        zn1_sound_main_cycles_ = cpu_.cycles;
    }
}

} // namespace multiarcaderecomp::zn
