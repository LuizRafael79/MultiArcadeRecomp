#include "machine_memory.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    mamerecomp::psx::MachineMemory memory(mamerecomp::psx::kZn2MemoryConfig);
    std::vector<uint8_t> bios(0x80000, 0x5a);
    assert(memory.load_bios(bios.data(), bios.size()));
    const PsxBus bus = memory.make_bus();

    uint32_t value = 0;
    assert(bus.read(bus.opaque, 0xbfc0c148, 4, 0, &value));
    assert(value == 0x5a5a5a5a);
    assert(bus.write(bus.opaque, 0xbfc0c148, 4, 0x12345678, 0));
    assert(bus.read(bus.opaque, 0x9fc0c148, 4, 0, &value));
    assert(value == 0x5a5a5a5a);

    // ZN-2 exposes 4 MiB of RAM repeatedly across an 8 MiB address window.
    assert(bus.write(bus.opaque, 0x00000004, 4, 0xfeed1234, 0));
    assert(bus.read(bus.opaque, 0x00400004, 4, 0, &value));
    assert(value == 0xfeed1234);
    assert(bus.read(bus.opaque, 0xa0400004, 4, 0, &value));
    assert(value == 0xfeed1234);

    PsxCpuState cpu{};
    psx_aot_reset(&cpu);
    mamerecomp::psx::RunCounters counters{};
    cpu.pc = 0x1e000000; // Unmapped instruction fetch reports IBE, not a stuck PC.
    assert(mamerecomp::psx::execute_one(&cpu, &bus, nullptr, 0, &counters) ==
           PSX_AOT_EXCEPTION);
    assert(cpu.pc == 0xbfc00180);
    assert(cpu.cp0[14] == 0x1e000000);
    assert(((cpu.cp0[13] >> 2) & 31) == 6);
    assert(counters.missing_fetches == 1);
}
