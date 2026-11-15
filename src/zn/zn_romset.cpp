#include "zn_romset.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace mamerecomp::zn {
namespace {
std::array<uint8_t, 8> security_key(const std::vector<uint8_t> &bytes)
{
    std::array<uint8_t, 8> key;
    key.fill(0xff);
    std::copy_n(bytes.begin(), std::min(bytes.size(), key.size()), key.begin());
    return key;
}

struct Load {
    const char *file;
    const char *region;
    size_t offset;
    size_t size;
    unsigned stride = 1;
    unsigned lane = 0;
    bool word_swap = false;
    uint32_t crc = 0;
};
struct Profile {
    const char *id;
    size_t boot_rom_bytes;
    size_t bank_bytes;
    size_t audio_bytes;
    size_t sample_bytes;
    const char *sample_region;
    const Load *loads;
    size_t count;
};

constexpr Load sfex2pa_loads[] = {
    {"m534002c-59.ic353", "bios", 0, 0x80000}, {"x2pa_04.2h", "country", 0, 0x80000},
    {"x2p-05m.3h", "program", 0x0000000, 0x800000}, {"x2p-06m.4h", "program", 0x0800000, 0x800000},
    {"x2p-07m.5h", "program", 0x1000000, 0x800000}, {"x2p-08m.2k", "program", 0x1800000, 0x800000},
    {"x2p-09m.3k", "program", 0x2000000, 0x800000}, {"x2p-10m.4k", "program", 0x2800000, 0x800000},
    {"x2p_02.2e", "audio_cpu", 0, 0x20000}, {"x2p_03.3e", "audio_cpu", 0x20000, 0x20000},
    {"x2p-01m.3a", "qsound", 0, 0x400000, 1, 0, true}, {"cp12", "security", 0, 8},
    {"cp10.ic652", "security_bios", 0, 8}
};
constexpr Load bldyror2_loads[] = {
    {"m27c402cz-54.ic353", "bios", 0, 0x80000}, {"flash0.021", "program", 0, 0x200000},
    {"flash1.024", "program", 0x200000, 0x200000}, {"rom-1a.028", "program", 0x800000, 0x400000},
    {"rom-1b.29", "program", 0xc00000, 0x400000}, {"rom-2a.026", "program", 0x1000000, 0x400000},
    {"rom-2b.210", "program", 0x1400000, 0x400000},
    {"br2_u049.049", "audio_cpu", 0, 0x80000, 2, 0}, {"br2_u0412.412", "audio_cpu", 0, 0x80000, 2, 1},
    {"rom-3.336", "ymf_samples", 0, 0x400000}, {"at28c16_world", "eeprom", 0, 0x800},
    {"et03", "security", 0, 8}, {"et01.ic652", "security_bios", 0, 8}
};
// Namco System 12 / coh700 ROM geometry from MAME's ROM_START(tekken3):
// 32-bit little-endian main ROM assembled from byte lanes, three 8 MiB
// banked flash pairs, a word-swapped H8 image, and C352 sample ROMs.
constexpr Load tekken3_loads[] = {
    {"tet2vere1.2e", "bios", 0, 0x200000, 2, 0},
    {"tet2vere1.2j", "bios", 0, 0x200000, 2, 1},
    {"tet1rom0l.6", "program", 0x0000000, 0x400000, 2, 0},
    {"tet1rom0u.9", "program", 0x0000000, 0x400000, 2, 1},
    {"tet1rom1l.7", "program", 0x0800000, 0x400000, 2, 0},
    {"tet1rom1u.10", "program", 0x0800000, 0x400000, 2, 1},
    {"tet1rom2l.8", "program", 0x1000000, 0x400000, 2, 0},
    {"tet1rom2u.11", "program", 0x1000000, 0x400000, 2, 1},
    {"tet1fl3l.12", "program", 0x1800000, 0x200000, 2, 0},
    {"tet1fl3u.13", "program", 0x1800000, 0x200000, 2, 1},
    {"tet1vere.11s", "audio_cpu", 0, 0x80000, 1, 0, true, 0xc92b98d1},
    {"tet1wave0.5", "c352", 0, 0x400000},
    {"tet1wave1.4", "c352", 0x400000, 0x400000}
};
constexpr Profile profiles[] = {
    {"sfex2pa", 0x80000, 0x3000000, 0x40000, 0x400000, "qsound", sfex2pa_loads, std::size(sfex2pa_loads)},
    {"bldyror2", 0x80000, 0x1800000, 0x100000, 0x400000, "ymf_samples", bldyror2_loads, std::size(bldyror2_loads)},
    {"tekken3", 0x400000, 0x1c00000, 0x80000, 0x1000000, "c352", tekken3_loads, std::size(tekken3_loads)}
};

const Profile &profile_for(std::string_view id) {
    for (const auto &p : profiles) if (id == p.id) return p;
    throw std::runtime_error("unsupported ZN ROM profile: " + std::string(id));
}
}

