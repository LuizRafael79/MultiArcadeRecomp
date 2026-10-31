#include "naomi_sh4_context.hpp"

#include "naomi_sh4_helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace mamerecomp::naomi {
namespace {
struct UnresolvedSh4Target {};
unsigned g_naomi_bios_callback_trace_events = 0;
unsigned g_naomi_callback_table_dispatch_events = 0;
std::uint64_t g_naomi_task_callback_dispatches = 0;
std::uint64_t g_naomi_task_callback_returns = 0;
unsigned g_aica_mailbox_read_logs = 0;
unsigned g_aica_mailbox_write_logs = 0;
bool g_aica_mailbox_nonzero_read_logged = false;
unsigned g_naomi_bios_loop_trace_events = 0;
unsigned g_naomi_event_memory_trace_events = 0;
unsigned g_naomi_game_entry_trace_events = 0;
unsigned g_naomi_watch_write_trace_events = 0;
unsigned g_naomi_task_range_read_trace_events = 0;
std::uint64_t g_naomi_task_range_read_suppressed = 0;
unsigned g_naomi_bios_pointer_table_reads = 0;
unsigned g_naomi_m2_pio_trace_events = 0;
std::array<std::uint32_t, 8> g_naomi_watched_pcs{};
std::array<unsigned, 8> g_naomi_watched_pc_events{};
std::size_t g_naomi_watched_pc_count = 0;
bool g_naomi_reset_idle_trace_emitted = false;

struct AicaTraceStats {
    std::uint64_t sequence = 0;
    std::uint64_t register_reads = 0;
    std::uint64_t register_writes = 0;
    std::uint64_t sound_ram_reads = 0;
    std::uint64_t sound_ram_writes = 0;
    std::uint64_t suppressed_register_events = 0;
    std::uint64_t suppressed_ram_reads = 0;
    std::uint64_t suppressed_ram_writes = 0;
    std::uint64_t suppressed_unique_keys = 0;
    std::map<std::tuple<std::uint32_t, std::uint32_t, char, unsigned>,
             std::uint64_t> by_pc_address;
};

AicaTraceStats& aica_trace_stats() {
    static AicaTraceStats stats;
    return stats;
}

bool aica_trace_enabled() {
    static const bool enabled = std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") != nullptr;
    return enabled;
}

bool event_memory_trace_enabled() {
    static const bool enabled =
        std::getenv("MAMERECOMP_NAOMI_TRACE_EVENT_MEMORY") != nullptr;
    return enabled;
}

void trace_bios_pointer_table_read(const Sh4Context& context,
                                  std::uint32_t address, unsigned width,
                                  std::uint32_t value) {
    static const bool enabled =
        std::getenv("MAMERECOMP_NAOMI_TRACE_BIOS_POINTER_TABLE") != nullptr;
    if (!enabled || g_naomi_bios_pointer_table_reads >= 128) return;
    const auto physical = address & 0x1fffffffu;
    if (physical >= 0x0c018080u || physical + width <= 0x0c018040u) return;
    ++g_naomi_bios_pointer_table_reads;
    std::fprintf(stderr,
                 "SH-4 BIOS pointer-table read #%u pc=%08x guest=%08x physical=%08x width=%u value=%08x index=%u\n",
                 g_naomi_bios_pointer_table_reads, context.pc, address, physical,
                 width, value, (physical - 0x0c018040u) / 4u);
}

void trace_bios_pointer_table_read_summary() {
    if (!std::getenv("MAMERECOMP_NAOMI_TRACE_BIOS_POINTER_TABLE")) return;
    std::fprintf(stderr, "SH-4 BIOS pointer-table read summary count=%u range=0c018040-0c018080\n",
                 g_naomi_bios_pointer_table_reads);
}

void trace_m2_pio_access(const Sh4Context& context, char operation,
                         std::uint32_t address, unsigned width,
                         std::uint32_t value) {
    static const bool enabled =
        std::getenv("MAMERECOMP_NAOMI_TRACE_M2_PIO_PC") != nullptr;
    if (!enabled || g_naomi_m2_pio_trace_events >= 4096) return;
    const auto physical = address & 0x1fffffffu;
    if (physical < 0x005f7000u || physical >= 0x005f7016u) return;
    ++g_naomi_m2_pio_trace_events;
    std::fprintf(stderr,
                 "SH-4 M2 PIO #%u op=%c pc=%08x guest=%08x physical=%08x width=%u value=%08x\n",
                 g_naomi_m2_pio_trace_events, operation, context.pc, address,
                 physical, width, value);
}

void trace_aica_access(const Sh4Context& context, char operation,
                       std::uint32_t address, unsigned width,
                       std::uint32_t value) {
    if (!aica_trace_enabled()) return;
    const auto physical = address & 0x1fffffffu;
    const bool registers = physical >= 0x00700000u && physical < 0x00708000u;
    const bool sound_ram = physical >= 0x00800000u && physical < 0x01000000u;
    if (!registers && !sound_ram) return;

    if (physical == 0x0080005cu) {
        if (operation == 'W' && g_aica_mailbox_write_logs < 32) {
            ++g_aica_mailbox_write_logs;
            std::fprintf(stderr,
                         "AICA mailbox access %c pc=%08x guest=%08x physical=%08x width=%u value=%08x\n",
                         operation, context.pc, address, physical, width, value);
        } else if (operation == 'R' &&
                   (g_aica_mailbox_read_logs < 16 ||
                    (value != 0 && !g_aica_mailbox_nonzero_read_logged))) {
            ++g_aica_mailbox_read_logs;
            if (value != 0) g_aica_mailbox_nonzero_read_logged = true;
            std::fprintf(stderr,
                         "AICA mailbox access %c pc=%08x guest=%08x physical=%08x width=%u value=%08x\n",
                         operation, context.pc, address, physical, width, value);
        }
    }

    auto& stats = aica_trace_stats();
    const auto sequence = ++stats.sequence;
    const auto key = std::tuple{context.pc, physical, operation, width};
    if (const auto existing = stats.by_pc_address.find(key);
        existing != stats.by_pc_address.end()) {
        ++existing->second;
    } else if (stats.by_pc_address.size() < 65536) {
        stats.by_pc_address.emplace(key, 1);
    } else {
        ++stats.suppressed_unique_keys;
    }
    const auto pc_physical = context.pc & 0x1fffffffu;
    const char* phase = "other";
    if (pc_physical < 0x00200000u ||
        (pc_physical >= 0x0c000000u && pc_physical < 0x0c020000u))
        phase = "bios";
    else if (pc_physical >= 0x0c020000u && pc_physical < 0x0e000000u)
        phase = "game";

    if (registers) {
        auto& count = operation == 'R' ? stats.register_reads : stats.register_writes;
        ++count;
        if (count <= 512) {
            std::fprintf(stderr,
                         "AICA trace seq=%llu phase=%s %c pc=%08x guest=%08x physical=%08x width=%u value=%08x source=open-bus-or-unattached\n",
                         static_cast<unsigned long long>(sequence), phase, operation,
                         context.pc, address, physical, width, value);
        } else {
            ++stats.suppressed_register_events;
        }
        return;
    }

    auto& count = operation == 'R' ? stats.sound_ram_reads : stats.sound_ram_writes;
    ++count;
    const auto limit = operation == 'R' ? 256u : 512u;
    if (count <= limit) {
        std::fprintf(stderr,
                     "AICA trace seq=%llu phase=%s %c pc=%08x guest=%08x physical=%08x width=%u value=%08x source=sound-ram\n",
                     static_cast<unsigned long long>(sequence), phase, operation,
                     context.pc, address, physical, width, value);
    } else if (operation == 'R') {
        ++stats.suppressed_ram_reads;
    } else {
        ++stats.suppressed_ram_writes;
    }
}

void trace_aot_table_guest_write(const Sh4Context& context, std::uint32_t address,
                                 unsigned width, std::uint32_t value) {
    static const bool enabled =
        std::getenv("MAMERECOMP_NAOMI_TRACE_AOT_TABLE_WRITES") != nullptr;
    if (!enabled) return;
    const auto physical = address & 0x1fffffffu;
    const bool braf_table = physical < 0x0c035fb0u && physical + width > 0x0c035f84u;
    const bool callback_table = physical < 0x0c05f03cu && physical + width > 0x0c05ec3cu;
    if (!braf_table && !callback_table) return;

    static unsigned logged_nonzero = 0;
    static unsigned logged_zero = 0;
    if (value == 0 && logged_zero >= 8) return;
    if (value != 0 && logged_nonzero >= 256) return;
    if (value == 0) ++logged_zero;
    else ++logged_nonzero;
    std::fprintf(stderr,
                 "SH-4 AOT guest table write kind=%s pc=%08x address=%08x physical=%08x value=%08x width=%u regs={r0:%08x,r1:%08x,r2:%08x,r3:%08x}\n",
                 braf_table ? "braf" : "callback", context.pc, address, physical,
                 value, width, context.r[0], context.r[1], context.r[2], context.r[3]);
}

void trace_aica_summary() {
    if (!aica_trace_enabled()) return;
    auto& stats = aica_trace_stats();
    std::fprintf(stderr,
                 "AICA trace summary seq=%llu regs={R:%llu,W:%llu,suppressed:%llu} sound_ram={R:%llu,W:%llu,suppressed_R:%llu,suppressed_W:%llu} suppressed_unique_keys=%llu repeated_readers:\n",
                 static_cast<unsigned long long>(stats.sequence),
                 static_cast<unsigned long long>(stats.register_reads),
                 static_cast<unsigned long long>(stats.register_writes),
                 static_cast<unsigned long long>(stats.suppressed_register_events),
                 static_cast<unsigned long long>(stats.sound_ram_reads),
                 static_cast<unsigned long long>(stats.sound_ram_writes),
                 static_cast<unsigned long long>(stats.suppressed_ram_reads),
                 static_cast<unsigned long long>(stats.suppressed_ram_writes),
                 static_cast<unsigned long long>(stats.suppressed_unique_keys));
    std::vector<std::pair<std::tuple<std::uint32_t, std::uint32_t, char, unsigned>,
                          std::uint64_t>> repeated;
    for (const auto& item : stats.by_pc_address)
        if (std::get<2>(item.first) == 'R' && item.second > 1)
            repeated.push_back(item);
    std::sort(repeated.begin(), repeated.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    for (std::size_t i = 0; i < repeated.size() && i < 32; ++i) {
        const auto& [pc, address, operation, width] = repeated[i].first;
        std::fprintf(stderr, "AICA trace repeated read count=%llu pc=%08x physical=%08x width=%u\n",
                     static_cast<unsigned long long>(repeated[i].second), pc,
                     address, width);
    }
}

bool is_bios_event_memory(std::uint32_t address) {
    const auto physical = address & 0x1fffffffu;
    return physical >= 0x0c000370u && physical < 0x0c000380u;
}

constexpr std::uint32_t kSh4IntevtAddress = 0x1f000028u;
unsigned g_naomi_intevt_trace_events = 0;

bool read_intevt(const Sh4Context& context, std::uint32_t address,
                 unsigned width, std::uint32_t& value) {
    const auto physical = address & 0x1fffffffu;
    if (physical < kSh4IntevtAddress || physical + width > kSh4IntevtAddress + 4u)
        return false;
    const auto shift = (physical - kSh4IntevtAddress) * 8u;
    value = context.intevt >> shift;
    if (width < 4) value &= (1u << (width * 8u)) - 1u;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_INTEVT") &&
        g_naomi_intevt_trace_events++ < 64)
        std::fprintf(stderr,
                     "SH-4 INTEVT R pc=%08x address=%08x width=%u -> %08x\n",
                     context.pc, address, width, value);
    return true;
}

bool write_intevt(const Sh4Context& context, std::uint32_t address,
                  unsigned width, std::uint32_t value) {
    const auto physical = address & 0x1fffffffu;
    if (physical < kSh4IntevtAddress || physical + width > kSh4IntevtAddress + 4u)
        return false;
    const auto shift = (physical - kSh4IntevtAddress) * 8u;
    const auto mask = width == 4 ? 0xffffffffu :
        ((1u << (width * 8u)) - 1u) << shift;
    auto& intevt = const_cast<Sh4Context&>(context).intevt;
    intevt = (intevt & ~mask) | ((value << shift) & mask);
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_INTEVT") &&
        g_naomi_intevt_trace_events++ < 64)
        std::fprintf(stderr,
                     "SH-4 INTEVT W pc=%08x address=%08x width=%u <- %08x\n",
                     context.pc, address, width, value);
    return true;
}

