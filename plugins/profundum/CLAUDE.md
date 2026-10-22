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
  band (`global_params.h`, `macro_params.h`, and from Phase 11 `core_params.h`, `anchor_params.h`, `note_params.h`,
  `fold_shape_params.h`, `character_params.h`, `dynamics_params.h`, `field_params.h`, `motion_params.h`,
  `morph_params.h`, `performance_params.h`) plus the 60-row E-parameter table (`sound_param_row.h`,
  `sound_param_table.h`); `preset/` and `update/` are the shared-config adapters
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

  | Range | Section | Registered (count) | Phase |
  |---|---|---|---|
  | 0–99 | Global: output gain 0, voice mode 1, polyphony 2, output ceiling 3; hidden performance: Pitch Bend 4, Sustain Pedal 5, Mod Wheel 6, Channel Pressure 7 | 0–7 (8) | 0–3 in 10; 4–7 in 11 |
  | 100–199 | Macros + Mass + Morph position: Weight 100, Dark/Bright 101, Tight/Loose 102, Clean/Violent 103, Mass 104, Morph Position 105 | 100–105 (6) | 100–103 in 10; 104–105 in 11 |
  | 200–299 | Core | 200–208 (9) | 11 |
  | 300–399 | Anchor (includes `anchorFoldFeed` 306) | 300–306 (7) | 11 |
  | 400–499 | Drop / note behaviour / amp envelope: Drop 400–403, note behaviour 410–417, amp envelope 420–426 | 19 | 11 |
  | 500–599 | Fold / Shape | 500–501 (2) | 11 |
  | 600–699 | Character | 600–602 (3) | 11 |
  | 700–799 | Mass dynamics | 700–701 (2) | 11 |
  | 800–899 | Stereo field | 800–809 (10) | 11 |
  | 900–999 | Motion | 900–914 (15) | 11 |
  | 1000–1099 | Morph control: Anchor Morph Mode 1000, Edit Target 1001, Morph Ramp Time 1002 | 1000–1002 (3) | 11 |

  **84 registered IDs** (Phase 11): 60 E-parameters (the morphed sound leaves: core, anchor, drop, amp envelope, fold,
  character, dynamics, field, motion and Mass 104), 8 note-behaviour (410–417), 4 morph-control (105, 1000–1002), 4 hidden
  performance (4–7) and the 8 Phase 10 IDs. The route table covers IDs 0..1002 (`kNumRoutedIds = 1003`).
  **Edit Target model:** an E-parameter edit writes the endpoint (A or B) selected by Edit Target (1001); switching the
  target re-displays the 60 E-parameters from the controller mirror and calls `restartComponent(kParamValuesChanged)`, with
  no `performEdit`. Edit Target is not `kCanAutomate` and is persisted. The processor is authoritative for both endpoints
  and the compare mode; the controller mirror is replaced only from its `kMsgEndpoint` / `kMsgCompareMode` messages
  (sent off the audio thread: a 50 ms SDK timer on Windows / macOS, none on Linux, plus the `kMsgRequestSync` pull on an
  Edit Target switch). Copy A→B / B→A and Compare (A, A·B, B) are controller operations (`copyEndpoint`, `setCompare`)
  with no UI until Phase 12; Compare is never persisted and resets to A·B on `setState` / `setActive(false)`.
  The seed is persisted and **not registered**. Macro IDs obey `id - 100 == static_cast<int>(ProfundumMacro::X)`.
  Registered types and ranges are **frozen**: `kOutputGainId`, `kOutputCeilingId` and the four macros are
  plain `Steinberg::Vst::Parameter`s, `kVoiceModeId` ("Mono", "Poly") and `kPolyphonyId` ("1".."8") are
  `StringListParameter`s — never swap a type or change a range at the same ID. Titles are ASCII:
  "Weight", "Dark/Bright", "Tight/Loose", "Clean/Violent".
  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **State:** `kCurrentStateVersion = 2`, `kStateV2Bytes = 510` (and the v1 constants `kStateV1Version = 1`,
  `kStateV1Bytes = 36`) live in `plugin_ids.h`, shared by processor and controller with no cross-include; every offset is
  `static_assert`ed. Little-endian `IBStreamer`:

  | Bytes | Block | Content |
  |---|---|---|
  | 0–3 | version | `int32` = 2 |
  | 4–19 | global (v1) | float outputGain, int32 voiceMode, int32 polyphony, float outputCeilingDb |
  | 20–35 | macros (v1) | four floats in `ProfundumMacro` order |
  | 36–67 | note behaviour | priority i32, legato i32, portamento ms f32, portamento mode i32, glide mode i32, Note Memory i32, retrigger i32, bend range f32 |
  | 68–83 | morph control | position f32, ramp ms f32, Anchor Morph Mode i32, Edit Target i32 |
  | 84–87 | seed | uint32 |
  | 88–288 | endpoint A | `serializeMorphState`, 201 bytes |
  | 289–489 | endpoint B | 201 bytes |
  | 490–509 | morph runtime | `serializeMorphRuntime`, 20 bytes |

  The hidden performance parameters (4–7) are never written or read. Version `> 2` → `kResultFalse`, nothing changed.
  A v1 (36-byte) stream loads the globals and macros and resets both endpoints, the note and morph-control fields and the
  seed to their defaults. A truncated stream keeps the prior value of every field it does not hold completely (an endpoint
  needs all 201 bytes). `setState` makes no engine call: the next `process()` applies the endpoints, then the morph-control
  fields, then the runtime blob only if its target, ramp and mode match. The next format change appends after byte 510.
