#include "runtime/sega/debugger/m68k_debugger.hpp"

#include <neogeorecomp/cpu.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mamerecomp {
namespace {

struct Range {
    std::uint32_t start;
    std::uint32_t length;
};
struct WriteHit {
    std::uint32_t address;
    std::uint8_t width;
    std::uint32_t value;
};

SegaAddressSpace* s_bus = nullptr;
const std::uint64_t* s_vblank_count = nullptr;
bool s_enabled = false;
bool s_quit = false;
bool s_initial_stop = false;
bool s_step_pending = false;
std::optional<std::uint32_t> s_skip_breakpoint_once;
std::optional<WriteHit> s_write_hit;
std::vector<std::uint32_t> s_breakpoints;
std::vector<Range> s_watchpoints;
std::ofstream s_trace;
bool s_trace_enabled = false;
std::uint64_t s_trace_start = 0;
std::uint64_t s_trace_end = 0;
std::uint32_t s_current_pc = 0;

std::uint16_t status_register();

void trace_instruction(std::uint32_t guest_pc) {
    if (!s_trace_enabled || ng_cycles < 0) return;
    const auto cycles = static_cast<std::uint64_t>(ng_cycles);
    if (cycles < s_trace_start || cycles > s_trace_end) return;
    s_trace << "I\t" << std::dec << cycles << '\t' << std::hex << std::setfill('0')
            << std::setw(6) << (guest_pc & 0x00ffffffU) << '\t' << std::setw(4) << status_register();
    for (unsigned i = 0; i < 8; ++i) s_trace << '\t' << std::setw(8) << CPU.d[i];
    for (unsigned i = 0; i < 8; ++i) s_trace << '\t' << std::setw(8) << CPU.a[i];
    s_trace << '\n' << std::flush;
}

std::optional<std::uint32_t> parse_number(std::string_view text) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

std::uint16_t status_register() {
    return static_cast<std::uint16_t>((CPU.t << 15) | (CPU.s << 13) | (CPU.ipl << 8)
        | (CPU.x << 4) | (CPU.n << 3) | (CPU.z << 2) | (CPU.v << 1) | CPU.c);
}

void print_registers(std::uint32_t guest_pc) {
    std::cout << "PC=" << std::hex << std::setfill('0') << std::setw(6) << (guest_pc & 0x00ffffffU)
              << " SR=" << std::setw(4) << status_register()
              << " cycles=" << std::dec << ng_cycles
              << " vblank=" << (s_vblank_count ? *s_vblank_count : 0) << '\n';
    for (unsigned i = 0; i < 8; ++i) {
        std::cout << 'D' << i << '=' << std::hex << std::setw(8) << CPU.d[i]
                  << (i == 7 ? '\n' : ' ');
    }
    for (unsigned i = 0; i < 8; ++i) {
        std::cout << 'A' << i << '=' << std::hex << std::setw(8) << CPU.a[i]
                  << (i == 7 ? '\n' : ' ');
    }
    std::cout << "USP/SSP=" << std::setw(8) << CPU.osp << std::dec
              << " stopped=" << static_cast<unsigned>(CPU.stopped) << '\n';
}

bool in_range(const Range& range, std::uint32_t address) {
    address &= 0x00ffffffU;
    const std::uint32_t start = range.start & 0x00ffffffU;
    return address >= start && static_cast<std::uint64_t>(address - start) < range.length;
}

void dump_memory(std::string_view address_text, std::string_view length_text) {
    const auto parsed_address = parse_number(address_text);
    const auto parsed_length = length_text.empty() ? std::optional<std::uint32_t>(32) : parse_number(length_text);
    if (!s_bus || !parsed_address || !parsed_length || *parsed_length == 0 || *parsed_length > 256) {
        std::cout << "usage: m <address> [length 1..256]\n";
        return;
    }
    const std::uint32_t start = *parsed_address & 0x00ffffffU;
    for (std::uint32_t offset = 0; offset < *parsed_length; offset += 16) {
        const std::uint32_t count = std::min<std::uint32_t>(16, *parsed_length - offset);
        std::cout << std::hex << std::setfill('0') << std::setw(6) << (start + offset) << ":";
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto byte = s_bus->peek8((start + offset + i) & 0x00ffffffU);
            if (byte) std::cout << ' ' << std::setw(2) << static_cast<unsigned>(*byte);
            else std::cout << " ??";
        }
        std::cout << '\n';
    }
    std::cout << std::dec;
}

