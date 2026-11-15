// Copyright (c) 2026 MultiArcadeRecomp contributors.
// The instruction semantics in mcs51_ops.cpp are adapted from MAME's
// BSD-3-Clause MCS-51 implementation; see docs/license/mame-mcs51/LICENSE.md.
#include "mcs51.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace multiarcaderecomp::mcs51 {

Core::Core(std::span<const u8> program) : Core(program, Bus{}) {}

Core::Core(std::span<const u8> program, Bus bus) : m_bus(std::move(bus)) {
    m_rom.fill(0xff);
    std::copy_n(program.begin(), std::min(program.size(), m_rom.size()), m_rom.begin());
    reset();
}

void Core::reset() {
    m_internal_ram.fill(0);
    m_pc = 0;
    m_ppc = 0;
    m_dptr = 0;
    m_acc = m_psw = m_b = m_pcon = m_tcon = m_tmod = m_scon = m_sbuf = m_ie = m_ip = 0;
    m_sp = 7;
    m_tl0 = m_tl1 = m_th0 = m_th1 = 0;
    m_p0 = m_p1 = m_p2 = m_p3 = 0xff;
    m_rwm = m_last_op = m_last_bit = m_irq_delayed = 0;
    m_irq0_line = false;
    m_irq1_line = false;
    m_inst_cycles = 0;
    m_cur_irq_prio = -1;
    m_irq_active = 0;
    m_irq_prio.fill(0);
    m_last_line_state = 0;
    m_t0_cnt = m_t1_cnt = 0;
    m_clock_count = 0;
    update_irq_priority();
    port_write(3, 0xff);
    port_write(2, 0xff);
    port_write(1, 0xff);
    port_write(0, 0xff);
}

u8 Core::read_program(u32 address) const noexcept {
    return address < m_rom.size() ? m_rom[address] : 0xff;
}

u8 Core::read_idata(u8 address) const noexcept {
    return m_internal_ram[address & 0x7f];
}

void Core::write_idata(u8 address, u8 value) noexcept {
    m_internal_ram[address & 0x7f] = value;
}

u8 Core::port_read(unsigned port, u8 latch) {
    const u8 pins = port < m_bus.read_port.size() && m_bus.read_port[port] ? m_bus.read_port[port]() : 0xff;
    u8 value = static_cast<u8>(latch & pins);
    if (port == 3) {
        if (m_last_line_state & 1U) value &= static_cast<u8>(~0x04U);
        if (m_last_line_state & 2U) value &= static_cast<u8>(~0x08U);
        if (m_last_line_state & 4U) value &= static_cast<u8>(~0x10U);
        if (m_last_line_state & 8U) value &= static_cast<u8>(~0x20U);
    }
    return m_rwm ? latch : value;
}

void Core::port_write(unsigned port, u8 value) {
    auto& latch = port == 0 ? m_p0 : port == 1 ? m_p1 : port == 2 ? m_p2 : m_p3;
    latch = value;
    if (port < m_bus.write_port.size() && m_bus.write_port[port]) m_bus.write_port[port](value);
}

u8 Core::read_sfr(u8 address) {
    switch (address) {
    case 0x80: return port_read(0, m_p0);
    case 0x81: return m_sp;
    case 0x82: return static_cast<u8>(m_dptr);
    case 0x83: return static_cast<u8>(m_dptr >> 8);
    case 0x87: return m_pcon;
    case 0x88: return m_tcon;
    case 0x89: return m_tmod;
    case 0x8a: return m_tl0;
    case 0x8b: return m_tl1;
    case 0x8c: return m_th0;
    case 0x8d: return m_th1;
    case 0x90: return port_read(1, m_p1);
    case 0x98: return m_scon;
    case 0x99: return m_sbuf;
    case 0xa0: return port_read(2, m_p2);
    case 0xa8: return m_ie;
    case 0xb0: return port_read(3, m_p3);
    case 0xb8: return m_ip;
    case 0xd0: return m_psw;
    case 0xe0: return m_acc;
    case 0xf0: return m_b;
    default: return 0xff;
    }
}

void Core::write_sfr(u8 address, u8 value) {
    switch (address) {
    case 0x80: port_write(0, value); break;
    case 0x81: m_sp = value; break;
    case 0x82: m_dptr = static_cast<u16>((m_dptr & 0xff00U) | value); break;
    case 0x83: m_dptr = static_cast<u16>((m_dptr & 0x00ffU) | (u16(value) << 8)); break;
    case 0x87: m_pcon = value; break;
    case 0x88:
        m_tcon = value;
        if (!(m_tcon & 0x01U)) set_ie0(m_irq0_line);
        if (!(m_tcon & 0x04U)) set_ie1(m_irq1_line);
        break;
    case 0x89: m_tmod = value; break;
    case 0x8a: m_tl0 = value; break;
    case 0x8b: m_tl1 = value; break;
    case 0x8c: m_th0 = value; break;
    case 0x8d: m_th1 = value; break;
    case 0x90: port_write(1, value); break;
    case 0x98: m_scon = value; break;
    case 0x99: m_sbuf = value; break;
    case 0xa0: port_write(2, value); break;
    case 0xa8: m_ie = value; break;
    case 0xb0: port_write(3, value); break;
    case 0xb8: m_ip = value; update_irq_priority(); break;
    case 0xd0: m_psw = static_cast<u8>((m_psw & 1U) | (value & 0xfeU)); break;
    case 0xe0: acc_w(value); break;
    case 0xf0: m_b = value; break;
    default: break;
    }
}

