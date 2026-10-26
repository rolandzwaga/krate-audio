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
  (`makeProfundumPresetConfig()` / `makeProfundumUpdateConfig()`; the controller owns the live
  `UpdateChecker`: the banner over the editor, the manual button on Advanced → SETTINGS); `ui/` holds the Phase 12 editor classes (see **UI** below).
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
  Edit Target switch). Copy A→B / B→A and Compare (A, A·B, B) are controller operations (`copyEndpoint`, `setCompare`),
  driven from the editor's morph component since Phase 12; Compare is never persisted and resets to A·B on `setState` / `setActive(false)`.
  The seed is persisted and **not registered**. Macro IDs obey `id - 100 == static_cast<int>(ProfundumMacro::X)`.
  Registered types and ranges are **frozen**: `kOutputGainId`, `kOutputCeilingId` and the four macros are
  plain `Steinberg::Vst::Parameter`s, `kVoiceModeId` ("Mono", "Poly") and `kPolyphonyId` ("1".."8") are
  `StringListParameter`s — never swap a type or change a range at the same ID. Titles are ASCII:
  "Weight", "Dark/Bright", "Tight/Loose", "Clean/Violent".
  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
- **State:** `kCurrentStateVersion = 3`, `kStateV3Bytes = 574` (and `kStateV2Version = 2` / `kStateV2Bytes = 510`,
  `kStateV1Version = 1` / `kStateV1Bytes = 36`) live in `plugin_ids.h`, shared by processor and controller with no
  cross-include; every offset is `static_assert`ed. Little-endian `IBStreamer`:

  | Bytes | Block | Content |
  |---|---|---|
  | 0–3 | version | `int32` = 3 |
  | 4–19 | global (v1) | float outputGain, int32 voiceMode, int32 polyphony, float outputCeilingDb |
  | 20–35 | macros (v1) | four floats in `ProfundumMacro` order |
  | 36–67 | note behaviour | priority i32, legato i32, portamento ms f32, portamento mode i32, glide mode i32, Note Memory i32, retrigger i32, bend range f32 |
  | 68–83 | morph control | position f32, ramp ms f32, Anchor Morph Mode i32, Edit Target i32 |
  | 84–87 | seed | uint32 |
  | 88–288 | endpoint A | `serializeMorphState`, 201 bytes |
  | 289–489 | endpoint B | 201 bytes |
  | 490–509 | morph runtime | `serializeMorphRuntime`, 20 bytes |
  | 510–541 | name A (v3) | u8 length L (0–31), then L UTF-8 bytes, zero-padded to 31 (`kEndpointNameBlockBytes = 32`) |
  | 542–573 | name B (v3) | same layout |

  Bytes 0–509 of a v3 stream are the v2 body with version word 3. The hidden performance parameters (4–7) are never
  written or read. Version `> 3` → `kResultFalse`, nothing changed. A v1 (36-byte) stream loads the globals and macros
  and resets both endpoints, the note and morph-control fields, the seed and both names to their defaults. A v2 (510-byte)
  stream loads everything but the names, which reset to the defaults `A` and `B`. A truncated stream keeps the prior
  value of every field it does not hold completely (an endpoint needs all 201 bytes; a short or invalid name block keeps
  that endpoint's prior name and stops the chain). An empty name is the default name. `setState` makes no engine call:
  the next `process()` applies the endpoints, then the morph-control fields, then the runtime blob only if its target,
  ramp and mode match. `process()` never touches the names; the processor holds them under `namesMutex_`, taken only by
  `notify`, `getState` and `setState`. A name edit in the editor sends **`kMsgEndpointName`**
  (`"ProfundumEndpointName"`, ctl → proc, int `"endpoint"`, binary `"name"` of 0–31 bytes) and sets the project dirty.
  The endpoint descriptor shown beside a name is computed from the endpoint and is never stored. The next format change
  appends after byte 574.
- **UI (Phase 12, `specs/profundum-phase12-ui/`; design docs D1–D7 in its `design/`):**
  - **Pages:** one fixed 960 × 600 editor (`minSize = maxSize`) with a **Main** page and an **Advanced** page (session tag
    9100); Advanced has three tabs, Sections / Modulation / Settings (session tag 9110). `resources/editor.uidesc` is the
    whole layout; change it with XSLT.
  - **Sub-controller:** `createSubController("ProfundumPanel")` builds `UI::ProfundumPanelSubController`
    (`src/ui/panel_sub_controller.{h,cpp}`): page and tab switching, the morph component (Compare A / A·B / B, Copy
    A→B / B→A, the endpoint-name fields, the `Held at A` lock dimming) and the macro highlight.
  - **Custom views:** `createCustomView` makes `HarmonicBarView` (attribute `bar-variant` main / core; the live
    harmonic spectrum) and `SpreadCurveView` (the stereo-field spread curve); `OffsetRingKnob` is a registered view
    class (`src/ui/offset_ring_knob.{h,cpp}`) that draws a macro's offset as a one-sided ring from the knob's value.
    Macro offsets act on the resolved state and **never move the knobs**. `src/ui/macro_ui_map.h` maps the 16 macro
    rows to registered IDs; `src/ui/endpoint_descriptor.h` is the descriptor table.
  - **Spectrum frame:** the processor publishes at most one 288-byte `ProfundumSpectrumFrame`
    (`src/processor/spectrum_frame.h`, produced by `spectrum_producer.h`) per `process()` through a one-way
    DataExchange queue with user context **`kProfundumFrameUserContextId = 0x50524644`** (`'PRFD'`); the controller
    (`IDataExchangeReceiver`) caches the newest frame on the UI thread with or without an open editor. **The spectrum
    is empty in a host with neither DataExchange nor a run loop** (for example a bare test harness); this is documented,
    not worked around.
  - **Sync timer:** while an editor is open, a VSTGUI `CVSTGUITimer` with period `kEditorSyncIntervalMs = 100` refreshes
    the views; it skips a tick while a gesture is open and is stopped in `willClose` / `terminate`. It is separate from
    the processor's 50 ms SDK timer.
  - **Zoom:** steps 100 / 125 / 150 / 200 % (`Controller::kZoomSteps`), chosen in the header and in the SETTINGS tab
    (session tag 9300) or the host's zoom menu, applied with `VST3Editor::setZoomFactor`. Zoom, page, tab and highlight
    are **session state**: never persisted, never a parameter.
  - `IMidiLearn` is **not** implemented; MIDI learn is the host's.
- **Presets (Phase 13, `specs/profundum-phase13-presets-release/`):** 53 factory presets in six categories, `Sub`,
  `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated` (`makeProfundumPresetConfig()` in
  `src/preset/profundum_preset_config.h`; the list is additive-only and must match the directories under
  `resources/presets/`). The tree is **generated, never hand-edited**: definitions in `tools/profundum_preset_defs.h`,
  generator `tools/profundum_preset_generator.cpp` (target `profundum_preset_generator`; no Catch2, no `vstgui_support`),
  regenerated by the custom target `generate_profundum_presets`. Generator and tests share one state drive,
  `ProfundumPluginTest::PresetHost` (`tests/profundum_preset_host.h`). Byte determinism and idempotence:
  `node tools/check-preset-generator-determinism.js --plugin profundum`.
- **Browser:** the header `PresetBrowserButton` custom view is one `OutlineBrowserButton` ("PRESETS") that opens the
  shared `PresetBrowserView` overlay (added in `didOpen`, removed in `willClose`; tabs "All" + the six categories). No
  current-preset name, no previous / next, no new state. A preset load does **not** go through per-parameter host edits:
  the controller sends the whole component stream as **`kMsgLoadPresetState`** (`"ProfundumLoadPresetState"`,
  ctl → proc, binary `"state"` of at most 4096 bytes, `plugin_ids.h`), `Processor::notify` applies it through the
  `setState` body, and the controller then mirrors the same bytes and calls `restartComponent(kParamValuesChanged)`. That
  is how the seed, the morph runtime block and the endpoint names reach the processor.
  `Processor::setEngineSeamsForTest(const ProfundumEngineSeams&)` (P-11) is a test-only hook: the engine observer seams
  (`dsp/include/krate/dsp/systems/profundum_engine_observer.h`) applied at the next `setupProcessing`; default all off.
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

  Phase 12 cases (every `profundum_tests` case of the phase also carries `[ui]`), one line each:

  - `unit/state_v3_test.cpp` — `Profundum_EndpointNames` (`[morph][state]`): name sanitising, block round trip, descriptor table, version 3, one `kMsgEndpointName` per commit, v1 / v2 streams load.
  - `unit/spectrum_producer_test.cpp` — `Profundum_SpectrumProducerMath` (`[spectrum]`): the per-partial power arithmetic.
  - `unit/controller/custom_view_laws_test.cpp` — `Profundum_UiCustomViewLaws`: the drawing laws of the custom views.
  - `unit/controller/editor_layout_test.cpp` — `Profundum_UidescBindsEverySurfaceId`, `Profundum_UidescTagIntegrity`, `Profundum_UiClassRule`, `Profundum_UiLabels`, `Profundum_Editor_PageSwitch`, `Profundum_UiLayout`, `Profundum_UiPlacement`, `Profundum_UiZoom`: ID binding, tags, view classes, labels, page switching, layout and accessibility at every zoom step, placement, zoom.
  - `unit/controller/editor_lifecycle_test.cpp` — `Profundum_EditorLifecycle` (`[controller][lifecycle]`): open / close cycles of the real editor.
  - `unit/controller/editor_sync_test.cpp` — `Profundum_EditorSyncTimer` (`[lifecycle]`): the 100 ms timer exists only while an editor is open.
  - `unit/controller/macro_ui_test.cpp` — `Profundum_MacroUiMap`, `Profundum_MacroModulationRing`, `Profundum_MacroHighlight`, `Profundum_MidiLearnHostLevel`: macro row → ID map, offset rings, highlight sets, host-level MIDI learn. `Profundum_UiContractTables` (`[.][probe]`, hidden) prints the D3 / D4 CSVs that `specs/profundum-phase12-ui/evidence/close/regen-tables.sh` pastes into `design/macro-mapping.md` and `design/morph-laws.md`.
  - `unit/controller/morph_component_test.cpp` — `Profundum_MorphComponentUi`, `Profundum_MorphLockDimming` (`[morph]`), `Profundum_UiPortamentoUnitFollowsGlideMode`: compare / copy / names, the `Held at A` dimming, the portamento unit label.
  - `unit/controller/spread_curve_test.cpp` — `Profundum_SpreadCurveMatchesField`: the drawn curve follows the field.
  - `unit/controller/ui_views_test.cpp` — `Profundum_UiViewsUnit`: the custom views in isolation.
  - `integration/spectrum_frame_test.cpp` — `Profundum_SpectrumFrameMatchesSignal`, `…DoesNotChangeAudio`, `…AllocationFree` (`[rt]`), `…FocusVoice`, `…Sanitise`, `…HandlerLifecycle` (`[lifecycle]`), `…Cadence` (the others `[spectrum]`): frame against the rendered signal, audio unchanged, no allocation, focus voice, sanitising, queue lifecycle, cadence.
  - `integration/main_page_sanity_test.cpp` — `Profundum_MainPageSanity` (`[render]`): each of the 17 Main-page controls renders finite audio above the RMS bar and at or under the ceiling.
  - `integration/main_page_render_set_test.cpp` — `Profundum_MainPageRenderSet` (`[render][.listen]`, hidden): writes the listening WAVs to `specs/profundum-phase12-ui/evidence/listen/` (git-ignored).
  - `dsp/tests/unit/systems/profundum_engine_display_test.cpp` (in `dsp_systems_tests`) — `Profundum_EngineDisplayAccessorNeutral` (`[profundum][systems][display]`): the engine's display accessors do not change the render.

  Phase 13 cases, by TU (shared helpers `tests/preset_test_support.h`, `tests/preset_render_support.h`,
  `tests/profundum_preset_host.h`):

  | TU | Cases | Tags |
  |---|---|---|
  | `unit/preset/preset_library_test.cpp` | `Profundum_PresetCategoryConfig`, `…CategoryNameRule`, `…HostDrive`, `…HostValidation`, `Profundum_GeneratorSmoke`, `Profundum_PresetDefsRules`, `Profundum_PresetCategoriesMatchFilesystem`, `Profundum_FactoryPresetContainer`, `…LibraryShape`, `…PresetStateFormat`, `…PresetRoundTrip`, `Profundum_CoverageMatrixComplete`, `Profundum_CommittedPresetTreeMatches`, `Profundum_FactoryPresetCeiling` | `[preset]` |
  | `unit/manual_coverage_test.cpp` | `Profundum_ManualCoversSurface` | `[docs]` |
  | `integration/preset_browser_test.cpp` | `Profundum_LoadPresetStateMessage`, `Profundum_ControllerLoadPresetStream`, `Profundum_PresetBrowserSave`, `…ViewLifecycle`, `…Load`, `Profundum_FactoryPresetScan`; `Profundum_InstalledPresetTree` | `[browser]`; `[browser][.install]` (hidden) |
  | `integration/preset_rt_test.cpp` | `Profundum_FactoryPresetLoadRtSafe`, `Profundum_PresetRenderGuard_0of16` .. `_15of16`, `Profundum_PresetLoadUnderHeldNote` | `[preset][rt]` |
  | `integration/preset_probe_test.cpp` | `Profundum_EngineSeamsForTest`, `Profundum_ScriptRenderShim`, `Profundum_ProcessorScriptRender`, `Profundum_ScriptRenderRefusals`, `Profundum_EffectiveScriptCarriers`, `Profundum_PresetAudibilityProbeKeys`; eleven `Profundum_PresetAudibilityProbe_<Group>` | `[probe]`; `[probe][.probe]` (hidden) |
  | `integration/preset_showcase_test.cpp` | `Profundum_ShowcaseArmMechanics`, `Profundum_ReferenceColumns`; `Profundum_PresetDistanceMath`; `Profundum_PresetShowcase_<k>of8` (k = 0…7) | `[showcase]`; `[distinct]`; `[showcase][long][profundum-showcase]` |
  | `integration/preset_sweep_test.cpp` | `Profundum_SweepTimeline`, `…MonoSumMechanics`, `…DeterminismMechanics`; `Profundum_PresetDistinctnessParams`; `Profundum_PresetSweep`, `…SweepRates`, `…TruePeakMaxMass`, `…MonoSum`, `…Determinism`, `…LoudnessMatch`, `…Distinctness`; `…DistinctnessPilot`; `Profundum_PresetAuditionRenders` | `[sweep]`; `[distinct]`; `[long]`; `[.pilot]`; `[.listen]` (hidden) |
  | `integration/processor_cpu_test.cpp` | `Profundum_PresetCpu` (beside the two Phase 10–11 CPU cases) | `[.perf][performance]` |

  Phase 13 adds 15 `[long]` cases (the eight showcase shards and the seven `[long]` sweep / distinctness cases); CI
  splits the nightly lane into four Catch2 shards (`tools/ci-test-shard.js` `kSuiteShards`).
  The five `[long]` cases (`Profundum_TailCoversRelease`, `Profundum_StateRestoreRender`, `Profundum_ParamPathAudibility`,
  `Profundum_MassOffsetAudibility`, `Profundum_MonoSumViaParams`) are excluded from per-push CI and run nightly.
  ```bash
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Profundum profundum_tests
  node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' profundum_tests   # sharded, see root CLAUDE.md; CPU lane alone: node tools/run-cpu-tests.js profundum_tests
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
