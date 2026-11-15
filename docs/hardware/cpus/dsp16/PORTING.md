# DSP16A core: source and validation notes

`dsp16a.cpp` is a board independent interpreter for the WE/AT&T DSP16A. Its
instruction sequencing, register model, cache execution, and serial/parallel
interfaces are adapted from MAME's `src/devices/cpu/dsp16/` implementation by
Vas Crabb. The upstream files identify their license as BSD-3-Clause; the
MameRecomp files retain that license tag and attribution, and the complete
BSD-3-Clause text is included at [`docs/license/dsp16/LICENSE`](../../../license/dsp16/LICENSE) for source and
binary redistribution. The QSound-specific ROM, command latch, serial DAC
recovery, and HLE live in `core/chips/qsound.*`, outside the CPU core.

JTDSP16 in the MiSTer ZN-2 tree is used as a hardware-behavior reference for
DAU wiring and bit widths. No JTDSP16 RTL source has been copied into this
interpreter. In particular, `rtl/sound/jt_qsound/jtdsp16_dau.v` documents the
37-bit ALU/accumulators, AUC-controlled accumulator low-word clearing, product
alignment, and YL clearing. If RTL code is ported later, its GPL-3.0-or-later
notice must travel with that code; behavioral comparisons alone do not change
the MAME-derived core's BSD-3-Clause attribution.

Reference revisions inspected during this port:

- MAME: `b2a182f0b761030db051210494ed7c6b7e412eb6` (`src/devices/cpu/dsp16/`
  and `src/devices/sound/qsound.cpp`), BSD-3-Clause.
- ZN-2 MiSTer: `d16528fd9e833265dfce473bf3923ca6ca406135`
  (`rtl/sound/jt_qsound/`), GPL-3.0-or-later. Used to check behavior only;
  no RTL source is included in the MameRecomp DSP16A core.
- ValleyBell/qsound-hle: `68e63be325ddce6288adc3f571a4647f634046fb`.
  The separate `core/third_party/qsound-hle/` HLE source carries its upstream
  BSD-3-Clause `LICENSE` and attribution to ValleyBell and Ian Karlsson.

## Verified DAU details

- Loading YH clears YL when AUC[6] is clear.
- DL-1425 sets AUC to `0x0002` at firmware PCs `0x0504` and `0x08a2`; this
  leaves AUC[6] clear. In the first sample loop, `0x0512` loads YH, `0x0514`
  explicitly loads YL, and later YH loads provide integer loop/volume values.
  Thus the clear is required for the DSP's full-width Y arithmetic, though it
  does not by itself explain a 10x PCM-level difference: the X*Y operation
  multiplies by YH only, and the sample phase YL is explicitly restored.
- MAME's `dau_set_atx`/`dau_set_at` use PSW[4]/PSW[5] to decide whether an
  A0H/A1H write preserves the low word; JTDSP16 instead wires this to
  AUC[4]/AUC[5]. MameRecomp now follows the RTL wiring. An A/B replay with
  identical NVRAM over 960 frames previously produced bit-identical QSound PCM
  under both rules (RMS 10,032.9; seven samples at the limit in both runs), so
  this semantic difference does not explain the current hiss in SFEX2PA.
- MAME's F2=9 implementation also differs from JTDSP16: RTL increments the
  source's 21-bit high field and chooses low-word retention from the
  destination's AUC bit, retaining the source low word. MameRecomp now follows
  that RTL behavior with explicit 37-bit wrapping. Regression vectors cover
  both mismatched AUC selector bits, the source low word, and high-field carry.
  In the observed DL-1425 sample loop AUC is `0x02`, making both AUC[4] and
  AUC[5] clear; this F2=9 difference therefore yields a cleared low word in
  both MAME and RTL semantics in that loop. It is a generic core fidelity
  difference, not an explanation for the current audio-level mismatch there.
- F2 RND follows the JTDSP16 bit-level path; the negative half-way tie differs
  from MAME's implementation, whose source marks the rounding rule uncertain.
- AUC alignment value 2 shifts the 32-bit P bus left by two and then sign
  extends the 32-bit result. A prior implementation widened signed P to 64 bits
  before shifting, which preserved discarded upper bits. For P=`0xc0008000`,
  hardware produces `+0x00020000`; the old code produced a large negative
  value. This fed an erroneous value to the QSound mixer and caused widespread
  full-scale PCM samples. `dsp16a_dau_test` now covers the wrap/sign-extension
  case.
- The RTL treats reserved AUC alignment value 3 as value 0. The core now does
  the same instead of throwing; all four encodings are covered by DAU vectors.
- JTDSP16 defines the DSP16A heads/tails condition using a 32-bit LFSR seeded
  with `0xcafecafe`, with feedback taps 31, 21, 1, and 0. The core now reads
  the old MSB and advances the LFSR on conditional-F2 checks, and resets it on
  reset/PI load. MAME still throws for this condition. A focused firmware
  vector checks the seed and first advance. This closes a core semantic gap; it
  is not presented as a fix for the current QSound noise. A scan of the loaded
  DL-1425 0x1000-word image found no heads/tails condition opcodes, so this
  addition does not change the QSound firmware's audio path.

## Current QSound LLE evidence

### What the MAME HLE and LLE actually share

The MAME source keeps the two implementations as different device classes.
`qsound_hle_device` (`qsoundhle.cpp`) implements the voice register map,
sample stepping, ADPCM, panning, FIR filters, echo, delay lines, and final
mixing in software. It advances that model at the QSound sample cadence.
`qsound_device` (`qsound.cpp`) does not run that mixer: it hosts the DSP16A,
provides its sample-ROM PIO interface, accepts Z80 commands through the DSP
PIO/IRQ handshake, and reconstructs stereo samples from the DSP's OCK/DO/OSE/
OLD serial pins. The DL-1425 firmware therefore selects the algorithmic path;
the HLE is not an execution fallback inside LLE. This places the remaining LLE
audio mismatch in firmware execution, DSP I/O timing, sample-ROM access, or
serial recovery, rather than in the HLE's software voice mixer.

`QSOUND_LLE` therefore means that the DSP16A and DL-1425 firmware are used; it
does not mean the whole device is a cycle-accurate hardware model. MAME's own
DSP16 source lists unresolved behavior for serial input/signals, PSW overflow,
and rounding, and throws for random conditions. The local core now implements
the RTL-defined random condition. MAME's QSound serial recovery also has a
FIXME about deriving word select from the DSP outputs. The LLE is real firmware
execution with modeled peripherals, but still has known approximation points.
MiSTer/JTDSP16 is a useful hardware-oriented reference for specific datapath
rules, while its QSound SIO is also simplified. Neither implementation should
be treated as a complete oracle for every subsystem.

On 2026-11-07, a diagnostic MAME was rebuilt as
`/home/luiz/git/MameRecomp/build/qsoundtest` with the
ZN, CPS-1, and CPS-2 drivers selected and `QSOUND_LLE` enabled for driver
translation units. `/home/luiz/git/MameRecomp/build/qsoundtest -listdevices
sfex2pa` reports `QSound` with child `WE|AT&T DSP16A @ 60.00 MHz`; its
`-listroms sfex2pa` reports
`dl-1425.bin`, CRC `d6cf5ef5`. The installed `/usr/bin/mame` still reports
`QSound (HLE)`, so it is not the LLE comparison binary. Use the project-local
diagnostic executable when comparing LLE. The reported 90–91% speed in MAME
shows that the LLE has measurable host cost, but does not explain the local
audio-level/noise divergence by itself.

