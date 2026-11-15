#include "commands/naomi_commands.hpp"
#include "commands/machine_game_command.hpp"
#include "naomi/naomi_module.hpp"
#include "naomi/naomi_romset.hpp"
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#include <SDL3/SDL.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <dlfcn.h>

namespace multiarcaderecomp::app::naomi {

class NaomiMetadataModule {
public:
    explicit NaomiMetadataModule(const std::filesystem::path& fallback_directory) {
        const char* configured_directory = std::getenv("MULTIARCADERECOMP_MODULE_DIR");
        const auto module_directory = configured_directory && *configured_directory
            ? std::filesystem::path(configured_directory) : fallback_directory;
        const auto path = module_directory / "multiarcaderecomp_naomi.so";
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle) throw std::runtime_error("could not load Naomi module " + path.string() + ": " + dlerror());
        const auto abi = reinterpret_cast<std::uint32_t (*)()>(dlsym(m_handle, "multiarcaderecomp_naomi_abi"));
        m_profiles = reinterpret_cast<ProfilesFn>(dlsym(m_handle, "multiarcaderecomp_naomi_profiles"));
        m_probe_create = reinterpret_cast<ProbeCreateFn>(dlsym(m_handle, "multiarcaderecomp_naomi_sh4_probe_create"));
        m_probe_destroy = reinterpret_cast<ProbeDestroyFn>(dlsym(m_handle, "multiarcaderecomp_naomi_sh4_probe_destroy"));
        m_probe_start = reinterpret_cast<ProbeStartFn>(dlsym(m_handle, "multiarcaderecomp_naomi_sh4_probe_start"));
        m_probe_run = reinterpret_cast<ProbeRunFn>(dlsym(m_handle, "multiarcaderecomp_naomi_sh4_probe_run"));
        m_probe_frame = reinterpret_cast<ProbeFrameFn>(dlsym(m_handle, "multiarcaderecomp_naomi_sh4_probe_frame"));
        if (!abi || !m_profiles || !m_probe_create || !m_probe_destroy ||
            !m_probe_start || !m_probe_run || !m_probe_frame || abi() != 6) {
            const auto error = dlerror();
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("Naomi module ABI mismatch: " + path.string()
                + (error ? ": " + std::string(error) : std::string{}));
        }
    }
    ~NaomiMetadataModule() { if (m_handle) dlclose(m_handle); }
    NaomiMetadataModule(const NaomiMetadataModule&) = delete;
    NaomiMetadataModule& operator=(const NaomiMetadataModule&) = delete;
    const NaomiProfileRecord* profiles(std::size_t* count) const { return m_profiles(count); }
    void* create_probe(const char* set_id, bool interpreter_only) const {
        return m_probe_create(set_id, interpreter_only ? 1 : 0);
    }
    void destroy_probe(void* probe) const { m_probe_destroy(probe); }
    int start_probe(void* probe, const std::vector<std::uint8_t>& rom,
                    const std::vector<std::uint8_t>& board_id_eeprom,
                    const std::vector<std::uint8_t>& bios,
                    NaomiSh4ProbeResult* result) const {
        return m_probe_start(probe, rom.data(), rom.size(),
                             board_id_eeprom.data(), board_id_eeprom.size(),
                             bios.data(), bios.size(), result);
    }
    int run_probe(void* probe, std::uint64_t budget,
                  NaomiSh4ProbeResult* result) const {
        return m_probe_run(probe, budget, result);
    }
    bool capture_frame(void* probe, NaomiPvrFrameInfo* info,
                       std::vector<std::uint8_t>* rgb) const {
        if (!info || !rgb || !m_probe_frame(probe, info, nullptr, 0)) return false;
        rgb->resize(info->rgb_bytes);
        return m_probe_frame(probe, info, rgb->data(), rgb->size()) != 0;
    }
private:
    using ProfilesFn = const NaomiProfileRecord* (*)(std::size_t*);
    using ProbeCreateFn = void* (*)(const char*, int);
    using ProbeDestroyFn = void (*)(void*);
    using ProbeStartFn = int (*)(void*, const std::uint8_t*, std::size_t,
                                 const std::uint8_t*, std::size_t,
                                 const std::uint8_t*, std::size_t,
                                 NaomiSh4ProbeResult*);
    using ProbeRunFn = int (*)(void*, std::uint64_t, NaomiSh4ProbeResult*);
    using ProbeFrameFn = int (*)(void*, NaomiPvrFrameInfo*, std::uint8_t*, std::size_t);
    void* m_handle = nullptr;
    ProfilesFn m_profiles = nullptr;
    ProbeCreateFn m_probe_create = nullptr;
    ProbeDestroyFn m_probe_destroy = nullptr;
    ProbeStartFn m_probe_start = nullptr;
    ProbeRunFn m_probe_run = nullptr;
    ProbeFrameFn m_probe_frame = nullptr;
};






