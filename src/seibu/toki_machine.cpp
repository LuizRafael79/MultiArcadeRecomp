#include "toki_machine.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace mamerecomp::seibu {
namespace {
struct RomLoad {
    const char* name;
    const char* region;
    size_t length;
    size_t destination;
    unsigned group;
    unsigned stride;
    uint32_t crc;
};

// MAME's toki ROM_START (world set 1), including ROM_LOAD16_BYTE lane wiring.
constexpr RomLoad loads[] = {
    {"6e.m10", "maincpu", 0x20000, 0x00000, 1, 2, 0x94015d91},
    {"4e.k10", "maincpu", 0x20000, 0x00001, 1, 2, 0x531bd3ef},
    {"5.m12", "maincpu", 0x10000, 0x40000, 1, 2, 0xd6a82808},
    {"3.k12", "maincpu", 0x10000, 0x40001, 1, 2, 0xa01a5b10},
    {"8.m3", "audiocpu", 0x02000, 0x00000, 1, 1, 0x6c87c4c5},
    {"7.m7", "audiocpu", 0x10000, 0x10000, 1, 1, 0xa67969c4},
    {"1.c5", "chars", 0x10000, 0, 1, 1, 0x8aa964a2},
    {"2.c3", "chars", 0x10000, 0x10000, 1, 1, 0x86e87e48},
    {"toki_obj1.c20", "sprites", 0x80000, 0, 1, 1, 0xa27a80ba},
    {"toki_obj2.c22", "sprites", 0x80000, 0x80000, 1, 1, 0xfa687718},
    {"toki_bk1.cd8", "bgtiles", 0x80000, 0, 1, 1, 0xfdaa5f4b},
    {"toki_bk2.ef8", "fgtiles", 0x80000, 0, 1, 1, 0xd86ac664},
    {"9.m1", "oki", 0x20000, 0, 1, 1, 0xae7a6b8b},
    {"prom26.b6", "proms", 0x100, 0, 1, 1, 0xea6312c6},
    {"prom27.j3", "proms", 0x100, 0x100, 1, 1, 0xe616ae85},
};

uint32_t crc32_bytes(const std::vector<uint8_t>& bytes)
{
    return static_cast<uint32_t>(::crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}

uint8_t bitswap8(uint8_t value, const std::array<unsigned, 8>& order)
{
    uint8_t result = 0;
    for (unsigned bit = 0; bit < 8; ++bit)
        result |= ((value >> order[bit]) & 1) << (7 - bit);
    return result;
}

bool bit(uint16_t value, unsigned index) { return (value >> index) & 1; }

void trace_sound_port(char width, uint32_t address, uint16_t value)
{
    static unsigned count = 0;
    const char* path = std::getenv("MAMERECOMP_TOKI_SOUND_TRACE");
    if (!path || !*path || count >= 256) return;
    std::fprintf(stderr, "TOKI_SOUND_BUS %c addr=%06x data=%04x\n", width, address, value);
    ++count;
}

void trace_sound_cpu_port(char access, uint16_t address, uint8_t value)
{
    static unsigned count = 0;
    const char* path = std::getenv("MAMERECOMP_TOKI_SOUND_TRACE");
    if (!path || !*path || count >= 160) return;
    std::fprintf(stderr, "TOKI_SOUND_CPU %c addr=%04x data=%02x\n", access, address, value);
    ++count;
}

} // namespace

const std::vector<uint8_t>& TokiRomSet::at(std::string_view region) const
{
    const auto found = regions.find(std::string(region));
    if (found == regions.end()) throw std::runtime_error("missing Toki ROM region " + std::string(region));
    return found->second;
}

TokiRomSet load_toki_romset(const std::filesystem::path& zip)
{
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) throw std::runtime_error("could not allocate Toki ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open Toki ROM ZIP " + zip.string());

