#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
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

struct NaomiTaListSpan {
    std::uint8_t list_type = 0;
    std::uint64_t begin_byte = 0;
    std::uint64_t end_byte = 0; // exclusive; includes the EOL parameter
    std::uint32_t render_pass = 0;
};

struct NaomiTaPrimitiveSpan {
    std::uint64_t header_byte = 0;
    std::uint64_t data_begin_byte = 0;
    std::uint64_t data_end_byte = 0; // exclusive; includes all vertex halves
    std::uint32_t pcw = 0;
    std::uint8_t list_type = 0;
    std::uint8_t vertex_packet_bytes = 32;
    bool sprite = false;
    bool complete = false;
    bool modifier_volume = false;
    std::uint8_t vertex_format = 0xff;
    std::uint32_t tile_clip = 0;
    std::uint32_t render_pass = 0;
    std::uint32_t context_address = 0;
};

struct NaomiPvrVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float u[2]{};
    float v[2]{};
    float base_color[2][4]{};   // RGBA, normalized to 0..1.
    float offset_color[2][4]{};
};

struct NaomiPvrModifierTriangle {
    float xyz[9]{};
};

struct NaomiPvrScenePrimitive {
    std::uint8_t list_type = 0;
    std::uint32_t render_pass = 0;
    std::uint32_t context_address = 0;
    std::uint8_t vertex_format = 0xff;
    std::uint32_t pcw = 0;
    std::uint32_t isp = 0;
    std::uint32_t tsp[2]{};
    std::uint32_t tcw[2]{};
    std::uint32_t tile_clip = 0;
    bool textured = false;
    bool dual_volume = false;
    bool sprite = false;
    bool background = false;
    bool modifier_volume = false;
    std::vector<NaomiPvrVertex> vertices;
    // Inclusive vertex indices at TA EndOfStrip markers. The renderer must
    // restart strip winding here instead of joining adjacent TA strips.
    std::vector<std::size_t> strip_end_vertex_indices;
    std::vector<NaomiPvrModifierTriangle> modifier_triangles;
};

// Naomi 1's single 315-6267/Holly PVR2 bus endpoint. It accepts the SH-4
// store-queue and direct TA/YUV/VRAM paths and exposes the PVR register window.
class NaomiPvr2 {
public:
    static constexpr std::size_t kStoreQueueBytes = 32;
    // Naomi 1 has 16 MiB of PVR VRAM. Dreamcast wraps the 16 MiB Area 1
    // aperture over 8 MiB, but Naomi exposes the full 16 MiB to textures and
    // framebuffer storage (Flycast setPlatform).
    static constexpr std::size_t kVramBytes = 16 * 1024 * 1024;
    using DmaRead32 = std::function<std::uint32_t(std::uint32_t)>;
    using DmaWrite32 = std::function<void(std::uint32_t, std::uint32_t)>;

