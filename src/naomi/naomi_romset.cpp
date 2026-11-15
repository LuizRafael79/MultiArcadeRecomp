#include "naomi_romset.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace multiarcaderecomp::naomi {
namespace {

struct RomLoad {
    const char* filename;
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t crc;
    std::uint8_t stride = 1;
    std::uint8_t lane = 0;
};

struct RomSet {
    const char* id;
    const char* parent;
    std::uint32_t region_size;
    const RomLoad* loads;
    std::size_t load_count;
    std::uint32_t copy_source = 0;
    std::uint32_t copy_destination = 0;
    std::uint32_t copy_length = 0;
};

constexpr RomLoad kMvsc2uLoads[]{
    {"epr-23062a.ic22", 0x0000000, 0x0400000, 0x96038276},
    {"mpr-23048.ic1",   0x0800000, 0x0800000, 0x93d7a63a},
    {"mpr-23049.ic2",   0x1000000, 0x0800000, 0x003dcce0},
    {"mpr-23050.ic3",   0x1800000, 0x0800000, 0x1d6b88a7},
    {"mpr-23051.ic4",   0x2000000, 0x0800000, 0x01226aaa},
    {"mpr-23052.ic5",   0x2800000, 0x0800000, 0x74bee120},
    {"mpr-23053.ic6",   0x3000000, 0x0800000, 0xd92d4401},
    {"mpr-23054.ic7",   0x3800000, 0x0800000, 0x78ba02e8},
    {"mpr-23055.ic8",   0x4000000, 0x0800000, 0x84319604},
    {"mpr-23056.ic9",   0x4800000, 0x0800000, 0xd7386034},
    {"mpr-23057.ic10",  0x5000000, 0x0800000, 0xa3f087db},
    {"mpr-23058.ic11",  0x5800000, 0x0800000, 0x61a6cc5d},
    {"mpr-23059.ic12s", 0x6000000, 0x0800000, 0x64808024},
    {"mpr-23060.ic13s", 0x6800000, 0x0800000, 0x67519942},
    {"mpr-23061.ic14s", 0x7000000, 0x0800000, 0xfb1844c4},
};

constexpr RomLoad kMvsc2Loads[]{
    {"epr-23085a.ic11", 0x0000000, 0x0400000, 0x5d5b7ad1},
    {"mpr-23048.ic17s", 0x0800000, 0x0800000, 0x93d7a63a},
    {"mpr-23049.ic18",  0x1000000, 0x0800000, 0x003dcce0},
    {"mpr-23050.ic19s", 0x1800000, 0x0800000, 0x1d6b88a7},
    {"mpr-23051.ic20",  0x2000000, 0x0800000, 0x01226aaa},
    {"mpr-23052.ic21s", 0x2800000, 0x0800000, 0x74bee120},
    {"mpr-23053.ic22",  0x3000000, 0x0800000, 0xd92d4401},
    {"mpr-23054.ic23s", 0x3800000, 0x0800000, 0x78ba02e8},
    {"mpr-23055.ic24",  0x4000000, 0x0800000, 0x84319604},
    {"mpr-23056.ic25s", 0x4800000, 0x0800000, 0xd7386034},
    {"mpr-23057.ic26",  0x5000000, 0x0800000, 0xa3f087db},
    {"mpr-23058.ic27s", 0x5800000, 0x0800000, 0x61a6cc5d},
    {"mpr-23059.ic28",  0x6000000, 0x0800000, 0x64808024},
    {"mpr-23060.ic29",  0x6800000, 0x0800000, 0x67519942},
    {"mpr-23061.ic30s", 0x7000000, 0x0800000, 0xfb1844c4},
    {"mpr-23083.ic31",  0x7800000, 0x0400000, 0xc61d2dfe},
    {"mpr-23083.ic31",  0x8000000, 0x0400000, 0xc61d2dfe, 4, 0},
    {"mpr-23084.ic32s", 0x8000000, 0x0400000, 0x4ebbbdd9, 4, 2},
};

constexpr RomSet kSets[]{
    {"mvsc2u", "mvsc2", 0x07800000, kMvsc2uLoads, std::size(kMvsc2uLoads)},
    {"mvsc2", "", 0x08800000, kMvsc2Loads, std::size(kMvsc2Loads),
        0x01200000, 0x00400000, 0x00400000},
};
// MAME's NAOMI_DEFAULT_EEPROM loads this 0x84-byte X76F100 image into
// `naomibd_eeprom`; the separate `some_eeprom` ROM region is not this port.
constexpr std::uint32_t kDefaultBoardIdEepromCrc = 0x3ea24b6a;

struct BiosCandidate {
    const char* filename;
    std::uint32_t crc;
};

constexpr BiosCandidate kUsaBioses[]{
    {"epr-21577h.ic27", 0xfdf17452}, {"epr-21577g.ic27", 0x25f64af7},
    {"epr-21577e.ic27", 0xcf36e97b}, {"epr-21577d.ic27", 0x60ddcbbe},
    {"epr-21577a.ic27", 0x969dc491},
};
constexpr BiosCandidate kExportKoreaBioses[]{
    {"epr-21578h.ic27", 0x7b452946}, {"epr-21578g.ic27", 0x55413214},
    {"epr-21578f.ic27", 0x628a27fd}, {"epr-21578e.ic27", 0x087f09a3},
    {"epr-21578d.ic27", 0xdfd5f42a}, {"epr-21578a.ic27", 0x6c9aad83},
    {"epr-21579d.ic27", 0x33513691}, {"epr-21579.ic27", 0x71f9c918},
};
constexpr BiosCandidate kJapanBioses[]{
    {"epr-21576h.ic27", 0xd4895685}, {"epr-21576g.ic27", 0xd2a1c6bf},
    {"epr-21576e.ic27", 0x08c0add7}, {"epr-21576d.ic27", 0x3b2afa7b},
    {"epr-21576c.ic27", 0x4599ad13}, {"epr-21576b.ic27", 0x755a6e07},
    {"epr-21576a.ic27", 0xcedfe439}, {"epr-21576.ic27", 0x9dad3495},
};

struct ArchiveCloser {
    void operator()(archive* value) const { if (value) archive_read_free(value); }
};
using ArchivePtr = std::unique_ptr<archive, ArchiveCloser>;
using RomContents = std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>;

RomContents read_archive(const std::filesystem::path& path,
                         const std::unordered_set<std::uint32_t>& wanted) {
    ArchivePtr reader(archive_read_new());
    if (!reader) throw std::runtime_error("could not allocate ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), path.c_str(), 10240) != ARCHIVE_OK) {
        const char* message = archive_error_string(reader.get());
        throw std::runtime_error("could not open Naomi ROM ZIP " + path.string() + ": " +
                                 (message ? message : "unknown archive error"));
    }

    RomContents contents;
    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const auto size = archive_entry_size(entry);
        if (size < 0 || static_cast<std::uint64_t>(size) > 0x10000000ull) {
            archive_read_data_skip(reader.get());
            continue;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const la_ssize_t count = archive_read_data(reader.get(), bytes.data() + offset,
                                                        bytes.size() - offset);
            if (count <= 0) {
                if (count < 0) {
                    const char* message = archive_error_string(reader.get());
                    throw std::runtime_error("error reading Naomi ROM ZIP: " +
                        std::string(message ? message : "unknown archive error"));
                }
                break;
            }
            offset += static_cast<std::size_t>(count);
        }
        if (offset != bytes.size()) continue;
        const auto crc = static_cast<std::uint32_t>(crc32(0, bytes.data(),
            static_cast<uInt>(bytes.size())));
        if (wanted.contains(crc)) contents.try_emplace(crc, std::move(bytes));
    }
    if (result != ARCHIVE_EOF) {
        const char* message = archive_error_string(reader.get());
        throw std::runtime_error("error traversing Naomi ROM ZIP: " +
            std::string(message ? message : "unknown archive error"));
    }
    return contents;
}

