#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace mamerecomp::naomi {

// Flycast's 64 channel AICA generator, with host audio output detached. The
// generated channel MIXS values are consumed by NaomiAicaDsp.
class NaomiAicaVoices {
public:
    NaomiAicaVoices();
    ~NaomiAicaVoices();
    NaomiAicaVoices(const NaomiAicaVoices&) = delete;
    NaomiAicaVoices& operator=(const NaomiAicaVoices&) = delete;

    void writeRegister(std::uint32_t offset, unsigned width,
                       std::array<std::uint8_t, 0x8000>& registers,
                       std::vector<std::uint8_t>& wave_ram);
    void readRegister(std::uint32_t offset, bool byte,
                      std::array<std::uint8_t, 0x8000>& registers,
                      std::vector<std::uint8_t>& wave_ram);
    [[nodiscard]] std::array<std::int32_t, 16> sample(
        std::array<std::uint8_t, 0x8000>& registers,
        std::vector<std::uint8_t>& wave_ram);

private:
    void bind(std::array<std::uint8_t, 0x8000>& registers,
              std::vector<std::uint8_t>& wave_ram);
};

} // namespace mamerecomp::naomi