    std::array<std::vector<uint8_t>, std::size(loads)> files;
    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* path = archive_entry_pathname(entry);
        const auto name = path ? std::filesystem::path(path).filename().string() : std::string{};
        size_t index = std::size(loads);
        for (size_t i = 0; i < std::size(loads); ++i)
            if (name == loads[i].name) { index = i; break; }
        if (index == std::size(loads) || !files[index].empty()) {
            archive_read_data_skip(reader.get());
            continue;
        }
        if (archive_entry_size(entry) != static_cast<la_int64_t>(loads[index].length))
            throw std::runtime_error(name + " has an unexpected size in Toki ZIP");
        auto& bytes = files[index];
        bytes.resize(loads[index].length);
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = archive_read_data(reader.get(), bytes.data() + offset, bytes.size() - offset);
            if (n <= 0) throw std::runtime_error("truncated Toki ROM " + name);
            offset += static_cast<size_t>(n);
        }
        if (crc32_bytes(bytes) != loads[index].crc)
            throw std::runtime_error(name + " CRC does not match MAME toki ROM metadata");
    }
    if (result != ARCHIVE_EOF) throw std::runtime_error("error reading Toki ZIP " + zip.string());

    TokiRomSet out;
    out.regions.emplace("maincpu", std::vector<uint8_t>(0x60000, 0xff));
    out.regions.emplace("audiocpu", std::vector<uint8_t>(0x20000, 0xff));
    out.regions.emplace("chars", std::vector<uint8_t>(0x20000, 0xff));
    out.regions.emplace("sprites", std::vector<uint8_t>(0x100000, 0xff));
    out.regions.emplace("bgtiles", std::vector<uint8_t>(0x80000, 0xff));
    out.regions.emplace("fgtiles", std::vector<uint8_t>(0x80000, 0xff));
    out.regions.emplace("oki", std::vector<uint8_t>(0x40000, 0xff));
    out.regions.emplace("proms", std::vector<uint8_t>(0x200, 0xff));
    for (size_t i = 0; i < std::size(loads); ++i) {
        if (files[i].empty()) throw std::runtime_error(std::string("missing Toki ROM ") + loads[i].name);
        auto& destination = out.regions.at(loads[i].region);
        const auto& source = files[i];
        for (size_t byte = 0; byte < source.size(); ++byte) {
            const size_t at = loads[i].destination + (byte / loads[i].group) * loads[i].stride + byte % loads[i].group;
            if (at >= destination.size()) throw std::runtime_error(std::string(loads[i].name) + " exceeds its MAME region");
            destination[at] = source[byte];
        }
    }
    return out;
}

uint8_t toki_sei80bu_data(uint16_t address, uint8_t value)
{
    const auto a = address;
    if (bit(a, 9) && bit(a, 8)) value ^= 0x80;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) value ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) value ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) value ^= 0x01;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) value ^= 0x40;
    if (bit(a, 13) && bit(a, 4)) value = bitswap8(value, {7,6,5,4,3,2,0,1});
    if (bit(a, 8) && bit(a, 4)) value = bitswap8(value, {7,6,5,4,2,3,1,0});
    return value;
}

uint8_t toki_sei80bu_opcode(uint16_t address, uint8_t value)
{
    const auto a = address;
    if (bit(a, 9) && bit(a, 8)) value ^= 0x80;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) value ^= 0x40;
    if (!bit(a, 13) && bit(a, 12)) value ^= 0x20;
    if (!bit(a, 6) && bit(a, 1)) value ^= 0x10;
    if (!bit(a, 12) && bit(a, 2)) value ^= 0x08;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) value ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) value ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) value ^= 0x01;
    if (bit(a, 13) && bit(a, 4)) value = bitswap8(value, {7,6,5,4,3,2,0,1});
    if (bit(a, 8) && bit(a, 4)) value = bitswap8(value, {7,6,5,4,2,3,1,0});
    if (bit(a, 12) && bit(a, 9)) value = bitswap8(value, {7,6,4,5,3,2,1,0});
    if (bit(a, 11) && !bit(a, 6)) value = bitswap8(value, {6,7,5,4,3,2,1,0});
    return value;
}

TokiBoard::TokiBoard(TokiRomSet roms)
    : roms_(std::move(roms)), ym3812_(*this), oki_(roms_.at("oki"))
{
    const auto& audio = roms_.at("audiocpu");
    std::copy_n(audio.begin() + 0x10000, 0x10000, sound_bank_.begin());
    ym3812_.reset();
}

