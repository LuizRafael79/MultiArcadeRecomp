// Diagnostic runner for the MK rev. 5 T-Unit AOT translation. It executes a
// bounded number of TMS34010 cycles and writes a scanout snapshot; it is not a
// real-time machine frontend (beam timing and MC6809E scheduling are pending).
#include "mk/mk_machine.hpp"
#include "core/cpus/tms34010/interpreter.hpp"
#include "core/cpus/tms34010/runtime.hpp"
#include "core/cpus/m6809/interpreter.hpp"
#include "core/cpus/interpreter_trace.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifdef MAMERECOMP_MK_SDL
#include <SDL3/SDL.h>
#endif

namespace {
using namespace mamerecomp::tms34010;

std::ofstream tms34010_fallback_event_log;
std::uint64_t tms34010_fallback_event_count{};
std::ofstream m6809_fallback_event_log;
std::uint64_t m6809_fallback_event_count{};

struct InputReplayEvent {
    std::uint64_t cycle{};
    std::uint16_t in0{0xffff};
    std::uint16_t in1{0xffff};
};

std::vector<InputReplayEvent> read_input_replay(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open input replay " + path);
    std::vector<InputReplayEvent> events;
    InputReplayEvent event;
    unsigned in0 = 0xffff, in1 = 0xffff;
    while (input >> event.cycle >> std::hex >> in0 >> in1 >> std::dec) {
        event.in0 = static_cast<std::uint16_t>(in0);
        event.in1 = static_cast<std::uint16_t>(in1);
        events.push_back(event);
    }
    if (!input.eof()) throw std::runtime_error("invalid input replay " + path);
    return events;
}

void log_tms34010_fallback(std::uint32_t pc, std::uint16_t opcode,
                           MamerecompFallbackReason reason) {
    if (!tms34010_fallback_event_log) return;
    tms34010_fallback_event_log << "[TMS34010 interpreter] PC=0x"
        << std::hex << pc << " OPCODE=0x" << opcode << std::dec
        << " CATEGORY=" << mamerecomp_fallback_reason_name(reason) << '\n';
    if ((++tms34010_fallback_event_count % 128) == 0)
        tms34010_fallback_event_log.flush();
}

void log_m6809_fallback(std::uint16_t pc, std::uint8_t opcode,
                        MamerecompFallbackReason reason) {
    if (!m6809_fallback_event_log) return;
    m6809_fallback_event_log << "[MC6809E interpreter] PC=0x"
        << std::hex << pc << " OPCODE=0x" << unsigned(opcode) << std::dec
        << " CATEGORY=" << mamerecomp_fallback_reason_name(reason) << '\n';
    if ((++m6809_fallback_event_count % 128) == 0)
        m6809_fallback_event_log.flush();
}

void on_untranslated(void*, std::uint32_t pc, std::uint16_t opcode) {
    std::cerr << "untranslated TMS34010 entry: PC=0x" << std::hex << pc
              << " opcode=0x" << opcode << '\n';
}

struct TraceEntry {
    std::uint32_t pc;
    std::uint16_t opcode;
    std::uint32_t sp;
    std::uint64_t cycles;
};

struct TraceRing {
    std::array<TraceEntry, 32> entries{};
    std::array<TraceEntry, 64> stack_changes{};
    std::size_t next{};
    std::size_t count{};
    std::size_t change_next{};
    std::size_t change_count{};
    std::uint32_t previous_sp{};
    bool have_previous_sp{};
};

std::uint8_t sound_bus_read(void* opaque, std::uint16_t address) {
    return static_cast<mamerecomp::mk::MKMachine*>(opaque)->
        sound_cpu_read(address);
}

void sound_bus_write(void* opaque, std::uint16_t address, std::uint8_t value) {
    static_cast<mamerecomp::mk::MKMachine*>(opaque)->
        sound_cpu_write(address, value);
}

std::uint8_t sound_bus_bank(void* opaque) {
    return static_cast<mamerecomp::mk::MKMachine*>(opaque)->sound_cpu_bank();
}

struct SoundCpuRunner {
    struct TraceEntry {
        std::uint16_t pc{};
        std::uint8_t opcode{};
        std::uint8_t bank{};
        std::uint8_t a{};
        std::uint8_t b{};
        std::uint8_t cc{};
        std::uint16_t s{};
    };

    explicit SoundCpuRunner(mamerecomp::mk::MKMachine& machine)
        : machine(machine), bus{&machine, &sound_bus_read, &sound_bus_write,
                            &sound_bus_bank} {}

    mamerecomp::mk::MKMachine& machine;
    mamerecomp::m6809::State state{};
    mamerecomp::m6809::Bus bus{};
    std::uint64_t main_cycle_remainder{};
    std::uint64_t sound_cycle_target{};
    std::uint64_t native_instructions{};
    std::uint64_t interpreted_instructions{};
    std::set<std::pair<std::uint16_t, std::uint8_t>> logged_fallbacks;
    std::array<TraceEntry, 64> recent{};
    std::size_t recent_next{};
    std::size_t recent_count{};
    bool logged_swi{};
    bool logged_rom_to_ram_jump{};
    bool reset_released{};
    bool stalled_on_unsupported{};

