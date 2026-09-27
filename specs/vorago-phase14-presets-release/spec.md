# Feature Specification: Vorago Phase 14 — Factory Presets & Release Readiness

**Spec slug:** `vorago-phase14-presets-release`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, **Phase 14** (lines 571–590), including the
**variety paragraph added 2026-09-27** (lines 580–590), which this spec treats as the governing
requirement of the whole phase. Also consumed: the Phase 10a line that hands preset use of the ghost
features to this phase (line 496, "both features ship inert, Phase 14 presets engage them"), the
Phase 10 CPU promise (lines 479–485), and the cross-cutting constraints (lines 614–638).
**Deferred into this phase by earlier specs (verified this session):** `getTailSamples()` revisit
(`specs/vorago-phase11-plugin-scaffold/spec.md:447-451`, Clarification Q6 at `:1181-1182`);
`docs/index.html` (`vorago-phase11-plugin-scaffold/spec.md:744-746`, FR-080); the Phase 10a Q1
density-vs-trigger preset choice (`specs/vorago-phase10a-ghost-extension/spec.md:1470-1473`); the
full hand-off list in `specs/vorago-phase12-parameters/spec.md:240` and
`specs/vorago-phase13-ui/spec.md:159`.
**Depends on:** Phases 1–13 (all ✅ per roadmap lines 157–569).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-09-27

---

## Overview

Vorago ships today with an **empty factory library**: the only tracked file under
`plugins/vorago/resources/presets/` is `Drones/.gitkeep`
(`git ls-files plugins/vorago/resources/presets` → one entry), and `makeVoragoPresetConfig()` declares
exactly one subcategory, `{"Drones"}` (`plugins/vorago/src/preset/vorago_preset_config.h:24-30`),
which that file's banner calls additive-only (`:6-12`). The plumbing around the emptiness is complete
and was read this session: the browser button and view are wired (Phase 13 compliance FR-070/FR-071),
the Windows POST_BUILD copy is wired (`plugins/vorago/CMakeLists.txt:121-123` →
`cmake/KratePlugin.cmake:287-310`), the Inno installer ships `presets\*` to
`{commonappdata}\Krate Audio\Vorago` (`plugins/vorago/installers/windows/setup.iss:66-68`), and the
Linux README documents both destinations (`installers/linux/README.txt:29-44`).

This phase produces the **content**, the **tool** that generates it, the **harness** that proves it,
and the **release gate**. Its governing requirement is not "some presets": it is that **the factory set
shows off everything the instrument can do, and no two presets are near-variants of each other**
(roadmap lines 580–590). Every decision below is shaped by that requirement — the coverage matrix is a
functional requirement (FR-010…FR-016), the preset list is derived from the matrix and not from a
per-category quota (C-2), and distinctness is measured in both parameter space and sound space with a
threshold ruled here (C-7).

Four findings from this session shape the design:

1. **`release.yml` already requires a target that does not exist.** The release workflow already lists
   `vorago` (`.github/workflows/release.yml:41-42`), maps every non-Iterum plugin to
   `${PLUGIN}_preset_generator` (`:150-166`, the `*` case at `:162-164`), builds it (`:168-169`), runs
   `./build/bin/${binary} generated-presets` (`:171-174`) and uploads with
   `if-no-files-found: error` (`:181`); the Windows installer then copies **that artifact**, not
   `resources/presets` (`:224`). A Vorago release run today fails at the build step. The generator's
   target name `vorago_preset_generator`, its `build/bin` output location and its `argv[1]` contract are
   therefore **fixed constraints**, exactly as they were for Seraphis (root `CMakeLists.txt:594-669`).
