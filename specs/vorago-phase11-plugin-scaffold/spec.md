# Feature Specification: Vorago Phase 11 — Plugin Scaffold

**Spec slug:** `vorago-phase11-plugin-scaffold`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B (lines 519–522), Phase 11 (lines 524–542); Open Questions 1 and 7 (lines 620–634)
**Depends on:** Phase 10 (`VoragoEngine` / `VoragoVoice` / `VoragoMacroMatrix`) ✅, Phase 10a (ghost extension, ships inert) ✅, Phase 9 (`CavernVerb`) ✅
**Template:** `specs/seraphis-phase8-plugin-scaffold/spec.md` — roadmap line 521: *"Follows the Seraphis Part B template nearly verbatim — those phases were specified against the same repo infrastructure and their checklists apply directly."*
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-09-24

---

## Overview

Phase 11 turns the ten finished Vorago KrateDSP phases into a real VST3 plugin. It creates
`plugins/vorago/` in Ruinae's shape (a polyphonic instrument with `parameters/` packs, an engine-config
layer and a preset manager; roadmap line 528) with Membrum/Gradus's instrument bus and AU configuration
(event input, stereo output, no audio input, `kSupportedNumChannels 02`; roadmap lines 529–530). The
processor drives the Phase 10 composed chain exactly as the shipped chain test drives it —
`VoragoEngine::processStereoBlock` → `CavernVerb::processStereoBlock` (in place) →
`VoragoEngine::processOutputStage` (`dsp/tests/unit/effects/vorago_composed_chain_test.cpp:243–245`).
It registers two parameter packs (global and the twelve macros, which stay inert), ships a
placeholder `editor.uidesc`, stands up a `vorago_tests` Catch2 target, and adds the plugin to
**every** CI, tooling and documentation roster outside `plugins/` in the same change (roadmap lines
534–536: *"every item, day one"*).

What you can hear is small: a held MIDI note produces non-silent, bounded stereo audio through the full
Phase 1–10a chain. The main work is the infrastructure. Every roster entry is a separate place CI can
fail, and a missing entry makes CI pass while silently skipping the plugin.

**No new DSP is written in this phase and no file under `dsp/` is modified.** No Seraphis file is
modified either.

---

## Scope

**In scope**

1. `plugins/vorago/` directory skeleton, `CMakeLists.txt`, `version.json`, `CHANGELOG.md`, `README.md`,
   `CLAUDE.md` leaf, `docs/`, `installers/`.