- **Tests:** `profundum_tests` (namespace `ProfundumPluginTest`; every case carries `[profundum]`). The timed CPU
  cases `Profundum_ProcessorCpu` and `Profundum_ProcessorCpuFullSurface` are hidden behind `[.perf]`; run them only via
  `node tools/run-cpu-tests.js profundum_tests`, alone. `Profundum_IdleTailProbe` (`[.][probe]`) is hidden and prints only.
  Phase 11 cases, by TU and tag:

  | TU | Cases | Tags |
  |---|---|---|
  | `unit/param_table_test.cpp` | `Profundum_SoundParamTableRouting`, `Profundum_ParamTableConformance`, `Profundum_ParamDisplayFormats`, `Profundum_PortamentoLabelFollowsGlideMode` | `[params]` |
  | `unit/state_v2_test.cpp` | `Profundum_StateVersions`, `Profundum_StateRoundTripV2`; `Profundum_RestoreHandOffNoTear` | `[state]`; `[state][rt]` |
  | `unit/midi_mapping_test.cpp` | `Profundum_MidiMapping`, `Profundum_MassPerformanceOffset` | `[midi]` |
  | `unit/phase11_alloc_test.cpp` | `Profundum_Phase11PathsDoNotAllocate` | `[rt]` |
  | `integration/param_wiring_test.cpp` | `Profundum_DefaultsMatchEngine`, `Profundum_EveryParamReachesEngine`, `Profundum_EditTargetAddressing` | `[params]` |
  | `integration/copy_compare_test.cpp` | `Profundum_CopyEndpoint`, `Profundum_CompareDoesNotWriteState`, `Profundum_ProcessorControllerSync` | `[morph]` |
  | `integration/tail_test.cpp` | `Profundum_TailFormula`; `Profundum_TailCoversRelease` | `[tail]`; `[tail][long]` |
  | `integration/state_restore_render_test.cpp` | `Profundum_StateRestoreRender` | `[state][long]` |
  | `integration/param_path_audibility_test.cpp` | `Profundum_ParamPathAudibility`, `Profundum_MassOffsetAudibility` | `[audibility][long]` |
  | `integration/mono_sum_params_test.cpp` | `Profundum_MonoSumViaParams` | `[mono][long]` |
  | `integration/determinism_test.cpp` | `Profundum_Determinism` | `[determinism]` |
  | `integration/band_level_metric_test.cpp` | `Profundum_BandLevelMetricSelfTest` | `[metric]` |

  The five `[long]` cases (`Profundum_TailCoversRelease`, `Profundum_StateRestoreRender`, `Profundum_ParamPathAudibility`,
  `Profundum_MassOffsetAudibility`, `Profundum_MonoSumViaParams`) are excluded from per-push CI and run nightly.
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
5. **`IMidiMapping` arrived in Phase 11**, while Profundum was unreleased at `0.1.0`, so adding the interface to the
   controller FUID cost nothing in host caches. `getMidiControllerAssignment` answers bus 0 only, on every channel:

   | MIDI controller | Parameter |
   |---|---|
   | `kCtrlModWheel` (CC 1) | Mod Wheel (6) |
   | `kCtrlSustainOnOff` (CC 64) | Sustain Pedal (5) |
   | `kAfterTouch` (channel pressure) | Channel Pressure (7) |
   | `kPitchBend` | Pitch Bend (4) |
   | anything else, or bus ≠ 0 | `kResultFalse` |

   Mod Wheel and Channel Pressure are hidden, non-automatable and non-persisted; their sum (clamped to 1) offsets the
   assembled Mass of both endpoints and never writes the persisted Mass leaf. Pitch Bend depth is Pitch Bend Range (417).
   Sustain uses the shared `Krate::Plugins::SustainLatch` (`plugins/shared/src/midi/sustain_latch.h`).
6. Mass (104) is an E-parameter (a morphed leaf); Morph Position (105) is a morph-control parameter, not morphed.
7. The plugin test namespace is `ProfundumPluginTest`, never `ProfundumTest`, which the dsp test helpers already use.
8. **The supported host sample-rate range is [22050, 192000].** Outside it `setupProcessing` returns `kResultFalse` and the plugin renders silence (FR-023).
9. **The four Personality macros are live**; 0.5 is the identity. **Host tempo is forwarded** when `kTempoValid` is set; otherwise the engine's 120 BPM stands.
