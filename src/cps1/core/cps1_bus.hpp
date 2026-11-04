#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mamerecomp::cps1::detail {

void initialize_bus(std::span<const uint8_t> image);
uint16_t bus_read16(uint32_t address);
void bus_write16(uint32_t address, uint16_t value);
uint64_t bus_reads();
uint64_t bus_writes();

} // namespace mamerecomp::cps1::detail
