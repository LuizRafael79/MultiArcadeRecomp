#include "naomi_pvr2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

namespace multiarcaderecomp::naomi {
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
constexpr std::uint32_t kPvrStartRender = 0x005f8014;
constexpr std::uint32_t kPvrSoftReset = 0x005f8008;
unsigned g_pvr_trace_writes = 0;
unsigned g_pvr_trace_submits = 0;
unsigned g_pvr_trace_ta_init_reads = 0;
unsigned g_pvr_trace_ta_eols = 0;
unsigned g_pvr_dma_trace_events = 0;

struct TaObjectFormat {
    bool valid = false;
    unsigned polygon_header_units = 1;
    unsigned vertex_units = 1;
    unsigned vertex_format = 0;
};

// Port of Flycast's TaTypeLut::poly_data_type_id() and
// poly_header_type_size() for the object-control byte. The high two bits of
// the returned Flycast LUT entry encode the vertex packet size; the next byte
// encodes the polygon header format. Keeping this as a pure decoder avoids
// guessing packet boundaries from only texture/colour flags.
TaObjectFormat decodeTaObjectFormat(std::uint8_t object_control) {
    const unsigned uv16 = object_control & 1u;
    const unsigned texture = (object_control >> 3u) & 1u;
    const unsigned color = (object_control >> 4u) & 3u;
    const unsigned volume = (object_control >> 6u) & 1u;

    unsigned data_type = 0;
    if (texture) {
        if (!volume) {
            data_type = color == 0 ? (uv16 ? 4u : 3u)
                       : color == 1 ? (uv16 ? 6u : 5u)
                                    : (uv16 ? 8u : 7u);
        } else {
            if (color == 1) return {};
            data_type = color == 0 ? (uv16 ? 12u : 11u)
                                   : (uv16 ? 14u : 13u);
        }
    } else if (!volume) {
        data_type = color == 0u ? 0u : color == 1u ? 1u : 2u;
    } else {
        if (color == 1) return {};
        data_type = color == 0 ? 9u : 10u;
    }

    unsigned header_units = 1;
    if (!volume) {
        if (color < 2) header_units = 1;
        else if (color == 2) header_units = (texture && (object_control & 4u)) ? 2u : 1u;
        else header_units = 1;
    } else if (color == 0 || color == 3) {
        header_units = 1;
    } else if (color == 2) {
        header_units = 2;
    } else {
        return {};
    }

    // Flycast ta_const_df.h's ta_poly_data_lut: formats 5, 6 and 11..14
    // consume 64-byte vertices; all other valid polygon formats consume 32.
    const unsigned vertex_units =
        (data_type == 5u || data_type == 6u || data_type >= 11u) ? 2u : 1u;
    return {true, header_units, vertex_units, data_type};
}

// Flycast ta.cpp::fill_fsm() transition rules, reduced to the state transition
// (the command side effects are handled by NaomiPvr2's TA parser below).
// States: NS, PLV32, PLV64, MLV64, PLHV32, PLHV64, PLV64_H, MLV64_H.
std::uint8_t advanceTaFsm(std::uint8_t state, unsigned parameter_type,
                          std::uint8_t object_control, unsigned list_type) {
    switch (parameter_type) {
    case 0: // End of list
        return state <= 3u ? 0u : state;
    case 1: // User tile clip
    case 2: // Object list set
        return state;
    case 4: { // Polygon or modifier-volume header
        if (state == 3u) return 3u;
        if (state != 0u && state != 1u && state != 2u) return state;
        if (state == 0u && (list_type == 1u || list_type == 3u)) return 3u;
        const auto format = decodeTaObjectFormat(object_control);
        if (!format.valid) return state;
        const bool header64 = format.polygon_header_units == 2u;
        const bool vertex64 = format.vertex_units == 2u;
        if (header64) return vertex64 ? 5u : 4u;
        return vertex64 ? 2u : 1u;
    }
    case 5: // Sprite header
        if (state == 0u) return 1u;
        if (state == 1u || state == 2u) return 2u;
        return state;
    case 7: // Vertex parameter, including the second 32-byte half
        switch (state) {
        case 1: return 1;
        case 2: return 6;
        case 3: return 7;
        case 4: return 1;
        case 5: return 2;
        case 6: return 2;
        case 7: return 3;
        default: return state;
        }
    default:
        return state;
    }
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

float read_pvr_uv16(std::uint16_t value) {
    // Flycast ta_structs.h stores PVR 16-bit UV values in the upper half of
    // an IEEE single-precision word (not IEEE binary16).
    const std::uint32_t bits = static_cast<std::uint32_t>(value) << 16u;
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

void unpack_argb(std::uint32_t packed, float rgba[4]) {
    rgba[0] = static_cast<float>((packed >> 16u) & 0xffu) / 255.0f;
    rgba[1] = static_cast<float>((packed >> 8u) & 0xffu) / 255.0f;
    rgba[2] = static_cast<float>(packed & 0xffu) / 255.0f;
    rgba[3] = static_cast<float>((packed >> 24u) & 0xffu) / 255.0f;
}

std::uint8_t to_saturated_u8(float value) {
    if (std::isnan(value)) return 0xffu;
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(value * 255.0f);
}

void quantize_rgba(float rgba[4]) {
    for (unsigned i = 0; i < 4; ++i)
        rgba[i] = static_cast<float>(to_saturated_u8(rgba[i])) / 255.0f;
}

void read_float_rgba(const std::uint32_t* words, float rgba[4]) {
    rgba[3] = std::bit_cast<float>(words[0]); // TA order is A, R, G, B.
    rgba[0] = std::bit_cast<float>(words[1]);
    rgba[1] = std::bit_cast<float>(words[2]);
    rgba[2] = std::bit_cast<float>(words[3]);
}

void apply_intensity(const float source[4], float intensity, float result[4]) {
    const unsigned intensity_byte = to_saturated_u8(intensity);
    for (unsigned channel = 0; channel < 3; ++channel) {
        const unsigned base_byte = to_saturated_u8(source[channel]);
        result[channel] = static_cast<float>((base_byte * intensity_byte) / 256u) / 255.0f;
    }
    result[3] = static_cast<float>(to_saturated_u8(source[3])) / 255.0f;
}

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
    resetYuvConverter();
}

NaomiPvr2::~NaomiPvr2() {
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR"))
        std::fprintf(stderr,
                     "Naomi PVR runtime: dma-commands=%llu dma-bytes=%llu render-starts=%llu register-writes=%u TA-bytes=%llu completed-lists=%zu VRAM-bytes=%llu\n",
                     static_cast<unsigned long long>(m_pvr_dma_commands),
                     static_cast<unsigned long long>(m_pvr_dma_bytes),
                     static_cast<unsigned long long>(m_render_start_requests),
                     m_register_writes,
                     static_cast<unsigned long long>(m_ta_bytes_submitted),
                     m_ta_lists.size(),
                     static_cast<unsigned long long>(m_vram_bytes_written));
    const char* prefix = std::getenv("MULTIARCADERECOMP_NAOMI_DUMP_PVR_PREFIX");
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
    // Keep a directly viewable copy of the *guest scanout* alongside the raw
    // readback.  This is deliberately produced from captureFramebuffer(),
    // after Naomi's FB_R registers and VRAM layout have been decoded; dumping
    // the software raster target here would conceal a broken framebuffer
    // writeback or scanout contract.
    if (frame.width != 0 && frame.height != 0 &&
        frame.rgb.size() == static_cast<std::size_t>(frame.width) * frame.height * 3u) {
        const std::string path = std::string(prefix) + ".scanout.ppm";
        std::ofstream ppm(path, std::ios::binary);
        ppm << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        ppm.write(reinterpret_cast<const char*>(frame.rgb.data()),
                  static_cast<std::streamsize>(frame.rgb.size()));
        if (ppm)
            std::fprintf(stderr, "Naomi PVR PPM path=%s dimensions=%ux%u bytes=%zu\n",
                         path.c_str(), frame.width, frame.height, frame.rgb.size());
        else
            std::fprintf(stderr, "Naomi PVR PPM write failed path=%s\n", path.c_str());
    } else {
        std::fprintf(stderr,
                     "Naomi PVR PPM skipped: guest scanout unavailable (FB_R_CTRL=%08x FB_R_SIZE=%08x)\n",
                     frame.control, frame.size);
    }
    if (!m_last_render_rgb.empty() && m_render_surface_width != 0 &&
        m_render_surface_height != 0) {
        const std::string path = std::string(prefix) + ".pvr-render.ppm";
        std::ofstream ppm(path, std::ios::binary);
        ppm << "P6\n" << m_render_surface_width << ' ' << m_render_surface_height
            << "\n255\n";
        ppm.write(reinterpret_cast<const char*>(m_last_render_rgb.data()),
                  static_cast<std::streamsize>(m_last_render_rgb.size()));
        if (ppm)
            std::fprintf(stderr, "Naomi PVR render PPM path=%s dimensions=%ux%u\n",
                         path.c_str(), m_render_surface_width, m_render_surface_height);
    }

    const std::string lists_path = std::string(prefix) + ".ta-lists.txt";
    std::ofstream lists(lists_path);
    if (lists) {
        lists << "# render_pass list_type begin_byte end_byte bytes\n";
        for (const auto& list : m_ta_lists)
            lists << list.render_pass << ' ' << unsigned(list.list_type) << ' '
                  << list.begin_byte << ' ' << list.end_byte << ' '
                  << (list.end_byte - list.begin_byte) << '\n';
        lists << "# render_pass header data_begin data_end pcw list vertex_bytes format tileclip kind complete\n";
        for (const auto& primitive : m_ta_primitives)
            lists << primitive.render_pass << ' ' << primitive.header_byte << ' '
                  << primitive.data_begin_byte << ' '
                  << primitive.data_end_byte << " 0x" << std::hex << primitive.pcw
                  << std::dec << ' ' << unsigned(primitive.list_type) << ' '
                  << unsigned(primitive.vertex_packet_bytes) << ' '
                  << unsigned(primitive.vertex_format) << " 0x" << std::hex
                  << primitive.tile_clip << std::dec << ' '
                  << (primitive.sprite ? "sprite" : primitive.modifier_volume ? "modifier" : "polygon") << ' '
                  << (primitive.complete ? 1 : 0) << '\n';
    }

    const std::string scene_path = std::string(prefix) + ".ta-scene.txt";
    std::ofstream scene(scene_path);
    if (scene) {
        scene << "# render_pass list format flags pcw isp tsp0 tcw0 tsp1 tcw1 tileclip vertices modifier_triangles\n";
        for (const auto& primitive : m_scene_primitives) {
            scene << primitive.render_pass << ' ' << unsigned(primitive.list_type) << ' '
                  << unsigned(primitive.vertex_format)
                  << ' ' << (primitive.background ? 'B' : '-')
                  << (primitive.sprite ? 'S' : '-')
                  << (primitive.modifier_volume ? 'M' : '-')
                  << (primitive.textured ? 'T' : '-')
                  << (primitive.dual_volume ? '2' : '-')
                  << " 0x" << std::hex << primitive.pcw << ' ' << primitive.isp
                  << ' ' << primitive.tsp[0] << ' ' << primitive.tcw[0]
                  << ' ' << primitive.tsp[1] << ' ' << primitive.tcw[1]
                  << ' ' << primitive.tile_clip << std::dec << ' '
                  << primitive.vertices.size() << ' '
                  << primitive.modifier_triangles.size() << '\n';
        }
    }

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
             << "\nTA_parse_offset=" << m_ta_parse_offset
             << "\nSTARTRENDER_requests=" << m_render_start_requests
             << "\nFB_enabled=" << ((frame.control & 1u) ? 1 : 0)
             << "\nTA_completed_lists=" << m_ta_lists.size()
             << "\nPVR_tile_clip_x=" << m_render_tile_clip[0]
             << "\nPVR_tile_clip_y=" << m_render_tile_clip[1]
             << "\nPVR_tile_clip_width=" << m_render_tile_clip[2]
             << "\nPVR_tile_clip_height=" << m_render_tile_clip[3] << "\n";
    for (std::size_t i = 0; i < m_ta_parameter_counts.size(); ++i)
        metadata << "TA_parameter_type_" << i << "=" << m_ta_parameter_counts[i] << "\n";
    for (std::size_t i = 0; i < m_ta_list_packet_counts.size(); ++i)
        metadata << "TA_list_" << i << "_packets=" << m_ta_list_packet_counts[i] << "\n";
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
        const auto offset = address - kPvrIfStart;
        std::uint32_t value = 0;
        switch (offset) {
        case 0x00: value = m_pvr_dma_pvr_address; break;
        case 0x04: value = m_pvr_dma_system_address; break;
        case 0x08: value = m_pvr_dma_size; break;
        case 0x0c: value = m_pvr_dma_direction; break;
        case 0x10: value = m_pvr_dma_select; break;
        case 0x14: value = m_pvr_dma_enable; break;
        case 0x18: value = m_pvr_dma_start; break;
        default: value = m_interface_registers[offset / 4]; break;
        }
        const auto shift = (address & 3u) * 8u;
        return width == 4 ? value : (value >> shift) & ((1u << (width * 8u)) - 1u);
    }
    if (address >= kPvrCoreStart && address < kPvrCoreEnd) {
        if (address == kPvrCoreStart + 0x150u)
            return m_yuv_block_index;
        if (address == kTaListInit) {
            // Flycast's PVR register handler exposes this command register as
            // zero on readback.
            if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") &&
                g_pvr_trace_ta_init_reads < 64) {
                ++g_pvr_trace_ta_init_reads;
                std::fprintf(stderr, "Naomi PVR REG R address=%08x width=%u value=00000000\n",
                             address, width);
            }
            return 0;
        }
        auto value = m_registers[(address - kPvrCoreStart) / 4];
        if ((address & ~3u) == kPvrCoreStart + 0x10cu) {
            value = (m_scanline & 0x3ffu) |
                    (m_field_number ? (1u << 10) : 0u) |
                    (m_vsync ? (1u << 13) : 0u);
        }
        const auto shift = (address & 3u) * 8u;
        return width == 4 ? value : (value >> shift) & ((1u << (width * 8u)) - 1u);
    }
    if (address >= kArea1Start && address < kArea1End) {
        const auto offset = mapVram32((address - kArea1Start) & 0x00ffffffu);
        std::uint32_t result = 0;
        for (unsigned i = 0; i < width; ++i)
            result |= static_cast<std::uint32_t>(m_vram[(offset + i) % m_vram.size()]) << (i * 8u);
        return result;
    }
    return width == 1 ? 0xffu : width == 2 ? 0xffffu : 0xffffffffu;
}

