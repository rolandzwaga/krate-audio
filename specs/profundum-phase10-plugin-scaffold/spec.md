# Feature Specification: Profundum Phase 10 — Plugin Scaffold

**Spec slug:** `profundum-phase10-plugin-scaffold`
**Roadmap:** `specs/Profundum-roadmap.md` → Part B preamble (lines 748–751), Phase 10 (lines 753–774); the Phase 9 handoff
(line 682 ff., source `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt` §10.1–§10.11, §11.3–§11.5)
**Depends on:** Phase 9 (`ProfundumEngine`, `ProfundumVoice`, `ProfundumMotion`, `ProfundumMacroMatrix`) ✅ COMPLETE 2026-10-22
**Template:** `specs/vorago-phase11-plugin-scaffold/spec.md` and the shipped `plugins/vorago/` tree. Roadmap line 750 says
*"Follows the Vorago Part B template nearly verbatim"* and line 757 says *"Template: Vorago shape"*.
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-22 · verified against `synth/profundum` at `285613c5`

---

## Overview

Phase 10 turns the finished Profundum KrateDSP phases into a VST3/AU instrument. It creates `plugins/profundum/` in the
Vorago shape: a Ruinae-style instrument with `parameters/` packs, an engine-config layer and a preset manager. It uses the
Membrum/Gradus instrument bus: event in, stereo out, no audio input, `kSupportedNumChannels 02`. The phase also:

- gives the plugin its permanent identity: AU `aumu` / `Prfd` / `KrAt`, bundle base `com.krateaudio.profundum` and two fresh FUIDs;
- reserves the parameter-ID bands of roadmap line 762;
- ships the **global** and **Personality-macro** parameter packs;
- creates a `profundum_tests` Catch2 target with the day-one tests of roadmap line 769;
- adds the plugin to **every** roster outside `plugins/` in the same change. Roadmap line 766 says *"every item, day one"*.

The processor drives the Phase 9 `ProfundumEngine` the way the engine's own tests drive it:

- One `processBlock(left, right, n)` call per event-delimited slice. The engine is a generator, so it overwrites both
  buffers (phase-handoffs §10.2).
- A wrapper-side output-gain multiply.
- The engine's 64-sample look-ahead reported to the host as latency (phase-handoffs §10.3).

A held MIDI note produces bounded stereo bass through the full Phase 1–9 chain. Voice mode, polyphony, output ceiling, output
gain and the four Personality macros all work and change the sound. Most of the work is infrastructure: each roster entry is
a separate place where CI can pass while silently skipping the plugin.

**This phase writes no new DSP and modifies no file under `dsp/`.** No shared header changes, so the close lane is
`profundum_tests` alone (roadmap line 1036; CLAUDE.md, "Run only the tests the change set can reach").

---

## Scope

**In scope**

1. The `plugins/profundum/` skeleton: `CMakeLists.txt`, `version.json`, `CHANGELOG.md`, `README.md`, a `CLAUDE.md` leaf,
   `docs/` and `installers/`.
2. Identity: two fresh FUIDs, AU `aumu` / `Prfd` / `KrAt`, bundle base `com.krateaudio.profundum`, the bus layout, the
   reserved parameter-ID map, `kCurrentStateVersion = 1` and one seed preset category (roadmap lines 757–764).
3. A `Processor` and a `Controller`, one `.cpp` each, split only if a file passes ~1500 lines. The processor owns and drives
   `ProfundumEngine`.
4. Two parameter packs:
   - `parameters/global_params.h`: output gain, voice mode, polyphony and output ceiling. These are the four items roadmap
     line 762 names for band 0–99.
   - `parameters/macro_params.h`: Weight, Dark↔Bright, Tight↔Loose and Clean↔Violent. The macros are live.
5. `engine/profundum_engine_config.h` (the thin engine-config layer of roadmap lines 757–758),
   `preset/profundum_preset_config.h` and `update/profundum_update_config.h`.
6. `resources/`: a placeholder `editor.uidesc` built from stock VSTGUI views only, `au-info.plist`,
   `auv3/audiounitconfig.h.in`, `auv3/macOS/Profundum.entitlements` and one seed preset category directory.
7. `profundum_tests`, with:
   - the day-one coverage of roadmap line 769: bus setup, denorm round-trip, state round-trip, non-silent render and
     editor-lifecycle enrollment;
   - the audio-path behaviours the processor must get right: sample-accurate event splitting, latency, the ceiling,
     degenerate shapes, no allocation, wrapper transparency and wrapper CPU overhead.
8. The full Seraphis Phase 8.5 registration checklist (roadmap line 766; `specs/Seraphis-roadmap.md:431–447`). It also
   covers the roster sites added since Seraphis that the Vorago scaffold had to touch: `run-cpu-tests.js`,
   `ci-test-shard.js`, `run-close-lanes.js` and `.gitignore`. Each site is verified by a lint or a named check.

**Non-goals, owned by later phases**

| Deferred to | What |
|---|---|
| Phase 11 (`profundum-phase11-parameters`, roadmap lines 776–791) | Every sound parameter in bands 200–1099. **Mass** and **Morph position** in band 100–199, together with the morph editing model (OQ-2: Edit A / Edit B, both `MorphState` snapshots in state). `IMidiMapping`: mod wheel and channel pressure → Mass, **pitch-bend range** and **sustain**. The seed, the Morph ramp time and `anchorFoldFeed`. State versions above 1. Velocity mapping (phase-handoffs §11.5). `getTailSamples()`, once the amp release is registered |
| Phase 12 (`profundum-phase12-ui`) | The real `editor.uidesc`, custom views, the spectrum view and the DataExchange piggyback. The **preset browser UI**: the `Krate::Plugins::PresetBrowserView` hookup in `Controller::didOpen`, modelled on `plugins/vorago/src/controller/controller.cpp:388–391`. Roadmap line 758 lists "preset browser" in the Vorago shape; the Phase 12 roadmap row does not name it, so this spec assigns it there and Phase 12 owns it. `src/ui/` stays empty here |
| Phase 13 (`profundum-phase13-presets-release`) | The fixed preset category set, factory presets, the preset sweep, the release gate, `MANUAL.md` and `docs/index.html` |

Also out of scope within Phase 10:

- modifying any file under `dsp/` or another plugin's sources;
- `INoteExpressionController` and `IMidiMapping`;
- a preset browser UI (deferred to Phase 12, see the table above), a preset generator and `.vstpreset` files;
- an `UpdateChecker` instance;
- attaching an observer to the engine. `ProfundumEngine::setObserver` is a Phase 9 test seam.

---

## Existing components (verified this session)

Every signature below was read from the file this session at `285613c5`. "engine.h" means
`dsp/include/krate/dsp/systems/profundum_engine.h`.

### DSP the processor drives (reuse-inventory rows "Voice / engine" and "Output safety", roadmap lines 154–176)

| Component | Header | What Phase 10 reuses (verified signature) |
|---|---|---|
| `ProfundumEngine` | engine.h:95 | `void prepare(double sampleRate, const ProfundumEngineConfig& cfg = {}) noexcept` (:147). The header says *"Not real-time safe … May be called again: every setter's pending value is kept"* (:144–146). It clamps the rate to `[kMinSampleRate, kMaxSampleRate]` = `[22050, 192000]` (:118–119, :151–152). `void reset() noexcept` (:201): *"Control thread; allocation-free and bounded"* (:199). `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` (:264): velocity 0 becomes `noteOff`, and a full queue drops the note-on and counts it. `void noteOff(std::uint8_t note) noexcept` (:280): never dropped; it falls back to 128 pending-release flags when the queue is full. `void setTempoBpm(float bpm) noexcept` (:299): a non-finite or ≤ 0 value keeps the previous tempo. `void setVoiceMode(VoiceMode m) noexcept` (:326). `void setPolyphony(std::size_t n) noexcept` (:330): clamps to `[1, kMaxVoices]` and only stores. `void setOutputCeilingDb(float db) noexcept` (:364): a non-finite value is kept out, the range is clamped to `[kMinCeilingDb, kMaxCeilingDb]`. `void setMacros(const ProfundumMacroValues& v) noexcept` (:371). `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (:422): **overwrites** both buffers; null or 0 returns; before prepare it writes zeros; it splits internally on the 32-sample grid. `std::size_t latencySamples() const noexcept` (:444) returns `limiter_.lookaheadSamples()`. `std::uint64_t droppedEvents() const noexcept` (:446). `bool stateFinite() const noexcept` (:448). Read-backs: `tempoBpm()` (:380), `voiceMode()` (:387), `polyphony()` (:388), `outputCeilingDb()` (:397), `macros()` (:398) |
| `ProfundumEngine` constants | engine.h:97–119 | `enum class VoiceMode : std::uint8_t { Mono = 0, Poly = 1 }` (:97). `kMaxVoices = 8` (:99). `kDefaultPolyphony = 4` (:100). `kControlInterval = 32` (:101). `kMaxQueuedEvents = 256` (:102). `kLimiterLookaheadSamples = 64` (:107). `kMinCeilingDb = -12.0f` and `kMaxCeilingDb = 0.0f` (:108–109). `kDefaultCeilingDb = TruePeakLimiter::kDefaultCeilingDb` (:110), which is `-1.0f` (`true_peak_limiter.h:49`). `kDefaultSeed = 0xB455u` (:114). The default voice mode is `Mono` (`Settings::mode`, engine.h private section) |
| `ProfundumEngineConfig` | engine.h:84–90 | `placement = ProfundumNonlinearPlacement::PerVoice; bool dynamicsOversampling = true; std::size_t dynamicsOversamplingFactor = 4; ProfundumEngineSeams seams{};`. Phase 9 measured its CPU figures with this configuration (phase-handoffs §10.1, §10.9) |
| Event model | engine.h:16–20 | *"every event and setter is latched at the engine's current sample and applied at the first grid point at or after it; the API carries no sample offsets (the caller splits its host block at each event)"*. Phase 9 proves exact partition invariance: `VoiceEngine_Partition` (`dsp/tests/unit/systems/profundum_engine_test.cpp:938`) asserts `diff == 0u` mismatches for host blocks {1, 7, 32, 64, 511, 4096} (:950–966) |
| Allocator reconfiguration | engine.h:630–634 | `configureAllocator(polyphony)`: `Oldest` allocation, `Hard` steal, `setVoiceCount(polyphony)` |
| Engine size | engine.h:92–93 | *"Heavy (eight voices, each about 100 KB): construct it with std::make_unique"* |
| `ProfundumMacro` / `ProfundumMacroValues` | `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:38, :58–63` | `enum class ProfundumMacro : std::uint8_t { Weight = 0, DarkBright, TightLoose, CleanViolent, Count }`; `struct ProfundumMacroValues { float weight = 0.5f; float darkBright = 0.5f; float tightLoose = 0.5f; float cleanViolent = 0.5f; }`; `kNeutral = 0.5f` (:67). The matrix sanitises its input (`setMacros`, :175) |
| Default sound state | `dsp/include/krate/dsp/systems/morph_state.h:71–75, :103, :114, :117`; `bass_morph_engine.h:509–510` | Both endpoints default to `MorphState{}` (`a_{}`, `b_{}`), so the Morph position has no effect until Phase 11 installs endpoints. `MorphState{}` produces sound. Recipe defaults: depth, body and edge 0.5 (`spectral_shape_recipe.h:240–241`). Amp envelope: attack 10 ms, release 100 ms (`morph_state.h:71–75`). Anchor: `Waveform::Sine`, `Mode::Lock`, level `kDefaultLevel = 0.5f` (`anchor_oscillator.h:72, :100–102`). `mass` is a `MorphState` field (:117) |
| `TruePeakLimiter` look-ahead | `dsp/include/krate/dsp/processors/true_peak_limiter.h:76, :124–131, :242–243, :257–258, :280` | `lookahead_ = 0` until `prepare` copies the requested value (:76, :280), so the engine's own read-back is 0 before prepare and 64 after, which is why the wrapper reports the constant `kLimiterLookaheadSamples` instead (FR-032). Every output sample is clamped to `±ceilingLin_` on both the look-ahead path and the zero-latency path |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (:160), `setTarget` (:170), `getCurrentValue()` (:191), `process()` (:197), `snapTo(float)` (:263) |
| `ScopedDenormalMode` | `dsp/include/krate/dsp/core/scoped_denormal_mode.h:60` | RAII FTZ/DAZ guard. Phase-handoffs §10.7: *"FTZ / DAZ is the wrapper's job"* |
| `detail::isFinite` | `dsp/include/krate/dsp/core/db_utils.h` (engine.h:40 includes it for that symbol) | A bit-pattern finite check that stays correct under `-ffast-math` |

**Include-closure check (C4459).** The Vorago template suppresses MSVC warning C4459 (`/wd4459`) because `vorago_voice.h`
reaches `timevar_comb_bank.h` (`plugins/vorago/CMakeLists.txt:133–146`). This session walked every
`#include <krate/dsp/...>` reachable from `profundum_engine.h`: 73 headers, none of them `timevar_comb_bank.h` or
`continuous_body.h`. Also, `grep "constexpr float kPi" timevar_comb_bank.h` now returns nothing. **Profundum needs no
`/wd4459`** (FR-006).

