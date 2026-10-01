# Feature Specification: Vorago Phase 14 — Factory Presets & Release Readiness

**Spec slug:** `vorago-phase14-presets-release`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, **Phase 14** (lines 615–645), including the
**variety paragraph added 2026-09-27** (lines 628–638), which this spec treats as the governing
requirement of the whole phase, and the **Q2 ruling** (lines 640–645) that the ecosystem rule knobs
become registered parameters inside this phase. Also consumed: the Phase 10a line that hands preset use
of the ghost features to this phase (line 496, "both features ship inert, Phase 14 presets engage
them"), the Phase 10 CPU promise (lines 479–485), the **Phase 13b hand-off** (lines 598–600: "Phase 14
then re-runs its specify stage against the new surface: Q2's roster comes from the 13b probe table";
line 606: "Phase 14 Q2 counts its knobs from the two FR-013 tables"), and the cross-cutting
constraints (lines 669–693).
**Deferred into this phase by earlier specs (verified this session):** `getTailSamples()` revisit
(`specs/vorago-phase11-plugin-scaffold/spec.md:447-451`, Clarification Q6 at `:1181-1182`);
`docs/index.html` (`vorago-phase11-plugin-scaffold/spec.md:744-746`, FR-080); the Phase 10a Q1
density-vs-trigger preset choice (`specs/vorago-phase10a-ghost-extension/spec.md:1470-1473`); the
full hand-off list in `specs/vorago-phase12-parameters/spec.md:240` and
`specs/vorago-phase13-ui/spec.md:159`.
**Depends on:** Phases 1–13 and **13b** (all ✅ per roadmap lines 157–613; 13b closed 2026-09-29 with
Gate 2 recorded UNMET by ruling, roadmap lines 602–610).
**Status:** DRAFT (second specify pass) — specification only, no implementation beyond the first pass's
kept artifacts (the probe TU, `preset_test_support.h` part 0, the inert probe friend; roadmap line 620)
**Date:** 2026-09-29 (first pass 2026-09-27; this pass re-specifies against the post-13b surface)

### What changed in this pass (read first)

The first pass (2026-09-27) paused at gate G1 when its own probe measured 0 of 14 rule knobs audible
(`specs/vorago-phase14-presets-release/compliance.md`, runs 1–4). Phase 13b then gave the colony direct
sonic levers (`vorago_voice.h:2099-2144`) and retuned the peak/loop wake bases to 0.45
(`vorago_voice.h:391-392`). This pass keeps every first-pass ruling (Clarifications Q1–Q8, plan R-1–R-8)
and changes only what the post-13b surface forces:

