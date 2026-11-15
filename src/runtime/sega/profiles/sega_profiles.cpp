#include "sega_profiles.hpp"
#include "sha1.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mamerecomp {
namespace {

template <typename RomRecord>
std::size_t rom_size(const RomRecord& rom) {
    if constexpr (requires { rom.file_size; }) return rom.file_size;
    else return rom.size;
}










template <typename RomRecord>
std::unordered_map<std::string, std::vector<std::uint8_t>> read_one_zip(
    const std::filesystem::path& zip_path, std::span<const RomRecord> requested) {
    std::unordered_map<std::string, std::vector<std::uint8_t>> contents;
    std::unordered_map<std::string, std::vector<std::string>> aliases;
    std::unordered_map<std::string, const RomRecord*> records;
    for (const auto& rom : requested) {
        contents.emplace(std::string(rom.filename), std::vector<std::uint8_t>{});
        records.emplace(std::string(rom.filename), &rom);
        if (!rom.merge_filename.empty())
            aliases[std::string(rom.merge_filename)].emplace_back(rom.filename);
    }
    if (!std::filesystem::is_regular_file(zip_path)) return contents;

    using ArchivePtr = std::unique_ptr<struct archive, decltype(&archive_read_free)>;
    ArchivePtr reader(archive_read_new(), &archive_read_free);
    if (!reader) {
        throw std::runtime_error("could not allocate libarchive reader");
    }

    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip_path.c_str(), 10240) != ARCHIVE_OK) {
        const char* error = archive_error_string(reader.get());
        throw std::runtime_error("could not open ROM set: " + std::string(error ? error : "unknown archive error"));
    }

    struct archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* path = archive_entry_pathname(entry);
        const std::string filename = path ? std::filesystem::path(path).filename().string() : std::string{};
        std::vector<std::string> targets;
        if (contents.contains(filename)) targets.push_back(filename);
        if (const auto alias = aliases.find(filename); alias != aliases.end()) {
            for (const auto& target : alias->second)
                if (std::find(targets.begin(), targets.end(), target) == targets.end()) targets.push_back(target);
        }
        if (targets.empty()) {
            archive_read_data_skip(reader.get());
            continue;
        }

        std::vector<std::uint8_t> bytes;
        std::array<std::uint8_t, 8192> buffer{};
        for (;;) {
            const la_ssize_t count = archive_read_data(reader.get(), buffer.data(), buffer.size());
            if (count < 0) {
                const char* error = archive_error_string(reader.get());
                throw std::runtime_error("could not read " + filename + ": " + (error ? error : "unknown archive error"));
            }
            if (count == 0) {
                break;
            }
            bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
        }
        for (const auto& target : targets) {
            if (!contents[target].empty()) continue;
            const auto* rom = records.at(target);
            const std::size_t expected_size = rom_size(*rom);
            const auto actual_crc = static_cast<std::uint32_t>(crc32(0L, bytes.data(), static_cast<uInt>(bytes.size())));
            const auto actual_sha1 = mamerecomp::roms::sha1_hex(bytes);
            if (bytes.size() != expected_size || actual_crc != rom->crc32 ||
                (!rom->sha1.empty() && actual_sha1 != rom->sha1)) {
                if (filename == target) {
                    std::cerr << "ROM audit mismatch: set=" << zip_path.stem().string()
                              << " file=" << filename << " expected_size=" << expected_size
                              << " actual_size=" << bytes.size() << " expected_crc32=" << std::hex
                              << rom->crc32 << " actual_crc32=" << actual_crc << std::dec
                              << " expected_sha1=" << (rom->sha1.empty() ? "(not supplied)" : rom->sha1)
                              << " actual_sha1=" << actual_sha1 << '\n';
                }
                continue;
            }
            contents[target] = bytes;
        }
    }

    if (result != ARCHIVE_EOF) {
        const char* error = archive_error_string(reader.get());
        throw std::runtime_error("error while reading ROM set: " + std::string(error ? error : "unknown archive error"));
    }

    return contents;
}

template <typename RomRecord>
bool contains_all(std::span<const RomRecord> requested,
    const std::unordered_map<std::string, std::vector<std::uint8_t>>& entries) {
    return std::all_of(requested.begin(), requested.end(), [&](const auto& rom) {
        const auto found = entries.find(std::string(rom.filename));
        return found != entries.end() && !found->second.empty();
    });
}

