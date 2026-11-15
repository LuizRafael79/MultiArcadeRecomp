#include "kof98_z80_host.h"

#include "native_z80.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>
#include <stdexcept>
#include <vector>

namespace {
struct BusHooks {
    void *context = nullptr;
    ng_z80_read_fn read = nullptr;
    ng_z80_write_fn write = nullptr;
    ng_z80_in_fn port_in = nullptr;
    ng_z80_out_fn port_out = nullptr;
    ng_z80_address_fn dynamic_code = nullptr;
    ng_z80_address_fn valid_code = nullptr;
    ng_z80_mapping_fn mapping = nullptr;
};

class NeoGeoZ80Bus final : public multiarcaderecomp::z80::Z80Bus {
public:
    BusHooks hooks;
    uint8_t Read(uint16_t address) override {
        return hooks.read ? hooks.read(hooks.context, address) : 0xFF;
    }
    void Write(uint16_t address, uint8_t value) override {
        if (hooks.write) hooks.write(hooks.context, address, value);
    }
    uint8_t In(uint16_t port) override {
        return hooks.port_in ? hooks.port_in(hooks.context, port) : 0xFF;
    }
    void Out(uint16_t port, uint8_t value) override {
        if (hooks.port_out) hooks.port_out(hooks.context, port, value);
    }
    uint32_t Mapping() override {
        return hooks.mapping ? hooks.mapping(hooks.context) : 0;
    }
    bool IsDynamicCode(uint16_t address) const override {
        return hooks.dynamic_code && hooks.dynamic_code(hooks.context, address);
    }
    bool IsValidCodeAddress(uint16_t address) const override {
        return !hooks.valid_code || hooks.valid_code(hooks.context, address);
    }
};

NeoGeoZ80Bus s_bus;
multiarcaderecomp::z80::Z80 s_cpu(s_bus);
const multiarcaderecomp::z80::Z80Code *s_bios_code = nullptr;
const multiarcaderecomp::z80::Z80Code *s_cart_code = nullptr;
int s_active_bios = -1;
std::vector<uint16_t> s_bios_misses;
std::vector<uint16_t> s_cart_misses;

void collect_entries(std::vector<uint16_t> &out) {
    out.insert(out.end(), s_cpu.newEntries.begin(), s_cpu.newEntries.end());
    s_cpu.newEntries.clear();
}

void persist_entries(const std::string &path, std::vector<uint16_t> entries) {
    std::ifstream old(path);
    std::string token;
    while (old >> token) {
        try {
            entries.push_back(static_cast<uint16_t>(std::stoul(token, nullptr, 16)));
        } catch (...) {
        }
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[z80-misses] cannot write %s\n", path.c_str());
        return;
    }
    for (uint16_t pc : entries) out << std::uppercase << std::hex << std::setw(4)
                                   << std::setfill('0') << pc << '\n';
    std::fprintf(stderr, "[z80-misses] persisted %zu roots to %s\n", entries.size(), path.c_str());
}
}

extern "C" void ng_z80_recomp_set_codes(const void *bios_code, const void *cart_code) {
    s_bios_code = static_cast<const multiarcaderecomp::z80::Z80Code *>(bios_code);
    s_cart_code = static_cast<const multiarcaderecomp::z80::Z80Code *>(cart_code);
    if (std::getenv("NG_PROFILE")) {
        std::fprintf(stderr, "[audio] Z80 AOT entries BIOS=%zu cart=%zu\n",
                     s_bios_code ? s_bios_code->entryCount : 0,
                     s_cart_code ? s_cart_code->entryCount : 0);
    }
}

extern "C" void ng_z80_recomp_set_bus(void *context, ng_z80_read_fn read,
                                         ng_z80_write_fn write, ng_z80_in_fn port_in,
                                         ng_z80_out_fn port_out, ng_z80_address_fn dynamic_code,
                                         ng_z80_address_fn valid_code, ng_z80_mapping_fn mapping) {
    s_bus.hooks = {context, read, write, port_in, port_out, dynamic_code, valid_code, mapping};
}