void NaomiPvr2::write(std::uint32_t address, unsigned width, std::uint32_t value) {
    if (address >= kPvrIfStart && address < kPvrIfEnd) {
        const auto aligned = address & ~3u;
        const auto offset = aligned - kPvrIfStart;
        const auto index = offset / 4;
        auto& reg = m_interface_registers[index];
        const auto shift = (address & 3u) * 8u;
        const std::uint32_t mask = width == 4 ? 0xffffffffu : ((1u << (width * 8u)) - 1u) << shift;
        reg = (reg & ~mask) | ((value << shift) & mask);
        switch (offset) {
        case 0x00: m_pvr_dma_pvr_address = reg; break;
        case 0x04: m_pvr_dma_system_address = reg; break;
        case 0x08: m_pvr_dma_size = reg; break;
        case 0x0c: m_pvr_dma_direction = reg; break;
        case 0x10: m_pvr_dma_select = reg & 1u; break;
        case 0x14: m_pvr_dma_enable = reg & 1u; break;
        case 0x18:
            m_pvr_dma_start = reg & 1u;
            if (m_pvr_dma_start)
                startPvrDma();
            break;
        }
        if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
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
        if (aligned == kPvrCoreStart + 0x150u) return; // TA_YUV_TEX_CNT is read-only.
        auto& reg = m_registers[index];
        const auto shift = (address & 3u) * 8u;
        const std::uint32_t mask = width == 4 ? 0xffffffffu : ((1u << (width * 8u)) - 1u) << shift;
        const auto merged = (reg & ~mask) | ((value << shift) & mask);
        if (aligned == kPvrStartRender) {
            // Flycast handles STARTRENDER as a command and does not store it
            // back. Decode the TA packet stream and schedule completion here.
            // Flycast's scene renderer still needs its software framebuffer
            // path adapted to guest VRAM.
            ++m_render_start_requests;
            resetYuvConverter();
            m_render_fb_w_ctrl = m_registers[0x48u / 4u];
            m_render_fb_w_sof1 = m_registers[0x60u / 4u];
            m_render_fb_w_line_stride = (m_registers[0x4cu / 4u] & 0x1ffu) * 8u;
            const auto fb_x_clip = m_registers[0x68u / 4u];
            const auto fb_y_clip = m_registers[0x6cu / 4u];
            m_render_clip_min_x = fb_x_clip & 0x7ffu;
            m_render_clip_max_x = (fb_x_clip >> 16u) & 0x7ffu;
            m_render_clip_min_y = fb_y_clip & 0x3ffu;
            m_render_clip_max_y = (fb_y_clip >> 16u) & 0x3ffu;
            const auto global_tile_clip = m_registers[0x13cu / 4u];
            const auto scaler = m_registers[0xf4u / 4u];
            const auto base_width = ((global_tile_clip & 0x3fu) + 1u) * 32u;
            const auto base_height = (((global_tile_clip >> 16u) & 0xfu) + 1u) * 32u;
            const auto vertical_scale = scaler & 0xffffu;
            m_render_surface_width = (scaler & (1u << 16u)) ? base_width / 2u : base_width;
            m_render_surface_height = vertical_scale != 0u && vertical_scale != 1024u &&
                                     vertical_scale != 1025u
                ? static_cast<std::uint32_t>(base_height * (1024.0f / vertical_scale))
                : base_height;
            m_active_ta_contexts.clear();
            const auto opb_contexts = getTaContextAddresses();
            for (const auto address : opb_contexts) {
                if (std::find(m_known_ta_contexts.begin(), m_known_ta_contexts.end(), address) !=
                    m_known_ta_contexts.end())
                    m_active_ta_contexts.push_back(address);
            }
            decodeRegionTileClip();
            decodeTaScene();
            m_last_render_rgb.clear();
            if (!m_active_ta_contexts.empty() && !m_scene_primitives.empty() &&
                m_render_surface_width != 0 && m_render_surface_height != 0) {
                const auto rgba = rasterizeTaScene();
                const auto pixel_count = static_cast<std::size_t>(m_render_surface_width) *
                                         m_render_surface_height;
                if (rgba.size() == pixel_count * 4u) {
                    writeFramebufferToVram(m_render_surface_width, m_render_surface_height, rgba);
                    m_last_render_rgb.resize(pixel_count * 3u);
                    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
                        m_last_render_rgb[pixel * 3u] = rgba[pixel * 4u];
                        m_last_render_rgb[pixel * 3u + 1u] = rgba[pixel * 4u + 1u];
                        m_last_render_rgb[pixel * 3u + 2u] = rgba[pixel * 4u + 2u];
                    }
                }
            }
            // Capture at the first actual render boundary as well as at
            // shutdown. Long interpreter runs can be externally stopped;
            // the frame must survive that without substituting a diagnostic
            // surface for guest pixels.
            if (!m_ppm_capture_written) {
                if (const char* prefix = std::getenv("MULTIARCADERECOMP_NAOMI_DUMP_PVR_PREFIX");
                    prefix && *prefix) {
                    const auto frame = captureFramebuffer();
                    if (frame.width != 0 && frame.height != 0 &&
                        frame.rgb.size() == static_cast<std::size_t>(frame.width) *
                                               frame.height * 3u) {
                        const std::string path = std::string(prefix) + ".scanout.ppm";
                        std::ofstream ppm(path, std::ios::binary);
                        ppm << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
                        ppm.write(reinterpret_cast<const char*>(frame.rgb.data()),
                                  static_cast<std::streamsize>(frame.rgb.size()));
                        m_ppm_capture_written = static_cast<bool>(ppm);
                        if (m_ppm_capture_written)
                            std::fprintf(stderr, "Naomi PVR PPM path=%s dimensions=%ux%u bytes=%zu\n",
                                         path.c_str(), frame.width, frame.height, frame.rgb.size());
                    }
                    if (!m_ppm_capture_written && !m_last_render_rgb.empty() &&
                        m_render_surface_width != 0 && m_render_surface_height != 0) {
                        const std::string path = std::string(prefix) + ".pvr-render.ppm";
                        std::ofstream ppm(path, std::ios::binary);
                        ppm << "P6\n" << m_render_surface_width << ' '
                            << m_render_surface_height << "\n255\n";
                        ppm.write(reinterpret_cast<const char*>(m_last_render_rgb.data()),
                                  static_cast<std::streamsize>(m_last_render_rgb.size()));
                        m_ppm_capture_written = static_cast<bool>(ppm);
                        if (m_ppm_capture_written)
                            std::fprintf(stderr, "Naomi PVR render PPM path=%s dimensions=%ux%u\n",
                                         path.c_str(), m_render_surface_width, m_render_surface_height);
                    }
                }
            }
            std::uint64_t context_bytes = 0;
            for (const auto address : m_active_ta_contexts)
                context_bytes += m_ta_context_bytes[address];
            // Flycast spg.cpp::scheduleRenderDone() uses 4096 cycles only if
            // OPB resolution produced no TA_context. Otherwise it sums the
            // queued bytes of the linked contexts and schedules 450000 +
            // 100 cycles per byte, capped at 1500000.
            const auto render_delay = m_active_ta_contexts.empty()
                ? std::uint64_t{4096}
                : std::min<std::uint64_t>(450000u + context_bytes * 100u,
                                          1500000u);
            m_render_done_cycles_remaining = static_cast<std::uint32_t>(render_delay);
            if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") &&
                m_render_start_requests <= 16) {
                std::size_t vertex_count = 0;
                for (const auto& primitive : m_scene_primitives)
                    vertex_count += primitive.vertices.size();
                std::fprintf(stderr,
                             "Naomi PVR STARTRENDER #%llu TA-bytes=%zu lists=%zu scene-primitives=%zu vertices=%zu contexts=%zu/%zu/%zu spans=%zu FB_W_CTRL=%08x FB_W_SOF1=%08x stride=%u clip=%u,%u..%u,%u surface=%ux%u\n",
                             static_cast<unsigned long long>(m_render_start_requests),
                             m_ta_stream.size(), m_ta_lists.size(), m_scene_primitives.size(),
                             vertex_count, opb_contexts.size(), m_active_ta_contexts.size(),
                             m_known_ta_contexts.size(), m_ta_primitives.size(),
                             m_render_fb_w_ctrl, m_render_fb_w_sof1,
                             m_render_fb_w_line_stride,
                             m_render_clip_min_x, m_render_clip_min_y,
                             m_render_clip_max_x, m_render_clip_max_y,
                             m_render_surface_width, m_render_surface_height);
                for (const auto address : opb_contexts)
                    std::fprintf(stderr, "  OPB context=%08x%s\n", address,
                        std::find(m_known_ta_contexts.begin(), m_known_ta_contexts.end(), address) !=
                            m_known_ta_contexts.end() ? " known" : " UNKNOWN");
                for (const auto& span : m_ta_primitives)
                    std::fprintf(stderr, "  TA span context=%08x pass=%u header=%u bytes=%u..%u complete=%u\n",
                                 span.context_address, span.render_pass, span.header_byte,
                                 span.data_begin_byte, span.data_end_byte, span.complete ? 1u : 0u);
                for (std::size_t i = 0; i < m_scene_primitives.size(); ++i) {
                    const auto& primitive = m_scene_primitives[i];
                    std::fprintf(stderr, "  scene[%zu] pcw=%08x isp=%08x tsp=%08x tcw=%08x vertices=%zu\n",
                                 i, primitive.pcw, primitive.isp, primitive.tsp[0],
                                 primitive.tcw[0], primitive.vertices.size());
                    for (std::size_t v = 0; v < std::min<std::size_t>(primitive.vertices.size(), 4); ++v) {
                        const auto& vertex = primitive.vertices[v];
                        std::fprintf(stderr, "    v%zu=(%.3f,%.3f,%.5f) uv=(%.4f,%.4f) rgba=(%.3f,%.3f,%.3f,%.3f)\n",
                                     v, vertex.x, vertex.y, vertex.z, vertex.u[0], vertex.v[0],
                                     vertex.base_color[0][0], vertex.base_color[0][1],
                                     vertex.base_color[0][2], vertex.base_color[0][3]);
                    }
                }
                if (const char* prefix = std::getenv("MULTIARCADERECOMP_NAOMI_DUMP_PVR_PREFIX");
                    prefix && *prefix && m_render_start_requests == 1) {
                    std::ofstream scene(std::string(prefix) + ".first-render-scene.txt");
                    if (scene) {
                        scene << "surface=" << m_render_surface_width << 'x' << m_render_surface_height
                              << " FB_W_CTRL=0x" << std::hex << m_render_fb_w_ctrl
                              << " FB_W_SOF1=0x" << m_render_fb_w_sof1 << std::dec
                              << " stride=" << m_render_fb_w_line_stride << '\n';
                        for (const auto& primitive : m_scene_primitives) {
                            scene << "pcw=0x" << std::hex << primitive.pcw << " isp=0x" << primitive.isp
                                  << " tsp=0x" << primitive.tsp[0] << " tcw=0x" << primitive.tcw[0]
                                  << std::dec << " vertices=" << primitive.vertices.size() << '\n';
                            for (const auto& vertex : primitive.vertices)
                                scene << vertex.x << ' ' << vertex.y << ' ' << vertex.z << ' '
                                      << vertex.u[0] << ' ' << vertex.v[0] << ' '
                                      << vertex.base_color[0][0] << ' ' << vertex.base_color[0][1] << ' '
                                      << vertex.base_color[0][2] << ' ' << vertex.base_color[0][3] << '\n';
                        }
                    }
                }
            }
            if (const char* prefix = std::getenv("MULTIARCADERECOMP_NAOMI_DUMP_PVR_PREFIX");
                prefix && *prefix &&
                (m_render_start_requests == 1 || m_render_start_requests == 2 ||
                 m_render_start_requests == 4 || m_render_start_requests == 8 ||
                 m_render_start_requests == 16 || m_render_start_requests == 32 ||
                 m_render_start_requests == 64 || m_render_start_requests == 96) &&
                !m_last_render_rgb.empty()) {
                char suffix[64]{};
                std::snprintf(suffix, sizeof(suffix), ".pvr-render-%03llu.ppm",
                              static_cast<unsigned long long>(m_render_start_requests));
                std::ofstream render_ppm(std::string(prefix) + suffix, std::ios::binary);
                render_ppm << "P6\n" << m_render_surface_width << ' '
                           << m_render_surface_height << "\n255\n";
                render_ppm.write(reinterpret_cast<const char*>(m_last_render_rgb.data()),
                                  static_cast<std::streamsize>(m_last_render_rgb.size()));

                const auto scanout = captureFramebuffer();
                std::snprintf(suffix, sizeof(suffix), ".scanout-%03llu.ppm",
                              static_cast<unsigned long long>(m_render_start_requests));
                std::ofstream scanout_ppm(std::string(prefix) + suffix, std::ios::binary);
                if (scanout.width != 0 && scanout.height != 0 &&
                    scanout.rgb.size() == static_cast<std::size_t>(scanout.width) *
                                              scanout.height * 3u) {
                    scanout_ppm << "P6\n" << scanout.width << ' ' << scanout.height << "\n255\n";
                    scanout_ppm.write(reinterpret_cast<const char*>(scanout.rgb.data()),
                                      static_cast<std::streamsize>(scanout.rgb.size()));
                }

                std::snprintf(suffix, sizeof(suffix), ".scene-%03llu.txt",
                              static_cast<unsigned long long>(m_render_start_requests));
                std::ofstream scene(std::string(prefix) + suffix);
                scene << "FB_R_SOF1=0x" << std::hex << m_registers[0x50u / 4u]
                      << " FB_W_SOF1=0x" << m_render_fb_w_sof1
                      << " FB_R_CTRL=0x" << m_registers[0x44u / 4u]
                      << " FB_W_CTRL=0x" << m_render_fb_w_ctrl
                      << " PARAM_BASE=0x" << m_registers[0x20u / 4u]
                      << " ISP_BACKGND_T=0x" << m_registers[0x8cu / 4u]
                      << std::dec << " surface=" << m_render_surface_width << 'x'
                      << m_render_surface_height << '\n';
                for (const auto& primitive : m_scene_primitives) {
                    scene << "pcw=0x" << std::hex << primitive.pcw
                          << " isp=0x" << primitive.isp << " tsp=0x" << primitive.tsp[0]
                          << " tcw=0x" << primitive.tcw[0] << std::dec
                          << " list=" << unsigned(primitive.list_type)
                          << " background=" << primitive.background
                          << " sprite=" << primitive.sprite
                          << " vertices=" << primitive.vertices.size() << '\n';
                    for (const auto& vertex : primitive.vertices)
                        scene << vertex.x << ' ' << vertex.y << ' ' << vertex.z << ' '
                              << vertex.u[0] << ' ' << vertex.v[0] << ' '
                              << vertex.base_color[0][0] << ' ' << vertex.base_color[0][1]
                              << ' ' << vertex.base_color[0][2] << ' '
                              << vertex.base_color[0][3] << '\n';
                }
            }
            ++m_register_writes;
            return;
        }
        if (aligned == kPvrCoreStart + 0x148u) {
            reg = merged & 0x00fffff8u;
            resetYuvConverter();
            ++m_register_writes;
            return;
        }
        if (aligned == kPvrCoreStart + 0x14cu) {
            reg = merged;
            resetYuvConverter();
            ++m_register_writes;
            return;
        }
        if (aligned == kTaListInit) {
            if (merged & 0x80000000u) {
                // Flycast pvr_regs.cpp::TA_LIST_INIT initializes the address
                // generators before ta_vtx_ListInit seeds this pass's OPBs.
                // These are live readback registers used by Naomi software;
                // treating LIST_INIT only as a FIFO reset leaves the guest's
                // TA allocator observing stale/zero pointers.
                m_registers[0x134u / 4u] = m_registers[0x164u / 4u]; // TA_NEXT_OPB
                m_registers[0x138u / 4u] = m_registers[0x128u / 4u]; // TA_ITP_CURRENT
                m_ta_context_active = true;
                m_ta_render_pass = 0;
                m_ta_fsm_state = 0;
                m_ta_tile_clip = (39u << 6u) | (14u << 17u);
                m_ta_context_bytes.clear();
                const auto context_address = m_registers[0x124u / 4u];
                if (std::find(m_known_ta_contexts.begin(), m_known_ta_contexts.end(),
                              context_address) == m_known_ta_contexts.end())
                    m_known_ta_contexts.push_back(context_address);
                m_ta_context_bytes[context_address] = 0;
                markObjectListBlocks(false);
                m_ta_stream.clear();
                m_ta_parse_offset = 0;
                m_ta_active_list = -1;
                m_ta_parameter_counts.fill(0);
                m_ta_list_packet_counts.fill(0);
                m_ta_lists.clear();
                m_ta_primitives.clear();
                m_scene_primitives.clear();
                m_ta_continuation_units = 0;
                m_ta_continuation_is_vertex = false;
                m_ta_continuation_ends_strip = false;
                m_ta_vertex_is_64_bytes = false;
                m_ta_active_list_begin = 0;
                m_ta_active_primitive = -1;
            }
            // Flycast handles TA_LIST_INIT as a command without storing data.
            if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
                ++g_pvr_trace_writes;
                std::fprintf(stderr, "Naomi PVR REG W address=%08x width=%u value=%08x command_readback=00000000\n",
                             address, width, value);
            }
            ++m_register_writes;
            return;
        }
        if (aligned == kPvrSoftReset && (merged & 1u)) {
            // Flycast pvr_regs.cpp::SOFTRESET calls ta_vtx_SoftReset(), which
            // returns the TA parser FSM to idle but does not erase submitted
            // FIFO bytes or completed render-pass data.
            m_ta_active_list = -1;
            m_ta_active_primitive = -1;
            m_ta_fsm_state = 0;
            m_ta_continuation_units = 0;
            m_ta_continuation_is_vertex = false;
            m_ta_continuation_ends_strip = false;
            m_ta_vertex_is_64_bytes = false;
            m_ta_active_list_begin = m_ta_parse_offset;
            ++m_register_writes;
            return; // Flycast does not store the SOFTRESET command.
        }
        // Flycast pvr_regs.cpp calls ta_vtx_ListInit(true) for every write:
        // start another TA render pass, but retain prior submitted FIFO data.
        if (aligned == kTaListContinue) {
            m_registers[0x134u / 4u] = m_registers[0x164u / 4u]; // TA_NEXT_OPB
            m_registers[0x138u / 4u] = m_registers[0x128u / 4u]; // TA_ITP_CURRENT
            m_ta_context_active = true;
            m_ta_fsm_state = 0;
            m_ta_tile_clip = (39u << 6u) | (14u << 17u);
            const auto context_address = m_registers[0x124u / 4u];
            if (std::find(m_known_ta_contexts.begin(), m_known_ta_contexts.end(),
                          context_address) == m_known_ta_contexts.end())
                m_known_ta_contexts.push_back(context_address);
            m_ta_context_bytes.try_emplace(context_address, 0);
            markObjectListBlocks(true);
            m_ta_active_primitive = -1;
            m_ta_active_list = -1;
            m_ta_vertex_is_64_bytes = false;
            m_ta_continuation_units = 0;
            m_ta_continuation_is_vertex = false;
            m_ta_continuation_ends_strip = false;
            m_ta_active_list_begin = m_ta_parse_offset;
        }
        reg = merged;
        if (aligned == kPvrCoreStart + 0x44u ||
            aligned == kPvrCoreStart + 0xd0u ||
            aligned == kPvrCoreStart + 0xd8u)
            recalculateTiming();
        if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_writes < 256) {
            ++g_pvr_trace_writes;
            std::fprintf(stderr, "Naomi PVR REG W address=%08x width=%u value=%08x merged=%08x\n",
                         address, width, value, reg);
        }
        ++m_register_writes;
        return;
    }
    if (address >= kArea1Start && address < kArea1End) {
        const auto offset = mapVram32((address - kArea1Start) & 0x00ffffffu);
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
    if (m_scanline == 0) {
        if (m_registers[0xd0u / 4u] & (1u << 4u))
            m_field_number = !m_field_number;
        else
            m_field_number = false;
    }
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
    if (m_render_done_cycles_remaining != 0) {
        if (cycles < m_render_done_cycles_remaining) {
            m_render_done_cycles_remaining -= cycles;
            cycles = 0;
        } else {
            cycles -= m_render_done_cycles_remaining;
            m_render_done_cycles_remaining = 0;
            if (raise_normal_irq) {
                // Flycast Renderer_if.cpp::rend_end_render raises TSP, ISP,
                // then video render-done at completion.
                raise_normal_irq(2);
                raise_normal_irq(1);
                raise_normal_irq(0);
            }
        }
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

void NaomiPvr2::setDmaBus(DmaRead32 read32, DmaWrite32 write32,
                          std::function<void(unsigned)> raise_normal_irq) {
    m_dma_read32 = std::move(read32);
    m_dma_write32 = std::move(write32);
    m_raise_normal_irq = std::move(raise_normal_irq);
}

void NaomiPvr2::startPvrDma() {
    if (!m_dma_read32 || !m_dma_write32) return;
    // Port of Flycast's pvr_sb_regs.cpp::do_pvr_dma: the SH-4 DMAC must be
    // enabled (DMAOR mask 0xffff8201 == 0x8201), then transfer exactly PDLEN
    // bytes and complete synchronously with Holly's PVR-DMA interrupt.
    constexpr std::uint32_t kDmaor = 0xffa00040u;
    constexpr std::uint32_t kDmaorMask = 0xffff8201u;
    const auto dmaor = m_dma_read32(kDmaor);
    if ((dmaor & kDmaorMask) != 0x8201u) {
        if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_dma_trace_events++ < 128)
            std::fprintf(stderr,
                         "Naomi PVR DMA rejected DMAOR=%08x mask=%08x PDSTAP=%08x PDSTAR=%08x PDLEN=%08x\n",
                         dmaor, dmaor & kDmaorMask, m_pvr_dma_pvr_address,
                         m_pvr_dma_system_address, m_pvr_dma_size);
        return;
    }

    const auto bytes = std::min<std::uint32_t>(m_pvr_dma_size, 16u * 1024u * 1024u);
    auto source = m_pvr_dma_direction ? m_pvr_dma_pvr_address : m_pvr_dma_system_address;
    auto destination = m_pvr_dma_direction ? m_pvr_dma_system_address : m_pvr_dma_pvr_address;
    // Flycast's block helper transfers complete words and ignores any tail.
    const auto transfer_bytes = bytes & ~3u;
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_dma_trace_events++ < 128)
        std::fprintf(stderr,
                     "Naomi PVR DMA start dir=%u src=%08x dst=%08x bytes=%08x DMAOR=%08x\n",
                     m_pvr_dma_direction ? 1u : 0u, source, destination,
                     transfer_bytes, dmaor);
    for (std::uint32_t offset = 0; offset < transfer_bytes; offset += 4u)
        m_dma_write32(destination + offset, m_dma_read32(source + offset));
    m_pvr_dma_bytes += transfer_bytes;
    ++m_pvr_dma_commands;
    m_dma_write32(0xffa00000u, m_pvr_dma_system_address + transfer_bytes); // DMAC SAR0
    m_dma_write32(0xffa00008u, 0); // DMAC DMATCR0
    const auto chcr0 = m_dma_read32(0xffa0000cu);
    m_dma_write32(0xffa0000cu, (chcr0 & ~1u) | 2u); // DE=0, TE=1
    m_pvr_dma_start = 0;
    m_interface_registers[0x18 / 4] = 0;
    if (m_raise_normal_irq) m_raise_normal_irq(11); // Holly PVR DMA bit.
}

