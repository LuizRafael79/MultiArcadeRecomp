#pragma once

#include "boards/sega/common/video_frame.hpp"
#include "runtime/sega/game_settings_api.hpp"
#include "runtime/sega/presentation/sega_config.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace multiarcaderecomp {

struct SegaLiveInputs {
    std::uint8_t service = 0xff, p1 = 0xff, p2 = 0xff;
};

struct SegaPresenterTiming {
    std::uint64_t frames = 0;
    std::uint64_t convert_ns = 0, upload_ns = 0, vsync_ns = 0, limiter_ns = 0;
    std::uint64_t convert_max_ns = 0, upload_max_ns = 0, vsync_max_ns = 0, limiter_max_ns = 0;
};

class SegaPresenter {
public:
    explicit SegaPresenter(std::string title,
        std::vector<multiarcaderecomp::sega::GameInputBinding> input_bindings = {}, bool frame_limit_enabled = true,
        bool profile_enabled = false, std::uint32_t refresh_num = 60, std::uint32_t refresh_den = 1);
    ~SegaPresenter();
    SegaPresenter(const SegaPresenter&) = delete;
    SegaPresenter& operator=(const SegaPresenter&) = delete;
    bool configure_display(SegaDisplayMode mode, std::uint8_t window_scale);
    bool present(const SegaVideoFrame& frame, std::span<const std::uint8_t> palette_ram);
    SegaLiveInputs live_inputs() const;
    bool has_input_focus() const;
    SegaPresenterTiming timing() const;
    bool show_configuration_menu(SegaSharedSettings& settings,
        const std::filesystem::path& settings_path, const std::string& game_title,
        const multiarcaderecomp::sega::GameSettingsApi* game_api = nullptr, void* game_state = nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace multiarcaderecomp
