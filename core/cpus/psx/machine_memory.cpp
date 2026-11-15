#include "machine_memory.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace multiarcaderecomp::psx {
namespace {
uint32_t read_le(const std::vector<uint8_t> &data, size_t offset, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= uint32_t(data[offset + i]) << (i * 8);
    return value;
}
void write_le(std::vector<uint8_t> &data, size_t offset, unsigned size, uint32_t value)
{
    for (unsigned i = 0; i < size; ++i)
        data[offset + i] = uint8_t(value >> (i * 8));
}
void write_le32_direct(uint8_t *data, uint32_t value)
{
    if constexpr (std::endian::native == std::endian::big)
        value = __builtin_bswap32(value);
    std::memcpy(data, &value, sizeof(value));
}
uint32_t read_le32_direct(const uint8_t *data)
{
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    if constexpr (std::endian::native == std::endian::big)
        value = __builtin_bswap32(value);
    return value;
}
size_t visible_ram_size(uint32_t config, size_t fallback)
{
    switch ((config >> 8) & 0xf) {
    case 0x0: case 0x1: return 1u << 20;
    case 0x4: case 0x8: case 0x9: return 2u << 20;
    case 0x2: case 0x3: case 0x5: case 0x6: case 0xc: return 4u << 20;
    case 0x7: case 0xa: case 0xb: case 0xd: case 0xe: return 8u << 20;
    case 0xf: return 16u << 20;
    default: return fallback;
    }
}
}

MachineMemory::MachineMemory(MemoryConfig config)
    : ram_(config.ram_bytes),
      dma_(ram_, [this](unsigned source, bool active) { set_irq(source, active); }),
      vram_(config.vram_bytes), gpu_(vram_),
      timers_([this](unsigned source, bool active) { set_irq(source, active); },
              config.cpu_cycle_hz),
      sio_(&MachineMemory::sio_irq_cb, this),
      sio1_(&MachineMemory::sio_irq_cb, this, 8),
      spu_([this](bool active) { set_irq(9, active); }),
      ram_config_(config.ram_config), visible_ram_bytes_(visible_ram_size(config.ram_config, config.ram_bytes)),
      cpu_cycle_hz_(config.cpu_cycle_hz),
      bios_window_bytes_(config.bios_window_bytes)
{
    profile_timing_ = std::getenv("MULTIARCADERECOMP_ZN_PROFILE_RUNTIME") != nullptr;
    (void)gpu_.advance_screen_timing(0, cpu_cycle_hz_);
    next_gpu_event_cycle_ = gpu_.cycles_until_screen_event(cpu_cycle_hz_);
    next_spu_irq_cycle_ = UINT64_MAX;
    mdec_.device_start();
    mdec_.device_reset();
    dma_.set_device_write(2, [this](uint32_t, std::span<const uint32_t> words) { gpu_.dma_write(words); });
    dma_.set_device_read(2, [this](uint32_t, size_t words) { return gpu_.dma_read(words); });
    dma_.set_device_write(0, [this](uint32_t address, std::span<const uint32_t> words) {
        std::vector<uint32_t> ram_words(ram_.size() / 4);
        for (size_t i = 0; i < ram_words.size(); ++i)
            ram_words[i] = read_le32_direct(ram_.data() + i * 4);
        mdec_.dma_write(ram_words.data(), address, static_cast<int32_t>(words.size()));
    });
    dma_.set_device_read(1, [this](uint32_t address, size_t words) {
        std::vector<uint32_t> ram_words(ram_.size() / 4);
        for (size_t i = 0; i < ram_words.size(); ++i)
            ram_words[i] = read_le32_direct(ram_.data() + i * 4);
        const size_t offset = (address & (ram_.size() - 1)) / 4;
        const size_t count = std::min(words, ram_words.size() - offset);
        mdec_.dma_read(ram_words.data(), address, static_cast<int32_t>(count));
        for (size_t i = 0; i < count; ++i)
            write_le32_direct(ram_.data() + (offset + i) * 4, ram_words[offset + i]);
        return std::vector<uint32_t>(ram_words.begin() + offset,
                                     ram_words.begin() + offset + count);
    });
    dma_.set_device_write(4, [this](uint32_t, std::span<const uint32_t> words) {
        spu_.dma_write(words);
    });
    dma_.set_device_read(4, [this](uint32_t, size_t words) {
        return spu_.dma_read(words);
    });
}

