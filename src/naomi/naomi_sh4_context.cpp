#include "naomi_sh4_context.hpp"

#include "naomi_sh4_helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace multiarcaderecomp::naomi {
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

constexpr std::uint32_t kSh4Mmucr = 0xff000010u;
constexpr std::uint32_t kSh4Pteh = 0xff000000u;
constexpr std::uint32_t kSh4Tea = 0xff00000cu;
constexpr std::uint32_t kSh4TlbMissRead = 0x040u;
constexpr std::uint32_t kSh4TlbMissWrite = 0x060u;
constexpr std::uint32_t kSh4TlbFirstWrite = 0x080u;
constexpr std::uint32_t kSh4TlbProtectionRead = 0x0a0u;
constexpr std::uint32_t kSh4TlbProtectionWrite = 0x0c0u;
constexpr std::uint32_t kSh4AddressErrorRead = 0x0e0u;
constexpr std::uint32_t kSh4AddressErrorWrite = 0x100u;
constexpr std::uint32_t kSh4TlbMultiHit = 0x140u;
constexpr std::uint32_t kItlbLruOr[4] = {0x00u, 0x20u, 0x14u, 0x0bu};
constexpr std::uint32_t kItlbLruAnd[4] = {0x07u, 0x39u, 0x3eu, 0x3fu};

std::uint32_t sh4_itlb_replacement_slot(std::uint32_t lrui) {
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
        const auto key = (~kItlbLruAnd[slot]) & 0x3fu;
        const auto mask = key | kItlbLruOr[slot];
        if ((lrui & mask) == key) return slot;
    }
    return 0;
}

void sh4_itlb_touch(const Sh4Context& context, std::uint32_t mmucr,
                    std::uint32_t slot) {
    auto updated = mmucr;
    auto lrui = (mmucr >> 26) & 0x3fu;
    lrui = (lrui & kItlbLruAnd[slot]) | kItlbLruOr[slot];
    updated = (updated & ~(0x3fu << 26)) | (lrui << 26);
    if (context.bus && context.bus->write32)
        context.bus->write32(kSh4Mmucr, updated, context.pc);
}

enum class Sh4Access { Read, Write, Instruction };

std::uint32_t sh4_mmr_read(const Sh4Context& context, std::uint32_t address) {
    return context.bus && context.bus->read32 ? context.bus->read32(address) : 0;
}

[[noreturn]] void sh4_memory_exception(const Sh4Context& source,
                                       std::uint32_t address,
                                       Sh4Access access,
                                       std::uint32_t event) {
    auto& context = const_cast<Sh4Context&>(source);
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_SH4_EXCEPTIONS")) {
        static unsigned memory_fault_trace_events = 0;
        if (memory_fault_trace_events < 128) {
            ++memory_fault_trace_events;
            std::fprintf(stderr,
                         "SH-4 memory fault access=%s address=%08x event=%03x pc=%08x delay=%u r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r14=%08x sp=%08x\n",
                         access == Sh4Access::Write ? "write" :
                             access == Sh4Access::Read ? "read" : "fetch",
                         address, event, context.pc, context.delay_slot ? 1u : 0u,
                         context.r[0], context.r[1], context.r[2], context.r[3],
                         context.r[4], context.r[5], context.r[6], context.r[7],
                         context.r[14], context.r[15]);
        }
    }
    const auto old_pteh = sh4_mmr_read(context, kSh4Pteh);
    if (context.bus && context.bus->write32) {
        context.bus->write32(kSh4Tea, address, context.pc);
        context.bus->write32(kSh4Pteh,
            (address & 0xfffffc00u) | (old_pteh & 0xffu), context.pc);
    }
    const auto fault_pc = access == Sh4Access::Instruction
        ? (context.delay_slot ? address - 2u : address)
        : context.delay_slot ? context.pc - 4u : context.pc - 2u;
    host_raise_exception(context, event, fault_pc,
                         event == kSh4TlbMissRead || event == kSh4TlbMissWrite
                             ? 0x400u : 0x100u);
    throw Sh4MemoryFault{};
}

bool sh4_segment_bypasses_tlb(std::uint32_t address) {
    const auto segment = address >> 29;
    return segment == 4u || segment == 5u || segment == 7u ||
           (address & 0xfc000000u) == 0x7c000000u;
}

