#include "recomp_board_profile.h"

#include <string.h>

static const M68kRecompBoardProfile s_profiles[] = {
    /* Raw images default to direct control-flow discovery. */
    {"generic", 0, 0},
    /* CPS-1 entry discovery is completed by observed runtime PCs. */
    {"cps1", 0, 0},
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
