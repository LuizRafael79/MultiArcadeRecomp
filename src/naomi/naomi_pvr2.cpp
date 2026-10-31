#include "naomi_pvr2.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace mamerecomp::naomi {
namespace {
constexpr std::uint32_t kSh4Clock = 200'000'000;
constexpr std::uint32_t kPixelClock = 27'000'000;
constexpr std::uint32_t kPvrIfStart = 0x005f7c00;
constexpr std::uint32_t kPvrIfEnd = 0x005f7d00;
constexpr std::uint32_t kPvrCoreStart = 0x005f8000;
constexpr std::uint32_t kPvrCoreEnd = 0x005fa000;
constexpr std::uint32_t kArea1Start = 0x04000000;
constexpr std::uint32_t kArea1End = 0x06000000;
constexpr std::uint32_t kTaAreaStart = 0x10000000;
constexpr std::uint32_t kTaAreaEnd = 0x14000000;
constexpr std::uint32_t kTaListInit = 0x005f8144;
constexpr std::uint32_t kTaListContinue = 0x005f8160;
constexpr std::uint32_t kPvrSoftReset = 0x005f80e8;
unsigned g_pvr_trace_writes = 0;
unsigned g_pvr_trace_submits = 0;
unsigned g_pvr_trace_ta_init_reads = 0;
unsigned g_pvr_trace_ta_eols = 0;
}

NaomiPvr2::NaomiPvr2() : m_vram(kVramBytes) {
    m_ta_stream.reserve(1024 * 1024);
    // Flycast's PVR reset defaults: NTSC timing, 858 clocks x 263 lines,
    // VBlank IRQs at lines 260 (in) and 21 (out).
    // The game reads these identity registers during renderer startup and
    // rejects the PVR if either value is missing.
    m_registers[0x00u / 4] = 0x17fd11dbu; // ID
    m_registers[0x04u / 4] = 0x00000011u; // REVISION
    m_registers[0x08u / 4] = 0x00000007u; // SOFTRESET
    m_registers[(0x44u) / 4] = 0;
    m_registers[(0xccu) / 4] = 0x00150104u;
    m_registers[(0xd0u) / 4] = 0;
    m_registers[(0xd8u) / 4] = 0x01060359u;
    m_registers[(0xdcu) / 4] = 0x01500104u;
    recalculateTiming();
}

NaomiPvr2::~NaomiPvr2() {
    const char* prefix = std::getenv("MAMERECOMP_NAOMI_DUMP_PVR_PREFIX");
    if (!prefix || !*prefix) return;

    const auto write_bytes = [prefix](const char* suffix, const std::uint8_t* data,
                                      std::size_t size) {
        const std::string path = std::string(prefix) + suffix;
        std::ofstream out(path, std::ios::binary);
        if (!out || !out.write(reinterpret_cast<const char*>(data),
                               static_cast<std::streamsize>(size))) {
            std::fprintf(stderr, "Naomi PVR dump failed path=%s bytes=%zu\n",
                         path.c_str(), size);
            return false;
        }
        std::fprintf(stderr, "Naomi PVR dump path=%s bytes=%zu\n", path.c_str(), size);
        return true;
    };
    write_bytes(".ta.bin", m_ta_stream.data(), m_ta_stream.size());
    write_bytes(".vram.bin", m_vram.data(), m_vram.size());
    const auto frame = captureFramebuffer();
    write_bytes(".scanout.rgb", frame.rgb.data(), frame.rgb.size());

    const std::string metadata_path = std::string(prefix) + ".meta.txt";
    std::ofstream metadata(metadata_path);
    if (!metadata) {
        std::fprintf(stderr, "Naomi PVR dump failed path=%s\n", metadata_path.c_str());
        return;
    }
    metadata << "width=" << frame.width << "\nheight=" << frame.height
             << "\nFB_R_CTRL=0x" << std::hex << frame.control
             << "\nFB_R_SIZE=0x" << frame.size << "\nFB_R_SOF1=0x" << frame.start
             << std::dec << "\nTA_bytes=" << m_ta_stream.size()
             << "\nVRAM_bytes=" << m_vram.size()
             << "\nTA_parse_offset=" << m_ta_parse_offset << "\n";
    std::fprintf(stderr, "Naomi PVR dump path=%s\n", metadata_path.c_str());
}

