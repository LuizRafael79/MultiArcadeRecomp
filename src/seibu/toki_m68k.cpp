#include "toki_machine.hpp"

#include <neogeorecomp/bus.h>
#include "system16/system16_m68k_bus.h"
#include "system16/m68k_recomp_dispatch.h"
#include "system16/system16_musashi_fallback.h"
#include "cpus/m68000/runtime/cpu_runtime.h"

#include <cstdio>
#include <stdexcept>

namespace mamerecomp::seibu {
namespace {
bool read8(void* context, uint32_t address, uint8_t* value)
{
    *value = static_cast<TokiBoard*>(context)->main_read8(address & 0x00ffffffu);
    return true;
}
bool read16(void* context, uint32_t address, uint16_t* value)
{
    *value = static_cast<TokiBoard*>(context)->main_read16(address & 0x00ffffffu);
    return true;
}
bool write8(void* context, uint32_t address, uint8_t value)
{
    static_cast<TokiBoard*>(context)->main_write8(address & 0x00ffffffu, value);
    return true;
}
bool write16(void* context, uint32_t address, uint16_t value)
{
    static_cast<TokiBoard*>(context)->main_write16(address & 0x00ffffffu, value);
    return true;
}
uint16_t exception_read16(uint32_t address) { return ng_r16(address); }
void exception_write16(uint32_t address, uint16_t value) { ng_w16(address, value); }
}

class TokiMainCpu::Impl {
public:
    Impl(TokiBoard& board, const ng_func_entry_t* entries, size_t count)
        : board(board), entries(entries), count(count)
    {
        if (!entries || !count) throw std::runtime_error("Toki 68000 AOT table is empty");
        const M68kBusCallbacks bus{&board, read8, read16, write8, write16};
        m68k_system16_bus_bind(&bus);
        mame_recomp_m68k_set_exception_bus(exception_read16, exception_write16);
        reset();
    }
    ~Impl() { m68k_system16_bus_bind(nullptr); }

    void reset()
    {
        ng_cpu = {};
        ng_cpu.a[7] = (uint32_t(board.main_read16(0)) << 16) | board.main_read16(2);
        ng_cpu.pc = ((uint32_t(board.main_read16(4)) << 16) | board.main_read16(6)) & 0x00ffffffu;
        ng_cpu.s = 1;
        ng_cpu.ipl = 7;
        ng_cycles = 0;
        ng_next_event = 0;
        ng_icount = 0;
    }

    int execute_until(uint64_t deadline)
    {
        const int64_t start = ng_cycles;
        const int64_t target = static_cast<int64_t>(deadline);
        while (ng_cycles < target) {
            if (board.main_irq_pending() && ng_cpu.ipl < 1) {
                const auto irq = m68k_recomp_accept_interrupt(1);
                if (irq.status != M68K_RECOMP_DISPATCH_OK)
                    throw std::runtime_error("Toki 68000 failed to enter its level-1 vblank interrupt");
                board.acknowledge_main_irq();
            }
            if (ng_cpu.stopped) { ng_cycles = target; break; }
            ng_next_event = target;
            const auto before = ng_icount;
            const auto result = m68k_recomp_dispatch_once(entries, count);
            if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY) {
                m68k_system16_bus_clear_fault();
                const int consumed = mamerecomp_system16_musashi_step();
                ng_cycles += consumed > 0 ? consumed : 1;
                ++ng_icount;
                ++fallback;
                continue;
            }
            if (result.status != M68K_RECOMP_DISPATCH_OK) {
                char message[128];
                std::snprintf(message, sizeof(message), "Toki 68000 dispatch failed at PC=0x%06X (status %u)",
                              result.pc, static_cast<unsigned>(result.status));
                throw std::runtime_error(message);
            }
            aot += ng_icount - before;
        }
        return static_cast<int>(ng_cycles - start);
    }

    TokiBoard& board;
    const ng_func_entry_t* entries;
    size_t count;
    uint64_t aot = 0;
    uint64_t fallback = 0;
};

TokiMainCpu::TokiMainCpu(TokiBoard& board, const ng_func_entry_t* entries, size_t count)
    : impl_(new Impl(board, entries, count)) {}
TokiMainCpu::~TokiMainCpu() { delete impl_; }
void TokiMainCpu::reset() { impl_->reset(); }
int TokiMainCpu::execute_until(uint64_t cycles) { return impl_->execute_until(cycles); }
uint32_t TokiMainCpu::pc() const { return ng_cpu.pc; }
uint64_t TokiMainCpu::cycles() const { return static_cast<uint64_t>(ng_cycles); }
uint64_t TokiMainCpu::aot_instructions() const { return impl_->aot; }
uint64_t TokiMainCpu::fallback_instructions() const { return impl_->fallback; }

} // namespace mamerecomp::seibu