void trace_pvr_framebuffer_write(const Sh4Context& context,
                                std::uint32_t address,
                                unsigned width, std::uint32_t value) {
    static unsigned events = 0;
    if (!std::getenv("MAMERECOMP_NAOMI_TRACE_PVR_FB_WRITES") || events >= 128)
        return;
    const auto physical = address & 0x1fffffffu;
    const auto aligned = physical & ~3u;
    switch (aligned) {
    case 0x005f8040u: // FB_W_CTRL
    case 0x005f8044u: // FB_R_CTRL
    case 0x005f804cu: // FB_W_LINESTRIDE
    case 0x005f8050u: // FB_R_SOF1
    case 0x005f8054u: // FB_R_SOF2
    case 0x005f805cu: // FB_R_SIZE
    case 0x005f8060u: // FB_W_SOF1
    case 0x005f8064u: // FB_W_SOF2
        ++events;
        std::fprintf(stderr,
                     "SH-4 PVR framebuffer write #%u pc=%08x address=%08x physical=%08x width=%u value=%08x\n",
                     events, context.pc, address, physical, width, value);
        break;
    default:
        break;
    }
}

void trace_watched_write(const Sh4Context& context, std::uint32_t address,
                         unsigned width, std::uint32_t value) {
    // Opt-in bounded trace for the MvC2 task table. This is diagnostic only:
    // it observes guest writes and never changes the memory or scheduler state.
    static bool range_initialized = false;
    static bool range_enabled = false;
    static std::uint32_t range_begin = 0;
    static std::uint32_t range_end = 0;
    static unsigned range_events = 0;
    if (!range_initialized) {
        range_initialized = true;
        if (const char* text = std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_RANGE")) {
            char* end = nullptr;
            const auto begin = std::strtoul(text, &end, 0);
            if (end != text && *end == ':') {
                char* range_end_text = nullptr;
                const auto finish = std::strtoul(end + 1, &range_end_text, 0);
                if (range_end_text != end + 1 && *range_end_text == '\0' &&
                    begin < finish && finish <= 0x20000000ul) {
                    range_begin = static_cast<std::uint32_t>(begin) & 0x1fffffffu;
                    range_end = static_cast<std::uint32_t>(finish) & 0x1fffffffu;
                    range_enabled = true;
                }
            }
        }
    }
    const auto range_physical = address & 0x1fffffffu;
    // Ignore byte-wise zero fills so they cannot consume the bounded trace
    // before the scheduler installs actual callback/state values.
    const bool zero_fill_byte = width == 1 && value == 0;
    if (range_enabled && !zero_fill_byte && range_events < 512 &&
        range_physical < range_end && range_physical + width > range_begin) {
        ++range_events;
        std::fprintf(stderr,
                     "SH-4 task-range write #%u pc=%08x address=%08x physical=%08x width=%u value=%08x regs={r0:%08x,r1:%08x,r2:%08x,r3:%08x,r4:%08x,r5:%08x} pr=%08x sr=%08x T=%u\n",
                     range_events, context.pc, address, range_physical, width, value,
                     context.r[0], context.r[1], context.r[2], context.r[3],
                     context.r[4], context.r[5], context.pr, context.sr,
                     context.t ? 1u : 0u);
    }

    static bool initialized = false;
    static bool enabled = false;
    static std::uint32_t watched_address = 0;
    if (!initialized) {
        initialized = true;
        if (const char* text = std::getenv("MAMERECOMP_NAOMI_WATCH_ADDRESS")) {
            char* end = nullptr;
            const auto parsed = std::strtoul(text, &end, 0);
            if (end != text && *end == '\0') {
                watched_address = static_cast<std::uint32_t>(parsed) & 0x1fffffffu;
                enabled = true;
            }
        }
    }
    if (!enabled || g_naomi_watch_write_trace_events >= 128) return;
    const auto physical = address & 0x1fffffffu;
    if (watched_address < physical || watched_address >= physical + width) return;
    ++g_naomi_watch_write_trace_events;
    std::fprintf(stderr,
                 "SH-4 watched RAM write pc=%08x address=%08x width=%u value=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x pr=%08x sr=%08x T=%u\n",
                 context.pc, address, width, value, context.r[0], context.r[1],
                 context.r[2], context.r[3], context.r[4], context.r[5],
                 context.r[6], context.pr, context.sr, context.t ? 1u : 0u);
}

