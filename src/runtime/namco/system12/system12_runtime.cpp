#include "system12_runtime.hpp"

namespace multiarcaderecomp::namco::system12 {

System12Runtime::System12Runtime(const multiarcaderecomp::rom::RomRegions& roms)
    : m_board(roms),
      m_h8([this](multiarcaderecomp::h8::u32 address) {
          std::uint32_t value = 0xff;
          return m_board.sub_read(address, 1, &value)
              ? static_cast<multiarcaderecomp::h8::u8>(value)
              : static_cast<multiarcaderecomp::h8::u8>(0xff);
      }, [this](multiarcaderecomp::h8::u32 address, multiarcaderecomp::h8::u8 value) {
          m_board.sub_write(address, 1, value);
      })
{
}

void System12Runtime::sync_maincpu_cycles(std::uint64_t cycles) {
    if (cycles - m_maincpu_cycles < 4096) return;
    const std::uint64_t elapsed = cycles - m_maincpu_cycles;
    // MAME clocks the H8/3002 at 16.9344 MHz against the 67.7376 MHz
    // R3000A. Keep its quarter-rate phase exact across bounded slices.
    const unsigned budget = static_cast<unsigned>(elapsed / 4);
    if (budget) m_h8.run_cycles(budget);
    m_maincpu_cycles += std::uint64_t(budget) * 4;
}

} // namespace multiarcaderecomp::namco::system12