uint16_t TokiBoard::read_be16(const std::vector<uint8_t>& bytes, size_t offset) const
{
    if (offset + 1 >= bytes.size()) return 0xffff;
    return static_cast<uint16_t>((uint16_t(bytes[offset]) << 8) | bytes[offset + 1]);
}

void TokiBoard::write_be16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value, uint16_t mask)
{
    if (offset + 1 >= bytes.size()) return;
    uint16_t old = read_be16(bytes, offset);
    old = static_cast<uint16_t>((old & ~mask) | (value & mask));
    bytes[offset] = static_cast<uint8_t>(old >> 8);
    bytes[offset + 1] = static_cast<uint8_t>(old);
}

uint8_t TokiBoard::main_read8(uint32_t address)
{
    if (address < 0x60000) return roms_.at("maincpu")[address];
    if (address >= 0x60000 && address <= 0x6d7ff) return work_ram_[address - 0x60000];
    if (address >= 0x6d800 && address <= 0x6dfff) return sprite_ram_[address - 0x6d800];
    if (address >= 0x6e000 && address <= 0x6e7ff) return palette_ram_[address - 0x6e000];
    if (address >= 0x6e800 && address <= 0x6efff) return tile_ram_[0][address - 0x6e800];
    if (address >= 0x6f000 && address <= 0x6f7ff) return tile_ram_[1][address - 0x6f000];
    if (address >= 0x6f800 && address <= 0x6ffff) return text_ram_[address - 0x6f800];
    if (address >= 0x80000 && address <= 0x8000d) {
        // MAME maps this 16-bit port with umask16(0x00ff): the low byte is
        // physically on the odd address, and handler offsets count words.
        if (!(address & 1)) return 0xff;
        const unsigned offset = (address - 0x80000) >> 1;
        if (offset == 2 || offset == 3) return static_cast<uint8_t>(sound_to_main_[offset - 2]);
        if (offset == 5) return main_to_sound_pending_ ? 1 : 0;
        return 0xff;
    }
    if (address >= 0xc0000 && address <= 0xc0005) {
        const uint16_t value = address < 0xc0002 ? input_dsw_ : address < 0xc0004 ? input_controls_ : input_system_;
        return (address & 1) ? static_cast<uint8_t>(value) : static_cast<uint8_t>(value >> 8);
    }
    return 0xff;
}

uint16_t TokiBoard::main_read16(uint32_t address)
{
    address &= ~1u;
    if (address >= 0x60000 && address <= 0x6d7fe) return read_be16(work_ram_, address - 0x60000);
    if (address >= 0x6d800 && address <= 0x6dffe) return read_be16(sprite_ram_, address - 0x6d800);
    if (address >= 0x6e000 && address <= 0x6e7fe) return read_be16(palette_ram_, address - 0x6e000);
    if (address >= 0x6e800 && address <= 0x6effe) return read_be16(tile_ram_[0], address - 0x6e800);
    if (address >= 0x6f000 && address <= 0x6f7fe) return read_be16(tile_ram_[1], address - 0x6f000);
    if (address >= 0x6f800 && address <= 0x6fffe) return read_be16(text_ram_, address - 0x6f800);
    if (address >= 0x80000 && address <= 0x8000c) {
        const unsigned offset = (address - 0x80000) >> 1;
        if (offset == 2 || offset == 3)
            return static_cast<uint16_t>(0xff00 | static_cast<uint8_t>(sound_to_main_[offset - 2]));
        if (offset == 5) return static_cast<uint16_t>(0xff00 | (main_to_sound_pending_ ? 1 : 0));
        return 0xffff;
    }
    return static_cast<uint16_t>((uint16_t(main_read8(address)) << 8) | main_read8(address + 1));
}

