#include "upd7759.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    std::vector<std::uint8_t> rom(0x8000, 0);
    rom[0] = 0;      // sample 0 is present
    rom[5] = 1;      // sample table address high byte (offset 0x200)
    rom[6] = 0;
    rom[0x201] = 0x40; // 256-nibble block, fastest rate
    rom[0x202] = 0x77;

    mamerecomp::audio::Upd7759 standalone(mamerecomp::audio::Upd7759::Mode::Rom);
    standalone.set_rom(rom);
    standalone.write_port(0);
    standalone.start();
    standalone.advance(2'000);
    assert(!standalone.idle());
    assert(standalone.pcm_sample() != 0);
    standalone.reset();
    assert(standalone.idle());
    assert(standalone.pcm_sample() == 0);

    mamerecomp::audio::Upd7759 fifo(mamerecomp::audio::Upd7759::Mode::Fifo);
    unsigned requests = 0;
    fifo.set_request_callback([&] { ++requests; });
    fifo.start();
    fifo.advance(160);
    assert(requests > 0);
    fifo.write_port(0x10);
    fifo.advance(160);
    assert(requests > 1);
}