void trace_task_range_read(const Sh4Context& context, std::uint32_t address,
                           unsigned width, std::uint32_t value) {
    // Opt-in bounded counterpart to MAMERECOMP_NAOMI_TRACE_TASK_RANGE writes.
    // This observes scheduler reads only; it never changes guest memory/state.
    static bool initialized = false;
    static bool enabled = false;
    static bool changes_only = false;
    static std::uint32_t range_begin = 0;
    static std::uint32_t range_end = 0;
    static std::unordered_map<std::uint64_t, std::uint32_t> last_values;
    if (!initialized) {
        initialized = true;
        changes_only = std::getenv(
            "MAMERECOMP_NAOMI_TRACE_TASK_RANGE_READ_CHANGES") != nullptr;
        if (const char* text = std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_RANGE_READ")) {
            char* end = nullptr;
            const auto begin = std::strtoul(text, &end, 0);
            if (end != text && *end == ':') {
                char* range_end_text = nullptr;
                const auto finish = std::strtoul(end + 1, &range_end_text, 0);
                if (range_end_text != end + 1 && *range_end_text == '\0' &&
                    begin < finish && finish <= 0x20000000ul) {
                    range_begin = static_cast<std::uint32_t>(begin) & 0x1fffffffu;
                    range_end = static_cast<std::uint32_t>(finish) & 0x1fffffffu;
                    enabled = true;
                }
            }
        }
    }
    if (!enabled) return;
    const auto physical = address & 0x1fffffffu;
    if (physical >= range_end || physical + width <= range_begin) return;
    if (changes_only) {
        const auto key = (static_cast<std::uint64_t>(physical) << 8) | width;
        const auto [it, inserted] = last_values.emplace(key, value);
        if (!inserted && it->second == value) return;
        if (!inserted) it->second = value;
    }
    if (g_naomi_task_range_read_trace_events >= 512) {
        ++g_naomi_task_range_read_suppressed;
        return;
    }
    ++g_naomi_task_range_read_trace_events;
    std::fprintf(stderr,
                 "SH-4 task-range read #%u pc=%08x address=%08x physical=%08x width=%u value=%08x regs={r0:%08x,r1:%08x,r2:%08x,r3:%08x,r4:%08x,r5:%08x} pr=%08x sr=%08x T=%u\n",
                 g_naomi_task_range_read_trace_events, context.pc, address,
                 physical, width, value, context.r[0], context.r[1], context.r[2],
                 context.r[3], context.r[4], context.r[5], context.pr, context.sr,
                 context.t ? 1u : 0u);
}