NaomiPvrFramebuffer NaomiPvr2::captureFramebuffer() const {
    NaomiPvrFramebuffer frame;
    frame.control = m_registers[0x44u / 4]; // FB_R_CTRL
    frame.size = m_registers[0x5cu / 4];    // FB_R_SIZE
    frame.start = m_registers[0x50u / 4]; // FB_R_SOF1
    if ((frame.control & 1u) == 0 || (m_registers[0xe8u / 4u] & (1u << 3u)))
        return frame; // FB disabled or VO_CONTROL.blank_video set.

    const auto x_size = frame.size & 0x3ffu;
    const auto y_size = (frame.size >> 10u) & 0x3ffu;
    const auto modulus_raw = (frame.size >> 20u) & 0x3ffu;
    frame.width = (x_size + 1u) * 2u;
    frame.height = y_size + 1u;
    const auto depth = (frame.control >> 2u) & 3u;
    std::uint32_t bytes_per_pixel = 2;
    // Flycast's ReadFramebuffer uses signed pixel units here. The register
    // encodes the number of 16-bit words to skip minus one.
    std::int32_t modulus = (static_cast<std::int32_t>(modulus_raw) - 1) * 2;
    if (depth == 2u) {
        frame.width = (frame.width * 2u) / 3u;
        modulus = (modulus * 2u) / 3u;
        bytes_per_pixel = 3;
    } else if (depth == 3u) {
        frame.width /= 2u;
        modulus /= 2u;
        bytes_per_pixel = 4;
    }
    const bool interlaced = (m_registers[0xd0u / 4u] & (1u << 4u)) != 0;
    const auto field2_start = m_registers[0x54u / 4u]; // FB_R_SOF2
    if (interlaced) {
        if (static_cast<std::int32_t>(frame.width) == modulus &&
            field2_start == frame.start + frame.width * bytes_per_pixel) {
            modulus = 0;
            frame.height *= 2u;
        } else {
            frame.start = m_field_number ? field2_start : frame.start;
        }
    } else if ((frame.control & (1u << 23u)) == 0) {
        // Flycast treats a non-VGA, non-interlaced mode as 240p.
        frame.height = std::min(frame.height, 240u);
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
        const auto mapped = mapVram32(address & 0x00ffffffu);
        return m_vram[mapped % static_cast<std::uint32_t>(m_vram.size())];
    };
    auto put = [&frame](std::size_t pixel, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const auto at = pixel * 3u;
        frame.rgb[at] = r;
        frame.rgb[at + 1u] = g;
        frame.rgb[at + 2u] = b;
    };
    const auto concat = frame.control >> 4u & 7u;
    std::int64_t row_start = frame.start;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const auto row = static_cast<std::uint32_t>(
            (row_start % static_cast<std::int64_t>(m_vram.size()) +
             static_cast<std::int64_t>(m_vram.size())) % static_cast<std::int64_t>(m_vram.size()));
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
        row_start += (static_cast<std::int64_t>(frame.width) + modulus) * bytes_per_pixel;
    }
    return frame;
}

void NaomiPvr2::writeFramebufferToVram(
    std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> rgba) {
    const auto expected = static_cast<std::uint64_t>(width) * height * 4u;
    if (width == 0 || height == 0 || expected != rgba.size())
        throw std::invalid_argument("invalid RGBA render surface for PVR framebuffer writeback");

    const auto pack_mode = m_render_fb_w_ctrl & 7u;
    const std::uint32_t bytes_per_pixel = pack_mode <= 3u ? 2u
        : pack_mode == 4u ? 3u : 4u;
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * bytes_per_pixel;
    const auto padding = m_render_fb_w_line_stride > row_bytes
        ? static_cast<std::uint64_t>(m_render_fb_w_line_stride) - row_bytes : 0u;
    const auto kval = static_cast<std::uint8_t>(m_render_fb_w_ctrl >> 8u);
    const auto alpha_threshold = static_cast<std::uint8_t>(m_render_fb_w_ctrl >> 16u);
    const auto put = [this](std::uint32_t address, std::uint8_t byte) {
        const auto mapped = mapVram32(address & 0x00ffffffu);
        m_vram[mapped % m_vram.size()] = byte;
        ++m_vram_bytes_written;
    };

    const auto x_begin = std::min(m_render_clip_min_x, width);
    const auto x_end = std::min(width, m_render_clip_max_x + 1u);
    const auto y_begin = std::min(m_render_clip_min_y, height);
    const auto y_end = std::min(height, m_render_clip_max_y + 1u);
    if (x_end <= x_begin || y_end <= y_begin) return;
    for (std::uint32_t y = y_begin; y < y_end; ++y) {
        auto dst = static_cast<std::uint64_t>(m_render_fb_w_sof1 & 0x00ffffffu) +
                   static_cast<std::uint64_t>(y) * (row_bytes + padding) +
                   static_cast<std::uint64_t>(x_begin) * bytes_per_pixel;
        for (std::uint32_t x = x_begin; x < x_end; ++x) {
            const auto src = (static_cast<std::size_t>(y) * width + x) * 4u;
            const auto red = rgba[src];
            const auto green = rgba[src + 1u];
            const auto blue = rgba[src + 2u];
            const auto alpha = rgba[src + 3u];
            std::uint32_t packed = 0;
            unsigned bytes = bytes_per_pixel;
            switch (pack_mode) {
            case 0: // 0555 KRGB, FB_W_CTRL.KVAL[7] selects K.
                packed = ((red >> 3u) << 10u) | ((green >> 3u) << 5u) |
                         (blue >> 3u) | ((kval & 0x80u) << 8u);
                break;
            case 1: // 565 RGB.
                packed = ((red >> 3u) << 11u) | ((green >> 2u) << 5u) |
                         (blue >> 3u);
                break;
            case 2: // 4444 ARGB.
                packed = ((alpha >> 4u) << 12u) | ((red >> 4u) << 8u) |
                         ((green >> 4u) << 4u) | (blue >> 4u);
                break;
            case 3: // 1555 ARGB; A is thresholded.
                packed = ((alpha >= alpha_threshold ? 1u : 0u) << 15u) |
                         ((red >> 3u) << 10u) | ((green >> 3u) << 5u) |
                         (blue >> 3u);
                break;
            case 4: // packed 888 RGB in B, G, R byte order.
                packed = (static_cast<std::uint32_t>(red) << 16u) |
                         (static_cast<std::uint32_t>(green) << 8u) | blue;
                break;
            case 5: // 0888 KRGB.
                packed = (static_cast<std::uint32_t>(kval) << 24u) |
                         (static_cast<std::uint32_t>(red) << 16u) |
                         (static_cast<std::uint32_t>(green) << 8u) | blue;
                break;
            case 6: // 8888 ARGB.
                packed = (static_cast<std::uint32_t>(alpha) << 24u) |
                         (static_cast<std::uint32_t>(red) << 16u) |
                         (static_cast<std::uint32_t>(green) << 8u) | blue;
                break;
            default:
                throw std::invalid_argument("invalid FB_W_CTRL framebuffer pack mode");
            }
            for (unsigned byte = 0; byte < bytes; ++byte)
                put(static_cast<std::uint32_t>(dst++),
                    static_cast<std::uint8_t>(packed >> (byte * 8u)));
        }
    }
}

