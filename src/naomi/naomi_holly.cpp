#include "naomi_holly.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <cstdio>
#include <cstdlib>

namespace mamerecomp::naomi {

namespace {
constexpr std::array<unsigned, 3> kLevels{2, 4, 6};
constexpr std::array<unsigned, 3> kSh4Irl{13, 11, 9};
constexpr std::uint32_t kHollyMapleDma = 1u << 12;
constexpr std::uint32_t kHollySysctrlStart = 0x005f6800;
constexpr std::uint32_t kHollySbRevision = 0x005f689c;
constexpr std::uint32_t kHollySbRevisionValue = 0x0000000b;
constexpr std::uint32_t kHollySysctrlEnd = 0x005f6a00;
constexpr std::array<std::uint32_t, 3> kStatusAddress{0x005f6900, 0x005f6904, 0x005f6908};
constexpr std::array<std::array<std::uint32_t, 3>, 3> kMaskAddress{{
    {0x005f6910, 0x005f6914, 0x005f6918},
    {0x005f6920, 0x005f6924, 0x005f6928},
    {0x005f6930, 0x005f6934, 0x005f6938}}};
constexpr unsigned kMapleIllegalAddress = 8;
constexpr unsigned kMapleOverrun = 9;
constexpr unsigned kMapleIllegalCommand = 11;
constexpr std::uint32_t kMaxDescriptors = 4096;
constexpr std::uint32_t kMaxFrameWords = 256;
constexpr std::uint32_t kC2Dstat = 0x005f6800;
constexpr std::uint32_t kC2Dlen = 0x005f6804;
constexpr std::uint32_t kC2Dst = 0x005f6808;
constexpr std::uint32_t kSh4Sar2 = 0xffa00020;
constexpr std::uint32_t kSh4Dmatcr2 = 0xffa00028;
constexpr std::uint32_t kSh4Chcr2 = 0xffa0002c;
constexpr unsigned kC2DmaInterrupt = 19;
constexpr std::array<std::uint32_t, 8> kSh4DmaUnitBytes{8, 1, 2, 4, 32, 0, 0, 0};
std::uint64_t g_maple_trace_requests = 0;
std::uint64_t g_maple_post_firmware_trace_requests = 0;
bool g_maple_firmware_upload_finished = false;
}

HollyInterrupts::HollyInterrupts(IrqCallback callback) : m_irq(std::move(callback)) {
    reset();
}

std::size_t HollyInterrupts::groupIndex(Group group) {
    return static_cast<std::size_t>(group);
}

void HollyInterrupts::reset() {
    m_status.fill(0);
    for (auto& group_masks : m_masks) group_masks.fill(0);
    m_lines.fill(false);
    if (m_irq) {
        for (const auto irl : kSh4Irl) m_irq(irl, false);
    }
}

void HollyInterrupts::raise(Group group, unsigned bit) {
    if (bit >= 32) return;
    m_status[groupIndex(group)] |= 1u << bit;
    updateLines();
}

void HollyInterrupts::clear(Group group, std::uint32_t bits) {
    m_status[groupIndex(group)] &= ~bits;
    updateLines();
}

void HollyInterrupts::setMask(unsigned level, Group group, std::uint32_t value) {
    const auto it = std::find(kLevels.begin(), kLevels.end(), level);
    if (it == kLevels.end()) return;
    m_masks[static_cast<std::size_t>(it - kLevels.begin())][groupIndex(group)] = value;
    updateLines();
}

std::uint32_t HollyInterrupts::status(Group group) const {
    return m_status[groupIndex(group)];
}

std::uint32_t HollyInterrupts::normalStatusWithSummary() const {
    auto value = m_status[groupIndex(Group::normal)] & 0x3fffffffu;
    if (m_status[groupIndex(Group::external)] != 0) value |= 0x40000000u;
    if (m_status[groupIndex(Group::error)] != 0) value |= 0x80000000u;
    return value;
}

std::uint32_t HollyInterrupts::mask(unsigned level, Group group) const {
    const auto it = std::find(kLevels.begin(), kLevels.end(), level);
    if (it == kLevels.end()) return 0;
    return m_masks[static_cast<std::size_t>(it - kLevels.begin())][groupIndex(group)];
}

bool HollyInterrupts::handles(std::uint32_t address) const {
    return address >= kHollySysctrlStart && address < kHollySysctrlEnd;
}

std::uint32_t HollyInterrupts::readRegister(std::uint32_t address) const {
    const auto original_address = address;
    address &= ~3u;
    std::uint32_t value = 0;
    for (std::size_t group = 0; group < kStatusAddress.size(); ++group) {
        if (address == kStatusAddress[group]) {
            value = group == groupIndex(Group::normal) ? normalStatusWithSummary()
                                                       : m_status[group];
            break;
        }
    }
    for (std::size_t level = 0; level < kMaskAddress.size(); ++level)
        for (std::size_t group = 0; group < kMaskAddress[level].size(); ++group)
            if (address == kMaskAddress[level][group]) value = m_masks[level][group];
    if (address == kHollySbRevision) value = kHollySbRevisionValue;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_SYSCTRL"))
        std::fprintf(stderr, "Naomi sysctrl R [%08x] -> %08x\n", original_address, value);
    return value;
}

void HollyInterrupts::writeRegister(std::uint32_t address, std::uint32_t value) {
    const auto original_address = address;
    address &= ~3u;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_SYSCTRL"))
        std::fprintf(stderr, "Naomi sysctrl W [%08x] <- %08x\n", original_address, value);
    for (std::size_t group = 0; group < kStatusAddress.size(); ++group) {
        if (address != kStatusAddress[group]) continue;
        if (group == groupIndex(Group::external)) return; // device clears its own external IRQ
        const auto writable = group == groupIndex(Group::normal) ? value & 0x3fffffffu : value;
        clear(static_cast<Group>(group), writable); // status bits are write-one-to-clear
        return;
    }
    for (std::size_t level = 0; level < kMaskAddress.size(); ++level)
        for (std::size_t group = 0; group < kMaskAddress[level].size(); ++group)
            if (address == kMaskAddress[level][group]) {
                setMask(kLevels[level], static_cast<Group>(group), value);
                return;
            }
}

void HollyInterrupts::updateLines() {
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        bool pending = false;
        for (std::size_t group = 0; group < m_status.size(); ++group)
            pending |= (m_status[group] & m_masks[i][group]) != 0;
        if (pending == m_lines[i]) continue;
        m_lines[i] = pending;
        if (m_irq) m_irq(kSh4Irl[i], pending);
    }
}

