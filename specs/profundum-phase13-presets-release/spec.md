# Feature Specification: Profundum Phase 13 — Factory Presets & Release Readiness

**Spec slug:** `profundum-phase13-presets-release`
**Roadmap:** `specs/Profundum-roadmap.md`, Part B, **Phase 13** (lines 1052–1069), the **Phase 13 handoff from Phase 12**
(lines 1043–1050), and the cross-cutting constraints (lines 1089–1112). Concept: `specs/Profundum-concept.md` (§9 line 458,
"presets could store these coordinates"; §10 lines 464–500, the A/B morph).
**Deferred into this phase by earlier specs (read this session):**
- `specs/profundum-phase10-plugin-scaffold/spec.md:75` hands over "the fixed preset category set, factory presets, the preset
  sweep, the release gate, `MANUAL.md` and `docs/index.html`". `:227` and `:987` (Q4) make `Sub` the seed category: it is
  permanent, and "Phase 13 keeps it and may only add categories".
- `specs/profundum-phase11-parameters/spec.md:390`: "Phase 13 presets may carry a seed through state".
- `specs/profundum-phase12-ui/spec.md:117` (P-9) leaves the header without a preset field in Phase 12: "Phase 13 adds the
  browser". `:246`: Phase 13 owns "the endpoint names / descriptors of factory presets (this phase provides only the field and
  its editor), release readiness". `:565`: "Phase 13 factory presets supply names only".
- `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:206-212` (12.4: a Bipolar ENV rests at −1) and `:214-253`
  (13.1: the Anchor-through-chain ρ / θ grid "for the Phase 13 audition"). `specs/profundum-phase9-voice-engine/spec.md:969`
  (SC-021, G14-13 (3)): "a Phase 13 preset note (Rectify at Tone 1 with Fold 100 in WIDE below 48 kHz)".
**Depends on:** Phases 1–12. All are ✅ in the roadmap status blocks; Phase 12 closed 2026-10-24 (roadmap line 1029). Branch
`synth/profundum`, HEAD `e6095b24` when this spec was written.
**Status:** DRAFT, clarified 2026-10-24 (Q1-Q7 decided; see Clarifications). Specification only.
**Date:** 2026-10-24

---

## Overview

Profundum ships today with **no factory presets** and **no way to open the preset browser**:

- `plugins/profundum/resources/presets/` holds only `Sub/.gitkeep` (`find plugins/profundum/resources/presets` lists one
  file). `makeProfundumPresetConfig()` declares exactly one subcategory, `{"Sub"}`
  (`plugins/profundum/src/preset/profundum_preset_config.h:19-25`), and the file's banner says the list is additive-only (`:6-12`).
- The controller builds a `PresetManager` with `nullptr` as the processor (`plugins/profundum/src/controller/controller.cpp:101-102`)
  and sets neither a state provider nor a load provider. `editor.uidesc` has no preset control: its only "preset" strings are the
  two endpoint-name tooltips (`plugins/profundum/resources/editor.uidesc:173-174`). Phase 12's D1 header has "no preset field in
  12b" (`specs/profundum-phase12-ui/design/main-page.md:83`).
- **A Profundum release run fails today at its generator step.** `release.yml` lists `profundum` (`.github/workflows/release.yml:43`).
  It maps every plugin except Iterum to `${PLUGIN}_preset_generator` (`:155-171`; the `*` case is at `:167-170`), builds that
  target (`:173-174`), runs `./build/bin/${binary} generated-presets` (`:176-179`) and uploads the result with
  `if-no-files-found: error` (`:181-186`). The root `CMakeLists.txt` defines a generator for every other plugin (Vorago's block is
  `:679-743`) and **none for Profundum** (`grep -n profundum_preset CMakeLists.txt` finds nothing). The generator's target name,
  its `build/bin` output and its `argv[1]` contract are therefore fixed constraints, as they were for Vorago
  (`CMakeLists.txt:681-685`).

The plumbing around the empty library exists and was read this session:

- The Windows POST_BUILD copy is called (`plugins/profundum/CMakeLists.txt:128`, defined at `cmake/KratePlugin.cmake:287`).
- The Inno installer ships `presets\*` to `{commonappdata}\Krate Audio\Profundum`
  (`plugins/profundum/installers/windows/setup.iss:66-68`).
- The Linux README documents both manual destinations (`plugins/profundum/installers/linux/README.txt:29-44`).
- `release-readiness.js` already maps Profundum (`.claude/workflows/release-readiness.js:23`, Phase 10 user ruling Q-B).

This phase delivers six things: the **factory library**, the **tool** that generates it, the **harness** that proves it, the
**browser** that opens it, the **documentation** Phase 10 deferred, and the **release gate**. The governing requirement is the
roadmap's variety rule (lines 1057–1059). The spec carries a preset × capability coverage matrix as an FR, the preset list is
derived from that matrix, and the harness measures pairwise distinctness alongside boundedness.

**Every capability is shown off by at least one preset** (lines 1059–1066):

- The matrix's columns are the rows of Phase 9's audibility table, and no column may be empty.
- For each column, at least one preset is built around that capability.
- The harness measures, on that preset, that the capability's descriptor contrast clears the Phase 9 bar. The descriptor and the
  bar are the same as in the Phase 9 row.
- **The audibility probe runs before the preset list is written** (lines 1066–1067).

Four findings shape the design:

1. **The Phase 9 descriptors cannot be included.** They live in the anonymous namespace of
   `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`, which opens at `:90`. Phase 11 ported fifteen rows to the
   processor path, verbatim: AT-01, 02, 03, 05, 10, 11, 21, 23, 25, 27, 28, 33, 38, 42 and 43
   (`plugins/profundum/tests/param_path_audibility_helpers.h:1-42`). Phase 12 added main-page rows from the AT-02, 10, 14, 17, 26,
   41, 42, 47 and 48 families in `integration/main_page_audibility_test.cpp`. **More than thirty Phase 9 rows have no
   processor-path reader today** (C-3).
2. **The Profundum test fixture cannot be reused by a tool.** `plugins/profundum/tests/profundum_test_fixture.h:57` includes
   `<catch2/catch_test_macros.hpp>` and calls `REQUIRE` (`:482-483`, `:504`, `:513-515`). The generator must not link Catch2, so it
   needs a host drive without Catch2 that the tests share. Vorago has the precedent, `PresetHost`
   (`plugins/vorago/tests/vorago_preset_host.h:1-19`).
3. **The Profundum processor carries a Highway kernel.** `processor.h:33-34` includes `spectrum_producer.h`, which includes
   `goertzel_bank_simd.h` (`plugins/profundum/src/processor/spectrum_producer.h:26`). The plugin links `hwy` for it
   (`plugins/profundum/CMakeLists.txt`, comment "T040: the plugin-local Goertzel kernel"). The Vorago generator compiles
   `processor.cpp` into the tool (`CMakeLists.txt:686-697`). A Profundum generator built the same way must also compile
   `goertzel_bank_simd.cpp` and link `hwy`, so the Vorago block cannot be copied unchanged.
4. **This is a bass, not a drone.** Envelope times are capped at `kMaxEnvelopeTimeMs = 10000.0f`
   (`dsp/include/krate/dsp/primitives/envelope_utils.h:52`) and the morph ramp at `kMaxPositionRampMs = 2000.0f`
   (`dsp/include/krate/dsp/systems/bass_morph_engine.h:68`). The roadmap asks for a **seconds-scale** sweep (line 1068). The
   timeline is therefore fixed, as in Seraphis (`specs/seraphis-phase12-presets-release/spec.md:277-298`), not derived from each
   envelope over minutes as in Vorago.

---

## Scope

In scope:

- The **fixed category set** (C-1), in `makeProfundumPresetConfig()` and on disk, with `Sub` kept unchanged and first.
- The **preset × capability coverage matrix** (C-2) and the **factory library** derived from it: `.vstpreset` files with their
  `Info` metadata and both endpoint names.
- The **audibility probe** on the processor path, run and recorded before any preset definition is written (C-3), preceded by the pure code move of the Phase 9 readers into a shared test header (FR-057).
- The **generator**: `tools/profundum_preset_generator.cpp`, its data-only definitions header `tools/profundum_preset_defs.h`, and
  the CMake targets `profundum_preset_generator` and `generate_profundum_presets`, on the contract `release.yml` already assumes.
- The **preset browser control** in the header row of the ratified D1 layout (roadmap handoff 13.1, line 1044).
- The **validation harness** in `profundum_tests`:
  - container and `Info` checks, round trip, metadata against the filesystem, browser scan;
  - semantic equality of the committed tree, RT-safe loading;
  - the seconds-scale **NoteOn render sweep**, the **per-preset mono-sum check**, the **true-peak check at maximum Mass**;
  - the **showcase arms**, the **distinctness arms**, and determinism.
- The **audition render set** and its listening sheet (roadmap handoff 13.4, line 1048; Phase 9 handoff 13.1).
- `plugins/profundum/MANUAL.md` and `plugins/profundum/docs/index.html`, deferred by Phase 10 (`spec.md:75`).
- **Release-roster registration** where Profundum is still missing (`.claude/skills/release/SKILL.md:15-16` and `:22-31`;
  `tools/check-preset-generator-determinism.js:54-57`), and the **release gate** through the `release-readiness` flow (line 1069).

## Non-goals (owned elsewhere or out of scope)

- **Any change under `dsp/include/`.** No DSP class is created or edited. The single exemption under `dsp/` is the Q4 pure code move of the Phase 9 audibility readers inside `dsp/tests/unit/systems/` (FR-057); it touches no `dsp/include/` file. Variety comes from authoring the shipped surface. If a
  capability cannot be showcased at its Phase 9 bar from that surface, the build **stops and surfaces it** (FR-023); that is not
  licence to edit `dsp/`. This is the Vorago 13b–13d lesson the roadmap cites at line 1067.
- **New registered parameters, parameter-type or range changes, or a state version bump.** The 84 registered IDs,
  `kCurrentStateVersion = 3` and `kStateV3Bytes = 574` (`plugins/profundum/src/plugin_ids.h:23`, `:61-63`) are used as they are.
- **New UI beyond the header preset control and the browser overlay.** The D1–D7 layout is ratified (roadmap line 1029).
- **Reading the `Info` chunk back at runtime, except the `Comment` attribute (FR-065, user ruling 2026-10-24).** `PresetManager`
  takes the subcategory from the parent directory (`plugins/shared/src/preset/preset_manager.cpp:95-103`), and the name and category
  keep coming from the filename and folder; only the description is read back.
- **Cutting the release.** A version bump, a tag and a push are the user's decisions (the `release` skill; the project memory rule
  "NEVER push without permission"). Phase 13 adds bullets to the open `[0.1.0] - Unreleased` entry
  (`plugins/profundum/CHANGELOG.md:8`) and proves the release gate green. It does not edit `version.json`.
- **macOS and Linux factory-preset installers.** `krate_plugin_install_presets` is a Windows POST_BUILD copy
  (`cmake/KratePlugin.cmake:276-287`). Linux keeps the documented manual copy.

---

## Existing components (verified this session)

