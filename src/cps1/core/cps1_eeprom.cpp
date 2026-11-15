#include "cps1_eeprom.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

namespace multiarcaderecomp::cps1::detail {
namespace {
enum class State { Reset, Start, Command, Read, WriteData, WriteAllData, Complete };
std::array<std::uint8_t, 128> cells;
State state = State::Reset;
std::string nvram_path;
std::uint16_t command = 0;
unsigned command_bits = 0;
std::uint8_t data_shift = 0;
unsigned data_bits = 0;
unsigned address = 0;
int read_bit = -1;
bool cs = false, clk = false, di = false, data_out = true;
bool write_enabled = false, dirty = false;
bool read_complete = false;

void execute_command() {
    const unsigned opcode = (command >> 7) & 3;
    address = command & 0x7f;
    command_bits = 0;
    switch (opcode) {
    case 2: // READ; MAME drives a dummy zero until the next rising edge.
        state = State::Read;
        read_bit = -1;
        data_out = false;
        break;
    case 1:
        state = State::WriteData;
        data_shift = 0;
        data_bits = 0;
        break;
    case 3:
        if (write_enabled) { cells[address] = 0xff; dirty = true; }
        state = State::Complete;
        data_out = true;
        break;
    default: {
        const unsigned special = address >> 5;
        if (special == 0) write_enabled = false;       // EWDS / lock
        else if (special == 1) {
            state = State::WriteAllData;
            data_shift = 0;
            data_bits = 0;
            return;
        } else if (special == 2) {
            if (write_enabled) { cells.fill(0xff); dirty = true; }
        } else write_enabled = true;                   // EWEN / unlock
        state = State::Reset;
        data_out = true;
        break;
    }
    }
}

void rising_clock() {
    switch (state) {
    case State::Start:
        if (di) { state = State::Command; command = 0; command_bits = 0; }
        break;
    case State::Command:
        command = static_cast<std::uint16_t>((command << 1) | di);
        if (++command_bits == 9) execute_command();
        break;
    case State::Read:
        if (read_complete) { data_out = true; break; }
        if (read_bit < 0) {
            data_shift = cells[address];
            read_bit = 7;
        }
        data_out = ((data_shift >> read_bit) & 1) != 0;
        if (read_bit-- == 0) read_complete = true;
        break;
    case State::WriteData:
    case State::WriteAllData:
        data_shift = static_cast<std::uint8_t>((data_shift << 1) | di);
        if (++data_bits == 8) {
            if (write_enabled) {
                if (state == State::WriteData) cells[address] = data_shift;
                else cells.fill(data_shift);
                dirty = true;
            }
            state = State::Complete;
            data_out = true;
        }
        break;
    default:
        break;
    }
}
} // namespace

void cps1_eeprom_set_path(std::string path) { nvram_path = std::move(path); }

void cps1_eeprom_reset() {
    cells.fill(0xff);
    if (nvram_path.empty()) {
        if (const char* path = std::getenv("MULTIARCADERECOMP_CPS1_EEPROM")) nvram_path = path;
    }
    if (!nvram_path.empty()) {
        std::ifstream in(nvram_path, std::ios::binary);
        if (in) in.read(reinterpret_cast<char*>(cells.data()), cells.size());
    }
    state = State::Reset;
    command = 0;
    command_bits = data_bits = 0;
    address = 0;
    read_bit = -1;
    read_complete = false;
    cs = clk = di = false;
    data_out = true;
    write_enabled = false;
    dirty = false;
}

bool cps1_eeprom_read_do() { return data_out; }

void cps1_eeprom_write_lines(unsigned data) {
    const bool next_di = (data & 0x01) != 0;
    const bool next_clk = (data & 0x40) != 0;
    const bool next_cs = (data & 0x80) != 0;
    di = next_di;
    if (next_cs != cs) {
        cs = next_cs;
        if (cs) {
            state = State::Start;
            command = 0;
            command_bits = 0;
            data_out = true;
        } else {
            state = State::Reset;
            command = 0;
            command_bits = data_bits = 0;
            read_bit = -1;
            read_complete = false;
            data_out = true;
        }
    }
    if (cs && !clk && next_clk) rising_clock();
    clk = next_clk;
}

bool cps1_eeprom_save() {
    if (!dirty || nvram_path.empty()) return true;
    std::ofstream out(nvram_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(cells.data()), cells.size());
    if (out) dirty = false;
    return static_cast<bool>(out);
}

} // namespace multiarcaderecomp::cps1::detail
