# plugins/gradus/ — Gradus Standalone Step Arpeggiator

Auto-loads when working under `plugins/gradus/`. Root `CLAUDE.md` still applies.

- **Type:** standalone step arpeggiator instrument (AU `aumu`), MIDI in/out + stereo out.
  Extracted from Ruinae's arp section. **Version:** see `version.json`.
- **src skeleton:** `controller/ dsp/ parameters/ preset/ processor/ ui/`
  (`dsp/` = minimal built-in audition voice).
- **Param IDs:** arp base 3000 (`kArpBaseId`=3000 ... 3372). **These IDs are SHARED with Ruinae** —
  a save/load or registration fix here must stay behavior-compatible with `plugins/ruinae/`.
  The two plugins currently keep parallel copies of `parameters/arpeggiator_params.h`; Gradus's copy
  carries the unique MIDI-delay / sequencer / Markov / speed-curve lanes on top of the shared 3000–3372 core.
  Pipeline: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **Tests:** `gradus_tests`.
  ```bash
  build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Gradus.vst3"`
