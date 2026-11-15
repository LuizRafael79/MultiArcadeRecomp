#pragma once

#include <cstdint>

namespace multiarcaderecomp::cpus::sh4::flycast {

struct CycleState {
    // Flycast's execution-unit pipeline and memory-operation window.
    std::uint8_t last_unit = 5; // CO
    std::uint8_t memory_operations = 0; // saturates at 4, like the timing rule needs
};

// Returns Naomi SH-4 core cycles (Flycast's 100 MHz external cycle model is
// scaled by CPU_RATIO=2). Address-dependent external bus wait states are not
// included yet; they belong in the SH-4 memory bus contract.
std::uint32_t instruction_cycles(std::uint16_t opcode, CycleState& state);

} // namespace multiarcaderecomp::cpus::sh4::flycast
