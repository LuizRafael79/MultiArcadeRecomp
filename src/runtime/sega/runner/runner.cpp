#include "runtime/sega/runner/runner.hpp"
#include "runtime/sega/runner/generated_board_module.hpp"
#include "runtime/sega/board_runtime.hpp"
#include "runtime/sega/board_registry.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "boards/sega/common/sega_address_space.hpp"
#include "boards/sega/common/video_output.hpp"
#include "runtime/sega/presentation/sega_presenter.hpp"
#include "runtime/sega/presentation/sega_audio_output.hpp"
#include "runtime/sega/debugger/m68k_debugger.hpp"
#include "cpus/m68000/runtime/recomp_dispatch.h"
#include "cpus/m68000/runtime/musashi_fallback.h"
#include "cpus/m68000/runtime/recomp_bus.h"
#include "native_z80.h"
#ifdef MULTIARCADERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
extern "C" void multiarcaderecomp_debug_write_adapter(std::uint32_t, std::uint8_t, std::uint32_t);
extern "C" void multiarcaderecomp_debug_read_adapter(std::uint32_t, std::uint8_t, std::uint32_t);
#endif
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace multiarcaderecomp::sega {
namespace {
std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 3 >= bytes.size()) throw std::runtime_error("reset vectors are truncated");
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("could not open binary input: " + path.string());
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("could not determine input size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("failed reading binary input: " + path.string());
    return bytes;
}
}

