// Bridge PSXRecomp's function-level CPUState ABI to the shared R3000A bus.
#include "psxrecomp_bridge.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {
constexpr unsigned EXC_SYS = 8;
constexpr unsigned EXC_RI = 10;

thread_local PsxRecompContext *active_context = nullptr;
uint32_t g_last_store_pc = 0;

void sync_to_machine(PsxRecompContext *context)
{
    if (!context || !context->machine_state)
        return;
    PsxCpuState *dst = context->machine_state;
    const CPUState &src = context->generated;
    std::copy(std::begin(src.gpr), std::end(src.gpr), std::begin(dst->r));
    dst->r[0] = 0;
    dst->pc = src.pc;
    dst->hi = src.hi;
    dst->lo = src.lo;
    std::copy_n(src.cop0, 16, dst->cp0);
    std::copy(std::begin(src.gte_data), std::end(src.gte_data),
              std::begin(dst->gte_data));
    std::copy(std::begin(src.gte_ctrl), std::end(src.gte_ctrl),
              std::begin(dst->gte_control));
}

void sync_from_machine(PsxRecompContext *context)
{
    if (!context || !context->machine_state)
        return;
    const PsxCpuState *src = context->machine_state;
    CPUState &dst = context->generated;
    std::copy(std::begin(src->r), std::end(src->r), std::begin(dst.gpr));
    dst.gpr[0] = 0;
    dst.pc = src->pc;
    dst.hi = src->hi;
    dst.lo = src->lo;
    std::copy_n(src->cp0, 16, dst.cop0);
    std::copy(std::begin(src->gte_data), std::end(src->gte_data),
              std::begin(dst.gte_data));
    std::copy(std::begin(src->gte_control), std::end(src->gte_control),
              std::begin(dst.gte_ctrl));
}

bool bus_read(CPUState *cpu, uint32_t address, unsigned size,
              uint32_t *value)
{
    PsxRecompContext *context = active_context;
    if (!context || !context->bus || !context->bus->read || !value) {
        if (context) context->faulted = 1;
        *value = 0;
        return false;
    }
    sync_to_machine(context);
    const bool ok = context->bus->read(context->bus->opaque, address, size,
                                       cpu->current_pc, value);
    sync_from_machine(context);
    if (!ok) context->faulted = 1;
    return ok;
}

bool bus_write(CPUState *cpu, uint32_t address, unsigned size, uint32_t value)
{
    PsxRecompContext *context = active_context;
    if (!context || !context->bus || !context->bus->write) {
        if (context) context->faulted = 1;
        return false;
    }
    sync_to_machine(context);
    const bool ok = context->bus->write(context->bus->opaque, address, size,
                                        value, cpu->current_pc);
    sync_from_machine(context);
    if (!ok) context->faulted = 1;
    return ok;
}

uint32_t read_word(uint32_t address)
{
    uint32_t value = 0;
    if (active_context)
        bus_read(&active_context->generated, address, 4, &value);
    return value;
}

void write_word(uint32_t address, uint32_t value)
{
    if (active_context)
        bus_write(&active_context->generated, address, 4, value);
}

uint16_t read_half(uint32_t address)
{
    uint32_t value = 0;
    if (active_context)
        bus_read(&active_context->generated, address, 2, &value);
    return static_cast<uint16_t>(value);
}

void write_half(uint32_t address, uint16_t value)
{
    if (active_context)
        bus_write(&active_context->generated, address, 2, value);
}

uint8_t read_byte(uint32_t address)
{
    uint32_t value = 0;
    if (active_context)
        bus_read(&active_context->generated, address, 1, &value);
    return static_cast<uint8_t>(value);
}

void write_byte(uint32_t address, uint8_t value)
{
    if (active_context)
        bus_write(&active_context->generated, address, 1, value);
}

void raise_exception(CPUState *cpu, unsigned exception, uint32_t pc,
                     uint32_t instruction)
{
    if (!active_context || !active_context->machine_state) return;
    sync_to_machine(active_context);
    (void)psx_aot_raise_exception(active_context->machine_state, exception,
                                  pc, instruction);
    sync_from_machine(active_context);
    active_context->faulted = 1;
    cpu->pc = active_context->generated.pc;
}
}

extern "C" void psx_recomp_context_init(PsxRecompContext *context,
                                         PsxCpuState *state,
                                         const PsxBus *bus)
{
    if (!context) return;
    *context = PsxRecompContext{};
    context->machine_state = state;
    context->bus = bus;
    sync_from_machine(context);
    context->generated.read_word = read_word;
    context->generated.write_word = write_word;
    context->generated.read_half = read_half;
    context->generated.write_half = write_half;
    context->generated.read_byte = read_byte;
    context->generated.write_byte = write_byte;
}

