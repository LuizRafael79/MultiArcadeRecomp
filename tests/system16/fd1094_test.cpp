#include "fd1094.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> read_key(const char* zip_path)
{
    std::unique_ptr<archive, decltype(&archive_read_free)> zip(archive_read_new(), archive_read_free);
    assert(zip);
    archive_read_support_filter_all(zip.get());
    archive_read_support_format_zip(zip.get());
    assert(archive_read_open_filename(zip.get(), zip_path, 10240) == ARCHIVE_OK);
    archive_entry* entry = nullptr;
    while (archive_read_next_header(zip.get(), &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (!name || std::string(name).find("317-0130.key") == std::string::npos) {
            archive_read_data_skip(zip.get());
            continue;
        }
        assert(archive_entry_size(entry) == 0x2000);
        std::vector<uint8_t> bytes(0x2000);
        assert(archive_read_data(zip.get(), bytes.data(), bytes.size()) == static_cast<la_ssize_t>(bytes.size()));
        return bytes;
    }
    assert(false && "ESWAT FD1094 key was not found in the ROM ZIP");
    return {};
}
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto key = read_key(argv[1]);
    mamerecomp::Fd1094 fd1094(key);

    // MAME's FD1094 vector table documents the encrypted and expected reset
    // vectors for ESWAT (317-0130): SP 0, PC 0x400.
    constexpr std::array<uint16_t, 4> encrypted{0xa711, 0xaf59, 0x5533, 0xbc59};
    constexpr std::array<uint16_t, 4> expected{0x0000, 0x0000, 0x0000, 0x0400};
    for (uint32_t word = 0; word < encrypted.size(); ++word)
        assert(fd1094.decrypt_opcode(word * 2, encrypted[word], true) == expected[word]);

    fd1094.change_state(0x0005);
    assert(fd1094.state() == 0x05);
    fd1094.change_state(0x0200);
    assert(fd1094.state() == key[0]);
    fd1094.change_state(0x0300);
    assert(fd1094.state() == 0x05);
    fd1094.change_state(0x010c);
    assert(fd1094.state() == 0x0c);

    std::cout << "FD1094 ESWAT vectors and state control: OK\n";
}
