#include "tmnt2_machine.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    mamerecomp::konami::Tmnt2RomSet roms;
    roms.regions.emplace("maincpu", std::vector<uint8_t>(0x100000, 0xff));
    roms.regions.emplace("audiocpu", std::vector<uint8_t>(0x10000, 0xff));
    roms.regions.emplace("k052109", std::vector<uint8_t>(0x100000, 0xff));
    roms.regions.emplace("k053245", std::vector<uint8_t>(0x400000, 0xff));
    roms.regions.emplace("k053260", std::vector<uint8_t>(0x200000, 0xff));
    roms.regions.emplace("eeprom", std::vector<uint8_t>(0x80, 0xff));
    roms.regions["maincpu"][0] = 0x12;
    roms.regions["maincpu"][1] = 0x34;
    roms.regions["audiocpu"][0xe123] = 0x5a;
    roms.regions["k053260"][1] = 0x87;

    mamerecomp::konami::Tmnt2Board board("tmnt2", std::move(roms));
    assert(board.main_read(0) == 0x1234);
    assert(board.main_read(0x100000) == 0xffff);

    board.main_write(0x104000, 0x1234);
    board.main_write(0x104000, 0xab00, 0xff00);
    assert(board.main_read(0x104000) == 0xab34);
    board.main_write(0x140000, 0x4567);
    assert(board.main_read(0x140000) == 0x4567);
    board.main_write(0x600000, 0x89ab);
    assert(board.main_read(0x600000) == 0x8900);
    board.main_write(0x600000, 0x0056, 0x00ff);
    assert(board.main_read(0x600000) == 0x8900);
    assert(board.main_read(0x600000, 0x00ff) == 0x0056);
    board.main_write(0x603a00, 0x0400, 0xff00); // K052109 IRQ4 enable
    board.video_vblank();
    assert(board.main_irq4_pending());
    board.main_write(0x603a00, 0x0000, 0xff00); // falling edge acknowledges
    assert(!board.main_irq4_pending());
    // The K052109 character graphics are decoded as 8x8 4bpp tiles.
    assert(board.tile_pixel(0, 0, 0, 0) == 0x0f);
    assert(board.tile_pixel(3, 0, 0, 0) == 0);
    const auto frame = board.render_tile_layers();
    assert(frame.width == 304);
    assert(frame.palette_indices.size() == 304 * 224);
    assert(frame.priorities.size() == 304 * 224);
    assert(frame.palette_indices[0] == 15);
    assert(frame.priorities[0] == 4);
    for (uint32_t address : {0x5c0704u, 0x5c0706u, 0x5c0708u})
        board.main_write(address, 0x003f, 0x00ff);
    board.main_write(0x180000, 0x8000); // active, 1x1 sprite
    board.main_write(0x180004, 0x0000); // code
    board.main_write(0x180008, 0x03e1); // y
    board.main_write(0x18000c, 0x0015); // x
    board.main_write(0x180010, 0x0040); // normal y zoom
    board.main_write(0x180014, 0x0040); // normal x zoom
    board.main_write(0x180018, 0x0001); // color
    board.main_write(0x5a000c, 0x0100, 0xff00); // latch sprite RAM
    const auto composed = board.render_frame();
    assert(composed.palette_indices[2] == 31);
    assert(board.frame_rgb(composed).size() == 304 * 224 * 3);

    board.set_input(0, 0xfffe);
    assert(board.main_read(0x1c0000) == 0xfffe);
    board.main_write(0x1c0200, 0x0028, 0x00ff);
    assert(board.eeprom_control() == 0x28);
    // The TMNT2 053990 HLE converts a logical object into the K053245 list
    // when the driver commits its 0x8200 command.
    board.main_write(0x104100, 0x0123);
    board.main_write(0x104102, 0x0010);
    board.main_write(0x104104, 0x0011);
    board.main_write(0x104106, 0x0022);
    board.main_write(0x104200, 0x8000);
    board.main_write(0x10421c, 0x4f00);
    board.main_write(0x10421e, 0x4f00);
    board.main_write(0x10422a, 0x0700);
    board.main_write(0x1c0800, 0x4100); // source 0x104100
    board.main_write(0x1c0802, 0x0010);
    board.main_write(0x1c0804, 0x0100); // destination 0x180100
    board.main_write(0x1c0806, 0x0018);
    board.main_write(0x1c0808, 0x4200); // modifiers 0x104200
    board.main_write(0x1c080a, 0x0010);
    board.main_write(0x1c0810, 0x8200);
    board.main_write(0x1c0818, 0x8200, 0xff00);
    assert(board.main_read(0x180100) == 0x8000);
    assert(board.main_read(0x180104) == 0x0123);
    assert(board.main_read(0x180108) == 0x0022);
    assert(board.main_read(0x18010c) == 0x0011);
    assert(board.main_read(0x180118) == 0x0007);
    board.main_write(0x5c0604, 0x0061, 0x00ff);
    assert(board.sound_irq_pending());
    board.main_write(0x5c0704, 0x0034, 0x00ff);
    board.main_write(0x5c0712, 0x0029, 0x00ff);
    board.main_write(0x5c0714, 0x0029, 0x00ff);
    assert(board.priority(2) == 0x34);
    assert(board.palette_base(0) == 32);
    assert(board.palette_base(1) == 64);
    assert(board.palette_base(2) == 64);
    assert(board.palette_base(3) == 16);

    assert(board.sound_read(0xe123) == 0x5a);
    board.sound_write(0xf000, 0x77);
    assert(board.sound_read(0xf000) == 0x77);
    board.main_write(0x5c0600, 0x0033, 0x00ff);
    assert(board.sound_read(0xfa00) == 0x33);
    board.sound_write(0xfa02, 0x42);
    assert(board.main_read(0x5c0600, 0x00ff) == 0xff42);
    board.sound_write(0xfc00, 0);
    assert(board.sound_irq_pending()); // NMI acknowledge is distinct from held IM1.
    board.acknowledge_sound_irq();
    assert(!board.sound_irq_pending());
    board.sound_write(0xfa08, 0x00);
    board.sound_write(0xfa09, 0x08);
    board.sound_write(0xfa0a, 0x04);
    board.sound_write(0xfa0c, 0x00);
    board.sound_write(0xfa0f, 0x7f);
    board.sound_write(0xfa2c, 0x04);
    board.sound_write(0xfa2f, 0x02);
    board.sound_write(0xfa28, 0x01);
    const auto sample = board.generate_k053260_audio(4);
    assert(sample.size() == 8);
    assert(sample[0] < 0 && sample[1] < 0);
    assert(board.sound_read(0xfa29) & 1);
    board.sound_write(0xfa28, 0x00);
    board.sound_write(0xfa2a, 0x10); // KADPCM voice 0
    board.sound_write(0xfa28, 0x01);
    const auto adpcm = board.generate_k053260_audio(2);
    assert(adpcm[0] > 0 && adpcm[1] > 0);
    board.advance_sound_to_cycles(287);
    assert(board.take_audio_samples().size() == 4);
    assert(board.sound_nmi_pending());
    board.sound_write(0xfc00, 0);
    assert(!board.sound_nmi_pending());

    auto ssriders_roms = board.roms();
    mamerecomp::konami::Tmnt2Board ssriders("ssriders", std::move(ssriders_roms));
    assert(ssriders.render_tile_layers().width == 288);
}