extern "C" PsxAotExit psx_recomp_call(PsxRecompContext *context,
                                       PsxRecompFunction function)
{
    if (!context || !context->machine_state || !function)
        return PSX_AOT_INTERPRETER;
    if (context->machine_state->delayed_load_pending ||
        context->machine_state->branch_pending)
        return PSX_AOT_INTERPRETER;

    sync_from_machine(context);
    context->generated.instruction_count = 0;
    context->faulted = 0;
    PsxRecompContext *previous = active_context;
    active_context = context;
    function(&context->generated);
    active_context = previous;
    sync_to_machine(context);
    context->machine_state->instructions +=
        context->generated.instruction_count;
    context->machine_state->r[0] = 0;
    return context->faulted ? PSX_AOT_EXCEPTION : PSX_AOT_DISPATCH;
}

extern "C" void psx_recomp_set_pc(CPUState *cpu, uint32_t pc)
{
    cpu->current_pc = pc;
    ++cpu->instruction_count;
}

extern "C" uint32_t psx_cyc_load_byte(CPUState *cpu, uint32_t address,
                                        uint32_t, uint32_t)
{
    return read_byte(address);
}

extern "C" uint32_t psx_cyc_load_half(CPUState *cpu, uint32_t address,
                                        uint32_t, uint32_t)
{
    return read_half(address);
}

extern "C" uint32_t psx_cyc_load_word(CPUState *cpu, uint32_t address,
                                        uint32_t, uint32_t)
{
    return read_word(address);
}

extern "C" uint32_t psx_cyc_lwc2_read(CPUState *cpu, uint32_t address)
{
    return read_word(address);
}

extern "C" void psx_store_cycle_barrier(void) {}
extern "C" void debug_server_log_call_entry(uint32_t) {}
extern "C" void psx_restore_state_escape(void) {}
extern "C" void psx_rfe_mark_escape(void) {}
extern "C" uint32_t g_debug_last_store_pc = 0;

extern "C" void psx_check_interrupts_at(CPUState *cpu, uint32_t resume_pc)
{
    if (!active_context) return;
    cpu->pc = resume_pc;
    sync_to_machine(active_context);
    if (active_context->bus && active_context->bus->check_interrupts)
        active_context->bus->check_interrupts(active_context->bus->opaque,
                                              active_context->machine_state,
                                              resume_pc);
    sync_from_machine(active_context);
}

extern "C" void gte_execute(CPUState *cpu, uint32_t instruction)
{
    if (!active_context || !active_context->bus ||
        !active_context->bus->cop2_command) {
        if (active_context) active_context->faulted = 1;
        return;
    }
    sync_to_machine(active_context);
    const bool ok = active_context->bus->cop2_command(
        active_context->bus->opaque, active_context->machine_state,
        cpu->current_pc, instruction);
    sync_from_machine(active_context);
    if (!ok) active_context->faulted = 1;
}

namespace {
uint32_t gte_read(CPUState *cpu, unsigned reg, bool control)
{
    if (!active_context || !active_context->bus ||
        !active_context->bus->cop2_read)
        return control ? cpu->gte_ctrl[reg & 31] : cpu->gte_data[reg & 31];
    sync_to_machine(active_context);
    uint32_t value = 0;
    const bool ok = active_context->bus->cop2_read(
        active_context->bus->opaque, active_context->machine_state,
        cpu->current_pc, control, reg & 31, &value);
    sync_from_machine(active_context);
    if (!ok) active_context->faulted = 1;
    return value;
}

void gte_write(CPUState *cpu, unsigned reg, uint32_t value, bool control)
{
    if (!active_context || !active_context->bus ||
        !active_context->bus->cop2_write) {
        (control ? cpu->gte_ctrl : cpu->gte_data)[reg & 31] = value;
        return;
    }
    sync_to_machine(active_context);
    const bool ok = active_context->bus->cop2_write(
        active_context->bus->opaque, active_context->machine_state,
        cpu->current_pc, control, reg & 31, value);
    sync_from_machine(active_context);
    if (!ok) active_context->faulted = 1;
}
}

extern "C" uint32_t gte_read_data(CPUState *cpu, uint32_t reg)
{
    return gte_read(cpu, reg, false);
}

extern "C" void gte_write_data(CPUState *cpu, uint32_t reg, uint32_t value)
{
    gte_write(cpu, reg, value, false);
}

extern "C" uint32_t gte_read_ctrl(CPUState *cpu, uint32_t reg)
{
    return gte_read(cpu, reg, true);
}

extern "C" void gte_write_ctrl(CPUState *cpu, uint32_t reg, uint32_t value)
{
    gte_write(cpu, reg, value, true);
}

extern "C" void gte_precision_store_word(uint32_t, uint32_t, uint32_t) {}

extern "C" int gte_nclip_exact_sign(uint32_t, uint32_t, int32_t mac0)
{
    return mac0 < 0;
}

extern "C" void psx_syscall(CPUState *cpu, uint32_t pc, uint32_t instruction)
{
    raise_exception(cpu, EXC_SYS, pc, instruction);
}

extern "C" void psx_unknown_dispatch(CPUState *cpu, uint32_t pc,
                                       uint32_t instruction)
{
    raise_exception(cpu, EXC_RI, pc, instruction);
}