void TokiBoard::main_write8(uint32_t address, uint8_t value)
{
    if (address >= 0x60000 && address <= 0x6d7ff) work_ram_[address - 0x60000] = value;
    else if (address >= 0x6d800 && address <= 0x6dfff) sprite_ram_[address - 0x6d800] = value;
    else if (address >= 0x6e000 && address <= 0x6e7ff) palette_ram_[address - 0x6e000] = value;
    else if (address >= 0x6e800 && address <= 0x6efff) tile_ram_[0][address - 0x6e800] = value;
    else if (address >= 0x6f000 && address <= 0x6f7ff) tile_ram_[1][address - 0x6f000] = value;
    else if (address >= 0x6f800 && address <= 0x6ffff) text_ram_[address - 0x6f800] = value;
    else if (address >= 0x80000 && address <= 0x8000d) {
        trace_sound_port('B', address, value);
        if (!(address & 1)) return;
        const unsigned offset = (address - 0x80000) >> 1;
        if (offset < 2) { main_to_sound_[offset] = value; ++sound_command_writes_; }
        if (offset == 4) { rst18_irq_ = true; ++sound_irq_assertions_; }
        if (offset == 2 || offset == 6) {
            sound_to_main_pending_ = false;
            main_to_sound_pending_ = true;
        }
    }
    else if (address >= 0xa0000 && address <= 0xa005f) {
        const unsigned offset = (address - 0xa0000) >> 1;
        const unsigned shift = (address & 1) ? 0 : 8;
        scroll_ram_[offset] = static_cast<uint16_t>((scroll_ram_[offset] & ~(0xffu << shift)) | (uint16_t(value) << shift));
    }
}

void TokiBoard::main_write16(uint32_t address, uint16_t value)
{
    address &= ~1u;
    if (address >= 0x60000 && address <= 0x6d7fe) write_be16(work_ram_, address - 0x60000, value);
    else if (address >= 0x6d800 && address <= 0x6dffe) write_be16(sprite_ram_, address - 0x6d800, value);
    else if (address >= 0x6e000 && address <= 0x6e7fe) write_be16(palette_ram_, address - 0x6e000, value);
    else if (address >= 0x6e800 && address <= 0x6effe) write_be16(tile_ram_[0], address - 0x6e800, value);
    else if (address >= 0x6f000 && address <= 0x6f7fe) write_be16(tile_ram_[1], address - 0x6f000, value);
    else if (address >= 0x6f800 && address <= 0x6fffe) write_be16(text_ram_, address - 0x6f800, value);
    else if (address >= 0xa0000 && address <= 0xa005e) scroll_ram_[(address - 0xa0000) >> 1] = value;
    else if (address >= 0x80000 && address <= 0x8000c) {
        trace_sound_port('W', address, value);
        const unsigned offset = (address - 0x80000) >> 1;
        if (offset < 2) {
            main_to_sound_[offset] = static_cast<uint8_t>(value);
            last_sound_bus_word_ = value;
            if (value & 0x00ff) ++sound_low_lane_nonzero_;
            if (value & 0xff00) ++sound_high_lane_nonzero_;
            ++sound_command_writes_;
        }
        else if (offset == 4) { rst18_irq_ = true; ++sound_irq_assertions_; }
        else if (offset == 2 || offset == 6) { sound_to_main_pending_ = false; main_to_sound_pending_ = true; }
    }
}

void TokiBoard::video_vblank()
{
    sprite_buffer_ = sprite_ram_;
    ++main_irq_count_;
    main_irq_pending_ = true;
}

uint8_t TokiBoard::sound_read(uint16_t address, bool opcode_fetch)
{
    const auto& audio = roms_.at("audiocpu");
    if (address < 0x2000) {
        const uint8_t source = audio[address];
        return opcode_fetch ? toki_sei80bu_opcode(address, source) : toki_sei80bu_data(address, source);
    }
    if (address >= 0x2000 && address <= 0x27ff) return sound_ram_[address - 0x2000];
    if (address >= 0x4008 && address <= 0x4009) {
        const uint8_t value = ym3812_.read(address - 0x4008);
        trace_sound_cpu_port('R', address, value);
        return value;
    }
    if (address == 0x6000) {
        const uint8_t value = oki_.read();
        trace_sound_cpu_port('R', address, value);
        return value;
    }
    if (address >= 0x4010 && address <= 0x4011) {
        const uint8_t value = static_cast<uint8_t>(main_to_sound_[address - 0x4010]);
        ++sound_latch_reads_;
        last_sound_latch_value_ = value;
        trace_sound_cpu_port('R', address, value);
        return value;
    }
    if (address == 0x4012) return sound_to_main_pending_ ? 1 : 0;
    if (address == 0x4013) return coin_inputs_;
    if (address >= 0x8000) return sound_bank_[address - 0x8000];
    return 0xff;
}

