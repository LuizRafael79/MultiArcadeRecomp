#pragma once

#include "boards/sega/common/sega_address_space.hpp"
#include "boards/sega/common/audio_sink.hpp"
#include "boards/sega/common/video_session.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "runtime/sega/board_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace mamerecomp::sega {

struct BoardRuntimeContext {
    const GameProfile& profile;
    const std::filesystem::path& rom_path;
    std::span<const std::uint8_t> maincpu_rom;
    std::span<const std::uint8_t> tile_rom;
    std::span<const std::uint8_t> sprite_rom;
    SegaVideoSignals& video_signals;
    SegaInputState& input_state;
    bool audio_enabled = false;
    bool window_enabled = false;
    bool audio_stereo_spread = false;
    std::uint8_t audio_volume = 100;
    std::optional<std::uint8_t> dsw1_override;
    std::optional<std::uint8_t> dsw2_override;
    SegaVideoBackend video_backend = SegaVideoBackend::Software;
    bool compare_cpu_video = false;
    const BoardRegistration& registration;
    std::function<std::uint64_t()> cycle_provider;
    std::function<std::uint32_t()> pc_provider;
    std::function<void()> invalidate_aot;
    SegaAudioSink audio_sink;
    std::function<bool(std::uint8_t)> sound_command_observer;
    // Bit N mutes YM2151 channel N+1. Currently used by System 16B diagnostics.
    std::uint8_t ym2151_muted_channels = 0;
    bool ym2151_suppress_slot0_channel2 = false;
    bool ym2151_suppress_slot3_channel5 = false;
    bool ym2151_suppress_slot4_channel6 = false;
    bool ym2151_suppress_slot5_channel7 = false;
};

struct BoardRuntimeHardware {
    // The dynamic board runtime must outlive all polymorphic objects and
    // callbacks created by its factory. Declare this first so it is destroyed last.
    std::shared_ptr<void> module_owner;
    std::shared_ptr<void> cpu_variant_owner;
    std::shared_ptr<void> board_state_owner;
    std::shared_ptr<void> sound_owner;
    std::vector<std::uint8_t> decoded_tiles;
    std::uint64_t tile_decode_ns = 0;
    std::unique_ptr<SegaAddressSpace> bus;
    std::unique_ptr<SegaBoardVideoRenderer> video;
    std::function<bool()> sound_active = [] { return false; };
    std::function<void(std::uint64_t)> advance_sound = [](std::uint64_t) {};
    std::function<void()> report_sound = [] {};
    std::function<void(std::uint64_t)> advance_devices_to_cycles = [](std::uint64_t) {};
    std::function<void()> signal_vblank = [] {};
    std::function<std::uint64_t()> maincpu_stall_until_cycles = [] { return std::uint64_t{0}; };
    std::function<void()> report_devices = [] {};
    std::function<void(std::ostream&)> report_debug_snapshot = [](std::ostream&) {};
    std::function<void()> on_maincpu_interrupt_accepted = [] {};
    // Interrupts requested by board devices (for example the 315-5195 mapper)
    // are separate from the runner's periodic vblank interrupt.
    std::function<unsigned()> pending_maincpu_irq_level = [] { return 0U; };
    std::function<void(unsigned)> acknowledge_maincpu_irq = [](unsigned) {};
    std::function<bool()> maincpu_reset_asserted = [] { return false; };
    // Board/game machine configurations may route vblank to a device MCU and
    // remove the main CPU's vblank interrupt entirely (System 18 I8751 sets).
    unsigned maincpu_vblank_irq_level = 4;
    std::function<std::uint16_t(std::uint32_t, std::uint16_t, bool)> transform_maincpu_opcode =
        [](std::uint32_t, std::uint16_t opcode, bool) { return opcode; };
    bool stateful_opcode_transform = false;
    std::function<std::uint64_t()> next_device_event = [] { return std::numeric_limits<std::uint64_t>::max(); };
    std::function<void(const std::filesystem::path&, std::ostream&)> write_board_video_state =
        [](const std::filesystem::path&, std::ostream&) {};
    std::function<void(std::ostream&)> append_video_signal_state = [](std::ostream&) {};
    std::function<void(std::ostream&)> report_video_signals = [](std::ostream&) {};
    std::function<void(std::ostream&)> report_board_video_state = [](std::ostream&) {};
    std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> trace_inputs;
    bool supports_video_profile = true;
    bool cpu_sprite_gpu_mixer = false;
    std::string video_backend_description;
};

BoardRuntimeHardware make_board_runtime(const BoardRuntimeContext& context);

struct BoardMemoryRegion {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t size = 0;
    std::uint32_t mirror_mask = 0;
    bool writable = false;
    bool device = false;
};
std::vector<BoardMemoryRegion> inspect_board_memory_map(const GameProfile& profile,
    std::span<const std::uint8_t> maincpu_rom);
std::vector<std::uint8_t> decode_board_tiles(const GameProfile& profile,
    std::span<const std::uint8_t> tile_rom);
std::vector<std::uint8_t> decrypt_board_initial_program(const GameProfile& profile,
    const std::filesystem::path& rom_path, std::span<const std::uint8_t> encrypted_program);

} // namespace mamerecomp::sega
