#pragma once

#include <cstdint>
#include <string_view>

namespace multiarcaderecomp::app {

std::uint64_t parse_u64(std::string_view value, std::string_view label);
void print_usage(const char* program);
int machine_game_command(int argc, char** argv, bool build_mode, bool audit_mode = false);

} // namespace multiarcaderecomp::app