RomRegions load_romset(std::string_view set, const std::filesystem::path &zip) {
    const auto &profile = profile_for(set);
    std::unordered_map<std::string, const Load *> expected;
    for (size_t i = 0; i < profile.count; ++i) expected.emplace(profile.loads[i].file, &profile.loads[i]);

    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) throw std::runtime_error("could not allocate ZIP reader");
    archive_read_support_filter_all(reader.get()); archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open ZN ROM ZIP " + zip.string());

    std::unordered_map<std::string, std::vector<uint8_t>> files;
    archive_entry *entry = nullptr;
    int result;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        const auto name = path ? std::filesystem::path(path).filename().string() : std::string{};
        auto found = expected.find(name);
        const auto size = static_cast<size_t>(archive_entry_size(entry));
        const Load *load = found == expected.end() ? nullptr : found->second;
        if (load && files.contains(load->file)) {
            archive_read_data_skip(reader.get());
            continue;
        }
        // MAME recognizes ROMs by content when an archive member was renamed.
        // Do the same for entries with a declared CRC instead of extracting or
        // guessing from size alone.
        if (!load) {
            for (const auto &[expected_name, candidate] : expected) {
                if (candidate->crc && candidate->size == size && !files.contains(expected_name)) {
                    load = candidate;
                    break;
                }
            }
        }
        if (!load || size != load->size) { archive_read_data_skip(reader.get()); continue; }
        std::vector<uint8_t> bytes(size);
        size_t offset = 0;
        while (offset < size) {
            auto n = archive_read_data(reader.get(), bytes.data() + offset, size - offset);
            if (n <= 0) throw std::runtime_error("truncated ZN ROM " + name);
            offset += static_cast<size_t>(n);
        }
        if (load->crc && static_cast<uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size()))) != load->crc) {
            if (found != expected.end())
                throw std::runtime_error(name + " failed its MAME ROM CRC check");
            continue;
        }
        files.emplace(load->file, std::move(bytes));
    }
    if (result != ARCHIVE_EOF) throw std::runtime_error("error while reading ZN ROM ZIP " + zip.string());
    for (const auto &[name, _] : expected) if (!files.contains(name))
        throw std::runtime_error("missing ROM " + name + " in " + zip.string());

    RomRegions out;
    out.regions.emplace("bios", std::vector<uint8_t>(profile.boot_rom_bytes, 0xff));
    out.regions.emplace("country", std::vector<uint8_t>(0x80000, 0xff));
    out.regions.emplace("program", std::vector<uint8_t>(profile.bank_bytes, 0xff));
    out.regions.emplace("audio_cpu", std::vector<uint8_t>(profile.audio_bytes, 0xff));
    if (profile.sample_bytes) out.regions.emplace(profile.sample_region, std::vector<uint8_t>(profile.sample_bytes, 0xff));
    for (const auto *key : {"security", "security_bios", "eeprom"})
        out.regions.emplace(key, std::vector<uint8_t>(key == std::string("eeprom") ? 0x800 : 8, 0xff));

    for (size_t i = 0; i < profile.count; ++i) {
        const auto &load = profile.loads[i];
        auto &region = out.regions.at(load.region);
        const auto &source = files.at(load.file);
        const size_t end = load.stride == 1
            ? load.offset + source.size()
            : load.offset + (source.size() - 1) * load.stride + load.lane + 1;
        if (end > region.size())
            throw std::runtime_error(std::string(load.file) + " exceeds its MAME ROM region");
        if (load.stride == 1 && !load.word_swap) {
            std::copy(source.begin(), source.end(), region.begin() + load.offset);
        } else if (load.stride == 1 && load.word_swap) {
            for (size_t j = 0; j < source.size(); ++j) region[load.offset + (j ^ 1)] = source[j];
        } else {
            for (size_t j = 0; j < source.size(); ++j) region[load.offset + j * load.stride + load.lane] = source[j];
        }
    }
    return out;
}