There are two similarly named executables: the LLE capture and device-list
evidence above refer specifically to `/home/luiz/git/MameRecomp/build/qsoundtest`.
The separate `/home/luiz/git/mame/qsoundtest` is an older HLE build with no
DSP16A CPU; do not use it for DSP-state traces. The user's opened MAME instance
and the project-local diagnostic binary were confirmed to expose the DSP16A.

For a fresh NVRAM comparison, MAME's `at28c16_device::nvram_default()` fills
the EEPROM with `0xff`. I repeated a 900-frame MameRecomp capture using the
same erased image. The MAME `-wavwrite` capture is 48 kHz, 15 s, and runs at
94.49% average speed; its audio begins around second 10, with per-second RMS
about 1.5–2.0k through the active section and peak 8,207. The local raw QSound
capture is 24,038 Hz; it begins around second 8, with RMS about 14.1–15.2k
through the active section, peak 32,767, and 20 samples reaching that peak.
The host-speed difference therefore remains separate from the much higher
local signal level. These files are `build/qsoundtest_sfex2pa.wav` and
`build/mamerecomp_qsound_lle_sfex2pa_ff.pcm`; this is a level/timing comparison,
not sample-aligned parity.

The source also rules out copying the HLE's output clamp as a fix for LLE.
`qsound_hle_device::state_normal_update` clamps its computed mixer output to
`[-0x7fff, 0x7fff]`. The LLE instead captures each 16-bit serial word into
`m_samples`, and `qsound_device::sound_stream_update` presents that DAC value
directly, normalized by 32768. MameRecomp follows the LLE route and does not
apply the HLE mixer clamp. Its high peaks are already present in recovered DSP
words, before SDL/presentation, so output limiting would conceal an upstream
DSP, timing, or serial-state error.

The source comparison also confirms the intended serial handshake: MAME starts
a word when the prior OLD transition set `m_ready`, OSE is inactive, and no
word is in progress; it selects the channel from PSEL, shifts DO on OCK, then
updates the stream before replacing the left DAC value. MameRecomp models the
same OLD/OSE/PSEL/DO/OCK sequence and snapshots the preceding stereo pair at
the left-word boundary. No source evidence supports a different sample
polarity or an HLE-style saturation stage in the LLE path.

The earlier QSound HLE correction in commit `625d2bc` is also distinct from
the current LLE issue. In `core/third_party/qsound-hle/qsound.c`, it fixed
software mixer overflow by accumulating voice products in 64 bits, clamping
the summed dry/wet values, then scaling them. That change also corrected the
HLE sample-ROM low-byte wiring, alternate-filter table source, and final
rounding/clamp. Those are HLE mixer operations; the LLE executes the same
firmware algorithm on the DSP16A's architectural accumulator and emits its
serial DAC words. The local DAU and MAME both use signed 36-bit accumulators,
and the first 500 compared cache states matched, so the HLE accumulator patch
cannot be transplanted as an output-stage fix. The relevant next evidence is
the first divergent DSP state/SDX word during the firmware's active voice-mix
loop, with the command stream aligned.

Fresh-NVRAM, 900-frame SFEX2PA captures after the product-alignment fix:

| Path | RMS L/R | full-scale samples L/R | peak |
| --- | ---: | ---: | ---: |
| MameRecomp DSP16A LLE, raw 24,038 Hz PCM | 9,243.9 / 9,273.9 | 6 / 8 | 32,767 |
| MameRecomp QSound HLE, raw 24,038 Hz PCM | 799.6 / 825.2 | 0 / 0 | 7,670 |

Before the fix, LLE had RMS about 20,200 and roughly 34% full-scale samples.
This confirms the product alignment bug was a major clipping source. It does
not yet prove LLE parity: the LLE level remains much higher than the local HLE
capture. Do not apply gain, limiting, or filtering as a substitute for resolving
that difference.

After this fix the user reported that the noise changed audibly, the music
changed with it, and the noise is still too loud. This confirms the DSP
arithmetic correction affects the actual music path, but the remaining sound
is not validated. Keep the next comparison focused on the SDX words and serial
PCM recovered from the DSP, then compare those against an aligned MAME LLE
capture; do not tune output volume to conceal a DSP or serial-path mismatch.

The first MAME WAV comparison was not QSound-only: MAME's ZN machine routes the
PSX SPU and QSound independently to the shared speaker (`zn.cpp`, SPU gain
0.35, QSound gain 1.0). MameRecomp likewise has separate SDL streams. A fresh
16-second headless MameRecomp capture (`MAMERECOMP_ZN_DUMP_QSOUND_PCM`) contains
385,725 QSound frames and has high-level PCM after ~8 s; the separate
`MAMERECOMP_ZN_DUMP_SPU_PCM` file was empty in that run. Therefore the earlier
full-MAME-WAV RMS comparison is not evidence of QSound-level parity. The local
SPU dump hook is now available for future stream-by-stream checks.

The MAME mixer was then isolated using per-device `<device_volume>` settings in
temporary per-game configs (`-cfg_directory`), leaving the user's saved mixer
untouched. With `:spu` muted, the MAME QSound-only WAV has RMS 1,317–2,028 in
seconds 10–13. With `:qsound` muted, that audio is silent through second 14 and
the SPU produces a separate signal around second 15. On the same 960-frame
headless run, local HLE has similar QSound RMS (~1,450–1,660 around seconds
8–11), while local DSP16A LLE is ~13,800–15,100 in seconds 9–11 and approaches
full scale. Startup timing differs, so this is a level/path comparison rather
than a sample-aligned parity result. It confirms that the remaining loud noise
is specific to the LLE path; the earlier product-alignment correction changed
the audible output, but did not finish correcting it.

A temporary trace in the authorized MAME test executable recorded the raw
16-bit words recovered by `qsound_device::dsp_ock_w` before host mixing. Across
a 16-second run the MAME word stream has RMS 930, peak 10,408, and 34.8% nonzero
samples. The corresponding local LLE PCM capture has RMS 9,701, peak 32,767,
and 50.0% nonzero samples; local HLE has RMS 787, peak 7,670, and 35.2% nonzero
samples. This confirms that the level/noise divergence is already present at
or before the local DSP serial-word stream, not in SDL or the presenter. The
MAME and local streams are not sample-aligned yet, so these statistics do not
identify the first DSP instruction or state that diverges.

An initial instruction trace appeared to diverge at a firmware DO/cache loop,
but the local trace filter omitted PCs `0x05a5–0x05d1`, so it could not support
that conclusion. A corrected continuous comparison found the first 500
executed cache states identical between MAME and MameRecomp, including PC,
opcode, cache pointer/limit/iteration count, C0-C2, flags, and R0-R3. This
eliminates the cache loop as the cause for that boot segment; the first actual
DSP or peripheral divergence remains to be located. The diagnostic MAME source
instrumentation was removed after capture, and the MAME checkout was restored.

A controlled local experiment used the same copied AT28C16 NVRAM seed for both
variants. Replacing the RTL-width AUC=2 product alignment with MAME's 64-bit
`P << 2` semantics increased local raw LLE RMS from 10,033 to 21,468; the
nonzero rate was unchanged. The experiment was reverted. Matching MAME's
software arithmetic therefore makes the current output louder, so the
RTL-width implementation remains in place while the first state divergence is
being traced.

