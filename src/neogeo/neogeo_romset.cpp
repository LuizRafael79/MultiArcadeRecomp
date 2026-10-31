#include "neogeo_romset.hpp"

extern "C" void mamerecomp_kof98_decrypt(std::uint8_t* program, std::size_t size);

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <array>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

// KOF98 PROGSF1 byte/word permutation follows MAME's BSD-3-Clause
// prot_kof98.cpp (device implementation, not linked at runtime).
namespace mamerecomp::neogeo {
namespace {
struct Load { const char* name; const char* region; std::uint32_t offset, size, crc; bool word_swap; };
constexpr Load kLoads[]{
    {"242-p1.p1", "maincpu", 0x000000, 0x200000, 0x8893df89, true},
    {"242-p2.sp2", "maincpu", 0x200000, 0x400000, 0x980aba4c, true},
    {"242-s1.s1", "fixed", 0, 0x020000, 0x7f7b4805, false},
    {"242-m1.m1", "audiocpu", 0, 0x040000, 0x4ef7016b, false},
    {"242-v1.v1", "adpcma", 0x000000, 0x400000, 0xb9ea8051, false},
    {"242-v2.v2", "adpcma", 0x400000, 0x400000, 0xcc11106e, false},
    {"242-v3.v3", "adpcma", 0x800000, 0x400000, 0x044ea4e1, false},
    {"242-v4.v4", "adpcma", 0xc00000, 0x400000, 0x7985ea30, false},
    {"242-c1.c1", "sprites", 0x0000000, 0x800000, 0xe564ecd6, false},
    {"242-c2.c2", "sprites", 0x0000001, 0x800000, 0xbd959b60, false},
    {"242-c3.c3", "sprites", 0x1000000, 0x800000, 0x22127b4f, false},
    {"242-c4.c4", "sprites", 0x1000001, 0x800000, 0x0b4fa044, false},
    {"242-c5.c5", "sprites", 0x2000000, 0x800000, 0x9d10bed3, false},
    {"242-c6.c6", "sprites", 0x2000001, 0x800000, 0xda07b6a2, false},
    {"242-c7.c7", "sprites", 0x3000000, 0x800000, 0xf6d7a38a, false},
    {"242-c8.c8", "sprites", 0x3000001, 0x800000, 0xc823e045, false},
};
struct Region { const char* name; std::uint32_t size; };
constexpr Region kRegions[]{{"maincpu",0x600000},{"fixed",0x20000},{"audiocpu",0x40000},
    {"adpcma",0x1000000},{"sprites",0x4000000}};
struct ArchiveCloser { void operator()(archive* a) const { if (a) archive_read_free(a); } };
}

namespace {
std::unordered_map<std::string, std::vector<std::uint8_t>> read_roms(
    const std::filesystem::path& path, std::span<const char* const> wanted_names) {
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
        bool wanted = false; for (const auto* wanted_name : wanted_names) wanted |= name == wanted_name;
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
}

void decrypt_kof98_program(std::vector<std::uint8_t>& program) {
    if (program.size() < 0x600000) throw std::runtime_error("KOF98 program region must be at least 0x600000 bytes");
    mamerecomp_kof98_decrypt(program.data(), program.size());
}

RomSetInfo load_kof98(const std::filesystem::path& path, const std::filesystem::path& bios_path) {
    std::array<const char*, std::size(kLoads)> load_names{};
    for (std::size_t i=0; i<std::size(kLoads); ++i) load_names[i]=kLoads[i].name;
    auto files = read_roms(path, load_names);

    RomSetInfo out{"kof98", files.size(), {}, std::vector<std::uint8_t>(0x600000, 0xff), {}, {}};
    for (const auto& region : kRegions) out.regions.push_back({region.name, region.size, 0, 0});
    for (const auto& load : kLoads) {
        const auto it = files.find(load.name);
        if (it == files.end()) throw std::runtime_error("missing Neo Geo ROM " + std::string(load.name));
        const auto& bytes = it->second;
        if (bytes.size() != load.size) throw std::runtime_error("wrong size for Neo Geo ROM " + std::string(load.name));
        const auto crc = static_cast<std::uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
        if (crc != load.crc) throw std::runtime_error("CRC mismatch for Neo Geo ROM " + std::string(load.name));
        for (auto& region : out.regions) if (region.name == load.region) {
            ++region.loads; region.bytes_loaded += bytes.size(); break;
        }
        if (load.region == std::string_view("maincpu")) {
            for (std::size_t i = 0; i < bytes.size(); ++i)
                out.maincpu[load.offset + i] = load.word_swap && (i ^ 1u) < bytes.size() ? bytes[i ^ 1u] : bytes[i];
        }
    }
    decrypt_kof98_program(out.maincpu);

    constexpr const char* bios_names[]{"sp-s2.sp1", "sp-s.sp1", "sp-e.sp1", "sp-u2.sp1",
        "sp-j2.sp1", "sp1-u2", "japan-j3.bin", "sp-1v1_3db8c.bin", "uni-bios_4_0.rom"};
    auto bioses = read_roms(bios_path, bios_names);
    constexpr std::pair<const char*, std::uint32_t> bios_crc[]{{"sp-s2.sp1",0x9036d879},
        {"sp-s.sp1",0xc7f2fa45},{"sp-e.sp1",0x2723a5b5},{"sp-u2.sp1",0xe72943de},
        {"sp-j2.sp1",0xacede59c},{"sp1-u2",0x62f021f4},{"japan-j3.bin",0xdff6d41f},
        {"sp-1v1_3db8c.bin",0x162f0ebe},{"uni-bios_4_0.rom",0xa7aab458}};
    for (const auto& [name, expected] : bios_crc) {
        const auto it=bioses.find(name); if (it==bioses.end()) continue;
        const auto crc=static_cast<std::uint32_t>(crc32(0,it->second.data(),static_cast<uInt>(it->second.size())));
        if (crc != expected) throw std::runtime_error("CRC mismatch for Neo Geo BIOS " + std::string(name));
        if (it->second.size() != 0x20000) continue; // MVS 0x80000 mask ROM variants are not selected yet.
        out.bios_filename=name; out.bios=std::move(it->second);
        for (std::size_t i=0; i+1<out.bios.size(); i+=2) std::swap(out.bios[i],out.bios[i+1]);
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
    std::array<const char*, std::size(kLoads)> game_names{};
    for (std::size_t i=0; i<std::size(kLoads); ++i) game_names[i]=kLoads[i].name;
    auto game_files=read_roms(game_zip, game_names);
    constexpr const char* bios_names[]{"sp-s2.sp1","sp-s.sp1","sp-e.sp1","sp-u2.sp1",
        "sp-j2.sp1","sp1-u2","japan-j3.bin","sp-1v1_3db8c.bin","uni-bios_4_0.rom",
        "sfix.sfix","sm1.sm1","000-lo.lo"};
    auto bios_files=read_roms(bios_zip,bios_names);
    constexpr std::pair<const char*, std::uint32_t> aux_crc[]{{"sfix.sfix",0xc2ea0cfd},
        {"sm1.sm1",0x94416d67},{"000-lo.lo",0x5a86cff2}};
    for (const auto& [name, expected] : aux_crc) {
        const auto it=bios_files.find(name);
        if (it==bios_files.end()) throw std::runtime_error("missing Neo Geo system ROM " + std::string(name));
        const auto crc=static_cast<std::uint32_t>(crc32(0,it->second.data(),static_cast<uInt>(it->second.size())));
        if (crc!=expected) throw std::runtime_error("CRC mismatch for Neo Geo system ROM " + std::string(name));
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