template <typename RomRecord>
std::unordered_map<std::string, std::vector<std::uint8_t>> read_zip_entries(
    const std::filesystem::path& zip_path, std::span<const RomRecord> requested,
    std::string_view game_id, std::string_view parent_id) {
    std::vector<std::filesystem::path> candidates;
    std::unordered_set<std::string> seen;
    seen.insert(std::filesystem::weakly_canonical(zip_path).string());
    std::string parent(parent_id);
    for (std::size_t depth = 0; !parent.empty() && depth < 32; ++depth) {
        const auto path = zip_path.parent_path() / (parent + ".zip");
        if (seen.insert(std::filesystem::weakly_canonical(path).string()).second)
            candidates.push_back(path);
        const auto* profile = find_game_profile(parent);
        if (!profile) break;
        parent = std::string(profile->parent_id);
    }

    // MAME-style priority: complete clone archive (non-merged), complete
    // parent archive(s) (merged), then combine clone and parent archives (split).
    auto clone_entries = read_one_zip(zip_path, requested);
    if (contains_all(requested, clone_entries)) return clone_entries;

    std::vector<std::unordered_map<std::string, std::vector<std::uint8_t>>> parent_entries;
    parent_entries.reserve(candidates.size());
    for (const auto& path : candidates) {
        auto entries = read_one_zip(path, requested);
        if (contains_all(requested, entries)) {
            std::cout << "ROM set layout: merged (clone='" << game_id << "', archive='"
                      << path.stem().string() << "')\n";
            return entries;
        }
        parent_entries.emplace_back(std::move(entries));
    }

    auto combined = std::move(clone_entries);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        std::vector<RomRecord> missing;
        for (const auto& rom : requested) {
            const auto found = combined.find(std::string(rom.filename));
            if (found == combined.end() || found->second.empty()) missing.push_back(rom);
        }
        if (missing.empty()) break;
        const auto& parent = parent_entries[i];
        for (const auto& rom : missing) {
            const std::string filename(rom.filename);
            const auto found = parent.find(filename);
            if (found != parent.end() && !found->second.empty()) {
                combined[filename] = found->second;
                std::cout << "ROM fallback: " << filename << " loaded from parent set "
                          << candidates[i].stem().string() << '\n';
            }
        }
    }
    if (contains_all(requested, combined))
        std::cout << "ROM set layout: split (clone='" << game_id << "')\n";
    return combined;
}

} // namespace

const char* board_family_name(SegaBoardFamily family) {
    switch (family) {
    case SegaBoardFamily::System16A: return "System 16A";
    case SegaBoardFamily::System16B: return "System 16B";
    case SegaBoardFamily::System18: return "System 18";
    }
    return "unknown board family";
}

std::vector<std::uint8_t> load_maincpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    auto entries = read_zip_entries(zip_path, profile.maincpu_roms, profile.id, profile.parent_id);
    std::vector<std::uint8_t> maincpu(profile.maincpu_size, 0xff);

    for (const auto& rom : profile.maincpu_roms) {
        const auto found = entries.find(std::string(rom.filename));
        if (found == entries.end() || found->second.empty()) {
            throw std::runtime_error("missing program ROM: " + std::string(rom.filename));
        }
        const auto& bytes = found->second;
        if (bytes.size() != rom.size) {
            throw std::runtime_error(std::string(rom.filename) + " has unexpected size " + std::to_string(bytes.size()));
        }
        const uLong crc = crc32(0L, bytes.data(), static_cast<uInt>(bytes.size()));
        const auto sha1 = mamerecomp::roms::sha1_hex(bytes);
        if (crc != rom.crc32 || (!rom.sha1.empty() && sha1 != rom.sha1)) {
            throw std::runtime_error(std::string("ROM audit failed for ") + std::string(rom.filename)
                + ": CRC32 expected=" + [&] { std::ostringstream out; out << std::hex << rom.crc32; return out.str(); }()
                + " actual=" + [&] { std::ostringstream out; out << std::hex << crc; return out.str(); }()
                + " SHA-1 expected=" + (rom.sha1.empty() ? std::string("(not supplied)") : std::string(rom.sha1))
                + " actual=" + sha1);
        }
        if (rom.destination >= maincpu.size() || rom.destination + (bytes.size() - 1) * 2 >= maincpu.size()) {
            throw std::runtime_error(std::string(rom.filename) + " exceeds the main CPU region");
        }
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            maincpu[rom.destination + i * 2] = bytes[i];
        }
    }

    return maincpu;
}