std::vector<std::uint16_t> load_qsound_firmware(const std::filesystem::path &zip) {
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) throw std::runtime_error("could not allocate QSound firmware ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open QSound firmware ZIP " + zip.string());
    archive_entry *entry = nullptr;
    int result;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        const auto name = path ? std::filesystem::path(path).filename().string() : std::string{};
        const auto size = static_cast<std::size_t>(archive_entry_size(entry));
        if (name != "dl-1425.bin" || size != 0x6000) {
            archive_read_data_skip(reader.get());
            continue;
        }
        std::vector<std::uint8_t> bytes(size);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = archive_read_data(reader.get(), bytes.data() + offset, bytes.size() - offset);
            if (n <= 0) throw std::runtime_error("truncated DL-1425 QSound firmware in " + zip.string());
            offset += static_cast<std::size_t>(n);
        }
        const auto crc = static_cast<std::uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
        if (crc != 0xd6cf5ef5u)
            throw std::runtime_error("DL-1425 QSound firmware failed CRC check in " + zip.string());
        // The ZIP contains raw bytes beginning 88 02. MAME's LLE region uses
        // ROM_REGION16_BE plus ROM_LOAD16_WORD_SWAP; its HLE region uses
        // ROM_REGION16_LE plus ROM_LOAD16_WORD. Both produce the same first
        // DSP program word, 0x0288. Decode the raw image as little-endian here
        // to preserve that effective word value. The first 0x2000 bytes are
        // the shared 0x1000-word firmware/table image (remaining 0x4000 ignored).
        std::vector<std::uint16_t> words(0x1000);
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] = static_cast<std::uint16_t>(bytes[i * 2] | (std::uint16_t(bytes[i * 2 + 1]) << 8));
        return words;
    }
    if (result != ARCHIVE_EOF)
        throw std::runtime_error("error reading QSound firmware ZIP " + zip.string());
    throw std::runtime_error("DL-1425 firmware (dl-1425.bin) not found in " + zip.string());
}

CapcomZn2Board::CapcomZn2Board(const RomRegions &roms)
    : roms_(&roms), security_a_(security_key(roms.at("security_bios"))),
      security_b_(security_key(roms.at("security"))),
      eeprom_(25'000'000, roms.at("eeprom"))
{
    // MAME's generic ZN DSW defaults all four DIP bits high (0x0f).
    // capcom6b inherits zn2p; it does not replace these defaults.
    mcu_.set_dsw(0x0f);
}

