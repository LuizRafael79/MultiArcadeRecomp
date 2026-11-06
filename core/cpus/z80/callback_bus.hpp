#pragma once

#include "z80.h"

#include <functional>
#include <utility>

namespace mamerecomp::z80 {

class CallbackBus final : public Z80Bus {
public:
    using ReadFn = std::function<std::uint8_t(std::uint16_t)>;
    using WriteFn = std::function<void(std::uint16_t, std::uint8_t)>;
    using InputFn = std::function<std::uint8_t(std::uint16_t)>;
    using OutputFn = std::function<void(std::uint16_t, std::uint8_t)>;
    using BoundaryFn = std::function<void(std::uint64_t)>;

    CallbackBus(ReadFn read, WriteFn write, InputFn input = {}, OutputFn output = {})
        : m_read(std::move(read)), m_write(std::move(write)),
          m_input(std::move(input)), m_output(std::move(output)) {}

    std::uint8_t Read(std::uint16_t address) override { return m_read(address); }
    void Write(std::uint16_t address, std::uint8_t value) override { m_write(address, value); }
    std::uint8_t In(std::uint16_t port) override { return m_input ? m_input(port) : 0xff; }
    void Out(std::uint16_t port, std::uint8_t value) override {
        if (m_output) m_output(port, value);
    }
    void InstructionBoundary(std::uint64_t cycles) override {
        if (m_boundary) m_boundary(cycles);
    }
    void SetInstructionBoundary(BoundaryFn callback) { m_boundary = std::move(callback); }

private:
    ReadFn m_read;
    WriteFn m_write;
    InputFn m_input;
    OutputFn m_output;
    BoundaryFn m_boundary;
};

} // namespace mamerecomp::z80
