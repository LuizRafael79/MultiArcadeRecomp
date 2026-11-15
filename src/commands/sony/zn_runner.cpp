#include "commands/machine_runner_support.hpp"
#include "commands/machine_runners.hpp"
#include "konami/tmnt_machine.hpp"
#include "konami/tmnt_presenter.hpp"
#include "video/sdl_video_presenter.hpp"
#include "konami/tmnt2_machine.hpp"
#include "seibu/toki_machine.hpp"
#include "seibu/toki_presenter.hpp"
#include "zn_runtime.hpp"
#include "native_z80.h"
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#include <SDL3/SDL.h>
#endif
#ifdef MULTIARCADERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <dlfcn.h>

namespace multiarcaderecomp::app::machine_runners {
using multiarcaderecomp::app::read_binary_file;
using multiarcaderecomp::app::run_child_process;
using InputEvent = multiarcaderecomp::sega::InputEvent;

int run_zn_machine(const std::filesystem::path& source_root,
                   const std::string& game,
                   const std::optional<std::filesystem::path>& rom_zip,
                   bool show_window, bool audio_enabled, bool gpu_hw,
                   const std::vector<InputEvent>& button_events,
                   std::uint64_t frames,
                   const std::optional<std::uint64_t>& cycles,
                   const std::optional<std::filesystem::path>& frame_out,
                   const std::optional<std::filesystem::path>& frame_sequence_dir,
                   std::uint64_t frame_sequence_step,
                   const char *platform_name, const char *board_name,
                   const std::optional<std::filesystem::path>& nvram_option,
                   bool r3000a_no_aot,
                   bool z80_no_aot,
                   bool dsp16a_no_aot,
                   multiarcaderecomp::chips::QSoundMode qsound_mode,
                   bool qsound_mixed_requested) {
    const bool zn1 = game == "bldyror2";
    const bool system12 = game == "tekken3";
    // MAME configures the PSX-derived CPUs at their crystal rates, then
    // converts four crystal clocks into one execute cycle. Frame scheduling
    // shares the same execute-cycle counter as PsxCpuState::cycles.
    const std::uint64_t cpu_clock_hz = zn1
        ? multiarcaderecomp::psx::kZn1MemoryConfig.cpu_cycle_hz
        : system12 ? multiarcaderecomp::psx::kSystem12MemoryConfig.cpu_cycle_hz
                   : multiarcaderecomp::psx::kZn2MemoryConfig.cpu_cycle_hz;
    // MAME's PSX GPU uses 59.8260978565 Hz for the default NTSC 240-line
    // mode (psxgpu_device::updatevisiblearea). Keep the CPU frame scheduler
    // on that same cadence so --frames, input events, and GPU vblank don't
    // drift apart by one frame every few seconds.
    constexpr std::uint64_t refresh_scaled = 59'826'097'857ull;
    constexpr std::uint64_t refresh_scale = 1'000'000'000ull;
    auto cycle_at_frame = [cpu_clock_hz](std::uint64_t frame_number) {
        const __uint128_t numerator = __uint128_t(frame_number) * cpu_clock_hz * refresh_scale;
        return static_cast<std::uint64_t>((numerator + refresh_scaled - 1) / refresh_scaled);
    };
    const bool unlimited = show_window && frames == 0 && !cycles;
    if (frames && cycles)
        throw std::runtime_error("choose either --frames or --cycles for a ZN run");
    if (frame_sequence_step == 0)
        throw std::runtime_error("--frame-sequence-step must be positive");
    if (frame_sequence_dir) std::filesystem::create_directories(*frame_sequence_dir);

    const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
                                  : source_root.parent_path() / "mame/roms" / (game + ".zip");
    if (!std::filesystem::is_regular_file(rom_path))
        throw std::runtime_error("ZN ROM ZIP not found: " + rom_path.string());

#ifndef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    if (show_window || audio_enabled)
        throw std::runtime_error("ZN live video/audio requires an SDL3-enabled build");
#endif

    const std::filesystem::path zn_aot_module = source_root / "build/multiarcaderecomp_zn2_sfex2pa.so";
    void *zn_aot_handle = nullptr;
    const multiarcaderecomp::z80::Z80Code *zn2_z80_code = nullptr;
    const PsxAotEntry *zn2_psx_entries = nullptr;
    size_t zn2_psx_entry_count = 0;
    const multiarcaderecomp::dsp16::Dsp16ACompiledEntry *zn2_dsp16a_entries = nullptr;
    size_t zn2_dsp16a_entry_count = 0;
    multiarcaderecomp::dsp16::Dsp16ARunHandler zn2_dsp16a_run = nullptr;
#if defined(__unix__) || defined(__APPLE__)
    if (game == "sfex2pa" && std::filesystem::is_regular_file(zn_aot_module)) {
        zn_aot_handle = dlopen(zn_aot_module.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!zn_aot_handle)
            throw std::runtime_error("could not load ZN-2 AOT module " + zn_aot_module.string() + ": " + dlerror());
        using Z80CodeFn = const multiarcaderecomp::z80::Z80Code *(*)();
        auto get_code = reinterpret_cast<Z80CodeFn>(dlsym(zn_aot_handle, "multiarcaderecomp_zn2_sfex2pa_z80_code"));
        const char *symbol_error = dlerror();
        if (symbol_error || !get_code)
            throw std::runtime_error("ZN-2 AOT module has no Z80 table: " + std::string(symbol_error ? symbol_error : "unknown loader error"));
        if (!z80_no_aot)
            zn2_z80_code = get_code();
        using PsxEntriesFn = void (*)(const PsxAotEntry **, size_t *);
        auto get_psx_entries = reinterpret_cast<PsxEntriesFn>(dlsym(zn_aot_handle, "multiarcaderecomp_zn2_sfex2pa_psx_entries"));
        if (get_psx_entries && !r3000a_no_aot)
            get_psx_entries(&zn2_psx_entries, &zn2_psx_entry_count);
        using Dsp16AEntriesFn = void (*)(const multiarcaderecomp::dsp16::Dsp16ACompiledEntry **, size_t *);
        auto get_dsp16a_entries = reinterpret_cast<Dsp16AEntriesFn>(
            dlsym(zn_aot_handle, "multiarcaderecomp_zn2_sfex2pa_dsp16a_entries"));
        if (get_dsp16a_entries && !dsp16a_no_aot)
            get_dsp16a_entries(&zn2_dsp16a_entries, &zn2_dsp16a_entry_count);
        if (!dsp16a_no_aot)
            zn2_dsp16a_run = reinterpret_cast<multiarcaderecomp::dsp16::Dsp16ARunHandler>(
                dlsym(zn_aot_handle, "multiarcaderecomp_zn2_sfex2pa_dsp16a_run"));
    }
#endif
    std::vector<std::uint16_t> qsound_firmware;
    if (game == "sfex2pa") {
        const char *firmware_archive = qsound_mode != multiarcaderecomp::chips::QSoundMode::Hle
            ? "qsound.zip" : "qsound_hle.zip";
        const char *alternate_archive = qsound_mode != multiarcaderecomp::chips::QSoundMode::Hle
            ? "qsound_hle.zip" : "qsound.zip";
        const auto adjacent_firmware = rom_path.parent_path() / firmware_archive;
        const auto alternate_adjacent = rom_path.parent_path() / alternate_archive;
        const auto default_firmware = source_root.parent_path() / "mame/roms" / firmware_archive;
        const auto alternate_default = source_root.parent_path() / "mame/roms" / alternate_archive;
        const auto firmware_zip = std::filesystem::is_regular_file(adjacent_firmware)
            ? adjacent_firmware : std::filesystem::is_regular_file(alternate_adjacent)
                ? alternate_adjacent : std::filesystem::is_regular_file(default_firmware)
                    ? default_firmware : alternate_default;
        if (!std::filesystem::is_regular_file(firmware_zip))
            throw std::runtime_error("QSound DL-1425 firmware ZIP not found: " + firmware_zip.string());
        qsound_firmware = multiarcaderecomp::zn::load_qsound_firmware(firmware_zip);
        std::cout << "ZN-2 QSound mode: ";
        if (qsound_mixed_requested)
            std::cout << "mixed (DSP16A LLE primary; automatic HLE fallback armed)";
        else if (qsound_mode == multiarcaderecomp::chips::QSoundMode::Lle)
            std::cout << "DSP16A LLE (forced)";
        else
            std::cout << "MAME HLE (forced)";
        std::cout << " (firmware=" << firmware_zip << ").\n";
    }
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    // Declare the window before the machine so the PSX renderer is destroyed
    // before the externally owned GL context during normal return/unwinding.
    std::unique_ptr<multiarcaderecomp::video::SdlVideoPresenter> presenter;
#endif
    multiarcaderecomp::zn::ZnMachineRuntime machine(game, rom_path, zn2_psx_entries,
                                             zn2_psx_entry_count, zn2_z80_code,
                                             qsound_mode, qsound_firmware,
                                             std::span<const multiarcaderecomp::dsp16::Dsp16ACompiledEntry>(
                                                 zn2_dsp16a_entries, zn2_dsp16a_entry_count),
                                             zn2_dsp16a_run);
    bool direct_gpu_present = false;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    direct_gpu_present = gpu_hw && show_window && !frame_out && !frame_sequence_dir;
    if (direct_gpu_present) {
        presenter = std::make_unique<multiarcaderecomp::video::SdlVideoPresenter>(
            "Sony ZN - " + game + " - MultiArcadeRecomp", 640, 480, 4, 3, 960, 720, true);
        machine.memory().gpu().enable_hardware_renderer(
            presenter->native_window(), presenter->native_gl_context());
    } else if (gpu_hw) {
        machine.memory().gpu().enable_hardware_renderer();
    }
#else
    if (gpu_hw) machine.memory().gpu().enable_hardware_renderer();
#endif
    const auto nvram_path = (game == "sfex2pa" || game == "bldyror2")
        ? std::optional<std::filesystem::path>(nvram_option.value_or(
            source_root / "build/nvram" / (game + ".at28c16")))
        : std::nullopt;
    if (nvram_path && std::filesystem::is_regular_file(*nvram_path)) {
        const auto image = read_binary_file(*nvram_path);
        if (!machine.load_nvram(image))
            throw std::runtime_error("AT28C16 NVRAM image must contain 0x800 or 0x820 bytes: " + nvram_path->string());
    }
    const bool capture_vblank_frames = show_window || frame_out.has_value() ||
                                       frame_sequence_dir.has_value();
    machine.memory().gpu().set_frame_capture_enabled(capture_vblank_frames);
    machine.memory().gpu().set_hardware_scanout_direct(direct_gpu_present);
    machine.memory().gpu().set_hardware_scanout_async(false);
    std::uint64_t instruction_budget = cycles ? *cycles
        : frames ? cycle_at_frame(frames)
        : cycle_at_frame(1);
    std::uint64_t next_frame_number = 1;
    std::uint64_t next_frame_cycle = cycle_at_frame(next_frame_number);
    std::uint64_t frame_updates = 0;
    const bool zn_diagnostics = std::getenv("MULTIARCADERECOMP_ZN_DIAGNOSTICS") != nullptr;
    uint64_t diagnostics_window_cycles = machine.cpu().cycles;
    double diagnostics_window_host_ms = 0.0;
    unsigned diagnostics_window_frames = 0;
    bool window_open = true;
    uint64_t system12_audio_frames = 0;
    uint32_t system12_audio_peak = 0;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    SDL_AudioStream* zn_audio_stream = nullptr;
    SDL_AudioStream* zn_spu_stream = nullptr;
    std::ofstream zn_qsound_pcm_dump;
    std::ofstream zn_spu_pcm_dump;
    if (const char* dump_path = std::getenv("MULTIARCADERECOMP_ZN_DUMP_QSOUND_PCM")) {
        zn_qsound_pcm_dump.open(dump_path, std::ios::binary | std::ios::trunc);
        if (!zn_qsound_pcm_dump)
            throw std::runtime_error("could not create ZN QSound PCM dump " + std::string(dump_path));
    }
    if (const char* dump_path = std::getenv("MULTIARCADERECOMP_ZN_DUMP_SPU_PCM")) {
        zn_spu_pcm_dump.open(dump_path, std::ios::binary | std::ios::trunc);
        if (!zn_spu_pcm_dump)
            throw std::runtime_error("could not create ZN PSX SPU PCM dump " + std::string(dump_path));
    }
    const bool zn_audio_active = show_window || audio_enabled;
    if (zn_audio_active && (game == "bldyror2" || game == "sfex2pa" || system12)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL audio init failed: ") + SDL_GetError());
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(zn1 ? machine.zn1_audio_sample_rate()
            : system12 ? machine.system12_audio_sample_rate()
                       : machine.zn2_qsound_sample_rate());
        zn_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                    &spec, nullptr, nullptr);
        if (!zn_audio_stream || !SDL_ResumeAudioStreamDevice(zn_audio_stream)) {
            const std::string error = SDL_GetError();
            if (zn_audio_stream) SDL_DestroyAudioStream(zn_audio_stream);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw std::runtime_error("SDL audio stream failed: " + error);
        }
        SDL_AudioSpec spu_spec{};
        spu_spec.format = SDL_AUDIO_S16;
        spu_spec.channels = 2;
        spu_spec.freq = 44100;
        zn_spu_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                   &spu_spec, nullptr, nullptr);
        if (!zn_spu_stream || !SDL_ResumeAudioStreamDevice(zn_spu_stream)) {
            const std::string error = SDL_GetError();
            if (zn_spu_stream) SDL_DestroyAudioStream(zn_spu_stream);
            SDL_DestroyAudioStream(zn_audio_stream);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw std::runtime_error("PSX SPU SDL stream failed: " + error);
        }
    }
    uint64_t system12_audio_main_cycles = 0;
    uint64_t system12_audio_cycle_remainder = 0;
    size_t diagnostics_qsound_frames = 0;
    size_t diagnostics_spu_frames = 0;
    int diagnostics_qsound_queued_bytes = -1;
    int diagnostics_spu_queued_bytes = -1;
    auto flush_zn_audio = [&] {
        std::vector<int16_t> samples;
        if (system12) {
            const uint64_t elapsed = machine.cpu().cycles - system12_audio_main_cycles;
            const uint64_t numerator = elapsed * machine.system12_audio_sample_rate() +
                                       system12_audio_cycle_remainder;
            const size_t audio_frames = static_cast<size_t>(numerator / cpu_clock_hz);
            system12_audio_cycle_remainder = numerator % cpu_clock_hz;
            system12_audio_main_cycles = machine.cpu().cycles;
            samples = machine.render_system12_audio(audio_frames);
            system12_audio_frames += audio_frames;
            for (int16_t sample : samples)
                system12_audio_peak = std::max(system12_audio_peak,
                    static_cast<uint32_t>(std::abs(static_cast<int>(sample))));
        } else {
            samples = zn1 ? machine.take_zn1_audio() : machine.take_zn2_audio();
        }
        if (game == "sfex2pa" && zn_qsound_pcm_dump && !samples.empty())
            zn_qsound_pcm_dump.write(reinterpret_cast<const char*>(samples.data()),
                                     static_cast<std::streamsize>(samples.size() * sizeof(int16_t)));
        diagnostics_qsound_frames = samples.size() / 2;
        if (zn_audio_stream && !samples.empty() &&
            !SDL_PutAudioStreamData(zn_audio_stream, samples.data(),
                                   static_cast<int>(samples.size() * sizeof(int16_t))))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        auto spu_samples = machine.take_spu_audio();
        diagnostics_spu_frames = spu_samples.size() / 2;
        if (zn_spu_pcm_dump && !spu_samples.empty())
            zn_spu_pcm_dump.write(reinterpret_cast<const char*>(spu_samples.data()),
                                  static_cast<std::streamsize>(spu_samples.size() * sizeof(int16_t)));
        if (zn_spu_stream && !spu_samples.empty() &&
            !SDL_PutAudioStreamData(zn_spu_stream, spu_samples.data(),
                                   static_cast<int>(spu_samples.size() * sizeof(int16_t))))
            throw std::runtime_error(std::string("PSX SPU audio queue failed: ") + SDL_GetError());
        if (zn_diagnostics) {
            diagnostics_qsound_queued_bytes = zn_audio_stream
                ? SDL_GetAudioStreamAvailable(zn_audio_stream) : 0;
            diagnostics_spu_queued_bytes = zn_spu_stream
                ? SDL_GetAudioStreamAvailable(zn_spu_stream) : 0;
        }
    };