void NaomiPvr2::decodeTaScene() {
    m_scene_primitives.clear();
    // Flycast's BaseTAParser::reset() initializes the inherited face-color
    // caches to 0xff. Intensity vertices can legally inherit these values
    // until a face-color header replaces them, so preserve that parser state.
    std::array<float, 4> previous_mode2_base{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> previous_mode2_base1{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t decode_context_address = 0xffffffffu;

    const auto word_at = [this](std::size_t byte_offset) {
        if (byte_offset + 4u > m_ta_stream.size()) return 0u;
        return read_le32(m_ta_stream.data() + byte_offset);
    };
    const auto read_uv32 = [](const std::uint32_t* words, std::size_t u_word,
                              std::size_t v_word, NaomiPvrVertex& vertex,
                              unsigned volume) {
        vertex.u[volume] = std::bit_cast<float>(words[u_word]);
        vertex.v[volume] = std::bit_cast<float>(words[v_word]);
    };
    const auto read_uv16 = [](std::uint32_t packed, NaomiPvrVertex& vertex,
                              unsigned volume) {
        vertex.u[volume] = read_pvr_uv16(static_cast<std::uint16_t>(packed >> 16u));
        vertex.v[volume] = read_pvr_uv16(static_cast<std::uint16_t>(packed));
    };

    for (const auto& span : m_ta_primitives) {
        if (!span.complete || span.header_byte + 32u > m_ta_stream.size() ||
            span.data_begin_byte > span.data_end_byte ||
            span.data_end_byte > m_ta_stream.size())
            continue;
        const auto context = std::find(m_active_ta_contexts.begin(),
                                       m_active_ta_contexts.end(),
                                       span.context_address);
        if (context == m_active_ta_contexts.end())
            continue;
        if (decode_context_address != span.context_address) {
            // Flycast ta_parse_vdrc() resets BaseTAParser state for each
            // TA_context; mode-2 face colors cannot leak between contexts.
            previous_mode2_base = {1.0f, 1.0f, 1.0f, 1.0f};
            previous_mode2_base1 = {1.0f, 1.0f, 1.0f, 1.0f};
            decode_context_address = span.context_address;
        }

        NaomiPvrScenePrimitive primitive;
        primitive.list_type = span.list_type;
        primitive.render_pass = static_cast<std::uint32_t>(
            std::distance(m_active_ta_contexts.begin(), context));
        primitive.context_address = span.context_address;
        primitive.vertex_format = span.vertex_format;
        primitive.pcw = span.pcw;
        primitive.tile_clip = span.tile_clip;
        primitive.sprite = span.sprite;
        primitive.modifier_volume = span.modifier_volume;
        primitive.textured = (span.pcw & (1u << 3u)) != 0;
        primitive.dual_volume = span.vertex_format >= 9u && span.vertex_format <= 14u;

        std::array<std::uint32_t, 16> header{};
        for (std::size_t i = 0; i < 16; ++i)
            header[i] = word_at(static_cast<std::size_t>(span.header_byte) + i * 4u);
        primitive.isp = header[1];
        primitive.tsp[0] = header[2];
        primitive.tcw[0] = header[3];
        if (span.vertex_format >= 11u && span.vertex_format <= 14u) {
            primitive.tsp[1] = header[4];
            primitive.tcw[1] = header[5];
        }

        std::array<float, 4> face_base{};
        std::array<float, 4> face_offset{};
        std::array<float, 4> face_base1{};
        std::array<float, 4> face_offset1{};
        const unsigned color_type = (span.pcw >> 4u) & 3u;
        // PCW.Offset only enables the color-offset channel for textured
        // polygons. Flycast's TA FIFO parser forces this bit off otherwise.
        const bool offset_color = primitive.textured && ((span.pcw >> 2u) & 1u) != 0;
        if (primitive.dual_volume && color_type == 2u) {
            read_float_rgba(header.data() + 8, face_base.data());
            read_float_rgba(header.data() + 12, face_base1.data());
            previous_mode2_base = face_base;
            previous_mode2_base1 = face_base1;
        } else if (primitive.dual_volume && color_type == 3u) {
            face_base = previous_mode2_base;
            face_base1 = previous_mode2_base1;
        } else if (color_type == 2u) {
            if (offset_color) {
                read_float_rgba(header.data() + 8, face_base.data());
                read_float_rgba(header.data() + 12, face_offset.data());
            } else {
                read_float_rgba(header.data() + 4, face_base.data());
            }
            previous_mode2_base = face_base;
        } else if (color_type == 3u) {
            face_base = previous_mode2_base;
        }

        if (span.sprite) {
            if (span.data_begin_byte + 64u > span.data_end_byte) continue;
            const auto* packet = m_ta_stream.data() + span.data_begin_byte;
            std::array<std::uint32_t, 16> words{};
            for (std::size_t i = 0; i < words.size(); ++i)
                words[i] = read_le32(packet + i * 4u);
            primitive.vertices.resize(4);
            auto& v0 = primitive.vertices[0];
            auto& v1 = primitive.vertices[1];
            auto& v2 = primitive.vertices[2];
            auto& v3 = primitive.vertices[3];
            v2.x = std::bit_cast<float>(words[1]);
            v2.y = std::bit_cast<float>(words[2]);
            v2.z = std::bit_cast<float>(words[3]);
            v3.x = std::bit_cast<float>(words[4]);
            v3.y = std::bit_cast<float>(words[5]);
            v3.z = std::bit_cast<float>(words[6]);
            v1.x = std::bit_cast<float>(words[7]);
            v1.y = std::bit_cast<float>(words[8]);
            v1.z = std::bit_cast<float>(words[9]);
            v0.x = std::bit_cast<float>(words[10]);
            v0.y = std::bit_cast<float>(words[11]);
            unpack_argb(header[4], v0.base_color[0]);
            unpack_argb(header[4], v1.base_color[0]);
            unpack_argb(header[4], v2.base_color[0]);
            unpack_argb(header[4], v3.base_color[0]);
            unpack_argb(header[5], v0.offset_color[0]);
            unpack_argb(header[5], v1.offset_color[0]);
            unpack_argb(header[5], v2.offset_color[0]);
            unpack_argb(header[5], v3.offset_color[0]);
            read_uv16(words[13], v2, 0);
            read_uv16(words[14], v3, 0);
            read_uv16(words[15], v1, 0);

            // Flycast calculates the fourth vertex's reciprocal depth and UV
            // from the affine sprite plane defined by vertices 1, 2 and 3.
            const float acx = v1.x - v2.x, acy = v1.y - v2.y;
            const float acz = v1.z - v2.z, acu = v1.u[0] - v2.u[0], acv = v1.v[0] - v2.v[0];
            const float abx = v3.x - v2.x, aby = v3.y - v2.y;
            const float abz = v3.z - v2.z, abu = v3.u[0] - v2.u[0], abv = v3.v[0] - v2.v[0];
            const float apx = v0.x - v2.x, apy = v0.y - v2.y;
            const float determinant = acx * aby - acy * abx;
            if (determinant != 0.0f) {
                const float k2 = (apx * aby - apy * abx) / determinant;
                const float k1 = abx != 0.0f ? (apx - k2 * acx) / abx
                                             : (apy - k2 * acy) / aby;
                v0.z = v2.z + k1 * abz + k2 * acz;
                v0.u[0] = v2.u[0] + k1 * abu + k2 * acu;
                v0.v[0] = v2.v[0] + k1 * abv + k2 * acv;
            }
        } else if (span.modifier_volume) {
            for (std::size_t at = static_cast<std::size_t>(span.data_begin_byte);
                 at + 64u <= span.data_end_byte; at += 64u) {
                std::array<std::uint32_t, 16> words{};
                for (std::size_t i = 0; i < words.size(); ++i)
                    words[i] = word_at(at + i * 4u);
                NaomiPvrModifierTriangle triangle;
                for (unsigned i = 0; i < 7; ++i)
                    triangle.xyz[i] = std::bit_cast<float>(words[i + 1u]);
                triangle.xyz[7] = std::bit_cast<float>(words[8]);
                triangle.xyz[8] = std::bit_cast<float>(words[9]);
                primitive.modifier_triangles.push_back(triangle);
            }
        } else if (span.vertex_format <= 14u) {
            const unsigned format = span.vertex_format;
            for (std::size_t at = static_cast<std::size_t>(span.data_begin_byte);
                 at + span.vertex_packet_bytes <= span.data_end_byte;
                 at += span.vertex_packet_bytes) {
                std::array<std::uint32_t, 16> words{};
                const auto* packet = m_ta_stream.data() + at;
                for (std::size_t i = 0; i < span.vertex_packet_bytes / 4u; ++i)
                    words[i] = read_le32(packet + i * 4u);
                NaomiPvrVertex vertex;
                vertex.x = std::bit_cast<float>(words[1]);
                vertex.y = std::bit_cast<float>(words[2]);
                vertex.z = std::bit_cast<float>(words[3]);
                switch (format) {
                case 0:
                    unpack_argb(words[6], vertex.base_color[0]);
                    break;
                case 1:
                    read_float_rgba(words.data() + 4, vertex.base_color[0]);
                    quantize_rgba(vertex.base_color[0]);
                    break;
                case 2:
                    apply_intensity(face_base.data(), std::bit_cast<float>(words[6]),
                                    vertex.base_color[0]);
                    break;
                case 3: case 11:
                    read_uv32(words.data(), 4, 5, vertex, 0);
                    unpack_argb(words[6], vertex.base_color[0]);
                    unpack_argb(words[7], vertex.offset_color[0]);
                    if (format == 11) {
                        read_uv32(words.data(), 8, 9, vertex, 1);
                        unpack_argb(words[10], vertex.base_color[1]);
                        unpack_argb(words[11], vertex.offset_color[1]);
                    }
                    break;
                case 4: case 12:
                    read_uv16(words[4], vertex, 0);
                    unpack_argb(words[6], vertex.base_color[0]);
                    unpack_argb(words[7], vertex.offset_color[0]);
                    if (format == 12) {
                        read_uv16(words[8], vertex, 1);
                        unpack_argb(words[10], vertex.base_color[1]);
                        unpack_argb(words[11], vertex.offset_color[1]);
                    }
                    break;
                case 5: case 6:
                    if (format == 5) read_uv32(words.data(), 4, 5, vertex, 0);
                    else read_uv16(words[4], vertex, 0);
                    read_float_rgba(words.data() + 8, vertex.base_color[0]);
                    read_float_rgba(words.data() + 12, vertex.offset_color[0]);
                    quantize_rgba(vertex.base_color[0]);
                    quantize_rgba(vertex.offset_color[0]);
                    break;
                case 7: case 13:
                    read_uv32(words.data(), 4, 5, vertex, 0);
                    apply_intensity(face_base.data(), std::bit_cast<float>(words[6]),
                                    vertex.base_color[0]);
                    apply_intensity(face_offset.data(), std::bit_cast<float>(words[7]),
                                    vertex.offset_color[0]);
                    if (format == 13) {
                        read_uv32(words.data(), 8, 9, vertex, 1);
                        apply_intensity(face_base1.data(), std::bit_cast<float>(words[10]),
                                        vertex.base_color[1]);
                        apply_intensity(face_offset1.data(), std::bit_cast<float>(words[11]),
                                        vertex.offset_color[1]);
                    }
                    break;
                case 8: case 14:
                    read_uv16(words[4], vertex, 0);
                    apply_intensity(face_base.data(), std::bit_cast<float>(words[6]),
                                    vertex.base_color[0]);
                    apply_intensity(face_offset.data(), std::bit_cast<float>(words[7]),
                                    vertex.offset_color[0]);
                    if (format == 14) {
                        read_uv16(words[8], vertex, 1);
                        apply_intensity(face_base1.data(), std::bit_cast<float>(words[10]),
                                        vertex.base_color[1]);
                        apply_intensity(face_offset1.data(), std::bit_cast<float>(words[11]),
                                        vertex.offset_color[1]);
                    }
                    break;
                case 9:
                    unpack_argb(words[4], vertex.base_color[0]);
                    unpack_argb(words[5], vertex.base_color[1]);
                    break;
                case 10:
                    apply_intensity(face_base.data(), std::bit_cast<float>(words[4]),
                                    vertex.base_color[0]);
                    apply_intensity(face_base1.data(), std::bit_cast<float>(words[5]),
                                    vertex.base_color[1]);
                    break;
                default:
                    break;
                }
                primitive.vertices.push_back(vertex);
                if (word_at(at) & (1u << 28u)) {
                    primitive.strip_end_vertex_indices.push_back(primitive.vertices.size() - 1u);
                }
            }
        }
        m_scene_primitives.push_back(std::move(primitive));
    }
    decodeBackgroundPlane();
}

void NaomiPvr2::decodeBackgroundPlane() {
    const auto read_vram32 = [this](std::uint32_t address) {
        const auto mapped = mapVram32(address & 0x00ffffffu);
        std::uint32_t value = 0;
        for (unsigned lane = 0; lane < 4; ++lane)
            value |= static_cast<std::uint32_t>(
                m_vram[(mapped + lane) % m_vram.size()]) << (lane * 8u);
        return value;
    };

    // Flycast ta_vtx.cpp::FillBGP reads a separate ISP/TSP/TCW strip selected
    // by ISP_BACKGND_T. It is the first opaque polygon, not a TA FIFO packet.
    const auto param_base = m_registers[0x20u / 4u] & 0x00f00000u;
    const auto background_tag = m_registers[0x8cu / 4u];
    const auto tag_address = (background_tag >> 3u) & 0x001fffffu;
    const auto strip_base = (param_base + tag_address * 4u) & 0x00ffffffu;
    const auto raw_isp = read_vram32(strip_base);

    NaomiPvrScenePrimitive primitive;
    primitive.background = true;
    primitive.list_type = 0;
    primitive.isp = (raw_isp & ~0xf8000000u) | (7u << 29u); // depth always, no culling
    primitive.tsp[0] = read_vram32(strip_base + 4u);
    primitive.tcw[0] = read_vram32(strip_base + 8u);
    primitive.textured = (raw_isp & (1u << 25u)) != 0;
    primitive.pcw = ((raw_isp >> 22u) & 0x0fu) |
                    (((background_tag >> 27u) & 1u) << 7u);
    primitive.tile_clip = 0; // Flycast disables tile clipping for the background plane.

    const auto skip = (background_tag >> 24u) & 7u;
    const bool intensity_shadow = (m_registers[0x74u / 4u] & (1u << 8u)) != 0;
    const bool shadow = (background_tag & (1u << 27u)) != 0;
    auto stride = 3u + skip;
    if (intensity_shadow && shadow) stride += skip;
    stride *= 4u;
    auto vertex_address = strip_base + ((background_tag & 7u) * stride) + 12u;

    const bool textured = primitive.textured;
    const bool uv16 = (raw_isp & (1u << 22u)) != 0;
    const bool offset_color = (raw_isp & (1u << 24u)) != 0;
    primitive.vertices.resize(4);
    for (unsigned index = 0; index < 3; ++index) {
        auto& vertex = primitive.vertices[index];
        vertex.x = std::bit_cast<float>(read_vram32(vertex_address));
        vertex.y = std::bit_cast<float>(read_vram32(vertex_address + 4u));
        vertex.z = std::bit_cast<float>(read_vram32(vertex_address + 8u));
        auto cursor = vertex_address + 12u;
        if (textured) {
            if (uv16) {
                const auto packed_uv = read_vram32(cursor);
                vertex.u[0] = std::bit_cast<float>((packed_uv & 0xffffu) << 16u);
                vertex.v[0] = std::bit_cast<float>((packed_uv >> 16u) << 16u);
                cursor += 4u;
            } else {
                vertex.u[0] = std::bit_cast<float>(read_vram32(cursor));
                vertex.v[0] = std::bit_cast<float>(read_vram32(cursor + 4u));
                cursor += 8u;
            }
        }
        unpack_argb(read_vram32(cursor), vertex.base_color[0]);
        cursor += 4u;
        if (offset_color)
            unpack_argb(read_vram32(cursor), vertex.offset_color[0]);
        vertex_address += stride;
    }

    const auto background_depth = std::max(
        std::bit_cast<float>(m_registers[0x88u / 4u]) - 1.0e-6f, 1.0e-11f);
    for (unsigned index = 0; index < 3; ++index)
        primitive.vertices[index].z = background_depth;

    const float scale_x = (m_registers[0xf4u / 4u] & (1u << 16u)) ? 2.0f : 1.0f;
    auto& v0 = primitive.vertices[0];
    auto& v1 = primitive.vertices[1];
    auto& v2 = primitive.vertices[2];
    auto& v3 = primitive.vertices[3];
    if (!textured) {
        v0.x = -256.0f * scale_x;
        v0.y = 0.0f;
        v1.x = 896.0f * scale_x;
        v1.y = 0.0f;
        v2.x = v0.x;
        v2.y = 480.0f;
        v3 = v2;
        v3.x = v1.x;
    } else {
        if (v2.x == v1.x) {
            v2.x = v0.x;
            v2.u[0] = v0.u[0];
        }
        const float delta_u = (v1.u[0] - v0.u[0]) * 0.4f;
        v0.x -= 256.0f;
        v0.u[0] -= delta_u;
        v1.x += 256.0f;
        v1.u[0] += delta_u;
        v2.x -= 256.0f;
        v2.u[0] -= delta_u;
        v0.x *= scale_x;
        v1.x *= scale_x;
        v2.x *= scale_x;
        v3 = v2;
        v3.x = v1.x;
        v3.u[0] = v1.u[0];
    }
    m_scene_primitives.insert(m_scene_primitives.begin(), std::move(primitive));
}

std::array<float, 4> NaomiPvr2::sampleTexture(
    std::uint32_t tsp, std::uint32_t tcw, float u, float v, float lod) const {
    const auto tex_u = (tsp >> 3u) & 7u;
    const auto tex_v = tsp & 7u;
    const std::uint32_t base_width = 8u << tex_u;
    const auto pixel_format = (tcw >> 27u) & 7u;
    const bool scan_order = (tcw & (1u << 26u)) != 0;
    const bool planar = scan_order && pixel_format <= 4u;
    const bool vq = (tcw & (1u << 30u)) != 0;
    const bool mipmapped = (tcw & (1u << 31u)) != 0 && !planar;
    // Flycast TexCache.cpp::BaseTextureCacheData forces height=width for
    // twiddled mipmapped textures; TexV is ignored by the PVR in this mode.
    const std::uint32_t base_height = mipmapped ? base_width : (8u << tex_v);
    std::uint32_t width = base_width;
    std::uint32_t height = base_height;
    const std::uint32_t base = ((tcw & 0x001fffffu) << 3u) & 0x00ffffffu;
    const std::uint32_t palette_select = (tcw >> 21u) & 0x3fu;
    const std::uint32_t palette_mode = m_registers[0x108u / 4u] & 3u;
    const bool clamp_u = (tsp & (1u << 16u)) != 0;
    const bool clamp_v = (tsp & (1u << 15u)) != 0;
    const bool mirror_u = (tsp & (1u << 18u)) != 0;
    const bool mirror_v = (tsp & (1u << 17u)) != 0;
    if (mipmapped && ((tsp >> 13u) & 3u) > 1u) {
        // Flycast's Vulkan/OIT drawers submit two mip passes for trilinear
        // filtering. Their blend is 0.25 * (MipMapD & 3); sample the two
        // adjacent levels here and combine them in the equivalent software
        // path. FilterMode=1 prevents this recursive call from taking the
        // trilinear branch again while retaining bilinear filtering.
        const auto bilinear_tsp = (tsp & ~(3u << 13u)) | (1u << 13u);
        const auto lower = sampleTexture(bilinear_tsp, tcw, u, v, lod);
        const auto upper = sampleTexture(bilinear_tsp, tcw, u, v, lod + 1.0f);
        const float blend = static_cast<float>((tsp >> 9u) & 3u) * 0.25f;
        std::array<float, 4> result{};
        for (unsigned channel = 0; channel < 4; ++channel)
            result[channel] = lower[channel] + (upper[channel] - lower[channel]) * blend;
        return result;
    }
    static constexpr std::array<float, 16> d_adjust_lod_bias{
        0.0f, -4.0f, -2.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const auto mip_point = [](unsigned level) {
        static constexpr std::array<std::uint32_t, 11> points{
            3, 4, 8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558,
            0x15558, 0x55558};
        return points[std::min(level, 10u)];
    };
    const auto vq_mip_point = [](unsigned level) {
        static constexpr std::array<std::uint32_t, 11> points{
            2048, 2049, 2050, 2054, 2070, 2134, 2390, 3414, 7510,
            23894, 89430};
        return points[std::min(level, 10u)];
    };
    const auto max_level = std::min(tex_u + 3u, 10u);
    const auto selected_level = mipmapped
        ? std::min(static_cast<unsigned>(std::clamp(std::floor(lod + d_adjust_lod_bias[(tsp >> 9u) & 15u] + 0.5f),
                                                    0.0f, static_cast<float>(max_level))), max_level)
        : 0u;
    width = std::max(1u, base_width >> selected_level);
    height = std::max(1u, base_height >> selected_level);
    const std::uint32_t bpp = pixel_format == 5u ? 4u : pixel_format == 6u ? 8u : 16u;
    const std::uint32_t stride = (tcw & (1u << 25u))
        ? (((m_registers[0xe4u / 4u] & 31u) * 32u) != 0
               ? (m_registers[0xe4u / 4u] & 31u) * 32u : width)
        : width;
    const std::uint32_t data_base = base + (mipmapped
        ? (vq ? vq_mip_point(max_level - selected_level)
              : mip_point(max_level - selected_level) * bpp / 8u)
        : (vq ? 2048u : 0u));
    const auto byte_at = [this](std::uint32_t address) {
        return m_vram[address % m_vram.size()];
    };
    const auto read16_at = [&byte_at](std::uint32_t address) {
        return static_cast<std::uint16_t>(byte_at(address) |
            (static_cast<std::uint16_t>(byte_at(address + 1u)) << 8u));
    };
    const auto read32_at = [&byte_at](std::uint32_t address) {
        return static_cast<std::uint32_t>(byte_at(address)) |
            (static_cast<std::uint32_t>(byte_at(address + 1u)) << 8u) |
            (static_cast<std::uint32_t>(byte_at(address + 2u)) << 16u) |
            (static_cast<std::uint32_t>(byte_at(address + 3u)) << 24u);
    };
    const auto twiddle_slow = [](std::uint32_t x, std::uint32_t y,
                                 std::uint32_t x_size, std::uint32_t y_size) {
        std::uint32_t result = 0, shift = 0;
        x_size >>= 1u;
        y_size >>= 1u;
        while (x_size != 0 || y_size != 0) {
            if (y_size != 0) {
                result |= (y & 1u) << shift;
                y_size >>= 1u; y >>= 1u; ++shift;
            }
            if (x_size != 0) {
                result |= (x & 1u) << shift;
                x_size >>= 1u; x >>= 1u; ++shift;
            }
        }
        return result;
    };
    const auto twiddle = [&twiddle_slow](std::uint32_t x, std::uint32_t y,
                                         std::uint32_t w, std::uint32_t h) {
        // Port Flycast texconv.cpp::twop and its detwiddle tables. The first
        // table is indexed by log2(height), the second by log2(width); both
        // use the fixed 1024-pixel table extent. Passing texture dimensions
        // directly here shifts the x bits into the wrong bank and repeats
        // unrelated texels (visible as four Ns instead of the source glyph).
        const auto bcx = static_cast<unsigned>(std::bit_width(w) - 1u);
        const auto bcy = static_cast<unsigned>(std::bit_width(h) - 1u);
        return twiddle_slow(x, 0, 1024u, 1u << bcy) +
               twiddle_slow(0, y, 1u << bcx, 1024u);
    };
    const auto unpack16 = [pixel_format](std::uint16_t word) {
        std::array<float, 4> c{};
        if (pixel_format == 0u || pixel_format == 7u) {
            const auto a = (word >> 15u) & 1u;
            const auto r = (word >> 10u) & 31u;
            const auto g = (word >> 5u) & 31u;
            const auto b = word & 31u;
            c = {static_cast<float>((r << 3u) | (r >> 2u)) / 255.0f,
                 static_cast<float>((g << 3u) | (g >> 2u)) / 255.0f,
                 static_cast<float>((b << 3u) | (b >> 2u)) / 255.0f,
                 static_cast<float>(a)};
        } else if (pixel_format == 1u) {
            const auto r = (word >> 11u) & 31u;
            const auto g = (word >> 5u) & 63u;
            const auto b = word & 31u;
            c = {static_cast<float>((r << 3u) | (r >> 2u)) / 255.0f,
                 static_cast<float>((g << 2u) | (g >> 4u)) / 255.0f,
                 static_cast<float>((b << 3u) | (b >> 2u)) / 255.0f, 1.0f};
        } else {
            const auto a = (word >> 12u) & 15u;
            const auto r = (word >> 8u) & 15u;
            const auto g = (word >> 4u) & 15u;
            const auto b = word & 15u;
            c = {static_cast<float>(r * 17u) / 255.0f,
                 static_cast<float>(g * 17u) / 255.0f,
                 static_cast<float>(b * 17u) / 255.0f,
                 static_cast<float>(a * 17u) / 255.0f};
        }
        return c;
    };
    const auto unpack_palette = [this, palette_mode, palette_select](unsigned index,
                                                                     unsigned format) {
        const unsigned base_index = format == 5u ? palette_select << 4u
                                                 : (palette_select >> 4u) << 8u;
        const auto word = m_registers[(0x1000u / 4u) + ((base_index + index) & 1023u)];
        std::array<float, 4> c{};
        if (palette_mode == 0u) {
            const auto a = (word >> 15u) & 1u;
            const auto r = (word >> 10u) & 31u;
            const auto g = (word >> 5u) & 31u;
            const auto b = word & 31u;
            c = {static_cast<float>((r << 3u) | (r >> 2u)) / 255.0f,
                 static_cast<float>((g << 3u) | (g >> 2u)) / 255.0f,
                 static_cast<float>((b << 3u) | (b >> 2u)) / 255.0f,
                 static_cast<float>(a)};
        } else if (palette_mode == 1u) {
            const auto r = (word >> 11u) & 31u;
            const auto g = (word >> 5u) & 63u;
            const auto b = word & 31u;
            c = {static_cast<float>((r << 3u) | (r >> 2u)) / 255.0f,
                 static_cast<float>((g << 2u) | (g >> 4u)) / 255.0f,
                 static_cast<float>((b << 3u) | (b >> 2u)) / 255.0f, 1.0f};
        } else if (palette_mode == 2u) {
            c = {static_cast<float>(((word >> 8u) & 15u) * 17u) / 255.0f,
                 static_cast<float>(((word >> 4u) & 15u) * 17u) / 255.0f,
                 static_cast<float>((word & 15u) * 17u) / 255.0f,
                 static_cast<float>(((word >> 12u) & 15u) * 17u) / 255.0f};
        } else {
            c = {static_cast<float>((word >> 16u) & 0xffu) / 255.0f,
                 static_cast<float>((word >> 8u) & 0xffu) / 255.0f,
                 static_cast<float>(word & 0xffu) / 255.0f,
                 static_cast<float>((word >> 24u) & 0xffu) / 255.0f};
        }
        return c;
    };
    const auto decode_codebook = [&](std::uint32_t code, unsigned x, unsigned y) {
        const auto address = base + code * 8u;
        if (planar && pixel_format != 3u)
            return unpack16(read16_at(address + (x & 3u) * 2u));
        if (planar && pixel_format == 3u) {
            const auto pair = x & 3u;
            const auto packed = read32_at(address + (pair / 2u) * 4u);
            const int yy = static_cast<int>((packed >> ((pair & 1u) ? 24u : 8u)) & 0xffu);
            const int yu = static_cast<int>(packed & 0xffu) - 128;
            const int yv = static_cast<int>((packed >> 16u) & 0xffu) - 128;
            return std::array<float, 4>{
                std::clamp(yy + yv * 11 / 8, 0, 255) / 255.0f,
                std::clamp(yy - (yu * 11 + yv * 22) / 32, 0, 255) / 255.0f,
                std::clamp(yy + yu * 110 / 64, 0, 255) / 255.0f, 1.0f};
        }
        if (pixel_format == 5u || pixel_format == 6u) {
            unsigned palette_index = 0;
            if (pixel_format == 6u) {
                const unsigned lane = ((y & 3u) >> 1u) * 4u +
                                      (x & 1u) * 2u + (y & 1u);
                palette_index = byte_at(address + lane);
            } else {
                const unsigned lane = ((x & 3u) >> 1u) * 8u +
                    ((y & 3u) >> 1u) * 4u + ((x & 1u) * 2u) + (y & 1u);
                const auto packed = byte_at(address + lane / 2u);
                palette_index = (lane & 1u) ? packed >> 4u : packed & 15u;
            }
            return unpack_palette(palette_index, pixel_format);
        }
        const unsigned lane = (x & 1u) * 2u + (y & 1u);
        if (pixel_format == 3u) {
            const unsigned row = y & 1u;
            const auto yuv0 = read16_at(address + row * 4u);
            const auto yuv1 = read16_at(address + row * 4u + 4u);
            const int yy = static_cast<int>((x & 1u) ? (yuv1 >> 8u) : (yuv0 >> 8u));
            const int yu = static_cast<int>(yuv0 & 0xffu) - 128;
            const int yv = static_cast<int>(yuv1 & 0xffu) - 128;
            const auto r = std::clamp(yy + yv * 11 / 8, 0, 255);
            const auto g = std::clamp(yy - (yu * 11 + yv * 22) / 32, 0, 255);
            const auto b = std::clamp(yy + yu * 110 / 64, 0, 255);
            return std::array<float, 4>{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
        }
        return unpack16(read16_at(address + lane * 2u));
    };
    const auto texel = [&](unsigned x, unsigned y) {
        x %= width; y %= height;
        if (planar) {
            if (vq) {
                const auto block = y * (width / 4u) + x / 4u;
                return decode_codebook(byte_at(data_base + block), x, 0);
            }
            const auto row = data_base + y * stride * 2u;
            if (pixel_format == 3u) {
                const auto group = row + (x / 4u) * 8u;
                const auto packed = read32_at(group + ((x % 4u) / 2u) * 4u);
                const auto pair = x & 1u;
                const int yy = static_cast<int>((packed >> (pair ? 24u : 8u)) & 0xffu);
                const int yu = static_cast<int>(packed & 0xffu) - 128;
                const int yv = static_cast<int>((packed >> 16u) & 0xffu) - 128;
                return std::array<float, 4>{
                    std::clamp(yy + yv * 11 / 8, 0, 255) / 255.0f,
                    std::clamp(yy - (yu * 11 + yv * 22) / 32, 0, 255) / 255.0f,
                    std::clamp(yy + yu * 110 / 64, 0, 255) / 255.0f, 1.0f};
            }
            return unpack16(read16_at(row + x * 2u));
        }
        const bool pal4 = pixel_format == 5u;
        const bool pal8 = pixel_format == 6u;
        const unsigned group_w = pal4 ? 4u : 2u;
        const unsigned group_h = pal4 ? 4u : pal8 ? 4u : 2u;
        const unsigned divider = group_w * group_h;
        unsigned lane = 0;
        if (pal4) {
            const auto x2 = x & 3u, y2 = y & 3u;
            lane = (x2 >> 1u) * 8u + (y2 >> 1u) * 4u + (x2 & 1u) * 2u + (y2 & 1u);
        } else if (pal8) {
            lane = ((y & 3u) >> 1u) * 4u + (x & 1u) * 2u + (y & 1u);
        } else if (!scan_order) {
            lane = (x & 1u) * 2u + (y & 1u);
        }
        const auto group_x = x & ~(group_w - 1u);
        const auto group_y = y & ~(group_h - 1u);
        const unsigned block_index = twiddle(group_x, group_y, width, height) / divider;
        if (vq) {
            const auto index_address = data_base + block_index;
            return decode_codebook(byte_at(index_address), x, y);
        }
        const auto address = data_base + block_index * 8u;
        if (pal4) {
            const auto packed = byte_at(address + lane / 2u);
            return unpack_palette((lane & 1u) ? packed >> 4u : packed & 15u, pixel_format);
        }
        if (pal8)
            return unpack_palette(byte_at(address + lane), pixel_format);
        if (pixel_format == 3u) {
            const auto yuv_word = read16_at(address + (lane * 2u));
            const auto pair_base = address + ((lane & 1u) * 2u);
            const auto first = read16_at(pair_base);
            const auto second = read16_at(pair_base + 4u);
            const int yy = static_cast<int>(yuv_word >> 8u);
            const int yu = static_cast<int>(first & 0xffu) - 128;
            const int yv = static_cast<int>(second & 0xffu) - 128;
            return std::array<float, 4>{
                std::clamp(yy + yv * 11 / 8, 0, 255) / 255.0f,
                std::clamp(yy - (yu * 11 + yv * 22) / 32, 0, 255) / 255.0f,
                std::clamp(yy + yu * 110 / 64, 0, 255) / 255.0f, 1.0f};
        }
        return unpack16(read16_at(address + lane * 2u));
    };
    auto wrap = [](float coord, std::uint32_t size, bool clamp, bool mirror) {
        if (clamp) return std::clamp(coord, 0.0f, 1.0f);
        float whole = std::floor(coord);
        float fraction = coord - whole;
        auto tile = static_cast<std::int64_t>(whole);
        if (mirror && (tile & 1)) fraction = 1.0f - fraction;
        return fraction;
    };
    u = wrap(u, width, clamp_u, mirror_u);
    v = wrap(v, height, clamp_v, mirror_v);
    const bool linear = ((tsp >> 13u) & 3u) != 0;
    if (!linear) {
        const auto x = std::min(width - 1u, static_cast<unsigned>(u * width));
        const auto y = std::min(height - 1u, static_cast<unsigned>(v * height));
        return texel(x, y);
    }
    const float x = u * width - 0.5f;
    const float y = v * height - 0.5f;
    const auto x0 = static_cast<int>(std::floor(x));
    const auto y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    auto index_coord = [](int value, unsigned size, bool clamp, bool mirror) {
        if (clamp) return static_cast<unsigned>(std::clamp(value, 0, static_cast<int>(size) - 1));
        const int period = mirror ? static_cast<int>(size * 2u) : static_cast<int>(size);
        int wrapped = value % period;
        if (wrapped < 0) wrapped += period;
        if (mirror && wrapped >= static_cast<int>(size)) wrapped = period - 1 - wrapped;
        return static_cast<unsigned>(wrapped);
    };
    const auto ix0 = index_coord(x0, width, clamp_u, mirror_u);
    const auto ix1 = index_coord(x0 + 1, width, clamp_u, mirror_u);
    const auto iy0 = index_coord(y0, height, clamp_v, mirror_v);
    const auto iy1 = index_coord(y0 + 1, height, clamp_v, mirror_v);
    const auto c00 = texel(ix0, iy0), c10 = texel(ix1, iy0);
    const auto c01 = texel(ix0, iy1), c11 = texel(ix1, iy1);
    std::array<float, 4> out{};
    for (unsigned channel = 0; channel < 4; ++channel) {
        const float top = c00[channel] + (c10[channel] - c00[channel]) * fx;
        const float bottom = c01[channel] + (c11[channel] - c01[channel]) * fx;
        out[channel] = top + (bottom - top) * fy;
    }
    return out;
}

std::vector<std::uint8_t> NaomiPvr2::rasterizeTaScene() const {
    // Software colour pass adapted from Flycast's PVR renderer. This pass
    // samples texture formats and writes fragments; exact pass sorting, fog
    // and modifier-volume operations remain separate work.
    const auto width = m_render_surface_width;
    const auto height = m_render_surface_height;
    const auto pixel_count = static_cast<std::uint64_t>(width) * height;
    if (pixel_count == 0 || pixel_count > 16u * 1024u * 1024u)
        return {};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixel_count) * 4u, 0u);
    std::vector<float> depth(static_cast<std::size_t>(pixel_count), 1.0f);
    const auto clip_left = std::min(m_render_tile_clip[0], width);
    const auto clip_top = std::min(m_render_tile_clip[1], height);
    const auto clip_right = std::min(width, clip_left + m_render_tile_clip[2]);
    const auto clip_bottom = std::min(height, clip_top + m_render_tile_clip[3]);
    if (clip_right <= clip_left || clip_bottom <= clip_top)
        return rgba;

    struct RasterVertex {
        double x, y, z;
        double color[4];
        double offset[4];
        double u, v;
    };
    const auto edge = [](const RasterVertex& a, const RasterVertex& b,
                         double x, double y) {
        return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    };
    const auto raster_triangle = [&](const NaomiPvrScenePrimitive& primitive,
                                     const NaomiPvrVertex& va,
                                     const NaomiPvrVertex& vb,
                                     const NaomiPvrVertex& vc) {
        RasterVertex v[3]{{va.x, va.y, va.z, {}, {}, va.u[0], va.v[0]},
                          {vb.x, vb.y, vb.z, {}, {}, vb.u[0], vb.v[0]},
                          {vc.x, vc.y, vc.z, {}, {}, vc.u[0], vc.v[0]}};
        const bool gouraud = (primitive.pcw & (1u << 1u)) != 0;
        for (unsigned channel = 0; channel < 4; ++channel) {
            v[0].color[channel] = va.base_color[0][channel];
            v[1].color[channel] = vb.base_color[0][channel];
            v[2].color[channel] = vc.base_color[0][channel];
            v[0].offset[channel] = va.offset_color[0][channel];
            v[1].offset[channel] = vb.offset_color[0][channel];
            v[2].offset[channel] = vc.offset_color[0][channel];
            if (!gouraud) {
                // Flycast's renderer uses the last TA strip vertex as the
                // provoking vertex for flat shading.
                v[0].color[channel] = v[1].color[channel] = v[2].color[channel];
                v[0].offset[channel] = v[1].offset[channel] = v[2].offset[channel];
            }
        }
        double area = edge(v[0], v[1], v[2].x, v[2].y);
        if (area == 0.0) return;
        const unsigned cull = (primitive.isp >> 27u) & 3u;
        // Flycast flips the PVR cull mode once for its GL front-face setup.
        const bool front = area < 0.0;
        if ((cull == 2u && front) || (cull == 3u && !front)) return;
        if (area < 0.0) {
            std::swap(v[1], v[2]);
            area = -area;
        }
        const auto min_x = std::max<std::int64_t>(clip_left,
            static_cast<std::int64_t>(std::floor(std::min({v[0].x, v[1].x, v[2].x}))));
        const auto min_y = std::max<std::int64_t>(clip_top,
            static_cast<std::int64_t>(std::floor(std::min({v[0].y, v[1].y, v[2].y}))));
        const auto max_x = std::min<std::int64_t>(clip_right - 1u,
            static_cast<std::int64_t>(std::ceil(std::max({v[0].x, v[1].x, v[2].x}))));
        const auto max_y = std::min<std::int64_t>(clip_bottom - 1u,
            static_cast<std::int64_t>(std::ceil(std::max({v[0].y, v[1].y, v[2].y}))));
        if (max_x < min_x || max_y < min_y) return;

        const auto clip_mode = (primitive.tile_clip >> 28u) & 3u;
        const auto clip_x0 = static_cast<double>(primitive.tile_clip & 63u) * 32.0;
        const auto clip_x1 = static_cast<double>(((primitive.tile_clip >> 6u) & 63u) + 1u) * 32.0;
        const auto clip_y0 = static_cast<double>((primitive.tile_clip >> 12u) & 31u) * 32.0;
        const auto clip_y1 = static_cast<double>(((primitive.tile_clip >> 17u) & 31u) + 1u) * 32.0;
        const unsigned depth_mode = (primitive.isp >> 29u) & 7u;
        for (auto y = min_y; y <= max_y; ++y) {
            for (auto x = min_x; x <= max_x; ++x) {
                const double px = static_cast<double>(x) + 0.5;
                const double py = static_cast<double>(y) + 0.5;
                if (clip_mode >= 2u) {
                    const bool inside_clip = px >= clip_x0 && px < clip_x1 &&
                                             py >= clip_y0 && py < clip_y1;
                    // Flycast getTileClip(): even mode keeps only pixels in
                    // the rectangle; odd mode renders outside it.
                    if ((clip_mode & 1u) ? inside_clip : !inside_clip)
                        continue;
                }
                const double w0 = edge(v[1], v[2], px, py) / area;
                const double w1 = edge(v[2], v[0], px, py) / area;
                const double w2 = edge(v[0], v[1], px, py) / area;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
                const double z = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                const double w = 100000.0 * z;
                const float fragment_depth = static_cast<float>(
                    std::log2(1.0 + std::max(w, -0.999999)) / 34.0);
                const auto index = static_cast<std::size_t>(y) * width +
                                   static_cast<std::size_t>(x);
                const auto old_depth = depth[index];
                const bool depth_pass = depth_mode == 0 ? false : depth_mode == 1 ? fragment_depth < old_depth
                    : depth_mode == 2 ? fragment_depth == old_depth : depth_mode == 3 ? fragment_depth <= old_depth
                    : depth_mode == 4 ? fragment_depth > old_depth : depth_mode == 5 ? fragment_depth != old_depth
                    : depth_mode == 6 ? fragment_depth >= old_depth : true;
                if (!depth_pass) continue;
                const auto perspective = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                const auto interp = [&](double a, double b, double c) {
                    if (perspective == 0.0) return w0 * a + w1 * b + w2 * c;
                    return (w0 * a * v[0].z + w1 * b * v[1].z +
                            w2 * c * v[2].z) / perspective;
                };
                std::array<float, 4> color{}, offset{};
                for (unsigned channel = 0; channel < 4; ++channel) {
                    color[channel] = static_cast<float>(interp(v[0].color[channel],
                        v[1].color[channel], v[2].color[channel]));
                    offset[channel] = static_cast<float>(interp(v[0].offset[channel],
                        v[1].offset[channel], v[2].offset[channel]));
                }
                const auto tsp = primitive.tsp[0];
                const bool textured = primitive.textured;
                const bool use_alpha = (tsp & (1u << 20u)) != 0;
                if (!use_alpha) color[3] = 1.0f;
                if (textured) {
                    const float u = static_cast<float>(interp(v[0].u, v[1].u, v[2].u));
                    const float tex_v = static_cast<float>(interp(v[0].v, v[1].v, v[2].v));
                    const auto interp_at = [&](double sx, double sy, double a, double b, double c) {
                        const double sw0 = edge(v[1], v[2], sx, sy) / area;
                        const double sw1 = edge(v[2], v[0], sx, sy) / area;
                        const double sw2 = edge(v[0], v[1], sx, sy) / area;
                        const double swp = sw0 * v[0].z + sw1 * v[1].z + sw2 * v[2].z;
                        return swp == 0.0 ? sw0 * a + sw1 * b + sw2 * c
                            : (sw0 * a * v[0].z + sw1 * b * v[1].z +
                               sw2 * c * v[2].z) / swp;
                    };
                    const double ux = interp_at(px + 1.0, py, v[0].u, v[1].u, v[2].u);
                    const double uy = interp_at(px, py + 1.0, v[0].u, v[1].u, v[2].u);
                    const double vx = interp_at(px + 1.0, py, v[0].v, v[1].v, v[2].v);
                    const double vy = interp_at(px, py + 1.0, v[0].v, v[1].v, v[2].v);
                    const double rho_x = std::hypot((ux - u) * (8u << ((primitive.tsp[0] >> 3u) & 7u)),
                                                    (vx - tex_v) * (8u << (primitive.tsp[0] & 7u)));
                    const double rho_y = std::hypot((uy - u) * (8u << ((primitive.tsp[0] >> 3u) & 7u)),
                                                    (vy - tex_v) * (8u << (primitive.tsp[0] & 7u)));
                    const float lod = static_cast<float>(std::max(0.0, std::log2(std::max({rho_x, rho_y, 1.0e-8}))));
                    auto texel = sampleTexture(tsp, primitive.tcw[0], u, tex_v, lod);
                    if ((tsp & (1u << 19u)) != 0) texel[3] = 1.0f;
                    const unsigned shade = (tsp >> 6u) & 3u;
                    if (shade == 0u) color = texel;
                    else if (shade == 1u) {
                        for (unsigned channel = 0; channel < 3; ++channel)
                            color[channel] *= texel[channel];
                        color[3] = texel[3];
                    } else if (shade == 2u) {
                        for (unsigned channel = 0; channel < 3; ++channel)
                            color[channel] += (texel[channel] - color[channel]) * texel[3];
                    } else {
                        for (unsigned channel = 0; channel < 4; ++channel)
                            color[channel] *= texel[channel];
                    }
                    if ((primitive.pcw & (1u << 2u)) != 0 &&
                        ((primitive.tcw[0] >> 27u) & 7u) != 4u) {
                        for (unsigned channel = 0; channel < 3; ++channel)
                            color[channel] += offset[channel];
                    }
                }
                for (auto& channel : color)
                    channel = std::clamp(channel, 0.0f, 1.0f);
                if (primitive.list_type == 4u) {
                    const auto alpha_ref = m_registers[0x11cu / 4u] & 0xffu;
                    if (alpha_ref > static_cast<unsigned>(color[3] * 255.0f + 0.5f)) continue;
                    color[3] = 1.0f;
                }
                const auto factor = [&](unsigned mode, unsigned channel, bool source) {
                    const float src_color = color[channel];
                    const float dst_color = rgba[index * 4u + channel] / 255.0f;
                    const float src_alpha = color[3];
                    const float dst_alpha = rgba[index * 4u + 3u] / 255.0f;
                    switch (mode) {
                    case 0: return 0.0f;
                    case 1: return 1.0f;
                    case 2: return source ? dst_color : src_color;
                    case 3: return source ? 1.0f - dst_color : 1.0f - src_color;
                    case 4: return src_alpha;
                    case 5: return 1.0f - src_alpha;
                    case 6: return dst_alpha;
                    default: return 1.0f - dst_alpha;
                    }
                };
                // Flycast disables blending for opaque polygon parameters in
                // the first render pass before submission.
                const bool first_pass_opaque = primitive.render_pass == 0u &&
                                               primitive.list_type == 0u;
                const unsigned src_blend = first_pass_opaque ? 1u : (tsp >> 29u) & 7u;
                const unsigned dst_blend = first_pass_opaque ? 0u : (tsp >> 26u) & 7u;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    const float dst = rgba[index * 4u + channel] / 255.0f;
                    const float out = color[channel] * factor(src_blend, channel, true) +
                                      dst * factor(dst_blend, channel, false);
                    rgba[index * 4u + channel] = to_saturated_u8(out);
                }
                if ((primitive.isp & (1u << 26u)) == 0)
                    depth[index] = fragment_depth;
            }
        }
    };

    struct TriangleCommand {
        const NaomiPvrScenePrimitive* primitive;
        std::array<std::size_t, 3> vertices;
        std::uint8_t list_order;
        float sort_depth;
    };
    struct PassState { bool autosort; bool clear_depth; };
    std::vector<TriangleCommand> commands;
    std::uint32_t max_pass = 0;
    if (!m_active_ta_contexts.empty())
        max_pass = static_cast<std::uint32_t>(m_active_ta_contexts.size() - 1u);

    // Flycast getRegionTileAddrAndSize/getRegionSettings: pass flags live in
    // the first tile's region header. The initial region header can be empty.
    const bool type1_region = (m_registers[0x7cu / 4u] & (1u << 21u)) == 0;
    std::uint32_t region_entry_size = type1_region ? 20u : 24u;
    const auto region_base = m_registers[0x2cu / 4u] & 0x00ffffffu;
    const auto read_vram32 = [this](std::uint32_t address) {
        const auto offset = mapVram32(address & 0x00ffffffu);
        return read_le32(m_vram.data() + offset);
    };
    bool empty_first_region = true;
    for (int word = type1_region ? 4 : 5; word > 0; --word) {
        if ((read_vram32(region_base + static_cast<std::uint32_t>(word * 4)) &
             0x80000000u) == 0) {
            empty_first_region = false;
            break;
        }
    }
    const auto first_region = region_base + (empty_first_region ? region_entry_size : 0u);
    // Match Flycast getRegionTileAddrAndSize(): the first tile's pre-sort
    // flag promotes every region entry to six words, including the pass
    // headers consumed below. Without the promotion later pass flags are
    // read from the middle of a tile entry.
    if ((read_vram32(first_region) & (1u << 29u)) != 0)
        region_entry_size = 24u;
    const bool type1_autosort = (m_registers[0x98u / 4u] & 1u) == 0;
    std::vector<PassState> pass_states(static_cast<std::size_t>(max_pass) + 1u);
    for (std::uint32_t pass = 0; pass <= max_pass; ++pass) {
        const auto header = read_vram32(first_region + pass * region_entry_size);
        const bool presort = (header & (1u << 29u)) != 0;
        pass_states[pass] = {
            type1_region ? type1_autosort : !presort,
            (header & (1u << 30u)) == 0};
    }

    for (const auto& primitive : m_scene_primitives) {
        if (primitive.render_pass >= m_active_ta_contexts.size() ||
            primitive.modifier_volume || primitive.vertices.size() < 3)
            continue;
        const auto list_order = static_cast<std::uint8_t>(
            primitive.list_type == 0u ? 0u : primitive.list_type == 4u ? 1u :
            primitive.list_type == 2u ? 2u : 3u);
        // Flycast sortPolyParams assigns one depth key to the entire polygon
        // parameter (minimum IEEE-754 Z word over all its vertices), then
        // stable-sorts polygon parameters. Reusing that key for every
        // triangle in a strip keeps a polygon's triangle order intact.
        std::uint32_t min_z_bits = 0xffffffffu;
        for (const auto& vertex : primitive.vertices)
            min_z_bits = std::min(min_z_bits,
                                  std::bit_cast<std::uint32_t>(vertex.z));
        const auto primitive_sort_depth = std::bit_cast<float>(min_z_bits);
        const auto add_triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
            commands.push_back({&primitive, {a, b, c}, list_order,
                                primitive_sort_depth});
        };
        if (primitive.sprite && primitive.vertices.size() >= 4) {
            add_triangle(0, 1, 2);
            add_triangle(2, 1, 3);
            continue;
        }
        std::size_t begin = 0;
        for (std::size_t end_index = 0; end_index < primitive.vertices.size(); ++end_index) {
            const bool strip_end = end_index + 1u == primitive.vertices.size() ||
                std::find(primitive.strip_end_vertex_indices.begin(),
                          primitive.strip_end_vertex_indices.end(), end_index) !=
                    primitive.strip_end_vertex_indices.end();
            if (!strip_end) continue;
            for (std::size_t i = begin + 2u; i <= end_index; ++i) {
                if (((i - begin) & 1u) == 0)
                    add_triangle(i - 2u, i - 1u, i);
                else
                    add_triangle(i - 1u, i - 2u, i);
            }
            begin = end_index + 1u;
        }
    }
    std::stable_sort(commands.begin(), commands.end(), [&](const auto& left, const auto& right) {
        if (left.primitive->render_pass != right.primitive->render_pass)
            return left.primitive->render_pass < right.primitive->render_pass;
        if (left.list_order != right.list_order)
            return left.list_order < right.list_order;
        if (left.list_order == 2u &&
            pass_states[left.primitive->render_pass].autosort &&
            left.sort_depth != right.sort_depth)
            return left.sort_depth < right.sort_depth;
        return false;
    });

    std::uint32_t active_pass = std::numeric_limits<std::uint32_t>::max();
    for (const auto& command : commands) {
        const auto pass = command.primitive->render_pass;
        if (active_pass != pass) {
            if (pass_states[pass].clear_depth)
                std::fill(depth.begin(), depth.end(), 1.0f);
            active_pass = pass;
        }
        const auto& v = command.primitive->vertices;
        raster_triangle(*command.primitive, v[command.vertices[0]],
                        v[command.vertices[1]], v[command.vertices[2]]);
    }
    return rgba;
}

