#include "system16_video.hpp"

#include <stdexcept>
#include <utility>

namespace mamerecomp {
struct System16APresenter::Impl {};

System16APresenter::System16APresenter(std::string)
    : impl_(std::make_unique<Impl>()) {
    throw std::runtime_error("SDL3 live window presentation was not built");
}
System16APresenter::~System16APresenter() = default;

bool System16APresenter::present(const System16AFrame&, std::span<const std::uint8_t>) {
    throw std::runtime_error("SDL3 live window presentation was not built");
}

System16ALiveInputs System16APresenter::live_inputs() const { return {}; }
bool System16APresenter::show_configuration_menu(System16GameSettings&, const std::filesystem::path&, const std::string&) { return false; }
bool system16a_presenter_available() { return false; }
} // namespace mamerecomp