int run_game(const multiarcaderecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    std::uint64_t cycle_budget, const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::uint64_t>& frame_out_vblank,
    const std::optional<std::filesystem::path>& video_state_out,
    const std::optional<std::filesystem::path>& runtime_entry_out,
    const std::optional<std::filesystem::path>& nvram_in, bool mame_nvram_in, bool video_profile_enabled,
    bool aot_profile_enabled, bool frame_limit_enabled,
    multiarcaderecomp::SegaVideoBackend video_backend, bool compare_cpu_video,
    const std::vector<InputEvent>& input_events,
    std::optional<std::uint8_t> dsw1_override, std::optional<std::uint8_t> dsw2_override, bool debugger_enabled,
    const std::optional<std::filesystem::path>& cpu_trace_out,
    std::uint64_t cpu_trace_start, std::uint64_t cpu_trace_end, bool window_enabled, bool audio_enabled,
    bool audio_stereo_spread, std::uint8_t audio_volume, multiarcaderecomp::SegaDisplayMode display_mode,
    std::uint8_t window_scale, std::uint8_t ym2151_muted_channels,
    bool ym2151_suppress_slot0_channel2,
    bool ym2151_suppress_slot3_channel5,
    bool ym2151_suppress_slot4_channel6,
    bool ym2151_suppress_slot5_channel7,
    bool arranged_bgm,
    const std::vector<multiarcaderecomp::sega::GameInputBinding>& game_input_bindings) {
    bool aot_state_valid = true;
    std::unique_ptr<GeneratedBoardModule> board_module;
    const auto* registration = find_board(multiarcaderecomp::board_family(profile));
    if (!registration) throw std::runtime_error("Sega profile has no registered board runtime");
    board_module = std::make_unique<GeneratedBoardModule>(*registration, profile.id.data());
    if (arranged_bgm && !board_module->has_game_audio_api())
        throw std::runtime_error("--arranged-bgm requires a game module with a sound-command API");
    multiarcaderecomp::z80::SetZ80Code(board_module ? board_module->z80_code() : nullptr);
    multiarcaderecomp::z80::z80native::SetHostCycleProvider([] { return ng_cycles; });
    std::unique_ptr<multiarcaderecomp::SegaPresenter> presenter;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    if (window_enabled) {
        presenter = std::make_unique<multiarcaderecomp::SegaPresenter>(
            std::string(profile.title) + " - MultiArcadeRecomp", game_input_bindings, frame_limit_enabled, video_profile_enabled,
            registration->refresh_rate_num, registration->refresh_rate_den);
        if (!presenter->configure_display(display_mode, window_scale))
            throw std::runtime_error(std::string("SDL display configuration failed: ") + SDL_GetError());
    }
#else
    if (window_enabled) throw std::runtime_error("--window requires SDL3 development files; reconfigure with SDL3 installed");
#endif
    multiarcaderecomp::SegaVideoSignals video_signals;
    multiarcaderecomp::SegaInputState input_state;
    std::unique_ptr<multiarcaderecomp::SegaAudioOutput> audio_output;
    multiarcaderecomp::SegaAudioSink audio_sink;
    if (audio_enabled && window_enabled) {
        audio_output = std::make_unique<multiarcaderecomp::SegaAudioOutput>();
        if (arranged_bgm)
            audio_output->load_external_tracks("/home/luiz/Documentos/GoldenAxeArranged");
        audio_sink = [output = audio_output.get()](std::span<const std::int16_t> samples, std::uint32_t rate) {
            output->submit(samples, rate);
        };
    }
    multiarcaderecomp::sega::BoardRuntimeContext runtime_context{
        profile, zip_path, {}, {}, {}, video_signals, input_state,
        audio_enabled, window_enabled, audio_stereo_spread, audio_volume,
        dsw1_override, dsw2_override,
        video_backend, compare_cpu_video,
        *registration,
        [] { return ng_cycles < 0 ? std::uint64_t{0} : static_cast<std::uint64_t>(ng_cycles); },
        [] { return ng_cpu.pc; },
        [&aot_state_valid] { aot_state_valid = false; },
        std::move(audio_sink),
        [output = audio_output.get(), &board_module, arranged_bgm](std::uint8_t command) {
            if (!arranged_bgm) return false;
            const multiarcaderecomp::sega::GameAudioHostApi host{
                output,
                [](void* context, const char* track) {
                    return context && static_cast<multiarcaderecomp::SegaAudioOutput*>(context)->play_track(track);
                },
                [](void* context, const char* track) {
                    return context && static_cast<multiarcaderecomp::SegaAudioOutput*>(context)->play_one_shot(track);
                },
                [](void* context) {
                    if (context) static_cast<multiarcaderecomp::SegaAudioOutput*>(context)->stop_track();
                },
                [](void* context, float gain) {
                    if (context) static_cast<multiarcaderecomp::SegaAudioOutput*>(context)->set_track_gain(gain);
                }};
            return board_module->on_sound_command(command, &host);
        },
        ym2151_muted_channels, ym2151_suppress_slot0_channel2,
        ym2151_suppress_slot3_channel5, ym2151_suppress_slot4_channel6,
        ym2151_suppress_slot5_channel7
    };

    const auto maincpu = multiarcaderecomp::load_maincpu_roms(profile, zip_path);
    std::set<std::uint32_t> runtime_entry_candidates;
    if (runtime_entry_out) {
        std::ifstream input(*runtime_entry_out);
        if (input) {
            std::string line;
            while (std::getline(input, line)) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '#') continue;
                std::size_t parsed = 0;
                unsigned long address = 0;
                try {
                    address = std::stoul(line.substr(first), &parsed, 16);
                } catch (const std::exception&) {
                    throw std::runtime_error("invalid runtime 68000 entry in " + runtime_entry_out->string() + ": " + line);
                }
                if (parsed == 0 || address >= maincpu.size() || (address & 1U) != 0)
                    throw std::runtime_error("invalid runtime 68000 entry in " + runtime_entry_out->string() + ": " + line);
                runtime_entry_candidates.insert(static_cast<std::uint32_t>(address));
            }
            if (!input.eof()) throw std::runtime_error("failed reading runtime 68000 entries: " + runtime_entry_out->string());
        }
    }
    const auto graphics_load_start = std::chrono::steady_clock::now();
    const auto graphics = multiarcaderecomp::load_graphics_roms(profile, zip_path);
    const auto graphics_load_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - graphics_load_start).count();

    runtime_context.maincpu_rom = maincpu;
    runtime_context.tile_rom = graphics.tiles;
    runtime_context.sprite_rom = graphics.sprites;
    auto board_runtime = multiarcaderecomp::sega::make_board_runtime(runtime_context);
    const bool stateful_opcode_transform = board_runtime.stateful_opcode_transform;
    const auto reset_vector_word = [&](std::uint32_t address) {
        const auto encrypted = static_cast<std::uint16_t>((maincpu[address] << 8) | maincpu[address + 1]);
        return board_runtime.transform_maincpu_opcode(address, encrypted, true);
    };
    const std::uint32_t initial_sp = stateful_opcode_transform
        ? (std::uint32_t(reset_vector_word(0)) << 16) | reset_vector_word(2)
        : read_be32(maincpu, 0);
    const std::uint32_t initial_pc = stateful_opcode_transform
        ? (std::uint32_t(reset_vector_word(4)) << 16) | reset_vector_word(6)
        : read_be32(maincpu, 4);
    if ((initial_pc & 1U) != 0 || initial_pc + 2 > maincpu.size() || (initial_sp & 1U) != 0) {
        throw std::runtime_error("invalid 68000 reset vectors");
    }

    const auto& sound_active = board_runtime.sound_active;
    const auto& advance_sound = board_runtime.advance_sound;
    const auto tile_decode_ns = board_runtime.tile_decode_ns;
    const auto& decoded_tiles = board_runtime.decoded_tiles;
    auto& bus_owner = board_runtime.bus;
    auto& bus = *bus_owner;
    if (nvram_in) {
        auto nvram = read_binary_file(*nvram_in);
        if (mame_nvram_in) {
            if ((nvram.size() & 1U) != 0) throw std::runtime_error("MAME NVRAM image must contain whole 16-bit words");
            for (std::size_t i = 0; i < nvram.size(); i += 2) std::swap(nvram[i], nvram[i + 1]);
        }
        bus.load_ram("nvram", nvram);
        std::cout << "Loaded 0x" << std::hex << nvram.size() << " bytes of NVRAM from "
                  << *nvram_in << (mame_nvram_in ? " (MAME word order converted)" : "") << std::dec << '\n';
    }
    const M68kBusCallbacks maincpu_bus{
        &bus,
        &multiarcaderecomp::sega_bus_read8_callback,
        &multiarcaderecomp::sega_bus_read16_callback,
        &multiarcaderecomp::sega_bus_write8_callback,
        &multiarcaderecomp::sega_bus_write16_callback,
    };
    multiarcaderecomp_m68k_bus_bind(&maincpu_bus);
    const bool debug_hooks_enabled = debugger_enabled || cpu_trace_out.has_value();
    multiarcaderecomp_m68k_bus_set_read_observer(debug_hooks_enabled ? multiarcaderecomp_debug_read_adapter : nullptr);
    multiarcaderecomp_m68k_bus_set_write_observer(debug_hooks_enabled ? multiarcaderecomp_debug_write_adapter : nullptr);
    ng_cpu = {};
    ng_cpu.a[7] = initial_sp;
    ng_cpu.pc = initial_pc;
    ng_cpu.s = 1;
    ng_cpu.ipl = 7;
    ng_cycles = 0;
    ng_icount = 0;
    const std::uint64_t maincpu_clock_hz = registration->maincpu_clock_hz;
    const std::uint64_t frame_rate_num = registration->refresh_rate_num;
    const std::uint64_t frame_rate_den = registration->refresh_rate_den;
    const std::uint64_t sound_sync_period = maincpu_clock_hz / 1000; // Bound sound scheduling latency to 1 ms.
    std::uint64_t next_vblank = 0;
    std::uint64_t next_sound_sync = sound_active() ? sound_sync_period : std::numeric_limits<std::uint64_t>::max();
    std::uint64_t elapsed_vblanks = 0;
    multiarcaderecomp::configure_m68k_debugger(debugger_enabled, &bus, &elapsed_vblanks,
        cpu_trace_out ? &*cpu_trace_out : nullptr, cpu_trace_start, cpu_trace_end);
    std::uint64_t accepted_vblanks = 0;
    std::uint64_t accepted_mapper_irqs = 0;
    std::uint32_t frame_remainder = 0;
    const auto schedule_next_vblank = [&]() {
        const std::uint64_t numerator = maincpu_clock_hz * frame_rate_den + frame_remainder;
        next_vblank += numerator / frame_rate_num;
        frame_remainder = static_cast<std::uint32_t>(numerator % frame_rate_num);
    };
    schedule_next_vblank();
    bool vblank_pending = false;
    bool vblank_frame_written = false;
    bool maincpu_reset_seen = false;
    bool window_open = true;
    auto& board_video_renderer = board_runtime.video;
    const auto render_current_frame = [&](std::vector<multiarcaderecomp::SegaVideoFrame>* stages = nullptr,
        multiarcaderecomp::SegaVideoProfile* profile = nullptr) -> multiarcaderecomp::SegaVideoFrame {
        const auto start = std::chrono::steady_clock::now();
        auto frame = board_video_renderer->render(stages, profile);
        if (profile) {
            profile->render_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count());
        }
        return frame;
    };

    struct LiveVideoStats {
        std::uint64_t frames = 0;
        std::uint64_t render_steady_ns = 0;
        std::uint64_t render_max_steady_ns = 0;
        std::uint64_t gpu_setup_ns = 0;
        std::uint64_t present_wait_ns = 0;
        std::uint64_t present_max_ns = 0;
        std::uint64_t frame_interval_count = 0;
        std::uint64_t frame_interval_ns = 0;
        std::uint64_t frame_interval_max_ns = 0;
        std::array<std::uint64_t, 2> focus_interval_count{};
        std::array<std::uint64_t, 2> focus_interval_ns{};
        std::array<std::uint64_t, 2> focus_interval_max_ns{};
        std::uint64_t simulation_interval_count = 0;
        std::uint64_t simulation_interval_ns = 0;
        std::uint64_t simulation_interval_max_ns = 0;
    } live_video_stats;
    auto last_vblank_wall_time = std::chrono::steady_clock::time_point{};
    auto last_simulation_start = std::chrono::steady_clock::time_point{};
    const auto compare_frame_with_cpu = [&](const multiarcaderecomp::SegaVideoFrame& frame, std::string_view context) {
        if (!compare_cpu_video || video_backend != multiarcaderecomp::SegaVideoBackend::OpenGL) return;
        multiarcaderecomp::SegaVideoFrame cpu_frame = board_video_renderer->render_cpu_reference();
        std::uint64_t index_differences = 0;
        std::uint64_t priority_differences = 0;
        std::uint64_t shadow_differences = 0;
        for (std::size_t pixel = 0; pixel < frame.palette_indices.size(); ++pixel) {
            index_differences += frame.palette_indices[pixel] != cpu_frame.palette_indices[pixel];
            priority_differences += frame.tile_priorities[pixel] != cpu_frame.tile_priorities[pixel];
            shadow_differences += frame.shadow_pixels[pixel] != cpu_frame.shadow_pixels[pixel];
        }
        std::cout << board_video_renderer->comparison_label();
        if (!context.empty()) std::cout << " " << context;
        std::cout << ": palette-index=" << index_differences
                  << ", priority=" << priority_differences << ", shadow=" << shadow_differences
                  << " differing pixels\n";
    };
    const auto advance_vblank = [&]() {
        if (presenter && video_profile_enabled) {
            const auto now = std::chrono::steady_clock::now();
            if (last_vblank_wall_time != std::chrono::steady_clock::time_point{}) {
                const auto interval = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - last_vblank_wall_time).count());
                ++live_video_stats.frame_interval_count;
                live_video_stats.frame_interval_ns += interval;
                live_video_stats.frame_interval_max_ns = std::max(live_video_stats.frame_interval_max_ns, interval);
                const unsigned focused = presenter->has_input_focus() ? 1U : 0U;
                ++live_video_stats.focus_interval_count[focused];
                live_video_stats.focus_interval_ns[focused] += interval;
                live_video_stats.focus_interval_max_ns[focused] = std::max(
                    live_video_stats.focus_interval_max_ns[focused], interval);
            }
            last_vblank_wall_time = now;
            if (last_simulation_start != std::chrono::steady_clock::time_point{}) {
                const auto simulation_interval = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - last_simulation_start).count());
                ++live_video_stats.simulation_interval_count;
                live_video_stats.simulation_interval_ns += simulation_interval;
                live_video_stats.simulation_interval_max_ns = std::max(
                    live_video_stats.simulation_interval_max_ns, simulation_interval);
            }
        }
        board_runtime.signal_vblank();
        vblank_pending = board_runtime.maincpu_vblank_irq_level != 0;
        ++elapsed_vblanks;
        schedule_next_vblank();
        const bool capture_this_frame = frame_out && frame_out_vblank && !vblank_frame_written
            && elapsed_vblanks == *frame_out_vblank;
        if (presenter || capture_this_frame) {
            if (debugger_enabled && capture_this_frame) {
                multiarcaderecomp::report_m68k_debug_snapshot(elapsed_vblanks, ng_cycles);
                board_runtime.report_debug_snapshot(std::cout);
            }
            multiarcaderecomp::SegaVideoProfile live_frame_profile;
            const auto frame = render_current_frame(nullptr,
                (presenter && video_profile_enabled) ? &live_frame_profile : nullptr);
            if (presenter && video_profile_enabled) {
                const auto steady_render_ns = live_frame_profile.render_ns > live_frame_profile.gpu_setup_ns
                    ? live_frame_profile.render_ns - live_frame_profile.gpu_setup_ns : 0;
                ++live_video_stats.frames;
                live_video_stats.render_steady_ns += steady_render_ns;
                live_video_stats.render_max_steady_ns = std::max(live_video_stats.render_max_steady_ns, steady_render_ns);
                live_video_stats.gpu_setup_ns += live_frame_profile.gpu_setup_ns;
            }
            if (compare_cpu_video && video_backend == multiarcaderecomp::SegaVideoBackend::OpenGL) {
                compare_frame_with_cpu(frame, "at physical vblank " + std::to_string(elapsed_vblanks));
            }
            if (presenter) {
                const auto present_start = std::chrono::steady_clock::now();
                const bool keep_open = presenter->present(frame, bus.memory("paletteram"));
                const auto present_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - present_start).count());
                if (video_profile_enabled) {
                    live_video_stats.present_wait_ns += present_ns;
                    live_video_stats.present_max_ns = std::max(live_video_stats.present_max_ns, present_ns);
                }
                if (!keep_open) window_open = false;
            }
            if (presenter && video_profile_enabled)
                last_simulation_start = std::chrono::steady_clock::now();
            if (capture_this_frame) {
                multiarcaderecomp::write_sega_frame_ppm(frame, bus.memory("paletteram"), *frame_out);
                vblank_frame_written = true;
                std::cout << "Wrote video snapshot at physical vblank " << std::dec << elapsed_vblanks
                          << " to " << *frame_out << '\n';
            }
        }
    };

    std::size_t entry_count = 0;
    const std::string generated_profile_id(profile.id);
    const ng_func_entry_t* entries = board_module
        ? board_module->generated_table(generated_profile_id.c_str(), &entry_count) : nullptr;
    if ((!entries || entry_count == 0) && !stateful_opcode_transform) {
        throw std::runtime_error("the loaded board module has no generated 68000 table for profile '"
            + generated_profile_id + "'; include its translation output in that board's module and rebuild");
    }
    std::vector<std::uint32_t> aot_entry_pcs;
    aot_entry_pcs.reserve(entry_count);
    for (std::size_t index = 0; index < entry_count; ++index)
        aot_entry_pcs.push_back(entries[index].addr);
    std::uint64_t dispatches = 0;
    std::uint64_t musashi_fallback_instructions = 0;
    bool musashi_fallback_announced = false;
    bool fallback_miss_segment = false;
    const std::uint64_t scaled_limit = cycle_budget > std::numeric_limits<std::uint64_t>::max() / 4
        ? std::numeric_limits<std::uint64_t>::max() : cycle_budget * 4;
    const std::uint64_t dispatch_limit = std::max<std::uint64_t>(scaled_limit, 1024);
    const auto cpu_run_start = std::chrono::steady_clock::now();
    while (static_cast<std::uint64_t>(ng_cycles) < cycle_budget && window_open) {
        if (sound_active()) {
            advance_sound(static_cast<std::uint64_t>(ng_cycles));
            while (next_sound_sync <= static_cast<std::uint64_t>(ng_cycles)) next_sound_sync += sound_sync_period;
        }
        input_state.service = 0xff;
        input_state.p1 = 0xff;
        input_state.p2 = 0xff;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (presenter) {
            const auto live = presenter->live_inputs();
            input_state.service &= live.service;
            input_state.p1 &= live.p1;
            input_state.p2 &= live.p2;
        }
#endif
        for (const auto& event : input_events) {
            if (elapsed_vblanks < event.first_frame ||
                elapsed_vblanks - event.first_frame >= event.duration_frames) continue;
            auto* port = event.port == InputEvent::Port::Service ? &input_state.service
                : event.port == InputEvent::Port::P1 ? &input_state.p1 : &input_state.p2;
            *port &= event.active_low_mask;
        }
        if (board_runtime.trace_inputs)
            board_runtime.trace_inputs(input_state.service, input_state.p1, input_state.p2);
        while (static_cast<std::uint64_t>(ng_cycles) >= next_vblank) {
            advance_vblank();
        }
        board_runtime.advance_devices_to_cycles(static_cast<std::uint64_t>(ng_cycles));
        if (board_runtime.maincpu_reset_asserted()) {
            maincpu_reset_seen = true;
        } else if (maincpu_reset_seen) {
            // The mapper's RESET pin holds the 68000, then releases it into
            // the reset vectors. A stall alone leaves the pre-reset PC and
            // registers intact, which is not equivalent to the hardware.
            ng_cpu = {};
            ng_cpu.a[7] = initial_sp;
            ng_cpu.pc = initial_pc;
            ng_cpu.s = 1;
            ng_cpu.ipl = 7;
            maincpu_reset_seen = false;
        }
        const auto stall_until = board_runtime.maincpu_stall_until_cycles();
        if (stall_until > static_cast<std::uint64_t>(ng_cycles)) {
            ng_cycles = static_cast<std::int64_t>(std::min({stall_until, next_vblank, next_sound_sync,
                board_runtime.next_device_event(), cycle_budget}));
            continue;
        }
        const unsigned mapper_irq_level = board_runtime.pending_maincpu_irq_level();
        const unsigned vblank_irq_level = vblank_pending ? board_runtime.maincpu_vblank_irq_level : 0;
        const unsigned irq_level = std::max(mapper_irq_level, vblank_irq_level);
        if (irq_level != 0 && ng_cpu.ipl < irq_level) {
            const auto irq = m68k_recomp_accept_interrupt(irq_level);
            if (irq.status == M68K_RECOMP_DISPATCH_BUS_FAULT) {
                std::cerr << "68000 IRQ4 vector bus fault at 0x" << std::hex << irq.fault_address << '\n';
                return 1;
            }
            if (irq.status == M68K_RECOMP_DISPATCH_OK) {
                board_runtime.on_maincpu_interrupt_accepted();
                if (vblank_irq_level == irq_level) {
                    vblank_pending = false;
                    ++accepted_vblanks;
                }
                if (mapper_irq_level == irq_level) {
                    board_runtime.acknowledge_maincpu_irq(mapper_irq_level);
                    ++accepted_mapper_irqs;
                }
                continue;
            }
        }
        if (ng_cpu.stopped) {
            ng_cycles = static_cast<int64_t>(std::min({next_vblank, board_runtime.next_device_event(), cycle_budget}));
            continue;
        }
        ng_next_event = static_cast<int64_t>(std::min({next_vblank, next_sound_sync,
            board_runtime.next_device_event(), cycle_budget}));
        if (++dispatches > dispatch_limit) {
            throw std::runtime_error("dispatch safety limit reached before the cycle budget");
        }
        const auto* active_entries = aot_state_valid ? entries : nullptr;
        const auto active_entry_count = aot_state_valid ? entry_count : 0;
        const auto result = m68k_recomp_dispatch_once(active_entries, active_entry_count);
        if (multiarcaderecomp::m68k_debugger_quit_requested()) break;
        if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY) {
            if (!fallback_miss_segment && (result.pc & 1U) == 0 && result.pc < maincpu.size())
                runtime_entry_candidates.insert(result.pc);
            fallback_miss_segment = true;
            if (!musashi_fallback_announced) {
                std::uint16_t opcode = 0;
                char disassembly[128]{};
                multiarcaderecomp_m68k_musashi_describe(result.pc, &opcode, disassembly, sizeof(disassembly));
                std::cerr << "68000 Musashi fallback engaged at pc=0x" << std::hex << result.pc
                          << " opcode=0x" << std::setw(4) << std::setfill('0') << opcode
                          << std::dec << " instruction=\"" << disassembly << "\"\n";
                musashi_fallback_announced = true;
            }
            const auto fallback_start_cycles = ng_cycles;
            std::uint64_t fallback_instructions = 0;
            const bool* active_state_valid = aot_state_valid ? &aot_state_valid : nullptr;
            const int consumed_cycles = multiarcaderecomp_m68k_musashi_run_until(
                active_entries ? aot_entry_pcs.data() : nullptr,
                active_entries ? aot_entry_pcs.size() : 0,
                active_state_valid, &fallback_instructions);
            if (consumed_cycles <= 0 || fallback_instructions == 0)
                throw std::runtime_error("Musashi fallback stopped without executing an instruction (cycles="
                    + std::to_string(consumed_cycles) + ", pc=0x" + [&] { std::ostringstream out;
                        out << std::hex << ng_cpu.pc; return out.str(); }()
                    + ", guest cycles=" + std::to_string(ng_cycles)
                    + ", next event=" + std::to_string(ng_next_event)
                    + ", instructions=" + std::to_string(fallback_instructions) + ")");
            ng_cycles = fallback_start_cycles + consumed_cycles;
            ng_icount += fallback_instructions;
            musashi_fallback_instructions += fallback_instructions;
            const M68kBusFault fallback_fault = multiarcaderecomp_m68k_bus_fault();
            if (fallback_fault.access != M68K_BUS_ACCESS_NONE) {
                std::cerr << "68000 Musashi fallback bus fault access="
                          << static_cast<unsigned>(fallback_fault.access)
                          << " address=0x" << std::hex << fallback_fault.address << '\n';
                return 1;
            }
            continue;
        }
        if (result.status != M68K_RECOMP_DISPATCH_OK) {
            std::cerr << "68000 execution stopped: ";
            if (result.status == M68K_RECOMP_DISPATCH_BUS_FAULT) {
                std::cerr << "bus fault access=" << static_cast<unsigned>(result.fault_access)
                          << " address=0x" << std::hex << result.fault_address;
            } else if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY) {
                std::cerr << "no generated entry";
            } else {
                std::cerr << "bad or odd guest PC";
            }
            std::cerr << " pc=0x" << std::hex << result.pc << '\n';
            board_runtime.report_devices();
            return 1;
        }
        fallback_miss_segment = false;
    }
    // A whole guest instruction can take ng_cycles past the requested budget
    // and across a video edge. Account for those elapsed board events even if
    // the cycle-budget condition prevents another CPU dispatch.
    while (static_cast<std::uint64_t>(ng_cycles) >= next_vblank) {
        advance_vblank();
    }
    board_runtime.advance_devices_to_cycles(static_cast<std::uint64_t>(ng_cycles));
    if (sound_active()) advance_sound(static_cast<std::uint64_t>(ng_cycles));
    if (frame_out_vblank && !vblank_frame_written) {
        throw std::runtime_error("cycle budget ended before the requested physical vblank capture");
    }
    const auto cpu_run_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - cpu_run_start).count();

    const char* stop_reason = ng_cpu.stopped ? "STOP instruction"
        : !window_open && static_cast<std::uint64_t>(ng_cycles) < cycle_budget ? "window close"
        : "cycle budget";
    std::cout << "Emulation run stopped at " << stop_reason
              << "\ncycles=" << std::dec << ng_cycles << " instructions=" << ng_icount
              << " dispatches=" << dispatches << " vblanks=" << elapsed_vblanks
              << " vblank_irq=" << accepted_vblanks << " mapper_irqs=" << accepted_mapper_irqs
              << "\npc=0x" << std::hex << std::setw(8)
              << std::setfill('0') << ng_cpu.pc << " sp=0x" << std::setw(8) << ng_cpu.a[7] << '\n';
    std::cout << "68000 execution coverage: AOT instructions=" << std::dec
              << (ng_icount >= musashi_fallback_instructions ? ng_icount - musashi_fallback_instructions : 0)
              << " Musashi fallback instructions=" << musashi_fallback_instructions << '\n';
    std::cout << "DIP state: DSW1=0x" << std::hex << static_cast<unsigned>(input_state.dsw1)
              << ", DSW2=0x" << static_cast<unsigned>(input_state.dsw2) << std::dec
              << " (reads=" << input_state.dsw1_reads << '/' << input_state.dsw2_reads << ")\n";
    board_runtime.report_sound();
    board_runtime.report_devices();
    if (runtime_entry_out) {
        const auto fallback_log = runtime_entry_out->parent_path() / "z80_fallbacks.txt";
        const auto fallback_path = fallback_log.string();
        multiarcaderecomp::z80::z80native::PersistFallbackLog(fallback_path.c_str());
    }
    if (aot_profile_enabled) multiarcaderecomp::z80::z80native::ReportInterpreterProfile();
    if (!sound_active()) {
        std::cout << "Z80 execution coverage: AOT instructions=0 fallback instructions=0"
                  << " (sound CPU was not instantiated; audio is disabled)\n";
    }
    if (!input_events.empty()) {
        std::cout << std::dec << "Input port reads: SERVICE=" << input_state.service_reads
                  << " (pressed=" << input_state.service_pressed_reads << ") P1=" << input_state.p1_reads
                  << " (pressed=" << input_state.p1_pressed_reads << ") P2=" << input_state.p2_reads
                  << " (pressed=" << input_state.p2_pressed_reads << ")\n";
    }
    // Input counters switch the stream to decimal; the register labels below
    // promise hexadecimal values regardless of whether scripted input is used.
    std::cout << std::hex;
    for (unsigned i = 0; i < 8; ++i) {
        std::cout << "D" << i << "=0x" << std::setw(8) << ng_cpu.d[i] << (i == 7 ? '\n' : ' ');
    }
    for (unsigned i = 0; i < 7; ++i) {
        std::cout << "A" << i << "=0x" << std::setw(8) << ng_cpu.a[i] << (i == 6 ? '\n' : ' ');
    }
    std::cout << std::setfill(' ');
    std::vector<multiarcaderecomp::SegaVideoFrame> video_stages;
    std::uint64_t ppm_write_ns = 0;
    if ((frame_out && !frame_out_vblank) || video_state_out || video_profile_enabled) {
        multiarcaderecomp::SegaVideoProfile render_profile;
        const auto frame = render_current_frame(video_state_out ? &video_stages : nullptr,
            video_profile_enabled ? &render_profile : nullptr);
        if (compare_cpu_video && video_backend == multiarcaderecomp::SegaVideoBackend::OpenGL) {
            compare_frame_with_cpu(frame, "at final cycle-budget state");
        }
        if (frame_out && !frame_out_vblank) {
            const auto ppm_start = std::chrono::steady_clock::now();
            multiarcaderecomp::write_sega_frame_ppm(frame, bus.memory("paletteram"), *frame_out);
            ppm_write_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - ppm_start).count();
            std::cout << "Wrote indexed framebuffer (PPM) to " << *frame_out
                      << "; sprite rendering and palette shadow/highlight are enabled.\n";
            const char* backend_name = board_runtime.video_backend_description.empty()
                ? (video_backend == multiarcaderecomp::SegaVideoBackend::OpenGL
                    ? "OpenGL tilemap/text + CPU sprites" : "software CPU")
                : board_runtime.video_backend_description.c_str();
            std::cout << "Video backend: " << backend_name << '\n';
            std::cout << "Video signals: display=" << video_signals.display_enable
                      << " flip=" << video_signals.flip
                      ;
            board_runtime.report_video_signals(std::cout);
            std::cout << '\n';
        }
        if (video_profile_enabled) {
            std::array<bool, 2048> used_palette{};
            std::array<std::uint64_t, 16> priority_histogram{};
            std::uint64_t sprite_output_pixels = 0;
            for (const auto index : frame.palette_indices) {
                if (index < used_palette.size()) used_palette[index] = true;
                if (index >= 0x400) ++sprite_output_pixels;
            }
            for (const auto priority : frame.tile_priorities) ++priority_histogram[priority & 0x0fU];
            const auto used_palette_entries = static_cast<std::uint64_t>(
                std::count(used_palette.begin(), used_palette.end(), true));
            const auto frame_pixels = static_cast<std::uint64_t>(frame.palette_indices.size());
            const auto ms = [](std::uint64_t ns) { return static_cast<double>(ns) / 1'000'000.0; };
            std::cout << std::dec << std::fixed << std::setprecision(3)
                      << "Video profile (wall-clock; independent of IRQ4 and MAME frame counters):\n"
                      << "  graphics ROM load/assembly: " << ms(graphics_load_ns) << " ms ("
                      << graphics.tiles.size() << " tile-ROM bytes, " << graphics.sprites.size() << " sprite-ROM bytes)\n"
                      << "  tile decode: " << ms(tile_decode_ns) << " ms ("
                      << decoded_tiles.size() / 64 << " 8x8 tiles, " << decoded_tiles.size() << " pen indices; "
                      << (tile_decode_ns ? decoded_tiles.size() * 1000.0 / tile_decode_ns : 0.0) << " Mpen/s)\n"
                      << "  run loop elapsed (including presentation and pacing): " << ms(cpu_run_ns) << " ms for "
                      << ng_cycles << " guest cycles and " << accepted_vblanks << " accepted IRQ4s\n"
                      << "  framebuffer composition: " << ms(render_profile.render_ns) << " ms ("
                      << multiarcaderecomp::SegaVideoFrame::width << 'x' << multiarcaderecomp::SegaVideoFrame::height
                      << ", " << frame_pixels << " pixels)\n";
            if (!board_runtime.supports_video_profile) {
                std::cout << "    per-stage video counters are not available for this board\n";
            } else if (video_backend == multiarcaderecomp::SegaVideoBackend::OpenGL) {
                std::cout << "    OpenGL setup/upload/dispatch+readback: " << ms(render_profile.gpu_setup_ns) << "/"
                          << ms(render_profile.gpu_upload_ns) << "/" << ms(render_profile.gpu_dispatch_readback_ns) << " ms\n"
                          << "    compute invocations: " << (video_signals.display_enable ? frame_pixels : 0)
                          << "; tile-atlas pen lookups: " << (video_signals.display_enable ? frame_pixels * 3 : 0);
                if (board_runtime.cpu_sprite_gpu_mixer)
                    std::cout << "; CPU sprite rasterization + OpenGL priority/palette/shadow mixer\n";
                else
                    std::cout << "; sprites/mixer on CPU: " << ms(render_profile.sprites_ns) << " ms\n";
            } else {
                std::cout << "    background: " << ms(render_profile.background_ns) << " ms; foreground: "
                          << ms(render_profile.foreground_ns) << " ms; text: " << ms(render_profile.text_ns)
                          << " ms; sprites/mixer: " << ms(render_profile.sprites_ns) << " ms\n"
                          << "    page coordinates visited: " << render_profile.page_pixel_visits
                          << "; page pen lookups: " << render_profile.page_pen_lookups
                          << "; text coordinates/pen lookups: " << render_profile.text_pixel_visits << "/"
                          << render_profile.text_pen_lookups << '\n'
                          << "    palette-index writes BG/FG/text: " << render_profile.background_palette_writes << "/"
                          << render_profile.foreground_palette_writes << "/" << render_profile.text_palette_writes
                          << "; priority marks: " << render_profile.priority_marks << '\n';
            }
            if (board_runtime.supports_video_profile)
                std::cout << "    sprite ROM words/pens: " << render_profile.sprite_words_read << "/"
                          << render_profile.sprite_pen_lookups << "; rasterized pixels: "
                          << render_profile.sprite_pixels_rasterized << "; mixed pixels: "
                          << render_profile.sprite_pixels_mixed << "; shadow pixels: "
                          << render_profile.sprite_shadow_pixels << '\n';
            std::cout << "  final framebuffer: " << frame_pixels << " indexed pixels ("
                      << frame.palette_indices.size() * sizeof(std::uint16_t) << " bytes), "
                      << frame.tile_priorities.size() << " priority bytes, " << frame.shadow_pixels.size()
                      << " shadow bytes; " << used_palette_entries << " palette indices used; "
                      << sprite_output_pixels << " sprite-color pixels\n"
                      << "  RGB PPM conversion/write: ";
            if (frame_out && !frame_out_vblank) {
                const auto ppm_bytes = std::filesystem::file_size(*frame_out);
                std::cout << ms(ppm_write_ns) << " ms (" << ppm_bytes << " bytes, "
                          << frame_pixels * 3 << " RGB payload bytes)\n";
            } else if (frame_out_vblank) {
                std::cout << "captured at vblank " << *frame_out_vblank
                          << "; not included in final-frame timing\n";
            } else {
                std::cout << "not measured; pass --frame-out <image.ppm>\n";
            }
            std::cout << "  priority-mask pixel counts:";
            for (std::size_t i = 0; i < priority_histogram.size(); ++i) {
                if (priority_histogram[i]) std::cout << ' ' << i << '=' << priority_histogram[i];
            }
            if (presenter && live_video_stats.frames != 0) {
                std::cout << std::dec << std::fixed << std::setprecision(3)
                          << "  Live window (" << live_video_stats.frames << " frames): composition mean/max excluding GPU setup "
                          << ms(live_video_stats.render_steady_ns / live_video_stats.frames) << "/"
                          << ms(live_video_stats.render_max_steady_ns) << " ms; presenter call mean/max (includes frame pacing) "
                          << ms(live_video_stats.present_wait_ns / live_video_stats.frames) << "/"
                          << ms(live_video_stats.present_max_ns) << " ms; GPU setup total "
                          << ms(live_video_stats.gpu_setup_ns) << " ms\n";
                if (live_video_stats.frame_interval_count != 0) {
                    std::cout << "    Emulated VBlank wall interval mean/max: "
                              << ms(live_video_stats.frame_interval_ns / live_video_stats.frame_interval_count) << "/"
                              << ms(live_video_stats.frame_interval_max_ns) << " ms\n";
                }
                for (unsigned focused = 0; focused < 2; ++focused) {
                    const auto count = live_video_stats.focus_interval_count[focused];
                    if (!count) continue;
                    std::cout << "    " << (focused ? "Focused" : "Unfocused")
                              << " VBlank intervals: " << count << "; mean/max "
                              << ms(live_video_stats.focus_interval_ns[focused] / count) << "/"
                              << ms(live_video_stats.focus_interval_max_ns[focused]) << " ms\n";
                }
                if (live_video_stats.simulation_interval_count != 0) {
                    std::cout << "    guest simulation between presents mean/max: "
                              << ms(live_video_stats.simulation_interval_ns / live_video_stats.simulation_interval_count) << "/"
                              << ms(live_video_stats.simulation_interval_max_ns) << " ms\n";
                }
                const auto presentation = presenter->timing();
                if (presentation.frames) {
                    const auto report_phase = [&](const char* name, std::uint64_t total, std::uint64_t maximum) {
                        std::cout << " " << name << "=" << ms(total / presentation.frames) << "/" << ms(maximum);
                    };
                    std::cout << "    Presenter mean/max (ms):";
                    report_phase("RGB", presentation.convert_ns, presentation.convert_max_ns);
                    report_phase("upload", presentation.upload_ns, presentation.upload_max_ns);
                    report_phase("SDL_RenderPresent", presentation.vsync_ns, presentation.vsync_max_ns);
                    report_phase("limiter", presentation.limiter_ns, presentation.limiter_max_ns);
                    std::cout << '\n';
                }
            }
            std::cout << std::defaultfloat << '\n';
        }
        if (video_state_out) {
            static constexpr const char* stage_names[] = {"background", "foreground", "text", "sprites"};
            for (std::size_t i = 0; i < video_stages.size(); ++i) {
                const auto stage_path = *video_state_out / (std::string("stage_") + stage_names[i] + ".ppm");
                multiarcaderecomp::write_sega_frame_ppm(video_stages[i], bus.memory("paletteram"), stage_path);
            }
            std::cout << "Wrote " << video_stages.size() << " intermediate composition stages to "
                      << *video_state_out << '\n';
        }
    }
    if (frame_out && !frame_out_vblank) {
        board_runtime.report_board_video_state(std::cout);
    }
    if (video_state_out) {
        std::filesystem::create_directories(*video_state_out);
        const auto write_bytes = [](const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
            std::ofstream out(path, std::ios::binary);
            if (!out) throw std::runtime_error("could not create video-state dump: " + path.string());
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) throw std::runtime_error("failed writing video-state dump: " + path.string());
        };
        write_bytes(*video_state_out / "tileram.bin", bus.memory("tileram"));
        write_bytes(*video_state_out / "textram.bin", bus.memory("textram"));
        write_bytes(*video_state_out / "sprites.bin", bus.memory("sprites"));
        write_bytes(*video_state_out / "paletteram.bin", bus.memory("paletteram"));
        write_bytes(*video_state_out / "nvram.bin", bus.memory("nvram"));
        write_bytes(*video_state_out / "tiles_decoded.bin", decoded_tiles);
        write_bytes(*video_state_out / "sprites_decoded.bin", graphics.sprites);
        if (video_stages.size() >= 4) {
            const auto& final_frame = video_stages.back();
            std::vector<std::uint8_t> palette_indices_be;
            palette_indices_be.reserve(final_frame.palette_indices.size() * 2);
            for (const auto index : final_frame.palette_indices) {
                palette_indices_be.push_back(static_cast<std::uint8_t>(index >> 8));
                palette_indices_be.push_back(static_cast<std::uint8_t>(index));
            }
            write_bytes(*video_state_out / "frame_palette_indices_be.bin", palette_indices_be);
            write_bytes(*video_state_out / "frame_priorities.bin", final_frame.tile_priorities);
        }
        std::ofstream info(*video_state_out / "state.txt");
        if (!info) throw std::runtime_error("could not create video-state metadata");
        info << "cycles=" << ng_cycles << '\n'
             << "display_enable=" << video_signals.display_enable << '\n'
             << "flip=" << video_signals.flip << '\n';
        board_runtime.append_video_signal_state(info);
        board_runtime.write_board_video_state(*video_state_out, info);
        std::cout << "Wrote raw video RAM, graphics, and signal metadata to " << *video_state_out << '\n';
    }
    if (runtime_entry_out) {
        std::filesystem::create_directories(runtime_entry_out->parent_path());
        std::ofstream output(*runtime_entry_out, std::ios::trunc);
        if (!output) throw std::runtime_error("could not write runtime 68000 entries: " + runtime_entry_out->string());
        output << std::hex << std::setfill('0');
        for (const auto address : runtime_entry_candidates)
            output << std::setw(6) << address << '\n';
        if (!output) throw std::runtime_error("failed writing runtime 68000 entries: " + runtime_entry_out->string());
        std::cout << "Recorded " << std::dec << runtime_entry_candidates.size()
                  << " runtime-observed ROM entry candidate(s) to " << *runtime_entry_out << '\n';
    }
    return 0;
}

} // namespace multiarcaderecomp::sega