void NaomiPvr2::writeStoreQueue(std::uint32_t address,
                                const std::array<std::uint8_t, 64>& queues) {
    const auto queue_offset = static_cast<std::size_t>((address >> 5) & 1u) * kStoreQueueBytes;
    const auto target = address & 0x01ffffe0u;
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_submits < 64) {
        ++g_pvr_trace_submits;
        std::fprintf(stderr, "Naomi PVR SQ address=%08x target=%08x queue=%zu first=%02x%02x%02x%02x\n",
                     address, target, queue_offset,
                     queues[queue_offset + 3], queues[queue_offset + 2],
                     queues[queue_offset + 1], queues[queue_offset]);
    }
    submit(target, queues.data() + queue_offset, kStoreQueueBytes);
}

std::uint32_t NaomiPvr2::mapVram32(std::uint32_t address) {
    // Flycast pvr_mem.cpp::pvr_map32: the two Naomi PVR VRAM banks are
    // interleaved on 32-bit accesses, with the bank select at address bit 22.
    constexpr std::uint32_t kVramMask = 0x00ffffffu;
    constexpr std::uint32_t kBankBit = 0x00400000u;
    constexpr std::uint32_t kStaticBits = kVramMask - (kBankBit * 2u - 1u) + 3u;
    constexpr std::uint32_t kOffsetBits = (kBankBit - 1u) & ~3u;
    const auto bank = (address & kBankBit) / kBankBit;
    std::uint32_t mapped = address & kStaticBits;
    mapped |= (address & kOffsetBits) * 2u;
    mapped |= bank * 4u;
    return mapped & kVramMask;
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
        // Flycast ta_thd_data32_i() explicitly ignores FIFO data before
        // ta_vtx_ListInit() creates a current TA context. Accepting it here
        // can fabricate a scene from writes made while the TA is idle.
        if (!m_ta_context_active) return;
        if (m_ta_stream.size() + count > 64 * 1024 * 1024) {
            m_ta_stream.clear();
            m_ta_parse_offset = 0;
            m_ta_active_list = -1;
            m_ta_fsm_state = 0;
            m_ta_lists.clear();
            m_ta_primitives.clear();
            m_scene_primitives.clear();
            m_ta_continuation_units = 0;
            m_ta_continuation_is_vertex = false;
            m_ta_continuation_ends_strip = false;
            m_ta_vertex_is_64_bytes = false;
            m_ta_active_list_begin = 0;
            m_ta_active_primitive = -1;
        }
        m_ta_stream.insert(m_ta_stream.end(), bytes, bytes + count);
        m_ta_bytes_submitted += count;
        parseTaPackets();
    } else if (target < 0x01000000u) {
        submitYuv(bytes, count);
        m_yuv_bytes_submitted += count;
    } else {
        const auto offset = (target - 0x01000000u) % static_cast<std::uint32_t>(m_vram.size());
        storeLittle(m_vram, offset, bytes, count);
        m_vram_bytes_written += count;
    }
}

