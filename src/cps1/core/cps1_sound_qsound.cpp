#include "cps1_sound_qsound.hpp"

#include <qsound.h>
#include <native_z80.h>
#include "cps1_state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ostream>
#include <utility>
#include <vector>

#ifdef MAMERECOMP_CPS1_SDL
#include <SDL3/SDL.h>
#endif

namespace mamerecomp::cps1::detail {
extern "C" std::int64_t ng_cycles;
namespace {
constexpr std::uint32_t kZ80Clock = 8'000'000;
constexpr std::uint32_t kQSoundClock = 60'000'000;
constexpr std::uint32_t kIrqPeriod = 32'000;

struct QSoundRuntime {
    qsound_chip chip{};
    std::vector<std::uint8_t> program;
    std::vector<std::uint8_t> samples;
    std::vector<std::uint8_t> decrypted_opcodes;
    std::vector<std::uint8_t> decrypted_data;
    std::array<std::uint8_t, 0x1000> shared0{};
    std::array<std::uint8_t, 0x1000> shared1{};
    std::uint8_t bank = 0;
    std::uint8_t command = 0xff;
    std::uint64_t next_irq = kIrqPeriod;
    std::uint64_t generated_samples = 0;
    std::uint64_t generated_fraction = 0;
    std::uint64_t qsound_writes = 0;
    bool active = false;
#ifdef MAMERECOMP_CPS1_SDL
    SDL_AudioStream* audio_stream = nullptr;
#endif
};

QSoundRuntime g_qsound;
extern mamerecomp::z80::Z80 g_cpu;
void sync_qsound_devices(std::uint64_t z80_cycles);

std::uint8_t decode_byte(std::uint8_t src, unsigned key1, unsigned key2,
    unsigned xor_key, unsigned select) {
    auto swap1 = [](int v, unsigned key, unsigned mask) {
        if (mask & (1u << ((key >> 0) & 7))) v = (v & 0xfc) | ((v & 1) << 1) | ((v & 2) >> 1);
        if (mask & (1u << ((key >> 4) & 7))) v = (v & 0xf3) | ((v & 4) << 1) | ((v & 8) >> 1);
        if (mask & (1u << ((key >> 8) & 7))) v = (v & 0xcf) | ((v & 0x10) << 1) | ((v & 0x20) >> 1);
        if (mask & (1u << ((key >> 12) & 7))) v = (v & 0x3f) | ((v & 0x40) << 1) | ((v & 0x80) >> 1);
        return v;
    };
    auto swap2 = [](int v, unsigned key, unsigned mask) {
        if (mask & (1u << ((key >> 12) & 7))) v = (v & 0xfc) | ((v & 1) << 1) | ((v & 2) >> 1);
        if (mask & (1u << ((key >> 8) & 7))) v = (v & 0xf3) | ((v & 4) << 1) | ((v & 8) >> 1);
        if (mask & (1u << ((key >> 4) & 7))) v = (v & 0xcf) | ((v & 0x10) << 1) | ((v & 0x20) >> 1);
        if (mask & (1u << ((key >> 0) & 7))) v = (v & 0x3f) | ((v & 0x40) << 1) | ((v & 0x80) >> 1);
        return v;
    };
    int value = swap1(src, key1 & 0xffff, select & 0xff);
    value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7);
    value = swap2(value, key1 >> 16, select & 0xff);
    value ^= xor_key;
    value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7);
    value = swap2(value, key2 & 0xffff, select >> 8);
    value = ((value & 0x7f) << 1) | ((value & 0x80) >> 7);
    return static_cast<std::uint8_t>(swap1(value, key2 >> 16, select >> 8));
}

