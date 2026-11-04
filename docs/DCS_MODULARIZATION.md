# DCS consumers and shared component boundary

The common `core/chips/dcs/` implementation currently models Midway DCS Audio
2K host latches, ROM banking, DSP boot transfer, SPORT1 autobuffer output and
sample delivery. It uses the shared `core/cpus/adsp2105/` backend. A board owns
its memory map, host command/reset wiring, clock ratio, and final audio output.

| Game / board | Current DCS path | Status |
| --- | --- | --- |
| Mortal Kombat II / T-Unit | `core/chips/dcs/` + shared ADSP-2105 | Migrated; MK2 module links the common DCS library. |
| Cruis'n USA / V-Unit | `core/chips/dcs/` + shared ADSP-2105 | Uses the common device with V-Unit host wiring and its own clock ratio. |
| Killer Instinct 1 and 2 / Seattle | `src/ki/runtime/dcs/` + UltraCombo ADSP | Still separate. KI is working on the UltraCombo backend; do not replace it until ROM banking, mutable program RAM, SPORT scheduling, host mailbox and audio output match the common component. |
| Mortal Kombat / T-Unit | No DCS Audio 2K | Uses its Williams audio board path; it is not a DCS consumer. |

When changing the shared DCS or ADSP interfaces, check every row above. KI1
and KI2 are known DCS consumers even though they are not yet linked to this
implementation. Their eventual migration is tracked in `TODO.md` and must keep
the working UltraCombo behavior until parity has been demonstrated.
