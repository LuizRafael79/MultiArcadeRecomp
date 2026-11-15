#include "runtime/sega/system16b/fd1094_musashi_binding.hpp"

#include "cpus/m68000/runtime/musashi_fallback.h"

#include <utility>

namespace mamerecomp::sega {

Fd1094MusashiBinding::Fd1094MusashiBinding(mamerecomp::Fd1094& cipher,
    mamerecomp::SegaAddressSpace& bus, std::span<const std::uint8_t> encrypted_program,
    std::function<void()> invalidate_aot)
    : m_context{&cipher, &bus, encrypted_program, std::move(invalidate_aot)} {
    mamerecomp_m68k_musashi_set_opcode_reader(opcode_read, &m_context);
    mamerecomp_m68k_musashi_set_extension_callbacks(cmpild, rte, irq_ack, &m_context);
}

Fd1094MusashiBinding::~Fd1094MusashiBinding() {
    mamerecomp_m68k_musashi_set_opcode_reader(nullptr, nullptr);
    mamerecomp_m68k_musashi_set_extension_callbacks(nullptr, nullptr, nullptr, nullptr);
}

std::uint16_t Fd1094MusashiBinding::opcode_read(void* opaque, std::uint32_t address) {
    auto& context = *static_cast<Context*>(opaque);
    const auto raw = context.bus->read16(address);
    if (!raw) return 0xffff;
    if (address < context.encrypted_program.size() && address + 1 < context.encrypted_program.size())
        return context.cipher->decrypt_opcode(address, raw.value, address < 8);
    return raw.value;
}

void Fd1094MusashiBinding::cmpild(void* opaque, std::uint32_t value, unsigned reg) {
    auto& context = *static_cast<Context*>(opaque);
    if (reg == 0 && (value & 0xffffu) == 0xffffu) {
        context.cipher->change_state(static_cast<std::uint16_t>(value >> 16));
        context.invalidate_aot();
    }
}

void Fd1094MusashiBinding::rte(void* opaque) {
    auto& context = *static_cast<Context*>(opaque);
    context.cipher->leave_irq();
    context.invalidate_aot();
}

int Fd1094MusashiBinding::irq_ack(void* opaque, int) {
    auto& context = *static_cast<Context*>(opaque);
    context.cipher->enter_irq();
    context.invalidate_aot();
    return -1; // Musashi's M68K_INT_ACK_AUTOVECTOR.
}

} // namespace mamerecomp::sega