PsxBus CapcomZn2Board::make_bus() {
    PsxBus bus{}; bus.opaque = this; bus.read = read_cb; bus.write = write_cb;
    bus.serial_clock = serial_clock_cb; bus.serial_data = serial_data_cb;
    bus.serial_read = serial_read_cb; bus.serial_dsr = serial_dsr_cb;
    bus.advance_serial = advance_serial_cb;
    bus.serial_next_event_cycle = serial_next_event_cycle_cb;
    bus.set_cpu_cycles = cpu_cycles_cb;
    return bus;
}
void CapcomZn2Board::cpu_cycles_cb(void *opaque, uint64_t cycles) {
    auto *self = static_cast<CapcomZn2Board *>(opaque);
    self->cpu_cycles_ = cycles;
    self->eeprom_.advance(cycles);
}
void CapcomZn2Board::serial_clock_cb(void *opaque, int state) {
    auto *self = static_cast<CapcomZn2Board *>(opaque);
    self->security_a_.clock(state != 0); self->security_b_.clock(state != 0);
    self->mcu_.clock(state);
}
void CapcomZn2Board::serial_data_cb(void *opaque, int state) {
    auto *self = static_cast<CapcomZn2Board *>(opaque);
    self->security_a_.data_in(state != 0); self->security_b_.data_in(state != 0);
}
int CapcomZn2Board::serial_read_cb(void *opaque) {
    auto *self = static_cast<CapcomZn2Board *>(opaque);
    const bool a = self->security_a_.data_out();
    const bool b = self->security_b_.data_out();
    return a && b && self->mcu_.data_out();
}
int CapcomZn2Board::serial_dsr_cb(void *opaque) {
    return static_cast<CapcomZn2Board *>(opaque)->mcu_.dsr();
}
void CapcomZn2Board::advance_serial_cb(void *opaque, uint64_t cycles) {
    static_cast<CapcomZn2Board *>(opaque)->mcu_.advance(cycles);
}
uint64_t CapcomZn2Board::serial_next_event_cycle_cb(void *opaque) {
    return static_cast<CapcomZn2Board *>(opaque)->mcu_.next_event_cycle();
}

bool CapcomZn2Board::read_cb(void *opaque, uint32_t address, unsigned size,
                             uint32_t, uint32_t *value) {
    return static_cast<CapcomZn2Board *>(opaque)->read(address, size, value);
}
bool CapcomZn2Board::write_cb(void *opaque, uint32_t address, unsigned size,
                              uint32_t value, uint32_t pc) {
    return static_cast<CapcomZn2Board *>(opaque)->write(address, size, value, pc);
}

bool CapcomZn2Board::read(uint32_t address, unsigned size, uint32_t *value) {
    if (!value || (size != 1 && size != 2 && size != 4)) return false;
    const uint32_t p = address & 0x1fffffff;
    auto read_region = [&](const char *region, size_t offset) {
        const auto &bytes = roms_->at(region);
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) {
            const size_t index = offset + i;
            if (index >= bytes.size()) return false;
            result |= uint32_t(bytes[index]) << (i * 8);
        }
        *value = result;
        return true;
    };
    if (p >= 0x1f000000 && p <= 0x1f3fffff)
        return read_region("program", p - 0x1f000000);
    if (p >= 0x1f400000 && p <= 0x1f7fffff) {
        const size_t offset = 0x400000 + size_t(bank_) * 0x400000 + (p - 0x1f400000);
        return read_region("program", offset);
    }
    if (p >= 0x1fb80000 && p <= 0x1fbfffff)
        return read_region("country", p - 0x1fb80000);
    auto read_input = [&](uint32_t base, size_t port) {
        if (p < base || p > base + 3) return false;
        const uint32_t input = input_ports_[port];
        if (std::getenv("MAMERECOMP_ZN_TRACE_INPUTS") && input != 0xff) {
            static unsigned traced = 0;
            if (traced++ < 64)
                std::cerr << "ZN input-port=" << port << " read address=0x"
                          << std::hex << address << " size=" << std::dec << size
                          << " value=0x" << std::hex << input << std::dec << '\n';
        }
        *value = size == 1 ? input : size == 2 ? (0xff00u | input)
                                                : (0xffffff00u | input);
        return true;
    };
    if (read_input(0x1fa00000, 0)) return true;
    if (read_input(0x1fa00100, 1)) return true;
    if (read_input(0x1fa00200, 2)) return true;
    if (read_input(0x1fa00300, 3)) return true;
    if (read_input(0x1fa10000, 4)) return true;
    if (read_input(0x1fa10100, 5)) return true;
    if (p == 0x1fa10200) { *value = 0x69; return true; }
    if (p == 0x1fa10300) { *value = security_select_; return true; }
    if (p == 0x1fa20000) { *value = coin_; return true; }
    if (p >= 0x1fa30000 && p + size <= 0x1fa30004) { *value = 0; return true; }
    if (p >= 0x1fa40000 && p + size <= 0x1fa40004) { *value = 0; return true; }
    if (p >= 0x1fa51c00 && p + size <= 0x1fa51e00) { *value = 0; return true; }
    // MAME maps this 16-bit register at 0x1fa60000-0x1fa60001, but the
    // game polls it with an R3000A `lw`. Preserve the device read side effect
    // for that 32-bit access too; otherwise the poll sees a constant zero and
    // repeatedly times out/reset the GPU before enabling video.
    if (p == 0x1fa60000) {
        spu_hack_ ^= 8;
        *value = spu_hack_;
        return true;
    }
    if (p >= 0x1faf0000 && p <= 0x1faf07ff) {
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) {
            const size_t offset = p - 0x1faf0000 + i;
            result |= uint32_t(eeprom_.read(offset, cpu_cycles_)) << (i * 8);
        }
        *value = result;
        return true;
    }
    if (read_input(0x1fb40010, 6)) return true;
    if (read_input(0x1fb40020, 7)) return true;
    if (p >= 0x1fb20000 && p <= 0x1fb20007) { *value = 0xffffffff; return true; }
    if (p >= 0x1fa51c00 && p <= 0x1fa51dff) { *value = 0xffffffff; return true; }
    return false;
}