    void advance_main_cycles(std::uint64_t main_cycles,
                             bool force_interpreter = false) {
        // Keep sound hardware time tied to the TMS clock. The 6809 executes
        // whole instructions, so its instruction-cycle counter can be a few
        // cycles ahead of this target; that excess is debt, not extra elapsed
        // time. Accumulating a target avoids rounding every tiny TMS slice up
        // to at least one complete 6809 instruction.
        main_cycle_remainder += main_cycles * 8;
        const std::uint64_t sound_cycles = main_cycle_remainder / 25;
        main_cycle_remainder %= 25;
        sound_cycle_target += sound_cycles;
        if (sound_cycles) machine.advance_sound_cycles(
            static_cast<std::uint32_t>(sound_cycles));

        if (machine.sound_cpu_reset_asserted()) {
            reset_released = false;
            stalled_on_unsupported = false;
            state = {};
            state.cycles = sound_cycle_target;
            return;
        }
        if (!reset_released) {
            state = {};
            state.cycles = sound_cycle_target;
            state.pc = mamerecomp::m6809::reset_vector(bus);
            state.cc = mamerecomp::m6809::cc_i | mamerecomp::m6809::cc_f;
            reset_released = true;
            stalled_on_unsupported = false;
            std::cerr << "[MC6809E] reset vector PC=0x" << std::hex
                      << state.pc << std::dec << '\n';
        }
        if (stalled_on_unsupported) return;
        while (state.cycles < sound_cycle_target) {
            state.irq = machine.sound_cpu_irq_asserted();
            state.firq = machine.sound_cpu_firq_asserted();
            if (mamerecomp::m6809::service_interrupt(state, bus))
                continue;
            const auto pc = state.pc;
            const auto opcode = bus.read8(bus.opaque, pc);
            const auto bank = static_cast<std::uint8_t>(
                bus.rom_bank ? bus.rom_bank(bus.opaque) : 0);
            recent[recent_next] = {pc, opcode, bank, state.a, state.b,
                                   state.cc, state.s};
            recent_next = (recent_next + 1) % recent.size();
            recent_count = std::min(recent_count + 1, recent.size());
            if (opcode == 0x3f && !logged_swi) {
                logged_swi = true;
                std::cerr << "[MC6809E SWI context] bank=0x" << std::hex
                          << unsigned(bank) << " PC=0x" << pc << std::dec << '\n';
                const auto first = (recent_next + recent.size() - recent_count) %
                                   recent.size();
                for (std::size_t i = 0; i < recent_count; ++i) {
                    const auto& entry = recent[(first + i) % recent.size()];
                    std::cerr << "  PC=0x" << std::hex << entry.pc
                              << " OP=0x" << unsigned(entry.opcode)
                              << " bank=0x" << unsigned(entry.bank)
                              << " A=0x" << unsigned(entry.a)
                              << " B=0x" << unsigned(entry.b)
                              << " CC=0x" << unsigned(entry.cc)
                              << " S=0x" << entry.s << std::dec << '\n';
                }
            }
            auto result = mamerecomp::m6809::BlockExit::NeedsInterpreter;
            MamerecompFallbackReason reason = MAMERECOMP_UNCOVERED_ENTRY;
#ifdef MAMERECOMP_M6809_HAS_AOT
            if (!force_interpreter) if (const auto block = mamerecomp::m6809::lookup(
                    state.pc, bus.rom_bank ? bus.rom_bank(bus.opaque) : 0)) {
                result = block(state, bus, sound_cycle_target);
                if (result == mamerecomp::m6809::BlockExit::NeedsInterpreter)
                    reason = MAMERECOMP_UNSUPPORTED_OPCODE;
                if (result == mamerecomp::m6809::BlockExit::Dispatch)
                    ++native_instructions;
            }
#endif
            if (result == mamerecomp::m6809::BlockExit::NeedsInterpreter) {
                if (logged_fallbacks.emplace(pc, opcode).second)
                    log_m6809_fallback(pc, opcode, reason);
                result = mamerecomp::m6809::interpreter_step(
                    state, bus, sound_cycle_target, reason);
                if (result == mamerecomp::m6809::BlockExit::Dispatch ||
                    result == mamerecomp::m6809::BlockExit::Budget)
                    ++interpreted_instructions;
            }
            if (result == mamerecomp::m6809::BlockExit::NeedsInterpreter ||
                result == mamerecomp::m6809::BlockExit::BusFault) {
                std::cerr << "[MC6809E stalled] PC=0x" << std::hex << state.pc
                          << " opcode=0x" << unsigned(bus.read8(bus.opaque, state.pc))
                          << " bank=0x" << unsigned(bank) << std::dec << '\n';
                const auto first = (recent_next + recent.size() - recent_count) %
                                   recent.size();
                for (std::size_t i = 0; i < recent_count; ++i) {
                    const auto& entry = recent[(first + i) % recent.size()];
                    std::cerr << "  PC=0x" << std::hex << entry.pc
                              << " OP=0x" << unsigned(entry.opcode)
                              << " bank=0x" << unsigned(entry.bank)
                              << " A=0x" << unsigned(entry.a)
                              << " B=0x" << unsigned(entry.b)
                              << " CC=0x" << unsigned(entry.cc)
                              << " S=0x" << entry.s << std::dec << '\n';
                }
                stalled_on_unsupported = true;
                break;
            }
            if (pc >= 0x4000 && state.pc < 0x2000 &&
                !logged_rom_to_ram_jump) {
                logged_rom_to_ram_jump = true;
                std::cerr << "[MC6809E ROM-to-RAM jump] PC=0x" << std::hex
                          << pc << " OP=0x" << unsigned(opcode)
                          << " next=0x" << state.pc << " bank=0x"
                          << unsigned(bank) << std::dec << '\n';
                const auto first = (recent_next + recent.size() - recent_count) %
                                   recent.size();
                for (std::size_t i = 0; i < recent_count; ++i) {
                    const auto& entry = recent[(first + i) % recent.size()];
                    std::cerr << "  PC=0x" << std::hex << entry.pc
                              << " OP=0x" << unsigned(entry.opcode)
                              << " bank=0x" << unsigned(entry.bank)
                              << " A=0x" << unsigned(entry.a)
                              << " B=0x" << unsigned(entry.b)
                              << " CC=0x" << unsigned(entry.cc)
                              << " S=0x" << entry.s << std::dec << '\n';
                }
            }
            if (result == mamerecomp::m6809::BlockExit::Budget) break;
        }
    }
};

void record_trace(void* opaque, std::uint32_t pc, std::uint16_t opcode,
                  std::uint32_t sp, std::uint64_t cycles) {
    auto& ring = *static_cast<TraceRing*>(opaque);
    const TraceEntry entry{pc, opcode, sp, cycles};
    ring.entries[ring.next] = entry;
    ring.next = (ring.next + 1) % ring.entries.size();
    ring.count = std::min(ring.count + 1, ring.entries.size());
    if (ring.have_previous_sp && ring.previous_sp != sp) {
        ring.stack_changes[ring.change_next] = entry;
        ring.change_next = (ring.change_next + 1) % ring.stack_changes.size();
        ring.change_count = std::min(ring.change_count + 1,
                                     ring.stack_changes.size());
    }
    ring.previous_sp = sp;
    ring.have_previous_sp = true;
}

void print_trace(const TraceRing& ring) {
    std::cerr << "recent TMS34010 dispatches (PC/op/SP/cycles):\n";
    const std::size_t first = (ring.next + ring.entries.size() - ring.count) %
                              ring.entries.size();
    for (std::size_t i = 0; i < ring.count; ++i) {
        const auto& entry = ring.entries[(first + i) % ring.entries.size()];
        std::cerr << "  0x" << std::hex << entry.pc << " / 0x" << entry.opcode
                  << " / 0x" << entry.sp << " / " << std::dec << entry.cycles
                  << '\n';
    }
    std::cerr << "recent stack-pointer changes (PC/op/SP/cycles):\n";
    const std::size_t change_first =
        (ring.change_next + ring.stack_changes.size() - ring.change_count) %
        ring.stack_changes.size();
    for (std::size_t i = 0; i < ring.change_count; ++i) {
        const auto& entry = ring.stack_changes[
            (change_first + i) % ring.stack_changes.size()];
        std::cerr << "  0x" << std::hex << entry.pc << " / 0x" << entry.opcode
                  << " / 0x" << entry.sp << " / " << std::dec << entry.cycles
                  << '\n';
    }
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}

void save_cmos_file(const std::string& path,
                    std::span<const std::uint8_t> data) {
    const std::filesystem::path target(path);
    const auto temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write " + temporary);
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        if (!output) throw std::runtime_error("failed writing " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot replace CMOS file " + path + ": " +
                                 error.message());
    }
}

constexpr unsigned kFrameWidth = mamerecomp::mk::TUnit::frame_width;
constexpr unsigned kFrameHeight = mamerecomp::mk::TUnit::frame_height;