void NaomiPvr2::resetYuvConverter() {
    const auto base = m_registers[0x148u / 4u] & 0x00fffff8u;
    const auto control = m_registers[0x14cu / 4u];
    m_yuv_destination = base & static_cast<std::uint32_t>(m_vram.size() - 1u);
    m_yuv_width = ((control & 0x3fu) + 1u) * 16u;
    m_yuv_height = (((control >> 8u) & 0x3fu) + 1u) * 16u;
    m_yuv_block_count = ((control & 0x3fu) + 1u) * (((control >> 8u) & 0x3fu) + 1u);
    m_yuv_block_index = 0;
    m_yuv_x = 0;
    m_yuv_y = 0;
    m_yuv_pending_bytes = 0;
}

void NaomiPvr2::submitYuv(const std::uint8_t* bytes, std::size_t count) {
    // Flycast's YUV_data accepts store queues in units of 32 bytes and
    // converts complete 384-byte 16x16 YUV422 macroblocks into VRAM.
    if (m_yuv_block_count == 0 || !bytes) return;
    if ((m_registers[0x14cu / 4u] & (1u << 24u)) != 0) {
        // Flycast does not implement the 512-byte YUV format either.
        return;
    }
    while (count != 0) {
        if (m_yuv_block_index == m_yuv_block_count) resetYuvConverter();
        const auto remaining = m_yuv_pending_block.size() - m_yuv_pending_bytes;
        const auto take = std::min(count, remaining);
        std::memcpy(m_yuv_pending_block.data() + m_yuv_pending_bytes, bytes, take);
        m_yuv_pending_bytes += take;
        bytes += take;
        count -= take;
        if (m_yuv_pending_bytes != m_yuv_pending_block.size()) continue;

        convertYuvMacroblock(m_yuv_pending_block.data());
        m_yuv_pending_bytes = 0;
        ++m_yuv_block_index;
        if (m_yuv_block_index == m_yuv_block_count && m_raise_normal_irq)
            m_raise_normal_irq(6); // Holly YUV DMA completion.
    }
}

