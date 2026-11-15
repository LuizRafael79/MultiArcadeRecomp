#include "multiarcaderecomp_cps2_coverage.h"
#include "multiarcaderecomp_cps2_game.h"

#include <cstdio>

namespace multiarcaderecomp_cps2_coverage {
std::uint64_t m68k_aot_instructions = 0;
std::uint64_t z80_aot_instructions = 0;
std::uint64_t m68k_fallback_instructions = 0;
std::uint64_t z80_fallback_instructions = 0;
} // namespace multiarcaderecomp_cps2_coverage

extern int MULTIARCADERECOMP_CPS2_MAIN(int argc, char **argv);

extern "C" __attribute__((visibility("default")))
int MULTIARCADERECOMP_CPS2_ENTRY(int argc, char **argv) {
    using namespace multiarcaderecomp_cps2_coverage;
    m68k_aot_instructions = z80_aot_instructions = 0;
    m68k_fallback_instructions = z80_fallback_instructions = 0;
    const int result = MULTIARCADERECOMP_CPS2_MAIN(argc, argv);
    std::fprintf(stderr,
        "CPS2 execution coverage: 68000 AOT instructions=%llu fallback instructions=%llu; "
        "Z80 AOT instructions=%llu fallback instructions=%llu\n",
        static_cast<unsigned long long>(m68k_aot_instructions),
        static_cast<unsigned long long>(m68k_fallback_instructions),
        static_cast<unsigned long long>(z80_aot_instructions),
        static_cast<unsigned long long>(z80_fallback_instructions));
    return result;
}