void TokiBoard::sound_write(uint16_t address, uint8_t value)
{
    if (address == 0x6000 || (address >= 0x4008 && address <= 0x4009))
        trace_sound_cpu_port('W', address, value);
    if (address >= 0x2000 && address <= 0x27ff) sound_ram_[address - 0x2000] = value;
    else if (address == 0x6000) { oki_.write(value); ++oki_write_count_; }
    else if (address == 0x4000) { main_to_sound_pending_ = false; sound_to_main_pending_ = true; ++sound_pending_acks_; }
    else if (address == 0x4001 || address == 0x4003) rst18_service_ = false;
    else if (address == 0x4002) rst10_service_ = false;
    else if (address == 0x4007) {
        sound_bank_select_ = value & 1;
        const size_t offset = 0x10000 + size_t(sound_bank_select_) * 0x8000;
        const auto& audio = roms_.at("audiocpu");
        if (offset + 0x8000 <= audio.size()) std::copy_n(audio.begin() + offset, 0x8000, sound_bank_.begin());
    }
    else if (address == 0x4018 || address == 0x4019) sound_to_main_[address - 0x4018] = value;
    else if (address >= 0x4008 && address <= 0x4009) {
        ym3812_.write(address - 0x4008, value);
        ++ym_write_count_;
        if (address == 0x4008) ym_address_ = value;
        else {
            ym_last_value_ = value;
            if (ym_address_ >= 0xb0 && ym_address_ <= 0xb8 && (value & 0x20)) ++ym_key_on_count_;
        }
    }
}

void TokiBoard::advance_sound_to_cycles(uint64_t cycles)
{
    // Toki's Z80 and YM3812 share the verified 14.318181 MHz / 4 clock.
    constexpr uint64_t clocks_per_sample = 72;
    while ((ym_generated_samples_ + 1) * clocks_per_sample <= cycles) {
        const uint64_t clock = (++ym_generated_samples_) * clocks_per_sample;
        ym_clock_ = clock;
        for (uint32_t timer = 0; timer < ym_timer_active_.size(); ++timer) {
            if (ym_timer_active_[timer] && ym_timer_deadlines_[timer] <= clock) {
                ym_timer_active_[timer] = false;
                ym3812_.timer_expired(timer);
            }
        }
        ymfm::ym3812::output_data output{};
        ym3812_.generate(&output, 1);
        output.clamp16();
        oki_clock_fraction_ += clocks_per_sample * 1'000'000ull;
        constexpr uint64_t oki_nibble_threshold = 3'579'545ull * 132ull;
        while (oki_clock_fraction_ >= oki_nibble_threshold) {
            oki_clock_fraction_ -= oki_nibble_threshold;
            oki_.clock_nibble();
        }
        const int mixed = static_cast<int>(output.data[0]) + static_cast<int>(oki_.output()) * 16;
        const int16_t sample = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        ym_peak_ = std::max<uint16_t>(ym_peak_, static_cast<uint16_t>(std::abs(int(sample))));
        audio_samples_.push_back(sample);
    }
    ym_clock_ = cycles;
}

std::vector<int16_t> TokiBoard::take_audio_samples()
{
    std::vector<int16_t> result;
    result.swap(audio_samples_);
    return result;
}

void TokiBoard::ymfm_set_timer(uint32_t timer, int32_t duration_in_clocks)
{
    if (timer >= ym_timer_active_.size()) return;
    if (duration_in_clocks < 0) {
        ym_timer_active_[timer] = false;
    } else {
        ym_timer_active_[timer] = true;
        ym_timer_deadlines_[timer] = ym_clock_ + static_cast<uint32_t>(duration_in_clocks);
    }
}