| Component | Path | What is reused (real signature / fact) |
|---|---|---|
| Profundum processor state I/O | `plugins/profundum/src/processor/processor.h:91-92` | `tresult PLUGIN_API setState(IBStream*)`, `tresult PLUGIN_API getState(IBStream*)`. This is the **only** serializer the generator uses (C-4) |
| Tail and latency | `processor.h:90`, `:93-94`; `processor.cpp:570-575` | `getLatencySamples()` returns a constant 64. `getTailSamples()` = `ceil((R_eff + 1250) * fs / 1000) + 64` (Phase 11 FR-060); it sets the sweep's tail span (C-6) |
| Test seams | `processor.h:108`, `:117` | `const Krate::DSP::ProfundumEngine* engineForTest() const noexcept`; `void setEngineObserverForTest(Krate::DSP::ProfundumEngineObserver*) noexcept` |
| State constants | `plugins/profundum/src/plugin_ids.h:23`, `:40`, `:59-63`, `:93` | `kCurrentStateVersion = 3`, `kStateV2Bytes = 510`, `kEndpointNameBlockBytes = 32`, `kStateV3Bytes = 574`, `kProcessorUID` |
| Parameter ID map | `plugin_ids.h:116-219` | The 84 IDs. Examples: `kMassId = 104`, `kMorphPositionId = 105`, `kAnchorWaveformId = 300`, `kAnchorModeId` (301), `kNoteMemoryId` (415), `kCharacterId = 600`, `kFieldImageModeId = 800`, `kMotionSourceId = 900`, `kMotionTargetId` (901), `kMotionPolarityId` (911), `kMorphAnchorModeId = 1000`, `kMorphEditTargetId = 1001` |
| Endpoint names | `plugins/profundum/src/parameters/endpoint_names.h:27-28`, `:32`, `:80`, `:94` | `kEndpointNameBytes = 31`; `struct EndpointName`; `EndpointName defaultEndpointName(BassMorphEngine::Endpoint)`; `EndpointName sanitizeEndpointName(std::string_view)` |
| Endpoint descriptor (derived, never stored) | `plugins/profundum/src/ui/endpoint_descriptor.h:46-48`, `:67` | `std::array<const char*, 3> describeEndpoint(const Krate::DSP::MorphState&)`, built from `kShapeWords`, `kColourWords` and `kForceWords` |
| Registered display strings | `parameters/character_params.h:25-27`; `anchor_params.h:25`, `:28-29`; `field_params.h:37-38`; `motion_params.h:37-40`, `:45` | Character: Tube, Wavefold, Rectify, Bite, Crush, Fracture. Anchor waveform: Sine, Triangle, Rounded Sine, Asymmetric Sine, Octave Down. Anchor mode: Lock, Follow, Punch, Ghost. Image mode: Solid, Spread, Wide. Motion source: Off, Env, Cycle, Drift. Motion target: Mass, Depth, Body, Edge, Shift. Polarity: Bipolar, Unipolar |
| `MorphState` | `dsp/include/krate/dsp/systems/morph_state.h:43`, `:81-84`, `:117` | `struct MorphState`; `MotionConfig::Source { Off, Env, Cycle, Drift }`, `Target { Mass, Depth, Body, Edge, Shift }`, `Polarity { Bipolar, Unipolar }`; `float mass = MassLaw::kNeutralMass; // [0, 100]` |
| Enumerations behind the matrix columns | `systems/bass_stereo_field.h:64`; `systems/anchor_oscillator.h:57-58`; `systems/bass_character_stage.h:78`; `processors/note_memory.h:24-29`; `systems/profundum_macro_matrix.h:38` | `ImageMode { Solid, Spread, Wide }`; `Waveform { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }`; `Mode { Lock, Follow, Punch, Ghost }`; `Algorithm { Tube, Fold, Rectify, Bite, Crush, Fracture }`; `NoteMemory::Mode { AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous }`; `ProfundumMacro { Weight, DarkBright, TightLoose, CleanViolent, Count }` |
| Engine constants | `systems/profundum_engine.h:99`, `:107`, `:110`, `:339` | `kMaxVoices = 8`; `kLimiterLookaheadSamples = 64`; `kDefaultCeilingDb = TruePeakLimiter::kDefaultCeilingDb` (−1 dBTP); `void setSeed(std::uint32_t) noexcept` |
| `makeProfundumPresetConfig()` | `plugins/profundum/src/preset/profundum_preset_config.h:19-25` | `PresetManagerConfig{kProcessorUID, "Profundum", "Synth", {"Sub"}}`, extended here (C-1) |
| `Krate::Plugins::PresetManagerConfig` | `plugins/shared/src/preset/preset_manager_config.h:19-24` | Field order: `processorUID, pluginName, pluginCategoryDesc, subcategoryNames` |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h:55-61`, `:70`, `:75`, `:85`, `:113`, `:120`, `:126`, `:129`; `.cpp:95-103`, `:264-276` | `explicit PresetManager(PresetManagerConfig, IComponent*, IEditController*, path userDirOverride = {}, path factoryDirOverride = {})`; `scanPresets()`; `getPresetsForSubcategory(const std::string&) const`; `loadPreset(const PresetInfo&)`; `getFactoryPresetDirectory() const`; `static bool isValidPresetName(const std::string&)`; `setStateProvider` / `setLoadProvider`. The subcategory is an exact match on the parent directory. The `Info` XML holds `MediaType`, `PlugInName`, `PlugInCategory`, `Name`, `MusicalCategory`, `MusicalInstrument` and an optional `Comment` |
| `PresetBrowserView` / `OutlineBrowserButton` | `plugins/shared/src/ui/preset_browser_view.h:53-66`; `plugins/shared/src/ui/outline_button.h:79-89` | `PresetBrowserView(const CRect&, PresetManager*, std::vector<std::string> tabLabels)`, `open()`; `OutlineBrowserButton(const CRect&, IControlListener*, int32_t tag, std::string title, CColor)` |
| Vorago browser wiring (pattern) | `plugins/vorago/src/controller/controller.cpp:113-118`, `:364-367`, `:388-391`, `:411-417`, `:433-437`; `plugins/vorago/src/preset/vorago_preset_config.h:40-48` | State and load providers on the manager; `createCustomView("PresetBrowserButton")`; the browser is added in `didOpen` and removed in `willClose`; `makeVoragoPresetTabLabels()` returns `"All"` followed by the subcategories |
| Vorago generator pattern | `CMakeLists.txt:679-743`; `plugins/vorago/tests/vorago_preset_host.h:1-19`, `:47`, `:198`; `tools/vorago_preset_defs.h:39`, `:46`, `:51`, `:54`, `:326` | `processor.cpp` is compiled into the tool, which links `KrateDSP KratePluginsShared sdk` and no Catch2, with `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`. `VoragoTest::PresetHost` (no Catch2) and `buildPresetComponentState(...)` are shared by the tool and the tests. Definitions: `Vorago::PresetDefs::{ParamSetting, CapabilityGroup, Capability, VoragoPresetDef}` |
| Generator determinism gate | `tools/check-preset-generator-determinism.js:54-58` | `GENERATORS = { seraphis: ..., vorago: ... }`, selected by `--plugin`. It **omits `profundum`**; it is extended, not copied (FR-028) |
| Processor-path audibility render | `plugins/profundum/tests/param_path_audibility_helpers.h:576`, `:645`; `integration/main_page_audibility_test.cpp:1-60` | `auPt::VoiceEngineTaps ppRenderProcessor(const PpArm&, bool twin)`; `ppRender(PpPath, const PpArm&, bool)`. The base endpoints load through `setState`, levers arrive as normalised parameter writes, and the descriptor is read on the Sum tap (roadmap handoff 13.3, line 1046) |
| Phase 9 descriptor helpers (public, can be included) | `dsp/tests/unit/systems/bass_morph_test_helpers.h:771`, `:808`, `:816`, `:829`; `profundum_voice_engine_test_helpers.h:139`, `:187`, `:377`, `:449`; `bass_morph_state_helpers.h:167` | `double dNlDb(const MorphChainTaps&, double f0, double fs)`; `dBrOct(const FieldChainTaps&, double, double)`; `lKDb(const FieldChainTaps&, double)`; `eSubDb(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs)`; `struct VoiceEngineScript`; `struct VoiceEngineTaps`; `voiceEngineRender(const VoiceEngineScript&)`; `voiceEngineRenderWithTwin(...)`; `showcasePair()` |
| Phase 9 audibility table (the matrix's columns and bars) | `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv` (175 lines, header at `:1`) | 50 rows, AT-01 to AT-50; AT-13 has two variants, Fracture and Rectify. Each row has `contrast`, `descriptor`, `tap`, `bar` and `gated`. 102 gated readings, 0 FAIL (`phase-handoffs.txt:180-204`) |
| Mono-sum criteria | `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:95`, `:97`, `:736`; `plugins/profundum/tests/integration/mono_sum_params_test.cpp:1-30`; `main_page_sanity_test.cpp:12-24` | `kSubBandQBarDb = 1.0`, `kSubBandRhoBar = 0.99`; `BandReading bandReading(const float* L, const float* R, std::size_t n, double fs, double fLo, double fHi)`. SOLID reads L == R bitwise after the mode crossfade + 64 samples; every voice reads `anchorL == anchorR` bitwise; the generator tap reads ρ ≥ 0.9989 |
| True peak | `tests/test_helpers/low_frequency_metrics.h:411-424` | `float measureTruePeakDb(const float* l, const float* r, std::size_t n, double sampleRate)`, 4× oversampled on the limiter's own basis. The Phase 9 SC-013 bar is "≤ the limiter ceiling + 0.1 dB" (`specs/profundum-phase9-voice-engine/spec.md:961`) |
| Band-level metric | `plugins/profundum/tests/band_level_metric.h:33-41`, `:167`, `:178` | `struct BandLevelReading` (mid and side × five bands, edges at 20 / 80 / 160 / 320 / 1280 / 20000 Hz); `bandLevels(L, R, fs)`; `bandLevelDifferenceDb(...)` |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h:58`, `:61`, `:63`, `:122-124` | `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `struct RenderFingerprint`, `compareFingerprints(...)`. No bit-exact goldens |
| Allocation instrument | `tests/test_helpers/allocation_detector.h:111` | `class AllocationScope` |
| Audition render template | `specs/profundum-phase12-ui/evidence/listen/render.sh` (+ `manifest.txt`) | Renders a patch list to WAV with a manifest (roadmap handoff 13.4) |
| Phase 9 audition grid | `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:214-253` | ρ / θ and dE_sub per Character algorithm × Anchor waveform × E0–E2. At Amount 1, Rectify, Crush and Fracture drop the summed fundamental by 6.0–7.8 dB while the Anchor's own w1 stays unchanged (`:229-231`) |
| Release rosters | `.github/workflows/release.yml:43`, `:155-186`; `.claude/workflows/release-readiness.js:14-24`; `.claude/skills/release/SKILL.md:15-16`, `:22-31` | The release choice, the generator contract and the readiness map include Profundum. **The release skill's input list and target table do not** |
| Installers | `plugins/profundum/installers/windows/setup.iss:66-68`; `plugins/profundum/installers/linux/README.txt:29-44`; `plugins/profundum/CMakeLists.txt:128` | `{commonappdata}\Krate Audio\Profundum`; manual copy on Linux; `krate_plugin_install_presets(${PLUGIN_NAME})` |
| Manual and site precedent | `plugins/vorago/MANUAL.md` (sections Introduction, Signal Flow, Interface Overview, Macros, then one per page); `plugins/vorago/docs/{index.html, manual-template.html, assets/}` | Structure copied. `plugins/profundum/docs/` is empty today |

## New components

**No DSP class and no plugin runtime class is created.** The roadmap names no new class for Phase 13, so this phase goes nowhere
near its ODR hazard names (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`,
`HarmonicCloud`, `SpectralState`, `MacroMapper`; roadmap lines 174–176). Every new name lives in tool or test code.

Namespace hazard: `Krate::DSP::ProfundumTest` is the namespace of the dsp test helpers (`param_path_audibility_helpers.h:88`).
Plugin test code stays in `ProfundumPluginTest` (`plugins/profundum/CLAUDE.md`, decision 7).

ODR sweep: `grep -rn -E "(class|struct|enum class|enum) <Name>\b" dsp/ plugins/ tools/ tests/`, run 2026-10-24 on HEAD `e6095b24`.

