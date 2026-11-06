// Minimal stand-ins for MAME utility helpers used by the BSD-3-Clause MDEC port.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using offs_t = unsigned;

#define WORD_XOR_LE(address) (address)
#define DEFINE_DEVICE_TYPE(...)
#define NAME(value) value
#define save_item(...)
#define LOG(...)
#define LOGMASKED(...)
#define logerror(...)
#define osd_printf_debug(...)
#define ATTR_COLD
#define machine() (MdecMachineCompat{})
struct MdecMachineCompat { const char *describe_context() const { return "MDEC"; } };
namespace util {
template<class T> constexpr T sext(uint32_t value, unsigned bits)
{
    const uint32_t mask = (uint32_t(1) << bits) - 1;
    value &= mask;
    return static_cast<T>((value ^ (uint32_t(1) << (bits - 1))) - (uint32_t(1) << (bits - 1)));
}
}
