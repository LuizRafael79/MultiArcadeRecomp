#pragma once

#include "mk/tunit.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mamerecomp::mk {

class WilliamsAdpcmBoard;

struct AudioDiagnostics {
    std::uint64_t ym_writes{};
    std::uint64_t oki_writes{};
    std::uint64_t dac_writes{};
    std::uint64_t generated_sample_frames{};
    std::uint64_t nonzero_sample_frames{};
    std::uint16_t peak{};
    std::uint16_t ym_peak{};
    std::uint16_t oki_peak{};
    std::uint16_t dac_peak{};
};

// MK rev. 5 T-Unit machine wiring. This composes board devices and their
// interconnects; CPU execution remains in the shared core/cpus backends.
class MKMachine final {
public:
    MKMachine(std::span<const std::uint8_t> main_rom,
              std::span<const std::uint8_t> graphics_rom,
              std::span<const std::uint8_t> sound_cpu_rom,
              std::span<const std::uint8_t> oki_rom);
    ~MKMachine();

    TUnit& tunit() noexcept;
    const TUnit& tunit() const noexcept;
    WilliamsAdpcmBoard& sound_board() noexcept;
    const WilliamsAdpcmBoard& sound_board() const noexcept;

    // Machine-side sound CPU bus and line interface. CPU code calls these
    // through its generic bus callbacks; device implementation stays private.
    std::uint8_t sound_cpu_read(std::uint16_t address) noexcept;
    void sound_cpu_write(std::uint16_t address, std::uint8_t value) noexcept;
    std::uint8_t sound_cpu_bank() const noexcept;
    bool sound_cpu_reset_asserted() const noexcept;
    bool sound_cpu_irq_asserted() const noexcept;
    bool sound_cpu_firq_asserted() const noexcept;
    void advance_sound_cycles(std::uint32_t cycles) noexcept;
    std::vector<std::int16_t> take_audio_samples() noexcept;
    AudioDiagnostics audio_diagnostics() const noexcept;

private:
    // The T-Unit keeps a non-owning link to this owned sound-board device.
    std::unique_ptr<WilliamsAdpcmBoard> m_sound_board;
    TUnit m_tunit;
};

} // namespace mamerecomp::mk
