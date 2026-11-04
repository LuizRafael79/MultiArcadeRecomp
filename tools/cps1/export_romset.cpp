#include "cps1_driver.hpp"
#include "cps1_romset.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: cps1_romset_export <game> <rom.zip> <output-dir>\n";
        return 2;
    }
    try {
        const auto* driver = mamerecomp::cps1::find_game_driver(argv[1]);
        if (!driver || !driver->romset) throw std::runtime_error("no CPS-1 game driver for " + std::string(argv[1]));
        mamerecomp::cps1::export_romset(*driver->romset, argv[2], argv[3]);
        std::cout << "Validated " << driver->romset->loads.size() << " load segments from "
                  << driver->romset->game << " ROM driver.\n";
    } catch (const std::exception& error) {
        std::cerr << "CPS-1 ROM export failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
