// Adapts the MameRecomp emitter's generated tables to the shared runtime ABI.
#include "rec_abi.h"

#ifdef MAMERECOMP_MIPS3_HAS_AOT
extern const rec_func_t rec_funcs[];
extern const uint32_t rec_nfuncs;
extern const rec_label_t rec_labels[];
extern const uint32_t rec_nlabels;
extern const uint32_t rec_image_base, rec_image_words;
extern const uint32_t rec_image[];

#ifndef MAMERECOMP_MIPS3_GAME_ID
#define MAMERECOMP_MIPS3_GAME_ID ""
#endif

const rec_module_t *rec_module_init(const rec_host_t *host)
{
    (void)host;
    static rec_module_t module;
    module = (rec_module_t){
        .abi = REC_ABI_VERSION,
        .sizeof_cpu = sizeof(cpu_t),
        .sizeof_tlbc = sizeof(tlbc_t),
        .funcs = rec_funcs,
        .labels = rec_labels,
        .nfuncs = rec_nfuncs,
        .nlabels = rec_nlabels,
        .image_base = rec_image_base,
        .image_words = rec_image_words,
        .image = rec_image,
        .game = MAMERECOMP_MIPS3_GAME_ID,
    };
    return &module;
}
#else
const rec_module_t *rec_module_init(const rec_host_t *host)
{
    (void)host;
    return 0;
}
#endif