bool service_interrupt(State& state, Bus& bus,
                       mamerecomp::mk::TUnit& machine) noexcept {
    constexpr std::uint32_t kIe = 1u << 21;
    if (!bus.read16) return false;
    constexpr unsigned kRegHstctlH = 16;
    const std::uint16_t hstctlh = machine.io_register(kRegHstctlH);
    if (hstctlh & 0x0100) {
        if (!bus.write16) return false;
        const std::uint32_t interrupted_pc = state.pc;
        const std::uint64_t cycles_before = state.cycles;
        if (!(hstctlh & 0x0200)) {
            state.sp -= 32;
            if (!write_long(bus, state.sp, state.pc)) return false;
            state.sp -= 32;
            if (!write_long(bus, state.sp, state.st)) return false;
        }
        bus.write16(bus.opaque,
                    mamerecomp::mk::TUnit::internal_io_base +
                        kRegHstctlH * 16u,
                    static_cast<std::uint16_t>(hstctlh & ~0x0100u));
        state.st = 0x10;
        if (!read_long(bus, 0xfffffee0u, state.pc)) return false;
        state.pc &= 0xfffffff0u;
        state.cycles += 16;
        mamerecomp_interpreter_trace_event("TMS34010", "aot-dispatch",
            MAMERECOMP_EXCEPTION_BOUNDARY, interrupted_pc, state.pc,
            cycles_before, state.cycles);
        return true;
    }
    if (!(state.st & kIe)) return false;
    const auto pending = bus.read16(bus.opaque, mamerecomp::mk::TUnit::internal_io_base + 18u * 16u);
    const auto enabled = bus.read16(bus.opaque, mamerecomp::mk::TUnit::internal_io_base + 17u * 16u);
    const auto irq = static_cast<std::uint16_t>(pending & enabled);
    std::uint32_t vector = 0;
    bool dma_irq = false;
    if (irq & 0x0200) vector = 0xfffffec0u;       // host interrupt
    else if (irq & 0x0400) vector = 0xfffffea0u;  // display interrupt
    else if (irq & 0x0800) vector = 0xfffffe80u;  // window violation
    else if (irq & 0x0002) { vector = 0xffffffc0u; dma_irq = true; }
    else if (irq & 0x0004) vector = 0xffffffa0u;  // external 2
    if (!vector) return false;
    const std::uint32_t interrupted_pc = state.pc;
    const std::uint64_t cycles_before = state.cycles;
    state.sp -= 32;
    if (!write_long(bus, state.sp, state.pc)) return false;
    state.sp -= 32;
    if (!write_long(bus, state.sp, state.st)) return false;
    state.st = 0x10;
    if (!read_long(bus, vector, state.pc)) return false;
    state.pc &= 0xfffffff0u;
    if (dma_irq) machine.acknowledge_interrupt(0x0002);
    state.cycles += 16;
    mamerecomp_interpreter_trace_event("TMS34010", "mame-compatible-fallback",
        MAMERECOMP_EXCEPTION_BOUNDARY, interrupted_pc, state.pc, cycles_before,
        state.cycles);
    return true;
}

void write_rgb_snapshot(std::span<const std::uint32_t> frame,
                        const std::string& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write " + path);
    output << "P6\n" << kFrameWidth << ' ' << kFrameHeight << "\n255\n";
    for (const auto pixel : frame) {
        const char rgb[3]{static_cast<char>(pixel >> 16),
                          static_cast<char>(pixel >> 8),
                          static_cast<char>(pixel)};
        output.write(rgb, sizeof(rgb));
    }
}

void write_vram_page_snapshot(const mamerecomp::mk::TUnit& machine,
                              unsigned page_base,
                              const std::string& path) {
    std::vector<std::uint32_t> frame(
        static_cast<std::size_t>(kFrameWidth) * kFrameHeight, 0xff000000u);
    const auto vram = machine.video_ram();
    const auto params = machine.current_display_params();
    const unsigned first_x = std::min<unsigned>(params.hblank_start, kFrameWidth);
    const unsigned last_x = std::min<unsigned>(params.hblank_end, kFrameWidth);
    const std::uint32_t first_column = params.column_address << 1;
    const unsigned first_y = std::min<unsigned>(params.visible_start, kFrameHeight);
    const unsigned last_y = std::min<unsigned>(params.blank_start, kFrameHeight);
    for (unsigned y = first_y; y < last_y; ++y) {
        const unsigned source_y = (page_base + y - first_y) & 0x1ff;
        const std::size_t source_base = static_cast<std::size_t>(source_y) * 512;
        const std::size_t dest_base = static_cast<std::size_t>(y) * kFrameWidth;
        for (unsigned x = first_x; x < last_x; ++x) {
            const unsigned column = (first_column + x - first_x) & 0x1ff;
            const unsigned pen = vram[source_base + column] & 0x7fff;
            frame[dest_base + x] = machine.palette_rgb(pen);
        }
    }
    write_rgb_snapshot(frame, path);
}

#ifdef MAMERECOMP_MK_SDL
struct LiveWindow {
    using Clock = std::chrono::steady_clock;
    static constexpr auto frame_period = std::chrono::nanoseconds(18'279'250);

    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    SDL_AudioStream* audio_stream{};
    bool running{true};
    bool coin1_pulse{};
    bool coin2_pulse{};
    bool frame_deadline_initialized{};
    Clock::time_point next_frame_deadline{};
    std::uint64_t presented_frames{};
    std::uint64_t late_frames{};

    LiveWindow() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL video/audio init failed: ") + SDL_GetError());
        window = SDL_CreateWindow("Mortal Kombat - T-Unit (prototype)", 1012, 578,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(std::string("SDL window failed: ") + SDL_GetError());
        renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) throw std::runtime_error(std::string("SDL renderer failed: ") + SDL_GetError());
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    kFrameWidth, kFrameHeight);
        if (!texture) throw std::runtime_error(std::string("SDL texture failed: ") + SDL_GetError());
        const SDL_AudioSpec audio_spec{SDL_AUDIO_S16, 2, 55'930};
        audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
        if (!audio_stream)
            throw std::runtime_error(std::string("SDL audio stream failed: ") + SDL_GetError());
        if (!SDL_ResumeAudioStreamDevice(audio_stream))
            throw std::runtime_error(std::string("SDL audio start failed: ") + SDL_GetError());
    }

    ~LiveWindow() {
        if (audio_stream) SDL_DestroyAudioStream(audio_stream);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void poll_and_set_inputs(mamerecomp::mk::TUnit& machine) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
                running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_5) coin1_pulse = true;
                if (event.key.key == SDLK_6) coin2_pulse = true;
            }
        }
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const auto held = [&](SDL_Scancode key) { return keys && keys[key]; };
        std::uint16_t controls = 0xffff;
        const auto press = [](std::uint16_t& port, bool down, unsigned bit) {
            if (down) port &= static_cast<std::uint16_t>(~(1u << bit));
        };
        // P1: WASD + J/K/L (high punch/block/high kick), U/I/O (low punch,
        // low kick, alternate block). P2: arrows + keypad 1-6.
        press(controls, held(SDL_SCANCODE_W), 0); press(controls, held(SDL_SCANCODE_S), 1);
        press(controls, held(SDL_SCANCODE_A), 2); press(controls, held(SDL_SCANCODE_D), 3);
        press(controls, held(SDL_SCANCODE_J) || held(SDL_SCANCODE_SPACE), 4);
        press(controls, held(SDL_SCANCODE_K), 5);
        press(controls, held(SDL_SCANCODE_L), 6);
        press(controls, held(SDL_SCANCODE_UP), 8); press(controls, held(SDL_SCANCODE_DOWN), 9);
        press(controls, held(SDL_SCANCODE_LEFT), 10); press(controls, held(SDL_SCANCODE_RIGHT), 11);
        press(controls, held(SDL_SCANCODE_KP_1), 12); press(controls, held(SDL_SCANCODE_KP_2), 13);
        press(controls, held(SDL_SCANCODE_KP_3), 14);
        std::uint16_t system = 0xffff;
        press(system, coin1_pulse, 0); press(system, coin2_pulse, 1);
        press(system, held(SDL_SCANCODE_1) || held(SDL_SCANCODE_RETURN), 2);
        press(system, held(SDL_SCANCODE_2), 5);
        press(system, held(SDL_SCANCODE_F2), 6);
        press(system, held(SDL_SCANCODE_U), 12); press(system, held(SDL_SCANCODE_I), 13);
        press(system, held(SDL_SCANCODE_O), 15);
        press(system, held(SDL_SCANCODE_KP_4), 9); press(system, held(SDL_SCANCODE_KP_5), 10);
        press(system, held(SDL_SCANCODE_KP_6), 11);
        machine.set_input(0, controls);
        machine.set_input(1, system);
        machine.set_input(2, 0xffff);
        machine.set_input(3, 0xfe7d); // MAME mk DIP defaults.
        coin1_pulse = coin2_pulse = false;
    }

    void present(std::span<const std::uint32_t> frame,
                 const mamerecomp::mk::TUnit::DisplayParams& display,
                 std::span<const std::int16_t> audio_samples) {
        if (!audio_samples.empty() &&
            !SDL_PutAudioStreamData(audio_stream, audio_samples.data(),
                static_cast<int>(audio_samples.size_bytes())))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        SDL_UpdateTexture(texture, nullptr, frame.data(), kFrameWidth * sizeof(std::uint32_t));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        const int crop_left = std::clamp<int>(display.hblank_start, 0, kFrameWidth);
        const int crop_right = std::clamp<int>(display.hblank_end, crop_left, kFrameWidth);
        const int crop_top = std::clamp<int>(display.visible_start, 0, kFrameHeight);
        const int crop_bottom = std::clamp<int>(display.blank_start, crop_top, kFrameHeight);
        const SDL_FRect source{
            static_cast<float>(crop_left), static_cast<float>(crop_top),
            static_cast<float>(crop_right - crop_left),
            static_cast<float>(crop_bottom - crop_top)};

        int output_width = 0;
        int output_height = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &output_width, &output_height);
        constexpr float display_aspect = 4.0f / 3.0f;
        float dest_width = static_cast<float>(output_width);
        float dest_height = dest_width / display_aspect;
        if (dest_height > output_height) {
            dest_height = static_cast<float>(output_height);
            dest_width = dest_height * display_aspect;
        }
        const SDL_FRect destination{
            (static_cast<float>(output_width) - dest_width) * 0.5f,
            (static_cast<float>(output_height) - dest_height) * 0.5f,
            dest_width, dest_height};
        SDL_RenderTexture(renderer, texture, &source, &destination);
        SDL_RenderPresent(renderer);
        const int queued_bytes = std::max(0, SDL_GetAudioStreamQueued(audio_stream));
        const double queued_ms = queued_bytes * 1000.0 /
            (55'930.0 * 2.0 * sizeof(std::int16_t));

        // T-Unit timing is 8 MHz / (506 * 289) ~= 54.7 Hz. Keep the host
        // presentation in real time without skipping emulated frames.
        const auto now = Clock::now();
        if (!frame_deadline_initialized) {
            next_frame_deadline = now;
            frame_deadline_initialized = true;
        }
        const auto previous_deadline = next_frame_deadline;
        next_frame_deadline += frame_period;
        if (now < next_frame_deadline)
            std::this_thread::sleep_until(next_frame_deadline);
        else {
            ++late_frames;
            next_frame_deadline = now;
        }
        if ((++presented_frames % 120) == 0) {
            const double render_ms = std::chrono::duration<double, std::milli>(
                now - previous_deadline).count();
            std::cerr << "[MK pacing/audio] frames=" << presented_frames
                      << " late=" << late_frames << " render-ms=" << render_ms
                      << " queued-audio-ms=" << queued_ms
                      << " added-sample-frames=" << (audio_samples.size() / 2) << '\n';
        }
    }
};
#endif

