#include "machine_memory.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    mamerecomp::psx::MachineMemory memory(mamerecomp::psx::kZn2MemoryConfig);
    const PsxBus bus = memory.make_bus();
    auto write = [&](uint32_t address, uint32_t value) {
        assert(bus.write(bus.opaque, address, 4, value, 0));
    };
    auto read = [&](uint32_t address) {
        uint32_t value = 0;
        assert(bus.read(bus.opaque, address, 4, 0, &value));
        return value;
    };

    // OTC writes the backward RAM chain and terminator used by the BIOS.
    write(0x1f8010f0, 1u << 27); // DMA channel 6 enable
    write(0x1f8010f4, (1u << 23) | (1u << 22)); // DMA IRQ master + channel 6
    write(0x1f8010e0, 0x100);
    write(0x1f8010e4, 3);
    write(0x1f8010e8, 0x11000002);
    assert(read(0x100) == 0xfc);
    assert(read(0xfc) == 0xf8);
    assert(read(0xf8) == 0xf4);
    assert(read(0xf4) == 0x00ffffff);
    assert(read(0x1f8010e0) == 0xf4);
    assert((read(0x1f8010e8) & 0x11000000) == 0);
    assert((read(0x1f8010f4) & 0x80000000) != 0);
    assert((read(0x1f801070) & (1u << 3)) != 0);

    // A manually triggered RAM-to-device transfer preserves word order.
    std::vector<uint32_t> received;
    memory.dma().set_device_write(2, [&](uint32_t, std::span<const uint32_t> words) {
        received.assign(words.begin(), words.end());
    });
    write(0x200, 0x12345678);
    write(0x204, 0x90abcdef);
    write(0x1f8010f0, 1u << 11); // DMA channel 2 enable
    write(0x1f8010a0, 0x200);
    write(0x1f8010a4, 2);
    write(0x1f8010a8, 0x01000001);
    assert((received == std::vector<uint32_t>{0x12345678, 0x90abcdef}));

    // GPU-style linked-list transfer honors the 24-bit 0xffffff terminator.
    received.clear();
    write(0x300, (2u << 24) | 0x00ffffff);
    write(0x304, 0x11111111);
    write(0x308, 0x22222222);
    write(0x1f8010a0, 0x300);
    write(0x1f8010a4, 0);
    write(0x1f8010a8, 0x01000401);
    assert((received == std::vector<uint32_t>{0x11111111, 0x22222222}));
    assert((read(0x1f8010a8) & (1u << 24)) != 0);
    memory.dma().advance_to(1500, 100'000'000);
    assert((read(0x1f8010a8) & (1u << 24)) == 0);
    assert(read(0x1f8010a0) == 0x00ffffff);

    // DICR write-one-to-clear drops the DMA IRQ line.
    write(0x1f8010f4, (1u << 23) | (1u << 22) | (1u << 30));
    assert((read(0x1f8010f4) & 0x80000000) == 0);
    assert((read(0x1f801070) & (1u << 3)) == 0);

    // GP0 fill plus GPUREAD VRAM transfer is visible through the shared VRAM.
    write(0x1f801810, 0x020000ff); // red fill
    write(0x1f801810, 0x00000000); // x/y
    write(0x1f801810, 0x00010002); // 2x1
    assert(memory.vram()[0] == 0x1f && memory.vram()[1] == 0x00);
    write(0x1f801810, 0xc0000000); // VRAM -> CPU
    write(0x1f801810, 0x00000000);
    write(0x1f801810, 0x00010002);
    assert(read(0x1f801810) == 0x001f001f);

    mamerecomp::psx::MachineMemory gpu_dma_memory(mamerecomp::psx::kZn2MemoryConfig);
    const PsxBus gpu_bus = gpu_dma_memory.make_bus();
    auto gpu_write = [&](uint32_t address, uint32_t value) {
        assert(gpu_bus.write(gpu_bus.opaque, address, 4, value, 0));
    };
    gpu_write(0x400, (3u << 24) | 0x00ffffff);
    gpu_write(0x404, 0x020000ff);
    gpu_write(0x408, 0x00000000);
    gpu_write(0x40c, 0x00010002);
    gpu_write(0x1f8010f0, 1u << 11);
    gpu_write(0x1f8010a0, 0x400);
    gpu_write(0x1f8010a4, 0);
    gpu_write(0x1f8010a8, 0x01000401);
    assert(gpu_dma_memory.vram()[0] == 0x1f && gpu_dma_memory.vram()[2] == 0x1f);
    gpu_dma_memory.dma().advance_to(1500, 100'000'000);
}
