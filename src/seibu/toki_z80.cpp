#include "toki_machine.hpp"

#include "z80.h"
#include "native_z80.h"

#include <memory>

namespace multiarcaderecomp::seibu {
namespace {
class TokiSoundBus final : public multiarcaderecomp::z80::Z80Bus {
public:
    explicit TokiSoundBus(TokiBoard& board) : board_(board) {}
    void bind(multiarcaderecomp::z80::Z80& cpu) { cpu_ = &cpu; }
    uint8_t Read(uint16_t address) override { return board_.sound_read(address, false); }
    uint8_t ReadOpcode(uint16_t address) override { return board_.sound_read(address, true); }
    uint8_t InterruptAcknowledge() override { return board_.sound_irq_vector(); }
    void Write(uint16_t address, uint8_t value) override { board_.sound_write(address, value); }
    void InstructionBoundary(uint64_t cycles) override {
        board_.advance_sound_to_cycles(cycles);
        if (cpu_) cpu_->SetInterruptLine(board_.sound_irq_pending());
    }
private:
    TokiBoard& board_;
    multiarcaderecomp::z80::Z80* cpu_ = nullptr;
};
}

class TokiSoundCpu::Impl {
public:
    explicit Impl(TokiBoard& board)
        : board(board), bus(std::make_unique<TokiSoundBus>(board)), cpu(std::make_unique<multiarcaderecomp::z80::Z80>(*bus))
    {
        bus->bind(*cpu);
        reset();
    }
    void reset() { cpu->Reset(); }
    void run_until(uint64_t timestamp) {
        if (timestamp <= cpu->cycles) return;
        cpu->SetInterruptLine(board.sound_irq_pending());
        const auto mode = multiarcaderecomp::z80::HasZ80Code()
            ? multiarcaderecomp::z80::Z80::Mode::Native
            : multiarcaderecomp::z80::Z80::Mode::Interpreter;
        cpu->Run(timestamp, mode);
    }
    TokiBoard& board;
    std::unique_ptr<TokiSoundBus> bus;
    std::unique_ptr<multiarcaderecomp::z80::Z80> cpu;
};

TokiSoundCpu::TokiSoundCpu(TokiBoard& board) : impl_(new Impl(board)) {}
TokiSoundCpu::~TokiSoundCpu() { delete impl_; }
void TokiSoundCpu::reset() { impl_->reset(); }
void TokiSoundCpu::run_until(uint64_t timestamp) { impl_->run_until(timestamp); }
uint16_t TokiSoundCpu::pc() const { return impl_->cpu->pc; }
uint64_t TokiSoundCpu::cycles() const { return impl_->cpu->cycles; }
uint64_t TokiSoundCpu::instructions() const { return impl_->cpu->interpreted; }
uint64_t TokiSoundCpu::aot_instructions() const { return impl_->cpu->nativeInstructions; }
uint64_t TokiSoundCpu::fallback_instructions() const { return impl_->cpu->interpreted; }

} // namespace multiarcaderecomp::seibu
