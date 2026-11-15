#ifndef MULTIARCADERECOMP_MUSASHI_CONF_H
#define MULTIARCADERECOMP_MUSASHI_CONF_H

#include "ng_musashi_conf.h"

/* Keep the common 68000 build lean, but enable Musashi's instruction hook so
 * every board runtime can report actual fallback instruction coverage. */
#undef M68K_INSTRUCTION_HOOK
#define M68K_INSTRUCTION_HOOK M68K_OPT_ON

#endif
