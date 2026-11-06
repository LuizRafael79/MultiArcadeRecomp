#include "dma.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace mamerecomp::psx {

DmaController::DmaController(std::vector<uint8_t> &ram, Irq irq)
    : ram_(ram), irq_(std::move(irq)) {}

void DmaController::set_device_write(unsigned channel, DeviceWrite callback)
{ if (channel < 7) device_write_[channel] = std::move(callback); }
void DmaController::set_device_read(unsigned channel, DeviceRead callback)
{ if (channel < 7) device_read_[channel] = std::move(callback); }

void DmaController::advance_to(uint64_t cpu_cycles, uint64_t cpu_hz)
{
    current_cpu_cycles_ = cpu_cycles;
    if (cpu_hz) cpu_hz_ = cpu_hz;
    for (unsigned channel = 0; channel < 7; ++channel)
        if (completion_cycles_[channel] && cpu_cycles >= completion_cycles_[channel]) {
            completion_cycles_[channel] = 0;
            if (deferred_write_[channel]) {
                deferred_write_[channel] = false;
                auto words = transfer_words(channel, deferred_count_[channel],
                                            deferred_decrement_[channel]);
                if (device_write_[channel])
                    device_write_[channel](deferred_address_[channel], words);
            }
            complete(channel);
        }
}

uint64_t DmaController::next_event_cycle() const
{
    uint64_t next = UINT64_MAX;
    for (const uint64_t cycle : completion_cycles_)
        if (cycle) next = std::min(next, cycle);
    return next;
}

uint32_t DmaController::ram_word(uint32_t address) const
{
    if (ram_.empty()) return 0xffffffff;
    const size_t at = (address & (uint32_t(ram_.size()) - 1)) & ~size_t(3);
    if (at + 4 > ram_.size()) return 0xffffffff;
    return uint32_t(ram_[at]) | (uint32_t(ram_[at + 1]) << 8) |
           (uint32_t(ram_[at + 2]) << 16) | (uint32_t(ram_[at + 3]) << 24);
}

void DmaController::set_ram_word(uint32_t address, uint32_t value)
{
    static const bool trace_pointer_writes = std::getenv("MAMERECOMP_ZN_TRACE_POINTER_WRITES") != nullptr;
    if (ram_.empty()) return;
    const size_t at = (address & (uint32_t(ram_.size()) - 1)) & ~size_t(3);
    if (at + 4 > ram_.size()) return;
    if (trace_pointer_writes) {
        static const uint32_t watched = [] {
            const char *text = std::getenv("MAMERECOMP_ZN_WATCH_WRITE_ADDRESS");
            return text ? static_cast<uint32_t>(std::strtoul(text, nullptr, 0)) : 0xffffffffu;
        }();
        const size_t watched_at = watched & (uint32_t(ram_.size()) - 1);
        if (at >= watched_at && at - watched_at < 4)
            std::cerr << "ZN DMA write address=0x" << std::hex << address
                      << " value=0x" << value << std::dec << '\n';
    }
    for (unsigned i = 0; i < 4; ++i) ram_[at + i] = uint8_t(value >> (8 * i));
}

std::vector<uint32_t> DmaController::transfer_words(unsigned channel,
                                                     uint32_t count,
                                                     bool decrement) const
{
    std::vector<uint32_t> words;
    words.reserve(count);
    uint32_t address = channels_[channel].base & 0x00fffffc;
    for (uint32_t i = 0; i < count; ++i) {
        words.push_back(ram_word(address));
        address = decrement ? address - 4 : address + 4;
    }
    return words;
}

void DmaController::complete(unsigned channel)
{
    channels_[channel].control &= ~((1u << 24) | (1u << 28));
    dicr_ |= 1u << (24 + channel);
    update_irq();
}

void DmaController::update_irq()
{
    const uint32_t flags = (dicr_ >> 24) & 0x7f;
    const uint32_t masks = (dicr_ >> 16) & 0xff;
    const bool active = (masks & 0x80) && (flags & masks & 0x7f);
    if (active) dicr_ |= 0x80000000;
    else dicr_ &= ~0x80000000u;
    if (irq_) irq_(3, active); // PSX DMA IRQ source.
}