1. **The roster source is fixed (C-2.3, FR-070).** Q2's roster is counted from Phase 13b's two FR-013
   tables — not from a new probe — per roadmap lines 598–600 and 606. Those tables count **two** knobs,
   `syncRate` and `selfAffinity`, both one-sided (`.hi`); `R` is now **ratified as exactly this pair**
   (Clarifications session 2026-09-29, Q1 — see **## Clarifications**).
2. **The distinctness threshold must be re-ruled (C-7.3, OQ-10).** The same C-7.2 descriptor
   (`VoragoTest::describe`/`meanOf`, `plugins/vorago/tests/preset_test_support.h:96`, `:167`) now
   measures the **default surface's own seed twin at d = 5.9515** (Life max 6.8514)
   (`specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log`, "t0on" line;
   `final2_table_lifemax.log`). Before 13b it was 1.7128 (first-pass run 1). The first pass's control
   (b) requires a seed twin `d ≤ F/2 = 2.0` and its floor carries a `2 · t_max` term, so as written the
   harness is **predicted to stop-and-surface at the pilot** (FR-017) and every floor would sit at
   ≥ 11.9. This is the roadmap's "threshold ruled in the spec" (line 637) and is surfaced, not assumed.
   **Resolved** (Clarifications session 2026-09-29, Q2–Q4): a **seed-marginal descriptor** — `D(P)` is
   averaged over K takes, so the take term shrinks with K instead of F rising to swallow the measured
   spread; F = 4.0 and every first-pass margin stand; K is ruled at the pilot (Q3, hard-capped at 8).
3. **The roster-extension cells' role must be re-ruled (C-2.3, OQ-11).** The counted knob extremes move
   the descriptor by 2.1606 (`syncRate` → 0.5, default surface) and 2.6401 (`selfAffinity` → +2, Life
   max) — above the secondary bar D_abl = 1.5 but below the primary bar F = 4.0 that plan ruling R-2
   (N == 38 + |E-ext|, every E-ext cell a primary) requires. **Resolved** (Clarifications session
   2026-09-29, Q5): primaries, as R-2 ruled — one pilot candidate per knob, FR-017 stops if the bar is
   unreachable; N = 38 + |R| = 40. *(Gate G2, 2026-09-29: both bars were unreachable — the cells are secondaries, N = 38.)*

Everything else is carried forward unchanged, with every line citation re-verified against the post-13b
tree (only `vorago_voice.h` and `vorago_engine.h` changed under `dsp/include` since the first pass:
`git diff --stat c980c4f5 HEAD`).

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
(roadmap lines 628–638). Every decision below is shaped by that requirement — the coverage matrix is a
functional requirement (FR-010…FR-016), the preset list is derived from the matrix and not from a
per-category quota (C-2), and distinctness is measured in both parameter space and sound space with a
threshold ruled here (C-7).

Five findings shape the design (1–4 from the first pass, re-verified; 5 from Phase 13b's measured
surface):

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
   (`dsp/include/krate/dsp/systems/vorago_voice.h:324-328`); Growth mode's default is 120 s
   (`:336-337`). A "seconds-scale" sweep in the Seraphis style (sustain window `[A+1, A+4]`) would measure
   an unfinished swell. The roadmap asks for **minutes-scale** non-silence/non-runaway assertions
   (line 624-626); C-6 derives every window from each preset's own decoded envelope.
5. **Phase 13b made the colony audible, and made takes vary more.** The colony now writes noise-source
   level (+12 dB span), peak level (+18 dB) and slewed peak wander, loop gain (+0.18) and ring coupling
   (+0.30) from `lanes.eco` only (`vorago_voice.h:2099-2144`; spans at `:1586-1603`). By the C-7.2
   descriptor the ecosystem on/off distance at the default surface is 5.6272 (1.392 × the colony-off
   reseed distance t0 = 4.0439) and 5.8810 at Life max (`final2_table_default.log`,
   `final2_table_lifemax.log`, "true-off reference" lines) — against 0.6312 / 2.1248 before 13b
   (first-pass runs 2 and 3, measured against the `900 = 0` reference, which the 13b log cross-checks
   as equivalent to true-off: `d(true-off, 900=0) = 0.0004`). The same levers raised the **colony-on seed-twin distance** from 1.7128 to
   5.9515 (default) and from 2.1601 to 6.8514 (Life max). Group E (C-2.1) becomes measurable where the
   first pass was predicted to fail; C-7.3's take-to-take terms, sized against the old 1.7, do not hold
   (OQ-10). The 13b voicing change is intentional and recorded (13b compliance FR-030: default M1 RMS
   −26.10 → −26.47 dBFS).

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
  gate** via the `release-readiness` flow (roadmap line 626).
- The **dedicated nightly preset-sweep lane** (FR-066): new job(s) in `long-tests-nightly.yml` and the
  `[long]~[vorago-sweep]` filter in `ci.yml`'s nightly "Run Tests" step.

## Non-goals (owned elsewhere or out of scope)

- **New DSP classes, new UI outside the ecosystem page, new parameter *types*.** No new DSP component is
  created — the ratified ecosystem rule-knob parameters (Clarification Q2, session 2026-09-27; C-2.3,
  FR-070…FR-076) wire *existing* `EcosystemEngine::set*` setters
  (`dsp/include/krate/dsp/systems/ecosystem_engine.h:469-707`) to new registered `float`/enum parameter
  IDs on the Phase 13 ecosystem page only; every registered parameter *type* stays frozen
  (`plugin_ids.h:82-83`). Unlike a normal Non-goal, the registered **surface size** and
  `kCurrentStateVersion` DO move in this phase: 108 IDs → **110** (108 + |R| = 108 + 2, `R` ratified
  Clarifications Q1, 2026-09-29), version **2 → 3**, with v2 presets/state loading at the new knobs'
  current hard-coded defaults (FR-072). This is
  the one scope-widening exception Clarification Q2 authorises, and it is gated end-to-end by the
  roster ratification (FR-071 — `R` = {`syncRate`, `selfAffinity`}, ratified Clarifications Q1,
  2026-09-29, counted from Phase 13b's FR-013 tables, FR-070) before
  any ID, format, control or preset is authored. The Phase 10-style append-only `dsp/` wiring ruled in
  R-1 (one `VoragoVoice` forwarder, one `VoragoVoiceParams` field and one `applyVoiceParams` line per
  ratified knob; `VoragoVoiceParams::kFieldCount = 31` today, `vorago_engine.h:160`, `:188`, `:859`,
  becoming **33** = 31 + |R|)
  is the only `dsp/` edit this phase makes.
- **Re-voicing the ecosystem.** Phase 13b owns the colony levers, wake bases and the Gate 2 shortfall
  (recorded UNMET by ruling, roadmap lines 602–610). This phase consumes the shipped levers as they are;
  it does not widen a span, retune a wake base or add a lever to make a rule knob or an E cell pass.
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
| Envelope ranges/defaults | `envelope_params.h:42-50`, `:60-67`; `vorago_voice.h:319-342` | stage times [0, 120000] ms, growth [1, 120] s; sustain point 4; `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }` |
| `makeVoragoPresetConfig()` / `makeVoragoPresetTabLabels()` | `plugins/vorago/src/preset/vorago_preset_config.h:24-30`, `:36-44` | the category list (extended here, C-1) and the browser tab list |
| `Krate::Plugins::PresetManagerConfig` | `plugins/shared/src/preset/preset_manager_config.h:19-24` | field order `processorUID, pluginName, pluginCategoryDesc, subcategoryNames` |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h:55`, `:70`, `:75`, `:120`; `.cpp:95-103`, `:264-275` | overrides ctor, `scanPresets()`, `getPresetsForSubcategory()`, `static bool isValidPresetName(const std::string&)`; exact-match subcategory; the six-attribute (+`Comment`) `Info` XML the factory files must match |
| `krate_plugin_install_presets` | `cmake/KratePlugin.cmake:287-310` | POST_BUILD copy to `%PROGRAMDATA%/Krate Audio/<target>` (already called, `plugins/vorago/CMakeLists.txt:123`) |
| Seraphis generator pattern | root `CMakeLists.txt:594-669`; `tools/seraphis_preset_generator.cpp:1-80`; `tools/seraphis_preset_defs.h:50-112` | processor.cpp compiled into the tool, `KrateDSP KratePluginsShared sdk` only, `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`; `{ParamID, normalizedValue}` definitions in a data-only header shared with the test |
| Generator determinism gate | `tools/check-preset-generator-determinism.js:1-60` | byte determinism + idempotence of one binary; **hard-coded to Seraphis** (`:52-55`, `:111-113`, `:209`) — extended, not copied (FR-024) |
| `ProcessorFixture` | `plugins/vorago/tests/vorago_test_fixture.h:166-330` | `prepare(double sr, int32 maxBlock)`, `processBlock(n, IEventList*, IParameterChanges*)`, `renderScript(...)`, `reserveCapture(n)`; `MultiParamChanges` (`:118`); Catch2-dependent (`:38`) |
| Engine constants | `dsp/include/krate/dsp/systems/vorago_engine.h:225`, `:255`, `:439` | `kMaxVoices = 6`; `kOutputCeilingDb = -0.3f`; `limiter_.setCeilingDb(kOutputCeilingDb)` |
| Macro roster | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h:106-121` | `enum class VoragoMacro : std::uint8_t { Darkness = 0, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass, Count }` |
| Ecosystem kinds → destinations | `ecosystem_engine.h:282-288`; `vorago_voice.h:250-262`, `:1862-1870` | `enum class Kind { Partial, Resonator, Noise, Feedback, Ghost }`; `kindForFamily()` maps BloomTrigger→Partial, NoiseWake→Noise, PeakWake→Resonator, LoopWake→Feedback, GhostBurst→Ghost |
| Noise model semantics | `dsp/include/krate/dsp/systems/noise_organism.h:126-131`, `:463` | `enum class NoiseOrganismModel { Direct, FilteredWind, GranularDust, MetallicHiss }`; the noise **type** is effective only in `Direct` |
| Body materials | `dsp/include/krate/dsp/systems/continuous_body.h:84-100` | `enum class BodyMaterial` — Glass, Strings, MetalPlate, Chamber, Ice, StoneChamber, SteelTank, WoodenHull, CathedralColumn, CavernWall, GlassSphere (`kNumMaterials = 11`) |
| Cavern freeze / decay | `dsp/include/krate/dsp/effects/cavern_verb.h:255`, `:626`, `:735` | `kDefaultDecaySeconds = 20.0f`, `setDecaySeconds(float)`, `setFreeze(bool)`; decay range [0.5, 60] s (`space_params.h:58-59`) |
| Ghost features (Phase 10a) | `vorago_engine.h:989-1011`; `ghost_params.h:35-40` | `setGhostReverseProbability(float)`, `setGhostEventTriggers(bool)`; plugin defaults reverse 0.0, triggers Off (inert) |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h:58-63`, `:122-124` | `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `struct RenderFingerprint`, `compareFingerprints(...)` |
| Level-invariant spectral metrics | `tests/test_helpers/vorago_fixtures.h:245`, `:316`, `:344`, `:441`, `:502` | `bandEnergyDb(span, sr, loHz, hiHz)`, `crestFactorDb(span)`, `blockRmsDb(span, …)`, `perBandTotalVariation(span, sr)`, `perBinMagnitudeFlux(span, sr)` |
| Stereo correlation | `tests/test_helpers/low_frequency_metrics.h:478` | `float calculateCorrelation(const float* a, const float* b, std::size_t n)` |
| Allocation instruments | `tests/test_helpers/allocation_detector.h:111`, `:149` | `AllocationScope` (process-global), `ThreadScopedAllocationScope` (thread-filtered) |
| CPU budget constants | `dsp/tests/unit/systems/vorago_perf_budget.h:78`, `:82`; `plugins/vorago/tests/integration/processor_cpu_test.cpp:82-91` | `kBlockBudgetNs`, `kReferenceNs = kBlockBudgetNs * 0.30` (3 200 000 ns); `kCpuNotes{36, 40, 43, 47}`, `kCpuPolyphony = 4` |
| Release rosters | `.claude/workflows/release-readiness.js:13-22`; `.claude/skills/release/SKILL.md:15-31` | `PLUGIN_MAP` and the target/bundle table — **both omit `vorago`** |
| Long lane | `.github/workflows/long-tests-nightly.yml:1-31`, `:75-80`; `ci.yml:322`, `:365-376`, `:608`, `:1069` | `[long]` cases run nightly on three OSes by calling `ci.yml` with `long-tests: true`; each OS's single sequential "Run Tests" step has `timeout-minutes: ${{ inputs.long-tests && 40 || 20 }}`; `vorago_tests` is in every CI leg |
| `EcosystemEngine` rule setters (the roster candidates) | `dsp/include/krate/dsp/systems/ecosystem_engine.h:469-707`; ranges `:237-238`, member defaults `:2374-2412` | `void setSyncRate(float v) noexcept` (`:595`; "Kuramoto phase-coupling gain. Range [0, 0.5]. DEFAULT 0", clamp at `:599`, non-finite rejected); `void setAffinity(Kind from, Kind to, float v) noexcept` (`:707`; clamp to `[kMinAffinity, kMaxAffinity]` = [−2, +2], `:719`; "THE MATRIX IS NOT SYMMETRISED"); `defaultAffinity()` = −1.0 on the diagonal, +0.45 elsewhere (`:2403-2411`); also `setKernelSigma` `:469`, `setExchangeRate` `:479`, `setPredation` `:491`, `setLeakRate` `:522`, `setMoveRate` `:548`, `setMaxSpeed` `:558`, `setForageRate` `:568`, `setCrowding` `:577`, `setGrazeRate` `:624`, `setFeedRate` `:633`, `setFreqDrift` `:689`. No registered parameter reaches any of them today: `grep "ecosystem_\.set" vorago_voice.h` → only `setSeed` (`:1684`) |
| Voice → ecosystem access | `vorago_voice.h:1438`, `:1465`, `:1549` | `void setEcosystemDepth(float d) noexcept`, `void setEcosystemDepthFor(EcosystemEngine::Kind dest, float d) noexcept`, `const EcosystemEngine& ecosystem() const noexcept` (const — rule knobs need the R-1 forwarder) |
| Engine voice-parameter fan-out | `vorago_engine.h:160`, `:188`, `:859` | `struct VoragoVoiceParams` (`kFieldCount = 31`), `void applyVoiceParams(const VoragoVoiceParams& p) noexcept` — the R-1 path each ratified knob extends by one field |
| Phase 13b colony levers (consumed, not edited) | `vorago_voice.h:391-392`, `:585`, `:1586-1603`, `:1634`, `:2099-2144` | `kPeakWakeBase = kLoopWakeBase = 0.45f`, noise wake base 0.35; spans `kNoiseLevelLeverSpanDb = 12`, `kPeakLevelLeverSpanDb = 18`, `kLoopGainLeverSpan = 0.18`, `kCouplingLeverSpan = 0.30`, `kFreqWanderLeverSpanSemis = 3`; `kLeverInputGain{1, 2, 2, 2, 1}`; levers read `lanes.eco` only |
| Voice noise-level base (S1 ablation target) | `vorago_voice.h:1256`, `:1278`, `:1655-1661` | `void setNoiseLevelDb(float dB) noexcept` writes `noiseLevelBaseDb_`; `writeNoiseLevel` sends `clamp(base + leverOffset, −96, 12)` — so the S1 ablation (`kNoiseLevelId` → −96 dB) leaves at most −84 dB per source under a fully driven colony |
| C-7.2 descriptor (landed in the first pass, ruling R-8) | `plugins/vorago/tests/preset_test_support.h:40-56`, `:96`, `:151`, `:167` | `struct PresetDescriptor { std::array<double, kDescriptorBands> band; double motion, flux, corr, energySpread, crest; }` (`kDescriptorBands = 9`, 14 components); `PresetDescriptor describe(std::span<const float> L, std::span<const float> R, double sr)`, `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)`, `PresetDescriptor meanOf(std::span<const PresetDescriptor>)`; Catch2-free (`:6-8`) |
| Audibility probe TU (first pass, extended by 13b) | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp:1-60`, `:193`, `:279-295`, `:308-325` | `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")`; `kGateFactor = 0.5`; `applyAffinityDiagonal` (sets all five `setAffinity(k, k, v)`) and `applyAffinityOffDiagonal`; the 14-row `kCandidates` table with ranges and defaults |
| Probe friends (inert) | `vorago_voice.h:171`, `:1568`; `vorago_engine.h:1304` | `struct VoragoEcosystemRuleProbe;` forward-declared in `detail`, befriended by voice and engine (plan R-1) |

## New components

No DSP class and no plugin runtime class is created. The new names are tool/test-side only, plus one
registered parameter ID per ratified roster knob (C-2.3). `VoragoTest::PresetDescriptor`, listed here in
the first pass, now **exists** (Existing components table) and is reused, not re-created.

| Name | Kind / layer | Location | ODR sweep (`grep -rn -E "(class|struct|enum class) <Name>\b" dsp/ plugins/ tools/ tests/`) |
|---|---|---|---|
| `Vorago::PresetDefs::VoragoPresetDef` | struct, tool data (no DSP layer) | `tools/vorago_preset_defs.h` | **0 hits** |
| `Vorago::PresetDefs::ParamSetting` | struct | `tools/vorago_preset_defs.h` | 1 hit: `Seraphis::PresetDefs::ParamSetting` (`tools/seraphis_preset_defs.h:59`) — **different namespace, never linked into the same target**; no TU may `using namespace` both (`plugins/vorago/CLAUDE.md`, near-name hazard) |
| `Vorago::PresetDefs::Capability` | `enum class`, the matrix cell roster (C-2) | `tools/vorago_preset_defs.h` | **0 hits** |
| `VoragoTest::SweepTimeline` | struct, C-6 timeline | `plugins/vorago/tests/preset_test_support.h` | 1 hit: `SeraphisTest::SweepTimeline` (`plugins/seraphis/tests/preset_test_support.h:592`) — different namespace and target |
| `VoragoTest::DecodedPresetState` | struct, typed decode (FR-031) | `plugins/vorago/tests/preset_test_support.h` | 1 hit: `SeraphisTest::DecodedPresetState` (`plugins/seraphis/tests/preset_test_support.h:387`) — different namespace and target |
| Catch2-free host drive (name chosen by the plan) | header, tool+test shared | `plugins/vorago/tests/` or `tools/` (plan decides) | plan MUST sweep its chosen name before creating it |
| `kEcosystemSyncRateId` *(ratified — Clarifications Q1, 2026-09-29)* | parameter ID, ecosystem block 900–999 (`plugin_ids.h:78`, `:172-173`) | `plugins/vorago/src/plugin_ids.h` | **0 hits** (`grep -rn kEcosystemSyncRateId dsp/ plugins/ tools/ tests/`) |
| `kEcosystemSelfAffinityId` *(ratified — Clarifications Q1, 2026-09-29)* | parameter ID, ecosystem block | `plugins/vorago/src/plugin_ids.h` | **0 hits** |
| `kStateV3Bytes` | constant | `plugins/vorago/src/plugin_ids.h` | **0 hits** |
| ~~`Vorago::detail::VoragoEcosystemRosterProbe`~~ | **Withdrawn (plan-stage ruling P2-4, 2026-09-29):** SC-026a reads through the existing public const `Processor::engineForTest()` (`processor.h:116-118`); no new friend | — | — |
| `loadEcosystemParamsV3Ext` / `saveEcosystemParamsV3Ext` (names from plan R-3; final names the plan's) | free functions | `plugins/vorago/src/parameters/ecosystem_params.h` | **0 hits** for `loadEcosystemParamsV3Ext` |

Sweeps run 2026-09-29 on HEAD `59fbd9e6`: `VoragoPresetDef` 0, `Capability` 0, `CellSpec` 0 (plan-side
name) — no hit anywhere under `dsp/ plugins/ tools/ tests/`.
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
**foregrounded** by at least one preset (line 630-633). "Foregrounded" is made testable two ways:
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
`kindForFamily()` (`vorago_voice.h:1862-1870`). The routes are fixed and all live at once: there is no
per-route control, and the per-scheduler `lastEventTarget_` (`vorago_voice.h:2613`, written at `:2030`) is private,
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
| D1.1–D1.11 | each of the 11 body materials is Material A or B of ≥ 1 preset **that verifies S10** **and** in which that material's **blend weight is ≥ 0.35**: Material A is credited only when Body Blend (ID 1000, [0, 1], `body_params.h:55`) is ≤ 0.65, Material B only when it is ≥ 0.35 — the voice crossfades `(1 − b)·A + b·B` (`vorago_voice.h:2444-2447`), and S10's ablation (`kBodyMixId` → 0) removes both bodies together, so it cannot tell which material is heard (previously required only for the **6 dark** materials, StoneChamber…GlassSphere; a material in a muted body, or below 0.35 blend weight, is not foregrounded) |
| D2 | ≥ 1 preset with body blend in [0.35, 0.65] that verifies S10 (both bodies heard) |
| D3.1–D3.4 | each of the 4 noise models is the model of ≥ 1 slot in a preset that verifies S1 |
| D4.1–D4.12 | each of the 12 selectable noise types is the **effective** type of a `Direct` slot in ≥ 1 preset **that verifies S1** (a type under `kNoiseLevelId` at −96 dB, `noise_params.h:48`, does not count) |
| D5.1–D5.3 | Free, Keyed, Hybrid anchor each in ≥ 1 preset that verifies S2 |
| D6.1–D6.3 | Lowpass, Bandpass, Highpass each on ≥ 1 loop of a preset that verifies S4 |
| D7.1–D7.3 | f/2, f/4, fifth-below each the **loudest** stored sub tone of ≥ 1 preset that verifies S5 |
| D8.1–D8.2 | Standard and Growth envelope each in ≥ 1 preset — *always audible* (the envelope shapes every voice); as a **primary** (D8.2; D8.1 is default-state) scored by the **attack-window reversion** of C-7.4, never on `Sus` |
| D9.1–D9.2 | attack span `A` (C-6) ≤ 10 s in ≥ 1 preset; `A` ≥ 90 s in ≥ 1 preset — *always audible*; as a **primary** (D9.1; D9.2 is default-state, the default `A` being 155 s) scored by the **attack-window reversion** of C-7.4, never on `Sus` |
| D10.1 | **Freeze audibly holds a field** — claimed and verified as a **secondary of the preset whose primary is S8** (plan ruling R-5, FR-011b), on that preset's additional freeze-gesture render: C-6 arm 4's Freeze-On criteria including the absolute floor pass, **and** the gesture render's `kSpaceMixId` → 0 dry-residue twin passes its bound (Clarification Q1, C-6 arm 4). **Never a primary** of any preset |
| D10.2 | Space Freeze Off in ≥ 1 preset — *default state* |
| D11 | ghost reverse probability ≥ 0.5 in ≥ 1 preset that verifies S9 (roadmap line 496) |
| D12.1–D12.2 | Ghost Event Triggers On in ≥ 1 preset that verifies S9 (line 496); Off in ≥ 1 — D12.2 is *default state* |
| D13.1–D13.2 | `kEventsRateScaleId` (800) ≤ 0.3 in ≥ 1 preset; ≥ 3.0 in ≥ 1 — each in a preset that verifies S7 **and** whose reversion ablation (800 → its 1.0 default, normalized 0.5, `events_params.h:7`) passes C-7.4 |
| D14.1–D14.2 | `kLifeBreathingDepthId` (1500) ≥ 0.7 in ≥ 1 preset; `kLifeTidalDepthId` (1502) ≥ 0.7 in ≥ 1 — each in a preset whose ablation of that depth to 0.0 passes C-7.4 |

**Default-state cells** are D8.1, D10.2, D12.2, and every other **D** cell whose state predicate **and**
audibility conjunct the **default surface** itself satisfies when run through the same harness, the
audibility conjunct scored at the **secondary bar** D_abl (FR-012 evaluates the default surface as a
pseudo-preset for this purpose only). **S, M and E cells are never default-state**, whatever the default
surface verifies: they are defined by ablating a preset's authored surface, and the default surface
moving under an S ablation (the colony alone moves it by 5.6272, finding 5) does not make S7 or any other
section un-showcaseable. Default-state cells count toward coverage but are **ineligible as primaries**
(C-2.2).

#### C-2.2 Showcase rules (the roadmap's "near-variant" and "showcases nothing" clauses)

- **Unique, eligible, foregrounded primary.** Every preset definition names exactly one **primary**
  cell; no two presets name the same primary; the primary MUST NOT be a default-state cell (C-2.1); and
  the primary MUST verify at the **primary bar** (C-7.4: `d ≥ max(F, 2·s)`, i.e. removing the showcased
  capability turns the preset into what would count as a different preset). A state-verified D primary is scored at the primary bar by its reversion ablation (its IDs reset
  to registered defaults); an envelope primary (D8.2, D9.1) is scored by C-7.4's **attack-window
  reversion**, because an envelope reversion changes only the swell, which `Sus` never sees. D10.1 is
  never a primary (FR-011b).
- **Non-subset showcase (the roadmap's clause itself).** "A preset that showcases nothing another preset
  already shows is a defect" (line 637-638) is tested on *verified* sets, not labels: for every ordered
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
  category floor (FR-004) and SC-013's wall-clock budget (C-10) still bind within that band. *(Plan
  ruling R-2 superseded the band with N == 38 + |E-ext|; `R` is ratified as {`syncRate`, `selfAffinity`}
  (Clarifications Q1, 2026-09-29) and both counted extremes are showcase primaries (Clarifications Q5,
  2026-09-29), so `|E-ext|` = 2 and **N = 40**. Gate G2 (2026-09-29) then measured both cells under F and made them secondaries: **N = 38**.)*

#### C-2.3 What "distinct ecosystem behaviour" means: routes, plus a ratified rule-knob roster (Clarification Q2)

The ecosystem's rule knobs (kernel sigma, exchange, predation, sync, affinity …) exist on the component
(`ecosystem_engine.h:469-707`) but today **no registered parameter reaches them**: `grep "ecosystem_\.set"`
in `vorago_voice.h` finds only `setSeed` (`:1684`); the plugin exposes `kEcosystemDepthId` (900) alone,
plus the Life macro and the seed. The five **kind→destination routes** (Group E1…E5, unchanged) remain
one axis of "distinct ecosystem behaviour" — *which part of the drone the colony animates*.

**Clarification Q2 (session 2026-09-27) widens this phase's scope, inside Phase 14, to add a second
axis: a curated roster of the ecosystem's own rule knobs becomes registered parameters.**

- **Roster source (FR-070) — second pass.** The measurement is **already taken**: roadmap lines 598–600
  and 606 hand Q2's roster to Phase 13b's two FR-013 tables, produced by the same probe TU on the
  shipped post-13b tree (`specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log`
  and `final2_table_lifemax.log`, 28 renders each, 340 s, 48 kHz, block 512; both end "table for Phase
  14 Q2 — no roster named here"). This phase does **not** re-probe to select `R`. The bar the tables
  count at is 13b's ruled one: an extreme is **counted** iff `d ≥ f·t0` with f = 0.5 and t0 = the
  colony-off reseed distance (4.0439 default, 4.0645 Life max), it is not a **KILL** (any minute's RMS
  moves > 6 dB or falls below −60 dBFS) and it is not **OFF-LIKE** (`dOff < 0.25·t0`)
  (`ecosystem_rule_probe_test.cpp:25-27`, `:193`, `:529-530`, `:877-880`). The tables, best extreme per
  knob, ordered by best `d` on either surface:

  | Knob (setter) | Range, default | Default surface: best extreme → `d` (d/t0), flag | Life max (`109 = 1`): best → `d` (d/t0), flag | Counted? |
  |---|---|---|---|---|
  | `selfAffinity` (`setAffinity(k, k, v)` ×5) | [−2, 2], −1 | +2 → 1.9460 (0.481) INAUDIBLE; −2 → 0.0257 | +2 → **2.6401 (0.650)**; −2 → 0.0803 | **yes (.hi, Life max)** |
  | `syncRate` (`setSyncRate`) | [0, 0.5], 0 | 0.5 → **2.1606 (0.534)** | 0.5 → **2.1446 (0.528)** | **yes (.hi, both)** |
  | `grazeRate` | [0, 3], 0.75 | 0 → 5.6271 (1.392) OFF-LIKE | 0 → 5.8819 (1.447) OFF-LIKE | no — colony kill |
  | `leakRate` | [0, 1], 0.06 | 1 → 5.0549 (1.250) OFF-LIKE | 1 → 5.3385 (1.313) OFF-LIKE | no — colony kill |
  | `kernelSigma` | [0.01, 0.35], 0.03 | 1.6258 (0.402) | 1.8636 (0.459) | no |
  | `moveRate` | [0, 0.5], 0.2 | 0.9793 (0.242) | 1.5393 (0.379) | no |
  | `exchangeRate` | [0, 3], 0.35 | 1.4315 (0.354) | 1.2008 (0.295) | no |
  | `predation` | [0, 1], 0.55 | 1.3610 (0.337) | 1.2473 (0.307) | no |
  | `forageRate` | [0, 0.05], 0.01 | 0.9520 (0.235) | 1.0225 (0.252) | no |
  | `crossAffinity` | [−2, 2], 0.45 | 0.8671 (0.214) | 0.9779 (0.241) | no |
  | `freqDrift` | [0, 0.0002], 4e-5 | 0.5077 (0.126) | 0.8720 (0.215) | no |
  | `maxSpeed` | [0.001, 0.05], 0.03 | 0.3640 (0.090) | 0.3690 (0.091) | no |
  | `crowding` | [0, 0.2], 0.05 | 0.3270 (0.081) | 0.3002 (0.074) | no |
  | `feedRate` | [0, 1], 0 | 0.0033 (0.001) | 0.0102 (0.003) | no |

  The counted union is **two knobs, `syncRate` and `selfAffinity`**, against Q2's "roughly 4–6"; 13b
  recorded its own ≥ 4 gate (Gate 2) UNMET by ruling (roadmap lines 602–610). Both counted knobs are
  **one-sided**: `syncRate`'s default sits at its range floor, so only `.hi` exists (plan P-4); the
  `selfAffinity → −2` extreme measures 0.0257 / 0.0803, so its `.lo` cell cannot verify at either bar.
  `selfAffinity` is also not one setter: the probe applies it as `setAffinity(k, k, v)` for all five
  kinds (`applyAffinityDiagonal`, `ecosystem_rule_probe_test.cpp:279-284`), so the registered parameter
  writes the whole diagonal, exactly as measured.
- **Ratification gate (FR-071) — ratified.** No new parameter ID, no state v3
  format, no UI control and no preset referencing `R` is authored until the user ratifies `R`. **`R` is
  now ratified: `R` = {`syncRate`, `selfAffinity`}** (Clarifications Q1, session 2026-09-29) — exactly
  the counted set, option (a) of OQ-9. `R` is non-empty, so FR-072…FR-075 apply in full: the state
  becomes v3, `kStateV3Bytes = 436` (= 428 + 4·2).
- **Consequences of the ratified `R`:**
  - New parameter IDs are added to the ecosystem block for `syncRate` and `selfAffinity` (FR-072); the
    registered surface grows from 108 to **110** IDs.
  - The state format bumps to **v3** (`kCurrentStateVersion = 3`), `kStateV3Bytes = 436`, with **v2 load
    compatibility**: a v2
    stream (or a v2 factory preset) loads with both knobs at their *current hard-coded default*
    (FR-072).
  - Controls for `syncRate` and `selfAffinity` are added to the Phase 13 ecosystem page
    (`resources/editor.uidesc`);
    the page union, bound-ID count and allowlist tests are updated to include them (FR-073).
  - Processor, controller, uidesc, param-table and state tests are updated for the new IDs and the v3
    format (FR-074).
  - **Coverage matrix Group E is extended** (FR-075): the two counted extremes become `Capability` cells
    `E6.hi` (`syncRate`, ratification order) and `E7.hi` (`selfAffinity`) — no `.lo` cell for either
    knob, both being one-sided in the 13b tables — verified by C-7.4 ablation (the knob's ID reset to
    its registered default) — **in
    addition to**, never in place of, the five kind→destination routes E1…E5. **Both cells are showcase
    primaries** (Clarifications Q5, session 2026-09-29 — option (a) of OQ-11, as plan ruling R-2 required):
    each is scored at the primary bar F with one pilot candidate per knob (FR-017a); FR-017 stops if
    either bar is unreachable. This fixed N = 38 + |R| = **40** (SC-029) — until gate G2 (2026-09-29): both bars unreachable, the cells are secondaries, **N = 38**.
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
| `A` | attack span, s: Standard → Σ stage0..3 ms / 1000; Growth → growth duration s (stage times are zeroed in Growth, `vorago_voice.h:338-342`) |
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
4. **Tail** (selector: the decoded freeze toggle, **or Freeze-On for the freeze-gesture render**): Freeze Off → RMS(`Tail`) ≤ RMS(`Sus`) − 40 dB (a
   stuck voice, a self-sustaining loop or an endless ghost replay fails); Freeze On → over the 60 s
   `Tail` the final-10 s RMS ≤ loudest-10 s RMS + 1.0 dB (non-growing) **and** ≥ loudest − 6.0 dB
   (the frozen field is held, not dying) **and**, the absolute floor, loudest-10 s RMS(`Tail`) ≥
   RMS(`Sus`) − **20 dB** (X = 20 dB, ratified — Clarifications Q3/OQ-2, this value fixed, not
   pilot-calibrated). The floor exists because the two
   relative inequalities pass vacuously on silence: `blockRmsDb` floors at −240 dBFS
   (`vorago_fixtures.h:343`), so an all-floor tail is "held and non-growing". The voices have released by `Tail`, but what remains is **not**
   only the cavern's frozen field: the ghost `AtmosphereEngine` sits inside the engine, before the
   cavern, and its wet return is summed into the bus the caller's `CavernVerb` receives
   (`vorago_engine.h:1154-1170`); freeze mutes only the tank's sends (`aether_reverb.h:409-410`), and a
   ghost grain lives up to `AtmosphereEngine::kMaxGrainSeconds` = 30 s (`atmosphere_engine.h:311`). A
   dry residue (ghost grains, smear and sub tails) can therefore reach the output inside the Freeze-On
   `Tail`, which starts up to 55 s earlier than the Freeze-Off window the ungestured render bounds, and
   the floor alone does **not** prove a held cavern field. **Dry-residue twin (required).** The
   freeze-gesture render is repeated with `kSpaceMixId` (1105) → 0.0, identical otherwise (same preset,
   seed, stimulus and gesture); its loudest-10 s RMS over the same `[H + Rel + 10 s, H + Rel + 70 s]`
   window MUST be ≤ RMS(`Sus`) − **40 dB**. With the floor, this puts the gesture render's loudest-10 s
   `Tail` ≥ 20 dB above the twin's, so the frozen cavern, not the dry path, carries the held field. Both
   figures and their difference are printed.
   **Load-then-play precondition.** A stored-On stream would be loaded before the NoteOn at t = 0, and the
   cavern's engine mutes every send into the tank as freeze latches — "freeze mutes all three sends", the
   injection riding `(1 − freezeRamp)` (`aether_reverb.h:409-410`; `CavernVerb::setFreeze` forwards to it,
   `cavern_verb.h:735-737`) — so a preset loaded with freeze already On is predicted to freeze an
   **empty** tank and fail the floor. **How factory freeze is shipped and verified (Clarification Q1,
   session 2026-09-27, ratifying the recommended protocol): the freeze-gesture render.** The D10.1 host preset — the S8-primary preset (ruling R-5, FR-011b) — stores **Space Freeze Off**; an **additional** render of it with the same stimulus, the **freeze-gesture render**, delivers `kSpaceFreezeId` (1115) → On through `IParameterChanges` at `A + 65 s`, exactly as a player engaging Freeze after the bloom would, and then
   applies the Freeze-On tail criteria above including the absolute floor; the preset's `Comment` (FR-003)
   instructs the player to engage Freeze once the drone has bloomed. No processor load-behaviour change:
   freeze still mutes the tank's sends on a stored-On load exactly as today, and a preset that stores
   Freeze On and fails the floor remains an FR-017 finding, never a pass (FR-033).
   **Render identity.** The freeze-gesture render (and its dry-residue twin) is **in addition to** the
   host preset's ordinary, ungestured C-6 render, never a replacement. Arms 1–3, arm 4's Freeze-Off
   criterion (the host stores Freeze Off), `M1…M3`, `D(P)`, `s(P)`, every ablation/E twin and every
   distinctness score come from the **ungestured** render only. The gesture render is scored by arm 1
   over its own `[0, Total]` plus the Freeze-On tail criteria (floor included) and the dry-residue twin
   — nothing else.

### C-7 — Distinctness: parameter space and sound space, threshold ruled here

The roadmap requires "pairwise measured-tolerance spectral/fingerprint distance over the long-render
sweep … with the threshold ruled in the spec rather than assumed" (line 635-637). Seraphis's precedent
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

**Seed-marginal descriptor (Clarification Q2, session 2026-09-29 — resolving OQ-10 as option (a)).**
`D(P)` is redefined as the mean, over **K takes**, of P's three per-minute descriptors: the stored seed
plus `K − 1` fixed seed-index offsets from it. This absorbs the post-13b take-to-take spread (finding 5;
default-surface seed twin measured at 5.9515) by averaging it down instead of raising F to swallow it.
F = 4.0, the factor 2, the F/2 margins and the 0.05 level bound are **unchanged** from the first pass.

- **K takes and `t_K` (Clarification Q3, session 2026-09-29 — option (a)).** Only 16 seed indices exist
  (`global_params.h:118-124`). The K takes of a preset are the stored seed plus offsets forming **two
  disjoint sets** of seed indices `(stored + j) mod 16`; `t_K` is the sound-space `d` between the two
  disjoint K-take means of the same preset. Since two disjoint sets of 16 indices admit at most 8 each,
  **K is hard-capped at 8**. K itself is ruled once, at the pilot (FR-017a), as the smallest K with
  `2 · t_K ≤ F`; if `K = 8` does not reach that, FR-017 stops and surfaces the measured `t_K` curve — K
  is never pushed past 8 and F is never raised to compensate.
- **Self-distance** `s(P)`: the **largest** pairwise `d` among P's three per-minute descriptors, **each
  averaged over the ruled K takes** (Clarification Q4, session 2026-09-29 — option (a): `s(P)` uses the
  K-take-averaged `M1`, `M2`, `M3`, never a single take's spread or the largest single-take spread) —
  the preset's own intrinsic variation across the whole hold, free of charge. For an
  ablation/E twin, which stays **single-take at the stored seed** (Clarification Q4), rendered only to
  the end of `Sus`, `s` of that render is `d` between `Sus`'s two 30 s
  halves of that one take.
- **Measured take-to-take distance** `t_max`: the largest `t_K` observed across the control set in
  control (b) below (the K-fold generalisation of the first pass's single seed-twin `d`).
- **Distinctness floor:** for every pair, `d(P, Q) ≥ max(F, 2 · t_max)` with **F = 4.0** (sweep ruling
  S-2, 2026-09-30: the former `2 · max(s(P), s(Q))` term is recorded per preset, not gated). The `2 · t_max` term ties the floor to measurement: if a new K-take mean of one preset
  moves the descriptor by `t_K`, two presets must differ by at least twice that.
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
  (b) *Seed twin (K-fold, Clarification Q3)* — for each preset in `C`, `t_K` (the `d` between its two
  disjoint K-take means, as defined above) scores `d ≤ F / 2` (= 2.0): a margin, not
  merely `< F`, above take-to-take variation. `t_max` is the largest `t_K` observed across `C`. A `t_K`
  above F/2 means the metric barely tells a new K-take mean from a new preset: **stop and surface**
  (FR-017), never lower F or widen the margin silently — this is the same stop-and-surface path K's own
  hard cap at 8 can trigger (FR-017a).
  (c) *Sub twin* — each preset in `C` with `kSubLevelOffsetId` at stored ± 6 dB (normalized ± 0.125;
  nothing else changed) scores `d < F`. A side whose shifted value would leave [0, 1] is **not
  rendered** (an out-of-range definition is a generator error, Edge Cases); the control uses the
  in-range side(s) only — at least one always exists, the range being 1.0 wide — and the harness
  records which side(s) ran.
- **Pilot-calibrated, not merely asserted (Clarification Q3, session 2026-09-27; K ruled by Clarification
  Q2–Q4, Q6, session 2026-09-29 — FR-017a).** F is not
  ratified from arithmetic alone: before the full library is authored, the plan renders a 6–8 preset
  pilot set (including the default surface and one showcase candidate per ratified roster knob),
  measures `t_K` for increasing K, rules **K** as the smallest with `2 · t_K ≤ F` (hard-capped at 8), and
  measures a deliberately near-variant pair's `d` as a **floor check** (it must score `d < F`; scoring
  `d ≥ F` is itself an FR-017 stop, not a signal to raise F). F is frozen at **4.0** — the K-take
  averaging (not a higher F) is what absorbs the post-13b take-to-take spread. Every threshold in this
  spec uses the frozen
  **F = 4.0** floor. The factor 2, the F/2 margins and the 0.05 level bound are this spec's
  ruling per roadmap line 636-637 and do not move with the pilot.
- **Post-13b measurement, resolved (second pass — OQ-10 resolved by Clarifications Q2–Q4, Q6, session
  2026-09-29).** The
  seed-twin control is no longer a prediction. The 13b tables render the **default surface** and its
  seed twin (`kSeedId` index 0 → 1, `ecosystem_rule_probe_test.cpp:185`) with this exact descriptor and
  hold windows (M1–M3 of a 340 s render = `A + 185` for the default `A` = 155 s), and print
  `t0on = d(default, seed twin)` = **5.9515** (default) and **6.8514** (Life max) — this is the K = 1
  case of `t_K`. The colony-off
  reseed distance is 4.0439 / 4.0645 (six-seed medians, 13b compliance FR-010/FR-011 rows). Rather than
  raising F to ≥ 11.9 to swallow this spread (option (b), rejected) or dropping the take term altogether
  (option (c), rejected — the weakest reading of "measured-tolerance", roadmap line 636), this spec
  **keeps F = 4.0 and the take term, and averages the spread down**: the pilot measures `t_K` at
  increasing K and rules K as the smallest with `2 · t_K ≤ F`; if per-take spread falls roughly as
  `1/√K`, the 5.95 figure needs K ≈ 9, so the pilot's hard cap at K = 8 (only 16 seed indices exist,
  C-7.3 above) **may bind** — if it does, FR-017a's stop-and-surface fires with the measured `t_K` curve,
  not a fallback to (b) or (c). The main render's K× cost is absorbed by C-10's sharding lever, never by
  shortening. **The F-based numbers in C-7.3, C-7.4, FR-011, FR-015 and FR-017a are no longer blocked:**
  F = 4.0, the factor 2 and the F/2 margins stand as ruled; `t_max` and K are measured at the pilot per
  FR-017a. SC-008, SC-010 and SC-011 are unblocked and read the concrete formulas above.

#### C-7.4 Ablation (verifies Group S and M claims)

A claimed S/M cell (and every D cell whose audibility conjunct is a reversion or depth ablation)
verifies iff `d(P_Sus, P_ablated) ≥ D` (G2 ruling 2026-09-29: the former `2 · s(P)` term is recorded, not gated), where `P_ablated` is P with the C-2.1 override
applied, rendered on P's own timeline to the end of M3, and `P_Sus` is the mean of P's `M1…M3`
descriptors, the twin scored on the mean of its own three minute descriptors (sweep ruling S-3,
2026-09-30; before it: `Sus` = `M1` only). The
bar `D` depends on the claim's role:

- **Primary:** `D = F = 4.0` — "foregrounded": removing the showcased capability must move the preset as
  far as the distinctness floor separates two presets.
- **Secondary:** `D = D_abl = 1.5` — "measurably present".

Both are ruled here (F pilot-calibrated per Clarification Q3/FR-017a; D_abl = 1.5 fixed, does not move
with the pilot). E cells use the same two bars through their route-isolated measurement (C-2.1 Group E);
the C-2.3 roster-extension E cells use the same two bars through plain C-7.4 ablation.

**Envelope cells (D8.x, D9.x) — attack-window reversion.** Reverting an envelope mode or its stage times
changes only the swell, and `Sus` = `[A + 5, A + 65]` begins after it, so a `Sus`-scored reversion
cannot see the capability (any `Sus` distance it did find would be seed-chaotic colony divergence caused
by the different timing, not the envelope). For these cells P and its reversion twin `P_rev` (the
envelope IDs reset to their registered defaults; same seed and stimulus) are rendered from t = 0 on the
**same absolute timeline** to `W_end = max(A_P, A_rev) + 5 s`, and:

- `D_att(X)` = the C-7.2 descriptor of X over `[0, W_end]`, with every 1 s `blockRmsDb` value feeding
  the `e` component clamped below at RMS(`Sus`_P) − 60 dB, so the −240 dBFS silence floor
  (`vorago_fixtures.h:343`) cannot dominate;
- the cell verifies iff `d(D_att(P), D_att(P_rev)) ≥ D` (G2 ruling: no `2 · s(P)` term) **and**
  `d(D_att(P), D_att(P_rev)) ≥ d_Sus(P, P_rev) + D_abl`, where `d_Sus` is the distance between the two
  renders' own `Sus` descriptors, each on its own C-6 timeline — the attributability conjunct, as
  `R_∅` is for Group E: the change must live in the swell, not in sustain divergence;
- the harness prints each render's time to first reach RMS(`Sus`) − 6 dB (first 1 s `blockRmsDb`
  window at or above it) for the compliance record.

Its render length is bounded by C-6's `A ≤ 180 s` and is accounted for by C-10 sharding.

### C-8 — Determinism without bit-exact goldens

`kSeedId` is persisted (`global_params.h:118-126`), so two renders of one preset in one process are
driven from the same seed. Reproducibility is asserted with `compareFingerprints` at the shared
tolerances (`render_fingerprint.h:58-63`, `:122-124`). No float bit digest, and no integer digest of
float bits, is introduced anywhere (roadmap line 687). The generator's **file-byte** determinism
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
  calculation, not the stored decay alone) + the ghost-grain ceiling `G` = `AtmosphereEngine::kMaxGrainSeconds` (30 s, FR-060), from the current decoded state,
  and **`kInfiniteTail`** while Space Freeze is On. Tested against the decoded state (FR-060).
- **Ghost density vs trigger (Phase 10a Q1) — RESOLVED (Clarifications Q8, session 2026-09-29).**
  Additive, as shipped: the density scheduler keeps running at 0.30
  grains/s, and triggered grains add on top; no new parameter is introduced. Replacement would need a
  density control the plugin does
  not register (`ghost_params.h:35-40` has peak level, blur, reverse probability, triggers only) and is
  out of scope. [FR-061]
- **`docs/index.html` (Phase 11 FR-080).** `plugins/vorago/docs/` holds only `.gitkeep`; the Seraphis
  page (`plugins/seraphis/docs/index.html`, `assets/style.css`) is the template. `docs.yml` needs no edit.

---

## Functional Requirements

### Category set and library

- **FR-001** `makeVoragoPresetConfig()` MUST declare exactly the seven C-1 subcategories in C-1 order,
  `"Drones"` byte-identical and first; `processorUID`, `pluginName == "Vorago"`,
  `pluginCategoryDesc == "Synth"` unchanged; designated field order matches
  `preset_manager_config.h:19-24`. [roadmap 623; CLAUDE decision 2]
- **FR-002** `plugins/vorago/resources/presets/` MUST contain exactly one directory per C-1 category and
  no other, every `.vstpreset` directly inside one of them (1:1 both ways with FR-001). [roadmap 623]
- **FR-003** Every preset MUST embed an `Info` chunk whose attributes equal what the shared
  `PresetManager::savePreset` writes (`preset_manager.cpp:264-275`): `MediaType = VstPreset`,
  `PlugInName = Vorago`, `PlugInCategory = Synth`, `Name` = file stem, `MusicalCategory` and
  `MusicalInstrument` = the directory name, `Comment` = the definition's description. [roadmap 623-624]
- **FR-004** N (plan-derived, C-2.2) MUST satisfy every C-2 rule; every category MUST hold **≥ 3**
  presets (a browse tab with one or two entries is a hole, not a category); there is no other per-category
  count. [roadmap 634-635]
- **FR-005** Names MUST be unique across the library, satisfy `PresetManager::isValidPresetName`
  (`preset_manager.h:120`), and be ASCII with no path separator.
- **FR-006** Every `Comp` chunk MUST be a full current-version stream: first int32 == `kCurrentStateVersion`
  (= 3), length == `kStateV3Bytes` (= 428 + 4·|R| = **436**, `R` = {`syncRate`, `selfAffinity`} ratified
  Clarifications Q1). (Plan ruling R-3 / P-2: factory
  presets are v3, never "version 2, 428 bytes".)
- **FR-007** Every preset MUST store polyphony index ≤ 3 (≤ 4 voices) (C-5).
- **FR-008** Every preset MUST satisfy the C-6 authoring ceilings `A ≤ 180 s`, `Rel ≤ 60 s`.
- **FR-009** No stored float may be non-finite (bit-pattern check).

### Coverage matrix (the variety requirement)

- **FR-010** `tools/vorago_preset_defs.h` MUST declare the `Capability` enum with **exactly** the C-2.1
  cells (S1–S10, M1–M12, E1–E5, D1.1–D14.2), plus the ratified-roster extension cells of C-2.3/FR-075:
  `E6.hi` (`syncRate`) and `E7.hi` (`selfAffinity`), the two counted extremes of the ratified `R`
  (Clarifications Q1), each carrying its group, its predicate and — for S/M and
  the roster extension — its ablation override. [roadmap 634]
- **FR-011** Every `VoragoPresetDef` MUST declare one **primary** cell and zero or more secondary cells;
  no two definitions share a primary; no primary is a default-state cell; every primary verifies at the C-7.4 **primary bar** (`d ≥ max(F, 2·s)`) (C-2.2). [roadmap 632, 637-638]
- **FR-011b** **D10.1 is never a primary (plan ruling R-5).** No definition may name D10.1 as its
  primary. D10.1 MUST be claimed as a verified **secondary** of the preset whose primary is **S8**, and
  verified on that preset's freeze-gesture render (FR-033, C-6 arm 4): its reversion (freeze → Off)
  sounds identical over `Sus`, so no reversion ablation can score it. SC-029's floor of 38 depends on
  this exclusion. [R-5; Clarification Q1]
- **FR-011a** **Non-subset showcase.** For every ordered pair (P, Q), P ≠ Q, at least one cell P claims
  MUST NOT be verified by Q, evaluated against Q's **full verification vector** (FR-012, Clarification
  Q5) rather than rendered on demand. The harness MUST print, per P, the witness cell found against each
  Q. [roadmap 637-638]
- **FR-012** **Full verification vector (Clarification Q5).** For every preset, the harness MUST compute
  and cache the **full verification vector**: every C-2.1 cell's predicate result (verified / not),
  not only the cells that preset claims — S/M by C-7.4 ablation, E by the route-isolated ablation of
  C-2.1 Group E (never by the S7 + destination conjunction) plus one plain-ablation cell per counted extreme of each
  ratified roster knob (C-2.3), D from the decoded state (FR-031) **plus** the cell's audibility
  conjunct — computed **once per preset** (O(N × cells), the strongest and most deterministic of the
  evaluated alternatives). The harness MUST then check every declared claim against its own preset's
  cached vector at the bar for the claim's role and fail on any claim that does not verify; it MUST also
  run the default surface through the D predicates to identify default-state cells. FR-011a and FR-013's
  printed matrix are derived from these cached vectors, never re-rendered per ordered pair.
- **FR-013** Every C-2.1 cell MUST be verified by ≥ 1 preset; every E*k* (E1–E5) MUST be the verified
  **primary** of ≥ 1 preset (five distinct presets); and, per Clarification Q5 (resolving OQ-11), every
  ratified roster-extension cell (`E6.hi`, `E7.hi`) MUST likewise be the verified **primary** of its own
  distinct preset (seven distinct primary presets across Group E in total). The harness MUST print the full preset × cell matrix (verified
  as primary / verified as secondary / claimed-failed / unclaimed), the default-state cells, and the
  FR-011a witnesses for the compliance record.
- **FR-014** Every preset MUST satisfy C-7.1 (parameter-space distinctness) against every other preset
  and against the default surface. [roadmap 633]
- **FR-015** Every preset pair MUST satisfy C-7.3's sound-space floor, computed on the seed-marginal,
  K-take descriptor `D(P)` (Clarification Q2/FR-017a) and including the `2·t_K` term; all four C-7.3
  negative controls (level, gain, seed, sub twins) MUST pass on the control set in the
  same run. [roadmap 635-637]
- **FR-016** The plan MUST derive the preset list from the matrix (each preset = a primary cell plus
  compatible secondaries), and only then assign categories; the plan MUST NOT start from a per-category
  count. [roadmap 635]
- **FR-017** **Stop-and-surface.** If a cell cannot be verified by any authorable preset, or a C-7.3
  control fails, the build stops and reports the finding with measurements; it MUST NOT relax a predicate,
  a floor or a control, drop a cell, or edit `dsp/` to pass.
- **FR-017a** **Pilot calibration, seed-marginal descriptor and K (Clarifications Q2–Q4, Q6, session
  2026-09-29 — resolving OQ-10).** Before authoring the full preset library, the
  plan MUST render a **6–8 preset pilot set** that includes the default surface as a pseudo-preset (so
  the pilot re-measures the 13b figure on the harness's own renders) and **one showcase candidate per
  ratified roster knob** (`syncRate`, `selfAffinity` — OQ-11 is resolved as primaries, Clarification Q5,
  so both `E6.hi` and `E7.hi` need a pilot candidate). F stays **4.0** (never set below it) and is not
  raised to absorb the post-13b seed-twin spread; instead:
  - **Seed-marginal descriptor.** `D(P)` is redefined as the mean, over **K takes**, of the per-minute
    descriptors (C-7.3): the stored seed plus `K − 1` fixed seed-index offsets from it. `t_K` is the `d`
    between two disjoint K-take means of the same preset (Clarification Q3): the K takes split into two
    disjoint sets of seed indices `(stored + j) mod 16` for `j` in each half; **K is hard-capped at 8**
    (two disjoint sets of 16 seed indices admit at most 8 each). The pilot MUST measure `t_K` for
    `K = 1, 2, 4, 8` (or the smallest prefix needed) on the pilot set and **rule K** as the smallest K
    with `2 · t_K ≤ F`. If `K = 8` does not reach `2 · t_8 ≤ F`, **FR-017 stops and surfaces the measured
    `t_K` curve** (K is not raised past 8, F is not raised, and the run is not silently retried at a
    different K).
  - **Self-distance `s(P)`.** Per Clarification Q4, `s(P)` is the largest pairwise `d` among the three
    per-minute descriptors, **each averaged over the ruled K takes** — i.e. `s(P)` is computed from the
    K-take-averaged `M1`, `M2`, `M3`, not from a single take's spread and not from the largest
    single-take spread across takes.
  - **What runs on every take vs. the stored seed only (Clarification Q4).** The C-6 long-render arms
    1–4 (FR-033, SC-012) run on **every one of the K takes**. The C-7.4 ablation renders and the Group E
    route-isolated renders (`R_k`, `R_k⁰`, `R_∅`, `R_∅⁰`) stay **single-take, at the stored seed**, and
    are scored against that take's `M1` (`Sus`) descriptor — they are same-seed comparisons whose noise
    term is `s(P)`, not the take-to-take term. The main K-take render's cost (K× the single-take cost) is
    absorbed by C-10's sharding lever, never by shortening any render.
  - **Near-variant pilot pair (Clarification Q6).** The pilot's deliberately authored near-variant pair
    is a **floor check only**: it MUST score `d < F`. If it scores `d ≥ F`, that is itself an FR-017
    stop — the descriptor cannot tell near-variants apart at F = 4.0 — never a signal to raise F from
    the pair's measured `d` (F is not set from this pair; option (b) of the original Q6 choice is
    rejected).
  - The pilot measurement (`t_K` per K, the ruled K, the near-variant pair's `d`, and every showcase
    candidate's pilot score) MUST be recorded in the compliance record. Every F-based threshold elsewhere
    in this spec (C-7.3, C-7.4, FR-011, FR-015, SC-008, SC-010, SC-011) uses **F = 4.0**.

### Ecosystem rule-knob parameters (Clarification Q2 — scope widening, inside Phase 14)

- **FR-070** **Roster evidence (second pass).** The roster `R` MUST be drawn from Phase 13b's two FR-013
  tables (`final2_table_default.log`, `final2_table_lifemax.log`; C-2.3's table), counted at 13b's
  ruled bar (`d ≥ 0.5·t0`, not KILL, not OFF-LIKE), per roadmap lines 598–600 and 606. No new selection
  probe is run; the probe TU (`Vorago_EcosystemRuleProbe`, `[.probe]`) stays in the tree unchanged as the
  instrument that produced the evidence. The compliance record MUST cite both tables' knob rows for every
  knob in `R` and for every counted knob left out of `R`. [roadmap 598-600, 606, 640-645]
- **FR-071** **Stop-and-surface roster ratification — discharged.** `R` MUST be ratified by the user
  before any new parameter ID, the state v3 format, any UI control, or any preset referencing `R` is
  authored. **`R` is ratified as `R` = {`syncRate`, `selfAffinity`}** (Clarifications Q1, session
  2026-09-29 — option (a): exactly the counted set; no additional knob outside the counted set is
  added). [roadmap 644]
- **FR-071a** **Parameter semantics follow the measurement.** Each registered knob MUST drive exactly
  the setter call(s) the 13b probe measured: `syncRate` → `EcosystemEngine::setSyncRate(float)` on every
  voice's engine, range [0, 0.5], default 0 (`ecosystem_engine.h:595-599`); `selfAffinity` →
  `setAffinity(k, k, v)` for all five `Kind`s (`ecosystem_rule_probe_test.cpp:279-284`), range [−2, 2],
  default −1.0 (`ecosystem_engine.h:2403-2411`). The registered default MUST equal the component's
  current hard-coded default, so a v2 load and the untouched surface render as today (FR-072).
- **FR-072** **New parameter IDs and state v3.** Once `R` is ratified, a new registered parameter ID MUST
  be added to the ecosystem block (`plugin_ids.h`) for every knob in `R`, wired to its existing
  `EcosystemEngine::set*` setter; `kCurrentStateVersion` MUST become **3**; `setState`/`getState` MUST
  read/write the v3 block; a v2 stream (including every v2 factory preset built before this ratification)
  MUST load successfully with every knob in `R` at its **current hard-coded default**.
- **FR-073** **UI controls.** The Phase 13 ecosystem page (`resources/editor.uidesc`) MUST gain one
  control per knob in `R`; the page union, bound-ID count and allowlist tests MUST be updated to include
  the new IDs.
- **FR-074** **Test coverage.** Processor, controller, uidesc, param-table and state-format tests MUST be
  updated for the new IDs and the v3 format (round-trip, v2→v3 default-load preconditioned per SC-027, bounds).
- **FR-077** **Noise-bus make-up gain (sweep ruling S-1, 2026-09-30).** `VoragoVoice` MUST apply one
  constant make-up gain (`kNoiseBusMakeupDb` = 30 dB, `kNoiseBusMakeupGain` = 10^(30/20)) on the noise
  organism's bus, after the organism's own gain chain and before the excitation bus, sized so the bed
  at +12 dB / wake 1.0 reads within 6 dB of the drone at the voice output (measured −25.1 dBFS against
  −19.7). This is the phase's one DSP change beyond R-1; every Phase 2–13 suite, 13b's ghost
  fingerprint and the Phase 10 [long] soaks MUST pass on the amended voice, and any fingerprint that
  moves is re-harvested inside its consuming binary (SC-033).
- **FR-077a** **Reset reproducibility after a type switch (sweep-2 ruling S-6, 2026-09-30).**
  `NoiseGenerator` MUST expose `snapLevelSmoothers()` (every per-type level smoother and the master landed
  on its target), and `NoiseOrganism::applySlotConfiguration` MUST call it after its level pushes, so that
  after `reset()` the organism's stream is a function of configuration alone whether or not the instance
  rendered between a noise-type switch and the reset. The phase's second DSP change beyond R-1; every
  Phase 2–13 suite MUST pass on it and any fingerprint that moves is re-harvested inside its consuming
  binary (SC-033).
- **FR-077b** **Ghost-tap make-up (re-author ruling S-7, 2026-09-30).** `VoragoEngine` MUST apply one constant
  make-up gain (`kGhostTapMakeupDb` = 12 dB, `kGhostTapMakeupGain` = 10^(12/20)) on the atmosphere's WET texture
  at the bus sum, after the component's own [0, 2] trim, sized so the tap's loudest second at peak level 1.0
  sits within 6 dB of the drone (measured −20.8 vs −19.2 dBFS); `setGhostTapMakeupDb` / `getGhostTapMakeupDb`
  expose it for measurement (clamped [0, 24] dB, non-finite rejected). The phase's third DSP change beyond
  R-1; every Phase 2–13 suite MUST pass on it and any fingerprint that moves is re-harvested inside its
  consuming binary (SC-033).
- **FR-075** **Coverage matrix extension.** For `syncRate` and `selfAffinity` (the ratified `R`), the two
  extreme cells that exist and were counted (`E6.hi` = `syncRate` high, `E7.hi` = `selfAffinity` high;
  no `.lo` cell for either knob) MUST be added to the `Capability` enum (FR-010) and to the coverage matrix
  (FR-012/FR-013), verified by C-7.4 ablation (the knob's ID reset to its registered default) at the
  **primary bar F** (Clarification Q5, session 2026-09-29 — resolving OQ-11 as option (a): both cells
  MUST be showcase primaries, never merely secondaries), **in addition to** the five kind→destination
  routes E1…E5 (never in place of
  them). **Side predicate, ratified at 0.5 (Clarification Q7, session 2026-09-29 — resolving OQ-9
  sub-question (d)):** an `E{n}.hi` claim additionally requires the
  decoded normalized knob `n ≥ n₀ + 0.5·(1 − n₀)`, and an `E{n}.lo` claim (where one exists)
  `n ≤ n₀ − 0.5·n₀`, with `n₀` the registered default normalized — the stored value sits at least
  halfway from the default toward the counted extreme, so the reversion scores the counted extreme's
  neighbourhood, not a token nudge; `.lo` and `.hi` are mutually exclusive on any one preset. Concretely:
  `syncRate` (n₀ = 0) stored ≥ 0.25 plain;
  `selfAffinity` (n₀ = 0.25 over [−2, 2]) stored ≥ +0.5 plain.
- **FR-076** **Non-goals boundary.** No DSP class is created for `R` (existing `EcosystemEngine::set*`
  setters only, C-2.3); no parameter outside the ecosystem block and its page is added; every other
  Non-goal (new DSP, new parameter *types*, retuning to force variety, `Info`-chunk read-back, user-preset
  UX, non-Windows factory installers) is unaffected by this widening. **The `dsp/` edit is bounded (plan ruling R-1):** the only `dsp/`
  edits are, per `k ∈ R`, one `VoragoVoice` forwarder, one `VoragoVoiceParams` field
  (`kFieldCount` becomes exactly **33** = 31 + |R|, `vorago_engine.h:187`) and one `applyVoiceParams` line
  (`:859`), all append-only (no existing field, line or forwarder reordered or changed), beside the inert
  probe friends already in the tree (`vorago_voice.h:171`, `:1568`; `vorago_engine.h:1304`). Each knob
  reaches the engine only through `VoragoVoiceParams` → `applyVoiceParams` → the forwarder; no direct
  processor-to-`EcosystemEngine` path exists and no other `dsp/` file is edited.

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

- **FR-027a** **Harness registration staging** (plan ruling R-7(i), coverage fix, Clarifications session
  2026-09-29): the probe TU MUST be registered in `plugins/vorago/tests/CMakeLists.txt` before gate G1;
  every other new test TU and the generator targets MUST be registered in **one** mid-phase task; the
  final task group only audits the registration (no new registrations at that point).
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
  absolute floor), run on **every one of the preset's K takes** (Clarification Q4, session 2026-09-29 —
  C-7.3's seed-marginal descriptor), and, for the D10.1 host preset (the S8-primary preset, FR-011b), the
  **freeze-gesture protocol** (Clarification Q1): the preset stores Space Freeze Off; an **additional**
  render, the freeze-gesture render, delivers `kSpaceFreezeId` → On via `IParameterChanges` at
  `A + 65 s` and is scored by arm 1 and the Freeze-On tail criteria including the floor **only** — arms
  1–4 (Freeze-Off tail), the descriptors and every ablation and distinctness score come from the
  preset's ungestured render (C-6 "Render identity"); a **dry-residue twin** of the gesture render with
  `kSpaceMixId` (1105) → 0.0 MUST score loudest-10 s RMS over the same Freeze-On `Tail` ≤ RMS(`Sus`) −
  40 dB; the preset's `Comment` instructs the player to engage Freeze after the bloom. **Load behaviour
  is frozen:** the processor's and `CavernVerb`/`AetherReverb`'s freeze-on-load behaviour MUST NOT
  change — a stored-On stream still mutes the tank's sends on load (`aether_reverb.h:409-410`). A preset
  storing Space Freeze On that fails the absolute floor is an FR-017 finding, never fixed in
  `processor.cpp`, `cavern_verb.h` or `aether_reverb.h`. [roadmap 624-626; Clarification Q1]
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
  every preset (mean of `M1…M3`, each averaged over the ruled K takes per Clarification Q2/FR-017a) from
  the FR-033 render; C-7.3 over all C(N, 2) pairs; all four negative
  controls on the control set. The run MUST print min / median / max `d`, the minimum pair's names, every
  `s(P)`, `t_max`, the ruled K, and the effective floor.
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
  `vorago_voice.h:324-328`), then blocks inside its own `Sus` window `[A + 5 s, A + 65 s]` are timed,
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
  prevent. Levers on breach: re-author the preset; never relax. [roadmap 479-485, 677]
- **FR-042** **Human listening checkpoint:** the phase owner auditions every preset and records, per
  preset, its category fit and a one-line character note, and confirms no two presets read as variants.
  Recorded in the compliance document; no automated arm substitutes for it and it substitutes for none.

### Deferred items and release

- **FR-060** **`getTailSamples()` is state-derived (Clarification Q7).** It MUST report
  `llround((Rel + RT60_eff + G) · sampleRate)` samples, where `Rel` and `RT60_eff` are C-6's, from the
  currently decoded state (effective RT60 per C-6's cavern-target calculation, not the stored decay
  alone), and **`G` = `AtmosphereEngine::kMaxGrainSeconds` = 30 s** (`atmosphere_engine.h:311`) — the
  longest life any ghost grain born before NoteOff can have (`setGrainSeconds` clamps to it,
  `:855-857`), a state-independent ceiling, not the configured grain length. It MUST report
  **`kInfiniteTail`** while Space Freeze is On. `getTailSamples()` is a **ceiling a host must not
  truncate before**, not a prediction of when the output falls silent: over-reporting costs a host only
  rendered silence, under-reporting truncates. It therefore legitimately extends past the start of C-6's
  Freeze-Off `Tail` (`H + Rel + RT60 + 5 s`), where arm 4 asserts the output has *already* fallen to
  ≤ RMS(`Sus`) − 40 dB; the two do not conflict, and a ghost grain still audible there is the Edge Cases
  FR-017 finding, not a reason to move either. A test MUST assert the reported value equals the expected
  value within **±1 sample**, computed from independently decoded state, for the named representative
  set: (i) minimum stored decay (0.5 s) with no decay-raising macro; (ii) maximum stored decay (60 s)
  with Depth = 1 and Age = 1 (the 60 s clamp engaged); (iii) Space Freeze On (`kInfiniteTail`); (iv) the
  default surface; (v) every factory preset.
- **FR-061** **Ghost density and event triggers are additive (Clarification Q8, session 2026-09-29).**
  The 0.30 grains/s density scheduler MUST keep running unchanged when Ghost Event Triggers is On;
  triggered grains MUST add on top of it, never replace it. No new parameter is introduced. Presets that
  verify D12.1 (Ghost Event Triggers On) use this additive behaviour, and the compliance record cites
  this ruling.
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
  exists). Record a green/red verdict. [roadmap 626]
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
| SC-008 | Coverage + showcase | 100 % of C-2.1 cells verified; each E*k* (E1–E5) the verified primary of a distinct preset, and each ratified roster-extension cell (`E6.hi`, `E7.hi`) verified as a **secondary** by ≥ 1 preset (G2 ruling 2026-09-29; before it: the verified primary of a distinct preset (seven distinct primary presets in Group E); 0 claimed-but-failed; F = 4.0 (Clarifications Q2–Q4, Q6); unique primaries, none a default-state cell, D10.1 no preset's primary (FR-011b), each at the primary bar `d ≥ max(F, 2·s)` (`s` from the K-take-averaged descriptor; envelope primaries by the C-7.4 attack-window reversion); every ordered pair (P, Q) has a claimed witness of P that Q fails (FR-011a); D cells verified with their audibility conjuncts | `Vorago_PresetMatrix_CoverageComplete`, `Vorago_PresetMatrix_NoShowcaseSubset` ([long][vorago-sweep]; + printed matrix, default-state cells and witnesses) |
| SC-009 | Parameter-space distinctness | every pair and preset-vs-default: ≥ 8 IDs differ (≥ 0.10 normalized or index) | `Vorago_PresetMatrix_ParameterSpaceDistinct` |
| SC-010 | Sound-space distinctness | Resolved (Clarifications Q2–Q4, Q6, session 2026-09-29): `D(P)` = mean of the `M1…M3` descriptors over K takes (stored seed + K − 1 fixed seed-index offsets, two disjoint sets of `(stored + j) mod 16`, K hard-capped at 8); `t_K` = `d` between two disjoint K-take means of the same preset, measured on every control-set preset; `t_max` = the largest `t_K`; K ruled at the pilot as the smallest with `2·t_K ≤ F` (FR-017a; FR-017 stop if K = 8 does not reach it). Bands relative to `E_hi`, −60 dB clamp. Every pair `d ≥ max(F, 2·max(s), 2·t_max)` with **F = 4.0**; on the control set `C` (≥ 3 named presets): post-render level twin `d < 0.05` (every preset), rendered gain twin on the highest-Pressure preset `d ≤ F/2`, seed twins (`t_K`) `d ≤ F/2` (gated), sub twins (±0.125 normalized, in-range side(s) only, C-7.3 (c)) `d < F` | `Vorago_PresetSweep_SoundSpaceDistinct` ([long][vorago-sweep]) |
| SC-011 | Ablation | every claimed secondary S/M/ablation-D cell `d(P, P_abl) ≥ 1.5`; every primary `≥ F` with **F = 4.0** (G2 ruling 2026-09-29: the `2·s(P)` term is recorded, not gated) (Clarifications Q2–Q4, Q6); D_abl = 1.5 is fixed; `s(P)` from the K-take-averaged descriptor for the preset's own full render, single-take for the ablation twin itself (Clarification Q4); envelope cells (D8.x/D9.x) scored by the C-7.4 attack-window reversion including its `d_Sus + 1.5` attributability conjunct; every claimed E cell (E1–E5 **and** the ratified `E6.hi`/`E7.hi`) passes the route-isolated or plain-ablation test at its role's bar and exceeds `d(R_∅, R_∅⁰) + 1.5` where applicable | `Vorago_PresetSweep_AblationVerifiesClaims` ([long][vorago-sweep]) |
| SC-012 | Long-render boundedness | 100 % presets: finite; peak ≤ 0.9661; every 10 s RMS ≤ −6 dBFS over `[0, Total]`; every 10 s RMS ≥ −60 dBFS over `[A, H]`; late/early ratio in [−18, +12] dB; tail arm per freeze with `Tail` placed on the **effective** RT60; Freeze-On tail loudest-10 s ≥ RMS(`Sus`) − 20 dB | `Vorago_PresetSweep_LongRender` ([long]) |
| SC-013 | Wall clock (CI lane) | every Vorago sweep job, per OS, measured step time ≤ 60 % of its `timeout-minutes` (≤ 180); shared per-push "Run Tests" step ≤ 80 % of its 20 min on every OS; generic nightly `[long]` step unchanged by this phase (filter `[long]~[vorago-sweep]`); local alone-run durations recorded for reference; the K-fold (Clarification Q2) main-render cost is met by sharding, never by shortening | CI job/step durations from the run logs (URLs cited in compliance); `-d yes` local durations |
| SC-014 | Short load-time guard | 100 % presets finite, peak ≤ 0.9661 at 44.1/48/96 kHz and chord (load-time guard only; no sustain claim) | `Vorago_PresetSweep_ShortBounded` |
| SC-015 | Reproducibility | `withinTolerance()` true for every preset | `Vorago_PresetSweep_RendersAreReproducible` ([long]) |
| SC-016 | RT-safe loading | sequential: 0 allocations; concurrent: 0 audio-thread allocations, 100 % `kResultOk`, bounded output | `Vorago_FactoryPresets_SequentialLoadNoAlloc`, `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe` |
| SC-017 | Preset CPU | timed blocks inside each patch's own `Sus` after an untimed pre-roll; polyphony forced to 4; worst preset ≤ 1.15 × default surface, interleaved, alone; absolute processor figure and stored-polyphony figure recorded vs 3 200 000 ns (engine-level 30 % ceiling not re-gated, FR-041) | `Vorago_PresetCpu` ([.perf]) |
| SC-018 | Ghost features engaged | ≥ 1 verified D11 and D12.1 preset | subset of SC-008 |
| SC-019 | Release gate | `version.json` == `1.0.0` and `CHANGELOG.md` has a `[1.0.0]` entry describing the factory library (FR-064); `release-readiness` vorago row green; pluginval 5 exit 0; check-portability clean; clang-tidy `vorago` 0 findings; `auval -v aumu Vrgo KrAt` passes (FR-065) | compliance record citing the release commit's `ci.yml` "Run Vorago AU Validation" run URL and log line |
| SC-020 | Listening checkpoint | 100 % presets audited per FR-042 | compliance record |
| SC-021 | Rosters | `vorago` present in both release rosters | grep in compliance record |
| SC-022 | Sustain at 44.1/96 kHz | 100 % presets over `[0, A + 65 s]` at 44.1 and 96 kHz: finite, peak ≤ 0.9661, every 10 s RMS ≤ −6 dBFS | `Vorago_PresetSweep_SustainAtAllRates` ([long][vorago-sweep]) |
| SC-023 | Dedicated sweep lane | `long-tests-nightly.yml` holds the FR-066 job(s) on three OSes, each `timeout-minutes` ≤ 180; `ci.yml` nightly filter is `[long]~[vorago-sweep]` on all three legs | grep in compliance record + one green nightly run URL |
| SC-024 | Freeze showcase | the **S8-primary** preset claims and verifies D10.1 as a **secondary** under the freeze-gesture protocol: its additional gesture render passes arm 1 and C-6 arm 4's Freeze-On criteria including the −20 dB absolute floor, and its `kSpaceMixId` → 0 dry-residue twin scores loudest-10 s `Tail` ≤ RMS(`Sus`) − 40 dB; **no preset names D10.1 as primary** (FR-011b); **no diff** to the processor's or `CavernVerb`/`AetherReverb`'s freeze load path over the phase (Clarification Q1, FR-033) | `Vorago_PresetSweep_FreezeGesture` ([long][vorago-sweep]); subset of SC-012 / SC-008; `git diff` of `processor.cpp` freeze/setState handling, `cavern_verb.h`, `aether_reverb.h` cited in compliance |
| SC-025 | Roster evidence | every knob in `R` = {`syncRate`, `selfAffinity`} and every counted knob outside `R` (none, per Clarification Q1) cited by its row in both 13b FR-013 tables (d, d/t0, flags); every `R` knob counted (`d ≥ 0.5·t0`, not KILL/OFF-LIKE) on ≥ 1 surface | compliance record (FR-070, FR-071) |
| SC-026 | Ecosystem roster ratified before authoring | the ratified `R` = {`syncRate`, `selfAffinity`} (Clarifications Q1, session 2026-09-29) is recorded, dated, before the first commit that adds a roster parameter ID, the v3 format, a roster UI control or a preset referencing `R` (commit order in `git log`) | compliance record (FR-071) |
| SC-026a | Knob semantics | for each `k ∈ R`, a processor-level test reaches the engine through the existing public const `Processor::engineForTest()` (`processor.h:116-118`; plan-stage ruling P2-4, 2026-09-29 — no new friend), and iterates **all `kMaxVoices` = 6 voices** via `VoragoEngine::getVoice(i)` (`vorago_engine.h:1280`) after ≥ 1 `process()`, once with no note held and again with a note held; it sets the registered knob to its counted extreme and asserts every voice's `ecosystem()` getter (`getSyncRate()`, `ecosystem_engine.h:601`; `getAffinity(k, k)` for all five kinds, `:721`) equals the plain value, and at the registered default equals the component defaults (0.0, −1.0); the count of voices checked is asserted == 6 in each pass | `Vorago_EcosystemRosterReachesEngine` (FR-071a) |
| SC-027 | Ecosystem parameters + state v3 | new IDs registered for `syncRate` and `selfAffinity`; `kCurrentStateVersion == 3`; `kStateV3Bytes == 436`; `static_assert(VoragoVoiceParams::kFieldCount == 33)` (= 31 + \|R\|) in a test TU (R-1 bound, FR-076); v2 load **from a non-default state**: every `k ∈ R` first driven to its counted extreme (by `IParameterChanges` or by loading a v3 stream with non-default values) and one `process()` run, then `setState(v2)` and one `process()`; both the persisted values (`getState` decode) and the engine getters on all 6 voices (`getSyncRate()`, `getAffinity(k, k)` for all five kinds, via the SC-026a probe) equal the hard-coded defaults | `Vorago_State_V2LoadsWithRosterDefaults`, `Vorago_VoiceParams_FieldCount`, param-table test (FR-072, FR-076) |
| SC-028 | Ecosystem UI + coverage extension | one control per `k ∈ R` on the ecosystem page; page union/bound-ID/allowlist tests updated; `E6.hi` (`syncRate`) and `E7.hi` (`selfAffinity`) each verified by ≥ 1 preset at the **primary bar F** (Clarification Q5, resolving OQ-11 as primaries) | `Vorago_Ecosystem_PageBindsRosterIds`, subset of SC-008 (FR-073, FR-075) |
| SC-033 | Noise bus audible (FR-077) | `VoragoVoice_NoiseBusProbe` (or its gated successor): output RMS with the noise at +12 dB / wake 1.0 vs at −96 dB, difference signal within 6 dB of the drone; the eight per-push suites, 13b's `VoragoEngine_GhostExtensionWiring` and the Phase 10 [long] soaks green on the amended voice | probe printout; regression logs |
| SC-033a | Reset replays after a type switch (FR-077a) | `NoiseOrganism_TypeSwitchedAfterPrepareReplaysAfterReset`: max\|diff\| == 0 and no differing sample between an instance that never rendered since switching every slot to MetallicHiss and one that rendered first, both after `reset()`, 8 kHz, 2 s; `VoragoEngine_SlotSeedReproducibility` (a)/(b) green on the amended voice | test output; `artifacts/reset_fix_targeted.log` |
| SC-033b | Ghost tap audible (FR-077b) | `VoragoEngine_GhostLevelProbe`: at peak level 1.0 the difference signal's loudest 1 s window within 6 dB of the drone's sustain RMS and above −40 dBFS in every 1 s window of 220–340 s; the eight per-push suites, 13b's `VoragoEngine_GhostExtensionWiring` (re-harvested) and the Phase 10 [long] soaks green on the amended engine | `artifacts/ghost_tap_makeup_probe.log`; regression logs |
| SC-029 | Library size + primaries | N == **42** (T043 ruling 2026-09-29: the measured default-state set leaves the four noise models to showcase presets; G2 ruling: the two roster-extension cells are secondaries; before it 38 + \|R\| = 40 — Clarifications Q1, Q5, session 2026-09-29 — `R` = {`syncRate`, `selfAffinity`}; plan ruling R-2 / P-3; supersedes Q4's [35, 45] band — default-state cells are ineligible primaries under FR-011, so the floor of unique primaries is 10 S + 12 M + 5 E + 9 non-default materials + D8.2 + D9.1 = 38, plus one preset per roster-extension cell (`E6.hi`, `E7.hi`) = 40; D10.1 is excluded by FR-011b and D8.2/D9.1 are scored by the C-7.4 attack-window reversion); every S, M, E (incl. `E6.hi`, `E7.hi`) cell is some preset's primary; each **non-default-state** member of the 11 body materials, 4 noise models and D8/D9 envelope extremes is some preset's primary (R-3 / P-5; the default-state members are verified secondaries of factory presets, never primaries) | grep/count in compliance record + coverage matrix; `Vorago_FactoryPresets_LibraryShape` (SC-031) (FR-004, FR-011, FR-011b, FR-013) |
| SC-030 | Tail samples | `getTailSamples()` == `llround((Rel + RT60_eff + G) · sampleRate)` within ±1 sample, `G` = `AtmosphereEngine::kMaxGrainSeconds` = 30 s (`atmosphere_engine.h:311`), from independently decoded state, for FR-060's named set (min decay; max decay with Depth = Age = 1; default surface; every factory preset); `kInfiniteTail` when Space Freeze is On | `Vorago_Processor_GetTailSamplesMatchesState` (FR-060) |
| SC-031 | Library shape | every C-1 category holds ≥ 3 presets (FR-004); N == **40** (SC-029; the Q6 N floor of 21 is met a fortiori) (FR-001, FR-004, Clarification Q6) | `Vorago_FactoryPresets_LibraryShape` (per-push; its own test case per plan ruling R-7(ii)) |
| SC-032 | Install path | `krate_plugin_install_presets(${PLUGIN_NAME})` (`plugins/vorago/CMakeLists.txt:123`) resolves to `%PROGRAMDATA%/Krate Audio/Vorago` = `Platform::getFactoryPresetDirectory("Vorago")` (`preset_paths.h:27`); `setup.iss:66-68` and `installers/linux/README.txt:29-44` name the same destinations (FR-026, FR-027) | grep/inspection in compliance record, citing each file:line |

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

## Open Questions (deferred to this spec by the roadmap or by earlier specs — all now resolved)

All eleven items below are resolved: eight in the Clarifications session of 2026-09-27, and OQ-9, OQ-10
and OQ-11 (opened by the second specify pass) in the Clarifications session of 2026-09-29 (see
**## Clarifications**). The resolution is recorded here and carried by the FR/SC ids cited.

- **OQ-1 — Category names** — **RESOLVED (Clarifications Q6, 2026-09-27).** Ratified as proposed: Drones · Abyss ·
  Caverns · Organisms · Machines · Textures · Ghosts, `Drones` first and verbatim, permanent once shipped.
  Carried by FR-001, FR-002.
- **OQ-2 — Distinctness thresholds** — **RESOLVED (Clarifications Q3, 2026-09-27; numbers re-affirmed and
  extended by Clarifications Q2–Q4, Q6, 2026-09-29).** Pilot-calibrated, not ratified from
  arithmetic alone: F = 4.0 is a **floor**, not a fixed value, but stays exactly 4.0 through both
  sessions. FR-017a requires a 6–8 preset pilot
  render measuring `t_K` (the K-fold seed-twin term) and a near-variant pair's `d` (a floor check only)
  before K is ruled and F is frozen. Every other value here is fixed as originally ruled: self-distance
  factor 2 (over the
  three hold minutes), the `2·t_max` measured-take term, seed-twin and gain-twin margin F/2, sub-twin
  bound F, level-twin bound 0.05, band clamp −60 dB, primary bar = F, secondary bar D_abl = 1.5,
  Freeze-On tail floor X = 20 dB, parameter-space K = 8 at Δ ≥ 0.10 (C-7.1's parameter-space K, distinct
  from the sound-space take-count K of OQ-10). Carried by FR-017a and every F-based
  FR/SC (FR-011, FR-015, SC-008, SC-010, SC-011).
- **OQ-3 — `getTailSamples()`** — **RESOLVED (Clarifications Q7, 2026-09-27).** State-derived tail:
  `Rel + effective-RT60 + ghost-grain ceiling`, `kInfiniteTail` under freeze. Carried by FR-060, SC-030.
- **OQ-4 — Ghost density vs trigger** (Phase 10a Q1 → Phase 12/14) — **RESOLVED (Clarifications Q8,
  2026-09-29).** Additive, as shipped: the 0.30 grains/s density scheduler keeps running and triggered
  grains add on top; no new parameter. Carried by FR-061.
- **OQ-5 — Ecosystem behaviour definition** — **RESOLVED (Clarifications Q2, 2026-09-27, scope widened,
  inside Phase 14; roster and E-cell role fixed by Clarifications Q1 and Q5, 2026-09-29).** The five
  kind→destination routes (E1–E5) stay; the ratified roster `R` = {`syncRate`, `selfAffinity`} (OQ-9)
  becomes registered parameters — new IDs, state v3 with v2 load compatibility, ecosystem-page
  controls, and coverage-matrix cells `E6.hi`/`E7.hi`, both showcase primaries (OQ-11). Carried by C-2.3,
  FR-070…FR-076, SC-025…SC-028.
- **OQ-6 — "Showcases nothing another preset already shows"** (C-2.2) — **RESOLVED (ratified as
  recommended).** Unique, non-default, primary-bar primary **and** the non-subset rule on verified
  sets (FR-011a) **and** each E*k* (including the ratified `E6.hi`/`E7.hi`) a distinct preset's primary.
- **OQ-7 — Release version** — **RESOLVED (Clarifications Q8, 2026-09-27).** `1.0.0` — the public release with the
  factory library; the controller interface set is frozen from this version. Carried by FR-064.
- **OQ-8 — How factory freeze is played** — **RESOLVED (Clarifications Q1, 2026-09-27).** D10.1 is verified by the
  **freeze-gesture** render: the preset stores Space Freeze Off; the harness delivers `kSpaceFreezeId` →
  On at `A + 65 s` through `IParameterChanges`; the preset's `Comment` tells the player to engage Freeze
  once the drone has bloomed. No processor load-behaviour change. Carried by C-6 arm 4, FR-033, SC-024.
- **OQ-9 — The roster `R` (Q2's G1 ratification, on 13b's evidence)** — **RESOLVED (Clarifications Q1,
  2026-09-29 — option (a)).** `R` = {`syncRate`, `selfAffinity`}, exactly the counted set: two new IDs
  in the 900 block, `kStateV3Bytes = 436`, two ecosystem-page controls, cells `E6.hi` and `E7.hi`. Fewer
  than Q2's "roughly 4–6", which 13b's Gate 2 already recorded UNMET by ruling — accepted, because this
  is the only roster measured audible on the shipped tree. **Sub-question (d) — the E-ext halfway
  margin** — **RESOLVED (Clarifications Q7, 2026-09-29 — option (a)).** Ratified at 0.5: an `E{n}.hi`
  claim requires decoded `n ≥ n₀ + 0.5·(1 − n₀)` (`.lo` symmetric, `n ≤ n₀ − 0.5·n₀`), `.lo`/`.hi`
  mutually exclusive. Concretely `syncRate` stored ≥ 0.25, `selfAffinity` stored ≥ +0.5. Carried by
  FR-070, FR-071, FR-071a, FR-072…FR-075, SC-025, SC-026, SC-026a, SC-027, SC-028, SC-029.
- **OQ-10 — The distinctness threshold on the post-13b surface** — **RESOLVED (Clarifications Q2, Q3,
  Q4, Q6, 2026-09-29 — option (a) of Q2).** Measured: the default surface's seed
  twin is d = 5.9515 (Life max 6.8514); first-pass control (b) requires ≤ F/2 = 2.0 and the floor's
  `2·t_max` term would otherwise put every pair at ≥ 11.9 (C-7.3 "Post-13b measurement"). Resolution:
  a **seed-marginal descriptor** — `D(P)` becomes the mean of the three-minute descriptors over **K
  takes** (the stored seed plus K − 1 fixed seed-index offsets from it, two disjoint sets of
  `(stored + j) mod 16`, hard-capped at K = 8 — Clarification Q3, option (a)); the take term
  `t_max` (the largest `t_K` across the control set) is measured between two disjoint K-take means, so it
  shrinks with K while F = 4.0, the factor 2,
  F/2 and every other first-pass number stay unchanged. K is ruled from the pilot as the smallest K with
  `2·t_K ≤ F`; if `K = 8` does not reach it, FR-017 stops and surfaces the measured `t_K` curve — never a
  silent fall-back to raising F or dropping the take term. `s(P)` is the largest pairwise `d` among the
  K-take-averaged per-minute descriptors; the C-6 long-render arms 1–4 run on every take; the ablation
  and E twins stay single-take at the stored seed, scored against that take's `M1` (Clarification Q4,
  option (a)). The pilot's near-variant pair is a floor check only (`d < F` required; `d ≥ F` is itself
  an FR-017 stop) and never sets F (Clarification Q6, option (a)). Budget via C-10's sharding lever,
  never by shortening. Carried by C-7.2, C-7.3, C-7.4, FR-011, FR-015, FR-017a, FR-036, SC-008, SC-010,
  SC-011, SC-013.
- **OQ-11 — Role of the rule-knob E cells** — **RESOLVED (Clarifications Q5, 2026-09-29 — option (a)).**
  `E6.hi` and `E7.hi` are **primaries**, as plan ruling R-2 required: a showcase preset must author a
  surface on which the knob's reversion moves the preset by ≥ `max(F, 2·s(P))`; the pilot (FR-017a)
  includes one candidate per knob and FR-017 stops if either bar is unreachable. This fixes
  **N = 38 + |R| = 40** (superseded at gate G2, 2026-09-29: secondaries, **N = 38**). Carried by C-2.2, C-2.3, FR-011, FR-013, FR-017a, FR-075, SC-008, SC-028,
  SC-029.

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
- **R-7 — structure accepted** (coverage fix, Clarifications session 2026-09-29: citation corrected —
  FR-035 and SC-004 were wrong citations). (i) CMake registration is split: the probe TU is registered
  before G1, every other new TU and the generator targets in one mid-phase task, and the final group only
  audits. (ii) The library-size and ≥ 3-per-category checks live in their own
  `Vorago_FactoryPresets_LibraryShape` case so it is the only red while the library is partly authored.
  [FR-027a, FR-004, SC-031]
- **R-8 — harness-first descriptor, no duplication.** The C-7.2 descriptor and C-7.3 distance land in
  `preset_test_support.h` before the probe TU exists, and the probe includes them from there; the probe
  ranking and the sweep use one implementation. [FR-070, FR-036, C-7.2]

### Second specify pass (2026-09-29) — status of the first-pass rulings

Re-specified against the post-13b tree (HEAD `59fbd9e6`). Every ruling above stands except where noted:

- **Q1, Q5, Q6, Q7, Q8; R-1, R-3, R-4, R-5, R-6, R-7 [FR-027a, FR-004, SC-031], R-8** — unchanged
  (R-7's citation corrected, coverage fix, Clarifications session 2026-09-29). R-1's append-only `dsp/` wiring
  (forwarder + `VoragoVoiceParams` field + `applyVoiceParams` line per knob) now targets
  `vorago_engine.h:160`, `:188`, `:859` and the post-13b `vorago_voice.h`; the inert probe friends it
  added are at `vorago_voice.h:171`, `:1568` and `vorago_engine.h:1304`. R-8's descriptor is shipped
  (`preset_test_support.h:40-167`) and was the instrument of 13b's tables.
- **Q2** — stands (rule knobs become registered parameters, state v3, ecosystem-page controls). Its
  *probe* half is discharged by Phase 13b's FR-013 tables (FR-070); its *ratification* half, OQ-9, is
  **now resolved** (Clarifications Q1, session 2026-09-29): `R` = {`syncRate`, `selfAffinity`}.
- **Q3** — stands as a procedure (pilot, then freeze); its numbers, re-opened by OQ-10 when the 13b
  tables measured the seed twin at 5.9515, are **now resolved** (Clarifications Q2–Q4, Q6, session
  2026-09-29): the seed-marginal K-take descriptor, F = 4.0 unchanged.
- **Q4 / R-2** — N is 38 + |E-ext primaries|; **now resolved** (Clarifications Q1, Q5, session
  2026-09-29): |E-ext| = 2, both cells primaries (OQ-11), so **N = 40** — then gate G2 (2026-09-29) made them secondaries, **N = 38**.
- **First-pass gate G1** — its "roster not ratified" ruling is superseded by OQ-9's resolution; its
  "E-route risk: keep E cells as primaries, FR-017 catches an unverifiable route" ruling stands and
  is now expected to be exercised with measurable effects (the ecosystem on/off distance is 5.6272 at the
  default surface, finding 5).

### Session 2026-09-29 (second pass clarifications — resolving OQ-9, OQ-10, OQ-11)

The user was interviewed on the second pass's three open items (OQ-9, OQ-10, OQ-11) plus their internal
sub-questions. All eight questions are resolved; the former **## Open Clarifications** section (which
posed them) is removed — its content is superseded by this log and by the FR/SC text it updated.

- **Q1 — Which ecosystem rule knobs form the ratified roster `R` (OQ-9)?** Option (a): `R` =
  {`syncRate`, `selfAffinity`} — exactly the counted set. Two new IDs in the 900 block,
  `kStateV3Bytes = 436`, cells `E6.hi` and `E7.hi`. `kernelSigma` and `moveRate` are **not** in `R`.
  [FR-070, FR-071, FR-071a, FR-072, FR-073, FR-074, FR-075, FR-076, FR-006, FR-010, FR-013, SC-025,
  SC-026, SC-026a, SC-027, SC-028, SC-029, SC-031]
- **Q2 — How is the distinctness threshold re-ruled given the default surface's seed twin at
  d = 5.9515 (OQ-10)?** Option (a): a **seed-marginal descriptor** — `D(P)` is the mean over K takes;
  F = 4.0 and every first-pass margin (factor 2, F/2, 0.05 level bound) stay; K is ruled at the pilot as
  the smallest K with `2·t_K ≤ F`; the main render costs K× and is absorbed by C-10's sharding, never by
  shortening. [C-7.2, C-7.3, FR-015, FR-017a, FR-036, SC-010, SC-013]
- **Q3 — How are the K takes and `t_K` built, with only 16 seed indices?** Option (a): two **disjoint**
  take sets from the 16 seed indices, seeds `(stored + j) mod 16`, hard-capped at **K ≤ 8**. If K = 8
  still fails `2·t_8 ≤ F`, FR-017 stops and surfaces the measured `t_K` curve. [C-7.3, FR-017, FR-017a,
  SC-010]
- **Q4 — Which take(s) feed `s(P)`, the long-render arms and the ablation/E twins?** Option (a): `s(P)`
  is taken from the K-take-averaged per-minute descriptors; the C-6 long-render arms 1–4 run on every
  take; the ablation and E twins stay single-take at the stored seed, scored against that take's `M1`.
  [C-6, C-7.3, C-7.4, FR-011, FR-017a, SC-011, SC-012]
- **Q5 — Must each rule-knob E cell (`E6.hi`, `E7.hi`) be the primary of its own showcase preset
  (OQ-11)?** Option (a): primaries, as plan ruling R-2 required — one pilot candidate per knob; FR-017
  stops if the bar is unreachable. N = 38 + |R| = **40** (the stop happened at G2: secondaries, N = 38). [C-2.2, C-2.3, FR-011, FR-013, FR-017a,
  FR-075, SC-008, SC-028, SC-029, SC-031]
- **Q6 — In the FR-017a pilot, what does the near-variant pair's `d` decide?** Option (a): a **floor
  check only** — the pair MUST score `d < F`; scoring `d ≥ F` is itself an FR-017 stop (the descriptor
  cannot tell near-variants apart), and the pair never sets F. [FR-017a, C-7.3]
- **Q7 — Is the E-ext side-predicate margin ratified at 0.5 (OQ-9 sub-question (d))?** Option (a):
  ratify 0.5 — `syncRate` stored ≥ 0.25, `selfAffinity` stored ≥ +0.5; `.lo` and `.hi` stay mutually
  exclusive. [FR-075]
- **Q8 — How do ghost density and event triggers interact in the factory presets (OQ-4)?** Option (a):
  additive, as shipped — the 0.30 grains/s density scheduler keeps running and triggered grains add on
  top; no new parameter. [FR-061]
- **R7-coverage — Coverage fix for plan ruling R-7's citation (structure accepted; ruled 2026-09-27,
  unchanged).** Mint **FR-027a — Harness registration staging** under the "Validation harness" heading,
  carrying R-7 clause (i) verbatim: the probe TU is registered in `plugins/vorago/tests/CMakeLists.txt`
  before gate G1; every other new test TU and the generator targets are registered in one mid-phase task;
  the final task group only audits the registration (no new registrations). Clause (ii) — the
  library-size and ≥ 3-per-category checks live in their own `Vorago_FactoryPresets_LibraryShape` case so
  it is the only red while the library is partly authored — is carried by SC-031 (already states it) and
  FR-004. R-7's citation is corrected to [FR-027a, FR-004, SC-031] (FR-035 and SC-004 were wrong
  citations). [FR-027a, FR-004, SC-031]