BlockExit run_scheduled(State& cpu, Bus& bus,
                        std::uint64_t stop_cycle,
                        mamerecomp::mk::TUnit& machine,
                        SoundCpuRunner& sound_cpu,
                        TraceRing& trace,
                        std::uint64_t& native_instructions,
                        std::uint64_t& interpreted_instructions,
                        std::set<std::pair<std::uint32_t, std::uint16_t>>& logged_fallbacks,
                        std::ostream* cpu_timeline,
                        std::ostream* graphics_op_trace,
                        std::uint64_t& next_timeline_cycle,
                        std::uint64_t timeline_interval,
                        const std::vector<InputReplayEvent>& input_replay,
                        std::size_t& input_replay_index,
                        bool scripted_start,
                        std::uint64_t start_cycle,
                        bool scripted_start_release,
                        std::uint64_t start_release_cycle,
                        bool scripted_start_again,
                        std::uint64_t start_again_cycle,
                        bool scripted_coin,
                        std::uint64_t coin_cycle,
                        bool scripted_coin_release,
                        std::uint64_t coin_release_cycle,
                        bool scripted_button,
                        std::uint64_t button_cycle,
                        bool scripted_button_release,
                        std::uint64_t button_release_cycle,
                        bool scripted_down,
                        std::uint64_t down_cycle,
                        bool scripted_down_release,
                        std::uint64_t down_release_cycle,
                        bool scripted_punch,
                        std::uint64_t punch_cycle,
                        bool scripted_punch_release,
                        std::uint64_t punch_release_cycle,
                        bool scripted_button_again,
                        std::uint64_t button_again_cycle,
                        const std::string& frame_dump_dir,
                        std::uint64_t frame_dump_from,
                        std::size_t frame_dump_limit,
                        std::size_t& frame_dumps_written,
                        bool force_interpreter
#ifdef MAMERECOMP_MK_SDL
                        , LiveWindow* window
#endif
                        ) {
    using namespace mamerecomp::tms34010;
    bool noted_ram_jump = false;
    bool noted_idle_jump = false;
    while (cpu.cycles < stop_cycle) {
        while (input_replay_index < input_replay.size() &&
               input_replay[input_replay_index].cycle <= cpu.cycles) {
            const auto& event = input_replay[input_replay_index++];
            machine.set_input(0, event.in0);
            machine.set_input(1, event.in1);
        }
        const bool start_down =
            (scripted_start && cpu.cycles >= start_cycle &&
             (!scripted_start_release || cpu.cycles < start_release_cycle)) ||
            (scripted_start_again && cpu.cycles >= start_again_cycle);
        if (scripted_start || scripted_start_again) {
            std::uint16_t system = machine.input(1);
            if (start_down) system &= static_cast<std::uint16_t>(~0x0004u);
            else system |= 0x0004u;
            machine.set_input(1, system); // IN1 START1 is active low.
        }
        if (scripted_coin) {
            std::uint16_t system = machine.input(1);
            const bool coin_down = cpu.cycles >= coin_cycle &&
                (!scripted_coin_release || cpu.cycles < coin_release_cycle);
            if (coin_down) system &= static_cast<std::uint16_t>(~0x0001u);
            else system |= 0x0001u;
            machine.set_input(1, system); // IN1 COIN1 is active low.
        }
        const bool button_down =
            (scripted_button && cpu.cycles >= button_cycle &&
             (!scripted_button_release || cpu.cycles < button_release_cycle)) ||
            (scripted_button_again && cpu.cycles >= button_again_cycle);
        const bool down_pressed = scripted_down && cpu.cycles >= down_cycle &&
            (!scripted_down_release || cpu.cycles < down_release_cycle);
        const bool punch_pressed = scripted_punch && cpu.cycles >= punch_cycle &&
            (!scripted_punch_release || cpu.cycles < punch_release_cycle);
        if (scripted_button || scripted_button_again || scripted_down ||
            scripted_punch) {
            std::uint16_t p1_controls = machine.input(0);
            if (scripted_button || scripted_button_again || scripted_punch) {
                if (button_down || punch_pressed)
                    p1_controls &= static_cast<std::uint16_t>(~0x0010u);
                else
                    p1_controls |= 0x0010u;
            }
            if (scripted_down) {
                if (down_pressed)
                    p1_controls &= static_cast<std::uint16_t>(~0x0002u);
                else
                    p1_controls |= 0x0002u;
            }
            machine.set_input(0, p1_controls); // IN0 is active low.
        }
        const auto pc = cpu.pc;
        machine.note_cpu_pc(pc);
        if ((pc == 0xffe224d0u && !noted_ram_jump) ||
            (pc == 0xffdd19c0u && !noted_idle_jump)) {
            const bool idle = pc == 0xffdd19c0u;
            (idle ? noted_idle_jump : noted_ram_jump) = true;
            std::cerr << "[TMS control checkpoint] PC=0x" << std::hex << pc
                      << " OP=0x" << bus.read16(bus.opaque, pc)
                      << " ST=0x" << cpu.st << " SP=0x" << cpu.sp
                      << " A0=0x" << reg(cpu, Bank::A, 0)
                      << " A1=0x" << reg(cpu, Bank::A, 1)
                      << " A2=0x" << reg(cpu, Bank::A, 2)
                      << " A3=0x" << reg(cpu, Bank::A, 3)
                      << " B0=0x" << reg(cpu, Bank::B, 0)
                      << " B6=0x" << reg(cpu, Bank::B, 6)
                      << " cycles=" << std::dec << cpu.cycles << '\n';
        }
        const auto opcode = bus.read16(bus.opaque, pc);
        const auto cycle_before = cpu.cycles;
        if (graphics_op_trace &&
            ((opcode & 0xffe0u) == 0x0fe0u ||
             (opcode & 0xffe0u) == 0x0fa0u)) {
            *graphics_op_trace << "cycles=" << std::dec << cpu.cycles
                << " PC=0x" << std::hex << pc << " OP=0x" << opcode
                << " B0=0x" << cpu.b[0] << " B1=0x" << cpu.b[1]
                << " B2=0x" << cpu.b[2] << " B3=0x" << cpu.b[3]
                << " B4=0x" << cpu.b[4] << " B5=0x" << cpu.b[5]
                << " B6=0x" << cpu.b[6] << " B7=0x" << cpu.b[7]
                << " B8=0x" << cpu.b[8] << " B9=0x" << cpu.b[9]
                << " CONTROL=0x" << machine.io_register(11)
                << " PSIZE=0x" << machine.io_register(21)
                << " DPYSTRT=0x" << machine.io_register(9)
                << " DPYADR=0x" << machine.io_register(30)
                << " INTPEND=0x" << machine.io_register(18) << std::dec << '\n';
        }
        record_trace(&trace, pc, opcode, cpu.sp, cpu.cycles);
        BlockExit result = BlockExit::NeedsInterpreter;
        MamerecompFallbackReason fallback_reason = MAMERECOMP_UNCOVERED_ENTRY;
        if (!force_interpreter) if (const auto block = lookup(cpu.pc)) {
            result = block(cpu, bus, stop_cycle);
            if (result == BlockExit::NeedsInterpreter)
                fallback_reason = classify_unsupported_opcode(opcode);
            else if (result == BlockExit::BusFault) {
                mamerecomp_interpreter_trace_event("TMS34010",
                    "aot-dispatch", MAMERECOMP_MEMORY_ACCESS, pc,
                    cpu.pc, cycle_before, cpu.cycles);
            } else if (result == BlockExit::Dispatch &&
                       opcode_causes_exception(opcode) && cpu.pc != pc + 16u) {
                mamerecomp_interpreter_trace_event("TMS34010",
                    "aot-dispatch", MAMERECOMP_EXCEPTION_BOUNDARY, pc,
                    cpu.pc, cycle_before, cpu.cycles);
            }
            if (result == BlockExit::Dispatch) ++native_instructions;
        } else if (bus.is_mutable_code &&
                   bus.is_mutable_code(bus.opaque, cpu.pc)) {
            fallback_reason = MAMERECOMP_DYNAMIC_CODE;
        }
        if (result == BlockExit::NeedsInterpreter) {
            if (logged_fallbacks.emplace(pc, opcode).second)
                log_tms34010_fallback(pc, opcode, fallback_reason);
            result = interpreter_step(cpu, bus, stop_cycle, fallback_reason);
            if (result == BlockExit::Dispatch) ++interpreted_instructions;
        }
        if (result == BlockExit::NeedsInterpreter) {
            on_untranslated(nullptr, cpu.pc, bus.read16(bus.opaque, cpu.pc));
            print_trace(trace);
            return result;
        }
        if (result != BlockExit::Dispatch) return result;
        const auto elapsed = cpu.cycles - cycle_before;
        machine.advance_cycles(elapsed);
        sound_cpu.advance_main_cycles(elapsed, force_interpreter);
        if (!frame_dump_dir.empty() && cpu.cycles >= frame_dump_from &&
            frame_dumps_written < frame_dump_limit) {
            const auto completed = machine.take_completed_frames();
            if (completed) {
                const auto params = machine.current_display_params();
                std::cerr << "[frame-snapshot] n=" << frame_dumps_written
                          << " cycles=" << cpu.cycles
                          << " DMA-commands="
                          << machine.dma_diagnostics().commands
                          << " DPYSTRT=0x" << std::hex << machine.io_register(9)
                          << " DPYADR=0x" << machine.io_register(30)
                          << " row=" << params.row_address
                          << " VCOUNT=" << params.vcount << std::dec << '\n';
                const auto path = frame_dump_dir + "/frame-" +
                    std::to_string(frame_dumps_written++) + ".ppm";
                write_rgb_snapshot(machine.framebuffer(), path);
            }
        }
        if (service_interrupt(cpu, bus, machine)) {
            machine.advance_cycles(16);
            sound_cpu.advance_main_cycles(16, force_interpreter);
        }
        if (cpu_timeline && cpu.cycles >= next_timeline_cycle) {
            *cpu_timeline << "cycles=" << std::dec << cpu.cycles
                          << " PC=0x" << std::hex << cpu.pc
                          << " OP=0x" << bus.read16(bus.opaque, cpu.pc)
                          << " ST=0x" << cpu.st
                          << " SP=0x" << cpu.sp
                          << " A0=0x" << reg(cpu, Bank::A, 0)
                          << " A1=0x" << reg(cpu, Bank::A, 1)
                          << " A2=0x" << reg(cpu, Bank::A, 2)
                          << " A3=0x" << reg(cpu, Bank::A, 3)
                          << " B0=0x" << reg(cpu, Bank::B, 0)
                          << " B6=0x" << reg(cpu, Bank::B, 6)
                          << " DPYCTL=0x" << machine.io_register(8)
                          << " DPYSTRT=0x" << machine.io_register(9)
                          << " DPYADR=0x" << machine.io_register(30)
                          << " VCOUNT=0x" << machine.io_register(29)
                          << " INTPEND=0x" << machine.io_register(18)
                          << " INTENB=0x" << machine.io_register(17)
                          << " IN0=0x" << bus.read16(bus.opaque, 0x01600000u)
                          << " IN1=0x" << bus.read16(bus.opaque, 0x01600010u)
                          << " input-reads=" << machine.input_read_counts()[0]
                          << "/" << machine.input_read_counts()[1]
                          << '\n';
            cpu_timeline->flush();
            do { next_timeline_cycle += timeline_interval; }
            while (next_timeline_cycle <= cpu.cycles);
        }
#ifdef MAMERECOMP_MK_SDL
        if (window && machine.take_completed_frames()) {
            window->poll_and_set_inputs(machine);
            window->present(machine.framebuffer(), machine.current_display_params(),
                            sound_cpu.machine.take_audio_samples());
        }
#endif
    }
    return BlockExit::Budget;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mamerecomp_mk_trace <work-dir> [cycles] [frame.ppm] "
                     "[--window] [--fallback-summary=PATH] "
                     "[--interpreter-steps=PATH] [--press-any-at=CYCLES] "
                     "[--release-start-at=CYCLES] [--press-start-again-at=CYCLES] "
                     "[--press-coin-at=CYCLES] [--release-coin-at=CYCLES] "
                     "[--release-any-at=CYCLES] [--press-any-again-at=CYCLES] "
                     "[--press-down-at=CYCLES] [--release-down-at=CYCLES] "
                     "[--press-punch-at=CYCLES] [--release-punch-at=CYCLES] "
                     "[--frame-dump-dir=DIR] [--frame-dump-from=CYCLES] "
                     "[--frame-dump-count=N] "
                     "[--dump-vram-pages=DIR] "
                     "[--graphics-op-trace=PATH] "
                     "[--force-interpreter] [--nvram=PATH] "
                     "[--input-replay=PATH] "
                     "[--cpu-timeline=PATH] [--cpu-timeline-every=CYCLES]\n";
        return 2;
    }
    try {
        const std::string work = argv[1];
        std::uint64_t budget = 5'000'000;
        bool explicit_budget = false;
        bool window_requested = false;
        bool scripted_start = false;
        std::uint64_t start_cycle = 0;
        bool scripted_start_release = false;
        std::uint64_t start_release_cycle = 0;
        bool scripted_start_again = false;
        std::uint64_t start_again_cycle = 0;
        bool scripted_coin = false;
        std::uint64_t coin_cycle = 0;
        bool scripted_coin_release = false;
        std::uint64_t coin_release_cycle = 0;
        bool scripted_button = false;
        std::uint64_t button_cycle = 0;
        bool scripted_button_release = false;
        std::uint64_t button_release_cycle = 0;
        bool scripted_button_again = false;
        std::uint64_t button_again_cycle = 0;
        bool scripted_down = false;
        std::uint64_t down_cycle = 0;
        bool scripted_down_release = false;
        std::uint64_t down_release_cycle = 0;
        bool scripted_punch = false;
        std::uint64_t punch_cycle = 0;
        bool scripted_punch_release = false;
        std::uint64_t punch_release_cycle = 0;
        bool force_interpreter = false;
        std::string input_replay_path;
        std::string frame = work + "/mk-frame.ppm";
        std::string video_ram_dump;
        std::string dma_trace_path;
        std::string nvram_path = work + "/mk.nvram";
        std::string cpu_timeline_path = work + "/tms34010_timeline.log";
        std::string graphics_op_trace_path;
        std::string frame_dump_dir;
        std::string vram_pages_dir;
        std::uint64_t frame_dump_from = 0;
        std::size_t frame_dump_limit = 12;
        std::size_t frame_dumps_written = 0;
        std::string fallback_summary = work + "/tms34010_fallbacks.log";
        std::string interpreter_steps =
            work + "/tms34010_interpreter_steps.log";
        std::uint64_t timeline_interval = 500'000;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--window") window_requested = true;
            else if (argument == "--force-interpreter") force_interpreter = true;
            else if (argument.rfind("--fallback-summary=", 0) == 0)
                fallback_summary = argument.substr(19);
            else if (argument.rfind("--interpreter-steps=", 0) == 0)
                interpreter_steps = argument.substr(20);
            else if (argument.rfind("--nvram=", 0) == 0)
                nvram_path = argument.substr(8);
            else if (argument.rfind("--input-replay=", 0) == 0)
                input_replay_path = argument.substr(15);
            else if (argument.rfind("--cpu-timeline=", 0) == 0)
                cpu_timeline_path = argument.substr(15);
            else if (argument.rfind("--cpu-timeline-every=", 0) == 0)
                timeline_interval = std::stoull(argument.substr(21), nullptr, 0);
            else if (argument.rfind("--graphics-op-trace=", 0) == 0)
                graphics_op_trace_path = argument.substr(20);
            else if (argument.rfind("--frame-dump-dir=", 0) == 0)
                frame_dump_dir = argument.substr(17);
            else if (argument.rfind("--frame-dump-from=", 0) == 0)
                frame_dump_from = std::stoull(argument.substr(18), nullptr, 0);
            else if (argument.rfind("--frame-dump-count=", 0) == 0)
                frame_dump_limit = std::stoull(argument.substr(19), nullptr, 0);
            else if (argument.rfind("--dump-vram-pages=", 0) == 0)
                vram_pages_dir = argument.substr(18);
            else if (argument.rfind("--dump-vram=", 0) == 0)
                video_ram_dump = argument.substr(12);
            else if (argument.rfind("--dma-trace=", 0) == 0)
                dma_trace_path = argument.substr(12);
            else if (argument.rfind("--press-start-at=", 0) == 0) {
                start_cycle = std::stoull(argument.substr(17), nullptr, 0);
                scripted_start = true;
            }
            else if (argument.rfind("--release-start-at=", 0) == 0) {
                start_release_cycle = std::stoull(argument.substr(19), nullptr, 0);
                scripted_start_release = true;
            }
            else if (argument.rfind("--press-start-again-at=", 0) == 0) {
                start_again_cycle = std::stoull(argument.substr(23), nullptr, 0);
                scripted_start_again = true;
            }
            else if (argument.rfind("--press-coin-at=", 0) == 0) {
                coin_cycle = std::stoull(argument.substr(16), nullptr, 0);
                scripted_coin = true;
            }
            else if (argument.rfind("--release-coin-at=", 0) == 0) {
                coin_release_cycle = std::stoull(argument.substr(18), nullptr, 0);
                scripted_coin_release = true;
            }
            else if (argument.rfind("--press-any-at=", 0) == 0) {
                button_cycle = std::stoull(argument.substr(15), nullptr, 0);
                scripted_button = true;
            }
            else if (argument.rfind("--release-any-at=", 0) == 0) {
                button_release_cycle = std::stoull(argument.substr(17), nullptr, 0);
                scripted_button_release = true;
            }
            else if (argument.rfind("--press-any-again-at=", 0) == 0) {
                button_again_cycle = std::stoull(argument.substr(21), nullptr, 0);
                scripted_button_again = true;
            }
            else if (argument.rfind("--press-down-at=", 0) == 0) {
                down_cycle = std::stoull(argument.substr(16), nullptr, 0);
                scripted_down = true;
            }
            else if (argument.rfind("--release-down-at=", 0) == 0) {
                down_release_cycle = std::stoull(argument.substr(18), nullptr, 0);
                scripted_down_release = true;
            }
            else if (argument.rfind("--press-punch-at=", 0) == 0) {
                punch_cycle = std::stoull(argument.substr(17), nullptr, 0);
                scripted_punch = true;
            }
            else if (argument.rfind("--release-punch-at=", 0) == 0) {
                punch_release_cycle = std::stoull(argument.substr(19), nullptr, 0);
                scripted_punch_release = true;
            }
            else if (!explicit_budget && !argument.empty() && argument[0] >= '0' && argument[0] <= '9') {
                budget = std::stoull(argument, nullptr, 0);
                explicit_budget = true;
            } else frame = argument;
        }
        const auto input_replay = input_replay_path.empty()
            ? std::vector<InputReplayEvent>{}
            : read_input_replay(input_replay_path);
        std::size_t input_replay_index = 0;