MapleDma::MapleDma(MapleMemory memory, MapleEndpoint endpoint, HollyInterrupts& interrupts)
    : m_memory(std::move(memory)), m_endpoint(std::move(endpoint)), m_interrupts(interrupts) {
    reset();
}

void MapleDma::reset() {
    m_mdstar = 0;
    m_mdt_sel = 0;
    m_mden = 0;
    m_mdst = 0;
    m_msys = 0x3a980000;
    m_mmsel = 1;
}

std::uint32_t MapleDma::readRegister(std::uint32_t address) const {
    switch (address) {
    case kMdstar: return m_mdstar;
    case kMdtsel: return m_mdt_sel;
    case kMden: return m_mden;
    case kMdst: return m_mdst;
    case kMsys: return m_msys;
    case kMmsel: return m_mmsel;
    default: return 0;
    }
}

void MapleDma::writeRegister(std::uint32_t address, std::uint32_t value) {
    switch (address) {
    case kMdstar:
        m_mdstar = value & 0x1fffffe0u;
        break;
    case kMdtsel:
        m_mdt_sel = value;
        break;
    case kMden:
        m_mden = value & 1u;
        if (m_mden == 0) m_mdst = 0;
        break;
    case kMdst:
        if ((value & 1u) != 0 && m_mden != 0) {
            m_mdst = 1;
            runTable();
        }
        break;
    case kMsys:
        m_msys = value;
        break;
    case kMshtcl:
        // Hard-trigger pending state is consumed by the board's VBlank timing
        // adapter. The register itself is write-only on the Naomi map.
        break;
    case kMmsel:
        m_mmsel = value & 1u;
        break;
    default:
        break;
    }
}

void MapleDma::onVblank() {
    if (m_mden != 0 && m_mdt_sel == 1 && m_mdst == 0) {
        m_mdst = 1;
        runTable();
    }
}

bool MapleDma::fail(unsigned error_bit) {
    m_mdst = 0;
    m_interrupts.raise(HollyInterrupts::Group::error, error_bit);
    return false;
}

std::uint32_t MapleDma::swap32(std::uint32_t value) {
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}

