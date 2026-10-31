#pragma once

#include "naomi_module.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace mamerecomp::naomi {

// Small SDL diagnostic viewer for PVR direct-framebuffer readback. This is
// intentionally separate from the System 16 presenter and has no game input.
class NaomiPvrPresenter {
public:
    explicit NaomiPvrPresenter(const std::string& title);
    ~NaomiPvrPresenter();
    NaomiPvrPresenter(const NaomiPvrPresenter&) = delete;
    NaomiPvrPresenter& operator=(const NaomiPvrPresenter&) = delete;

    void show(const NaomiPvrFrameInfo& info, std::span<const std::uint8_t> rgb);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace mamerecomp::naomi