---

### Plan stage (2026-09-29) — plan §12 rulings and task confirmations

Ruled by the user 2026-09-29 on the second-pass plan (plan §12 items 1–6) and the tasks agent's
confirmations. Every item took the plan's default unless stated.

- **P2-2 — take sets acknowledged.** `A_K = {(s+j) mod 16 : j < K}` feeds `D(P)`; `B_K = {(s+K+j) mod 16 :
  j < K}` feeds `t_K`; each set holds K takes, so K ≤ 8. [FR-015, FR-017a, SC-008, SC-010]
- **P2-3 — controls (a), (a′), (c) are same-seed single-take comparisons.** Master gain and the sub
  offset act downstream of every voice, so their take term is exactly zero; no K-take twins. [FR-017a,
  SC-011]
- **P2-4 — the `VoragoEcosystemRosterProbe` friend is DROPPED.** SC-026a reads through the existing
  public const `Processor::engineForTest()` (`processor.h:116-118`); every read it makes is const. No
  new friend in `processor.h`; the New-components row is withdrawn. [SC-026a, FR-071a]
- **P2-1 — E0 runs before stage B.** The default surface's take curve over all 16 seeds (hard-coded
  timeline, no decode/defs/tail estimate) is measured first, so a predicted G2 stop (K = 8 extrapolates
  to 4.21 / 4.84 against F = 4.0) surfaces after ≈ 9 min of rendering, not after the harness is built.
  [FR-017, FR-017a, SC-008]
