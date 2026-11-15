#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace multiarcaderecomp::cps1::detail {

uint16_t gfxram_word(size_t byte_offset);
size_t video_base(unsigned register_index, size_t boundary);
void copy_cps1_palette();
bool write_frame(const std::string& path);
uint64_t video_nonzero_pens();
void set_live_window_enabled(bool enabled);
bool live_window_quit_requested();

} // namespace multiarcaderecomp::cps1::detail
