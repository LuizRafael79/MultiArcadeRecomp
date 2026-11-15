#pragma once

#ifdef __cplusplus
extern "C" {
#endif

unsigned multiarcaderecomp_board_module_abi(void);
const char* multiarcaderecomp_board_module_id(void);
int multiarcaderecomp_board_run(int argc, char** argv);

#ifdef __cplusplus
}
#endif