void print_help() {
    std::cout << "Commands: c continue, s step one 68000 instruction, r registers, "
                 "m <addr> [len] memory, b <addr> breakpoint, bl list breakpoints, "
                 "w <addr> [len] write watchpoint, wl list watchpoints, q quit\n"
                 "Addresses and lengths accept decimal or 0x-prefixed hex. MMIO reads are not performed.\n";
}

void print_stop(std::uint32_t pc) {
    if (s_write_hit) {
        std::cout << "Write watchpoint: addr=0x" << std::hex << s_write_hit->address
                  << " width=" << std::dec << static_cast<unsigned>(s_write_hit->width)
                  << " value=0x" << std::hex << s_write_hit->value << '\n';
        s_write_hit.reset();
    }
    print_registers(pc);
}

void enter_monitor(std::uint32_t pc) {
    print_stop(pc);
    std::string line;
    for (;;) {
        std::cout << "m68k> " << std::flush;
        if (!std::getline(std::cin, line)) {
            s_quit = true;
            return;
        }
        std::istringstream input(line);
        std::string command, first, second;
        input >> command >> first >> second;
        if (command.empty()) continue;
        if (command == "c" || command == "continue") {
            s_skip_breakpoint_once = pc;
            s_step_pending = false;
            return;
        }
        if (command == "s" || command == "step") {
            s_skip_breakpoint_once = pc;
            s_step_pending = true;
            return;
        }
        if (command == "q" || command == "quit") {
            s_quit = true;
            return;
        }
        if (command == "r" || command == "regs") {
            print_registers(pc);
        } else if (command == "m" || command == "mem") {
            dump_memory(first, second);
        } else if (command == "b" || command == "break") {
            const auto address = parse_number(first);
            if (!address) std::cout << "usage: b <address>\n";
            else {
                const auto normalized = *address & 0x00ffffffU;
                if (std::find(s_breakpoints.begin(), s_breakpoints.end(), normalized) == s_breakpoints.end())
                    s_breakpoints.push_back(normalized);
                std::cout << "Breakpoint set at 0x" << std::hex << normalized << std::dec << '\n';
            }
        } else if (command == "bl") {
            for (const auto address : s_breakpoints) std::cout << "break 0x" << std::hex << address << std::dec << '\n';
        } else if (command == "w" || command == "watch") {
            const auto address = parse_number(first);
            const auto length = second.empty() ? std::optional<std::uint32_t>(2) : parse_number(second);
            if (!address || !length || *length == 0) std::cout << "usage: w <address> [length]\n";
            else {
                s_watchpoints.push_back({*address & 0x00ffffffU, *length});
                std::cout << "Write watchpoint set at 0x" << std::hex << (*address & 0x00ffffffU)
                          << " length=0x" << *length << std::dec << '\n';
            }
        } else if (command == "wl") {
            for (const auto& watch : s_watchpoints)
                std::cout << "write 0x" << std::hex << watch.start << " length=0x" << watch.length << std::dec << '\n';
        } else if (command == "h" || command == "help") {
            print_help();
        } else {
            print_help();
        }
    }
}

void instruction_hook(std::uint32_t pc) {
    s_current_pc = pc & 0x00ffffffU;
    static bool trace_hook_reported = false;
    if (s_trace_enabled && !trace_hook_reported) {
        std::cout << "68000 trace hook active at cycle " << ng_cycles << '\n';
        trace_hook_reported = true;
    }
    trace_instruction(s_current_pc);
    if (!s_enabled || s_quit) return;
    const bool skip_this_breakpoint = s_skip_breakpoint_once && *s_skip_breakpoint_once == (pc & 0x00ffffffU);
    s_skip_breakpoint_once.reset();
    const bool breakpoint = !skip_this_breakpoint
        && std::find(s_breakpoints.begin(), s_breakpoints.end(), pc & 0x00ffffffU) != s_breakpoints.end();
    const bool should_stop = s_initial_stop || s_step_pending || s_write_hit || breakpoint;
    if (!should_stop) return;
    s_initial_stop = false;
    s_step_pending = false;
    if (breakpoint && !skip_this_breakpoint) std::cout << "Breakpoint at 0x" << std::hex << pc << std::dec << '\n';
    enter_monitor(pc);
    if (s_quit) ng_debug_instruction_hook = nullptr;
}

} // namespace