The firmware contains four conditional `F2=11` round instructions, so I also
temporarily substituted MAME's negative tie rule and replayed from the same
NVRAM seed. Only 6 of 771,452 captured samples changed, each by one least
significant bit; RMS remained 10,033. This confirms the rounding difference is
real but does not explain the high-level hiss in this run. The RTL round rule
was restored.

The local PIO writer also previously forwarded writes while `PODS` was passive.
MAME only calls the peripheral write callback on an active PODS strobe, so that
passive callback was removed. Replaying with the same NVRAM seed produced a
bit-identical PCM capture (RMS 10,033); it was a real contract mismatch but not
the audible regression.

Finally, I compared the first 171 QSound commands and DSP PIO reads from fresh
NVRAM/config runs. They match for commands 0–136, then the host Z80 command
stream diverges (`0x84/0x011e` in MAME versus `0x80/0x011f` locally at command
137). This remains when the local Z80 and R3000A both run interpreted, so it is
not caused by either AOT table. It may be a scheduler/runtime or game-state
divergence; no causal link to the LLE hiss is proven. The raw LLE PCM is still
about 10× the MAME DSP word RMS, while local HLE is close in level. Continue by
capturing the mixer state and command/latch timeline around the first mismatch;
do not alter the known RTL saturation/rounding behavior based on RMS alone.

Using the confirmed LLE diagnostic binary and the same EEPROM image (SHA-256
`9bedc80c8f8b0b537fd6a8a9257da307c18a51e51d659b73b9a2a062ace2b195`), a MAME
debugger breakpoint with `focus qsound:dsp` and both `maincpu` and `audiocpu`
observed reached PC `0x05d7` / opcode `0x49a0` at DSP cycle 312,279,077 with
AUC `0x02`, PSW `0x0000`, A0 `0x024e0000`, A1 `0x00cea440`, X `0`, Y
`0x01270000`, P `0`, and R0–R3 `0x012c,0x01f1,0x00e7,0x011e`. The local trace
at the same PC/opcode 578 cycles later has the same R0–R3 and AUC, but PSW
`0x8000`, A0 `-0x40000`, A1 `0xffffffffc99007cc`, Y `0x7fff7fff`, and recovered
SDX `0xfffc`. This establishes an architectural-state divergence during the
active loop. The Z80 command/latch events are not aligned at this point, so it
does not identify whether the first cause is DSP arithmetic, sample-ROM
addressing, or earlier command timing. Capture and align those inputs before
changing DSP semantics.

The MAME QSound source confirms that a PIO read of the pending command address
clears the DSP IRQ and schedules `set_dsp_ready` through
`machine().scheduler().synchronize` before the DSP continues. The DSP bus now
returns a scheduler-yield request at that point and retains unexecuted DSP
clocks for the next run slice. A fresh erased-NVRAM 960-frame replay executes
the LLE and preserves the expected 385,725 audio sample slots, but the first
host command mismatch remains at command 137 (`0x84/0x011e` in MAME versus
`0x80/0x011f` locally). This implements the MAME synchronization contract; the
remaining command divergence is not explained by that barrier.

The MAME source also declares different ROM layouts for the two QSound paths:
`qsound.cpp` uses `ROM_REGION16_BE` with `ROM_LOAD16_WORD_SWAP`, while
`qsoundhle.cpp` uses `ROM_REGION16_LE` with `ROM_LOAD16_WORD`. That source-level
difference must be interpreted as a pair (region endianness plus load
transform), not as a request to byte-swap the firmware vector in isolation.
`romload.cpp` reverses each two-byte load group for `WORD_SWAP`, then swaps the
BE region to native host order in `region_post_process`; these two operations
cancel. The HLE LE region loads the original byte order directly. Thus both
MAME paths expose the same native `u16` word values, matching the existing
loader's little-endian byte assembly. A local experiment that swapped the
words directly made the DSP remain at PC `0x0003` with no sample-ROM reads, PIO
activity, or serial output; it was reverted and the executable rebuilt. The
firmware endian hypothesis is therefore eliminated for the current loader.

A focused source/runtime comparison found a scheduler-level timing difference
around QSound busy polling. Local traces show `Zn2SoundCpu::Read(0xd007)` first
advances the DSP to the Z80 callback cycle, so the DSP progresses during each
busy-poll read. In MAME, `qsound_r()` only returns `m_dsp_ready`; `qsound_w()`
defers `set_cmd()` with `machine().scheduler().synchronize`, and the DSP's PIO
address read defers `set_dsp_ready()` through another scheduler synchronization.
In the MAME trace, multiple Z80 status reads occurred at the same DSP
`total_cycles()` before the DSP resumed. The local trace instead showed a
busy-to-ready transition over about 646 DSP machine cycles in one poll burst.
Although both captures used identical EEPROM bytes, they were not aligned to
the same command index/time, so this establishes a scheduler behavior
difference but does not yet prove it causes the LLE audio noise. Align status
and command events before changing the DSP math or adding scheduler delay.

The status read was changed to match the MAME contract: `d007` no longer clocks
the DSP as a side effect. The previous 4096-main-cycle slices were only a
local bounded-advance policy; no matching MAME scheduler quantum was found, so
they must not be treated as the hardware timing contract. The sound-latch path
now queues each main-CPU write with its R3000A cycle timestamp. `PsxBus` has an
optional scheduler-yield callback, and the R3000A AOT/interpreter returns at
the instruction boundary after a device requests synchronization. The runtime
then applies the queued latch event and advances the Z80/QSound devices at that
timestamp. This mirrors the observable MAME `scheduler().synchronize()`
boundary instead of invoking the Z80 reentrantly from the board write. The
Z80 NMI is raised once for the pending transition and the latch stays pending
until the Z80 reads port `0x00`, matching the generic latch acknowledge path.
ZN-2 now advances to observable deadlines (the latch handoff, 250 Hz Z80 IRQ,
and QSound sample cadence) instead of imposing a fixed 4096-main-cycle quantum.
The source
contract was checked in MAME's `src/mame/sony/zn.cpp` address map,
`src/devices/machine/gen_latch.cpp` deferred latch callback, and
`src/emu/schedule.cpp` synchronization processing.

A fresh-NVRAM, 720-frame run with Coin1 at frame 60 and Start1 at frame 90
verified the new boundary path. The R3000A emitted four latch commands
(`0x30`, `0x01`, `0x00`, `0x00`), and the Z80 read all four. It still emitted
only the QSound initialization sequence at addresses `0x06–0x7e` (32 writes
per QSound register offset), with no audio command stream. The DSP16A produced
578,591 serial words but all were zero. A separate HLE run with the same clean
NVRAM and inputs also stopped at the same initialization sequence and ended
with QSound peak 0. Thus the scheduler handoff now
matches the MAME boundary more closely, but this capture does not show it
fixing the upstream command-flow stall. Continue from the first divergence in
the R3000A/Z80 command sequence, rather than altering DSP arithmetic.
An additional 180-frame interpreter-only PSX run also delivered and read the
first two latch commands (`0x30`, `0x01`), confirming that the yield path works
without the static R3000A AOT table.

