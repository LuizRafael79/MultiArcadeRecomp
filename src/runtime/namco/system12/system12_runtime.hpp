#pragma once

#include "boards/namco/system12/system12_board.hpp"
#include "h8_cpu.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mamerecomp::namco::system12 {

// System 12 board session: board bus and H8/3002 auxiliary CPU scheduling.
// The PSX-derived SoC remains in the shared CPU/machine runtime.
class System12Runtime {
public:
    explicit System12Runtime(const mamerecomp::rom::RomRegions& roms);

    PsxBus make_bus() { return m_board.make_bus(); }
    std::vector<std::uint32_t> dma_rom_read(std::size_t words) {
        return m_board.dma_rom_read(words);
    }
    void sync_maincpu_cycles(std::uint64_t cycles);

    bool sub_read(std::uint32_t address, unsigned size, std::uint32_t* value) {
        return m_board.sub_read(address, size, value);
    }
    bool sub_write(std::uint32_t address, unsigned size, std::uint32_t value) {
        return m_board.sub_write(address, size, value);
    }
    std::uint64_t h8_cycles() const { return m_h8.cycles(); }
    std::uint32_t h8_pc() const { return m_h8.pc(); }
    std::uint32_t audio_sample_rate() const { return m_board.audio_sample_rate(); }
    unsigned active_audio_voices() const { return m_board.active_audio_voices(); }
    std::uint64_t c352_writes() const { return m_board.c352_writes(); }
    std::vector<std::int16_t> render_audio(std::size_t frames) {
        return m_board.render_audio(frames);
    }

private:
    NamcoSystem12Board m_board;
    mamerecomp::h8::H8Advanced m_h8;
    std::uint64_t m_maincpu_cycles = 0;
};

} // namespace mamerecomp::namco::system12
