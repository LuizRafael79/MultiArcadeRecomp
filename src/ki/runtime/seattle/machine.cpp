#include "machine.hpp"

#include "mips3/mips3.h"
#include "mips3/rec_runtime.h"
#include "ki/runtime/dcs/sound.h"
#include "ki/runtime/seattle/ide/ide.h"
#include "ki/runtime/seattle/mem.h"

#include <algorithm>
#include <cstring>

namespace multiarcaderecomp::ki::seattle {
namespace {
constexpr int cpu_hz = 100000000;
constexpr int total_lines = 261;
constexpr int visible_lines = 240;
constexpr int cycles_per_line = static_cast<int>(cpu_hz / frame_rate / total_lines);
std::uint64_t frames{};
int sound_cycle_remainder{};
}

void power_on() noexcept {
    std::memset(ram, 0, RAM_SIZE);
    frames = 0;
    sound_cycle_remainder = 0;
    hw = hw_t{.p1 = 0xffffffffu, .p2 = 0xffffffffu,
              .volume = 0xffffffffu, .dsw = hw.dsw};
    sound_power_on();
    ide_power_on();
    cpu_reset();
    rec_reset();
}

void run_frame() noexcept {
    ++frames;
    for (int line = 0; line < total_lines; ++line) {
        if (line == visible_lines) cpu_set_irq(0, true);
        if (line == 0) cpu_set_irq(0, false);
        int remaining = cycles_per_line;
        while (remaining > 0) {
            const int slice = std::min(remaining, 256);
            const int ran = static_cast<int>(cpu_run(static_cast<std::uint64_t>(slice)));
            const int advanced = cpu_idle ? remaining : slice;
            if (cpu_idle) cpu_skip(static_cast<std::uint64_t>(remaining - ran));
            ide_tick(advanced);
            sound_cycle_remainder += advanced;
            sound_run(sound_cycle_remainder / 10); // Seattle: 100 MHz R4600 / 10 MHz DCS
            sound_cycle_remainder %= 10;
            remaining -= advanced;
        }
    }
}

void set_inputs(std::uint32_t p1, std::uint32_t p2, std::uint32_t dips) noexcept {
    hw.p1 = p1;
    hw.p2 = p2;
    hw.dsw = dips;
}

std::uint64_t frame_number() noexcept { return frames; }

void copy_rgb(std::span<std::uint8_t> output) noexcept {
    if (output.size() < frame_width * frame_height * 3) return;
    const auto* source = video_base();
    for (int i = 0; i < frame_width * frame_height; ++i) {
        const std::uint16_t pixel = source[i];
        const unsigned b = (pixel >> 10) & 31;
        const unsigned g = (pixel >> 5) & 31;
        const unsigned r = pixel & 31;
        output[i * 3 + 0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
        output[i * 3 + 1] = static_cast<std::uint8_t>((g << 3) | (g >> 2));
        output[i * 3 + 2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    }
}

} // namespace multiarcaderecomp::ki::seattle