Follow-up using a copy of the persisted MAME AT28C16 image changes the result:
in a 720-frame LLE run, the R3000A emitted and the Z80 acknowledged `0x30`,
`0x01`, `0x00`, `0x00`, `0x04`, and `0x09`; QSound received active voice
commands, and the DSP emitted 578,581 serial words, 336,421 nonzero, with peak
32,767. This confirms that the latch handoff reaches the audio command stream
when the saved EEPROM state is present. It does not establish LLE audio fidelity
or explain why a clean-EEPROM boot follows a different startup path. The first
two latch writes occur locally at about 1.02 s and 1.04 s; later writes are
around 4.93–5.00 s, versus about 0.93–0.94 s and 2.53–2.55 s in the captured
MAME run. Treat that remaining timing/game-state difference separately from
the reentrant latch bug.

The DSP-side ready event now follows the MAME callback boundary: reading the
pending command address queues `Ready`, the DSP bus synchronization callback
requests a yield without applying it, and `QSound::run_clocks()` drains that
event after `Dsp16A::run_clocks()` returns. These changes compile and a
960-frame LLE run still produces serial samples. The latest direct MAME command
capture did not reach command 137, so there is not yet evidence that this
resolves the previously observed command-stream divergence or the audible
noise.

The Z80-to-QSound command-address write now also follows MAME's instruction
boundary ordering. `qsound_w()` lowers DSP-ready immediately and schedules
`set_cmd()` with `scheduler().synchronize()`. The local Z80 bus previously
queued the command during the write callback but did not run the DSP until the
next sample/IRQ deadline. `Zn2SoundCpu::InstructionBoundary()` now advances
the DSP to the retired Z80 instruction timestamp, commits the command, and
drains the DSP command/IRQ event there. A 960-frame replay with the same saved
AT28C16 image produced identical aggregate DSP counters and final state before
and after this change (771,448 serial words, 408,695 nonzero words, peak
32,767, final SDX `0x4ffd`). This establishes that the callback ordering is
implemented, but also that this mismatch was not the audible-noise fix; the
active DSP/SDX divergence remains to be located.

The renewed trace used byte-identical AT28C16 images and reconstructed QSound
register writes from both logs. Commands 0–15 match and occur at the same
emulated time after converting Z80 cycles to DSP machine cycles (3.75:1). The
next initialization burst begins at command 16 at DSP cycle 319,497,131 in
MAME, but at local Z80 cycle 59,644,983 (DSP-machine-cycle equivalent
223,668,686). Values continue to match through command 136; command 137 is
`0x84/0x011e` in MAME and `0x80/0x011f` locally. This shows the mismatch starts
as a multi-second startup/game-state timing divergence, well before the single
command where values first differ. The Z80 instruction-boundary change did not
alter the 960-frame DSP totals or final state, so it is a corrected scheduler
contract, not a fix for the current noise. Next compare the ZN main-CPU/game
timeline across the long gap after QSound's first 16 initialization writes.
The raw clock ratios match the MAME configuration (CXD8661R 100 MHz, Z80
8 MHz, DSP16A 60 MHz), and the local AT28C16 write-busy interval matches the
MAME device's 200 μs timer. These checks rule out a simple clock-ratio or
EEPROM write-delay mismatch as the source of the 3.2-second gap; the next
comparison needs the main CPU's timer/interrupt-visible state during that
interval.

The `0xd007` read path was re-evaluated against the cooperative local scheduler.
MAME's `qsound_r()` is passive, but its DSP continues to advance independently
while the Z80 polls. The local runtime now synchronizes the DSP to the Z80 bus
callback timestamp before returning the passive status value, so the device is
at the same emulated time without making the status register itself mutate
QSound state. A 720-frame interpreter-only Z80 run compiled and executed this
path. It still reaches the same first command payload divergence
at index 137 (`0x80/0x011f` locally versus `0x84/0x011e` in MAME), and the LLE
serial peak remains near full scale (32,751). This timing correction therefore
does not resolve the audible noise; continue tracing the earlier command/game
timeline rather than treating status polling as the cause.

The Z80 `0xd002` write had a second ordering discrepancy: local code deferred
both the command payload and the `DSP-ready = busy` transition to the retired
instruction boundary, while MAME's `qsound_w()` clears `m_dsp_ready` in the
write callback and defers only `set_cmd()` with `scheduler().synchronize()`.
`QSound::begin_command_write()` now exposes the busy transition at the bus
callback, with the payload still committed at the instruction boundary. The
targeted QSound test verifies the immediate LLE status edge, and a same-seed
720-frame interpreter run completed. Its command stream and DSP serial totals
were unchanged, including the first command payload mismatch at index 137;
this source-contract correction is therefore not the cause of the current
noise. Continue by aligning the ZN main-CPU timeline and the Z80's status/IRQ
events leading into that command divergence.

The focused `dsp16a_dau_test` covers RTL-derived rounding, upper accumulator
loads, P alignment values 0–3, and YH/YL behavior. It does not yet provide
aligned MAME-vs-MameRecomp architectural state/SDX vectors, nor broad vectors
for signed multiplication, P, A0/A1 accumulation, and saturation. Keep QSound
HLE selectable as a fallback while expanding that differential coverage.

The launcher now defaults SFEX2PA to DSP16A LLE. `--qsound-mode=hle` forces
the HLE path. `--qsound-mode=mixed` runs DSP16A LLE as primary while advancing
the MAME HLE with the same firmware, commands, and clock stream as a shadow.

### 2026-11-07: matched QSound-ready latency

The fresh-NVRAM captures in `/tmp/qsound_pair_yhfix4` contain a directly
matched Z80-to-QSound command: command #100 is register `0x65`, data `0x53e3`
on both MAME and MameRecomp. MAME reports D007 busy for 3.962125 ms (1,093
polls); MameRecomp reports busy for 18.125 us (5 polls). This is a real
ready-latch timing divergence, not a command-payload mismatch. In the MAME
capture the DSP-visible PC/PR remain at `0x051e/0x04ff` while the Z80 polls,
then change when ready is observed. The local status read calls `sync_qsound`,
and `QSound::run_clocks()` drains the Ready event immediately after the DSP
slice yields at the PIO read. MAME's `dsp_pio_r()` instead schedules
`set_dsp_ready()` with `scheduler().synchronize()`; the timer is dispatched
when the global scheduler base time reaches its zero-time expiry.

This supports premature local ready delivery and can change when the Z80
issues following commands. It does not prove that the DSP performs excess
work or that this alone causes host CPU saturation or the serial-audio hiss.
The firmware's `DO K=16, NI=14` loop at `0x050f–0x051d` was confirmed in both
sources; a separate, unaligned MAME trace exits that loop in about 51 us, so
that loop alone does not explain a 3.96 ms busy interval. The next required
comparison is the PIO-ack timestamp and DSP `total_cycles()` against the
same-run MAME ready timer and the first subsequent serial words. Do not
replace this with a fixed 4 ms delay or alter DO/cache iteration counts without
that evidence.