int list_games(const std::filesystem::path& module_directory) {
    const NaomiMetadataModule module(module_directory);
    std::size_t count = 0;
    const auto* profiles = module.profiles(&count);
    std::cout << "Naomi game profiles (metadata only; SH-4 runtime is not implemented yet):\n";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& profile = profiles[i];
        std::cout << "  " << profile.set_id << "  " << profile.title
                  << "  [Naomi, " << profile.rom_board << ", ROM region 0x"
                  << std::hex << profile.rom_board_region_size << std::dec
                  << ", key=" << profile.security_key
                  << ", SH-4=" << static_cast<unsigned>(profile.sh4_count)
                  << ", PVR2=" << static_cast<unsigned>(profile.pvr2_count)
                  << ", ARM7/AICA=" << (profile.has_arm7_aica ? "yes" : "no")
                  << ", Maple/JVS=" << (profile.has_maple_jvs ? "yes" : "no")
                  << ", ELAN=" << (profile.has_elan ? "yes" : "no")
                  << ", GD-ROM=" << (profile.has_gdrom ? "yes" : "no") << "]\n";
    }
    return 0;
}

int inspect_roms(const char* set_id, const char* rom_path) {
    const auto image = multiarcaderecomp::naomi::load_mvc2_rom_board(set_id, rom_path);
    std::cout << "Validated Naomi cartridge set " << image.set_id
              << ": " << image.rom_files_loaded << " ROM loads assembled into 0x"
              << std::hex << image.bytes.size() << std::dec
              << " bytes; BIOS not loaded (BIOS-free path).\n";
    return 0;
}

