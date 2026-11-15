# ADSP-2105 and DCS Audio 2K notes

`core/cpus/adsp2105/` is the shared CPU backend. The MK2 DCS-2K board is a
separate runtime consumer; do not fold its host bus, ROM banking, or DAC policy
into the CPU implementation. The local reference is `/home/luiz/git/mame`:

- `src/devices/cpu/adsp2100/adsp2100.cpp`
- `src/devices/cpu/adsp2100/adsp2100.h`
- `src/devices/cpu/adsp2100/2100ops.hxx`
- `src/devices/cpu/adsp2100/2100dasm.cpp`
- `src/mame/shared/dcs.cpp` and `dcs.h`
- `src/mame/williams/midtunit.cpp` and `midtunit_m.cpp`

## CPU and board boundary

The ADSP core owns 24-bit instruction decode, word-addressed PC, loop and
interrupt behavior, ALU/MAC/shift state, register banks, DAGs, and separate
program/data spaces. `adsp2105.hpp` is the common interpreter/AOT state and bus
contract. Program callbacks take 14-bit word addresses and return 24-bit
instructions. Data callbacks take 14-bit word addresses and transfer 16-bit
words. Do not pass byte addresses through the DAG helpers.

The DAG is part of the CPU ISA, not a DCS feature. It uses I0-I7, signed M0-M7,
L0-L7, and derived base/mask values. `modify_address` wraps I+M to 14 bits and
then applies circular-buffer correction. DAG1 bit-reverses memory addresses
when MSTAT.REVERSE is set; DAG2 accesses data or program space. Program transfers
use PX for the low byte of a 24-bit word. Both interpreter and generated code
must call the same helpers in `dag.hpp` so address updates and PX behavior agree.

## TMS-to-DCS handshake on MK2

MAME's T-Unit map exposes DCS status at `0x01d00000-0x01d0001f` and the DCS
host data/control window at `0x01d01020-0x01d0103f`. In `midtunit_m.cpp`, the
status read is `dcs.control_r() >> 4`; data reads consume the DCS output latch
(and return its low byte). A host write is accepted at the nonzero mirrored
word offset only when both byte lanes are active: bit 8 drives DCS reset and
bits 0-7 send the command byte. `dcs.cpp` then sets ADSP IRQ2, fills the input
latch, and signals the input-full state. The ADSP reads that latch through its
own data map at `0x3400-0x37ff`; reading/acknowledging clears input-full and
IRQ2. In the other direction, ADSP writes to `0x3400-0x37ff` fill the output
latch; TMS reads the host window, which acknowledges output-full. These are two
bus interfaces and two latches with status/IRQ side effects, not a direct
function call from TMS into the ADSP or its DAG.

The ADSP data map for DCS-2K is:

- `0x0000-0x1fff` mirrored 2 Kiword data RAM
- `0x2000-0x2fff` selected sound ROM bank
- `0x3000-0x33ff` sound ROM bank select writes
- `0x3400-0x37ff` input/output latches
- `0x3800-0x39ff` local RAM
- `0x3fe0-0x3fff` ADSP control registers

The program map has internal RAM at `0x0000-0x03ff` and shared external
program RAM at `0x0800-0x1fff`. Keep this memory map on the DCS device side of
the CPU callbacks.

## Boot and audio output

MAME configures one ADSP-2105 at 10 MHz. DCS reset selects a page of 0x1000
16-bit sound-ROM words, takes the low byte of each word into a byte buffer,
then `load_boot_data` reads 4-byte slots: the first three bytes form a 24-bit
instruction and the fourth byte specifies the count as `(byte + 1) * 8`.
Instructions are copied into internal ADSP program RAM. That boot extraction
belongs to DCS runtime. AOT input should be the decoded program image, not the
raw `mk2.zip` ROM region. Program RAM can be rewritten, so any translation must
check mutability and return to the interpreter for dynamic code.