    NaomiPvr2();
    ~NaomiPvr2();
    [[nodiscard]] bool handles(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t read(std::uint32_t address, unsigned width) const;
    void write(std::uint32_t address, unsigned width, std::uint32_t value);
    void setDmaBus(DmaRead32 read32, DmaWrite32 write32,
                   std::function<void(unsigned)> raise_normal_irq);
    // The first scheduler bridge uses one coarse cycle per translated
    // instruction; scan timing follows SPG timing registers.
    void advanceCycles(std::uint32_t cycles,
                       const std::function<void(unsigned)>& raise_normal_irq);
    void writeStoreQueue(std::uint32_t address,
                         const std::array<std::uint8_t, 64>& queues);

    [[nodiscard]] std::uint64_t taBytesSubmitted() const { return m_ta_bytes_submitted; }
    [[nodiscard]] std::uint64_t yuvBytesSubmitted() const { return m_yuv_bytes_submitted; }
    [[nodiscard]] std::uint64_t vramBytesWritten() const { return m_vram_bytes_written; }
    [[nodiscard]] std::uint32_t registerWrites() const { return m_register_writes; }
    [[nodiscard]] std::uint64_t pvrDmaCommands() const { return m_pvr_dma_commands; }
    [[nodiscard]] std::uint64_t pvrDmaBytes() const { return m_pvr_dma_bytes; }
    [[nodiscard]] std::uint64_t renderStartRequests() const { return m_render_start_requests; }
    [[nodiscard]] std::uint32_t renderSurfaceWidth() const { return m_render_surface_width; }
    [[nodiscard]] std::uint32_t renderSurfaceHeight() const { return m_render_surface_height; }
    [[nodiscard]] const std::array<std::uint32_t, 4>& renderTileClip() const {
        return m_render_tile_clip;
    }
    [[nodiscard]] std::uint32_t scanline() const { return m_scanline; }
    [[nodiscard]] const std::array<std::uint64_t, 8>& taParameterCounts() const {
        return m_ta_parameter_counts;
    }
    [[nodiscard]] const std::array<std::uint64_t, 5>& taListPacketCounts() const {
        return m_ta_list_packet_counts;
    }
    [[nodiscard]] const std::vector<NaomiTaListSpan>& taLists() const {
        return m_ta_lists;
    }
    [[nodiscard]] const std::vector<NaomiTaPrimitiveSpan>& taPrimitives() const {
        return m_ta_primitives;
    }
    [[nodiscard]] const std::vector<NaomiPvrScenePrimitive>& scenePrimitives() const {
        return m_scene_primitives;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& taStream() const { return m_ta_stream; }
    [[nodiscard]] const std::vector<std::uint8_t>& vram() const { return m_vram; }
    // Readback of the configured PVR framebuffer in guest VRAM.
    [[nodiscard]] NaomiPvrFramebuffer captureFramebuffer() const;
    // Flycast TexCache.cpp::WriteFramebuffer equivalent. STARTRENDER latches
    // FB_W_CTRL/SOF1/LINESTRIDE. Input pixels are RGBA8 from the completed
    // render target; output is packed into interleaved Naomi VRAM.
    void writeFramebufferToVram(std::uint32_t width, std::uint32_t height,
                                std::span<const std::uint8_t> rgba);

private:
    [[nodiscard]] static std::uint32_t mapVram32(std::uint32_t address);
    static void storeLittle(std::vector<std::uint8_t>& target, std::size_t offset,
                            const std::uint8_t* bytes, std::size_t count);
    void submit(std::uint32_t target, const std::uint8_t* bytes, std::size_t count);
    void parseTaPackets();
    void markObjectListBlocks(bool continuation);
    [[nodiscard]] std::vector<std::uint32_t> getTaContextAddresses() const;
    void decodeRegionTileClip();
    void decodeTaScene();
    void decodeBackgroundPlane();
    void resetYuvConverter();
    void submitYuv(const std::uint8_t* bytes, std::size_t count);
    void convertYuvMacroblock(const std::uint8_t* block);
    [[nodiscard]] std::array<float, 4> sampleTexture(
        std::uint32_t tsp, std::uint32_t tcw, float u, float v,
        float lod = 0.0f) const;
    [[nodiscard]] std::vector<std::uint8_t> rasterizeTaScene() const;

    std::array<std::uint32_t, 0x2000 / 4> m_registers{};
    std::array<std::uint32_t, 0x100 / 4> m_interface_registers{};
    // TA parameter types are the PCW's top three bits. Keep per-type and
    // per-list accounting for comparison with Flycast's TA state machine.
    std::array<std::uint64_t, 8> m_ta_parameter_counts{};
    std::array<std::uint64_t, 5> m_ta_list_packet_counts{};
    std::vector<NaomiTaListSpan> m_ta_lists;
    std::vector<NaomiTaPrimitiveSpan> m_ta_primitives;
    std::vector<NaomiPvrScenePrimitive> m_scene_primitives;
    // Context ids selected from the first tile's opaque/translucent/punch
    // OPBs at STARTRENDER, matching Flycast getTAContextAddresses().
    std::vector<std::uint32_t> m_active_ta_contexts;
    std::vector<std::uint32_t> m_known_ta_contexts;
    std::unordered_map<std::uint32_t, std::uint64_t> m_ta_context_bytes;
    std::uint8_t m_ta_continuation_units = 0;
    // Flycast ta.cpp's three-bit ta_state FSM. It distinguishes polygon,
    // modifier, 64-byte header/vertex continuations and idle packets.
    std::uint8_t m_ta_fsm_state = 0;
    bool m_ta_continuation_is_vertex = false;
    bool m_ta_continuation_ends_strip = false;
    bool m_ta_vertex_is_64_bytes = false;
    std::uint64_t m_ta_active_list_begin = 0;
    std::uint32_t m_ta_render_pass = 0;
    // Flycast ta_vtx.cpp tileclip_val: mode in bits 31..28, then the
    // inclusive tile rectangle in 5/6-bit coordinates.
    std::uint32_t m_ta_tile_clip = (39u << 6u) | (14u << 17u);
    std::vector<std::uint8_t> m_vram;
    std::vector<std::uint8_t> m_ta_stream;
    std::vector<unsigned> m_pending_ta_irqs;
    std::size_t m_ta_parse_offset = 0;
    int m_ta_active_list = -1;
    int m_ta_active_primitive = -1;
    std::uint64_t m_ta_bytes_submitted = 0;
    // Flycast's ta_thd_data32_i drops FIFO packets until TA_LIST_INIT has
    // installed the current TA context. Keep that bus contract explicit.
    bool m_ta_context_active = false;
    std::uint64_t m_yuv_bytes_submitted = 0;
    std::array<std::uint8_t, 384> m_yuv_pending_block{};
    std::size_t m_yuv_pending_bytes = 0;
    std::uint32_t m_yuv_destination = 0;
    std::uint32_t m_yuv_block_count = 0;
    std::uint32_t m_yuv_block_index = 0;
    std::uint32_t m_yuv_x = 0;
    std::uint32_t m_yuv_y = 0;
    std::uint32_t m_yuv_width = 0;
    std::uint32_t m_yuv_height = 0;
    std::uint64_t m_vram_bytes_written = 0;
    DmaRead32 m_dma_read32;
    DmaWrite32 m_dma_write32;
    std::function<void(unsigned)> m_raise_normal_irq;
    std::uint32_t m_pvr_dma_system_address = 0;
    std::uint32_t m_pvr_dma_size = 0;
    std::uint32_t m_pvr_dma_pvr_address = 0;
    std::uint32_t m_pvr_dma_start = 0;
    std::uint32_t m_pvr_dma_enable = 0;
    std::uint32_t m_pvr_dma_direction = 0;
    std::uint32_t m_pvr_dma_select = 0;
    std::uint64_t m_pvr_dma_commands = 0;
    std::uint64_t m_pvr_dma_bytes = 0;
    std::uint64_t m_render_start_requests = 0;
    std::uint32_t m_render_fb_w_ctrl = 0;
    std::uint32_t m_render_fb_w_sof1 = 0;
    std::uint32_t m_render_fb_w_line_stride = 0;
    std::uint32_t m_render_surface_width = 0;
    std::uint32_t m_render_surface_height = 0;
    std::vector<std::uint8_t> m_last_render_rgb;
    bool m_ppm_capture_written = false;
    // Flycast setTileClipping() result: x, y, width, height in pixels.
    std::array<std::uint32_t, 4> m_render_tile_clip{};
    std::uint32_t m_render_clip_min_x = 0;
    std::uint32_t m_render_clip_max_x = 0;
    std::uint32_t m_render_clip_min_y = 0;
    std::uint32_t m_render_clip_max_y = 0;
    void startPvrDma();
    std::uint32_t m_register_writes = 0;
    std::uint32_t m_scanline = 0;
    std::uint32_t m_cycles_into_line = 0;
    std::uint32_t m_render_done_cycles_remaining = 0;
    std::uint32_t m_line_cycles = 0;
    std::uint32_t m_scanlines = 0;
    bool m_field_number = false;
    bool m_vsync = false;
    void recalculateTiming();
    void advanceScanline(const std::function<void(unsigned)>& raise_normal_irq);
};

} // namespace mamerecomp::naomi
