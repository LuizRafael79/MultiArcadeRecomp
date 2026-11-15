# ymfm

Source: MAME's `3rdparty/ymfm` (Aaron Giles, BSD 3-Clause).

This subset contains the generic FM engine, YM2151 OPM, YM3812 OPL2 and
YM3438/OPN implementations used by the System 16A, System 18 and Seibu Toki
audio runtimes. OPN includes shared ADPCM/SSG sources. OPL/PCM were copied
from the local MAME 0.289 `3rdparty/ymfm` tree and use the in-tree common FM
and ADPCM headers; `ymfm_adpcm.h` gained the equivalent status-clear helper
expected by this OPL revision. The corresponding license is in [`docs/license/ymfm/LICENSE`](../../license/ymfm/LICENSE).