| Name | Kind / layer | Location | ODR sweep result |
|---|---|---|---|
| `Profundum::PresetDefs::ProfundumPresetDef` | struct, tool data (no DSP layer) | `tools/profundum_preset_defs.h` | **0 hits** |
| `Profundum::PresetDefs::ParamSetting` | struct `{ParamID, normalized value}` | `tools/profundum_preset_defs.h` | 2 hits: `tools/seraphis_preset_defs.h:59`, `tools/vorago_preset_defs.h:46`. Different namespaces, never linked into one target; no TU may `using namespace` two of them |
| `Profundum::PresetDefs::Capability` | `enum class`, the C-2 column roster | `tools/profundum_preset_defs.h` | 1 hit: `tools/vorago_preset_defs.h:54`. Different namespace and target |
| `Profundum::PresetDefs::CapabilityGroup` | `enum class` (only if the plan groups columns) | `tools/profundum_preset_defs.h` | 1 hit: `tools/vorago_preset_defs.h:51`. Different namespace and target |
| `ProfundumPluginTest::PresetHost` | class, host drive without Catch2, shared by tool and tests (finding 2) | `plugins/profundum/tests/profundum_preset_host.h` | 1 hit: `plugins/vorago/tests/vorago_preset_host.h:47` (`VoragoTest`). Different namespace and target |
| `ProfundumPluginTest::PresetDescriptor` | struct, the C-7 sound-space descriptor | `plugins/profundum/tests/preset_test_support.h` | 1 hit: `plugins/vorago/tests/preset_test_support.h:48`. Different namespace and target |
| `ProfundumPluginTest::DecodedPresetState` | struct, typed decode of a preset's 574-byte state | `plugins/profundum/tests/preset_test_support.h` | 2 hits: `plugins/seraphis/tests/preset_test_support.h:387`, `plugins/vorago/tests/preset_test_support.h:843`. Different namespaces and targets |
| `ProfundumPluginTest::SweepTimeline` | struct, the C-6 timeline | `plugins/profundum/tests/preset_test_support.h` | 2 hits: `plugins/seraphis/tests/preset_test_support.h:592`, `plugins/vorago/tests/preset_test_support.h:918`. Different namespaces and targets |
| `ProfundumPluginTest::ShowcaseArm` | struct, one C-5 showcase contrast | `plugins/profundum/tests/preset_test_support.h` | **0 hits** |
| `tools/profundum_preset_generator.cpp`; targets `profundum_preset_generator`, `generate_profundum_presets` | tool + CMake targets | root `CMakeLists.txt`, after the Vorago block | **0 hits** (`grep -n profundum_preset CMakeLists.txt`) |

The plan MUST re-run the sweep for any further name before it creates it.

---

## Decisions

### C-1: The category set is fixed and additive-only, and `Sub` stays first

The set is declared once in `makeProfundumPresetConfig()` and mirrored one-to-one by the directories under `resources/presets/`.
This is the Membrum lesson the roadmap cites (line 1055): the browser compares the parent directory name with `==`
(`preset_manager.cpp:95-103`). `Sub` stays unchanged and first (Phase 10 Q4). Every other name is a sound class along the
concept's continuum, "pure sub → resonant bass → harmonic bass → distorted" (roadmap lines 6–8), and never a capability name;
capabilities are the matrix's columns (C-2). The names are decided (Q1): `Sub`, `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated`, six categories, `Sub` first. Once shipped they are permanent (FR-001).

"Never a capability name" is made checkable (FR-001). A category name MUST NOT equal, case-insensitively:
- any registered parameter title or list display string of the 84 registered IDs (for example `Body`, `Wide`, `Drift`,
  `Cycle`, `Mass`; `plugins/profundum/src/parameters/*_params.h`, `field_params.h:37-39`);
- any C-2.1 group name (Core, Character, Anchor, Pitch, Envelope, Mass, Field, Motion, Morph, NoteMemory, Macros);
- the leading word of a registered title, which is how the sections are named (for example `Drop` from Drop Amount, `Excitation`,
  `Field`, `Motion`).

The concept's own continuum words (`Sub`, `Resonant`, `Harmonic`, `Distorted`; roadmap lines 6–8) are sound classes by
definition and are exempt from the third clause only. `Harmonic` is therefore allowed although it leads "Harmonic Fold".

### C-2: Variety governs, and the preset list is derived from a coverage matrix

#### C-2.1 The columns

The matrix has one column per Phase 9 audibility-table row, keyed by AT id (roadmap line 1060). Read from
`specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`:

| Group (Phase 9 shard) | Columns (AT id: contrast) |
|---|---|
| Core | AT-01 Excitation mix 0 → 1 (drive 1); AT-02 SHAPE 0 → 1; AT-03 Harmonic Fold 0 → 100 |
| Character | AT-04 Tube, AT-05 Wavefold, AT-06 Rectify, AT-07 Bite, AT-08 Crush, AT-09 Fracture (each Amount 0 → 1) |
| Anchor | AT-10 level 0 → 1; AT-11 LOCK → PUNCH; AT-12 LOCK → GHOST; AT-13 LOCK → FOLLOW (one column, carried by the pitch-moving stage of either Phase 9 variant, Fracture or Rectify); AT-14 Sine → Triangle; AT-15 → Rounded Sine; AT-16 → Asymmetric Sine; AT-17 → Octave Down; AT-18 Anchor Fold Feed 0 → 1 |
| Pitch | AT-19 Drop amount; AT-20 Drop time; AT-21 Drop shape; AT-22 Drop rebound; AT-23 Glide |
| Envelope | AT-24 amp attack; AT-25 amp release |
| Mass | AT-26 Mass 0 → 100; AT-27 Mass dynamics gain; AT-28 transient attack |
| Field | AT-29 SOLID → SPREAD; AT-30 SOLID → WIDE; AT-31 Spread; AT-32 Gravity; AT-33 Field Motion |
| Motion | AT-34 to AT-38 CYCLE → Mass / Depth / Body / Edge / Shift; AT-39 ENV → Mass; AT-40 DRIFT → Mass |
| Morph | AT-41 Morph Position 0 → 1; AT-42 Morph Position 0 → 0.5 |
| NoteMemory | AT-43 AlwaysRetrigger; AT-44 Legato; AT-45 EnvelopeRetriggerOnly; AT-46 OscillatorContinuous |
| Macros | AT-47 Weight; AT-48 Dark ↔ Bright; AT-49 Tight ↔ Loose; AT-50 Clean ↔ Violent |

That makes **50 audibility columns**. The roadmap also lists "every image mode, Anchor mode and waveform" (line 1061). Three
enumerators are only ever the *reference* side of a Phase 9 row and have no row of their own: **SOLID** (AT-29 and 30), **LOCK**
(AT-11 to 13) and **Sine** (AT-14 to 17). They become three **reference columns**: R-SOLID, R-LOCK and R-SINE. LOCK, Sine and SOLID are also the
defaults, so "a preset stores it" alone cannot fail. A reference column is covered only when a preset that stores the enumerator on
both endpoints also passes a **measured reverse arm** (C-5): the Phase 9 row that uses the enumerator as its `lo` is run with P as
stored as the lo arm and P with the row's `hi` value as the hi arm, at the row's descriptor and bar, at every note where that row is
gated:
- R-LOCK: AT-11 (LOCK → PUNCH);
- R-SINE: AT-14 (Sine → Triangle);
- R-SOLID: AT-29 (SOLID → SPREAD, spread 1), plus the SOLID bit-identity clause of the mono-sum arm (FR-042).

No other column is added, so no capability is invented.

#### C-2.2 Showcase rules

- Every preset definition declares exactly one **primary** column (Q2) and zero or more **secondary** columns. A preset never carries two primaries.
- **Each of the 53 columns is the primary of at least one preset**, because the roadmap requires that "for each column at least one
  preset is built around that capability" (lines 1062–1063).
- A preset may claim secondaries as well. A claimed secondary is checked with the same showcase arm and the same bar as a primary
  (C-5), so no claim goes unchecked.
- Therefore **N ≥ 53** (Q2: the literal roadmap reading, each capability gets a preset built around it). The plan sets N from the matrix and the per-category floor (FR-004); the list is never written from a quota
  per category.

### C-3: The audibility probe runs first, on the processor path, with the Phase 9 descriptors

Before any `ProfundumPresetDef` is written, a hidden probe case renders every column's Phase 9 contrast **through the processor**,
in the `ppRenderProcessor` pattern (roadmap handoff 13.3): base endpoints by `setState`, levers by normalised parameter writes. It
records each descriptor value against its Phase 9 bar. If a column's processor-path reading misses its bar on its Phase 9 showcase
setting, the build stops and surfaces it (FR-023), and no preset may be built on that column until the user rules.

**The unit is the gated cell, not the column.** A Phase 9 row has several readings: one per note, per variant and per candidate base,
and only the ones whose `gated` field reads `gated` were the Phase 9 verdict. Parsed this session (a quoted-CSV parse of
`audibility-table.csv`): **102 gated cells** over the 50 AT ids. Every id is gated at both E1 and C3 except:
- **AT-32** (Gravity), gated at E1 only, on base B (`B + sustain 1 (B10-1)`); its C3 readings are `recorded`;
- **AT-13**, gated four times: Fracture and Rectify variants, each at E1 and C3;
- **AT-26** (Mass), gated three times: E1 on two bases (`A + sustain 1 + anchor level 0.5` and `B (its own sustain 0.8)`) and C3
  on the first.

The gated setting can differ by note: AT-12 is gated at `ghostTimeMs 100` at E1 and at `ghostTimeMs 15` at C3. A column's
**Phase 9 showcase setting** at a note is the `base` and contrast text of its `gated` cell(s) at that note, never a `recorded`
candidate. For example, AT-01 is gated only on the `edge 1` candidate base. The probe reads every gated cell.

Each column's descriptor MUST be the Phase 9 row's own: the same estimator, tap, window, note and bar. The Phase 9 readers are local
to their TU (finding 1), so the mechanism is decided (Q4): **(b)**. Both options are kept for the record:

- **(a)** Extend the Phase 11 verbatim-port pattern (`param_path_audibility_helpers.h:5-12`) to the remaining rows. Each port gets
  an engine-direct fidelity arm that reproduces the Phase 9 CSV `value` within a tolerance the plan states.
- **(b)** Move the Phase 9 readers out of the anonymous namespace into a test header under `dsp/tests/unit/systems/`, as a pure code
  move. This changes `dsp/tests`, not `dsp/include`. It is gated by the `dsp_systems_tests` rebuild and by the moved cases' verdicts
  staying unchanged.

**Decision (Q4): mechanism (b); (a) is rejected.** The Phase 9 audibility readers move, as a pure code move, out of the anonymous namespace of `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` (readers from `:90`) into a header under `dsp/tests/unit/systems/`; that TU then includes the header. There is one source of truth for the descriptors (FR-021, FR-057). The move is an explicit, user-ruled exemption from the build stage's `dsp/` scope fence. Its gate follows the pure-code-move rule (FR-058): the `dsp_systems_tests` rebuild with zero warnings plus the moved cases (the Phase 9 audibility cases that use the readers) with unchanged verdicts, never the full `dsp_systems_tests` suite and never its `[long]` set beyond those cases. No bar is re-derived: the bars are copied from the CSV `bar` column.

### C-4: The generator reuses the shipped serializer and a host drive without Catch2

There is no `profundum_preset_format.h`. The generator drives one `::Profundum::Processor`, held on the heap, through
`ProfundumPluginTest::PresetHost` (finding 2):

1. `initialize`, then `setupProcessing`, then `setActive`.
2. One parameter-only `process()` under Edit Target A carrying every E-parameter value of endpoint A, then one under Edit Target B
   for endpoint B.