The local `QSOUND_STATUS` trace clarifies the apparent “29 us” repeat: successive
busy polls are 29 **Z80 cycles** apart, or 3.625 us at 8 MHz. For the matched
command #100, five local polls span 18.125 us before ready, while 1,093 MAME
polls span 3.962125 ms. The local DSP cycle counter advances between those
polls and `Dsp16A::run_clocks()` retains clocks left over when the PIO barrier
yields; this is not evidence that the same 29 us of DSP work is being rerun.
Premature ready can still let the Z80 submit later commands much earlier in
emulated time, potentially changing DSP workload and audio cadence. Proving
that it saturates the host requires measuring DSP instructions and host time
over the same emulated interval; the ready-latency comparison alone does not
show that.

### 2026-11-07: paired command/serial capture

A new 1,000-frame MAME/LLE and MameRecomp/LLE pair uses absent initial NVRAM,
the same coin/start frame events, and byte-identical final AT28C16 images
(SHA-256 `980b498dd5eacfaaf4f4d2fb9047614277000a136d1059cd879c07871fde66bb`).
The serial files are `/tmp/qsound_ready_pair_20261107/mame_lle/qsound.pcm`
(MAME big-endian serial words) and
`/tmp/qsound_ready_pair_20261107/local/qsound.pcm` (local little-endian
interleaved stereo). Reconstructing the D000/D001 payload immediately before
each D002 address shows commands 96–105 match exactly, including #100
`0x65/0x53e3` and #101 `0x64/0x0ce2`.

The same run reproduces the ready-latency mismatch. MAME command #100 has
1,093 busy reads from 16.252039750 s to ready at 16.256025125 s (3.985375 ms);
its next command is written 4.001875 ms after #100. Locally, #100 is busy for
five polls over 145 Z80 cycles (18.125 us), and #101 is issued 34.625 us after
#100. The local QSound event trace also shows the DSP advancing past the PIO
yield before that early ready is observed. This confirms that the local
ready/scheduler boundary changes command cadence before the first known
payload mismatch at #116. It is evidence of a scheduling divergence, not
evidence that DSP cycles are being re-executed or that this alone saturates
the host.

The complete-run raw levels are now close: MAME serial-word RMS is 1,059.9
(peak 15,783), while local stereo RMS is 1,066.8/1,020.6 (peaks 15,981/13,741).
Ten-millisecond RMS envelopes correlate at about 0.94 at zero offset, but the
individual sample streams do not correlate after a coarse lag search. Since
the raw MAME dump does not record the selected channel beside each word, this
does not yet establish sample-for-sample divergence or its first SDX cause.
The next comparison must keep the matching command sequence and record the
MAME scheduler base/local DSP time plus channel-tagged serial words around
commands 100–116; do not alter DSP arithmetic or insert a fixed 4 ms delay.

### 2026-11-07: scheduler-boundary experiment and serial timing

The MAME diagnostic was rebuilt with separate files for D002 writes, DSP PIO
reads, and channel-tagged serial words. This avoids the prior collision between
the MAME Lua tap output and the device trace. The paired MAME and local runs
again finish with byte-identical AT28C16 NVRAM (SHA-256
`980b498dd5eacfaaf4f4d2fb9047614277000a136d1059cd879c07871fde66bb`).
Commands match by address and data through #115; #116 is the first payload
mismatch. The PIO traces also show matching first three boot reads. If the
local scheduler omits the serial-frame boundary, the fourth read is delayed
from MAME's 723,872 DSP machine cycles to local 842,147 (about 4 ms). Restoring
the serial-frame boundary moves it to local 725,117 (about 41.5 us later than
MAME). This rules out the coarse 4 ms-only boundary as a correct replacement,
but the remaining phase offset still changes D007 polling cadence.

For matched command #100 (`0x65/0x53e3`), the fresh MAME capture shows 1,093
busy reads and a 4.001875 ms interval to command #101. The local serial-frame
run issues #101 81.7 us after #100 (18 busy reads in that interval). The MAME
and local DSP PIO snapshots therefore need scheduler-local timestamps, not
just equal command indices, before attributing a sample-value difference to
DSP arithmetic.

The new channel-tagged capture finds the local first nonzero word after cycle
480,000,000 at DSP cycle 485,078,329, channel 1, value `0xffff`; MAME emits
zero at the same cycle/channel. MAME's first nonzero word in that run occurs
at cycle 489,253,753, channel 0, value `0xffff`. This is an aligned serial
output mismatch, but its onset follows the command-timing divergence and does
not isolate a DSP16A instruction or pin-level cause yet.

A no-trace 1,200-frame benchmark with fresh NVRAM and matching inputs produced
identical guest counters and final DSP/QSound state in both modes. DSP16A AOT
took 28.499 s wall time; interpreter-only took 23.342 s (about 22% faster).
This confirms the current DSP16A AOT dispatch is a performance regression,
not a speedup; it does not explain the serial mismatch because the captured
guest state and output counters were identical between the two modes.
If DSP execution throws, the runtime switches to that synchronized HLE state
and clears queued LLE samples. The fallback emits a log with the failure
reason. It catches unsupported or invalid DSP execution, but does not detect a
DSP that continues running with incorrect, non-faulting results.

### Current build validation

On 2026-11-06, the clean `mamerecomp` target and `dsp16a_dau_test` target built;
`ctest --test-dir build -R '^dsp16a_dau_test$' --output-on-failure` passed.
Separate headless SFEX2PA runs for 960 frames confirmed both launcher paths:
`--qsound-mode=true` ran the DSP16A LLE for 387,760,559 instructions and
recovered 771,448 serial words; `--qsound-mode=hle` loaded `qsound_hle.zip`
and produced audio without running the DSP16A path. The LLE reached a serial
word peak of 32,767, while the HLE peak was 7,670 in these runs. This verifies
that both modes execute, but also confirms the LLE audio mismatch/clipping is
still unresolved; it is not a clean-audio parity result.

A later rebuild of both `mamerecomp` and `mamerecomp_zn2_sfex2pa.so` was tested
with 960 headless frames, both a copied current NVRAM image and a new erased
NVRAM image. Both completed with 771,448 serial words, 385,408 non-zero words,
peak 32,767, and last SDX `0x966f` (non-zero A0/A1). This did not reproduce a
reported silent run whose last-SDX state was all zero. The output proves that
the current headless DSP/serial path generates non-zero samples with both
tested NVRAM states; it does not establish audible output through SDL or
explain the differing run.

The same rebuilt executable was also run for 960 frames with
`--qsound-mode=hle` and erased NVRAM. It selected `qsound_hle.zip`, completed
with 385,725 audio samples at 24,038 Hz and peak 7,670, and did not report DSP16A
execution. This confirms that the selectable HLE path remains operational in
the rebuilt executable.

On 2026-11-06, a further 720-frame run with a copy of the current MAME AT28C16
image completed in each mode. The LLE run received the six latch commands and
produced 336,421 nonzero DSP serial words (peak 32,767); the HLE run loaded
`qsound_hle.zip`, generated 289,294 audio samples at 24,038 Hz (peak 7,474),
and reported no DSP16A execution. The DAU regression target passed, and
`mamerecomp`, `mamerecomp_zn2_sfex2pa`, and `dsp16a_dau_test` built. These runs
confirm routing and sample production in both modes, not LLE sound fidelity.

