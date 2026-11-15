#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace multiarcaderecomp::devices {

// Serial EEPROM protocols used by arcade boards. The host owns pin mapping;
// this device owns the serial command state and storage behavior.
class SerialEeprom {
public:
    enum class Protocol { Er5911_8Bit };

    explicit SerialEeprom(Protocol protocol, std::span<const std::uint8_t> image = {});
    void load(std::span<const std::uint8_t> image);
    void write_lines(bool data_in, bool chip_select, bool clock);
    bool data_out() const { return data_out_; }
    bool ready() const { return true; }
    std::span<const std::uint8_t> image() const { return cells_; }

private:
    void rising_clock();
    void reset_transaction();

    Protocol protocol_;
    std::vector<std::uint8_t> cells_;
    std::uint16_t command_ = 0;
    std::uint8_t command_bits_ = 0;
    std::uint16_t address_ = 0;
    std::uint8_t read_bit_ = 0;
    std::uint8_t read_byte_ = 0xff;
    std::uint8_t write_byte_ = 0;
    std::uint8_t write_bits_ = 0;
    bool selected_ = false;
    bool clock_ = false;
    bool data_in_ = false;
    bool data_out_ = true;
    bool wait_start_ = true;
    bool reading_ = false;
    bool writing_ = false;
    bool writing_all_ = false;
    bool waiting_for_cs_cycle_ = false;
    bool locked_ = true;
};

} // namespace multiarcaderecomp::devices