void TokiBoard::ymfm_set_busy_end(uint32_t clocks)
{
    ym_busy_until_ = ym_clock_ + clocks;
}

bool TokiBoard::ymfm_is_busy()
{
    return ym_clock_ < ym_busy_until_;
}

void TokiBoard::ymfm_update_irq(bool asserted)
{
    rst10_irq_ = asserted;
    if (!asserted) rst10_service_ = false;
}

void TokiBoard::ymfm_external_write(ymfm::access_class, uint32_t, uint8_t) { }

uint8_t TokiBoard::sound_irq_vector()
{
    if (rst18_irq_ && !rst18_service_) {
        rst18_service_ = true;
        rst18_irq_ = false;
        ++sound_irq18_acks_;
        return 0xdf;
    }
    if (rst10_irq_ && !rst10_service_) { rst10_service_ = true; ++sound_irq10_acks_; return 0xd7; }
    return 0xff;
}

void TokiBoard::sound_irq_acknowledge() { rst18_service_ = false; rst10_service_ = false; }

uint8_t TokiBoard::graphics_pixel(std::string_view region_name, bool chars, unsigned code,
                                   unsigned x, unsigned y) const
{
    const auto& region = roms_.at(region_name);
    const unsigned width = chars ? 8 : 16;
    const size_t char_stride_bits = chars ? 128 : 1024;
    const size_t count = chars ? 0x1000 : static_cast<unsigned>(region.size() * 8 / char_stride_bits);
    if (code >= count || x >= width || y >= width) return 0;
    const std::array<unsigned, 4> planes = chars
        ? std::array<unsigned, 4>{static_cast<unsigned>(region.size() * 4), static_cast<unsigned>(region.size() * 4 + 4), 0, 4}
        : std::array<unsigned, 4>{8, 12, 0, 4};
    size_t bit_offset = size_t(code) * char_stride_bits;
    if (chars) {
        const unsigned xoff = x < 4 ? 3 - x : 8 + 3 - (x - 4);
        bit_offset += y * 16 + xoff;
    } else {
        const unsigned xoff = x < 4 ? 3 - x : x < 8 ? 16 + 3 - (x - 4)
            : x < 12 ? 512 + 3 - (x - 8) : 528 + 3 - (x - 12);
        bit_offset += y * 32 + xoff;
    }
    uint8_t pixel = 0;
    for (unsigned plane = 0; plane < 4; ++plane) {
        size_t offset = bit_offset + planes[plane];
        if (chars && plane < 2) offset %= region.size() * 8;
        const size_t byte = offset >> 3;
        // MAME maps the first gfx plane to the most significant pen bit.
        if (byte < region.size()) pixel |= ((region[byte] >> (7 - (offset & 7))) & 1) << (3 - plane);
    }
    return pixel;
}

