#include "psx_aot_abi.h"

#include <cassert>
#include <cstdint>

int main()
{
    {
        PsxCpuState state{};
        state.pc = 0x100;
        state.r[5] = 0x55;
        psx_aot_schedule_load(&state, 5, 0x12345678);
        assert(state.pc == 0x104);
        assert(state.r[5] == 0x55);
        assert(state.delayed_load_pending);
        psx_aot_advance(&state);
        assert(state.pc == 0x108);
        assert(state.r[5] == 0x12345678);
        assert(!state.delayed_load_pending);
    }

    {
        PsxCpuState state{};
        state.pc = 0x200;
        psx_aot_schedule_load(&state, 3, 0x11111111);
        psx_aot_schedule_load(&state, 3, 0x22222222);
        assert(state.pc == 0x208);
        assert(state.r[3] == 0);
        assert(state.delayed_load_pending);
        psx_aot_advance(&state);
        assert(state.r[3] == 0x22222222);
    }

    {
        PsxCpuState state{};
        state.pc = 0x300;
        psx_aot_schedule_load(&state, 3, 0x33333333);
        psx_aot_schedule_load(&state, 4, 0x44444444);
        assert(state.pc == 0x308);
        assert(state.r[3] == 0x33333333);
        assert(state.r[4] == 0);
        psx_aot_advance(&state);
        assert(state.r[4] == 0x44444444);
    }

    {
        PsxCpuState state{};
        state.pc = 0x400;
        psx_aot_advance(&state); // branch instruction enters its delay slot
        psx_aot_schedule_branch(&state, 0x500, true);
        assert(state.pc == 0x404);
        assert(state.branch_pending);
        psx_aot_advance(&state); // delay slot retires and applies the target
        assert(state.pc == 0x500);
        assert(!state.branch_pending);
    }

    {
        PsxCpuState state{};
        state.pc = 0x600;
        psx_aot_advance(&state);
        psx_aot_schedule_branch(&state, 0x608, false); // untaken branch still has a slot
        psx_aot_advance(&state);
        assert(state.pc == 0x608);
    }

    {
        PsxCpuState state{};
        state.pc = 0x700;
        psx_aot_advance(&state);
        psx_aot_schedule_branch(&state, 0x800, true);
        psx_aot_schedule_load(&state, 7, 0x77777777); // load in branch delay slot
        assert(state.pc == 0x800);
        assert(state.delayed_load_pending);
        psx_aot_advance(&state);
        assert(state.pc == 0x804);
        assert(state.r[7] == 0x77777777);
    }

    {
        PsxCpuState state{};
        state.r[0] = 1;
        psx_aot_retire(&state, 0, 0);
        assert(state.r[0] == 0);
        assert(state.cycles == 1);
        assert(state.instructions == 1);
    }
}
