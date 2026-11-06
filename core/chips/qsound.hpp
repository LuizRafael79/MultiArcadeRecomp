// license:BSD-3-Clause
// Copyright (c) 2026 MameRecomp contributors.
// QSound peripheral contract adapted from MAME's qsound.cpp/qsoundhle.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace mamerecomp::chips {

enum class QSoundMode : std::uint8_t { Hle, Lle };

struct QSoundConfig {
    std::uint32_t clock_hz = 60'000'000;
    QSoundMode mode = QSoundMode::Lle;
    // DSP firmware contains the 0x1000 internal 16-bit words. It is used by
    // LLE directly and by the MAME-faithful HLE for coefficient tables.
    std::span<const std::uint16_t> firmware;
};

// Board-neutral Capcom QSound device. The owner supplies sample ROM and clocks
// the device. ZN/CPS wiring, address maps, Z80 and audio host APIs stay outside.
class QSound {
public:
    explicit QSound(QSoundConfig config = {});
    ~QSound();
    QSound(QSound&&) noexcept;
    QSound& operator=(QSound&&) noexcept;
    QSound(const QSound&) = delete;
    QSound& operator=(const QSound&) = delete;

    void set_sample_rom(std::span<const std::uint8_t> bytes);
    void set_firmware(std::span<const std::uint16_t> words);
    void reset();
    void write(std::uint8_t offset, std::uint8_t data);
    std::uint8_t read_status();
    // Advance DSP input clocks; ignored by HLE, used by LLE.
    void run_clocks(std::uint64_t clocks);
    // Interleaved signed stereo PCM at sample_rate().
    void render(std::span<std::int16_t> stereo);
    std::uint32_t sample_rate() const;
    QSoundMode mode() const;
    std::uint64_t dsp_cycles() const;
    std::uint64_t dsp_instructions() const;
    std::uint16_t dsp_pc() const;
    std::uint64_t sample_reads() const;
    std::uint64_t pio_reads() const;
    std::uint64_t pio_writes() const;
    std::uint64_t serial_words() const;
    std::uint64_t serial_left_words() const;
    std::uint64_t serial_right_words() const;
    std::uint64_t pcm_frames_produced() const;
    std::uint64_t nonzero_sample_reads() const;
    std::uint64_t nonzero_serial_words() const;
    std::uint16_t serial_word_peak() const;
    std::uint64_t dsp_ock_rising_edges() const;
    std::uint64_t dsp_do_transitions() const;
    std::uint64_t dsp_ose_transitions() const;
    std::uint64_t dsp_old_transitions() const;
    std::uint64_t dsp_serial_word_loads() const;
    std::uint64_t dsp_sdx_writes() const;
    std::uint64_t dsp_nonzero_sdx_writes() const;
    std::uint64_t dsp_nonzero_serial_loads() const;
    std::uint16_t dsp_last_sdx_value() const;
    std::uint16_t dsp_last_serial_word_loaded() const;
    std::uint16_t dsp_last_sdx_pc() const;
    std::uint16_t dsp_last_sdx_opcode() const;
    std::int64_t dsp_last_sdx_a0() const;
    std::int64_t dsp_last_sdx_a1() const;
    std::int16_t dsp_last_sdx_x() const;
    std::int32_t dsp_last_sdx_y() const;
    std::int32_t dsp_last_sdx_p() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mamerecomp::chips
