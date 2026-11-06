# DSP16A core: source and validation notes

`dsp16a.cpp` is a board independent interpreter for the WE/AT&T DSP16A. Its
instruction sequencing, register model, cache execution, and serial/parallel
interfaces are adapted from MAME's `src/devices/cpu/dsp16/` implementation by
Vas Crabb. The upstream files identify their license as BSD-3-Clause; the
MameRecomp files retain that license tag and attribution. The QSound-specific
ROM, command latch, serial DAC recovery, and HLE live in `core/chips/qsound.*`,
outside the CPU core.

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
- Loading A0H/A1H clears the corresponding low word when AUC[4]/AUC[5] is
  clear.
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

## Current QSound LLE evidence

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

The core still needs aligned MAME-vs-MameRecomp DSP state/SDX vectors and tests
for signed multiplication, P, A0/A1 accumulation, saturation, and all DAU
alignment modes. Keep QSound HLE selectable as a fallback while doing this.

The launcher now defaults SFEX2PA to DSP16A LLE. `--qsound-mode=hle` forces
the HLE path. `--qsound-mode=mixed` currently starts in HLE; automatic runtime
failover to HLE is still pending, and the startup message says so explicitly.
