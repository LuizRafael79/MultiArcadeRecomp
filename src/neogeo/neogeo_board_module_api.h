#pragma once

#ifdef __cplusplus
extern "C" {
#endif

unsigned mamerecomp_board_module_abi(void);
const char* mamerecomp_board_module_id(void);
int mamerecomp_board_run(int argc, char** argv);

#ifdef __cplusplus
}
#endif
