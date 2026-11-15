#include "tmnt2_machine.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>

int main(int argc, char** argv)
{
    assert(argc == 3);
    const auto tmnt2 = multiarcaderecomp::konami::load_tmnt2_romset("tmnt2", argv[1]);
    assert(tmnt2.at("maincpu").size() == 0x100000);
    assert(tmnt2.at("audiocpu").size() == 0x10000);
    assert(tmnt2.at("k052109").size() == 0x100000);
    assert(tmnt2.at("k053245").size() == 0x400000);
    assert(tmnt2.at("k053260").size() == 0x200000);
    assert(tmnt2.at("eeprom").size() == 0x80);
    assert(tmnt2.at("maincpu")[0] != 0xff || tmnt2.at("maincpu")[1] != 0xff);

    const auto ssriders = multiarcaderecomp::konami::load_tmnt2_romset("ssriders", argv[2]);
    assert(ssriders.at("maincpu").size() == 0x0c0000);
    assert(ssriders.at("k053245").size() == 0x200000);
    assert(ssriders.at("k053260").size() == 0x100000);
    assert(ssriders.at("maincpu")[0x80000] != 0xff ||
           ssriders.at("maincpu")[0x80001] != 0xff);
}