bool MachineMemory::load_bios(const uint8_t *bytes, size_t size)
{
    if (!bytes || size == 0 || size > bios_window_bytes_) return false;
    bios_.assign(bytes, bytes + size);
    return true;
}

PsxBus MachineMemory::make_bus()
{
    PsxBus bus{};
    bus.opaque = this;
    bus.read = read_cb;
    bus.write = write_cb;
    bus.cop2_command = cop2_command_cb;
    bus.cop2_read = cop2_read_cb;
    bus.cop2_write = cop2_write_cb;
    bus.check_interrupts = check_interrupts_cb;
    bus.scheduler_yield_requested = scheduler_yield_requested_cb;
    bus.cache_read = cache_read_cb;
    bus.cache_write = cache_write_cb;
    bus.direct_ram = ram_.data();
    bus.direct_ram_write = ram_.data();
    bus.direct_ram_visible_bytes = &visible_ram_bytes_;
    bus.direct_ram_size = ram_.size();
    bus.direct_ram_mask = (ram_.size() & (ram_.size() - 1)) == 0
        ? ram_.size() - 1 : SIZE_MAX;
    return bus;
}

bool MachineMemory::scheduler_yield_requested_cb(void *opaque)
{
    const auto *self = static_cast<const MachineMemory *>(opaque);
    return self->board_bus_ && self->board_bus_->scheduler_yield_requested &&
        self->board_bus_->scheduler_yield_requested(self->board_bus_->opaque);
}

void MachineMemory::attach_cpu(PsxCpuState *state)
{
    cpu_state_ = state;
    sync_irq_line();
    update_event_deadline();
}

uint32_t MachineMemory::cache_readword(uint32_t address) const
{
    constexpr uint32_t biu_tag = 0x04, biu_lock = 0x01;
    constexpr uint32_t biu_invalidate = 0x02, biu_data = 0x80;
    constexpr uint32_t biu_instruction = 0x800, tag_match_mask = 0xfffff000;
    uint32_t data = 0;
    if (biu_ & biu_tag) {
        if (biu_ & biu_instruction) {
            const uint32_t tag = icache_tags_[(address / 16) % icache_tags_.size()];
            data |= tag & 0x0f;
            if (((tag ^ address) & tag_match_mask) == 0) data |= 0x10;
        }
    } else if (biu_ & (biu_lock | biu_invalidate)) {
        return 0;
    } else {
        if (biu_ & biu_instruction)
            data |= icache_[(address / 4) % icache_.size()];
        if (biu_ & biu_data)
            data |= dcache_[(address / 4) % dcache_.size()];
    }
    return data;
}

void MachineMemory::cache_writeword(uint32_t address, uint32_t value)
{
    constexpr uint32_t biu_tag = 0x04, biu_lock = 0x01;
    constexpr uint32_t biu_invalidate = 0x02, biu_data = 0x80;
    constexpr uint32_t biu_instruction = 0x800, tag_match_mask = 0xfffff000;
    const size_t tag_index = (address / 16) % icache_tags_.size();
    if (biu_ & biu_tag) {
        if (biu_ & biu_instruction)
            icache_tags_[tag_index] = (value & 0x0f) | (address & tag_match_mask);
    } else if (biu_ & (biu_lock | biu_invalidate)) {
        if (biu_ & biu_instruction)
            icache_tags_[tag_index] = address & tag_match_mask;
    } else {
        if (biu_ & biu_instruction)
            icache_[(address / 4) % icache_.size()] = value;
        if (biu_ & biu_data)
            dcache_[(address / 4) % dcache_.size()] = value;
    }
}

void MachineMemory::advance_gpu_to(uint64_t cycles)
{
    if (gpu_.advance_screen_timing(cycles, cpu_cycle_hz_))
        set_irq(0, true);
    next_gpu_event_cycle_ = cycles + gpu_.cycles_until_screen_event(cpu_cycle_hz_);
}

void MachineMemory::advance_spu_to(uint64_t cycles)
{
    current_cpu_cycles_ = std::max(current_cpu_cycles_, cycles);
    spu_.advance(cycles, cpu_cycle_hz_);
    next_spu_irq_cycle_ = spu_.irq_timing_active()
        ? cycles + spu_.cycles_until_sample(cpu_cycle_hz_)
        : UINT64_MAX;
}

bool MachineMemory::cache_read_cb(void *opaque, uint32_t address,
                                  unsigned size, uint32_t *value)
{
    const auto *self = static_cast<MachineMemory *>(opaque);
    const uint32_t word = self->cache_readword(address);
    const unsigned shift = size == 1 ? (address & 3) * 8 :
                           size == 2 ? (address & 2) * 8 : 0;
    *value = word >> shift;
    return true;
}

