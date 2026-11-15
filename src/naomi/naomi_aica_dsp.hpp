#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace multiarcaderecomp::naomi {

// Naomi AICA's microcoded 128-step DSP interpreter, adapted from Flycast's
// core/hw/aica/dsp_interp.cpp and dsp.cpp. Voice generation/mixing feeds MIXS
// through the surrounding AICA runtime.
class NaomiAicaDsp {
public:
    NaomiAicaDsp();

    [[nodiscard]] static bool ownsDataRegister(std::uint32_t offset);
    [[nodiscard]] std::uint32_t readDataRegister(
        std::uint32_t offset, unsigned width,
        const std::array<std::uint8_t, 0x8000>& registers) const;
    void writeDataRegister(std::uint32_t offset, unsigned width,
                           std::uint32_t value);
    void noteRegisterWrite(std::uint32_t offset, unsigned width);
    void setVoiceMix(const std::array<std::int32_t, 16>& mixs);
    void runSample(std::array<std::uint8_t, 0x8000>& registers,
                   std::vector<std::uint8_t>& wave_ram);

private:
    [[nodiscard]] static std::uint32_t read32(
        const std::array<std::uint8_t, 0x8000>& registers,
        std::uint32_t offset);
    static void write32(std::array<std::uint8_t, 0x8000>& registers,
                        std::uint32_t offset, std::uint32_t value);
    void runStep(std::array<std::uint8_t, 0x8000>& registers,
                 std::vector<std::uint8_t>& wave_ram);

    std::array<std::int32_t, 128> m_temp{};
    std::array<std::int32_t, 32> m_mems{};
    std::array<std::int32_t, 16> m_mixs{};
    std::uint32_t m_rbp = 0;
    std::uint32_t m_rbl = 0x7fffu;
    std::uint32_t m_mdec_ct = 1;
    bool m_stopped = true;
    bool m_program_dirty = true;
};

} // namespace multiarcaderecomp::naomi