On 2026-11-07, the current `mamerecomp`, `mamerecomp_zn2_sfex2pa`, and
`dsp16a_dau_test` targets rebuilt, and `ctest -R '^dsp16a_dau_test$'` passed.
Two 960-frame runs used separate copies of the same persisted MAME AT28C16
image. `--qsound-mode=true` selected `qsound.zip`, executed 387,764,786 DSP16A
instructions, and recovered 408,695 nonzero serial words from 771,448 words;
the peak remained 32,767. `--qsound-mode=hle` selected `qsound_hle.zip`,
produced 385,725 samples at 24,038 Hz with peak 7,928, and reported no DSP16A
execution. This revalidates primary LLE routing and selectable HLE operation
on the active saved state. The large LLE/HLE peak difference remains an open
fidelity defect; successful execution is not audio parity.

On 2026-11-07, `qsound_mixed_fallback_test` injected a reserved DSP16A opcode,
confirmed that mixed mode starts in LLE, switches to HLE without propagating
the exception, and continues advancing HLE frames after the switch. It and
`dsp16a_dau_test` passed. A 2-frame headless SFEX2PA launcher smoke printed
the mixed-mode/LLE-primary status, executed 806,633 DSP16A instructions, and
recovered 1,598 serial words. That smoke confirms launcher wiring and normal
LLE startup in mixed mode; it did not trigger fallback or establish audio
fidelity.

### PSX execution-cycle clock correction

The MAME PSX core's `execute_clocks_to_cycles()` converts four input clocks to
one execute cycle, and `execute_run()` retires one instruction per execute
cycle. The ZN-1 and ZN-2 runtime had instead used the input crystals
(67.7376 MHz and 100 MHz) as the rate of `PsxCpuState::cycles`, which is an
instruction/execute-cycle counter. This made the main CPU run four times too
many instructions per emulated second relative to the Z80, DSP16A, and the
MAME scheduler. The same crystal-rate assumption was also used by frame
scheduling, so changing only `MachineMemory` would not have fixed it.

The runtime now uses the corresponding execute-cycle rates (16.9344 MHz for
ZN-1, 25 MHz for ZN-2, and the already-correct 25 MHz for System 12) throughout
frame timing, PSX devices, ZN MCU/EEPROM timing, and secondary-CPU pacing. The
ZN-1's 12 MHz MC68000 ratio and the ZN-2's 8 MHz Z80 ratio are calculated from
the same execute-cycle timebase. This follows MAME's CPU clock conversion and
keeps the device ratios stable; it is not an audio gain or clipping change.

Validation on 2026-11-07 used 960 headless frames and the same erased AT28C16
image (`9bedc80c...`). Before the correction, SFEX2PA executed 1,604,650,871
R3000A instructions in 960 frames; after correction it executed 401,162,718,
exactly one quarter. The LLE run still recovered 771,448 serial words, with
254,298 nonzero words and a 32,767 peak. A same-image HLE run used the same
401,162,718 main-CPU cycles and produced 385,726 samples at 24,038 Hz, peak
7,132. The clock mismatch is corrected, but the LLE's full-scale serial words
and large LLE/HLE output-level difference remain unresolved; this correction
alone is not an audio-noise fix.

### Recheck of the reported divergence at PC `0x05d7`

The local architectural trace reaches opcode `0x49a0` at PC `0x05d7` with DAU
state that differs from an earlier MAME observation. A source-level comparison
of this instruction found no semantic mismatch: MAME's group `0x09/0x0b`
dispatch and the local dispatch both apply AUC-controlled saturation to A0/A1,
take bits 31–16, and write the selected register. For this firmware opcode,
the selected register is SDX; both implementations update the serial output
buffer and clear PIOC's OBE bit. Their saturation helpers also match.

This rules out the implementation of opcode `0x49a0` itself as the cause. The
available MAME and local snapshots are not aligned at the same command and
instruction history, so the differing A0/A1 values are not evidence of the
first DSP-core divergence. The next comparison must capture both sides at a
shared command/SDX sequence and find the earliest differing state before
`0x05d7`; no DSP arithmetic or output-level change is justified by the current
evidence.

### XAAU and interrupt contract corrections

The source audit found that MAME's `xaau_increment_pt()` preserves PT[15:12]
and wraps only PT[11:0]. The local interpreter had added the signed increment
to the full 16-bit PT value, which could carry into another page. DL-1425
executes `0xf844` (`X=*PT++`) at PC `0x0599` in the active sample loop. The
interpreter now uses the MAME page/offset rule for all four XAAU increment
sites. A focused firmware test reads from `0x8fff` and verifies the next PT is
`0x8000`, with no page carry.

The same audit found two interrupt differences. MAME does not arbitrate IRQs
during the ROM pass that loads a DO loop into the instruction cache, and it
latches an accepted enabled external INT into PIOC status bit 0. The local core
now follows both rules. A regression vector enables the external PIO interrupt,
asserts it as a DO loop starts, verifies the loop body executes before the
interrupt is acknowledged, and reads the PIOC INT status from the handler.

The 960-frame SFEX2PA run after these corrections produced 771,448 serial
words, of which 278,530 were non-zero; raw QSound PCM RMS was 8,013 and peak
was 32,767. Its filtered DSP trace showed PT `0x09d2` at both active sample
loops and never approached the `0x0fff` boundary, so the PT page-wrap defect
was not exercised by this particular capture and cannot explain its remaining
high level. The non-zero fraction is near the previously captured MAME stream,
but RMS remains much higher; command history and DSP state still need alignment
before assigning the remaining audio mismatch to a specific arithmetic, IRQ,
or serial operation.

Validation after these edits: `mamerecomp`, `dsp16a_dau_test`, and
`qsound_mixed_fallback_test` built; both focused CTest cases passed, and
`git diff --check` reported no whitespace errors. The HLE remains selectable
with `--qsound-mode=hle`, while `true` selects LLE and `mixed` retains the
synchronized HLE fallback.

### ZN-2 sound-latch event comparison

MAME's `generic_latch_8_device::write()` queues `sync_callback()` with
`scheduler().synchronize()`. The callback updates the latched byte and asserts
the pending callback on the transition to pending; a Z80 read clears pending.
The local board queues a cycle-stamped write, yields the R3000A scheduler at
the MMIO instruction boundary, then applies it before advancing the Z80. The
Z80 reads the latched byte and clears the local pending state. This matches the
observable latch/NMI ordering at a high level, but does not prove identical
scheduler timing.

A MAME debugger watchpoint and the local sound trace were captured from the
same initial 2,080-byte AT28C16 image and with coin/start events scheduled at
frames 60/90. The first six latch writes carry the same values and originate
at the same two guest PCs in both runs. MAME: `0x30 @ 92,826,617 / PC 0x802297d4`,
`0x01 @ 93,244,490 / PC 0x80229800`, then `0x00 @ 253,291,841`,
`0x00 @ 253,709,713`, `0x04 @ 254,545,472`, and `0x09 @ 254,963,346`.
Local: `0x30 @ 93,244,498 / PC 0x802297d4`, `0x01 @ 93,662,369 / PC
0x80229800`, then `0x00 @ 253,709,717`, `0x00 @ 254,127,591`,
`0x04 @ 254,963,352`, and `0x09 @ 255,381,223`.

The apparent threefold cycle mismatch and different PCs in the earlier
capture were a debugger-context mistake: after changing focus to the Z80 to
trace reads, the watchpoint action reported that CPU's cycle counter and PC.
With focus held on `maincpu`, MAME and local PCs match, and each local latch
write is delayed by about 417,879 R3000A cycles, one ZN-2 video frame. The
current evidence therefore points to a frame-phase offset in latch delivery,
not a different R3000A code path. It does not establish that this offset causes
the DSP's noisy output; compare the Z80 QSound register stream and DSP state
next before changing latch or DSP arithmetic.

