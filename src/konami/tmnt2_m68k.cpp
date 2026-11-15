#include "tmnt2_machine.hpp"

#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>
#include "cpus/m68000/runtime/recomp_bus.h"
#include "cpus/m68000/runtime/recomp_dispatch.h"
#include "cpus/m68000/runtime/musashi_fallback.h"
#include "cpus/m68000/runtime/cpu_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using multiarcaderecomp::konami::Tmnt2Board;

bool bus_read8(void* context, uint32_t address, uint8_t* value)
{
    auto& board = *static_cast<Tmnt2Board*>(context);
    const uint16_t mask = (address & 1) ? 0x00ff : 0xff00;
    const uint16_t word = board.main_read(address & ~1u, mask);
    *value = (address & 1) ? static_cast<uint8_t>(word) : static_cast<uint8_t>(word >> 8);
    return true;
}

bool bus_read16(void* context, uint32_t address, uint16_t* value)
{ *value = static_cast<Tmnt2Board*>(context)->main_read(address); return true; }

bool bus_write8(void* context, uint32_t address, uint8_t value)
{
    if (address & 1) static_cast<Tmnt2Board*>(context)->main_write(address & ~1u, value, 0x00ff);
    else static_cast<Tmnt2Board*>(context)->main_write(address, uint16_t(value) << 8, 0xff00);
    return true;
}

bool bus_write16(void* context, uint32_t address, uint16_t value)
{ static_cast<Tmnt2Board*>(context)->main_write(address, value); return true; }
uint16_t exception_read16(uint32_t address) { return ng_r16(address); }
void exception_write16(uint32_t address, uint16_t value) { ng_w16(address, value); }
}

namespace multiarcaderecomp::konami {

Tmnt2MainCpu::Tmnt2MainCpu(Tmnt2Board& board, const ng_func_entry_t* entries, size_t entry_count)
    : board_(board), entries_(entries), entry_count_(entry_count)
{
    if (!entries_ || entry_count_ == 0) throw std::runtime_error("TMNT2-family 68000 AOT table is empty");
    const M68kBusCallbacks bus{&board_, bus_read8, bus_read16, bus_write8, bus_write16};
    multiarcaderecomp_m68k_bus_bind(&bus);
    multiarcade_recomp_m68k_set_exception_bus(exception_read16, exception_write16);
    reset();
}

Tmnt2MainCpu::~Tmnt2MainCpu()
{
    multiarcaderecomp_m68k_bus_bind(nullptr);
}

void Tmnt2MainCpu::reset()
{
    ng_cpu = {};
    ng_cpu.a[7] = (uint32_t(board_.main_read(0)) << 16) | board_.main_read(2);
    ng_cpu.pc = ((uint32_t(board_.main_read(4)) << 16) | board_.main_read(6)) & 0x00ffffffu;
    ng_cpu.s = 1;
    ng_cpu.ipl = 7;
    ng_cycles = 0;
    ng_next_event = 0;
    ng_icount = 0;
}

int Tmnt2MainCpu::execute_until(uint64_t cycle_deadline)
{
    const int64_t start = ng_cycles;
    const int64_t target = static_cast<int64_t>(cycle_deadline);
    if (target <= start) return 0;
    while (ng_cycles < target) {
        board_.set_main_cycles(static_cast<uint64_t>(ng_cycles));
        if (board_.main_irq4_pending() && ng_cpu.ipl < 4) {
            const auto irq = m68k_recomp_accept_interrupt(4);
            if (irq.status != M68K_RECOMP_DISPATCH_OK)
                throw std::runtime_error("TMNT2-family 68000 could not enter its level-4 interrupt");
        }
        if (ng_cpu.stopped) { ng_cycles = target; break; }
        ng_next_event = target;
        const uint32_t instruction_pc = pc();
        static const bool trace_bad_pc = std::getenv("MULTIARCADERECOMP_TMNT2_TRACE_BAD_PC") != nullptr;
        const uint32_t sp = ng_cpu.a[7];
        const uint32_t stack_top = trace_bad_pc
            ? ((uint32_t(board_.main_read(sp)) << 16) | board_.main_read(sp + 2)) : 0;
        recent_pc_.push_back({instruction_pc, board_.main_read(instruction_pc),
                              sp, stack_top, ng_cpu.a[4], ng_cpu.a[5], ng_cpu.a[6]});
        if (recent_pc_.size() > 32) recent_pc_.pop_front();
        const uint64_t instruction_count_before = ng_icount;
        const auto result = m68k_recomp_dispatch_once(entries_, entry_count_);
        // A generated block can end at an odd indirect target. On a 68000
        // this is guest address-error behavior for the interpreter to process,
        // not a host-side fatal dispatch condition.
        if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY ||
            result.status == M68K_RECOMP_DISPATCH_BAD_PC) {
            if (result.status == M68K_RECOMP_DISPATCH_BAD_PC && trace_bad_pc) {
                std::cerr << "TMNT2 68000 BAD_PC fallback at 0x" << std::hex
                          << result.pc << " SP=0x" << ng_cpu.a[7]
                          << " A4=0x" << ng_cpu.a[4] << " A5=0x" << ng_cpu.a[5]
                          << " A6=0x" << ng_cpu.a[6] << " stack=0x"
                          << ((uint32_t(board_.main_read(ng_cpu.a[7])) << 16) |
                              board_.main_read(ng_cpu.a[7] + 2)) << '\n'
                          << disassembly_before_pc();
            }
            multiarcaderecomp_m68k_bus_clear_fault();
            const int consumed = multiarcaderecomp_m68k_musashi_step();
            ng_cycles += consumed > 0 ? consumed : 1;
            ++ng_icount;
            ++fallback_instructions_;
            continue;
        }
        if (result.status != M68K_RECOMP_DISPATCH_OK) {
            std::ostringstream message;
            message << "TMNT2-family 68000 dispatch failed at PC=0x" << std::hex
                    << result.pc << " (status " << std::dec
                    << static_cast<unsigned>(result.status) << ")\n"
                    << disassembly_before_pc();
            throw std::runtime_error(message.str());
        }
        aot_instructions_ += ng_icount - instruction_count_before;
    }
    return static_cast<int>(ng_cycles - start);
}

