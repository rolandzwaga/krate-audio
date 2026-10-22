# plugins/profundum/ — Profundum Mass Bass Instrument

Auto-loads when working under `plugins/profundum/`. Root `CLAUDE.md` still applies.

- **Type:** mass bass instrument (AU `aumu`, subtype `Prfd`, manufacturer `KrAt`,
  bundle base `com.krateaudio.profundum`). **Version:** see `version.json` (unreleased `0.1.0`).
- **Roadmap:** `specs/Profundum-roadmap.md` — the plugin wrapper starts at Phase 10
  (`specs/profundum-phase10-plugin-scaffold/`); the DSP it drives is Phases 1–9 in
  `dsp/include/krate/dsp/` (`ProfundumEngine`, `ProfundumVoice`, `ProfundumMacroMatrix`, `ProfundumMotion`).
  **No DSP lives in this plugin** — `src/engine/` holds prepare-time *config* only.
- **src skeleton:** `controller/ engine/ parameters/ preset/ processor/ ui/ update/` plus `entry.cpp` and
  `plugin_ids.h` — `engine/` = `profundum_engine_config.h` (`makeProfundumEngineConfig()` returns
  `ProfundumEngineConfig{}` unchanged, plus `kOutputGainSmoothMs`; no DSP); `parameters/` = one pack header per
  band (`global_params.h`, `macro_params.h`); `preset/` and `update/` are the shared-config adapters
  (`makeProfundumPresetConfig()` / `makeProfundumUpdateConfig()`; the controller never creates an
  `UpdateChecker`); `ui/` holds only `.gitkeep` until the UI phase.
  Generated, never hand-edited, never committed (see `.gitignore`): `src/version.h`,
  `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` — only `audiounitconfig.h.in` is authored.
- **Buses:** 1 event input, 1 stereo audio output, **no `addAudioInput()`**. `kSupportedNumChannels` is
  `02` and `au-info.plist` declares exactly `Inputs 0 / Outputs 2`; a mismatch between the bus layout and
  those two files is the documented AU `-10875` init failure.
- **Param IDs:** flat base 0 with 100-ID section gaps. The **reserved map is load-bearing** — a later phase
  must claim its own band, never squat in another (code of record: the comment above
  `enum ParameterIDs` in `src/plugin_ids.h`, and the `k{Section}ParamRangeEnd` constants in it):

  | Range | Section | Phase |
  |---|---|---|
  | 0–99 | Global: output gain 0, voice mode 1, polyphony 2, output ceiling 3 | 10 (shipped) |
  | 100–199 | Macros + Mass + Morph position: Weight 100, Dark/Bright 101, Tight/Loose 102, Clean/Violent 103; Mass and Morph position at >= 104 | 100–103 in 10 (**live**); 104+ in 11 |
  | 200–299 | Core | 11 |
  | 300–399 | Anchor (includes `anchorFoldFeed`) | 11 |
  | 400–499 | Drop / note behaviour | 11 |
  | 500–599 | Fold / Shape | 11 |
  | 600–699 | Character | 11 |
  | 700–799 | Mass dynamics | 11 |
  | 800–899 | Stereo field | 11 |
  | 900–999 | Motion | 11 |
  | 1000–1099 | Morph control (anchor morph mode, edit target) | 11 |

  **Eight registered IDs, eight persisted.** Macro IDs obey `id - 100 == static_cast<int>(ProfundumMacro::X)`.
  Registered types and ranges are **frozen**: `kOutputGainId`, `kOutputCeilingId` and the four macros are
  plain `Steinberg::Vst::Parameter`s, `kVoiceModeId` ("Mono", "Poly") and `kPolyphonyId` ("1".."8") are
  `StringListParameter`s — never swap a type or change a range at the same ID. Titles are ASCII:
  "Weight", "Dark/Bright", "Tight/Loose", "Clean/Violent".
  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **State:** `kCurrentStateVersion = 1` and `kStateV1Bytes = 36` live in `plugin_ids.h` (shared by processor and
  controller with no cross-include; the byte count is a `static_assert`ed sum). Little-endian: int32 version,
  then the global block (float outputGain, int32 voiceMode, int32 polyphony, float outputCeilingDb), then the
  four macro floats in `ProfundumMacro` order. Version `> 1` → `kResultFalse`, nothing changed; a truncated
  stream leaves the unread parameters unchanged. The next format change appends after byte 36.
- **Tests:** `profundum_tests` (namespace `ProfundumPluginTest`; every case carries `[profundum]`). The timed CPU
  case `Profundum_ProcessorCpu` is hidden behind `[.perf]`; run it only via
  `node tools/run-cpu-tests.js profundum_tests`, alone.
  ```bash
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Profundum profundum_tests
  build/windows-x64-release/bin/Release/profundum_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Profundum.vst3"`

## IMMUTABLE identity — the FUIDs

Both GUIDs were generated once in Phase 10 (plan §2.1, 2026-10-22) and are **IMMUTABLE** — never change them.
Changing either one after release makes every host treat the plugin as a different class: saved projects lose
their instance, and preset/state bindings referencing the processor UID are orphaned. Never regenerate, never
"tidy", never reuse elsewhere.

| Class | Constant | Value (verbatim, as in `src/plugin_ids.h`) |
|---|---|---|
| Processor | `Profundum::kProcessorUID` | `0x94C64960, 0xF7E844BE, 0xB439A0F3, 0x95AF84C9` |
| Controller | `Profundum::kControllerUID` | `0x449C9192, 0xE39F4DA0, 0xAD6E7BCA, 0x260F4CA0` |

The Windows installer `AppId` (`installers/windows/setup.iss`) is a third, separate GUID,
`{4DDA5FC2-4CD6-44E2-BF11-18887F0F6CD9}` — equally immutable once an installer ships.

`src/plugin_ids.h` is the code-of-record; this table is the **durable prose record**. If the two ever
disagree, `plugin_ids.h` as shipped in the released binary wins — and the disagreement is a bug to be
investigated, not silently "fixed" by editing the header.

## Decisions that outlive Phase 10

1. **Latency is 64 samples, constant.** It is `ProfundumEngine::kLimiterLookaheadSamples` (§10.3), reported whenever the engine exists, before and after prepare, at every sample rate. No `kLatencyChanged` message exists.
2. **Output gain sits after the limiter and only attenuates**, as a dB taper of −60 dB .. 0 dB with −∞ at normalized 0 and unity at 1.0. Never widen its range or add a boost.
3. **Polyphony "1".."8" is registered once** and is permanent. A later raise of `kMaxVoices` needs a new parameter ID.
4. **Preset categories only grow.** `Sub` is permanent.
5. **There is no `IMidiMapping` yet**; Phase 11 adds it. Profundum is unreleased at `0.1.0`, so adding the interface to the
   controller FUID costs nothing in host caches.
6. Mass and Morph position are deliberately absent until the Phase 11 endpoint model.
7. The plugin test namespace is `ProfundumPluginTest`, never `ProfundumTest`, which the dsp test helpers already use.
8. **The supported host sample-rate range is [22050, 192000].** Outside it `setupProcessing` returns `kResultFalse` and the plugin renders silence (FR-023).
9. **The four Personality macros are live**; 0.5 is the identity. **Host tempo is forwarded** when `kTempoValid` is set; otherwise the engine's 120 BPM stands.