3. The morph-control, note-behaviour, global and macro values, ending with Edit Target (1001) = A (FR-047).
4. The endpoint names, through `kMsgEndpointName` (`plugins/profundum/CLAUDE.md`, State).
5. `getState()`.
6. **The seed.** The seed is persisted but not registered (`processor.h:220`: "FR-043: persisted, not registered"); only
   `setState` writes it (`processor.cpp:138-142`), so steps 1–5 always store `kDefaultSeed`. The drive therefore writes the
   definition's seed (`kDefaultSeed` when it declares none) into bytes 84–87 (`kStateSeedBytes`, `plugin_ids.h:36`) of the step 5
   stream, calls `setState` on that stream and `getState` again. The second `getState` is the stored `Comp`, and it MUST decode
   field-for-field equal to the patched stream (FR-049).

No note is played during the drive, so the morph runtime block is the processor's pre-render blob (Phase 11 Q10,
`profundum-phase11-parameters/spec.md:52`): a static position, target equal to the ramped position, with no ramp pending (FR-047).

Values reach the state the way a host delivers them, following the Phase 11 Edit Target model. The committed-tree test uses the
same drive, so the tool and the tests cannot diverge.

The CMake block copies Vorago's (`CMakeLists.txt:679-743`) and adds `plugins/profundum/src/processor/goertzel_bank_simd.cpp` and
`hwy` (finding 3). It links neither `vstgui_support` nor Catch2.

### C-5: The showcase arm measures the capability's own contrast on the preset

For a preset P and a column c that P claims, the **showcase arm** renders two versions of P through the processor, once for **every
gated cell of c** (C-3): every note at which the Phase 9 row carries a `gated` reading (E1 and C3 for every row except AT-32,
which is E1 only), and for AT-13 both the Fracture and the Rectify variant at each note. A claimed column passes only if every
such cell clears the bar. Each cell uses its own stimulus: its note, its event script (for example the legato E1 → E2 or C3 → C4
pair for AT-23 and AT-43 to 46, or the 2 s ENV retrigger for AT-39) and its read window.

- **hi arm:** P as stored.
- **lo arm:** P with c's lever set to the Phase 9 row's `lo` value, on whichever endpoints carry the lever. Examples: Character
  Amount 0 for AT-04 to 09, Image Mode SOLID for AT-29 and 30, Anchor Mode LOCK for AT-11 to 13, Motion Source Off for AT-34 to 40,
  and the Note Memory reference mode M_ref for AT-43 to 46.

The reading is c's descriptor between the two arms, gated at c's Phase 9 bar.

**Row preconditions.** Several Phase 9 contrasts hold only in one engine configuration, stated in the row's contrast text. AT-43
to AT-46 are MONO with Note Retrigger Reset (`note_params.h:317` registers Reset / Continue) and an overlap at sample 24768 (E1)
or 24128 (C3). AT-23 is MONO legato. AT-29 to AT-33 are at spread 1, and AT-31 to AT-33 are in SPREAD.
- **Authoring rule:** a preset whose **primary** column is c MUST store every precondition c's contrast text names. For AT-23 that
  means Voice Mode Mono; for AT-43 to AT-46, Voice Mode Mono and Note Retrigger Reset; for the Field rows, the stated image mode and
  spread. The primary arm overrides nothing.
- **Secondary claims** may differ from a precondition. The arm then sets that precondition to the row's value on **both** arms, and
  every such override is written to `coverage-matrix.csv` (FR-014) as `parameter=value`.
- **Setting-defined windows** take P's stored value. AT-11 reads over `[note-on, note-on + punchTimeMs)` and AT-12 over
  `[note-on, note-on + ghostTimeMs)` (the CSV descriptor text), so the window is P's stored Punch Time or Ghost Time. The value
  is recorded in `coverage-matrix.csv`.

**Twin-tap columns.** Twelve gated columns read D_nl on the `Char / twin` tap: AT-01, 03, 04 to 09, 18, 41, 42 and 50 (CSV `tap`
column, parsed this session). The processor cannot produce that twin: "The D_nl twin (FR-086 forceLinearCore) cannot be reached
through the processor" (`param_path_audibility_helpers.h:24-26`). A `VoiceEngineScript` carries macros, voice mode, polyphony,
seed, config and events of its own (`dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h:139-155`), and the processor
applies macros through `engine_->setMacros` (`processor.cpp:938`), not through the endpoints. A twin built from a Phase 9 or
default script would therefore render P's endpoints under different macros, note behaviour and voice mode, and D_nl would measure
that difference. For these columns:
- **The twin is an engine-direct render of P's decoded state.** The `VoiceEngineScript` is built from the arm's decoded `Comp`
  state: both endpoints, Morph Position and the runtime block, the four macros, voice mode, polyphony, output ceiling, the seed, and
  every note-behaviour value turned into the script's config or events. The plan lists, by name, which script field or event
  carries each decoded field. A decoded field with no carrier stops the build (FR-023).
- **The twin needs a fidelity arm.** The same decoded-state script, rendered engine-direct *without* `forceLinearCore`, MUST match
  the processor render's Sum or Char tap within the `render_fingerprint.h` tolerances (`compareFingerprints`). The check runs per
  (preset, note) wherever a twin is used. Without it, a twin that has drifted from the processor state goes uncaught.
- AT-50's whole contrast is a macro, so its twin is built per arm: macro 0 and macro 1 each give their own decoded-state script.

**Performance columns** work differently: Morph Position (AT-41, AT-42) and the four macros (AT-47 to 50). Here the capability *is*
the sweep, so both arms apply the row's own lo and hi values to P's stored state: Morph 0 against 1 or 0.5, or a macro 0 against 1
with the other macros at P's stored values.

For reference columns, the arm is the decoded-state check plus the measured reverse arm (C-2.1). The Phase 9 row's direction convention (a dB difference, a
ratio ≥ 2, or ± cents) is used unchanged.

### C-6: One seconds-scale timeline per render

Every sweep render uses the same timeline at 48 kHz, host block 512. The rate arm (FR-040) repeats it at 44.1 and 96 kHz.

- Note-on at 0 s, note-off at `H = 3.0 s`.
- The render runs to `H + T`, where `T = getTailSamples() / fs`, read after `setupProcessing` for that preset's state.
- Authoring ceilings keep `H` meaningful: amp attack ≤ 1000 ms, attack + decay ≤ 2000 ms, amp release ≤ 4000 ms, and Morph ramp
  ≤ 2000 ms (the engine maximum).
- Notes: E1 (MIDI 28) and C3 (MIDI 48), the Phase 9 registers (`param_path_audibility_helpers.h:100-102`). A preset stored in Poly
  adds a 3-note chord render: E1, B1, E2 (MIDI 28, 35, 40: root, fifth, octave; Q7, FR-063). CPU is measured on a different stimulus, 4 held notes (FR-062).

### C-7: Distinctness in parameter space and in sound space

- **Parameter space (deterministic, from the definitions).** For every pair (P, Q), at least **3** registered parameters differ by
  ≥ 0.1 normalised, or by list index for a stepped parameter. Each endpoint's E-parameter counts separately.
- **Sound space.** Components 0–9, 13 and 14 of `PresetDescriptor` are computed on the E1 and C3 sweep renders over the preset's **descriptor window**. This is
  the held window `[1.0 s, 3.0 s)` when its mid total (`bandLevels`) is ≥ −60 dBFS. Otherwise (a decaying, sustain-0 class) it
  is `[t₉₀, t₉₀ + 2^16)` from the amp-onset t₉₀ of that render. A preset whose descriptor window still reads below −60 dBFS fails
  SC-020: no descriptor is ever computed on a near-silent window. A band component (band shape, side/mid) whose band energy is below
  −60 dBFS in either preset of a pair is masked out of that pair's distance on both sides, and the masked count is reported.
  Components 10–12 (`dNlDb`, `dBrOct`, and `eSubDb` minus the mid total) are read by the moved `auSteady` on a held NoteOn render of the same note and state (`auSteadyTotal()` = 6.46 s), over the Phase 8 `readWindow`. The mid total comes from the same render, tap and window. These components are masked when that window's mid total is below −60 dBFS in either preset. (Amended 2026-10-24, plan P-5.) All its
  components already exist:
  - the mid band shape: the five `bandLevels` mid bands minus their power sum;
  - the side-to-mid ratio per band;
  - `dNlDb`, `dBrOct`, and `eSubDb` relative to the mid total. `dNlDb` needs the forceLinearCore twin, so its twin follows the
    C-5 twin-tap rule: an engine-direct render of the preset's decoded state, with the same fidelity arm against the processor
    render;
  - the amp-onset t₉₀, in log₂ ms;
  - the frame-wise E_sub peak-to-peak, on the AT-34 frame.

  **Every pair is compared on this static E1 / C3 NoteOn descriptor alone (Q3).** That includes pairs where either preset's primary is a
  behaviour or performance column (AT-23 Glide, AT-43 to 46 Note Memory, AT-41 and 42 Morph, AT-47 to 50 the macros). Such presets must
  also differ in held timbre; there is no gesture-render comparison and no exemption from sound space. A failing pair is an authoring
  defect of the library (FR-064), never a reason to lower F or exempt the pair.

  The plan fixes how each component is normalised.
- **Threshold F, calibrated once at the pilot.** F MUST be more than twice the largest distance between two renders of the same
  preset, under (i) a seed twin over the **seed-independent** presets only and (ii) a block-size twin (512 against 37) over every
  preset. A seed-dependent preset's seed-twin distance is part of its sound, not measurement noise, so it never enters F. F = max(2.5 · t_max, 1.0) with the plan §5.8 units (dB ÷ 3, E_sub ÷ 2, D_br ÷ 0.25 oct, t₉₀ log₂ ms ÷ 1, side/mid clamped to [−40, 0] dB) (amended 2026-10-24, plan P-7). Every
  `ProfundumPresetDef` declares `seedDependent`. It is expected to be true when an active endpoint stores Motion Source Drift,
  Field Motion Source Drift with Field Motion Depth > 0 (`field_params.h:39`), or a random Cycle Shape (S&H, Smooth Random,
  `motion_params.h:42`) on an active Motion. The declaration is verified by measurement in both directions (FR-044), so a wrong flag
  fails rather than biasing F. The negative controls MUST all pass in the same run, or F is void:
  - a near-variant reads `d < F`. It is one continuous registered parameter moved by 0.05 normalised on one named,
    seed-independent pilot preset. The plan names the preset and the parameter, and both are recorded in `distinctness.csv`;
  - two presets known to differ (the Phase 8 `showcasePair()` endpoints, each as a single-endpoint preset) read `d ≥ F`.

  Every pair of factory presets MUST then read `d ≥ F`. If the pilot cannot satisfy its own controls, the build stops and surfaces
  it; F is never lowered to pass.

### C-8: Determinism without bit-exact goldens

- The generator is byte-deterministic for one binary on one machine: two runs give byte-identical output trees.
- The committed tree is compared **semantically** (FR-036): same file set and paths, identical `Info` XML, identical decoded state
  field by field, with floats within `kMetricTolerance`.
- Renders are compared with the `render_fingerprint.h` tolerances.
- A preset may store a seed (Phase 11 `spec.md:390`), written by the C-4 seed step and checked in bytes 84–87 (FR-049). A
  seed-dependent preset reproduces under its seed within `kSampleTolerance`. A seed-independent preset renders within
  `kSampleTolerance` per sample under two different seeds (FR-044).

### C-9: Authoring rules carried from earlier phases

- **A Bipolar ENV rests at −1** (`phase-handoffs.txt:206-212`). A preset whose Motion Source is ENV stores Polarity Unipolar,
  unless its `Comment` says the resting offset is intended and its showcase arm still clears AT-39.
