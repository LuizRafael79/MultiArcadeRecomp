#include "serial_eeprom.hpp"

#include <algorithm>

namespace multiarcaderecomp::devices {

SerialEeprom::SerialEeprom(Protocol protocol, std::span<const std::uint8_t> image)
    : protocol_(protocol), cells_(128, 0xff)
{
    load(image);
}

void SerialEeprom::load(std::span<const std::uint8_t> image)
{
    std::fill(cells_.begin(), cells_.end(), 0xff);
    std::copy_n(image.begin(), std::min(image.size(), cells_.size()), cells_.begin());
    reset_transaction();
}

void SerialEeprom::reset_transaction()
{
    command_ = 0;
    command_bits_ = 0;
    address_ = 0;
    read_bit_ = 0;
    write_byte_ = 0;
    write_bits_ = 0;
    data_out_ = true;
    wait_start_ = true;
    reading_ = false;
    writing_ = false;
    writing_all_ = false;
    waiting_for_cs_cycle_ = false;
}

void SerialEeprom::write_lines(bool data_in, bool chip_select, bool clock)
{
    data_in_ = data_in;
    const bool chip_select_changed = chip_select != selected_;
    if (chip_select_changed) {
        selected_ = chip_select;
        reset_transaction();
    }
    // MAME ignores a clock edge that occurs in the same output update as
    // CS assertion. The ER5911 needs CS to settle before the start-bit clock.
    const bool rising = !chip_select_changed && selected_ && !clock_ && clock;
    clock_ = clock;
    if (rising) rising_clock();
}

void SerialEeprom::rising_clock()
{
    if (wait_start_) {
        if (data_in_) {
            wait_start_ = false;
            command_ = 0;
            command_bits_ = 0;
        }
        return;
    }
    if (waiting_for_cs_cycle_) return;

    // MAME configures ER5911 with nine command address bits, plus the two
    // opcode bits. The device has 128 cells, so only the low seven address
    // bits select storage; the upper two decode LOCK/UNLOCK/ERASE ALL.
    constexpr std::uint8_t command_length = 11;
    constexpr unsigned command_address_bits = 9;
    if (command_bits_ < command_length) {
        command_ = static_cast<std::uint16_t>((command_ << 1) | (data_in_ ? 1 : 0));
        if (++command_bits_ != command_length) return;

        const unsigned opcode = command_ >> command_address_bits;
        address_ = command_ & 0x1ff;
        if (opcode == 2) {
            reading_ = true;
            read_bit_ = 0;
            data_out_ = false; // dummy zero before the first serial data bit
        } else if (opcode == 1) {
            writing_ = true;
            write_byte_ = 0;
            write_bits_ = 0;
        } else if (opcode == 3) {
            if (!locked_) cells_[address_ & 0x7f] = 0xff;
            waiting_for_cs_cycle_ = true;
            data_out_ = true;
        } else {
            const unsigned subcommand = address_ >> (command_address_bits - 2);
            if (subcommand == 0) {
                locked_ = true;
                waiting_for_cs_cycle_ = true;
            } else if (subcommand == 1) {
                writing_all_ = true;
                write_byte_ = 0;
                write_bits_ = 0;
            } else if (subcommand == 2 && !locked_) {
                std::fill(cells_.begin(), cells_.end(), 0xff);
                waiting_for_cs_cycle_ = true;
            } else if (subcommand == 3) {
                locked_ = false;
                waiting_for_cs_cycle_ = true;
            } else {
                // MAME returns to IN_RESET for invalid/locked commands; the
                // next command requires CS to fall and rise.
                waiting_for_cs_cycle_ = true;
            }
            data_out_ = true;
        }
        return;
    }

    if (reading_) {
        if (read_bit_ < 8) {
            if (read_bit_ == 0) read_byte_ = cells_[address_ & 0x7f];
            data_out_ = (read_byte_ & (0x80 >> read_bit_)) != 0;
            ++read_bit_;
        } else {
            // MAME's ER5911 device defaults to streaming disabled. After one
            // byte, DO returns to its pulled-up state until CS is cycled and
            // another command/address is sent.
            reading_ = false;
            data_out_ = true;
        }
    } else if ((writing_ || writing_all_) && write_bits_ < 8) {
        write_byte_ = static_cast<std::uint8_t>((write_byte_ << 1) | (data_in_ ? 1 : 0));
        if (++write_bits_ == 8) {
            if (!locked_) {
                if (writing_all_) {
                    for (auto& cell : cells_) cell &= write_byte_;
                } else {
                    cells_[address_ & 0x7f] = write_byte_;
                }
            }
            writing_ = false;
            writing_all_ = false;
            waiting_for_cs_cycle_ = true;
        }
    }
}

} // namespace multiarcaderecomp::devices