DCS connects ADSP SPORT transmit to its sound callback and uses a mono DMADAC
identified as AD-1851. SPORT/control-register timing, DMA cadence, gain, and
sample queueing belong to the DCS/AD1851 runtime, not the ADSP instruction
semantics. The current CPU `Bus` includes a SPORT transmit callback as the
boundary for that connection.

SPORT1 setup must update the selected I register through the DAG helper, not
by assigning `I[n]` directly. MAME's `set_state_int(I[n], source)` calls
`update_i()` and refreshes the circular base; omitting that side effect leaves
the old base active after the aligned autobuffer start is installed. The DCS
runtime now uses `dag_write_i()` both when SPORT1 starts and when its DMA
reader advances the index. This is a runtime/DAG state fix, not an opcode
change; rollback marker: the `dag_write_i()` calls in `audio_2k.cpp` and the
SPORT1 register-write path in `interpreter.cpp`.

For DCS Audio 2K, timer control writes are owned by the board bus, so they must
also be mirrored into the shared ADSP state used by `advance_clock()`. The DCS
register view stores `TIMER_SCALE` as `(raw & 0xff) + 1`, while the CPU state
keeps the raw register and applies `+1` during cycle advancement. `TIMER_COUNT`
reads must expose the live decremented CPU count. The timer phase resets when
MSTAT.TIMER changes or a timer register restarts the timer, matching MAME's
`timer_enable_callback()`/`reset_timer()` flow. Rollback marker: the MSTAT timer
phase reset in `update_mstat()` and timer register mirroring in
`DcsAudio2K::read_data()`/`write_data()`; no opcode semantics changed.
The V-Unit trace also reports timer enable, scale/count/period, phase and
pending timer IRQ so gameplay captures can verify whether the DSP is running
against the expected timer configuration.

The Cruis'n USA zero-sample trace exposed a separate CPU semantic mismatch:
`mac_to_mr()` and `mac_to_mf()` were reading operands with the ALU selector
tables. MAME has distinct MAC tables (`m_mac_xregs`/`m_mac_yregs` in
`src/midway/vunit/mame_source/adsp2100.cpp`): MAC X selector 0 reads MX0
(not AX0), and MAC Y selector 2 reads MF (not AF). The interpreter now has
`mac_x()`/`mac_y()` helpers matching those tables and uses them for both MAC
result paths. This is a shared ADSP semantic fix, not V-Unit audio glue. It is
the first audio correction: the following gameplay trace reached nonzero
samples. That trace then exposed a second MAC mismatch. The MAME helpers use
signed×signed for rounded operations 1-3 and hold the multiplier intermediate
in `int32_t` before extending it into the 40-bit accumulator. Our MAC-to-MR path
incorrectly inferred signedness from those operation numbers' low bits, and
both MR/MF paths retained a wider intermediate. `mac_product()` now applies
the MAME signedness table and explicit 32-bit wrap before accumulation. The
resulting PCM still needs comparison/listening; rollback marker is this helper
and its call sites in `interpreter.cpp`.

## Current implementation status

- KI1 sound-runtime update: the shared ADSP state now models internal data RAM,
  control registers `0x3fe0-0x3fff`, timer ticks, and SPORT1 autobuffer output
  at its configured period. The board opts into core-owned internal DM and
  SPORT timing; the existing MK2 DCS-2K remains on its board-owned memory/DMA
  path. The KI1 boot exposed ADSP program-memory DAG2 groups `0x50-0x5f`;
  interpreter/native semantics now cover MAC/ALU program reads and writes.
  Last opcode family added: `0x56-0x57` (ALU-to-AF plus DAG2 program read),
  observed as `0x57696c` at boot PC `0x0007`; the complete `0x50-0x5f` family
  was added together. A headless smoke now reports SPORT1 enabled and nonzero
  samples, but audio quality and runtime parity still need user listening and
  comparison against MAME. Keep the exact opcode marker here for rollback.