void trace_task_range_read_summary() {
    if (g_naomi_task_range_read_trace_events != 0 ||
        g_naomi_task_range_read_suppressed != 0)
        std::fprintf(stderr,
                 "SH-4 task-range read summary logged=%u suppressed=%llu changes_only=%u\n",
                     g_naomi_task_range_read_trace_events,
                 static_cast<unsigned long long>(g_naomi_task_range_read_suppressed),
                 std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_RANGE_READ_CHANGES") ? 1u : 0u);
}

void trace_watched_pc(const Sh4Context& context) {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        if (const char* text = std::getenv("MAMERECOMP_NAOMI_TRACE_PC")) {
            const char* cursor = text;
            while (*cursor && g_naomi_watched_pc_count < g_naomi_watched_pcs.size()) {
                char* end = nullptr;
                const auto parsed = std::strtoul(cursor, &end, 0);
                if (end == cursor) break;
                g_naomi_watched_pcs[g_naomi_watched_pc_count++] =
                    static_cast<std::uint32_t>(parsed);
                if (*end != ',') break;
                cursor = end + 1;
            }
        }
    }
    std::size_t watched_index = 0;
    while (watched_index < g_naomi_watched_pc_count &&
           g_naomi_watched_pcs[watched_index] != context.pc)
        ++watched_index;
    if (watched_index == g_naomi_watched_pc_count ||
        g_naomi_watched_pc_events[watched_index] >= 64) return;
    ++g_naomi_watched_pc_events[watched_index];
    std::fprintf(stderr,
                 "SH-4 watched PC=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                 context.pc, context.r[0], context.r[1], context.r[2], context.r[3],
                 context.r[4], context.r[5], context.r[6], context.r[7], context.pr,
                 context.sr, context.t ? 1u : 0u, context.r[15]);
    std::fprintf(stderr,
                 "SH-4 watched regs r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x\n",
                 context.r[8], context.r[9], context.r[10], context.r[11],
                 context.r[12], context.r[13], context.r[14]);
}
}