std::uint32_t sh4_translate(const Sh4Context& context, std::uint32_t address,
                            Sh4Access access) {
    auto mmucr = sh4_mmr_read(context, kSh4Mmucr);
    const bool user_mode = (context.sr & (1u << 30)) == 0;
    const bool write = access == Sh4Access::Write;
    const bool instruction = access == Sh4Access::Instruction;
    const bool store_queue_write = write &&
        (address & 0xfc000000u) == 0xe0000000u;
    // Access-width alignment is checked by read16/read32/write16/write32.
    // Do not impose a longword check here: write16/write32 may decompose a
    // legal store-queue access into byte writes, and Flycast's mmu_WriteMem
    // likewise validates alignment using the original access width.
    if (store_queue_write && (((mmucr >> 9) & 1u) && user_mode))
        sh4_memory_exception(context, address, access, kSh4AddressErrorWrite);
    if (!(mmucr & 1u)) return address;
    if (!store_queue_write && user_mode && (address & 0x80000000u))
        sh4_memory_exception(context, address, access,
                             write ? kSh4AddressErrorWrite : kSh4AddressErrorRead);
    if (instruction && (address >> 29) == 7u)
        sh4_memory_exception(context, address, access, kSh4AddressErrorRead);
    if (sh4_segment_bypasses_tlb(address)) return address;

    const auto pteh = sh4_mmr_read(context, kSh4Pteh);
    static constexpr std::uint32_t masks[4] = {
        0xfffffc00u, 0xfffff000u, 0xffff0000u, 0xfff00000u
    };
    const Sh4TlbEntry* found = nullptr;
    auto matches = [&](const Sh4TlbEntry& entry) {
        if (!(entry.ptel & (1u << 8))) return false;
        const auto size = ((entry.ptel >> 4) & 1u) |
                          ((entry.ptel >> 6) & 2u);
        if (((entry.pteh & 0xfffffc00u) & masks[size]) !=
            (address & masks[size])) return false;
        const bool asid_match_required = !(entry.ptel & (1u << 1)) &&
            (user_mode || !(mmucr & (1u << 8)));
        return !asid_match_required || (entry.pteh & 0xffu) == (pteh & 0xffu);
    };
    auto scan = [&](const auto& entries) {
        for (const auto& entry : entries) {
            if (!matches(entry)) continue;
            if (found)
                sh4_memory_exception(context, address, access, kSh4TlbMultiHit);
            found = &entry;
        }
    };
    std::uint32_t itlb_slot = 4;
    if (instruction) {
        for (std::uint32_t slot = 0; slot < context.itlb.size(); ++slot) {
            if (!matches(context.itlb[slot])) continue;
            if (found)
                sh4_memory_exception(context, address, access, kSh4TlbMultiHit);
            found = &context.itlb[slot];
            itlb_slot = slot;
        }
    }
    else scan(context.utlb);
    if (!found && instruction) {
        // Flycast fills the four-entry ITLB from the UTLB on an instruction
        // miss and selects the victim from CCN.MMUCR.LRUI.
        auto& mutable_context = const_cast<Sh4Context&>(context);
        auto replacement_mmucr = mmucr;
        auto urc = ((mmucr >> 10) + 1u) & 0x3fu;
        if (urc == ((mmucr >> 18) & 0x3fu)) urc = 0;
        replacement_mmucr = (replacement_mmucr & ~(0x3fu << 10)) | (urc << 10);
        mmucr = replacement_mmucr;
        if (context.bus && context.bus->write32)
            context.bus->write32(kSh4Mmucr, replacement_mmucr, context.pc);
        scan(context.utlb);
        if (found) {
            const auto slot = sh4_itlb_replacement_slot((mmucr >> 26) & 0x3fu);
            mutable_context.itlb[slot] = *found;
            found = &mutable_context.itlb[slot];
            itlb_slot = slot;
        }
    }
    if (instruction && found) sh4_itlb_touch(context, mmucr, itlb_slot);
    if (!instruction) {
        auto replacement_mmucr = mmucr;
        auto urc = ((mmucr >> 10) + 1u) & 0x3fu;
        if (urc == ((mmucr >> 18) & 0x3fu)) urc = 0;
        replacement_mmucr = (replacement_mmucr & ~(0x3fu << 10)) | (urc << 10);
        mmucr = replacement_mmucr;
        if (context.bus && context.bus->write32)
            context.bus->write32(kSh4Mmucr, replacement_mmucr, context.pc);
    }
    if (!found)
        sh4_memory_exception(context, address, access,
            write ? kSh4TlbMissWrite : kSh4TlbMissRead);

    const auto size = ((found->ptel >> 4) & 1u) |
                      ((found->ptel >> 6) & 2u);
    const auto mask = masks[size];
    const auto privilege = (found->ptel >> 5) & 3u;
    if (user_mode && privilege == 0)
        sh4_memory_exception(context, address, access,
            write ? kSh4TlbProtectionWrite : kSh4TlbProtectionRead);
    if (write && !(found->ptel & 1u))
        sh4_memory_exception(context, address, access, kSh4TlbProtectionWrite);
    if (write && !(found->ptel & (1u << 2)))
        sh4_memory_exception(context, address, access, kSh4TlbFirstWrite);
    auto physical = ((found->ptel & 0x1ffffc00u) & mask) | (address & ~mask);
    if ((physical & 0x1c000000u) == 0x1c000000u) physical |= 0xf0000000u;
    return store_queue_write ? address : physical;
}

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
    static const bool enabled = std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_AICA") != nullptr;
    return enabled;
}

