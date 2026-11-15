#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>
#include "m68k.h"
#include "native_registry.h"
#include "cps1_state.hpp"
#include "cps1_gfxram.hpp"
#include "cps1_bus.hpp"
#include "cps1_sound_ym_oki.hpp"
#include "cps1_sound_qsound.hpp"
#include "cps1_eeprom.hpp"
#include "cps1_video.hpp"
#include "cps1_inputs.hpp"
#include "cps1_cpu.hpp"
#include "cps1_driver.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef MULTIARCADERECOMP_CPS1_SDL
#include <SDL3/SDL.h>
#endif

extern "C" {
unsigned multiarcaderecomp_board_module_abi();
const char* multiarcaderecomp_board_id();
unsigned multiarcaderecomp_game_abi();
const char* multiarcaderecomp_game_id();
const char* multiarcaderecomp_game_board_id();
const ng_func_entry_t* multiarcaderecomp_cps1_game_m68k_table(size_t* count);
const uint8_t* multiarcaderecomp_cps1_game_ram_snapshot(size_t* size);
const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_cps1_game_z80_code();
const multiarcaderecomp::cps1::Cps1GameDriver* multiarcaderecomp_cps1_game_driver();
}

namespace multiarcaderecomp::cps1::detail {
// The CPU exposes 0x30000 bytes, while CPS-A base registers select from a
// 0x40000-byte aperture (MAME masks the address with 0x3ffff).
// CPS1 main_map exposes the player port across $800000-$800007 and the
// system/DIP bytes at $800018-$80001f (returned in the high byte, low byte $ff).




bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), {});
    return !out.empty();
}

} // namespace multiarcaderecomp::cps1::detail

using namespace multiarcaderecomp::cps1::detail;

