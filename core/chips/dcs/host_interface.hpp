#pragma once

#include <cstdint>

namespace mamerecomp::dcs {

// Shared main-CPU side of Midway DCS host interfaces. Board maps own the
// addresses and bus timing; the DCS device owns the latches and DSP runtime.
class HostInterface {
public:
    virtual ~HostInterface() = default;
    virtual std::uint16_t host_status() const noexcept = 0;
    virtual std::uint16_t host_data_read() noexcept = 0;
    virtual void host_data_write(std::uint16_t data) noexcept = 0;
    virtual std::uint64_t consume_host_stall_cycles() noexcept { return 0; }
};

} // namespace mamerecomp::dcs