#ifndef MAMERECOMP_MK_SDL
        if (window_requested)
            throw std::runtime_error("--window needs an SDL3-enabled build (configure with -DMAMERECOMP_ENABLE_WINDOW=ON)");
#endif
        if (window_requested && !frame_dump_dir.empty())
            throw std::runtime_error("--frame-dump-dir cannot be combined with --window");
        if (!frame_dump_dir.empty())
            std::filesystem::create_directories(frame_dump_dir);
        if (!vram_pages_dir.empty())
            std::filesystem::create_directories(vram_pages_dir);
        auto program = read_file(work + "/maincpu.bin");
        auto graphics = read_file(work + "/video.bin");
        auto sound_cpu_rom = read_file(work + "/soundcpu.bin");
        auto oki_rom = read_file(work + "/oki.bin");
        tms34010_fallback_event_log.open(
            work + "/tms34010_uncovered_entries.log", std::ios::trunc);
        if (!tms34010_fallback_event_log)
            throw std::runtime_error("cannot write TMS34010 fallback event log");
        m6809_fallback_event_log.open(
            work + "/m6809e_uncovered_entries.log", std::ios::trunc);
        if (!m6809_fallback_event_log)
            throw std::runtime_error("cannot write MC6809E fallback event log");
        std::cout << "First-hit TMS34010/MC6809E fallback events will be logged to "
                  << work << "/tms34010_uncovered_entries.log\n";
        std::cout << "MC6809E fallback events: " << work
                  << "/m6809e_uncovered_entries.log\n";
        mamerecomp_interpreter_trace_set_summary_log(fallback_summary.c_str());
        mamerecomp_interpreter_trace_set_step_log(interpreter_steps.c_str());
        mamerecomp::mk::MKMachine runtime(program, graphics, sound_cpu_rom,
                                          oki_rom);
        auto& machine = runtime.tunit();
        machine.enable_dma_history(!dma_trace_path.empty());
        SoundCpuRunner sound_cpu(runtime);
        if (std::filesystem::exists(nvram_path)) {
            const auto saved_cmos = read_file(nvram_path);
            if (!machine.load_cmos(saved_cmos))
                throw std::runtime_error("invalid CMOS/NVRAM size in " +
                    nvram_path + " (expected " +
                    std::to_string(machine.cmos().size()) + " bytes)");
            std::cout << "Loaded " << saved_cmos.size()
                      << " bytes of T-Unit CMOS from " << nvram_path << '\n';
        }
        State cpu{};
        cpu.pc = entry_pc();
        cpu.st = 0x10;
        auto bus = machine.cpu_bus();
        TraceRing trace{};
        std::uint64_t native_instructions = 0;
        std::uint64_t interpreted_instructions = 0;
        std::set<std::pair<std::uint32_t, std::uint16_t>> logged_fallbacks;
        std::ofstream cpu_timeline(cpu_timeline_path, std::ios::trunc);
        if (!cpu_timeline)
            throw std::runtime_error("cannot write " + cpu_timeline_path);
        std::uint64_t next_timeline_cycle = timeline_interval;
        std::ofstream graphics_op_trace;
        if (!graphics_op_trace_path.empty()) {
            graphics_op_trace.open(graphics_op_trace_path, std::ios::trunc);
            if (!graphics_op_trace)
                throw std::runtime_error("cannot write " + graphics_op_trace_path);
        }
        std::ostream* graphics_op_trace_stream = graphics_op_trace_path.empty()
            ? nullptr : &graphics_op_trace;
