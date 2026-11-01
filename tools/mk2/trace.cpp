// Bounded diagnostic runner for the MK2 T-Unit prototype.
#include "mk2/mk2_machine.hpp"
#include "core/cpus/tms34010/interpreter.hpp"
#include "core/cpus/tms34010/runtime.hpp"
#include "core/cpus/adsp2105/runtime.hpp"
#include "core/cpus/interpreter_trace.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_ppm(std::span<const std::uint32_t> frame,
               const std::filesystem::path& path) {
    constexpr auto width = mamerecomp::mk::TUnit::frame_width;
    constexpr auto height = mamerecomp::mk::TUnit::frame_height;
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto pixel : frame) {
        const char rgb[] = {static_cast<char>(pixel >> 16),
                            static_cast<char>(pixel >> 8),
                            static_cast<char>(pixel)};
        out.write(rgb, sizeof(rgb));
    }
}
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: mamerecomp_mk2_trace <work-dir> [cycles] [frame.ppm]\n";
        return 2;
    }
    try {
        const std::filesystem::path work = argv[1];
        const auto limit = argc > 2 ? std::stoull(argv[2], nullptr, 0)
                                    : 50'000'000ull;
        const auto frame_path = argc > 3 ? std::filesystem::path(argv[3])
                                         : work / "mk2-frame.ppm";
        const auto main = read_file(work / "maincpu.bin");
        const auto graphics = read_file(work / "video.bin");
        const auto sound = read_file(work / "dcs_sound.bin");
        mamerecomp::mk2::MK2Machine machine(main, graphics, sound);
        auto& board = machine.tunit();
        auto tms_bus = board.cpu_bus();
        mamerecomp::tms34010::State cpu{};
        cpu.pc = mamerecomp::tms34010::entry_pc();
        cpu.st = 0x10;
        std::uint64_t native = 0, interpreted = 0;
        MamerecompFallbackReason reason = MAMERECOMP_UNCOVERED_ENTRY;
        auto& adsp = machine.dcs();
        adsp.set_reset(true);
        auto next_sound_cycle = std::uint64_t{0};
        std::uint64_t sound_ratio_remainder = 0;
        while (cpu.cycles < limit) {
            const auto before = cpu.cycles;
            auto result = mamerecomp::tms34010::BlockExit::NeedsInterpreter;
            if (const auto block = mamerecomp::tms34010::lookup(cpu.pc))
                result = block(cpu, tms_bus, limit);
            if (result == mamerecomp::tms34010::BlockExit::NeedsInterpreter) {
                result = mamerecomp::tms34010::interpreter_step(
                    cpu, tms_bus, limit, reason);
                if (result == mamerecomp::tms34010::BlockExit::Dispatch)
                    ++interpreted;
            } else if (result == mamerecomp::tms34010::BlockExit::Dispatch) {
                ++native;
            }
            if (result != mamerecomp::tms34010::BlockExit::Dispatch &&
                result != mamerecomp::tms34010::BlockExit::Budget) {
                std::cerr << "TMS stopped at PC=0x" << std::hex << cpu.pc
                          << " reason=" << static_cast<int>(result) << std::dec << '\n';
                break;
            }
            const auto elapsed = cpu.cycles - before;
            board.advance_cycles(elapsed);
            sound_ratio_remainder += elapsed * 8;
            next_sound_cycle += sound_ratio_remainder / 5; // 6.25 : 10 MHz.
            sound_ratio_remainder %= 5;
            if (next_sound_cycle > adsp.cpu().cycles)
                (void)adsp.run_until(next_sound_cycle);
            if (result == mamerecomp::tms34010::BlockExit::Budget) break;
        }
        write_ppm(board.framebuffer(), frame_path);
        const auto d = adsp.diagnostics();
        std::cout << "MK2 TMS cycles=" << cpu.cycles << " pc=0x" << std::hex
                  << cpu.pc << std::dec << " native=" << native
                  << " interpreted=" << interpreted << " frames="
                  << board.video_diagnostics().completed_frames << '\n';
        std::cout << "DCS2K commands=" << d.host_commands << " reads="
                  << d.host_reads << " ADSP cycles=" << adsp.cpu().cycles
                  << " pc=0x" << std::hex << adsp.cpu().pc << std::dec
                  << " native=" << adsp.cpu().native_instructions
                  << " interpreted=" << adsp.cpu().interpreted_instructions
                  << " samples=" << d.sport_words
                  << " stalled=" << d.cpu_stalled << '\n';
        std::cout << "frame: " << frame_path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mk2-trace: " << e.what() << '\n';
        return 1;
    }
}