- **Recorded aliasing cells** (`profundum-phase9-voice-engine/spec.md:969`, G14-13 (3)). No factory preset stores Character Rectify
  with Character Tone 1, Harmonic Fold 100 and Image Mode WIDE together. Those four recorded cells read −78 to −80 dBFS against the
  −80 dBFS bar at 44.1 and 48 kHz.
- **Fundamental loss at Amount 1** (`phase-handoffs.txt:229-231`). A preset that stores Rectify, Crush or Fracture at Amount ≥ 0.9
  with Anchor Level < 0.25 goes into the audition sheet with its measured change in summed fundamental, so the listening verdict is
  made knowingly.
- **Endpoint names** (roadmap handoff 13.2). Every preset stores an A name and a B name: sanitised, at most 31 UTF-8 bytes, and never
  the default `A` or `B`. The three-word descriptor is never stored; `describeEndpoint` derives it.
- **Polyphony:** every preset that stores Poly stores at most "4", the Phase 9 / 10 CPU operating point.
- **Carrier-less fields** (C-5): every factory preset stores Output Gain 0 dB (normalized 1.0), Output Ceiling −1 dBTP, Pitch Bend Range 2 st, Polyphony 4 and Edit Target A. Polyphony 4 satisfies 'at most 4'. (Amended 2026-10-24, plan P-6.)

### C-10: Lanes

The lane of every check is the **Lane** column of the Success Criteria table; this section only states the rule.
- **Per push:** every NaN/Inf, boundedness and state-format check, without exception. These include the per-push render guard
  (FR-046), which duplicates the finite check of FR-038 and the true-peak bounds of FR-039 and FR-041 on a short span, so none of
  them lives only under `[long]`.
- **`[long]` only if > ~15 s** (the root `CLAUDE.md` rule): the full-length sweep, showcase, mono-sum, rate and sound-space
  distinctness arms. They are sharded by preset so that each nightly OS leg stays within its timeout.
- **CPU:** hidden behind `[.perf]` and run only through `node tools/run-cpu-tests.js profundum_tests`, alone, after an idle, and
  never straight after a clang-tidy lane (roadmap handoff 13.5, line 1050).

---

## Functional Requirements

### Category set and library

- **FR-001** `makeProfundumPresetConfig()` MUST declare exactly the six C-1 names in this order: `Sub`, `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated` (Q1), `Sub` first, with every new name added to `{"Sub"}`. The set is permanent once shipped and may only grow.
  No name may break the C-1 capability-name rule: a test compares every category name, case-insensitively, against every
  registered parameter title and list display string, every C-2.1 group name, and the leading word of every registered title (the
  continuum words exempt from the last clause only). [roadmap 1055; Phase 10 Q4]
- **FR-002** `plugins/profundum/resources/presets/` MUST contain exactly one directory per FR-001 name and no other directory, and
  every `.vstpreset` lies directly inside one of them (one-to-one both ways). `Sub/.gitkeep` is removed once `Sub` holds presets.
  [roadmap 1055]
- **FR-003** Every preset MUST embed an `Info` chunk whose attributes equal what `PresetManager::savePreset` writes
  (`preset_manager.cpp:264-276`): `MediaType` = `VstPreset`, `PlugInName` = `Profundum`, `PlugInCategory` = `Synth`, `Name`, and
  `MusicalCategory` = `MusicalInstrument` = its directory name. `Comment` carries the preset's one-line description, which names
  its primary column in words. [roadmap 1055]
- **FR-004** Every category MUST hold at least 3 presets (6 categories, so at least 18), and N MUST satisfy C-2.2, that is N ≥ 53 (Q2). [roadmap 1057–1059]
- **FR-005** Preset names MUST be unique across the library and pass `PresetManager::isValidPresetName`.
- **FR-006** Every `Comp` chunk MUST be a full current-version stream: its first int32 equals `kCurrentStateVersion` (3) and its
  length equals `kStateV3Bytes` (574). [Phase 11 / 12 state]
- **FR-007** Every preset MUST satisfy the C-6 authoring ceilings and the C-9 rules, including the carrier-less pins, and the plan P-3 / P-4 claim rules: a claim of a single-state column (AT-11, 12, 13, 17, 19–23, 33–40, 43–46) stores Morph Position 0; the AT-33 claimant stores SPREAD, spread 1 and Field Motion depth 1. (Amended 2026-10-24, plan P-6, P-3 / P-4.)
- **FR-008** No stored float may be non-finite (bit-pattern check, immune to fast-math).
- **FR-009** Every preset MUST store both endpoint names as C-9 requires, and the names round-trip through `setState` and
  `getState`. [roadmap 1045]
- **FR-066** (Phase 13b, user 2026-10-25) At least 5 factory presets MUST store Voice Mode Poly (control 1 = Poly, Polyphony 4 by
  P-6), so the Poly arms of FR-038 / FR-063 (the E1, B1, E2 chord render) and FR-045 (POLY4 CPU, ≤ 25 % at 4 voices) measure real
  presets. Each is an ordinary library preset: one primary column whose contrast does not require Mono (no AT-23, no AT-43 to
  AT-46), and every FR-004 / FR-012 / FR-038 to FR-047 / FR-061 / C-7 rule applies to it unchanged. N becomes 58.

### Coverage matrix (the variety requirement)

- **FR-010** `tools/profundum_preset_defs.h` MUST declare `Capability` with exactly the C-2.1 columns: the 50 AT columns plus R-SOLID,
  R-LOCK and R-SINE. Each AT column maps to its Phase 9 AT id; the reference columns map to none. [roadmap 1060–1062]
- **FR-011** Every `ProfundumPresetDef` MUST declare exactly one primary column and zero or more secondaries from `Capability` (Q2); a test MUST
  fail if any preset declares no primary or more than one. A test MUST
  fail if any column is the primary of no preset ("no column may be empty"). [roadmap 1062–1066]
- **FR-012** For every pair (preset, claimed column) whose column has an AT id, the C-5 showcase arm MUST clear the column's Phase 9
  bar with the Phase 9 descriptor at **every gated cell** of that column. That is every note at which the Phase 9 row carries a
  `gated` reading (E1 and C3 for every row except AT-32, which is E1 only), for AT-13, the gated cells of the variant the preset stores (Fracture or Rectify at Amount ≥ 1 − 1e-6 on endpoint A, with p = 0), at E1 and C3. The column is covered only when each variant is claimed by at least one preset (amended 2026-10-24, plan P-2). A claimed column passes only
  if every such cell clears the bar. Twin-tap columns use the C-5 decoded-state twin and its fidelity arm. A primary arm carries no
  precondition override (C-5). A failing primary is a defect of the library, not a reason to drop the column. [roadmap 1063–1065]
- **FR-013** For every reference column, at least one preset MUST store that enumerator on both endpoints (decoded-state check)
  **and** pass the C-2.1 measured reverse arm at every gated cell of its row: AT-11 for R-LOCK, AT-14 for R-SINE, AT-29 for
  R-SOLID. The R-SOLID preset MUST also pass the SOLID bit-identity clause of FR-042. [roadmap 1061]
- **FR-014** The harness MUST write the matrix to `specs/profundum-phase13-presets-release/evidence/coverage-matrix.csv`, from the
  same run that gates FR-012: one row per (preset, column, variant, note) cell with primary or secondary, measured value, bar,
  margin, the stored Punch / Ghost Time where C-5 uses it, and every precondition override.

### Audibility probe (before the list)

- **FR-020** A hidden probe case MUST render every Phase 9 **gated cell** (C-3: 102 cells over the 50 AT ids, AT-13 in both
  variants) through the processor path. Each cell is rendered on its Phase 9 showcase setting, which is the `base` and contrast of
  that cell's `gated` reading at that note. The case writes one row per cell to `evidence/probe-table.csv`, keyed by AT id,
  variant, note and base, with the descriptor value, the bar and the verdict.
  [roadmap 1066–1067; handoff 13.3]
- **FR-021** Each column's descriptor MUST be the Phase 9 row's own, provided by mechanism (b) of C-3 (Q4): the shared header of moved Phase 9 readers (FR-057), so the processor-path probe and the Phase 9 verdicts use the same reader code.
- **FR-022** No `ProfundumPresetDef` may be committed before FR-020's table is recorded; the plan orders the tasks to enforce this.
- **FR-023** If a column's probe reading misses its bar, or the column cannot be showcased from the shipped surface under FR-012, the
  build MUST stop and surface the column with its measured value. No `dsp/` edit, bar change or column removal happens without a
  user ruling.
- **FR-057** (Q4) The Phase 9 audibility readers MUST be moved out of the
  anonymous namespace of `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` into a header under
  `dsp/tests/unit/systems/`, with no change of constants, bars or computed values. The only non-verbatim edits are: (i) the namespace line; (ii) `inline` on namespace-scope functions and variables; (iii) the `auPt` alias redirected to the render shim (plan P-1); (iv) the named-function lifts of plan P-10. A lift turns an inline row block or lambda into a named header function that takes its arm states as parameters, and the Phase 9 shard calls it with its own base. (Amended 2026-10-24, plan P-1 + P-10.) That TU and the Phase 13 probe and showcase arms MUST consume the readers from that header only (no copy
  or re-port). The move is the sole `dsp/` edit of this phase, is restricted to `dsp/tests/unit/systems/`, and is exempt from the
  scope fence by the user's ruling of 2026-10-24. It MUST land before the FR-020 probe is written.
- **FR-058** (Q4) Gate of the FR-057 move (pure-code-move rule): the `dsp_systems_tests` rebuild has zero warnings, and the moved cases (the
  Phase 9 audibility cases that use the readers) run with verdicts unchanged from before the move. The full `dsp_systems_tests` suite
  and its `[long]` set beyond those cases MUST NOT be run for this gate.

### Generator and determinism

- **FR-024** `tools/profundum_preset_generator.cpp` MUST build as target `profundum_preset_generator`, write its binary to
  `${CMAKE_BINARY_DIR}/bin`, take the output directory as `argv[1]`, and write the FR-002 tree with FR-003 metadata.
  [release.yml :155-186]
- **FR-025** A custom target `generate_profundum_presets` MUST regenerate `plugins/profundum/resources/presets`.
- **FR-026** The generator MUST NOT link Catch2 or `vstgui_support`. It MUST compile `goertzel_bank_simd.cpp` and link `hwy`
  (C-4, finding 3), use the Vorago warning flags (`CMakeLists.txt:718-734`), and build with zero warnings.
- **FR-027** The generator and the tests MUST share one state-building drive (`PresetHost`, C-4).
- **FR-028** `tools/check-preset-generator-determinism.js` MUST gain `profundum: 'profundum_preset_generator'` in `GENERATORS`
  (`:54-57`) and pass for `--plugin profundum` (byte determinism and idempotence).

### Browser

- **FR-030** The header row of the D1 layout MUST gain one preset browser control: `createCustomView("PresetBrowserButton")`,
  returning an `OutlineBrowserButton`. It is placed without moving or resizing any ratified D1 control, at every zoom step from
  100 % to 200 %. It is the only new header element (Q5, FR-059). [handoff 13.1]