- **§6.11 skip rules acknowledged.** A render is skipped only where its verdict is known exactly without
  it (override equal to the stored value; an M cell below its displacement threshold; E1–E5 with 900
  stored at 0; a D cell with a false state predicate; an E-ext cell with a false side predicate); the
  skip reason is recorded in the record; the raw `d` of the last four kinds is not rendered. FR-037's
  "recorded regardless" reads as "the verdict is recorded regardless". [FR-037, SC-013]
- **SC-011's envelope clause** applies to the envelope **primaries** (D8.2, D9.1: attack-window
  reversion); the D8/D9 **secondaries** are always-audible state cells (their default-state reversion
  is the preset itself, d = 0). [SC-011]
- **Task confirmations:** (i) FR-027a's single mid-phase registration is T003 (ten skeleton TUs, the
  generator targets, the fast-math exemptions, `${CMAKE_SOURCE_DIR}/tools` on the `vorago_tests` include
  path per the Seraphis precedent `plugins/seraphis/tests/CMakeLists.txt:109`), T060 only audits;
  (ii) ruling R-6 (`run-cpu-tests.js` FILTER gains `~[vorago-sweep]`) moves to T002, before any sweep
  case exists; (iii) the tasks agent's added per-push cases stay (`Vorago_PresetHost_DriveContract`,
  the `Vorago_PresetSupport_*` mechanics cases, `Vorago_PresetDefs_DStatePredicates` /
  `RequiredPrimaries` / `InfoXmlBytes`, `Vorago_ControllerState_V3AndV2`,
  `Vorago_State_V2LoadsWithRosterDefaults_Engine`); (iv) E0's reproduction check of 13b's `t0on`
  (5.9515) uses tolerance 0.0015. [FR-027a, FR-074, SC-008]