bool MachineMemory::cache_write_cb(void *opaque, uint32_t address,
                                   unsigned size, uint32_t value)
{
    if (size != 1 && size != 2 && size != 4) return false;
    auto *self = static_cast<MachineMemory *>(opaque);
    const unsigned shift = size == 1 ? (address & 3) * 8 :
                           size == 2 ? (address & 2) * 8 : 0;
    self->cache_writeword(address, value << shift);
    return true;
}

bool MachineMemory::read_cb(void *opaque, uint32_t address, unsigned size,
                            uint32_t pc, uint32_t *value)
{
    auto *self = static_cast<MachineMemory *>(opaque);
    if (self->cpu_state_) self->current_cpu_cycles_ = self->cpu_state_->cycles;
    // The interpreter fetches one aligned word at PC for every guest
    // instruction. Keep that hot path out of the general device decoder.
    if (value && size == 4 && address == pc) {
        const uint32_t physical = address & 0x1fffffffu;
        if (physical < self->visible_ram_bytes_ && physical < self->ram_.size()) {
            *value = read_le32_direct(self->ram_.data() + physical);
            return true;
        }
        if (physical >= 0x1f800000 && physical <= 0x1f8003fc) {
            *value = read_le32_direct(self->scratch_.data() + physical - 0x1f800000);
            return true;
        }
        if (!self->bios_.empty() && physical >= 0x1fc00000 &&
            physical - 0x1fc00000 + 4 <= std::min(self->bios_.size(), self->rom_window_size())) {
            *value = read_le32_direct(self->bios_.data() + physical - 0x1fc00000);
            return true;
        }
    }
    // Ordinary RAM, scratchpad, and BIOS reads cannot move a device deadline.
    // Avoid rescanning GPU/timer/DMA/serial deadlines on this very hot path.
    size_t ram_at = 0;
    const bool plain_ram = self->ram_offset(address, &ram_at) &&
                           ram_at + size <= self->ram_.size();
    const uint32_t physical = address & 0x1fffffffu;
    const bool plain_scratch = physical >= 0x1f800000 &&
        physical + size <= 0x1f800400;
    const bool plain_bios = !self->bios_.empty() && physical >= 0x1fc00000 &&
        physical - 0x1fc00000 + size <=
            std::min(self->bios_.size(), self->rom_window_size());
    const bool ok = self->read(address, size, value);
    if (!plain_ram && !plain_scratch && !plain_bios)
        self->update_event_deadline();
    static const bool trace_gpu_status = std::getenv("MULTIARCADERECOMP_ZN_TRACE_GPU_STATUS") != nullptr;
    if (ok && trace_gpu_status &&
        (address & 0x1fffffffu) == 0x1f801814) {
        static unsigned logged = 0;
        if (logged++ < 256)
            std::cerr << "ZN GPUSTAT read pc=0x" << std::hex << pc
                      << " value=0x" << *value << " RCNT2=0x"
                      << self->timers_.peek(0x20) << std::dec << '\n';
    }
    return ok;
}
bool MachineMemory::write_cb(void *opaque, uint32_t address, unsigned size,
                             uint32_t value, uint32_t pc)
{
    static const bool trace_gpu_commands = std::getenv("MULTIARCADERECOMP_ZN_TRACE_GPU_COMMANDS") != nullptr;
    static const bool trace_pointer_writes = std::getenv("MULTIARCADERECOMP_ZN_TRACE_POINTER_WRITES") != nullptr;
    static const bool trace_sound_latch = std::getenv("MULTIARCADERECOMP_ZN_TRACE_SOUND_COMMANDS") != nullptr;
    auto *self = static_cast<MachineMemory *>(opaque);
    if (self->cpu_state_) self->current_cpu_cycles_ = self->cpu_state_->cycles;
    const uint32_t physical = address & 0x1fffffffu;
    if (physical == 0x1fb60000 && trace_sound_latch) {
        static unsigned logged_sound_writes = 0;
        size_t code_offset = 0;
        uint32_t opcode = 0xffffffffu;
        if (self->ram_offset(pc, &code_offset) &&
            code_offset + sizeof(opcode) <= self->ram_.size()) {
            opcode = read_le32_direct(self->ram_.data() + code_offset);
        }
        if (logged_sound_writes++ < 512)
            std::cerr << "ZN2 latch instruction pc=0x" << std::hex << pc
                      << " opcode=0x" << opcode << " cycle=" << std::dec
                      << self->current_cpu_cycles_ << '\n';
    }
    if (physical >= 0x1f801814 && physical < 0x1f801818 &&
        trace_gpu_commands) {
        static unsigned logged_gpu_writes = 0;
        if (logged_gpu_writes++ < 512)
            std::cerr << "ZN GP1 write pc=0x" << std::hex << pc
                      << " address=0x" << physical << " size=" << std::dec << size
                      << " value=0x" << std::hex << value << std::dec << '\n';
    }
    // Optional boot diagnostic: watch a RAM word while tracing BIOS state.
    if (trace_pointer_writes) {
        static const uint32_t watched_address = [] {
            const char *text = std::getenv("MULTIARCADERECOMP_ZN_WATCH_WRITE_ADDRESS");
            return text ? static_cast<uint32_t>(std::strtoul(text, nullptr, 0)) : 0xffffffffu;
        }();
        const uint32_t physical = address & 0x1fffffffu;
        const uint32_t watched = watched_address & 0x1fffffffu;
        if (physical >= watched && physical - watched < 4) {
        static unsigned logged = 0;
        if (logged++ < 256)
            std::cerr << "ZN watched RAM write pc=0x" << std::hex << pc
                      << " address=0x" << address << " size=" << size << " value=0x" << value
                      << std::dec << '\n';
        }
    }
    const bool ok = self->write(address, size, value, pc);
    // Plain RAM and scratchpad writes do not affect interrupt sources or
    // device scheduling. Keep those synchronizations on MMIO/board accesses.
    size_t ram_at = 0;
    const bool plain_ram = self->ram_offset(address, &ram_at) &&
                           ram_at + size <= self->ram_.size();
    const uint32_t physical_after = address & 0x1fffffffu;
    const bool plain_scratch = physical_after >= 0x1f800000 &&
        physical_after + size <= 0x1f800400;
    if (!plain_ram && !plain_scratch) {
        self->sync_irq_line();
        self->update_event_deadline();
    }
    return ok;
}
bool MachineMemory::board_read_adapter(void *opaque, uint32_t address,
                                      unsigned size, uint32_t pc,
                                      uint32_t *value)
{
    const auto *self = static_cast<const MachineMemory *>(opaque);
    if (self->board_bus_ && self->board_bus_->set_cpu_cycles)
        self->board_bus_->set_cpu_cycles(self->board_bus_->opaque, self->current_cpu_cycles_);
    return self->board_bus_ && self->board_bus_->read &&
        self->board_bus_->read(self->board_bus_->opaque, address, size, pc, value);
}
bool MachineMemory::board_write_adapter(void *opaque, uint32_t address,
                                        unsigned size, uint32_t pc,
                                        uint32_t value)
{
    const auto *self = static_cast<const MachineMemory *>(opaque);
    if (self->board_bus_ && self->board_bus_->set_cpu_cycles)
        self->board_bus_->set_cpu_cycles(self->board_bus_->opaque, self->current_cpu_cycles_);
    return self->board_bus_ && self->board_bus_->write &&
        self->board_bus_->write(self->board_bus_->opaque, address, size, value, pc);
}
void MachineMemory::check_interrupts_cb(void *opaque, PsxCpuState *state, uint32_t resume_pc)
{
    auto *self = static_cast<MachineMemory *>(opaque);
    self->current_cpu_cycles_ = state->cycles;
    self->dma_.advance_to(state->cycles, self->cpu_cycle_hz_);
    if (state->cycles >= self->next_gpu_event_cycle_)
        self->advance_gpu_to(state->cycles);
    if (state->cycles >= self->timers_.next_event_cycle())
        self->timers_.advance(state->cycles);
    if (state->cycles >= self->next_spu_irq_cycle_)
        self->advance_spu_to(state->cycles);
    const uint64_t serial_event_cycle = self->board_bus_ &&
        self->board_bus_->serial_next_event_cycle
        ? self->board_bus_->serial_next_event_cycle(self->board_bus_->opaque)
        : state->cycles;
    const bool serial_due = state->cycles >= serial_event_cycle;
    if (serial_due && self->board_bus_ && self->board_bus_->advance_serial)
        self->board_bus_->advance_serial(self->board_bus_->opaque, state->cycles);
    if (state->cycles >= self->sio_.next_event_cycle()) {
        self->sio_.advance(state->cycles, self->board_bus_, self->cpu_cycle_hz_);
    } else if (serial_due && self->board_bus_ && self->board_bus_->serial_dsr) {
        self->sio_.set_dsr_line(self->board_bus_->serial_dsr(self->board_bus_->opaque));
    }
    if (state->cycles >= self->sio1_.next_event_cycle())
        self->sio1_.advance(state->cycles, nullptr, self->cpu_cycle_hz_);
    self->sync_irq_line();
    self->update_event_deadline();
    (void)resume_pc;
}

