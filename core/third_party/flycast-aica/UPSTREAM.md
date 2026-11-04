# Flycast AICA DSP reference snapshot

Selected Flycast AICA sources staged for the Naomi AICA DSP interpreter port.

- Upstream: https://github.com/flycast/flycast
- Revision: `1b63c8a3677cc8eb1292863c4fe8fc8f94674fc8`
- License: GNU GPL version 2; see [`LICENSE`](LICENSE).
- Reference files: `dsp_interp.cpp`, `dsp.cpp`, `aica_mem.cpp`, and the
  channel/voice generator in `sgc_if.cpp` with its AICA interfaces.
- MameRecomp adapter: `src/naomi/naomi_aica_voices.cpp` compiles the upstream
  SG channel engine with `src/naomi/naomi_aica_voice_shims.hpp`; the separate
  `src/naomi/naomi_aica_dsp.cpp` implements the DSP interpreter. Register,
  wave RAM and sample integration live in `src/naomi/naomi_address_space.cpp`.

The selected implementation is Flycast's 128-step DSP interpreter and its
64-channel generator (PCM, ADPCM, envelopes, LFO, filters and mixing). The DSP
JIT/recompiler is not part of this port. NaomiAddressSpace binds the shared
AICA register file and 8 MiB wave RAM, dispatches channel/common-register
accesses and feeds voice MIXS values into the DSP on each sample tick. Small
compatibility types replace Flycast's host framework. Host audio output and
CDDA remain detached from this Naomi runtime integration.