u8 Core::read_xdata(u32 address) {
    return m_bus.read_external ? m_bus.read_external(static_cast<u16>(address)) : 0xff;
}

void Core::write_xdata(u32 address, u8 value) {
    if (m_bus.write_external) m_bus.write_external(static_cast<u16>(address), value);
}

u8 Core::read_direct(u8 address) {
    return address < 0x80 ? read_idata(address) : read_sfr(address);
}

void Core::write_direct(u8 address, u8 value) {
    if (address < 0x80) write_idata(address, value);
    else write_sfr(address, value);
}

u32 Core::external_ram_iaddr(u32 address, u32 mask) const noexcept {
    return address & mask & m_bus.external_data_mask;
}

void Core::set_cy(bool state) { if (state) m_psw |= 0x80U; else m_psw &= 0x7fU; }
void Core::set_ac(bool state) { if (state) m_psw |= 0x40U; else m_psw &= 0xbfU; }
void Core::set_ov(bool state) { if (state) m_psw |= 0x04U; else m_psw &= 0xfbU; }
void Core::set_tf0(bool state) { if (state) m_tcon |= 0x20U; else m_tcon &= 0xdfU; }
void Core::set_tf1(bool state) { if (state) m_tcon |= 0x80U; else m_tcon &= 0x7fU; }
void Core::set_ie0(bool state) { if (state) m_tcon |= 0x02U; else m_tcon &= 0xfdU; }
void Core::set_ie1(bool state) { if (state) m_tcon |= 0x08U; else m_tcon &= 0xf7U; }
void Core::set_it0(bool state) { if (state) m_tcon |= 0x01U; else m_tcon &= 0xfeU; }
void Core::set_it1(bool state) { if (state) m_tcon |= 0x04U; else m_tcon &= 0xfbU; }

void Core::set_irq0(bool asserted) {
    const bool old = (m_last_line_state & 1U) != 0;
    if (asserted) m_last_line_state |= 1U;
    else m_last_line_state &= ~1U;
    m_irq0_line = asserted;
    if (asserted && ((m_tcon & 0x01U) == 0 || !old)) set_ie0(true);
    if (!asserted && !(m_tcon & 0x01U)) set_ie0(false);
}

void Core::set_irq1(bool asserted) {
    const bool old = (m_last_line_state & 2U) != 0;
    if (asserted) m_last_line_state |= 2U;
    else m_last_line_state &= ~2U;
    m_irq1_line = asserted;
    if (asserted && ((m_tcon & 0x04U) == 0 || !old)) set_ie1(true);
    if (!asserted && !(m_tcon & 0x04U)) set_ie1(false);
}

void Core::update_irq_priority() {
    for (unsigned i = 0; i < m_irq_prio.size(); ++i) m_irq_prio[i] = (m_ip >> i) & 1U;
}

u8 Core::pending_irq_flags(u8 mask) const {
    const u8 flags = static_cast<u8>((m_tcon & 0x0fU) | ((m_scon & 0x03U) << 4));
    return flags & mask;
}

void Core::clear_current_irq() {
    if (m_cur_irq_prio >= 0) m_irq_active &= static_cast<u8>(~(1U << m_cur_irq_prio));
    if (m_irq_active & 0x04U) m_cur_irq_prio = 2;
    else if (m_irq_active & 0x02U) m_cur_irq_prio = 1;
    else if (m_irq_active & 0x01U) m_cur_irq_prio = 0;
    else m_cur_irq_prio = -1;
}

