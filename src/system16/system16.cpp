#include "system16.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <iostream>
#include <memory>
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
    const std::filesystem::path& zip_path, std::span<const RomRecord> requested,
    const std::unordered_map<std::string, std::vector<std::string>>& family_aliases) {
    std::unordered_map<std::string, std::vector<std::uint8_t>> contents;
    std::unordered_map<std::string, std::vector<std::string>> aliases;
    std::unordered_map<std::string, const RomRecord*> records;
    for (const auto& rom : requested) {
        contents.emplace(std::string(rom.filename), std::vector<std::uint8_t>{});
        records.emplace(std::string(rom.filename), &rom);
        if (!rom.merge_filename.empty())
            aliases[std::string(rom.merge_filename)].emplace_back(rom.filename);
    }
    for (const auto& [alias, targets] : family_aliases) {
        auto& entries = aliases[alias];
        for (const auto& target : targets)
            if (std::find(entries.begin(), entries.end(), target) == entries.end()) entries.push_back(target);
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
            if (bytes.size() != expected_size) continue;
            const auto actual_crc = static_cast<std::uint32_t>(crc32(0L, bytes.data(), static_cast<uInt>(bytes.size())));
            if (actual_crc != rom->crc32) continue;
            contents[target] = bytes;
        }
    }

    if (result != ARCHIVE_EOF) {
        const char* error = archive_error_string(reader.get());
        throw std::runtime_error("error while reading ROM set: " + std::string(error ? error : "unknown archive error"));
    }

    return contents;
}

std::string family_root(std::string id) {
    for (std::size_t depth = 0; depth < 32; ++depth) {
        const auto* profile = find_game_profile(id);
        if (!profile || profile->parent_id.empty()) return id;
        id = std::string(profile->parent_id);
    }
    throw std::runtime_error("cycle in ROM-set parent chain");
}

template <typename RomRecord>
std::unordered_map<std::string, std::vector<std::uint8_t>> read_zip_entries(
    const std::filesystem::path& zip_path, std::span<const RomRecord> requested,
    std::string_view game_id, std::string_view parent_id) {
    const auto root = family_root(std::string(game_id));
    std::unordered_map<std::string, std::vector<std::string>> family_aliases;
    const auto collect_aliases = [&](auto sibling_roms) {
        for (const auto& wanted : requested) {
            for (const auto& sibling : sibling_roms) {
                if (rom_size(wanted) == rom_size(sibling) && wanted.crc32 == sibling.crc32
                    && wanted.filename != sibling.filename) {
                    auto& names = family_aliases[std::string(sibling.filename)];
                    const std::string target(wanted.filename);
                    if (std::find(names.begin(), names.end(), target) == names.end()) names.push_back(target);
                }
            }
        }
    };
    for (const auto& sibling : game_profiles()) {
        if (sibling.id == game_id || family_root(std::string(sibling.id)) != root) continue;
        if constexpr (std::is_same_v<RomRecord, RomLoad16Byte>) {
            collect_aliases(sibling.maincpu_roms);
        } else {
            collect_aliases(sibling.tile_roms);
            collect_aliases(sibling.sprite_roms);
            collect_aliases(sibling.soundcpu_roms);
            collect_aliases(sibling.upd7751_roms);
            collect_aliases(sibling.upd7751_data_roms);
            collect_aliases(sibling.mcu_roms);
        }
    }
    auto contents = read_one_zip(zip_path, requested, family_aliases);
    std::vector<std::filesystem::path> candidates;
    std::unordered_set<std::string> seen{std::filesystem::weakly_canonical(zip_path).string()};
    std::string parent(parent_id);
    for (std::size_t depth = 0; !parent.empty() && depth < 32; ++depth) {
        const auto path = zip_path.parent_path() / (parent + ".zip");
        if (seen.insert(std::filesystem::weakly_canonical(path).string()).second)
            candidates.push_back(path);
        const auto* profile = find_game_profile(parent);
        if (!profile) break;
        parent = std::string(profile->parent_id);
    }

    for (const auto& candidate : game_profiles()) {
        if (candidate.id == game_id || family_root(std::string(candidate.id)) != root) continue;
        const auto path = zip_path.parent_path() / (std::string(candidate.id) + ".zip");
        if (seen.insert(std::filesystem::weakly_canonical(path).string()).second)
            candidates.push_back(path);
    }

    for (const auto& path : candidates) {
        std::vector<RomRecord> missing;
        for (const auto& rom : requested) {
            const auto found = contents.find(std::string(rom.filename));
            if (found != contents.end() && found->second.empty()) missing.push_back(rom);
        }
        if (missing.empty()) break;
        const auto found_entries = read_one_zip(path, std::span<const RomRecord>(missing.data(), missing.size()), family_aliases);
        for (const auto& rom : missing) {
            const std::string filename(rom.filename);
            const auto found = found_entries.find(filename);
            if (found != found_entries.end() && !found->second.empty()) {
                contents[filename] = found->second;
                std::cout << "ROM fallback: " << filename << " loaded from related set " << path.stem().string() << '\n';
            }
        }
    }
    return contents;
}

} // namespace

const char* board_family_name(BoardFamily family) {
    switch (family) {
    case BoardFamily::System16A: return "System 16A";
    case BoardFamily::System16B: return "System 16B";
    case BoardFamily::System18: return "System 18";
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
        if (crc != rom.crc32) {
            throw std::runtime_error(std::string("CRC32 mismatch for ") + std::string(rom.filename));
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
            if (crc != load.crc32) {
                throw std::runtime_error("CRC32 mismatch for " + filename);
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

System16GraphicsRoms load_graphics_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    return System16GraphicsRoms{
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.tile_region_size, profile.tile_roms),
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.sprite_region_size, profile.sprite_roms),
    };
}

System16ASoundRoms load_system16a_sound_roms(const GameProfile& profile, const std::filesystem::path& zip_path) {
    if (profile.upd7751_roms.empty() || profile.upd7751_data_roms.empty())
        throw std::runtime_error("game profile has no System 16A sound ROM layout");
    return System16ASoundRoms{
        load_soundcpu_roms(profile, zip_path),
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.upd7751_region_size, profile.upd7751_roms),
        load_rom_region(zip_path, profile.parent_id, profile.id, profile.upd7751_data_region_size, profile.upd7751_data_roms),
    };
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

} // namespace mamerecomp
