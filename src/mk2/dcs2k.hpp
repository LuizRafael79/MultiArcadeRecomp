#pragma once

// Compatibility names for the MK2 module. The DCS Audio 2K implementation is
// shared in core/chips/dcs and consumed by all boards using this DCS revision.
#include "core/chips/dcs/audio_2k.hpp"

namespace mamerecomp::mk2 {
using Dcs2K = mamerecomp::dcs::DcsAudio2K;
using DcsDiagnostics = mamerecomp::dcs::DcsAudio2KDiagnostics;
}