uint32_t TokiBoard::palette_rgb(unsigned index) const
{
    const uint16_t value = read_be16(palette_ram_, (index & 0x3ff) * 2);
    const uint8_t r = static_cast<uint8_t>((value & 0x0f) * 17);
    const uint8_t g = static_cast<uint8_t>(((value >> 4) & 0x0f) * 17);
    const uint8_t b = static_cast<uint8_t>(((value >> 8) & 0x0f) * 17);
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void TokiBoard::put_pixel(TokiFrame& frame, int x, int y, uint16_t index, bool transparent) const
{
    if (transparent || x < 0 || x >= 256 || y < 16 || y >= 240) return;
    const size_t offset = (size_t(y - 16) * 256 + x) * 3;
    const auto color = palette_rgb(index);
    frame.rgb[offset] = static_cast<uint8_t>(color >> 16);
    frame.rgb[offset + 1] = static_cast<uint8_t>(color >> 8);
    frame.rgb[offset + 2] = static_cast<uint8_t>(color);
}

void TokiBoard::draw_tile_layer(TokiFrame& frame, unsigned layer, bool opaque) const
{
    int scroll_x = 0, scroll_y = 0;
    bool flip = false;
    if (layer == 0) {
        scroll_x = ((scroll_ram_[6] & 0x7f) << 1) | ((scroll_ram_[6] & 0x80) >> 7) | ((scroll_ram_[5] & 0x10) << 4);
        scroll_y = ((scroll_ram_[0xd] & 0x10) << 4) + ((scroll_ram_[0xe] & 0x7f) << 1) + ((scroll_ram_[0xe] & 0x80) >> 7);
    } else if (layer == 1) {
        scroll_x = ((scroll_ram_[0x16] & 0x7f) << 1) | ((scroll_ram_[0x16] & 0x80) >> 7) | ((scroll_ram_[0x15] & 0x10) << 4);
        scroll_y = ((scroll_ram_[0x1d] & 0x10) << 4) + ((scroll_ram_[0x1e] & 0x7f) << 1) + ((scroll_ram_[0x1e] & 0x80) >> 7);
    }
    flip = (scroll_ram_[0x28] & 0x8000) == 0;
    const auto& ram = tile_ram_[layer];
    const std::string_view region = layer == 0 ? "bgtiles" : "fgtiles";
    const uint16_t palette_base = layer == 0 ? 32 * 16 : 48 * 16;
    for (int sy = 16; sy < 240; ++sy) for (int sx = 0; sx < 256; ++sx) {
        int px = flip ? 255 - sx : sx;
        int py = flip ? 255 - sy : sy;
        const int wx = (px + scroll_x) & 0x1ff;
        const int wy = (py + scroll_y) & 0x1ff;
        const unsigned tile = unsigned((wy >> 4) * 32 + (wx >> 4));
        const uint16_t attr = read_be16(ram, tile * 2);
        const uint8_t pixel = graphics_pixel(region, false, attr & 0x0fff, wx & 15, wy & 15);
        put_pixel(frame, sx, sy, palette_base + ((attr >> 12) & 15) * 16 + pixel, !opaque && pixel == 15);
    }
}

void TokiBoard::draw_sprites(TokiFrame& frame) const
{
    const bool flip = (scroll_ram_[0x28] & 0x8000) == 0;
    for (int offs = int(sprite_buffer_.size() / 2) - 4; offs >= 0; offs -= 4) {
        const uint16_t a = read_be16(sprite_buffer_, offs * 2);
        const uint16_t b = read_be16(sprite_buffer_, (offs + 1) * 2);
        const uint16_t c = read_be16(sprite_buffer_, (offs + 2) * 2);
        const uint16_t d = read_be16(sprite_buffer_, (offs + 3) * 2);
        if (c == 0xf000 || a == 0xffff) continue;
        int x = (c + (a & 0xf0)) & 0x1ff;
        int y = (d + ((a & 0x0f) << 4)) & 0x1ff;
        if (x > 256) x -= 512;
        if (y > 256) y -= 512;
        bool flip_x = (a & 0x100) != 0;
        bool flip_y = false;
        if (flip) { x = 240 - x; y = 240 - y; flip_x = !flip_x; flip_y = true; }
        const unsigned code = (b & 0x0fff) + ((c & 0x8000) >> 3);
        const unsigned color = b >> 12;
        for (unsigned py = 0; py < 16; ++py) for (unsigned px = 0; px < 16; ++px) {
            const uint8_t pixel = graphics_pixel("sprites", false, code, flip_x ? 15 - px : px, flip_y ? 15 - py : py);
            put_pixel(frame, x + int(px), y + int(py), color * 16 + pixel, pixel == 15);
        }
    }
}

TokiFrame TokiBoard::render_frame() const
{
    TokiFrame frame;
    frame.rgb.resize(TokiFrame::width * TokiFrame::height * 3);
    // MAME's xBGR444 palette, with palette RAM initially zeroed by the board.
    for (unsigned y = 0; y < 224; ++y) for (unsigned x = 0; x < 256; ++x) {
        const auto c = palette_rgb(0);
        const size_t at = (size_t(y) * 256 + x) * 3;
        frame.rgb[at] = c >> 16; frame.rgb[at + 1] = c >> 8; frame.rgb[at + 2] = c;
    }
    const bool bg_first = (scroll_ram_[0x28] & 0x100) != 0;
    draw_tile_layer(frame, bg_first ? 0 : 1, true);
    draw_tile_layer(frame, bg_first ? 1 : 0, false);
    draw_sprites(frame);
    const bool flip = (scroll_ram_[0x28] & 0x8000) == 0;
    for (int sy = 16; sy < 240; ++sy) for (int sx = 0; sx < 256; ++sx) {
        const int px = flip ? 255 - sx : sx;
        const int py = flip ? 255 - sy : sy;
        const unsigned tile = unsigned((py >> 3) * 32 + (px >> 3));
        const uint16_t attr = read_be16(text_ram_, tile * 2);
        const uint8_t pixel = graphics_pixel("chars", true, attr & 0x0fff, px & 7, py & 7);
        put_pixel(frame, sx, sy, 16 * 16 + ((attr >> 12) & 15) * 16 + pixel, pixel == 15);
    }
    return frame;
}

std::array<size_t, 5> TokiBoard::nonzero_video_bytes() const
{
    const auto count = [](const std::vector<uint8_t>& bytes) {
        return static_cast<size_t>(std::count_if(bytes.begin(), bytes.end(), [](uint8_t value) { return value != 0; }));
    };
    return {count(palette_ram_), count(sprite_buffer_), count(tile_ram_[0]),
            count(tile_ram_[1]), count(text_ram_)};
}

std::array<size_t, 2> TokiBoard::visible_tile_pixels() const
{
    std::array<size_t, 2> counts{};
    for (unsigned layer = 0; layer < 2; ++layer) {
        const auto& ram = tile_ram_[layer];
        const auto region = layer == 0 ? "bgtiles" : "fgtiles";
        const int sx_scroll = layer == 0
            ? ((scroll_ram_[6] & 0x7f) << 1) | ((scroll_ram_[6] & 0x80) >> 7) | ((scroll_ram_[5] & 0x10) << 4)
            : ((scroll_ram_[0x16] & 0x7f) << 1) | ((scroll_ram_[0x16] & 0x80) >> 7) | ((scroll_ram_[0x15] & 0x10) << 4);
        const int sy_scroll = layer == 0
            ? ((scroll_ram_[0xd] & 0x10) << 4) + ((scroll_ram_[0xe] & 0x7f) << 1) + ((scroll_ram_[0xe] & 0x80) >> 7)
            : ((scroll_ram_[0x1d] & 0x10) << 4) + ((scroll_ram_[0x1e] & 0x7f) << 1) + ((scroll_ram_[0x1e] & 0x80) >> 7);
        const bool flip = (scroll_ram_[0x28] & 0x8000) == 0;
        for (int sy = 16; sy < 240; ++sy) for (int sx = 0; sx < 256; ++sx) {
            const int px = flip ? 255 - sx : sx;
            const int py = flip ? 255 - sy : sy;
            const int wx = (px + sx_scroll) & 0x1ff;
            const int wy = (py + sy_scroll) & 0x1ff;
            const unsigned tile = unsigned((wy >> 4) * 32 + (wx >> 4));
            const uint16_t attr = read_be16(ram, tile * 2);
            if (graphics_pixel(region, false, attr & 0xfff, wx & 15, wy & 15) != 15) ++counts[layer];
        }
    }
    return counts;
}

std::array<size_t, 32> TokiBoard::text_tiles_per_row() const
{
    std::array<size_t, 32> counts{};
    for (unsigned tile = 0; tile < 1024; ++tile)
        if (read_be16(text_ram_, tile * 2) != 0) ++counts[tile / 32];
    return counts;
}

size_t TokiBoard::active_sprite_entries() const
{
    size_t count = 0;
    for (int offset = int(sprite_buffer_.size() / 2) - 4; offset >= 0; offset -= 4) {
        const auto a = read_be16(sprite_buffer_, size_t(offset) * 2);
        const auto c = read_be16(sprite_buffer_, size_t(offset + 2) * 2);
        if (c != 0xf000 && a != 0xffff) ++count;
    }
    return count;
}

} // namespace mamerecomp::seibu
