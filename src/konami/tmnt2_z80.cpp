#include "tmnt2_machine.hpp"

#include "z80.h"
#include "native_z80.h"

namespace multiarcaderecomp::konami {
namespace {
class SoundBus final : public multiarcaderecomp::z80::Z80Bus {
public:
    explicit SoundBus(Tmnt2Board& board) : board_(board) {}
    void bind(multiarcaderecomp::z80::Z80& cpu) { cpu_ = &cpu; }
    uint8_t Read(uint16_t address) override {
        if (cpu_) board_.advance_sound_to_cycles(cpu_->BusCallbackCycles());
        return board_.sound_read(address);
    }
    void Write(uint16_t address, uint8_t value) override {
        if (cpu_) board_.advance_sound_to_cycles(cpu_->BusCallbackCycles());
        board_.sound_write(address, value);
    }
    void InstructionBoundary(uint64_t cycles) override {
        board_.advance_sound_to_cycles(cycles);
        if (cpu_ && cpu_->nmiServiced != nmi_serviced_) {
            nmi_serviced_ = cpu_->nmiServiced;
            board_.acknowledge_sound_nmi();
        }
        if (cpu_ && board_.sound_nmi_pending() && !cpu_->NmiPending())
            cpu_->RaiseNmi();
    }
private:
    Tmnt2Board& board_;
    multiarcaderecomp::z80::Z80* cpu_ = nullptr;
    uint64_t nmi_serviced_ = 0;
};
}

Tmnt2SoundCpu::Tmnt2SoundCpu(Tmnt2Board& board)
    : board_(board), bus_(std::make_unique<SoundBus>(board)),
      cpu_(std::make_unique<multiarcaderecomp::z80::Z80>(*bus_))
{
    static_cast<SoundBus*>(bus_.get())->bind(*cpu_);
    reset();
}

Tmnt2SoundCpu::~Tmnt2SoundCpu() = default;

void Tmnt2SoundCpu::reset()
{
    cpu_->Reset();
}

void Tmnt2SoundCpu::run(uint64_t cycles)
{
    const bool irq_pending = board_.sound_irq_pending();
    cpu_->SetInterruptLine(irq_pending);
    if (board_.sound_nmi_pending()) cpu_->RaiseNmi();
    const auto mode = multiarcaderecomp::z80::HasZ80Code()
        ? multiarcaderecomp::z80::Z80::Mode::Native
        : multiarcaderecomp::z80::Z80::Mode::Interpreter;
    cpu_->Run(cpu_->cycles + cycles, mode);
    board_.advance_sound_to_cycles(cpu_->cycles);
    if (irq_pending && !cpu_->InterruptLineAsserted())
        board_.acknowledge_sound_irq();
}

void Tmnt2SoundCpu::run_until(uint64_t cycle_timestamp)
{
    if (cycle_timestamp > cpu_->cycles)
        run(cycle_timestamp - cpu_->cycles);
}

uint16_t Tmnt2SoundCpu::pc() const { return cpu_->pc; }
uint16_t Tmnt2SoundCpu::sp() const { return cpu_->sp; }
uint64_t Tmnt2SoundCpu::cycles() const { return cpu_->cycles; }
uint64_t Tmnt2SoundCpu::nmi_count() const { return cpu_->nmiServiced; }
uint64_t Tmnt2SoundCpu::aot_instructions() const { return cpu_->nativeInstructions; }
uint64_t Tmnt2SoundCpu::fallback_instructions() const { return cpu_->interpreted; }

} // namespace multiarcaderecomp::konami
