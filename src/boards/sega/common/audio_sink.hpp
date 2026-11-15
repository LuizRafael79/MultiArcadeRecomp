#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace mamerecomp {

// Board sound devices deliver already mixed PCM to the host presentation
// layer without depending on a particular audio API.
using SegaAudioSink = std::function<void(std::span<const std::int16_t>, std::uint32_t)>;

} // namespace mamerecomp
