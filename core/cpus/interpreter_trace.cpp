#include "interpreter_trace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
using SiteKey = std::tuple<std::string_view, std::string_view,
                           MamerecompFallbackReason,
                           std::uint32_t, std::uint64_t, unsigned>;

std::mutex s_mutex;
std::map<SiteKey, std::uint64_t> s_sites;
std::string s_step_path;
std::string s_summary_path;
std::FILE *s_step_file{};
std::uint64_t s_step_lines{};
std::uint64_t s_step_limit{1'000'000};
bool s_step_limit_initialized{};
bool s_atexit_registered{};
bool s_flushed{};

const char *nonempty(const char *value, const char *fallback) {
    return value && *value ? value : fallback;
}

const char *reason_name(MamerecompFallbackReason reason) {
    static constexpr const char *names[] = {
        "UNSUPPORTED_OPCODE",
        "UNSUPPORTED_VARIANT",
        "UNCOVERED_ENTRY",
        "DYNAMIC_CODE",
        "MEMORY_ACCESS",
        "EXCEPTION_BOUNDARY",
        "NATIVE_DIVERGENCE",
    };
    const auto index = static_cast<unsigned>(reason);
    return index < std::size(names) ? names[index] : "UNKNOWN_REASON";
}

void register_exit_flush() {
    if (s_atexit_registered) return;
    std::atexit(mamerecomp_interpreter_trace_flush);
    s_atexit_registered = true;
}

void open_step_log() {
    if (!s_step_path.empty() && !s_step_file)
        s_step_file = std::fopen(s_step_path.c_str(), "w");
    if (!s_step_limit_initialized) {
        s_step_limit_initialized = true;
        if (const char *limit = std::getenv("MAMERECOMP_INTERPRETER_STEP_LIMIT")) {
            char *end = nullptr;
            const auto parsed = std::strtoull(limit, &end, 0);
            if (end != limit && *end == '\0') s_step_limit = parsed;
        }
    }
}

void print_summary(std::FILE *out) {
    if (!out) return;
    std::map<std::tuple<std::string_view, std::string_view,
                        MamerecompFallbackReason>, std::uint64_t> totals;
    std::vector<std::pair<SiteKey, std::uint64_t>> ranked(s_sites.begin(), s_sites.end());
    for (const auto &[key, count] : s_sites) {
        const auto &[cpu, backend, reason, pc, opcode, bits] = key;
        (void)pc;
        (void)opcode;
        (void)bits;
        totals[{cpu, backend, reason}] += count;
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    std::fprintf(out, "[interpreter] unique-sites=%zu detailed-steps=%llu\n",
        s_sites.size(), static_cast<unsigned long long>(s_step_lines));
    for (const auto &[key, count] : totals) {
        const auto &[cpu, backend, reason] = key;
        const auto *category = reason_name(reason);
        std::fprintf(out, "[interpreter-total] CPU=%.*s BACKEND=%.*s CATEGORY=%s COUNT=%llu\n",
            static_cast<int>(cpu.size()), cpu.data(),
            static_cast<int>(backend.size()), backend.data(), category,
            static_cast<unsigned long long>(count));
    }
    const auto site_count = std::min<std::size_t>(ranked.size(), 256);
    for (std::size_t i = 0; i < site_count; ++i) {
        const auto &[key, count] = ranked[i];
        const auto &[cpu, backend, reason, pc, opcode, bits] = key;
        std::fprintf(out,
            "[interpreter-site] CPU=%.*s BACKEND=%.*s CATEGORY=%s PC=%08X OPCODE=%0*llX COUNT=%llu\n",
            static_cast<int>(cpu.size()), cpu.data(),
            static_cast<int>(backend.size()), backend.data(),
            reason_name(reason), pc,
            static_cast<int>((bits + 3) / 4),
            static_cast<unsigned long long>(opcode),
            static_cast<unsigned long long>(count));
    }
    if (ranked.size() > site_count)
        std::fprintf(out, "[interpreter-site] ... %zu lower-frequency sites omitted\n",
            ranked.size() - site_count);
}
} // namespace