### Checklist audit: DSP16A arithmetic, addressing, QSound and SIO

The checklist was reviewed against the local core, MAME's DSP16/QSound
implementation, and the JTDSP16 RTL used by the ZN2 MiSTer project. This was a
source audit, not cycle-by-cycle proof of the noisy SFEX2PA run.

- **Accumulator width, saturation, multiplier and Y:** no general guard-bit
  truncation, unsigned multiply, or YH/YL defect was found. The local state
  retains the 36-bit accumulator plus sign/overflow extension; AUC-controlled
  extraction saturation, signed `X * YH`, YH/YL loads and arithmetic shifts
  match the inspected RTL/MAME contracts. Existing DAU tests exercise selected
  edge vectors but do not constitute broad architectural parity coverage.
- **Two known MAME-vs-RTL DAU differences remain intentional and need hardware
  vectors before changing.** For F2=9, local code selects low-word preservation
  from the destination AUC bit, as wired by JTDSP16's `clr_alo`; MAME selects
  using the source AUC bit. For product alignment 2, local truncates `P << 2`
  to 32 bits and sign-extends, matching RTL `p_ext`; MAME's 64-bit expression
  differs for negative P. F2=11 rounding also follows the RTL's accumulator
  bit-15 increment, while MAME uses its own behavior and documents rounding as
  uncertain. These are not evidence that the local RTL-oriented behavior is
  wrong or that any one of them causes the audible noise.
- **Circular/data/program addressing and nominal cycles:** R0–R3 postmodify,
  RB/RE wrap, PT's 12-bit page offset, separate IROM/external program/data
  spaces, two DSP clocks per machine cycle, per-cycle SIO/PIO stepping, and
  the inspected IRQ/IE arbitration match MAME for the QSound configuration.
  The reusable core still lacks an EXM input to select external program space
  over IROM at low addresses; QSound uses the default IROM mapping, but other
  DSP16A boards may require this pin.
- **Z80/QSound command handshake:** D000 high byte, D001 low byte, D002 command
  ordering matches MAME. D002 clears ready immediately, then command delivery
  is deferred to the instruction/scheduler boundary. DSP PIO command reads
  clear pending IRQ and defer ready assertion. D007 is passive in MAME; local
  catches the DSP up to the Z80 timestamp before returning it. No static
  handshake mismatch was identified; aligned status/ready traces are still
  required to validate timing.
- **SFEX2PA sample ROM path:** the local 24-bit bank/offset calculation, PSEL
  bank-half selection and high-byte return for 8-bit samples match MAME's
  `dsp_sample_r`/`dsp_pio_w`. The SFEX2PA QSound region is 4 MiB; local
  power-of-two padding and modulo addressing mirror that region's 4 MiB
  address aliasing. The loader's 16-bit byte order agrees with MAME's
  `ROM_LOAD16_WORD_SWAP` and the Jotego ZN2 ROM-bus comment.
- **Serial output:** both MAME and local capture on OCK rising edges, shift
  MSB-first for 16 bits, use PSEL for channel selection, and update the stereo
  stream at the left-word boundary before replacing that sample. JTDSP16 is a
  useful logic reference, but its SIO sets OLD permanently high because OLD
  is unimplemented; it cannot validate the MAME/local OLD-based word framing
  or its exact timing.

The current evidence does not identify a checklist item as the remaining noise
cause. The next decisive comparison is a shared QSound command and DSP-cycle
trace that records D007 reads/ready edges, the first differing DSP state, and
the corresponding SDX word. Prior local/MAME traces diverged in command history
before the DSP-state snapshots, so their arithmetic-state difference cannot be
used to pick a fix. Keep the HLE selectable and do not conceal LLE differences
with gain, clipping or filtering.

### Follow-up: Claude's proposed specific fixes

Each code-level suggestion was checked against MAME and the DSP16A RTL before
changing behavior:

- **F2 left shifts:** no change. MAME explicitly computes each left shift as
  `s32(u32(u64(s)) << n)`, so the 32-bit truncation is part of its current
  contract. JTDSP16 also slices the shifted value to 32 bits before
  sign-extension. The local executable test exercises F2 `<<1` with a source
  whose guard bits would make a widened shift differ; it confirms the current
  result matches both references. Widening this operation would be incorrect.
- **DO K=1:** local code now executes the load-pass body only once. MAME's
  DSP16 documentation asserts K is 2–127, so K=1 is outside the documented
  instruction range and cannot explain normal DL-1425 firmware output. A
  defensive regression test confirms the local interpreter does not execute
  that invalid case twice.
- **Aliased DAU source/destination:** local code snapshots the source
  accumulator before writing the destination, so an instruction with `src ==
  dest` leaves Y with the old source value. A firmware-level regression vector
  verifies this ordering against MAME's C++ reference semantics.
- **Sample pacing:** output frame targets now use the exact rational DSP-clock
  ratio (`60 MHz / 2496`) against elapsed Z80 cycles instead of a floored
  24,038 Hz intermediate. A timing test verifies the accumulated count over a
  minute. This removes the prior 27-frame/minute arithmetic drift; it does not
  establish that the audible LLE waveform matches MAME's stream timing.
- **Forced L/R alternation:** the user A/B run produced the same hiss with
  forced alternation as with PSEL. This makes channel selection an unlikely
  cause; normal operation remains on the MAME PSEL rule.

The remaining checklist areas were rechecked against the local implementation
and MAME source: signed multiplier and 36-bit accumulator handling; YH/YL and
AUC extraction; R0–R3 postmodify and RB/RE wrap; PT page wrap; interrupt
arbitration and two DSP clocks per machine cycle; PIO busy/ready sequencing;
the 24-bit sample-ROM bank and high-byte data bus; and OCK/OLD/DO serial
framing. No additional static mismatch was confirmed in these paths. Existing
trace captures are not cycle-aligned after the QSound command history diverges,
so they cannot prove instruction-by-instruction parity or identify the first
state that creates the excessive LLE amplitude. That remains the unresolved
diagnostic, rather than a reason to change these source-audited paths blindly.

The existing 720-frame run gives a separate scheduler sanity check: for
300,872,039 PSX execute cycles at 25 MHz, the QSound DSP should have executed
361,046,446.8 machine cycles at 60 MHz with two input clocks per machine
cycle. It executed 361,046,460, a difference of 13.2 DSP cycles. The recovered
289,291 stereo frames are within nine frames of the serial cadence implied by
that DSP total. This rules out a large cumulative clock-rate or scheduler
backlog in that capture; it does not rule out sample-phase skew at individual
PIO yields or prove the LLE sample values are correct.

### 2026-11-07: LLE binary confirmed and raw-word byte order corrected

The MAME reference executable for this comparison is
`/home/luiz/git/MameRecomp/build/qsoundtest`. Its `-listdevices sfex2pa`
output includes `qsound` with a nested `WE|AT&T DSP16A`, confirming that this
specific build is LLE. The separate `/home/luiz/git/mame/qsoundtest` binary is
not the reference for this run.

