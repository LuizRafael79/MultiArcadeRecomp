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

## Current implementation status

- `dag.hpp`: circular addressing, bit reversal, DAG1/DAG2 data access and DAG2
  program transfers are shared helpers.
- `interpreter.cpp`: prototype interpreter currently handles a small control
  flow subset (NOP, flags, branches/calls, stack control, mode control, DO
  UNTIL, and MODIFY). All other opcodes report a categorized fallback and stop;
  it is **not** yet a complete ADSP interpreter.
- `tools/adsp2105/recomp.py`: initial AOT emitter translates only statically
  identified NOP words to native blocks. Other instructions still need native
  emitters. Its input format is a packed decoded program image: three big-endian
  bytes per 24-bit instruction.
- `runtime.cpp`: executes generated blocks and falls back to the interpreter;
  it is CPU dispatch only. Interrupt delivery and the DCS-2K/AD1851 runtime are
  not implemented yet.

Do not wire this into MK1. That prototype remains frozen; the next board
integration is MK2, after the ADSP semantics and DCS host protocol are complete.
