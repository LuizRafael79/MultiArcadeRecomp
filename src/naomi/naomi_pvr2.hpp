#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mamerecomp::naomi {

struct NaomiPvrFramebuffer {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t control = 0;
    std::uint32_t size = 0;
    std::uint32_t start = 0;
    std::vector<std::uint8_t> rgb;
};

// Naomi 1's single 315-6267/Holly PVR2 bus endpoint. This first connection
// accepts the SH-4 store-queue and direct TA/YUV/VRAM paths and exposes the
// PVR register window. TA list interpretation/rasterization is a later layer.
class NaomiPvr2 {
public:
    static constexpr std::size_t kStoreQueueBytes = 32;
    // Naomi 1 has 16 MiB of PVR VRAM. Dreamcast wraps the 16 MiB Area 1
    // aperture over 8 MiB, but Naomi exposes the full 16 MiB to textures and
    // framebuffer storage (Flycast setPlatform and MAME's Naomi map).
    static constexpr std::size_t kVramBytes = 16 * 1024 * 1024;

    NaomiPvr2();
    ~NaomiPvr2();
    [[nodiscard]] bool handles(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t read(std::uint32_t address, unsigned width) const;
    void write(std::uint32_t address, unsigned width, std::uint32_t value);
    // The first scheduler bridge uses one coarse cycle per translated
    // instruction; the raster model itself follows SPG timing registers.
    void advanceCycles(std::uint32_t cycles,
                       const std::function<void(unsigned)>& raise_normal_irq);
    void writeStoreQueue(std::uint32_t address,
                         const std::array<std::uint8_t, 64>& queues);

    [[nodiscard]] std::uint64_t taBytesSubmitted() const { return m_ta_bytes_submitted; }
    [[nodiscard]] std::uint64_t yuvBytesSubmitted() const { return m_yuv_bytes_submitted; }
    [[nodiscard]] std::uint64_t vramBytesWritten() const { return m_vram_bytes_written; }
    [[nodiscard]] std::uint32_t registerWrites() const { return m_register_writes; }
    [[nodiscard]] std::uint32_t scanline() const { return m_scanline; }
    [[nodiscard]] const std::vector<std::uint8_t>& taStream() const { return m_ta_stream; }
    [[nodiscard]] const std::vector<std::uint8_t>& vram() const { return m_vram; }
    // Diagnostic readback of the PVR's configured direct framebuffer. This
    // decodes VRAM only; it does not rasterize TA polygon lists.
    [[nodiscard]] NaomiPvrFramebuffer captureFramebuffer() const;

private:
    static void storeLittle(std::vector<std::uint8_t>& target, std::size_t offset,
                            const std::uint8_t* bytes, std::size_t count);
    void submit(std::uint32_t target, const std::uint8_t* bytes, std::size_t count);
    void parseTaPackets();

    std::array<std::uint32_t, 0x2000 / 4> m_registers{};
    std::array<std::uint32_t, 0x100 / 4> m_interface_registers{};
    std::vector<std::uint8_t> m_vram;
    std::vector<std::uint8_t> m_ta_stream;
    std::vector<unsigned> m_pending_ta_irqs;
    std::size_t m_ta_parse_offset = 0;
    int m_ta_active_list = -1;
    std::uint64_t m_ta_bytes_submitted = 0;
    std::uint64_t m_yuv_bytes_submitted = 0;
    std::uint64_t m_vram_bytes_written = 0;
    std::uint32_t m_register_writes = 0;
    std::uint32_t m_scanline = 0;
    std::uint32_t m_cycles_into_line = 0;
    std::uint32_t m_line_cycles = 0;
    std::uint32_t m_scanlines = 0;
    bool m_vsync = false;
    void recalculateTiming();
    void advanceScanline(const std::function<void(unsigned)>& raise_normal_irq);
};

} // namespace mamerecomp::naomi
