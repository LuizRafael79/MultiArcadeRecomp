#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *board_id;
    int scan_rom_pointer_words;
    int infer_indirect_code_entries;
} M68kRecompBoardProfile;

const M68kRecompBoardProfile *m68k_recomp_find_board_profile(const char *board_id);

#ifdef __cplusplus
}
#endif