void MachineMemory::sio_irq_cb(void *opaque, unsigned source, bool active)
{
    static_cast<MachineMemory *>(opaque)->set_irq(source, active);
}

bool MachineMemory::cop2_command_cb(void *opaque, PsxCpuState *state,
                                    uint32_t pc, uint32_t opcode)
{
    auto *self = static_cast<MachineMemory *>(opaque);
    const auto profile_start = self->profile_timing_
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const bool decoded = self->gte_.docop2(pc, opcode);
    if (self->profile_timing_) {
        self->profile_gte_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - profile_start).count());
        ++self->profile_gte_commands_;
    }
    if (!decoded) return false;
    // Keep the device's register file authoritative, as MAME's PSX core does.
    // The command updates FLAG (control register 31), which BC2/B*C2 consume
    // directly from the CPU state between GTE commands.
    state->gte_control[31] = self->gte_.m_cp2cr[31].d;
    return true;
}

bool MachineMemory::cop2_read_cb(void *opaque, PsxCpuState *state, uint32_t pc,
                                bool control, unsigned reg, uint32_t *value)
{
    auto *self = static_cast<MachineMemory *>(opaque);
    *value = control ? self->gte_.getcp2cr(pc, reg) : self->gte_.getcp2dr(pc, reg);
    (control ? state->gte_control : state->gte_data)[reg & 31] = *value;
    return true;
}

