#include "zn1_sound.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace mamerecomp::zn {

Zn1SoundCpu::Zn1SoundCpu(RaizingZn1Board &board, const RomRegions &roms)
    : board_(board), rom_(roms.at("audio_cpu")),
      ymf_(16'934'400, roms.at("ymf_samples")),
      context_(mamerecomp_m68k_context_size())
{
    bus_ = {this, read_cb, write_cb, irq_ack_cb, instruction_cb};
    if (!mamerecomp_m68k_reset(context_.data(), context_.size(), &bus_))
        throw std::runtime_error("could not reset ZN-1 sound MC68000");
}

uint8_t Zn1SoundCpu::read_cb(void *opaque, uint32_t address)
{ return static_cast<Zn1SoundCpu *>(opaque)->read(address); }

void Zn1SoundCpu::write_cb(void *opaque, uint32_t address, uint8_t value)
{ static_cast<Zn1SoundCpu *>(opaque)->write(address, value); }

int Zn1SoundCpu::irq_ack_cb(void *opaque, unsigned level)
{
    auto &self = *static_cast<Zn1SoundCpu *>(opaque);
    if (level == 2 && std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
        static unsigned traced = 0;
        if (traced++ < 128) std::cerr << "ZN 68000 acknowledged IRQ2\n";
    }
    if (level == 2) self.board_.acknowledge_sound_irq();
    return -1; // Musashi's M68K_INT_ACK_AUTOVECTOR.
}

void Zn1SoundCpu::instruction_cb(void *opaque, uint32_t)
{ ++static_cast<Zn1SoundCpu *>(opaque)->instructions_; }

uint8_t Zn1SoundCpu::read(uint32_t address)
{
    address &= 0x00ffffffu;
    if (address < 0x080000 && address < rom_.size()) return rom_[address];
    if (address >= 0x080000 && address <= 0x0fffff) return ram_[address - 0x080000];
    if (address >= 0x100000 && address <= 0x10001f) {
        if ((address & 1) == 0) return 0xff; // MAME's .umask16(0x00ff), low byte lane.
        sync_ymf_to_cpu_cycles(run_base_cycles_ +
            static_cast<unsigned>(mamerecomp_m68k_current_cycles_run()));
        return ymf_.read((address - 0x100001) >> 1);
    }
    if (address == 0x180009) {
        const uint8_t value = board_.sound_latch();
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 128)
                std::cerr << "ZN 68000 read sound latch data=0x" << std::hex
                          << unsigned(value) << std::dec << '\n';
        }
        return value;
    }
    return 0xff;
}

void Zn1SoundCpu::write(uint32_t address, uint8_t value)
{
    address &= 0x00ffffffu;
    if (address >= 0x080000 && address <= 0x0fffff) {
        ram_[address - 0x080000] = value;
    } else if (address >= 0x100000 && address <= 0x10001f) {
        if ((address & 1) == 0) return; // high byte lane is not connected.
        sync_ymf_to_cpu_cycles(run_base_cycles_ +
            static_cast<unsigned>(mamerecomp_m68k_current_cycles_run()));
        const unsigned offset = (address - 0x100001) >> 1;
        ymf_.write(offset, value);
        if (std::getenv("MAMERECOMP_ZN_TRACE_YMF271")) {
            static unsigned traced = 0;
            if (traced++ < 1024)
                std::cerr << "ZN YMF271 write pcpu_offset=0x" << std::hex << offset
                          << " data=0x" << unsigned(value) << std::dec << '\n';
        }
        ++ymf_writes_;
    }
}

void Zn1SoundCpu::sync_ymf_to_cpu_cycles(uint64_t cpu_cycles)
{
    const uint64_t target = cpu_cycles * 16'934'400ull / 12'000'000ull;
    if (target > ymf_master_cycles_) {
        ymf_.advance_master_clocks(target - ymf_master_cycles_);
        ymf_master_cycles_ = target;
    }
}

void Zn1SoundCpu::run(unsigned cycle_budget)
{
    if (board_.sound_irq_pending())
        mamerecomp_m68k_set_irq(context_.data(), context_.size(), &bus_, 2);
    while (cycle_budget) {
        const int batch = static_cast<int>(std::min(cycle_budget, 4096u));
        run_base_cycles_ = cycles_;
        const int consumed = mamerecomp_m68k_run(
            context_.data(), context_.size(), &bus_, batch);
        if (consumed <= 0) break;
        cycles_ += static_cast<unsigned>(consumed);
        sync_ymf_to_cpu_cycles(cycles_);
        if (static_cast<unsigned>(consumed) >= cycle_budget) break;
        cycle_budget -= static_cast<unsigned>(consumed);
    }
}

} // namespace mamerecomp::zn