void DmaController::start(unsigned channel)
{
    Channel &c = channels_[channel];
    const bool trace_gpu_dma = channel == 2 && std::getenv("MAMERECOMP_ZN_TRACE_DMA");
    if (trace_gpu_dma)
        std::cerr << "ZN DMA2 start base=0x" << std::hex << c.base
                  << " block=0x" << c.block << " control=0x" << c.control
                  << " dpcr=0x" << dpcr_ << std::dec << '\n';
    if (!(dpcr_ & (1u << (3 + channel * 4)))) return;
    const unsigned sync = (c.control >> 9) & 3;
    const bool decrement = (c.control & 2) != 0;
    const bool to_device = (c.control & 1) != 0;
    const auto schedule_completion = [&](uint64_t ticks) {
        const uint64_t delay = (ticks * cpu_hz_ + 33'868'799ull) / 33'868'800ull;
        completion_cycles_[channel] = current_cpu_cycles_ + delay;
    };

    if (channel == 1 && c.control == 0x01000200u) {
        uint32_t count = c.block & 0xffff;
        uint32_t blocks = c.block >> 16;
        if (!count) count = 0x10000;
        if (!blocks) blocks = 0x10000;
        count = std::min<uint32_t>(count * blocks, 0x100000);
        if (device_read_[channel]) {
            const uint32_t address = c.base & 0x00fffffc;
            auto words = device_read_[channel](address, count);
            uint32_t at = address;
            for (uint32_t i = 0; i < std::min<uint32_t>(count, words.size()); ++i, at += 4)
                set_ram_word(at, words[i]);
        }
        schedule_completion(26'000);
        return;
    }

    if (channel == 4 && c.control == 0x01000201u) {
        uint32_t count = c.block & 0xffff;
        uint32_t blocks = c.block >> 16;
        if (!count) count = 0x10000;
        if (!blocks) blocks = 0x10000;
        count = std::min<uint32_t>(count * blocks, 0x100000);
        deferred_write_[channel] = true;
        deferred_address_[channel] = c.base & 0x00fffffc;
        deferred_count_[channel] = count;
        deferred_decrement_[channel] = decrement;
        schedule_completion(24'000);
        return;
    }

    if (channel == 6 && c.control == 0x11000002u) {
        uint32_t address = c.base & 0x00fffffc;
        uint32_t count = c.block & 0xffff;
        if (!count) count = 0x10000;
        for (uint32_t i = 1; i < count; ++i) {
            const uint32_t next = (address - 4) & 0x00ffffff;
            set_ram_word(address, next);
            address = next;
        }
        set_ram_word(address, 0x00ffffff);
        // MAME leaves channel 6 busy for 2150 ticks after the OTC list is
        // written. The RAM links are visible immediately, but completion and
        // the DMA IRQ remain scheduler events.
        schedule_completion(2'150);
        return;
    }

    if (channel == 2 && to_device && sync == 2) {
        uint32_t address = c.base & 0x00fffffc;
        const bool has_linked_list = address != 0x00ffffff;
        uint32_t total_words = 0;
        size_t nodes = 0;
        while (address != 0x00ffffff) {
            if (total_words > 65535) {
                c.base = address;
                // Match MAME's malformed/oversized-list completion delay
                // (not the normal 500-tick list terminator path).
                schedule_completion(16'000);
                return;
            }
            address &= uint32_t(ram_.size() - 1);
            const uint32_t header = ram_word(address);
            const uint32_t count = header >> 24;
            const uint32_t next = header & 0x00ffffff;
            if (trace_gpu_dma && nodes++ < 64)
                std::cerr << "ZN DMA2 node address=0x" << std::hex << address
                          << " header=0x" << header << " count=0x" << count
                          << " next=0x" << next << std::dec << '\n';
            std::vector<uint32_t> words;
            words.reserve(count);
            uint32_t payload = address + 4;
            for (uint32_t i = 0; i < count; ++i, payload += 4)
                words.push_back(ram_word(payload));
            if (device_write_[channel] && !words.empty()) device_write_[channel](payload - count * 4, words);
            if (next != 0x00ffffff) {
                const uint32_t next_ram_address = next & uint32_t(ram_.size() - 1);
                if (address == ram_word(next_ram_address) || address == next_ram_address)
                    break;
            }
            address = next;
            total_words += count + 1;
        }
        c.base = address;
        if (has_linked_list) {
            // MAME's PSX DMA schedules channel 2 completion 500 ticks after
            // the linked-list terminator. Keep CHCR busy until that timer fires.
            schedule_completion(500);
        } else {
            complete(channel);
        }
        return;
    }

    uint32_t count = c.block & 0xffff;
    if (sync == 1) {
        uint32_t blocks = c.block >> 16;
        if (!blocks) blocks = 0x10000;
        if (!count) count = 0x10000;
        count *= blocks;
    } else if (!count) count = 0x10000;
    count = std::min<uint32_t>(count, 0x100000);

    if (to_device) {
        auto words = transfer_words(channel, count, decrement);
        if (device_write_[channel]) device_write_[channel](c.base & 0x00fffffc, words);
        uint32_t address = c.base & 0x00fffffc;
        if (count) address = decrement ? address - count * 4 : address + count * 4;
        c.base = address & 0x00ffffff;
    } else if (device_read_[channel]) {
        const uint32_t transfer_address = c.base & 0x00fffffc;
        auto words = device_read_[channel](transfer_address, count);
        uint32_t address = c.base & 0x00fffffc;
        for (uint32_t i = 0; i < std::min<uint32_t>(count, words.size()); ++i) {
            set_ram_word(address, words[i]);
            address = decrement ? address - 4 : address + 4;
        }
        c.base = address & 0x00ffffff;
    }
    complete(channel);
}

uint32_t DmaController::read(unsigned offset) const
{
    const unsigned channel = offset / 4;
    const unsigned reg = offset % 4;
    if (channel < 7) {
        if (reg == 0) return channels_[channel].base;
        if (reg == 1) return channels_[channel].block;
        return channels_[channel].control;
    }
    if (reg == 0) return dpcr_;
    if (reg == 1) return dicr_;
    return 0;
}

void DmaController::write(unsigned offset, uint32_t value)
{
    const unsigned channel = offset / 4;
    const unsigned reg = offset % 4;
    if (channel < 7) {
        if (reg == 0) channels_[channel].base = value & 0x00ffffff;
        else if (reg == 1) channels_[channel].block = value;
        else {
            channels_[channel].control = value;
            if ((value & (1u << 24)) && (channel != 6 || (value & (1u << 28))))
                start(channel);
        }
        return;
    }
    if (reg == 0) {
        dpcr_ = value;
        for (unsigned i = 0; i < 7; ++i)
            if ((channels_[i].control & (1u << 24)) && (dpcr_ & (1u << (3 + i * 4)))) start(i);
    } else if (reg == 1) {
        dicr_ = (dicr_ & 0x80000000u) | (dicr_ & ~value & 0x7f000000u) |
                (value & 0x00ff803fu);
        update_irq();
    }
}

} // namespace mamerecomp::psx