- **Push / CI-dispatch tasks** (T048 AppleClang tree tolerance, T057 runner measurements, T063 auval on
  the release commit) stay **pending** until the user grants a push at that point; nothing is pushed
  without an explicit ruling.

### Gate G2 rulings (2026-09-29, after the first pilot run)

The first pilot (T039, `artifacts/pilot_calibrate.log`, compliance "FR-017a pilot / G2") ruled **K = 4**
and stopped on three fronts. Measured before ruling (compliance "Main-loop probes before the G2 ruling"):
the colony-knob reversions score 0.85 (E6.hi on Locked Choir) and 0.15 (E7.hi on Clotting Colony) on the
three-minute descriptor, 2.14 at best (the plain Life-max surface, reproducing 13b through the parameter
path); the colony-forward surfaces cut the whole colony's on/off effect to 1.2-2.2 (default surface 5.6).

- **G2-1 — E6.hi and E7.hi are secondaries; N = 38.** FR-017 is accepted for both cells as primaries: on
  every measured surface the counted knob moves the preset by under F = 4.0. The knobs stay in the roster
  and ship (state v3, the two controls, SC-025…SC-027 unchanged); each cell verifies as a **secondary**
  (bar 1.5, ExtReversion, Q7 side margin) on an Organisms row that stores the knob at or above the Q7
  margin on a Life-high surface (the surface that measured 2.14). The two colony-knob showcase rows leave
  the library: **N = 38**; SC-029's derived count is 38; `requiredPrimaryCells()` excludes the two cells.
  [C-2.2, C-2.3, FR-011, FR-013, FR-017, FR-017a, FR-075, SC-008, SC-028, SC-029, SC-031]