- **FR-031** The controller MUST give its `PresetManager` a state provider and a load provider, as the Vorago controller does
  (`vorago/src/controller/controller.cpp:113-118`). It adds the `PresetBrowserView` in `didOpen`, with tab labels `"All"` followed
  by the FR-001 names, and removes it in `willClose`. Loading a preset restores the full 574-byte state, both names included.
  **The load mechanism is a whole-stream message, not host edits.** The Vorago load provider restores only through
  begin/perform/endEdit per parameter (`vorago/src/controller/controller.cpp:140-149`, `loadComponentStateWithEdits`). That
  cannot carry Profundum's non-parameter fields: the seed (`processor.h:220`), the 20-byte morph runtime block at
  `kStateRuntimeOffset` 490 (`plugin_ids.h:39`), and the endpoint names (an `IMessage`, `controller.cpp:339-353`). It also needs
  the Edit Target switched mid-load to reach endpoint B's E-parameters. Instead:
  1. The load provider sends one controller → processor message carrying the preset's whole `Comp` chunk as a binary attribute
     (a new message ID beside `kMsgEndpointName` in `plugin_ids.h:84-89`; not a parameter and not a state-format change).
  2. `Processor::notify` (`processor.cpp:700`) applies it through the same body as `setState`, including the FR-042 restore
     hand-off.
  3. The controller mirrors the same stream through `setComponentState` and tells the host with
     `restartComponent(kParamValuesChanged)`, which it already uses (`controller.cpp:296`).
  The plan names the message ID and runs the ODR/ID sweep for it.
- **FR-059** (Q5) The header preset control is exactly one `OutlineBrowserButton` whose action opens the `PresetBrowserView` overlay (the
  Vorago pattern, Phase 12 plan Appendix A). It MUST NOT show a current-preset name, MUST NOT have previous / next controls, and MUST NOT
  add any new controller or processor state (no "modified" flag, no current-preset notification, no new parameter or message beyond the
  FR-031 load message). The ratified D1 layout does not move.
- **FR-032** Loading a factory preset through the browser MUST produce the same decoded state as `setState` with its `Comp`
  chunk. Every decoded field MUST match, and the seed (bytes 84–87), both endpoint names and the morph runtime block are named fields
  of that comparison. The editor-lifecycle harness (`Profundum_EditorLifecycle`) MUST stay green with the browser present.

### Validation harness

- **FR-033** Container and metadata: every file parses as a `.vstpreset` carrying `kProcessorUID`, a `Comp` chunk and an `Info`
  chunk, and FR-002 to FR-006 hold.
- **FR-034** Round trip: `setState(Comp)` followed by `getState` reproduces every decoded field, floats within `kMetricTolerance` and
  names byte-equal.
- **FR-035** Browser scan: a `PresetManager` with `factoryDirOverride` pointed at the committed tree lists every preset under its
  category, and none with an empty subcategory. **Installed tree (Windows):** after the build target's install step
  (`krate_plugin_install_presets`, `cmake/KratePlugin.cmake:276-287`), `PresetManager::getFactoryPresetDirectory()`
  (`preset_manager.cpp:480-484`) resolves to `%PROGRAMDATA%\Krate Audio\Profundum` (`platform/preset_paths.h:24`). A scan with
  no override lists all N presets under exactly the FR-001 categories. The plan confirms that `setup.iss:68` ships the same
  `resources/presets` tree (`presets\*` → `{commonappdata}\Krate Audio\Profundum`). [roadmap 1055–1056]
- **FR-036** Committed tree: the generator's output in a temp directory equals the committed tree semantically (C-8).
- **FR-037** RT-safe load: applying a factory preset's state and rendering the next block allocates nothing on the audio thread
  (`AllocationScope`).
- **FR-038** NoteOn render sweep: every preset at E1 and C3, plus the E1, B1, E2 chord (FR-063) when it stores Poly, on the C-6 timeline. Every output
  sample is finite (also checked per push by FR-046); RMS over `[0.2 s, 3.0 s)` is above −60 dBFS (the Phase 12 sanity bar); the RMS over the last 0.25 s of the tail
  span is ≤ −100 dBFS. [roadmap 1068]
- **FR-039** Boundedness: the true peak over the whole render is ≤ the preset's stored output ceiling + 0.1 dB (`measureTruePeakDb`,
  the Phase 9 SC-013 bar). The same bound is checked per push by FR-046.
- **FR-040** Rate arm: the FR-038 and FR-039 checks at 44.1 and 96 kHz for every preset (E1 only).
- **FR-041** True peak at maximum Mass: every preset is rendered again with Mass (104) = 100 written under Edit Target A and B and
  Mod Wheel (6) = 1, at E1 and C3. The true peak stays ≤ the stored ceiling + 0.1 dB and every sample is finite. The E1 case is
  also checked per push by FR-046. [roadmap 1069]
- **FR-060** (Q6) Every factory preset MUST store the default Output Ceiling (ID 3, -1 dBTP) in its decoded `Comp`; no preset stores another
  ceiling. FR-039, FR-041 and FR-046 therefore bound true peak at -1 dBTP + 0.1 dB for every preset.
- **FR-061** (Q6) Library loudness: each preset's held mid total level at E1 (the `bandLevels` mid-band power sum over the C-7 descriptor
  window of its E1 render: `[1.0 s, 3.0 s)`, or the sounding-window fallback for a decaying preset) MUST lie within ±4 dB of the median of
  that quantity over the whole library. The check is a gated `[long]` case; the median and every preset's deviation are printed. A preset
  outside the band is re-authored, the ±4 dB is not widened.
- **FR-042** Per-preset mono sum, on the FR-038 E1 render's **sounding window**. That window is `[1.5 s, 1.5 s + 2^16)` when its
  band energy below the resolved crossover is ≥ −60 dBFS, and otherwise `[t₉₀ + 20 ms, t₉₀ + 20 ms + 2^16)` from the render's
  amp-onset t₉₀ (a decaying, sustain-0 preset):
  - the processor output satisfies `bandReading` ρ ≥ `kSubBandRhoBar` (0.99) and |Q| ≤ `kSubBandQBarDb` (1.0 dB) below the resolved
    crossover, with the validity and sub-in-band conditions of `mono_sum_params_test.cpp:338-360`. **Every preset's read MUST
    be gated**: a read that is invalid (< −60 dBFS band energy) in both candidate windows fails the preset, it is never skipped.
    E1 (41.2 Hz) is always below the crossover, since `kMinCrossoverHz = 60` (`bass_stereo_field.h:70`);
  - every voice reads `anchorL == anchorR` bitwise;
  - a preset that stores SOLID on its active endpoints reads L == R bitwise from `kLimiterLookaheadSamples` + the mode crossfade on;
  - a non-vacuity control (R = −L on a copy) MUST fail.

  [roadmap 1068; the cross-cutting mono FR]
- **FR-043** Distinctness: the C-7 parameter-space and sound-space clauses hold for every pair, behaviour- and performance-primary presets included, on the static E1 / C3 NoteOn descriptor alone (Q3, FR-064). The pilot calibration and its
  controls are recorded in `evidence/distinctness.csv`. [roadmap 1059]
- **FR-064** (Q3) Every pair of presets, including every pair in which either preset's primary is AT-23, AT-41 to 46 or AT-47 to 50, MUST
  read sound-space `d ≥ F` on the static E1 / C3 NoteOn renders alone. No pair is exempt and no gesture render substitutes; a failing pair is
  reported as an authoring defect and the preset is re-authored (F is never lowered).
- **FR-065 (preset descriptions in the browser; user ruling 2026-10-24)** The shared preset browser MUST show a preset's description:
  - **Scan:** `PresetManager::readMetadata` (`plugins/shared/src/preset/preset_manager.cpp`, a stub until now) reads the
    `.vstpreset` chunk list, finds the `Info` chunk and sets `PresetInfo::description` from its `Comment` attribute, unescaping
    `&quot; &amp; &lt; &gt; &apos;`. It reads nothing else, so `name` and `category` still come from the filename and folder
    and no plugin regroups. A missing, truncated or malformed chunk leaves the description empty and the scan otherwise unchanged.
  - **Browser:** `PresetBrowserView` shows one line of description text between the preset list and the button bar. It shows
    the selected row's description, is empty when nothing is selected or the preset has none, and clears whenever the
    selection resets (tab, search, rescan). It is in the shared view, so every plugin's browser gets it.
  - **Gate (user ruling 2026-10-24, scoped):** full `shared_tests`, plus every plugin's preset / browser / editor-lifecycle
    cases (the only paths that reach the scan or the view), pluginval strictness 5 on all 9 plugins, and clang-tidy
    `shared`. Never the other plugins' full suites.
- **FR-044** Determinism, in both directions of the `seedDependent` declaration (C-7):
  - two renders of every seed-dependent preset under its stored seed match within `kSampleTolerance`, and a different seed
    changes the render by more than that somewhere (non-vacuity);
  - every seed-independent preset rendered under two different seeds matches within `kSampleTolerance` per sample.
- **FR-045** CPU: **every** factory preset MUST meet ≤ 8 % of one core in MONO (for a preset that stores Poly, measured with Voice Mode forced to Mono and the override recorded in `cpu.csv`), and every Poly preset ≤ 25 % at 4 voices on its stored state, at
  48 kHz, each measured with 4 notes held (E1, B1, E2, G#2; FR-062) whatever its stored polyphony (the Phase 9 / 10 / 11 bars, never relaxed). There is no "worst preset" chosen by inspection.
  Each preset is measured with the Phase 11 SC-020 method (`profundum-phase11-parameters/spec.md:54`, `:718-725`): 640 trials of
  10 blocks, P and D alternated with the order flipped on odd trials, best per side, on one engine instance. The run's
  identical-engine control arm must read inside 1.05, or the run is INVALID and is repeated, never read. [cross-cutting CPU FR]
- **FR-062** (Q7) The CPU stimulus of FR-045 is always 4 held notes, E1, B1, E2 and G#2 (MIDI 28, 35, 40, 44), whatever the preset's stored
  polyphony (at most 4 by C-9): the ruled 4-voice operating point. It is not the preset's own polyphony count and not the FR-063 chord.
- **FR-063** (Q7) The Poly chord render of FR-038 and C-6 is E1, B1, E2 (MIDI 28, 35, 40: root, fifth, octave), held and released on the C-6
  timeline.
- **FR-046** Per-push render guard. Every factory preset is rendered at E1, 48 kHz, host block 512, on a short span: note-on at 0,
  note-off at 0.5 s, render to 0.75 s. It is rendered twice, as stored and at FR-041's maximum Mass (Mass 100 under Edit Target A
  and B, Mod Wheel 1). Both renders MUST read 0 non-finite samples and true peak ≤ the stored ceiling + 0.1 dB
  (`measureTruePeakDb`). This case carries the finite check of FR-038 and the bounds of FR-039 and FR-041 in the per-push lane.
  It is never tagged `[long]`: the plan sizes it under ~15 s, records its wall clock, and shards it across per-push cases if it
  would exceed that. [root `CLAUDE.md` `[long]` rule; C-10]
- **FR-047** Stored control state. Every preset's decoded `Comp` MUST have:
  - Edit Target (1001) = A;
  - a morph runtime block (bytes 490–509) whose target and ramped position both equal the stored Morph Position (105) within
    `kMetricTolerance`, so no ramp is pending and a load never starts one. The block's layout is `bass_morph_engine.h:230-262`,
    `:336-348` (version, target, ramped p, ramped p_a, mode, ramp ms, masks).
- **FR-048** Load under a held note. For every factory preset, a render holds E1 on the default state, loads the preset's `Comp`
  through `setState` mid-note (the Phase 11 restore hand-off, `processor.h:227-232`), and keeps rendering. It MUST read 0
  non-finite samples. It MUST also satisfy a click bound: the largest `|y[n] − y[n−1]|` over `[load block, load + 20 ms)` is
  ≤ 2 × max(step_before, step_after). step_before is the largest step over the 2^12 samples before the load; step_after is the
  largest step over 2^12 samples starting 100 ms after the load. A preset that fails is surfaced, and the factor is not raised.
- **FR-049** Stored seed. A preset that declares a seed MUST store it in bytes 84–87 of its `Comp` (C-4 step 6), and every other
  preset stores `kDefaultSeed` there.

### Audition, documentation, release