bool NaomiPvr2::handles(std::uint32_t address) const {
    return (address >= kPvrIfStart && address < kPvrIfEnd) ||
           (address >= kPvrCoreStart && address < kPvrCoreEnd) ||
           (address >= kArea1Start && address < kArea1End) ||
           (address >= kTaAreaStart && address < kTaAreaEnd);
}

std::uint32_t NaomiPvr2::read(std::uint32_t address, unsigned width) const {
    address &= ~static_cast<std::uint32_t>(width - 1);
    if (address >= kPvrIfStart && address < kPvrIfEnd) {
        const auto value = m_interface_registers[(address - kPvrIfStart) / 4];
        const auto shift = (address & 3u) * 8u;
        return width == 4 ? value : (value >> shift) & ((1u << (width * 8u)) - 1u);
    }
    if (address >= kPvrCoreStart && address < kPvrCoreEnd) {
        if (address == kTaListInit) {
            // MAME powervr2_device::ta_list_init_r and Flycast's PVR register
            // handler both expose this command register as zero on readback.
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") &&
                g_pvr_trace_ta_init_reads < 64) {
                ++g_pvr_trace_ta_init_reads;
                std::fprintf(stderr, "Naomi PVR REG R address=%08x width=%u value=00000000\n",
                             address, width);
            }
            return 0;
        }
        auto value = m_registers[(address - kPvrCoreStart) / 4];
        if ((address & ~3u) == kPvrCoreStart + 0x10cu) {
            value = (m_scanline & 0x3ffu) | (m_vsync ? (1u << 13) : 0u);
        }
        const auto shift = (address & 3u) * 8u;
        return width == 4 ? value : (value >> shift) & ((1u << (width * 8u)) - 1u);
    }
    if (address >= kArea1Start && address < kArea1End) {
        const auto offset = (address - kArea1Start) % static_cast<std::uint32_t>(m_vram.size());
        std::uint32_t result = 0;
        for (unsigned i = 0; i < width; ++i)
            result |= static_cast<std::uint32_t>(m_vram[(offset + i) % m_vram.size()]) << (i * 8u);
        return result;
    }
    return width == 1 ? 0xffu : width == 2 ? 0xffffu : 0xffffffffu;
}