- **G2-2 — Q6 near-variant: measure a nearer pair first.** The first pair (sub +6 dB, smear +0.05) scored
  4.81 ≥ F on a preset whose own s(P) is 4.95. The pilot now authors the near-variant at **sub +2 dB
  (600 stored + 1/24 normalized) and smear +0.02**; the floor rule stays `d(P3, P3′) < F`. If the nearer
  pair still scores at or above F, the reading is trajectory divergence, not the +2 dB, and the case
  returns to the user with the measurement. [C-7.3, FR-017a, SC-010]
- **G2-3 — Twin bars are the fixed floors: primary F = 4.0, secondary 1.5.** The first-pass
  `max(D, 2·s(P))` term is withdrawn from every same-seed, same-window twin comparison (S/M ablation,
  reversion-D, ExtReversion, route arms and the attack window's D term): a twin shares P's seed and
  window, so P's minute-to-minute evolution s(P) and its reseed distance t_1 are common to both sides
  and are not that comparison's noise. Pilot evidence: real effects (S5 6.9, D1.1 3.5, the Growth attack
  4.4) failed bars of 8.7-9.9 set by s(P) alone. `s(P)` and `t_1(P)` stay **recorded** per preset
  (record field `selfDistance`, cell field `twoS`); the pairwise distinctness floor (C-7.3, D(P) vs
  D(Q) across seeds) keeps its take and s terms unchanged. A ruled correction: the main loop first
  recommended `max(F, 2·t_1(P))` and withdrew it in the same session, t_1 being the reseed distance a
  same-seed twin does not suffer. [C-7.4, FR-011, FR-017a, SC-011]
