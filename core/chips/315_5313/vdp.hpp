#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace multiarcaderecomp::chips {

// Behavioral model of Sega's 315-5313 VDP bus, memory, and scanline output.
// System 18 layer mixing remains a board/video-pipeline responsibility.
class Sega3155313 {
public:
    struct Pixel {
        std::uint8_t color = 0; // CRAM color index: palette*16 + 4bpp pen.
        bool opaque = false;
        bool high_priority = false;
    };
    using Scanline = std::array<Pixel, 320>;
    struct Callbacks {
        std::function<std::uint16_t(std::uint32_t)> read_maincpu16;
        std::function<void(unsigned, bool)> irq_line;
        std::function<void(std::uint8_t)> psg_write;
    };

    explicit Sega3155313(Callbacks callbacks = {});
    void set_psg_write_callback(std::function<void(std::uint8_t)> callback) {
        m_callbacks.psg_write = std::move(callback);
    }
    void reset();
    std::uint16_t read(std::uint32_t byte_offset);
    void write(std::uint32_t byte_offset, std::uint16_t data, std::uint8_t lanes = 3);
    void set_scanline(unsigned line);
    void set_beam_position(unsigned line, unsigned horizontal_position);
    void acknowledge_irq(unsigned level);
    Scanline render_scanline(unsigned line) const;

    std::uint8_t reg(unsigned index) const { return m_regs[index & 0x1fU]; }
    std::uint16_t vram_word(unsigned index) const { return m_vram[index & 0x7fffU]; }
    std::uint16_t cram_word(unsigned index) const { return m_cram[index & 0x3fU]; }
    std::uint16_t vsram_word(unsigned index) const { return m_vsram[index & 0x3fU]; }
    std::uint16_t address() const { return m_vdp_address; }
    std::uint8_t code() const { return m_vdp_code; }
    unsigned scanline() const { return m_scanline; }
    unsigned horizontal_position() const { return m_horizontal_position; }
    const std::array<std::uint8_t, 0x20>& registers() const { return m_regs; }
    const std::array<std::uint16_t, 0x8000>& vram() const { return m_vram; }
    const std::array<std::uint16_t, 0x40>& cram() const { return m_cram; }
    const std::array<std::uint16_t, 0x40>& vsram() const { return m_vsram; }

private:
    void write_control(std::uint16_t data);
    void write_data(std::uint16_t data);
    void scanline_event();
    std::uint16_t read_data();
    std::uint16_t read_status();
    std::uint16_t read_hv_counter() const;
    void set_register(unsigned index, std::uint8_t value);
    void update_command();
    void increment_address();
    void write_vram(std::uint16_t data);
    void write_cram(std::uint16_t data);
    void write_vsram(std::uint16_t data);
    std::uint16_t vram_read() const;
    void execute_dma();
    void dma_68k_to_vram(std::uint32_t source, std::uint32_t byte_length);
    void dma_68k_to_cram(std::uint32_t source, std::uint32_t byte_length);
    void dma_68k_to_vsram(std::uint32_t source, std::uint32_t byte_length);
    void dma_vram_copy(std::uint16_t source, std::uint16_t length);
    void dma_vram_fill(std::uint16_t value);
    std::uint16_t read_maincpu(std::uint32_t address) const;
    void set_irq(unsigned level, bool asserted);

    Callbacks m_callbacks;
    std::array<std::uint8_t, 0x20> m_regs{};
    std::array<std::uint16_t, 0x8000> m_vram{};
    std::array<std::uint16_t, 0x40> m_cram{};
    std::array<std::uint16_t, 0x40> m_vsram{};
    std::array<std::uint16_t, 0x200> m_internal_sprite_attributes{};
    std::uint16_t m_command_part1 = 0;
    std::uint16_t m_command_part2 = 0;
    std::uint16_t m_vdp_address = 0;
    std::uint8_t m_vdp_code = 0;
    std::uint16_t m_vram_fill_length = 0;
    unsigned m_scanline = 0;
    unsigned m_horizontal_position = 0;
    bool m_scanline_initialized = false;
    int m_irq4_counter = -1;
    bool m_command_pending = false;
    bool m_vram_fill_pending = false;
    bool m_vblank = false;
    bool m_irq4_pending = false;
    bool m_irq6_pending = false;
    bool m_irq4_asserted = false;
    bool m_irq6_asserted = false;
};

} // namespace multiarcaderecomp::chips
