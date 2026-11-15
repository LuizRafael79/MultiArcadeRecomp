#include "interpreter_trace.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {
struct SiteKey {
    const char *cpu{};
    const char *backend{};
    MultiArcadeRecompFallbackReason reason{};
    std::uint32_t pc{};
    std::uint64_t opcode{};
    unsigned bits{};

    bool operator==(const SiteKey& other) const noexcept {
        return cpu == other.cpu && backend == other.backend &&
            reason == other.reason && pc == other.pc &&
            opcode == other.opcode && bits == other.bits;
    }
};

std::mutex s_mutex;
struct SiteHash {
    std::size_t operator()(const SiteKey& key) const noexcept {
        // CPU/backend labels are static strings at all call sites. Hash their
        // addresses instead of rescanning both strings through std::_Hash_bytes
        // for every interpreted instruction.
        std::uint64_t value =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.cpu) >> 3);
        const auto mix = [&value](std::uint64_t part) {
            value ^= part + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        };
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.backend) >> 3));
        mix(static_cast<unsigned>(key.reason));
        mix(key.pc);
        mix(key.opcode);
        mix(key.bits);
        return static_cast<std::size_t>(value);
    }
};
using SiteCounts = std::unordered_map<SiteKey, std::uint64_t, SiteHash>;
struct ThreadSites { SiteCounts counts; };
std::vector<ThreadSites*> s_thread_sites;
thread_local ThreadSites* t_thread_sites{};
std::string s_step_path;
std::string s_summary_path;
std::FILE *s_step_file{};
std::uint64_t s_step_lines{};
std::uint64_t s_step_limit{1'000'000};
bool s_step_limit_initialized{};
bool s_atexit_registered{};
bool s_flushed{};
std::atomic<bool> s_step_enabled{};
std::atomic<bool> s_step_env_checked{};
std::atomic<bool> s_profile_enabled{};

const char *nonempty(const char *value, const char *fallback) {
    return value && *value ? value : fallback;
}

const char *reason_name(MultiArcadeRecompFallbackReason reason) {
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
    std::atexit(multiarcaderecomp_interpreter_trace_flush);
    s_atexit_registered = true;
}

void open_step_log() {
    if (!s_step_path.empty() && !s_step_file)
        s_step_file = std::fopen(s_step_path.c_str(), "w");
    if (!s_step_limit_initialized) {
        s_step_limit_initialized = true;
        if (const char *limit = std::getenv("MULTIARCADERECOMP_INTERPRETER_STEP_LIMIT")) {
            char *end = nullptr;
            const auto parsed = std::strtoull(limit, &end, 0);
            if (end != limit && *end == '\0') s_step_limit = parsed;
        }
    }
    s_step_enabled.store(s_step_file != nullptr, std::memory_order_release);
}

SiteCounts collect_sites() {
    SiteCounts sites;
    for (const auto* thread_sites : s_thread_sites) {
        for (const auto& [key, count] : thread_sites->counts)
            sites[key] += count;
    }
    return sites;
}

void print_summary(std::FILE *out, const SiteCounts& sites) {
    if (!out) return;
    std::map<std::tuple<std::string_view, std::string_view,
                        MultiArcadeRecompFallbackReason>, std::uint64_t> totals;
    std::vector<std::pair<SiteKey, std::uint64_t>> ranked(sites.begin(), sites.end());
    for (const auto &[key, count] : sites) {
        totals[{key.cpu, key.backend, key.reason}] += count;
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    std::fprintf(out, "[interpreter] unique-sites=%zu detailed-steps=%llu\n",
        sites.size(), static_cast<unsigned long long>(s_step_lines));
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
        const std::string_view cpu(key.cpu);
        const std::string_view backend(key.backend);
        std::fprintf(out,
            "[interpreter-site] CPU=%.*s BACKEND=%.*s CATEGORY=%s PC=%08X OPCODE=%0*llX COUNT=%llu\n",
            static_cast<int>(cpu.size()), cpu.data(),
            static_cast<int>(backend.size()), backend.data(),
            reason_name(key.reason), key.pc,
            static_cast<int>((key.bits + 3) / 4),
            static_cast<unsigned long long>(key.opcode),
            static_cast<unsigned long long>(count));
    }
    if (ranked.size() > site_count)
        std::fprintf(out, "[interpreter-site] ... %zu lower-frequency sites omitted\n",
            ranked.size() - site_count);
}
} // namespace

extern "C" const char *multiarcaderecomp_fallback_reason_name(
    MultiArcadeRecompFallbackReason reason) {
    return reason_name(reason);
}