namespace {

std::vector<std::uint8_t> load_rom_region(const std::filesystem::path& zip_path, std::string_view parent_id,
    std::string_view game_id, std::uint32_t region_size, std::span<const RomRegionLoad> loads, std::uint8_t fill = 0) {
    auto entries = read_zip_entries(zip_path, loads, game_id, parent_id);
    std::vector<std::uint8_t> region(region_size, fill);
    std::unordered_map<std::string, bool> verified;
    for (const auto& load : loads) {
        const std::string filename(load.filename);
        const auto found = entries.find(filename);
        if (found == entries.end() || found->second.empty()) {
            throw std::runtime_error("missing graphics ROM: " + filename);
        }
        const auto& bytes = found->second;
        if (bytes.size() != load.file_size) {
            throw std::runtime_error(filename + " has unexpected size " + std::to_string(bytes.size()));
        }
        if (!verified[filename]) {
            const uLong crc = crc32(0L, bytes.data(), static_cast<uInt>(bytes.size()));
            const auto sha1 = mamerecomp::roms::sha1_hex(bytes);
            if (crc != load.crc32 || (!load.sha1.empty() && sha1 != load.sha1)) {
                std::ostringstream message;
                message << "ROM audit failed for " << filename << ": CRC32 expected=" << std::hex << load.crc32
                        << " actual=" << crc << " SHA-1 expected="
                        << (load.sha1.empty() ? "(not supplied)" : std::string(load.sha1))
                        << " actual=" << sha1;
                throw std::runtime_error(message.str());
            }
            verified[filename] = true;
        }
        if (load.destination_stride == 0 || load.source_offset > bytes.size() ||
            load.length > bytes.size() - load.source_offset || load.destination >= region.size() ||
            (load.length != 0 && static_cast<std::uint64_t>(load.destination) +
                static_cast<std::uint64_t>(load.length - 1) * load.destination_stride >= region.size())) {
            throw std::runtime_error("invalid ROM load segment for " + filename);
        }
        for (std::uint32_t i = 0; i < load.length; ++i) {
            region[load.destination + i * load.destination_stride] = bytes[load.source_offset + i];
        }
    }
    return region;
}

} // namespace

SegaGraphicsRoms load_graphics_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    return SegaGraphicsRoms{
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.tile_region_size, profile.tile_roms),
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.sprite_region_size, profile.sprite_roms),
    };
}

std::vector<std::uint8_t> load_profile_rom_region(const GameProfile& profile,
    const std::filesystem::path& zip_path, std::uint32_t region_size,
    std::span<const RomRegionLoad> loads, std::uint8_t fill) {
    return load_rom_region(zip_path, profile.parent_id, profile.id, region_size, loads, fill);
}

std::vector<std::uint8_t> load_soundcpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    if (profile.soundcpu_region_size == 0 || profile.soundcpu_roms.empty())
        throw std::runtime_error("game profile has no sound CPU ROM layout");
    return load_rom_region(zip_path, profile.parent_id, profile.id, profile.soundcpu_region_size,
        profile.soundcpu_roms, profile.soundcpu_fill);
}

std::vector<std::uint8_t> load_mcu_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    if (profile.mcu_region_size == 0 || profile.mcu_roms.empty())
        throw std::runtime_error("game profile has no MCU ROM layout");
    return load_rom_region(zip_path, profile.parent_id, profile.id, profile.mcu_region_size, profile.mcu_roms);
}

std::vector<std::uint8_t> load_fd1094_key_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    if (profile.fd1094_key_region_size != 0x2000 || profile.fd1094_key_roms.empty())
        throw std::runtime_error("game profile has no complete FD1094 key ROM layout");
    return load_rom_region(zip_path, profile.parent_id, profile.id,
        profile.fd1094_key_region_size, profile.fd1094_key_roms);
}

std::vector<std::uint8_t> load_pld_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    if (profile.pld_region_size == 0 || profile.pld_roms.empty()) return {};
    return load_rom_region(zip_path, profile.parent_id, profile.id,
        profile.pld_region_size, profile.pld_roms);
}

SegaBoardFamily board_family(const GameProfile& profile) {
    if (std::holds_alternative<System16AConfiguration>(profile.board_configuration))
        return SegaBoardFamily::System16A;
    if (std::holds_alternative<System16BConfiguration>(profile.board_configuration))
        return SegaBoardFamily::System16B;
    return SegaBoardFamily::System18;
}

void validate_profile_sound_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    (void)load_soundcpu_roms(profile, zip_path);
    if (board_family(profile) == SegaBoardFamily::System16A) {
        if (profile.upd7751_roms.empty() || profile.upd7751_data_roms.empty())
            throw std::runtime_error("game profile has no System 16A sound ROM layout");
        (void)load_profile_rom_region(profile, zip_path, profile.upd7751_region_size,
            profile.upd7751_roms);
        (void)load_profile_rom_region(profile, zip_path, profile.upd7751_data_region_size,
            profile.upd7751_data_roms);
    }
}

} // namespace mamerecomp
