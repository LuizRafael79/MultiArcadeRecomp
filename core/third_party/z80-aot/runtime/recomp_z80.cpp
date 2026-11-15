// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
//
// Running the Z80: the loop around the sound driver's translated code and the interpreter (Z80::Run).
// The same rules as for the 68000 (recomp.cpp): time and the interrupt are looked at only where a block
// starts, and the interpreter can be run that way too (Mode::Blocks), which is what translated code is
// checked against.
#include "native_z80.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mamerecomp::z80 {

namespace {
    uint64_t s_interpreted_pc[0x10000] = {};
    bool s_image_bios = false;
    FILE *s_step_trace = nullptr;
    bool s_step_trace_initialized = false;
    uint64_t s_step_trace_until = 1000000;
    z80native::HostCycleProvider s_host_cycle_provider;
    struct FallbackKey {
        Z80FallbackKind kind;
        bool bios;
        uint16_t pc;
        uint8_t opcode;
        uint32_t mapping;
        bool operator==(const FallbackKey &) const = default;
    };
    struct FallbackHash {
        size_t operator()(const FallbackKey &k) const {
            size_t h = static_cast<size_t>(k.kind);
            h = h * 131u + k.bios;
            h = h * 131u + k.pc;
            h = h * 131u + k.opcode;
            h = h * 131u + k.mapping;
            return h;
        }
    };
    struct FallbackRecord { FallbackKey key; uint64_t count; };
    std::vector<FallbackRecord> s_fallbacks;
    std::unordered_map<FallbackKey, size_t, FallbackHash> s_fallback_index;

    struct Tables {
        Z80Fn fn[0x10000] = {};
        uint8_t covered[0x10000] = {};
        explicit Tables(const Z80Code *code) {
            if (!code) return;
            for (size_t n = 0; n < code->entryCount; n++) {
                fn[code->entries[n].address] = code->entries[n].fn;
            }
            for (size_t n = 0; n < code->rangeCount; n++) {
                for (unsigned at = code->ranges[n].start; at < code->ranges[n].end; at++) {
                    covered[at] = 1;
                }
            }
        }
        bool Inside(uint16_t pc) const {
            return covered[pc] && !fn[pc];
        }
    };
    const Z80Code *s_code = nullptr;
    std::unique_ptr<Tables> s_tables;
    const Tables &GetTables() {
        if (!s_tables) s_tables = std::make_unique<Tables>(s_code);
        return *s_tables;
    }
    void TraceStep(Z80 &cpu, uint16_t pc, uint8_t opcode, uint64_t before) {
        if (!s_step_trace_initialized) {
            s_step_trace_initialized = true;
            const char *path = std::getenv("NG_Z80_STEP_TRACE");
            if (path && *path) s_step_trace = std::fopen(path, "w");
            const char *until = std::getenv("NG_Z80_STEP_TRACE_UNTIL");
            if (until && *until) s_step_trace_until = std::strtoull(until, nullptr, 0);
        }
        if (!s_step_trace || cpu.cycles > s_step_trace_until) return;
        std::fprintf(s_step_trace,
                     "%llu HOST=%lld PC=%04X OP=%02X DELTA=%llu NEXT=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X IFF=%d%d IRQ=%d EIDELAY=%d\n",
                     static_cast<unsigned long long>(before),
                     static_cast<long long>(s_host_cycle_provider ? s_host_cycle_provider() : 0), pc, opcode,
                     static_cast<unsigned long long>(cpu.cycles - before), cpu.pc,
                     cpu.AF(), cpu.BC(), cpu.DE(), cpu.HL(), cpu.ix, cpu.iy, cpu.sp,
                     cpu.iff1, cpu.iff2, cpu.InterruptLineAsserted(), cpu.EiDelayPending());
    }
} // namespace

void z80native::SetHostCycleProvider(HostCycleProvider provider) {
    s_host_cycle_provider = std::move(provider);
}

