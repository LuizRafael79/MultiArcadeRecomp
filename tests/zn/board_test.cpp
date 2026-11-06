#include "zn_romset.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

extern "C" { std::int64_t ng_cycles = 0; }

int main()
{
    mamerecomp::zn::RomRegions roms;
    roms.regions.emplace("program", std::vector<uint8_t>(0x1800000, 0xff));
    roms.regions.emplace("security", std::vector<uint8_t>(8, 0xff));
    roms.regions.emplace("security_bios", std::vector<uint8_t>(8, 0xff));
    roms.regions.emplace("eeprom", std::vector<uint8_t>(0x800, 0xff));

    mamerecomp::zn::RaizingZn1Board zn1(roms);
    PsxBus zn1_bus = zn1.make_bus();
    uint32_t value = 0xffffffff;
    assert(zn1_bus.read(zn1_bus.opaque, 0x1fa40000, 4, 0, &value));
    assert(value == 0); // MAME maps this as nopr(), not a bus fault.
    assert(zn1_bus.read(zn1_bus.opaque, 0x1fa30000, 4, 0, &value));
    assert(value == 0); // noprw()
    assert(zn1_bus.write(zn1_bus.opaque, 0x1fa30000, 4, 0x12345678, 0));

    mamerecomp::zn::CapcomZn2Board zn2(roms);
    PsxBus zn2_bus = zn2.make_bus();
    assert(zn2_bus.read(zn2_bus.opaque, 0x1fa60000, 4, 0, &value));
    assert(value == 8); // MAME's 16-bit handler, as observed through a 32-bit lw.
    assert(zn2_bus.read(zn2_bus.opaque, 0x1fa60000, 4, 0, &value));
    assert(value == 0);
    assert(zn2_bus.read(zn2_bus.opaque, 0x1fa60000, 2, 0, &value));
    assert(value == 8);
    assert(zn2_bus.read(zn2_bus.opaque, 0x1fa60000, 1, 0, &value));
    assert(value == 0);
}