bool CapcomZn2Board::write(uint32_t address, unsigned size, uint32_t value,
                           uint32_t pc) {
    if (size != 1 && size != 2 && size != 4) return false;
    const uint32_t p = address & 0x1fffffff;
    const uint8_t data = static_cast<uint8_t>(value);
    if (p == 0x1fb00000) { bank_ = data & 0x0f; return true; }
    if (p == 0x1fb60000) {
        sound_latch_ = data;
        sound_pending_ = true;
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 512)
                std::cerr << "ZN2 sound latch write value=0x" << std::hex
                          << unsigned(data) << " pc=0x" << pc << " cycle="
                          << std::dec << cpu_cycles_ << '\n';
        }
        if (sound_command_callback_)
            sound_command_callback_(sound_command_opaque_, cpu_cycles_);
        return true;
    }
    if (p == 0x1fa10300) {
        security_select_ = data;
        security_a_.select((data & 4) != 0);
        security_b_.select((data & 8) != 0);
        mcu_.set_analog_select((data & 0x10) != 0);
        mcu_.set_trackball_select((data & 0x20) != 0);
        mcu_.select((data & 0x8c) != 0x8c);
        return true;
    }
    if (p == 0x1fa20000) { coin_ = data; return true; }
    if ((p >= 0x1fa30000 && p + size <= 0x1fa30004) ||
        (p >= 0x1fa40000 && p + size <= 0x1fa40004) ||
        (p >= 0x1fa51c00 && p + size <= 0x1fa51e00) ||
        (p >= 0x1fa60000 && p + size <= 0x1fa60002)) return true;
    if (p >= 0x1faf0000 && p + size <= 0x1faf0800) {
        for (unsigned i = 0; i < size; ++i)
            eeprom_.write(p - 0x1faf0000 + i, uint8_t(value >> (8 * i)), cpu_cycles_);
        return true;
    }
    if (p >= 0x1f000000 && p <= 0x1f7fffff) return true;
    if (p >= 0x1fa00000 && p <= 0x1fbfffff) return true;
    return false;
}

