#include "mcs48.hpp"

#include <algorithm>
#include <stdexcept>

namespace mamerecomp {

Mcs48::Mcs48(std::span<const std::uint8_t> program, Bus bus) : m_bus(std::move(bus)) {
    if (program.size() != m_rom.size())
        throw std::invalid_argument("MCS-48 internal program ROM must be exactly 0x400 bytes");
    std::copy(program.begin(), program.end(), m_rom.begin());
    reset();
}

void Mcs48::reset() {
    // Reset changes the device state, not the shared emulation timeline. The
    // board clocks this MCU continuously, including while /RESET is asserted.
    m_ram.fill(0);
    m_pc = 0;
    m_a11 = 0;
    m_a = 0;
    m_psw = 0;
    m_f1 = false;
    m_p1 = 0xff;
    m_p2 = 0xff;
    m_timer = 0;
    m_prescaler = 0;
    m_timer_enabled = false;
    m_timer_flag = false;
    m_timer_irq_enabled = false;
    m_timer_irq_pending = false;
    m_irq_enabled = false;
    m_irq_in_progress = false;
    if (m_bus.write_port) {
        m_bus.write_port(1, m_p1);
        m_bus.write_port(2, m_p2);
    }
}

void Mcs48::set_reset(bool asserted) {
    if (asserted && !m_reset_asserted) reset();
    m_reset_asserted = asserted;
}

void Mcs48::set_irq(bool asserted) { m_irq_asserted = asserted; }

std::uint8_t Mcs48::fetch() {
    const auto value = m_rom[m_pc & 0x3ff];
    m_pc = static_cast<std::uint16_t>((m_pc & 0x800U) | ((m_pc + 1U) & 0x7ffU));
    return value;
}

std::uint8_t Mcs48::fetch_arg() { return fetch(); }

std::uint8_t& Mcs48::reg(unsigned index) {
    const unsigned base = (m_psw & RB) ? 24U : 0U;
    return m_ram[base + (index & 7U)];
}

std::uint8_t Mcs48::indirect(unsigned index) const {
    const unsigned base = (m_psw & RB) ? 24U : 0U;
    return m_ram[m_ram[base + (index & 1U)] & 0x3fU];
}

void Mcs48::set_indirect(unsigned index, std::uint8_t value) {
    const unsigned base = (m_psw & RB) ? 24U : 0U;
    m_ram[m_ram[base + (index & 1U)] & 0x3fU] = value;
}

void Mcs48::add(std::uint8_t value, bool with_carry) {
    const unsigned carry = with_carry && (m_psw & C) ? 1U : 0U;
    const unsigned result = unsigned(m_a) + value + carry;
    const unsigned low = (m_a & 0x0fU) + (value & 0x0fU) + carry;
    m_psw = static_cast<std::uint8_t>((m_psw & ~(C | AC)) | ((result > 0xffU) ? C : 0) | ((low > 0x0fU) ? AC : 0));
    m_a = static_cast<std::uint8_t>(result);
}

void Mcs48::branch(bool take) {
    const auto offset = fetch_arg();
    if (take) m_pc = static_cast<std::uint16_t>((m_pc & 0xf00U) | offset);
}

void Mcs48::jump(std::uint16_t address) {
    m_pc = static_cast<std::uint16_t>((address & 0x7ffU) | (m_irq_in_progress ? 0 : m_a11));
}

void Mcs48::call(std::uint16_t address) {
    const auto sp = m_psw & 7U;
    m_ram[8U + 2U * sp] = static_cast<std::uint8_t>(m_pc);
    m_ram[9U + 2U * sp] = static_cast<std::uint8_t>(((m_pc >> 8) & 0x0fU) | (m_psw & 0xf0U));
    m_psw = static_cast<std::uint8_t>((m_psw & 0xf0U) | ((sp + 1U) & 7U));
    jump(address);
}

void Mcs48::ret(bool restore_psw) {
    const auto sp = static_cast<unsigned>((m_psw - 1U) & 7U);
    const auto low = m_ram[8U + 2U * sp];
    const auto high = m_ram[9U + 2U * sp];
    m_pc = static_cast<std::uint16_t>((low | ((high & 0x0fU) << 8)) & (m_irq_in_progress ? 0x7ffU : 0xfffU));
    m_psw = static_cast<std::uint8_t>((restore_psw ? (high & 0xf0U) : (m_psw & 0xf0U)) | sp);
    if (restore_psw) m_irq_in_progress = false;
}

void Mcs48::expander(unsigned operation, unsigned port) {
    m_p2 = static_cast<std::uint8_t>((m_p2 & 0xf0U) | ((operation << 2) & 0x0cU) | (port & 3U));
    if (m_bus.write_port) m_bus.write_port(2, m_p2);
    if (m_bus.prog_line) m_bus.prog_line(false);
    if (operation == 0) {
        m_p2 |= 0x0fU;
        if (m_bus.write_port) m_bus.write_port(2, m_p2);
        m_a = m_bus.read_port ? static_cast<std::uint8_t>(m_bus.read_port(2) & 0x0fU) : 0x0f;
    } else {
        m_p2 = static_cast<std::uint8_t>((m_p2 & 0xf0U) | (m_a & 0x0fU));
        if (m_bus.write_port) m_bus.write_port(2, m_p2);
    }
    if (m_bus.prog_line) m_bus.prog_line(true);
}

void Mcs48::advance_timer(unsigned machine_cycles) {
    if (!m_timer_enabled) return;
    const unsigned accumulated = unsigned(m_prescaler) + machine_cycles;
    const unsigned increments = accumulated >> 5;
    m_prescaler = static_cast<std::uint8_t>(accumulated & 0x1fU);
    if (increments != 0) {
        const auto previous = m_timer;
        m_timer = static_cast<std::uint8_t>(m_timer + increments);
        if (unsigned(previous) + increments > 0xffU) {
            m_timer_flag = true;
            if (m_timer_irq_enabled) m_timer_irq_pending = true;
        }
    }
}

std::uint32_t Mcs48::step() {
    if (m_reset_asserted) {
        ++m_cycles;
        return 1;
    }

    unsigned interrupt_cycles = 0;
    if (!m_irq_in_progress && m_irq_enabled && m_irq_asserted) {
        call(0x003);
        m_irq_in_progress = true;
        interrupt_cycles = 2;
    } else if (!m_irq_in_progress && m_timer_irq_enabled && m_timer_irq_pending) {
        call(0x007);
        m_irq_in_progress = true;
        m_timer_irq_pending = false;
        interrupt_cycles = 2;
    }

    const std::uint8_t opcode = fetch();
    const unsigned n = opcode & 7U;
    unsigned cycles = 1;
    auto p1_write = [this](std::uint8_t value) {
        m_p1 = value;
        if (m_bus.write_port) m_bus.write_port(1, value);
    };
    auto p2_write = [this](std::uint8_t value) {
        m_p2 = value;
        if (m_bus.write_port) m_bus.write_port(2, value);
    };
    auto bus_read = [this]() { return m_bus.read_bus ? m_bus.read_bus() : std::uint8_t(0xff); };
    auto bus_write = [this](std::uint8_t value) { if (m_bus.write_bus) m_bus.write_bus(value); };

    if ((opcode & 0x1fU) == 0x04U) { // JMP page
        jump(static_cast<std::uint16_t>(((opcode & 0xe0U) << 3) | fetch_arg())); cycles = 2;
    } else if ((opcode & 0x1fU) == 0x14U) { // CALL page
        call(static_cast<std::uint16_t>(((opcode & 0xe0U) << 3) | fetch_arg())); cycles = 2;
    } else if (opcode >= 0x18U && opcode <= 0x1fU) {
        ++reg(n);
    } else if (opcode >= 0x28U && opcode <= 0x2fU) {
        std::swap(m_a, reg(n));
    } else if (opcode >= 0x48U && opcode <= 0x4fU) {
        m_a |= reg(n);
    } else if (opcode >= 0x58U && opcode <= 0x5fU) {
        m_a &= reg(n);
    } else if (opcode >= 0x68U && opcode <= 0x6fU) {
        add(reg(n), false);
    } else if (opcode >= 0x78U && opcode <= 0x7fU) {
        add(reg(n), true);
    } else if (opcode >= 0xa8U && opcode <= 0xafU) {
        reg(n) = m_a;
    } else if (opcode >= 0xb8U && opcode <= 0xbfU) {
        reg(n) = fetch_arg(); cycles = 2;
    } else if (opcode >= 0xc8U && opcode <= 0xcfU) {
        --reg(n);
    } else if (opcode >= 0xd8U && opcode <= 0xdfU) {
        m_a ^= reg(n);
    } else if (opcode >= 0xe8U && opcode <= 0xefU) {
        --reg(n); branch(reg(n) != 0); cycles = 2;
    } else if (opcode >= 0xf8U) {
        m_a = reg(n);
    } else {
        switch (opcode) {
        case 0x00: break; // NOP
        case 0x02: bus_write(m_a); cycles = 2; break;
        case 0x03: add(fetch_arg(), false); cycles = 2; break;
        case 0x05: m_irq_enabled = true; break;
        case 0x07: --m_a; break;
        case 0x08: m_a = bus_read(); cycles = 2; break;
        case 0x09: m_a = (m_bus.read_port ? m_bus.read_port(1) : 0xff) & m_p1; cycles = 2; break;
        case 0x0a: m_a = (m_bus.read_port ? m_bus.read_port(2) : 0xff) & m_p2; cycles = 2; break;
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: expander(0, opcode - 0x0cU + 4U); cycles = 2; break;
        case 0x10: case 0x11: set_indirect(opcode & 1U, static_cast<std::uint8_t>(indirect(opcode & 1U) + 1U)); break;
        case 0x12: branch(m_a & 0x01U); cycles = 2; break;
        case 0x13: add(fetch_arg(), true); cycles = 2; break;
        case 0x15: m_irq_enabled = false; break;
        case 0x16: branch(m_timer_flag); m_timer_flag = false; cycles = 2; break;
        case 0x17: ++m_a; break;
        case 0x20: case 0x21: std::swap(m_a, m_ram[m_ram[(m_psw & RB ? 24U : 0U) + (opcode & 1U)] & 0x3fU]); break;
        case 0x23: m_a = fetch_arg(); cycles = 2; break;
        case 0x25: m_timer_irq_enabled = true; break;
        case 0x26: branch(true); cycles = 2; break; // JNT0 (board ties T0 low)
        case 0x27: m_a = 0; break;
        case 0x30: case 0x31: {
            const auto old = indirect(opcode & 1U);
            set_indirect(opcode & 1U, static_cast<std::uint8_t>((old & 0xf0U) | (m_a & 0x0fU)));
            m_a = static_cast<std::uint8_t>((m_a & 0xf0U) | (old & 0x0fU)); break;
        }
        case 0x32: branch(m_a & 0x02U); cycles = 2; break;
        case 0x34: m_timer_irq_enabled = false; m_timer_irq_pending = false; break;
        case 0x35: m_timer_irq_enabled = false; m_timer_irq_pending = false; break; // DIS TCNTI
        case 0x36: branch(false); cycles = 2; break; // JT0
        case 0x37: m_a ^= 0xffU; break;
        case 0x39: p1_write(m_a); cycles = 2; break;
        case 0x3a: p2_write(m_a); cycles = 2; break;
        case 0x3c: case 0x3d: case 0x3e: case 0x3f: expander(1, opcode - 0x3cU + 4U); cycles = 2; break;
        case 0x40: m_a |= indirect(0); break;
        case 0x41: m_a |= indirect(1); break;
        case 0x42: m_a = m_timer; break;
        case 0x43: m_a |= fetch_arg(); cycles = 2; break;
        case 0x45: m_timer_enabled = false; break; // STRT CNT; T1 is held low
        case 0x46: branch(true); cycles = 2; break; // JNT1, T1 grounded
        case 0x47: m_a = static_cast<std::uint8_t>((m_a << 4) | (m_a >> 4)); break;
        case 0x50: m_a &= indirect(0); break;
        case 0x51: m_a &= indirect(1); break;
        case 0x53: m_a &= fetch_arg(); cycles = 2; break;
        case 0x55: m_timer_enabled = true; m_prescaler = 0; break;
        case 0x56: branch(false); cycles = 2; break; // JT1, T1 grounded
        case 0x57: { // DA A
            if ((m_a & 0x0fU) > 9 || (m_psw & AC)) { if (m_a > 0xf9U) m_psw |= C; m_a += 6; }
            if ((m_a & 0xf0U) > 0x90U || (m_psw & C)) { m_a += 0x60; m_psw |= C; }
            break;
        }
        case 0x60: add(indirect(0), false); break;
        case 0x61: add(indirect(1), false); break;
        case 0x62: m_timer = m_a; break;
        case 0x65: m_timer_enabled = false; break;
        case 0x66: branch(m_f1); cycles = 2; break;
        case 0x67: { const auto carry = m_a & 1U; m_a = static_cast<std::uint8_t>((m_a >> 1) | (m_a << 7)); (void)carry; break; }
        case 0x70: add(indirect(0), true); break;
        case 0x71: add(indirect(1), true); break;
        case 0x75: break; // ENT0 CLK: unused on uPD7751
        case 0x76: branch(m_f1); cycles = 2; break;
        case 0x77: { const auto carry = (m_psw & C) ? 0x80U : 0U; const auto next = m_a & C; m_a = static_cast<std::uint8_t>((m_a >> 1) | carry); m_psw = static_cast<std::uint8_t>((m_psw & ~C) | (next ? C : 0)); break; }
        case 0x80: case 0x81: m_a = bus_read(); cycles = 2; break; // MOVX reads the external sample bus
        case 0x83: ret(false); cycles = 2; break;
        case 0x85: m_psw &= static_cast<std::uint8_t>(~F0); break;
        case 0x86: branch(!m_irq_asserted); cycles = 2; break; // JNI
        case 0x88: bus_write(static_cast<std::uint8_t>(bus_read() | fetch_arg())); cycles = 2; break;
        case 0x89: p1_write(static_cast<std::uint8_t>(m_p1 | fetch_arg())); cycles = 2; break;
        case 0x8a: p2_write(static_cast<std::uint8_t>(m_p2 | fetch_arg())); cycles = 2; break;
        case 0x8c: case 0x8d: case 0x8e: case 0x8f: expander(2, opcode - 0x8cU + 4U); cycles = 2; break;
        case 0x90: case 0x91: break; // MOVX writes do not drive the ROM bus on this board
        case 0x93: ret(true); cycles = 2; break;
        case 0x95: m_psw ^= F0; break;
        case 0x96: branch(m_a != 0); cycles = 2; break;
        case 0x97: m_psw &= static_cast<std::uint8_t>(~C); break;
        case 0x98: bus_write(static_cast<std::uint8_t>(bus_read() & fetch_arg())); cycles = 2; break;
        case 0x99: p1_write(static_cast<std::uint8_t>(m_p1 & fetch_arg())); cycles = 2; break;
        case 0x9a: p2_write(static_cast<std::uint8_t>(m_p2 & fetch_arg())); cycles = 2; break;
        case 0x9c: case 0x9d: case 0x9e: case 0x9f: expander(3, opcode - 0x9cU + 4U); cycles = 2; break;
        case 0xa0: case 0xa1: set_indirect(opcode & 1U, m_a); break;
        case 0xa3: m_a = m_rom[(m_pc & 0x300U) | m_a]; cycles = 2; break;
        case 0xa5: m_f1 = false; break;
        case 0xa7: m_psw ^= C; break;
        case 0xb0: case 0xb1: set_indirect(opcode & 1U, fetch_arg()); cycles = 2; break;
        case 0xb3: m_pc = static_cast<std::uint16_t>((m_pc & 0xf00U) | m_rom[(m_pc & 0xf00U) | m_a]); cycles = 2; break;
        case 0xb5: m_f1 = !m_f1; break;
        case 0xb6: branch((m_psw & F0) != 0); cycles = 2; break;
        case 0xc5: m_psw &= static_cast<std::uint8_t>(~RB); break;
        case 0xc6: branch(m_a == 0); cycles = 2; break;
        case 0xc7: m_a = static_cast<std::uint8_t>(m_psw | 0x08U); break;
        case 0xd0: m_a ^= indirect(0); break;
        case 0xd1: m_a ^= indirect(1); break;
        case 0xd3: m_a ^= fetch_arg(); cycles = 2; break;
        case 0xd5: m_psw |= RB; break;
        case 0xd7: m_psw = static_cast<std::uint8_t>(m_a & ~0x08U); break;
        case 0xe3: m_a = m_rom[0x300U | m_a]; cycles = 2; break;
        case 0xe5: m_a11 = 0; break;
        case 0xe6: branch((m_psw & C) == 0); cycles = 2; break;
        case 0xe7: m_a = static_cast<std::uint8_t>((m_a << 1) | (m_a >> 7)); break;
        case 0xf0: m_a = indirect(0); break;
        case 0xf1: m_a = indirect(1); break;
        case 0xf5: m_a11 = 0x800; break;
        case 0xf6: branch((m_psw & C) != 0); cycles = 2; break;
        case 0xf7: { const auto carry = (m_psw & C) ? 1U : 0U; const auto next = m_a & 0x80U; m_a = static_cast<std::uint8_t>((m_a << 1) | carry); m_psw = static_cast<std::uint8_t>((m_psw & ~C) | (next ? C : 0)); break; }
        default:
            // Remaining regular forms: arithmetic/logic, conditional branches,
            // register transfers, and DJNZ are decoded above or by opcode class.
            break;
        }
    }

    cycles += interrupt_cycles;
    advance_timer(cycles);
    m_cycles += cycles;
    return cycles;
}

} // namespace mamerecomp