int run_sh4_command(int argc, char** argv, const std::filesystem::path& module_directory) {
            if (argc < 4) {
                multiarcaderecomp::app::print_usage(argv[0]);
                return 2;
            }
            std::uint64_t budget = 1000000;
            bool show_window = false;
            bool interpreter_only = false;
            std::filesystem::path bios_path =
                std::filesystem::path(argv[3]).parent_path() / "naomi.zip";
            for (int i = 4; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--instructions" && i + 1 < argc)
                    budget = multiarcaderecomp::app::parse_u64(argv[++i], "SH-4 instruction budget");
                else if (std::string_view(argv[i]) == "--bios" && i + 1 < argc)
                    bios_path = argv[++i];
                else if (std::string_view(argv[i]) == "--window")
                    show_window = true;
                else if (std::string_view(argv[i]) == "--interpreter-only")
                    interpreter_only = true;
                else
                    throw std::runtime_error("unknown run-naomi-sh4 option: " + std::string(argv[i]));
            }
            if (budget == 0) throw std::runtime_error("SH-4 instruction budget must be nonzero");
#ifndef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            if (show_window)
                throw std::runtime_error("--window requires SDL3 development files; reconfigure with SDL3 installed");
#endif

            const auto image = multiarcaderecomp::naomi::load_mvc2_rom_board(argv[2], argv[3]);
            const auto bios = multiarcaderecomp::naomi::load_naomi_bios(argv[2], bios_path);
            NaomiMetadataModule module(module_directory);
            void* raw_probe = module.create_probe(argv[2], interpreter_only);
            if (!raw_probe)
                throw std::runtime_error("Naomi module has no SH-4 probe for set " + std::string(argv[2]));
            const auto destroy = [&module](void* probe) { module.destroy_probe(probe); };
            std::unique_ptr<void, decltype(destroy)> probe(raw_probe, destroy);

            NaomiSh4ProbeResult result{};
            if (!module.start_probe(probe.get(), image.bytes, image.board_id_eeprom,
                                    bios.bytes, &result))
                throw std::runtime_error(result.error[0] ? result.error : "Naomi SH-4 boot setup failed");
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            std::unique_ptr<multiarcaderecomp::naomi::NaomiPvrPresenter> presenter;
            if (show_window) {
                presenter = std::make_unique<multiarcaderecomp::naomi::NaomiPvrPresenter>(
                    "MultiArcadeRecomp - Naomi BIOS executing (PVR scanout pending)");
                NaomiPvrFrameInfo boot_surface{};
                boot_surface.width = 640;
                boot_surface.height = 480;
                boot_surface.rgb_bytes = std::size_t(boot_surface.width) *
                                         boot_surface.height * 3u;
                std::vector<std::uint8_t> black(boot_surface.rgb_bytes, 0);
                presenter->show(boot_surface, black, false);
                std::cout << "Naomi window opened before SH-4 execution; waiting for PVR scanout\n"
                          << std::flush;
            }
#endif
            if (!module.run_probe(probe.get(), budget, &result))
                throw std::runtime_error(result.error[0] ? result.error : "Naomi SH-4 execution failed");

            std::cout << "SH-4 "
                      << (interpreter_only ? "interpreter-only " : "AOT+interpreter ")
                      << "Naomi-BIOS boot "
                      << image.set_id << " BIOS="
                      << bios.filename << " CRC=" << std::hex << bios.crc
                      << ": entry=0x"
                      << std::hex << result.entry << " instructions=" << std::dec
                      << result.instructions_executed << " native/recompiled="
                      << result.native_instructions << " interpreted="
                      << result.interpreted_instructions << " budget=" << budget
                      << " pc=0x" << std::hex << result.pc;
            std::cout << " PVR2={TA:" << std::dec << result.pvr_ta_bytes
                      << "B,YUV:" << result.pvr_yuv_bytes
                      << "B,VRAM:" << result.pvr_vram_bytes
                      << "B,regs:" << result.pvr_register_writes
                      << ",render-starts:" << result.pvr_render_start_requests
                      << ",fb-enabled:" << (result.pvr_framebuffer_enabled ? "yes" : "no")
                      << ",lists:" << std::dec << result.pvr_ta_completed_lists
                      << ",line:" << result.pvr_scanline << '}';
            std::cout << " TA-PCW={";
            for (std::size_t i = 0; i < 8; ++i) {
                if (i) std::cout << ',';
                std::cout << i << ':' << std::dec << result.pvr_ta_parameter_counts[i];
            }
            std::cout << "} TA-lists={";
            for (std::size_t i = 0; i < 5; ++i) {
                if (i) std::cout << ',';
                std::cout << i << ':' << std::dec << result.pvr_ta_list_packet_counts[i];
            }
            std::cout << '}';
            std::cout << " Holly={ISTNRM:0x" << std::hex << result.holly_normal_status
                      << ",IML2:0x" << result.holly_level2_mask
                      << ",IML4:0x" << result.holly_level4_mask
                      << ",IML6:0x" << result.holly_level6_mask << std::dec
                      << ",accepted:" << result.interrupts_accepted << '}';
            std::cout << " indirect={count:" << std::dec << result.indirect_calls
                      << ",last:0x" << std::hex << result.last_indirect_target << ",recent:[";
            for (std::size_t i = 0; i < result.indirect_calls && i < 16; ++i) {
                if (i != 0) std::cout << ',';
                std::cout << "0x" << result.recent_indirect_targets[i];
            }
            std::cout << "]}";
            if (result.stopped_by_budget || result.execution_stopped)
                std::cout << " regs={r1:0x" << std::hex << result.r1
                          << ",r2:0x" << result.r2 << ",r3:0x" << result.r3
                          << ",r8:0x" << result.r8 << ",r9:0x" << result.r9
                          << ",r10:0x" << result.r10 << ",r11:0x" << result.r11
                          << ",r12:0x" << result.r12 << ",r14:0x" << result.r14
                          << ",r13:0x" << result.r13 << ",r15:0x" << result.r15 << '}';
            if (result.unresolved_target ||
                (result.execution_stopped && !result.stopped_by_budget))
                std::cout << " unresolved=0x" << result.unresolved_target;
            if (result.exception_code)
                std::cout << " exception=0x" << result.exception_code;
            if (result.stopped_by_budget)
                std::cout << " stopped-by-budget";
            if (result.execution_stopped && !result.stopped_by_budget &&
                !result.unresolved_target && !result.exception_code)
                std::cout << " stopped";
            std::cout << std::dec << '\n';
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            if (show_window) {
                NaomiPvrFrameInfo frame_info{};
                std::vector<std::uint8_t> rgb;
                if (!module.capture_frame(probe.get(), &frame_info, &rgb))
                    throw std::runtime_error("could not capture the PVR direct framebuffer");
                const bool has_scanout = frame_info.width != 0 && frame_info.height != 0 &&
                    rgb.size() == static_cast<std::size_t>(frame_info.width) *
                                  frame_info.height * 3u;
                if (!has_scanout) {
                    // A cold Naomi BIOS may not have enabled FB_R yet. Keep
                    // --window useful in that state: open a clearly identified
                    // clearly identified window instead of exiting before SDL
                    // creates a window. This surface is not a PVR readback.
                    frame_info.width = 640;
                    frame_info.height = 480;
                    frame_info.rgb_bytes = static_cast<std::size_t>(frame_info.width) *
                                           frame_info.height * 3u;
                    rgb.assign(frame_info.rgb_bytes, 0);
                    std::cout << "No active PVR scanout; showing diagnostic black only "
                                 "(no framebuffer readback or guest pixels)\n";
                }
                if (has_scanout) {
                    std::cout << "PVR framebuffer readback: " << frame_info.width << 'x'
                              << frame_info.height << " RGB bytes=" << frame_info.rgb_bytes
                              << " FB_R_CTRL=0x" << std::hex << frame_info.control
                              << " FB_R_SIZE=0x" << frame_info.size
                              << " FB_R_SOF1=0x" << frame_info.start << std::dec
                              << " enabled=" << ((frame_info.control & 1u) ? "yes" : "no")
                              << "\n";
                } else {
                    std::cout << "PVR scanout state: FB_R_CTRL=0x" << std::hex << frame_info.control
                              << " FB_R_SIZE=0x" << frame_info.size
                              << " FB_R_SOF1=0x" << frame_info.start << std::dec
                              << " enabled=" << ((frame_info.control & 1u) ? "yes" : "no")
                              << "\n";
                }
                if (!presenter)
                    presenter = std::make_unique<multiarcaderecomp::naomi::NaomiPvrPresenter>(
                        has_scanout ? "MultiArcadeRecomp - Naomi PVR2 framebuffer"
                                    : "MultiArcadeRecomp - Naomi PVR2 (no active scanout)");
                presenter->show(frame_info, rgb, has_scanout);
                presenter->waitUntilClosed();
            }
#endif
            return 0;
    return 2;
}

} // namespace multiarcaderecomp::app::naomi
