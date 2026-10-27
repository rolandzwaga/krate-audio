# plugins/innexus/ — Innexus Harmonic Resynthesis Instrument

Auto-loads when working under `plugins/innexus/`. Root `CLAUDE.md` still applies.

- **Type:** harmonic analysis/resynthesis instrument (AU `aumu`). **Version:** see `version.json`.
- **src skeleton:** `controller/ dsp/ engine/ parameters/ preset/ processor/ update/`
  (`dsp/` = sample analyzer + live analysis pipeline).
- **Param IDs:** section bases — 200s = envelope (`kReleaseTimeId`=200, ...), 400s = harmonic
  (`kHarmonicLevelId`=400). Note `kPartialCountId`=202 is a **StringListParameter** ("48"/"64"/"80"/"96"),
  not a range. Pipeline: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **Tests:** `innexus_tests` — has both unit (`tests/unit/{processor,vst}/`) and full-pipeline
  `tests/integration/` tests. Run both kinds after processor changes.
  ```bash
  node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' innexus_tests   # sharded, see root CLAUDE.md; CPU lane alone: node tools/run-cpu-tests.js innexus_tests
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`
- **AU config:** instrument — see `resources/au-info.plist` and `resources/auv3/audiounitconfig.h(.in)`.
  If you add an audio input bus (e.g. sidechain), update BOTH files (channel config + `kSupportedNumChannels`
  digit-pair format — each char is one digit, paired as in/out).