std::uint8_t Sh4Context::read8(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (read_intevt(*this, address, 1, value)) return static_cast<std::uint8_t>(value);
    value = bus && bus->read8 ? bus->read8(address) : 0xff;
    trace_task_range_read(*this, address, 1, value);
    trace_bios_pointer_table_read(*this, address, 1, value);
    trace_m2_pio_access(*this, 'R', address, 1, value);
    trace_aica_access(*this, 'R', address, 1, value);
    return static_cast<std::uint8_t>(value);
}

std::uint16_t Sh4Context::read16(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (read_intevt(*this, address, 2, value)) return static_cast<std::uint16_t>(value);
    value = bus && bus->read16
        ? bus->read16(address)
        : static_cast<std::uint16_t>(read8(address)) |
              (static_cast<std::uint16_t>(read8(address + 1)) << 8);
    trace_task_range_read(*this, address, 2, value);
    trace_bios_pointer_table_read(*this, address, 2, value);
    trace_m2_pio_access(*this, 'R', address, 2, value);
    trace_aica_access(*this, 'R', address, 2, value);
    return static_cast<std::uint16_t>(value);
}

std::uint32_t Sh4Context::read32(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (!read_intevt(*this, address, 4, value)) {
        value = bus && bus->read32
            ? bus->read32(address)
            : static_cast<std::uint32_t>(read16(address)) |
                  (static_cast<std::uint32_t>(read16(address + 2)) << 16);
    }
    trace_task_range_read(*this, address, 4, value);
    trace_bios_pointer_table_read(*this, address, 4, value);
    if (event_memory_trace_enabled() &&
        is_bios_event_memory(address) && g_naomi_event_memory_trace_events < 128) {
        ++g_naomi_event_memory_trace_events;
        std::fprintf(stderr, "SH-4 BIOS event RAM R pc=%08x address=%08x value=%08x\n",
                     pc, address, value);
    }
    trace_m2_pio_access(*this, 'R', address, 4, value);
    trace_aica_access(*this, 'R', address, 4, value);
    return value;
}

void Sh4Context::write8(std::uint32_t address, std::uint8_t value) const {
    trace_pvr_framebuffer_write(*this, address, 1, value);
    trace_aot_table_guest_write(*this, address, 1, value);
    trace_watched_write(*this, address, 1, value);
    trace_m2_pio_access(*this, 'W', address, 1, value);
    trace_aica_access(*this, 'W', address, 1, value);
    if (write_intevt(*this, address, 1, value)) return;
    if ((address & 0xfc000000u) == 0xe0000000u) {
        const auto offset = static_cast<std::size_t>(address & 0x3fu);
        const_cast<Sh4Context*>(this)->store_queue[offset] = value;
        return;
    }
    if (bus && bus->write8) bus->write8(address, value, pc);
}

void Sh4Context::write16(std::uint32_t address, std::uint16_t value) const {
    trace_pvr_framebuffer_write(*this, address, 2, value);
    trace_aot_table_guest_write(*this, address, 2, value);
    trace_watched_write(*this, address, 2, value);
    trace_m2_pio_access(*this, 'W', address, 2, value);
    trace_aica_access(*this, 'W', address, 2, value);
    if (write_intevt(*this, address, 2, value)) return;
    if ((address & 0xfc000000u) == 0xe0000000u) {
        write8(address, static_cast<std::uint8_t>(value));
        write8(address + 1, static_cast<std::uint8_t>(value >> 8));
        return;
    }
    if (bus && bus->write16) {
        bus->write16(address, value, pc);
        return;
    }
    write8(address, static_cast<std::uint8_t>(value));
    write8(address + 1, static_cast<std::uint8_t>(value >> 8));
}

void Sh4Context::write32(std::uint32_t address, std::uint32_t value) const {
    trace_pvr_framebuffer_write(*this, address, 4, value);
    trace_aot_table_guest_write(*this, address, 4, value);
    trace_watched_write(*this, address, 4, value);
    trace_m2_pio_access(*this, 'W', address, 4, value);
    trace_aica_access(*this, 'W', address, 4, value);
    if (write_intevt(*this, address, 4, value)) return;
    if (event_memory_trace_enabled() &&
        is_bios_event_memory(address) && g_naomi_event_memory_trace_events < 128) {
        ++g_naomi_event_memory_trace_events;
        std::fprintf(stderr, "SH-4 BIOS event RAM W pc=%08x address=%08x value=%08x\n",
                     pc, address, value);
    }
    if ((address & 0xfc000000u) == 0xe0000000u) {
        write8(address, static_cast<std::uint8_t>(value));
        write8(address + 1, static_cast<std::uint8_t>(value >> 8));
        write8(address + 2, static_cast<std::uint8_t>(value >> 16));
        write8(address + 3, static_cast<std::uint8_t>(value >> 24));
        return;
    }
    if (bus && bus->write32) {
        bus->write32(address, value, pc);
        return;
    }
    write16(address, static_cast<std::uint16_t>(value));
    write16(address + 2, static_cast<std::uint16_t>(value >> 16));
}