class QSoundBus final : public mamerecomp::z80::Z80Bus {
public:
    std::uint8_t Read(std::uint16_t address) override {
        if (address < 0x8000) return g_qsound.decrypted_data[address];
        if (address < 0xc000) {
            const std::size_t offset = 0x10000 + static_cast<std::size_t>(g_qsound.bank) * 0x4000
                + (address - 0x8000);
            return offset < g_qsound.program.size() ? g_qsound.program[offset] : 0xff;
        }
        if (address < 0xd000) return g_qsound.shared0[address - 0xc000];
        if (address >= 0xf000) return g_qsound.shared1[address - 0xf000];
        if (address == 0xd007) {
            sync_qsound_devices(g_cpu.BusCallbackCycles());
            return qsound_r(&g_qsound.chip);
        }
        if (address == 0xf008) return g_qsound.command;
        return 0xff;
    }
    std::uint8_t ReadOpcode(std::uint16_t address) override {
        if (address < g_qsound.decrypted_opcodes.size()) return g_qsound.decrypted_opcodes[address];
        return Read(address);
    }
    void Write(std::uint16_t address, std::uint8_t value) override {
        if (address >= 0xc000 && address < 0xd000) {
            g_qsound.shared0[address - 0xc000] = value;
            if (std::getenv("MAMERECOMP_CPS1_TRACE_QSOUND"))
                std::fprintf(stderr, "[cps1-qsound] Z80 PC=%04x RAM0[%04x]=%02x\n", g_cpu.pc, address, value);
        }
        else if (address >= 0xf000) {
            g_qsound.shared1[address - 0xf000] = value;
            if (std::getenv("MAMERECOMP_CPS1_TRACE_QSOUND"))
                std::fprintf(stderr, "[cps1-qsound] Z80 PC=%04x RAM1[%04x]=%02x\n", g_cpu.pc, address, value);
        }
        else if (address >= 0xd000 && address <= 0xd002) {
            sync_qsound_devices(g_cpu.BusCallbackCycles());
            qsound_w(&g_qsound.chip, static_cast<std::uint8_t>(address - 0xd000), value);
            ++g_qsound.qsound_writes;
        } else if (address == 0xd003) g_qsound.bank = value & 0x0f;
    }
    bool ChangesMapping(std::uint16_t address) override { return address == 0xd003; }
    std::uint32_t Mapping() override { return g_qsound.bank; }
    void SetMapping(std::uint32_t mapping) override { g_qsound.bank = mapping & 0x0f; }
};

QSoundBus g_bus;
mamerecomp::z80::Z80 g_cpu(g_bus);

void push_audio(std::uint64_t count) {
    if (!count) return;
    std::array<std::int16_t, 1024> left{}, right{};
    while (count) {
        const int chunk = static_cast<int>(std::min<std::uint64_t>(count, left.size()));
        std::int16_t* outputs[]{left.data(), right.data()};
        qsound_stream_update(&g_qsound.chip, outputs, chunk);
#ifdef MAMERECOMP_CPS1_SDL
        if (g_qsound.audio_stream) {
            std::array<std::int16_t, 2048> interleaved{};
            for (int i = 0; i < chunk; ++i) {
                interleaved[i * 2] = left[i];
                interleaved[i * 2 + 1] = right[i];
            }
            SDL_PutAudioStreamData(g_qsound.audio_stream, interleaved.data(), chunk * 4);
        }
#endif
        g_qsound.generated_samples += chunk;
        count -= chunk;
    }
}

void sync_qsound_devices(std::uint64_t z80_cycles) {
    const std::uint64_t scaled = z80_cycles * 24038;
    const std::uint64_t target = scaled / kZ80Clock;
    if (target > g_qsound.generated_samples) push_audio(target - g_qsound.generated_samples);
}
} // namespace

bool initialize_qsound(std::vector<std::uint8_t> program_rom, std::vector<std::uint8_t> sample_rom,
    const mamerecomp::z80::Z80Code* code, bool audio_output) {
    if (program_rom.size() != 0x28000 || sample_rom.size() != 0x200000) return false;
    g_qsound = {};
    g_qsound.program = std::move(program_rom);
    g_qsound.samples = std::move(sample_rom);
    g_qsound.decrypted_opcodes.resize(0x8000);
    g_qsound.decrypted_data.resize(0x8000);
    for (unsigned address = 0; address < 0x8000; ++address) {
        const unsigned opcode_select = address + 0x4343;
        const unsigned data_select = ((address ^ 0x1fc0) + 0x4343 + 1) & 0xffff;
        g_qsound.decrypted_opcodes[address] = decode_byte(g_qsound.program[address],
            0x76543210, 0x24601357, 0x43, opcode_select);
        g_qsound.decrypted_data[address] = decode_byte(g_qsound.program[address],
            0x76543210, 0x24601357, 0x43, data_select);
    }
    qsound_start(&g_qsound.chip, kQSoundClock);
    g_qsound.chip.rom_data = g_qsound.samples.data();
    g_qsound.chip.rom_mask = g_qsound.samples.size() - 1;
    g_qsound.chip.ready_flag = 0x80;
    mamerecomp::z80::SetZ80Code(code);
    g_cpu.Reset();
    g_cpu.recording = true;
    g_qsound.active = true;
#ifdef MAMERECOMP_CPS1_SDL
    if (audio_output) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = 24038;
        g_qsound.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!g_qsound.audio_stream || !SDL_ResumeAudioStreamDevice(g_qsound.audio_stream)) return false;
    }