bool event_memory_trace_enabled() {
    static const bool enabled =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_EVENT_MEMORY") != nullptr;
    return enabled;
}

void trace_bios_pointer_table_read(const Sh4Context& context,
                                  std::uint32_t address, unsigned width,
                                  std::uint32_t value) {
    static const bool enabled =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_BIOS_POINTER_TABLE") != nullptr;
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
    if (!std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_BIOS_POINTER_TABLE")) return;
    std::fprintf(stderr, "SH-4 BIOS pointer-table read summary count=%u range=0c018040-0c018080\n",
                 g_naomi_bios_pointer_table_reads);
}

void trace_m2_pio_access(const Sh4Context& context, char operation,
                         std::uint32_t address, unsigned width,
                         std::uint32_t value) {
    static const bool enabled =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_M2_PIO_PC") != nullptr;
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
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_AOT_TABLE_WRITES") != nullptr;
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
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_INTEVT") &&
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
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_INTEVT") &&
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
    if (!std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PVR_FB_WRITES") || events >= 128)
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
        if (const char* text = std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_RANGE")) {
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
        if (const char* text = std::getenv("MULTIARCADERECOMP_NAOMI_WATCH_ADDRESS")) {
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
    // Opt-in bounded counterpart to MULTIARCADERECOMP_NAOMI_TRACE_TASK_RANGE writes.
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
            "MULTIARCADERECOMP_NAOMI_TRACE_TASK_RANGE_READ_CHANGES") != nullptr;
        if (const char* text = std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_RANGE_READ")) {
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
                 std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_RANGE_READ_CHANGES") ? 1u : 0u);
}

void trace_watched_pc(const Sh4Context& context) {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        if (const char* text = std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PC")) {
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
                 "SH-4 watched PC=%08x op=%04x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                 context.pc, context.read16(context.pc), context.r[0], context.r[1], context.r[2], context.r[3],
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
    const auto physical = sh4_translate(*this, address, Sh4Access::Read);
    value = bus && bus->read8 ? bus->read8(physical) : 0xff;
    trace_task_range_read(*this, address, 1, value);
    trace_bios_pointer_table_read(*this, address, 1, value);
    trace_m2_pio_access(*this, 'R', address, 1, value);
    trace_aica_access(*this, 'R', address, 1, value);
    return static_cast<std::uint8_t>(value);
}

std::uint16_t Sh4Context::read16(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (read_intevt(*this, address, 2, value)) return static_cast<std::uint16_t>(value);
    if (address & 1u)
        sh4_memory_exception(*this, address, Sh4Access::Read, kSh4AddressErrorRead);
    const auto physical = sh4_translate(*this, address, Sh4Access::Read);
    if (bus && bus->read16) value = bus->read16(physical);
    else {
        const auto low = bus && bus->read8 ? bus->read8(physical) : 0xffu;
        const auto high = bus && bus->read8 ? bus->read8(physical + 1u) : 0xffu;
        value = static_cast<std::uint16_t>(low) |
                (static_cast<std::uint16_t>(high) << 8);
    }
    trace_task_range_read(*this, address, 2, value);
    trace_bios_pointer_table_read(*this, address, 2, value);
    trace_m2_pio_access(*this, 'R', address, 2, value);
    trace_aica_access(*this, 'R', address, 2, value);
    return static_cast<std::uint16_t>(value);
}

std::uint32_t Sh4Context::read32(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (!read_intevt(*this, address, 4, value)) {
        if (address & 3u)
            sh4_memory_exception(*this, address, Sh4Access::Read, kSh4AddressErrorRead);
        const auto physical = sh4_translate(*this, address, Sh4Access::Read);
        if (bus && bus->read32) value = bus->read32(physical);
        else {
            const auto low = bus && bus->read16 ? bus->read16(physical) : 0xffffu;
            const auto high = bus && bus->read16 ? bus->read16(physical + 2u) : 0xffffu;
            value = static_cast<std::uint32_t>(low) |
                    (static_cast<std::uint32_t>(high) << 16);
        }
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

bool Sh4Context::fetchInstruction16(std::uint32_t address,
                                    std::uint16_t& instruction) const {
    if (address & 1u)
        sh4_memory_exception(*this, address, Sh4Access::Instruction,
                             kSh4AddressErrorRead);
    const auto physical = sh4_translate(*this, address, Sh4Access::Instruction);
    if (bus && bus->read16) instruction = bus->read16(physical);
    else {
        const auto low = bus && bus->read8 ? bus->read8(physical) : 0xffu;
        const auto high = bus && bus->read8 ? bus->read8(physical + 1u) : 0xffu;
        instruction = static_cast<std::uint16_t>(low) |
                      (static_cast<std::uint16_t>(high) << 8);
    }
    return true;
}

void Sh4Context::write8(std::uint32_t address, std::uint8_t value) const {
    noteAotCodeWrite(address, 1);
    trace_pvr_framebuffer_write(*this, address, 1, value);
    trace_aot_table_guest_write(*this, address, 1, value);
    trace_watched_write(*this, address, 1, value);
    trace_m2_pio_access(*this, 'W', address, 1, value);
    trace_aica_access(*this, 'W', address, 1, value);
    if (write_intevt(*this, address, 1, value)) return;
    const auto physical = sh4_translate(*this, address, Sh4Access::Write);
    if ((address & 0xfc000000u) == 0xe0000000u) {
        const auto offset = static_cast<std::size_t>(address & 0x3fu);
        const_cast<Sh4Context*>(this)->store_queue[offset] = value;
        return;
    }
    if (bus && bus->write8) bus->write8(physical, value, pc);
}

void Sh4Context::write16(std::uint32_t address, std::uint16_t value) const {
    noteAotCodeWrite(address, 2);
    trace_pvr_framebuffer_write(*this, address, 2, value);
    trace_aot_table_guest_write(*this, address, 2, value);
    trace_watched_write(*this, address, 2, value);
    trace_m2_pio_access(*this, 'W', address, 2, value);
    trace_aica_access(*this, 'W', address, 2, value);
    if (write_intevt(*this, address, 2, value)) return;
    if (address & 1u)
        sh4_memory_exception(*this, address, Sh4Access::Write, kSh4AddressErrorWrite);
    const auto physical = sh4_translate(*this, address, Sh4Access::Write);
    if ((address & 0xfc000000u) == 0xe0000000u) {
        write8(address, static_cast<std::uint8_t>(value));
        write8(address + 1, static_cast<std::uint8_t>(value >> 8));
        return;
    }
    if (bus && bus->write16) {
        bus->write16(physical, value, pc);
        return;
    }
    if (bus && bus->write8) {
        bus->write8(physical, static_cast<std::uint8_t>(value), pc);
        bus->write8(physical + 1u, static_cast<std::uint8_t>(value >> 8), pc);
    }
}

void Sh4Context::write32(std::uint32_t address, std::uint32_t value) const {
    noteAotCodeWrite(address, 4);
    trace_pvr_framebuffer_write(*this, address, 4, value);
    trace_aot_table_guest_write(*this, address, 4, value);
    trace_watched_write(*this, address, 4, value);
    trace_m2_pio_access(*this, 'W', address, 4, value);
    trace_aica_access(*this, 'W', address, 4, value);
    if (write_intevt(*this, address, 4, value)) return;
    if (address & 3u)
        sh4_memory_exception(*this, address, Sh4Access::Write, kSh4AddressErrorWrite);
    const auto physical = sh4_translate(*this, address, Sh4Access::Write);
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
        const auto bus_address = physical & 0x1fffffffu;
        if (bus_address == 0x1f000010u && (value & (1u << 2))) {
            auto& mutable_context = *const_cast<Sh4Context*>(this);
            mutable_context.utlb.fill({});
            mutable_context.itlb.fill({});
            value &= ~(1u << 2); // MMUCR.TI is a write-one command bit.
        }
        bus->write32(physical, value, pc);
        return;
    }
    if (bus && bus->write16) {
        bus->write16(physical, static_cast<std::uint16_t>(value), pc);
        bus->write16(physical + 2u, static_cast<std::uint16_t>(value >> 16), pc);
    } else if (bus && bus->write8) {
        for (unsigned byte = 0; byte < 4; ++byte)
            bus->write8(physical + byte,
                        static_cast<std::uint8_t>(value >> (byte * 8u)), pc);
    }
}

void Sh4Context::noteAotCodeWrite(std::uint32_t address, unsigned width) const {
    if (width == 0) return;
    auto& epochs = const_cast<Sh4Context*>(this)->aot_code_write_epochs;
    std::uint32_t last_line = 0xffffffffu;
    for (unsigned byte = 0; byte < width; ++byte) {
        const auto line = ((address + byte) & 0x1fffffffu) & ~0x1fu;
        if (line != last_line) ++epochs[line];
        last_line = line;
    }
}

void Sh4Context::dispatch_indirect(std::uint32_t target) {
    const auto source_pc = pc;
    static const bool trace_callback_table =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_CALLBACK_TABLE") != nullptr;
    if (trace_callback_table && source_pc == 0x0c0353fcu &&
        g_naomi_callback_table_dispatch_events < 256) {
        ++g_naomi_callback_table_dispatch_events;
        std::fprintf(stderr,
                     "SH-4 callback-table dispatch call_pc=%08x delay_pc=%08x index=%u r10=%08x table_base=%08x r3=%08x target=%08x pr=%08x\n",
                     source_pc - 2u, source_pc, r[10] >> 2, r[10], r[0],
                     r[3], target, pr);
    }
    static const bool trace_task_callbacks =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_CALLBACKS") != nullptr;
    // The BIOS is commonly reached through P1/P2 aliases. Compare the
    // physical SH-4 address so diagnostics follow the same code regardless
    // of the alias used by the current AOT block.
    if (trace_task_callbacks &&
        ((source_pc & 0x1fffffffu) == 0x0c045fdeu)) {
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
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_CALLBACKS") &&
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
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_M2_HANDOFF") &&
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
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_DISPATCH")) {
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
    std::optional<Sh4PendingInterrupt> pending;
    if (bus && bus->pending_internal_interrupt)
        pending = bus->pending_internal_interrupt();
    unsigned level = 0;
    std::uint32_t event = 0;
    if (irln == 9 || irln == 11 || irln == 13) {
        level = 15u - irln;
        event = level == 6 ? 0x320u : level == 4 ? 0x360u : 0x3a0u;
    }
    // On equal priority, preserve Flycast's source ordering: external IRL
    // entries precede the internal TMU sources in InterruptSourceList.
    if (pending && pending->priority > level) {
        level = pending->priority;
        event = pending->event;
    }
    if (level == 0) return false;
    const unsigned mask = (sr >> 4) & 0x0fu;
    constexpr std::uint32_t kSrBl = 1u << 28;
    if ((sr & kSrBl) != 0 || level <= mask) return false;

    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_IRQ")) {
        static unsigned trace_events = 0;
        if (trace_events++ < 32)
            std::fprintf(stderr,
                         "SH-4 IRQ accept pc=%08x level=%u sr=%08x irln=%u vbr=%08x\n",
                         pc, level, sr, irln, vbr);
    }

    spc = pc;
    ssr = sr;
    sgr = r[15];
    intevt = event;
    load_sr(*this, sr | (1u << 30) | (1u << 29) | kSrBl);
    pc = vbr + 0x600u;
    ++interrupts_accepted;
    sleeping = false;
    interrupt_taken = true;
    return true;
}

bool Sh4Context::consume_instruction(unsigned cycles, bool interpreted) {
    // These switches sit on the per-instruction hot path. Cache them once so
    // disabled debug traces do not call getenv() for every SH-4 instruction.
    static const bool trace_reset_idle =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_RESET_IDLE") != nullptr;
    static const bool trace_game_entry =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_GAME_ENTRY") != nullptr;
    static const bool trace_boot_loop =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_BOOT_LOOP") != nullptr;
    static const bool trace_progress =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_PROGRESS") != nullptr;
    static const char* trace_manifest_sha256 =
        std::getenv("MULTIARCADERECOMP_NAOMI_MANIFEST_SHA256");
    static const bool trace_bios_task_pc =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_CALLBACKS") != nullptr;
    static std::uint32_t bios_task_pc_events = 0;
    const auto physical_pc = pc & 0x1fffffffu;
    if (trace_bios_task_pc && bios_task_pc_events < 256u &&
        ((physical_pc >= 0x0c045fd0u && physical_pc <= 0x0c045ff0u) ||
         physical_pc == 0x0c04b88cu)) {
        ++bios_task_pc_events;
        std::fprintf(stderr,
                     "SH-4 BIOS task retired #%u pc=%08x op=%04x r2=%08x r3=%08x r4=%08x pr=%08x mode=%s\n",
                     bios_task_pc_events, pc, read16(pc), r[2], r[3], r[4], pr,
                     interpreted ? "interpreter" : "AOT");
    }

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
    if (!delay_slot && cycles_until_irq_poll == 0) {
        cycles_until_irq_poll = 448; // Flycast SH4_TIMESLICE
        if (acceptInterrupt()) return false;
    }
    if (instruction_budget == 0) {
        execution_stopped = true;
        return false;
    }
    --instruction_budget;
    ++instructions_executed;
    if (interpreted)
        ++interpreted_instructions;
    else
        ++native_instructions;
    if (cycle_sink && cycles != 0) cycle_sink(cycles);
    cycles_until_irq_poll = cycles >= cycles_until_irq_poll
        ? 0u : cycles_until_irq_poll - cycles;
    return true;
}

bool Sh4Context::retire_opcode(std::uint16_t opcode, bool interpreted) {
    // Log GBR loads at the next instruction boundary so the same hook observes
    // both interpreted instructions and generated AOT instructions without
    // changing either execution path.
    static const bool trace_gbr =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_GBR") != nullptr;
    static std::uint32_t previous_pc = 0;
    static std::uint16_t previous_opcode = 0;
    static bool previous_wrote_gbr = false;
    static std::uint32_t trace_count = 0;
    if (trace_gbr && previous_wrote_gbr && trace_count < 4096) {
        std::fprintf(stderr,
                     "SH-4 GBR load pc=%08x op=%04x value=%08x next_pc=%08x next_op=%04x mode=%s\n",
                     previous_pc, previous_opcode, gbr, pc, opcode,
                     interpreted ? "interpreter" : "AOT");
        ++trace_count;
    }
    if (trace_gbr) {
        previous_pc = pc;
        previous_opcode = opcode;
        previous_wrote_gbr = (opcode & 0xf0ffu) == 0x401eu ||
                             (opcode & 0xf0ffu) == 0x4017u;
    }
    return consume_instruction(
        multiarcaderecomp::cpus::sh4::flycast::instruction_cycles(opcode, cycle_state),
        interpreted);
}

void Sh4Dispatcher::add(std::uint32_t guest_address, Sh4TranslatedFunction function) {
    if (!function) return;
    if (guest_address == 0x0c04b88cu &&
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_CALLBACKS")) {
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

void Sh4Dispatcher::add(std::uint32_t guest_address,
                        Sh4TranslatedFunction function,
                        std::uint32_t code_base,
                        std::size_t expected_word_count,
                        std::uint64_t expected_hash) {
    add(guest_address, function);
    if (!function || expected_word_count == 0 || expected_hash == 0) return;
    auto [it, inserted] = m_code_guards.try_emplace(function);
    if (inserted) {
        it->second.base = code_base;
        it->second.word_count = expected_word_count;
        it->second.hash = expected_hash;
        return;
    }
    // A generated function must have one stable source range regardless of
    // how many PC entries/aliases point at it. A mismatch means the generator
    // accidentally merged incompatible translations; keep the first guard,
    // which makes the later code fail closed to the interpreter.
    if (it->second.base != code_base || it->second.word_count != expected_word_count ||
        it->second.hash != expected_hash) {
        it->second.word_count = 0;
    }
}

bool Sh4Dispatcher::contains(std::uint32_t guest_address) const {
    return m_functions.contains(guest_address);
}

void Sh4Dispatcher::call(Sh4Context& context, std::uint32_t target) const {
    if (std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_BIOS_M2_FLOW")) {
        static constexpr std::array<std::uint32_t, 7> watched{
            0x0c0193d8u, 0x0c01943cu, 0x0c019550u, 0x0c0195e8u,
            0x0c019602u, 0x0c019626u, 0x0c019640u};
        static std::array<unsigned, watched.size()> counts{};
        const auto physical = target & 0x1fffffffu;
        for (std::size_t i = 0; i < watched.size(); ++i) {
            if (physical != watched[i] || counts[i] >= 16) continue;
            ++counts[i];
            std::fprintf(stderr,
                         "SH-4 BIOS M2 flow #%u target=%08x pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x pr=%08x sr=%08x T=%u sp=%08x\n",
                         counts[i], target, context.pc, context.r[0], context.r[1],
                         context.r[2], context.r[3], context.r[4], context.r[5],
                         context.r[6], context.r[7], context.r[8], context.r[9],
                         context.r[10], context.r[11], context.r[12], context.r[13],
                         context.r[14], context.pr, context.sr,
                         context.t ? 1u : 0u, context.r[15]);
            break;
        }
    }
    // Keep translated blocks out of the AOT path when their source contains
    // an opcode whose current AOT decoder assigns the wrong SH-4 register.
    // This is a dispatcher compatibility check: it does not alter generated
    // code or the AOT translator, and the Flycast interpreter retires the
    // block instruction by instruction until it reaches another AOT entry.
    // LDC Rn,SSR (0x4n3e) is the concrete case: Flycast's SH-4 opcode table
    // names SSR, while the existing AOT decoder aliases it to LDC GBR.
    const auto requires_interpreter = [](std::uint16_t opcode) {
        return (opcode & 0xf0ffu) == 0x403eu;
    };
    std::unordered_set<Sh4TranslatedFunction> interpreter_only_functions;
    const bool trace_aot_fallback =
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_AOT_FALLBACK") != nullptr;
    const auto lookup = [this, &context, &requires_interpreter,
                         &interpreter_only_functions, trace_aot_fallback](std::uint32_t address) {
        if (context.interpreter_only) return m_functions.end();
        const auto physical = address & 0x1fffffffu;
        if (context.invalidated_aot_lines.contains(physical & ~0x1fu))
            return m_functions.end();
        auto found = m_functions.find(address);
        if (found == m_functions.end()) {
            // P0, P1, and P2 may name the same physical instruction bytes.
            found = m_functions.find(physical);
            if (found == m_functions.end())
                found = m_functions.find(0x80000000u | physical);
            if (found == m_functions.end())
                found = m_functions.find(0xa0000000u | physical);
        }
        if (found == m_functions.end()) return found;
        if (interpreter_only_functions.contains(found->second))
            return m_functions.end();
        const auto guard = m_code_guards.find(found->second);
        if (guard == m_code_guards.end()) return found; // hand-written AOT entry

        const auto& code = guard->second;
        if (code.word_count == 0) return m_functions.end();
        const auto first_line = (code.base & 0x1fffffffu) & ~0x1fu;
        const auto last_address = code.base +
            static_cast<std::uint32_t>(code.word_count * 2u - 1u);
        const auto last_line = (last_address & 0x1fffffffu) & ~0x1fu;
        std::vector<std::pair<std::uint32_t, std::uint64_t>> epochs;
        for (auto line = first_line;; line += 32u) {
            if (context.invalidated_aot_lines.contains(line)) return m_functions.end();
            const auto epoch_it = context.aot_code_write_epochs.find(line);
            epochs.emplace_back(line, epoch_it == context.aot_code_write_epochs.end()
                                           ? 0u : epoch_it->second);
            if (line == last_line) break;
            // A generated code span cannot cross the 512 MiB physical alias
            // boundary in practice; fail closed if a malformed range does.
            if (line > last_line || line > 0x1fffffe0u) return m_functions.end();
        }

        auto& cached = m_guard_cache[found->second];
        const bool epochs_match = cached.initialized && cached.line_epochs == epochs;
        if (!epochs_match) {
            constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
            constexpr std::uint64_t fnv_prime = 1099511628211ull;
            auto hash = fnv_offset;
            for (std::size_t i = 0; i < code.word_count; ++i) {
                const auto word = context.read16(
                    code.base + static_cast<std::uint32_t>(i * 2u));
                hash ^= static_cast<std::uint8_t>(word);
                hash *= fnv_prime;
                hash ^= static_cast<std::uint8_t>(word >> 8u);
                hash *= fnv_prime;
            }
            cached.initialized = true;
            cached.matches = hash == code.hash;
            cached.line_epochs = std::move(epochs);
        }
        if (!cached.matches) return m_functions.end();
        for (std::size_t i = 0; i < code.word_count; ++i) {
            const auto opcode = context.read16(
                code.base + static_cast<std::uint32_t>(i * 2u));
            if (requires_interpreter(opcode)) {
                interpreter_only_functions.insert(found->second);
                if (trace_aot_fallback) {
                    std::fprintf(stderr,
                                 "SH-4 AOT block routed to Flycast interpreter: entry=%08x code=%08x opcode=%04x offset=%zu\n",
                                 found->first, code.base + static_cast<std::uint32_t>(i * 2u),
                                 opcode, i);
                }
                return m_functions.end();
            }
        }
        return found;
    };

    auto found = lookup(target);
    while (found == m_functions.end() && context.interpreter_step &&
           !context.execution_stopped && context.instruction_budget != 0) {
        if (context.sleeping) {
            if (context.cycles_until_irq_poll == 0) {
                context.cycles_until_irq_poll = 448; // Flycast SH4_TIMESLICE
                if (context.acceptInterrupt()) return;
            }
            --context.instruction_budget; // budget also bounds time spent in SLEEP
            if (context.cycle_sink) context.cycle_sink(1);
            if (context.cycles_until_irq_poll != 0)
                --context.cycles_until_irq_poll;
            continue;
        }
        context.pc = target;
        const auto instructions_before_step = context.instructions_executed;
        const auto step_pc = context.pc;
        const auto physical_step_pc = step_pc & 0x1fffffffu;
        const bool trace_bios_task_step =
            std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_CALLBACKS") &&
            (physical_step_pc == 0x0c045fdcu ||
             physical_step_pc == 0x0c04b88cu);
        static std::uint32_t bios_task_step_events = 0;
        const auto task_step_event = trace_bios_task_step
            ? ++bios_task_step_events : 0u;
        if (trace_bios_task_step && task_step_event <= 64u) {
            std::fprintf(stderr,
                         "SH-4 BIOS task interpreter before #%u pc=%08x op=%04x r2=%08x r3=%08x r4=%08x pr=%08x sp=%08x\n",
                         task_step_event, step_pc, context.read16(step_pc),
                         context.r[2], context.r[3], context.r[4], context.pr,
                         context.r[15]);
        }
        const bool stepped = context.interpreter_step(context);
        if (trace_bios_task_step && task_step_event <= 64u) {
            std::fprintf(stderr,
                         "SH-4 BIOS task interpreter after  #%u pc=%08x r2=%08x r3=%08x r4=%08x pr=%08x sp=%08x retired=%llu\n",
                         task_step_event, context.pc, context.r[2], context.r[3],
                         context.r[4], context.pr, context.r[15],
                         static_cast<unsigned long long>(context.instructions_executed - instructions_before_step));
        }

        // An interrupt can redirect execution without retiring an instruction;
        // let the outer dispatcher select its vector entry.
        if (context.interrupt_taken) return;
        if (!stepped) {
            if (context.instruction_budget == 0 || context.execution_stopped)
                return;
            const auto failed_pc = context.pc;
            std::fprintf(stderr,
                         "SH-4 interpreter declined execution pc=%08x opcode=%04x delay_slot=%u sr=%08x fpscr=%08x pr=%u\n",
                         failed_pc, context.read16(failed_pc),
                         context.delay_slot ? 1u : 0u, context.sr, context.fpscr,
                         context.fpscr_pr ? 1u : 0u);
            context.unresolved_target = context.pc;
            context.execution_stopped = true;
            throw UnresolvedSh4Target{};
        }
        if (context.instructions_executed == instructions_before_step && !context.sleeping) {
            context.unresolved_target = context.pc;
            context.execution_stopped = true;
            std::fprintf(stderr,
                         "SH-4 interpreter step made no guest progress: pc=%08x\n",
                         context.pc);
            throw UnresolvedSh4Target{};
        }

        target = context.pc;
        found = lookup(target);
    }
    if (found == m_functions.end()) {
        if (context.execution_stopped || context.instruction_budget == 0) return;
        context.pc = target;
        context.unresolved_target = target;
        context.execution_stopped = true;
        // Static translated functions call one another on the host stack. A
        // missing guest target must unwind that whole stack immediately; if
        // we merely returned here, the caller would continue past the guest
        // JSR/JMP and could make a broken boot look like an idle loop.
        throw UnresolvedSh4Target{};
    }
    // An entry may have been found through a P0/P1/P2 physical alias. The
    // generated function's internal PC switch uses the exact virtual alias
    // under which it was translated (for example ACxxxxxx), so enter with
    // that registered key. Leaving the caller's alias here makes the function
    // hit its default switch arm and return without retiring an instruction.
    context.pc = found->first;
    const auto task_callback_instruction_start = context.instructions_executed;
    const auto task_callback_function = found->second;
    found->second(context);
    if (target == 0x0c04b88cu &&
        std::getenv("MULTIARCADERECOMP_NAOMI_TRACE_TASK_CALLBACKS")) {
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
    context.cycles_until_irq_poll = 448;
    context.cycle_state = {};
    context.native_instructions = 0;
    context.interpreted_instructions = 0;
    context.unresolved_target = 0;
    context.last_indirect_target = 0;
    context.indirect_calls = 0;
    context.recent_indirect_targets.fill(0);
    context.execution_stopped = false;
    context.interrupt_taken = false;
    context.indirect_call = [this](Sh4Context& ctx, std::uint32_t target) {
        call(ctx, target);
    };

    // Most guest calls return through the active translated C++ call chain.
    // A top-level tail jump/RTS has no translated caller, however, so resume
    // from the guest PC after that chain unwinds (notably BIOS P2 -> P0 paths).
    // Pass even an unregistered PC back to call(): its AOT lookup will miss
    // and the Flycast interpreter will run until a valid translated entry is
    // reached. Stopping here would silently disable the hybrid fallback at
    // precisely the AOT-to-interpreter boundary.
    auto next_entry = entry;
    while (!context.execution_stopped && context.instruction_budget != 0) {
        const auto dispatched_entry = next_entry;
        context.interrupt_taken = false;
        const auto instructions_before_dispatch = context.instructions_executed;
        try {
            call(context, dispatched_entry);
        } catch (const Sh4MemoryFault&) {
            // The access path has already entered the guest exception vector.
            // Continue at that PC after unwinding the current translated or
            // interpreted instruction, as Flycast does for MMU exceptions.
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
        next_entry = context.pc;
    }

    trace_task_range_read_summary();
    trace_bios_pointer_table_read_summary();
    trace_aica_summary();
    return {context.instructions_executed, context.native_instructions,
            context.interpreted_instructions, context.unresolved_target,
            context.execution_stopped && context.instruction_budget == 0 &&
                context.unresolved_target == 0};
}

} // namespace multiarcaderecomp::naomi
