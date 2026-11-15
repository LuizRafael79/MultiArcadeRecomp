#include "cps1_driver.hpp"

namespace multiarcaderecomp::cps1 {
const Cps1GameDriver& sf2ce_driver();

const Cps1GameDriver* find_game_driver(std::string_view id) {
    if (id == "sf2ce") return &sf2ce_driver();
    if (id == "dino") return &dino_driver();
    return nullptr;
}

} // namespace multiarcaderecomp::cps1
