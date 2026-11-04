#include "naomi_module.hpp"

#include "naomi_sh4_context.hpp"
#include "../../core/cpus/sh4/flycast_interpreter.hpp"
#include "naomi_address_space.hpp"
#include "naomi_pvr2.hpp"
#include "naomi_holly.hpp"
#include "naomi_jvs_837_13551.hpp"

#include <cstring>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <exception>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <cstdio>

namespace mamerecomp::naomi {
#if defined(MAMERECOMP_NAOMI_HAS_TRANSLATED_SH4)
void mamerecomp_register_sh4_mvsc2u_bios(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_reset(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_reset_p1(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bootstrap(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_ram_shadow(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_ram_vector(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_ram_irq(Sh4Dispatcher& dispatcher);
void mamerecomp_register_sh4_naomi_bios_copied_code(Sh4Dispatcher& dispatcher);
std::uint32_t mamerecomp_sh4_entry_naomi_bios_reset();
#if defined(MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION)
void mamerecomp_register_sh4_mvsc2u_boot_ram(Sh4Dispatcher& dispatcher);
std::uint32_t mamerecomp_sh4_entry_mvsc2u_boot_ram();
#else
void mamerecomp_register_sh4_mvsc2u(Sh4Dispatcher& dispatcher);
std::uint32_t mamerecomp_sh4_entry_mvsc2u();
#endif
#if defined(MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS)
void mamerecomp_register_sh4_mvsc2u_draw_pref_207adc(Sh4Dispatcher& dispatcher);
#endif
#endif

struct Sh4Probe {
    bool interpreter_only = false;
    Sh4Context cpu;
    std::array<bool, 3> irq_lines{};
    HollyInterrupts holly;
    NaomiPvr2 pvr;
    std::unique_ptr<MapleDma> maple;
    std::unique_ptr<HollyC2Dma> c2_dma;
    NaomiAddressSpace memory;
    Sh4Bus bus;
    Sh4Dispatcher dispatcher;
    std::uint32_t entry = 0;
    bool started = false;

    explicit Sh4Probe(bool interpreterOnly)
        : interpreter_only(interpreterOnly),
          holly([this](unsigned irl, bool asserted) {
              const unsigned level = 15u - irl;
              const std::size_t index = level == 2 ? 0 : level == 4 ? 1 : 2;
              irq_lines[index] = asserted;
              const unsigned encoded = irq_lines[2] ? 9u : irq_lines[1] ? 11u :
                                       irq_lines[0] ? 13u : 15u;
              cpu.setIrlnInput(encoded);
          }),
          memory(NaomiDeviceBus{
              [this](std::uint32_t address, unsigned width)
                  -> std::optional<std::uint32_t> {
                  if (c2_dma && c2_dma->handles(address))
                      return c2_dma->readRegister(address);
                  if (holly.handles(address)) return holly.readRegister(address);
                  if (maple && address >= 0x005f6c00u && address < 0x005f6d00u)
                      return maple->readRegister(address & ~3u);
                  if (pvr.handles(address)) return pvr.read(address, width);
                  return std::nullopt;
              },
              [this](std::uint32_t address, unsigned width, std::uint32_t value,
                     std::uint32_t writer_pc) {
                  if (c2_dma && c2_dma->handles(address)) {
                      c2_dma->writeRegister(address, value);
                      return true;
                  }
                  if (holly.handles(address)) {
                      holly.writeRegister(address, value);
                      return true;
                  }
                  if (maple && address >= 0x005f6c00u && address < 0x005f6d00u) {
                      maple->writeRegister(address & ~3u, value);
                      return true;
                  }
                  if (!pvr.handles(address)) return false;
                  if (std::getenv("MAMERECOMP_NAOMI_TRACE_PVR_PC") &&
                      address >= 0x005f8000u && address < 0x005fa000u) {
                      const auto reg = address & ~3u;
                      if (reg == 0x005f8044u || reg == 0x005f8050u ||
                          reg == 0x005f805cu || reg == 0x005f8144u ||
                          reg == 0x005f8014u || reg == 0x005f8160u) {
                          static unsigned traced_pvr_control_writes = 0;
                          if (traced_pvr_control_writes++ < 512)
                              std::fprintf(stderr,
                                           "Naomi PVR CTRL pc=%08x address=%08x width=%u value=%08x\n",
                                           writer_pc, address, width, value);
                      }
                  }
                  pvr.write(address, width, value);
                  return true;
              }}),
          bus(memory.make_sh4_bus()) {
        memory.setG1DmaIrq([this](unsigned bit) {
            holly.raise(HollyInterrupts::Group::normal, bit);
        });
        memory.setAicaG2DmaIrq([this](unsigned bit) {
            holly.raise(HollyInterrupts::Group::normal, bit);
        });
        memory.setAicaSh4Irq([this](bool asserted) {
            if (asserted)
                holly.raise(HollyInterrupts::Group::external, 1);
            else
                holly.clear(HollyInterrupts::Group::external, 1u << 1);
        });
        memory.setDmaWriteObserver([this](std::uint32_t address, unsigned width) {
            cpu.noteAotCodeWrite(address, width);
        });
        pvr.setDmaBus(
            [this](std::uint32_t address) { return memory.read32(address); },
            [this](std::uint32_t address, std::uint32_t value) {
                memory.writeDma32(address, value);
            },
            [this](unsigned bit) {
                holly.raise(HollyInterrupts::Group::normal, bit);
            });
        c2_dma = std::make_unique<HollyC2Dma>(HollyC2DmaBus{
            [this](std::uint32_t address, unsigned width) {
                switch (width) {
                case 1: return static_cast<std::uint32_t>(memory.read8(address));
                case 2: return static_cast<std::uint32_t>(memory.read16(address));
                default: return memory.read32(address);
                }
            },
            [this](std::uint32_t address, unsigned width, std::uint32_t value) {
                switch (width) {
                case 1: memory.writeDma8(address, static_cast<std::uint8_t>(value)); break;
                case 2: memory.writeDma16(address, static_cast<std::uint16_t>(value)); break;
                default: memory.writeDma32(address, value); break;
                }
            },
            [this](unsigned bit) { holly.raise(HollyInterrupts::Group::normal, bit); }});
        maple = std::make_unique<MapleDma>(
            MapleMemory{
                [this](std::uint32_t address, std::uint32_t& value) {
                    value = memory.read32(address);
                    return true;
                },
                [this](std::uint32_t address, std::uint32_t value) {
                    memory.writeDma32(address, value);
                    return true;
                }},
            make_jvs_maple_endpoint(g_naomiJvs13551()), holly);
        wireCpu();
#if defined(MAMERECOMP_NAOMI_HAS_TRANSLATED_SH4)
#if defined(MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION)
        mamerecomp_register_sh4_mvsc2u_boot_ram(dispatcher);
#else
        mamerecomp_register_sh4_mvsc2u(dispatcher);
#endif
#if defined(MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS)
        // The dynamic ROM set contains only entries whose bytes are verified
        // as ROM-backed; 0x0c04b88c remains supplied by the RAM snapshot.
        mamerecomp_register_sh4_mvsc2u_draw_pref_207adc(dispatcher);
#endif
        mamerecomp_register_sh4_mvsc2u_bios(dispatcher);
        mamerecomp_register_sh4_naomi_bios_reset(dispatcher);
        mamerecomp_register_sh4_naomi_bios_reset_p1(dispatcher);
        mamerecomp_register_sh4_naomi_bootstrap(dispatcher);
        mamerecomp_register_sh4_naomi_bios_ram_shadow(dispatcher);
        mamerecomp_register_sh4_naomi_bios_ram_vector(dispatcher);
        mamerecomp_register_sh4_naomi_bios_ram_irq(dispatcher);
        mamerecomp_register_sh4_naomi_bios_copied_code(dispatcher);
        entry = mamerecomp_sh4_entry_naomi_bios_reset();
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS"))
            std::fprintf(stderr,
                         "SH-4 task trace armed entry=%08x callback-aot=%u\n",
                         entry, dispatcher.contains(0x0c04b88cu) ? 1u : 0u);
#else
        // Both interpreter-only and hybrid paths start at the Naomi BIOS reset
        // vector. The BIOS then performs the cartridge boot sequence.
        entry = 0xa0000000u;
#endif
    }

    void wireCpu() {
        cpu.bus = &bus;
        cpu.interpreter_only = interpreter_only;
        cpu.interpreter_step = [](Sh4Context& context) {
            return mamerecomp::cpus::sh4::flycast::step(context);
        };
        cpu.cycle_sink = [this](unsigned cycles) {
            memory.advanceSh4Cycles(cycles);
            pvr.advanceCycles(cycles, [this](unsigned bit) {
                holly.raise(HollyInterrupts::Group::normal, bit);
            });
            if (c2_dma) c2_dma->advanceCycles(cycles);
        };
        cpu.store_queue_handler = [this](Sh4Context& context, std::uint32_t address) {
            // SH-4 PREF addresses the E-area store queue, not the destination
            // bus. MAME sh4.cpp and Flycast sh4/storeq.cpp both apply the
            // selected QACR Area bits before dispatching the 32-byte write.
            const auto queue = (address >> 5) & 1u;
            const auto qacr = memory.read32(0xff000038u + queue * 4u);
            const auto destination = (address & 0x03ffffe0u) |
                                     ((qacr & 0x1cu) << 24);
            const bool goes_to_pvr = pvr.handles(destination);

            if (std::getenv("MAMERECOMP_NAOMI_TRACE_SQ")) {
                static unsigned trace_events = 0;
                if (trace_events++ < 128)
                    std::fprintf(stderr,
                                 "Naomi SH-4 PREF pc=%08x raw=%08x QACR%u=%08x dest=%08x route=%s\n",
                                 context.pc, address, queue, qacr, destination,
                                 goes_to_pvr ? "PVR" : "memory");
            }

            if (goes_to_pvr) {
                pvr.writeStoreQueue(destination, context.store_queue);
                return;
            }

            const auto queue_offset = static_cast<std::size_t>(queue) * 32u;
            for (std::size_t offset = 0; offset < 32u; offset += 4u) {
                const auto* bytes = context.store_queue.data() + queue_offset + offset;
                const auto value = static_cast<std::uint32_t>(bytes[0]) |
                                   (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                   (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                   (static_cast<std::uint32_t>(bytes[3]) << 24);
                memory.writeDma32(destination + static_cast<std::uint32_t>(offset), value);
            }
        };
    }
};
} // namespace mamerecomp::naomi

namespace {

// MAME: src/mame/sega/naomi.cpp, ROM_START(mvsc2u)/ROM_START(mvsc2) and
// corresponding GAME declarations. The machine is NAOMI; `naomim1` and
// `naomim2` in MAME identify cartridge configurations, not NAOMI generations.
constexpr NaomiProfileRecord kProfiles[]{
    {"mvsc2u", "Marvel Vs. Capcom 2: New Age of Heroes (USA, Rev A)", "mvsc2",
        "171-7919A / Sega M2 crypt", ":rom_board:segam2crypt:key", "0002c840",
        0x07800000, 1, 1, 1, true, true, false, false},
    {"mvsc2", "Marvel Vs. Capcom 2: New Age of Heroes (Export/Korea, Rev A)", "naomi",
        "171-7978B", ":rom_board:key", "c18b6e7c", 0x08800000, 1,
        1, 1, true, true, false, false},
};

} // namespace

extern "C" std::uint32_t mamerecomp_naomi_abi() {
    return 6;
}

extern "C" const NaomiProfileRecord* mamerecomp_naomi_profiles(std::size_t* count) {
    if (count) *count = sizeof(kProfiles) / sizeof(kProfiles[0]);
    return kProfiles;
}

extern "C" int mamerecomp_naomi_register_sh4(const char* set_id,
                                               void* dispatcher,
                                               std::uint32_t* entry) {
    if (!set_id || !dispatcher || !entry)
        return 0;
#if defined(MAMERECOMP_NAOMI_HAS_TRANSLATED_SH4)
    if (std::strcmp(set_id, "mvsc2u") == 0) {
        auto& sh4 = *static_cast<mamerecomp::naomi::Sh4Dispatcher*>(dispatcher);
#if defined(MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION)
        mamerecomp::naomi::mamerecomp_register_sh4_mvsc2u_boot_ram(sh4);
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS"))
            std::fprintf(stderr, "SH-4 task callback registration stage=boot_ram contains=%u\n",
                         sh4.contains(0x0c04b88cu) ? 1u : 0u);
        *entry = mamerecomp::naomi::mamerecomp_sh4_entry_mvsc2u_boot_ram();
#else
        mamerecomp::naomi::mamerecomp_register_sh4_mvsc2u(sh4);
        *entry = mamerecomp::naomi::mamerecomp_sh4_entry_mvsc2u();
#endif
#if defined(MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS)
        mamerecomp::naomi::mamerecomp_register_sh4_mvsc2u_draw_pref_207adc(sh4);
#endif
        mamerecomp::naomi::mamerecomp_register_sh4_mvsc2u_bios(sh4);
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS"))
            std::fprintf(stderr, "SH-4 task callback registration stage=game_bios contains=%u\n",
                         sh4.contains(0x0c04b88cu) ? 1u : 0u);
        return 1;
    }
#else
    (void)set_id;
    (void)dispatcher;
    (void)entry;
#endif
    return 0;
}

namespace {
void copy_probe_result(NaomiSh4ProbeResult* output,
                       mamerecomp::naomi::Sh4Probe& probe,
                       const mamerecomp::naomi::Sh4RunResult* run = nullptr) {
    if (!output) return;
    *output = {};
    output->entry = probe.entry;
    output->instructions_executed = run ? run->instructions_executed : probe.cpu.instructions_executed;
    output->native_instructions = run ? run->native_instructions : probe.cpu.native_instructions;
    output->interpreted_instructions = run ? run->interpreted_instructions : probe.cpu.interpreted_instructions;
    output->pc = probe.cpu.pc;
    output->unresolved_target = probe.cpu.unresolved_target;
    output->exception_code = probe.cpu.exception_code;
    output->pvr_ta_bytes = probe.pvr.taBytesSubmitted();
    output->pvr_yuv_bytes = probe.pvr.yuvBytesSubmitted();
    output->pvr_vram_bytes = probe.pvr.vramBytesWritten();
    output->pvr_register_writes = probe.pvr.registerWrites();
    output->pvr_scanline = probe.pvr.scanline();
    output->pvr_render_start_requests = probe.pvr.renderStartRequests();
    output->pvr_framebuffer_enabled = probe.pvr.read(0x005f8044u, 4) & 1u;
    output->pvr_ta_completed_lists = probe.pvr.taLists().size();
    std::copy(probe.pvr.taParameterCounts().begin(), probe.pvr.taParameterCounts().end(),
              output->pvr_ta_parameter_counts);
    std::copy(probe.pvr.taListPacketCounts().begin(), probe.pvr.taListPacketCounts().end(),
              output->pvr_ta_list_packet_counts);
    output->holly_normal_status = probe.holly.normalStatusWithSummary();
    output->holly_level2_mask = probe.holly.mask(2, mamerecomp::naomi::HollyInterrupts::Group::normal);
    output->holly_level4_mask = probe.holly.mask(4, mamerecomp::naomi::HollyInterrupts::Group::normal);
    output->holly_level6_mask = probe.holly.mask(6, mamerecomp::naomi::HollyInterrupts::Group::normal);
    output->interrupts_accepted = probe.cpu.interrupts_accepted;
    output->indirect_calls = probe.cpu.indirect_calls;
    output->last_indirect_target = probe.cpu.last_indirect_target;
    output->r1 = probe.cpu.r[1];
    output->r2 = probe.cpu.r[2];
    output->r3 = probe.cpu.r[3];
    output->r8 = probe.cpu.r[8];
    output->r9 = probe.cpu.r[9];
    output->r10 = probe.cpu.r[10];
    output->r11 = probe.cpu.r[11];
    output->r12 = probe.cpu.r[12];
    output->r14 = probe.cpu.r[14];
    output->r13 = probe.cpu.r[13];
    output->r15 = probe.cpu.r[15];
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_BOOT_STATE")) {
        std::fprintf(stderr,
                     "Naomi BIOS state pc=%08x pr=%08x sr=%08x T=%u Q=%u M=%u VBR=%08x IRLN=%u\n",
                     probe.cpu.pc, probe.cpu.pr, probe.cpu.sr,
                     probe.cpu.t ? 1u : 0u, probe.cpu.q ? 1u : 0u,
                     probe.cpu.m_bit ? 1u : 0u, probe.cpu.vbr, probe.cpu.irln);
        for (unsigned base = 0; base < 16; base += 4)
            std::fprintf(stderr, "Naomi BIOS regs r%u=%08x r%u=%08x r%u=%08x r%u=%08x\n",
                         base, probe.cpu.r[base], base + 1, probe.cpu.r[base + 1],
                         base + 2, probe.cpu.r[base + 2], base + 3, probe.cpu.r[base + 3]);
        std::fprintf(stderr,
                     "Naomi BIOS memory r8=%08x->[ %08x %08x ] r9=%08x->[ %08x %08x ] sp=%08x\n",
                     probe.cpu.r[8], probe.memory.read32(probe.cpu.r[8]),
                     probe.memory.read32(probe.cpu.r[8] + 4), probe.cpu.r[9],
                     probe.memory.read32(probe.cpu.r[9]), probe.memory.read32(probe.cpu.r[9] + 4),
                     probe.cpu.r[15]);
        for (unsigned offset = 0; offset < 48; offset += 4)
            std::fprintf(stderr, "Naomi BIOS stack [%08x]=%08x\n",
                         probe.cpu.r[15] + offset,
                         probe.memory.read32(probe.cpu.r[15] + offset));
        std::fprintf(stderr,
                     "Naomi SH-4 interrupt state sr=%08x vbr=%08x irln=%u intevt=%08x Holly={ISTNRM:%08x,IML2:%08x,IML4:%08x,IML6:%08x}\n",
                     probe.cpu.sr, probe.cpu.vbr, probe.cpu.irln, probe.cpu.intevt,
                     probe.holly.normalStatusWithSummary(),
                     probe.holly.mask(2, mamerecomp::naomi::HollyInterrupts::Group::normal),
                     probe.holly.mask(4, mamerecomp::naomi::HollyInterrupts::Group::normal),
                     probe.holly.mask(6, mamerecomp::naomi::HollyInterrupts::Group::normal));
        std::fprintf(stderr,
                     "Naomi SH-4 INTC={ICR:%04x,IPRA:%04x,IPRB:%04x,IPRC:%04x} TMU={TSTR:%02x,TCOR0:%08x,TCNT0:%08x,TCR0:%04x,TCOR1:%08x,TCNT1:%08x,TCR1:%04x,TCOR2:%08x,TCNT2:%08x,TCR2:%04x}\n",
                     probe.memory.read16(0xffd00000), probe.memory.read16(0xffd00004),
                     probe.memory.read16(0xffd00008), probe.memory.read16(0xffd0000c),
                     probe.memory.read8(0xffd80004), probe.memory.read32(0xffd80008),
                     probe.memory.read32(0xffd8000c), probe.memory.read16(0xffd80010),
                     probe.memory.read32(0xffd80014), probe.memory.read32(0xffd80018),
                     probe.memory.read16(0xffd8001c), probe.memory.read32(0xffd80020),
                     probe.memory.read32(0xffd80024), probe.memory.read16(0xffd80028));
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_PC_BYTES")) {
        std::fprintf(stderr, "Naomi SH-4 code bytes pc=%08x:", probe.cpu.pc);
        for (unsigned offset = 0; offset < 32; offset += 2)
            std::fprintf(stderr, " %04x", probe.memory.read16(probe.cpu.pc + offset));
        std::fputc('\n', stderr);
        for (const auto address : {0x0c001cbau, 0x0c021000u, 0x0c038d90u}) {
            std::fprintf(stderr, "Naomi SH-4 code bytes at %08x:", address);
            for (unsigned offset = 0; offset < 32; offset += 2)
                std::fprintf(stderr, " %04x", probe.memory.read16(address + offset));
            std::fputc('\n', stderr);
        }
    }
    if (const char* path = std::getenv("MAMERECOMP_NAOMI_DUMP_BOOT_RAM");
        path && *path && probe.cpu.instructions_executed != 0) {
        std::ofstream dump(path, std::ios::binary | std::ios::trunc);
        constexpr std::uint32_t start = 0x0c020000u;
        constexpr std::uint32_t size = 0x00400000u;
        for (std::uint32_t offset = 0; dump && offset < size; ++offset)
            dump.put(static_cast<char>(probe.memory.read8(start + offset)));
        if (dump)
            std::fprintf(stderr, "Naomi SH-4 dumped boot RAM [%08x,%08x) to %s\n",
                         start, start + size, path);
    }
    if (const char* path = std::getenv("MAMERECOMP_NAOMI_DUMP_MAIN_RAM");
        path && *path && probe.cpu.instructions_executed != 0) {
        std::ofstream dump(path, std::ios::binary | std::ios::trunc);
        const auto& ram = probe.memory.mainRam();
        dump.write(reinterpret_cast<const char*>(ram.data()),
                   static_cast<std::streamsize>(ram.size()));
        if (dump)
            std::fprintf(stderr,
                         "Naomi SH-4 dumped main RAM [0c000000,0e000000) bytes=%zu to %s\n",
                         ram.size(), path);
        else
            std::fprintf(stderr, "ERROR: failed writing main RAM dump path %s\n", path);
    }
    if (const char* path = std::getenv("MAMERECOMP_NAOMI_DUMP_LOW_RAM");
        path && *path && probe.cpu.instructions_executed != 0) {
        std::ofstream dump(path, std::ios::binary | std::ios::trunc);
        constexpr std::uint32_t start = 0x0c000000u;
        constexpr std::uint32_t size = 0x00040000u;
        for (std::uint32_t offset = 0; dump && offset < size; ++offset)
            dump.put(static_cast<char>(probe.memory.read8(start + offset)));
        if (dump)
            std::fprintf(stderr, "Naomi SH-4 dumped low RAM [%08x,%08x) to %s\n",
                         start, start + size, path);
    }
    if (const char* path = std::getenv("MAMERECOMP_NAOMI_DUMP_TA");
        path && *path) {
        const auto& stream = probe.pvr.taStream();
        std::ofstream dump(path, std::ios::binary | std::ios::trunc);
        if (!dump) {
            std::fprintf(stderr, "ERROR: cannot open PVR TA dump path %s\n", path);
        } else {
            dump.write(reinterpret_cast<const char*>(stream.data()),
                       static_cast<std::streamsize>(stream.size()));
            if (!dump)
                std::fprintf(stderr, "ERROR: failed writing PVR TA dump path %s\n", path);
            else
                std::fprintf(stderr, "Naomi PVR dumped TA stream bytes=%zu to %s\n",
                             stream.size(), path);
        }
    }
    if (const char* path = std::getenv("MAMERECOMP_NAOMI_DUMP_VRAM");
        path && *path) {
        const auto& vram = probe.pvr.vram();
        std::ofstream dump(path, std::ios::binary | std::ios::trunc);
        if (!dump) {
            std::fprintf(stderr, "ERROR: cannot open PVR VRAM dump path %s\n", path);
        } else {
            dump.write(reinterpret_cast<const char*>(vram.data()),
                       static_cast<std::streamsize>(vram.size()));
            if (!dump) {
                std::fprintf(stderr, "ERROR: failed writing PVR VRAM dump path %s\n", path);
            } else {
                std::fprintf(stderr,
                             "Naomi PVR dumped VRAM bytes=%zu to %s FB_R_CTRL=%08x FB_R_SOF1=%08x FB_R_SOF2=%08x FB_R_SIZE=%08x FB_W_SOF1=%08x FB_W_SOF2=%08x\n",
                             vram.size(), path,
                             probe.pvr.read(0x005f8044u, 4),
                             probe.pvr.read(0x005f8050u, 4),
                             probe.pvr.read(0x005f8054u, 4),
                             probe.pvr.read(0x005f805cu, 4),
                             probe.pvr.read(0x005f8060u, 4),
                             probe.pvr.read(0x005f8064u, 4));
            }
        }
    }
    for (std::size_t i = 0; i < 16 && i < probe.cpu.indirect_calls; ++i) {
        const auto call_index = probe.cpu.indirect_calls - std::min<std::uint64_t>(16, probe.cpu.indirect_calls) + i;
        output->recent_indirect_targets[i] = probe.cpu.recent_indirect_targets[
            call_index % probe.cpu.recent_indirect_targets.size()];
    }
    output->execution_stopped = probe.cpu.execution_stopped ? 1 : 0;
    if (run) {
        output->instructions_executed = run->instructions_executed;
        output->stopped_by_budget = run->stopped_by_budget ? 1 : 0;
    }
}

void copy_error(NaomiSh4ProbeResult* output, const char* message) {
    if (!output) return;
    *output = {};
    if (!message) return;
    std::strncpy(output->error, message, sizeof(output->error) - 1);
}
}

extern "C" void* mamerecomp_naomi_sh4_probe_create(const char* set_id,
                                                       int interpreter_only) {
    if (!set_id || std::strcmp(set_id, "mvsc2u") != 0) return nullptr;
    try {
        return new mamerecomp::naomi::Sh4Probe(interpreter_only != 0);
    } catch (...) {
        return nullptr;
    }
}

extern "C" void mamerecomp_naomi_sh4_probe_destroy(void* probe) {
    delete static_cast<mamerecomp::naomi::Sh4Probe*>(probe);
}

extern "C" int mamerecomp_naomi_sh4_probe_start(void* opaque,
                                                  const std::uint8_t* rom,
                                                  std::size_t rom_size,
                                                  const std::uint8_t* board_id_eeprom,
                                                  std::size_t board_id_eeprom_size,
                                                  const std::uint8_t* bios,
                                                  std::size_t bios_size,
                                                  NaomiSh4ProbeResult* result) {
    auto* probe = static_cast<mamerecomp::naomi::Sh4Probe*>(opaque);
    if (!probe || !rom || rom_size == 0 || !board_id_eeprom ||
        board_id_eeprom_size == 0 || !bios || bios_size == 0) {
        copy_error(result, "invalid Naomi probe, ROM, board-ID EEPROM, or BIOS image");
        return 0;
    }
    try {
        const bool eeprom_configured = mamerecomp::naomi::g_naomiJvs13551().configureSystemEeprom(
            std::span<const std::uint8_t>(rom, rom_size));
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_BOOT_ID")) {
            const auto& eeprom = mamerecomp::naomi::g_naomiJvs13551().eeprom;
            std::fprintf(stderr,
                         "Naomi BootID EEPROM configured=%u game-id=%02x%02x%02x%02x cabinet=%02x vertical=%02x\n",
                         eeprom_configured ? 1u : 0u, eeprom[3], eeprom[4], eeprom[5], eeprom[6],
                         rom_size > 0x42au ? rom[0x429] : 0u,
                         rom_size > 0x42bu ? rom[0x42b] : 0u);
        }
        probe->memory.loadBios(std::span<const std::uint8_t>(bios, bios_size));
        probe->memory.loadM2RomBoard(
            std::span<const std::uint8_t>(rom, rom_size), 0x0002c840u,
            std::span<const std::uint8_t>(board_id_eeprom, board_id_eeprom_size));
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_BOOT_RAM_PROVENANCE"))
            std::fprintf(stderr,
                         "Naomi boot RAM initial [0c020000]=%08x [0c020004]=%08x [0c020008]=%08x\n",
                         probe->memory.read32(0x0c020000u),
                         probe->memory.read32(0x0c020004u),
                         probe->memory.read32(0x0c020008u));
        probe->cpu = {};
        probe->wireCpu();
        // The official Naomi BIOS owns startup; no REIOS handoff is used.
        probe->cpu.sr = 0x700000f0u;
        probe->cpu.vbr = 0xa0000000u;
        probe->cpu.pc = 0xa0000000u;
        if (probe->interpreter_only)
            probe->entry = 0xa0000000u;
        probe->started = true;
        copy_probe_result(result, *probe);
        return 1;
    } catch (const std::exception& error) {
        copy_error(result, error.what());
        return 0;
    }
}

extern "C" int mamerecomp_naomi_sh4_probe_run(void* opaque,
                                                std::uint64_t budget,
                                                NaomiSh4ProbeResult* result) {
    auto* probe = static_cast<mamerecomp::naomi::Sh4Probe*>(opaque);
    if (!probe || !probe->started || budget == 0) {
        copy_error(result, "Naomi SH-4 probe must be started with a nonzero budget");
        return 0;
    }
    const auto run = probe->dispatcher.run(probe->cpu, probe->entry, budget);
    copy_probe_result(result, *probe, &run);
    return 1;
}

extern "C" int mamerecomp_naomi_sh4_probe_frame(void* opaque,
                                                   NaomiPvrFrameInfo* frame_info,
                                                   std::uint8_t* rgb,
                                                   std::size_t rgb_capacity) {
    auto* probe = static_cast<mamerecomp::naomi::Sh4Probe*>(opaque);
    if (!probe || !frame_info) return 0;
    const auto frame = probe->pvr.captureFramebuffer();
    frame_info->width = frame.width;
    frame_info->height = frame.height;
    frame_info->control = frame.control;
    frame_info->size = frame.size;
    frame_info->start = frame.start;
    frame_info->rgb_bytes = frame.rgb.size();
    if (!rgb) return 1;
    if (rgb_capacity < frame.rgb.size()) return 0;
    std::copy(frame.rgb.begin(), frame.rgb.end(), rgb);
    return 1;
}