#ifdef MAMERECOMP_MK_SDL
        std::unique_ptr<LiveWindow> live;
        if (window_requested) live = std::make_unique<LiveWindow>();
        BlockExit result = BlockExit::Budget;
        if (live) {
            live->poll_and_set_inputs(machine);
            while (live->running && (!explicit_budget || cpu.cycles < budget)) {
                // TMS execution cycles run at 50 MHz / 8. One video frame is
                // 506 * 289 pixel clocks at 8 MHz, so advance about one frame
                // of emulated time before queuing its audio and presenting it.
                // Large batches made SDL receive many frames of audio at once,
                // causing an ever-growing playback delay while the window
                // still presented only once per batch.
                constexpr std::uint64_t cycles_per_video_frame =
                    (50'000'000ull / 8ull) * 506ull * 289ull / 8'000'000ull;
                const auto next_stop = explicit_budget
                    ? std::min(budget, cpu.cycles + cycles_per_video_frame)
                    : cpu.cycles + cycles_per_video_frame;
                result = run_scheduled(cpu, bus, next_stop, machine, sound_cpu,
                                       trace, native_instructions,
                                       interpreted_instructions, logged_fallbacks,
                                       &cpu_timeline, graphics_op_trace_stream,
                                       next_timeline_cycle,
                                       timeline_interval,
                                       input_replay, input_replay_index,
                                       scripted_start, start_cycle,
                                       scripted_start_release, start_release_cycle,
                                       scripted_start_again, start_again_cycle,
                                       scripted_coin, coin_cycle,
                                       scripted_coin_release, coin_release_cycle,
                                       scripted_button, button_cycle,
                                       scripted_button_release, button_release_cycle,
                                       scripted_down, down_cycle,
                                       scripted_down_release, down_release_cycle,
                                       scripted_punch, punch_cycle,
                                       scripted_punch_release, punch_release_cycle,
                                       scripted_button_again, button_again_cycle,
                                       frame_dump_dir, frame_dump_from,
                                       frame_dump_limit, frame_dumps_written,
                                       force_interpreter,
                                       live.get());
                if (result != BlockExit::Budget) break;
            }
            live->present(machine.framebuffer(), machine.current_display_params(),
                          runtime.take_audio_samples());
        } else {
            result = run_scheduled(cpu, bus, budget, machine, sound_cpu, trace,
                                   native_instructions, interpreted_instructions,
                                   logged_fallbacks,
                                   &cpu_timeline, graphics_op_trace_stream,
                                   next_timeline_cycle,
                                   timeline_interval,
                                   input_replay, input_replay_index,
                                   scripted_start, start_cycle,
                                   scripted_start_release, start_release_cycle,
                                   scripted_start_again, start_again_cycle,
                                   scripted_coin, coin_cycle,
                                   scripted_coin_release, coin_release_cycle,
                                   scripted_button, button_cycle,
                                   scripted_button_release, button_release_cycle,
                                   scripted_down, down_cycle,
                                   scripted_down_release, down_release_cycle,
                                   scripted_punch, punch_cycle,
                                   scripted_punch_release, punch_release_cycle,
                                   scripted_button_again, button_again_cycle,
                                   frame_dump_dir, frame_dump_from,
                                   frame_dump_limit, frame_dumps_written,
                                   force_interpreter,
                                   nullptr);
        }