const RomSet& find_set(std::string_view id) {
    for (const auto& set : kSets) if (id == set.id) return set;
    throw std::runtime_error("unknown Naomi ROM set: " + std::string(id));
}

} // namespace

NaomiRomBoardImage load_mvc2_rom_board(const std::string& set_id,
                                       const std::filesystem::path& zip_path) {
    const RomSet& set = find_set(set_id);
    std::unordered_set<std::uint32_t> wanted;
    for (std::size_t i = 0; i < set.load_count; ++i) wanted.insert(set.loads[i].crc);
    wanted.insert(kDefaultBoardIdEepromCrc);

    auto contents = read_archive(zip_path, wanted);
    if (contents.size() < wanted.size() && set.parent[0] != '\0') {
        const auto parent_path = zip_path.parent_path() / (std::string(set.parent) + ".zip");
        if (std::filesystem::is_regular_file(parent_path)) {
            auto parent_contents = read_archive(parent_path, wanted);
            for (auto& [crc, bytes] : parent_contents)
                contents.try_emplace(crc, std::move(bytes));
        }
    }

    NaomiRomBoardImage image{set.id, std::vector<std::uint8_t>(set.region_size, 0xff), 0, {}};
    const auto eeprom = contents.find(kDefaultBoardIdEepromCrc);
    if (eeprom == contents.end() || eeprom->second.size() != 0x84)
        throw std::runtime_error("missing or invalid Naomi X76F100 image x76f100_eeprom.bin (CRC 3ea24b6a)");
    image.board_id_eeprom = eeprom->second;
    for (std::size_t i = 0; i < set.load_count; ++i) {
        const auto& load = set.loads[i];
        const auto found = contents.find(load.crc);
        if (found == contents.end())
            throw std::runtime_error("missing ROM " + std::string(load.filename) +
                                     " (CRC " + std::to_string(load.crc) + ") in " + zip_path.string());
        const auto& bytes = found->second;
        if (bytes.size() != load.length)
            throw std::runtime_error("wrong size for ROM " + std::string(load.filename));

        const std::uint64_t end = load.stride == 1
            ? static_cast<std::uint64_t>(load.offset) + load.length
            : static_cast<std::uint64_t>(load.offset) + load.lane +
                ((load.length - 1u) / 2u) * 4u + ((load.length - 1u) % 2u) + 1u;
        if (end > image.bytes.size())
            throw std::runtime_error("ROM mapping exceeds Naomi region for " + std::string(load.filename));

        if (load.stride == 1) {
            std::copy(bytes.begin(), bytes.end(), image.bytes.begin() + load.offset);
        } else {
            for (std::size_t source = 0; source < bytes.size(); ++source) {
                const auto destination = load.offset + load.lane +
                    static_cast<std::uint32_t>((source / 2u) * 4u + (source % 2u));
                image.bytes[destination] = bytes[source];
            }
        }
        ++image.rom_files_loaded;
    }

    if (set.copy_length != 0) {
        if (set.copy_source + set.copy_length > image.bytes.size() ||
            set.copy_destination + set.copy_length > image.bytes.size())
            throw std::runtime_error("invalid ROM_COPY in Naomi profile " + set_id);
        std::copy_n(image.bytes.begin() + set.copy_source, set.copy_length,
                    image.bytes.begin() + set.copy_destination);
    }
    return image;
}

