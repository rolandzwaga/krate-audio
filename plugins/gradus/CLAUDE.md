# plugins/gradus/ — Gradus Standalone Step Arpeggiator

Auto-loads when working under `plugins/gradus/`. Root `CLAUDE.md` still applies.

- **Type:** standalone step arpeggiator instrument (AU `aumu`), MIDI in/out + stereo out.
  Extracted from Ruinae's arp section. **Version:** see `version.json`.
- **src skeleton:** `controller/ dsp/ parameters/ preset/ processor/ ui/`
  (`dsp/` = minimal built-in audition voice).
- **Param IDs:** arp base 3000 (`kArpBaseId`=3000 ... 3372). **These IDs are SHARED with Ruinae.**
  The shared SAVE prefix is unified in `plugins/shared/src/parameters/arp_params_common.h`
  (`Krate::Shared::saveArpParamsShared`) — Gradus's `saveArpParams` calls it, then appends its
  Gradus-specific tail (MIDI-delay / sequencer / Markov / speed-curve lanes). A cross-plugin byte-golden
  test guards shared-save identity. `loadArpParams` stays Gradus-local (its clamp ranges / version gates
  diverge from Ruinae — e.g. `mode` clamps 0-11 here vs 0-9 there — so it must NOT be unified).
  Pipeline: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **Tests:** `gradus_tests`.
  ```bash
  build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Gradus.vst3"`
