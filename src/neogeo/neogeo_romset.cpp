#include "neogeo_romset.hpp"
#include "kof98_romset_data.hpp"

extern "C" void mamerecomp_kof98_decrypt(std::uint8_t* program, std::size_t size);

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// KOF98 PROGSF1 byte/word permutation follows MAME's BSD-3-Clause
// prot_kof98.cpp (device implementation, not linked at runtime).
namespace mamerecomp::neogeo {
namespace {
namespace data = romset_data;
struct ArchiveCloser { void operator()(archive* a) const { if (a) archive_read_free(a); } };
}

namespace {
std::unordered_map<std::string, std::vector<std::uint8_t>> read_roms(
    const std::filesystem::path& path, std::span<const std::string_view> wanted_names) {
    std::unique_ptr<archive, ArchiveCloser> zip(archive_read_new());
    if (!zip) throw std::runtime_error("could not allocate Neo Geo ZIP reader");
    archive_read_support_filter_all(zip.get()); archive_read_support_format_zip(zip.get());
    if (archive_read_open_filename(zip.get(), path.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open Neo Geo ROM ZIP " + path.string());
    std::unordered_map<std::string, std::vector<std::uint8_t>> files;
    archive_entry* entry = nullptr;
    int result;
    while ((result = archive_read_next_header(zip.get(), &entry)) == ARCHIVE_OK) {
        const auto name = std::string_view(archive_entry_pathname(entry));
        bool wanted = false; for (const auto wanted_name : wanted_names) wanted |= name == wanted_name;
        if (!wanted) { archive_read_data_skip(zip.get()); continue; }
        const auto size = archive_entry_size(entry);
        if (size < 0 || size > 0x1000000) throw std::runtime_error("invalid size for Neo Geo ROM " + std::string(name));
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto n = archive_read_data(zip.get(), bytes.data() + done, bytes.size() - done);
            if (n <= 0) throw std::runtime_error("truncated Neo Geo ROM " + std::string(name));
            done += static_cast<std::size_t>(n);
        }
        files.emplace(std::string(name), std::move(bytes));
    }
    if (result != ARCHIVE_EOF) throw std::runtime_error("error reading Neo Geo ROM ZIP " + path.string());
    return files;
}

template <typename Entries>
std::vector<std::string_view> member_names(const Entries& entries) {
    std::vector<std::string_view> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) names.emplace_back(entry.name);
    return names;
}
}

void decrypt_kof98_program(std::vector<std::uint8_t>& program) {
    if (program.size() < 0x600000) throw std::runtime_error("KOF98 program region must be at least 0x600000 bytes");
    mamerecomp_kof98_decrypt(program.data(), program.size());
}

RomSetInfo load_kof98(const std::filesystem::path& path, const std::filesystem::path& bios_path) {
    const auto load_names = member_names(data::loads);
    auto files = read_roms(path, load_names);

    const auto maincpu_region = std::find_if(data::regions.begin(), data::regions.end(),
        [](const auto& region) { return std::string_view(region.name) == "maincpu"; });
    if (maincpu_region == data::regions.end()) throw std::runtime_error("KOF98 runtime manifest has no maincpu region");
    RomSetInfo out{"kof98", files.size(), {}, std::vector<std::uint8_t>(maincpu_region->size, 0xff), {}, {}};
    for (const auto& region : data::regions) out.regions.push_back({region.name, region.size, 0, 0});
    for (const auto& load : data::loads) {
        const auto it = files.find(load.name);
        if (it == files.end()) throw std::runtime_error("missing Neo Geo ROM " + std::string(load.name));
        const auto& bytes = it->second;
        if (bytes.size() != load.rom_size || load.source_offset + load.length > bytes.size())
            throw std::runtime_error("wrong size for Neo Geo ROM " + std::string(load.name));
        const auto crc = static_cast<std::uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
        if (crc != load.crc) throw std::runtime_error("CRC mismatch for Neo Geo ROM " + std::string(load.name));
        for (auto& region : out.regions) if (region.name == load.region) {
            ++region.loads; region.bytes_loaded += bytes.size(); break;
        }
        if (load.region == std::string_view("maincpu")) {
            if (!load.group_bytes || load.stride < load.group_bytes || load.length % load.group_bytes)
                throw std::runtime_error("invalid Neo Geo ROM load geometry for " + std::string(load.name));
            const auto end = load.offset + ((load.length - load.group_bytes) / load.group_bytes) * load.stride
                + load.group_bytes;
            if (end > out.maincpu.size()) throw std::runtime_error("Neo Geo ROM load exceeds maincpu region");
            for (std::size_t i = 0; i < load.length; ++i) {
                const auto source = load.source_offset + (load.word_swap ? (i ^ 1u) : i);
                const auto destination = load.offset + (i / load.group_bytes) * load.stride + (i % load.group_bytes);
                if (source < bytes.size() && source < load.source_offset + load.length)
                    out.maincpu[destination] = bytes[source];
            }
        }
    }
    decrypt_kof98_program(out.maincpu);

    const auto bios_names = member_names(data::bios_candidates);
    auto bioses = read_roms(bios_path, bios_names);
    for (const auto& candidate : data::bios_candidates) {
        const auto it = bioses.find(candidate.name);
        if (it == bioses.end()) continue;
        const auto crc = static_cast<std::uint32_t>(crc32(0, it->second.data(), static_cast<uInt>(it->second.size())));
        if (crc != candidate.crc) throw std::runtime_error("CRC mismatch for Neo Geo BIOS " + std::string(candidate.name));
        if (it->second.size() != candidate.size) continue; // MVS 0x80000 mask ROM variants are not selected yet.
        out.bios_filename = candidate.name;
        out.bios = std::move(it->second);
        if (candidate.word_swap)
            for (std::size_t i = 0; i + 1 < out.bios.size(); i += 2) std::swap(out.bios[i], out.bios[i + 1]);
        break;
    }
    if (out.bios.empty()) throw std::runtime_error("no supported 0x20000 MVS BIOS found in " + bios_path.string());
    return out;
}

void extract_kof98_inputs(const std::filesystem::path& game_zip,
                          const std::filesystem::path& bios_zip,
                          const std::filesystem::path& game_directory,
                          const std::filesystem::path& bios_directory) {
    (void)load_kof98(game_zip, bios_zip); // verify set, BIOS and decryption prerequisites first
    const auto game_names = member_names(data::loads);
    auto game_files=read_roms(game_zip, game_names);
    auto bios_names = member_names(data::bios_candidates);
    for (const auto& rom : data::system_roms) bios_names.emplace_back(rom.name);
    auto bios_files=read_roms(bios_zip,bios_names);
    for (const auto& rom : data::system_roms) {
        const auto it=bios_files.find(rom.name);
        if (it==bios_files.end()) throw std::runtime_error("missing Neo Geo system ROM " + std::string(rom.name));
        const auto crc=static_cast<std::uint32_t>(crc32(0,it->second.data(),static_cast<uInt>(it->second.size())));
        if (crc!=rom.crc) throw std::runtime_error("CRC mismatch for Neo Geo system ROM " + std::string(rom.name));
    }
    const auto ensure_directory=[](const std::filesystem::path& dir) {
        std::filesystem::create_directories(dir);
    };
    ensure_directory(game_directory); ensure_directory(bios_directory);
    const auto write_files=[](const std::filesystem::path& dir, const auto& files) {
        for (const auto& [name, bytes] : files) {
            std::ofstream output(dir/name, std::ios::binary);
            if (!output) throw std::runtime_error("could not extract Neo Geo ROM " + (dir/name).string());
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("failed writing extracted Neo Geo ROM " + (dir/name).string());
        }
    };
    write_files(game_directory,game_files);
    write_files(bios_directory,bios_files);
}
} // namespace mamerecomp::neogeo