bool MachineMemory::cop2_write_cb(void *opaque, PsxCpuState *state, uint32_t pc,
                                 bool control, unsigned reg, uint32_t value)
{
    auto *self = static_cast<MachineMemory *>(opaque);
    if (control) self->gte_.setcp2cr(pc, reg, value);
    else self->gte_.setcp2dr(pc, reg, value);
    auto *registers = control ? state->gte_control : state->gte_data;
    const auto *gte_registers = control ? self->gte_.m_cp2cr : self->gte_.m_cp2dr;
    registers[reg & 31] = gte_registers[reg & 31].d;
    // MTC2 r15 also shifts the screen-coordinate FIFO.
    if (!control && (reg & 31) == 15)
        for (unsigned index = 12; index <= 15; ++index)
            state->gte_data[index] = self->gte_.m_cp2dr[index].d;
    // LZCS writes compute LZCR as a side effect.
    if (!control && (reg & 31) == 30)
        state->gte_data[31] = self->gte_.m_cp2dr[31].d;
    if (control && (reg & 31) == 31)
        state->gte_control[31] = self->gte_.m_cp2cr[31].d;
    return true;
}

void MachineMemory::raise_irq(unsigned source)
{
    set_irq(source, true);
}
void MachineMemory::set_irq(unsigned source, bool active)
{
    if (source >= 11) return;
    if (active) irq_status_ |= 1u << source;
    else irq_status_ &= ~(1u << source);
    sync_irq_line();
}

void MachineMemory::sync_irq_line()
{
    if (!cpu_state_) return;
    constexpr uint32_t cause_ip2 = 1u << 10;
    if ((irq_status_ & irq_mask_) != 0) {
        cpu_state_->irq_lines |= 1u;
        cpu_state_->cp0[13] |= cause_ip2;
    } else {
        cpu_state_->irq_lines &= ~1u;
        cpu_state_->cp0[13] &= ~cause_ip2;
    }
}

void MachineMemory::update_event_deadline()
{
    if (!cpu_state_) return;
    if (spu_.irq_timing_active()) {
        if (next_spu_irq_cycle_ == UINT64_MAX ||
            next_spu_irq_cycle_ <= current_cpu_cycles_)
            next_spu_irq_cycle_ = current_cpu_cycles_ +
                spu_.cycles_until_sample(cpu_cycle_hz_);
    } else {
        next_spu_irq_cycle_ = UINT64_MAX;
    }
    uint64_t deadline = std::min({next_gpu_event_cycle_,
                                  timers_.next_event_cycle(),
                                  next_spu_irq_cycle_,
                                  sio_.next_event_cycle(), sio1_.next_event_cycle(),
                                  dma_.next_event_cycle()});
    if (board_bus_ && board_bus_->serial_next_event_cycle)
        deadline = std::min(deadline,
            board_bus_->serial_next_event_cycle(board_bus_->opaque));
    cpu_state_->next_event_cycle = deadline;
}

