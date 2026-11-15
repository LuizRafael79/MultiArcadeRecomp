#include "runtime/sega/presentation/sega_presenter.hpp"

#include <stdexcept>
#include <utility>

namespace multiarcaderecomp {
struct SegaPresenter::Impl {};

SegaPresenter::SegaPresenter(std::string,
    std::vector<multiarcaderecomp::sega::GameInputBinding>, bool, bool, std::uint32_t, std::uint32_t)
    : impl_(std::make_unique<Impl>()) {
    throw std::runtime_error("SDL3 live window presentation was not built");
}
SegaPresenter::~SegaPresenter() = default;

bool SegaPresenter::configure_display(SegaDisplayMode, std::uint8_t) {
    throw std::runtime_error("SDL3 live window presentation was not built");
}

bool SegaPresenter::present(const SegaVideoFrame&, std::span<const std::uint8_t>) {
    throw std::runtime_error("SDL3 live window presentation was not built");
}

SegaLiveInputs SegaPresenter::live_inputs() const { return {}; }
bool SegaPresenter::has_input_focus() const { return false; }
SegaPresenterTiming SegaPresenter::timing() const { return {}; }
bool SegaPresenter::show_configuration_menu(SegaSharedSettings&, const std::filesystem::path&,
    const std::string&, const multiarcaderecomp::sega::GameSettingsApi*, void*) { return false; }

} // namespace multiarcaderecomp
