#include "mk2/dcs2k.hpp"

#include "core/cpus/adsp2105/runtime.hpp"
#include "core/cpus/adsp2105/dag.hpp"

#include <algorithm>
#include <iterator>
#include <limits>

namespace mamerecomp::mk2 {
namespace {
constexpr std::uint16_t kInputEmpty = 0x0800;
constexpr std::uint16_t kOutputEmpty = 0x0400;
constexpr std::uint16_t kIrq2 = 2;
constexpr std::uint16_t kDataRamEnd = 0x0800;
constexpr std::uint16_t kRomBase = 0x2000;
constexpr std::uint16_t kBankSelectBase = 0x3000;
constexpr std::uint16_t kLatchBase = 0x3400;
constexpr std::uint16_t kLocalRamBase = 0x3800;
constexpr std::uint16_t kControlBase = 0x3fe0;
constexpr std::uint16_t kProgramFlagData = 5;
constexpr std::uint16_t kProgramFlagControl = 6;
constexpr std::uint16_t kSport1Autobuffer = 15;
constexpr std::uint16_t kSport1Sclkdiv = 17;
constexpr std::uint16_t kSport1Control = 18;
constexpr std::uint16_t kSystemControl = 31;
}

Dcs2K::Dcs2K(std::span<const std::uint8_t> sound_region)
    : m_sound_region(sound_region.begin(), sound_region.end()),
      m_internal_program(0x400), m_external_program(0x1800),
      m_local_ram(0x200) {
    m_control[0] = 0;
    boot();
}

mamerecomp::adsp2105::State& Dcs2K::cpu() noexcept { return m_cpu; }
const mamerecomp::adsp2105::State& Dcs2K::cpu() const noexcept { return m_cpu; }

mamerecomp::adsp2105::Bus Dcs2K::cpu_bus() noexcept {
    return {this, program_read, program_write, data_read, data_write,
            io_read, io_write, sport_tx, sport_rx, mutable_program};
}

std::uint16_t Dcs2K::host_status() const noexcept {
    // T-Unit mk2_state::dcs_state_r returns DCS control_r() >> 4.
    return static_cast<std::uint16_t>(((m_input_full ? 0 : kInputEmpty) |
                                       (m_output_full ? 0 : kOutputEmpty)) >> 4);
}

std::uint16_t Dcs2K::host_data_read() noexcept {
    ++m_host_reads;
    const std::uint16_t result = m_output_latch & 0x00ff;
    if (m_output_full) acknowledge_output();
    return result;
}

void Dcs2K::host_data_write(std::uint16_t data) noexcept {
    // The MK2 TMS map ignores the low mirrored word (offset 0); the caller
    // must pass the selected high word and the 16-bit write value.
    set_reset((data & 0x0100) != 0);
    m_input_latch = data & 0x00ff;
    m_input_full = true;
    m_cpu.irq[2] = true;
    m_cpu.irq_latched[2] = true;
    ++m_host_commands;
}

void Dcs2K::set_reset(bool released) noexcept {
    if (!released) {
        m_reset_asserted = true;
        m_sound_bank = 0;
        m_input_full = false;
        m_output_full = false;
        m_cpu.irq[2] = false;
        m_cpu.irq_latched[2] = false;
        std::fill(std::begin(m_control), std::end(m_control), 0);
        m_dma_active = false;
        boot();
        return;
    }
    m_reset_asserted = false;
}

mamerecomp::adsp2105::BlockExit Dcs2K::run_until(std::uint64_t cycle_limit) noexcept {
    if (m_reset_asserted) return mamerecomp::adsp2105::BlockExit::Idle;
    auto bus = cpu_bus();
    const auto start = m_cpu.cycles;
    const auto result = mamerecomp::adsp2105::run_until(m_cpu, bus, cycle_limit);
    advance_cycles(m_cpu.cycles - start);
    return result;
}

std::vector<std::int16_t> Dcs2K::take_samples() noexcept {
    std::vector<std::int16_t> out;
    out.swap(m_samples);
    return out;
}

void Dcs2K::advance_cycles(std::uint64_t cycles) noexcept {
    if (!m_dma_active || !m_dma_period_cycles || !m_dma_sample_count) return;
    m_dma_cycle_phase += cycles;
    while (m_dma_cycle_phase >= m_dma_period_cycles) {
        m_dma_cycle_phase -= m_dma_period_cycles;
        std::uint32_t address = m_cpu.i[m_dma_ireg] & 0x3fff;
        for (std::uint32_t n = 0; n < m_dma_sample_count; ++n) {
            const auto sample = static_cast<std::int16_t>(read_data(address));
            m_samples.push_back(sample);
            address = (address + m_dma_increment) & 0x3fff;
        }
        m_sport_words += m_dma_sample_count;
        const std::uint32_t end = m_dma_base + m_dma_length;
        if (address >= end) {
            address = m_dma_base;
            // MAME pulses ADSP2105 IRQ1 at each autobuffer wrap.
            m_cpu.irq[1] = false;
            m_cpu.irq_latched[1] = true;
        }
        m_cpu.i[m_dma_ireg] = address;
    }
}

DcsDiagnostics Dcs2K::diagnostics() const noexcept {
    return {m_host_commands, m_host_reads, m_input_reads, m_output_writes,
            m_boot_count, m_sport_words, m_sound_bank, m_input_latch,
            m_output_latch, m_input_full, m_output_full, m_reset_asserted};
}

std::uint32_t Dcs2K::program_read(void* opaque, std::uint16_t address) noexcept {
    return static_cast<Dcs2K*>(opaque)->read_program(address);
}
void Dcs2K::program_write(void* opaque, std::uint16_t address,
                          std::uint32_t value) noexcept {
    static_cast<Dcs2K*>(opaque)->write_program(address, value);
}
std::uint16_t Dcs2K::data_read(void* opaque, std::uint16_t address) noexcept {
    return static_cast<Dcs2K*>(opaque)->read_data(address);
}
void Dcs2K::data_write(void* opaque, std::uint16_t address,
                       std::uint16_t value) noexcept {
    static_cast<Dcs2K*>(opaque)->write_data(address, value);
}
std::uint16_t Dcs2K::io_read(void* opaque, std::uint16_t address) noexcept {
    auto& self = *static_cast<Dcs2K*>(opaque);
    return static_cast<std::uint16_t>(self.m_control[address & 31]);
}
void Dcs2K::io_write(void* opaque, std::uint16_t address,
                      std::uint16_t value) noexcept {
    auto& self = *static_cast<Dcs2K*>(opaque);
    self.m_control[address & 31] = value;
}
void Dcs2K::sport_tx(void* opaque, unsigned port, std::uint32_t value) noexcept {
    auto& self = *static_cast<Dcs2K*>(opaque);
    if (port != 1) return;
    (void)value;
    if (!self.m_sport1_enabled || !self.m_sport1_autobuffer) {
        self.m_dma_active = false;
        return;
    }
    const auto autobuffer = self.m_control[kSport1Autobuffer];
    self.m_dma_ireg = static_cast<std::uint8_t>((autobuffer >> 9) & 7);
    const unsigned mreg = ((autobuffer >> 7) & 3) | (self.m_dma_ireg & 4);
    const std::int32_t increment = self.m_cpu.m[mreg];
    self.m_dma_length = self.m_cpu.l[self.m_dma_ireg];
    self.m_dma_increment = static_cast<std::uint32_t>(increment > 0 ? increment : 1);
    auto source = static_cast<std::uint16_t>(self.m_cpu.i[self.m_dma_ireg] & ~0x0f);
    mamerecomp::adsp2105::dag_write_i(self.m_cpu, self.m_dma_ireg, source);
    self.m_dma_base = self.m_cpu.base[self.m_dma_ireg];
    self.m_dma_sample_count = self.m_dma_length /
        (2u * std::max(1u, self.m_dma_increment));
    const std::uint32_t count = std::max(1u, self.m_dma_sample_count);
    const std::uint64_t sample_period_cycles =
        (self.m_control[kSport1Control] & 0x4000)
            ? 2ull * (self.m_control[kSport1Sclkdiv] + 1ull) * 16ull
            : 10'000'000ull / 31'250ull;
    self.m_dma_period_cycles = std::max<std::uint64_t>(1,
        sample_period_cycles * count);
    self.m_dma_cycle_phase = 0;
    self.m_dma_active = self.m_dma_length != 0 && increment > 0;
}
std::uint32_t Dcs2K::sport_rx(void*, unsigned) noexcept { return 0; }
bool Dcs2K::mutable_program(void* opaque, std::uint16_t address) noexcept {
    const auto& self = *static_cast<Dcs2K*>(opaque);
    address &= mamerecomp::adsp2105::pc_mask;
    return address < self.m_internal_program.size() ||
           (address >= 0x0800 && address < 0x2000);
}

std::uint32_t Dcs2K::read_program(std::uint16_t address) const noexcept {
    address &= mamerecomp::adsp2105::pc_mask;
    if (address < m_internal_program.size()) return m_internal_program[address];
    if (address >= 0x0800 && address < 0x2000)
        return m_external_program[address - 0x0800];
    return 0x00ffffff;
}

void Dcs2K::write_program(std::uint16_t address, std::uint32_t value) noexcept {
    address &= mamerecomp::adsp2105::pc_mask;
    value &= 0x00ffffff;
    if (address < m_internal_program.size()) m_internal_program[address] = value;
    else if (address >= 0x0800 && address < 0x2000)
        m_external_program[address - 0x0800] = value;
}

std::uint16_t Dcs2K::read_data(std::uint16_t address) noexcept {
    address &= mamerecomp::adsp2105::pc_mask;
    if (address < kDataRamEnd || (address >= 0x1800 && address < 0x2000)) {
        const auto offset = address & 0x07ff;
        return static_cast<std::uint16_t>(m_external_program[offset] >> 8);
    }
    if (address >= kRomBase && address < 0x3000) {
        const std::size_t word = static_cast<std::size_t>(m_sound_bank) * 0x1000 +
                                 (address - kRomBase);
        const std::size_t byte = word * 2;
        if (byte + 1 < m_sound_region.size())
            return static_cast<std::uint16_t>(m_sound_region[byte] |
                    (static_cast<std::uint16_t>(m_sound_region[byte + 1]) << 8));
        return 0xffff;
    }
    if (address >= kLatchBase && address < 0x3800) {
        ++m_input_reads;
        const auto result = m_input_latch;
        acknowledge_input();
        return result;
    }
    if (address >= kLocalRamBase && address < 0x3a00)
        return m_local_ram[address - kLocalRamBase];
    if (address >= kControlBase)
    {
        const auto index = address - kControlBase;
        if (index == kProgramFlagData) {
            const auto value = static_cast<std::uint16_t>(
                (m_control[kProgramFlagControl] & m_control[kProgramFlagData]) |
                (m_progflags & ~m_control[kProgramFlagControl]));
            m_progflags ^= 0x0006;
            return value;
        }
        return m_control[index];
    }
    return 0xffff;
}

void Dcs2K::write_data(std::uint16_t address, std::uint16_t value) noexcept {
    address &= mamerecomp::adsp2105::pc_mask;
    if (address < kDataRamEnd || (address >= 0x1800 && address < 0x2000)) {
        const auto offset = address & 0x07ff;
        m_external_program[offset] = (m_external_program[offset] & 0x000000ffu) |
                                    (static_cast<std::uint32_t>(value & 0xff) << 8);
        return;
    }
    if (address >= kBankSelectBase && address < 0x3400) {
        const std::size_t banks = m_sound_region.size() / (0x1000 * 2);
        m_sound_bank = banks ? static_cast<std::uint16_t>((value & 0x07ff) % banks) : 0;
        return;
    }
    if (address >= kLatchBase && address < 0x3800) {
        m_output_latch = value;
        m_output_full = true;
        ++m_output_writes;
        return;
    }
    if (address >= kLocalRamBase && address < 0x3a00) {
        m_local_ram[address - kLocalRamBase] = value;
        return;
    }
    if (address >= kControlBase) {
        const auto index = static_cast<std::uint16_t>(address - kControlBase);
        m_control[index] = value;
        if (index == kSystemControl) {
            m_sport1_enabled = (value & 0x0800) != 0;
            if (value & 0x0200) {
                std::fill(std::begin(m_control), std::end(m_control), 0);
                boot();
            }
        } else if (index == kSport1Autobuffer) {
            m_sport1_autobuffer = (value & 0x0002) != 0;
        } else if (index == kProgramFlagData) {
            m_progflags ^= 0x0006;
        } else if (index == kProgramFlagControl) {
            // Control mask is retained in the register array.
        } else if (index == kSport1Control) {
            // DCS 2K's SPORT1 timing is serviced by the board audio scheduler.
        }
    }
}

void Dcs2K::boot() noexcept {
    if (m_sound_region.empty()) return;
    const std::size_t words = m_sound_region.size() / 2;
    const std::size_t banks = words / 0x1000;
    if (!banks) return;
    m_sound_bank %= static_cast<std::uint16_t>(banks);
    const std::size_t base_word = static_cast<std::size_t>(m_sound_bank) * 0x1000;
    std::uint8_t buffer[0x1000]{};
    for (std::size_t i = 0; i < sizeof(buffer); ++i) {
        const std::size_t byte = (base_word + i) * 2;
        if (byte < m_sound_region.size()) buffer[i] = m_sound_region[byte];
    }
    const std::size_t count = (static_cast<std::size_t>(buffer[3]) + 1) * 8;
    const std::size_t bounded_count = std::min(count, m_internal_program.size());
    for (std::size_t i = 0; i < bounded_count; ++i) {
        const std::size_t offset = i * 4;
        m_internal_program[i] = (static_cast<std::uint32_t>(buffer[offset]) << 16) |
                                (static_cast<std::uint32_t>(buffer[offset + 1]) << 8) |
                                buffer[offset + 2];
    }
    m_cpu = {};
    m_cpu.loop = 0xffff;
    m_cpu.sstat = 0x55;
    ++m_boot_count;
}

void Dcs2K::acknowledge_input() noexcept {
    m_input_full = false;
    m_cpu.irq[2] = false;
    m_cpu.irq_latched[2] = false;
}
void Dcs2K::acknowledge_output() noexcept { m_output_full = false; }

} // namespace mamerecomp::mk2
