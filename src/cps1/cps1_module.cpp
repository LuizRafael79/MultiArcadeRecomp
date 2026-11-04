#include <cstdint>

extern "C" unsigned mamerecomp_board_module_abi() { return 1; }
extern "C" const char* mamerecomp_board_id() { return "cps1"; }