2. **The Vorago state is 428 bytes of plain scalars.** `Processor::getState()` writes the version then
   the v1 block and fourteen packs (`plugins/vorago/src/processor/processor.cpp:637-661`), totalling
   `kStateV2Bytes = 428` (`plugins/vorago/src/plugin_ids.h:28-47`). There is no opaque payload (unlike
   Seraphis's four 541-byte `SpectralState` blocks), so the Seraphis "link the shipped processor and call
   the shipped `getState()`" pattern (C-3) applies with no hand-skipped blocks at all.
3. **The Vorago test fixture cannot be reused by a tool.** Seraphis's generator drives presets through
   `seraphis_test_fixture.h`, which includes no Catch2 (verified: no `catch` include). Vorago's
   `plugins/vorago/tests/vorago_test_fixture.h:38` includes `<catch2/catch_test_macros.hpp>` and its
   `ProcessorFixture` uses `REQUIRE` in its constructor and `prepare()` (`:186-202`). The generator
   therefore needs a Catch2-free host drive, shared with the test twin so the two cannot diverge
   (FR-021).
4. **The shipped default voice takes 155 s to reach sustain.** `VoragoVoice::kDefaultStageTimesMs` is
   `{20000, 30000, 45000, 60000, 0, 0}` with release `45000` ms
   (`dsp/include/krate/dsp/systems/vorago_voice.h:320-324`); Growth mode's default is 120 s
   (`:333-334`). A "seconds-scale" sweep in the Seraphis style (sustain window `[A+1, A+4]`) would measure
   an unfinished swell. The roadmap asks for **minutes-scale** non-silence/non-runaway assertions
   (line 576-578); C-6 derives every window from each preset's own decoded envelope.

---

## Scope

In scope:

- The **fixed category set** (C-1) in `makeVoragoPresetConfig()` and on disk, `Drones` kept verbatim.
- The **preset × capability coverage matrix** (C-2, FR-010…FR-016) and the **factory library** derived
  from it — `.vstpreset` files with their `Info` metadata.
- The **generator** `tools/vorago_preset_generator.cpp`, its shared definitions header
  `tools/vorago_preset_defs.h`, and the `vorago_preset_generator` / `generate_vorago_presets` CMake
  targets on the contract `release.yml` already assumes.
- The **validation harness** in `vorago_tests`: container, round-trip, metadata↔filesystem, browser
  scan, committed-tree semantic equality, RT-safe loading, the **minutes-scale long-render sweep**, the
  **capability-verification (ablation) arm**, and the **distinctness arms** (parameter space + sound
  space).
- The three items earlier specs deferred here: `getTailSamples()` (FR-060), the ghost density-vs-trigger
  preset choice (FR-061), `plugins/vorago/docs/index.html` (FR-062).
- **Release-roster registration** (`.claude/workflows/release-readiness.js`,
  `.claude/skills/release/SKILL.md`, `tools/check-preset-generator-determinism.js`) and the **release
  gate** via the `release-readiness` flow (roadmap line 578).
- The **dedicated nightly preset-sweep lane** (FR-066): new job(s) in `long-tests-nightly.yml` and the
  `[long]~[vorago-sweep]` filter in `ci.yml`'s nightly "Run Tests" step.

## Non-goals (owned elsewhere or out of scope)

- **New DSP classes, new UI outside the ecosystem page, new parameter *types*.** No new DSP component is
  created — the ratified ecosystem rule-knob parameters (Clarification Q2, session 2026-09-27; C-2.3,
  FR-070…FR-076) wire *existing* `EcosystemEngine::set*` setters
  (`dsp/include/krate/dsp/systems/ecosystem_engine.h:469-707`) to new registered `float`/enum parameter
  IDs on the Phase 13 ecosystem page only; every registered parameter *type* stays frozen
  (`plugin_ids.h:82-83`). Unlike a normal Non-goal, the registered **surface size** and
  `kCurrentStateVersion` DO move in this phase: 108 IDs → 108 + |R| (the ratified roster, C-2.3), version
  **2 → 3**, with v2 presets/state loading at the new knobs' current hard-coded defaults (FR-072). This is
  the one scope-widening exception Clarification Q2 authorises, and it is gated end-to-end by the
  audibility-probe roster ratification (FR-071) before any ID, format, control or preset is authored.
- **Retuning DSP or macro rows to make presets sound different.** Variety is achieved by authoring the
  shipped surface. If the harness shows a capability cannot be foregrounded from the shipped surface,
  that is a stop-and-surface finding (FR-017), not licence to edit `dsp/`.
- **Reading the `Info` chunk back at runtime** (`PresetManager` derives subcategory from the directory,
  `plugins/shared/src/preset/preset_manager.cpp:95-103`); this phase writes and tests metadata only.
- **User preset UX** — shipped in Phase 13 (browser, save via the shared `PresetManager`).
- **macOS/Linux factory-preset installers** — `krate_plugin_install_presets` is Windows-only by
  construction (`cmake/KratePlugin.cmake:290-292`); the Linux path is documented manual copy.

---

## Existing components (verified this session)

| Component | Path | What is reused (real signature / fact) |
|---|---|---|
| `Vorago::Processor` state I/O | `plugins/vorago/src/processor/processor.cpp:569`, `:637-661` | `tresult PLUGIN_API setState(IBStream*)`, `tresult PLUGIN_API getState(IBStream*)` — the **only** serializer the generator uses (C-3) |
| `Processor::processParameterChanges` | `plugins/vorago/src/processor/processor.h:206` (private) | reached only through `process()` carrying `IParameterChanges` (C-4) |
| State constants | `plugins/vorago/src/plugin_ids.h:23`, `:28-47`, `:52`, `:56` | `kCurrentStateVersion = 2`, `kStateV2Bytes = 428`, `kProcessorUID`, `kControllerUID` |
| Pack load/save free functions | `plugins/vorago/src/parameters/*_params.h`, called in order at `processor.cpp:643-659` | `save*Params` / `load*Params` — the typed decode for harness reads (FR-031) |
| Dropdown rosters | `global_params.h:109-126`, `noise_params.h:219-247`, `resonance_params.h:146-148`, `ecology_params.h:112-114`, `body_params.h:121-125`, `envelope_params.h:141-143`, `space_params.h:214-215`, `ghost_params.h:87-89` | polyphony 1–6 (default idx 3), 16 seeds, 4 noise models, 12 noise types, 3 anchor modes, 3 loop filter modes, 11 body materials, 2 envelope modes, freeze Off/On, ghost triggers Off/On |
| Envelope ranges/defaults | `envelope_params.h:42-50`, `:60-67`; `vorago_voice.h:313-338` | stage times [0, 120000] ms, growth [1, 120] s; sustain point 4; `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }` |
| `makeVoragoPresetConfig()` / `makeVoragoPresetTabLabels()` | `plugins/vorago/src/preset/vorago_preset_config.h:24-30`, `:36-44` | the category list (extended here, C-1) and the browser tab list |
| `Krate::Plugins::PresetManagerConfig` | `plugins/shared/src/preset/preset_manager_config.h:19-24` | field order `processorUID, pluginName, pluginCategoryDesc, subcategoryNames` |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h:55`, `:70`, `:75`, `:120`; `.cpp:95-103`, `:264-275` | overrides ctor, `scanPresets()`, `getPresetsForSubcategory()`, `static bool isValidPresetName(const std::string&)`; exact-match subcategory; the six-attribute (+`Comment`) `Info` XML the factory files must match |
| `krate_plugin_install_presets` | `cmake/KratePlugin.cmake:287-310` | POST_BUILD copy to `%PROGRAMDATA%/Krate Audio/<target>` (already called, `plugins/vorago/CMakeLists.txt:123`) |
| Seraphis generator pattern | root `CMakeLists.txt:594-669`; `tools/seraphis_preset_generator.cpp:1-80`; `tools/seraphis_preset_defs.h:50-112` | processor.cpp compiled into the tool, `KrateDSP KratePluginsShared sdk` only, `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`; `{ParamID, normalizedValue}` definitions in a data-only header shared with the test |
| Generator determinism gate | `tools/check-preset-generator-determinism.js:1-60` | byte determinism + idempotence of one binary; **hard-coded to Seraphis** (`:52-55`, `:111-113`, `:209`) — extended, not copied (FR-024) |
| `ProcessorFixture` | `plugins/vorago/tests/vorago_test_fixture.h:166-330` | `prepare(double sr, int32 maxBlock)`, `processBlock(n, IEventList*, IParameterChanges*)`, `renderScript(...)`, `reserveCapture(n)`; `MultiParamChanges` (`:118`); Catch2-dependent (`:38`) |
| Engine constants | `dsp/include/krate/dsp/systems/vorago_engine.h:225`, `:255`, `:439` | `kMaxVoices = 6`; `kOutputCeilingDb = -0.3f`; `limiter_.setCeilingDb(kOutputCeilingDb)` |
| Macro roster | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h:106-121` | `enum class VoragoMacro : std::uint8_t { Darkness = 0, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass, Count }` |
| Ecosystem kinds → destinations | `ecosystem_engine.h:282-288`; `vorago_voice.h:254-261`, `:1705-1713` | `enum class Kind { Partial, Resonator, Noise, Feedback, Ghost }`; `kindForFamily()` maps BloomTrigger→Partial, NoiseWake→Noise, PeakWake→Resonator, LoopWake→Feedback, GhostBurst→Ghost |
| Noise model semantics | `dsp/include/krate/dsp/systems/noise_organism.h:126-131`, `:463` | `enum class NoiseOrganismModel { Direct, FilteredWind, GranularDust, MetallicHiss }`; the noise **type** is effective only in `Direct` |
| Body materials | `dsp/include/krate/dsp/systems/continuous_body.h:84-100` | `enum class BodyMaterial` — Glass, Strings, MetalPlate, Chamber, Ice, StoneChamber, SteelTank, WoodenHull, CathedralColumn, CavernWall, GlassSphere (`kNumMaterials = 11`) |
| Cavern freeze / decay | `dsp/include/krate/dsp/effects/cavern_verb.h:255`, `:626`, `:735` | `kDefaultDecaySeconds = 20.0f`, `setDecaySeconds(float)`, `setFreeze(bool)`; decay range [0.5, 60] s (`space_params.h:58-59`) |
| Ghost features (Phase 10a) | `vorago_engine.h:978-1015`; `ghost_params.h:35-40` | `setGhostReverseProbability(float)`, `setGhostEventTriggers(bool)`; plugin defaults reverse 0.0, triggers Off (inert) |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h:52-60`, `:122-124` | `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `struct RenderFingerprint`, `compareFingerprints(...)` |
| Level-invariant spectral metrics | `tests/test_helpers/vorago_fixtures.h:245`, `:316`, `:344`, `:441`, `:502` | `bandEnergyDb(span, sr, loHz, hiHz)`, `crestFactorDb(span)`, `blockRmsDb(span, …)`, `perBandTotalVariation(span, sr)`, `perBinMagnitudeFlux(span, sr)` |
| Stereo correlation | `tests/test_helpers/low_frequency_metrics.h:478` | `float calculateCorrelation(const float* a, const float* b, std::size_t n)` |
| Allocation instruments | `tests/test_helpers/allocation_detector.h:111`, `:149` | `AllocationScope` (process-global), `ThreadScopedAllocationScope` (thread-filtered) |
| CPU budget constants | `dsp/tests/unit/systems/vorago_perf_budget.h:78`, `:82`; `plugins/vorago/tests/integration/processor_cpu_test.cpp:82-91` | `kBlockBudgetNs`, `kReferenceNs = kBlockBudgetNs * 0.30` (3 200 000 ns); `kCpuNotes{36, 40, 43, 47}`, `kCpuPolyphony = 4` |
| Release rosters | `.claude/workflows/release-readiness.js:13-22`; `.claude/skills/release/SKILL.md:15-31` | `PLUGIN_MAP` and the target/bundle table — **both omit `vorago`** |
| Long lane | `.github/workflows/long-tests-nightly.yml:1-31`, `:75-80`; `ci.yml:322`, `:365-376`, `:608`, `:1069` | `[long]` cases run nightly on three OSes by calling `ci.yml` with `long-tests: true`; each OS's single sequential "Run Tests" step has `timeout-minutes: ${{ inputs.long-tests && 40 || 20 }}`; `vorago_tests` is in every CI leg |

## New components

No DSP class and no plugin runtime class is created. The new names are tool/test-side only.

| Name | Kind / layer | Location | ODR sweep (`grep -rn -E "(class|struct|enum class) <Name>\b" dsp/ plugins/ tools/ tests/`) |
|---|---|---|---|
| `Vorago::PresetDefs::VoragoPresetDef` | struct, tool data (no DSP layer) | `tools/vorago_preset_defs.h` | **0 hits** |
| `Vorago::PresetDefs::ParamSetting` | struct | `tools/vorago_preset_defs.h` | 1 hit: `Seraphis::PresetDefs::ParamSetting` (`tools/seraphis_preset_defs.h:59`) — **different namespace, never linked into the same target**; no TU may `using namespace` both (`plugins/vorago/CLAUDE.md`, near-name hazard) |
| `Vorago::PresetDefs::Capability` | `enum class`, the matrix cell roster (C-2) | `tools/vorago_preset_defs.h` | **0 hits** |
| `VoragoTest::PresetDescriptor` | struct, the C-7 sound-space descriptor | `plugins/vorago/tests/preset_test_support.h` | **0 hits** |
| `VoragoTest::SweepTimeline` | struct, C-6 timeline | `plugins/vorago/tests/preset_test_support.h` | 1 hit: `SeraphisTest::SweepTimeline` (`plugins/seraphis/tests/preset_test_support.h:592`) — different namespace and target |
| `VoragoTest::DecodedPresetState` | struct, typed decode (FR-031) | `plugins/vorago/tests/preset_test_support.h` | 1 hit: `SeraphisTest::DecodedPresetState` (`plugins/seraphis/tests/preset_test_support.h:387`) — different namespace and target |
| Catch2-free host drive (name chosen by the plan) | header, tool+test shared | `plugins/vorago/tests/` or `tools/` (plan decides) | plan MUST sweep its chosen name before creating it |

`AuditionStimulus` (`tools/seraphis_preset_defs.h:67`) is **not** reused as a name: C-6 fixes one common
stimulus, so no per-preset stimulus type exists.

---

## Decisions

### C-1 — The category set is fixed and additive-only; `Drones` is kept verbatim

Vorago ships **seven** subcategories, in this order:

```
Drones · Abyss · Caverns · Organisms · Machines · Textures · Ghosts
```

`Drones` keeps its exact spelling and directory: `PresetManager::parsePresetFile` matches the parent
directory name against `subcategoryNames` by exact `==` and leaves `subcategory` empty on a miss
(`preset_manager.cpp:95-103`), so a rename orphans every user preset saved against it
(`plugins/vorago/CLAUDE.md`, decision 2; `vorago_preset_config.h:6-12`). Names are single-word ASCII —
they are simultaneously directory names, `subcategoryNames` entries, browser tab labels
(`makeVoragoPresetTabLabels()`, `vorago_preset_config.h:36-44`) and `MusicalCategory` /
`MusicalInstrument` attribute values (`preset_manager.cpp:271-272`). Categories are **listener-facing
browse labels, not coverage quotas**: C-2 derives the preset list, then each preset is filed under the
category that best describes how it sounds. *(Ratified — Clarifications session 2026-09-27, Q6: this
exact seven-name, `Drones`-first order; see FR-001.)*

### C-2 — Variety governs: the preset list is derived from a coverage matrix

#### C-2.1 The matrix

The roadmap requires every section, every macro and every distinct ecosystem behaviour to be
**foregrounded** by at least one preset (line 582-585). "Foregrounded" is made testable two ways:
**ablation-verified** cells (the capability measurably changes the preset's sound when it is removed,
C-7.4 — at the stronger **primary bar** when the cell is the preset's primary) and **state-verified**
cells, each with an audibility conjunct wherever the capability can be silenced, (the preset's decoded state engages a shipped enumeration value or
range extreme). The cell roster is the `Capability` enum:

**Group S — sections (ablation-verified; the ablation twin sets the listed IDs to the listed normalized
values, everything else unchanged):**

| Cell | Section (roadmap) | Ablation override |
|---|---|---|
| S1 | Noise organism (Phase 2, lines 195-205) | `kNoiseLevelId` 300 → 0.0 (−96 dB) |
| S2 | Resonance drift (Phase 3, 229-236) | `kResonanceMixId` 401 → 0.0 |
| S3 | Spectral smear (Phase 4, 251-258) | `kSmearAmountId` 700 → 0.0 and `kSmearDecoherenceId` 701 → 0.0 |
| S4 | Feedback ecology (Phase 5, 284-292) | `kEcologyMixId` 500 → 0.0 |
| S5 | Subharmonic (Phase 6, 318-328) | 610, 611, 612 → 0.0 (−60 dB each) **and** `kSubLevelOffsetId` 600 → 0.0 (−24 dB; the offset is linear over [−24, +24] dB and "the engine setter has no clamp", `sub_params.h:7-12`, so leaving an authored +24 dB offset in place would keep the "ablated" subs near −36 dB) |
| S6 | Harmonic bloom (Phase 7, 351-360) | `kBloomDepthId` 1300 → 0.0 |
| S7 | Ecosystem (Phase 8, 390-402) | `kEcosystemDepthId` 900 → 0.0 |
| S8 | Cavern space (Phase 9, 431-440) | `kSpaceMixId` 1105 → 0.0 |
| S9 | Ghost / atmosphere (Phase 10a, 504-509) | `kGhostPeakLevelId` 1400 → 0.0 |
| S10 | Acoustic body with the dark material table (roadmap 123, 465-467) | `kBodyMixId` 1003 → 0.0 |

Macro rows write *on top of* these bases (`vorago_macro_matrix.h:125-139`; the MB route, 
`plugins/vorago/CLAUDE.md` route table), so an ablation removes the **base** contribution only. A
preset whose section is audible only through a macro fails its S-claim and is re-authored.

**Group M — the twelve macros (ablation-verified):** M1…M12 in `VoragoMacro` order
(`vorago_macro_matrix.h:106-121`). A preset foregrounds macro *m* iff its stored macro value is displaced
from the registered default by **≥ 0.5** (Gravity, bipolar around 0.5 per `macro_params.h:38`: by
**≥ 0.35**) **and** the ablation twin with that macro reset to its registered default passes C-7.4.

**Group E — the five ecosystem behaviours (route-isolated ablation):** E1 Partial→bloom, E2
Resonator→resonance peaks, E3 Noise→noise wake, E4 Feedback→loop wake, E5 Ghost→ghost bursts — the
`EcosystemEngine::Kind` destinations (`ecosystem_engine.h:282-288`), mapped by the constexpr
`kindForFamily()` (`vorago_voice.h:1705-1713`). The routes are fixed and all live at once: there is no
per-route control, and the per-scheduler `lastEventTarget_` (`vorago_voice.h:1873`, `:2387`) is private,
so no shipped observable counts events per family. The conjunction "verifies S7 and verifies the
destination section" is **not** a route measurement (both halves are independent ablations, and one
preset verifying S1, S2, S4, S6, S7 and S9 would satisfy all five at once without any route being heard),
so it is not used. Instead, with destination map E1↔S6, E2↔S2, E3↔S1, E4↔S4, E5↔S9:

- `R_k` = P with the S-overrides of the **other four** destinations applied (destination *k* soloed among
  the five ecosystem destinations; non-destination sections untouched), ecosystem as authored;
  `R_k⁰` = `R_k` with `kEcosystemDepthId` 900 → 0.0.
- `R_∅` / `R_∅⁰` = the same pair with **all five** destinations ablated (the ecosystem's residual effect
  on anything that is not a route destination).
- E*k* verifies iff `d(R_k, R_k⁰) ≥ max(D, 2 · s(R_k))` **and** `d(R_k, R_k⁰) ≥ d(R_∅, R_∅⁰) + D_abl`
  (the change is attributable to route *k*, not to a route-independent effect), where D is the C-7.4 bar
  for the claim's role (primary or secondary). All four renders run on P's own timeline to the end of
  `Sus`; `s(R_k)` is the half-window self-distance of `R_k`'s `Sus`.
- Macro rows can still feed a "soloed-away" destination (ablation removes bases only, as for Group S);
  a route whose E-measurement is carried by a macro rather than the colony fails and is re-authored.
- **Foregrounding:** coverage of E*k* (FR-013) requires ≥ 1 preset whose **primary** is E*k*, so the five
  behaviours are foregrounded by five distinct presets, each animating a different destination; an E
  cell verified only as a secondary does not count toward coverage.
- **Extension (Clarification Q2, C-2.3, FR-075).** Group E gains one pair of cells per ratified
  ecosystem rule-knob (the roster `R` from FR-070/FR-071): `E6.lo`/`E6.hi`, `E7.lo`/`E7.hi`, … — each
  verified by plain C-7.4 ablation (the knob's ID reset to its registered default), never the
  route-isolated measurement above (that measurement is specific to the five fixed kind→destination
  routes).

**Group D — shipped enumerations and ranges (decoded state, FR-031, plus an audibility conjunct
wherever the capability can be silenced):** a state match alone is never "foregrounded" when the
capability can sit under a muted section, so every D cell below either carries an audibility conjunct
(an S verification, or a **reversion ablation** — the cell's IDs reset to their registered defaults,
scored by C-7.4) or is marked *always audible*.

| Cell(s) | Requirement |
|---|---|
| D1.1–D1.11 | each of the 11 body materials is Material A or B of ≥ 1 preset **that verifies S10** (previously required only for the **6 dark** materials, StoneChamber…GlassSphere; a material in a muted body is not foregrounded) |
| D2 | ≥ 1 preset with body blend in [0.35, 0.65] that verifies S10 (both bodies heard) |
| D3.1–D3.4 | each of the 4 noise models is the model of ≥ 1 slot in a preset that verifies S1 |
| D4.1–D4.12 | each of the 12 selectable noise types is the **effective** type of a `Direct` slot in ≥ 1 preset **that verifies S1** (a type under `kNoiseLevelId` at −96 dB, `noise_params.h:48`, does not count) |
| D5.1–D5.3 | Free, Keyed, Hybrid anchor each in ≥ 1 preset that verifies S2 |
| D6.1–D6.3 | Lowpass, Bandpass, Highpass each on ≥ 1 loop of a preset that verifies S4 |
| D7.1–D7.3 | f/2, f/4, fifth-below each the **loudest** stored sub tone of ≥ 1 preset that verifies S5 |
| D8.1–D8.2 | Standard and Growth envelope each in ≥ 1 preset — *always audible* (the envelope shapes every voice) |
| D9.1–D9.2 | attack span `A` (C-6) ≤ 10 s in ≥ 1 preset; `A` ≥ 90 s in ≥ 1 preset — *always audible* |
| D10.1 | **Freeze audibly holds a field** — the C-6 arm 4 Freeze-On criteria including the absolute floor pass for ≥ 1 preset, verified via the freeze-gesture render (Clarification Q1, C-6 arm 4) |
| D10.2 | Space Freeze Off in ≥ 1 preset — *default state* |
| D11 | ghost reverse probability ≥ 0.5 in ≥ 1 preset that verifies S9 (roadmap line 496) |
| D12.1–D12.2 | Ghost Event Triggers On in ≥ 1 preset that verifies S9 (line 496); Off in ≥ 1 — D12.2 is *default state* |
| D13.1–D13.2 | `kEventsRateScaleId` (800) ≤ 0.3 in ≥ 1 preset; ≥ 3.0 in ≥ 1 — each in a preset that verifies S7 **and** whose reversion ablation (800 → its 1.0 default, normalized 0.5, `events_params.h:7`) passes C-7.4 |
| D14.1–D14.2 | `kLifeBreathingDepthId` (1500) ≥ 0.7 in ≥ 1 preset; `kLifeTidalDepthId` (1502) ≥ 0.7 in ≥ 1 — each in a preset whose ablation of that depth to 0.0 passes C-7.4 |

**Default-state cells** are D8.1, D10.2, D12.2, and every other cell the **default surface** itself
verifies when run through the same harness (FR-012 evaluates the default surface as a pseudo-preset for
this purpose only). They count toward coverage but are **ineligible as primaries** (C-2.2).

#### C-2.2 Showcase rules (the roadmap's "near-variant" and "showcases nothing" clauses)

- **Unique, eligible, foregrounded primary.** Every preset definition names exactly one **primary**
  cell; no two presets name the same primary; the primary MUST NOT be a default-state cell (C-2.1); and
  the primary MUST verify at the **primary bar** (C-7.4: `d ≥ max(F, 2·s)`, i.e. removing the showcased
  capability turns the preset into what would count as a different preset). A state-verified D primary
  is scored at the primary bar by its reversion ablation (its IDs reset to registered defaults, each
  twin rendered on its own C-6 timeline when the reversion changes the envelope).
- **Non-subset showcase (the roadmap's clause itself).** "A preset that showcases nothing another preset
  already shows is a defect" (line 589-590) is tested on *verified* sets, not labels: for every ordered
  pair (P, Q), P ≠ Q, **at least one cell P claims is not verified by Q**. Q's verification of P's
  claims is evaluated by the same predicates on Q's renders (lazily, state conjuncts first: the first
  claim of P that Q fails ends the pair). Requiring a *claimed* witness is strictly stronger than
  requiring `V(P) ⊄ V(Q)` over full verified sets, so under-claiming cannot help a preset pass. A
  primary name alone therefore cannot carry a preset whose whole showcase sits inside another's.
- **Declared = verified.** Every cell a definition *claims* (primary + secondaries) MUST verify. A claim
  that fails is a defect in the preset, never a relaxed predicate.
- **Complete coverage.** Every cell of C-2.1 is verified by ≥ 1 preset.
- **No per-category quota.** The library size N is derived from this matrix, not authored to a
  per-category count, and targets the **35–45** band (Clarification Q4, session 2026-09-27): every S, M
  and E cell (C-2.1) — including the E cells C-2.3 adds for the ratified ecosystem rule-knob roster — is
  the primary of its own preset, plus the most audible discrete D cells (the 11 body materials, the 4
  noise models, and the envelope-mode/attack-span extremes, D8/D9) authored as primaries; the ≥ 3-per-
  category floor (FR-004) and SC-013's wall-clock budget (C-10) still bind within that band.

#### C-2.3 What "distinct ecosystem behaviour" means: routes, plus a ratified rule-knob roster (Clarification Q2)

The ecosystem's rule knobs (kernel sigma, exchange, predation, sync, affinity …) exist on the component
(`ecosystem_engine.h:469-707`) but today **no registered parameter reaches them**: `grep "ecosystem_\.set"`
in `vorago_voice.h` finds only `setSeed` (`:1546`); the plugin exposes `kEcosystemDepthId` (900) alone,
plus the Life macro and the seed. The five **kind→destination routes** (Group E1…E5, unchanged) remain
one axis of "distinct ecosystem behaviour" — *which part of the drone the colony animates*.

**Clarification Q2 (session 2026-09-27) widens this phase's scope, inside Phase 14, to add a second
axis: a curated roster of the ecosystem's own rule knobs becomes registered parameters.**

- **Roster selection (FR-070).** Candidates: predation, sync rate, exchange rate, crowding, forage/feed/
  graze rates, leak, move/max speed, kernel sigma, freq drift, per-kind affinities
  (`ecosystem_engine.h:469-707`). The plan's **first task** is a hidden audibility probe: render each
  candidate knob at its two extremes (registered default vs. extreme, everything else unchanged) and
  measure how distinct each knob's extremes sound, using the C-7.2 descriptor distance on the probe
  renders. The probe selects a **curated roster `R`, roughly 4–6 knobs**, ranked by measured audibility.
- **Ratification gate (FR-071).** The plan MUST **stop and surface** the proposed roster `R` — the
  candidates, their measured audibility, and the recommended cut — to the user, and MUST NOT author any
  new parameter ID, the state v3 format, any UI control, or any preset referencing `R` until the user
  ratifies it. This is the single point in the phase where scope can still be corrected before it is
  built into the surface.
- **Consequences once `R` is ratified:**
  - New parameter IDs are added to the ecosystem block for every knob in `R` (FR-072); the registered
    surface grows from 108 to 108 + |R| IDs.
  - The state format bumps to **v3** (`kCurrentStateVersion = 3`), with **v2 load compatibility**: a v2
    stream (or a v2 factory preset) loads with every knob in `R` at its *current hard-coded default*
    (FR-072).
  - Controls for every knob in `R` are added to the Phase 13 ecosystem page (`resources/editor.uidesc`);
    the page union, bound-ID count and allowlist tests are updated to include them (FR-073).
  - Processor, controller, uidesc, param-table and state tests are updated for the new IDs and the v3
    format (FR-074).
  - **Coverage matrix Group E is extended** (FR-075): for every ratified knob `k ∈ R`, its two extremes
    become new primary `Capability` cells (named `E6.lo`/`E6.hi`, `E7.lo`/`E7.hi`, … in ratification
    order), each verified by C-7.4 ablation (the knob's ID reset to its registered default) at the
    primary or secondary bar per C-2.2 — **in addition to**, never in place of, the five kind→destination
    routes E1…E5.
- This is the only Non-goal exception this phase authorises (see the updated Non-goals entry); every
  other new-parameter surface stays out of scope.

### C-3 — The generator reuses the shipped serializer; there is no `vorago_preset_format.h`

The tool compiles `plugins/vorago/src/processor/processor.cpp` into the executable (Vorago, like Seraphis,
builds every plugin `.cpp` into one `smtg_add_vst3plugin` MODULE, `plugins/vorago/CMakeLists.txt:9-33`)
and produces each `Comp` chunk by calling the shipped `Processor::getState()`. No state layout is
duplicated, so no `tools/vorago_preset_format.h` and no format-compat test exist. `processor.cpp`
includes no VSTGUI header (its includes are `processor.cpp:11-36`; `dataexchange.h` at `:20` is SDK
`public.sdk/source/vst/utility`, not VSTGUI), so the tool links `KrateDSP KratePluginsShared sdk` only.
The `.vstpreset` class id is derived at run time from `Vorago::kProcessorUID`, never a string literal.

### C-4 — Values reach the state the way a host delivers them

Each preset is driven `initialize(nullptr)` → `setupProcessing` → `setActive(true)` → **one**
`process()` carrying every authored `{ParamID, normalized}` point → `getState()`. Stored values are
therefore exactly what the shipped denormalizers produce; a preset can never encode a value the plugin
cannot reach. Definitions are authored in **normalized** units; untouched IDs keep their registered
defaults. `kSustainPedalId` (4) and `kChannelPressureId` (5) are never authored (never persisted,
`plugins/vorago/CLAUDE.md` FR-045).

### C-5 — Factory operating point

Every preset stores **polyphony ≤ 4** (the gated default; `plugins/vorago/CLAUDE.md` decision 4 —
polyphony 5/6 measured 37.99–43.41 % of a core and are an opt-in cost, never a factory default) and
authors `kOutputSaturationId` only through its MB route (decision 3). Seeds are free.

### C-6 — One timeline per preset, derived from the preset's own envelope

| Symbol | Definition |
|---|---|
| `A` | attack span, s: Standard → Σ stage0..3 ms / 1000; Growth → growth duration s (stage times are zeroed in Growth, `vorago_voice.h:335-338`) |
| `Rel` | `kEnvelopeReleaseId` (1205) value / 1000 |
| `RT60` | the **effective** cavern decay, s: the shipped `VoragoMacroMatrix::computeCavernTargets().decaySeconds` (`vorago_macro_matrix.h:1129-1134`) evaluated with the preset's decoded `kSpaceDecayId` (1102) installed as the `CavernDecaySeconds` target base (`setTargetBase`, `:1006`; the per-target base override, `:58-64`) and the preset's decoded macro values, then clamped to the cavern's [0.5, 60] s range (`space_params.h:58-59`). Never the stored decay alone: Depth adds up to +25 s and Age up to −14 s on that target (`:682-687`, `:348-353`). No macro row targets an envelope stage or release (verified: no such target in `vorago_macro_matrix.h`), so `A` and `Rel` are the decoded stored values |
| `M1, M2, M3` | three 60 s minutes of the hold: `[A + 5, A + 65]`, `[A + 65, A + 125]`, `[A + 125, A + 185]` |
| `Sus` | `= M1`, `[A + 5 s, A + 65 s]` — used by non-silence reference, ablation and E arms |
| `H` | NoteOff instant `= A + 185 s`: **three full minutes of held drone after the swell settles**; `[0, H]` is the roadmap's NoteOn-only hold |
| `Tail` | Freeze Off: `[H + Rel + RT60 + 5 s, H + Rel + RT60 + 15 s]`; Freeze On: `[H + Rel + 10 s, H + Rel + 70 s]` |
| `Total` | end of `Tail` |

**Authoring ceilings (FR-008):** `A ≤ 180 s`, `Rel ≤ 60 s`. Worst `Total` = 180 + 185 + 60 + 60 + 15 =
500 s. Nothing is truncated for a real preset; the ceiling bounds what ships.

**Stimulus:** one note, **MIDI 36 (C2), velocity 100** (`kVelocity100 = 100.0f / 127.0f`, as
`plugins/vorago/tests/integration/soak_test.cpp:44`) at t = 0, block 512, **48 000 Hz**, for every
preset and every sound-space arm. A common stimulus is deliberate: per-preset pitches would let two
presets "differ" by transposition. The chord arm uses `kCpuNotes {36, 40, 43, 47}`
(`processor_cpu_test.cpp:85`).

**Arms on this render:**

1. **Bounded** over `[0, Total]`: every sample finite by bit pattern (never `std::isnan`); sample peak ≤
   `10^(-0.3/20)` = 0.9661 (`kOutputCeilingDb`, `vorago_engine.h:255`; the constant the soak test uses,
   `soak_test.cpp:45`); **non-runaway**: every 10 s stereo-RMS window ≤ −6 dBFS (a drone pinned at the
   limiter is runaway even when the limiter holds the peak).
2. **Non-silence** over `[A, H]`: every 10 s stereo-RMS window ≥ −60 dBFS.
3. **Neither dies nor explodes:** RMS(`[H − 60 s, H]`) within **[−18 dB, +12 dB]** of RMS(`Sus`).
4. **Tail** (from the decoded freeze toggle only): Freeze Off → RMS(`Tail`) ≤ RMS(`Sus`) − 40 dB (a
   stuck voice, a self-sustaining loop or an endless ghost replay fails); Freeze On → over the 60 s
   `Tail` the final-10 s RMS ≤ loudest-10 s RMS + 1.0 dB (non-growing) **and** ≥ loudest − 6.0 dB
   (the frozen field is held, not dying) **and**, the absolute floor, loudest-10 s RMS(`Tail`) ≥
   RMS(`Sus`) − **20 dB** (X = 20 dB, ratified — Clarifications Q3/OQ-2, this value fixed, not
   pilot-calibrated). The floor exists because the two
   relative inequalities pass vacuously on silence: `blockRmsDb` floors at −240 dBFS
   (`vorago_fixtures.h:343`), so an all-floor tail is "held and non-growing". Because the voices have
   released by `Tail`, whatever remains is the cavern's frozen field, so the floor is also the tail-window
   S8 verification that D10.1 requires (the `kSpaceMixId` → 0 twin of the same render has only the
   released-voice tail, which the Freeze-Off arm bounds at `Sus` − 40 dB).
   **Load-then-play precondition.** A stored-On stream would be loaded before the NoteOn at t = 0, and the
   cavern's engine mutes every send into the tank as freeze latches — "freeze mutes all three sends", the
   injection riding `(1 − freezeRamp)` (`aether_reverb.h:409-410`; `CavernVerb::setFreeze` forwards to it,
   `cavern_verb.h:735-737`) — so a preset loaded with freeze already On is predicted to freeze an
   **empty** tank and fail the floor. **How factory freeze is shipped and verified (Clarification Q1,
   session 2026-09-27, ratifying the recommended protocol): the freeze-gesture render.** The D10.1 preset
   stores **Space Freeze Off**; the same stimulus render delivers `kSpaceFreezeId` (1115) → On through
   `IParameterChanges` at `A + 65 s`, exactly as a player engaging Freeze after the bloom would, and then
   applies the Freeze-On tail criteria above including the absolute floor; the preset's `Comment` (FR-003)
   instructs the player to engage Freeze once the drone has bloomed. No processor load-behaviour change:
   freeze still mutes the tank's sends on a stored-On load exactly as today, and a preset that stores
   Freeze On and fails the floor remains an FR-017 finding, never a pass.

### C-7 — Distinctness: parameter space and sound space, threshold ruled here

The roadmap requires "pairwise measured-tolerance spectral/fingerprint distance over the long-render
sweep … with the threshold ruled in the spec rather than assumed" (line 587-589). Seraphis's precedent
is a warning, not a template: its level-normalised `RenderFingerprint` shape distance was gated at an
absolute floor of 0.02, its compliance record concedes the second term was self-referential and "the
only real gate is the absolute 0.02" (`specs/seraphis-phase12-presets-release/compliance.md:345`), and
the library then needed a palette-widening pass because it sounded same-y. Vorago gates two
independent spaces.

#### C-7.1 Parameter space (deterministic, from definitions)

For every unordered pair of presets, **≥ 8 persisted IDs** differ — a continuous ID "differs" when the
normalized values differ by **≥ 0.10**, a list ID when its index differs. The same test applies between
every preset and the **default surface** (a preset that is the init patch plus a few tweaks is not a
showcase). A pair differing only in seed, level or one section fails here regardless of how it sounds.

#### C-7.2 Sound-space descriptor (level-invariant, sub-immune)

`PresetDescriptor` is computed per 60 s minute of the C-6 hold (`M1`, `M2`, `M3`; no extra render),
stereo, from existing helpers only. The preset's descriptor `D(P)` is the component-wise **mean of its
three per-minute descriptors**, so distinctness is measured over the whole held drone, not its first
minute: the event schedulers fire every 20–90 s, the bloom fades in over ~45 s, so a single 60 s window
can miss an event cycle entirely while three consecutive minutes contain several.

Let `E_hi` = energy in `[80 Hz, 20 kHz]` (`bandEnergyDb`, `vorago_fixtures.h:245`). The stimulus (C2,
65.4 Hz) puts the fundamental and every stored sub tone (f/2 ≈ 32.7 Hz, fifth-below ≈ 43.6 Hz, f/4 ≈
16.4 Hz) below 80 Hz, so `E_hi` is sub-free.

| Component | Measure | Unit (1.0 = one step) |
|---|---|---|
| b1…b8 | octave-band energy **relative to `E_hi`** in dB, bands `[80·2^k, 80·2^(k+1)]` Hz for k = 0…6 plus `[10 240, 20 000]`; each relative value **clamped below at −60 dB** before scaling, so inaudible differences 80–120 dB down in an empty band of a dark preset cannot dominate `d` (`bandEnergyDb` floors at −300 dB via `kPowerFloor = 1e-30`, `vorago_fixtures.h:101`, `:265`, and is otherwise unclamped) | 3 dB |
| b0 | low-region energy `[20, 80]` Hz relative to `E_hi`, dB, same −60 dB clamp — the **only** component through which sub/fundamental level enters | 3 dB |
| m | log2 `perBandTotalVariation` (`vorago_fixtures.h:441`, per-band dB, therefore sub-immune) | 1 (a doubling) |
| f | log2 `perBinMagnitudeFlux` (`vorago_fixtures.h:502`) | 1 |
| c | inter-channel correlation (`calculateCorrelation`, `low_frequency_metrics.h:478`) | 0.25 |
| e | standard deviation of 1 s block RMS dB (`blockRmsDb`, `vorago_fixtures.h:344`) | 2 dB |
| k | crest factor dB (`crestFactorDb`, `vorago_fixtures.h:316`) | 3 dB |

Every component is level-invariant (relative band energies, dB-domain motion, normalised flux,
correlation, dB spread, crest factor). No full-band centroid or flatness is used: Phase 10 measured the
full-band centroid pinned near 47 Hz by the subs, so any such metric on this instrument must be
sub-immune. Normalising to full-band total would **not** be sub-immune: with a sub-dominated total, a
+6 dB sub change alone would shift every non-sub band by ≈ −6 dB (2 units each, `d` ≈ 5.7 over eight
bands, above F) — which is exactly how two presets differing only in sub level, or only in
Weight/Mass/Pressure (all writing `SubToneLevelOffsetDb`, `vorago_macro_matrix.h:530-546`, `:751-756`), would have
passed as distinct. With `E_hi` normalisation a sub-level change moves `b0` alone (6 dB → 2 units).
`d(P, Q)` = Euclidean norm of the unit-scaled component differences of `D(P)` and `D(Q)`.

#### C-7.3 The ruled threshold

- **Self-distance** `s(P)`: the **largest** pairwise `d` among P's three per-minute descriptors
  (`M1`, `M2`, `M3`) — the preset's own intrinsic variation across the whole hold, free of charge. For an
  ablation/E twin rendered only to the end of `Sus`, `s` of that render is `d` between `Sus`'s two 30 s
  halves.
- **Measured take-to-take distance** `t_max`: the largest seed-twin `d` observed in control (b) below.
- **Distinctness floor:** for every pair, `d(P, Q) ≥ max(F, 2 · max(s(P), s(Q)), 2 · t_max)` with
  **F = 4.0**. The `2 · t_max` term ties the floor to measurement: if a new take of one preset moves the
  descriptor by `t`, two presets must differ by at least twice that.
- **Calibration of F with the `E_hi` normalisation.** A 1 dB/octave tilt across the eight `E_hi` bands
  moves them by 0…7 dB about the energy-weighted pivot: with the pivot mid-spectrum the deviations are
  ±0.5…±3.5 dB → `d` ≈ √(42/9) ≈ 2.2; with the pivot at the lowest band (a dark preset) 0…7 dB →
  `d` ≈ √(140/9) ≈ 3.9. Both sit below F = 4.0, so one EQ-like move of 1 dB/oct does not pass; a
  2 dB/oct tilt (4.3–7.9) does, which is the intended "large move in one axis". A ±6 dB sub change moves
  `b0` alone by 2 units (below F). These are arithmetic on the ruled descriptor; the controls below are
  the measured check of them.
- **Negative controls** — all MUST pass in the same run or the floor is void (FR-017). They run on the
  **control set** `C`, named in the plan from the measured sweep: the most and the least evolving preset
  by `s(P)`, one freeze preset (the D10.1 preset), the preset with the highest stored Pressure macro and
  the one with the highest stored Weight macro (deduplicated; ≥ 3 distinct presets).
  (a) *Level twin (descriptor math)* — **every** preset's own `M1…M3` buffers scaled by −6 dB after
  rendering (no re-render; a true level-only change) score `d < 0.05`.
  (a′) *Gain twin (rendered)* — on the highest-Pressure preset, `kMasterGainId` set to **half its stored
  normalized value** (−6 dB on the linear 0–2 taper, `plugins/vorago/CLAUDE.md`, whatever gain the preset
  authored). Master gain is applied **before** the nonlinear output stage — `renderGainAndOutputStage`
  multiplies by `masterGain_` then calls `processOutputStage`, saturator then limiter
  (`processor.cpp:1106-1113`) — and Pressure drives `OutputDriveDb` up to +18 dB
  (`vorago_macro_matrix.h:520-525`), so this is **not** a level-only change; its ruled budget for the
  nonlinearity is `d ≤ F / 2`.
  (b) *Seed twin* — each preset in `C` at the next seed index scores `d ≤ F / 2` (= 2.0): a margin, not
  merely `< F`, above take-to-take variation. A seed twin above F/2 means the metric barely tells a new
  take from a new preset: **stop and surface**, never lower F or widen the margin silently.
  (c) *Sub twin* — each preset in `C` with `kSubLevelOffsetId` at stored ± 6 dB (normalized ± 0.125;
  nothing else changed) scores `d < F`.
- **Pilot-calibrated, not merely asserted (Clarification Q3, session 2026-09-27, FR-017a).** F is not
  ratified from arithmetic alone: before the full library is authored, the plan renders a 6–8 preset
  pilot set, measures the seed-twin `t_max` and a deliberately near-variant pair's `d`, then rules F from
  that measurement (never below **4.0**) and freezes it. Every threshold below assumes the frozen
  **F = 4.0** floor; if the pilot rules higher, every F-based bar in this spec scales with it and the
  change is recorded as a ruling. The factor 2, the F/2 margins and the 0.05 level bound are this spec's
  ruling per roadmap line 588-589 and do not move with the pilot.

#### C-7.4 Ablation (verifies Group S and M claims)

A claimed S/M cell (and every D cell whose audibility conjunct is a reversion or depth ablation)
verifies iff `d(P_Sus, P_ablated) ≥ max(D, 2 · s(P))`, where `P_ablated` is P with the C-2.1 override
applied, rendered on P's own timeline to the end of `Sus` only, and `P_Sus` is P's `M1` descriptor. The
bar `D` depends on the claim's role:

- **Primary:** `D = F = 4.0` — "foregrounded": removing the showcased capability must move the preset as
  far as the distinctness floor separates two presets.
- **Secondary:** `D = D_abl = 1.5` — "measurably present".

Both are ruled here (F pilot-calibrated per Clarification Q3/FR-017a; D_abl = 1.5 fixed, does not move
with the pilot). E cells use the same two bars through their route-isolated measurement (C-2.1 Group E);
the C-2.3 roster-extension E cells use the same two bars through plain C-7.4 ablation.

### C-8 — Determinism without bit-exact goldens

`kSeedId` is persisted (`global_params.h:118-126`), so two renders of one preset in one process are
driven from the same seed. Reproducibility is asserted with `compareFingerprints` at the shared
tolerances (`render_fingerprint.h:52-60`, `:122-124`). No float bit digest, and no integer digest of
float bits, is introduced anywhere (roadmap line 632). The generator's **file-byte** determinism
(FR-023) is legitimate only because it is one binary, one machine, one run set.

### C-9 — The committed tree is compared semantically

The committed `resources/presets` tree is generated on Windows/MSVC; the release artifact is generated
on `ubuntu-latest`/GCC and is what the installer ships (`release.yml:100-103`, `:171-174`, `:224`).
Stored floats pass through log/offset-log tapers (`param_mapping.h`; e.g. `envelopeTimeFromNormalized`,
`envelope_params.h:72`) built on `std::pow`, which is not correctly rounded across libms. The
committed-tree test (FR-032) therefore asserts: same file set and paths, byte-identical `Info` XML,
version == `kCurrentStateVersion` and length == `kStateV3Bytes` (plan ruling R-3 / P-2), every int32
field equal, every float field within a **measured**
relative tolerance (measure on **all three** toolchains the test is enforced on — MSVC, WSL/GCC and
AppleClang, the last from the macOS CI leg, which builds with `-ffast-math` and a different libm `pow` —
pin at ~10× the worst observed across the three, one ULP floor where the measured worst is 0 — the
Seraphis FR-029a procedure, `seraphis-phase12-presets-release/compliance.md:369`, extended by the macOS
leg). The test prints its per-field worst relative difference on every leg; if a later macOS run exceeds
the pinned tolerance, the tolerance is re-measured on all three and the change recorded as a ruling in
the compliance record — never widened silently.

### C-10 — Lanes and wall-clock budget

Per `CLAUDE.md`'s `[long]` convention, preset sweeps are `[long]` (nightly on three OSes) — **except**
the NaN/Inf and bound guards and the state-format tests, which stay in the per-push lane.

**The CI lane is the budget that matters, not the local machine.** The nightly lane is
`long-tests-nightly.yml` calling `ci.yml` with `long-tests: true`; each OS's single "Run Tests" step runs
every suite's exe **sequentially** under `timeout-minutes: ${{ inputs.long-tests && 40 || 20 }}`
(`ci.yml:322`, `:608`, `:1069`), with filter `[long]` nightly and
`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` per push (`ci.yml:365-376`). The runners are
small — the step comments cite a 2-vCPU Windows runner (`ci.yml:315-317`), a 3-vCPU / 7 GB macOS ARM
runner (`:604-606`) and 2-4 vCPU Linux (`:1066`) — and heavy suites are deliberately not run in parallel
there. A multi-hour preset sweep cannot share that 40-minute step with every other plugin's `[long]`
roster, and thread parallelism buys little on 2–3 vCPUs. So:

- **Per-push** (`ci.yml` "Run Tests", 20 min, shared): container / metadata / round-trip / browser-scan /
  committed-tree / parameter-space distinctness (FR-018…FR-035), plus the **short load-time guard**
  (FR-034): per preset, 8 s at 48 kHz, 4 s at 44.1 kHz, 4 s at 96 kHz single-note, and 8 s of the 4-note
  chord at 48 kHz — finite + peak ≤ 0.9661 only. This arm is a NaN/Inf and load-time guard; it makes no
  sustain-state claim (sustain at 44.1/96 kHz is FR-033a's).
- **Dedicated nightly sweep** (new, FR-066): every heavy preset arm is tagged `[long][vorago-sweep]` and
  runs in its **own** job(s) in `long-tests-nightly.yml` on all three OSes, with its own
  `timeout-minutes`, **sharded by preset** (an environment variable `VORAGO_SWEEP_SHARD=i/n` selects
  definitions by index mod n, deterministically) for the per-preset arms — the C-6 render and its arms
  (FR-033), the 44.1/96 kHz sustain arm (FR-033a), ablation and E arms (FR-037), the non-subset
  evaluation (evaluated per Q, so shardable by Q), reproducibility (FR-038) — and **one unsharded job**
  for the cross-preset arms that need every descriptor in one process: sound-space distinctness and the
  controls (FR-036). The generic `[long]` step's filter becomes `[long]~[vorago-sweep]` so the existing
  40-minute budget is untouched by this phase.
- **Budget (SC-013), measured on the CI runners, not scaled from a local figure:** every Vorago sweep
  job's measured step wall clock on its own runner, per OS, ≤ **60 %** of that job's `timeout-minutes`,
  and no sweep job's `timeout-minutes` above **180** (GitHub's job ceiling is 360; 180 keeps a nightly
  run inside its night). Per push, the added Vorago preset TUs' measured time is recorded per OS from the
  CI log, and the shared "Run Tests" step's total on each OS stays ≤ **80 %** of its 20-minute timeout
  after this phase lands; if the pre-existing roster already exceeds that, it is an FR-017 finding, not a
  licence to trim. The local `windows-x64-release` alone-run durations are recorded too, for reference.
- **Levers, in order:** more shards (more parallel jobs), then threads within a shard where the runner
  has the cores; **never** drop presets, sample rates, arms, ablation cells, E measurements, control
  presets or window length. The full-verification-vector ablation cost (FR-012/FR-037, Clarification Q5)
  and the roster-extension E cells (Clarification Q2, C-2.3) are accounted for by sharding, never by
  narrowing the vector back to claimed-only cells.

### C-11 — Deferred items

- **`getTailSamples()` (Phase 11 Q6).** Today `kNoTail` (`processor.h:89`), an accepted risk that an
  offline bounce truncates the tail. **Ratified (Clarification Q7, session 2026-09-27; FR-060):**
  state-derived — `Rel` (release, `kEnvelopeReleaseId`) + the **effective** RT60 (C-6's cavern-target
  calculation, not the stored decay alone) + the ghost-grain ceiling, all from the current decoded state,
  and **`kInfiniteTail`** while Space Freeze is On. Tested against the decoded state (FR-060).
- **Ghost density vs trigger (Phase 10a Q1).** Additive (density scheduler keeps running at 0.30
  grains/s, triggered grains on top) is what ships; replacement needs a density control the plugin does
  not register (`ghost_params.h:35-40` has peak level, blur, reverse probability, triggers only).
  **Recommended:** presets use the additive behaviour; replacement is not reachable without a new
  parameter and is out of scope. *(OQ-4.)*
- **`docs/index.html` (Phase 11 FR-080).** `plugins/vorago/docs/` holds only `.gitkeep`; the Seraphis
  page (`plugins/seraphis/docs/index.html`, `assets/style.css`) is the template. `docs.yml` needs no edit.

---

## Functional Requirements

### Category set and library

- **FR-001** `makeVoragoPresetConfig()` MUST declare exactly the seven C-1 subcategories in C-1 order,
  `"Drones"` byte-identical and first; `processorUID`, `pluginName == "Vorago"`,
  `pluginCategoryDesc == "Synth"` unchanged; designated field order matches
  `preset_manager_config.h:19-24`. [roadmap 575; CLAUDE decision 2]
- **FR-002** `plugins/vorago/resources/presets/` MUST contain exactly one directory per C-1 category and
  no other, every `.vstpreset` directly inside one of them (1:1 both ways with FR-001). [roadmap 575]
- **FR-003** Every preset MUST embed an `Info` chunk whose attributes equal what the shared
  `PresetManager::savePreset` writes (`preset_manager.cpp:264-275`): `MediaType = VstPreset`,
  `PlugInName = Vorago`, `PlugInCategory = Synth`, `Name` = file stem, `MusicalCategory` and
  `MusicalInstrument` = the directory name, `Comment` = the definition's description. [roadmap 575-576]
- **FR-004** N (plan-derived, C-2.2) MUST satisfy every C-2 rule; every category MUST hold **≥ 3**
  presets (a browse tab with one or two entries is a hole, not a category); there is no other per-category
  count. [roadmap 586-587]
- **FR-005** Names MUST be unique across the library, satisfy `PresetManager::isValidPresetName`
  (`preset_manager.h:120`), and be ASCII with no path separator.
- **FR-006** Every `Comp` chunk MUST be a full current-version stream: first int32 == `kCurrentStateVersion`
  (3 once FR-072 lands), length == `kStateV3Bytes` (= 428 + 4·|R|). (Plan ruling R-3 / P-2: factory
  presets are v3, never "version 2, 428 bytes".)
- **FR-007** Every preset MUST store polyphony index ≤ 3 (≤ 4 voices) (C-5).
- **FR-008** Every preset MUST satisfy the C-6 authoring ceilings `A ≤ 180 s`, `Rel ≤ 60 s`.
- **FR-009** No stored float may be non-finite (bit-pattern check).

### Coverage matrix (the variety requirement)

- **FR-010** `tools/vorago_preset_defs.h` MUST declare the `Capability` enum with **exactly** the C-2.1
  cells (S1–S10, M1–M12, E1–E5, D1.1–D14.2), plus the ratified-roster extension cells of C-2.3/FR-075
  (`E6.lo`/`E6.hi` … one pair per knob in `R`), each carrying its group, its predicate and — for S/M and
  the roster extension — its ablation override. [roadmap 586]
- **FR-011** Every `VoragoPresetDef` MUST declare one **primary** cell and zero or more secondary cells;
  no two definitions share a primary; no primary is a default-state cell; every primary verifies at the
  C-7.4 **primary bar** (`d ≥ max(F, 2·s)`) (C-2.2). [roadmap 584, 589-590]
- **FR-011a** **Non-subset showcase.** For every ordered pair (P, Q), P ≠ Q, at least one cell P claims
  MUST NOT be verified by Q, evaluated against Q's **full verification vector** (FR-012, Clarification
  Q5) rather than rendered on demand. The harness MUST print, per P, the witness cell found against each
  Q. [roadmap 589-590]
- **FR-012** **Full verification vector (Clarification Q5).** For every preset, the harness MUST compute
  and cache the **full verification vector**: every C-2.1 cell's predicate result (verified / not),
  not only the cells that preset claims — S/M by C-7.4 ablation, E by the route-isolated ablation of
  C-2.1 Group E (never by the S7 + destination conjunction) plus one pair of plain-ablation cells per
  ratified roster knob (C-2.3), D from the decoded state (FR-031) **plus** the cell's audibility
  conjunct — computed **once per preset** (O(N × cells), the strongest and most deterministic of the
  evaluated alternatives). The harness MUST then check every declared claim against its own preset's
  cached vector at the bar for the claim's role and fail on any claim that does not verify; it MUST also
  run the default surface through the D predicates to identify default-state cells. FR-011a and FR-013's
  printed matrix are derived from these cached vectors, never re-rendered per ordered pair.
- **FR-013** Every C-2.1 cell MUST be verified by ≥ 1 preset; every E*k* MUST be the verified **primary**
  of ≥ 1 preset (five distinct presets). The harness MUST print the full preset × cell matrix (verified
  as primary / verified as secondary / claimed-failed / unclaimed), the default-state cells, and the
  FR-011a witnesses for the compliance record.
- **FR-014** Every preset MUST satisfy C-7.1 (parameter-space distinctness) against every other preset
  and against the default surface. [roadmap 585]
- **FR-015** Every preset pair MUST satisfy C-7.3's sound-space floor (including the `2·t_max` term);
  all four C-7.3 negative controls (level, gain, seed, sub twins) MUST pass on the control set in the
  same run. [roadmap 587-589]
- **FR-016** The plan MUST derive the preset list from the matrix (each preset = a primary cell plus
  compatible secondaries), and only then assign categories; the plan MUST NOT start from a per-category
  count. [roadmap 587]
- **FR-017** **Stop-and-surface.** If a cell cannot be verified by any authorable preset, or a C-7.3
  control fails, the build stops and reports the finding with measurements; it MUST NOT relax a predicate,
  a floor or a control, drop a cell, or edit `dsp/` to pass.
- **FR-017a** **Pilot calibration of F (Clarification Q3).** Before authoring the full preset library, the
  plan MUST render a **6–8 preset pilot set**, measure the seed-twin `t_max` (C-7.3) and the sound-space
  `d` of a deliberately authored near-variant pair from that pilot set, then **rule F from that
  measurement** (F MUST NOT be set below **4.0**) and **freeze** F before authoring the remaining presets.
  The pilot measurement and the frozen F MUST be recorded in the compliance record; every F-based
  threshold elsewhere in this spec (C-7.3, C-7.4, FR-011, FR-015, SC-008, SC-010, SC-011) uses **F = 4.0**
  as the default value unless the pilot rules a higher F, in which case every such threshold scales with
  it and the change is recorded as a ruling, never a silent retune (F never moves below 4.0, per C-7.3).

### Ecosystem rule-knob parameters (Clarification Q2 — scope widening, inside Phase 14)

- **FR-070** **Audibility probe.** The plan's first task MUST render each candidate ecosystem rule knob
  (predation, sync rate, exchange rate, crowding, forage/feed/graze rates, leak, move/max speed, kernel
  sigma, freq drift, per-kind affinities — `ecosystem_engine.h:469-707`) at its registered default and at
  its extreme (everything else unchanged), measure the C-7.2 sound-space descriptor distance between the
  two renders for each candidate, and rank candidates by that measured audibility.
- **FR-071** **Stop-and-surface roster ratification.** The plan MUST present the probe's measurements and
  a proposed curated roster `R` (roughly 4–6 knobs, the most audible by FR-070) to the user and MUST NOT
  author any new parameter ID, the state v3 format, any UI control, or any preset referencing `R` until
  the user ratifies `R` or a revised roster.
- **FR-072** **New parameter IDs and state v3.** Once `R` is ratified, a new registered parameter ID MUST
  be added to the ecosystem block (`plugin_ids.h`) for every knob in `R`, wired to its existing
  `EcosystemEngine::set*` setter; `kCurrentStateVersion` MUST become **3**; `setState`/`getState` MUST
  read/write the v3 block; a v2 stream (including every v2 factory preset built before this ratification)
  MUST load successfully with every knob in `R` at its **current hard-coded default**.
- **FR-073** **UI controls.** The Phase 13 ecosystem page (`resources/editor.uidesc`) MUST gain one
  control per knob in `R`; the page union, bound-ID count and allowlist tests MUST be updated to include
  the new IDs.
- **FR-074** **Test coverage.** Processor, controller, uidesc, param-table and state-format tests MUST be
  updated for the new IDs and the v3 format (round-trip, v2→v3 default-load, bounds).
- **FR-075** **Coverage matrix extension.** For every knob `k ∈ R`, C-2.3's `E{n}.lo`/`E{n}.hi` cell pair
  MUST be added to the `Capability` enum (FR-010) and to the coverage matrix (FR-012/FR-013), each
  verified by C-7.4 ablation (the knob's ID reset to its registered default) at the primary or secondary
  bar per C-2.2, **in addition to** the five kind→destination routes E1…E5 (never in place of them).
- **FR-076** **Non-goals boundary.** No DSP class is created for `R` (existing `EcosystemEngine::set*`
  setters only, C-2.3); no parameter outside the ecosystem block and its page is added; every other
  Non-goal (new DSP, new parameter *types*, retuning to force variety, `Info`-chunk read-back, user-preset
  UX, non-Windows factory installers) is unaffected by this widening.

### Generator

- **FR-018** `tools/vorago_preset_generator.cpp` MUST build as the CMake target
  **`vorago_preset_generator`** with `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"` (fixed by
  `release.yml:150-174`), compiling `plugins/vorago/src/processor/processor.cpp` and the SDK sources the
  Seraphis target lists (`CMakeLists.txt:604-619`), linking `KrateDSP KratePluginsShared sdk` only.
- **FR-019** It MUST take the output directory as `argv[1]`, create the C-1 directories, and write every
  preset (the upload fails on an empty directory, `release.yml:181`).
- **FR-020** A custom target **`generate_vorago_presets`** MUST regenerate the committed tree in place
  (`COMMAND vorago_preset_generator "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets"`).
- **FR-021** The `Comp` chunk MUST come from the shipped `Processor::getState()` through the C-4 drive,
  implemented in **one Catch2-free drive header** used by both the generator and the committed-tree test
  (finding 3). No state layout, no `vorago_preset_format.h`, no hard-coded class-id string.
- **FR-022** Definitions (`tools/vorago_preset_defs.h`, namespace `Vorago::PresetDefs`) MUST be data
  only — name, category, description, primary + secondary cells, `{ParamID, normalized}` pairs — shared
  by the generator TU and the test TUs.
- **FR-023** The generator MUST be deterministic and idempotent on one machine (definition-order
  iteration, no timestamp, no directory iteration, no RNG).
- **FR-024** `tools/check-preset-generator-determinism.js` MUST gain a `--plugin <name>` selector
  (default `seraphis`, behaviour unchanged) that resolves `vorago_preset_generator`; the Seraphis
  invocation MUST keep working byte-for-byte as documented.
- **FR-025** The generator MUST build and run on the release runner (Linux/GCC/Ninja) with no VSTGUI.

### Installation

- **FR-026** Factory presets MUST install to `%PROGRAMDATA%\Krate Audio\Vorago\<Category>\` through the
  existing `krate_plugin_install_presets(${PLUGIN_NAME})` call (`plugins/vorago/CMakeLists.txt:123`),
  matching `Platform::getFactoryPresetDirectory("Vorago")` (`preset_paths.h:27`). Verification only.
- **FR-027** `setup.iss:66-68` and `installers/linux/README.txt:29-44` MUST remain accurate. Verification only.

### Validation harness (all TUs enumerated in `plugins/vorago/tests/CMakeLists.txt`, never globbed)

- **FR-028** **Container:** every file has the `VST3` magic, the class id of `kProcessorUID`, a chunk
  list holding `Comp` and `Info`, in-bounds offsets.
- **FR-029** **Round-trip:** `setState(Comp)` returns `kResultOk` and a following `getState()` is
  byte-identical to the loaded chunk.
- **FR-030** **Metadata↔filesystem and browser scan:** the FR-003 attributes parse and match; a
  `PresetManager` built with **both** overrides (an empty temp user dir and `resources/presets` as the
  factory dir) returns N entries, all `isFactory`, none with an empty `subcategory`, and
  `getPresetsForSubcategory(c)` returns the defined count per category; the browser tab list equals
  `{"All"} ∪ subcategoryNames` (`makeVoragoPresetTabLabels()`).
- **FR-031** **Typed decode:** harness reads of decoded values (envelope, freeze, enumerations, macro
  values, sub levels …) MUST use the shipped `load*Params` functions in `getState()` order
  (`processor.cpp:643-659`) then `loadEcosystemParamsV3Ext`, and assert exactly `kStateV3Bytes` bytes
  consumed (plan ruling R-3 / P-2) — never arithmetic re-derivation.
- **FR-032** **Committed tree:** C-9's semantic comparison of the committed tree against in-process
  regeneration from FR-022's definitions, on all three CI legs, tolerances measured and pinned.
- **FR-033** **Long-render sweep** (`[long][vorago-sweep]`): for every preset, the C-6 render and its
  four arms (bounded / non-silence / neither-dies-nor-explodes / tail, the Freeze-On tail with its
  absolute floor), and for the D10.1 preset the **freeze-gesture protocol** (Clarification Q1: preset
  stores Space Freeze Off, harness delivers `kSpaceFreezeId` → On via `IParameterChanges` at `A + 65 s`,
  Freeze-On tail criteria including the floor apply; the preset's `Comment` instructs the player to
  engage Freeze after the bloom). [roadmap 576-578]
- **FR-033a** **Sustain at the non-reference rates** (`[long][vorago-sweep]`): every preset rendered with
  the C-6 stimulus over `[0, A + 65 s]` at **44.1 kHz and 96 kHz**, asserting C-6 arm 1 (finite by bit
  pattern, peak ≤ 0.9661, every 10 s stereo-RMS window ≤ −6 dBFS) — so the hot corners (loop gain 0.9
  with high coupling, subs at +6 dB with Pressure 1, freeze with 60 s decay) are driven at sustain at
  every shipped rate, not only at 48 kHz.
- **FR-034** **Short load-time guard** (per-push): C-10's short renders, finite + peak ≤ 0.9661, at 44.1,
  48 and 96 kHz and the 4-note chord at polyphony 4. A NaN/Inf and load-time guard only: it makes no
  claim about the sustained drone at any rate (FR-033/FR-033a do).
- **FR-035** **Parameter-space distinctness** (per-push): C-7.1 over all pairs and against the default.
- **FR-036** **Sound-space distinctness** (`[long][vorago-sweep]`, unsharded job — plan ruling R-4 / P-6: that
  job loads the shard jobs' descriptor and verification-vector record artifacts into one process and
  renders only the control twins; "one process" means one evaluation, not one re-render): `PresetDescriptor` of
  every preset (mean of `M1…M3`) from the FR-033 render; C-7.3 over all C(N, 2) pairs; all four negative
  controls on the control set. The run MUST print min / median / max `d`, the minimum pair's names, every
  `s(P)`, `t_max`, every control's `d`, and the effective floor.
- **FR-037** **Ablation — full verification vector** (`[long][vorago-sweep]`, Clarification Q5): one
  ablation render per **every** S/M cell (not only claimed ones) and per ablation-conjunct D cell, for
  every preset (C-7.4, scored at the claim's role's bar where claimed, recorded regardless); four renders
  per **every** E route (E1…E5) plus the preset's `R_∅` pair, and one ablation render per ratified-roster
  E cell (C-2.3), for every preset (C-2.1 Group E). This full-vector render set is what FR-012 caches and
  FR-011a/FR-013 consume; no additional on-demand renders are made for non-subset evaluation.
- **FR-038** **Reproducibility** (`[long]`): two renders of each preset over `[0, A + 65 s]` from fresh
  processors agree under `compareFingerprints` at the shared tolerances.
- **FR-039** **Sequential load:** loading every preset between `process()` calls of a warm processor
  allocates nothing on the audio path (`AllocationScope` around the process calls).
- **FR-040** **Concurrent load:** a message thread calls `setState` over all presets in a loop while the
  audio thread renders; output stays finite and ≤ 0.9661, every call returns `kResultOk`, and
  `ThreadScopedAllocationScope` sees no audio-thread allocation. The global `AllocationScope` is
  forbidden for this arm.
- **FR-041** **Preset CPU** (`[.perf]`, run alone via `node tools/run-cpu-tests.js vorago_tests`; the runner's
  `FILTER` excludes `[vorago-sweep]` — plan ruling R-6 — so the multi-hour sweep never shares a CPU run): each
  preset and the default surface driven with `kCpuNotes`, 48 kHz / 512. **Where:** each patch is
  pre-rolled **untimed** to its own `A + 5 s` (C-6; the default surface's `A` is 155 s,
  `vorago_voice.h:320-324`), then blocks inside its own `Sus` window `[A + 5 s, A + 65 s]` are timed,
  **interleaved** preset/default in one trial loop — the sustained drone the preset is designed around,
  never the quiet start of a multi-minute swell. **Polyphony:** forced to **4** for every patch (stored
  polyphony overridden; C-5 bounds every preset at ≤ 4, so 4 voices sounding `kCpuNotes` is the
  preset's worst-case cost, which is what a CPU bound must cover); the stored-polyphony figure is also
  measured and recorded. **Gate:** the worst preset MUST cost ≤ **1.15 ×** the default surface at forced
  polyphony 4. **What is NOT re-gated, and why:** the roadmap's 30 %-of-a-core ceiling (lines 479-485) is
  an **engine-level** gate — Phase 10 SC-001b, `VoragoEngine` everything-on at polyphony 4 with the
  Cavern term added arithmetically (`vorago_perf_budget.h:78-90`) — while this arm times the whole
  processor, whose default surface alone was already 1.19769 × `kReferenceNs` at Phase 11
  (roadmap 543-545). A preset at the 1.15 × limit could therefore reach ≈ 1.38 × `kReferenceNs` at
  processor level (≈ 41 % of a core). FR-041 does not claim the 30 % ceiling for presets; it records the
  absolute processor figure per preset against `kReferenceNs` (3 200 000 ns) and prints it, and an
  engine-level per-preset re-gate is not added because configuring a bare `VoragoEngine` from a decoded
  preset would need a second surface-push path beside the processor's, the divergence C-3/C-4 exist to
  prevent. Levers on breach: re-author the preset; never relax. [roadmap 479-485, 622]
- **FR-042** **Human listening checkpoint:** the phase owner auditions every preset and records, per
  preset, its category fit and a one-line character note, and confirms no two presets read as variants.
  Recorded in the compliance document; no automated arm substitutes for it and it substitutes for none.

### Deferred items and release

- **FR-060** **`getTailSamples()` is state-derived (Clarification Q7).** It MUST report
  `Rel + effective-RT60 + ghost-grain ceiling` computed from the currently decoded state (effective RT60
  per C-6's cavern-target calculation, not the stored decay alone), and MUST report **`kInfiniteTail`**
  while Space Freeze is On. A test MUST assert the reported value against independently decoded state for
  a representative set of presets (varying decay, macros, and the freeze toggle).
- **FR-061** The density-vs-trigger choice MUST be recorded (OQ-4); presets that verify D12.1 use it.
- **FR-062** `plugins/vorago/docs/index.html` MUST exist, on the Seraphis page's structure, describing
  the shipped instrument and its factory categories.
- **FR-063** `vorago` MUST be added to `PLUGIN_MAP` in `.claude/workflows/release-readiness.js`
  (`testTarget: 'vorago_tests'`, `bundle: 'Vorago.vst3'`) and to the plugin list and table in
  `.claude/skills/release/SKILL.md`.
- **FR-064** **Version 1.0.0 (Clarification Q8, session 2026-09-27).** `version.json` and `CHANGELOG.md`
  MUST be bumped together to **`1.0.0`** — the public release with the factory library — with a
  `CHANGELOG.md` `[1.0.0]` entry describing the factory library (the only files edited for the version,
  per the release skill). From this version, the controller interface set is **frozen**: no further
  controller interface additions without host-cache cost (`plugins/vorago/CLAUDE.md`, decision 1).
- **FR-065** The release gate MUST run the `release-readiness` flow for `vorago` — build, `vorago_tests`,
  pluginval strictness 5, version/CHANGELOG sync — plus `node tools/check-portability.js`, clang-tidy
  `vorago`, and FR-024's determinism check, **and** `auval -v aumu Vrgo KrAt` on macOS, whose evidence
  source is the `ci.yml` step "Run Vorago AU Validation" (`ci.yml:800-808`) on the release commit — the
  compliance record cites that run's URL and the auval result line from its log (no local macOS run
  exists). Record a green/red verdict. [roadmap 578]
- **FR-066** **Dedicated nightly sweep job(s).** `.github/workflows/long-tests-nightly.yml` MUST gain the
  Vorago preset-sweep job(s) of C-10 (three OSes; per-preset arms sharded via `VORAGO_SWEEP_SHARD`; one
  unsharded cross-preset job), each with its own `timeout-minutes` ≤ 180, and `ci.yml`'s nightly
  "Run Tests" filter MUST become `[long]~[vorago-sweep]` on all three legs so the shared 40-minute step
  is unaffected. The job's build must include `vorago_tests` only (plus what it links). SC-013's
  measurements are taken from these jobs' logs.

---

## Success Criteria

| ID | Criterion | Threshold | Measured by (test name sketch) |
|---|---|---|---|
| SC-001 | Category agreement | config list == C-1 order; dirs == list both ways; 0 strays | `Vorago_FactoryPresets_CategoriesMatchConfig` |
| SC-002 | Container + metadata | 100 % of files pass FR-028 and FR-003 (six attributes + Comment) | `Vorago_FactoryPresets_ContainerAndInfo` |
| SC-003 | Round-trip | 100 % byte-identical `getState()` after `setState()` | `Vorago_FactoryPresets_RoundTrip` |
| SC-004 | Browser scan | count == N; 0 empty subcategories; 0 non-factory; per-category counts == definitions | `Vorago_FactoryPresets_BrowserScan` |
| SC-005 | Stream shape + operating point | 100 %: version == `kCurrentStateVersion`, length == `kStateV3Bytes` (R-3), finite, polyphony ≤ 4, `A ≤ 180 s`, `Rel ≤ 60 s` | `Vorago_FactoryPresets_StreamShape` |
| SC-006 | Committed tree == generator | 0 path/XML/int mismatches; floats within the pinned measured tolerance on MSVC, GCC and AppleClang legs | `Vorago_FactoryPresets_TreeMatchesGenerator` |
| SC-007 | Generator determinism | two runs byte-identical; re-run over the tree changes 0 bytes | `node tools/check-preset-generator-determinism.js --plugin vorago` exit 0 |
| SC-008 | Coverage + showcase | 100 % of C-2.1 cells verified; each E*k* the verified primary of a distinct preset; 0 claimed-but-failed; unique primaries, none a default-state cell, each at the primary bar `d ≥ max(4.0, 2·s)`; every ordered pair (P, Q) has a claimed witness of P that Q fails (FR-011a); D cells verified with their audibility conjuncts | `Vorago_PresetMatrix_CoverageComplete`, `Vorago_PresetMatrix_NoShowcaseSubset` ([long][vorago-sweep]; + printed matrix, default-state cells and witnesses) |
| SC-009 | Parameter-space distinctness | every pair and preset-vs-default: ≥ 8 IDs differ (≥ 0.10 normalized or index) | `Vorago_PresetMatrix_ParameterSpaceDistinct` |
| SC-010 | Sound-space distinctness | descriptor = mean of `M1…M3`, bands relative to `E_hi`, −60 dB clamp; every pair `d ≥ max(4.0, 2·max(s), 2·t_max)`; on the control set `C` (≥ 3 named presets): post-render level twin `d < 0.05` (every preset), rendered gain twin on the highest-Pressure preset `d ≤ 2.0`, seed twins `d ≤ 2.0`, sub twins (±6 dB) `d < 4.0` | `Vorago_PresetSweep_SoundSpaceDistinct` ([long][vorago-sweep]) |
| SC-011 | Ablation | every claimed secondary S/M/ablation-D cell `d(P, P_abl) ≥ max(1.5, 2·s(P))`; every primary `≥ max(4.0, 2·s(P))`; every claimed E cell passes the route-isolated test at its role's bar and exceeds `d(R_∅, R_∅⁰) + 1.5` | `Vorago_PresetSweep_AblationVerifiesClaims` ([long][vorago-sweep]) |
| SC-012 | Long-render boundedness | 100 % presets: finite; peak ≤ 0.9661; every 10 s RMS ≤ −6 dBFS over `[0, Total]`; every 10 s RMS ≥ −60 dBFS over `[A, H]`; late/early ratio in [−18, +12] dB; tail arm per freeze with `Tail` placed on the **effective** RT60; Freeze-On tail loudest-10 s ≥ RMS(`Sus`) − 20 dB | `Vorago_PresetSweep_LongRender` ([long]) |
| SC-013 | Wall clock (CI lane) | every Vorago sweep job, per OS, measured step time ≤ 60 % of its `timeout-minutes` (≤ 180); shared per-push "Run Tests" step ≤ 80 % of its 20 min on every OS; generic nightly `[long]` step unchanged by this phase (filter `[long]~[vorago-sweep]`); local alone-run durations recorded for reference | CI job/step durations from the run logs (URLs cited in compliance); `-d yes` local durations |
| SC-014 | Short load-time guard | 100 % presets finite, peak ≤ 0.9661 at 44.1/48/96 kHz and chord (load-time guard only; no sustain claim) | `Vorago_PresetSweep_ShortBounded` |
| SC-015 | Reproducibility | `withinTolerance()` true for every preset | `Vorago_PresetSweep_RendersAreReproducible` ([long]) |
| SC-016 | RT-safe loading | sequential: 0 allocations; concurrent: 0 audio-thread allocations, 100 % `kResultOk`, bounded output | `Vorago_FactoryPresets_SequentialLoadNoAlloc`, `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe` |
| SC-017 | Preset CPU | timed blocks inside each patch's own `Sus` after an untimed pre-roll; polyphony forced to 4; worst preset ≤ 1.15 × default surface, interleaved, alone; absolute processor figure and stored-polyphony figure recorded vs 3 200 000 ns (engine-level 30 % ceiling not re-gated, FR-041) | `Vorago_PresetCpu` ([.perf]) |
| SC-018 | Ghost features engaged | ≥ 1 verified D11 and D12.1 preset | subset of SC-008 |
| SC-019 | Release gate | `release-readiness` vorago row green; pluginval 5 exit 0; check-portability clean; clang-tidy `vorago` 0 findings; `auval -v aumu Vrgo KrAt` passes (FR-065) | compliance record citing the release commit's `ci.yml` "Run Vorago AU Validation" run URL and log line |
| SC-020 | Listening checkpoint | 100 % presets audited per FR-042 | compliance record |
| SC-021 | Rosters | `vorago` present in both release rosters | grep in compliance record |
| SC-022 | Sustain at 44.1/96 kHz | 100 % presets over `[0, A + 65 s]` at 44.1 and 96 kHz: finite, peak ≤ 0.9661, every 10 s RMS ≤ −6 dBFS | `Vorago_PresetSweep_SustainAtAllRates` ([long][vorago-sweep]) |
| SC-023 | Dedicated sweep lane | `long-tests-nightly.yml` holds the FR-066 job(s) on three OSes, each `timeout-minutes` ≤ 180; `ci.yml` nightly filter is `[long]~[vorago-sweep]` on all three legs | grep in compliance record + one green nightly run URL |
| SC-024 | Freeze showcase | the D10.1 preset passes C-6 arm 4 Freeze-On criteria including the −20 dB absolute floor, under the freeze-gesture protocol (Clarification Q1, FR-033) | subset of SC-012 / SC-008 |
| SC-025 | Ecosystem audibility probe | every candidate rule knob probed at default vs. extreme; measured descriptor distances printed and ranked | compliance record (FR-070) |
| SC-026 | Ecosystem roster ratified before authoring | proposed roster `R` (~4–6 knobs) presented and ratified by the user before any new ID/format/UI/preset referencing `R` exists | compliance record (FR-071) |
| SC-027 | Ecosystem parameters + state v3 | new IDs registered for every `k ∈ R`; `kCurrentStateVersion == 3`; v2 stream loads with every `k ∈ R` at its hard-coded default | `Vorago_State_V2LoadsWithRosterDefaults`, param-table test (FR-072) |
| SC-028 | Ecosystem UI + coverage extension | one control per `k ∈ R` on the ecosystem page; page union/bound-ID/allowlist tests updated; every `E{n}.lo`/`E{n}.hi` cell verified by ≥ 1 preset | `Vorago_Ecosystem_PageBindsRosterIds`, subset of SC-008 (FR-073, FR-075) |
| SC-029 | Library size + primaries | N == **38 + \|E-ext\|** (plan ruling R-2 / P-3; supersedes Q4's [35, 45] band — default-state cells are ineligible primaries under FR-011, so the floor of unique primaries is 10 S + 12 M + 5 E + 9 non-default materials + D8.2 + D9.1 = 38, plus one preset per roster-extension cell); every S, M, E (incl. roster-extension) cell is some preset's primary; each **non-default-state** member of the 11 body materials, 4 noise models and D8/D9 envelope extremes is some preset's primary (R-3 / P-5; the default-state members are verified secondaries of factory presets, never primaries) | grep/count in compliance record + coverage matrix (FR-004, FR-011, FR-013) |
| SC-030 | Tail samples | `getTailSamples()` matches `Rel + effective-RT60 + ghost-grain ceiling` from decoded state for every representative preset; reports `kInfiniteTail` when Space Freeze is On | `Vorago_Processor_GetTailSamplesMatchesState` (FR-060) |

---

## Edge Cases

- **RT-safety boundary.** Preset load is `setState` on the message thread; Phase 12 made it raise a
  request consumed at the next `process()` (`plugins/vorago/CLAUDE.md`, `pushAllSurfaces`). FR-039/FR-040
  prove it under quiescent and concurrent load; a preset switch mid-note keeps voices sounding (seed and
  polyphony trackers deliberately excluded, D-P2) — the concurrent arm asserts boundedness, not a click
  criterion.
- **Parameter extremes.** Authored values are clamped by the shipped handlers (C-4); a definition outside
  [0, 1] is a generator error, not a clamp. Loop gain at its 0.9 ceiling with high coupling, sub tones at
  +6 dB with Pressure at 1, and Space Freeze with decay 60 s are the known hot corners; SC-012's
  non-runaway window is the gate for each preset that uses them.
- **Growth mode.** Stage times are zeroed, so `A` = growth duration; the swell is near-silent early,
  which is why non-silence starts at `A`, not at 0.
- **Freeze On.** A frozen field has no finite RT60 decay; the tail arm switches to held-and-non-growing
  with an absolute floor (C-6 arm 4). Voices still release under freeze. Freeze stored On at load mutes
  the tank's input before any note (`aether_reverb.h:409-410`), so a stored-On preset is predicted to
  hold silence; the floor detects it, and the freeze-gesture protocol (Clarification Q1, C-6 arm 4,
  FR-033) is how the factory showcase preset is shipped and verified instead.
- **Ghost capture after release.** The global `AtmosphereEngine` replays its capture ring; if the
  density scheduler keeps birthing grains from stale capture after the voices stop, the Freeze-Off tail
  arm fails — that is a finding to surface (FR-017), not a window to widen.
- **Sample-rate changes.** The short per-push arm is a load-time guard at 44.1/48/96 kHz; FR-033a drives
  every preset to sustain at 44.1 and 96 kHz with the arm-1 bounds; every other long arm runs at 48 kHz
  (the reference rate of every Vorago CPU and soak figure). A re-prepare re-sends every surface
  (`pushAllSurfaces(Scope::Reprepared)`), so a preset loaded before `setupProcessing` still sounds right.
- **Seed determinism.** Seeds are persisted; a same-process re-render is reproducible within the shared
  fingerprint tolerances (SC-015). A different seed is a different take: the seed twin control proves
  the distinctness floor sits above take-to-take variation.
- **Cross-toolchain.** Stored floats differ by ULPs across MSVC/GCC/AppleClang; SC-006 is semantic, never
  byte-level. macOS builds with `-ffast-math`: every finite check is by bit pattern.
- **Polyphony shrink on load.** A preset with fewer voices than the current count lets tails ring out
  (Phase 11 behaviour); the concurrent arm accepts it.

---

## Open Questions (deferred to this spec by the roadmap or by earlier specs)

Six of the eight items below were resolved in the Clarifications session of 2026-09-27 (see
**## Clarifications**); the resolution is recorded here and carried by the FR/SC ids cited. OQ-4 and
OQ-6 were not part of that session and remain as originally recommended/ruled.

- **OQ-1 — Category names** — **RESOLVED (Clarifications Q6).** Ratified as proposed: Drones · Abyss ·
  Caverns · Organisms · Machines · Textures · Ghosts, `Drones` first and verbatim, permanent once shipped.
  Carried by FR-001, FR-002.
- **OQ-2 — Distinctness thresholds** — **RESOLVED (Clarifications Q3): pilot-calibrated, not ratified from
  arithmetic alone.** F = 4.0 is a **floor**, not a fixed value: FR-017a requires a 6–8 preset pilot
  render measuring the seed-twin `t_max` and a near-variant pair's `d` before F is ruled (never below
  4.0) and frozen. Every other value here is fixed as originally ruled: self-distance factor 2 (over the
  three hold minutes), the `2·t_max` measured-take term, seed-twin and gain-twin margin F/2, sub-twin
  bound F, level-twin bound 0.05, band clamp −60 dB, primary bar = F, secondary bar D_abl = 1.5,
  Freeze-On tail floor X = 20 dB, parameter-space K = 8 at Δ ≥ 0.10. Carried by FR-017a and every F-based
  FR/SC (FR-011, FR-015, SC-008, SC-010, SC-011).
- **OQ-3 — `getTailSamples()`** — **RESOLVED (Clarifications Q7).** State-derived tail:
  `Rel + effective-RT60 + ghost-grain ceiling`, `kInfiniteTail` under freeze. Carried by FR-060, SC-030.
- **OQ-4 — Ghost density vs trigger** (Phase 10a Q1 → Phase 12/14) — **not part of this session, unchanged.**
  Recommended: additive as shipped.
- **OQ-5 — Ecosystem behaviour definition** — **RESOLVED (Clarifications Q2): scope widened, inside Phase
  14.** The five kind→destination routes (E1–E5) stay; a curated roster of ecosystem rule knobs (~4–6,
  chosen by an audibility probe and stop-and-surface ratified before any ID/format/UI/preset is authored)
  becomes registered parameters this phase — new IDs, state v3 with v2 load compatibility, ecosystem-page
  controls, and matching coverage-matrix `E{n}.lo`/`E{n}.hi` cells. Carried by C-2.3, FR-070…FR-076,
  SC-025…SC-028.
- **OQ-6 — "Showcases nothing another preset already shows"** (C-2.2) — **not part of this session,
  unchanged.** Ruled as: unique, non-default, primary-bar primary **and** the non-subset rule on verified
  sets (FR-011a) **and** each E*k* a distinct preset's primary. Ratify.
- **OQ-7 — Release version** — **RESOLVED (Clarifications Q8).** `1.0.0` — the public release with the
  factory library; the controller interface set is frozen from this version. Carried by FR-064.
- **OQ-8 — How factory freeze is played** — **RESOLVED (Clarifications Q1).** D10.1 is verified by the
  **freeze-gesture** render: the preset stores Space Freeze Off; the harness delivers `kSpaceFreezeId` →
  On at `A + 65 s` through `IParameterChanges`; the preset's `Comment` tells the player to engage Freeze
  once the drone has bloomed. No processor load-behaviour change. Carried by C-6 arm 4, FR-033, SC-024.

---

## Review notes (spec challenge, 2026-09-27)

All seventeen issues were applied; none was rejected outright. Where a suggestion offered alternatives, the
choice and the reason are recorded here.

- **Non-subset (fidelity, major).** Applied as a claimed-witness rule (FR-011a) plus primary-bar and
  default-state eligibility. The alternative "P's primary is verified by no other preset" was **not**
  adopted: S8 (cavern), S5 (subs) and similar cells are verified incidentally by most presets, so that
  rule would make them un-primary-able and push the library toward fewer showcased sections, the
  opposite of the governing requirement. The witness rule is strictly stronger than `V(P) ⊄ V(Q)`.
- **Group E (fidelity + testability, two majors).** Both suggested remedies applied together: a
  route-isolated ablation (destination soloed among the five, with a residual `R_∅` term), and each E*k*
  the primary of a distinct preset. The hidden per-family event-count probe was not chosen: the counter
  (`lastEventTarget_`) is private DSP state, and exposing it is a `dsp/` edit this phase's Non-goals
  forbid; a count would also measure scheduling, not audibility.
- **Descriptor window (minor).** Descriptor and `s(P)` now span three hold minutes; `H` moved from
  `A + 180` to `A + 185` so the minutes are whole (worst `Total` 495 → 500 s). Ablation and E twins stay
  on `Sus` (the first minute) to bound render cost; their bar still uses the whole-hold `s(P)`, which is
  the more conservative value.
- **Level twin (major).** Both variants adopted: the post-render scaling is the pure math control on every
  preset; the rendered gain twin is kept on the highest-Pressure preset with an explicit F/2 nonlinearity
  budget, because a gain move into the saturator is a real, if small, sound change.
- **Freeze On (major).** The absolute floor makes the predicted empty-tank failure visible; how factory
  freeze is played was surfaced as OQ-8 rather than silently assumed, and is now resolved as the
  freeze-gesture protocol (Clarifications Q1).
- **CPU ceiling (minor, fidelity).** The explicit-statement option was taken (FR-041): an engine-level
  per-preset arm would need a second decoded-surface-to-engine path beside the processor's, which C-3/C-4
  deliberately forbid. The relative gate is unchanged, not relaxed.
- **CI budget (blocker).** Resolved by a dedicated, sharded nightly lane (FR-066) and budgets measured on
  the runners (SC-013), not by shortening any render; the 90-minute local figure is gone.


---

## Clarifications

### Session 2026-09-27

- **Q1 — Factory freeze protocol.** Freeze-gesture: the D10.1 showcase preset stores Space Freeze Off;
  the harness delivers `kSpaceFreezeId` → On via `IParameterChanges` at `A + 65 s`; the preset's `Comment`
  tells the player to engage Freeze after the bloom. No DSP or processor load-behaviour change.
  [C-6 arm 4, FR-033, SC-024]
- **Q2 — What "every distinct ecosystem behaviour" means.** Widen scope, inside Phase 14: a curated
  roster (~4–6) of `EcosystemEngine` rule setters — candidates predation, sync rate, exchange rate,
  crowding, forage/feed/graze rates, leak, move/max speed, kernel sigma, freq drift, per-kind affinities —
  becomes registered parameters, the roster chosen by a hidden audibility probe and **stop-and-surfaced
  for user ratification** before any parameter ID, state v3 format, UI control or preset is authored.
  Consequences: new ecosystem-block parameter IDs, state v3 with v2 load compatibility (v2
  presets/state load with the new knobs at their current hard-coded defaults), controls on the Phase 13
  ecosystem page (page union, bound-ID count and allowlist tests updated), processor/controller/uidesc/
  param-table/state tests, and coverage-matrix Group E extended with the ratified knobs' extremes as
  primary cells, in addition to the five kind→destination routes.
  [C-2.3, FR-070, FR-071, FR-072, FR-073, FR-074, FR-075, FR-076, FR-010, SC-025, SC-026, SC-027, SC-028]
- **Q3 — Distinctness thresholds.** Pilot-calibrate: render 6–8 pilot presets, measure the seed-twin
  distance and a deliberate near-variant pair, then rule F (never below 4.0) and freeze it before
  authoring the rest; the ruling is recorded in the compliance record. [FR-017a, C-7.3, C-7.4]
- **Q4 — Target library size N.** Target band 35–45 presets: every section, macro and ecosystem cell
  (including the Q2 roster-extension cells) is a primary of its own preset, plus the most audible discrete
  cells (materials, noise models, envelope extremes) as primaries. [FR-004, C-2.2, SC-029]
  **Superseded on N by plan ruling R-2 (P-3):** default-state cells are ineligible primaries (FR-011), so
  the unique-primary floor is 38 + |E-ext| and SC-029 now reads N == 38 + |E-ext|.
- **Q5 — How FR-011a evaluates non-subset claims.** Full verification vector: render every section/macro
  ablation and ecosystem route set once per preset (O(N × cells)), cache the vectors, and check claimed
  cells against them; this also yields the printed matrix FR-013 needs. [FR-012, FR-011a, FR-037]
- **Q6 — Category names.** Ratify the seven fixed categories, Drones first: Drones, Abyss, Caverns,
  Organisms, Machines, Textures, Ghosts. N floor = 21. [FR-001, FR-002]
- **Q7 — `getTailSamples()`.** State-derived: release + effective RT60 + ghost-grain ceiling from the
  current state, `kInfiniteTail` while Space Freeze is On; tested against decoded state. [FR-060, SC-030]
- **Q8 — Release version.** Bump to `1.0.0`: public release with the factory library; the controller
  interface set is frozen from here; `CHANGELOG.md` gains a `[1.0.0]` entry. [FR-064]

### Plan stage (2026-09-27)

Rulings on the plan's §2 findings and §11 open questions, all taken before the build stage. Only the
roster `R` (with P-4's one-sided cells and the §5.8 E-ext halfway margin) remains for gate G1.

- **R-1 (P-1) — append-only `dsp/` edits accepted.** An inert `detail::VoragoEcosystemRuleProbe` forward
  declaration and friend in `VoragoVoice` and `VoragoEngine` now; after G1, per ratified knob one
  `VoragoVoice` forwarder, one `VoragoVoiceParams` field and one `applyVoiceParams` line
  (`kFieldCount` 31 → 31 + |R|). No new class. FR-017's "never edit `dsp/` to pass" is not engaged:
  this wires a ratified surface. [FR-070, FR-072, FR-076]
- **R-2 (P-3) — SC-029's ceiling raised to N == 38 + |E-ext|.** Every roster-extension cell gets its own
  showcase preset; the Q4 band is superseded on N. [SC-029, FR-004, FR-011]
- **R-3 (P-2, P-5) — spec text amended.** FR-006, FR-031, C-9 and SC-005 read `kCurrentStateVersion` /
  `kStateV3Bytes` instead of "version 2, 428 bytes"; SC-029's required-primaries list reads "each
  non-default-state member" per FR-011, the default-state members being verified secondaries.
  [FR-006, FR-031, C-9, SC-005, SC-029]
- **R-4 (P-6) — records, not re-render.** FR-036's "one process" is met by the unsharded aggregate job
  loading every shard's record artifacts into one process and rendering only the control twins.
  [FR-036, FR-037, C-10]
- **R-5 (P-8) — D10.1 is a verified secondary of the S8 cavern preset,** checked on that preset's
  freeze-gesture render; it is never a primary (its reversion sounds identical over `Sus`).
  [D10.1, SC-024, C-6 arm 4]
- **R-6 — the CPU runner excludes the sweep.** `tools/run-cpu-tests.js`'s `FILTER` gains
  `~[vorago-sweep]` so `node tools/run-cpu-tests.js vorago_tests` stays the one documented protocol
  for every suite and the sweep never shares a CPU run. [FR-041, SC-017]
- **R-7 — structure accepted.** (i) CMake registration is split: the probe TU is registered before G1,
  every other new TU and the generator targets in one mid-phase task, and the final group only audits.
  (ii) The library-size and ≥ 3-per-category checks live in their own
  `Vorago_FactoryPresets_LibraryShape` case so it is the only red while the library is partly authored.
  [FR-004, FR-035, SC-004]
- **R-8 — harness-first descriptor, no duplication.** The C-7.2 descriptor and C-7.3 distance land in
  `preset_test_support.h` before the probe TU exists, and the probe includes them from there; the probe
  ranking and the sweep use one implementation. [FR-070, FR-036, C-7.2]