extern "C" const char *mamerecomp_fallback_reason_name(
    MamerecompFallbackReason reason) {
    return reason_name(reason);
}

extern "C" void mamerecomp_interpreter_trace_set_step_log(const char *path) {
    std::lock_guard lock(s_mutex);
    register_exit_flush();
    if (s_step_file) {
        std::fclose(s_step_file);
        s_step_file = nullptr;
    }
    s_step_path = path && *path ? path : "";
    open_step_log();
}

extern "C" void mamerecomp_interpreter_trace_set_summary_log(const char *path) {
    std::lock_guard lock(s_mutex);
    register_exit_flush();
    s_summary_path = path && *path ? path : "";
}

extern "C" int mamerecomp_interpreter_trace_steps_enabled(void) {
    std::lock_guard lock(s_mutex);
    if (s_step_path.empty()) {
        const char *env_path = std::getenv("MAMERECOMP_INTERPRETER_STEP_LOG");
        if (env_path && *env_path) s_step_path = env_path;
    }
    open_step_log();
    return s_step_file != nullptr;
}

extern "C" void mamerecomp_interpreter_trace_step(const char *cpu,
    const char *backend, MamerecompFallbackReason reason, std::uint32_t pc,
    std::uint64_t opcode, unsigned opcode_bits, std::uint64_t cycles_before,
    std::uint64_t cycles_after, std::uint32_t next_pc,
    const char *instruction) {
    std::lock_guard lock(s_mutex);
    register_exit_flush();
    const char *cpu_name = nonempty(cpu, "UNKNOWN_CPU");
    const char *backend_name = nonempty(backend, "UNKNOWN_INTERPRETER");
    const char *category = reason_name(reason);
    ++s_sites[{cpu_name, backend_name, reason, pc, opcode, opcode_bits}];

    if (s_step_path.empty()) {
        const char *env_path = std::getenv("MAMERECOMP_INTERPRETER_STEP_LOG");
        if (env_path && *env_path) s_step_path = env_path;
    }
    open_step_log();
    if (!s_step_file || s_step_lines >= s_step_limit) return;
    std::fprintf(s_step_file,
        "CPU=%s BACKEND=%s CATEGORY=%s PC=%08X OPCODE=%0*llX CYCLES=%llu DELTA=%llu NEXT=%08X INSN=%s\n",
        cpu_name, backend_name, category, pc,
        static_cast<int>((opcode_bits + 3) / 4),
        static_cast<unsigned long long>(opcode),
        static_cast<unsigned long long>(cycles_before),
        static_cast<unsigned long long>(cycles_after - cycles_before), next_pc,
        nonempty(instruction, "?"));
    ++s_step_lines;
}

extern "C" void mamerecomp_interpreter_trace_event(const char *cpu,
    const char *backend, MamerecompFallbackReason reason, std::uint32_t pc,
    std::uint32_t next_pc, std::uint64_t cycles_before,
    std::uint64_t cycles_after) {
    mamerecomp_interpreter_trace_step(cpu, backend, reason, pc, 0, 0,
        cycles_before, cycles_after, next_pc, "<dispatch-event>");
}

extern "C" void mamerecomp_interpreter_trace_flush(void) {
    std::lock_guard lock(s_mutex);
    if (s_flushed) return;
    s_flushed = true;
    if (s_step_file) {
        std::fflush(s_step_file);
        std::fclose(s_step_file);
        s_step_file = nullptr;
    }
    if (s_summary_path.empty()) {
        print_summary(stderr);
    } else {
        std::FILE *summary = std::fopen(s_summary_path.c_str(), "w");
        if (!summary) {
            std::fprintf(stderr, "[interpreter] cannot write summary %s\n",
                s_summary_path.c_str());
            print_summary(stderr);
        } else {
            print_summary(summary);
            std::fclose(summary);
            std::fprintf(stderr, "[interpreter] wrote categorized fallback log %s\n",
                s_summary_path.c_str());
        }
    }
}