- **FR-050** An audition render script, copied from `specs/profundum-phase12-ui/evidence/listen/render.sh`, MUST render every preset
  at E1 and C3 to WAV with a manifest. Where the capability is a gesture (Morph sweep, macro sweep, legato pair), it renders the
  gesture as well. A listening sheet lists each preset with an empty **Verdict (user)** line; an agent never fills a verdict.
  [handoff 13.4; Phase 9 13.1]
- **FR-051** `plugins/profundum/MANUAL.md` MUST exist and follow the Vorago manual's structure. It documents every registered control
  by its registered name, the Anchor / Mass split, Morph and Edit Target, the macros (from the Phase 12 D3 table), and the factory
  library by category. [Phase 10 `spec.md:75`]
- **FR-052** `plugins/profundum/docs/index.html` MUST exist and follow `plugins/vorago/docs/index.html` and its template.
  [Phase 10 `spec.md:75`]
- **FR-053** `.claude/skills/release/SKILL.md` MUST list `profundum` in its inputs and in its target table (`profundum_tests`,
  `Profundum.vst3`).
- **FR-054** The open `[0.1.0] - Unreleased` CHANGELOG entry MUST gain bullets for the factory library, the browser and the manual.
  `version.json` is not edited.
- **FR-055** The release gate MUST be the `release-readiness` flow for `profundum`: build, `profundum_tests`, pluginval at strictness
  5, and version.json / CHANGELOG sync, all green. [roadmap 1069]
- **FR-056** Portability and lint: `node tools/check-portability.js`, `tools/lint-apple-globals.js` and clang-tidy are clean on every
  new or modified TU and on the generator, with zero compiler warnings.

---

## Success Criteria

