# plugins/ruinae/ — Ruinae Synthesizer Plugin

Auto-loads when working under `plugins/ruinae/`. Root `CLAUDE.md` still applies.

- **Type:** synthesizer instrument (AU `aumu`). **Version:** see `version.json`.
- **src skeleton:** `controller/ engine/ parameters/ preset/ processor/ update/`
  (`engine/` = synth engine, voice, effects chain).
- **Param IDs:** section bases (Osc A base 100, ...). **Arp section IDs 3000–3372 are SHARED with Gradus** —
  any change to arp save/load/registration must stay behavior-compatible with `plugins/gradus/`.
  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **Tests:** `ruinae_tests`.
  ```bash
  build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- **Reference decomposition:** Ruinae is the in-repo model for splitting large VST files —
  `controller.cpp` (~907 lines) is split across `controller_view_sync`, `controller_verify_view`,
  `controller_mod_matrix`, `controller_adsr`, `controller_arp`, `controller_settings`,
  `controller_param_display`, `controller_presets`; `processor.cpp` (~701) across 5 files.
  Copy this `controller_*` / `processor_*` pattern when a file grows past ~1500 lines.
