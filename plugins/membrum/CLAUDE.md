# plugins/membrum/ — Membrum Drum Synthesizer

Auto-loads when working under `plugins/membrum/`. Root `CLAUDE.md` still applies.
**This is the current active-development plugin.**

- **Type:** physically-modelled drum synthesizer instrument (AU `aumu`). **Version:** see `version.json`.
- **src skeleton:** `controller/ dsp/ preset/ processor/ state/ voice_pool/ ui/`
  — **no `parameters/` dir, no `update/` dir** (unlike the other plugins).
  `dsp/` splits into `dsp/{bodies,exciters,unnatural}/`; `state/` = state codec; `voice_pool/` = polyphony.
- **Param IDs:** flat base 100 (`kMaterialId`=100, `kSizeId`=101, `kDecayId`=102, `kStrikePositionId`=103, ...).
  No `parameters/` helper dir, so the pipeline is:
  `plugin_ids.h → processor → controller → resources/editor.uidesc`.
- **Tests:** `membrum_tests`.
  ```bash
  build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"`

## Domain constraints (load-bearing — violating these breaks presets/tests)

- **Preset kit categories are FIXED:** `Acoustic`, `Electronic`, `Percussive`, `Unnatural`.
  Never invent new ones; the filesystem subdir AND the XML metadata must both match.
- **Ring/range constraints:** `maxPolyphony` must be `[4,16]`; `modeInject > 0` rings undamped (flat plateau);
  Friction sustains without note-off (use NoiseBurst for one-shot swirls). Enforced by factory round-trip
  + infinite-ring tests.
- **Presets** live in `C:\ProgramData\Krate Audio\Membrum\Kits\{category}\` — NOT inside the VST3 bundle.
  Regenerated presets must be copied there. Install via the build target; never hand-copy a single VST3 file
  (leaves `Resources/editor.uidesc` missing → blank UI + crash on unload).

## Where the deeper lore lives

- Skill: `membrum-signal-path` (full per-voice signal-path audit + dynamics/correctness diagnostic).
- Tool: `tools/membrum-fit/` (offline drum-sample fitter; see its `AGENT_GUIDE.md`).
- UI data bridge: piggyback per-block UI data on the `MetersBlock` DataExchange — don't add new queues/IMessage loops.