- `dag.hpp`: circular addressing, bit reversal, DAG1/DAG2 data access and DAG2
  program transfers are shared helpers.
- `interpreter.cpp`: the partial interpreter covers control flow, register
  moves, immediate loads, shifts, ALU/MAC subsets, direct and DAG memory
  transfers, and ALU/MAC register-move groups. Latest opcode change: groups
  `0x05–0x07` (saturate MR and DIVS/DIVQ), `0x0e`/`0x0f` exponent operations
  (EXP HI/HIX/LO and EXPADJ), groups `0x10–0x13` (shift with internal move,
  program-memory DAG2 transfer, data memory DAG1 transfer, and data memory
  DAG2 transfer), and all data-memory DAG1/DAG2 read/write groups `0x60–0x7f`.
  These match MAME's `adsp2100.cpp`/`2100ops.hxx` and shared `dag.hpp` helpers.
  MK2 traces successively exposed `0x1210a1` at `PC=0x03f0`, `0x0e670f` at
  `PC=0x008a`, `0x6ae0c1` at `PC=0x00bf`, and `0x050000` at `PC=0x0128`;
  all four opcode families are implemented. A controlled 20M TMS-cycle run
  then reached ADSP `PC=0x09fe` without a stop: hybrid ended at ADSP cycle
  32,000,001 and interpreter-only at 32,000,006. Their final PCs differed by
  four words, so AOT/interpreter equivalence is still not established. Both
  had four host commands, no DCS output reads, and 240 zero samples. The newest
  opcode family, `0x05–0x07`, is the rollback marker. Other unsupported
  variants still stop with categorized fallbacks; the interpreter is not
  complete.
- `tools/adsp2105/recomp.py`: emits native entries for bootstrap loads,
  immediate DAG1/DAG2 writes, shifts, register moves, ALU/MAC operations,
  absolute/DAG memory transfers, and conditional control flow. Each entry
  checks the live 24-bit program word before executing, so a changed writable
  program word falls back instead of running stale translated code. The
  unconditional self-wait branch fast-forwards only to the current scheduler
  deadline and is disabled for hardware-loop/timer cases. Input is the decoded
  program image: three big-endian bytes per 24-bit word.
- MK2 AOT checkpoint: latest addition is loop-end `0x5000b1` at boot PC `0x00ae`
  (DAG2 program-space read); preceding coverage includes loop-end `0x090001` at boot PC `0x0093`
  (DAG address modification); preceding additions include `0x0a000f` at boot PC `0x0125`
  (conditional return), `0x680001` at boot PC `0x004e`
  (DAG1 core-register store at a `DO UNTIL` loop boundary), `0x20980f` at PC
  `0x006a` (conditional MAC-to-MR), `0x0c3000` at PC `0x006d` (MSTAT
  mode-control), and loop-end `0x21600f` at `0x0072`. Loop-boundary execution
  is implemented only for the verified DAG modify, conditional MAC, MAC-function-zero
  DAG2 read and DAG1/DAG2 store forms; other loop-ending instructions still decline to the
  interpreter. Rollback marker: `0x21600f`/group `0x20-0x21` in
  `tools/adsp2105/recomp.py` and `native_execute_instruction` in
  `interpreter.cpp`; loop boundary handling remains separately guarded.
- `runtime.cpp`: executes generated blocks and falls back to the interpreter;
  it is CPU dispatch only and handles ADSP interrupt entry. `src/mk2/dcs2k.*`
  owns DCS-2K maps, host latches, ROM banking/boot, SPORT1 autobuffer cadence,
  IRQ1 buffer wraps, and the AD1851 mono sample queue. The MK2 trace drains that
  stream at the board's 31.25 kHz clock. Reset/handshake timing and DAC output
  still need broader frame-by-frame comparison with MAME.

Do not wire this into MK1. Its Williams/MC6809 sound board remains separate.