void NaomiPvr2::write(std::uint32_t address, unsigned width, std::uint32_t value) {
    if (address >= kPvrIfStart && address < kPvrIfEnd) {
        const auto index = (address - kPvrIfStart) / 4;
        auto& reg = m_interface_registers[index];
        const auto shift = (address & 3u) * 8u;
        const std::uint32_t mask = width == 4 ? 0xffffffffu : ((1u << (width * 8u)) - 1u) << shift;
        reg = (reg & ~mask) | ((value << shift) & mask);
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
            ++g_pvr_trace_writes;
            std::fprintf(stderr, "Naomi PVR IF W address=%08x width=%u value=%08x merged=%08x\n",
                         address, width, value, reg);
        }
        ++m_register_writes;
        return;
    }
    if (address >= kPvrCoreStart && address < kPvrCoreEnd) {
        const auto aligned = address & ~3u;
        const auto index = (aligned - kPvrCoreStart) / 4;
        auto& reg = m_registers[index];
        const auto shift = (address & 3u) * 8u;
        const std::uint32_t mask = width == 4 ? 0xffffffffu : ((1u << (width * 8u)) - 1u) << shift;
        const auto merged = (reg & ~mask) | ((value << shift) & mask);
        if (aligned == kTaListInit) {
            if (merged & 0x80000000u) {
                m_ta_stream.clear();
                m_ta_parse_offset = 0;
                m_ta_active_list = -1;
            }
            // TA_LIST_INIT is a command register; keep its readback value at
            // zero, matching MAME powervr2.cpp and Flycast pvr_regs.cpp.
            reg = 0;
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
                ++g_pvr_trace_writes;
                std::fprintf(stderr, "Naomi PVR REG W address=%08x width=%u value=%08x command_readback=00000000\n",
                             address, width, value);
            }
            ++m_register_writes;
            return;
        }
        if (aligned == kPvrSoftReset && (merged & 1u)) {
            m_ta_stream.clear();
            m_ta_parse_offset = 0;
            m_ta_active_list = -1;
        }
        // TA_LIST_CONT begins a new submission phase; it does not erase the
        // completed list data, which remains available until the renderer consumes it.
        reg = merged;
        if (aligned == kTaListContinue) reg = 0;
        if (aligned == kPvrCoreStart + 0x44u ||
            aligned == kPvrCoreStart + 0xd0u ||
            aligned == kPvrCoreStart + 0xd8u)
            recalculateTiming();
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
            ++g_pvr_trace_writes;
            std::fprintf(stderr, "Naomi PVR REG W address=%08x width=%u value=%08x merged=%08x\n",
                         address, width, value, reg);
        }
        ++m_register_writes;
        return;
    }
    if (address >= kArea1Start && address < kArea1End) {
        const auto offset = (address - kArea1Start) % static_cast<std::uint32_t>(m_vram.size());
        std::array<std::uint8_t, 4> bytes{};
        for (unsigned i = 0; i < width; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8u));
        storeLittle(m_vram, offset, bytes.data(), width);
        m_vram_bytes_written += width;
        return;
    }
    if (address >= kTaAreaStart && address < kTaAreaEnd) {
        const auto target = (address - kTaAreaStart) & 0x01ffffffu;
        std::array<std::uint8_t, 4> bytes{};
        for (unsigned i = 0; i < width; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8u));
        submit(target, bytes.data(), width);
    }
}

void NaomiPvr2::recalculateTiming() {
    const auto load = m_registers[0xd8u / 4];
    const auto control = m_registers[0xd0u / 4];
    const auto fb_read = m_registers[0x44u / 4];
    const std::uint32_t hcount = (load & 0x3ffu) + 1u;
    m_scanlines = ((load >> 16) & 0x3ffu) + 1u;
    const std::uint32_t pixel_clock = kPixelClock / ((fb_read & (1u << 23)) ? 1u : 2u);
    const std::uint64_t numerator = static_cast<std::uint64_t>(kSh4Clock) * hcount;
    m_line_cycles = static_cast<std::uint32_t>(std::max<std::uint64_t>(
        1u, numerator / pixel_clock / ((control & (1u << 4)) ? 2u : 1u)));
    if (m_scanlines == 0) m_scanlines = 1;
    m_scanline %= m_scanlines;
    m_cycles_into_line %= m_line_cycles;
}

void NaomiPvr2::advanceScanline(const std::function<void(unsigned)>& raise_normal_irq) {
    m_scanline = (m_scanline + 1u) % m_scanlines;
    const auto vblank = m_registers[0xccu / 4];
    if (m_scanline == (vblank & 0x3ffu) && raise_normal_irq) raise_normal_irq(3);
    if (m_scanline == ((vblank >> 16) & 0x3ffu) && raise_normal_irq)
        raise_normal_irq(4);
    const auto vblank_cfg = m_registers[0xdcu / 4];
    if (m_scanline == (vblank_cfg & 0x3ffu)) m_vsync = true;
    if (m_scanline == ((vblank_cfg >> 16) & 0x3ffu)) m_vsync = false;
}

void NaomiPvr2::advanceCycles(
    std::uint32_t cycles, const std::function<void(unsigned)>& raise_normal_irq) {
    if (raise_normal_irq && !m_pending_ta_irqs.empty()) {
        for (const auto bit : m_pending_ta_irqs) raise_normal_irq(bit);
        m_pending_ta_irqs.clear();
    }
    if (m_line_cycles == 0 || m_scanlines == 0) recalculateTiming();
    while (cycles != 0) {
        const auto until_line = m_line_cycles - m_cycles_into_line;
        if (cycles < until_line) {
            m_cycles_into_line += cycles;
            break;
        }
        cycles -= until_line;
        m_cycles_into_line = 0;
        advanceScanline(raise_normal_irq);
    }
}