void Sh4Context::dispatch_indirect(std::uint32_t target) {
    const auto source_pc = pc;
    static const bool trace_callback_table =
        std::getenv("MAMERECOMP_NAOMI_TRACE_CALLBACK_TABLE") != nullptr;
    if (trace_callback_table && source_pc == 0x0c0353fcu &&
        g_naomi_callback_table_dispatch_events < 256) {
        ++g_naomi_callback_table_dispatch_events;
        std::fprintf(stderr,
                     "SH-4 callback-table dispatch call_pc=%08x delay_pc=%08x index=%u r10=%08x table_base=%08x r3=%08x target=%08x pr=%08x\n",
                     source_pc - 2u, source_pc, r[10] >> 2, r[10], r[0],
                     r[3], target, pr);
    }
    static const bool trace_task_callbacks =
        std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS") != nullptr;
    if (trace_task_callbacks && source_pc == 0x0c045fdeu) {
        const auto dispatch = ++g_naomi_task_callback_dispatches;
        if (dispatch <= 16 || (dispatch % 1000000u) == 0) {
            const bool record_in_ram = r[3] >= 0x0c000000u && r[3] < 0x0d000000u;
            const auto record_word2 = record_in_ram ? read32(r[3] + 8u) : 0xffffffffu;
            const auto record_word3 = record_in_ram ? read32(r[3] + 12u) : 0xffffffffu;
            std::fprintf(stderr,
                         "SH-4 task callback #%llu call_pc=%08x record=%08x target=%08x arg=%08x words={%08x,%08x} r13=%08x r14=%08x pr=%08x sp=%08x\n",
                         static_cast<unsigned long long>(dispatch), source_pc - 2u,
                         r[3], target, r[4], record_word2, record_word3,
                         r[13], r[14], pr, r[15]);
        }
    }
    const bool trace_bios_callback =
        std::getenv("MAMERECOMP_NAOMI_TRACE_CALLBACKS") &&
        ((source_pc >= 0xac00119eu && source_pc <= 0xac001228u) ||
         target == 0xac0014b0u || target == 0xac00181cu || target == 0xac0023e2u) &&
        g_naomi_bios_callback_trace_events < 96;
    if (trace_bios_callback) {
        ++g_naomi_bios_callback_trace_events;
        std::fprintf(stderr,
                     "SH-4 BIOS callback enter source=%08x target=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                     source_pc, target, r[0], r[1], r[2], r[3], r[4],
                     r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12],
                     r[13], r[14], pr, sr, t ? 1u : 0u, r[15]);
    }
    // Trace-only probe for the BIOS M2 wrapper called from 0xac0195e8.
    // Match all SH-4 P0/P1/P2 aliases; this does not affect dispatch.
    static unsigned m2_handoff_trace_events = 0;
    const bool trace_m2_handoff =
        std::getenv("MAMERECOMP_NAOMI_TRACE_M2_HANDOFF") &&
        (target & 0x1fffffffu) == 0x0c019550u &&
        m2_handoff_trace_events < 16;
    if (trace_m2_handoff) {
        ++m2_handoff_trace_events;
        std::fprintf(stderr,
                     "SH-4 M2 BIOS handoff #%u source=%08x target=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                     m2_handoff_trace_events, source_pc, target,
                     r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                     r[8], r[9], r[10], r[11], r[12], r[13], r[14],
                     pr, sr, t ? 1u : 0u, r[15]);
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_DISPATCH")) {
        std::fprintf(stderr,
                     "SH-4 dispatch pc=0x%08x target=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x pr=0x%08x sp=0x%08x\n",
                     source_pc, target, r[0], r[1], r[2], r[3], pr, r[15]);
    }
    last_indirect_target = target;
    recent_indirect_targets[indirect_calls % recent_indirect_targets.size()] = target;
    ++indirect_calls;
    pc = target;
    if (indirect_call) {
        try {
            indirect_call(*this, target);
            if (trace_bios_callback)
                std::fprintf(stderr,
                             "SH-4 BIOS callback leave source=%08x target=%08x pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                             source_pc, target, pc, r[0], r[1], r[2], r[3], r[4],
                             r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12],
                             r[13], r[14], pr, sr, t ? 1u : 0u, r[15]);
        } catch (const UnresolvedSh4Target&) {
            if (unresolved_target == target)
                std::fprintf(stderr,
                             "SH-4 unresolved indirect target=0x%08x from delay-slot PC=0x%08x\n",
                             target, source_pc);
            throw;
        }
    } else {
        unresolved_target = target;
        execution_stopped = true;
    }
}

void Sh4Context::setIrlnInput(unsigned encoded_level) {
    irln = encoded_level & 0x0fu;
}

