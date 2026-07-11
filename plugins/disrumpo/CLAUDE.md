# plugins/disrumpo/ — Disrumpo Multi-band Distortion Plugin

Auto-loads when working under `plugins/disrumpo/`. Root `CLAUDE.md` still applies.

- **Type:** multi-band distortion effect (Fx | Distortion). **Version:** see `version.json`.
- **src skeleton:** `controller/ dsp/ preset/ processor/ update/` — **no `parameters/` dir**.
  Plugin-local DSP (morph engine, sweep, bands) lives in `dsp/`.
- **Param IDs:** bit-packed hex scheme — `makeBandParamId(band, type)` (e.g. `0xF000`),
  `makeNodeParamId(...)` (e.g. `0x2101`). **Not** a flat range; do NOT try to range-parse it.
  Pipeline to add one: `plugin_ids.h → processor → controller → resources/editor.uidesc`.
- **Tests:** `disrumpo_tests`.
  ```bash
  build/windows-x64-release/bin/Release/disrumpo_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"`
- **UI:** `resources/editor.uidesc` (custom views: morph pad, spectrum).
- **Processor is split (D2):** `processor.cpp` (core: lifecycle/process/bus/morph/solo),
  `processor_state.cpp` (getState/setState), `processor_params.cpp` (shape-slot map +
  processParameterChanges), `processor_messaging.cpp` (connect/disconnect/spectrum/mod-offset).
  All four are the same `Processor` class across TUs — put a new method in the file that matches
  its concern.
- **Heads-up:** `controller.cpp` (~2004 lines) is still an unsplit monolith — offset-read/Grep to
  locate before editing; anchor Edits carefully (repeated boilerplate causes collisions).