bool MachineMemory::ram_offset(uint32_t address, size_t *offset) const
{
    const uint32_t physical = address & 0x1fffffff;
    // RAM_CONFIG determines the visible address window. ZN-2's BIOS writes
    // 0x433e, whose size code 3 selects a 4 MiB window in MAME's decoder.
    if (physical >= visible_ram_bytes_) return false;
    *offset = physical % ram_.size();
    return true;
}

bool MachineMemory::byte_read(uint32_t address, uint8_t *value)
{
    size_t offset;
    if (ram_offset(address, &offset)) { *value = ram_[offset]; return true; }
    uint32_t p = address & 0x1fffffff;
    if (p >= 0x1f800000 && p < 0x1f800400) {
        *value = scratch_[p - 0x1f800000]; return true;
    }
    if (!bios_.empty() && p >= 0x1fc00000 &&
        p - 0x1fc00000 < std::min(bios_.size(), rom_window_size())) {
        *value = bios_[(p - 0x1fc00000) % bios_.size()]; return true;
    }
    // PSX device registers are supplied by the SoC/board adapters. Returning
    // the open-bus value keeps unmapped registers readable during early boot.
    if (p >= 0x1f000000 && p < 0x1f900000) { *value = 0xff; return true; }
    return false;
}

size_t MachineMemory::rom_window_size() const
{
    const unsigned exponent = (rom_config_ >> 16) & 0x1f;
    const size_t configured = exponent >= sizeof(size_t) * 8
        ? bios_window_bytes_ : (size_t(1) << exponent);
    return std::min(configured, bios_window_bytes_);
}

bool MachineMemory::byte_write(uint32_t address, uint8_t value)
{
    size_t offset;
    if (ram_offset(address, &offset)) { ram_[offset] = value; return true; }
    uint32_t p = address & 0x1fffffff;
    if (p >= 0x1f800000 && p < 0x1f800400) {
        scratch_[p - 0x1f800000] = value; return true;
    }
    if (p >= 0x1f000000 && p < 0x1f900000) return true;
    return false;
}

