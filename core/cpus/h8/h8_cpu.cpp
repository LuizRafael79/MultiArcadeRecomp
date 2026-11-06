#include "h8_cpu.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace mamerecomp::h8 {

H8Core::H8Core(Read8 read, Write8 write)
    : read8_(std::move(read)), write8_(std::move(write))
{
    if (!read8_ || !write8_)
        throw std::invalid_argument("H8 core requires byte read/write callbacks");
}

u32 H8Core::reg32(unsigned n) const
{
    n &= 7;
    return u32(m_R[n]) | (u32(m_R[n | 8]) << 16);
}

unsigned H8Core::execute_one()
{
    return run_cycles(1);
}

unsigned H8Core::run_cycles(unsigned cycle_budget)
{
    if (!cycle_budget) return 0;
    m_icount = static_cast<int>(cycle_budget);
    m_bcount = 0;

    // Match h8_device::execute_run: carry instruction overshoot between
    // scheduler slices, resume partial bus sequences, and keep dispatching
    // instructions until this slice is consumed.
    m_icount -= m_count_before_instruction_step;
    if (m_icount < 0) {
        m_count_before_instruction_step = -m_icount;
        m_icount = 0;
    } else {
        m_count_before_instruction_step = 0;
    }

    if (m_icount > 0 && m_inst_substate)
        do_exec_partial();
    while (m_icount > 0) {
        while (m_icount > m_bcount) {
            if (m_inst_state < 0x10000)
                m_PPC = m_NPC;
            do_exec_full();
        }
        if (m_icount > 0 && m_inst_substate)
            do_exec_partial();
    }
    if (m_icount < 0) {
        m_count_before_instruction_step = -m_icount;
        m_icount = 0;
    }

    cycles_ += cycle_budget;
    return cycle_budget;
}

u16 H8Core::read16i(u32 address)
{
    m_icount -= m_has_exr ? 1 : 2;
    address &= m_mode_advanced ? 0x00ffffffu : 0x0000ffffu;
    return static_cast<u16>((u16(read8_(address)) << 8) | read8_(address + 1));
}

u8 H8Core::read8(u32 address)
{
    m_icount -= m_has_exr ? 1 : 2;
    address &= m_mode_advanced ? 0x00ffffffu : 0x0000ffffu;
    return read8_(address);
}

void H8Core::write8(u32 address, u8 value)
{
    m_icount -= m_has_exr ? 1 : 2;
    address &= m_mode_advanced ? 0x00ffffffu : 0x0000ffffu;
    write8_(address, value);
}

u16 H8Core::read16(u32 address)
{
    m_icount -= m_has_exr ? 1 : 2;
    address &= m_mode_advanced ? 0x00ffffffu : 0x0000ffffu;
    return static_cast<u16>((u16(read8_(address)) << 8) | read8_(address + 1));
}

void H8Core::write16(u32 address, u16 value)
{
    m_icount -= m_has_exr ? 1 : 2;
    address &= m_mode_advanced ? 0x00ffffffu : 0x0000ffffu;
    write8_(address, static_cast<u8>(value >> 8));
    write8_(address + 1, static_cast<u8>(value));
}

void H8Core::internal(int states)
{
    m_icount -= states + (m_has_exr ? 0 : 1);
}

void H8Core::prefetch_done()
{
    if (m_requested_state != -1) {
        m_inst_state = m_requested_state;
        m_requested_state = -1;
    } else if (m_current_dma != -1) {
        m_inst_state = STATE_DMA;
    } else if (m_irq_vector) {
        m_taken_irq_vector = m_irq_vector;
        m_taken_irq_level = m_irq_level;
        m_inst_state = STATE_IRQ;
    } else {
        m_inst_state = m_IR[0] = m_PIR;
    }
    m_inst_substate = 0;
}

void H8Core::prefetch_done_noirq()
{
    m_inst_state = m_IR[0] = m_PIR;
    m_inst_substate = 0;
}

void H8Core::prefetch_done_notrace()
{
    prefetch_done();
}

int H8Core::trapa_setup()
{
    return 8;
}

void H8Core::irq_setup()
{
    m_CCR |= F_I;
    m_irq_vector = 0;
}

void H8Core::illegal()
{
    std::ostringstream message;
    message << "illegal H8 opcode 0x" << std::hex << (m_inst_state & 0xffff)
            << " at PC=0x" << m_PPC << " (NPC=0x" << m_NPC
            << ", PC=0x" << m_PC << ", IR0=0x" << m_IR[0]
            << ", PIR=0x" << m_PIR << ')';
    throw std::runtime_error(message.str());
}

#include "../../third_party/mame-h8/h8_helpers_generated.inc"
#include "../../third_party/mame-h8/h8_base_generated.inc"
#include "../../third_party/mame-h8/h8_advanced_generated.inc"

} // namespace mamerecomp::h8
