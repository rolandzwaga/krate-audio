# plugins/iterum/ — Iterum Delay Plugin

Auto-loads when working under `plugins/iterum/`. Root `CLAUDE.md` still applies.

- **Type:** delay effect (Fx). **Version:** see `version.json` (edit ONLY that for bumps).
- **src skeleton:** `controller/ parameters/ preset/ processor/ ui/ update/`
  (`parameters/` holds per-mode registration helpers; `ui/` holds the tap-pattern editor custom view).
- **Param IDs:** mode-prefixed enum scheme `k{Mode}{Parameter}Id` (Granular base 100, Spectral, Shimmer,
  Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze). Pipeline to add one:
  `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **Tests:** two targets — `plugin_tests` (unit) **and** `approval_tests` (golden-reference approvals).
  Run BOTH for any DSP/output change:
  ```bash
  build/windows-x64-release/bin/Release/plugin_tests.exe    2>&1 | tail -5
  build/windows-x64-release/bin/Release/approval_tests.exe  2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"`
- **UI:** `resources/editor.uidesc`.
