#include "tmnt2_machine.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    multiarcaderecomp::konami::Tmnt2RomSet roms;
    roms.regions.emplace("maincpu", std::vector<uint8_t>(0x100000, 0xff));
    roms.regions.emplace("audiocpu", std::vector<uint8_t>(0x10000, 0xff));
    roms.regions.emplace("k052109", std::vector<uint8_t>(0x100000, 0xff));
    roms.regions.emplace("k053245", std::vector<uint8_t>(0x400000, 0xff));
    roms.regions.emplace("k053260", std::vector<uint8_t>(0x200000, 0xff));
    roms.regions.emplace("eeprom", std::vector<uint8_t>(0x80, 0xff));
    auto& program = roms.regions.at("maincpu");
    auto put32 = [&](size_t at, uint32_t value) {
        program[at] = static_cast<uint8_t>(value >> 24);
        program[at + 1] = static_cast<uint8_t>(value >> 16);
        program[at + 2] = static_cast<uint8_t>(value >> 8);
        program[at + 3] = static_cast<uint8_t>(value);
    };
    auto put16 = [&](size_t at, uint16_t value) {
        program[at] = static_cast<uint8_t>(value >> 8);
        program[at + 1] = static_cast<uint8_t>(value);
    };
    put32(0, 0x00107ffcu); // initial SP in mapped work RAM
    put32(4, 8);            // reset PC
    put16(8, 0x7042);       // MOVEQ #$42,D0
    put16(10, 0x60fe);      // BRA.S back to itself
    auto& sound_program = roms.regions.at("audiocpu");
    sound_program[0] = 0x3e; sound_program[1] = 0x42; // LD A,$42
    sound_program[2] = 0x32; sound_program[3] = 0x00; sound_program[4] = 0xf0; // LD ($f000),A
    sound_program[5] = 0x76; // HALT
    sound_program[0x66] = 0xed; sound_program[0x67] = 0x45; // RETN

    multiarcaderecomp::konami::Tmnt2Board board("tmnt2", std::move(roms));
    multiarcaderecomp::konami::Tmnt2MainCpu cpu(board, nullptr, 0);
    assert(cpu.pc() == 8);
    cpu.execute_until(200); // Musashi accounts for reset cycles on its first execute call.
    cpu.execute_until(220);
    assert(cpu.data_register(0) == 0x42);
    assert(cpu.pc() >= 8 && cpu.pc() <= 12);

    multiarcaderecomp::konami::Tmnt2SoundCpu sound(board);
    sound.run(64);
    assert(board.sound_read(0xf000) == 0x42);
    assert(sound.pc() == 6);
    sound.run(512);
    assert(!board.take_audio_samples().empty());
    sound.run(10'000);
    assert(sound.pc() != 0x0066); // K053260 pulses must not retrigger every instruction.
}