int main(int argc, char** argv) {
    if (multiarcaderecomp_board_module_abi() != 1 || std::strcmp(multiarcaderecomp_board_id(), "cps1") != 0
        || multiarcaderecomp_game_abi() != 1
        || std::strcmp(multiarcaderecomp_game_id(), MULTIARCADERECOMP_CPS1_GAME_ID) != 0
        || std::strcmp(multiarcaderecomp_game_board_id(), multiarcaderecomp_board_id()) != 0) {
        std::cerr << "incompatible CPS-1 board/game AOT modules\n";
        return 1;
    }
    const auto* game_driver = multiarcaderecomp_cps1_game_driver();
    if (!game_driver || game_driver->id != MULTIARCADERECOMP_CPS1_GAME_ID || !game_driver->machine) {
        std::cerr << "CPS-1 game module has no matching C++ driver profile\n";
        return 1;
    }
    configure_game(*game_driver);
    if (argc < 2) {
        std::cerr << "usage: cps1_boot <program.bin> [cycle-budget] [m68k-misses] [z80-fixed z80-banked [z80-misses z80-fallbacks [gfx.bin [frame.ppm [--window] [--until-video] [--oki samples.bin] [--m68k-ram-entries file] [--gfxram-snapshot file]]]]]]\n";
        return 2;
    }
    std::vector<uint8_t> image;
    if (!load_file(argv[1], image) || image.size() < 8) {
        std::cerr << "cannot read 68000 image: " << argv[1] << '\n';
        return 1;
    }
    const uint64_t budget = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 12'000'000;
    const std::string miss_path = argc > 3 ? argv[3] : "m68k_dynamic_entries.txt";
    bool live_window = false;
    bool until_video = false;
    std::string oki_path;
    std::string qsound_program_path;
    std::string qsound_samples_path;
    std::string eeprom_path;
    std::string ram_entries_path = "m68k_ram_entries.txt";
    std::string gfxram_snapshot_path = "m68k_gfxram_snapshot.bin";
    for (int i = 10; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--window") live_window = true;
        else if (option == "--until-video") until_video = true;
        else if (option == "--oki" && i + 1 < argc) oki_path = argv[++i];
        else if (option == "--qsound-program" && i + 1 < argc) qsound_program_path = argv[++i];
        else if (option == "--qsound-samples" && i + 1 < argc) qsound_samples_path = argv[++i];
        else if (option == "--eeprom" && i + 1 < argc) eeprom_path = argv[++i];
        else if (option == "--m68k-ram-entries" && i + 1 < argc) ram_entries_path = argv[++i];
        else if (option == "--gfxram-snapshot" && i + 1 < argc) gfxram_snapshot_path = argv[++i];
        else if (!option.empty()) {
            std::cerr << "unknown CPS-1 runner option: " << option << '\n';
            return 2;
        }
    }
    if (argc > 5) {
        std::vector<uint8_t> fixed_rom, banked_rom, oki_samples;
        if (!load_file(argv[4], fixed_rom)) {
            std::cerr << "cannot read CPS-1 Z80 fixed ROM: " << argv[4] << '\n';
            return 1;
        }
        if (!load_file(argv[5], banked_rom)) {
            std::cerr << "cannot read CPS-1 Z80 banked ROM: " << argv[5] << '\n';
            return 1;
        }
        if (!oki_path.empty() && !load_file(oki_path, oki_samples)) {
            std::cerr << "cannot read CPS-1 OKI sample ROM: " << oki_path << '\n';
            return 1;
        }
        if (game_driver->machine->sound_board == multiarcaderecomp::cps1::Cps1SoundBoard::QSound) {
            std::vector<std::uint8_t> qsound_program, qsound_samples;
            if (qsound_program_path.empty() || qsound_samples_path.empty()
                || !load_file(qsound_program_path, qsound_program)
                || !load_file(qsound_samples_path, qsound_samples)
                || !initialize_qsound(std::move(qsound_program), std::move(qsound_samples),
                    multiarcaderecomp_cps1_game_z80_code(), live_window)) {
                std::cerr << "invalid CPS-1 QSound ROM images or audio initialization failed\n";
                return 1;
            }
        } else if (!initialize_sound(std::move(fixed_rom), std::move(banked_rom), std::move(oki_samples),
                       multiarcaderecomp_cps1_game_z80_code(), live_window)) {
            std::cerr << "invalid CPS-1 sound ROM sizes or sound device initialization failed\n";
            return 1;
        } else if (oki_path.empty()) {
            std::cerr << "[cps1-audio] OKI sample ROM not supplied; YM2151 will run without sample playback\n";
        }
    }
    std::string frame_path;
    if (argc > 8) {
        std::size_t expected_gfx_size = 0;
        if (game_driver->romset) {
            for (const auto& region : game_driver->romset->regions)
                if (region.name == "gfx") expected_gfx_size = region.exported_size;
        }
        if (!load_file(argv[8], g_gfx_rom) || g_gfx_rom.size() != expected_gfx_size) {
            std::cerr << "invalid CPS-1 graphics ROM image: " << argv[8] << '\n';
            return 1;
        }
    }
    if (argc > 9) frame_path = argv[9];
 #ifdef MULTIARCADERECOMP_CPS1_SDL
    set_live_window_enabled(live_window);
 #endif
    const char* frame_dump_dir_env = std::getenv("MULTIARCADERECOMP_CPS1_FRAME_DUMP_DIR");
    const char* frame_dump_every_env = std::getenv("MULTIARCADERECOMP_CPS1_FRAME_DUMP_EVERY");
    const std::filesystem::path frame_dump_dir = frame_dump_dir_env ? frame_dump_dir_env : "";
    const uint64_t frame_dump_every = frame_dump_every_env
        ? std::strtoull(frame_dump_every_env, nullptr, 0) : 60;
    if (!frame_dump_dir.empty() && frame_dump_every != 0) {
        std::error_code error;
        std::filesystem::create_directories(frame_dump_dir, error);
        if (error) {
            std::cerr << "cannot create CPS-1 frame dump directory: " << error.message() << '\n';
            return 1;
        }
        std::cout << "[cps1] dumping one PPM every " << frame_dump_every
                  << " VBlanks to " << frame_dump_dir << '\n';
    }
    bool video_found = false;
    const bool force_trap_fallback = std::getenv("MULTIARCADERECOMP_CPS1_FORCE_TRAP_FALLBACK") != nullptr;
    const bool force_all_fallback = std::getenv("MULTIARCADERECOMP_CPS1_FORCE_ALL_FALLBACK") != nullptr;
    g_trace_vblank = std::getenv("MULTIARCADERECOMP_CPS1_TRACE_VBLANK") != nullptr;
    initialize_bus(image);
    if (!eeprom_path.empty()) cps1_eeprom_set_path(eeprom_path);
    cps1_eeprom_reset();
    configure_m68k_diagnostics(std::getenv("MULTIARCADERECOMP_CPS1_PC_PROFILE") != nullptr,
        std::getenv("MULTIARCADERECOMP_CPS1_TRACE_TRAP") != nullptr
            || std::getenv("MULTIARCADERECOMP_CPS1_TRACE_LAYER_CONTROL") != nullptr);
    size_t m68k_entry_count = 0;
    const ng_func_entry_t* m68k_entries = multiarcaderecomp_cps1_game_m68k_table(&m68k_entry_count);
    size_t ram_snapshot_size = 0;
    const uint8_t* ram_snapshot = multiarcaderecomp_cps1_game_ram_snapshot(&ram_snapshot_size);
    if (!m68k_entries || m68k_entry_count == 0) {
        std::cerr << "CPS-1 game module has no generated 68000 entries\n";
        return 1;
    }
    initialize_m68k(m68k_entries, m68k_entry_count, ram_snapshot, ram_snapshot_size);

#ifdef MULTIARCADERECOMP_CPS1_SDL
    while (live_window || static_cast<uint64_t>(ng_cycles) < budget) {
#else
    while (static_cast<uint64_t>(ng_cycles) < budget) {
#endif
        sync_sound_to_main_clock();
        while (ng_cycles >= g_next_vblank) {
            ++g_vblank_count;
            if (std::getenv("MULTIARCADERECOMP_CPS1_AUTOPLAY_SELECT")) {
                const std::uint8_t system_inputs =
                    ((g_vblank_count >= 300 && g_vblank_count < 310) ? 0x01 : 0)
                    | ((g_vblank_count >= 360 && g_vblank_count < 370) ? 0x10 : 0);
                const std::uint16_t player_inputs =
                    (g_vblank_count >= 420 && g_vblank_count < 422) ? 0x0001 : 0;
                set_host_inputs(player_inputs, system_inputs, 0, 0);
                if (g_vblank_count == 300 || g_vblank_count == 310
                    || g_vblank_count == 360 || g_vblank_count == 370
                    || g_vblank_count == 420 || g_vblank_count == 422)
                    std::cerr << "[cps1-autoplay] frame=" << g_vblank_count
                              << " system-inputs=$" << std::hex << unsigned(system_inputs)
                              << " players=$" << player_inputs
                              << std::dec << '\n';
            }
            if (g_trace_vblank && g_vblank_count <= 8)
                std::cerr << "[cps1] VBLANK n=" << g_vblank_count << " cycles=" << ng_cycles
                          << " edge=" << g_next_vblank << " PC=$" << std::hex << ng_cpu.pc
                          << " SR=$" << ng_get_sr() << std::dec
                          << " pending=" << g_irq1_pending << '\n';
            const size_t obj_base = video_base(0, 0x800);
            for (size_t i = 0; i < g_buffered_obj.size(); ++i)
                g_buffered_obj[i] = g_gfxram[(obj_base + i) & 0x3ffff];
            g_irq1_pending = true;
            g_next_vblank += cps1_frame_cycles();
            if (!frame_dump_dir.empty() && frame_dump_every != 0
                && (g_vblank_count % frame_dump_every) == 0) {
                std::ostringstream filename;
                filename << "frame_" << std::setw(6) << std::setfill('0') << g_vblank_count
                         << "_cycle_" << std::setw(12) << static_cast<uint64_t>(ng_cycles) << ".ppm";
                const auto path = frame_dump_dir / filename.str();
                if (!write_frame(path.string())) {
                    std::cerr << "could not save CPS-1 frame dump: " << path << '\n';
                    return 1;
                }
            }
            if (until_video && !frame_path.empty() && !video_found) {
                if (!write_frame("")) {
                    std::cerr << "could not probe CPS-1 framebuffer: " << frame_path << '\n';
                    return 1;
                }
                video_found = video_nonzero_pens() != 0;
                if (video_found && !write_frame(frame_path)) {
                    std::cerr << "could not save CPS-1 framebuffer: " << frame_path << '\n';
                    return 1;
                }
            }
#ifdef MULTIARCADERECOMP_CPS1_SDL
            if (live_window) write_frame("");
#endif
        }
        if (live_window_quit_requested()) break;
        const unsigned sr_mask = (ng_get_sr() >> 8) & 7;
        // CPS1 calls set_interrupt_mixer(false); MAME's IPL1 input is bit 1
        // of the three-line interrupt level, so VBlank is priority level 2.
        if (g_irq1_pending && sr_mask < 2) {
            ng_exception(26, ng_cpu.pc);
            ng_cpu.ipl = 2;
            ng_cpu.stopped = 0;
            g_irq1_pending = false;
        }
        if (force_trap_fallback && (ng_cpu.pc == 0x0005c4u || (ng_cpu.pc >= 0x000aa6u && ng_cpu.pc <= 0x000ab2u))) {
            dispatch_m68k_instruction(force_trap_fallback, force_all_fallback, g_next_vblank);
            continue;
        }
        // Each generated block returns at or after the next board event.
        dispatch_m68k_instruction(false, force_all_fallback, g_next_vblank);
        if (ng_cpu.stopped) ng_cycles += 4;
    }
    if (const char* dump_path = std::getenv("MULTIARCADERECOMP_CPS1_DUMP_BUFFERED_OBJ")) {
        std::ofstream dump(dump_path, std::ios::binary);
        if (dump)
            dump.write(reinterpret_cast<const char*>(g_buffered_obj.data()),
                static_cast<std::streamsize>(g_buffered_obj.size()));
    }
    if (!frame_path.empty() && (!until_video || video_found)) {
        if (!write_frame(frame_path)) {
            std::cerr << "could not render CPS-1 framebuffer: " << frame_path << '\n';
            return 1;
        }
        std::cout << "[cps1] framebuffer=" << frame_path << " (384x224 prototype)\n";
        std::cout << "[cps1] video CPS-A=";
        for (unsigned i = 0; i < g_cps_a.size(); ++i)
            if (g_cps_a[i]) std::cout << std::hex << i << ':' << g_cps_a[i] << ' ';
        std::cout << " CPS-B=";
        for (unsigned i = 0; i < g_cps_b.size(); ++i)
            if (g_cps_b[i] != 0xffff) std::cout << std::hex << i << ':' << g_cps_b[i] << ' ';
        std::cout << std::dec << " gfxram-nonzero="
                  << std::count_if(g_gfxram.begin(), g_gfxram.end(), [](uint8_t v) { return v != 0; })
                  << " sound-cmd=$" << std::hex << unsigned(sound_command())
                  << " YM-register-writes=" << std::dec
                  << sound_ym_register_count()
                  << '\n';
        const size_t palette_base = video_base(5, 0x400);
        const size_t scroll_base = video_base(1, 0x4000);
        const size_t object_base = video_base(0, 0x800);
        std::cout << "[cps1] video samples palette=" << std::hex << gfxram_word(palette_base)
                  << ',' << gfxram_word(palette_base + 2) << " scroll1="
                  << gfxram_word(scroll_base) << ',' << gfxram_word(scroll_base + 2)
                  << " object=" << gfxram_word(object_base) << ','
                  << gfxram_word(object_base + 2) << ',' << gfxram_word(object_base + 4)
                  << ',' << gfxram_word(object_base + 6) << std::dec << '\n';
        const size_t other_base = video_base(4, 0x800);
        std::cout << "[cps1] scroll x/y S1=" << std::hex << g_cps_a[6] << ',' << g_cps_a[7]
                  << " S2=" << g_cps_a[8] << ',' << g_cps_a[9]
                  << " S3=" << g_cps_a[10] << ',' << g_cps_a[11]
                  << " row-base=" << other_base << " row-offset=" << g_cps_a[0x10]
                  << " video-control=" << g_cps_a[0x11] << " rows=";
        for (unsigned y : {16U, 96U, 208U}) {
            const unsigned row = (y + g_cps_a[0x10]) & 0x3ff;
            std::cout << y << ':' << gfxram_word(other_base + row * 2) << ' ';
        }
        std::cout << std::dec << '\n';
    }
    const Cps1CpuStats cpu_stats = m68k_stats();
    std::cout << "[cps1] cycles=" << ng_cycles << " PC=$" << std::hex << ng_cpu.pc << std::dec
              << " native blocks=" << cpu_stats.native_blocks
              << " native instructions=" << cpu_stats.native_instructions
              << " Musashi instructions=" << cpu_stats.interpreted_instructions
              << " uncovered ROM entries=" << cpu_stats.uncovered_entries;
    print_sound_summary(std::cout);
    std::cout << " SR=$" << std::hex << ng_get_sr() << std::dec
              << " bus reads=" << bus_reads() << " writes=" << bus_writes()
              << " video-pens=" << video_nonzero_pens() << '\n';
    std::cout << "[cps1] final-regs A0=$" << std::hex << ng_cpu.a[0]
              << " A1=$" << ng_cpu.a[1] << " A3=$" << ng_cpu.a[3]
              << " A5=$" << ng_cpu.a[5] << " A6=$" << ng_cpu.a[6]
              << " A4=$" << ng_cpu.a[4]
              << " D0=$" << ng_cpu.d[0] << " D1=$" << ng_cpu.d[1] << std::dec << '\n';
    write_m68k_profile(std::cerr);
    if (until_video && !video_found)
        std::cout << "[cps1] video target not reached within cycle budget; no PPM written\n";
    if (!save_m68k_entries(miss_path)) {
        std::cerr << "cannot write dynamic entry list: " << miss_path << '\n';
        return 1;
    }
    if (!save_m68k_ram_artifacts(ram_entries_path, gfxram_snapshot_path)) {
        std::cerr << "cannot write executable RAM diagnostics\n";
        return 1;
    }
    if (!save_sound_artifacts(argc > 6 ? argv[6] : "z80_dynamic_entries.txt",
            argc > 7 ? argv[7] : "z80_fallbacks.log")) {
        std::cerr << "cannot write CPS-1 Z80 diagnostics\n";
        return 1;
    }
    shutdown_sound();
    if (!cps1_eeprom_save())
        std::cerr << "[cps1] could not save EEPROM image: " << eeprom_path << '\n';
    return 0;
}