bool Sh4Context::acceptInterrupt() {
    if (irln != 9 && irln != 11 && irln != 13) return false;
    const unsigned level = 15u - irln;
    const unsigned mask = (sr >> 4) & 0x0fu;
    constexpr std::uint32_t kSrBl = 1u << 28;
    if ((sr & kSrBl) != 0 || level <= mask) return false;

    if (std::getenv("MAMERECOMP_NAOMI_TRACE_IRQ")) {
        static unsigned trace_events = 0;
        if (trace_events++ < 32)
            std::fprintf(stderr,
                         "SH-4 IRQ accept pc=%08x level=%u sr=%08x irln=%u vbr=%08x\n",
                         pc, level, sr, irln, vbr);
    }

    spc = pc;
    ssr = sr;
    sgr = r[15];
    intevt = level == 6 ? 0x320u : level == 4 ? 0x360u : 0x3a0u;
    load_sr(*this, sr | (1u << 30) | (1u << 29) | kSrBl);
    pc = vbr + 0x600u;
    ++interrupts_accepted;
    interrupt_taken = true;
    return true;
}

bool Sh4Context::consume_instruction(unsigned cycles) {
    // These switches sit on the per-instruction hot path. Cache them once so
    // disabled debug traces do not call getenv() for every SH-4 instruction.
    static const bool trace_reset_idle =
        std::getenv("MAMERECOMP_NAOMI_TRACE_RESET_IDLE") != nullptr;
    static const bool trace_game_entry =
        std::getenv("MAMERECOMP_NAOMI_TRACE_GAME_ENTRY") != nullptr;
    static const bool trace_boot_loop =
        std::getenv("MAMERECOMP_NAOMI_TRACE_BOOT_LOOP") != nullptr;
    static const bool trace_progress =
        std::getenv("MAMERECOMP_NAOMI_TRACE_PROGRESS") != nullptr;
    static const char* trace_manifest_sha256 =
        std::getenv("MAMERECOMP_NAOMI_MANIFEST_SHA256");

    if (trace_progress && instructions_executed != 0 &&
        instructions_executed % 10000000u == 0) {
        std::fprintf(stderr,
                     "SH-4 progress executed=%llu pc=%08x pr=%08x sp=%08x budget=%llu unresolved=%08x manifest_sha256=%s\n",
                     static_cast<unsigned long long>(instructions_executed), pc, pr,
                     r[15], static_cast<unsigned long long>(instruction_budget),
                     unresolved_target,
                     trace_manifest_sha256 ? trace_manifest_sha256 : "not-provided");
    }

    trace_watched_pc(*this);
    if (trace_reset_idle &&
        pc == 0x0c020090u && !g_naomi_reset_idle_trace_emitted) {
        g_naomi_reset_idle_trace_emitted = true;
        std::fprintf(stderr,
                     "SH-4 reset idle entry pc=%08x pr=%08x sr=%08x t=%u budget=%llu executed=%llu indirect=%llu last=%08x\\n",
                     pc, pr, sr, t ? 1u : 0u,
                     static_cast<unsigned long long>(instruction_budget),
                     static_cast<unsigned long long>(instructions_executed),
                     static_cast<unsigned long long>(indirect_calls), last_indirect_target);
        for (unsigned base = 0; base < 16; base += 4) {
            std::fprintf(stderr, "SH-4 reset idle regs r%u=%08x r%u=%08x r%u=%08x r%u=%08x\\n",
                         base, r[base], base + 1, r[base + 1],
                         base + 2, r[base + 2], base + 3, r[base + 3]);
        }
        for (unsigned offset = 0; offset < 32; offset += 4) {
            std::fprintf(stderr, "SH-4 reset idle stack [%08x]=%08x\\n",
                         r[15] + offset, read32(r[15] + offset));
        }
    }
    if (trace_game_entry &&
        pc >= 0x0c03ef60u && pc < 0x0c03f000u &&
        g_naomi_game_entry_trace_events < 512) {
        ++g_naomi_game_entry_trace_events;
        std::fprintf(stderr,
                     "SH-4 game entry pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x pr=%08x sp=%08x\n",
                     pc, r[0], r[1], r[2], r[3], r[4], r[5], pr, r[15]);
    }
    if (trace_boot_loop &&
        (pc == 0xac0011c8u || pc == 0xac0011d4u || pc == 0xac0011e0u ||
         pc == 0xac001216u || pc == 0xac00122au || pc == 0xac00122eu) &&
        g_naomi_bios_loop_trace_events < 120) {
        ++g_naomi_bios_loop_trace_events;
        std::fprintf(stderr,
                     "SH-4 BIOS loop pc=%08x r0=%08x r2=%08x r3=%08x r4=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x sp=%08x T=%u\n",
                     pc, r[0], r[2], r[3], r[4], r[8], r[9], r[10], r[11],
                     r[12], r[13], r[15], t ? 1u : 0u);
    }
    if (!delay_slot && acceptInterrupt()) return false;
    if (instruction_budget == 0) {
        execution_stopped = true;
        return false;
    }
    --instruction_budget;
    ++instructions_executed;
    if (cycle_sink) cycle_sink(cycles);
    return true;
}

void Sh4Dispatcher::add(std::uint32_t guest_address, Sh4TranslatedFunction function) {
    if (!function) return;
    if (guest_address == 0x0c04b88cu &&
        std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS")) {
        const auto old = m_functions.find(guest_address);
        std::fprintf(stderr,
                     "SH-4 task callback registration pc=%08x old=%p new=%p caller=%p replace=%u\n",
                     guest_address,
                     old == m_functions.end() ? nullptr : reinterpret_cast<const void*>(old->second),
                     reinterpret_cast<const void*>(function),
                     __builtin_return_address(0),
                     old == m_functions.end() ? 0u : 1u);
    }
    m_functions[guest_address] = function;
}

