#pragma once

#include "../../core/third_party/flycast-aica/aica.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#ifndef ARAM_MASK
#define ARAM_MASK 0x7fffffu
#endif

#ifndef DEBUG_LOG
#define DEBUG_LOG(category, ...) ((void)0)
#endif
#ifndef WARN_LOG
#define WARN_LOG(category, ...) ((void)0)
#endif

namespace config {
inline bool DSPEnabled = false;
inline bool VmuSound = false;
}
struct NaomiVoiceSettings {
    struct { bool muteAudio = false; } aica;
    struct { bool fastForwardMode = false; } input;
};
inline NaomiVoiceSettings settings;

inline void WriteSample(s32, s32) {}
inline void libCore_CDDA_Sector(s16* samples) {
    std::fill_n(samples, 2352 / 2, s16{0});
}

namespace aica {
extern RamRegion aica_ram;
alignas(4) extern u8 aica_reg[0x8000];

namespace dsp {
struct State { s32 MIXS[16]{}; };
extern State state;
inline void init() { state = {}; }
inline void term() {}
inline void step() {}
}
}