| SC | Requirement | Metric, threshold, method (test name sketch) | Lane |
|---|---|---|---|
| **SC-001** | FR-001, FR-002 | The category list equals `Sub`, `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated` in that order and equals the directory set in both directions, `Sub` first; 0 stray files. `Profundum_PresetCategoriesMatchFilesystem` | per push |
| **SC-002** | FR-003, FR-033 | 100 % of presets parse; all 7 `Info` attributes equal the expected strings. `Profundum_FactoryPresetContainer` | per push |
| **SC-003** | FR-004, FR-005 | The category list is exactly the 6 FR-001 names; every category ≥ 3 (≥ 18 total); N ≥ 53; 0 duplicate names; 0 names failing `isValidPresetName`. `Profundum_FactoryLibraryShape` | per push |
| **SC-004** | FR-006, FR-008, FR-047, FR-049 | 100 % of `Comp` chunks: version 3, 574 bytes, 0 non-finite floats; Edit Target decodes as A; runtime-block target and ramped position each within 2.5e-4 of the stored Morph Position (ramp idle); bytes 84–87 equal the declared seed (`kDefaultSeed` when none). `Profundum_FactoryPresetStateFormat` | per push |
| **SC-005** | FR-009, FR-034 | 100 % round trip: every decoded float within 2.5e-4, names byte-equal, no name equal to its default. `Profundum_FactoryPresetRoundTrip` | per push |
| **SC-006** | FR-010, FR-011 | 53 of 53 columns have at least 1 primary (0 empty columns); both AT-13 variants claimed (plan P-2); every preset declares exactly 1 primary (0 presets with none or 2). `Profundum_CoverageMatrixComplete` | per push |
| **SC-007** | FR-012, FR-014 | 100 % of claimed (preset, AT column, variant, note) cells ≥ the column's Phase 9 bar, at every note where the column has a gated Phase 9 cell (E1 and C3; AT-32 E1 only; AT-13 on the stored variant); every twin-tap cell's fidelity arm within the `render_fingerprint.h` tolerances; 0 precondition overrides on primary arms; the minimum margin per group is reported in `coverage-matrix.csv`. `Profundum_PresetShowcase` | `[long]`, sharded by preset |
| **SC-008** | FR-013 | R-SOLID, R-LOCK and R-SINE are each stored on both endpoints by at least 1 preset, and that preset's reverse arm clears the bar at every gated cell: AT-11 (R-LOCK), AT-14 (R-SINE), AT-29 (R-SOLID); the R-SOLID preset reads L == R bitwise after the settle point. `Profundum_ReferenceColumns` | per push |
| **SC-009** | FR-020, FR-021, FR-022, FR-057 | `probe-table.csv` exists with one row per Phase 9 gated reading (102), keyed by AT id, variant, note and base; it covers all 50 AT ids, with AT-13 probed in both variants at both notes; it was written before the first preset-definition commit; the readers it uses are the moved Phase 9 readers from the shared header (FR-057), not a copy; the AT-13 cells' processor runs prepare the engine with the `anchorPhaseTap` seam set through `Processor::setEngineSeamsForTest` (plan P-11), which is a test seam and never a preset field; each row's engine-direct fidelity arm reproduces the Phase 9 `value` within the tolerance the plan states. `Profundum_PresetAudibilityProbe` (`[.probe]`) | hidden `[.probe]`, run once before the list |
| **SC-010** | FR-024, FR-026 | `profundum_preset_generator` builds with 0 warnings and links neither Catch2 nor VSTGUI; `./build/bin/profundum_preset_generator <dir>` exits 0 and writes N files. Build log + `Profundum_GeneratorSmoke` | build + per push |
| **SC-011** | FR-028, C-8 | `node tools/check-preset-generator-determinism.js --plugin profundum` exits 0 (two runs byte-identical, idempotent). | tool, pre-commit |
| **SC-012** | FR-036 | The committed tree equals the generated tree: same paths, `Info` byte-equal, decoded fields within 2.5e-4. `Profundum_CommittedPresetTreeMatches` | per push |
| **SC-013** | FR-030, FR-031, FR-032, FR-059 | The browser control is exactly one `OutlineBrowserButton` (no name display, no previous / next, no new state) present at every zoom step without overlapping or moving a D1 control (layout test). Browser load fixture: a controller and a processor joined by `ConnectedPair` (`profundum_test_fixture.h:776`) with a component handler that records `restartComponent` (`RecordingComponentHandler`, `:400`). For every preset, `PresetManager::loadPreset` runs, then the processor's `getState` is decoded and compared with `setState(Comp)` on a fresh processor: 0 field mismatches, with the seed, both names and the runtime block listed by name; the controller reports `kParamValuesChanged`. `Profundum_EditorLifecycle` stays green. `Profundum_PresetBrowserLoad`, `Profundum_UiLayout` | per push |
| **SC-014** | FR-035 | The scan lists N presets, 0 with an empty subcategory. Installed tree (Windows): `getFactoryPresetDirectory()` equals `%PROGRAMDATA%\Krate Audio\Profundum`, and a scan with no override lists N presets under exactly the FR-001 categories. `Profundum_FactoryPresetScan`, `Profundum_InstalledPresetTree` | per push (scan); hidden `[.install]`, Windows, release gate (installed tree) |
| **SC-015** | FR-037 | 0 allocations on the audio thread across the block after the load, for every preset. `Profundum_FactoryPresetLoadRtSafe` | per push |
| **SC-016** | FR-038 | Every render: 0 non-finite samples; held RMS > −60 dBFS; tail RMS ≤ −100 dBFS. `Profundum_PresetSweep` | `[long]` (finite part also per push, SC-027) |
| **SC-017** | FR-039, FR-040 | True peak ≤ the stored ceiling + 0.1 dB at 44.1, 48 and 96 kHz; worst margin reported. `Profundum_PresetSweepRates` | `[long]` (48 kHz bound also per push, SC-027) |
| **SC-018** | FR-041 | At Mass 100 + Mod Wheel 1: true peak ≤ the stored ceiling + 0.1 dB and 0 non-finite samples, for every preset at E1 and C3. `Profundum_PresetTruePeakMaxMass` | `[long]` (E1 also per push, SC-027) |
| **SC-019** | FR-042 | Every preset: ρ ≥ 0.99 and \|Q\| ≤ 1.0 dB below f_c on its sounding window; the count of ungated (preset, read) pairs is 0; anchor L == R bitwise, SOLID L == R bitwise; the R = −L control fails. `Profundum_PresetMonoSum` | `[long]`, sharded by preset |
| **SC-020** | FR-043, FR-064 | Parameter space: every pair has ≥ 3 differing parameters. Sound space: the pilot controls pass in the same run (the near-variant's preset and parameter recorded); F is recorded with its seed-twin floor taken over seed-independent presets only; F = max(2.5 · t_max, 1.0) with the plan §5.8 units (dB ÷ 3, E_sub ÷ 2, D_br ÷ 0.25 oct, t₉₀ log₂ ms ÷ 1, side/mid clamped to [−40, 0] dB) (plan P-7); no preset's descriptor window reads below −60 dBFS, and no steady component is used unmasked on a window below −60 dBFS (plan P-5); every pair reads `d ≥ F` on the static E1 / C3 NoteOn descriptor, including every pair with a behaviour- or performance-primary preset (0 exempt pairs); the closest pair, its distance and the masked component count are reported; every twin used by `dNlDb` passes its fidelity arm. `Profundum_PresetDistinctness` | per push (parameter space); `[long]` (sound space) |
| **SC-021** | FR-044 | Seed-dependent presets: same seed within 5e-4 per sample; a different seed differs by more than that somewhere. Seed-independent presets: two different seeds within 5e-4 per sample. 0 presets whose measurement contradicts their `seedDependent` declaration. `Profundum_PresetDeterminism` | `[long]` |
| **SC-022** | FR-045, FR-062 | Every factory preset ≤ 8 % of one core in MONO (for a preset that stores Poly, measured with Voice Mode forced to Mono and the override recorded in `cpu.csv`), and every Poly preset ≤ 25 % at 4 voices on its stored state (amended 2026-10-24, plan §5.11), with 4 notes held (E1, B1, E2, G#2) whatever its stored polyphony, at 48 kHz (the Phase 9 / 10 / 11 bars, never relaxed), each with the 640-trial SC-020 method; the identical-engine control arm inside 1.05 or the run is INVALID; measured alone after an idle; every preset's reading printed and the maximum reported. `Profundum_PresetCpu` (`[.perf]`, `node tools/run-cpu-tests.js profundum_tests`) | `[.perf]`, alone, after an idle |
| **SC-023** | FR-050 | The audition script renders 2N WAVs plus the gesture WAVs, with a manifest listing each file's preset, note and gesture; the listening sheet has N empty verdict lines. Script exits 0 + manifest line count | script |
| **SC-024** | FR-051, FR-052 | `MANUAL.md` names every user-visible registered title (all 84 except the hidden IDs 4–7), checked by a test that reads the registered titles; `docs/index.html` exists and links the manual. `Profundum_ManualCoversSurface` | per push |
| **SC-025** | FR-053, FR-054, FR-055 | The `release-readiness` matrix for `profundum` reads build ✅, tests ✅, pluginval strictness 5 ✅, version.json / CHANGELOG sync ✅. The readiness report | release flow |
| **SC-026** | FR-056 | check-portability, lint-apple-globals and clang-tidy on the change set: 0 findings; build: 0 warnings. Tool logs | tools, pre-commit |
| **SC-027** | FR-046 | Every preset, stored and at maximum Mass, E1, 0.75 s: 0 non-finite samples; true peak ≤ the stored ceiling + 0.1 dB; case wall clock recorded and ≤ ~15 s per case (sharded if needed). `Profundum_PresetRenderGuard_0of4` to `_3of4` (split by preset index per R-11, 2026-10-24: the unsplit case read 21.28 s; re-split 2026-10-25 from `_0of2` / `_1of2`, which read 14.34 / 13.79 s alone and 16.02 / 15.68 s under the close lanes' load; each shard checks its own wall clock against the unchanged 15 s bar) | per push, never `[long]` |
| **SC-028** | FR-048 | Every preset loaded under a held E1: 0 non-finite samples; max step over `[load, load + 20 ms)` ≤ 2 × max(step_before, step_after); worst ratio reported. `Profundum_PresetLoadUnderHeldNote` | per push |
| **SC-029** | FR-057, FR-058 | After the pure code move: `dsp_systems_tests` rebuilds with 0 warnings; the moved Phase 9 audibility cases report verdicts identical to their pre-move verdicts (same pass / fail, same printed values) and byte-identical CSVs; `move-check.js` reports the verbatim remainder pure and lists every lift; `git status --porcelain -- dsp/` lists only the two files (amended 2026-10-24, plan P-1 + P-10); the full `dsp_systems_tests` suite is not run. Build log + the moved cases' output | gate of the move, before the probe |
| **SC-030** | FR-060 | 100 % of presets store Output Ceiling (ID 3) = the default (-1 dBTP) in their decoded `Comp`. `Profundum_FactoryPresetCeiling` | per push |
| **SC-031** | FR-061 | Every preset's held mid total at E1 is within ±4 dB of the library median; median and worst deviation printed. `Profundum_PresetLoudnessMatch` | `[long]` |
| **SC-032** | FR-062, FR-063 | The CPU case holds exactly E1, B1, E2, G#2 for every preset regardless of stored polyphony (note list asserted and printed); every Poly preset's chord render uses exactly E1, B1, E2 (MIDI 28, 35, 40). `Profundum_PresetCpu`, `Profundum_PresetSweep` | `[.perf]` (CPU), `[long]` (chord) |
| **SC-033** | FR-065 | (a) `Krate_PresetManagerReadsDescription` (shared_tests, `[preset]`): a preset saved with a description
(including `"`, `&`, `<` and `>`) scans back with the identical string; a preset without one scans back empty; a truncated file
and one whose chunk list points past the end both scan with an empty description and their name and category unchanged.
(b) Every one of the 58 Profundum factory files scans back with its def's `description` exactly (`Profundum_PresetDescriptions`).
(c) `Krate_PresetBrowserShowsDescription` (shared_tests, `[ui]`): selecting a row shows that preset's description, deselecting
or changing tabs clears it. The scoped FR-065 gate is green | per-push |
| **SC-034** | FR-066 | `Profundum_PolyPresetCount` (`[preset]`): at least 5 definitions decode with Voice Mode Poly and Polyphony 4, none with a Mono-requiring primary. Each passes its primary showcase arm with no override (coverage-matrix.csv), the chord render of `Profundum_PresetSweep`, and the SC-022 POLY4 arm reads a real maximum (not "0 % (-)") ≤ 25 % in the CPU lane | per-push; `[long]`; CPU lane |

---

## Edge cases

- **RT-safety boundaries.** Preset loads arrive on the UI thread through `setState`. The audio thread only sees the Phase 11 restore
  hand-off (`processor.h:227-232`). SC-015 covers the first block after a load. A load while a note is held (a held E1) is FR-048 /
  SC-028: finite, and within a stated click bound, relying on the existing restore re-snap (Phase 11 FR-074).
- **Parameter extremes.** FR-041 drives Mass to its maximum, plus the Mod Wheel offset, which clamps. FR-039 and FR-041 cover presets
  at Fold 100, Character Amount 1 and excitation drive 1. C-9 excludes the one recorded aliasing combination.
- **Sample-rate changes.** FR-040 renders every preset at 44.1, 48 and 96 kHz and re-reads `getTailSamples()` at each rate. The
  processor refuses host rates outside [22050, 192000] (Phase 10 decision 8); that is not a preset concern.
- **Seed determinism.** A preset's seed is stored in bytes 84–87. FR-044 proves that the same seed reproduces the render and a
  different seed changes it. A seed-independent preset must not depend on the seed beyond `kSampleTolerance` per sample; FR-044
  checks this for every preset, in both directions of the `seedDependent` declaration.
- **Block-size invariance.** The C-7 block-size twin (512 against 37) also checks that a preset's render does not depend on the host
  block size beyond the fingerprint tolerance.
- **Morph runtime blob.** Factory presets are generated with the transport stopped. The 20-byte morph runtime block holds a static
  position equal to Morph Position (105), so a load never starts a ramp (FR-047, SC-004).
- **Edit Target and Compare.** Edit Target is saved (Phase 11 Q6), and every factory preset stores Edit Target A (FR-047, SC-004).
  Compare is never persisted and resets to A·B on `setState`.
- **Decaying presets.** A sustain-0 preset (the Plucked class) can be silent over the held windows. FR-042 and C-7 move its read
  window into the sounding span, and they fail it rather than skip it when that window is still below −60 dBFS.
- **Bipolar ENV** and **fundamental loss at Amount 1:** see C-9.
- **Truncated or foreign files** in a factory directory are handled by the shared `PresetManager` as they are today; this phase adds
  no parsing.

---

## Open questions

None. OQ-1 (the category names) was resolved by Q1 on 2026-10-24: `Sub`, `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated` (C-1, FR-001).

---

## Review notes

- 2026-10-24 review round (fidelity + testability lenses). All sixteen issues were applied; none was rejected. Where an issue
  offered alternatives, the spec chose as follows:
  - **Load mechanism (FR-031):** a whole-stream controller → processor message plus `setComponentState` and
    `restartComponent(kParamValuesChanged)`, rather than host edits. Edits cannot carry the seed, names or runtime block.
  - **Category names (OQ-1, resolved by Q1):** C-1's "never a capability name" clause is kept and made checkable in FR-001. The recommendation was
    changed instead of the rule.
  - **Row preconditions (C-5):** the authoring rule for primaries and recorded overrides for secondaries, both together.
  - **CPU (FR-045):** every preset measured with the full method, instead of a ranking pass.
- The twin-tap issue counted "about 15" columns. The quoted-CSV parse of `audibility-table.csv` gives 12 gated twin-tap columns
  (AT-01, 03, 04–09, 18, 41, 42, 50), and C-5 lists those 12. The CSV also gives the 102 gated cells, AT-32 E1-only and AT-13 four
  cells, as the issues stated, plus AT-26 with three cells (two E1 bases).

---

## Clarifications

### Session 2026-10-24

- Q1: Which category names ship beside `Sub`? -> `Sub`, `Resonant`, `Harmonic`, `Driven`, `Plucked`, `Animated` (6 categories, at least 3 presets each, at least 18 by FR-004); `Sub` first; permanent once shipped. (OQ-1 resolved by Q1.) [FR-001, FR-002, FR-004, SC-001, SC-003]
- Q2: Does every preset carry exactly one primary column, forcing N >= 53? -> Yes: exactly one primary column per preset, so N >= 53 (the literal roadmap reading: each capability gets a preset built around it). [FR-004, FR-011, SC-003, SC-006]
- Q3: Must every pair, including behaviour / performance-primary presets (Glide, Note Memory, Morph, macros), read sound-space d >= F on the static E1 / C3 NoteOn render alone? -> Yes, every pair; a failing pair is an authoring defect. [FR-043, FR-064, SC-020]
- Q4: Which mechanism provides the Phase 9 descriptors on the processor path? -> (b): pure code move of the Phase 9 audibility readers out of the anonymous namespace of `profundum_engine_audibility_test.cpp` into a header under `dsp/tests/unit/systems/`; explicit scope-fence exemption for that move, ruled now; gate is the `dsp_systems_tests` rebuild with zero warnings plus the moved cases with unchanged verdicts, never the full suite nor its `[long]` set beyond those cases. [FR-021, FR-057, FR-058, SC-009, SC-029]
- Q5: What does the header preset control show and do? -> (a): one `OutlineBrowserButton` in the D1 header that opens the `PresetBrowserView` overlay (Vorago pattern); no preset name display, no previous / next, no new state; the ratified D1 layout does not move. [FR-030, FR-031, FR-059, SC-013]
- Q6: Library loudness rule and stored Output Ceiling? -> (b): every factory preset stores the default Output Ceiling (ID 3, -1 dBTP), and its held mid total level at E1 lies within +-4 dB of the library median (gated `[long]` check). [FR-060, FR-061, SC-030, SC-031]
- Q7: How are Poly presets stimulated for the chord render and the 4-voice CPU bar? -> (b): CPU is measured with 4 notes held (E1, B1, E2, G#2) whatever the stored polyphony (at most 4 by C-9), the ruled 4-voice operating point; the Poly chord render is E1, B1, E2 (root, fifth, octave). [FR-045, FR-062, FR-063, SC-022, SC-032]

### Session 2026-10-24 (preset descriptions)

- Show the factory descriptions (already written as the VST3 `Comment` meta attribute in all 53 files) in our own browser? → Yes:
  read `Comment` back in `PresetManager::readMetadata` and show the selected preset's description in `PresetBrowserView`, for
  every plugin. Gate: scoped (shared_tests + every plugin's preset / browser / editor-lifecycle cases + pluginval x9 + tidy
  shared) (user, 2026-10-24). [FR-065, SC-033]

### Session 2026-10-24 (plan rulings)

Ruled by the user on 2026-10-24 ("Approve all 10", `evidence/plan-rulings.md`). Each ruling was written into its rows first.

- P-1: the move redirects one line, the `auPt` alias, to the render shim; every other moved line is verbatim apart from the namespace line and `inline`. [FR-057, SC-029]
- P-10: route (b), the eleven named-function lifts L1–L11; the Phase 9 shard calls each lift with its own base and Phase 13 arms call the same functions; route (a) rejected. [FR-057, SC-029]
- P-2: AT-13 is measured on the variant the preset stores (Fracture or Rectify at Amount ≥ 1 − 1e-6 on endpoint A, p = 0), at E1 and C3; the column is covered only when both variants are claimed; no algorithm override. [FR-012, SC-006, SC-007]
- P-3 / P-4: a claim of a single-state column (AT-11, 12, 13, 17, 19–23, 33–40, 43–46) stores Morph Position 0; the AT-33 claimant stores SPREAD, spread 1 and Field Motion depth 1. [FR-007]
- P-5: descriptor components 10–12 are read by the moved `auSteady` on a held NoteOn render over the Phase 8 `readWindow`, with the mid total from the same render, tap and window, masked below −60 dBFS. [C-7, SC-020]
- P-6: every factory preset pins Output Gain 0 dB, Output Ceiling −1 dBTP, Pitch Bend Range 2 st, Polyphony 4 and Edit Target A. [C-9, FR-007]
- P-7: F = max(2.5 · t_max, 1.0) with the plan §5.8 units (dB ÷ 3, E_sub ÷ 2, D_br ÷ 0.25 oct, t₉₀ log₂ ms ÷ 1, side/mid clamped to [−40, 0] dB); t_max over the seed twins of seed-independent presets and the block-size twins of all presets; F fixed at the pilot. [C-7, SC-020]
- CPU MONO arm: a preset that stores Poly meets the MONO bar measured with Voice Mode forced to Mono, the override recorded in `cpu.csv`, plus the POLY4 arm on its stored state. [FR-045, SC-022]
- P-11: the processor test seam `setEngineSeamsForTest` (applied at the `processor.cpp:276` prepare) carries `anchorPhaseTap` for the AT-13 processor runs; never a preset field. [SC-009]
- §2.0: the amended texts of FR-057 / SC-029, FR-012 / SC-007 / SC-006, C-7 / SC-020, C-9 / FR-007, FR-045 / SC-022 and SC-009 approved verbatim and written into those rows. [FR-057, SC-029, FR-012, SC-007, SC-006, C-7, SC-020, C-9, FR-007, FR-045, SC-022, SC-009]

### Session 2026-10-25 (Phase 13b: Poly presets)

- Q: How many factory presets store Poly? -> At least 5 (user: "I actually want at least 5 poly presets"; done as a lean 13b on the Phase 13 spec, no new workflow). [FR-066, SC-034]