bool MachineMemory::read(uint32_t address, unsigned size, uint32_t *value)
{
    if (!value || (size != 1 && size != 2 && size != 4)) return false;
    if (address >= 0xfffe0130 && address + size <= 0xfffe0134) {
        const unsigned shift = (address & 3) * 8;
        *value = (biu_ >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    const uint32_t physical = address & 0x1fffffff;
    // Main RAM occupies the low physical address window and is by far the
    // most common data path after the BIOS has initialized the machine.
    size_t offset;
    if (ram_offset(address, &offset) && offset + size <= ram_.size()) {
        *value = size == 4 ? read_le32_direct(ram_.data() + offset)
                           : read_le(ram_, offset, size);
        return true;
    }
    if (physical >= 0x1f801c00 && physical + size <= 0x1f802000) {
        advance_spu_to(current_cpu_cycles_);
        *value = spu_.read(physical - 0x1f801c00, size);
        return true;
    }
    if (physical >= 0x1f801810 && physical + size <= 0x1f801818) {
        advance_gpu_to(current_cpu_cycles_);
        const uint32_t word = physical < 0x1f801814 ? gpu_.read_data() : gpu_.read_status();
        const unsigned shift = (physical & 3) * 8;
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801820 && physical + size <= 0x1f801828) {
        const unsigned shift = (physical & 3) * 8;
        const uint32_t word = mdec_.read((physical - 0x1f801820) >> 2);
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801040 && physical + size <= 0x1f801050) {
        if (board_bus_ && board_bus_->advance_serial)
            board_bus_->advance_serial(board_bus_->opaque, current_cpu_cycles_);
        sio_.advance(current_cpu_cycles_, board_bus_, cpu_cycle_hz_);
        const uint32_t reg = (physical - 0x1f801040) >> 2;
        const unsigned shift = (physical & 3) * 8;
        const uint32_t word = sio_.read(reg);
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801050 && physical + size <= 0x1f801060) {
        sio1_.advance(current_cpu_cycles_, nullptr, cpu_cycle_hz_);
        const uint32_t reg = (physical - 0x1f801050) >> 2;
        const unsigned shift = (physical & 3) * 8;
        const uint32_t word = sio1_.read(reg);
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801080 && physical <= 0x1f8010ff) {
        advance_gpu_to(current_cpu_cycles_);
        dma_.advance_to(current_cpu_cycles_, cpu_cycle_hz_);
        const unsigned shift = (physical & 3) * 8;
        const uint32_t word = dma_.read((physical - 0x1f801080) >> 2);
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801100 && physical <= 0x1f80112f) {
        timers_.advance(current_cpu_cycles_);
        const unsigned shift = (physical & 3) * 8;
        const uint32_t word = timers_.read((physical - 0x1f801100) >> 2);
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801000 && physical + size <= 0x1f801070) {
        const uint32_t reg = physical & ~3u;
        uint32_t word = 0;
        if (reg == 0x1f801000) word = expansion_base_;
        else if (reg == 0x1f801008) word = expansion_config_;
        else if (reg == 0x1f801010) word = rom_config_;
        else if (reg == 0x1f801020) word = 0;
        else if (reg == 0x1f801060) word = ram_config_;
        else {
            const size_t offset = reg - 0x1f801000;
            for (unsigned i = 0; i < 4; ++i) word |= uint32_t(internal_ram_[offset + i]) << (8 * i);
        }
        const unsigned shift = (physical & 3) * 8;
        *value = (word >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f801070 && physical <= 0x1f801077) {
        const uint32_t reg = physical < 0x1f801074 ? irq_status_ : irq_mask_;
        const unsigned shift = (physical & 3) * 8;
        *value = (reg >> shift) & (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu);
        return true;
    }
    if (physical >= 0x1f800000 && physical + size <= 0x1f800400) {
        const size_t offset = physical - 0x1f800000;
        *value = size == 4 ? read_le32_direct(scratch_.data() + offset)
                           : read_le(scratch_, offset, size);
        return true;
    }
    if (!bios_.empty() && physical >= 0x1fc00000 &&
        physical - 0x1fc00000 + size <= std::min(bios_.size(), rom_window_size())) {
        const size_t at = (physical - 0x1fc00000) % bios_.size();
        if (size == 4 && at + 4 <= bios_.size())
            *value = read_le32_direct(bios_.data() + at);
        else {
            uint32_t result = 0;
            for (unsigned i = 0; i < size; ++i)
                result |= uint32_t(bios_[(at + i) % bios_.size()]) << (i * 8);
            *value = result;
        }
        return true;
    }
    if (board_memory_bus_.read &&
        board_memory_bus_.read(board_memory_bus_.context, address, size, 0, value))
        return true;
    uint32_t result = 0;
    for (unsigned i = 0; i < size; ++i) {
        uint8_t byte = 0;
        if (!byte_read(address + i, &byte)) {
            if (board_memory_bus_.read)
                return board_memory_bus_.read(board_memory_bus_.context, address, size, 0, value);
            return false;
        }
        result |= uint32_t(byte) << (i * 8);
    }
    *value = result;
    return true;
}

bool MachineMemory::write(uint32_t address, unsigned size, uint32_t value, uint32_t pc)
{
    if (size != 1 && size != 2 && size != 4) return false;
    if (address >= 0xfffe0130 && address + size <= 0xfffe0134) {
        const unsigned shift = (address & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const uint32_t mask = bits << shift;
        biu_ = (biu_ & ~mask) | ((value & bits) << shift);
        return true;
    }
    size_t offset;
    if (ram_offset(address, &offset) && offset + size <= ram_.size()) {
        if (size == 4) write_le32_direct(ram_.data() + offset, value);
        else write_le(ram_, offset, size, value);
        return true;
    }
    const uint32_t p = address & 0x1fffffff;
    if (p >= 0x1f801c00 && p + size <= 0x1f802000) {
        advance_spu_to(current_cpu_cycles_);
        spu_.write(p - 0x1f801c00, size, value);
        return true;
    }
    if (p >= 0x1f801810 && p + size <= 0x1f801818) {
        advance_gpu_to(current_cpu_cycles_);
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const uint32_t value32 = (value & bits) << shift;
        if (p < 0x1f801814) gpu_.write_data(value32);
        else {
            gpu_.write_status(value32);
            // GP1 display-mode and reset commands can change the refresh
            // period. The deadline computed before the register write is no
            // longer valid, so re-arm from the preserved beam/frame phase.
            next_gpu_event_cycle_ = current_cpu_cycles_ +
                gpu_.cycles_until_screen_event(cpu_cycle_hz_);
        }
        return true;
    }
    if (p >= 0x1f801820 && p + size <= 0x1f801828) {
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        mdec_.write((p - 0x1f801820) >> 2, (value & bits) << shift);
        return true;
    }
    if (p >= 0x1f801040 && p + size <= 0x1f801050) {
        if (board_bus_ && board_bus_->advance_serial)
            board_bus_->advance_serial(board_bus_->opaque, current_cpu_cycles_);
        sio_.advance(current_cpu_cycles_, board_bus_, cpu_cycle_hz_);
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        sio_.write((p - 0x1f801040) >> 2,
                   (value & bits) << shift, bits << shift, board_bus_);
        return true;
    }
    if (p >= 0x1f801050 && p + size <= 0x1f801060) {
        sio1_.advance(current_cpu_cycles_, nullptr, cpu_cycle_hz_);
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        sio1_.write((p - 0x1f801050) >> 2,
                    (value & bits) << shift, bits << shift, nullptr);
        return true;
    }
    if (p >= 0x1f801080 && p <= 0x1f8010ff) {
        advance_gpu_to(current_cpu_cycles_);
        advance_spu_to(current_cpu_cycles_);
        dma_.advance_to(current_cpu_cycles_, cpu_cycle_hz_);
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const unsigned offset = (p - 0x1f801080) >> 2;
        const uint32_t old = dma_.read(offset);
        dma_.write(offset, (old & ~(bits << shift)) | ((value & bits) << shift));
        return true;
    }
    if (p >= 0x1f801100 && p <= 0x1f80112f) {
        timers_.advance(current_cpu_cycles_);
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const unsigned offset = (p - 0x1f801100) >> 2;
        const uint32_t old = timers_.peek(offset);
        timers_.write(offset, (old & ~(bits << shift)) | ((value & bits) << shift));
        return true;
    }
    if (p >= 0x1f801000 && p + size <= 0x1f801070) {
        const uint32_t reg = p & ~3u;
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const uint32_t mask = bits << shift;
        const uint32_t incoming = (value & bits) << shift;
        uint32_t *target = nullptr;
        if (reg == 0x1f801000) target = &expansion_base_;
        else if (reg == 0x1f801008) target = &expansion_config_;
        else if (reg == 0x1f801010) target = &rom_config_;
        else if (reg == 0x1f801060) target = &ram_config_;
        if (target) {
            *target = (*target & ~mask) | incoming;
            if (reg == 0x1f801000) *target = 0x1f000000 | (*target & 0x00ffffff);
            if (reg == 0x1f801008) *target &= 0xaf1fffff;
            if (reg == 0x1f801060)
                visible_ram_bytes_ = visible_ram_size(ram_config_, ram_.size());
        } else if (reg != 0x1f801020) {
            const size_t base = reg - 0x1f801000;
            for (unsigned i = 0; i < size; ++i)
                internal_ram_[base + (p & 3) + i] = uint8_t(value >> (8 * i));
        }
        return true;
    }
    if (p >= 0x1f801070 && p <= 0x1f801077) {
        uint32_t &reg = p < 0x1f801074 ? irq_status_ : irq_mask_;
        const unsigned shift = (p & 3) * 8;
        const uint32_t bits = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        const uint32_t mask = bits << shift;
        const uint32_t incoming = (value & bits) << shift;
        if (&reg == &irq_status_) {
            // Match psxirq_device::write(): status bits written as one remain
            // pending only while their source is enabled in I_MASK.
            reg = (reg & ~mask) | (reg & irq_mask_ & incoming & mask);
        } else {
            reg = (reg & ~mask) | incoming;
        }
        sync_irq_line();
        return true;
    }
    if (p >= 0x1f800000 && p + size <= 0x1f800400) {
        const size_t offset = p - 0x1f800000;
        if (size == 4) write_le32_direct(scratch_.data() + offset, value);
        else write_le(scratch_, offset, size, value);
        return true;
    }
    // The PSX maps BIOS as read-only ROM. Writes are ignored by the bus
    // mapping; they must not become CPU DBE exceptions.
    if (!bios_.empty() && p >= 0x1fc00000 &&
        p - 0x1fc00000 + size <= std::min(bios_.size(), rom_window_size())) return true;
    if (board_memory_bus_.write &&
        board_memory_bus_.write(board_memory_bus_.context, address, size, pc, value)) return true;
    if (p >= 0x1f000000 && p < 0x1f900000) {
        return true;
    }
    return false;
}

} // namespace multiarcaderecomp::psx