void NaomiPvr2::convertYuvMacroblock(const std::uint8_t* block) {
    const std::uint32_t uv_quadrant_offsets[4]{0u, 4u, 32u, 36u};
    const std::uint32_t y_quadrant_offsets[4]{0u, 64u, 128u, 192u};
    const std::uint32_t x_quadrant_offsets[4]{0u, 8u, 0u, 8u};
    const std::uint32_t y_quadrant_offsets_out[4]{0u, 0u, 8u, 8u};
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant) {
        const auto uv_base = uv_quadrant_offsets[quadrant];
        const auto y_base = 128u + y_quadrant_offsets[quadrant];
        const auto tile_x = m_yuv_x + x_quadrant_offsets[quadrant];
        const auto tile_y = m_yuv_y + y_quadrant_offsets_out[quadrant];
        for (unsigned row_pair = 0; row_pair < 4; ++row_pair) {
            for (unsigned column_pair = 0; column_pair < 4; ++column_pair) {
                const auto uv = uv_base + row_pair * 8u + column_pair;
                const auto y_in = y_base + row_pair * 16u + column_pair * 2u;
                const auto u = block[uv];
                const auto v = block[uv + 64u];
                const auto x = tile_x + column_pair * 2u;
                const auto y = tile_y + row_pair * 2u;
                const auto row0 = (m_yuv_destination + (y * m_yuv_width + x) * 2u) % m_vram.size();
                const auto row1 = (m_yuv_destination + ((y + 1u) * m_yuv_width + x) * 2u) % m_vram.size();
                const std::uint8_t pixels[4]{u, block[y_in], v, block[y_in + 1u]};
                const std::uint8_t next_line[4]{u, block[y_in + 8u], v, block[y_in + 9u]};
                storeLittle(m_vram, row0, pixels, sizeof(pixels));
                storeLittle(m_vram, row1, next_line, sizeof(next_line));
                m_vram_bytes_written += sizeof(pixels) + sizeof(next_line);
            }
        }
    }

    m_yuv_destination += 32u;
    m_yuv_x += 16u;
    if (m_yuv_x == m_yuv_width) {
        m_yuv_destination += 15u * m_yuv_width * 2u;
        m_yuv_x = 0;
        m_yuv_y += 16u;
        if (m_yuv_y == m_yuv_height) m_yuv_y = 0;
    }
}

void NaomiPvr2::markObjectListBlocks(bool continuation) {
    // Port of Flycast ta.cpp::ta_vtx_ListInit/markObjectListBlocks. Region
    // array contents are written by the guest into VRAM; TA list init selects
    // the current region pass and seeds its per-tile OPB pointers.
    if (continuation)
        ++m_ta_render_pass;
    const auto read_vram32 = [this](std::uint32_t address) {
        const auto offset = mapVram32(address & 0x00ffffffu);
        return read_le32(m_vram.data() + offset);
    };
    const auto write_vram32 = [this](std::uint32_t address, std::uint32_t value) {
        const auto offset = mapVram32(address & 0x00ffffffu);
        for (unsigned byte = 0; byte < 4; ++byte)
            m_vram[(offset + byte) % m_vram.size()] =
                static_cast<std::uint8_t>(value >> (byte * 8u));
        m_vram_bytes_written += 4;
    };
    const auto region_base = m_registers[0x2cu / 4u] & 0x00ffffffu;
    const bool type1_tile = (m_registers[0x7cu / 4u] & (1u << 21u)) == 0;
    std::uint32_t tile_size = (type1_tile ? 5u : 6u) * 4u;
    bool empty_first_region = true;
    for (int i = type1_tile ? 4 : 5; i > 0; --i) {
        if ((read_vram32(region_base + static_cast<std::uint32_t>(i * 4)) & 0x80000000u) == 0) {
            empty_first_region = false;
            break;
        }
    }
    auto first_region_address = region_base + (empty_first_region ? tile_size : 0u);
    // getRegionTileAddrAndSize() promotes pre-sorted region entries to six
    // words even when FPU_PARAM_CFG selected the five-word layout. Flycast
    // does this before markObjectListBlocks() applies the render-pass stride;
    // without it, pass 1+ writes OPB pointers into the next tile's region row.
    if ((read_vram32(first_region_address) & (1u << 29u)) != 0)
        tile_size = 24u;
    auto region_address = first_region_address + m_ta_render_pass * tile_size;
    const bool empty_opaque = (read_vram32(region_address + 4u) & 0x80000000u) != 0;
    const auto alloc = m_registers[0x140u / 4u];
    const auto tile_clip = m_registers[0x13cu / 4u];
    const std::uint32_t tile_count = (((tile_clip >> 16u) & 0x0fu) + 1u) *
                                     ((tile_clip & 0x3fu) + 1u);
    const auto opb_size = [](unsigned encoding) {
        return encoding == 0 ? 0u : 16u << encoding;
    };
    auto address = m_registers[0x124u / 4u] & 0x00ffffffu;
    auto block_size = opb_size(alloc & 3u);
    if (empty_opaque)
        address += block_size * tile_count;
    if (block_size == 0 || empty_opaque) {
        address += opb_size((alloc >> 4u) & 3u) * tile_count;
        block_size = opb_size((alloc >> 8u) & 3u);
        if (block_size == 0) {
            address += opb_size((alloc >> 12u) & 3u) * tile_count;
            block_size = opb_size((alloc >> 16u) & 3u);
        }
    }
    if (block_size == 0) return;
    for (std::uint32_t tile = 0; tile < tile_count; ++tile) {
        write_vram32(address, m_registers[0x124u / 4u]);
        address += block_size;
    }
}