extern "C" void multiarcaderecomp_interpreter_trace_set_step_log(const char *path) {
    std::lock_guard lock(s_mutex);
    register_exit_flush();
    if (s_step_file) {
        std::fclose(s_step_file);
        s_step_file = nullptr;
    }
    s_step_path = path && *path ? path : "";
    s_step_env_checked.store(path && *path, std::memory_order_release);
    open_step_log();
    if (s_step_file) s_profile_enabled.store(true, std::memory_order_release);
}

extern "C" void multiarcaderecomp_interpreter_trace_set_summary_log(const char *path) {
    std::lock_guard lock(s_mutex);
    register_exit_flush();
    s_summary_path = path && *path ? path : "";
    if (!s_summary_path.empty())
        s_profile_enabled.store(true, std::memory_order_release);
}

extern "C" void multiarcaderecomp_interpreter_trace_set_profile_enabled(int enabled) {
    s_profile_enabled.store(enabled != 0, std::memory_order_release);
    if (enabled) {
        std::lock_guard lock(s_mutex);
        register_exit_flush();
    }
}

extern "C" int multiarcaderecomp_interpreter_trace_steps_enabled(void) {
    std::lock_guard lock(s_mutex);
    if (s_step_path.empty()) {
        const char *env_path = std::getenv("MULTIARCADERECOMP_INTERPRETER_STEP_LOG");
        if (env_path && *env_path) s_step_path = env_path;
    }
    open_step_log();
    return s_step_file != nullptr;
}

extern "C" void multiarcaderecomp_interpreter_trace_step(const char *cpu,
    const char *backend, MultiArcadeRecompFallbackReason reason, std::uint32_t pc,
    std::uint64_t opcode, unsigned opcode_bits, std::uint64_t cycles_before,
    std::uint64_t cycles_after, std::uint32_t next_pc,
    const char *instruction) {
    if (!s_profile_enabled.load(std::memory_order_relaxed)) return;
    if (!t_thread_sites) {
        std::lock_guard lock(s_mutex);
        if (!t_thread_sites) {
            t_thread_sites = new ThreadSites;
            t_thread_sites->counts.reserve(256);
            s_thread_sites.push_back(t_thread_sites);
            register_exit_flush();
        }
    }
    const char *cpu_name = nonempty(cpu, "UNKNOWN_CPU");
    const char *backend_name = nonempty(backend, "UNKNOWN_INTERPRETER");
    ++t_thread_sites->counts[{cpu_name, backend_name, reason, pc,
                              opcode, opcode_bits}];

    if (s_step_enabled.load(std::memory_order_acquire)) {
        std::lock_guard lock(s_mutex);
        const char *category = reason_name(reason);
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
        return;
    }

    if (!s_step_env_checked.load(std::memory_order_acquire) &&
        !s_step_env_checked.exchange(true, std::memory_order_acq_rel)) {
        std::lock_guard lock(s_mutex);
        if (s_step_path.empty()) {
            const char *env_path = std::getenv("MULTIARCADERECOMP_INTERPRETER_STEP_LOG");
            if (env_path && *env_path) s_step_path = env_path;
        }
        open_step_log();
    }
}

extern "C" void multiarcaderecomp_interpreter_trace_event(const char *cpu,
    const char *backend, MultiArcadeRecompFallbackReason reason, std::uint32_t pc,
    std::uint32_t next_pc, std::uint64_t cycles_before,
    std::uint64_t cycles_after) {
    multiarcaderecomp_interpreter_trace_step(cpu, backend, reason, pc, 0, 0,
        cycles_before, cycles_after, next_pc, "<dispatch-event>");
}

extern "C" void multiarcaderecomp_interpreter_trace_flush(void) {
    std::lock_guard lock(s_mutex);
    if (s_flushed) return;
    s_flushed = true;
    if (s_step_file) {
        std::fflush(s_step_file);
        std::fclose(s_step_file);
        s_step_file = nullptr;
    }
    const auto sites = collect_sites();
    if (s_summary_path.empty()) {
        print_summary(stderr, sites);
    } else {
        std::FILE *summary = std::fopen(s_summary_path.c_str(), "w");
        if (!summary) {
            std::fprintf(stderr, "[interpreter] cannot write summary %s\n",
                s_summary_path.c_str());
            print_summary(stderr, sites);
        } else {
            print_summary(summary, sites);
            std::fclose(summary);
            std::fprintf(stderr, "[interpreter] wrote categorized fallback log %s\n",
                s_summary_path.c_str());
        }
    }
}