bool MapleDma::runTable() {
    if (!m_memory.read32 || !m_memory.write32) return fail(kMapleIllegalAddress);

    auto address = m_mdstar;
    const bool swap_msb = (m_mmsel == 0);
    for (std::uint32_t descriptor = 0; descriptor < kMaxDescriptors; ++descriptor) {
        std::uint32_t header = 0, destination = 0;
        if (!m_memory.read32(address, header) || !m_memory.read32(address + 4, destination))
            return fail(kMapleIllegalAddress);
        destination &= 0x1fffffe0u;
        const bool last = (header >> 31) != 0;
        const unsigned pattern = (header >> 8) & 7u;
        const unsigned bus = (header >> 16) & 3u;

        switch (pattern) {
        case 0: { // START: frame follows the two-word DMA descriptor.
            const std::uint32_t frame_words = (header & 0xffu) + 1u;
            if (frame_words == 0 || frame_words > kMaxFrameWords)
                return fail(kMapleOverrun);
            std::vector<std::uint32_t> request(frame_words);
            for (std::uint32_t i = 0; i < frame_words; ++i) {
                if (!m_memory.read32(address + 8 + i * 4, request[i]))
                    return fail(kMapleIllegalAddress);
                if (swap_msb) request[i] = swap32(request[i]);
            }
            std::vector<std::uint32_t> response;
            if (m_endpoint) response = m_endpoint(bus, request);
            if (response.empty()) response.push_back(0xffffffffu);
            const auto maple_frame = request.empty() ? 0u : request[0];
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_MAPLE") &&
                g_maple_trace_requests < 64) {
                ++g_maple_trace_requests;
                std::fprintf(stderr,
                             "Naomi Maple DMA #%llu descriptor=%u bus=%u header=%08x destination=%08x frame=%08x command=%02x payload_words=%u data0=%08x data1=%08x response_words=%zu response0=%08x\n",
                             static_cast<unsigned long long>(g_maple_trace_requests),
                             descriptor, bus, header, destination, maple_frame,
                             maple_frame & 0xffu, (maple_frame >> 24) & 0xffu,
                             request.size() > 1 ? request[1] : 0u,
                             request.size() > 2 ? request[2] : 0u,
                             response.size(), response.empty() ? 0u : response[0]);
            }
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_MAPLE") &&
                g_maple_firmware_upload_finished &&
                g_maple_post_firmware_trace_requests < 64) {
                ++g_maple_post_firmware_trace_requests;
                std::fprintf(stderr,
                             "Naomi Maple post-firmware #%llu descriptor=%u bus=%u destination=%08x frame=%08x command=%02x payload_words=%u data0=%08x data1=%08x response_words=%zu response0=%08x\n",
                             static_cast<unsigned long long>(g_maple_post_firmware_trace_requests),
                             descriptor, bus, destination, maple_frame,
                             maple_frame & 0xffu, (maple_frame >> 24) & 0xffu,
                             request.size() > 1 ? request[1] : 0u,
                             request.size() > 2 ? request[2] : 0u,
                             response.size(), response.empty() ? 0u : response[0]);
            }
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_MAPLE") &&
                (maple_frame & 0xffu) == 0x80u && request.size() > 1 &&
                ((request[1] >> 8) & 0xffu) == 0xffu) {
                g_maple_firmware_upload_finished = true;
                std::fprintf(stderr,
                             "Naomi Maple firmware upload terminator descriptor=%u destination=%08x data0=%08x response_words=%zu response0=%08x\n",
                             descriptor, destination, request[1], response.size(), response[0]);
            }
            if (response.size() > kMaxFrameWords) return fail(kMapleOverrun);
            for (std::size_t i = 0; i < response.size(); ++i) {
                const auto word = swap_msb ? swap32(response[i]) : response[i];
                if (!m_memory.write32(destination + static_cast<std::uint32_t>(i * 4), word))
                    return fail(kMapleOverrun);
            }
            address += (2u + frame_words) * 4u;
            break;
        }
        case 2: // SDCKB occupy permission
        case 3: // bus reset
        case 4: // SDCKB occupy cancel
        case 7: // NOP
            address += 4u;
            break;
        default:
            return fail(kMapleIllegalCommand);
        }
        if (last) {
            m_mdst = 0;
            m_interrupts.raise(HollyInterrupts::Group::normal, 12);
            return true;
        }
    }
    return fail(kMapleOverrun); // no last descriptor within the bounded table
}

HollyC2Dma::HollyC2Dma(HollyC2DmaBus bus) : m_bus(std::move(bus)) {}

bool HollyC2Dma::handles(std::uint32_t address) const {
    address &= ~3u;
    return address == kC2Dstat || address == kC2Dlen || address == kC2Dst;
}

std::uint32_t HollyC2Dma::readRegister(std::uint32_t address) const {
    switch (address & ~3u) {
    case kC2Dstat: return m_destination;
    case kC2Dlen: return m_length;
    case kC2Dst: return m_start;
    default: return 0;
    }
}