NaomiPvrFramebuffer NaomiPvr2::captureFramebuffer() const {
    NaomiPvrFramebuffer frame;
    frame.control = m_registers[0x44u / 4]; // FB_R_CTRL
    frame.size = m_registers[0x5cu / 4];    // FB_R_SIZE
    frame.start = m_registers[0x50u / 4] & 0x00ffffffu; // FB_R_SOF1

    const auto x_size = frame.size & 0x3ffu;
    const auto y_size = (frame.size >> 10u) & 0x3ffu;
    const auto modulus_raw = (frame.size >> 20u) & 0x3ffu;
    frame.width = (x_size + 1u) * 2u;
    frame.height = y_size + 1u;
    const auto depth = (frame.control >> 2u) & 3u;
    std::uint32_t bytes_per_pixel = 2;
    std::uint32_t modulus = modulus_raw == 0 ? 0u : (modulus_raw - 1u) * 2u;
    if (depth == 2u) {
        frame.width = (frame.width * 2u) / 3u;
        modulus = (modulus * 2u) / 3u;
        bytes_per_pixel = 3;
    } else if (depth == 3u) {
        frame.width /= 2u;
        modulus /= 2u;
        bytes_per_pixel = 4;
    }
    // Bound malformed register values before allocating or reading VRAM.
    if (frame.width == 0 || frame.height == 0 || frame.width > 2048u ||
        frame.height > 1024u || static_cast<std::uint64_t>(frame.width) * frame.height * 3u >
            16u * 1024u * 1024u) {
        frame.width = frame.height = 0;
        return frame;
    }
    frame.rgb.resize(static_cast<std::size_t>(frame.width) * frame.height * 3u);
    auto byte_at = [this](std::uint32_t address) {
        return m_vram[address % static_cast<std::uint32_t>(m_vram.size())];
    };
    auto put = [&frame](std::size_t pixel, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const auto at = pixel * 3u;
        frame.rgb[at] = r;
        frame.rgb[at + 1u] = g;
        frame.rgb[at + 2u] = b;
    };
    const auto concat = frame.control >> 4u & 7u;
    const std::uint64_t row_pitch = static_cast<std::uint64_t>(frame.width + modulus) * bytes_per_pixel;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const auto row = frame.start + static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(y) * row_pitch) % m_vram.size());
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto out = static_cast<std::size_t>(y) * frame.width + x;
            const auto addr = row + x * bytes_per_pixel;
            if (depth <= 1u) {
                const std::uint16_t value = byte_at(addr) |
                    static_cast<std::uint16_t>(byte_at(addr + 1u)) << 8u;
                std::uint8_t r, g, b;
                if (depth == 0u) {
                    r = static_cast<std::uint8_t>(((value >> 10u) & 31u) << 3u | concat);
                    g = static_cast<std::uint8_t>(((value >> 5u) & 31u) << 3u | concat);
                    b = static_cast<std::uint8_t>((value & 31u) << 3u | concat);
                } else {
                    r = static_cast<std::uint8_t>(((value >> 11u) & 31u) << 3u | concat);
                    g = static_cast<std::uint8_t>(((value >> 5u) & 63u) << 2u | (concat & 3u));
                    b = static_cast<std::uint8_t>((value & 31u) << 3u | concat);
                }
                put(out, r, g, b);
            } else if (depth == 3u) {
                put(out, byte_at(addr + 2u), byte_at(addr + 1u), byte_at(addr));
            } else {
                // 888 data is packed as four pixels in three 32-bit words;
                // each pixel's byte order is B,G,R in little-endian VRAM.
                const auto group = x / 4u;
                const auto lane = x % 4u;
                const auto packed = row + group * 12u + lane * 3u;
                put(out, byte_at(packed + 2u), byte_at(packed + 1u), byte_at(packed));
            }
        }
    }
    return frame;
}