int Tmnt2MainCpu::execute_frame()
{
    // TMNT2 uses a 6 MHz pixel clock with 384x264 total pixels. The 16 MHz
    // 68000 therefore receives exactly 270336 clocks per frame. MAME's
    // K052109 asserts IRQ4 at the start of vblank (line 240), not at frame
    // end; execute up to that beam position, assert it, then run the tail.
    constexpr int cycles_per_line = 1'024;
    constexpr int vblank_line = 240;
    constexpr int total_lines = 264;
    constexpr int vblank_cycle = cycles_per_line * vblank_line;
    constexpr int cycles_per_frame = cycles_per_line * total_lines;
    const uint64_t start = cycles();
    int consumed = execute_until(start + vblank_cycle);
    board_.video_vblank();
    consumed += execute_until(start + cycles_per_frame);
    return consumed;
}

uint32_t Tmnt2MainCpu::pc() const
{
    return ng_cpu.pc;
}

std::string Tmnt2MainCpu::current_instruction() const
{
    char opcode[32];
    std::snprintf(opcode, sizeof(opcode), "opcode=0x%04X", board_.main_read(pc()));
    return opcode;
}

std::string Tmnt2MainCpu::disassembly_before_pc() const
{
    std::ostringstream output;
    for (const auto& entry : recent_pc_)
        output << "0x" << std::hex << entry.pc << ": opcode=0x" << entry.opcode
               << " SP=0x" << entry.sp << " [SP]=0x" << entry.stack_top
               << " A4=0x" << entry.a4 << " A5=0x" << entry.a5
               << " A6=0x" << entry.a6 << '\n';
    return output.str();
}

uint32_t Tmnt2MainCpu::data_register(unsigned index) const
{
    if (index >= 8) throw std::out_of_range("68000 data register index");
    return ng_cpu.d[index];
}

uint32_t Tmnt2MainCpu::address_register(unsigned index) const
{
    if (index >= 8) throw std::out_of_range("68000 address register index");
    return ng_cpu.a[index];
}

uint64_t Tmnt2MainCpu::cycles() const { return static_cast<uint64_t>(ng_cycles); }

} // namespace multiarcaderecomp::konami