void HollyC2Dma::writeRegister(std::uint32_t address, std::uint32_t value) {
    switch (address & ~3u) {
    case kC2Dstat:
        m_destination = value;
        return;
    case kC2Dlen:
        m_length = value;
        return;
    case kC2Dst: {
        const auto previous = m_start;
        m_start = value;
        if ((previous & 1u) == 0 && (value & 1u) != 0) start();
        return;
    }
    default:
        return;
    }
}

void HollyC2Dma::start() {
    // MAME dc_state::dc_sysctrl_w derives the PVR bus destination from
    // SB_C2DSTAT, then invokes the SH-4 DDT path on channel 2. Mode 25 reads
    // SAR2, selects an incrementing destination, and uses the guest's CHCR2
    // transfer width. Flycast's DMAC_Ch2St follows the same source/dest/len
    // fields for the direct list and texture paths.
    const auto source_reg = m_bus.read ? m_bus.read(kSh4Sar2, 4) : 0;
    const auto count_reg = m_bus.read ? m_bus.read(kSh4Dmatcr2, 4) : 0;
    const auto chcr = m_bus.read ? m_bus.read(kSh4Chcr2, 4) : 0;
    const auto source = source_reg & 0x1fffffe0u;
    const auto destination = (m_destination & 0x03ffffe0u) | 0x10000000u;
    const auto length = m_length == 0 ? 0x01000000u : m_length;
    const auto unit_bytes = kSh4DmaUnitBytes[(chcr >> 4) & 7u];
    const auto transfer_bytes = static_cast<std::uint64_t>(count_reg) * unit_bytes;
    const bool valid_transfer = unit_bytes != 0 && transfer_bytes == length &&
                                static_cast<bool>(m_bus.read) &&
                                static_cast<bool>(m_bus.write);

    // DDT mode 25 forces destination increment while retaining CHCR2's source
    // increment and transfer size. The SH-4 DMA core masks invalid increment
    // mode 3; mirror that by declining the payload while retaining completion.
    const auto source_mode = (chcr >> 12) & 3u;
    const auto destination_mode = 1u;
    const bool increments_valid = source_mode != 3u;
    std::uint32_t src = source;
    std::uint32_t dst = destination;

    if (valid_transfer && increments_valid) {
        const auto alignment = unit_bytes == 32 ? 32u : unit_bytes;
        if (alignment > 1) {
            src &= ~(alignment - 1u);
            dst &= ~(alignment - 1u);
        }
        const auto count = count_reg == 0 ? 0x01000000u : count_reg;
        for (std::uint32_t unit = 0; unit < count; ++unit) {
            if (source_mode == 2u) src -= unit_bytes;
            if (destination_mode == 2u) dst -= unit_bytes;
            for (std::uint32_t offset = 0; offset < unit_bytes;) {
                const auto width = std::min(4u, unit_bytes - offset);
                const auto word = m_bus.read(src + offset, width);
                m_bus.write(dst + offset, width, word);
                offset += width;
            }
            if (source_mode == 1u) src += unit_bytes;
            if (destination_mode == 1u) dst += unit_bytes;
        }
    }

    // MAME's Naomi channel-2 path updates the destination pointer immediately,
    // then clears C2DLEN/C2DST and raises ISTNRM.CH2_DMA after len/4 SH-4 cycles.
    if ((destination & 0x01000000u) != 0) m_destination = destination + length;
    else m_destination = destination;
    m_cycles_until_irq = std::max(1u, length / 4u);

    if (std::getenv("MAMERECOMP_NAOMI_TRACE_VIDEO_SETUP"))
        std::fprintf(stderr,
                     "Naomi C2 DMA start src=%08x dst=%08x len=%08x SAR2=%08x DMATCR2=%08x CHCR2=%08x unit=%u copied=%u irq_cycles=%u\n",
                     source, destination, length, source_reg, count_reg, chcr,
                     unit_bytes, valid_transfer && increments_valid ? 1u : 0u,
                     m_cycles_until_irq);
}

void HollyC2Dma::complete() {
    m_length = 0;
    m_start = 0;
    m_cycles_until_irq = 0;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_VIDEO_SETUP"))
        std::fprintf(stderr, "Naomi C2 DMA complete dst=%08x ISTNRM.bit=%u\n",
                     m_destination, kC2DmaInterrupt);
    if (m_bus.raise_normal_irq) m_bus.raise_normal_irq(kC2DmaInterrupt);
}

void HollyC2Dma::advanceCycles(std::uint32_t cycles) {
    if (m_cycles_until_irq == 0) return;
    if (cycles >= m_cycles_until_irq) complete();
    else m_cycles_until_irq -= cycles;
}

} // namespace mamerecomp::naomi