void z80native::NoteInterpreted(uint16_t address) {
    ++s_interpreted_pc[address];
}

void z80native::SetImage(bool bios) {
    s_image_bios = bios;
}

void z80native::NoteFallback(Z80 &cpu, Z80FallbackKind kind, uint16_t address, uint8_t opcode) {
    FallbackKey key{kind, s_image_bios, address, opcode, cpu.bus.Mapping()};
    auto [it, inserted] = s_fallback_index.emplace(key, s_fallbacks.size());
    if (inserted) s_fallbacks.push_back({key, 1});
    else ++s_fallbacks[it->second].count;
}

static const char *fallback_name(Z80FallbackKind kind) {
    switch (kind) {
    case Z80FallbackKind::UncoveredEntry: return "UNCOVERED_ENTRY";
    case Z80FallbackKind::UnsupportedOpcode: return "UNSUPPORTED_OPCODE";
    case Z80FallbackKind::InvalidMapping: return "INVALID_MAPPING";
    case Z80FallbackKind::DynamicCode: return "DYNAMIC_CODE";
    case Z80FallbackKind::BankedCodeUncovered: return "BANKED_CODE_UNCOVERED";
    case Z80FallbackKind::InterruptBoundary: return "INTERRUPT_BOUNDARY";
    }
    return "UNKNOWN";
}

void z80native::ReportInterpreterProfile() {
    for (unsigned n = 0; n < 20; ++n) {
        int best = -1;
        for (unsigned pc = 0; pc < 0x10000; ++pc) {
            if (s_interpreted_pc[pc] &&
                (best < 0 || s_interpreted_pc[pc] > s_interpreted_pc[best]))
                best = static_cast<int>(pc);
        }
        if (best < 0) break;
        fprintf(stderr, "[z80-profile] %10llu interpreted from $%04X\n",
                static_cast<unsigned long long>(s_interpreted_pc[best]), best);
        s_interpreted_pc[best] = 0;
    }
    uint64_t totals[6] = {};
    for (const auto &record : s_fallbacks)
        totals[static_cast<unsigned>(record.key.kind)] += record.count;
    for (unsigned kind = 0; kind < 6; ++kind)
        if (totals[kind])
            fprintf(stderr, "[z80-fallback] %-22s %llu instructions\n",
                    fallback_name(static_cast<Z80FallbackKind>(kind)),
                    static_cast<unsigned long long>(totals[kind]));
    std::vector<const FallbackRecord *> sorted;
    for (const auto &record : s_fallbacks) sorted.push_back(&record);
    std::sort(sorted.begin(), sorted.end(), [](const auto *a, const auto *b) {
        return a->count > b->count;
    });
    for (size_t n = 0; n < std::min<size_t>(20, sorted.size()); ++n) {
        const auto &r = *sorted[n];
        fprintf(stderr, "[z80-fallback-site] %-22s %s PC=$%04X opcode=%02X map=%08X count=%llu\n",
                fallback_name(r.key.kind), r.key.bios ? "BIOS" : "CART", r.key.pc,
                r.key.opcode, r.key.mapping, static_cast<unsigned long long>(r.count));
    }
}

