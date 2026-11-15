#pragma once

#include "boards/sega/common/video_frame.hpp"

#include <string_view>
#include <vector>

namespace mamerecomp {

class SegaBoardVideoRenderer {
public:
    virtual ~SegaBoardVideoRenderer() = default;
    virtual SegaVideoFrame render(std::vector<SegaVideoFrame>* stages = nullptr,
        SegaVideoProfile* profile = nullptr) = 0;
    virtual SegaVideoFrame render_cpu_reference() = 0;
    virtual std::string_view comparison_label() const = 0;
};

} // namespace mamerecomp