#else
    (void)audio_output;
#endif
    return true;
}

bool qsound_is_active() { return g_qsound.active; }
void qsound_sync_to_main_clock() {
    if (!g_qsound.active || ng_cycles <= 0) return;
    const auto mode = std::getenv("MAMERECOMP_CPS1_QSOUND_INTERPRETER")
        ? mamerecomp::z80::Z80::Mode::Interpreter
        : mamerecomp::z80::Z80::Mode::Native;
    const std::uint64_t target = static_cast<std::uint64_t>(ng_cycles) * kZ80Clock / g_main_clock_hz;
    while (g_qsound.next_irq <= target) {
        g_cpu.Run(g_qsound.next_irq, mode);
        sync_qsound_devices(g_qsound.next_irq);
        // MAME holds the 250 Hz IRQ line until the Z80 acknowledges it. Keep
        // it asserted while interrupts are disabled or the EI delay is active;
        // the shared Z80 core clears the line when TakeInterrupt accepts it.
        g_cpu.SetInterruptLine(true);
        g_qsound.next_irq += kIrqPeriod;
    }
    if (target > g_cpu.cycles) g_cpu.Run(target, mode);
    sync_qsound_devices(std::max(target, g_cpu.cycles));
}
void qsound_write_command(std::uint8_t value) { g_qsound.command = value; }
std::uint16_t qsound_main_read(std::uint32_t address) {
    if (address >= 0xf18000 && address <= 0xf19ffe) return 0xff00 | g_qsound.shared0[(address - 0xf18000) >> 1];
    if (address >= 0xf1e000 && address <= 0xf1fffe) return 0xff00 | g_qsound.shared1[(address - 0xf1e000) >> 1];
    return 0xffff;
}
bool qsound_main_write(std::uint32_t address, std::uint16_t value) {
    if (address >= 0xf18000 && address <= 0xf19ffe) {
        const auto offset = (address - 0xf18000) >> 1;
        g_qsound.shared0[offset] = static_cast<std::uint8_t>(value);
        if (std::getenv("MAMERECOMP_CPS1_TRACE_QSOUND"))
            std::fprintf(stderr, "[cps1-qsound] 68000 RAM0[%04x]=%02x\n", offset,
                static_cast<unsigned>(value & 0xff));
        return true;
    }
    if (address >= 0xf1e000 && address <= 0xf1fffe) {
        const auto offset = (address - 0xf1e000) >> 1;
        g_qsound.shared1[offset] = static_cast<std::uint8_t>(value);
        if (std::getenv("MAMERECOMP_CPS1_TRACE_QSOUND"))
            std::fprintf(stderr, "[cps1-qsound] 68000 RAM1[%04x]=%02x\n", offset,
                static_cast<unsigned>(value & 0xff));
        return true;
    }
    return false;
}
void qsound_print_summary(std::ostream& out) {
    if (!g_qsound.active) return;
    out << " Z80 native blocks=" << g_cpu.nativeBlocks
        << " interpreted instructions=" << g_cpu.interpreted
        << " PC=$" << std::hex << g_cpu.pc << std::dec
        << " instr=" << g_cpu.interpreted
        << " IFF=" << g_cpu.iff1
        << " IRQ=" << g_cpu.InterruptLineAsserted()
        << " shared0[$fff]=$" << std::hex << unsigned(g_qsound.shared0.back())
        << " shared1[$fff]=$" << unsigned(g_qsound.shared1.back()) << std::dec
        << " qram=$" << std::hex << unsigned(g_qsound.shared1.back()) << std::dec
        << " QSound writes=" << g_qsound.qsound_writes
        << " audio samples=" << g_qsound.generated_samples;
}
bool qsound_save_artifacts(const std::string& entry_path, const std::string&) {
    std::ofstream entries(entry_path);
    std::sort(g_cpu.newEntries.begin(), g_cpu.newEntries.end());
    g_cpu.newEntries.erase(std::unique(g_cpu.newEntries.begin(), g_cpu.newEntries.end()), g_cpu.newEntries.end());
    for (const std::uint16_t address : g_cpu.newEntries)
        entries << std::hex << std::uppercase << address << '\n';
    return static_cast<bool>(entries);
}
void qsound_shutdown() {
#ifdef MAMERECOMP_CPS1_SDL
    if (g_qsound.audio_stream) { SDL_DestroyAudioStream(g_qsound.audio_stream); g_qsound.audio_stream = nullptr; SDL_QuitSubSystem(SDL_INIT_AUDIO); }
#endif
    g_qsound.active = false;
}

} // namespace mamerecomp::cps1::detail