uint8_t CapcomZn2Board::sound_program_read(uint16_t address) const {
    const auto &rom = roms_->at("audio_cpu");
    if (address < 0x8000) return rom[address];
    if (address >= 0x8000 && address < 0xc000) {
        const size_t offset = 0x8000 + size_t(sound_bank_) * 0x4000 + (address - 0x8000);
        return offset < rom.size() ? rom[offset] : 0xff;
    }
    if (address == 0xd007) return qsound_response_;
    if (address == 0xf000 || address > 0xf000) return sound_ram_[address - 0xf000];
    return 0xff;
}
void CapcomZn2Board::sound_program_write(uint16_t address, uint8_t value) {
    if (address >= 0xf000) { sound_ram_[address - 0xf000] = value; return; }
    if (address >= 0xd000 && address <= 0xd002) { qsound_registers_[address - 0xd000] = value; return; }
    if (address == 0xd003) sound_bank_ = value & 0x0f;
}
uint8_t CapcomZn2Board::sound_port_read(uint8_t port) const {
    return port == 0 ? sound_latch_ : 0xff;
}
void CapcomZn2Board::sound_port_write(uint8_t, uint8_t) {}

RaizingZn1Board::RaizingZn1Board(const RomRegions &roms)
    : roms_(&roms), security_a_(security_key(roms.at("security_bios"))),
      security_b_(security_key(roms.at("security"))),
      eeprom_(16'934'400, roms.at("eeprom")) {}