void Core::check_irqs() {
    // IE enable bits and TCON/SCON request flags occupy different bit
    // positions. Convert the request flags into the five 8051 vector slots
    // explicitly instead of masking the two registers against each other.
    const u8 flags = pending_irq_flags(0xffU);
    const u8 delayed = m_irq_delayed;
    m_irq_delayed = 0;
    u8 pending = 0;
    if (m_ie & 0x80U) { // EA
        if ((m_ie & 0x01U) && (flags & 0x02U) && !(delayed & 0x02U)) pending |= 0x01U; // EX0 / IE0
        if ((m_ie & 0x02U) && (flags & 0x20U) && !(delayed & 0x20U)) pending |= 0x02U; // ET0 / TF0
        if ((m_ie & 0x04U) && (flags & 0x08U) && !(delayed & 0x08U)) pending |= 0x04U; // EX1 / IE1
        if ((m_ie & 0x08U) && (flags & 0x80U) && !(delayed & 0x80U)) pending |= 0x08U; // ET1 / TF1
        if ((m_ie & 0x10U) && (flags & 0x30U) && !(delayed & 0x30U)) pending |= 0x10U; // ES / RI|TI
    }
    if (!pending) return;
    int requested_priority = -1;
    unsigned vector_index = 0;
    for (unsigned i = 0; i < 5; ++i) {
        if ((pending & (1U << i)) && m_irq_prio[i] > requested_priority) {
            requested_priority = m_irq_prio[i];
            vector_index = i;
        }
    }
    if (m_irq_active && requested_priority <= m_cur_irq_prio) return;
    (void)read_program(m_pc); // 8051 interrupt polling consumes/discards the next fetch.
    push_pc();
    m_pc = static_cast<u16>((vector_index << 3) | 3U);
    m_inst_cycles += 2;
    m_cur_irq_prio = requested_priority;
    m_irq_active |= static_cast<u8>(1U << requested_priority);
    if (m_bus.interrupt_ack) m_bus.interrupt_ack(vector_index);
    switch (vector_index) {
    case 0: if (m_tcon & 0x01U) set_ie0(false); break;
    case 1: set_tf0(false); break;
    case 2: if (m_tcon & 0x04U) set_ie1(false); break;
    case 3: set_tf1(false); break;
    default: break;
    }
}

void Core::update_timer_t0(u32 cycles) {
    const unsigned mode = ((m_tmod >> 1) & 2U) | (m_tmod & 1U);
    if (!(m_tcon & 0x10U)) return;
    u32 delta = (m_tmod & 0x04U) ? m_t0_cnt : cycles;
    m_t0_cnt = 0;
    if ((m_tmod & 0x08U) && (m_last_line_state & 1U)) delta = 0;
    u32 count = mode == 0 ? ((u32(m_th0) << 5) | (m_tl0 & 0x1fU))
              : mode == 2 || mode == 3 ? m_tl0 : ((u32(m_th0) << 8) | m_tl0);
    count += delta;
    const u32 limit = mode == 0 ? 0x2000U : mode == 2 || mode == 3 ? 0x100U : 0x10000U;
    if (count >= limit) {
        set_tf0(true);
        if (mode == 2) count += m_th0;
    }
    if (mode == 0) { m_th0 = (count >> 5) & 0xffU; m_tl0 = count & 0x1fU; }
    else if (mode == 3) m_tl0 = count & 0xffU;
    else if (mode == 2) m_tl0 = count & 0xffU;
    else { m_th0 = (count >> 8) & 0xffU; m_tl0 = count & 0xffU; }
    if (mode == 3 && (m_tcon & 0x40U)) {
        u32 high = m_th0 + cycles;
        if (high >= 0x100) set_tf1(true);
        m_th0 = high & 0xffU;
    }
}

void Core::update_timer_t1(u32 cycles) {
    const unsigned mode0 = ((m_tmod >> 1) & 2U) | (m_tmod & 1U);
    const unsigned mode1 = ((m_tmod >> 5) & 2U) | ((m_tmod >> 4) & 1U);
    if (mode0 == 3 || !(m_tcon & 0x40U)) return;
    u32 delta = (m_tmod & 0x40U) ? m_t1_cnt : cycles;
    m_t1_cnt = 0;
    if ((m_tmod & 0x80U) && (m_last_line_state & 2U)) delta = 0;
    u32 count = mode1 == 0 ? ((u32(m_th1) << 5) | (m_tl1 & 0x1fU))
              : mode1 == 2 ? m_tl1 : ((u32(m_th1) << 8) | m_tl1);
    count += delta;
    const u32 limit = mode1 == 0 ? 0x2000U : mode1 == 2 ? 0x100U : 0x10000U;
    if (count >= limit) {
        set_tf1(true);
        if (mode1 == 2) count += m_th1;
    }
    if (mode1 == 0) { m_th1 = (count >> 5) & 0xffU; m_tl1 = count & 0x1fU; }
    else if (mode1 == 2) m_tl1 = count & 0xffU;
    else { m_th1 = (count >> 8) & 0xffU; m_tl1 = count & 0xffU; }
}

void Core::burn_cycles(u32 cycles) {
    for (u32 i = 0; i < cycles; ++i) {
        const u8 before = (i + 1 == cycles) ? pending_irq_flags(0xffU) : 0;
        update_timer_t0(1);
        update_timer_t1(1);
        if (i + 1 == cycles) m_irq_delayed |= pending_irq_flags(0xffU) & static_cast<u8>(~before);
    }
    m_clock_count += std::uint64_t(cycles) * 12U;
}

u32 Core::step() {
    m_inst_cycles = 0;
    check_irqs();
    m_ppc = m_pc;
    const u8 opcode = read_program(m_pc++);
    m_inst_cycles += mcs51_cycles[opcode];
    execute_op(opcode);
    const u32 cycles = static_cast<u32>(std::max(m_inst_cycles, 1));
    burn_cycles(cycles);
    return cycles;
}

} // namespace multiarcaderecomp::mcs51
