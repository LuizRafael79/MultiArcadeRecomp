#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace multiarcaderecomp::naomi {

// Naomi's SH-4-facing interrupt concentrator and Maple DMA channel.
// Register behavior is based on the MAME Naomi map and Flycast's Holly
// interrupt/Maple implementations; no Dreamcast GD-ROM path is included.
class HollyInterrupts {
public:
    enum class Group : std::uint8_t { normal, external, error };
    using IrqCallback = std::function<void(unsigned sh4_irl, bool asserted)>;

    explicit HollyInterrupts(IrqCallback callback = {});
    void reset();
    void raise(Group group, unsigned bit);
    void clear(Group group, std::uint32_t write_one_to_clear);
    void setMask(unsigned level, Group group, std::uint32_t value);
    [[nodiscard]] bool handles(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t readRegister(std::uint32_t address) const;
    void writeRegister(std::uint32_t address, std::uint32_t value);
    [[nodiscard]] std::uint32_t status(Group group) const;
    [[nodiscard]] std::uint32_t normalStatusWithSummary() const;
    [[nodiscard]] std::uint32_t mask(unsigned level, Group group) const;

private:
    static std::size_t groupIndex(Group group);
    void updateLines();

    std::array<std::uint32_t, 3> m_status{};
    std::array<std::array<std::uint32_t, 3>, 3> m_masks{}; // levels 2, 4, 6
    std::array<bool, 3> m_lines{};
    IrqCallback m_irq;
};

struct MapleMemory {
    std::function<bool(std::uint32_t address, std::uint32_t& value)> read32;
    std::function<bool(std::uint32_t address, std::uint32_t value)> write32;
};

using MapleEndpoint = std::function<std::vector<std::uint32_t>(
    unsigned bus, std::span<const std::uint32_t> request)>;

class NaomiJvs13551;
MapleEndpoint make_jvs_maple_endpoint(NaomiJvs13551& board);

// Executes a programmed Maple command table against a board-provided endpoint.
// The endpoint owns device protocol (for Naomi, the 315-6149/JVS bridge); this
// class owns Holly's descriptor traversal, memory writes, status and IRQs.
class MapleDma {
public:
    static constexpr std::uint32_t kMdstar = 0x005f6c04;
    static constexpr std::uint32_t kMdtsel = 0x005f6c10;
    static constexpr std::uint32_t kMden = 0x005f6c14;
    static constexpr std::uint32_t kMdst = 0x005f6c18;
    static constexpr std::uint32_t kMsys = 0x005f6c80;
    static constexpr std::uint32_t kMshtcl = 0x005f6c88;
    static constexpr std::uint32_t kMmsel = 0x005f6ce8;

    MapleDma(MapleMemory memory, MapleEndpoint endpoint, HollyInterrupts& interrupts);
    void reset();
    [[nodiscard]] std::uint32_t readRegister(std::uint32_t address) const;
    void writeRegister(std::uint32_t address, std::uint32_t value);
    // Called by the board scheduler for the VBlank-trigger mode (MDTSEL=1).
    void onVblank();
    [[nodiscard]] bool busy() const { return (m_mdst & 1u) != 0; }

private:
    bool runTable();
    bool fail(unsigned error_bit);
    static std::uint32_t swap32(std::uint32_t value);

    MapleMemory m_memory;
    MapleEndpoint m_endpoint;
    HollyInterrupts& m_interrupts;
    std::uint32_t m_mdstar = 0;
    std::uint32_t m_mdt_sel = 0;
    std::uint32_t m_mden = 0;
    std::uint32_t m_mdst = 0;
    std::uint32_t m_msys = 0x3a980000;
    std::uint32_t m_mmsel = 1;
};

// Holly channel 2 exposes direct display-list/YUV/texture transfers from SH-4
// RAM into the PVR. The register interface lives on Holly; the actual payload
// moves through the SH-4 bus and therefore uses callbacks supplied by the
// Naomi board wiring.
struct HollyC2DmaBus {
    std::function<std::uint32_t(std::uint32_t, unsigned)> read;
    std::function<void(std::uint32_t, unsigned, std::uint32_t)> write;
    std::function<void(unsigned)> raise_normal_irq;
};

class HollyC2Dma {
public:
    explicit HollyC2Dma(HollyC2DmaBus bus);
    [[nodiscard]] bool handles(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t readRegister(std::uint32_t address) const;
    void writeRegister(std::uint32_t address, std::uint32_t value);
    void advanceCycles(std::uint32_t cycles);

private:
    void start();
    void complete();

    HollyC2DmaBus m_bus;
    std::uint32_t m_destination = 0;
    std::uint32_t m_length = 0;
    std::uint32_t m_start = 0;
    std::uint32_t m_cycles_until_irq = 0;
};

} // namespace multiarcaderecomp::naomi
