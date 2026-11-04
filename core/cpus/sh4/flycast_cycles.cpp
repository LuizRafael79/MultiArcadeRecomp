#include "flycast_cycles.hpp"

#include <array>

namespace mamerecomp::cpus::sh4::flycast {
namespace {
enum class Unit : std::uint8_t { MT, EX, BR, LS, FE, CO };
struct Pattern {
    std::uint16_t mask;
    std::uint16_t value;
    std::uint8_t issue_cycles;
    Unit unit;
    std::uint8_t execution_type;
};
constexpr Pattern patterns[] = {
#include "flycast_cycle_patterns.inc"
};
struct Description {
    std::uint8_t issue_cycles = 1;
    Unit unit = Unit::CO;
    std::uint8_t execution_type = 1;
};

const std::array<Description, 0x10000>& descriptions() {
    static const auto table = [] {
        std::array<Description, 0x10000> result{};
        for (const auto& pattern : patterns) {
            for (std::uint32_t opcode = 0; opcode < result.size(); ++opcode) {
                if ((opcode & pattern.mask) == pattern.value) {
                    result[opcode] = {pattern.issue_cycles, pattern.unit,
                                      pattern.execution_type};
                }
            }
        }
        return result;
    }();
    return table;
}

bool is_memory_operation(std::uint8_t type) {
    // Flycast sh4_cycles.cpp's isMemOp[45], indexed by OpDesc::ex_type.
    switch (type) {
    case 2: case 3: case 5: case 6: case 7: case 12:
    case 17: case 18: case 19: case 22: case 23: case 25:
    case 27: case 29: case 31: case 33: case 35:
        return true;
    default:
        return false;
    }
}
} // namespace

std::uint32_t instruction_cycles(std::uint16_t opcode, CycleState& state) {
    const auto& description = descriptions()[opcode];
    std::uint32_t cycles = 0;

    // Naomi has its SH-4 MMU disabled during this BIOS phase. Flycast charges
    // two external cycles for the first three eligible memory operations.
    if (is_memory_operation(description.execution_type) &&
        state.memory_operations < 4) {
        ++state.memory_operations;
        if (state.memory_operations < 4) cycles += 2;
    }

    const auto unit = static_cast<std::uint8_t>(description.unit);
    if (state.last_unit == static_cast<std::uint8_t>(Unit::CO) ||
        unit == static_cast<std::uint8_t>(Unit::CO) ||
        (state.last_unit == unit && state.last_unit != static_cast<std::uint8_t>(Unit::MT))) {
        state.last_unit = unit;
        cycles += description.issue_cycles;
    } else {
        state.last_unit = static_cast<std::uint8_t>(Unit::CO);
    }
    return cycles * 2u; // Flycast CPU_RATIO for Naomi's 200 MHz SH-4.
}

} // namespace mamerecomp::cpus::sh4::flycast