void z80native::PersistFallbackLog(const char *path) {
    if (!path || !*path) return;
    std::map<std::string, uint64_t> observed;
    {
        std::ifstream previous(path);
        std::string line;
        while (std::getline(previous, line)) {
            std::istringstream record(line);
            std::string kind, source, pc, opcode, mapping, count;
            if (!(record >> kind >> source >> pc >> opcode >> mapping >> count)
                || !pc.starts_with("PC=") || !opcode.starts_with("OPCODE=")
                || !mapping.starts_with("MAP=") || !count.starts_with("COUNT=")) continue;
            try {
                const auto value = static_cast<uint64_t>(std::stoull(count.substr(6)));
                const auto key = kind + ' ' + source + ' ' + pc + ' ' + opcode + ' ' + mapping;
                auto& peak = observed[key];
                peak = std::max(peak, value);
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    for (const auto &r : s_fallbacks) {
        std::ostringstream key;
        key << fallback_name(r.key.kind) << ' ' << (r.key.bios ? "BIOS" : "CART")
            << " PC=" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << r.key.pc
            << " OPCODE=" << std::setw(2) << static_cast<unsigned>(r.key.opcode)
            << " MAP=" << std::setw(8) << r.key.mapping;
        auto& peak = observed[key.str()];
        peak = std::max(peak, r.count);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        fprintf(stderr, "[z80-fallback] cannot write %s\n", path);
        return;
    }
    for (const auto& [key, count] : observed) out << key << " COUNT=" << count << '\n';
    fprintf(stderr, "[z80-fallback] wrote categorized log %s\n", path);
}

void SetZ80Code(const Z80Code *code) {
    if (s_code == code) return;
    s_code = code;
    s_tables.reset();
}

bool HasZ80Code() {
    return s_code && s_code->entryCount != 0;
}

namespace z80native {
    void NotHere(Z80 &c, uint16_t routine) {
        fprintf(stderr, "translated sound routine %04X entered at %04X, where it has no block\n", routine, c.pc);
        abort();
    }
} // namespace z80native

void Z80::Record(uint16_t address) {
    // (only the fixed part of the ROM is translated: what is banked in above it is data)
    if (recording && address < 0x8000 && !GetTables().fn[address]) {
        newEntries.push_back(address);
        if (newEntries.size() > 100000) {
            std::sort(newEntries.begin(), newEntries.end());
            newEntries.erase(std::unique(newEntries.begin(), newEntries.end()), newEntries.end());
        }
    }
}

void Z80::Run(uint64_t until, Mode runMode) {
    const Tables &tables = GetTables();
    budget = until;
    const bool oldStrict = strictNative;
    strictNative = runMode == Mode::NativeStrict;
    struct RestoreStrict {
        bool &value;
        bool old;
        ~RestoreStrict() { value = old; }
    } restoreStrict{strictNative, oldStrict};
    if (runMode == Mode::Check) {
        RunChecked(until);
        return;
    }
    for (;;) {
        if (cycles >= until) {
            return;
        }
        if (m_nmi) {
            ++nmiServiced;
            cycles += TakeInterrupt();
            bus.InstructionBoundary(cycles);
            continue;
        }
        if (m_afterEI) {
            // The instruction after EI is done before an interrupt can be taken. Execute its
            // single-instruction AOT entry where available; otherwise preserve the interpreter path.
            m_afterEI = false;
            if (runMode == Mode::Native || runMode == Mode::NativeStrict) {
                if (const Z80Fn fn = tables.fn[pc]) {
                    // EI's delay is exactly one guest instruction. Constrain the AOT block to
                    // that instruction; precise per-instruction yields stop before any successor.
                    const uint64_t savedBudget = budget;
                    budget = cycles + 1;
                    depth = 0;
                    m_cycles = 0;
                    fn(*this);
                    budget = savedBudget;
                    nativeBlocks++;
                    Record(pc);
                    continue;
                }
            }
            if (runMode == Mode::NativeStrict) {
                char message[128];
                std::snprintf(message, sizeof(message),
                              "Z80_NATIVE_STRICT: INTERRUPT_BOUNDARY PC=%04X opcode=%02X",
                              pc, bus.ReadOpcode(pc));
                throw std::runtime_error(message);
            }
            m_cycles = 0;
            m_jumped = false;
            refresh++;
            const uint16_t trace_pc = pc;
            const uint8_t trace_op = bus.ReadOpcode(pc);
            const uint64_t trace_before = cycles;
            z80native::NoteFallback(*this, Z80FallbackKind::InterruptBoundary, pc,
                                    trace_op);
            Execute(FetchOpcode(), 0);
            cycles += m_cycles;
            bus.InstructionBoundary(cycles);
            TraceStep(*this, trace_pc, trace_op, trace_before);
            interpreted++;
            if (m_jumped) {
                Record(pc);
            }
            continue;
        }
        if (const unsigned taken = TakeInterrupt()) {
            cycles += taken;
            bus.InstructionBoundary(cycles);
            Record(pc);
            continue;
        }
        if (halted) {
            cycles = until; // (nothing happens until the interrupt)
            bus.InstructionBoundary(cycles);
            return;
        }
        if (runMode == Mode::Native || runMode == Mode::NativeStrict) {
            if (const Z80Fn fn = tables.fn[pc]) {
                depth = 0;
                m_cycles = 0;
                fn(*this);
                // An instruction boundary may lower the active budget to
                // yield for an interrupt. Restore the caller's run limit
                // before the dispatcher resumes after servicing it.
                budget = until;
                nativeBlocks++;
                Record(pc);
                continue;
            }
        }
        do {
            m_cycles = 0;
            m_jumped = false;
            refresh++;
            const uint16_t fallback_pc = pc;
            const Z80FallbackKind kind = bus.IsDynamicCode(fallback_pc)
                ? Z80FallbackKind::DynamicCode
                : !bus.IsValidCodeAddress(fallback_pc) ? Z80FallbackKind::InvalidMapping
                : fallback_pc >= 0x8000 && fallback_pc < 0xF800
                    ? Z80FallbackKind::BankedCodeUncovered
                    : Z80FallbackKind::UncoveredEntry;
            if (runMode == Mode::NativeStrict) {
                char message[160];
                std::snprintf(message, sizeof(message),
                              "Z80_NATIVE_STRICT: uncovered PC=%04X opcode=%02X map=%08X",
                              fallback_pc, bus.ReadOpcode(fallback_pc), bus.Mapping());
                throw std::runtime_error(message);
            }
            z80native::NoteFallback(*this, kind, fallback_pc, bus.ReadOpcode(fallback_pc));
            z80native::NoteInterpreted(pc);
            const uint8_t trace_op = bus.ReadOpcode(fallback_pc);
            const uint64_t trace_before = cycles;
            Execute(FetchOpcode(), 0);
            cycles += m_cycles;
            bus.InstructionBoundary(cycles);
            TraceStep(*this, fallback_pc, trace_op, trace_before);
            interpreted++;
            if (m_jumped) {
                Record(pc);
            }
        } while (runMode != Mode::Interpreter && !halted && !m_nmi
            && !(m_interrupt && iff1 && !m_afterEI) && tables.Inside(pc));
    }
}

#ifndef CPS2RECOMP_CHECK
void Z80::RunChecked(uint64_t until) {
    fprintf(stderr, "this build cannot check translated code (build the check target)\n");
    exit(1);
}
#else

namespace {
    struct Access {
        uint16_t address;
        uint8_t value;
        bool operator==(const Access &) const = default;
    };
    std::vector<Access> g_sideWrites, g_realWrites;
    uint64_t g_checked = 0;
} // namespace

namespace z80check {
    bool side = false, noting = false;
    uint8_t Read(Z80 &c, uint16_t address) {
        for (auto it = g_sideWrites.rbegin(); it != g_sideWrites.rend(); ++it) {
            if (it->address == address && !c.bus.ChangesMapping(address)) {
                return it->value;
            }
        }
        return c.bus.Read(address);
    }
    void Write(Z80 &c, uint16_t address, uint8_t value) {
        g_sideWrites.push_back({address, value});
        if (c.bus.ChangesMapping(address)) {
            c.bus.Write(address, value);
        }
    }
    void Note(uint16_t address, uint8_t value) {
        g_realWrites.push_back({address, value});
    }
} // namespace z80check

uint64_t Z80BlocksChecked() {
    return g_checked;
}

void Z80::RunChecked(uint64_t until) {
    const Tables &tables = GetTables();
    struct Regs {
        uint8_t r[8], f, r2[8], f2;
        uint16_t ix, iy, sp, pc;
        uint8_t i, refresh, mode;
        bool iff1, iff2, halted, afterEI;
        uint64_t cycles;
        bool operator==(const Regs &) const = default;
    };
    auto save = [&]() {
        Regs s = {};
        memcpy(s.r, r, 8);
        memcpy(s.r2, r2, 8);
        s.f = f, s.f2 = f2, s.ix = ix, s.iy = iy, s.sp = sp, s.pc = pc, s.i = i, s.refresh = refresh, s.mode = mode;
        s.iff1 = iff1, s.iff2 = iff2, s.halted = halted, s.afterEI = m_afterEI, s.cycles = cycles;
        return s;
    };
    auto load = [&](const Regs &s) {
        memcpy(r, s.r, 8);
        memcpy(r2, s.r2, 8);
        f = s.f, f2 = s.f2, ix = s.ix, iy = s.iy, sp = s.sp, pc = s.pc, i = s.i, refresh = s.refresh, mode = s.mode;
        iff1 = s.iff1, iff2 = s.iff2, halted = s.halted, m_afterEI = s.afterEI, cycles = s.cycles;
    };
    for (;;) {
        if (cycles >= until) {
            return;
        }
        if (m_afterEI) {
            m_afterEI = false;
            m_cycles = 0;
            refresh++;
            Execute(FetchOpcode(), 0);
            cycles += m_cycles;
            continue;
        }
        if (const unsigned taken = TakeInterrupt()) {
            cycles += taken;
            continue;
        }
        if (halted) {
            cycles = until;
            return;
        }
        const Z80Fn fn = tables.fn[pc];
        const Regs before = save();
        Regs native = {};
        if (fn) {
            g_sideWrites.clear();
            depth = 0;
            const uint32_t mapping = bus.Mapping();
            z80check::side = true;
            fn(*this);
            z80check::side = false;
            bus.SetMapping(mapping);
            native = save();
            load(before);
        }
        g_realWrites.clear();
        z80check::noting = true;
        do {
            m_cycles = 0;
            refresh++;
            Execute(FetchOpcode(), 0);
            cycles += m_cycles;
        } while (!halted && tables.Inside(pc));
        z80check::noting = false;
        if (!fn) {
            continue;
        }
        g_checked++;
        const Regs after = save();
        if (!(native == after) || g_sideWrites != g_realWrites) {
            fprintf(stderr, "translated sound block at %04X differs from the interpreter (after %llu blocks checked)\n", before.pc,
                    static_cast<unsigned long long>(g_checked));
            auto show = [&](const char *name, const Regs &s) {
                fprintf(stderr, "  %-12s pc %04X cycles +%llu af %02X%02X bc %02X%02X de %02X%02X hl %02X%02X ix %04X iy %04X sp %04X r %02X%s%s\n",
                        name, s.pc, static_cast<unsigned long long>(s.cycles - before.cycles), s.r[7], s.f, s.r[0], s.r[1], s.r[2],
                        s.r[3], s.r[4], s.r[5], s.ix, s.iy, s.sp, s.refresh, s.iff1 ? " ei" : " di", s.halted ? " halted" : "");
            };
            show("before", before);
            show("interpreter", after);
            show("translated", native);
            auto list = [](const char *name, const std::vector<Access> &writes) {
                fprintf(stderr, "  %s writes:", name);
                for (const Access &w : writes) {
                    fprintf(stderr, " %04X=%02X", w.address, w.value);
                }
                fprintf(stderr, "\n");
            };
            list("interpreter", g_realWrites);
            list("translated", g_sideWrites);
            exit(3);
        }
    }
}
#endif

} // namespace mamerecomp::z80
