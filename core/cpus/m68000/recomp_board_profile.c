#include "recomp_board_profile.h"

#include <string.h>

static const M68kRecompBoardProfile s_profiles[] = {
    /* Match the upstream raw-image frontend's pointer and indirect scans. */
    {"generic", 1, 1},
    /* CPS-1 entry discovery is completed by observed runtime PCs. */
    {"cps1", 0, 0},
    /* CPS-2 decrypted program images use runtime roots plus tracked jump tables;
     * whole-ROM pointer scanning is too broad for encrypted/data-heavy regions. */
    {"cps2", 0, 1},
    /* The imported Neo Geo flow relies on pointer and jump-table inference. */
    {"neogeo", 1, 1},
};

const M68kRecompBoardProfile *m68k_recomp_find_board_profile(const char *board_id)
{
    if (board_id == 0) return 0;
    for (unsigned i = 0; i < sizeof(s_profiles) / sizeof(s_profiles[0]); ++i)
        if (strcmp(board_id, s_profiles[i].board_id) == 0) return &s_profiles[i];
    return 0;
}
