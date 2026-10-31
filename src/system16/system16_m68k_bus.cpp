#include "system16_bus.hpp"
#include "system16_m68k_bus.h"

namespace {

M68kBusCallbacks callbacks_for(void* address_space)
{
    return M68kBusCallbacks{
        address_space,
        &mamerecomp::system16_bus_read8_callback,
        &mamerecomp::system16_bus_read16_callback,
        &mamerecomp::system16_bus_write8_callback,
        &mamerecomp::system16_bus_write16_callback,
    };
}

} // namespace

extern "C" void m68k_system16_bind_address_space(void* address_space)
{
    const auto callbacks = callbacks_for(address_space);
    m68k_system16_bus_bind(&callbacks);
}
