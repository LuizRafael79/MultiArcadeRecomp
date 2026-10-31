#pragma once

#include <cstdint>
#include <functional>
#include <array>

namespace mamerecomp::naomi::arm7 {

using Read = std::function<std::uint32_t(std::uint32_t address, unsigned width)>;
using Write = std::function<void(std::uint32_t address, unsigned width,
                                 std::uint32_t value, std::uint32_t writer_pc)>;

// Naomi's ARM7/AICA core is a single guest CPU. The runtime binds the shared
// AICA address space once per probe and advances this interpreter from the
// SH-4 cycle sink.
void bind_memory(Read read, Write write);
void set_enabled(bool enabled);
void set_fiq_line(bool asserted);
void run_cycles(std::uint32_t cycles);
void unbind_memory();

std::uint64_t instructions_executed();
std::uint32_t program_counter();
std::uint32_t instruction_address();
std::array<std::uint32_t, 16> register_snapshot();
std::uint64_t undefined_instructions();

} // namespace mamerecomp::naomi::arm7