NaomiBiosImage load_naomi_bios(const std::string& set_id,
                               const std::filesystem::path& zip_path) {
    const BiosCandidate* candidates = nullptr;
    std::size_t candidate_count = 0;
    if (set_id == "mvsc2u") {
        candidates = kUsaBioses;
        candidate_count = std::size(kUsaBioses);
    } else if (set_id == "mvsc2") {
        candidates = kExportKoreaBioses;
        candidate_count = std::size(kExportKoreaBioses);
    } else {
        throw std::runtime_error("no Naomi BIOS region policy for set " + set_id);
    }

    std::unordered_set<std::uint32_t> wanted;
    for (std::size_t i = 0; i < candidate_count; ++i)
        wanted.insert(candidates[i].crc);
    const auto contents = read_archive(zip_path, wanted);
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const auto found = contents.find(candidates[i].crc);
        if (found == contents.end()) continue;
        if (found->second.size() != 0x200000)
            throw std::runtime_error("wrong size for Naomi BIOS " +
                                     std::string(candidates[i].filename));
        return {candidates[i].filename, found->second, candidates[i].crc};
    }

    // If a USA profile is being investigated with a Japanese BIOS, permit the
    // standard default region after checking the exact known MAME/Flycast CRCs.
    if (set_id == "mvsc2u") {
        wanted.clear();
        for (const auto& candidate : kJapanBioses) wanted.insert(candidate.crc);
        const auto japanese = read_archive(zip_path, wanted);
        for (const auto& candidate : kJapanBioses) {
            const auto found = japanese.find(candidate.crc);
            if (found == japanese.end()) continue;
            if (found->second.size() != 0x200000)
                throw std::runtime_error("wrong size for Naomi BIOS " +
                                         std::string(candidate.filename));
            return {candidate.filename, found->second, candidate.crc};
        }
    }

    throw std::runtime_error("no recognized 2 MiB Naomi BIOS found in " + zip_path.string());
}

} // namespace multiarcaderecomp::naomi
