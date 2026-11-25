# phase1_default.bin re-baselines

`phase1_default.bin` is the audio golden of `approval/test_phase1_regression.cpp`: the default
`PadConfig` on pad 0 (note 36, velocity 0.8), rendered through `Membrum::VoicePool` at 44.1 kHz in
64-sample blocks for 500 ms, left channel. It is compared by render fingerprint
(`tests/test_helpers/render_fingerprint.h`) at the shared tolerances.

A re-baseline is a reviewed decision. Regenerate only with
`membrum_tests.exe "Generate Phase 1 golden reference"`, and append one line below: date, task id,
and why the rendered audio moved.

- 2026-11-25, T2.1 (MB-041, MB-142): rendered through the voice pool instead of the unused per-sample
  `DrumVoice::process()` renderer. Against the previous golden the fingerprint moved by 8.5 % (worst
  metric) and 0.017 (worst checkpoint).
