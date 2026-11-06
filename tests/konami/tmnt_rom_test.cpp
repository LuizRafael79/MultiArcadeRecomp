#include "tmnt_machine.hpp"

#include <cassert>

// The shared generic Z80 core references NeoGeo's cycle counter even when
// its native block runner is not used by this board-level test.
extern "C" { int64_t ng_cycles = 0; }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto roms = mamerecomp::konami::load_tmnt_romset(argv[1]);
    assert(roms.at("maincpu").size() == 0x60000);
    assert(roms.at("audiocpu").size() == 0x10000);
    assert(roms.at("k052109").size() == 0x100000);
    assert(roms.at("k051960").size() == 0x200000);
    assert(roms.at("proms").size() == 0x200);
    assert(roms.at("k007232").size() == 0x20000);
    assert(roms.at("upd").size() == 0x20000);
    assert(roms.at("title").size() == 0x80000);
    assert(roms.at("maincpu")[0] != 0xff || roms.at("maincpu")[1] != 0xff);

    mamerecomp::konami::TmntBoard board(roms);
    assert(board.main_read(0x060000) == 0x0000);
    board.main_write(0x060000, 0x1234);
    assert(board.main_read(0x060000) == 0x1234);
    board.main_write(0x0a0000, 0x0028, 0x00ff);
    board.main_write(0x0a0000, 0x0020, 0x00ff);
    assert(board.sound_irq_pending());
    board.video_vblank();
    assert(board.irq5_pending());

    mamerecomp::konami::TmntMainCpu cpu(board);
    const uint32_t reset_pc = (uint32_t(board.main_read(4)) << 16) | board.main_read(6);
    assert(cpu.pc() == reset_pc);
    assert(reset_pc < 0x60000);
    cpu.execute(10000);
    assert(cpu.pc() != reset_pc);
    const auto frame = board.render_frame();
    assert(frame.width == 320 && frame.height == 224);
    assert(frame.palette_indices.size() == 320u * 224u);
    for (const auto index : frame.palette_indices) assert(index < 1024);
    assert(board.frame_rgb(frame).size() == 320u * 224u * 3u);
    board.main_write(0x0a0008, 0x005a, 0x00ff);
    assert(board.sound_read(0xa000) == 0x5a);
    mamerecomp::konami::TmntSoundCpu sound_cpu(board);
    sound_cpu.run(20'000);
    assert(sound_cpu.cycles() >= 20'000);
    assert(sound_cpu.pc() < 0x8000);
    assert(!board.take_audio_samples().empty());
}