bool Sh4Dispatcher::contains(std::uint32_t guest_address) const {
    return m_functions.contains(guest_address);
}

void Sh4Dispatcher::call(Sh4Context& context, std::uint32_t target) const {
    auto found = m_functions.find(target);
    if (found == m_functions.end()) {
        // P0, P1, and P2 may name the same physical instruction bytes. Probe
        // the canonical physical and cache-alias entries after the exact PC.
        const auto physical = target & 0x1fffffffu;
        found = m_functions.find(physical);
        if (found == m_functions.end())
            found = m_functions.find(0x80000000u | physical);
        if (found == m_functions.end())
            found = m_functions.find(0xa0000000u | physical);
    }
    if (found == m_functions.end()) {
        context.pc = target;
        context.unresolved_target = target;
        context.execution_stopped = true;
        // Static translated functions call one another on the host stack. A
        // missing guest target must unwind that whole stack immediately; if
        // we merely returned here, the caller would continue past the guest
        // JSR/JMP and could make a broken boot look like an idle loop.
        throw UnresolvedSh4Target{};
    }
    context.pc = target;
    const auto task_callback_instruction_start = context.instructions_executed;
    const auto task_callback_function = found->second;
    found->second(context);
    if (target == 0x0c04b88cu &&
        std::getenv("MAMERECOMP_NAOMI_TRACE_TASK_CALLBACKS")) {
        const auto dispatch = ++g_naomi_task_callback_returns;
        if (dispatch <= 16 || (dispatch % 1000000u) == 0) {
            std::fprintf(stderr,
                         "SH-4 task callback return #%llu entry=%08x impl=%p guest_instructions=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r10=%08x r13=%08x r14=%08x pr=%08x sp=%08x T=%u stopped=%u\n",
                         static_cast<unsigned long long>(dispatch), target,
                         reinterpret_cast<const void*>(task_callback_function),
                         static_cast<unsigned long long>(context.instructions_executed - task_callback_instruction_start),
                         context.pc, context.r[0], context.r[1], context.r[2],
                         context.r[3], context.r[4], context.r[10],
                         context.r[13], context.r[14], context.pr, context.r[15],
                         context.t ? 1u : 0u, context.execution_stopped ? 1u : 0u);
        }
    }
}

Sh4RunResult Sh4Dispatcher::run(Sh4Context& context, std::uint32_t entry,
                                std::uint64_t instruction_budget) const {
    context.instruction_budget = instruction_budget;
    context.instructions_executed = 0;
    context.unresolved_target = 0;
    context.last_indirect_target = 0;
    context.indirect_calls = 0;
    context.recent_indirect_targets.fill(0);
    context.execution_stopped = false;
    context.interrupt_taken = false;
    context.indirect_call = [this](Sh4Context& ctx, std::uint32_t target) {
        call(ctx, target);
    };

    auto has_entry = [this](std::uint32_t target) {
        if (m_functions.contains(target)) return true;
        const auto physical = target & 0x1fffffffu;
        return m_functions.contains(physical) ||
               m_functions.contains(0x80000000u | physical) ||
               m_functions.contains(0xa0000000u | physical);
    };

    // Most guest calls return through the active translated C++ call chain.
    // A top-level tail jump/RTS has no translated caller, however, so resume
    // from the guest PC after that chain unwinds (notably BIOS P2 -> P0 paths).
    auto next_entry = entry;
    while (!context.execution_stopped && context.instruction_budget != 0) {
        const auto dispatched_entry = next_entry;
        context.interrupt_taken = false;
        const auto instructions_before_dispatch = context.instructions_executed;
        try {
            call(context, dispatched_entry);
        } catch (const UnresolvedSh4Target&) {
            break;
        }
        // A translated function that returns without executing a guest
        // instruction is an invalid AOT dispatch (usually a missing leader).
        // Surface it instead of redispatching the same alias indefinitely.
        if (!context.execution_stopped && !context.interrupt_taken &&
            context.instructions_executed == instructions_before_dispatch) {
            context.unresolved_target = context.pc;
            context.execution_stopped = true;
            std::fprintf(stderr,
                         "SH-4 dispatcher made no guest progress: entry=%08x pc=%08x pr=%08x sp=%08x aliases=[%08x,%08x,%08x]\n",
                         dispatched_entry, context.pc, context.pr, context.r[15],
                         context.pc & 0x1fffffffu,
                         0x80000000u | (context.pc & 0x1fffffffu),
                         0xa0000000u | (context.pc & 0x1fffffffu));
            break;
        }
        if (context.execution_stopped || context.instruction_budget == 0) break;
        if (context.interrupt_taken) {
            next_entry = context.pc;
            continue;
        }
        if (!has_entry(context.pc)) {
            // RTE resumes at the exact interrupted guest PC, which may be an
            // interior address of a statically translated function. Surface
            // this as an untranslated target instead of silently ending the
            // probe with an apparently normal dispatcher return.
            context.unresolved_target = context.pc;
            context.execution_stopped = true;
            break;
        }
        next_entry = context.pc;
    }

    trace_task_range_read_summary();
    trace_bios_pointer_table_read_summary();
    trace_aica_summary();
    return {context.instructions_executed, context.unresolved_target,
            context.execution_stopped && context.instruction_budget == 0 &&
                context.unresolved_target == 0};
}

} // namespace mamerecomp::naomi