void NaomiPvr2::writeStoreQueue(std::uint32_t address,
                                const std::array<std::uint8_t, 64>& queues) {
    const auto queue_offset = static_cast<std::size_t>((address >> 5) & 1u) * kStoreQueueBytes;
    const auto target = address & 0x01ffffe0u;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_submits < 64) {
        ++g_pvr_trace_submits;
        std::fprintf(stderr, "Naomi PVR SQ address=%08x target=%08x queue=%zu first=%02x%02x%02x%02x\n",
                     address, target, queue_offset,
                     queues[queue_offset + 3], queues[queue_offset + 2],
                     queues[queue_offset + 1], queues[queue_offset]);
    }
    submit(target, queues.data() + queue_offset, kStoreQueueBytes);
}

void NaomiPvr2::storeLittle(std::vector<std::uint8_t>& target, std::size_t offset,
                            const std::uint8_t* bytes, std::size_t count) {
    if (target.empty()) return;
    for (std::size_t i = 0; i < count; ++i)
        target[(offset + i) % target.size()] = bytes[i];
}

void NaomiPvr2::submit(std::uint32_t target, const std::uint8_t* bytes, std::size_t count) {
    // Flycast's TAWriteSQ bus split, matching the Naomi Area 4 windows:
    // [0,8 MiB) polygon list, [8,16 MiB) YUV, [16 MiB,...) direct VRAM.
    if (target < 0x00800000u) {
        if (m_ta_stream.size() + count > 64 * 1024 * 1024) {
            m_ta_stream.clear();
            m_ta_parse_offset = 0;
            m_ta_active_list = -1;
        }
        m_ta_stream.insert(m_ta_stream.end(), bytes, bytes + count);
        m_ta_bytes_submitted += count;
        parseTaPackets();
    } else if (target < 0x01000000u) {
        const auto offset = target - 0x00800000u;
        storeLittle(m_vram, offset, bytes, count);
        m_yuv_bytes_submitted += count;
        m_vram_bytes_written += count;
    } else {
        const auto offset = (target - 0x01000000u) % static_cast<std::uint32_t>(m_vram.size());
        storeLittle(m_vram, offset, bytes, count);
        m_vram_bytes_written += count;
    }
}

void NaomiPvr2::parseTaPackets() {
    static constexpr std::array<unsigned, 5> list_end_irq{7, 8, 9, 10, 21};
    constexpr std::size_t packet_bytes = 32;

    while (m_ta_parse_offset + packet_bytes <= m_ta_stream.size()) {
        const auto* packet = m_ta_stream.data() + m_ta_parse_offset;
        const std::uint32_t pcw = static_cast<std::uint32_t>(packet[0]) |
                                  (static_cast<std::uint32_t>(packet[1]) << 8u) |
                                  (static_cast<std::uint32_t>(packet[2]) << 16u) |
                                  (static_cast<std::uint32_t>(packet[3]) << 24u);
        const auto parameter_type = (pcw >> 29u) & 7u;
        const auto list_type = pcw & 7u;

        if (parameter_type == 4u || parameter_type == 5u || parameter_type == 6u) {
            m_ta_active_list = static_cast<int>(list_type);
        } else if (parameter_type == 0u) {
            const auto completed_list = m_ta_active_list >= 0
                ? static_cast<unsigned>(m_ta_active_list) : list_type;
            if (completed_list < list_end_irq.size()) {
                const auto bit = list_end_irq[completed_list];
                m_pending_ta_irqs.push_back(bit);
                if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_ta_eols < 64) {
                    ++g_pvr_trace_ta_eols;
                    std::fprintf(stderr,
                                 "Naomi PVR TA EOL list=%u irq_bit=%u stream_offset=%zu\n",
                                 completed_list, bit, m_ta_parse_offset);
                }
            } else if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_ta_eols < 64) {
                ++g_pvr_trace_ta_eols;
                std::fprintf(stderr,
                             "Naomi PVR TA EOL unsupported_list=%u stream_offset=%zu\n",
                             completed_list, m_ta_parse_offset);
            }
            m_ta_active_list = -1;
        }
        m_ta_parse_offset += packet_bytes;
    }
}

} // namespace mamerecomp::naomi
