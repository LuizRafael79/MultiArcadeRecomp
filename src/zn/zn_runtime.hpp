#pragma once

#include "zn_romset.hpp"

#include <memory>
#include <optional>

#include "machine_memory.h"
#include "zn2_sound.hpp"
#include "zn1_sound.hpp"
#include "native_z80.h"
#include "h8_cpu.hpp"
#include "../../core/runtime/machine_io.hpp"

namespace mamerecomp::zn {

// Composition layer for a BIOS-driven ZN board. It currently runs interpreter
// fallback only when no generated entry table is supplied; no display/audio
// frontend is implied by this CPU machine wrapper.
class ZnMachineRuntime {
public:
    ZnMachineRuntime(std::string_view set, const std::filesystem::path &zip,
                     const PsxAotEntry *entries = nullptr, size_t entry_count = 0,
                     const mamerecomp::z80::Z80Code *zn2_z80_code = nullptr);
    PsxAotExit step();
    void run_until(uint64_t target_cycle);
    void set_input_state(const mamerecomp::runtime::ArcadeInput &input);
    bool load_nvram(std::span<const uint8_t> image) {
        if (zn2_) return zn2_->load_nvram(image);
        if (zn1_) return zn1_->load_nvram(image);
        return false;
    }
    std::span<const uint8_t> nvram() const {
        if (zn2_) return zn2_->nvram();
        if (zn1_) return zn1_->nvram();
        return {};
    }
    const std::optional<mamerecomp::psx::GpuFrame> &last_vblank_frame() const {
        return memory_->gpu().last_vblank_frame();
    }
    mamerecomp::psx::GpuFrame framebuffer_rgb() const {
        return memory_->gpu().framebuffer_rgb();
    }
    PsxCpuState &cpu() { return cpu_; }
    const PsxCpuState &cpu() const { return cpu_; }
    mamerecomp::psx::MachineMemory &memory() { return *memory_; }
    std::vector<int16_t> take_spu_audio() { return memory_->take_spu_audio(); }
    const mamerecomp::psx::RunCounters &counters() const { return counters_; }
    uint64_t profile_run_until_ns() const { return profile_run_until_ns_; }
    uint64_t profile_zn2_sound_ns() const {
        return zn2_sound_ ? zn2_sound_->profile_runtime_ns() : 0;
    }
    uint64_t zn1_sound_instructions() const {
        return zn1_sound_ ? zn1_sound_->instructions() : 0;
    }
    uint64_t system12_h8_cycles() const {
        return system12_audio_cpu_ ? system12_audio_cpu_->cycles() : 0;
    }
    uint32_t system12_h8_pc() const {
        return system12_audio_cpu_ ? system12_audio_cpu_->pc() : 0;
    }
    uint32_t system12_audio_sample_rate() const {
        return system12_ ? system12_->audio_sample_rate() : 0;
    }
    unsigned system12_active_audio_voices() const {
        return system12_ ? system12_->active_audio_voices() : 0;
    }
    uint64_t system12_c352_writes() const {
        return system12_ ? system12_->c352_writes() : 0;
    }
    std::vector<int16_t> render_system12_audio(size_t frames) {
        return system12_ ? system12_->render_audio(frames) : std::vector<int16_t>{};
    }
    uint64_t zn1_ymf_writes() const {
        return zn1_sound_ ? zn1_sound_->ymf_writes() : 0;
    }
    uint64_t zn1_ymf_samples() const {
        return zn1_sound_ ? zn1_sound_->audio_samples() : 0;
    }
    uint32_t zn1_ymf_peak() const {
        return zn1_sound_ ? zn1_sound_->audio_peak() : 0;
    }
    uint32_t zn1_audio_sample_rate() const {
        return zn1_sound_ ? zn1_sound_->audio_sample_rate() : 0;
    }
    unsigned zn1_active_ymf_slots() const {
        return zn1_sound_ ? zn1_sound_->active_ymf_slots() : 0;
    }
    std::vector<int16_t> take_zn1_audio() {
        return zn1_sound_ ? zn1_sound_->take_audio() : std::vector<int16_t>{};
    }
    uint64_t zn2_qsound_samples() const {
        return zn2_sound_ ? zn2_sound_->audio_samples() : 0;
    }
    uint32_t zn2_qsound_peak() const {
        return zn2_sound_ ? zn2_sound_->audio_peak() : 0;
    }
    uint32_t zn2_qsound_sample_rate() const {
        return zn2_sound_ ? zn2_sound_->audio_sample_rate() : 0;
    }
    std::vector<int16_t> take_zn2_audio() {
        return zn2_sound_ ? zn2_sound_->take_audio() : std::vector<int16_t>{};
    }
    uint64_t zn2_z80_aot_instructions() const {
        return zn2_sound_ ? zn2_sound_->cpu().nativeInstructions : 0;
    }
    uint64_t zn2_z80_fallback_instructions() const {
        return zn2_sound_ ? zn2_sound_->cpu().interpreted : 0;
    }
private:
    static void sync_zn2_sound_command(void *opaque, uint64_t main_cycles);
    RomRegions roms_;
    std::unique_ptr<mamerecomp::psx::MachineMemory> memory_;
    std::unique_ptr<CapcomZn2Board> zn2_;
    std::unique_ptr<RaizingZn1Board> zn1_;
    std::unique_ptr<NamcoSystem12Board> system12_;
    std::unique_ptr<Zn2SoundCpu> zn2_sound_;
    std::unique_ptr<Zn1SoundCpu> zn1_sound_;
    std::unique_ptr<mamerecomp::h8::H8Advanced> system12_audio_cpu_;
    uint64_t system12_h8_main_cycles_ = 0;
    uint64_t zn2_sound_main_cycles_ = 0;
    uint64_t zn1_sound_main_cycles_ = 0;
    uint64_t zn1_sound_cycle_remainder_ = 0;
    PsxBus board_bus_{};
    PsxBus bus_{};
    PsxCpuState cpu_{};
    bool profile_timing_ = false;
    uint64_t profile_run_until_ns_ = 0;
    const PsxAotEntry *entries_ = nullptr;
    size_t entry_count_ = 0;
    mamerecomp::psx::RunCounters counters_{};
    void sync_secondary_processors();
};

} // namespace mamerecomp::zn
