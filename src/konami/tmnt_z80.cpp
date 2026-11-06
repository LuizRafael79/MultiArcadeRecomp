#include "tmnt_machine.hpp"

#include "z80.h"
#include "native_z80.h"

namespace mamerecomp::konami {
namespace {
class TmntSoundBus final : public mamerecomp::z80::Z80Bus {
public:
    explicit TmntSoundBus(TmntBoard& board) : board_(board) {}
    void bind(mamerecomp::z80::Z80& cpu) { cpu_ = &cpu; }
    uint8_t Read(uint16_t address) override
    {
        if (cpu_) board_.advance_sound_to_cycles(cpu_->BusCallbackCycles());
        return board_.sound_read(address);
    }
    void Write(uint16_t address, uint8_t value) override
    {
        if (cpu_) board_.advance_sound_to_cycles(cpu_->BusCallbackCycles());
        board_.sound_write(address, value);
    }
    void InstructionBoundary(uint64_t cycles) override
    { board_.advance_sound_to_cycles(cycles); }
private:
    TmntBoard& board_;
    mamerecomp::z80::Z80* cpu_ = nullptr;
};
}

TmntSoundCpu::TmntSoundCpu(TmntBoard& board)
    : board_(board), bus_(std::make_unique<TmntSoundBus>(board)),
      cpu_(std::make_unique<mamerecomp::z80::Z80>(*bus_))
{
    static_cast<TmntSoundBus*>(bus_.get())->bind(*cpu_);
    reset();
    board_.set_sound_sync_callback([this](uint64_t timestamp) { run_until(timestamp); });
}

TmntSoundCpu::~TmntSoundCpu() { board_.set_sound_sync_callback({}); }

void TmntSoundCpu::reset() { cpu_->Reset(); }

void TmntSoundCpu::run(uint64_t cycles)
{
    const bool irq_pending = board_.sound_irq_pending();
    cpu_->SetInterruptLine(irq_pending);
    const auto mode = mamerecomp::z80::HasZ80Code()
        ? mamerecomp::z80::Z80::Mode::Native
        : mamerecomp::z80::Z80::Mode::Interpreter;
    cpu_->Run(cpu_->cycles + cycles, mode);
    board_.advance_sound_to_cycles(cpu_->cycles);
    // TakeInterrupt() clears the held line when the Z80 accepts it. Looking
    // only at IFF1 misses interrupts accepted after EI inside this run slice.
    if (irq_pending && !cpu_->InterruptLineAsserted())
        board_.acknowledge_sound_irq();
}

void TmntSoundCpu::run_until(uint64_t cycle_timestamp)
{
    if (cycle_timestamp > cpu_->cycles)
        run(cycle_timestamp - cpu_->cycles);
}

uint16_t TmntSoundCpu::pc() const { return cpu_->pc; }
uint16_t TmntSoundCpu::sp() const { return cpu_->sp; }
uint64_t TmntSoundCpu::cycles() const { return cpu_->cycles; }
uint64_t TmntSoundCpu::aot_instructions() const { return cpu_->nativeInstructions; }
uint64_t TmntSoundCpu::fallback_instructions() const { return cpu_->interpreted; }

} // namespace mamerecomp::konami