- **G2-4 — Proceed.** The pilot set is P0 plus Tectonic Floor (S5, near-variant host), Cathedral Void
  (S8), Growth Ring (D8.2) and Glass Well (D1.1); the main loop re-runs the pilot under these rulings,
  re-authors any pilot still under its bar (C-2.2 route: Growth Ring's attack conjunct, Glass Well's
  D1.1 at 3.5), and continues into the library when every pilot primary verifies and the near-variant
  floor holds; any new stop returns to the user. [FR-017a]

### T043 ruling (2026-09-29): the measured default-state set, N = 42

T043 measured the default surface's own verification vector (`artifacts/default_state_vector.log`,
`Vorago_PresetSweep_AblationVerifiesClaims` on the pseudo-preset, shard 4/5, 474 s). The default surface
verifies **9** D cells at the secondary bar (D1.6 StoneChamber, D1.7 SteelTank, D2, D5.3, D7.1, D8.1,
D9.2, D10.2, D12.2). P2-6's prediction also listed D3.1–D3.4, D4.6 and D6.1: they fail because their
conjuncts do not register there — the S1 noise-organism ablation moves the default surface by **0.0110**
and the S4 noise filter by **0.0009** (bar 1.5): the noise bed is inaudible on the default surface (noise
level −18 dB), consistent with 13b. The derivation of plan §6.12 therefore adds the four noise models to
the required primaries: **N = 42** (27 S/M/E1–E5 + 9 non-default materials + D8.2 + D9.1 + D3.1–D3.4).
**Ruled: accept N = 42** — one showcase preset per noise model (Direct, FilteredWind, GranularDust,
MetallicHiss), each making the noise section audible (its S1 conjunct verified), which is the variety
mandate applied to the noise section; D4.6 and D6.1 are then verified as secondaries by presets whose
noise is audible. `kRecordedDefaultStateCells` is the measured 9-cell set. [C-2.1, C-2.2, FR-004,
FR-011, FR-013, FR-016, SC-008, SC-029, SC-031]

