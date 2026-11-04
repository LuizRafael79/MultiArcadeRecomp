// Minimal host types used to compile Flycast's SG channel/voice engine inside
// the Naomi runtime. The emulator's AICA RAM/register adapter is implemented
// in src/naomi/naomi_aica_voices.cpp.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

class Serializer {
public:
    template<class T> Serializer& operator<<(const T&) { return *this; }
};

class Deserializer {
public:
    enum Version { V22 = 22, V28 = 28, V49 = 49, V56 = 56 };
    [[nodiscard]] unsigned version() const { return V56; }
    template<class T> Deserializer& operator>>(T&) { return *this; }
    template<class T> void skip() {}
};

namespace aica {
class RamRegion {
public:
    void setRegion(u8* data, std::size_t size) { data_ = data; size_ = size; }
    [[nodiscard]] std::size_t size() const { return size_; }
    u8& operator[](std::size_t offset) { return data_[offset % size_]; }
    const u8& operator[](std::size_t offset) const { return data_[offset % size_]; }
private:
    u8* data_ = nullptr;
    std::size_t size_ = 0;
};
}

struct OnLoad {
    explicit OnLoad(void (*initialize)()) { initialize(); }
};
