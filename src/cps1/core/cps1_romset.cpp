#include "cps1_romset.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace multiarcaderecomp::cps1 {
namespace {
struct ArchiveCloser { void operator()(archive* value) const { if (value) archive_read_free(value); } };

std::unordered_map<std::string, std::vector<std::uint8_t>> read_members(
    const Cps1RomSet& romset, const std::filesystem::path& zip_path) {
    std::unordered_map<std::string, const Cps1RomLoad*> definitions;
    for (const auto& load : romset.loads) definitions.emplace(std::string(load.file), &load);

    std::unique_ptr<archive, ArchiveCloser> reader(archive_read_new());
    if (!reader) throw std::runtime_error("could not allocate CPS-1 ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip_path.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open CPS-1 ROM ZIP " + zip_path.string());

    std::unordered_map<std::string, std::vector<std::uint8_t>> files;
    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* raw_name = archive_entry_pathname(entry);
        const std::string name = raw_name ? std::filesystem::path(raw_name).filename().string() : std::string{};
        const auto definition = definitions.find(name);
        if (definition == definitions.end() || files.contains(name)) {
            archive_read_data_skip(reader.get());
            continue;
        }
        const auto expected_size = definition->second->file_size;
        if (archive_entry_size(entry) != expected_size)
            throw std::runtime_error(name + " has unexpected size in " + zip_path.string());
        std::vector<std::uint8_t> bytes(expected_size);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = archive_read_data(reader.get(), bytes.data() + offset, bytes.size() - offset);
            if (count <= 0) throw std::runtime_error("truncated CPS-1 ROM " + name);
            offset += static_cast<std::size_t>(count);
        }
        const auto actual_crc = static_cast<std::uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
        if (actual_crc != definition->second->crc32)
            throw std::runtime_error("CRC32 mismatch for CPS-1 ROM " + name);
        files.emplace(name, std::move(bytes));
    }
    if (result != ARCHIVE_EOF) throw std::runtime_error("error reading CPS-1 ZIP " + zip_path.string());
    for (const auto& [name, _] : definitions)
        if (!files.contains(name)) throw std::runtime_error("missing CPS-1 ROM " + name);
    return files;
}
}

RomRegions load_romset(const Cps1RomSet& romset, const std::filesystem::path& zip_path) {
    const auto files = read_members(romset, zip_path);
    RomRegions regions;
    for (const auto& region : romset.regions)
        regions.emplace(std::string(region.name), std::vector<std::uint8_t>(region.size, region.fill));

    for (const auto& load : romset.loads) {
        auto target = regions.find(std::string(load.region));
        const auto source = files.find(std::string(load.file));
        if (target == regions.end() || source == files.end())
            throw std::runtime_error("invalid CPS-1 ROM driver reference for " + std::string(load.file));
        if (!load.group_bytes || load.stride < load.group_bytes || load.length % load.group_bytes ||
            load.source_offset > source->second.size() || load.length > source->second.size() - load.source_offset)
            throw std::runtime_error("invalid CPS-1 load geometry for " + std::string(load.file));
        const std::uint64_t final = load.length
            ? static_cast<std::uint64_t>(load.destination)
                + static_cast<std::uint64_t>((load.length - load.group_bytes) / load.group_bytes) * load.stride
                + load.group_bytes
            : load.destination;
        if (final > target->second.size())
            throw std::runtime_error("CPS-1 ROM load exceeds region " + std::string(load.region));

        for (std::uint32_t i = 0; i < load.length; ++i) {
            std::uint32_t source_offset = load.source_offset + i;
            if (load.transform == Cps1RomTransform::WordSwap) source_offset = load.source_offset + (i ^ 1u);
            const std::uint32_t destination = load.destination
                + (i / load.group_bytes) * load.stride + (i % load.group_bytes);
            target->second[destination] = source->second[source_offset];
        }
    }
    return regions;
}

void export_romset(const Cps1RomSet& romset, const std::filesystem::path& zip_path,
                   const std::filesystem::path& output_dir) {
    const auto regions = load_romset(romset, zip_path);
    std::filesystem::create_directories(output_dir);
    for (const auto& definition : romset.regions) {
        const auto found = regions.find(std::string(definition.name));
        if (found == regions.end() || definition.exported_size > found->second.size())
            throw std::runtime_error("invalid CPS-1 export region " + std::string(definition.name));
        const auto path = output_dir / (std::string(definition.name) + ".bin");
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("could not create " + path.string());
        output.write(reinterpret_cast<const char*>(found->second.data()), definition.exported_size);
        if (!output) throw std::runtime_error("failed writing " + path.string());
    }
}

} // namespace multiarcaderecomp::cps1
