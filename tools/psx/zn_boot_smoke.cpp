#include "zn_runtime.hpp"

#include <cstdlib>
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>

extern "C" { std::int64_t ng_cycles = 0; }

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: zn_boot_smoke <sfex2pa|bldyror2> <rom.zip> [instructions]\n";
        return 2;
    }
    try {
        const uint64_t limit = argc == 4 ? std::strtoull(argv[3], nullptr, 0) : 10'000'000;
        multiarcaderecomp::zn::ZnMachineRuntime machine(argv[1], argv[2]);
        struct Trace { uint32_t pc, op, t0, t1, t2, t3, s0, s1, sp, ra, stack_ra; };
        std::array<Trace, 4096> history{};
        uint64_t exceptions = 0;
        unsigned low_trace_lines = 0;
        unsigned boot_trace_lines = 0;
        unsigned watched_pc_lines = 0;
        unsigned watched_write_lines = 0;
        const char *step_trace_text = std::getenv("MULTIARCADERECOMP_ZN_TRACE_STEPS");
        uint32_t step_trace_pc = 0xffffffffu;
        uint64_t step_trace_limit = 0;
        if (step_trace_text) {
            char *end = nullptr;
            step_trace_pc = static_cast<uint32_t>(std::strtoul(step_trace_text, &end, 0));
            if (end && *end == ':') step_trace_limit = std::strtoull(end + 1, nullptr, 0);
        }
        uint64_t step_trace_remaining = 0;
        const char *watched_pc_text = std::getenv("MULTIARCADERECOMP_ZN_TRACE_PC");
        const uint32_t watched_pc = watched_pc_text
            ? static_cast<uint32_t>(std::strtoul(watched_pc_text, nullptr, 0))
            : 0xffffffffu;
        const char *watched_write_text = std::getenv("MULTIARCADERECOMP_ZN_TRACE_WRITE");
        const uint32_t watched_write_address = watched_write_text
            ? static_cast<uint32_t>(std::strtoul(watched_write_text, nullptr, 0))
            : 0xffffffffu;
        for (uint64_t i = 0; i < limit; ++i) {
            const uint32_t fault_pc = machine.cpu().pc;
            uint32_t opcode = 0;
            (void)machine.memory().read(fault_pc, 4, &opcode);
            const auto &r = machine.cpu().r;
            if (fault_pc == step_trace_pc && step_trace_remaining == 0)
                step_trace_remaining = step_trace_limit;
            if (step_trace_remaining) {
                std::cerr << "step pc=0x" << std::hex << fault_pc << " op=0x" << opcode;
                for (unsigned reg = 1; reg < 32; ++reg)
                    std::cerr << " r" << std::dec << reg << "=0x" << std::hex << r[reg];
                std::cerr << " hi=0x" << machine.cpu().hi << " lo=0x" << machine.cpu().lo;
                uint32_t root_counter2 = 0;
                (void)machine.memory().read(0x1f801120, 4, &root_counter2);
                std::cerr << " cycles=" << std::dec << machine.cpu().cycles
                          << " rcnt2=0x" << std::hex << root_counter2;
                uint32_t sio_status = 0, sio_control = 0, sio_baud = 0;
                (void)machine.memory().read(0x1f801044, 4, &sio_status);
                (void)machine.memory().read(0x1f801048, 4, &sio_control);
                (void)machine.memory().read(0x1f80104c, 4, &sio_baud);
                std::cerr << " sio_status=0x" << sio_status
                          << " sio_control_mode=0x" << sio_control
                          << " sio_baud=0x" << sio_baud;
                std::cerr << std::dec << '\n';
                if (fault_pc == step_trace_pc) {
                    uint32_t value = 0;
                    if (machine.memory().read(0xfffe0130, 4, &value))
                        std::cerr << "  BIU=0x" << std::hex << value;
                    for (uint32_t address = 0x1f800000; address < 0x1f800040; address += 4) {
                        if (machine.memory().read(address, 4, &value))
                            std::cerr << " [" << address << "]=0x" << value;
                    }
                    std::cerr << " timers:";
                    for (uint32_t address = 0x1f801100; address <= 0x1f801128; address += 4) {
                        if (machine.memory().read(address, 4, &value))
                            std::cerr << " [" << address << "]=0x" << value;
                    }
                    std::cerr << std::dec << '\n';
                }
                --step_trace_remaining;
            }
            if (fault_pc == watched_pc && watched_pc_lines++ < 128) {
                uint32_t root_counter2 = 0;
                (void)machine.memory().read(0x1f801120, 4, &root_counter2);
                uint32_t irq_status = 0, irq_mask = 0;
                (void)machine.memory().read(0x1f801070, 4, &irq_status);
                (void)machine.memory().read(0x1f801074, 4, &irq_mask);
                uint32_t event_words[4]{};
                for (unsigned i = 0; i < 4; ++i)
                    (void)machine.memory().read(r[3] + i * 4, 4, &event_words[i]);
                std::cerr << "watched pc=0x" << std::hex << fault_pc << " op=0x" << opcode
                          << " v0=0x" << r[2] << " v1=0x" << r[3]
                          << " a0=0x" << r[4] << " a1=0x" << r[5]
                          << " s0=0x" << r[16] << " s1=0x" << r[17]
                          << " s2=0x" << r[18] << " s3=0x" << r[19]
                          << " s4=0x" << r[20] << " sp=0x" << r[29]
                          << " ra=0x" << r[31]
                          << " cycles=" << std::dec << machine.cpu().cycles
                          << " rcnt2=0x" << std::hex << root_counter2
                          << " irq=0x" << irq_status << " irq_mask=0x" << irq_mask
                          << " event=[0x" << event_words[0] << ",0x" << event_words[1]
                          << ",0x" << event_words[2] << ",0x" << event_words[3] << "]"
                          << std::dec << '\n';
                if (fault_pc == 0xbfc0417c) {
                    const uint32_t length = std::min<uint32_t>(r[5], 64);
                    std::cerr << "compare bytes at 0x" << std::hex << (r[4] + 1) << ":";
                    for (uint32_t i = 0; i < length; ++i) {
                        uint32_t byte = 0;
                        (void)machine.memory().read(r[4] + 1 + i, 1, &byte);
                        std::cerr << " " << std::setw(2) << std::setfill('0') << byte;
                    }
                    std::cerr << std::setfill(' ') << std::dec << '\n';
                }
            }
            uint32_t saved_ra = 0;
            (void)machine.memory().read(r[29] + 0x14, 4, &saved_ra);
            history[i % history.size()] = {fault_pc, opcode, r[8], r[9], r[10], r[11], r[16], r[17], r[29], r[31], saved_ra};
            uint32_t watched_write_before = 0;
            if (fault_pc != watched_write_address && watched_write_address != 0xffffffffu)
                (void)machine.memory().read(watched_write_address, 4, &watched_write_before);
            const bool trace_low = fault_pc >= 0x100 && fault_pc < 0x140 && low_trace_lines < 32;
            const bool trace_boot = fault_pc >= 0x1000 && fault_pc < 0x1040 && boot_trace_lines < 32;
            if (std::getenv("MULTIARCADERECOMP_ZN_TRACE_BOOT") &&
                (i < 100 || trace_low || trace_boot)) {
                if (trace_low) ++low_trace_lines;
                if (trace_boot) ++boot_trace_lines;
                std::cerr << "boot pc=" << std::hex << fault_pc << " op=" << opcode
                          << " sp=" << r[29] << " ra=" << r[31]
                          << " stack+14=" << saved_ra << std::dec << '\n';
            }
            if (std::getenv("MULTIARCADERECOMP_ZN_TRACE_BOOT") &&
                (fault_pc == 0x100 || fault_pc == 0x1000) && i >= history.size()) {
                std::cerr << "entry trace to RAM 0x" << std::hex << fault_pc << ":\n";
                for (size_t j = 0; j < history.size(); ++j) {
                    const auto &t = history[(i + 1 + j) % history.size()];
                    std::cerr << "  pc=" << std::hex << t.pc << " op=" << t.op
                              << " sp=" << t.sp << " ra=" << t.ra
                              << " stack-ra=" << t.stack_ra << std::dec << '\n';
                }
            }
            const int step_result = machine.step();
            if (watched_write_address != 0xffffffffu && fault_pc != watched_write_address &&
                watched_write_lines < 100) {
                uint32_t after = 0;
                if (machine.memory().read(watched_write_address, 4, &after) && after != watched_write_before) {
                    ++watched_write_lines;
                    std::cerr << "watched write address=0x" << std::hex << watched_write_address
                              << " pc=0x" << fault_pc << " op=0x" << opcode
                              << " old=0x" << watched_write_before << " new=0x" << after
                              << std::dec << '\n';
                }
            }
            if (step_result == PSX_AOT_EXCEPTION && exceptions++ < 24) {
                const auto &cpu = machine.cpu();
                std::cerr << "exception pc=0x" << std::hex << fault_pc
                          << " opcode=0x" << opcode << " vector=0x" << cpu.pc
                          << " epc=0x" << cpu.cp0[14]
                          << " cause=0x" << cpu.cp0[13]
                          << " status=0x" << cpu.cp0[12] << std::dec << '\n';
                if (exceptions == 1) {
                    for (size_t j = 0; j < history.size(); ++j) {
                        const auto &t = history[(i + 1 + j) % history.size()];
                        std::cerr << "  pc=" << std::hex << t.pc << " op=" << t.op
                                  << " t0=" << t.t0 << " t1=" << t.t1 << " t2=" << t.t2
                                  << " t3=" << t.t3 << " s0=" << t.s0 << " s1=" << t.s1
                                  << " sp=" << t.sp << " ra=" << t.ra
                                  << " stack-ra=" << t.stack_ra << std::dec << '\n';
                    }
                }
            }
        }
        const auto &cpu = machine.cpu();
        const auto &coverage = machine.counters();
        std::cout << "instructions=" << cpu.instructions << " cycles=" << cpu.cycles
                  << " exceptions=" << exceptions << " pc=0x" << std::hex << cpu.pc
                  << " epc=0x" << cpu.cp0[14] << " cause=0x" << cpu.cp0[13]
                  << " status=0x" << cpu.cp0[12] << std::dec
                  << " AOT=" << coverage.aot_instructions
                  << " fallback=" << coverage.fallback_instructions
                  << " missing_fetches=" << coverage.missing_fetches << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