void configure_m68k_debugger(bool enabled, SegaAddressSpace* bus, const std::uint64_t* vblank_count,
    const std::filesystem::path* trace_path, std::uint64_t trace_start, std::uint64_t trace_end) {
    s_bus = bus;
    s_vblank_count = vblank_count;
    s_enabled = enabled;
    s_trace.close();
    s_trace_enabled = trace_path && trace_start <= trace_end;
    s_trace_start = trace_start;
    s_trace_end = trace_end;
    if (s_trace_enabled) {
        s_trace.open(*trace_path, std::ios::out | std::ios::trunc);
        if (!s_trace) throw std::runtime_error("unable to open 68000 trace output: " + trace_path->string());
        s_trace << "# I cycle pc sr D0..D7 A0..A7; R/W cycle instruction_pc address width value\n" << std::flush;
        std::cout << std::dec << "68000 trace: " << trace_path->string() << " cycles "
                  << trace_start << ".." << trace_end << '\n';
    }
    s_quit = false;
    s_initial_stop = enabled;
    s_step_pending = false;
    s_skip_breakpoint_once.reset();
    s_write_hit.reset();
    s_breakpoints.clear();
    s_watchpoints.clear();
    ng_debug_instruction_hook = (enabled || s_trace_enabled) ? instruction_hook : nullptr;
}

bool m68k_debugger_quit_requested() { return s_quit; }

void report_m68k_debug_snapshot(std::uint64_t vblank, std::int64_t cycles) {
    if (!s_enabled) return;
    std::cout << "Debugger vblank snapshot: vblank=" << std::dec << vblank << " cycles=" << cycles << '\n';
    print_registers(CPU.pc);
}

void m68k_debugger_note_read(std::uint32_t address, std::uint8_t width, std::uint32_t value) {
    if (!s_trace_enabled || ng_cycles < 0) return;
    const auto cycles = static_cast<std::uint64_t>(ng_cycles);
    if (cycles < s_trace_start || cycles > s_trace_end) return;
    s_trace << "R\t" << std::dec << cycles << '\t' << std::hex << std::setfill('0')
            << std::setw(6) << s_current_pc << '\t' << std::setw(6) << (address & 0x00ffffffU)
            << '\t' << std::dec << static_cast<unsigned>(width) << '\t' << std::hex
            << std::setw(8) << value << '\n' << std::flush;
}

void m68k_debugger_note_write(std::uint32_t address, std::uint8_t width, std::uint32_t value) {
    if (s_trace_enabled && ng_cycles >= 0) {
        const auto cycles = static_cast<std::uint64_t>(ng_cycles);
        if (cycles >= s_trace_start && cycles <= s_trace_end) {
            s_trace << "W\t" << std::dec << cycles << '\t' << std::hex << std::setfill('0')
                    << std::setw(6) << s_current_pc << '\t' << std::setw(6) << (address & 0x00ffffffU)
                    << '\t' << std::dec << static_cast<unsigned>(width) << '\t' << std::hex
                    << std::setw(8) << value << '\n' << std::flush;
        }
    }
    if (!s_enabled || s_write_hit) return;
    for (const auto& watch : s_watchpoints) {
        if (in_range(watch, address)) {
            s_write_hit = WriteHit{address & 0x00ffffffU, width, value};
            return;
        }
    }
}

} // namespace mamerecomp

extern "C" void mamerecomp_debug_instruction_adapter(uint32_t guest_pc) {
    if (ng_debug_instruction_hook) ng_debug_instruction_hook(guest_pc);
}

extern "C" void mamerecomp_debug_write_adapter(uint32_t address, uint8_t width, uint32_t value) {
    mamerecomp::m68k_debugger_note_write(address, width, value);
}

extern "C" void mamerecomp_debug_read_adapter(uint32_t address, uint8_t width, uint32_t value) {
    mamerecomp::m68k_debugger_note_read(address, width, value);
}