### Sweep rulings (2026-09-30, after the first full local sweep)

The first full sweep (compliance "T048 — first full local sweep") verified 17 of 42 primaries and put
313 of 861 pairs under the C-7.3 floor. Three findings were measured (main-loop probes,
`artifacts/sweep_diag_*.log`, `VoragoVoice_NoiseBusProbe`) and ruled:

- **S-1 — Noise-bus make-up gain (a DSP amendment beyond R-1's append-only setters).** The noise
  organism calibrates every model to a −50.6 dBFS slot reference, so at its +12 dB maximum with the
  wake base at 1.0 the bed sat ≈ 33 dB under the drone at the voice output (difference signal
  −52.7 dBFS against a −19.7 dBFS drone; the plugin-level descriptor did not move). No preset could
  showcase S1, D3.1–D3.4, D4.1–D4.12 or E3. Ruled: one constant make-up gain on the organism's bus in
  `VoragoVoice` (`kNoiseBusMakeupDb`), **sized by measurement**: at +30 dB the +12 dB bed reads
  −25.1 dBFS against the drone's −19.7 (output RMS −22.8 dBFS), i.e. a foreground bed at the maximum
  and a faint one (≈ −55 dBFS) at the −18 dB default. Every Phase 2–13 suite and 13b's fingerprint are
  re-run on the amended voice (FR-077). [FR-077, SC-033; C-2.1 S1/D3/D4/E3]
- **S-2 — Cross-preset floor = max(F, 2·t_max).** The `2·max(s(P), s(Q))` term is recorded, not
  gated: D(P) and D(Q) are K-take, three-minute means whose noise is the take term (t_max 1.49), not
  either preset's own evolution — the reading gate G2 gave the twin bars. The first sweep read floors
  up to 13.7 from that term; 216 of its 313 failing pairs were above 4.0. [C-7.3, FR-015, SC-010]
- **S-3 — Twins are scored on the three sustain minutes.** `P_Sus` = the mean of the stored take's
  M1…M3 descriptors and every twin (S/M ablation, reversion-D, ExtReversion, the route arms and the
  attack window's sustain term) is captured over M1…M3 and scored on the mean of its three minute
  descriptors; twin renders run to the end of M3 (340 s). The same twin read 2–5× larger over three
  minutes (E6.hi 0.15 → 0.85; the Life macro's full travel 1.68), which is where the slow capabilities
  (M2, M3, M4, M9, M10, the bloom and ghost routes, D14) sit. Cost ≈ +55 % per sweep. [C-7.4, FR-011,
  FR-017a, SC-011]

### Sweep 2 rulings (2026-09-30, after the second full local sweep)

Sweep 2 (`artifacts/sweep2_shard_{0..3}.log`, `sweep2_aggregate.log`, `sweep2_wall.txt`: 08:05–12:56,
4 h 38 min shards + 13 min aggregate) verified **14 of 42** primaries (sweep 1: 17 — S4, M5, M8 and M12
dropped under the M1…M3 scoring, S1 gained) and put **99** of 861 pairs under the 4.0 floor (sweep 1: 313).
Stone Gravity still sits at the limiter (peak 0.966, hi −5.94 dB, late-sustain +14.3 dB). Three items were
measured and ruled:

- **S-4 — D3 primary rule.** Plan §6.12 makes every non-D1 `StateWithS` cell **false at Primary** and
  defers the noise-model cells to a ruling after the "Required primaries" stop; the T043 ruling added
  D3.1–D3.4 as required primaries without that ruling, so no noise-model preset could verify in either
  sweep ("state-only kind, pri NO"). Ruled: **a D3.m primary = the state predicate ∧ the preset's own S1
  ablation d ≥ F (4.0)** — the showcase is the audible noise organism. The harness copies the S1 conjunct's
  `rendered` / `d` into the D3 cell in pass 3, `verifiedAt` admits D3.x at Primary on those terms, and the
  secondary verdict is unchanged (no d term). Measured before ruling with the pilot probe's new
  `VORAGO_PILOT_OVERRIDE` (`artifacts/sweep2_noise_probe_*.log`, +30 dB make-up): S1 d as authored —
  Wind Through Basalt 5.35, Steam Vent 3.66, Abyssal Wind 2.31, Spore Drift 3.98, Dead Air 0.12; at
  +6 dB / wake 1.0 (the +24 dB maximum) — 6.39 / 5.67 / 4.59 / 2.98 / 0.46; Dead Air at +12 dB / wake
  1.0 — 1.06 (3.10 with one FilteredWind slot: its Velvet / VinylCrackle / Blue types are near-silent as a
  bed). [FR-011, FR-013, SC-029; plan §6.12]
- **S-5 — The make-up stays at +30 dB.** S-1's condition for dropping to +24 dB ("clears the bar with
  room") is not met: at the +24 dB maximum two of the four noise-model presets sit under 4.0 (Spore Drift
  2.98, Dead Air 0.46) and Wind Through Basalt saturates at 6.4 either way. [FR-077]
- **S-6 — The reset defect behind the slot-seed sentinel is fixed, not bounded.**
  `VoragoEngine_SlotSeedReproducibility` read 2.65e-4 against 2.5e-4 on the amended voice. Bisected with
  hidden probes (`artifacts/reset_*.log`): the divergence sits on the noise bus (noise at −96 dB: 4.07e-5
  → 1.52e-6; the engine's 2048-sample smear latency is why it first shows at sample 2049), in the
  **MetallicHiss** model alone (all-Direct / all-FilteredWind / all-GranularDust voices replay bit-exactly
  after reset, all-MetallicHiss diverges from sample 0), while the organism alone replays bit-exactly at
  8 / 16 / 44.1 / 48 kHz and under per-step level and wake modulation. The state is `NoiseGenerator`'s
  **per-type level smoothers**, which its `reset()` leaves alone: `prepare()`'s warm-up settles only the
  types active at prepare (Brown on every slot), so a type first enabled afterwards (MetallicHiss → Blue)
  fades in over ~5 ms on an instance that has never rendered since the switch and starts settled on one
  that has — the sentinel's prime arm is exactly the never-rendered case. Fix:
  `NoiseGenerator::snapLevelSmoothers()`, called by `NoiseOrganism::applySlotConfiguration` after its
  level pushes (click-free: a type change reaches it through the duck at gate 0; every other caller
  re-pushes the same targets). Regression `NoiseOrganism_TypeSwitchedAfterPrepareReplaysAfterReset`
  (bit-identity at 8 kHz; before the fix 0.0121 against a 1.28 peak from sample 0). After the fix every
  engine-probe variant reads 0.000e+00 and the sentinel passes. This is the phase's second DSP change
  beyond R-1 (FR-077a, SC-033a). [FR-077a, SC-033a, SC-033]

### Re-author loop rulings (2026-09-30, after probe batches 1–6)

Six probe batches (`artifacts/reauthor_probe_batch{1..6}.log`, compliance "Re-author loop after sweep 2")
measured every failing primary on its own levers. Twenty-six of 42 primaries pass by probe after the adopted
re-authoring; the rest split into cells still moving under levers and cells whose feature's audible range
sits under F on any preset setting. Ruled:

- **S-7 — Ghost tap: measure a make-up, then rule.** `VoragoEngine_GhostLevelProbe` read the ghost tap alone
  at −33 dBFS in its loudest second against a −19 dBFS drone at peak level 1.0 (S9 Choir of Absence 0.22,
  E5 Haunted Colony 0.42). The engine gains `setGhostTapMakeupDb` / `kGhostTapMakeupDb` (a constant gain on
  the atmosphere's WET texture at the bus sum, the S-1 shape) so +6 / +12 / +18 dB are measured at the
  engine and, for the candidate, through the pilot probe on both presets; the value is ruled on those
  numbers. **Measured and ruled (17:25): +12 dB ships** (`kGhostTapMakeupDb` = 12, `kGhostTapMakeupGain` =
  10^(12/20)): at the engine the tap's loudest second sits at the drone's level (−20.8 vs −19.2 dBFS) and it
  sounds in every second of the sustain (0 dB: 14 dB under, half the seconds; +18 dB: above the drone, mix
  +3 dB); at the preset level S9 Choir of Absence 0.22 → 1.83 and E5 Haunted Colony 0.42 → 1.19 — the
  secondary bar for S9, not F; both recorded UNMET under S-8. The third DSP change beyond R-1; the seventh
  fingerprint harvest and the Phase 10a / 13b suites re-run on it. [FR-077b, SC-033b]
- **S-8 — Ecosystem- and bloom-limited primaries are recorded UNMET at their measured ceilings.** E1 partial
  → bloom 0.06 (route arm; attribBase 0.06), E3 noise wake 0.65, E4 loop wake 1.22, M10 Life 0.63 (Life 1.0,
  ecosystem base 0), S6 harmonic bloom 1.74 (richness 0.7; the bloom's six children scale with sounding
  parents and the descriptor tops out there), and S7 ecosystem 3.90 (wake base 0.1, sync / affinity 1.0).
  A single route cannot exceed the ecosystem's audibility as ruled in 13b (Gate 1 = half an off-reseed), and
  the bloom's ceiling is its own. The presets keep their claims; the compliance table carries each cell's
  best d and the lever it was measured with; **no bar changes and N stays 42**; S7 keeps authoring. The
  13b Gate-2 precedent (UNMET by ruling, with the numbers). [FR-011, FR-013, SC-029]
- **S-9 — The attack window is the AUDIBLE attack.** C-6's attack span `A` is the stage-time sum (155 s for
  the registered envelope), but the registered envelope (stages 20 / 30 / 45 / 60 s, levels 1.0 / 0.8 /
  0.92 / 0.85) is at full level after its 20 s stage 0: the [0, 160 s] attack window compared 150 s of
  near-identical sustain (Sudden Chasm d_att 1.25 against an attributability floor of 4.83; P reaches
  RMS(Sus) − 6 dB at 4 s, P_rev at 6 s). Ruled: plan 6.8's `A_P` / `A_rev` are the audible attack —
  Standard: stage 0's time; Growth: the growth duration (`audibleAttackSeconds`) — so `W_end` =
  max(A_P, A_rev) + 5 s and `Sus_rev` = [A_rev + 5, A_rev + 65] read from the audible attack. C-6's sum
  stays the timeline's `A` (Sus placement, the D9 state predicate). D8.2 Growth Ring is re-probed on the
  same rule. No bar changes. [C-7.4, FR-011, SC-011; plan 6.8]
- **S-10 — One more probe round on the nine primaries still moving under levers** (D1.11 3.98, M3 3.46,
  M12 3.46, M5 3.5, D1.8 2.18, M2 2.45, M4 2.38, S4 1.79, M9 1.70), adopt what passes, regenerate the 42
  presets, then sweep 3 as the confirming run; whatever is still under F is recorded with its best reading.

## Review notes (spec challenge, second pass, 2026-09-29)

All eighteen issues were applied; none was rejected. Where an issue offered alternatives:

- **D8.2 / D9.1 envelope primaries (testability, major).** The attack-window reversion (C-7.4) was
  adopted rather than demoting the cells to secondaries: Clarification Q4 names the envelope extremes
  as primaries, and demoting them would lower N to 36 + |E-ext| against a user ruling. The added
  attributability conjunct (`d_att ≥ d_Sus + D_abl`) answers the "measures chaos, not the envelope"
  concern directly.
- **Freeze dry residue (testability, major).** The `kSpaceMixId` → 0 twin was chosen over
  `kGhostPeakLevelId` → 0, because it removes the whole cavern and so bounds every dry contributor
  (ghost, smear, sub), not only the ghost.
- **`getTailSamples()` vs the Freeze-Off `Tail` start (testability, major).** The tail window was not
  moved; the spec now states why the two do not conflict (the reported tail is a truncation ceiling, the
  arm asserts the output has already decayed). Moving the window by `G` would have added 25 s to every
  render without changing what is asserted.
- **Sub twin out of range (minor).** The in-range-side-only option was taken over a 12 dB substitute
  shift, so the control's ruled ±6 dB magnitude never changes.
- **D1 blend weight (major).** The blend-weight conjunct (≥ 0.35) was taken; the per-material reversion
  remains how D1 *primaries* are scored.
- **E-ext halfway margin (major).** Carried both ways: as FR-075's side predicate and as OQ-9
  sub-question (d), so the G1 ratification rules it explicitly.