2. Per-plugin identity: two fresh FUIDs, AU `aumu` / `Vrgo` / `KrAt`, bundle base
   `com.krateaudio.vorago`, the bus layout, the reserved parameter-ID map, `kCurrentStateVersion = 1`,
   one seed preset category (roadmap lines 531–533; OQ-1 decided *"Vorago is final — subtype `Vrgo`,
   `plugins/vorago/`, `vorago_tests`, bundle id derived from it"*, line 621).
3. `Processor` + `Controller` (2–3 TUs each) owning and driving `VoragoEngine`, `CavernVerb` and
   `VoragoMacroMatrix`.
4. Two parameter packs: `parameters/global_params.h` and `parameters/macro_params.h` (twelve macros,
   inert). All other packs are Phase 12 (roadmap lines 548–550).
5. `engine/vorago_engine_config.h` (thin prepare-time config: roadmap line 529 "engine-config layer"),
   `preset/vorago_preset_config.h`, `update/vorago_update_config.h`.
6. `resources/`: placeholder `editor.uidesc`, `au-info.plist`, `auv3/audiounitconfig.h.in`,
   `auv3/macOS/Vorago.entitlements`, one seed preset category directory.
7. `vorago_tests` with the day-one coverage roadmap line 537–538 names (bus setup, denorm round-trip,
   state round-trip, non-silent render, editor-lifecycle enrollment), plus the audio-path behaviours
   the processor must not get wrong (event timing, degenerate shapes, latency, allocation).
8. The full Seraphis Phase 8.5 registration checklist (roadmap lines 534–536; Seraphis roadmap 8.5):
   root CMake, `ci.yml`, `release.yml`, `valgrind-nightly.yml`, both clang-tidy scripts,
   `check-changelog-coverage.js`, `gen-specs-index.js`, `.gitignore`, root `CLAUDE.md` rosters and the
   new leaf — each verified by a lint or a named check, not by eye.

**Non-goals, owned by later phases**

| Deferred to | What |
|---|---|
| Phase 12 (`vorago-phase12-parameters`) | Every engine parameter beyond global + macros (the `cloud`, `noise`, `resonance`, `ecology`, `sub`, `smear`, `events`, `ecosystem`, `body`, `space` packs, roadmap line 549); wiring the macros into `VoragoMacroMatrix` (line 548 *"concept-macro system wired"*); state versioning beyond v1; a seed parameter; the Phase 10 SC-008 Gravity/Pressure/Mass product decision (roadmap line 452–455) |
| Phase 13 (`vorago-phase13-ui`) | The real `editor.uidesc`, the ecosystem view, DataExchange piggyback, custom views. `src/ui/` stays empty here |
| Phase 14 (`vorago-phase14-presets-release`) | The final fixed preset category set, factory presets, long-render preset sweep, release gate, `docs/index.html` |

Non-goals within Phase 11:

- No Ruinae-style many-TU processor/controller split. Seraphis roadmap 8.1 sets the day-one shape at
  2–3 TUs each; more are added only if a file passes ~1500 lines.
- No preset browser UI, no preset generator tool, no `.vstpreset` files.
- No modification of any file under `dsp/` or `plugins/seraphis/`.
- No `INoteExpressionController`, no `IMidiMapping` (OQ-7, resolved: deferred to Phase 12).

---

## Existing components (verified this session)

Every signature below was read from the file this session, on branch
`feat/vorago-phase1-events-modulation` at `ddd3c476`.

### DSP the processor drives

| Component | Header | What Phase 11 reuses (verified signature) |
|---|---|---|
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h:163` | `void prepare(double sampleRate, const VoragoEngineConfig& cfg) noexcept` (:277, *"The ONLY allocating path; NOT real-time safe"*, may be called repeatedly and keeps polyphony, seed and setter values, :270–276); `void reset() noexcept` (:427); `void silence() noexcept` (:447, *"NOT AN AUDIO-THREAD OPERATION"*); `bool isPrepared() const noexcept` (:458); `void setPolyphony(std::size_t n) noexcept` (:507, clamps to `[1, kMaxVoices]`, allocates nothing, a shrink is a musical release); `std::size_t getPolyphony() const noexcept` (:523); `void setSeed(std::uint32_t) noexcept` (:544); `std::uint32_t getSeed() const noexcept` (:552); `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` (:564; velocity `0` → `noteOff`, :568–571; no-op when unprepared); `void noteOff(std::uint8_t note) noexcept` (:601); `void setOutputSaturation(float) noexcept` (:850); `void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept` (:891; null pointer → nothing written, `n == 0` → nothing advances, unprepared → zeros); `void processOutputStage(float* l, float* r, std::size_t n) noexcept` (:1006; saturators then `limiter_.processBlock`, limiter always last); `std::size_t getLatencySamples() const noexcept` (:1031, returns only the smear's latency; the cavern's belongs to the caller, :1024–1030); `getActiveVoiceCount` (:1040); `getNonFiniteRecoveryCount` (:1081) |
| `VoragoEngine` constants | same header | `kMaxVoices = 6` (:175), `kDefaultPolyphony = 4` (:178), `kControlChunkSamples = 64` (:181), `kMaxBlockSamples = 2048` (:182), `kOutputSaturation = 0.12f` (:201), `kOutputCeilingDb = -0.3f` (:203, installed by `limiter_.setCeilingDb` at :381), `kEngineSizeBound = kMaxVoices * VoragoVoice::kVoiceSizeBound + 64 KiB` (:231) with `kVoiceSizeBound = 123840` (`vorago_voice.h:424`) ⇒ ≤ 808 576 B, asserted at :1609. The banner (:155–158) says: *"EVERY TEST AND EVERY CALLER HEAP-ALLOCATES THIS OBJECT"*. The engine has no gain setter at all (the public setter list at :507–858 contains none) |
| `VoragoEngineConfig` | same header, :105–156 | `std::size_t maxBlockSamples = 2048; VoragoVoiceConfig voice{}; float atmosCaptureSeconds = 20.0f; bool atmosBlurEnabled = true; bool atmosFreezeEnabled = false; std::size_t atmosBlurFftSize = 1024; std::size_t atmosFreezeFftSize = 2048; float atmosGhostReverseProbability = 0.0f; bool atmosGhostEventTriggers = false; bool smearEnabled = true; std::size_t smearFftSize = 2048;`. It has **no polyphony field**: polyphony is set only through `setPolyphony` |
| `VoragoMacroMatrix` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h:245` | `void setMacro(VoragoMacro, float) noexcept` (:832, clamps to [0,1] and rejects non-finite values); `void setMacros(const VoragoMacroValues&) noexcept` (:915); `void apply(VoragoEngine&) const noexcept` (:954; writes nothing to an unprepared engine, writes the Engine- and Voice-owned rows, **including `setOutputSaturation`** at :968); `VoragoCavernTargets computeCavernTargets() const noexcept` (:1026, pure). `kNumMacros == 12` (:251, `static_assert` :1114) |
| `VoragoMacro` / `VoragoMacroValues` | same header, :95–109 / :198–211 | Order: `Darkness, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass`. Defaults `0.0f` for all except `gravity = 0.5f` (bipolar). A default-constructed matrix is already at the FR-066 identity (:193–197) |
| `VoragoCavernTargets` | same header, :182–190 | `size 0.50f, darkness 0.80f, decaySeconds 20.0f, fog 0.30f, damperDepth 0.35f, mix 1.00f, width 1.00f`. These are the `CavernVerb` defaults, cross-checked against the real constants by `VoragoComposed_CavernDefaultsMatch` (`vorago_composed_chain_test.cpp:24–31`) |
| `OutputSaturation` macro row | same header, :476–481 | `{Pressure, Engine, OutputSaturation, base 0.12f, amount 0.35f, Linear}`. **Every `apply()` call rewrites the engine's output saturation.** This is why FR-041 ships no soft-limit parameter |
| `CavernVerb` | `dsp/include/krate/dsp/effects/cavern_verb.h:193` | `struct PrepareConfig { std::size_t numChannels = 8; std::size_t maxBlockSamples = 2048; float maxEarlySeconds = 0.30f; float maxDelaySeconds = 0.50f; bool spectralDiffusionEnabled = true; std::size_t diffusionFftSize = 1024; std::uint32_t seed = 1; }` (:300–322); `void prepare(double sampleRate, const PrepareConfig&) noexcept` (:358, the only allocating method); `void reset() noexcept` (:487, *"Allocation-free, but NOT an audio-thread operation"*); `void processStereoBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t n) noexcept` (:568; unprepared → zeros; safe to call with input and output aliased, per the chain test banner :33–38); setters `setSize` (:613), `setDarkness` (:619), `setDecaySeconds` (:626), `setFog` (:652), `setDamperDepth` (:717), `setWidth` (:741), `setMix` (:748), `setSeed` (:758); `std::size_t getLatencySamples() const noexcept` (:786) → owned `AetherReverb`'s. **No getter for any control** (the read surface at :766–801 has none) |
| `AetherReverb::getLatencySamples` | `dsp/include/krate/dsp/effects/aether_reverb.h:2717–2719` | `spectralEnabled_ ? diffusionFftSize_ : 0` with **member defaults** `diffusionFftSize_ = 1024` (:4672), `spectralEnabled_ = true` (:4674), so a `CavernVerb` reports **1024 even before prepare** |
| `SpectralSmear::getLatencySamples` | `dsp/include/krate/dsp/processors/spectral_smear.h:472` | `(prepared_ && enabled_) ? fftSize_ : 0`; `fftSize_ = bit_floor(clamp(fftSize, 512, 4096))` (:200, :84–85). With `smearFftSize = 2048` the engine reports **0 before prepare and 2048 after** |
| Composed-chain reference | `dsp/tests/unit/effects/vorago_composed_chain_test.cpp` | The three calls, in this order (:243–245 and :575–577); `pushCavernTargets(CavernVerb&, const VoragoCavernTargets&, bool)` (:188), which pushes the seven targets through `setSize/setDarkness/setDecaySeconds/setFog/setDamperDepth/setMix/setWidth`; `prepareCavern` (:303–313), which uses the `PrepareConfig` defaults apart from block size and seed |
| Envelope defaults | `dsp/include/krate/dsp/systems/vorago_voice.h:318–324` | `kDefaultStageTimesMs{20000, 30000, 45000, 60000, 0, 0}`, `kDefaultReleaseMs = 45000.0f`. **A note takes 20 s to reach full level and 45 s to fade after note-off.** This shapes SC-005/SC-006/SC-026 |
| Test fixtures namespace | `tests/test_helpers/vorago_fixtures.h:73` | `namespace Krate::DSP::TestUtils::Vorago`. It does not clash with a top-level `Vorago` namespace, but see the edge case on `using namespace` |

### Plugin template (Seraphis, the shipped Part B model; Ruinae shape)

| Item | File | Verified shape |
|---|---|---|
| Plugin CMake | `plugins/seraphis/CMakeLists.txt` | `krate_plugin_read_version(SERAPHIS)` + `krate_plugin_configure_generated_files()` (:10–11); `smtg_add_vst3plugin` with enumerated sources (:18–67); links `sdk vstgui_support KrateDSP KratePluginsShared` (:70–76); `smtg_target_configure_version_file` (:85); `krate_plugin_platform_setup(... TAG SERAPHIS BUNDLE_BASE com.krateaudio.seraphis ENTITLEMENTS Seraphis.entitlements KIND instrument)` (:90–95); `smtg_target_add_plugin_resources(... resources/editor.uidesc)` (:100–103); `krate_plugin_install_to_system` (:108), `krate_plugin_install_presets` (:113), `krate_plugin_set_warnings` (:118); **the MSVC `/wd4459` suppression and its justification** (:120–135); `add_subdirectory(tests)` under `VSTWORK_BUILD_TESTS` (:140–142) |
| C4459 source | `dsp/include/krate/dsp/systems/timevar_comb_bank.h:931` | `constexpr float kPi = std::numbers::pi_v<float>;`, still present. It shadows `Krate::DSP::kPi` and reaches every target that includes `continuous_body.h`, which `vorago_voice.h:137` does |
| `version.json` | `plugins/seraphis/version.json` | keys `version, name, description, publisher, url, copyright` |
| AU config | `plugins/seraphis/resources/auv3/audiounitconfig.h.in:1–40` | `kAUcomponentType 'aumu'` (:8) / `aumu` (:9), `kAUcomponentSubType 'Srph'` (:12) / `Srph` (:13), `kAUcomponentManufacturer 'KrAt'` (:16) / `KrAt` (:17), `kAUcomponentDescription AUv3WrapperExtension` (:19), `kAUcomponentName Krate Audio: Seraphis` (:20), `kAUcomponentTag Synthesizer` (:21), `kAUcomponentVersion @AU_COMPONENT_VERSION@` (:24), digit-pair comment (:26–35), `kSupportedNumChannels 02` (:36), flags + delegate (:38–40) |
| AU plist | `plugins/seraphis/resources/au-info.plist:23–51` | one `AudioComponents` dict (`AUWrapperFactory`, `KrAt`, `Srph`, `aumu`), one `SupportedNumChannels` dict `Inputs 0 / Outputs 2` |
| IDs header | `plugins/seraphis/src/plugin_ids.h` | `namespace Seraphis` (:15); `kCurrentStateVersion` (:28); `static const Steinberg::FUID kProcessorUID(...)` (:36), `kControllerUID(...)` (:40); `static const char* const kSubCategories = "Instrument\|Synth";` (:49) with the GCC-13 `-Wunused-variable` rationale (:43–48); reserved-map comment (:57–77); `enum ParameterIDs : Steinberg::Vst::ParamID` (:78) |
| Bus shape | `plugins/seraphis/src/processor/processor.cpp` | `addEventInput(STR16("Event In"))` (:560), `addAudioOutput(STR16("Main Out"), Vst::SpeakerArr::kStereo)` (:561), no `addAudioInput`; heap engine/reverb in `initialize()` (:564–565); `terminate()` assigns `nullptr` (:570–578); `setBusArrangements` rejects `numIns != 0`, `numOuts != 1`, non-stereo (:594–608). Model origin `plugins/membrum/src/processor/processor.cpp:117, 120`; anti-model `plugins/ruinae/src/processor/processor.cpp:56` (`addAudioInput`) |
| `process()` guards | same file, :1294–1356 | `ScopedDenormalMode` at the top (:1298); parameter changes latched before the shape guards (:1310); guard order `numOutputs/outputs` → `channelBuffers32` → `numChannels < 2` → `numSamples` → channel pointers → readiness, with zero-fill + `silenceFlags = 3` on the not-ready path (:1325–1356); `silenceFlags = 0` on a rendered block (:1772) |
| Parameter-pack contract | `plugins/seraphis/src/parameters/global_params.h` | `struct GlobalParams` of `std::atomic<>` (:40); `handleGlobalParamChange` (:85), `registerGlobalParams` (:124), `formatGlobalParam` (:169), `saveGlobalParams` (:211), `loadGlobalParams` (:218, EOF-safe), `template<typename SetParamFunc> loadGlobalParamsToController` (:240–241) |
| Macro pack | `plugins/seraphis/src/parameters/macro_params.h` | `struct MacroParams` with explicit per-field initializers (:50–55) and the same six-function shape (:62, :83, :103, :123, :132, :153–154) |
| Engine-config pattern | `plugins/seraphis/src/engine/seraphis_engine_config.h` | `inline constexpr float kMasterGainSmoothMs = 20.0f;` (:38); `[[nodiscard]] inline ... makeSeraphisEngineConfig(...)` (:46); `makeSeraphisReverbConfig(...)` (:67); `inline void applyAetherTargets(AetherReverb&, const SeraphisAetherTargets&) noexcept` (:96) |
| Factory | `plugins/seraphis/src/entry.cpp:47–85` | `#define stringPluginName "Seraphis"` (:47), `BEGIN_FACTORY_DEF` (:49), two `DEF_CLASS2` (:58, :73), `END_FACTORY` (:85) |
| Controller services | `plugins/seraphis/src/controller/controller.cpp` | `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(...)` in `initialize()` (:157–158), no `UpdateChecker` (:157 comment); `createView` → `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (:434–436) |
| Preset / update configs | `plugins/shared/src/preset/preset_manager_config.h:19–24`; `plugins/seraphis/src/update/seraphis_update_config.h:21–27` | `struct PresetManagerConfig { FUID processorUID; std::string pluginName; std::string pluginCategoryDesc; std::vector<std::string> subcategoryNames; }`, where field order matters for designated initializers (:16–18); `UpdateCheckerConfig{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }` |
| Dropdown helper | `plugins/shared/src/ui/parameter_helpers.h:47` | `createDropdownParameterWithDefault(title, id, defaultIndex, {…})` |
| Test target | `plugins/seraphis/tests/CMakeLists.txt` | `add_executable(seraphis_tests …)` (:5); SDK sources `memorystream.cpp` (:81), `hostclasses.cpp` (:84), `pluginterfacesupport.cpp` (:85), `vstgui_test_stubs.cpp` (:88), `moduleinit.cpp` (:89), `pluginfactory.cpp` (:90); `sdk` linked after `vstgui_support` (:93–94); `SERAPHIS_RESOURCES_DIR` (:124); `-fno-fast-math -fno-finite-math-only` (:186); `catch_discover_tests(seraphis_tests REPORTER console)` (:191) |
| Test main | `plugins/seraphis/tests/unit/test_main.cpp` | the **only** `#include <allocation_operator_overrides.h>` in the binary (:15); `void* moduleHandle = nullptr;` (:24); `enableFTZDAZ();` (:27) |
| Lifecycle tag | `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp:140` | `TEST_CASE("Seraphis_EditorLifecycle", "[seraphis][controller][ui][lifecycle]")` |

### Shared test infrastructure

| Helper | File | Verified |
|---|---|---|
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h:102` | `inline void exerciseEditorLifecycle(Steinberg::Vst::EditController& controller, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)`. Its only assertion is `getNbViews() > 0` (:128), which a one-label template passes |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h` | `kSampleTolerance = 5.0e-4f` (:58), `kMetricTolerance = 2.5e-4` (:61), `fingerprintRender(std::span<const float>)` (:73), `compareFingerprints(...)` (:122) |
| Allocation detection | `tests/test_helpers/allocation_detector.h` | `class AllocationDetector` (:48) with `getAllocationCount()` (:70); `class AllocationScope` (:111). `allocation_operator_overrides.h:4–7`: *"Include this header from EXACTLY ONE translation unit per test binary"* |
| Host doubles | `tests/test_helpers/vst_param_changes.h:31–78`, `tests/test_helpers/vst_event_list.h:34–36` | `Krate::Test::ParamValueQueue`, `Krate::Test::ParameterChanges` (single point per parameter, per the banner :16–19), `Krate::Test::EventList` |

### Tooling and registration sites

| Site | File | Verified current state |
|---|---|---|
| Root CMake | `CMakeLists.txt:488–494` | seven `add_subdirectory(plugins/<p>)` lines ending `seraphis` |
| Generated-file ignores | `.gitignore:58–78` | three lines per plugin, Seraphis at :76–78 |
| Roster lint | `tools/lint-plugin-roster.js` | derives the roster from `readdir(plugins/)` minus `shared` and checks: root CMake; `ci.yml` detect-changes outputs, paths-filter, `for p in` loop, `$GITHUB_OUTPUT` echoes, three `hashFiles` keys, nine `for plugin_info in \` blocks + `case` arms; `release.yml` choice list + `hashFiles`; `valgrind-nightly.yml` build + run lists; `run-clang-tidy.ps1` `ValidateSet` / case / `all`; `run-clang-tidy.sh` case / `all` / usage; `check-changelog-coverage.js` `PLUGINS`. It does **not** check `ci.yml`'s artifact uploads, `auval` steps or AUv3 verification steps, `.gitignore`, `gen-specs-index.js` or `CLAUDE.md` |
| Gate hook | `tools/hooks/guard-ci-gates.js` | `GENERATORS` = `gen-repo-map.js`, `gen-symbols.js`, `gen-specs-index.js` (:31–34); `LINTS` = nine scripts incl. `lint-plugin-roster.js` (:38–47) |
| `ci.yml` non-lint sites (Seraphis model) | `.github/workflows/ci.yml` | Windows artifact upload (:474–479); macOS `auval` step (:764–773, `auval -v aumu Srph KrAt`); macOS AUv3 bundle verification (:824–830); macOS artifact upload (:903–911); Linux artifact upload (:1176–1181). `grep -ci seraphis ci.yml` → **51** |
| `release.yml` | `.github/workflows/release.yml:41, :138` | `- seraphis` choice; `'plugins/seraphis/CMakeLists.txt'` in `hashFiles` |
| `valgrind-nightly.yml` | `:276`, `:283` | `seraphis_tests` in the build list and the `for bin in` run list, which filters by `'[lifecycle]'` |
| clang-tidy | `tools/run-clang-tidy.ps1:60, :196–207, :221/:229`; `tools/run-clang-tidy.sh:63, :151–152, :167` | per-plugin cases + `all` + usage text |
| Changelog coverage | `tools/check-changelog-coverage.js:50` | `const PLUGINS = [..., 'seraphis'];` |
| Specs index | `tools/gen-specs-index.js:19–37` | the first keyword that matches wins. **There is no `vorago` entry**, so `vorago-phase4-spectral-smear` is currently filed under "DSP / Spectral" (`specs/INDEX.md:91`) and the other Vorago specs under "Other" (`specs/INDEX.md:164–172`) |
| CPU-test runner | `tools/run-cpu-tests.js:45–50` | `DEFAULT_TARGETS` is a hard-coded list ending `'membrum_tests', 'seraphis_tests'`; not derived from `plugins/` and not checked by the roster lint (FR-081) |
| Bundle guard | `tools/check-bundle.js:28` | prints `` `OK   ${name}: editor.uidesc + moduleinfo.json present` `` |

---

## New components

Phase 11 adds **no DSP classes**. Its new C++ types are plugin-local, in a new top-level `Vorago`
namespace. The new free functions live in the same namespace.

**ODR sweep run this session** (`grep -rn --include=*.h --include=*.cpp "<name>\b" dsp/ plugins/ tests/`):

| New type / symbol | Layer | Header | ODR sweep result |
|---|---|---|---|
| `namespace Vorago` | plugin | `plugins/vorago/src/plugin_ids.h` | `grep -rn "namespace Vorago" dsp/ plugins/ tests/` → 4 hits, **none a conflict**: `tests/test_helpers/vorago_fixtures.h:73` (`namespace Vorago {`, i.e. `Krate::DSP::TestUtils::Vorago`, nested three levels deep) and its closing comment at `:765`; `dsp/tests/unit/systems/atmosphere_ghost_fixtures.h:66` (`namespace VoragoGhostFix {`, a different identifier that the unanchored pattern also matches) and its closing comment at `:612`. The anchored form `grep -rn "^namespace Vorago \?{" dsp/ plugins/ tests/` → 1 hit, `vorago_fixtures.h:73`. There is no top-level `Vorago`. CLEAR (see the `using namespace` edge case). |
| `Vorago::Processor` | plugin | `src/processor/processor.h` | `class Processor` → 7 definitions, each in its own plugin namespace (disrumpo :43, gradus :34, innexus :58, iterum :51, membrum :38, ruinae :124, seraphis :338). CLEAR. |
| `Vorago::Controller` | plugin | `src/controller/controller.h` | `class Controller` → 7 definitions (disrumpo :60, gradus :44, innexus :56, iterum :40, membrum :42, ruinae :69, seraphis :94) + forward declarations (`iterum/src/controller/custom_views.h:21`, `seraphis/src/ui/cloud_view.h:45`, `seraphis/src/ui/edit_sub_controller.h:46`). CLEAR. |
| `Vorago::GlobalParams` | plugin | `src/parameters/global_params.h` | `struct GlobalParams` → `ruinae/.../global_params.h:26` (`Ruinae`), `seraphis/.../global_params.h:40` (`Seraphis`). CLEAR. |
| `Vorago::MacroParams` | plugin | `src/parameters/macro_params.h` | `struct MacroParams` → `ruinae/.../macro_params.h:13`, `seraphis/.../macro_params.h:50`. CLEAR. |
| `makeVoragoEngineConfig`, `makeVoragoCavernConfig`, `applyCavernTargets`, `kMasterGainSmoothMs` | plugin | `src/engine/vorago_engine_config.h` | first three → **0 hits**. `kMasterGainSmoothMs` → `seraphis/src/engine/seraphis_engine_config.h:38` in `namespace Seraphis`. CLEAR. No new type: the functions return the DSP-owned `VoragoEngineConfig` / `CavernVerb::PrepareConfig`. |
| `makeVoragoPresetConfig`, `makeVoragoUpdateConfig` | plugin | `src/preset/…`, `src/update/…` | 0 hits. CLEAR. |
| `VoragoTest::ProcessorFixture` | test-only | `tests/vorago_test_fixture.h` | `ProcessorFixture` appears in `seraphis_test_fixture.h:158` (`SeraphisTest`) and in several Disrumpo test TUs. Those are different test binaries and namespaces. CLEAR. |

**AU subtype sweep:** `grep -rn "kAUcomponentSubType " plugins/*/resources/auv3/audiounitconfig.h.in` →
`Dsrm Grad Innx Itrm Mbrm Ruin Srph`. `grep -rn "Vrgo" plugins/ tools/ .github/ CMakeLists.txt cmake/` →
**0 hits**. `Vrgo` is free.

**FUID sweep:** the fourteen registered FUIDs are at `disrumpo/src/plugin_ids.h:26,:31`,
`gradus:20,:23`, `innexus:20,:24`, `iterum:21,:25`, `membrum:18,:21`, `ruinae:24,:28`,
`seraphis:36,:40`. FR-011's two new GUIDs must not collide with any of them.

---

## Conventions decided in this spec

| Convention | Decision | Trace |
|---|---|---|
| FUIDs | Two freshly generated v4 GUIDs, `Vorago::kProcessorUID` / `Vorago::kControllerUID`; never reused, never changed after release | roadmap line 532 |
| AU type / subtype / manufacturer | `aumu` / `Vrgo` / `KrAt` | lines 531–532, OQ-1 (line 621) |
| Bundle base | `com.krateaudio.vorago` | OQ-1 (line 621) *"bundle id derived from it"* |
| Buses | 1 event input, 1 stereo audio output, **no** `addAudioInput()` | lines 529–530 |
| `kSupportedNumChannels` | `02`, matching one plist config `Inputs 0 / Outputs 2` | line 530 |
| Parameter-ID base | 0, with 100-ID section gaps (the map below) | line 533 |
| State version | `constexpr Steinberg::int32 kCurrentStateVersion = 1;` in `plugin_ids.h` | Phase 12 line 548 names the constant; v1 is its starting value |
| Prepare-time block bound | `VoragoEngine::kMaxBlockSamples` (2048) for both the engine config and the cavern config, the slice bound, and the scratch size | FR-023 |
| Seeds | engine seed `1u` (the `VoragoEngine::seed_` default, `vorago_engine.h:1605`), cavern `PrepareConfig::seed = 1` (the default, `cavern_verb.h:321`). There is **no seed parameter** in Phase 11 | Phase 12 owns the parameter surface |
| Reported latency | `engine->getLatencySamples() + cavern->getLatencySamples()` = **2048 + 1024 = 3072** samples once prepared, at every sample rate | FR-033; Phase 10 spec ruling 8 (*"compensation is a plugin-level concern and belongs to Phase 11"*) |
| Master-gain smoothing | `OnePoleSmoother`, advanced per sample, `kMasterGainSmoothMs = 20.0f`, snapped on the first block after prepare | Seraphis template (`seraphis_engine_config.h:38`) |
| **Soft-limit parameter** | **Not shipped** (a deliberate departure from the Seraphis template). Reason in FR-041 | verified conflict with `vorago_macro_matrix.h:476–481, :968` |
| **Seed preset category** | **`Drones`**, a single category and a permanent name; Phase 14 extends the list and never renames it | roadmap line 565 (fixed category set, filesystem + XML must match) |
| MPE / channel pressure | **OQ-7, resolved:** no note-expression surface in Phase 11; deferred to Phase 12 with sustain/CC64 (Clarification Q3/Q4) | roadmap line 633–634 |
| MSVC C4459 | Same narrow per-target `/wd4459` as `plugins/seraphis/CMakeLists.txt:120–135`, with the same *"delete when the dsp/ shadow is removed"* note | zero-warning rule; no `dsp/` edits in scope |

### Reserved parameter-ID map (roadmap line 533; bands in roadmap line 549's pack order)

```
0–99      Global        (Phase 11 — SHIPPED: master gain, polyphony)
100–199   Macros        (Phase 11 — SHIPPED, inert; wired in Phase 12)
200–299   Cloud         (Phase 12)
300–399   Noise         (Phase 12)
400–499   Resonance     (Phase 12)
500–599   Ecology       (Phase 12)
600–699   Sub           (Phase 12)
700–799   Smear         (Phase 12)
800–899   Events        (Phase 12)
900–999   Ecosystem     (Phase 12)
1000–1099 Body          (Phase 12)
1100–1199 Space         (Phase 12)
1200+     UNASSIGNED    — Phase 12 claims a whole band here for any section roadmap line 549
                          does not name (for example voice envelope, bloom, ghost). It must never
                          squat inside another section's band.
```

### Parameters shipped in Phase 11

| ID | Enum name | Type | Range / mapping | Default (normalized) |
|---|---|---|---|---|
| 0 | `kMasterGainId` | Range | `value * 2.0` clamped `[0, 2]` linear, shown in dB | `0.5` (unity) |
| 1 | `kPolyphonyId` | `StringListParameter` "1".."6" | index+1 voices → `VoragoEngine::setPolyphony` | index `3` (= 4 = `VoragoEngine::kDefaultPolyphony`, `vorago_engine.h:178`) |
| 100 | `kMacroDarknessId` | Range | inert 0–1 | `0.0` |
| 101 | `kMacroAgeId` | Range | inert 0–1 | `0.0` |
| 102 | `kMacroDensityId` | Range | inert 0–1 | `0.0` |
| 103 | `kMacroMovementId` | Range | inert 0–1 | `0.0` |
| 104 | `kMacroGravityId` | Range | inert 0–1 (bipolar around the centre) | `0.5` |
| 105 | `kMacroEntropyId` | Range | inert 0–1 | `0.0` |
| 106 | `kMacroPressureId` | Range | inert 0–1 | `0.0` |
| 107 | `kMacroWeightId` | Range | inert 0–1 | `0.0` |
| 108 | `kMacroFogId` | Range | inert 0–1 | `0.0` |
| 109 | `kMacroLifeId` | Range | inert 0–1 | `0.0` |
| 110 | `kMacroDepthId` | Range | inert 0–1 | `0.0` |
| 111 | `kMacroMassId` | Range | inert 0–1 | `0.0` |

The macro IDs follow the `VoragoMacro` enumerator order (`vorago_macro_matrix.h:95–109`), so
`id - 100 == static_cast<int>(VoragoMacro::X)`. The defaults are exactly `VoragoMacroValues`
(`:198–211`), which means a Phase 12 wiring through `setMacros` is neutral at the plugin defaults. Parameter
**types** registered here stay the same for the life of the plugin. The Phase 13 rule *"No
param-type swaps on registered IDs, ever"* (roadmap line 559) applies from the moment they ship.

---

## Functional Requirements

### A. Plugin target and build integration

- **FR-001** `plugins/vorago/CMakeLists.txt` MUST call `krate_plugin_read_version(VORAGO)` then
  `krate_plugin_configure_generated_files()` before defining the target
  (model `plugins/seraphis/CMakeLists.txt:10–11`).
- **FR-002** `plugins/vorago/version.json` MUST carry exactly the six keys `version` (`"0.1.0"`), `name`
  (`"Vorago"`), `description`, `publisher` (`"Krate Audio"`), `url` (`"https://krateaudio.com/vorago/"`),
  `copyright`. It must not have a `preset_subdir` key (the Seraphis template's FR-002 reasoning applies:
  release staging then puts presets directly under `Krate Audio/Vorago`, which is the path roadmap
  line 566 names).
- **FR-003** The target MUST be created with `smtg_add_vst3plugin` using an **enumerated** source list
  (no globs), and link `sdk vstgui_support KrateDSP KratePluginsShared` PRIVATE with
  `${CMAKE_CURRENT_SOURCE_DIR}/src` PRIVATE (model :18–82).
- **FR-004** `krate_plugin_platform_setup(${PLUGIN_NAME} TAG VORAGO BUNDLE_BASE com.krateaudio.vorago
  ENTITLEMENTS Vorago.entitlements KIND instrument)` MUST be called (model :90–95).
- **FR-005** `smtg_target_configure_version_file`, `smtg_target_add_plugin_resources(… RESOURCES
  resources/editor.uidesc)`, `krate_plugin_install_to_system`, `krate_plugin_install_presets` (no
  arguments) and `krate_plugin_set_warnings` MUST all be called (model :85–118), followed by
  `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()` (:140–142).
- **FR-006** The plugin target MUST compile with **zero** warnings. Because `vorago_voice.h:137` pulls
  in `continuous_body.h` and so the C4459 shadow at `timevar_comb_bank.h:931`, the target MUST carry
  the same narrow `if(MSVC) target_compile_options(${PLUGIN_NAME} PRIVATE /wd4459) endif()` as
  `plugins/seraphis/CMakeLists.txt:133–135`. The comment must name the shadowing line and say to
  delete the block when that shadow is removed in `dsp/`. The fix itself is a `dsp/` edit, which is
  out of scope here. The same suppression MUST be applied to `vorago_tests` if it produces the warning.
- **FR-007** `src/version.h`, `resources/win32resource.rc` and `resources/auv3/audiounitconfig.h` are
  **generated** files. Only `audiounitconfig.h.in` is written by hand, and no generated file may be edited.
  `.gitignore` MUST gain the three Vorago lines after the Seraphis trio at `.gitignore:76–78`:
  ```
  /plugins/vorago/resources/win32resource.rc
  /plugins/vorago/src/version.h
  /plugins/vorago/resources/auv3/audiounitconfig.h
  ```
- **FR-008** The directory skeleton MUST be exactly:
  ```
  plugins/vorago/
    CMakeLists.txt  CLAUDE.md  CHANGELOG.md  README.md  version.json
    src/
      entry.cpp  plugin_ids.h  version.h(GENERATED)
      processor/    processor.h  processor.cpp
      controller/   controller.h  controller.cpp
      parameters/   global_params.h  macro_params.h
      engine/       vorago_engine_config.h
      preset/       vorago_preset_config.h
      update/       vorago_update_config.h
      ui/.gitkeep
    resources/
      editor.uidesc  au-info.plist  win32resource.rc(GENERATED)
      auv3/audiounitconfig.h.in  auv3/audiounitconfig.h(GENERATED)
      auv3/macOS/Vorago.entitlements
      presets/Drones/.gitkeep
    tests/
      CMakeLists.txt  vstgui_test_stubs.cpp  vorago_test_fixture.h
      unit/test_main.cpp
      unit/processor_bus_test.cpp  unit/param_denorm_test.cpp  unit/state_roundtrip_test.cpp
      unit/midi_event_test.cpp     unit/lifecycle_test.cpp
      unit/controller/editor_lifecycle_test.cpp
      integration/processor_audio_test.cpp  integration/param_flow_test.cpp
      integration/processor_cpu_test.cpp
    docs/.gitkeep
    installers/windows/setup.iss  installers/linux/README.txt
  ```
  `installers/*` and `CHANGELOG.md` are consumed by `release.yml` and
  `tools/check-changelog-coverage.js`. The `.gitkeep` files exist because git does not track empty
  directories. `processor_params.cpp` / `processor_state.cpp` / `controller_*.cpp` are added only if a
  file passes ~1500 lines. If they are added, they also go in FR-060's second-compilation list.
- **FR-009** A `plugins/vorago/CLAUDE.md` leaf MUST follow `plugins/seraphis/CLAUDE.md`'s shape (type
  and AU identity, roadmap pointer, *"No DSP lives in this plugin"*, src skeleton, generated files,
  buses, the reserved param-ID table, test target invocation, pluginval path). It MUST also record
  five decisions that have no other home in the tree:
  1. the OQ-7 ruling on MPE / channel pressure, **decided: deferred to Phase 12** (Clarification Q3,
     2026-09-24), **with the controller-FUID host-cache caveat**: adding an interface
     (`INoteExpressionController` or `IMidiMapping`) to a released controller FUID can invalidate
     host-cached class metadata — Vorago is unreleased at `0.1.0`, so a Phase 12 addition comes before
     any release and has no host-cache cost;
  2. **preset categories only grow**: `Drones` is permanent, and Phase 14 extends the list but never
     renames an entry, because renaming a category orphans every preset saved in it;
  3. **the soft-limit omission** (FR-041), so Phase 12 does not add it back in a form the macro matrix
     silently overrides;
  4. **the polyphony ceiling** (Clarification Q2, 2026-09-24): `kPolyphonyId` exposes "1".."6" (to
     `kMaxVoices`), but the 30% CPU ceiling gates only the shipped default of 4 voices, not the
     maximum — polyphony 5 and 6 measured 37.99–43.41% of one core (engine alone) in Phase 10, and are
     offered deliberately;
  5. **sustain pedal deferral** (Clarification Q4, 2026-09-24): CC64 and every non-note event are
     ignored in Phase 11 (FR-031); sustain is taken in Phase 12 together with the `IMidiMapping`
     addition from item 1, as a wrapper-side note-off latch, because `VoragoEngine` has no sustain API.
- **FR-010** `CHANGELOG.md` MUST contain a `## [0.1.0]` section describing the scaffold.

### B. Identity

- **FR-011** `src/plugin_ids.h` MUST declare, in `namespace Vorago`,
  `static const Steinberg::FUID kProcessorUID(…)` and `kControllerUID(…)` with two freshly generated
  GUIDs different from the fourteen listed under New components.
- **FR-012** `src/plugin_ids.h` MUST declare `constexpr Steinberg::int32 kCurrentStateVersion = 1;`.
  Processor and controller both use it and neither includes the other.
- **FR-013** `src/plugin_ids.h` MUST declare `enum ParameterIDs : Steinberg::Vst::ParamID` with
  exactly the fourteen IDs in the parameter table, and MUST document the reserved map as a comment.
- **FR-014** `src/plugin_ids.h` MUST declare `static const char* const kSubCategories =
  "Instrument|Synth";` with the GCC-13 rationale (`plugins/seraphis/src/plugin_ids.h:43–49`).
- **FR-015** `resources/auv3/audiounitconfig.h.in` MUST be a complete copy of
  `plugins/seraphis/resources/auv3/audiounitconfig.h.in:1–40`, with `Seraphis`→`Vorago` and
  `Srph`→`Vrgo` substituted. That includes the unquoted token forms (`kAUcomponentType1`,
  `kAUcomponentSubType1`, `kAUcomponentManufacturer1`), `kAUcomponentName Krate Audio: Vorago`,
  `kAUcomponentTag Synthesizer`, `kAUcomponentVersion @AU_COMPONENT_VERSION@`, the digit-pair comment,
  `kSupportedNumChannels 02`, the flags and the delegate define.
- **FR-016** `resources/au-info.plist` MUST declare exactly one `AudioComponents` entry
  (`AUWrapperFactory`, `aumu`, `Vrgo`, `KrAt`, name `Krate Audio: Vorago`) and exactly one
  `SupportedNumChannels` dict `Inputs 0 / Outputs 2` (model `plugins/seraphis/resources/au-info.plist:23–51`).
  If these do not match the bus layout, AU initialization fails with `-10875`.
- **FR-017** `resources/auv3/macOS/Vorago.entitlements` MUST exist (a copy of Seraphis's).
- **FR-018** `src/entry.cpp` MUST register exactly two classes via `BEGIN_FACTORY_DEF` / two
  `DEF_CLASS2` / `END_FACTORY` with `#define stringPluginName "Vorago"` (model
  `plugins/seraphis/src/entry.cpp:47–85`). It MUST NOT include any `ui/*.h` or custom-view header,
  because Phase 11 registers no custom views (Phase 13 does).
- **FR-019** The `Controller` MUST NOT implement `INoteExpressionController` or `IMidiMapping`, and the
  plugin MUST NOT declare note-expression types — **per the OQ-7 ruling** (deferred to Phase 12,
  Clarification Q3, 2026-09-24), which is recorded in the leaf (FR-009 item 1). The engine's note API
  carries no per-note expression (`noteOn(std::uint8_t, std::uint8_t)` / `noteOff(std::uint8_t)`,
  `vorago_engine.h:564, :601`). CC64 (sustain) is likewise out of scope for Phase 11 (FR-009 item 5,
  Clarification Q4); FR-031 already ignores it as a non-note event.

### C. Buses and the audio path

- **FR-020** `Processor::initialize()` MUST call `addEventInput(STR16("Event In"))` and
  `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)` and MUST NOT call `addAudioInput()`
  (roadmap lines 529–530; model `plugins/seraphis/src/processor/processor.cpp:560–561`).
- **FR-021** `setBusArrangements` MUST return `kResultFalse` for `numIns != 0`, for `numOuts != 1`,
  and for any output other than `SpeakerArr::kStereo`, and `kResultTrue` for exactly (0 in, 1 stereo
  out) (model :594–608).
- **FR-022** `Processor` MUST own one `VoragoEngine` and one `CavernVerb` behind `std::unique_ptr`,
  created once in `initialize()` and released in `terminate()` by assigning `nullptr`. It also owns one
  `VoragoMacroMatrix` by value. Neither the engine nor the cavern may ever be a by-value member or a
  stack local (`vorago_engine.h:155–158`).
- **FR-023** `setupProcessing()` is the only path that prepares, and `process()` MUST NOT be able to
  reach it. These rules apply, in this order:
  1. build both configs with `maxBlockSamples = VoragoEngine::kMaxBlockSamples` (2048), not
     `setup.maxSamplesPerBlock`. The cavern clamps that value to `[64, 8192]`, so 2048 passes through
     unchanged (`cavern_verb.h:381`);
  2. `engine->setSeed(1u)` **before** `engine->prepare(...)`. `prepare()` derives every slot's seed
     from the stored engine seed and seeds each voice **before** the voice's own prepare
     (`vorago_engine.h:264–267, :295`), which is the one point at which `ContinuousBody::setSeed` is
     configure-time legal. Seeding first therefore makes prepare the single place the streams are
     derived. *Correction of an inherited claim:* a post-prepare `setSeed` is **not** a silent no-op.
     `VoragoEngine::setSeed` forwards to every slot (`vorago_engine.h:544–550`), and
     `VoragoVoice::setSeed` on a prepared voice immediately runs `applySeeds()` and
     `assignAgentSlots()` (`vorago_voice.h:961–966`), i.e. it reseeds live. The "only stores while
     unprepared" wording at `vorago_engine.h:264–266` describes only the in-prepare sequence. Phase 11
     calls `setSeed` nowhere outside `setupProcessing()`. Whether a live reseed is safe is a question
     for Phase 12's seed parameter, which MUST NOT assume it is a no-op;
  3. `engine->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples))`, then
     `cavern->prepare(sr, makeVoragoCavernConfig(kMaxBlockSamples))`;
  4. **polyphony is set from the parameter.** Immediately after prepare call
     `engine->setPolyphony(<current denormalized polyphony atomic>)`, because `VoragoEngineConfig` has
     no polyphony field. Then reset the "last pushed polyphony" tracker to that same value. A
     `setState()` may legally arrive before `setupProcessing()`;
  5. configure the master-gain smoother and arm its first-block snap (FR-024a);
  6. size all scratch once (FR-028), then set `prepared_ = true`.

  A repeated `setupProcessing()` (a sample-rate change) MUST follow the same path. `VoragoEngine::prepare`
  is documented as repeatable (`vorago_engine.h:270–276`).
- **FR-024** `Processor::process()` MUST, for each slice:
  0. push the global parameters (FR-024a);
  1. dispatch the host events due at the slice start (FR-025, FR-031);
  2. `macros_.apply(*engine_)` and `applyCavernTargets(*cavern_, macros_.computeCavernTargets())`;
  3. `engine_->processStereoBlock(outL, outR, n)`;
  4. `cavern_->processStereoBlock(outL, outR, outL, outR, n)`, in place. This is the documented call,
     and it is safe with aliased buffers (`vorago_composed_chain_test.cpp:33–38`);
  5. multiply by the smoothed master gain, per sample (FR-024a);
  6. `engine_->processOutputStage(outL, outR, n)`, so the limiter is always the last stage
     (`vorago_engine.h:1006–1016`).

  Steps 3, 4 and 6 are the chain test's three calls in order (`vorago_composed_chain_test.cpp:243–245`).
  `process()` MUST set `data.outputs[0].silenceFlags = 0` on every rendered block. `getTailSamples()`
  stays at the SDK default `kNoTail` (Clarification Q6, 2026-09-24; the Seraphis template). This is a
  known, accepted risk: a host that honours tail on offline bounce may truncate the 45 s voice release
  plus the 20 s cavern decay; `silenceFlags = 0` on every rendered block prevents a stale-silence cut
  during playback, and Phase 14's long-render work revisits the bounce-truncation risk.
- **FR-024a** *(the shipped global parameters reach the chain)*.
  1. **Polyphony**: `engine_->setPolyphony(n)` is called **only when `n` differs from the last pushed
     value**. A redundant call with an unchanged `n` is behaviourally a no-op: the allocator's shrink
     loop runs only when the count drops (`voice_allocator.h:334`), and `sumGain_.setTarget` with the
     same value only rewrites the target (`vorago_engine.h:507–521`). The rule exists to keep
     `setPolyphony` (a clamp, an allocator walk and a smoother write) off the per-slice path, and to
     make the push edge-triggered so FR-023.4's tracker reset is meaningful. Because a redundant call is
     invisible in the render, the processor MUST expose a **call-count seam**: a plain
     `std::uint32_t` counter incremented at every `engine_->setPolyphony` call the processor makes
     (including FR-023.4's), readable through a `const` test-access getter. It is written only on the
     thread that calls `setupProcessing`/`process` and read by tests between calls. The result is
     also observable through `getPolyphony()` (:523).
  2. **Master gain**: `VoragoEngine` has no gain surface, so the gain is a wrapper-side per-sample
     multiply using `Krate::DSP::OnePoleSmoother`, configured with
     `kMasterGainSmoothMs = 20.0f` (declared in `engine/vorago_engine_config.h`). The smoother is
     **snapped** to the current atomic value on the first `process()` after
     `setupProcessing()`/`setActive(true)` and ramps after that. It is applied **after the cavern and
     before `processOutputStage`**. A multiply after the limiter is forbidden: at gain 2.0 it would
     break SC-006 by construction.
- **FR-025** The host block MUST be split at every event's `sampleOffset`, because
  `VoragoEngine::noteOn/noteOff` take no offset. Events are applied in `sampleOffset` order. An offset
  at or past the end of the block is clamped to the last sample, and a negative offset is clamped to 0.
- **FR-026** No slice passed to the engine or the cavern may exceed `VoragoEngine::kMaxBlockSamples`
  (2048). A longer host block is sub-divided. The processor MUST expose a **slice-count seam**: the
  number of engine slices rendered by the most recent `process()` call, readable through a `const`
  test-access getter (a plain counter reset at the top of each `process()`; no allocation). An
  event-free 4096-sample block MUST report exactly 2 slices.
- **FR-026a** *(test access to the engine)*. The processor MUST expose a `const` test-access getter
  returning `const Krate::DSP::VoragoEngine*`, so tests can read `getVoiceState(i)`
  (`vorago_engine.h:1065`), `getActiveVoiceCount()` (:1039), `getPolyphony()` (:523) and
  `getLatencySamples()`. No non-const access is exposed.
- **FR-027** The processor MUST NOT copy `processOutputStage`'s internal 64-sample loop as a block-size
  requirement. It is *"a CADENCE CHOICE, NOT A SIZE CONSTRAINT"* (`vorago_engine.h:996–1000`).
- **FR-028** Every scratch buffer is sized once at `setupProcessing()` to `kMaxBlockSamples`, and none
  is ever resized in `process()`. With the in-place chain of FR-024 the only audio scratch needed is
  whatever the master-gain stage requires. Slices are rendered straight into the host channel buffers
  at their offsets.
- **FR-029** `process()` MUST NOT allocate, lock, throw or perform I/O, and MUST construct a
  `Krate::DSP::ScopedDenormalMode` at its top. FTZ/DAZ is per-thread, so setting it in
  `setupProcessing()` does not reach the audio thread (`plugins/seraphis/src/processor/processor.cpp:1295–1298`).
- **FR-030** Degenerate shapes produce silence and `kResultOk` without touching the chain. The guard
  order is binding and matches `plugins/seraphis/src/processor/processor.cpp:1325–1356`: parameter
  changes are latched first, then `numOutputs > 0 && outputs != nullptr`, then
  `channelBuffers32 != nullptr`, then `numChannels >= 2` (a guard of its own, because a host may ignore
  the `kResultFalse` from `setBusArrangements`), then `numSamples > 0`, then both channel pointers, and
  then readiness (`prepared_ && engine_ && cavern_`). On the not-ready path the processor MUST
  zero-fill both channels and set `silenceFlags = 3`.
- **FR-031** MIDI: `kNoteOnEvent` with `velocity > 0` → `noteOn(pitch,
  uint8(clamp(velocity * 127 + 0.5, 1, 127)))`; `kNoteOnEvent` with `velocity <= 0`, and
  `kNoteOffEvent` → `noteOff(pitch)`; any pitch outside `[0, 127]` is dropped; every other event type is
  ignored. The floor of 1 matters: `noteOn` treats velocity 0 as a note-off (`vorago_engine.h:568–571`).
- **FR-032** `setActive(false)` MUST call `engine_->silence()` and `cavern_->reset()`. Both are
  documented as not-audio-thread and allocation-free (`vorago_engine.h:437–447`,
  `cavern_verb.h:486–487`), which is the thread `setActive` runs on. `setActive(true)` MUST NOT
  allocate. Because the release is 45 s (`vorago_voice.h:324`), a deactivated instance would otherwise
  keep a long tail from before it was stopped.
- **FR-033** `getLatencySamples()` MUST return `engine_->getLatencySamples() + cavern_->getLatencySamples()`
  (0 when either pointer is null). The two components run in series: the smear sits in the engine
  (`vorago_engine.h:1024–1030`), and the cavern's spectral diffusion delays its whole output while
  aligning its dry path to the same latency (`cavern_verb.h:404, :1312`). With the shipped configs the
  sum is **3072 samples after prepare, at every sample rate**. Before the first prepare the raw sum is
  **1024** (the smear reports 0 while unprepared and the cavern's `AetherReverb` reports 1024 from its
  member defaults). Hosts read latency after `setupProcessing()`/`setActive(true)`, and the value is
  constant from then on, so **no `restartComponent` is issued**. An `AudioEffect` also has no route to
  an `IComponentHandler`. The Seraphis Phase 8 amendment A2 reasoning applies unchanged. If a later
  phase makes the post-prepare value variable, it MUST announce the change through processor →
  `IMessage` → controller → `restartComponent(kLatencyChanged)`.
- **FR-034** The macro matrix MUST be applied every slice at its neutral values, with
  `computeCavernTargets()` pushed into the cavern. At the neutral, both are identity writes (the matrix
  is at its identity, and the cavern targets equal `CavernVerb`'s own defaults), so the rendered result
  does not change. It gives Phase 12 a working path: wiring the macros becomes a data change, not a
  change to the render loop.
- **FR-034a** *(making FR-034 testable)*. The cavern push MUST be a named free function in
  `src/engine/vorago_engine_config.h`:
  ```cpp
  inline void applyCavernTargets(Krate::DSP::CavernVerb& cavern,
                                 const Krate::DSP::VoragoCavernTargets& t) noexcept;
  ```
  It calls exactly `setSize, setDarkness, setDecaySeconds, setFog, setDamperDepth, setMix, setWidth`,
  in that order, with the matching field. That is `pushCavernTargets(..., forceDry=false)` from
  `vorago_composed_chain_test.cpp:188`. `CavernVerb` exposes no control getters, so SC-024 checks this
  function by its effect on the render, with non-neutral targets.

### D. Parameters and state

- **FR-040** `src/parameters/global_params.h` MUST follow the pack contract
  (`plugins/seraphis/src/parameters/global_params.h:40, 85, 124, 169, 211, 218, 240–241`):
  `struct GlobalParams { std::atomic<float> masterGain{1.0f}; std::atomic<int> polyphony{4}; }` plus
  `handleGlobalParamChange`, `registerGlobalParams`, `formatGlobalParam`, `saveGlobalParams`,
  `loadGlobalParams` (EOF-safe) and `loadGlobalParamsToController`.
- **FR-041** **No soft-limit parameter ships.** The Seraphis template shipped `kSoftLimitId` →
  `setOutputSaturation`, but here `VoragoMacroMatrix::apply()` rewrites
  `engine.setOutputSaturation(...)` on every call (`vorago_macro_matrix.h:968`, the Pressure row at
  :476–481, base `0.12f`), and FR-034 calls `apply()` every slice. A soft-limit parameter would
  therefore be overwritten within one slice. That makes it a control that does nothing, which is a
  defect. **This rationale is narrowed** (Clarification Q1, 2026-09-24) to controls that are
  **overwritten by another path** — the soft-limit case above, where `apply()` rewrites the value every
  slice — and does **not** extend to a control that is simply **not yet wired**. FR-042's twelve macro
  parameters are the explicit **unreleased-`0.1.0` exception**: they ship registered and inert, are
  never overwritten by anything Phase 11 runs (`macros_.apply()` reads them nowhere), and Phase 12
  wires them via `setMacros()` before any release. The output saturation belongs to the Pressure macro.
  The true-peak limiter has no bypass in any case (`vorago_engine.h:1015`). Phase 12 may add an
  output-trim or saturation control only through the matrix's base-override surface, which Phase 10
  deferred to it (Phase 10 spec ruling 10, `setTargetBase`).
- **FR-042** `src/parameters/macro_params.h` MUST define twelve `std::atomic<float>` fields in
  `VoragoMacro` order, **with explicit initializers**: `gravity{0.5f}`, all others `{0.0f}`. Without the
  initializer, value-initialization would put `gravity` at `0.0` while the controller reports `0.5`. It
  carries the same six functions. It MUST be **inert**: no Phase 11 code reads `MacroParams` into
  `VoragoMacroMatrix` (roadmap line 548 gives that wiring to Phase 12).
- **FR-043** `processParameterChanges()` MUST dispatch by ID range: IDs `< 100` go to
  `handleGlobalParamChange` and IDs `100–199` go to `handleMacroParamChange`. It takes the **last**
  point of each queue.
- **FR-044** Denormalization: master gain `clamp(value * 2.0, 0, 2)`; polyphony
  `clamp(int(value * 5 + 1 + 0.5), 1, 6)`, which is `VoragoEngine::setPolyphony`'s own clamp range
  `[1, kMaxVoices]` (`vorago_engine.h:508`); macros `clamp(value, 0, 1)`.
- **FR-045** `getState()` writes `int32 kCurrentStateVersion`, then `saveGlobalParams`, then
  `saveMacroParams`, via `Steinberg::IBStreamer(state, kLittleEndian)`.
- **FR-046** `setState()` reads the version first and returns `kResultFalse` for a version greater
  than `kCurrentStateVersion`. It loads the packs in the same order. A short or truncated stream leaves
  the unread parameters at their current values and corrupts nothing.
- **FR-047** `Controller::setComponentState()` reads the same stream through the
  `load…ParamsToController` helpers, so every registered parameter updates when a preset loads.
- **FR-048** `Controller::initialize()` MUST register exactly the fourteen parameters.
  Polyphony uses `createDropdownParameterWithDefault(..., 3, {"1","2","3","4","5","6"})`
  (`plugins/shared/src/ui/parameter_helpers.h:47`).

### E. Controller, preset and update adapters, resources

- **FR-050** `src/preset/vorago_preset_config.h` MUST provide
  `inline Krate::Plugins::PresetManagerConfig makeVoragoPresetConfig()` returning
  `{ kProcessorUID, "Vorago", "Synth", { "Drones" } }`, in the field order at
  `preset_manager_config.h:19–24`. `Controller::initialize()` MUST create a
  `Krate::Plugins::PresetManager` from it (model `plugins/seraphis/src/controller/controller.cpp:157–158`).
  Roadmap line 528 lists a preset browser as part of the Ruinae shape. The browser **UI** belongs to
  Phase 13/14.
- **FR-051** `resources/presets/Drones/` MUST exist, so the filesystem category and the config's
  category agree from the start (roadmap line 565 *"filesystem dirs + XML metadata must match —
  Membrum lesson"*).
- **FR-052** `src/update/vorago_update_config.h` MUST provide `makeVoragoUpdateConfig()` returning
  `{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }`. The
  `Controller` MUST NOT create an `UpdateChecker` in Phase 11. It starts a thread and fetches from the
  network, which has no place in the editor-lifecycle, ASan or valgrind lanes.
- **FR-053** `src/engine/vorago_engine_config.h` MUST be thin: free functions only, returning the
  DSP-owned structs.
  `inline Krate::DSP::VoragoEngineConfig makeVoragoEngineConfig(std::size_t maxBlockSamples)` returns the
  shipped `VoragoEngineConfig` defaults (`vorago_engine.h:105–156`) with only `maxBlockSamples` set,
  so `smearEnabled = true`, `smearFftSize = 2048`, and both Phase 10a ghost flags stay inert.
  `inline Krate::DSP::CavernVerb::PrepareConfig makeVoragoCavernConfig(std::size_t maxBlockSamples)`
  returns the shipped `PrepareConfig` defaults (`cavern_verb.h:300–322`: 8 channels,
  `maxEarlySeconds 0.30`, `maxDelaySeconds 0.50`, spectral diffusion on at 1024, seed 1). These are the
  same values the Phase 10 composed chain and its SC-001b Cavern term were measured with
  (`vorago_composed_chain_test.cpp:303–313`). Any change from these defaults must be justified in the
  header. The file also holds `kMasterGainSmoothMs` (FR-024a) and `applyCavernTargets` (FR-034a).
- **FR-054** `resources/editor.uidesc` MUST be a placeholder made only of stock VSTGUI views, with a
  template named `"editor"` and **fourteen** controls bound through `<control-tags>` to the fourteen
  shipped IDs: continuous controls for master gain and the twelve macros, and a `COptionMenu` for
  polyphony. It MUST contain no custom view class name and must not be empty, because
  `tools/check-bundle.js` fails a bundle without a non-empty `editor.uidesc`.
- **FR-055** `Controller::createView("editor")` MUST return a `VSTGUI::VST3Editor` on that template
  (model `controller.cpp:434–436`). The controller MUST survive `willClose()` on a view tree that was
  never attached to a window.
- **FR-056** `src/ui/` MUST exist and hold nothing except `.gitkeep`.

### F. Test target `vorago_tests`

- **FR-060** `plugins/vorago/tests/CMakeLists.txt` MUST define `add_executable(vorago_tests …)` in the
  shape of `plugins/seraphis/tests/CMakeLists.txt`: test sources, a **second compilation of every plugin
  `.cpp`** through `../src/...` paths, the SDK sources `memorystream.cpp`, `hosting/hostclasses.cpp`,
  `hosting/pluginterfacesupport.cpp`, `main/moduleinit.cpp`, `main/pluginfactory.cpp`, and a local
  `vstgui_test_stubs.cpp` (`GetPluginFactory()` returning `nullptr`). It links `KrateDSP
  KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk` with `sdk` after `vstgui_support`,
  includes `${CMAKE_SOURCE_DIR}/tests`, defines
  `VORAGO_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"`, and ends with
  `catch_discover_tests(vorago_tests REPORTER console)`.
- **FR-061** `tests/unit/test_main.cpp` MUST define `void* moduleHandle = nullptr;`, call `enableFTZDAZ()`
  before `Catch::Session().run()`, and be the **only** TU in the binary that includes
  `<allocation_operator_overrides.h>`. Without that include `AllocationScope` counts nothing.
- **FR-062** Any TU that injects NaN/Inf MUST carry `-fno-fast-math -fno-finite-math-only` under
  `Clang|GNU`, build non-finite values from bit patterns through a volatile, and never call
  `std::isnan` / `std::isinf` / `std::isfinite`.
- **FR-063** Test host doubles MUST reuse `Krate::Test::EventList`, `Krate::Test::ParamValueQueue` and
  `Krate::Test::ParameterChanges` (`tests/test_helpers/vst_event_list.h`, `vst_param_changes.h`) where
  a single point is enough. `tests/vorago_test_fixture.h` (namespace `VoragoTest`) may define only what
  those helpers lack: the multi-point queue SC-009 needs, and the `ProcessorFixture` harness. A test must
  not copy-paste a double that already exists under `tests/test_helpers/`.
- **FR-064** Every test that constructs `Vorago::Processor` does so on the heap, or asserts
  `static_assert(sizeof(Vorago::Processor) < 64 * 1024)` if it uses a stack local. No test may create a
  `VoragoEngine` on the stack.
- **FR-065** No test may pin a render with a bit-exact float digest. `node tools/lint-float-bit-goldens.js`
  stays clean. Comparisons across runs use `render_fingerprint.h`, and comparisons within a single
  process use the explicit max-abs thresholds named in the SCs.
- **FR-066** Day-one coverage, with fixed `TEST_CASE` names (roadmap lines 537–538 plus the audio-path
  FRs):

  | File | TEST_CASE | Covers |
  |---|---|---|
  | `unit/processor_bus_test.cpp` | `Vorago_ProcessorBusSetup` | FR-020, FR-021 |
  | `unit/param_denorm_test.cpp` | `Vorago_ParamDenormRoundTrip` | FR-043, FR-044, FR-048 |
  | `unit/state_roundtrip_test.cpp` | `Vorago_StateRoundTrip` | FR-042, FR-045–FR-047 |
  | `unit/midi_event_test.cpp` | `Vorago_MidiEventTranslation` | FR-025, FR-026, FR-031 |
  | `unit/lifecycle_test.cpp` | `Vorago_ProcessorLifecycle` | FR-023, FR-029, FR-030, FR-032, FR-033 |
  | `unit/controller/editor_lifecycle_test.cpp` | `Vorago_EditorLifecycle` tagged `[vorago][controller][ui][lifecycle]` | FR-050, FR-054, FR-055 |
  | `integration/processor_audio_test.cpp` | `Vorago_ProcessorRendersHeldNote` | FR-024, FR-024a, FR-034, FR-034a |
  | `integration/param_flow_test.cpp` | `Vorago_ParamFlowReachesEngine` | FR-023, FR-024a, FR-042 |
  | `integration/processor_cpu_test.cpp` | `Vorago_ProcessorCpu` tagged `[vorago][.perf][performance]` | FR-067, FR-067a (hidden: **excluded** from SC-002's default list and run) |

  The `[lifecycle]` tag is required. `valgrind-nightly.yml:283` filters test binaries with
  `'[lifecycle]'`, so without the tag the nightly lane would select no Vorago test.
- **FR-067** `tests/integration/processor_cpu_test.cpp` MUST define `Vorago_ProcessorCpu`, tagged
  `[vorago][.perf][performance]` so it is hidden from a default run and runs alone in the isolated CPU
  lane. It measures the composed chain through `Vorago::Processor::process()` exactly as SC-014
  specifies and prints the figure with `WARN`. The **absolute** per-block figure against `kReferenceNs`
  is a **recorded, non-gating** measurement (roadmap Phase 11 has no CPU criterion for the composed
  chain, lines 540–542; Clarification Q5, 2026-09-24), run only through `node tools/run-cpu-tests.js`
  (FR-081). FR-067a adds the gating clause.
- **FR-067a** *(wrapper-overhead gate, Clarification Q5, 2026-09-24)*. In the **same**
  `Vorago_ProcessorCpu` case, back-to-back with FR-067's measurement and using identical seed,
  polyphony, note-on script and block count, measure the composed chain driven **directly** —
  `engine_->processStereoBlock`, then `CavernVerb::processStereoBlock` in place, then the master-gain
  multiply, then `engine_->processOutputStage` — with no `Processor::process()` call in between. The
  case MUST `REQUIRE` that `Processor::process()`'s time is at most **1.05×** the direct-chain time.
  This gates only what Phase 11's wrapper adds over the Phase 10 chain — slicing, event dispatch, the
  master-gain smoother and `macros_.apply()` — because nothing else in `process()` is measurable
  overhead. A result over FR-067's 30% ceiling is still surfaced to the user as a Phase 10 budget
  finding; it is never absorbed, and no workload is shrunk to make either threshold pass.

### G. Registration outside `plugins/`, all of it in this phase (roadmap lines 534–536)

- **FR-070** Root `CMakeLists.txt` MUST add `add_subdirectory(plugins/vorago)` after `:494`.
- **FR-071** `.github/workflows/ci.yml` MUST add a Vorago entry at every site that names a plugin:
  1. the sites the roster lint checks (the detect-changes outputs, the paths-filter, the `for p in`
     loop, the `$GITHUB_OUTPUT` echoes, the three FetchContent `hashFiles` keys, and the nine
     `for plugin_info in \` blocks with their `case` arms). The build block entries MUST name the
     `Vorago_AU:Vorago_AUV3` targets on macOS, as Seraphis's `:551` does;
  2. the sites the lint does **not** check, each modelled on its Seraphis twin: the Windows artifact
     upload (`:474–479`), the macOS `auval` step running `auval -v aumu Vrgo KrAt` (`:764–773`), the
     macOS AUv3 bundle verification (`:824–830`), the macOS artifact upload with `.vst3`,
     `.component` and `AUv3.app` (`:903–911`), and the Linux artifact upload (`:1176–1181`).
- **FR-072** `.github/workflows/release.yml` MUST add `- vorago` to the dispatch choice list (`:41`)
  and `'plugins/vorago/CMakeLists.txt'` to the `hashFiles` key (`:138`).
- **FR-073** `.github/workflows/valgrind-nightly.yml` MUST add `vorago_tests` to the build list (`:276`)
  and to the run list (`:283`).
- **FR-074** `tools/run-clang-tidy.ps1` MUST add `"vorago"` to the `ValidateSet` (`:60`), a
  `"vorago" { … }` case adding `plugins/vorago/src` **and** `plugins/vorago/tests` (the Seraphis case at
  :196–207 records why both), and the same directories in the `all` case.
- **FR-075** `tools/run-clang-tidy.sh` MUST add a `vorago)` case (`plugins/vorago/src`
  `plugins/vorago/tests`), the paths in `all)`, and `vorago` in the usage text (`:63`).
- **FR-076** `tools/check-changelog-coverage.js` MUST add `'vorago'` to `PLUGINS` (`:50`).
- **FR-077** `tools/gen-specs-index.js` MUST add `['vorago', 'Vorago']` to `SUBSYSTEMS` **before**
  `spectral`, `filter`, `oscillat`, `grain` and `dsp`, with a comment matching the Seraphis one at
  `:20–22`. Without it, `vorago-phase4-spectral-smear` stays under "DSP / Spectral" and the rest of the
  Vorago specs stay under "Other".
- **FR-078** The generated artifacts MUST be regenerated and committed: `specs/_architecture_/repo-map.json`
  (`gen-repo-map.js` auto-discovers `plugins/`) and `specs/INDEX.md`. `symbols.json` scans only
  `dsp/include` and does not change.
- **FR-079** Root `CLAUDE.md` MUST be updated everywhere a plugin roster appears: the Project Overview
  list, the Monorepo Structure prose, the per-directory leaf list, the pluginval block, the plugin test
  target list, the "Built plugins are at" list, the clang-tidy `-Target` roster, and the Quick Reference
  rows (add parameter, add test, change UI). *(Roadmap line 536: "CLAUDE.md rosters + new leaf".)*
- **FR-080** `.github/workflows/docs.yml` needs no edit. It globs `plugins/*/`, and a plugin with no
  release tag gets no landing card. Phase 11 creates `docs/` with only a `.gitkeep`, and Phase 14 writes
  `index.html`.
- **FR-081** `tools/run-cpu-tests.js` MUST add `'vorago_tests'` to `DEFAULT_TARGETS` after
  `'seraphis_tests'` (`:45–50`). The list is hard-coded, does not read `plugins/`, and is not checked
  by `lint-plugin-roster.js`, so without this entry the documented "all suites" run
  (`node tools/run-cpu-tests.js`) skips `vorago_tests` and still reports success: the silent skip the
  Overview says registration must prevent.

---

## Success Criteria

Each criterion gives its metric, its threshold, and the test or command that measures it.

- **SC-001 — Builds on all three OS legs with zero warnings.** `Vorago.vst3` (plus `Vorago_AU` /
  `Vorago_AUV3` on macOS) is produced by the CI build job on Windows, macOS and Linux. **Threshold:**
  3/3 legs green **and** zero warnings emitted while compiling the `Vorago` and `vorago_tests`
  targets, **whatever path the warning line names**. A warning raised in a `dsp/` header included by a
  Vorago TU counts: on GCC/Clang its `warning:` line carries the header's path, and `plugins/vorago`
  appears only on a preceding "In file included from" line. Measurement: after cleaning those two
  targets' object files (or touching their sources), build **only** `--target Vorago vorago_tests`,
  capture that target-scoped log once, and count lines matching `warning C[0-9]|warning:`, with no
  path filter. Threshold **0**. On MSVC the same count is taken over the target-scoped log, where each
  warning line also carries the `[...Vorago.vcxproj]` / `[...vorago_tests.vcxproj]` suffix. Locally:
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Vorago vorago_tests`.
  *(Roadmap line 540.)*
- **SC-002 — `vorago_tests` green, with the required cases present.** (1) `vorago_tests.exe
  --list-tests` lists all eight non-hidden FR-066 names, and `vorago_tests.exe "[.perf]" --list-tests`
  lists `Vorago_ProcessorCpu` (hidden, and not run by clause 2); (2) the last line of `vorago_tests.exe 2>&1 | tail -5`
  reads `All tests passed`. A case **count** is not the threshold. *(Roadmap line 540.)*
- **SC-003 — pluginval strictness 5 clean.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Vorago.vst3"` exits 0 with zero failures. *(Roadmap
  line 540–541.)*
- **SC-004 — `auval` passes.** `auval -v aumu Vrgo KrAt` reports `AU VALIDATION SUCCEEDED` in the
  macOS CI step FR-071.2 adds. A `-10875` means FR-015, FR-016 and FR-020 disagree. *(Roadmap line 541.)*
- **SC-005 — A held note renders non-silent audio.** `Vorago_ProcessorRendersHeldNote`: 48 kHz,
  512-sample blocks, registered defaults (unity gain, polyphony 4), NoteOn(48, 100) at sample 0, 8 s
  rendered. **Threshold:** peak over the last second `>= 1.0e-4` (−80 dBFS), and every sample finite
  (bit-pattern check). The floor is set low on purpose. The shipped envelope takes 20 s to reach full
  level (`vorago_voice.h:322`), and the failures this criterion exists to catch (the zero-fill path
  taken by mistake, a note that never reaches the engine, a missing chain step) all produce exact zeros.
  The measured peak and RMS MUST be printed with `WARN` and recorded in `compliance.md`. The render
  MUST NOT change the envelope: the plugin has no route to `applyFastAttack`, and none may be added.
- **SC-006 — Output never exceeds the limiter ceiling.** A section of `Vorago_ProcessorRendersHeldNote`:
  polyphony 6, six notes `{36, 40, 43, 47, 50, 53}` (`vorago_composed_chain_test.cpp:122–123`)
  held, master gain normalized 1.0 (linear 2.0), 30 s rendered. **Threshold:** `max |sample| <=
  0.9661` (`10^(-0.3/20)`, `kOutputCeilingDb`, `vorago_engine.h:203`) on both channels, with every
  sample finite. The measured peak is recorded. The master gain sits before the limiter (FR-024a), so
  the bound holds by construction. **Discrimination clause:** the same 30 s render at master gain
  normalized 0.5 (linear 1.0) MUST have a peak `>= 0.49` on at least one channel, printed with `WARN`
  and recorded. The limiter guarantees `|out| <= ceiling` (`true_peak_limiter.h:12–14`), so with the
  gain moved after the limiter the gain-2.0 peak would be 2 × that peak, `>= 0.98 > 0.9661`. This
  clause is what makes "a gain after the limiter fails SC-006" true by measurement. `0.49` is derived
  from `0.9661 / 2` and MUST NOT be lowered; if the render does not reach it, the script changes
  (longer render, velocity 127), never the threshold.
- **SC-007 — Zero allocations in `process()`.** `Vorago_ProcessorLifecycle` section: after prepare and
  one warm-up block, a 2 s render with note-ons/offs and parameter changes inside an `AllocationScope`.
  **Threshold:** `AllocationDetector::instance().getAllocationCount()` read **inside** the scope is `0`.
  A liveness probe first proves the detector counts: one deliberate `new` inside a scope must read `>= 1`.
- **SC-008 — Block-size invariance.** (1) A section of `Vorago_MidiEventTranslation`: the same 4 s
  script (note events at non-multiples of every partition, and **no parameter change**: FR-043 applies
  a parameter change per host block, so a change at any offset > 0 lands at a partition-dependent
  sample, and no offset > 0 is a boundary shared by every partition within 4 s) rendered at host blocks
  **{1, 7, 64, 65, 512, 2048, 4096}**. Each is compared against the 512 reference. **Threshold:** max
  absolute per-sample difference `<= 1.0e-5` per channel. The test MUST also assert that a partition
  boundary fell inside a 64-sample control chunk (block 65 guarantees this) and that the 4096 run went
  through FR-026's sub-division, observed through FR-026's slice-count seam (an event-free 4096 block
  reports 2 slices). `compareFingerprints` runs as a `WARN`-only secondary check. Engine
  and cavern each claim exact partition invariance (Phase 10 SC-007, `vorago_engine.h:880–889`;
  `cavern_verb.h:560–567`), so anything above 1e-5 means the wrapper introduced the dependence.
  (2) **Parameter timing is block-granular, and asserted as such.** At 512-sample blocks, with a note
  held and at least 1 s of warm-up (so FR-024a's first-block snap has already happened), one render
  sends master gain normalized 0.0 at `sampleOffset` 300 of block N and a second render sends the same
  change at `sampleOffset` 0 of block N. **Threshold:** max-abs difference `<= 1e-5` over the whole
  render (the change takes effect from the start of the host block that carries it, FR-043).
  Non-vacuity: over the 1 s that follows block N plus the 3072-sample latency, the render differs from
  a no-change render by `> 1e-3` RMS.
- **SC-009 — Denormalization round-trip.** `Vorago_ParamDenormRoundTrip`: for each of the fourteen IDs
  and normalized values `{0, 0.25, 0.5, 0.75, 1}`, send the value through `processParameterChanges` and
  read the atomic back. **Thresholds:** master gain within `1e-6` of `2v`. Polyphony gives exactly
  `{1, 2, 4, 5, 6}`. Macros are within `1e-6` of `v`. A multi-point queue `{0.1 @ 0, 0.9 @ 100}` stores
  `0.9`. Each registered parameter's `toPlain(getDefaultNormalizedValue())` matches the table.
- **SC-010 — State round-trip.** `Vorago_StateRoundTrip`: (1) set all fourteen to non-default values,
  then `getState` → fresh `setState` → `getState`, and the two streams are **byte-identical**; (2) the
  default state's stream decodes to `gravity == 0.5f` and polyphony 4; (3) a stream with version
  `kCurrentStateVersion + 1` → `kResultFalse`, and the parameters are unchanged; (4) a stream cut after
  the global block leaves the macros at their prior values; (5) the controller clause:
  `Controller::setComponentState` on stream (1) moves every registered parameter to the streamed value
  within `1e-6`. *(Roadmap line 537.)*
- **SC-011 — Bus configuration is exactly instrument-shaped.** `Vorago_ProcessorBusSetup`: 1 event
  input bus, 0 audio input buses, 1 audio output bus with `kStereo`; `setBusArrangements` → `kResultTrue`
  for (0, stereo) and `kResultFalse` for (1 in, stereo), (0, mono), (0, 2 × stereo). *(Roadmap line 537.)*
- **SC-012 — Editor lifecycle is UAF-free and the placeholder is bound.** `Vorago_EditorLifecycle`:
  (1) `exerciseEditorLifecycle(controller, "editor", VORAGO_RESOURCES_DIR "/editor.uidesc")` completes
  3 cycles; (2) walking the built frame finds exactly fourteen `CControl`s whose tag set equals the
  fourteen IDs, and the `kPolyphonyId` control is a `COptionMenu`; (3) `makeVoragoPresetConfig()` reports
  `pluginName == "Vorago"` and `subcategoryNames == {"Drones"}`. A `PresetManager` built from it,
  with its factory override (`preset_manager.h:53–60`) pointed at a temp directory holding one file
  `Drones/Probe.vstpreset`, returns exactly **1** preset from `scanPresets()`, and that preset has
  `subcategory == "Drones"`. The scan keys on the `.vstpreset` extension and derives the subcategory
  from the parent directory (`preset_manager.cpp:63, :95–101`). Separately, the controller's own preset
  manager is non-null after `initialize()`, read through a `presetManagerForTest()` getter modelled on
  `plugins/seraphis/src/controller/controller.h:196`; (4) the case carries the `[lifecycle]` tag,
  checked with `vorago_tests.exe "[lifecycle]" --list-tests`, which lists it; (5) **gating UAF
  evidence**: `vorago_tests.exe "[lifecycle]"` built in the local ASan configuration
  (`-DENABLE_ASAN=ON`, Debug, `CMakeLists.txt:112`) exits 0 with no AddressSanitizer report. The first
  nightly valgrind run of the case is a **follow-up** recorded in `compliance.md`, not a phase gate. *(Roadmap line 538.)*
- **SC-013 — Reported latency.** A section of `Vorago_ProcessorLifecycle`: after `setupProcessing` at
  each of **{44 100, 48 000, 88 200, 96 000, 192 000} Hz**, `getLatencySamples() == 3072` **and** equals
  the engine value plus the cavern value, both read through test access. It stays 3072 after a second
  `setupProcessing` at a different rate, after `setActive(false/true)`, and after changing every shipped
  parameter. The value before the first prepare is recorded (1024 expected). This criterion gates only
  the post-prepare invariant.
- **SC-014 — Wrapper-overhead ceiling (GATING) plus composed-chain CPU (recorded, non-gating).**
  Clarification Q5 (2026-09-24) turns the wrapper overhead — the part of this chain Phase 11 actually
  owns — into a gate, while keeping the absolute composed-chain figure recorded only, because that
  figure belongs to Phase 10's budget (met by arithmetic: engine baseline plus the Phase 9 Cavern
  constant, `dsp/tests/unit/systems/vorago_perf_test.cpp:248–262`) and is flaky under machine load
  (Phase 10's own figure moved between 82.69% and 105.83% of its reference with no code change). The
  test is `Vorago_ProcessorCpu` (FR-067, FR-067a, `tests/integration/processor_cpu_test.cpp`), tagged
  `[vorago][.perf][performance]` so it runs alone in the isolated CPU-test lane: polyphony 4 with four
  held notes, 512-sample blocks, 48 kHz, best-of-16 × 100 blocks after a discarded warm-up, run through
  `node tools/run-cpu-tests.js vorago_tests`.
  **Threshold (gating):** in the same case, back-to-back, with identical seed, polyphony, note-on
  script and block count, `Processor::process()` time MUST be `<= 1.05×` the same chain driven
  directly (`engine_->processStereoBlock` → `CavernVerb::processStereoBlock` in place → master-gain
  multiply → `engine_->processOutputStage`, no `process()` call in between) — FR-067a.
  **Recorded (non-gating):** ns per 512-sample block against `kReferenceNs` = 30% of `10 666 666.7` ns
  = `3 200 000` ns (FR-067). **A figure above `kReferenceNs` is surfaced to the user as a Phase 10
  budget finding. It is never absorbed, and no workload is shrunk to pass either threshold.**
- **SC-015 — Portability gate clean.** `node tools/check-portability.js` exits 0 over every file this
  phase adds. *(Roadmap line 541.)*
- **SC-016 — clang-tidy picks the plugin up in both scripts.** `./tools/run-clang-tidy.ps1 -Target
  vorago -BuildDir build/windows-ninja` analyses ≥ 1 file under `plugins/vorago/src` and ≥ 1 under
  `plugins/vorago/tests` with 0 warnings; `-Target all` includes both directories. The `.sh` script
  with `--target vorago` / `--target all` covers the same directories (shown by its file list on
  Linux/macOS CI, or by a static check of the script's case arms, which is what `tools/lint-plugin-roster.js`
  sections 5–6 (`:293–360`) do; SC-025.1 runs that lint). *(Roadmap lines 541–542.)*
- **SC-017 — Generated artifacts and lints are in sync.** `node tools/gen-repo-map.js --check`,
  `node tools/gen-specs-index.js --check` and `node tools/gen-symbols.js --check` exit 0, and all nine
  lints at `tools/hooks/guard-ci-gates.js:38–47` exit 0 (including `lint-plugin-roster.js`, which
  enumerates `plugins/vorago` the moment the directory exists). `specs/INDEX.md` files every
  `vorago-*` slug under "Vorago".
- **SC-018 — Bundle resource guard passes.** `node tools/check-bundle.js
  build/windows-x64-release/VST3/Release/Vorago.vst3` exits 0 and prints
  `OK   Vorago.vst3: editor.uidesc + moduleinfo.json present`.
- **SC-019 — The global parameters reach the chain.** `Vorago_ParamFlowReachesEngine`: (1) with master
  gain normalized 0.0 set before the first block and a note held, the peak over the whole 4 s render is
  `< 1e-6` (possible only because of the first-block snap); (2) polyphony normalized 0.0 → `getPolyphony()
  == 1`, and 1.0 → `6`; (3) sending the same polyphony value twice calls `setPolyphony` once, observed **only** through
  FR-024a.1's call-count seam. A redundant call is invisible in the render and in `getPolyphony()`, so
  no other oracle is accepted. The test first proves the seam sees a real change: a different value
  raises the count by exactly 1; (4) **state before prepare**: `setState` with a stream carrying
  polyphony 2, on a processor that has never been prepared, then `setupProcessing` gives the engine's
  `getPolyphony() == 2` (FR-023.4).
- **SC-020 — The tree is clean after a full configure and build.** After `cmake --preset
  windows-x64-release` and a build of `Vorago`, `git status --porcelain plugins/vorago` is empty (the
  three generated files are ignored).
- **SC-021 — Degenerate `process()` shapes are safe.** A section of `Vorago_ProcessorLifecycle` covering
  each FR-030 shape (no outputs, null `channelBuffers32`, mono bus, `numSamples == 0`, null channel
  pointer, `process()` before `setupProcessing()`). Each returns `kResultOk` and nothing crashes. On
  the not-ready path an output buffer pre-filled with non-zero values is fully zeroed and
  `silenceFlags == 3`. A parameter change sent in a zero-sample block is still applied.
- **SC-022 — MIDI translation is correct.** `Vorago_MidiEventTranslation`, reading the engine through
  FR-026a's test access: (1) NoteOn raises `getActiveVoiceCount()` by one, and the note's slot reads
  `VoiceState::Active` (`vorago_engine.h:1065`); (2) NoteOff, and separately NoteOn with velocity 0,
  move that slot **Active → Releasing** (`voice_allocator.h:39–40, :253–267`) with
  `getActiveVoiceCount()` **unchanged**. The count includes every non-Idle slot
  (`vorago_engine.h:1039–1046`), and a slot reaches Idle only through `voiceFinished()` after the 45 s
  release, so no clause may require the count to drop inside the release; (3) pitch 128 or −1 is
  dropped; (4) a non-note event changes nothing; (5) **offset timing**: render A places NoteOn(48, 100)
  at offset 300 of the first 512 block; render B places it at offset 0 of a block that starts 300
  samples later (a 300-sample block first, then 512s). Both run for at least 1 s past the 3072-sample
  latency, and are compared over the window `[3072 + 300, end)`. **Precondition:** the window's peak in
  A is `>= 1e-4`. **Threshold:** max-abs difference `<= 1e-5`. **Negative control:** render C places the
  note at offset 0 of the first block (a 300-sample timing error) and MUST differ from A over the same
  window by `> 1e-5` max-abs; (6) velocity `0.003` produces a note-on and not a release; (7) **offset
  clamping** (FR-025): a NoteOn at offset 600 in a 512 block renders within `1e-5` max-abs of the same
  NoteOn at offset 511, and one at offset −5 within `1e-5` of offset 0, each over the clause-5 window
  with the same `>= 1e-4` peak precondition; (8) **event ordering** (FR-025): the event list
  `[NoteOn(55) @ 400, NoteOn(48) @ 100]` renders within `1e-5` max-abs of the same two events delivered
  in offset order, over the clause-5 window with the same peak precondition.
- **SC-023 — The macros are inert (the Phase 12 negative control).** A section of
  `Vorago_ParamFlowReachesEngine`: a render with all twelve macro parameters at 1.0 and one with all at
  their defaults, same script, have max-abs difference `<= 1e-5`.
- **SC-024 — Cavern targets are pushed.** A section of `Vorago_ProcessorRendersHeldNote` calls
  `applyCavernTargets` directly on a prepared `CavernVerb`. (1) Pushing non-neutral targets
  (`size 0.9, darkness 0.2, decaySeconds 5, fog 0.8, damperDepth 0.9, mix 0.5, width 0.3`) gives a
  render within `1e-6` max-abs of a reference cavern configured with the seven setters called by hand.
  (2) The same render differs from a default cavern by `> 1e-3` RMS. Clause 2 proves the push has an
  effect, and clause 1 proves it is the right one.
- **SC-025 — Registration is complete, including the sites the roster lint does not check.**
  (1) `node tools/lint-plugin-roster.js` exits 0; (2) `grep -c "auval -v aumu Vrgo KrAt"
  .github/workflows/ci.yml` → 1; `grep -c "Vorago AUv3" .github/workflows/ci.yml` ≥ 1;
  `grep -cE "name: Vorago-(Windows-x64|macOS|Linux-x64)" .github/workflows/ci.yml` → 3; (3)
  `grep -c "/plugins/vorago/" .gitignore` → 3; (4) `grep -c "'vorago', 'Vorago'" tools/gen-specs-index.js`
  → 1; (5) `plugins/vorago/CLAUDE.md` exists and `grep -ci vorago CLAUDE.md` rises at every FR-079
  site; (6) `grep -c "'vorago_tests'" tools/run-cpu-tests.js` → 1 (FR-081). As a diagnostic (not a gate), `grep -ci vorago ci.yml` is compared with `grep -ci seraphis ci.yml`
  (51 today), and any difference is explained in `compliance.md`.
- **SC-026 — `setActive` leaves no tail and does not allocate.** A section of `Vorago_ProcessorLifecycle`:
  hold a note for 2 s, `setActive(false)`, `setActive(true)`, then render 1 s with no notes. The peak
  over that second is `< 1e-6`. Without FR-032 the 45 s release and 20 s cavern decay would show here.
  `setActive(true)` runs inside an `AllocationScope` that reads 0.
- **SC-027 — Shared code untouched.** `git diff --stat <phase-base>..HEAD -- dsp/ plugins/seraphis/
  plugins/shared/` is empty, so the Vorago and Seraphis DSP suites cannot have changed because of
  this phase.

---

## FR → SC traceability

| FR | SC |
|---|---|
| FR-001–FR-006 | SC-001, SC-018, SC-020 |
| FR-007 | SC-020, SC-025.3 |
| FR-008–FR-010 | SC-002, SC-017, SC-025.5 |
| FR-011–FR-014, FR-018 | SC-001, SC-003 |
| FR-015–FR-017 | SC-004 |
| FR-019 | SC-003 (no interface is declared, so none can fail) plus inspection against OQ-7 |
| FR-020, FR-021 | SC-011 |
| FR-022, FR-023 | SC-007, SC-013, SC-019 (clause 4 covers FR-023.4) |
| FR-024, FR-024a | SC-005, SC-006, SC-019 |
| FR-025, FR-026, FR-026a, FR-027 | SC-008, SC-022 (clauses 5, 7 and 8 cover FR-025's timing, clamping and ordering) |
| FR-028, FR-029 | SC-007 |
| FR-030 | SC-021 |
| FR-031 | SC-022 |
| FR-032 | SC-026 |
| FR-033 | SC-013 |
| FR-034, FR-034a | SC-024, SC-008 |
| FR-040–FR-044, FR-048 | SC-009, SC-019 |
| FR-041 | inspection: `ParameterIDs` has no soft-limit ID (SC-009 enumerates exactly fourteen) |
| FR-042 | SC-010.2, SC-023 |
| FR-045–FR-047 | SC-010 |
| FR-050–FR-056 | SC-012, SC-018 |
| FR-060–FR-066 | SC-002, SC-007, SC-012, SC-017 |
| FR-067, FR-067a | SC-014, SC-002.1 |
| FR-070–FR-081 | SC-016, SC-017, SC-025 (clause 6 covers FR-081) |
| (no dsp/ edits) | SC-027 |

---

## Edge cases

**RT-safety boundaries**
- `VoragoEngine::prepare`, `reset`, `silence` and `CavernVerb::prepare` / `reset` are all documented
  as not-audio-thread (`vorago_engine.h:270–276, :414–447`; `cavern_verb.h:354–358, :486–487`). They
  are reachable only from `setupProcessing` and `setActive`, never from `process()`.
- Heap footprint: the engine object is ≤ 808 576 B (`kEngineSizeBound`), and it and the cavern are
  heap members (FR-022). The rings sized at prepare grow with sample rate. The global atmosphere
  capture alone is `2 × 20 s × fs` floats (7.3 MB at 48 kHz, `vorago_engine.h:113–117`), so about
  29 MB at 192 kHz. That is paid once, at prepare, and never on the audio thread.
- `CavernVerb::processStereoBlock` is safe when input and output alias, but `AtmosphereEngine`
  forbids aliasing. The engine already handles that internally (`vorago_engine.h:958–964`), and the
  wrapper never touches the atmosphere directly.
- A non-finite value inside the engine is contained by the engine's own recovery ladder
  (`getNonFiniteRecoveryCount`, `vorago_engine.h:1081`). The wrapper adds no second guard. SC-006's
  finiteness clause is the end-to-end check.

**Parameter extremes**
- Master gain 0 → exact silence from the first sample (SC-019.1, thanks to the snap). Master gain 2.0
  → +6 dB into the limiter, bounded at −0.3 dBFS (SC-006).
- A polyphony shrink with notes held is a **musical release**, not a cut: the allocator issues
  note-offs and the orphaned slots keep ringing (`vorago_engine.h:495–506`). With a 45 s release, a
  shrink from 6 to 1 can leave five tails sounding for up to 45 s. That is intended behaviour, and no
  test may treat it as a stuck voice.
- Macros at 0 or 1 have no audible effect in Phase 11 (FR-042, SC-023).
- Velocity: a very small velocity rounds up to 1, and velocity 1.0 maps to 127 (FR-031).

**Host / event shapes**
- A host block larger than 2048 is sub-divided (FR-026, and SC-008's 4096 arm).
- A `sampleOffset` outside `[0, numSamples)` is clamped (FR-025). Events that arrive out of order
  are applied in offset order.
- `setState()` before `setupProcessing()`: the atomics are loaded, and prepare then reads polyphony
  from them (FR-023.4).
- A mono bus the host forces despite `kResultFalse`: `process()` returns early without touching
  `channelBuffers32[1]` (FR-030).
- pluginval strictness 5 runs 96/192 kHz. At 192 kHz the chain costs about 4× its 48 kHz figure, which
  is above real time for pluginval's offline render but not a failure: pluginval asserts correctness,
  not speed. Prepare at 192 kHz allocates the larger rings described above.

**Sample-rate changes**
- A second `setupProcessing()` at a new rate re-prepares both components. Polyphony, seed and every
  engine-owned setter value survive (`vorago_engine.h:274–276`), and the cavern re-applies all its
  controls (`cavern_verb.h:354–356`). Reported latency stays at 3072 (SC-013), and the master-gain
  smoother re-snaps.

**Seed determinism**
- Engine seed `1u` is applied before every prepare (FR-023.2). Per-slot seeds are fixed per slot and
  never advanced per note (`vorago_engine.h:526–543`), so a render depends only on (seed, config,
  note sequence). Two plugin instances with identical input render identically, and that is accepted
  for Phase 11. A per-instance or user seed is a Phase 12 parameter decision.
- `setActive(false)` calls `silence()`, which clears voice audio state. It does **not** rewind the
  slots' life/ecosystem trajectories the way `prepare()` does. A render after reactivation is
  therefore not guaranteed to match a fresh instance, and no test may assume it does.

**Long time constants**
- 20 s attack and 45 s release (`vorago_voice.h:322–324`), plus a 20 s cavern decay
  (`cavern_verb.h:255`). Day-one renders are sized for these (SC-005 at 8 s with a −80 dBFS floor;
  SC-006 at 30 s). Minutes-scale non-silence and non-runaway assertions belong to Phase 14 (roadmap
  lines 565–568).

**Namespace hazard**
- `Krate::DSP::TestUtils::Vorago` (`tests/test_helpers/vorago_fixtures.h:73`) exists. A `vorago_tests`
  TU that writes `using namespace Krate::DSP::TestUtils;` and then names `Vorago::Processor` gets an
  ambiguous name. Tests MUST refer to plugin types as `::Vorago::…`, or not use that using-directive.
  `vorago_tests` does not need `vorago_fixtures.h` at all: `applyFastAttack` cannot reach a
  processor-owned engine.

**Cross-platform**
- VSTGUI stock views only (FR-054). There is no platform code, and no `std::isnan` in any test that
  injects NaN/Inf (FR-062).

---

## Open Questions

Only one question is explicitly deferred to this spec by the roadmap.

- **OQ-7 — MPE / channel-pressure mapping.** **RESOLVED 2026-09-24 (Clarification Q3/Q4): deferred to
  Phase 12**, per the recommendation below; sustain/CC64 is deferred alongside it, together with the
  `IMidiMapping` addition (FR-009 items 1 and 5). Roadmap Open Question 7 (lines 633–634): *"MPE /
  channel-pressure mapping (pressure → Weight/Pressure macros is a natural fit) — Phase 11/12 scope
  call."* The Phase 10 spec restates it as *"explicitly a Phase 11/12 scope call"*
  (`specs/vorago-phase10-voice-engine/spec.md:409–410`).
  **Recommendation: Phase 12.** Phase 11 ships no `INoteExpressionController` and no `IMidiMapping`
  (FR-019). Three reasons:
  1. the named targets, the Weight and Pressure macros, are **inert** until Phase 12 wires the matrix
     (FR-042; roadmap line 548), so a pressure route in Phase 11 would drive a value nothing reads;
  2. the engine's note API has no per-note expression (`vorago_engine.h:564, :601`), so true per-note
     MPE would need a DSP change, which is outside Phase 11's no-DSP scope. Channel pressure mapped
     onto global macro parameters needs none;
  3. the cost of adding it later is known and was accepted for Seraphis (Seraphis roadmap Phase 13):
     adding `IMidiMapping` or `INoteExpressionController` to a released controller FUID can invalidate
     host-cached class metadata. Vorago is at `0.1.0` and unreleased until Phase 14, so a Phase 12
     addition comes **before** any release and has **no** host-cache cost. That is the strongest
     argument for Phase 12 over "later".

  The user's ruling (Clarification Q3, 2026-09-24) confirms the recommendation: deferred to Phase 12.
  It is recorded in FR-009 item 1 and reflected in FR-019's wording.

---

## Review notes

- Review round (2026-09-24): all fifteen issues accepted; none rejected. SC-001's two findings (fidelity
  and testability) are resolved by one rewrite (a target-scoped warning count with no path filter).
  SC-016's two findings are resolved by pointing at `tools/lint-plugin-roster.js` sections 5–6.
  SC-014 is kept (recorded, non-gating) and made traceable through FR-067, rather than dropped.
  SC-012.5's nightly clause is now a follow-up; the gating UAF evidence is a local ASan run.

---

## Clarifications

### Session 2026-09-24

- Q1: Should the twelve macros ship in Phase 11 as registered-but-inert parameters, given FR-041's "a
  control that does nothing is a defect" rationale? → Yes — register all twelve as inert (14 registered
  parameters total, option (a)); FR-041's rationale is narrowed to controls *overwritten* by another
  path (the soft-limit case), with an explicit unreleased-`0.1.0` exception for not-yet-wired controls;
  Phase 12 wires the macros via `setMacros()`. [FR-041, FR-042, SC-023]
- Q2: What range does the `kPolyphonyId` StringListParameter expose — 1–6 or 1–4? → "1".."6" (to
  `kMaxVoices`, option (a)); the 30% CPU ceiling gates the shipped default of 4 voices, not the
  maximum; the plugin leaf documents that polyphony 5–6 measured 37.99–43.41% of one core (engine
  alone) in Phase 10 and are offered deliberately. [FR-009, FR-013, FR-048]
- Q3 (OQ-7): Is MPE / channel-pressure mapping (`INoteExpressionController` / `IMidiMapping` →
  Pressure/Weight macros) deferred to Phase 12, or taken in Phase 11? → Deferred to Phase 12
  (option (a)); FR-019 stands — no `INoteExpressionController` and no `IMidiMapping` in Phase 11; the
  interface is added in Phase 12, before any release, so there is no host-cache cost. [FR-019, FR-009]
- Q4: Does Phase 11 handle the sustain pedal (CC64)? → No sustain in Phase 11 (option (a)); CC64 and
  every non-note event are ignored (FR-031); sustain is taken in Phase 12 together with the OQ-7
  `IMidiMapping` decision, as a wrapper-side note-off latch, because `VoragoEngine` has no sustain API.
  [FR-031, FR-009]
- Q5: Should SC-014 stay a non-gating measurement, or gate something Phase 11 owns? → Gate the
  **wrapper overhead** only (option (c)): in one test case, `Processor::process()` time versus the
  same chain driven directly (`engine.processStereoBlock` → `CavernVerb` in place → master gain →
  `processOutputStage`), identical seed/polyphony/note-on/block count, `REQUIRE`d `<= 1.05×` the
  direct-chain time; tagged `[performance]` to run in the isolated lane. The absolute composed-chain
  figure against `kReferenceNs` stays recorded and non-gating; a result over the 30% ceiling is
  surfaced to the user, never absorbed. [FR-067, FR-067a, SC-014]
- Q6: What does `getTailSamples()` report, given a 45 s voice release plus a 20 s cavern decay? → SDK
  default `kNoTail` (option (a), the Seraphis template); `silenceFlags = 0` on every rendered block.
  Offline-bounce truncation of the 45 s release plus 20 s cavern decay is a known, accepted risk that
  Phase 14's long-render work revisits. [FR-024]
- Q7: Is `Drones` the right permanent name for the one seed preset category? → Yes (option (a));
  `Drones` is the single permanent seed category, Phase 14 builds its fixed set around it, and
  categories are grow-only and never renamed. [FR-009, FR-051]
- SEED: Confirmed — no seed parameter ships in Phase 11; engine seed `1u` and cavern seed `1` are fixed
  constants, so two instances render identically; Phase 12 adds the seed parameter, before release, and
  day-one render tests rely on this determinism. [FR-023]
- SOFTLIMIT: Confirmed — no soft-limit parameter ships in Phase 11, because `VoragoMacroMatrix::apply()`
  rewrites `setOutputSaturation` every slice (`vorago_macro_matrix.h:968`); output saturation stays a
  Pressure-macro target until Phase 12's base-override surface exists. [FR-041]
- NAME: Confirmed (roadmap OQ-1, decided 2026-09-24) — Vorago is the final name: subtype `Vrgo`,
  `plugins/vorago/`, `vorago_tests`, bundle id `com.krateaudio.vorago`; nothing renames.
  [FR-004, FR-011, FR-015, FR-016]

### Session 2026-09-24 (plan stage)

- R-1: Approve the twelve spec amendments plan §8.2 lists (P-1 to P-10)? → **Approved, all twelve**;
  task T002 writes them into this spec before any code; the four const test seams
  (`masterGainValueForTest`, `cavernForTest`, `globalParamsForTest`, `macroParamsForTest`) are
  in scope. No threshold moves. [FR-024, FR-024a, FR-025, FR-043, FR-048, SC-008, SC-009, SC-012,
  SC-019, SC-022, SC-026]
- R-2: Is a fixed event array of `kMaxEventsPerBlock = 1024` acceptable? → **Yes, 1024 with the
  keep-all overflow rule**: no event is ever dropped; offset order is guaranteed for the first 1024
  events of a host block, and the overflow segment is applied after them in list order. [FR-025,
  SC-022 clause 9]
- R-3: SC-008 (1)'s host-block-size-1 arm may exceed the ~15 s `[long]` threshold. → **Tag only if
  measured over**: T015 records the wall time and tags `Vorago_MidiEventTranslation` `[long]` only
  if it exceeds the threshold; the block-size set `{1, 7, 64, 65, 512, 2048, 4096}` is never reduced.
  [SC-008, FR-066]
- R-4: The task format puts CMake registration in the last group; the plan writes the in-tree CMake
  (plugin and test `CMakeLists.txt`, root `add_subdirectory`) in G4 so tests can compile from then
  on, with a final-group audit. → **Accepted** (Phase 10 precedent); the final CMake task audits and
  makes the one deferred `/wd4459` change. [FR-001, FR-060]