#else
        const auto result = run_scheduled(cpu, bus, budget, machine, sound_cpu,
                                          trace, native_instructions,
                                          interpreted_instructions, logged_fallbacks,
                                          &cpu_timeline, graphics_op_trace_stream,
                                          next_timeline_cycle,
                                          timeline_interval,
                                          input_replay, input_replay_index,
                                          scripted_start, start_cycle,
                                          scripted_start_release, start_release_cycle,
                                          scripted_start_again, start_again_cycle,
                                          scripted_coin, coin_cycle,
                                          scripted_coin_release, coin_release_cycle,
                                          scripted_button, button_cycle,
                                          scripted_button_release, button_release_cycle,
                                          scripted_down, down_cycle,
                                          scripted_down_release, down_release_cycle,
                                          scripted_punch, punch_cycle,
                                          scripted_punch_release, punch_release_cycle,
                                          scripted_button_again, button_again_cycle,
                                          frame_dump_dir, frame_dump_from,
                                          frame_dump_limit, frame_dumps_written,
                                          force_interpreter);
#endif
        tms34010_fallback_event_log.flush();
        m6809_fallback_event_log.flush();
        std::cout << "TMS34010 instructions: native/recompiled="
                  << native_instructions << " interpreted="
                  << interpreted_instructions << " total="
                  << (native_instructions + interpreted_instructions) << '\n';
        std::cout << "MC6809E instructions: native/recompiled="
                  << sound_cpu.native_instructions << " interpreted="
                  << sound_cpu.interpreted_instructions << " total="
                  << (sound_cpu.native_instructions +
                      sound_cpu.interpreted_instructions)
                  << " cycles=" << sound_cpu.state.cycles << " pc=0x"
                  << std::hex << sound_cpu.state.pc << " bank=0x"
                  << unsigned(runtime.sound_cpu_bank()) << std::dec
                  << " A=0x" << std::hex << unsigned(sound_cpu.state.a)
                  << " B=0x" << unsigned(sound_cpu.state.b)
                  << " X=0x" << sound_cpu.state.x
                  << " Y=0x" << sound_cpu.state.y
                  << " U=0x" << sound_cpu.state.u
                  << " S=0x" << sound_cpu.state.s
                  << " CC=0x" << unsigned(sound_cpu.state.cc) << std::dec
                  << " reset=" << runtime.sound_cpu_reset_asserted()
                  << " irq=" << runtime.sound_cpu_irq_asserted()
                  << " firq=" << runtime.sound_cpu_firq_asserted()
                  << " clock-target=" << sound_cpu.sound_cycle_target
                  << " cpu-cycle-lead="
                  << (sound_cpu.state.cycles >= sound_cpu.sound_cycle_target
                          ? sound_cpu.state.cycles - sound_cpu.sound_cycle_target
                          : 0)
                  << '\n';
        const auto audio = runtime.audio_diagnostics();
        std::cout << "Williams audio: YM-writes=" << audio.ym_writes
                  << " OKI-writes=" << audio.oki_writes
                  << " DAC-writes=" << audio.dac_writes
                  << " generated-frames=" << audio.generated_sample_frames
                  << " nonzero-frames=" << audio.nonzero_sample_frames
                  << " peak=" << audio.peak
                  << " YM-peak=" << audio.ym_peak
                  << " OKI-peak=" << audio.oki_peak
                  << " DAC-peak=" << audio.dac_peak << '\n';
        std::cout << "T-Unit sound link: command=0x" << std::hex
                  << unsigned(machine.sound_command()) << std::dec
                  << " reset=" << machine.sound_reset_asserted()
                  << '\n';
        if (result == BlockExit::Budget)
            print_trace(trace);
        std::cout << "TMS34010 result=" << static_cast<unsigned>(result)
                  << " cycles=" << cpu.cycles << " pc=0x" << std::hex << cpu.pc
                  << " st=0x" << cpu.st
                  << " dpyctl=0x" << machine.io_register(8)
                  << " dpystrt=0x" << machine.io_register(9)
                  << " vtotal=0x" << machine.io_register(7)
                  << " vram_nonzero=" << std::dec
                  << std::count_if(machine.video_ram().begin(),
                                   machine.video_ram().end(),
                                   [](std::uint16_t value) { return value != 0; })
                  << '\n';
        const auto count_nonzero = [](std::span<const std::uint16_t> values) {
            return std::count_if(values.begin(), values.end(),
                                 [](std::uint16_t value) { return value != 0; });
        };
        const auto display = machine.current_display_params();
        const auto video = machine.video_diagnostics();
        const auto framebuffer = machine.framebuffer();
        const auto nonblack_pixels = std::count_if(
            framebuffer.begin(), framebuffer.end(),
            [](std::uint32_t pixel) { return (pixel & 0x00ffffffu) != 0; });
        std::cout << "T-Unit scanout lines=" << video.scanlines
                  << " enabled-visible=" << video.enabled_visible_scanlines
                  << " nonzero-source-words=" << video.nonzero_source_words
                  << " nonblack-frame-pixels=" << nonblack_pixels
                  << " palette-nonzero=" << count_nonzero(machine.palette_ram())
                  << " veblnk=" << machine.io_register(5)
                  << " vsblnk=" << machine.io_register(6)
                  << " heblnk=" << machine.io_register(1)
                  << " hsblnk=" << machine.io_register(2)
                  << " htotal=" << machine.io_register(3)
                  << " vcount=" << machine.io_register(29)
                  << " dpyadr=" << machine.io_register(30)
                  << " dpytap=" << machine.io_register(27)
                  << " rowaddr=" << display.row_address
                  << " coladdr=" << display.column_address << '\n';
        const auto input_reads = machine.input_read_counts();
        const auto input_last_pc = machine.input_last_read_pc();
        std::cout << "T-Unit input reads IN0=" << input_reads[0]
                  << " IN1=" << input_reads[1]
                  << " IN2=" << input_reads[2]
                  << " DSW=" << input_reads[3]
                  << " last-read-PC=0x" << std::hex << input_last_pc[0]
                  << "/0x" << input_last_pc[1] << "/0x" << input_last_pc[2]
                  << "/0x" << input_last_pc[3] << std::dec << '\n';
        const auto dma = machine.dma_diagnostics();
        std::cout << "T-Unit DMA writes=" << dma.register_writes
                  << " command-writes=" << dma.command_writes
                  << " last-register=" << dma.last_register
                  << " last-value=0x" << std::hex << dma.last_value
                  << " last-command-write=0x" << dma.last_command_write
                  << " current-command=0x" << dma.current_command
                  << " commands=" << std::dec << dma.commands
                  << " completed=" << dma.completed
                  << " output-pixels=" << dma.output_pixels
                  << " last-command=0x" << std::hex << dma.last_command
                  << " last-source=0x" << dma.last_source
                  << " intenb=0x" << machine.io_register(17)
                  << " intpend=0x" << machine.io_register(18)
                  << " busy-cycles=" << std::dec << dma.cycles_remaining
                  << '\n';
        if (!dma_trace_path.empty()) {
            std::ofstream out(dma_trace_path, std::ios::trunc);
            if (!out) throw std::runtime_error("cannot write " + dma_trace_path);
            out << "# seq command source x y width height palette color xstep ystep top bottom left right lrs pixels-written\n";
            for (const auto& item : machine.dma_command_history()) {
                out << std::hex << item.sequence << ' ' << item.command << ' '
                    << item.source << ' ' << item.x_start << ' ' << item.y_start << ' '
                    << item.width << ' ' << item.height << ' ' << item.palette << ' '
                    << item.color << ' ' << item.x_step << ' ' << item.y_step << ' '
                    << item.top_clip << ' ' << item.bottom_clip << ' '
                    << item.left_clip << ' ' << item.right_clip << ' '
                    << item.left_right_skip << std::dec << ' '
                    << item.pixels_written << '\n';
            }
        }
        write_rgb_snapshot(framebuffer, frame);
        std::cout << "wrote scheduled T-Unit framebuffer: " << frame << '\n';
        if (!frame_dump_dir.empty())
            std::cout << "wrote " << frame_dumps_written
                      << " sequential frame snapshots to " << frame_dump_dir << '\n';
        if (!video_ram_dump.empty()) {
            const auto vram = machine.video_ram();
            std::ofstream output(video_ram_dump, std::ios::binary);
            if (!output) throw std::runtime_error("cannot write " + video_ram_dump);
            output.write(reinterpret_cast<const char*>(vram.data()),
                         static_cast<std::streamsize>(vram.size_bytes()));
            if (!output) throw std::runtime_error("failed writing " + video_ram_dump);
            std::cout << "wrote raw T-Unit VRAM (512x512 uint16) to "
                      << video_ram_dump << '\n';
        }
        if (!vram_pages_dir.empty()) {
            write_vram_page_snapshot(machine, 0,
                vram_pages_dir + "/page-000.ppm");
            write_vram_page_snapshot(machine, 256,
                vram_pages_dir + "/page-256.ppm");
            std::cout << "wrote display-page snapshots for VRAM rows 0 and 256 to "
                      << vram_pages_dir << '\n';
        }
        save_cmos_file(nvram_path, machine.cmos());
        std::cout << "Saved " << machine.cmos().size()
                  << " bytes of T-Unit CMOS to " << nvram_path << '\n';
        mamerecomp_interpreter_trace_flush();
        return result == BlockExit::Budget ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "mk-trace: " << error.what() << '\n';
        return 2;
    }
}
