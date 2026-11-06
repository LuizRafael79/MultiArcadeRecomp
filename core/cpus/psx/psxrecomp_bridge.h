#pragma once

#include "psx_aot_abi.h"
#include "psxrecomp_abi/psx_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsxRecompContext {
    CPUState generated;
    PsxCpuState *machine_state;
    const PsxBus *bus;
    uint32_t faulted;
} PsxRecompContext;

typedef void (*PsxRecompFunction)(CPUState *state);

void psx_recomp_context_init(PsxRecompContext *context, PsxCpuState *state,
                             const PsxBus *bus);
PsxAotExit psx_recomp_call(PsxRecompContext *context,
                           PsxRecompFunction function);

#ifdef __cplusplus
}
#endif