std::vector<std::uint32_t> NaomiPvr2::getTaContextAddresses() const {
    // Port of Flycast ta_util.cpp::getRegionTileAddrAndSize and
    // getTAContextAddresses. Region-array entries are grouped by tile; the
    // entries for the first tile identify each linked render-pass context.
    const bool type1 = (m_registers[0x7cu / 4u] & (1u << 21u)) == 0;
    std::uint32_t tile_bytes = type1 ? 20u : 24u;
    const auto region_base = m_registers[0x2cu / 4u] & 0x00ffffffu;
    const auto read_vram32 = [this](std::uint32_t address) {
        const auto offset = mapVram32(address & 0x00ffffffu);
        return read_le32(m_vram.data() + offset);
    };

    bool empty_first_region = true;
    for (int word = type1 ? 4 : 5; word > 0; --word) {
        if ((read_vram32(region_base + static_cast<std::uint32_t>(word * 4)) &
             0x80000000u) == 0) {
            empty_first_region = false;
            break;
        }
    }
    auto address = region_base + (empty_first_region ? tile_bytes : 0u);
    const auto first = read_vram32(address);
    // Flycast getRegionTileAddrAndSize() promotes pre-sorted region entries
    // to six words even when FPU_PARAM_CFG selected the five-word layout.
    if ((first & (1u << 29u)) != 0)
        tile_bytes = 24u;
    const auto first_x = (first >> 2u) & 0x3fu;
    const auto first_y = (first >> 8u) & 0x3fu;
    std::vector<std::uint32_t> contexts;
    contexts.reserve(10); // Flycast ta_ctx.h::MAX_PASSES
    for (unsigned pass = 0; pass < 10; ++pass) {
        const auto tile = read_vram32(address);
        if (((tile >> 2u) & 0x3fu) != first_x ||
            ((tile >> 8u) & 0x3fu) != first_y)
            break;
        auto opb = read_vram32(address + 4u); // opaque
        if (opb & 0x80000000u)
            opb = read_vram32(address + 12u); // translucent
        if ((opb & 0x80000000u) && tile_bytes >= 24u)
            opb = read_vram32(address + 20u); // punch-through
        if (opb & 0x80000000u)
            break;
        contexts.push_back(read_vram32(opb));
        const bool last = (tile & 0x80000000u) != 0;
        if (last) break;
        address += tile_bytes;
    }
    return contexts;
}

void NaomiPvr2::decodeRegionTileClip() {
    // Flycast ta_vtx.cpp::getRegionTileClipping/setTileClipping. The clip is
    // the bounding rectangle of the region-array tiles, intersected with the
    // global tile clip used to size the PVR render target.
    const bool type1_tile = (m_registers[0x7cu / 4u] & (1u << 21u)) == 0;
    std::uint32_t tile_size = (type1_tile ? 5u : 6u) * 4u;
    const auto base = m_registers[0x2cu / 4u] & 0x00ffffffu;
    const auto read_vram32 = [this](std::uint32_t address) {
        const auto offset = mapVram32(address & 0x00ffffffu);
        return read_le32(m_vram.data() + offset);
    };
    bool empty_first_region = true;
    for (int i = type1_tile ? 4 : 5; i > 0; --i) {
        if ((read_vram32(base + static_cast<std::uint32_t>(i * 4)) & 0x80000000u) == 0) {
            empty_first_region = false;
            break;
        }
    }
    auto address = base + (empty_first_region ? tile_size : 0u);
    // Match getRegionTileAddrAndSize(): pre-sorted tiles use the six-word
    // region entry even in type-1 mode, and every subsequent tile advances
    // by that promoted size.
    if ((read_vram32(address) & (1u << 29u)) != 0)
        tile_size = 24u;
    std::uint32_t min_x = 20, max_x = 0, min_y = 15, max_y = 0;
    bool last_region = false;
    for (unsigned count = 0; count < 3000 && !last_region; ++count) {
        const auto entry = read_vram32(address);
        min_x = std::min(min_x, (entry >> 2u) & 0x3fu);
        min_y = std::min(min_y, (entry >> 8u) & 0x3fu);
        max_x = std::max(max_x, (entry >> 2u) & 0x3fu);
        max_y = std::max(max_y, (entry >> 8u) & 0x3fu);
        last_region = (entry & 0x80000000u) != 0;
        address += tile_size;
    }
    const auto global = m_registers[0x13cu / 4u];
    const auto global_width = ((global & 0x3fu) + 1u) * 32u;
    const auto global_height = (((global >> 16u) & 0x0fu) + 1u) * 32u;
    const auto x = min_x * 32u;
    const auto y = min_y * 32u;
    const auto right = std::min((max_x + 1u) * 32u, global_width);
    const auto bottom = std::min((max_y + 1u) * 32u, global_height);
    m_render_tile_clip = {x, y, right > x ? right - x : 0u,
                          bottom > y ? bottom - y : 0u};
}

void NaomiPvr2::parseTaPackets() {
    static constexpr std::array<unsigned, 5> list_end_irq{7, 8, 9, 10, 21};
    constexpr std::size_t packet_bytes = 32;

    while (m_ta_parse_offset + packet_bytes <= m_ta_stream.size()) {
        m_ta_context_bytes[m_registers[0x124u / 4u]] += packet_bytes;
        const auto* packet = m_ta_stream.data() + m_ta_parse_offset;
        const std::uint32_t pcw = static_cast<std::uint32_t>(packet[0]) |
                                  (static_cast<std::uint32_t>(packet[1]) << 8u) |
                                  (static_cast<std::uint32_t>(packet[2]) << 16u) |
                                  (static_cast<std::uint32_t>(packet[3]) << 24u);
        const auto parameter_type = (pcw >> 29u) & 7u;
        // PCW layout from Flycast ta_structs.h: object control bits 0..7,
        // User_Clip 16..17, ListType 24..26, EndOfStrip 28, ParaType 29..31.
        // The old low-bit ListType decode used UV/color flags as list IDs.
        const auto list_type = (pcw >> 24u) & 7u;
        m_ta_fsm_state = advanceTaFsm(
            m_ta_fsm_state, parameter_type,
            static_cast<std::uint8_t>(pcw & 0xffu), list_type);

        // The SH-4 submits the TA stream in 32-byte store-queue chunks. Some
        // polygon headers and vertex parameters are 64 bytes, so their second
        // half has no PCW and must not be decoded as an independent command.
        if (m_ta_continuation_units != 0) {
            --m_ta_continuation_units;
            m_ta_parse_offset += packet_bytes;
            if (!m_ta_continuation_is_vertex && m_ta_active_primitive >= 0 &&
                static_cast<std::size_t>(m_ta_active_primitive) < m_ta_primitives.size()) {
                auto& primitive = m_ta_primitives[static_cast<std::size_t>(m_ta_active_primitive)];
                primitive.data_begin_byte = m_ta_parse_offset;
                primitive.data_end_byte = m_ta_parse_offset;
            } else if (m_ta_continuation_is_vertex && m_ta_active_primitive >= 0 &&
                static_cast<std::size_t>(m_ta_active_primitive) < m_ta_primitives.size()) {
                auto& primitive = m_ta_primitives[static_cast<std::size_t>(m_ta_active_primitive)];
                primitive.data_end_byte = m_ta_parse_offset;
                if (primitive.sprite) {
                    primitive.complete = true;
                    m_ta_active_primitive = -1;
                } else if (m_ta_continuation_ends_strip) {
                    // EndOfStrip closes this strip, not its polygon header.
                    // Keep the active primitive so another strip can follow
                    // without repeating the global TA parameter.
                    primitive.complete = true;
                }
            }
            m_ta_continuation_is_vertex = false;
            m_ta_continuation_ends_strip = false;
            continue;
        }
        ++m_ta_parameter_counts[parameter_type];

        // Flycast's PCW definitions assign these command values:
        // 0=EOL, 1=user clip, 2=object-list set, 4=polygon/modifier,
        // 5=sprite, 7=vertex. Type 6 is reserved; it must never start a list.
        // Vertices inherit the list selected by their polygon/sprite header.
        if (parameter_type == 1u) {
            // Flycast ta_vtx.cpp reads data_32[3..6] (byte offsets 12..24).
            const auto xmin = read_le32(packet + 12) & 63u;
            const auto ymin = read_le32(packet + 16) & 31u;
            const auto xmax = read_le32(packet + 20) & 63u;
            const auto ymax = read_le32(packet + 24) & 31u;
            m_ta_tile_clip = (m_ta_tile_clip & 0xf0000000u) | xmin |
                             (xmax << 6u) | (ymin << 12u) | (ymax << 17u);
        } else if (parameter_type == 4u || parameter_type == 5u) {
            const auto mode = (pcw >> 16u) & 3u;
            m_ta_tile_clip = (m_ta_tile_clip & 0x0fffffffu) | (mode << 28u);
        }
        if ((parameter_type == 4u || parameter_type == 5u) && list_type < 5u) {
            if (m_ta_active_primitive >= 0 &&
                static_cast<std::size_t>(m_ta_active_primitive) < m_ta_primitives.size()) {
                auto& previous = m_ta_primitives[static_cast<std::size_t>(m_ta_active_primitive)];
                previous.complete = true;
                previous.data_end_byte = m_ta_parse_offset;
            }
            if (m_ta_active_list < 0 || m_ta_active_list != static_cast<int>(list_type))
                m_ta_active_list_begin = m_ta_parse_offset;
            m_ta_active_list = static_cast<int>(list_type);
            ++m_ta_list_packet_counts[list_type];
            m_ta_active_primitive = static_cast<int>(m_ta_primitives.size());
            m_ta_primitives.push_back(NaomiTaPrimitiveSpan{
                m_ta_parse_offset,
                m_ta_parse_offset + packet_bytes,
                m_ta_parse_offset + packet_bytes,
                pcw,
                static_cast<std::uint8_t>(list_type),
                32,
                parameter_type == 5u,
                false});
            auto& primitive = m_ta_primitives.back();
            primitive.render_pass = m_ta_render_pass;
            primitive.context_address = m_registers[0x124u / 4u];
            primitive.modifier_volume = list_type == 1u || list_type == 3u;
            primitive.tile_clip = m_ta_tile_clip;
            if (parameter_type == 5u) {
                // Flycast's sprite parameter is always 32 bytes. A sprite
                // vertex is a 64-byte A+B pair, but its first half is a type-7
                // packet and only then do we skip the continuation half.
                m_ta_vertex_is_64_bytes = true;
                primitive.vertex_packet_bytes = 64;
            } else {
                const auto format = decodeTaObjectFormat(static_cast<std::uint8_t>(pcw));
                if (!format.valid) {
                    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") &&
                        m_render_start_requests < 8)
                        std::fprintf(stderr,
                                     "Naomi PVR TA invalid polygon format pcw=%08x offset=%zu\n",
                                     pcw, m_ta_parse_offset);
                    m_ta_vertex_is_64_bytes = list_type == 1u || list_type == 3u;
                    if (m_ta_vertex_is_64_bytes)
                        primitive.vertex_packet_bytes = 64;
                } else {
                    // Flycast forces a 64-byte vertex packet for
                    // modifier-volume lists, independently of the ordinary
                    // polygon vertex format table.
                    const bool modifier_volume_list = list_type == 1u || list_type == 3u;
                    m_ta_vertex_is_64_bytes = modifier_volume_list || format.vertex_units == 2u;
                    primitive.vertex_format = static_cast<std::uint8_t>(format.vertex_format);
                    primitive.vertex_packet_bytes =
                        static_cast<std::uint8_t>(m_ta_vertex_is_64_bytes ? 64u : packet_bytes);
                    if (format.polygon_header_units == 2u)
                        m_ta_continuation_units = format.polygon_header_units - 1u;
                    if (format.polygon_header_units == 2u)
                        m_ta_continuation_is_vertex = false;
                }
            }
        } else if (parameter_type == 7u && m_ta_active_list >= 0 &&
                   m_ta_active_list < static_cast<int>(m_ta_list_packet_counts.size())) {
            ++m_ta_list_packet_counts[static_cast<std::size_t>(m_ta_active_list)];
            if (m_ta_active_primitive >= 0 &&
                static_cast<std::size_t>(m_ta_active_primitive) < m_ta_primitives.size()) {
                auto& primitive = m_ta_primitives[static_cast<std::size_t>(m_ta_active_primitive)];
                if (primitive.data_begin_byte == primitive.data_end_byte)
                    primitive.data_begin_byte = m_ta_parse_offset;
                primitive.data_end_byte = m_ta_parse_offset + packet_bytes;
                if (m_ta_vertex_is_64_bytes) {
                    m_ta_continuation_units = 1;
                    m_ta_continuation_is_vertex = true;
                    m_ta_continuation_ends_strip =
                        primitive.sprite || ((pcw & (1u << 28u)) != 0);
                } else if (pcw & (1u << 28u)) {
                    primitive.complete = true;
                }
            } else if (m_ta_vertex_is_64_bytes) {
                m_ta_continuation_units = 1;
            }
        } else if (parameter_type == 0u) {
            const auto completed_list = m_ta_active_list >= 0
                ? static_cast<unsigned>(m_ta_active_list) : list_type;
            if (completed_list < list_end_irq.size()) {
                if (m_ta_active_list >= 0) {
                    m_ta_lists.push_back(NaomiTaListSpan{
                        static_cast<std::uint8_t>(completed_list),
                        m_ta_active_list_begin,
                        m_ta_parse_offset + packet_bytes,
                        m_ta_render_pass});
                }
                const auto bit = list_end_irq[completed_list];
                m_pending_ta_irqs.push_back(bit);
                if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_ta_eols < 64) {
                    ++g_pvr_trace_ta_eols;
                    std::fprintf(stderr,
                                 "Naomi PVR TA EOL list=%u irq_bit=%u stream_offset=%zu\n",
                                 completed_list, bit, m_ta_parse_offset);
                }
            } else if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR") && g_pvr_trace_ta_eols < 64) {
                ++g_pvr_trace_ta_eols;
                std::fprintf(stderr,
                             "Naomi PVR TA EOL unsupported_list=%u stream_offset=%zu\n",
                             completed_list, m_ta_parse_offset);
            }
            m_ta_active_list = -1;
            m_ta_vertex_is_64_bytes = false;
            if (m_ta_active_primitive >= 0 &&
                static_cast<std::size_t>(m_ta_active_primitive) < m_ta_primitives.size()) {
                auto& primitive = m_ta_primitives[static_cast<std::size_t>(m_ta_active_primitive)];
                primitive.complete = true;
                primitive.data_end_byte = m_ta_parse_offset;
            }
            m_ta_active_primitive = -1;
        }
        m_ta_parse_offset += packet_bytes;
    }
}

} // namespace multiarcaderecomp::naomi