#endif
    multiarcaderecomp::runtime::ArcadeInput live_inputs{};
    auto apply_zn_inputs = [&](std::uint64_t frame) {
        auto input = live_inputs;
        const auto add_directions_and_buttons = [&](unsigned player, uint8_t active) {
            auto &pressed = input.players_pressed[player];
            if (active & 0x01u) pressed |= multiarcaderecomp::runtime::ArcadeInput::up;
            if (active & 0x02u) pressed |= multiarcaderecomp::runtime::ArcadeInput::down;
            if (active & 0x04u) pressed |= multiarcaderecomp::runtime::ArcadeInput::left;
            if (active & 0x08u) pressed |= multiarcaderecomp::runtime::ArcadeInput::right;
            if (active & 0x10u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button1;
            if (active & 0x20u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button2;
            if (active & 0x40u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button3;
        };
        for (const auto &event : button_events) {
            if (frame < event.first_frame || frame - event.first_frame >= event.duration_frames)
                continue;
            const uint8_t active = static_cast<uint8_t>(~event.active_low_mask);
            switch (event.port) {
            case InputEvent::Port::Service:
                input.service_switches |= active;
                input.service_pressed = active != 0;
                break;
            case InputEvent::Port::P1:
                add_directions_and_buttons(0, active);
                break;
            case InputEvent::Port::P2:
                add_directions_and_buttons(1, active);
                break;
            case InputEvent::Port::System:
                if (active & 0x01u) input.players_pressed[0] |= multiarcaderecomp::runtime::ArcadeInput::start;
                if (active & 0x02u) input.players_pressed[1] |= multiarcaderecomp::runtime::ArcadeInput::start;
                if (active & 0x10u) input.coins_pressed |= 1u;
                if (active & 0x20u) input.coins_pressed |= 2u;
                break;
            case InputEvent::Port::P1Kick:
            case InputEvent::Port::P2Kick: {
                auto &pressed = input.players_pressed[event.port == InputEvent::Port::P1Kick ? 0 : 1];
                if (active & 0x01u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button4;
                if (active & 0x02u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button5;
                if (active & 0x04u) pressed |= multiarcaderecomp::runtime::ArcadeInput::button6;
                break;
            }
            case InputEvent::Port::Coin:
                input.coins_pressed |= event.active_low_mask;
                break;
            }
        }
        machine.set_input_state(input);
    };
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    auto sample_live_zn_inputs = [&] {
        if (!presenter || (game != "sfex2pa" && game != "bldyror2")) return;
        live_inputs = presenter->input_state();
    };
    auto show_frame = [&](const multiarcaderecomp::psx::GpuFrame &frame) {
        if (!show_window) return;
        if (!presenter) {
            presenter = std::make_unique<multiarcaderecomp::video::SdlVideoPresenter>(
                "Sony ZN - " + game + " - MultiArcadeRecomp", frame.width, frame.height,
                4, 3, 960, 720);
        }
        if (frame.direct_present) {
            if (!machine.memory().gpu().present_hardware_frame())
                throw std::runtime_error("PSX OpenGL direct presenter has no scanout frame");
            return;
        }
        presenter->present({frame.width, frame.height,
                            static_cast<std::size_t>(frame.width) * 3, frame.rgb});
    };
#endif

    std::cout << "Running " << game << " on " << platform_name << ' ' << board_name
              << " interpreter runtime; windowed frames use the shared SDL presenter.\n"
              << std::flush;
    apply_zn_inputs(0);
    while (window_open && (unlimited || machine.cpu().cycles < instruction_budget)) {
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (presenter) {
            if (!presenter->pump_events()) {
                window_open = false;
                break;
            }
            sample_live_zn_inputs();
            apply_zn_inputs(frame_updates);
        }
#endif
        const uint64_t execution_target = unlimited
            ? next_frame_cycle : std::min(instruction_budget, next_frame_cycle);
        const auto diagnostics_run_start = zn_diagnostics
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        machine.run_until(execution_target);
        if (machine.cpu().cycles >= next_frame_cycle) {
            const bool capture_sequence_frame = frame_sequence_dir &&
                (frame_updates + 1) % frame_sequence_step == 0;
            std::optional<multiarcaderecomp::psx::GpuFrame> fallback_frame;
            const multiarcaderecomp::psx::GpuFrame *frame = nullptr;
            if (show_window || capture_sequence_frame) {
                const auto& vblank_frame = machine.last_vblank_frame();
                if (vblank_frame) frame = &*vblank_frame;
                else {
                    fallback_frame = machine.framebuffer_rgb();
                    frame = &*fallback_frame;
                }
            }
            if (std::getenv("MULTIARCADERECOMP_ZN_TRACE_FRAME_STATE") &&
                (((frame_updates + 1) % 60 == 0) ||
                 ((frame_updates + 1) >= 178 && (frame_updates + 1) <= 185) ||
                 ((frame_updates + 1) >= 1205 && (frame_updates + 1) <= 1225))) {
                const auto &cpu = machine.cpu();
                uint32_t irq_status = 0;
                uint32_t epc_opcode = 0;
                uint32_t vector_word = 0, vector_word1 = 0, vector_word2 = 0, vector_word3 = 0;
                (void)machine.memory().read(0x1f801070, 4, &irq_status);
                (void)machine.memory().read(cpu.cp0[14], 4, &epc_opcode);
                (void)machine.memory().read(0x80000080, 4, &vector_word);
                (void)machine.memory().read(0x80000084, 4, &vector_word1);
                (void)machine.memory().read(0x80000088, 4, &vector_word2);
                (void)machine.memory().read(0x8000008c, 4, &vector_word3);
                std::cerr << "ZN frame-state frame=" << (frame_updates + 1)
                          << " cycles=" << cpu.cycles << " pc=0x" << std::hex << cpu.pc
                          << " gpu=0x" << machine.memory().gpu().read_status()
                          << " irq-status=0x" << irq_status
                          << " sr=0x" << cpu.cp0[12] << " cause=0x" << cpu.cp0[13]
                          << " epc=0x" << cpu.cp0[14] << " badvaddr=0x" << cpu.cp0[8]
                          << " op=0x" << epc_opcode
                          << " vector=" << std::hex << vector_word << ',' << vector_word1
                          << ',' << vector_word2 << ',' << vector_word3
                          << std::dec << '\n';
                for (unsigned reg = 0; reg < 32; ++reg)
                    std::cerr << " r" << reg << "=0x" << std::hex << cpu.r[reg];
                std::cerr << std::dec << '\n';
                if ((cpu.cp0[13] & 0x7cu) == 0x10u) {
                    std::cerr << "ZN EPC code";
                    for (int offset = -16; offset <= 4; offset += 4) {
                        uint32_t word = 0;
                        (void)machine.memory().read(cpu.cp0[14] + uint32_t(offset), 4, &word);
                        std::cerr << " [" << std::hex << offset << "]=0x" << word;
                    }
                    std::cerr << std::dec << '\n';
                }
            }
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            if (show_window) {
                if (presenter && !presenter->pump_events()) {
                    window_open = false;
                    break;
                }
                sample_live_zn_inputs();
                show_frame(*frame);
            }
            flush_zn_audio();
#endif
            if (zn_diagnostics) {
                diagnostics_window_host_ms += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - diagnostics_run_start).count();
                ++diagnostics_window_frames;
                const uint64_t frame_number = frame_updates + 1;
                if (frame_number % 60 == 0) {
                    uint32_t irq_status = 0, irq_mask = 0;
                    (void)machine.memory().read(0x1f801070, 2, &irq_status);
                    (void)machine.memory().read(0x1f801074, 2, &irq_mask);
                    const uint64_t elapsed_cycles = machine.cpu().cycles - diagnostics_window_cycles;
                    const double emulated_ms = cpu_clock_hz
                        ? 1000.0 * static_cast<double>(elapsed_cycles) / cpu_clock_hz
                        : 0.0;
                    const double frames = diagnostics_window_frames
                        ? diagnostics_window_frames : 1;
                    std::cerr << "ZN diagnostic frame=" << frame_number
                              << " pc=0x" << std::hex << machine.cpu().pc
                              << " sr=0x" << machine.cpu().cp0[12]
                              << " cause=0x" << machine.cpu().cp0[13]
                              << " irq-status=0x" << irq_status
                              << " irq-mask=0x" << irq_mask
                              << " gpustat=0x" << machine.memory().gpu().read_status()
                              << std::dec << " cycles/frame="
                              << static_cast<double>(elapsed_cycles) / frames
                              << " host-ms/frame=" << diagnostics_window_host_ms / frames
                              << " emu-ms/frame=" << emulated_ms / frames;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
                    std::cerr << " audio-frames(board/spu)="
                              << diagnostics_qsound_frames << '/' << diagnostics_spu_frames
                              << " stream-available-bytes(board/spu)="
                              << diagnostics_qsound_queued_bytes << '/'
                              << diagnostics_spu_queued_bytes;
#endif
                    if (game == "sfex2pa")
                        std::cerr << " qsound-total-samples=" << machine.zn2_qsound_samples()
                                  << " qsound-peak=" << machine.zn2_qsound_peak();
                    std::cerr << '\n';
                    diagnostics_window_cycles = machine.cpu().cycles;
                    diagnostics_window_host_ms = 0.0;
                    diagnostics_window_frames = 0;
                }
            }
            if (capture_sequence_frame) {
                const auto path = *frame_sequence_dir /
                    ("frame_" + std::to_string(frame_updates + 1) + ".ppm");
                std::ofstream output(path, std::ios::binary);
                if (!output) throw std::runtime_error("could not create ZN frame " + path.string());
                output << "P6\n" << frame->width << ' ' << frame->height << "\n255\n";
                output.write(reinterpret_cast<const char*>(frame->rgb.data()),
                             static_cast<std::streamsize>(frame->rgb.size()));
                if (!output) throw std::runtime_error("failed writing ZN frame " + path.string());
                std::cout << "Captured ZN frame " << frame_updates + 1 << " to " << path << '\n';
            }
            ++frame_updates;
            apply_zn_inputs(frame_updates);
            next_frame_cycle = cycle_at_frame(++next_frame_number);
        }
    }

    const auto& last_vblank_frame = machine.last_vblank_frame();
    const auto frame = last_vblank_frame ? *last_vblank_frame
                                         : machine.framebuffer_rgb();
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    flush_zn_audio();
    if (zn_audio_stream) {
        SDL_DestroyAudioStream(zn_audio_stream);
        if (zn_spu_stream) SDL_DestroyAudioStream(zn_spu_stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    if (zn_qsound_pcm_dump) {
        zn_qsound_pcm_dump.flush();
        if (!zn_qsound_pcm_dump)
            throw std::runtime_error("failed writing ZN QSound PCM dump");
    }
    if (zn_spu_pcm_dump) {
        zn_spu_pcm_dump.flush();
        if (!zn_spu_pcm_dump)
            throw std::runtime_error("failed writing ZN PSX SPU PCM dump");
    }
#endif
    if (const char* dump_path = std::getenv("MULTIARCADERECOMP_ZN_DUMP_VRAM")) {
        std::ofstream dump(dump_path, std::ios::binary);
        const auto& vram = machine.memory().vram();
        if (!dump) throw std::runtime_error("could not create ZN VRAM dump " + std::string(dump_path));
        dump.write(reinterpret_cast<const char*>(vram.data()),
                   static_cast<std::streamsize>(vram.size()));
        if (!dump) throw std::runtime_error("failed writing ZN VRAM dump " + std::string(dump_path));
        std::cout << "Wrote " << vram.size() << " bytes of ZN VRAM to " << dump_path << '\n';
    }
    if (const char* dump_path = std::getenv("MULTIARCADERECOMP_ZN_DUMP_RAM")) {
        std::ofstream dump(dump_path, std::ios::binary);
        const auto& ram = machine.memory().ram();
        if (!dump) throw std::runtime_error("could not create ZN RAM dump " + std::string(dump_path));
        dump.write(reinterpret_cast<const char*>(ram.data()),
                   static_cast<std::streamsize>(ram.size()));
        if (!dump) throw std::runtime_error("failed writing ZN RAM dump " + std::string(dump_path));
        std::cout << "Wrote " << ram.size() << " bytes of ZN RAM to " << dump_path << '\n';
    }
    if (frame_out) {
        if (!frame_out->parent_path().empty())
            std::filesystem::create_directories(frame_out->parent_path());
        std::ofstream output(*frame_out, std::ios::binary);
        if (!output) throw std::runtime_error("could not create ZN frame output " + frame_out->string());
        output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(frame.rgb.data()),
                     static_cast<std::streamsize>(frame.rgb.size()));
        if (!output) throw std::runtime_error("failed writing ZN framebuffer " + frame_out->string());
        std::cout << "Wrote " << frame.width << 'x' << frame.height
                  << " ZN framebuffer to " << *frame_out << '\n';
    }
    if (nvram_path) {
        std::filesystem::create_directories(nvram_path->parent_path());
        std::ofstream nvram_file(*nvram_path, std::ios::binary | std::ios::trunc);
        const auto image = machine.nvram();
        if (!nvram_file) throw std::runtime_error("could not save AT28C16 NVRAM " + nvram_path->string());
        nvram_file.write(reinterpret_cast<const char *>(image.data()),
                         static_cast<std::streamsize>(image.size()));
        if (!nvram_file) throw std::runtime_error("failed saving AT28C16 NVRAM " + nvram_path->string());
        std::cout << "Saved " << image.size() << " bytes of AT28C16 NVRAM to " << *nvram_path << '\n';
    }
    if (std::getenv("MULTIARCADERECOMP_ZN_PROFILE_RUNTIME")) {
        const auto total_ns = machine.profile_run_until_ns();
        const auto gpu_ns = machine.memory().gpu().profile_execute_packet_ns();
        const auto sound_ns = machine.profile_zn2_sound_ns();
        const auto gte_ns = machine.memory().profile_gte_ns();
        const auto remainder_ns = total_ns > gpu_ns + sound_ns + gte_ns
            ? total_ns - gpu_ns - sound_ns - gte_ns : 0;
        const auto milliseconds = [](uint64_t ns) {
            return static_cast<double>(ns) / 1'000'000.0;
        };
        std::cout << "ZN runtime profile (headless execution): run_until="
                  << milliseconds(total_ns) << " ms, GP0 packet raster/dispatch="
                  << milliseconds(gpu_ns) << " ms / "
                  << machine.memory().gpu().profile_execute_packet_calls()
                  << " packets, Z80+QSound=" << milliseconds(sound_ns)
                  << " ms, GTE=" << milliseconds(gte_ns) << " ms / "
                  << machine.memory().profile_gte_commands()
                  << " commands, R3000A+bus+timers remainder="
                  << milliseconds(remainder_ns) << " ms.\n";
        if (game == "sfex2pa")
            std::cout << "  QSound breakdown: DSP16A run="
                      << milliseconds(machine.profile_zn2_dsp_ns())
                      << " ms, Z80 run=" << milliseconds(machine.profile_zn2_z80_ns())
                      << " ms, QSound render="
                      << milliseconds(machine.profile_zn2_qsound_render_ns()) << " ms.\n";
        if (game == "sfex2pa") {
            const auto dsp_slices = machine.profile_zn2_dsp_slice_calls();
            const auto total_dsp_clocks = machine.profile_zn2_dsp_slice_clocks();
            std::cout << "  QSound scheduling: sync requests="
                      << machine.profile_zn2_qsound_sync_calls()
                      << ", DSP run slices=" << dsp_slices
                      << ", clocks/slice avg="
                      << (dsp_slices ? total_dsp_clocks / dsp_slices : 0)
                      << " min=" << machine.profile_zn2_dsp_slice_min_clocks()
                      << " max=" << machine.profile_zn2_dsp_slice_max_clocks()
                      << ", total clocks=" << total_dsp_clocks << ".\n";
        }
    }
    const auto& coverage = machine.counters();
    std::cout << "ZN instructions=" << machine.cpu().instructions
              << " cycles=" << machine.cpu().cycles << " PC=0x" << std::hex << machine.cpu().pc
              << std::dec << " AOT=" << coverage.aot_instructions
              << " interpreter=" << coverage.fallback_instructions
              << " idle-loop=" << coverage.idle_loop_instructions
              << " display=" << (frame.display_enabled ? "enabled" : "disabled")
              << " framebuffer=" << frame.width << 'x' << frame.height
              << " frame_updates=" << frame_updates << ".\n";
    if (game == "tekken3")
        std::cout << "System 12 H8/3002 interpreter cycles="
                  << machine.system12_h8_cycles() << " PC=0x" << std::hex
                  << machine.system12_h8_pc() << std::dec
                  << " C352 frames=" << system12_audio_frames
                  << " C352 writes=" << machine.system12_c352_writes()
                  << " active_voices=" << machine.system12_active_audio_voices()
                  << " peak=" << system12_audio_peak
                  << " sample_rate=" << machine.system12_audio_sample_rate() << ".\n";
    if (game == "bldyror2")
        std::cout << "ZN-1 audio MC68000 AOT=0 fallback instructions="
                  << machine.zn1_sound_instructions()
                  << " YMF271 register writes=" << machine.zn1_ymf_writes()
                  << " samples=" << machine.zn1_ymf_samples()
                  << " peak=" << machine.zn1_ymf_peak()
                  << " sample_rate=" << machine.zn1_audio_sample_rate()
                  << " active_slots=" << machine.zn1_active_ymf_slots() << ".\n";
    if (game == "sfex2pa")
        std::cout << "ZN-2 QSound Z80 AOT=" << machine.zn2_z80_aot_instructions()
                  << " fallback=" << machine.zn2_z80_fallback_instructions()
                  << " audio samples="
                  << machine.zn2_qsound_samples()
                  << " peak=" << machine.zn2_qsound_peak()
                  << " sample_rate=" << machine.zn2_qsound_sample_rate() << ".\n";
    if (game == "sfex2pa" && machine.zn2_qsound_mode() == multiarcaderecomp::chips::QSoundMode::Lle)
        std::cout << "ZN-2 DSP16A LLE cycles=" << machine.zn2_qsound_dsp_cycles()
                  << " instructions=" << machine.zn2_qsound_dsp_instructions()
                  << " (AOT=" << machine.zn2_qsound_dsp_aot_instructions()
                  << " interpreter=" << machine.zn2_qsound_dsp_interpreted_instructions() << ')'
                  << " PC=0x" << std::hex << machine.zn2_qsound_dsp_pc() << std::dec
                  << " sample-ROM reads=" << machine.zn2_qsound_sample_reads()
                  << " PIO reads/writes=" << machine.zn2_qsound_pio_reads() << '/'
                  << machine.zn2_qsound_pio_writes()
                  << " serial words=" << machine.zn2_qsound_serial_words()
                  << " (L/R=" << machine.zn2_qsound_serial_left_words() << '/'
                  << machine.zn2_qsound_serial_right_words() << ')'
                  << " PCM frames=" << machine.zn2_qsound_pcm_frames()
                  << " nonzero sample reads=" << machine.zn2_qsound_nonzero_sample_reads()
                  << " sample ROM unmapped/mirrored=" << machine.zn2_qsound_unmapped_sample_reads()
                  << '/' << machine.zn2_qsound_mirrored_sample_reads()
                  << " nonzero serial words=" << machine.zn2_qsound_nonzero_serial_words()
                  << " serial word peak=" << machine.zn2_qsound_serial_word_peak() << ".\n";
    if (game == "sfex2pa" && machine.zn2_qsound_mode() == multiarcaderecomp::chips::QSoundMode::Lle)
        std::cout << "ZN-2 DSP16A pins: OCK rising=" << machine.zn2_qsound_dsp_ock_edges()
                  << " DO transitions=" << machine.zn2_qsound_dsp_do_transitions()
                  << " OSE transitions=" << machine.zn2_qsound_dsp_ose_transitions()
                  << " OLD transitions=" << machine.zn2_qsound_dsp_old_transitions()
                  << " serial word loads=" << machine.zn2_qsound_dsp_serial_loads()
                  << " SDX writes=" << machine.zn2_qsound_dsp_sdx_writes()
                  << " nonzero SDX=" << machine.zn2_qsound_dsp_nonzero_sdx_writes()
                  << " nonzero serial loads=" << machine.zn2_qsound_dsp_nonzero_serial_loads()
                  << " last SDX/load=0x" << std::hex << machine.zn2_qsound_dsp_last_sdx()
                  << "/0x" << machine.zn2_qsound_dsp_last_serial_loaded() << std::dec << ".\n";
    if (game == "sfex2pa" && machine.zn2_qsound_mode() == multiarcaderecomp::chips::QSoundMode::Lle)
        std::cout << "ZN-2 DSP16A last SDX at PC=0x" << std::hex
                  << machine.zn2_qsound_dsp_last_sdx_pc() << " opcode=0x"
                  << machine.zn2_qsound_dsp_last_sdx_opcode() << " A0=0x"
                  << machine.zn2_qsound_dsp_last_sdx_a0() << " A1=0x"
                  << machine.zn2_qsound_dsp_last_sdx_a1() << " X=0x"
                  << machine.zn2_qsound_dsp_last_sdx_x() << " Y=0x"
                  << machine.zn2_qsound_dsp_last_sdx_y() << " P=0x"
                  << machine.zn2_qsound_dsp_last_sdx_p() << std::dec << ".\n";
    if (game == "sfex2pa" && zn_diagnostics) {
        std::cerr << "ZN-2 QSound voice peaks:";
        const auto &peaks = machine.zn2_qsound_voice_peaks();
        for (size_t voice = 0; voice < peaks.size(); ++voice)
            std::cerr << ' ' << voice << '=' << peaks[voice];
        std::cerr << '\n';
    }
    return 0;
}

} // namespace multiarcaderecomp::app::machine_runners