### Plugin template (Vorago, the shipped Part B model)

| Item | File | Verified shape |
|---|---|---|
| Plugin CMake | `plugins/vorago/CMakeLists.txt` | `krate_plugin_read_version(VORAGO)` and `krate_plugin_configure_generated_files()` (:12–13). `smtg_add_vst3plugin` with an enumerated source list that includes `memorystream.cpp` (:22–79). `sdk vstgui_support KrateDSP KratePluginsShared` linked PRIVATE (:82–89). `smtg_target_configure_version_file` (:98). `krate_plugin_platform_setup(... TAG VORAGO BUNDLE_BASE com.krateaudio.vorago ENTITLEMENTS Vorago.entitlements KIND instrument)` (:103–108). `smtg_target_add_plugin_resources(... resources/editor.uidesc)` (:113–116). `krate_plugin_install_to_system`, `krate_plugin_install_presets` and `krate_plugin_set_warnings` (:121, :126, :131). `add_subdirectory(tests)` under `VSTWORK_BUILD_TESTS` (:149–151). The functions are defined at `cmake/KratePlugin.cmake:35, :80, :126, :255, :287, :319` |
| `version.json` | `plugins/vorago/version.json` | keys `version, name, description, publisher, url, copyright` |
| AU config | `plugins/vorago/resources/auv3/audiounitconfig.h.in` | `kAUcomponentType 'aumu'` / `aumu` (:8–9), `kAUcomponentSubType 'Vrgo'` / `Vrgo` (:12–13), `kAUcomponentManufacturer 'KrAt'` / `KrAt` (:16–17), `kAUcomponentDescription AUv3WrapperExtension` (:19), `kAUcomponentName Krate Audio: Vorago` (:20), `kAUcomponentTag Synthesizer` (:21), `kAUcomponentVersion @AU_COMPONENT_VERSION@` (:24), `kSupportedNumChannels 02` (:36), flags and delegate (:38–40) |
| AU plist | `plugins/vorago/resources/au-info.plist:25–47` | one `AudioComponents` dict (`KrAt`, `Krate Audio: Vorago`, `Vrgo`, `aumu`) and one `SupportedNumChannels` dict with `Inputs` / `Outputs` (`0` / `2`) |
| IDs header | `plugins/vorago/src/plugin_ids.h` | `namespace Vorago` (:17); `kCurrentStateVersion` (:24); `static const Steinberg::FUID kProcessorUID(...)` (:59) and `kControllerUID(...)` (:63); `static const char* const kSubCategories = "Instrument\|Synth";` (:73) with the GCC-13 rationale (:65–72) |
| Bus shape | `plugins/vorago/src/processor/processor.cpp` | no `addAudioInput` (comment at :137); `addEventInput(STR16("Event In"))` (:141); `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)` (:142). The engine is created with `std::make_unique` in `initialize()` (:145) and released by `= nullptr` in `terminate()` (:155). `setBusArrangements` rejects `numIns != 0`, `numOuts != 1` and a non-stereo output (:167–178) |
| `process()` guards | same file, :306–345 | `ScopedDenormalMode` is the first statement (:307). Parameter changes are latched before the shape guards (:311). The guard order is `numOutputs/outputs` → `channelBuffers32` → `numChannels < 2` → `numSamples` → channel pointers → readiness; the not-ready path zero-fills and sets `silenceFlags = 3` (:316–345). A rendered block sets `silenceFlags = 0` (:454) |
| Event ordering | `plugins/vorago/src/processor/processor.h:69, :190, :262` | `kMaxEventsPerBlock = 1024`, `struct EventSlot` and `std::array<EventSlot, kMaxEventsPerBlock> eventOrder_`: the allocation-free stable insertion sort of Vorago FR-025. It is plugin-local, so Profundum re-implements it rather than sharing it |
| Shared MIDI dispatcher (**rejected**) | `plugins/shared/src/midi/midi_event_dispatcher.h:80, :92` | `dispatchMidiEvents(IEventList*, Handler&)` states *"Event.sampleOffset is deliberately not read"*. It quantises events to the block, which contradicts phase-handoffs §10.5 (split at every event's offset). Not used |
| Parameter-pack contract | `plugins/vorago/src/parameters/global_params.h` | `struct GlobalParams` of `std::atomic<>` fields (:40); `handleGlobalParamChange` (:62), `registerGlobalParams` (:102), `formatGlobalParam` (:145), `saveGlobalParams` (:176), `loadGlobalParams` (:186; EOF-safe, returns `bool`), `template <typename SetParamFunc> loadGlobalParamsToController` (:235–236) |
| Macro pack | `plugins/vorago/src/parameters/macro_params.h` | `struct MacroParams` with explicit per-field initialisers (:33–45) and the same function shape (:108, :122, :157, :175, :184) |
| Factory | `plugins/vorago/src/entry.cpp` | `#define stringPluginName "Vorago"` (:38), `BEGIN_FACTORY_DEF` (:40), two `DEF_CLASS2` (:49, :64), `END_FACTORY` (:76). It includes `<ui/arc_knob.h>` (:28); Profundum's Phase 10 entry includes no UI header |
| Controller services | `plugins/vorago/src/controller/controller.cpp` | `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeVoragoPresetConfig(), …)` (:113). No `UpdateChecker` (:64, :111). `createView` returns `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (:271–275) |
| Preset and update configs | `plugins/shared/src/preset/preset_manager_config.h:18–23`; `plugins/vorago/src/update/vorago_update_config.h:18–22` | `struct PresetManagerConfig { Steinberg::FUID processorUID; std::string pluginName; std::string pluginCategoryDesc; std::vector<std::string> subcategoryNames; }`; the field order is binding for designated initialisers (:15–17). `UpdateCheckerConfig{ …, "https://rolandzwaga.github.io/krate-audio/versions.json" }` |
| Dropdown helper | `plugins/shared/src/ui/parameter_helpers.h:47` | `createDropdownParameterWithDefault(...)` returns a `StringListParameter*`. It sets only the current value, so `defaultNormalizedValue` must be set by hand (Vorago FR-048 P-1) |
| Test target | `plugins/vorago/tests/CMakeLists.txt` | `add_executable(vorago_tests …)` (:7). SDK sources: `memorystream.cpp` (:72), `hostclasses.cpp` (:75), `pluginterfacesupport.cpp` (:76), `vstgui_test_stubs.cpp` (:79), `moduleinit.cpp` (:80), `pluginfactory.cpp` (:81). Link list `KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk` (:85–93), with `sdk` after `vstgui_support` (:84). `VORAGO_RESOURCES_DIR` (:113). `-fno-fast-math -fno-finite-math-only` (:157). `catch_discover_tests(vorago_tests REPORTER console)` (:169) |
| Test main | `plugins/vorago/tests/unit/test_main.cpp` | `#include <enable_ftz_daz.h>` (:6); the only `#include <allocation_operator_overrides.h>` in the binary (:15); `void* moduleHandle = nullptr;` (:24); `enableFTZDAZ();` (:27) |
| Lifecycle tag | `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:114` | `TEST_CASE("Vorago_EditorLifecycle", "[vorago][controller][ui][lifecycle]")` |

### Shared test infrastructure

| Helper | File | Verified |
|---|---|---|
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h:102–104` | `inline void exerciseEditorLifecycle(Steinberg::Vst::EditController& controller, const char* templateName, const std::string& uidescAbsolutePath, …)` |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h` | `kSampleTolerance = 5.0e-4f` (:58), `kMetricTolerance = 2.5e-4` (:61), `fingerprintRender(std::span<const float>)` (:73), `compareFingerprints(...)` (:122) |
| Allocation detection | `tests/test_helpers/allocation_detector.h` | `class AllocationDetector` (:48), `getAllocationCount()` (:70), `class AllocationScope` (:111) |
| Host doubles | `tests/test_helpers/vst_event_list.h:36`, `tests/test_helpers/vst_param_changes.h:36, :78` | `EventList`, `ParamValueQueue`, `ParameterChanges` |

### Registration sites (current state, with Vorago as the model entry)

| Site | File:line | Checked by `tools/lint-plugin-roster.js`? |
|---|---|---|
| Root CMake | `CMakeLists.txt:494–501`: eight `add_subdirectory(plugins/<p>)` lines, the last one `vorago` | yes (§1) |
| `ci.yml` detect-changes output, paths-filter, `for p in` loop, `$GITHUB_OUTPUT` echo | `.github/workflows/ci.yml:68, :100–101, :124, :142` | yes (§2) |
| `ci.yml` `for area in` loop | `ci.yml:116` | **no** |
| `ci.yml` three FetchContent `hashFiles` keys | `ci.yml:266, :533, :1007` | yes |
| `ci.yml` nine `for plugin_info in \` blocks and their `case` arms | `ci.yml:304/314, :356/366, :411/421, :575/585` (the macOS build entry `vorago:Vorago:Vorago_AU:Vorago_AUV3 vorago_tests`), `:636/646, :691/701, :1043/1053, :1095/1105, :1150/1160` | yes |
| `ci.yml` Windows artifact upload | `ci.yml:495–500` | **no** |
| `ci.yml` macOS `auval` step | `ci.yml:797–805` (`auval -v aumu Vrgo KrAt`) | **no** |
| `ci.yml` macOS AUv3 bundle verification | `ci.yml:864–870` | **no** |
| `ci.yml` macOS artifact upload (`.vst3`, `.component`, `AUv3.app`) | `ci.yml:954–962` | **no** |
| `ci.yml` Linux artifact upload | `ci.yml:1239–1244` | **no** |
| `release.yml` choice list and cache key | `.github/workflows/release.yml:42, :143` | yes (§3) |
| `valgrind-nightly.yml` build and run lists | `.github/workflows/valgrind-nightly.yml:284, :291`; the run filters by `'[lifecycle]'` (:301) | yes (§4) |
| `run-clang-tidy.ps1` `ValidateSet`, case and `all` | `tools/run-clang-tidy.ps1:66, :218–225, :240–251` | yes (§5) |
| `run-clang-tidy.sh` usage, case and `all` | `tools/run-clang-tidy.sh:67, :160–161, :177–178` | yes (§6) |
| `check-changelog-coverage.js` `PLUGINS` | `tools/check-changelog-coverage.js:50` | yes (§7) |
| `gen-specs-index.js` `SUBSYSTEMS` | `tools/gen-specs-index.js:19–27`. There is **no `profundum` entry**, so the nine Profundum specs are filed under "Other" (`specs/INDEX.md:179, :183–191`) | **no** |
| `run-cpu-tests.js` `DEFAULT_TARGETS` | `tools/run-cpu-tests.js:45–51`, a hard-coded list ending in `'vorago_tests'` | **no** |
| `ci-test-shard.js` Windows preset-install list | `tools/ci-test-shard.js:171–174`, hard-coded `[dir, target]` pairs ending in `['vorago', 'Vorago']` | **no** |
| `run-close-lanes.js` suite lanes | `tools/run-close-lanes.js:37–40`, a hard-coded list that includes `vorago_tests` and `seraphis_tests` | **no** |
| `.gitignore` generated files | `.gitignore:79–81` (the Vorago trio) | **no** |
| Root `CLAUDE.md` rosters | `CLAUDE.md:69–80` (overview), the Monorepo Structure prose, the leaf list, the pluginval block (:339), the test targets (:405), the built-plugin list (:459), the clang-tidy roster and the Quick Reference rows | **no** |
| `gen-repo-map.js` | `tools/gen-repo-map.js:22–27`: builds its roster from `readdir(plugins/)`, so it discovers the plugin automatically; regenerate only | n/a |
| `docs.yml` | `.github/workflows/docs.yml:41, :137–147`: globs `plugins/*/`; a plugin with no release tag gets no landing card (`if not tag: continue`) | n/a, no edit |

---

## New components

Phase 10 adds **no DSP classes**. Its new C++ types are plugin-local and live in a new top-level namespace `Profundum`.

**ODR sweep run this session.** Unless noted, the command is
`grep -rn --include=*.h --include=*.cpp "<name>\b" dsp/ plugins/ tests/`.

| New type / symbol | Layer | Header | ODR sweep result |
|---|---|---|---|
| `namespace Profundum` | plugin | `plugins/profundum/src/plugin_ids.h` | `grep -rn "namespace Profundum" dsp/ plugins/ tests/` → **0 hits**. Near-names that exist: `namespace Krate::DSP::ProfundumTest` (dsp test helpers in 7 files, e.g. `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h`) and `namespace detail::profundum_motion` (`profundum_motion.h:38`). Both are different identifiers with different nesting, and they live in a different binary. CLEAR |
| `Profundum::Processor` | plugin | `src/processor/processor.h` | `^class Processor\b` → 8 definitions, each in its own plugin namespace (disrumpo :43, gradus :34, innexus :58, iterum :51, membrum :38, ruinae :124, seraphis :338, vorago :65). CLEAR |
| `Profundum::Controller` | plugin | `src/controller/controller.h` | `^class Controller\b` → 8 controller definitions in plugin namespaces, plus 5 same-named classes in other plugins' UI headers (disrumpo `custom_buttons.h:13`, iterum `custom_views.h:21`, seraphis `cloud_view.h:45` and `edit_sub_controller.h:46`, vorago `panel_sub_controller.h:42`). CLEAR |
| `Profundum::GlobalParams` | plugin | `src/parameters/global_params.h` | `^struct GlobalParams` → ruinae :26, seraphis :40, vorago :40, each in its own namespace. CLEAR |
| `Profundum::MacroParams` | plugin | `src/parameters/macro_params.h` | `^struct MacroParams` → ruinae :13, seraphis :50, vorago :33. CLEAR |
| `makeProfundumEngineConfig`, `kOutputGainSmoothMs` | plugin | `src/engine/profundum_engine_config.h` | `makeProfundumEngineConfig` → **0 hits**. `kOutputGainSmoothMs` is a new name, chosen so it is not confused with `kMasterGainSmoothMs` (`plugins/seraphis/src/engine/seraphis_engine_config.h:38`, namespace `Seraphis`). The plan re-sweeps it; the namespace keeps it CLEAR in any case |
| `makeProfundumPresetConfig`, `makeProfundumUpdateConfig` | plugin | `src/preset/…`, `src/update/…` | 0 hits each. CLEAR |
| `ProfundumPluginTest::ProcessorFixture` | test-only | `tests/profundum_test_fixture.h` | `ProfundumPluginTest` → **0 hits**. The name `ProfundumTest` is avoided on purpose: `Krate::DSP::ProfundumTest` already has 273 references in the dsp tests. CLEAR |

**AU subtype sweep:** `grep -rn "kAUcomponentSubType " plugins/*/resources/auv3/audiounitconfig.h.in` → `Dsrm Grad Innx Itrm
Mbrm Ruin Srph Vrgo`. `grep -rn "Prfd" plugins/ tools/ .github/ CMakeLists.txt cmake/ dsp/` → **0 hits**. `Prfd` is free.

**FUID sweep:** the sixteen registered FUIDs are at `disrumpo/src/plugin_ids.h:26,:31`, `gradus:20,:23`, `innexus:20,:24`,
`iterum:21,:25`, `membrum:18,:21`, `ruinae:24,:28`, `seraphis:36,:40` and `vorago:59,:63`. The two new GUIDs of FR-011 must
not match any of them.

---

## Conventions decided in this spec

| Convention | Decision | Trace |
|---|---|---|
| FUIDs | Two freshly generated v4 GUIDs, `Profundum::kProcessorUID` and `kControllerUID`; never reused, never changed | roadmap line 761 |
| AU type / subtype / manufacturer | `aumu` / `Prfd` / `KrAt` | line 760 |
| Bundle base | `com.krateaudio.profundum` | line 761 |
| Buses | 1 event input and 1 stereo audio output; **no** `addAudioInput()` | lines 757–759 |
| `kSupportedNumChannels` | `02`, matching one plist config `Inputs 0 / Outputs 2` | line 759 |
| Parameter-ID base | 0, with 100-ID section gaps (map below) | lines 762–765 |
| State version | `constexpr Steinberg::int32 kCurrentStateVersion = 1;` | Phase 11 (line 777) names the constant |
| Version | `0.1.0`, unreleased | template |
| Engine config | The `ProfundumEngineConfig{}` defaults: PerVoice placement, sidechain oversampling on at 4×. Phase 9 measured its CPU figures with this configuration | phase-handoffs §10.1, §10.6, §10.9 |
| Seed | The engine default `kDefaultSeed = 0xB455u` (engine.h:114). **No seed parameter and no `setSeed` call** | Phase 11 owns the seed surface |
| Reported latency | The constant `ProfundumEngine::kLimiterLookaheadSamples` (64) whenever the engine exists, **before and after prepare, at every rate**. No `kLatencyChanged` message in Phase 10 (user ruling 2026-10-22) | phase-handoffs §10.3; engine.h:107 |
| Host sample rate | The supported range is the engine's `[22050, 192000]`. A rate outside it (or non-finite) is **refused**: `setupProcessing` returns `kResultFalse`, the processor stays unprepared and `process()` outputs silence with `silenceFlags = 3` (user ruling 2026-10-22) | engine.h:118–119, :151–152 |
| Output gain | Applied by the wrapper **after** the engine, because the limiter is inside `processBlock`. **Attenuation only**, dB-tapered: `-60 dB .. 0 dB`, with −∞ (gain exactly 0) at normalized 0, unity (0 dB) at normalized 1.0 and default unity (user ruling 2026-10-22). The plain value held in the atomic and in the state stream is the linear gain in `[0, 1]`. A boost after the limiter would break the ceiling, so the gain never goes above 1 | engine.h:422; the limiter is the engine's last stage (phase-handoffs §10.8) |
| Output ceiling | `kOutputCeilingId` → `setOutputCeilingDb`, range `[-12, 0]` dBTP, default −1 | roadmap line 762 ("limiter"); engine.h:108–110 |
| Polyphony range | "1".."8" (`kMaxVoices`), default 4. **Registered once, here**, one phase ahead of the roadmap's assignment (user ruling 2026-10-22). Permanent: a later raise of `kMaxVoices` needs a new ID | engine.h:99–100; phase-handoffs §11.3 (a Phase 11 handoff); roadmap OQ-8, line 1063, which assigns the registered ceiling to "Phase 9 / 11" and says the registered maximum is chosen once |
| Macros | **Live.** Pushed through `setMacros` on every `process()`. The neutral value 0.5 is the identity, so the defaults change nothing | profundum_macro_matrix.h:58–67; theme FR "nothing ships inert" |
| Mass, Morph position | **Not registered in Phase 10.** Mass is a `MorphState` field (`morph_state.h:117`). Both endpoints are `MorphState{}`, so a Morph position parameter would have no effect. Both need the Phase 11 endpoint model (OQ-2). Their IDs go in 100–199, above the macros | roadmap lines 763, 777–779 |
| Host tempo | When `kTempoValid` is set, `processContext->tempo` is forwarded through `setTempoBpm`. CYCLE sync and field motion read it (phase-handoffs §11.4: tempo is "host-driven per block") | phase-handoffs §11.4 |
| Pitch bend, sustain, CC | Not handled in Phase 10. There is no `IMidiMapping`, and VST3 delivers these only through it | roadmap line 783 assigns them to Phase 11 |
| Seed preset category | **`Sub`**: a single, permanent category. Phase 13 extends the list and never renames it | roadmap lines 7–8 (the instrument's first register is "pure sub"); line 989 (filesystem and XML must match) |
| Tail | `getTailSamples()` stays at the SDK default `kNoTail`, and `silenceFlags = 0` on every rendered block. The default release is 100 ms (`morph_state.h:75`) | Vorago template FR-024; Phase 11 owns the tail once release is registered |
| MSVC C4459 | **No suppression.** The include closure does not reach the old shadow (see the include-closure check) | zero-warning rule |

### Reserved parameter-ID map (roadmap lines 762–765)

```
0–99      Global          (Phase 10 — SHIPPED: output gain, voice mode, polyphony, output ceiling)
100–199   Macros + Mass + Morph position
                          (Phase 10 — SHIPPED: the four Personality macros at 100–103;
                           Phase 11 adds Mass and Morph position at >= 104)
200–299   Core            (Phase 11)
300–399   Anchor          (Phase 11; includes anchorFoldFeed, roadmap line 787)
400–499   Drop / note behaviour (Phase 11)
500–599   Fold / Shape    (Phase 11)
600–699   Character       (Phase 11)
700–799   Mass dynamics   (Phase 11)
800–899   Stereo field    (Phase 11)
900–999   Motion          (Phase 11)
1000–1099 Morph control   (Phase 11: anchor morph mode, edit target)
```

### Parameters shipped in Phase 10

| ID | Enum name | Type | Range / mapping → engine | Default (normalized) |
|---|---|---|---|---|
| 0 | `kOutputGainId` | Range | dB-tapered attenuation after the limiter: normalized 0 → gain 0 (−∞ dB); otherwise `dB = −60 + 60 v`, linear gain `10^(dB/20)` (−60 dB at v→0+, 0 dB at v = 1); shown in dB → wrapper multiply | `1.0` (unity, 0 dB) |
| 1 | `kVoiceModeId` | `StringListParameter` "Mono", "Poly" | index → `setVoiceMode(VoiceMode{index})` | index 0 (`Mono`, the engine default) |
| 2 | `kPolyphonyId` | `StringListParameter` "1".."8" | index + 1 → `setPolyphony` | index 3, normalized `3/7` (4 voices = `kDefaultPolyphony`) |
| 3 | `kOutputCeilingId` | Range | `-12 + 12 v` dBTP → `setOutputCeilingDb` | `11/12` (−1 dBTP) |
| 100 | `kMacroWeightId` | Range | `[0, 1]` → `ProfundumMacroValues::weight` | `0.5` |
| 101 | `kMacroDarkBrightId` | Range | `[0, 1]` → `darkBright` | `0.5` |
| 102 | `kMacroTightLooseId` | Range | `[0, 1]` → `tightLoose` | `0.5` |
| 103 | `kMacroCleanViolentId` | Range | `[0, 1]` → `cleanViolent` | `0.5` |

`id − 100 == static_cast<int>(ProfundumMacro::X)` (`profundum_macro_matrix.h:38`). The parameter **types and ranges**
registered here are permanent: roadmap line 872 says *"No param-type swaps on registered IDs, ever"*, and OQ-8 forbids a
range change at a shipped ID. The names "Weight", "Dark ↔ Bright", "Tight ↔ Loose" and "Clean ↔ Violent" do not collide with
any OQ-10 pair (roadmap lines 1067–1070); Phase 11 resolves those pairs.

---

## Functional Requirements

### A. Plugin target and build integration

- **FR-001** `plugins/profundum/CMakeLists.txt` MUST call `krate_plugin_read_version(PROFUNDUM)` and then
  `krate_plugin_configure_generated_files()` before defining the target (model `plugins/vorago/CMakeLists.txt:12–13`).
- **FR-002** `plugins/profundum/version.json` MUST carry exactly six keys: `version` (`"0.1.0"`), `name` (`"Profundum"`),
  `description`, `publisher` (`"Krate Audio"`), `url` (`"https://krateaudio.com/profundum/"`) and `copyright`. It MUST NOT
  have a `preset_subdir` key, so presets install under `Krate Audio/Profundum` (roadmap line 990).
- **FR-003** The target MUST be created with `smtg_add_vst3plugin` from an **enumerated** source list (no globs) that includes
  `memorystream.cpp`. It links `sdk vstgui_support KrateDSP KratePluginsShared` PRIVATE, with `src/` as a PRIVATE include
  directory (model :22–95).
- **FR-004** The CMake file MUST call `krate_plugin_platform_setup(${PLUGIN_NAME} TAG PROFUNDUM BUNDLE_BASE
  com.krateaudio.profundum ENTITLEMENTS Profundum.entitlements KIND instrument)` (model :103–108).
- **FR-005** The CMake file MUST call `smtg_target_configure_version_file`,
  `smtg_target_add_plugin_resources(… resources/editor.uidesc)`, `krate_plugin_install_to_system`,
  `krate_plugin_install_presets` and `krate_plugin_set_warnings`, followed by
  `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()` (model :98–151).
- **FR-006** The plugin and test targets MUST compile with **zero** warnings and MUST NOT suppress any warning, `/wd4459`
  included. If a warning appears, it is fixed. If the fix needs a `dsp/` edit, the phase stops and reports the finding to the
  user (FR-090). **D-3 (user ruling 2026-10-22):** the include-closure claim that the C4459 source is unreachable was false — the
  Profundum target (entry.cpp, processor.cpp) printed C4459 at `dsp/include/krate/dsp/core/closed_form_adaa2.h:124` (a
  function-local `kTwoPi` hiding `Krate::DSP::kTwoPi`) and C4456 at `dsp/include/krate/dsp/systems/profundum_engine.h:991`
  (a local `r` hiding the earlier `r` of the same grid step). The user allowed two identifier renames in `dsp/` as an FR-090
  exception (`kTwoPi` → `kTwoPiD`, `r` → `rc`; no expression changes, bit-identical), gated by the per-push suites of the
  two headers' consumers (dsp_processors_tests, dsp_systems_tests, dsp_effects_tests) plus profundum_tests. No suppression.
- **FR-007** `src/version.h`, `resources/win32resource.rc` and `resources/auv3/audiounitconfig.h` are generated files and are
  never edited. `.gitignore` MUST gain the three Profundum lines after the Vorago trio at `.gitignore:79–81`.
- **FR-008** The directory skeleton MUST be:
  ```
  plugins/profundum/
    CMakeLists.txt  CLAUDE.md  CHANGELOG.md  README.md  version.json
    src/
      entry.cpp  plugin_ids.h  version.h(GENERATED)
      processor/   processor.h  processor.cpp
      controller/  controller.h  controller.cpp
      parameters/  global_params.h  macro_params.h
      engine/      profundum_engine_config.h
      preset/      profundum_preset_config.h
      update/      profundum_update_config.h
      ui/.gitkeep
    resources/
      editor.uidesc  au-info.plist  win32resource.rc(GENERATED)
      auv3/audiounitconfig.h.in  auv3/audiounitconfig.h(GENERATED)
      auv3/macOS/Profundum.entitlements
      presets/Sub/.gitkeep
    tests/
      CMakeLists.txt  vstgui_test_stubs.cpp  profundum_test_fixture.h
      unit/test_main.cpp
      unit/processor_bus_test.cpp  unit/param_denorm_test.cpp  unit/state_roundtrip_test.cpp
      unit/midi_event_test.cpp     unit/lifecycle_test.cpp
      unit/controller/editor_lifecycle_test.cpp
      integration/processor_audio_test.cpp  integration/param_flow_test.cpp
      integration/processor_cpu_test.cpp
    docs/.gitkeep
    installers/windows/setup.iss  installers/linux/README.txt
  ```
- **FR-009** A `plugins/profundum/CLAUDE.md` leaf MUST follow the shape of `plugins/vorago/CLAUDE.md`: identity, roadmap
  pointer, *"No DSP lives in this plugin"*, skeleton, generated files, buses, the reserved ID table, the test target and the
  pluginval path. It MUST also record these decisions, which have no other home in the tree:
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
- **FR-010** `CHANGELOG.md` MUST contain a `## [0.1.0]` section describing the scaffold.
  `node tools/check-changelog-coverage.js profundum` exits 0.

### B. Identity

- **FR-011** `src/plugin_ids.h` MUST declare, in `namespace Profundum`, `static const Steinberg::FUID kProcessorUID(…)` and
  `kControllerUID(…)` from two freshly generated GUIDs that differ from the sixteen listed under New components.
- **FR-012** `src/plugin_ids.h` MUST declare `constexpr Steinberg::int32 kCurrentStateVersion = 1;`. The processor and the
  controller both use it, and neither includes the other.
- **FR-013** `src/plugin_ids.h` MUST declare `enum ParameterIDs : Steinberg::Vst::ParamID` with exactly the eight shipped IDs,
  and document the reserved map in a comment.
- **FR-014** `src/plugin_ids.h` MUST declare `static const char* const kSubCategories = "Instrument|Synth";` with the GCC-13
  rationale (model `plugins/vorago/src/plugin_ids.h:65–73`).
- **FR-015** `resources/auv3/audiounitconfig.h.in` MUST be a complete copy of Vorago's (:1–40) with `Vorago`→`Profundum` and
  `Vrgo`→`Prfd`. That includes the unquoted token forms, `kAUcomponentName Krate Audio: Profundum`,
  `kAUcomponentTag Synthesizer`, `kSupportedNumChannels 02`, the flags and the delegate.
- **FR-016** `resources/au-info.plist` MUST declare exactly one `AudioComponents` entry (`aumu`, `Prfd`, `KrAt`,
  `Krate Audio: Profundum`) and exactly one `SupportedNumChannels` dict `Inputs 0 / Outputs 2`. If these do not match the bus
  layout, AU initialisation fails with `-10875`.
- **FR-017** `resources/auv3/macOS/Profundum.entitlements` MUST exist (a copy of Vorago's).
- **FR-018** `src/entry.cpp` MUST register exactly two classes (`BEGIN_FACTORY_DEF`, two `DEF_CLASS2`, `END_FACTORY`) with
  `#define stringPluginName "Profundum"`. It MUST NOT include any `ui/*.h` header.
- **FR-019** The `Controller` MUST NOT implement `INoteExpressionController` or `IMidiMapping`. Roadmap line 783 assigns
  `IMidiMapping` to Phase 11.

### C. Buses and the audio path

- **FR-020** `Processor::initialize()` MUST call `addEventInput(STR16("Event In"))` and
  `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)`, and MUST NOT call `addAudioInput()` (model
  `plugins/vorago/src/processor/processor.cpp:137–142`).
- **FR-021** `setBusArrangements` MUST return `kResultFalse` when `numIns != 0`, when `numOuts != 1` or when the output is not
  stereo, and `kResultTrue` for exactly 0 inputs and 1 stereo output (model :167–178).
- **FR-022** `Processor` MUST own one `ProfundumEngine` behind `std::unique_ptr`, created in `initialize()` and released in
  `terminate()` by assigning `nullptr` (engine.h:92–93). It never attaches an observer.
- **FR-023** `setupProcessing()` is the only path that prepares the engine, and `process()` cannot reach it. The steps, in
  order:
  1. If the engine is null (an out-of-order host call), stay unprepared.
  2. **Refuse an unsupported rate.** If `setup.sampleRate` is non-finite (bit-pattern check) or outside
     `[ProfundumEngine::kMinSampleRate, ProfundumEngine::kMaxSampleRate]` = `[22050, 192000]`, set `prepared_ = false`, leave the
     engine un-prepared by this call, and return `kResultFalse`. Nothing is clamped. `process()` then takes the not-ready
     path of FR-030 (silence, `silenceFlags = 3`). A later `setupProcessing()` at a supported rate recovers normally.
  3. Call `engine_->prepare(setup.sampleRate, makeProfundumEngineConfig())`.
  4. Configure the output-gain smoother with `kOutputGainSmoothMs = 20.0f` at the host rate (now guaranteed in range), and arm
     its first-block snap.
  5. Push every shipped parameter atomic to the engine once, because a `setState()` may arrive before `setupProcessing()`.
  6. Set `prepared_ = true` and return the base-class result.

  A repeated `setupProcessing()`, such as a sample-rate change, follows the same path (including the refusal of step 2); `prepare` is repeatable
  (engine.h:144–146).
- **FR-024** `Processor::process()` MUST:
  1. Construct `Krate::DSP::ScopedDenormalMode` as its first statement (phase-handoffs §10.7).
  2. Latch parameter changes, taking the last point of each queue, before the shape guards.
  3. Run the shape guards of FR-030.
  4. Once per call, before the first slice, push the global and macro atomics to the engine and forward the host tempo
     (FR-027). Every setter is an O(1) store, and the engine applies only the values that changed, at its next grid point
     (engine.h:16–25).
  5. Render the block in event-delimited slices. For each slice: dispatch the events due at its start, call
     `engine_->processBlock(outL + start, outR + start, len)`, then apply the per-sample output-gain multiply to that slice.
  6. Set `data.outputs[0].silenceFlags = 0` on every rendered block.
- **FR-025** Events MUST be sample-accurate (phase-handoffs §10.5):
  - The host block is split at every event's `sampleOffset`.
  - Events are applied in offset order, using an allocation-free stable insertion sort of `(clampedOffset, listIndex)` into a
    fixed `std::array` of `kMaxEventsPerBlock = 1024` entries.
  - Offsets are clamped to `[0, numSamples − 1]`.
  - Events beyond the first 1024 are applied after the sorted queue, in list order, at non-decreasing effective offsets (the
    Vorago FR-025 overflow rule).

  The shared `dispatchMidiEvents` quantises events to the block, so it is not used (`midi_event_dispatcher.h:80`).
- **FR-026** MIDI translation:
  - `kNoteOnEvent` with `velocity > 0` → `noteOn(pitch, uint8(clamp(velocity * 127 + 0.5, 1, 127)))`.
  - `kNoteOnEvent` with `velocity <= 0`, and `kNoteOffEvent` → `noteOff(pitch)`.
  - A pitch outside `[0, 127]` is dropped.
  - Every other event type is ignored.

  The velocity floor of 1 matters because the engine treats velocity 0 as a note-off (engine.h:264–270).
- **FR-027** Host tempo: if `data.processContext != nullptr` and its `state` has `ProcessContext::kTempoValid` set, call
  `engine_->setTempoBpm(float(processContext->tempo))` once per `process()`. Otherwise call nothing; the engine keeps its
  previous tempo, which is the engine default of **120 BPM** until a valid tempo has been forwarded. The engine itself rejects non-finite and ≤ 0 values (engine.h:299–303).
- **FR-028** Output gain: `GlobalParams::outputGain` (the linear gain in `[0, 1]` that FR-043's dB taper produces) drives a `Krate::DSP::OnePoleSmoother` that advances
  once per output sample. Its value multiplies both channels **after** `processBlock`. The smoother is snapped to the current
  atomic value on the first `process()` after `setupProcessing()` or `setActive(true)`. The multiplier never exceeds 1, so the
  output never exceeds the engine's ceiling. The processor MUST expose
  `float outputGainValueForTest() const noexcept`, which returns the smoother's `getCurrentValue()` (`smoother.h:191`).
- **FR-029** `process()` MUST NOT allocate, lock, throw or perform I/O. It needs no scratch buffer, because the engine renders
  straight into the host channel buffers. No buffer may be resized on the audio thread.
- **FR-030** Degenerate shapes produce silence and return `kResultOk` without touching the engine. The order below is binding
  (model `plugins/vorago/src/processor/processor.cpp:316–345`):
  1. Latch parameters.
  2. Check `numOutputs > 0 && outputs != nullptr`.
  3. Check `channelBuffers32 != nullptr`.
  4. Check `numChannels >= 2`.
  5. Check `numSamples > 0`.
  6. Check that both channel pointers are non-null.
  7. Check readiness (`prepared_ && engine_`).

  On the not-ready path the processor zero-fills both channels and sets `silenceFlags = 3`.
- **FR-031** `setActive(false)` MUST call `engine_->reset()`, which runs on the control thread, does not allocate and keeps the
  pending setter values (engine.h:199–201). `setActive(true)` MUST NOT allocate, and it re-arms the gain snap.
- **FR-032** `getLatencySamples()` MUST return the constant `Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples` (64) whenever
  the engine exists, regardless of whether `setupProcessing()` has run, and 0 only when the engine is null (before
  `initialize()` or after `terminate()`). It never depends on `prepared_` or the sample rate. The value equals
  `engine_->latencySamples()` after every successful prepare, at every supported rate. No `restartComponent(kLatencyChanged)` is
  issued in Phase 10. If a later phase makes it variable, that phase MUST announce the change
  through processor → `IMessage` → controller.
- **FR-033** Test access, all `const` and allocation-free:
  - `const Krate::DSP::ProfundumEngine* engineForTest() const noexcept`, for the read-backs `voiceMode()`, `polyphony()`,
    `outputCeilingDb()`, `macros()`, `tempoBpm()`, `latencySamples()`, `droppedEvents()` and `stateFinite()`;
  - `globalParamsForTest()` and `macroParamsForTest()`;
  - `outputGainValueForTest()`;
  - a slice counter: the number of slices rendered by the most recent `process()`.

### D. Parameters and state

- **FR-040** `src/parameters/global_params.h` MUST follow the pack contract (model
  `plugins/vorago/src/parameters/global_params.h:40, 62, 102, 145, 176, 186, 235–236`):
  `struct GlobalParams { std::atomic<float> outputGain{1.0f}; std::atomic<int> voiceMode{0}; std::atomic<int> polyphony{4};
  std::atomic<float> outputCeilingDb{-1.0f}; }`, plus `handleGlobalParamChange`, `registerGlobalParams`,
  `formatGlobalParam` (output gain displays as dB, e.g. "-6.0 dB", and "-inf dB" at gain 0), `saveGlobalParams`, `loadGlobalParams` (EOF-safe) and `loadGlobalParamsToController`.
- **FR-041** `src/parameters/macro_params.h` MUST define four `std::atomic<float>` fields in `ProfundumMacro` order, with
  explicit `{0.5f}` initialisers, and the same six functions. The processor builds a `ProfundumMacroValues` from them and
  calls `engine_->setMacros(...)` on every `process()` (FR-024 step 4).
- **FR-042** `processParameterChanges()` MUST dispatch by ID range: IDs below 100 go to the global pack, IDs 100–199 to the
  macro pack. It takes the **last** point of each queue. Unregistered IDs inside a band (4–99, 104–199) are ignored and
  touch no atomic.
- **FR-043** Denormalisation. Every input is first checked with the bit-pattern `detail::isFinite` (a non-finite normalized
  value is ignored) and clamped to `[0, 1]`. Then:

  | Parameter | Plain value |
  |---|---|
  | output gain | `v == 0 ? 0 : 10^((−60 + 60 v) / 20)`: gain 0 (−∞ dB) at 0, −60 dB just above 0, unity (exactly 1.0f) at `v == 1`. Never above 1 |
  | voice mode | `v >= 0.5 ? Poly : Mono` |
  | polyphony | `clamp(int(v * 7 + 1 + 0.5), 1, 8)` (list "1".."8", `stepCount` 7; a `static_assert` ties the list length 8 to `ProfundumEngine::kMaxVoices`) |
  | output ceiling | `-12 + 12 v` dB |
  | macros | `v` |
- **FR-044** `getState()` writes, little-endian via `Steinberg::IBStreamer(state, kLittleEndian)`:
  1. `int32 kCurrentStateVersion`;
  2. the global block: `float outputGain, int32 voiceMode, int32 polyphony, float outputCeilingDb`;
  3. the macro block: four `float`s in `ProfundumMacro` order.

  The stream is **36 bytes**.
- **FR-045** `setState()` reads the version first and returns `kResultFalse` if it is greater than `kCurrentStateVersion`. It
  loads the packs in the same order. A truncated stream leaves the unread parameters unchanged. The stream holds **plain**
  values, so the FR-043 normalized-domain clamps do not apply; each loaded field is sanitised in the plain domain before it
  reaches an atomic, so a hostile stream cannot push an out-of-range or non-finite value into the engine:

  | Field | Rule |
  |---|---|
  | any `float` field | a non-finite value (bit-pattern `detail::isFinite`) keeps the previous atomic value |
  | `outputGain` | clamp to `[0, 1]` (so FR-028's multiplier never exceeds 1) |
  | `voiceMode` | `0` or `1` is stored; any other `int32` keeps the previous value |
  | `polyphony` | clamp to `[1, 8]` |
  | `outputCeilingDb` | clamp to `[-12, 0]` |
  | each macro | clamp to `[0, 1]` |

  The controller's `load…ParamsToController` helpers (FR-046) apply the same rules before normalising. The stream holds the
  **linear gain**; the output-gain helper normalises it with the inverse of FR-043's taper: `v = g <= 0 ? 0 : clamp(1 + 20 log10(g) / 60, 0, 1)` (a gain below −60 dB but above 0 normalizes to 0, the only non-round-tripping range).
- **FR-046** `Controller::setComponentState()` reads the same stream through the `load…ParamsToController` helpers, so every
  registered parameter follows a preset load.
- **FR-047** `Controller::initialize()` MUST register exactly the eight parameters. The two dropdowns use
  `createDropdownParameterWithDefault`, and then `getInfo().defaultNormalizedValue` is set by hand: `0` for voice mode and
  `3.0 / 7.0` for polyphony. The output-gain parameter's default normalized value is `1.0` (unity). The helper only moves the current value (Vorago FR-048 P-1).

### E. Controller, preset and update adapters, resources

- **FR-050** `src/preset/profundum_preset_config.h` MUST provide `makeProfundumPresetConfig()`, returning
  `{ kProcessorUID, "Profundum", "Synth", { "Sub" } }` in the field order of `preset_manager_config.h:18–23`.
  `Controller::initialize()` creates a `Krate::Plugins::PresetManager` from it.
- **FR-051** `resources/presets/Sub/` MUST exist, so the filesystem category and the config category agree from day one
  (roadmap line 989). The directory name `Sub` MUST equal `makeProfundumPresetConfig().subcategoryNames[0]`, byte for byte. `Sub` is
  permanent: Phase 13 keeps it and may only add categories.
- **FR-052** `src/update/profundum_update_config.h` MUST provide `makeProfundumUpdateConfig()`. The controller MUST NOT create
  an `UpdateChecker`: it starts a thread and uses the network, which has no place in the lifecycle, ASan or valgrind lanes.
- **FR-053** `src/engine/profundum_engine_config.h` MUST stay thin: `inline Krate::DSP::ProfundumEngineConfig
  makeProfundumEngineConfig() noexcept` returns `ProfundumEngineConfig{}` unchanged (engine.h:84–90), plus the constant
  `kOutputGainSmoothMs`. Any change from the defaults must be justified in the header against the Phase 9 measurements.
- **FR-054** `resources/editor.uidesc` MUST be a placeholder made only of stock VSTGUI views (`CViewContainer`, `CSlider`,
  `COptionMenu`, `CTextLabel`) with a template named `"editor"`. It holds **eight** controls bound through `<control-tags>`
  to the eight shipped IDs, with a `COptionMenu` for voice mode and for polyphony. It must not be empty, because
  `tools/check-bundle.js` fails a bundle with an empty `editor.uidesc`.
- **FR-055** `Controller::createView("editor")` MUST return a `VSTGUI::VST3Editor` on that template. The controller MUST survive
  `willClose()` on a view tree that was never attached to a window.
- **FR-056** `src/ui/` MUST hold nothing except `.gitkeep`.

### F. Test target `profundum_tests`

- **FR-060** `plugins/profundum/tests/CMakeLists.txt` MUST define `add_executable(profundum_tests …)` in the shape of
  `plugins/vorago/tests/CMakeLists.txt`:
  - the test sources and a **second compilation of every plugin `.cpp`**;
  - the SDK sources `memorystream.cpp`, `hostclasses.cpp`, `pluginterfacesupport.cpp`, `moduleinit.cpp` and
    `pluginfactory.cpp`;
  - a local `vstgui_test_stubs.cpp`;
  - the link list `KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk`, with `sdk` last;
  - `PROFUNDUM_RESOURCES_DIR`;
  - `catch_discover_tests(profundum_tests REPORTER console)`.
- **FR-061** `tests/unit/test_main.cpp` MUST define `void* moduleHandle = nullptr;` and call `enableFTZDAZ()` before
  `Catch::Session().run()`. It MUST be the only TU that includes `<allocation_operator_overrides.h>`.
- **FR-062** Any TU that injects NaN or Inf MUST carry `-fno-fast-math -fno-finite-math-only` under `Clang|GNU`, build
  non-finite values from bit patterns through a volatile, and never call `std::isnan`, `std::isinf` or `std::isfinite`.
- **FR-063** Tests MUST reuse `Krate::Test::EventList`, `ParamValueQueue` and `ParameterChanges`.
  `tests/profundum_test_fixture.h` (namespace `ProfundumPluginTest`) holds only what those lack: the multi-point queue
  (`MultiPointParamValueQueue` / `MultiParamChanges`, used only where a queue carries several points or a non-zero offset;
  every single-point offset-0 change uses `Krate::Test::ParameterChanges`), the `ProcessorFixture`, and the two doubles the
  plan defines with no shared counterpart (plan §4.2): `CountingConnectionPoint` (SC-014) and `DirectEngine` / `DirectEvent`
  (the reference arm of SC-008, SC-015, SC-016.4). [Amended 2026-10-22, compliance remediation: the row now names every
  fixture type plan §4.2 rules in.]
- **FR-064** Every test constructs `Profundum::Processor` and `ProfundumEngine` on the heap.
- **FR-065** No bit-exact float golden: `node tools/lint-float-bit-goldens.js` stays clean. Two renders compared within one
  process use the explicit mismatch counts or max-abs thresholds named in the SCs. Comparisons across runs use
  `render_fingerprint.h`.
- **FR-066** Day-one coverage, with fixed `TEST_CASE` names, all tagged `[profundum]`:

  | File | TEST_CASE | Covers |
  |---|---|---|
  | `unit/processor_bus_test.cpp` | `Profundum_ProcessorBusSetup` | FR-020, FR-021 |
  | `unit/param_denorm_test.cpp` | `Profundum_ParamDenormRoundTrip` | FR-042, FR-043, FR-047 |
  | `unit/state_roundtrip_test.cpp` | `Profundum_StateRoundTrip` | FR-044–FR-046 |
  | `unit/midi_event_test.cpp` | `Profundum_MidiEventTranslation` | FR-025, FR-026 |
  | `unit/lifecycle_test.cpp` | `Profundum_ProcessorLifecycle` | FR-019, FR-023, FR-028 (gain snap), FR-029, FR-030, FR-031, FR-032 |
  | `unit/controller/editor_lifecycle_test.cpp` | `Profundum_EditorLifecycle`, tagged `[profundum][controller][ui][lifecycle]` | FR-050, FR-054, FR-055 |
  | `integration/processor_audio_test.cpp` | `Profundum_ProcessorRendersHeldNote` | FR-024, FR-028, FR-032 |
  | `integration/param_flow_test.cpp` | `Profundum_ParamFlowReachesEngine` | FR-024, FR-027, FR-041, FR-043 |
  | `integration/processor_cpu_test.cpp` | `Profundum_ProcessorCpu`, tagged `[profundum][.perf][performance]` | FR-067 (hidden; runs only in the isolated CPU lane) |

  The `[lifecycle]` tag is required: `valgrind-nightly.yml:301` selects cases by `'[lifecycle]'`.
- **FR-067** `Profundum_ProcessorCpu` times, back-to-back in one case with an identical script, the chain through
  `Processor::process()` and the same chain driven directly (`engine_->processBlock` plus the gain multiply). It gates the
  ratio and the absolute Phase 9 bars (SC-015). It runs only through `node tools/run-cpu-tests.js profundum_tests`, alone on an idle machine.

### G. Registration outside `plugins/`, all of it in this phase (roadmap line 766)

- **FR-070** Root `CMakeLists.txt` MUST add `add_subdirectory(plugins/profundum)` after `:501`.
- **FR-071** `.github/workflows/ci.yml` MUST add a Profundum entry at every site that names a plugin:
  1. The sites the roster lint checks: the detect-changes output, the paths-filter, the `for p in` loop, the
     `$GITHUB_OUTPUT` echo, the three `hashFiles` keys, and the nine `for plugin_info in \` blocks with their `case` arms. The
     macOS build entry is `profundum:Profundum:Profundum_AU:Profundum_AUV3 profundum_tests`, modelled on :575.
  2. The sites the lint does not check, each modelled on its Vorago twin:
     - the `for area in` loop (:116);
     - the Windows artifact upload (:495–500);
     - the macOS step running `auval -v aumu Prfd KrAt` (:797–805);
     - the macOS AUv3 bundle verification (:864–870);
     - the macOS artifact upload with `.vst3`, `.component` and `AUv3.app` (:954–962);
     - the Linux artifact upload (:1239–1244).
- **FR-072** `.github/workflows/release.yml` MUST add `- profundum` to the choice list (:42) and
  `'plugins/profundum/CMakeLists.txt'` to the `hashFiles` key (:143).
- **FR-073** `.github/workflows/valgrind-nightly.yml` MUST add `profundum_tests` to the build list (:284) and the run list
  (:291).
- **FR-074** `tools/run-clang-tidy.ps1` MUST add `"profundum"` to the `ValidateSet` (:66), add a `"profundum"` case covering
  both `plugins/profundum/src` **and** `plugins/profundum/tests` (model :218–225), and add the same paths to `all`
  (:240–251).
- **FR-075** `tools/run-clang-tidy.sh` MUST add a `profundum)` case (src and tests), add the paths to `all)`, and add
  `profundum` to the usage text (:67).
- **FR-076** `tools/check-changelog-coverage.js` MUST add `'profundum'` to `PLUGINS` (:50).
- **FR-077** `tools/gen-specs-index.js` MUST add `['profundum', 'Profundum']` to `SUBSYSTEMS` before the DSP keywords, with a
  comment in the style of :20–26. `specs/INDEX.md` then lists every Profundum spec, this one included, under a "Profundum"
  heading instead of "Other".
- **FR-078** The generated artifacts MUST be regenerated and committed: `specs/_architecture_/repo-map.json`
  (`gen-repo-map.js` discovers `plugins/` automatically) and `specs/INDEX.md`. The generator checks in
  `tools/hooks/guard-ci-gates.js` pass.
- **FR-079** Root `CLAUDE.md` MUST add Profundum to every roster it contains:
  - the Project Overview list and the Monorepo Structure prose;
  - the per-directory leaf list;
  - the pluginval block, the plugin test-target list and the "Built plugins are at" list;
  - the clang-tidy `-Target` roster;
  - the Quick Reference rows for adding a parameter, adding a test and changing the UI.
- **FR-080** Four hard-coded tool lists MUST gain Profundum. The roster lint checks none of them, so a missed entry silently
  skips the plugin:
  - `.claude/workflows/release-readiness.js`: add `profundum: { testTarget: 'profundum_tests', bundle: 'Profundum.vst3' }`
    to `PLUGIN_MAP` (:13–22), the map the Phase 13 release gate reads (**user ruling Q-B, 2026-10-22**: added now, not in
    Phase 13);
  - `tools/run-cpu-tests.js`: add `'profundum_tests'` to `DEFAULT_TARGETS` (:45–51);
  - `tools/ci-test-shard.js`: add `['profundum', 'Profundum']` to the Windows preset-install list (:171–174);
  - `tools/run-close-lanes.js`: add `'profundum_tests'` to the suite lanes (:37–40).
- **FR-081** `.github/workflows/docs.yml` needs no edit: it globs `plugins/*/`, and an untagged plugin gets no card
  (:137–147).

### H. Scope fences

- **FR-090** No file under `dsp/`, `tests/test_helpers/` or another plugin's `src/` or `tests/` is modified. If the phase finds
  it needs such a change, it stops and reports the finding to the user. As a result, the close lane is `profundum_tests` plus
  the repo-wide lints, and no consumer suite is re-run (roadmap line 1036). **Exception D-3 (user ruling 2026-10-22):** two
  identifier renames in `dsp/` (FR-006), with the consumer per-push suites re-run as the gate; the SC-019 allow-list names the
  two headers.

---

## Success Criteria

- **SC-001 — Builds on all three OS legs with zero warnings.** The CI build job produces `Profundum.vst3` on Windows, macOS
  and Linux, plus `Profundum_AU` and `Profundum_AUV3` on macOS. Thresholds:
  - 3 of 3 legs green.
  - A target-scoped local build, run after touching the sources, shows **0** lines matching `warning C[0-9]|warning:` with
    no path filter. A warning raised in a `dsp/` header counts. Command:
    `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Profundum profundum_tests`.
  - `node tools/check-portability.js <every new .cpp under plugins/profundum/src and plugins/profundum/tests>` exits 0 with
    **0 skipped**: its summary line reads `check-portability: all clear -- N compiled.` with no `, M skipped` suffix
    (`tools/check-portability.js:356–358`), and N equals the number of new `.cpp` files. That output line is recorded in
    `compliance.md`. The tool's own floor (exit 0 when at least one TU compiled, `:340–350`) is not the bar.

  *(Roadmap line 772.)*
- **SC-002 — `profundum_tests` is green and contains the required cases.**
  1. `profundum_tests.exe --list-tests` lists all eight non-hidden FR-066 names, and
     `profundum_tests.exe "[.perf]" --list-tests` lists `Profundum_ProcessorCpu`.
  2. The last line of `profundum_tests.exe 2>&1 | tail -5` reads `All tests passed`.

  *(Roadmap line 772.)*
- **SC-003 — pluginval strictness 5 is clean.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Profundum.vst3"` exits 0. *(Roadmap line 773.)*
- **SC-004 — `auval` passes.** `auval -v aumu Prfd KrAt` reports `AU VALIDATION SUCCEEDED` in the macOS CI step added by
  FR-071. *(Roadmap line 773.)*
- **SC-005 — A held note renders non-silent, finite audio.** `Profundum_ProcessorRendersHeldNote`: 48 kHz, 512-sample blocks,
  registered defaults, NoteOn(E1 = 28, velocity 100) at sample 0, 2 s rendered. Thresholds:
  - RMS over [0.5 s, 2 s) ≥ −40 dBFS on each channel. This is the reference bar of the Phase 9 partition test
    (`profundum_engine_test.cpp:959`).
  - Every sample is finite, checked by bit pattern.

  The measured peak and RMS are printed with `WARN` and recorded in `compliance.md`. *(Roadmap line 769: "non-silent
  render".)*
- **SC-006 — Output never exceeds the ceiling.** Same case. Setup: POLY, polyphony 8, eight notes
  {28, 33, 40, 45, 52, 57, 64, 69} held, all four macros at 1.0, output gain 1.0, 4 s rendered. Thresholds:
  - `max |sample| <= 10^(-1/20)` (0.891251) on both channels.
  - With `kOutputCeilingId` then set to normalized 0 (−12 dBTP), from the first block after the change plus 64 samples:
    `max |sample| <= 10^(-12/20)` (0.251189).
  - Non-vacuity: the first render's peak is at least 0.5 × the ceiling. If it is not, the script is made louder; the bar never
    moves.

  The guarantee comes from the limiter's clamp (`true_peak_limiter.h:242–243, :257–258`) plus the gain of at most 1
  (FR-028).
- **SC-007 — Zero allocations in `process()`.** `Profundum_ProcessorLifecycle`: after prepare and one warm-up block, render
  2 s inside an `AllocationScope`, with note-ons and note-offs, changes to voice mode, polyphony, ceiling, gain and the macros,
  and a tempo change. Threshold: `getAllocationCount() == 0`. A liveness probe runs first: one deliberate `new` inside a scope
  must read ≥ 1.
- **SC-008 — The wrapper is transparent.** This SC also covers the mono-sum and anti-aliasing theme FRs by inheritance.
  `Profundum_ProcessorRendersHeldNote` section with a 3 s script: a MONO legato pair, then a POLY four-note chord, with notes
  at offsets that are not multiples of 32. Render it twice:
  - (a) through `Processor::process()` at host block 512;
  - (b) through a heap `ProfundumEngine` prepared with `ProfundumEngineConfig{}` and driven directly, with the same events at
    the same samples.

  The MONO → POLY switch rides a parameter queue at the **start of a host block** (point offset 0, on a 512-sample
  boundary), so arm (a) applies it before that block's first slice (FR-024 step 4). Arm (b) calls `setVoiceMode(Poly)`
  immediately before the `processBlock` call that renders that same sample, and receives **no other setter call**: the
  processor's per-block pushes of unchanged values are change-only no-ops in the engine (engine.h:25, "Every setter is
  change-only").

  Threshold: **0** sample mismatches on both channels at unity output gain. The processor adds no stereo or nonlinear stage,
  so the Phase 9 mono-sum results (SOLID / SPREAD / WIDE, Anchor L == R) and its SC-021 aliasing results carry over
  unchanged. This SC proves that they do.
- **SC-009 — Block-size invariance and sample-accurate events.** `Profundum_MidiEventTranslation`:
  - **Invariance.** The same 2 s script, with note events at offsets that are not multiples of any partition and no parameter
    change, is rendered at host blocks **{1, 7, 32, 65, 512, 4096}** and each compared with the 512 render. Threshold: **0**
    mismatches per channel. This is the engine's own partition result (`profundum_engine_test.cpp:963–965`), which the
    wrapper must not degrade.
  - **Event order.** A block whose `EventList` holds NoteOn(40) at offset 300 and then NoteOff(40) at offset 100 (list order
    reversed) renders identically to the same events listed in offset order.
  - **Overflow.** Exercises the wrapper's post-1024 path (FR-025) separately from the engine's 256-entry queue
    (engine.h:102, :271–274 drop note-ons; :283–288 fall back to `pendingRelease_` for note-offs). Script, at 48 kHz, POLY,
    polyphony 8:
    1. Block A: NoteOn for the eight notes {28, 33, 40, 45, 52, 57, 64, 69}, held; then 1 s of blocks with no events.
    2. Block B (512 samples, 1100 events). List positions 1–1024: filler `kNoteOffEvent`s on pitches 100–127 (none of them
       sounding), so the sorted range touches no voice. List positions 1025–1100: 76 `kNoteOffEvent`s cycling over the eight
       held pitches, at offsets that **decrease** in list order (`offset = 450 − 5·k` for k = 0…75), so the FR-025
       non-decreasing clamp is exercised.
    3. Then 1.5 s of blocks with no events.

    Thresholds:
    - (a) Every sample over [end of block B + 1 s, + 0.5 s) has `|x| < 1e-6` (−120 dBFS) on both channels. The default
      release is 100 ms (`morph_state.h:75`), so 1 s leaves an order of magnitude of margin.
    - (b) Control: the same script with list positions 1025–1100 removed is **not** silent over the same window: RMS
      ≥ −60 dBFS on each channel. This proves the overflow path carried the releases.
    - `droppedEvents()` is printed and recorded.
- **SC-010 — Denormalisation round-trip.** `Profundum_ParamDenormRoundTrip`: each of the eight IDs is sent the normalized
  values `{0, 0.25, 0.5, 0.75, 1}` through `process()` on an initialised, unprepared processor with `numOutputs = 0`, and read
  back through the test seams. Thresholds:
  - output gain: gain exactly `0.0f` at `v = 0` and exactly `1.0f` at `v = 1`; at `v = 0.25 / 0.5 / 0.75` the gain is within 1e-5 relative of `10^((−60 + 60 v) / 20)` (−45 / −30 / −15 dB); every gain read back is in `[0, 1]`; the display string at `v = 0` is "-inf dB" and at `v = 1` is "0.0 dB";
  - voice mode `{Mono, Mono, Poly, Poly, Poly}`;
  - polyphony exactly `{1, 3, 5, 6, 8}`, from FR-043's `int(v·7 + 1.5)`;
  - ceiling within 1e-5 dB of `-12 + 12v`;
  - macros within 1e-6 of `v`.

  Also:
  - a multi-point queue `{0.1 @ 0, 0.9 @ 100}` stores 0.9;
  - unregistered IDs 4, 99, 104 and 199 change no atomic;
  - a NaN normalized value, built from its bit pattern, changes no atomic;
  - each registered parameter's `getInfo().defaultNormalizedValue` equals the table;
  - the polyphony parameter has `stepCount == 7` and its 8 list entries read "1".."8".
- **SC-011 — State round-trip.** `Profundum_StateRoundTrip`:
  1. Set all eight parameters to non-default values, then `getState` → fresh `setState` → `getState`. The two streams are
     byte-identical and exactly 36 bytes long.
  2. The default stream decodes to unity gain, Mono, polyphony 4, −1 dBTP and macros at 0.5.
  3. A stream with version `kCurrentStateVersion + 1` returns `kResultFalse` and leaves the parameters unchanged.
  4. A stream cut after the global block leaves the macros unchanged.
  5. Hostile streams, each loaded on a processor whose atomics hold known non-default values, apply the FR-045 rules:
     - polyphony 99 → 8; ceiling +20 dB → 0 dB;
     - `outputGain` 4.0 → 1.0;
     - `voiceMode` 7 → the atomic is unchanged;
     - NaN and +Inf, each built from its bit pattern through a volatile (FR-062), in each float field in turn (gain,
       ceiling, each of the four macros) → that atomic is unchanged.

     After the `outputGain` 4.0 load, one prepared `process()` of the SC-006 chord follows, and `max |sample|` stays at or
     below `10^(ceilingDb/20)` on both channels.
  6. `Controller::setComponentState` on stream (1) moves every registered parameter to the streamed value within 1e-6. For output gain the
     streamed linear gain maps to the normalized value of FR-045's inverse taper (stream gain 1.0 → normalized 1.0; gain 0.0 → 0.0;
     gain 10^(−30/20) → 0.5, within 1e-5).

  *(Roadmap line 769: "state round-trip".)*
- **SC-012 — The bus configuration is instrument-shaped.** `Profundum_ProcessorBusSetup`: 1 event input, 0 audio inputs and 1
  stereo output. `setBusArrangements` returns `kResultTrue` for (0, stereo), and `kResultFalse` for (1 in, stereo),
  (0, mono) and (0, 2 × stereo). *(Roadmap line 769: "bus setup".)*
- **SC-013 — The editor lifecycle is free of use-after-free, and the placeholder is bound.** `Profundum_EditorLifecycle`:
  1. `exerciseEditorLifecycle(controller, "editor", PROFUNDUM_RESOURCES_DIR "/editor.uidesc")` completes 3 cycles.
  2. The built frame holds exactly eight `CControl`s whose tags equal the eight IDs, with voice mode and polyphony as
     `COptionMenu`.
  3. `makeProfundumPresetConfig()` reports `pluginName == "Profundum"` and `subcategoryNames == {"Sub"}`. A `PresetManager`
     pointed at a temporary factory directory holding `Sub/Probe.vstpreset`, with its user directory set to a separate empty
     temporary directory, scans exactly 1 preset, with subcategory `"Sub"`. The directory `plugins/profundum/resources/presets/Sub` exists on disk and its name equals `subcategoryNames[0]`.
  4. `profundum_tests.exe "[lifecycle]" --list-tests` lists the case.
  5. Built under the local ASan configuration (`-DENABLE_ASAN=ON`, Debug), the case exits 0 with no AddressSanitizer report.

  *(Roadmap line 769: "editor-lifecycle harness enrollment".)*
- **SC-014 — Latency is reported, and it is real.** `Profundum_ProcessorLifecycle`:
  - On an initialised processor **before any `setupProcessing`**, `getLatencySamples() == 64`, and it equals
    `ProfundumEngine::kLimiterLookaheadSamples`. On a processor before `initialize()` or after `terminate()` (null engine) it is 0.
  - After `setupProcessing` at each of {22 050, 44 100, 48 000, 88 200, 96 000, 192 000} Hz, `getLatencySamples() == 64`, and it
    equals `engineForTest()->latencySamples()` at every one of those rates.
  - The value stays 64 after a second `setupProcessing` at another rate, after `setActive(false)` and `setActive(true)`, and
    after every shipped parameter changes.
  - After a refused rate (SC-017.9) the value is still 64.
  - No `restartComponent` / `IMessage` is issued by the processor for latency in any of the above.
  - Reality check at 48 kHz: after a fresh prepare with NoteOn(28) at sample 0, the index of the first sample with
    `|x| > 0` on either channel lies in **[64, 64 + 4]**. The lower bound proves no signal leaks ahead of the reported
    latency. The upper bound proves no unreported latency: engine.h:443 names the limiter look-ahead as "the only latency in
    the chain", and k = 4 allows only for exact-zero first samples of the onset itself (an envelope that starts at 0 and a
    phase-0 oscillator each can produce one). An onset past 68 is a failure to report, never a widened k. The measured onset
    index is printed and recorded in `compliance.md`.
- **SC-015 — Wrapper CPU overhead and absolute CPU (both gating).** `Profundum_ProcessorCpu`, run through
  `node tools/run-cpu-tests.js profundum_tests` alone on an idle machine. Setup: 48 kHz, 512-sample blocks, best-of-16 × 100
  blocks after a discarded warm-up. Four arms: MONO with one held note, POLY with four held notes, and (**user ruling Q-A,
  2026-10-22**) MONO-MAX / POLY-MAX, the same notes with all four macros at 1.0, the heaviest state the Phase 10 surface
  reaches (Clean↔Violent drives excitation, Fold and Character amount). Each arm times bare
  `proc->process(data)` calls and, back-to-back, the direct chain (`processBlock` plus the gain multiply). In the timed region:
  all buffers are pre-built, the note-ons ride on the warm-up block only, and the timed blocks carry no events or parameter
  changes.
  Gating thresholds, both required:
  - Ratio: the `process()` time is at most **1.05×** the direct time in both arms.
  - Absolute: measured through `Processor::process()`, the MONO and MONO-MAX arms are **≤ 8 %** and the four-voice POLY
    and POLY-MAX arms **≤ 25 %** of one core in real time at 48 kHz (the macros-max arms gated by user ruling Q-A,
    2026-10-22: they are lighter than Phase 9's showcase, which read 7.5798 / 24.005 against the same bars). The ratio alone is not enough: 24.005 % × 1.05 = 25.2 % would pass it while breaking
    the 25 % bar. Roadmap line 684–685 hands Phase 10 this budget, and line 1034 makes CPU budgets FRs.

  Phase 9's 7.5798 % and 24.005 % (phase-handoffs §10.9) stay as the recorded reference, and both arms' percentages are
  printed and recorded. A reading over a bar is a **failure**, reported to the user, never a recorded number; no budget is
  relaxed and no workload is shrunk.
- **SC-016 — Every shipped parameter reaches the engine and changes the sound (audibility theme FR).**
  `Profundum_ParamFlowReachesEngine`:
  1. After one `process()`, each shipped value read back from the engine (`voiceMode()`, `polyphony()`,
     `outputCeilingDb()`, `macros()`) equals the denormalised atomic.
  2. Each control changes a render of its documented setting by more than its floor. Each render is 2 s at 48 kHz with E1
     held (the POLY rows use a four-note chord), compared with the default render:

     | Control | Setting | Floor |
     |---|---|---|
     | output gain | normalized 0.9 (−6 dB, linear 0.501187) | RMS ratio within 0.501 ± 0.01 after the 20 ms ramp |
     | voice mode | Mono → Poly, on a two-note overlap | max-abs difference > 1e-3 |
     | polyphony | 1 vs 4 in Poly, on a four-note chord | RMS difference > 1 dB |
     | ceiling | −12 dBTP, on the SC-006 chord | peak ≤ 0.251189, while the default render's peak is > 0.251189 |
     | each of the four macros | 0 and 1, vs 0.5 | RMS of the difference > −60 dBFS |
     | Clean ↔ Violent at 0 (**D-2, user ruling 2026-10-22**) | 0 vs 0.5 | **recorded, inert by construction**: the default patch is already at the Clean end (excitation mix 0, Fold 0, Character Amount 0; the row's offsets clamp at 0), so the 0 arm has no travel; the 1 arm is gated. The Phase 9 B10-1 form |

     The gain render also proves the taper end points: normalized 0 gives exact 0.0f on every sample after the ramp, and normalized 1.0
     gives a render with 0 mismatches against the default render. The macro floor is a non-vacuity check; the sound-design bars for each macro are Phase 9's AT rows. Every measured value
     is printed with `WARN` and recorded.
  3. Tempo: before any valid tempo, `engineForTest()->tempoBpm() == 120`. With a host `ProcessContext` carrying 90 BPM and
     `kTempoValid`, `engineForTest()->tempoBpm() == 90` after one block. Without `kTempoValid`, it keeps its previous value
     (a following block with a context lacking `kTempoValid` and tempo 140 leaves 90). A null `processContext` leaves it unchanged.
  4. Macros are live and pushed on every `process()`: after a `process()` the engine's `macros()` equals the atomics; changing one
     macro atomic between two consecutive `process()` calls changes `macros()` after the second call. With all four macros at 0.5
     the processor's render has 0 mismatches against an independently constructed default engine (0.5 is the identity; compare
     SC-008).
- **SC-017 — Degenerate shapes and lifecycle.** `Profundum_ProcessorLifecycle`:
  1. Each FR-030 guard 1–6 (numOutputs 0, null outputs, null `channelBuffers32`, 1 channel, numSamples 0, a null channel
     pointer), run on a prepared processor with no sounding note, receives a block whose `EventList` carries NoteOn(28) at
     offset 0. Each returns `kResultOk`. A following valid 512-sample block with **no** events then renders exactly 0.0f on
     every sample of both channels, and the engine read-backs and `droppedEvents()` are unchanged. A guard that dispatched
     the NoteOn before returning would make that block sound.
  2. Guard 7 (not prepared) returns `kResultOk`, and `droppedEvents()` is unchanged.
  3. Before every zero-fill assertion, both output buffers are pre-filled with the sentinel `0.123f`. The not-ready path
     (and `process()` before `setupProcessing()`) then overwrites every sample of both channels with exactly 0.0f and sets
     `silenceFlags == 3`.
  4. Reset (FR-031): hold NoteOn(28) for 0.5 s, then `setActive(false)`, `setActive(true)`, and render one 512-sample block
     with no events. Every sample is exactly 0.0f on both channels (the reset cleared the voices and the limiter delay; a
     missing reset leaves the held note sounding), and `stateFinite()` is true.
  5. Gain snap (FR-028, FR-031): on a never-prepared processor, `setState` loads output gain 0.5, then `setupProcessing`,
     then one block: `outputGainValueForTest() == 0.5f` exactly at the end of the first sample (no ramp from 1.0 or 0.0).
     Set the gain atomic to 0.25, `setActive(false)`, `setActive(true)`, render one block: the value is exactly 0.25f at the
     first sample.
  6. Push in `setupProcessing` (FR-023 step 4): on a never-prepared processor, `setState` with a stream carrying polyphony 2,
     Poly, ceiling −6 dB and macros {0.1, 0.2, 0.3, 0.4}; then `setupProcessing`, with **no** `process()` call:
     `engineForTest()` reads back `polyphony() == 2`, `voiceMode() == Poly`, `outputCeilingDb() == -6.0f` and `macros()`
     equal to the loaded values.
  7. `setActive(true)` runs inside an `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`):
     `getAllocationCount() == 0`.
  9. Unsupported host rate (FR-023 step 2): `setupProcessing` at 384 000 Hz and at 11 025 Hz (and at a NaN rate built from its bit
     pattern) each return `kResultFalse`. After each, on both a never-prepared processor and one previously prepared at 48 kHz,
     a `process()` with NoteOn(28) at offset 0, output buffers pre-filled with the sentinel `0.123f`, returns `kResultOk`, writes
     exactly 0.0f on every sample of both channels and sets `silenceFlags == 3`; `getLatencySamples() == 64`; and
     `engineForTest()->droppedEvents()` is unchanged. A following `setupProcessing` at 48 000 Hz returns `kResultOk` and a
     held note renders non-silent (RMS of SC-005's bar). 22 050 and 192 000 Hz return `kResultOk`.
  8. FR-019: `controller->queryInterface(Steinberg::Vst::IMidiMapping::iid, &obj)` and the same call with
     `INoteExpressionController::iid` each return `kNoInterface`.
- **SC-018 — Registration is complete.**
  1. `node tools/lint-plugin-roster.js` exits 0.
  2. Each unchecked site of FR-007, FR-071 item 2, FR-079 and FR-080 carries its entry, proved by an exact grep with an
     expected count. Each grep's output line is recorded in `compliance.md`:

     | File | Pattern (fixed string unless noted) | Expected |
     |---|---|---|
     | `.github/workflows/ci.yml` | regex `for area in .*profundum` | ≥ 1 |
     | `.github/workflows/ci.yml` | `auval -v aumu Prfd KrAt` | ≥ 1 |
     | `.github/workflows/ci.yml` | `Profundum-Windows-x64`, `Profundum-macOS`, `Profundum-Linux-x64`, `Profundum AUv3.app` | ≥ 1 each |
     | `.gitignore` | `/plugins/profundum/src/version.h`, `/plugins/profundum/resources/win32resource.rc`, `/plugins/profundum/resources/auv3/audiounitconfig.h` (the leading-slash form of the Vorago trio, `.gitignore:79–81`) | 1 each |
     | `tools/run-cpu-tests.js`, `tools/run-close-lanes.js` | `'profundum_tests'` | ≥ 1 each |
     | `tools/ci-test-shard.js` | `['profundum', 'Profundum']` | 1 |
     | root `CLAUDE.md` | `Profundum.vst3` | ≥ 2 (pluginval block and built-plugin list) |
     | root `CLAUDE.md` | `--target profundum_tests` | ≥ 1 |
     | root `CLAUDE.md` | `plugins/profundum/CLAUDE.md` | ≥ 1 (per-directory leaf list) |
     | root `CLAUDE.md` | `\|profundum` | ≥ 1 (clang-tidy `-Target` roster) |
     | root `CLAUDE.md` | `Add Profundum parameter`, `Add Profundum test`, `Change Profundum UI` | 1 each |
     | root `CLAUDE.md` | `**Profundum**` | ≥ 1 (Project Overview list) |

     The `.gitignore` greps prove only that the lines exist; SC-021 proves they are the right paths.
  3. `node tools/gen-specs-index.js --check` and `node tools/gen-repo-map.js --check` pass, and `specs/INDEX.md` has a
     `## Profundum` heading that lists this spec.
  4. `./tools/run-clang-tidy.ps1 -Target profundum -BuildDir build/windows-ninja` reports zero warnings over
     `plugins/profundum/src` and `tests`, and the `all` case of both scripts includes those directories.

  *(Roadmap lines 766–767, 773–774.)*
- **SC-019 — The scope fence holds.** The base is pinned to `285613c5` (the commit this spec was verified against), and the
  check covers the working tree as well as commits, because the build stage does not commit:
  - `git diff --name-only 285613c5` (every tracked change, committed or not), plus
  - `git status --porcelain --untracked-files=all` (untracked new files, which never appear in a commit-range diff).

  Every listed path MUST match this allow-list; any other path is a failure:
  - `plugins/profundum/**`;
  - `specs/profundum-phase10-plugin-scaffold/**`;
  - the registration files of FR-007 and FR-070–FR-080: `CMakeLists.txt` (root), `.gitignore`,
    `.github/workflows/ci.yml`, `.github/workflows/release.yml`, `.github/workflows/valgrind-nightly.yml`,
    `tools/run-clang-tidy.ps1`, `tools/run-clang-tidy.sh`, `tools/check-changelog-coverage.js`, `tools/gen-specs-index.js`,
    `tools/run-cpu-tests.js`, `tools/ci-test-shard.js`, `tools/run-close-lanes.js`,
    `.claude/workflows/release-readiness.js` (FR-080, user ruling Q-B 2026-10-22);
  - the two D-3 rename sites (FR-006, user ruling 2026-10-22): `dsp/include/krate/dsp/core/closed_form_adaa2.h`,
    `dsp/include/krate/dsp/systems/profundum_engine.h`;
  - the generated artifacts `specs/INDEX.md` and `specs/_architecture_/repo-map.json`;
  - root `CLAUDE.md`.

  Excluded by name: `.serena/project.yml`, whose modification predates this phase (it is in the `git status` at
  `285613c5`). No path under `dsp/`, `tests/test_helpers/` or `plugins/<other>/` may appear.
- **SC-020 — The built bundle passes `check-bundle`.** `node tools/check-bundle.js
  build/windows-x64-release/VST3/Release/Profundum.vst3` exits 0 and prints its `OK   Profundum.vst3: editor.uidesc +
  moduleinfo.json present` line and `check-bundle: all 1 bundle(s) OK` (`tools/check-bundle.js:47, :55`). This is the local
  run of the CI check FR-054 depends on. The output is recorded in `compliance.md`.
- **SC-021 — A configure and build leaves the tree clean.** After a configure of `build/windows-x64-release` and a build of
  `Profundum` and `profundum_tests`, `git status --porcelain plugins/profundum` prints nothing: every generated file
  (FR-007) is ignored by the `.gitignore` lines of SC-018.2, and no build output lands in the source tree.
- **SC-022 — Every pre-commit gate is green.** Every entry of `LINTS` (`tools/hooks/guard-ci-gates.js:38–48`) run as
  `node tools/<script>` exits 0, and every entry of `GENERATORS` (`:31–35`: `gen-repo-map.js`, `gen-symbols.js`,
  `gen-specs-index.js`) run with `--check` exits 0. Each command's exit code is recorded in `compliance.md`.

---

## Edge cases

- **RT-safety boundaries.** `prepare` allocates (engine.h:144) and runs only in `setupProcessing()`. `reset()` is a
  control-thread method (engine.h:199) and runs only in `setActive(false)`. A host that calls `process()` between
  `setActive(false)` and the next `setupProcessing()` reaches the prepared engine after a reset, which is legal and silent.
  `setState()` writes atomics only.
- **Out-of-order host calls** (pluginval). `process()` before `setupProcessing()` writes zeros and sets `silenceFlags 3`.
  `setupProcessing()` before `initialize()` or after `terminate()` finds a null engine and stays unprepared.
- **Sample rates outside `[22050, 192000]`.** The engine would clamp the rate (engine.h:151–152) and render off pitch, so Phase 10
  refuses it instead (user ruling 2026-10-22, FR-023 step 2): `setupProcessing` returns `kResultFalse`, the processor stays
  unprepared and `process()` writes silence with `silenceFlags = 3`. A host at 11 025 Hz or 384 kHz therefore gets silence, never a
  wrong-pitch render; pluginval's rates (44.1–192 kHz) are inside the range. A re-prepare at a new supported rate keeps every
  pending setter (engine.h:144–146); SC-014 and SC-017.9 cover it.
- **Event bursts.** If more than 256 note-ons are latched between two 32-sample grid points, the engine queue overflows
  (`kMaxQueuedEvents`, engine.h:102): the extra note-ons are dropped and counted (`droppedEvents()`), and note-offs are never
  dropped. More than 1024 host events in one block follow the FR-025 overflow rule. SC-009 exercises both.
- **Velocity.** The engine does not use velocity (phase-handoffs §11.5). FR-026's floor of 1 stops a quiet note from becoming
  a note-off.
- **Parameter extremes.**
  - Output gain at normalized 0 (−∞ dB) gives exact silence after the ramp; at normalized just above 0 it is −60 dB. The limiter's latency still holds 64 samples of signal.
  - A ceiling of −12 dBTP is bounded by SC-006.
  - A polyphony change while notes sound is applied at the engine's next grid point, where the allocator is reconfigured
    (engine.h:630–634). No click budget is gated here; Phase 9 SC-003 owns voice-steal clicks.
  - A voice-mode switch with held notes is engine-owned and applied at the next grid point.
- **Non-finite input.** A NaN or Inf normalized value, or one inside a loaded state, never reaches an atomic (FR-043, FR-045; SC-010, SC-011.5): it keeps the previous value.
  The engine rejects a non-finite `processContext->tempo` (engine.h:299–303).
- **Seed determinism.** The engine seed stays at `0xB455` for all of Phase 10. Two processor instances rendering the same
  script produce 0 mismatches; SC-008 covers this by comparing against an independently constructed engine.
- **Block size 1 and very large blocks.** The engine accepts any block size and splits it on its grid (engine.h:422–440). Unlike
  Vorago, with its 2048-sample bound, the wrapper does not sub-divide. SC-009 covers sizes 1 and 4096.
- **Latency before prepare.** It is 64 at all times while the engine exists: `getLatencySamples()` returns the constant
  `kLimiterLookaheadSamples` (FR-032), although the limiter itself copies its look-ahead only at prepare
  (`true_peak_limiter.h:76, :280`). A host that caches latency before `setupProcessing()` therefore sees the right value and no
  `kLatencyChanged` is needed (SC-014).
- **Denormals.** `test_main.cpp` enables FTZ/DAZ on the test thread, which hides a missing `ScopedDenormalMode`. FR-024 step 1
  is therefore verified by inspection: `grep -n "ScopedDenormalMode" plugins/profundum/src/processor/processor.cpp` shows it
  as the first statement of `process()`, cited in `compliance.md`.

---

## Open questions

Phase 10's text (lines 753–774) is fully determined, and OQ-2 and OQ-10 are assigned to Phases 8, 11 and 12. OQ-8 (the
registered polyphony ceiling, roadmap line 1063) is assigned to Phase 9 / 11, not to this phase. Phase 9 settled
`kMaxVoices = 8`, but registering the range here is a decision this spec makes ahead of the phase the roadmap names (item 6).

The decisions below were made here and are recorded in the Conventions table. Each one fixes a permanent surface, so the
clarify scan should put them to the user before the plan:

1. Output gain as a **post-limiter, attenuation-only, dB-tapered (−60 .. 0 dB)** parameter (ruled 2026-10-22). Alternatives: no gain parameter in Phase 10, or an
   engine-side pre-limiter trim, which would be a `dsp/` change and is out of scope.
2. The four macros ship **live** in Phase 10. The alternative is the Vorago precedent: inert until Phase 11.
3. The permanent seed preset category name **`Sub`**.
4. Host-tempo forwarding in Phase 10. The alternative is leaving it to Phase 11, whose roadmap text does not mention it.
5. Mass and Morph position are **not** registered in Phase 10, because they need the Phase 11 endpoint model.
6. Polyphony (range 1–8, default 4) is registered in Phase 10, not Phase 11. Roadmap OQ-8 (line 1063) assigns the registered
   ceiling to Phase 9 / 11, and the "register it ONCE" handoff is §11.3, a Phase 11 handoff. Because a range change at a
   shipped ID is forbidden later, this fixes a permanent surface one phase early. The alternative is to defer
   `kPolyphonyId` to Phase 11 and ship Phase 10 at the engine's default polyphony.

---

## Review notes

- Review round 1 (2026-10-22): every issue was applied; none was rejected. SC-015 now gates the absolute Phase 9 bars
  (MONO ≤ 8 %, 4 voices ≤ 25 %) as well as the 1.05× ratio, because 24.005 % × 1.05 = 25.2 % would otherwise pass over the
  bar (roadmap lines 684–685, 1034). FR-023 step 4 is kept and given an observable effect (SC-017.6) rather than dropped,
  because a `setState()` before `setupProcessing()` is a real host order and the engine read-backs are observable without a
  `process()` call.

---

## Clarifications

### Session 2026-10-22

- Q1: What permanent surface does `kOutputGainId` (ID 0) register? → Option (b): a dB-tapered attenuation AFTER the limiter, range −60 to 0 dB, −∞ (gain exactly 0) at normalized 0, unity (0 dB) at normalized 1.0, default unity; the ceiling holds because the gain never exceeds unity. The plain value in the atomic and the state stream is the linear gain in [0, 1]. [FR-028, FR-043, FR-045, FR-047, FR-009, SC-006, SC-010, SC-011, SC-016]
- Q2: Is `kPolyphonyId` registered now, and with what range? → Option (a): list "1".."8" now, default 4 (engine `kMaxVoices` = 8); permanent, a later `kMaxVoices` raise needs a new ID. [FR-009, FR-043, FR-047, SC-010]
- Q3: Are the four Personality macros (IDs 100–103) live? → Option (a): live in Phase 10, pushed through `ProfundumEngine::setMacros` on every `process()`; neutral 0.5 is the identity; each macro must move the render, nothing ships inert. [FR-024, FR-041, FR-009, SC-016]
- Q4: What is the single seed preset category? → Option (a): `Sub`; directory `resources/presets/Sub` and `PresetManagerConfig.subcategoryNames` must match; Phase 13 keeps it and may only add categories. [FR-009, FR-050, FR-051, SC-013]
- Q5: What does `getLatencySamples()` return before the first `setupProcessing()`? → Option (b): the constant `ProfundumEngine::kLimiterLookaheadSamples` (64) whenever the engine exists, regardless of prepare; before-prepare equals the post-prepare engine read-back at every rate; no `kLatencyChanged` message in Phase 10. [FR-032, FR-009, SC-014]
- Q6: What happens for a host sample rate outside [22050, 192000]? → Option (b): REFUSED. `setupProcessing` returns `kResultFalse`, the processor stays unprepared, `process()` outputs silence with `silenceFlags` set on both channels instead of a clamped, off-pitch render; tests cover 384 kHz and a rate below 22050. [FR-023, FR-030, FR-032, FR-009, SC-014, SC-017]
- Q7: Does Phase 10 forward the host tempo? → Option (a): once per `process()` through `setTempoBpm` when `ProcessContext::kTempoValid` is set; otherwise the engine default 120 BPM stands. [FR-027, FR-009, SC-016]
- Q-A (plan P-1): Is the SC-015 macros-max arm (all four macros at 1.0) gated or only recorded? → Gated: MONO-MAX ≤ 8 % and POLY-MAX ≤ 25 % at the Phase 9 bars, beside the default-state arms and the 1.05× ratio; the arms are lighter than Phase 9's showcase (7.5798 / 24.005). No bar moves. [SC-015, FR-067]
- Q-B (plan P-6): Is `profundum` added to `.claude/workflows/release-readiness.js` PLUGIN_MAP in Phase 10? → Yes, now (the day-one registration checklist covers every roster site); the file joins the SC-019 allow-list. [FR-080, SC-019]
- D-3 (build, comply): the Profundum target compiled with C4459 (closed_form_adaa2.h:124, a function-local `kTwoPi` hiding the global) and C4456 (profundum_engine.h:991, a local `r` hiding an earlier local), both only fixable in `dsp/`. → User ruling: FR-090 exception for the two renames (`kTwoPiD`, `rc`), bit-identical, gated by dsp_processors_tests / dsp_systems_tests / dsp_effects_tests per-push plus profundum_tests; no warning suppression. [FR-006, FR-090, SC-001, SC-019; tasks D-3]
- D-2 (build, G12 gate): SC-016.2's Clean ↔ Violent row at 0 rendered bit-identical to the default (RMS of the difference −200 dBFS): the default `MorphState` sits at the Clean end (excitation mix 0, `foldAmount` 0, `characterAmount` 0) and the macro's offsets clamp at 0, so only `excitation.drive` moves, which is inert at mix 0 — inert by construction, the Phase 9 B10-1 situation. → User ruling: gate the 1 arm, record the 0 arm with this attribution; no `dsp/` change, the shipped default sound stays. [SC-016, FR-041; tasks T016 / D-2]
- D-1 (build, G3 gate): the P-5 probe's "exact zero within 1 s of a note-off" clause went red at 1.011 s. Hidden-probe attribution: the voices are exactly silent 0.100 s after the note-off; the remaining tail is the global 5 Hz DC blocker's own decay (τ 31.8 ms) of the last voice sample, flushed to exact zero at 1.011 s plus the 64-sample look-ahead (Phase 9 read the same tail at 1.3 s). → User ruling: refine the reading, no `dsp/` change — Sum tap exactly zero by 0.125 s, \|out\| ≤ 1e-6 from 0.5 s, exact zero from 1.25 s after the note-off. No FR / SC rests on the 1 s premise (SC-017.1 never sounds a voice; SC-017.4 goes through reset). [plan P-5, tasks T006 / D-1]
