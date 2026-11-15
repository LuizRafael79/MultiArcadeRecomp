#pragma once

#include "boards/sega/common/sega_address_space.hpp"
#include "cpus/m68000/fd1094/fd1094.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace multiarcaderecomp::sega {

// Connects the FD1094 opcode/state behavior to the shared Musashi fallback CPU.
class Fd1094MusashiBinding {
public:
    Fd1094MusashiBinding(multiarcaderecomp::Fd1094& cipher, multiarcaderecomp::SegaAddressSpace& bus,
        std::span<const std::uint8_t> encrypted_program, std::function<void()> invalidate_aot);
    ~Fd1094MusashiBinding();
    Fd1094MusashiBinding(const Fd1094MusashiBinding&) = delete;
    Fd1094MusashiBinding& operator=(const Fd1094MusashiBinding&) = delete;

private:
    struct Context {
        multiarcaderecomp::Fd1094* cipher;
        multiarcaderecomp::SegaAddressSpace* bus;
        std::span<const std::uint8_t> encrypted_program;
        std::function<void()> invalidate_aot;
    };

    static std::uint16_t opcode_read(void* opaque, std::uint32_t address);
    static void cmpild(void* opaque, std::uint32_t value, unsigned reg);
    static void rte(void* opaque);
    static int irq_ack(void* opaque, int level);

    Context m_context;
};

} // namespace multiarcaderecomp::sega