PsxBus RaizingZn1Board::make_bus() {
    PsxBus bus{}; bus.opaque = this; bus.read = read_cb; bus.write = write_cb;
    bus.serial_clock = serial_clock_cb; bus.serial_data = serial_data_cb;
    bus.serial_read = serial_read_cb; bus.serial_dsr = serial_dsr_cb;
    bus.advance_serial = advance_serial_cb;
    bus.serial_next_event_cycle = serial_next_event_cycle_cb;
    bus.set_cpu_cycles = cpu_cycles_cb;
    return bus;
}
void RaizingZn1Board::cpu_cycles_cb(void *opaque, uint64_t cycles) {
    auto *self = static_cast<RaizingZn1Board *>(opaque);
    self->cpu_cycles_ = cycles;
    self->eeprom_.advance(cycles);
}
void RaizingZn1Board::serial_clock_cb(void *opaque, int state) {
    auto *self = static_cast<RaizingZn1Board *>(opaque);
    self->security_a_.clock(state != 0); self->security_b_.clock(state != 0);
    self->mcu_.clock(state);
}
void RaizingZn1Board::serial_data_cb(void *opaque, int state) {
    auto *self = static_cast<RaizingZn1Board *>(opaque);
    self->security_a_.data_in(state != 0); self->security_b_.data_in(state != 0);
}
int RaizingZn1Board::serial_read_cb(void *opaque) {
    auto *self = static_cast<RaizingZn1Board *>(opaque);
    return self->security_a_.data_out() && self->security_b_.data_out() &&
           self->mcu_.data_out();
}
int RaizingZn1Board::serial_dsr_cb(void *opaque) {
    return static_cast<RaizingZn1Board *>(opaque)->mcu_.dsr();
}
void RaizingZn1Board::advance_serial_cb(void *opaque, uint64_t cycles) {
    static_cast<RaizingZn1Board *>(opaque)->mcu_.advance(cycles);
}
uint64_t RaizingZn1Board::serial_next_event_cycle_cb(void *opaque) {
    return static_cast<RaizingZn1Board *>(opaque)->mcu_.next_event_cycle();
}
bool RaizingZn1Board::read_cb(void *opaque, uint32_t address, unsigned size,
                              uint32_t, uint32_t *value) {
    return static_cast<RaizingZn1Board *>(opaque)->read(address, size, value);
}
bool RaizingZn1Board::write_cb(void *opaque, uint32_t address, unsigned size,
                               uint32_t value, uint32_t) {
    return static_cast<RaizingZn1Board *>(opaque)->write(address, size, value);
}
bool RaizingZn1Board::read(uint32_t address, unsigned size, uint32_t *value) {
    if (!value || (size != 1 && size != 2 && size != 4)) return false;
    const uint32_t p = address & 0x1fffffff;
    auto read_region = [&](const char *region, size_t offset) {
        const auto &bytes = roms_->at(region);
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) {
            if (offset + i >= bytes.size()) return false;
            result |= uint32_t(bytes[offset + i]) << (8 * i);
        }
        *value = result; return true;
    };
    const auto read_input = [&](uint32_t base, size_t port) {
        if (p < base || p > base + 3) return false;
        const uint32_t input = input_ports_[port];
        *value = size == 1 ? input : size == 2 ? (0xff00u | input)
                                                : (0xffffff00u | input);
        return true;
    };
    if (p >= 0x1f000000 && p <= 0x1f7fffff)
        return read_region("program", size_t(bank_) * 0x800000 + (p - 0x1f000000));
    if (read_input(0x1fa00000, 0)) return true;
    if (read_input(0x1fa00100, 1)) return true;
    if (read_input(0x1fa00200, 2)) return true;
    if (read_input(0x1fa00300, 3)) return true;
    if (read_input(0x1fa10000, 4)) return true;
    if (read_input(0x1fa10100, 5)) return true;
    if (p >= 0x1fa00000 && p <= 0x1fa00303) { *value = 0xffffffff; return true; }
    if (p >= 0x1fa10000 && p <= 0x1fa10103) { *value = 0xffffffff; return true; }
    if (p == 0x1fa10200) { *value = 0x69; return true; }
    if (p == 0x1fa10300) { *value = security_select_; return true; }
    if (p == 0x1fa20000) { *value = 0; return true; }
    if (p >= 0x1fa30000 && p + size <= 0x1fa30004) { *value = 0; return true; }
    if (p >= 0x1fa40000 && p + size <= 0x1fa40004) { *value = 0; return true; }
    if (p >= 0x1faf0000 && p <= 0x1faf07ff) {
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) {
            const size_t offset = p - 0x1faf0000 + i;
            result |= uint32_t(eeprom_.read(offset, cpu_cycles_)) << (i * 8);
        }
        *value = result;
        return true;
    }
    if (p >= 0x1fb20000 && p <= 0x1fb20007) { *value = 0xffffffff; return true; }
    return false;
}
bool RaizingZn1Board::write(uint32_t address, unsigned size, uint32_t value) {
    if (size != 1 && size != 2 && size != 4) return false;
    const uint32_t p = address & 0x1fffffff;
    const uint8_t data = static_cast<uint8_t>(value);
    if (p == 0x1fa10300) {
        security_select_ = data;
        security_a_.select((data & 4) != 0);
        security_b_.select((data & 8) != 0);
        mcu_.set_analog_select((data & 0x10) != 0);
        mcu_.set_trackball_select((data & 0x20) != 0);
        mcu_.select((data & 0x8c) != 0x8c);
        bank_ = data & 3;
        return true;
    }
    if (p == 0x1fb00000) {
        sound_latch_ = data;
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 128)
                std::cerr << "ZN sound latch write data=0x" << std::hex
                          << unsigned(data) << std::dec << '\n';
        }
        return true;
    }
    if (p == 0x1fb00004) {
        sound_irq_pending_ = true;
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 128)
                std::cerr << "ZN sound IRQ2 write data=0x" << std::hex
                          << unsigned(data) << std::dec << '\n';
        }
        return true;
    }
    if ((p >= 0x1fa30000 && p + size <= 0x1fa30004) ||
        (p >= 0x1fa40000 && p + size <= 0x1fa40004)) return true;
    if (p == 0x1fa20000) return true;
    if (p >= 0x1faf0000 && p + size <= 0x1faf0800) {
        for (unsigned i = 0; i < size; ++i)
            eeprom_.write(p - 0x1faf0000 + i, uint8_t(value >> (8 * i)), cpu_cycles_);
        return true;
    }
    if (p >= 0x1f000000 && p <= 0x1f7fffff) return true;
    if (p >= 0x1fa00000 && p <= 0x1fbfffff) return true;
    return false;
}

} // namespace mamerecomp::zn
