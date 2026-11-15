# Sega System 18 TODO

## Shadow Dancer audio validation

- The RF5C68 sample and gain arithmetic must remain signed: converting negative samples to unsigned before division caused saturation and excessively loud voices. `rf5c68_test` covers both sample polarities.
- In game pan mode, the board combines the RF5C68 outputs into mono with unity routes, as in MAME; spread mode retains separate stereo outputs.
- A 3,000-frame attract-mode capture (about 52.4 seconds) was compared with MAME 0.289 at default mixer levels. The 100 ms RMS envelopes correlated at 0.9997; peak output was 17,134 versus 17,721 for MAME. This checks that interval, not every game sound or volume setting.
- A separate 3,000-frame spread capture retained distinct channels and contained no full-scale clipped samples. Continue validating gameplay, other sets, and the physical output with listening tests.
- Host playback now uses the board-neutral `SegaAudioSink` and lives in the
  presentation module. A dummy-driver 3,000-frame run after this extraction
  delivered 2,912,225 stereo PCM frames, peak 17,134, with no clipped samples.

## Moonwalker (`mwalkd`) runtime

- [x] Correct the gameplay timing and Michael movement, the repeating robot transformation, hostage/force-field/HELP ME rendering, and the audio path. The user confirmed that gameplay and sound now work, the hostages/effect/text render correctly, and the window remains responsive when unfocused.
- [x] Connect the 315-5195-controlled 68000 reset and the System 18 I8751 interrupt path required by Moonwalker. Keep this wiring in the board runtime and shared mapper/MCU devices, not in the MCS-51 or 68000 CPU cores.
- [x] Reach active gameplay with the 315-5313 enabled and its mixed VDP layers visible. The formerly corrupted hostage/effect capture was fixed in subsequent changes and is superseded by the user's later confirmation.
- [x] Restore audible Moonwalker music and effects. Earlier logs with zero FM/PCM peaks refer to a broken intermediate build and are no longer current.
- [ ] Continue pixel/timing comparisons against MAME for additional Moonwalker stages and other System 18 games; user confirmation covers the defects reported in this session, not every game state or clone set.


Current evidence and captures are recorded in [VIDEO.md](VIDEO.md). The project-wide System 18 tracking entry is in [docs/TODO.md](../../../TODO.md).