extern "C" void ng_z80_recomp_reset(uint64_t cycle) {
    s_cpu.Reset();
    s_cpu.cycles = cycle;
    s_cpu.recording = true;
}

extern "C" void ng_z80_recomp_run_until(uint64_t cycle, int bios_rom, int irq_asserted) {
    const int selected_bios = bios_rom != 0;
    if (s_active_bios != selected_bios) {
        if (s_active_bios == 1) collect_entries(s_bios_misses);
        else if (s_active_bios == 0) collect_entries(s_cart_misses);
        s_active_bios = selected_bios;
    }
    multiarcaderecomp::z80::SetZ80Code(bios_rom ? s_bios_code : s_cart_code);
    multiarcaderecomp::z80::z80native::SetImage(bios_rom != 0);
    static int last_bios = -1;
    if (std::getenv("NG_PROFILE") && last_bios != (bios_rom != 0)) {
        std::fprintf(stderr, "[audio] Z80 maps %s ROM at PC=$%04X\n",
                     bios_rom ? "BIOS" : "cart", s_cpu.pc);
        last_bios = bios_rom != 0;
    }
    s_cpu.SetInterruptLine(irq_asserted != 0);
    const char *mode = std::getenv("NG_Z80_MODE");
    const auto run_mode = mode && std::string(mode) == "interpreter"
        ? multiarcaderecomp::z80::Z80::Mode::Interpreter
        : mode && std::string(mode) == "blocks"
            ? multiarcaderecomp::z80::Z80::Mode::Blocks
            : multiarcaderecomp::z80::Z80::Mode::Native;
    s_cpu.Run(cycle, run_mode);
}

extern "C" void ng_z80_recomp_raise_nmi(void) {
    s_cpu.RaiseNmi();
}

extern "C" void ng_z80_recomp_set_irq(int asserted) {
    s_cpu.SetInterruptLine(asserted != 0);
}

extern "C" void ng_z80_recomp_report(void) {
    multiarcaderecomp::z80::z80native::ReportInterpreterProfile();
    if (s_active_bios == 1) collect_entries(s_bios_misses);
    else if (s_active_bios == 0) collect_entries(s_cart_misses);
    auto print_misses = [](const char *name, std::vector<uint16_t> &entries) {
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
        std::fprintf(stderr, "[z80-misses-%s] %zu fixed-ROM entries", name, entries.size());
        for (uint16_t pc : entries) std::fprintf(stderr, " $%04X", pc);
        std::fputc('\n', stderr);
    };
    print_misses("bios", s_bios_misses);
    print_misses("cart", s_cart_misses);
}

extern "C" void ng_z80_recomp_dump_misses(const char *path_prefix) {
    if (!path_prefix || !*path_prefix) return;
    if (s_active_bios == 1) collect_entries(s_bios_misses);
    else if (s_active_bios == 0) collect_entries(s_cart_misses);
    persist_entries(std::string(path_prefix) + ".bios.txt", s_bios_misses);
    persist_entries(std::string(path_prefix) + ".cart.txt", s_cart_misses);
    multiarcaderecomp::z80::z80native::PersistFallbackLog((std::string(path_prefix) + ".fallbacks.log").c_str());
}

extern "C" uint64_t ng_z80_recomp_cycles(void) {
    return s_cpu.cycles;
}

extern "C" uint64_t ng_z80_recomp_bus_cycles(void) {
    return s_cpu.BusCallbackCycles();
}

extern "C" uint64_t ng_z80_recomp_interpreted(void) {
    return s_cpu.interpreted;
}

extern "C" uint64_t ng_z80_recomp_native_blocks(void) {
    return s_cpu.nativeBlocks;
}

extern "C" void ng_z80_recomp_snapshot(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return;
    std::snprintf(buffer, capacity,
                  "PC=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X F2=%02X MAP=%08X",
                  static_cast<unsigned>(s_cpu.pc - 2), s_cpu.AF(), s_cpu.BC(), s_cpu.DE(), s_cpu.HL(),
                  s_cpu.ix, s_cpu.iy, s_cpu.sp, s_cpu.f2, s_bus.Mapping());
}