The diagnostic `qsound.pcm` file is a raw serial-word stream, not a WAV. MAME
writes each word high byte first in `qsound_device::dsp_ock_w`:
`put(u8(m_sr >> 8)); put(u8(m_sr));`. Read it as big-endian signed 16-bit
words. Earlier RMS calculations that interpreted this file as little-endian
are invalid and must not be used as evidence. Re-reading the saved capture
`/tmp/qsound.pcm` as big-endian gives 964,224 words, RMS 1,065, peak 16,649,
and first nonzero word 780,698. The separate local 1,200-frame capture
`/tmp/qsound_local_frame1000.raw` is little-endian interleaved PCM: 964,332
words, RMS 7,554, peak 32,768, and first nonzero word 777,536. These are
fresh-NVRAM runs with coin/start input, but their sample counts and onset differ
slightly and the streams are not aligned. This is strong evidence of a
substantial LLE signal mismatch, not proof of its first cause.

The local 1,200-frame trace `/tmp/qsound_local_frame1000.csv` begins at DSP
cycle 500,000,000 and contains 16,384 instruction-boundary records. The run
ends at 601,744,117 DSP machine cycles, confirming that the local DSP advances
about 0.5 million machine cycles per game frame. The earlier 940-million-cycle
threshold was therefore beyond this run and produced only a CSV header.

MAME frame snapshots in `/tmp/qsound_mame_dsp_frame.csv` show the same firmware
routine family (`PC=0x51e`, `PR=0x04b4`) around frames 941–1109, but they are
only once-per-frame samples and do not align the instruction stream. The
working MAME debugger setup uses the Qt debugger; `-debugger none` leaves the
DSP stopped at a breakpoint without producing a useful trace. A known-good
early trace at `/tmp/qsound_mame_hit500.tr` reaches `PC=0x05d7`, `PR=0x03c3`
at cycle 1,598,598, so it is still a boot-context capture, not the active
sample-mixing segment. The first active DSP state divergence remains
unidentified.

### 2026-11-07: boot alignment correction

The apparent `A0` mismatch previously reported at instruction `0x3c90` was a
false positive: it matched a later local firmware loop against MAME's initial
boot execution using only repeated PC/PR/PT values. A fresh local trace from
DSP cycle zero (`/tmp/qsound_local_boot0.csv`) and a MAME debugger probe
(`/tmp/qsound_memprobe.tr`) now compare the first execution of that code.
Both reach `PC=0x03ce`, `PR=0x03c3`, `R0=0x00f3` with `A0=0`; MAME reports
`dw@0x00f3=0` after the instruction and local A0 remains zero. This confirms
the first boot-time read agrees and the earlier `0xd5` versus `0xc8` values
came from mismatched later loop iterations, not a proven DSP-core defect.

The initial snapshots are close but not cycle-identical (MAME debugger total
cycles 7,512 versus local DSP machine cycle 7,524 at `PC=0x03cf`). The LLE
audio mismatch therefore still needs an instruction-sequence alignment after
the game begins sending sound commands. Do not treat the repeated-loop
register mismatch above as evidence for changing accumulator arithmetic or
data-RAM semantics.

### 2026-11-07: first QSound command-stream mismatch and AOT parity

The saved MAME and local command captures
`/tmp/qsound_mame_aligned_repro.csv` and
`/tmp/qsound_local_aligned_repro2.log` use byte-identical post-run AT28C16
images (SHA-256
`c8f681548eb897e37d522d623203a8acee4150ed67c5ec03a2b0046192b6fa81`) and
the same frame-scheduled coin/start/button inputs. Reconstructing each D001,
D000, D002 write triplet shows matching QSound register payloads through
one-based command 115. MAME's `qsound_w()` contract is D000=data high byte,
D001=data low byte, and D002=register address. The first payload mismatch is
one-based command 116 (zero-based index 115): MAME writes register `0x66`,
data `0x0000`, from Z80 PC `0x29f3` at 16.290226750 s; MameRecomp writes
register `0x46`, data `0x0a01`, from PC `0x29e5` at Z80 cycle 129,319,002
(16.164875250 s at 8 MHz). The previous command 115 is register/data
`0x8f/0x012f` on both sides. Thus the first captured mismatch is already in
the Z80-to-QSound input stream; it is not evidence of an earlier DSP
arithmetic mismatch. The event-time offset is non-monotonic: command-16
timestamps differ by 16.7 ms in the opposite direction, command 32 is aligned
to within 11 ns, and the offset grows again before command 116. This points to
a scheduler/game-state timeline difference that must be aligned before
comparing later DSP state or SDX output. The capture does not prove that this
command mismatch caused the user-reported hiss.

A current 1,620-frame AOT/interpreter replay with identical NVRAM seed and
coin/start/button events produces identical DSP instruction totals, sample-ROM
reads, PIO counts, serial word counts, nonzero SDX count, final SDX value, and
final DSP state. AOT runs 656,025,037 instructions with zero interpreter
fallback; interpreter-only runs the same count entirely interpreted. The
measured elapsed time is 42.094 s for AOT and 34.408 s for interpreter-only.
This excludes AOT-vs-interpreter semantics as the source of a difference in
that replay, but the current per-opcode AOT dispatch regresses performance and
does not replace the required MAME-vs-local aligned DSP/command/serial trace.

### 2026-11-07: DSP16A AOT frequency profile

`MAMERECOMP_QSOUND_DSP_AOT_MANIFEST` now writes `pc,opcode,executions`. The
manifest still records only instructions that reached the architectural
instruction dispatcher; it does not scan operand reads or claim complete
opcode-set coverage. `tools/dsp16a/build_aot.py --min-executions N` can emit
entries only for PCs whose observed execution count reaches `N`.

A 1,200-frame SFEX2PA profile with isolated erased NVRAM and the same
coin/start/button schedule executed 485,982,347 DSP instructions across 780
internal-ROM PCs, using 234 unique opcodes. The 27 hottest PCs account for
75.7% of execution; 205 PCs account for 93.4%. AOT coverage of 485,982,347
instructions with zero fallback in that replay means the observed path was
covered, not that all DSP16A opcodes have native implementations.

The full 780-entry table took 26.4 s headless. A 205-entry table (minimum
100,000 executions) took 27.0 s, while a 27-entry table (minimum 5,000,000)
took 26.1 s; the all-interpreter replay took 21.1 s. These measurements do
not show a useful speedup from pruning cold PCs. All variants produced the
same guest/DSP counters and final DSP state; selective tables naturally moved
the omitted instructions to the interpreter. The current AoT advantage is
therefore not improved by a simple frequency cutoff. This headless benchmark
isolates the CPU execution path and does not rule out additional scheduling or
presentation costs in windowed gameplay.

The runtime profiler now reports QSound sync request count and DSP run-slice
size when `MAMERECOMP_ZN_PROFILE_RUNTIME=1` is set. In a matched 1,200-frame
full-table replay, AoT and interpreter both made 26,735 sync requests and
26,733 DSP slices, advancing exactly 1,203,488,190 DSP clocks. Average slice
size was 45,018 clocks (minimum 82, maximum 1,003,073) in both runs. The
measured DSP run time was 20.512 s with AoT and 15.822 s interpreted; the GP0
and R3000A/bus/timer measurements were nearly unchanged. This rules out a
different sync-call cadence as the source of this AoT/interpreter timing gap
in the headless replay. Z80 timing contains nested QSound synchronization and
should not be added to the DSP timing as an independent cost.
