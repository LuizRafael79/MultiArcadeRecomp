#pragma once

#include <neogeorecomp/neogeo.h>

#ifdef __cplusplus
extern "C" {
#endif
extern const ng_game_t multiarcaderecomp_kof98_game;
void multiarcaderecomp_kof98_prot_reset(void);
void multiarcaderecomp_kof98_decrypt(uint8_t *program, size_t size);
uint16_t multiarcaderecomp_kof98_prot_read(void *context, uint32_t word_offset);
void multiarcaderecomp_kof98_prot_write(void *context, uint32_t address, uint16_t data);
#ifdef __cplusplus
}
#endif
