#pragma once

#include "naomi_module.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace multiarcaderecomp::naomi {

// SDL viewer for Naomi PVR direct-framebuffer readback.
class NaomiPvrPresenter {
public:
    explicit NaomiPvrPresenter(const std::string& title);
    ~NaomiPvrPresenter();
    NaomiPvrPresenter(const NaomiPvrPresenter&) = delete;
    NaomiPvrPresenter& operator=(const NaomiPvrPresenter&) = delete;

    void show(const NaomiPvrFrameInfo& info, std::span<const std::uint8_t> rgb,
              bool active_scanout);
    void waitUntilClosed();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace multiarcaderecomp::naomi
