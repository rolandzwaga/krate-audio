# Implementation Plan: Profundum Phase 13 — Factory Presets & Release Readiness

**Spec:** `specs/profundum-phase13-presets-release/spec.md` (reviewed; Clarifications session 2026-10-24, Q1–Q7)
**Roadmap:** `specs/Profundum-roadmap.md` Part B, Phase 13 (`:1052-1069`) and the Phase 12 handoff 13.1–13.5 (`:1043-1050`);
cross-cutting constraints `:1089-1112`
**Branch:** `synth/profundum` (one branch per roadmap), HEAD `e6095b24` when this plan was written
**Date:** 2026-10-24
**Status:** PLAN. Nothing below is implemented. Nine plan-stage rulings (P-1 … P-7, P-10, P-11; §2) need the user's
confirmation before the first task that depends on each; §13 lists them. Six of them narrow or amend ratified FR/SC text.
Each amendment (§2.0) is written into its spec row before the gate that depends on it. Every file:line cited below was read
in this planning session.

---

## 0. Summary

Phase 13 creates **no DSP class and no plugin runtime class** (spec "New components"). It adds:

| # | Deliverable | Where | Lane that proves it |
|---|---|---|---|
| 1 | Code move of the Phase 9 audibility readers (FR-057), with the P-1 render seam and the P-10 named-function lifts | `dsp/tests/unit/systems/profundum_audibility_readers.h` (new), `profundum_engine_audibility_test.cpp` (shrinks) | G0 move gate (FR-058) |
| 2 | Catch2-free preset host + the one state drive (C-4) | `plugins/profundum/tests/profundum_preset_host.h` | per push |
| 3 | Data-only definitions, the 53-column `Capability` roster, the library | `tools/profundum_preset_defs.h` | per push |
| 4 | Generator + CMake targets on the `release.yml` contract | `tools/profundum_preset_generator.cpp`, root `CMakeLists.txt` | build + SC-010/011/012 |
| 5 | Six category directories and `makeProfundumPresetConfig()` | `plugins/profundum/src/preset/`, `resources/presets/` | per push |
| 6 | Header preset button + browser overlay + whole-stream load message; the AT-13 engine-seam test hook (P-11) | controller, processor `notify` and `setEngineSeamsForTest`, `plugin_ids.h` message ID, `editor.uidesc` (XSLT), panel sub-controller | per push |
| 7 | Test support: decode, processor script renderer, render-seam adapter, showcase arms, descriptor | `plugins/profundum/tests/preset_test_support.h` (Catch2-free), `preset_render_support.h` (Catch2) | all |
| 8 | Probe, showcase, sweep, mono-sum, true-peak, distinctness, determinism, loudness, load and CPU cases | 7 new TUs + `processor_cpu_test.cpp` | per push / `[long]` / `[.probe]` / `[.perf]` |
| 9 | Audition renders + listening sheet | `specs/.../evidence/listen/` | script |
| 10 | `MANUAL.md`, `docs/index.html`, CHANGELOG bullets, release-skill roster, determinism-script roster, CI shard table | plugin + tools + `.claude/skills/release/SKILL.md` | release-readiness |

The library is **N = 53**: one preset per column (§7), six categories, each holding at least 7.

---

## 1. What was read (this session)

| Area | Files / lines |
|---|---|
| Roadmap | `specs/Profundum-roadmap.md:1-192` (philosophy, architecture, decisions, reuse inventory), `:748-1137` (Part B, Phase 12 status + handoff `:1029-1050`, Phase 13 `:1052-1069`, constraints `:1089-1112`, OQs) |
| Spec | `spec.md` in full (752 lines) |
| Phase 9 table | `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`: a quoted-CSV parse this session found 102 gated cells (per shard: Anchor 20, Character 12, Core 6, Envelope 4, Field 9, Macros 8, Mass 7, Morph 4, Motion 14, NoteMemory 8, Pitch 10). Close-lane wall clocks from `evidence/run-log/close/long/VoiceEngine_AudibilityTable_*.log`: Anchor 59 s, Character 193, Core 109, Envelope 18, Field 88, Macros 40, Mass 58, Morph 31, Motion 884, NoteMemory 10 s. `compliance.md:83` |
| Phase 9 TU | `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`:<br>• `:1-140`: header, includes `:41-86`, `using namespace` `:88`, anonymous namespace `:90`, aliases `:92-99`, constants `:105-139`.<br>• `:146-470`: utilities; bases; `AuSetting` `:217-222`; `auScript` `:261-279`; `auSteadyTotal` `:281-284`; `AuDesc` `:290`; `auSteady` `:338-391`; `auGateRender` `:402-421`; `auDtMs` `:423-443`; `auGateDt` `:445-461`.<br>• `:610-800`: `AuCell` `:613-624`; `auCsv`; `auReport` `:653-666`, which writes through `auWriteEvidence` `:184-190` into the Phase 9 evidence directory `:180-182`; `auSteadyCell` `:668-696`; `auRatio`; `auFloorRule` `:706-716`; `auGateOnEither` / `auGateOnFirst`; `auLkOutShort`; `auRiseMs` `:799`.<br>• `:986-1300`: AT-03, AT-10, AT-26; the Core, Character and Anchor shards.<br>• `:1300-1395`: Pitch; the `dropCell` lambda; the AT-23 `auGlideT50` pair.<br>• `:1494-1550`: Mass; the AT-27 `dlk` and AT-28 `onset` lambdas.<br>• `:1600-1800`: `auFieldMotionCell`, `auMotionCell`, `auCycle`, `auEnvRetriggers`.<br>• `:1951-2034`: `auNoteMemoryPhase` and the NoteMemory shard's value rule.<br>• `:2262-2419`: the second anonymous namespace with `auRunShard` `:2271-2281`, and the 16 TEST_CASEs `:2285-2409`.<br>Render call sites (grep this session): 16 × `auPt::voiceEngineRender(` and 2 × `auPt::voiceEngineRenderWithTwin(` |
| dsp test helpers | `profundum_voice_engine_test_helpers.h:60-200` (notes, `VoiceEngineEventKind`, `VoiceEngineScript` `:139-155`, taps), `:377-470` (`voiceEngineRender`, `voiceEngineTwinScript`, `voiceEngineRenderWithTwin`); `bass_morph_test_helpers.h:703-715` (`readWindow`: 1 s settle + `kLowFrequencyFftSize`, `REQUIRE(start + length <= n)`), `:760-835` (`dCxDb`, `dNlDb`, `dStDb`, `dBrOct`, `lKDb`, `eSubDb`); `bass_stereo_field_test_helpers.h:711-717` (`BandReading`); header locations (`profundum_core_test_helpers.h`, `mass_dynamics_test_helpers.h` in `dsp/tests/unit/systems/`; `enable_ftz_daz.h`, `k_weighting.h` in `tests/test_helpers/`) |
| dsp CMake | `dsp/tests/CMakeLists.txt:790-812` (systems list; the audibility TU is **not** in a `-fno-fast-math` block), `:900-912` (`KRATE_DSP_TESTS_DIR` on `dsp_processors_tests` and `dsp_systems_tests` only) |
| Engine | `dsp/include/krate/dsp/systems/profundum_engine.h:84-114` (`ProfundumEngineConfig`, `kMaxVoices 8`, `kDefaultPolyphony 4`, `kControlInterval 32`, `kLimiterLookaheadSamples 64`, `kDefaultCeilingDb`, `kDefaultBendRangeSt 2`, `kDefaultSeed 0xB455`), setters `:223-339` |
| Plugin | • `plugin_ids.h` (whole file).<br>• `processor/processor.h:18-260`.<br>• `processor/processor.cpp`: `:255-335` (setupProcessing step 3a, setActive); `:375-420` (`process` guards; a parameter-only call returns at `:383-386` before anything is published); `:570-594` (`getTailSamples`); `:596-658` (`setState`); `:661-695` (`getState`, runtime-blob precedence); `:700-745` (`notify`); `:827-893` (two-pass Edit Target routing, route kinds).<br>• `parameters/`: `sound_param_table.h:1-101`; `sound_param_row.h` (`SoundParamRow`, `kDefaultLeafBits`, `plainFromNormalized`, `normalizedFromPlain`, `indexFromNormalized`); `macro_params.h:8`, `:103`; `global_params.h:110-128`, `:182-187`; `note_params.h:219-234`, `:360-369`; `morph_params.h:161`, `:183-194`, `:215`, `:301-306`.<br>• `engine/profundum_engine_config.h` (returns `ProfundumEngineConfig{}`).<br>• `controller/controller.cpp:92-114`, `:280-330`, `:339-357`, `:824-900`; `controller.h:139-148`, `:257-258`, `:305`.<br>• `ui/panel_sub_controller.h:66-86` (13 session tags, base 9000).<br>• `resources/editor.uidesc:124-134` (header children; the span x 436–560 is free).<br>• `preset/profundum_preset_config.h` (whole file).<br>• `CMakeLists.txt:20-51`, `:80`, `:90`, `:128`.<br>• `plugins/profundum/CLAUDE.md` (state table, UI, tests) |
| Plugin tests | • `tests/CMakeLists.txt` (whole file).<br>• `profundum_test_fixture.h`: `:33-80` (includes Catch2 at `:57`); `:262-300`; `:400-420`; `:529-775` (`ProcessorFixture`: `renderScript` splits by host block only, and a parameter takes the last point per block); `:776-807` (`ConnectedPair`); `:1000-1060` (`StateBytes`, `kV2SeedAt = 84`, `memoryStreamOf`, `writeV2Stream`, `loadStateBytes`, `patchU32`, `withEndpoints` `:1065`).<br>• `param_path_audibility_helpers.h:1-112`, `:462-660` (`ppNorm`, `PpArm`, `ppRenderProcessor` `:576-641`).<br>• `integration/mono_sum_params_test.cpp:59-60`, `:93-132` (`MspObserver`, `lastCrossoverHz`), `:330-360`.<br>• `integration/processor_cpu_test.cpp:66-105`, `:240-330`.<br>• `unit/controller/editor_layout_test.cpp` (case list, `:521-550`).<br>• `band_level_metric.h:28-45`, `:160-180` |
| Test helpers | `tests/test_helpers/render_fingerprint.h:55-130` (`kSampleTolerance 5e-4`, `kMetricTolerance 2.5e-4`, `fingerprintRender(span)`, `compareFingerprints`); `low_frequency_metrics.h:405-425` (`measureTruePeakDb`); `allocation_detector.h:111-123` |
| Shared | `preset_manager.h:40-140`; `preset_manager.cpp:90-105` (subcategory = exact parent-dir match), `:153-213` (`loadPreset`: with `processor_ == nullptr` and a load provider, the provider gets a `ReadOnlyBStream` over the `Comp` chunk); `ui/outline_button.h:75-92`; `ui/preset_browser_view.h:50-68`; `platform/preset_paths.h:20-26` |
| Vorago precedent | root `CMakeLists.txt:670-743` (Seraphis tail + Vorago generator block); `plugins/vorago/tests/vorago_preset_host.h` (whole); `tools/vorago_preset_generator.cpp` (whole); `tools/vorago_preset_defs.h:1-120`, `:320-420`, `:1731-1757`; `plugins/vorago/src/controller/controller.cpp:105-185`, `:355-420`; `plugins/vorago/src/ui/panel_sub_controller.cpp:25-50`, `:85-100`, `:120-150`; `plugins/vorago/src/preset/vorago_preset_config.h:30-55`; `plugins/vorago/resources/editor.uidesc:159`; `plugins/vorago/MANUAL.md` headings; `plugins/vorago/docs/` listing; `specs/vorago-phase14-presets-release/plan.md:1-116` (structure) |
| Tooling / CI | `.github/workflows/release.yml:100-186`, `:229-230`; `tools/check-preset-generator-determinism.js:40-70`, `:107-133`; `.claude/skills/release/SKILL.md:10-32`; `.claude/workflows/release-readiness.js:14-24`; `tools/ci-test-shard.js:1-30`, `:106-113` (`kSuiteShards`, `kFilter`); `.github/workflows/long-tests-nightly.yml:80-170`; `tools/run-clang-tidy.ps1:66`, `:229-239` (the `profundum` target covers `plugins/profundum/{src,tests}` only, not `tools/`); `cmake/KratePlugin.cmake:276-290`; `plugins/profundum/installers/windows/setup.iss:64-68`; `plugins/profundum/CHANGELOG.md:1-14`; `plugins/profundum/version.json` |
| Phase 12 audition | `specs/profundum-phase12-ui/evidence/listen/render.sh:1-60`; `integration/main_page_render_set_test.cpp:417` (`[.listen]` case) |
| ODR sweep | `grep -rn "\b<Name>\b" dsp/ plugins/ tools/ tests/` for every new name in §4 (results per name in §4) |
| Review round (this session) | Phase 9 TU `:281-391`, `:462-472`, `:700-716`, `:826-960`, `:986-1040`, `:1125-1560`, `:1905-2200`; `profundum_voice.h:870-890` (the `anchorPhaseTap` branch); `profundum_engine_observer.h:32-41` (`ProfundumEngineSeams`); `profundum_engine.h:84-120`, `:150`, `:161`; `profundum_voice_engine_test_helpers.h:205-230`, `:290-305` (tap allocation and fill); `processor.h:100-125`; `processor.cpp:268-282`; `bass_stereo_field.h:91`, `:185-196`, `:320-338`, `:698`; `smoother.h:305` (`LinearRamp`); `bass_morph_state_helpers.h:167` (`showcasePair`); `bass_morph_test_helpers.h:703-716`; `profundum_test_fixture.h:394-420`, `:776-810`, `:1027-1072`; `preset_manager.h:60-140`; Vorago `controller.cpp:113-168`; `tools/vorago_preset_defs.h:1729-1757`; `disrumpo/tests/integration/preset/preset_save_test.cpp:195-225` (a `savePreset` test precedent); spec C-5 to C-10, FR-001 to FR-064 and SC-002 to SC-029 |

---

## 2. Plan-stage findings and rulings (confirm before the dependent task)

### 2.0 Spec rows these rulings amend (the row first, then Clarifications)

Six rulings below narrow or change ratified FR/SC text. The project rule is that a ruling goes into its FR/SC row first. So
once the user confirms a ruling, its amendment is written into the named spec rows **before** the gate that depends on it,
and only then into Clarifications. The test that checks a row is written against the amended text. Until the amendment
lands, the dependent gate does not start. This plan does not edit `spec.md`; the amendment is its own task (§11, step A).

| Ruling | Spec rows amended | Amended text (proposed) | Lands before |
|---|---|---|---|
| P-1 + P-10 | FR-057, SC-029 | FR-057: "… moved out of the anonymous namespace … into a header under `dsp/tests/unit/systems/`, with no change of constants, bars or computed values. The only non-verbatim edits are: (i) the namespace line; (ii) `inline` on namespace-scope functions and variables; (iii) the `auPt` alias redirected to the render shim (plan P-1); (iv) the named-function lifts of plan P-10. A lift turns an inline row block or lambda into a named header function that takes its arm states as parameters, and the Phase 9 shard calls it with its own base. …" SC-029: "… the moved cases report identical verdicts and byte-identical CSVs; `move-check.js` reports the verbatim remainder pure and lists every lift; `git status --porcelain -- dsp/` lists only the two files …" | G0 |
| P-2 | FR-012, SC-007, SC-006 | FR-012: "and both AT-13 variants" becomes "for AT-13, the gated cells of the variant the preset stores (Fracture or Rectify at Amount ≥ 1 − 1e-6 on endpoint A, with p = 0), at E1 and C3. The column is covered only when each variant is claimed by at least one preset". SC-007: "AT-13 both variants" becomes "AT-13 on the stored variant". SC-006 gains "both AT-13 variants claimed" | G4 |
| P-5 | C-7, SC-020 | C-7 "Sound space": "Components 0–9, 13 and 14 of `PresetDescriptor` are computed on the E1 and C3 sweep renders over the preset's descriptor window. Components 10–12 (`dNlDb`, `dBrOct`, and `eSubDb` minus the mid total) are read by the moved `auSteady` on a held NoteOn render of the same note and state (`auSteadyTotal()` = 6.46 s), over the Phase 8 `readWindow`. The mid total comes from the same render, tap and window. These components are masked when that window's mid total is below −60 dBFS in either preset." SC-020 gains "and no steady component is used unmasked on a window below −60 dBFS" | G3 |
| P-6 | C-9, FR-007 | C-9 gains: "**Carrier-less fields** (C-5): every factory preset stores Output Gain 0 dB (normalized 1.0), Output Ceiling −1 dBTP, Pitch Bend Range 2 st, Polyphony 4 and Edit Target A. Polyphony 4 satisfies 'at most 4'." FR-007: "… the C-6 authoring ceilings and the C-9 rules, including the carrier-less pins" | G3 |
| §5.11 (MONO arm) | FR-045, SC-022 | FR-045: "… ≤ 8 % of one core in MONO (for a preset that stores Poly, measured with Voice Mode forced to Mono and the override recorded in `cpu.csv`), and every Poly preset ≤ 25 % at 4 voices on its stored state …". SC-022 is amended the same way | G5 (CPU lane) |
| P-11 | SC-009 | SC-009 gains: "the AT-13 cells' processor runs prepare the engine with the `anchorPhaseTap` seam set through `Processor::setEngineSeamsForTest` (plan P-11), which is a test seam and never a preset field" | G2 |

**P-1: the move needs one redirected alias, or it cannot serve the processor path.** (FR-021, FR-057, SC-009)
After a verbatim move, every Phase 9 composite reader still renders **engine-direct** through `auPt::voiceEngineRender` /
`voiceEngineRenderWithTwin`. The composites are `auSteady` `:338`, `auGateRender` `:402`, `auRiseMs` `:799`,
`auOnsetESub` `:833`, `auAnchorFreq` `:853`, `auHalfPower` `:887`, `auDropRead` `:909`, `auGlideT50` `:935`,
`auNoteMemoryPhase` `:1959`, `auFieldMotionCell` `:1600`, `auMotionCell` `:1711` and `auMacroRead` `:2073`. FR-057 also
requires the probe and the showcase arms to consume the readers from the header only, with "no copy or re-port". Both
requirements can hold only with a render seam.

The plan keeps the moved text verbatim except **one line** and the P-10 lifts. The alias `namespace auPt = Krate::DSP::ProfundumTest;`
(`:92`) is redirected to a shim namespace. The shim re-exports all of `ProfundumTest` through a using-directive and
declares only its own `voiceEngineRender` and `voiceEngineRenderWithTwin` (§4.1). Qualified lookup finds a name declared
in the shim before it consults the using-directive ([namespace.qual]). So all 18 render call sites route through the shim,
and every other `auPt::X` resolves to `ProfundumTest::X` unchanged. With no hook installed, the shim calls the original
function and Phase 9 behaviour is unchanged. The FR-058 gate proves this with identical output (§5.1).

**Alternative, if the user rejects the alias:** a strictly byte-verbatim move. The processor path would then call only
the leaf tap readers, and Phase 13 would re-compose every row's script and switch logic. That is a re-port of all 50
rows, which FR-057 forbids, so the plan recommends P-1.

P-1 alone is not enough. About 25 columns compose the readers inside shard bodies that cannot be called from another TU,
and two readers hard-code their base. P-10 resolves this. Together, P-1 and P-10 change FR-057's "pure code move" text, so
the §2.0 amendment of FR-057 and SC-029 lands before G0.

**P-2: AT-13 has two variants, but a preset stores one Character algorithm.** (C-2.1, C-5, FR-012)
C-2.1 makes AT-13 "one column, carried by the pitch-moving stage of either Phase 9 variant". FR-012 requires both
variants at every note for every claim. A preset that stores Rectify cannot be measured on the Fracture variant without
overriding its algorithm, and a primary arm carries no override (C-5).

**Plan reading:** a claim of AT-13 is measured on the gated cells of the variant that matches the preset's stored
algorithm (Fracture or Rectify, at Amount ≥ 1 − 1e-6 on the sounding endpoint), at E1 and C3. The column counts as
covered only when **both** variants are claimed by some preset. The AT-13 primary stores Rectify (§7 row 13). A second
preset, "Fault Line" (§7 row 9), stores Fracture at Amount 1 and claims AT-13 as a secondary. No arm overrides the
algorithm. `Profundum_CoverageMatrixComplete` asserts that both variants are claimed.

This contradicts FR-012 and SC-007 as ratified, which require "both AT-13 variants" for every claim. Meeting that text
would need an algorithm override on the primary arm, which C-5 forbids. The §2.0 amendment of FR-012, SC-007 and SC-006
lands before G4.

**P-3: single-state composites read one `MorphState`.** (C-5)
`auOnsetESub`, `auAnchorFreq`, `auHalfPower`, `auDropRead`, `auGlideT50`, `auFieldMotionCell`, `auMotionCell` and the
NoteMemory reader build `auOne(s)`: A = B = s, at position 0. They represent "P as stored" exactly only when P's stored
Morph Position is 0, because the resolved state at position 0 is endpoint A.

**Rule (definition-level, per push):** a preset that claims any single-state column (AT-11, 12, 13, 17, 19–23, 33–40,
43–46) stores Morph Position (105) = 0. The arm passes P's endpoint A. Endpoint B still exists and is what Morph renders,
but no single-state arm reads it.

**P-4: forced values in a composite become authoring constraints.** (C-5)
`auFieldMotionCell` writes some lever and precondition values itself: SPREAD, spread 1, and `motionDepth` 0 / 1
(`:1607-1609`). The preset that claims AT-33 must store exactly the forced hi-side values, so that the composite's render
of the hi arm is P as stored. The definition test checks each forced value (§5.4 table, column "Forced / preconditions").

**P-5: the Phase 8 steady readers cannot read a 2 s window.** (C-7)
`dNlDb`, `dBrOct`, `eSubDb` and `lKDb` all use `readWindow(n, fs)`: it starts at 1 s, is 2^18 samples (5.46 s) long, and
asserts `REQUIRE(start + length <= n)` (`bass_morph_test_helpers.h:703-715`). The C-7 descriptor window `[1.0 s, 3.0 s)`
of the 3 s sweep render is shorter than that.

**Plan:**
- Components that take a window argument (band shape, side / mid, and frame-wise E_sub p2p, since `auFrameSeries` takes
  `from` / `to`) are read on the sweep render's descriptor window. t₉₀ comes from `auRiseMs`.
- The three steady components (`dNlDb`, `dBrOct`, `eSubDb`) are read by the moved `auSteady` on its own held NoteOn
  render of the same note (`auSteadyTotal()` = 6.46 s, `:281-284`). This is still a static NoteOn render (Q3), and the
  estimator is the Phase 9 one, unchanged.
- **Component 12 uses one window.** It is `eSubDb` minus the mid total of the **same** steady render, tap and window. The
  tap is Sum, because `auSteady` reads `eSubDb(t.sumL, t.sumR, …)` (`:359`), and the window is `readWindow`. It never uses
  the sweep window's mid total. The preset hook records the last steady render's Sum tap in the arm context (§4.7). The mid
  total is the `bandLevels` mid power sum over `[readWindow.start, readWindow.start + readWindow.length)` of that tap.
- The steady components are masked when the steady window's mid total is below −60 dBFS (the C-7 masking rule, applied
  to the steady window).

This changes C-7's "computed on the sweep renders" for components 10–12. The §2.0 amendment of C-7 and SC-020 lands
before G3.

**P-6: carrier-less decoded fields are pinned to engine defaults.** (C-5 twin rule)
Four decoded fields have no `VoiceEngineScript` carrier:
- Output Gain (0) is applied after the engine (`processor.h:10-13`).
- Output Ceiling (3) has `setOutputCeilingDb` but no script field.
- Pitch Bend Range (417) has `setPitchBendRange` but no event kind (`profundum_voice_engine_test_helpers.h:118-121`).
- Edit Target (1001) has no engine meaning.

C-5 says "a decoded field with no carrier stops the build". Rather than stop, the plan **pins these fields by authoring
rule**. Every preset stores Output Gain at its default (normalized 1.0, 0 dB), Ceiling at −1 dBTP (already FR-060), Bend
Range at 2 st (`kDefaultBendRangeSt`, `profundum_engine.h:112`) and Edit Target A (already FR-047). The engine's defaults
then equal the decoded values, and the twin builder asserts this before every twin.

**Polyphony** is pinned the same way. Every preset keeps the registered default "4" (`kDefaultPolyphony`, `:100`), which
satisfies C-9 ("at most 4"). FR-045's "4 voices" then holds for every Poly preset under the 4-note stimulus (FR-062).
Polyphony does have a carrier (the script's `polyphony` field, §5.2). It is pinned anyway, because a stored count below 4
would need a polyphony override on the POLY4 CPU arm before that arm could read "4 voices".

The pins narrow C-9 ("at most 4" becomes exactly 4) and add rules that C-9 and FR-007 do not state. The §2.0 amendment of
C-9 and FR-007 lands before G3.

**P-7: threshold F has a floor and fixed units.** (C-7, "the plan fixes how each component is normalised")
- **Units:** dB components ÷ 3 dB (the Phase 9 dB bar); the E_sub component ÷ 2 dB (the Anchor bar); D_br ÷ 0.25 oct;
  t₉₀ in log₂ ms ÷ 1 (the ratio-2 bar). Side / mid is clamped to [−40, 0] dB before it is scaled.
- **Distance:** Euclidean over the unmasked components of both notes.
- **F = max(2.5 · t_max, 1.0).** t_max is the largest twin distance: C-7 (i), the seed twins of the seed-independent
  presets, and (ii), the block-size twins of all presets. The factor 2.5 > 2 satisfies "more than twice".
- **The 1.0 floor** is one audibility bar in one component. Without it, a deterministic processor (twins reading about
  1e-6) would set F ≈ 0, and the near-variant control could never read below it.
- F is fixed at the pilot and never lowered (§5.8).

**P-8 (no ruling needed): the drive must not render audio before `getState`.** A rendered block publishes the morph
runtime blob (`processor.cpp` `publishRuntime`), and a 40 ms position ramp is not finished after one 512-sample block.
FR-047 (target = ramped position) would then fail. A parameter-only `process()` (numOutputs 0) returns at
`processor.cpp:383-386` before anything is published. `getState` then falls back to precedence 3, the synthesized blob
of the registered values (`:681-685`). The C-4 drive therefore prepares the processor (spec step 1) but delivers every
value with parameter-only calls.

**P-9 (no ruling needed): the generator TU is outside the clang-tidy `profundum` target.** That target covers
`plugins/profundum/{src,tests}` (`run-clang-tidy.ps1:229-239`). FR-056 is met by tidying
`tools/profundum_preset_generator.cpp` by explicit file list against the Ninja compile database, after reconfiguring
`build/windows-ninja` (memory: a stale database produces phantom warnings).

**P-10: the row logic Phase 13 needs is lifted into named header functions (route (b)).** (FR-012, FR-021, FR-057, FR-058)
About 25 columns compose the moved readers inside a shard body:
- the AT-11 / 12 block (`:1154-1173`);
- the AT-13 loop body (`:1207-1238`);
- the AT-17 block (`:1266-1286`);
- the `dropCell` lambda (`:1305-1327`);
- the AT-23 block (`:1365-1385`);
- the AT-24 `cell` lambda and the AT-25 block (`:1430-1489`);
- the AT-27 `dlk` / `cell` lambdas and the AT-28 `onset` lambda (`:1498-1549`);
- the NoteMemory loop body (`:2004-2031`);
- the macro reader (`auMacroRead` `:2073-2083`, `auMacroCell` `:2095-2127`).

None of these lambdas or blocks can be called from another TU, and two readers hard-code their base:
`auNoteMemoryPhase` builds `auScript(auOne(auStateAs()), …)` (`:1964`), and the AT-28 `onset` lambda builds `auStateB()`
(`:1529`). Composing that logic again in Phase 13 would be the copy that FR-057 forbids ("no copy or re-port"). Calling
`auNoteMemoryPhase` unchanged under the preset hook would render Phase 9's A_s and not P, because §5.2 takes the endpoints
from the script.

**Ruling (route (b)):** the move gains a fourth class of mechanical edit, the **lift** (§4.1, edit 4). Each listed block or
lambda becomes a named header function. Its arm states, and every value Phase 13 must vary, are parameters. The Phase 9
shard calls the function with exactly the base and lever values it used before. Phase 13 arms call the same functions with
states derived from P, and no Phase 13 code computes a cell value. The FR-058 gate proves that the lifts change nothing:
byte-identical CSVs and the same printed values (§5.1).

Route (a) is rejected. It would add Phase 13 glue functions with equivalence arms, which keeps a second copy of the row
logic. Only the equivalence arm would keep that copy honest, and only on Phase 9 bases. P-10 amends FR-057 and SC-029
(§2.0) and needs the user's confirmation before G0.

**Endpoint guard:** in preset context the hook asserts that the script's `a` and `b` are among the states the arm declared
(§5.3, step 1a). A reader that hard-codes its base therefore fails, instead of silently measuring a Phase 9 state.

**P-11: AT-13 needs the `anchorPhaseTap` engine seam on the processor path, so a processor test seam carries it.**
(FR-012, FR-020, SC-007, SC-009)
- `auAnchorFreq` sets `sc.config.seams.anchorPhaseTap = true` (`profundum_engine_audibility_test.cpp:855`) and reads
  `t.voices[0].anchorPhase` over [1 s, 2 s) (`:856-865`).
- With the seam off, the voice renders the Anchor block-wise and fills the tap with one phase value per chunk
  (`profundum_voice.h:881-889`). The reader would then compute the mean frequency from a staircase.
- The tap vector is allocated whether or not the seam is set (`profundum_voice_engine_test_helpers.h:219-224`). So the
  `REQUIRE(to <= ph.size())` at `:860` passes, and the error is silent.
- The processor always prepares with `engine_->prepare(sr, makeProfundumEngineConfig())` (`processor.cpp:276`). Its test
  seams (`processor.h:107-124`) cannot set the engine config. The engine copies `cfg.seams` at `prepare`
  (`profundum_engine.h:150`) and hands `&seams_` to every voice (`:161`).

**Ruling (mechanism (a)):** add a processor test seam in the FR-033 / FR-067 pattern, which is allocation-free and never
called by `process()` (`processor.h:116-119`):

```cpp
/// Phase 13 P-11: the engine observer seams applied at the next setupProcessing (prepare). Default: all off.
void setEngineSeamsForTest(const Krate::DSP::ProfundumEngineSeams& s) noexcept { engineSeamsForTest_ = s; }
// private:
Krate::DSP::ProfundumEngineSeams engineSeamsForTest_{};
```

`processor.cpp:276` becomes
`Krate::DSP::ProfundumEngineConfig cfg = makeProfundumEngineConfig(); cfg.seams = engineSeamsForTest_; engine_->prepare(sr, cfg);`.
- **Shipping behaviour is unchanged.** With the default member, the prepared config equals `makeProfundumEngineConfig()`
  field for field.
- **Scope.** This is a plugin runtime edit: a member and a setter on the existing `Processor`, with no new class. It
  reaches `profundum_tests` and pluginval, which §9 already runs because `processor.cpp` changes for `notify`.
- **ODR sweep** (this session): `setEngineSeamsForTest` and `engineSeamsForTest_` return 0 hits.
- **Measurement only.** The seam is never a preset field. The renderer sets it only from the script's `config.seams`
  (§5.2), and `anchorPhaseTap` is the only seam it may set (§5.3, step 4).

Mechanism (b), a separate processor-path AT-13 reader without the seam, is rejected: it would be a new reader and not the
Phase 9 row's own (FR-021). AT-13 is a G2 risk (§12, R-13).

---

## 3. Verified reuse inventory

| Reused | Real signature / fact (file:line) | Used for |
|---|---|---|
| `Processor::setState` / `getState` | `tresult PLUGIN_API setState(IBStream*)`, `getState(IBStream*)` (`processor.h:91-92`); `setState` touches atomics only and publishes the restore blob (`processor.cpp:596-658`) | C-4 drive, loads, round trip, browser load message |
| Parameter-only call | `process()` returns `kResultOk` at guard 2 when `numOutputs <= 0`, after routing params (`processor.cpp:380-386`) | C-4 drive (P-8) |
| Two-pass Edit Target routing | pass 1 reads 1001's last point; pass 2 routes every other queue to the selected slot (`processor.cpp:833-862`) | drive blocks A / B |
| Route kinds | `kParamRoutes[id].kind ∈ {None, Global, Macro, Performance, Sound, Note, MorphControl}` (`sound_param_table.h:71-101`); 60 Sound rows | drive validation |
| `getTailSamples` | `ceil((R_eff + 1250) fs / 1000) + 64`; reads atomics; 0 before setupProcessing (`processor.cpp:570-594`) | C-6 tail span |
| Test seams | `engineForTest()` (`:108`), `setEngineObserverForTest()` (`:117`), `mutableEngineForTest()` (`:119`), `endpointRejectionsForTest()` (`:120`) | renderer, CPU |
| `notify` | `kMsgEndpointName` sanitises, stores under `namesMutex_` and returns without a flush (`processor.cpp:723-739`); unknown IDs go to `AudioEffect::notify` (`:740`) | names in the drive; new load message |
| State constants | `kStateSeedBytes` 84–87, `kStateEndpointAOffset 88`, `kStateEndpointBOffset 289`, `kStateRuntimeOffset 490`, `kStateNamesOffset 510`, `kStateV3Bytes 574`, `kRt*Offset` (`plugin_ids.h:32-79`) | decode, seed patch, FR-047 |
| Block field order | global: gain f32, mode i32, polyphony i32, ceiling f32 (`global_params.h:182-187`); note: priority, legato, porta ms f32, porta mode, glide mode, memory, retrigger, bend f32 (`note_params.h:360-369`); morph: position f32, ramp f32, anchor mode i32, edit target i32 (`morph_params.h:301-306`) | `decodePresetState` |
| Laws | `normalizedFromPlain(const FieldInfo&, double)`, `indexFromNormalized(double, int)` (`sound_param_row.h`); `kPortamentoLaw` (`note_params.h:226`); `kMorphRampLaw` (`morph_params.h:161`); `kMaxNoteMemoryIndex 3`, `kMaxNoteRetriggerIndex 1` (`note_params.h:219-220`); macros are identity, neutral 0.5 (`macro_params.h:8`) | script → parameter carriers |
| `ppNorm` / `withEndpoints` / `writeV2Stream` / `loadStateBytes` / `patchU32` | `profundum_test_fixture.h:1013-1065`; `param_path_audibility_helpers.h:527-547` | renderer state construction |
| `VoiceEngineScript` | `fs, hostBlock, totalSamples, seed, config, a, b, endpointStates, position, macros, mode, polyphony, events, attachObserver, tappedVoices, countAllocations` (`profundum_voice_engine_test_helpers.h:139-155`) | carrier table (§5.2) |
| `voiceEngineRender` / `voiceEngineTwinScript` / `voiceEngineRenderWithTwin` | `:377`, `:441-447`, `:449` | twins, fidelity arms, the shim's fall-through |
| Phase 9 readers | moved to `profundum_audibility_readers.h` (§4.1); leaf readers `auBm::dNlDb(const MorphChainTaps&, f0, fs)`, `dBrOct(const FieldChainTaps&, f0, fs)`, `eSubDb(L, R, f0, fs)`, `dCxDb`, `lKDb`, `auSf::bandReading(L*, R*, n, fs, fLo, fHi)` (`bass_morph_test_helpers.h:762-835`, `bass_stereo_field_test_helpers.h:711-717`) | probe, showcase, descriptor |
| `measureTruePeakDb` | `float measureTruePeakDb(const float* l, const float* r, std::size_t n, double sampleRate)` (`low_frequency_metrics.h:419`) | FR-039, 041, 046 |
| `bandLevels` | `BandLevelReading bandLevels(span L, span R, double fs)`; `db[0][band]` is mid, `db[1][band]` side; edges 20/80/160/320/1280/20000 Hz (`band_level_metric.h:33-41`, `:167`) | descriptor, loudness |
| Fingerprints | `RenderFingerprint fingerprintRender(std::span<const float>)` (`render_fingerprint.h:73`), `compareFingerprints(actual, reference, metricTol, sampleTol)` (`:122`) | fidelity arms, determinism, twins |
| `AllocationScope` | `allocation_detector.h:111-123` | FR-037 |
| `PresetManager` | ctor `(config, IComponent*, IEditController*, userDirOverride, factoryDirOverride)` (`preset_manager.h:70-76`); `loadPreset` with `processor_ == nullptr` hands the `Comp` chunk to the load provider (`preset_manager.cpp:159-204`); `setStateProvider` / `setLoadProvider` (`preset_manager.h:126-129`) | browser |
| `OutlineBrowserButton` | `(const CRect&, IControlListener*, int32_t tag, std::string title, CColor frame = {64,64,72})` (`outline_button.h:79-89`) | header button |
| `PresetBrowserView` | `(const CRect&, PresetManager*, std::vector<std::string> tabLabels)`, `open()`, `close()`, `isOpen()` (`preset_browser_view.h:53-68`) | overlay |
| Vorago generator block | `CMakeLists.txt:679-743`: compiles processor.cpp, the SDK hosting sources and `vstgui_test_stubs.cpp` (GetPluginFactory only); links `KrateDSP KratePluginsShared sdk`; `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`; MSVC `/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458 /wd4459`, otherwise `-Wall -Wextra -Wpedantic -Wno-unused-parameter` | §4.5 |
| `.vstpreset` writer | `tools/vorago_preset_generator.cpp` `writeVstPreset` (48-byte header, Comp, Info, a List with 2 entries) and the `moduleHandle` global | §4.4 (duplicated, not shared, so the Vorago tool is not edited) |
| Info XML | `buildVoragoInfoXml` (`tools/vorago_preset_defs.h:1731-1755`) | `buildProfundumInfoXml` |
| CPU method | `kTrials = 640`, `kControlRatioCeiling = 1.05` (`processor_cpu_test.cpp:71-75`); `ProcessorArm`, `DirectArm`, `timeArm`, interleaved trials (`:105-330`) | FR-045 (same TU) |
| CI sharding | `kSuiteShards` long lane `{ dsp_systems_tests: 8, vorago_tests: 2, seraphis_tests: 2 }`; filter `[long]~[vorago-sweep]` (`ci-test-shard.js:106-112`) | §9 |

---

## 4. Component design

### 4.1 FR-057 move: `dsp/tests/unit/systems/profundum_audibility_readers.h` (test-only, no DSP layer)

**What moves.** Everything inside the first anonymous namespace of `profundum_engine_audibility_test.cpp`, from `:90` to
its closing `}  // namespace` at `:2264`. That is the aliases, constants, utilities, bases and `AuSetting`; the scripts;
the steady, gate, short-window and frame readers; `AuCell`, the CSV writer and `auReport`; the per-row cell builders and
every `auShard*` function; and the macro, sweep and swell helpers. The include block `:41-86` moves with it.

**What stays in the TU:** the header comment, `#include <catch2/catch_test_macros.hpp>`,
`#include "profundum_audibility_readers.h"`, the second anonymous namespace (`auRunShard`, `:2271-2281`) and the 16
TEST_CASEs (`:2285-2409`). The TU's `using namespace Krate::DSP;` (`:88`) is replaced by
`using namespace Krate::DSP; using namespace Krate::DSP::ProfundumAudibility;` at TU scope, so its remaining text
compiles unchanged.

**Mechanical edits only** (each one is checked by `evidence/move-check.js`, §5.1):
1. `namespace {` → `namespace Krate::DSP::ProfundumAudibility {`. Names of `Krate::DSP` stay visible through
   enclosing-namespace lookup, so the TU's `using namespace Krate::DSP;` is not repeated in the header.
2. `inline` is prepended to every namespace-scope non-template function and every namespace-scope `constexpr` variable.
   This is for ODR: the header is included by both `dsp_systems_tests` and `profundum_tests`.
3. **P-1:** `namespace auPt = Krate::DSP::ProfundumTest;` (`:92`) → `namespace auPt = Krate::DSP::ProfundumAudibility::render;`.
4. **P-10 lifts.** Each region below becomes a named function, placed in the header immediately before the function that
   contained it. Its body is the region's text, with captured variables turned into the listed parameters, and with
   `cells.push_back(c);` replaced by `return c;` where the region built one cell. The containing shard calls the function
   with exactly the values the region used. Every lifted function is `[[nodiscard]] inline`, except L4, which is a
   template. ODR sweep (this session, `grep -rn` over `dsp/ plugins/ tools/ tests/`): every name in the table returns 0
   hits.

| # | Lifted function | Source region (Phase 9 TU) | Phase 9 call after the lift |
|---|---|---|---|
| L1 | `AuCell auCellOnsetESub(bool punch, const MorphState& lo, const MorphState& hi, float windowMs, std::uint8_t note)` | AT-11 / 12 loop body after the lo / hi construction, `:1154-1173` | `cells.push_back(auCellOnsetESub(punch, lo, hi, windowMs, note));`. The round-2 AT-12 block (`:1177-1205`) is not edited |
| L2 | `AuCell auCellAt13(AuAlg alg, const MorphState& lock, const MorphState& follow, std::uint8_t note)` | AT-13 loop body after the lock / follow construction, `:1213-1238` | `cells.push_back(auCellAt13(alg, lock, follow, note));` |
| L3 | `AuCell auCellAt17(const MorphState& lo, const MorphState& hi, std::uint8_t note)` | `:1266-1286` | `cells.push_back(auCellAt17(lo, hi, note));` |
| L4 | `template <class Pick> [[nodiscard]] AuCell auDropCell(const char* row, const std::string& contrast, const MorphState& lo, const MorphState& hi, std::uint8_t note, const char* descriptor, const Pick& pick, double bar, const std::string& barText, bool ratio)` | the `dropCell` lambda, `:1305-1327` | each of its four calls becomes `cells.push_back(auDropCell(…, note, …));` |
| L5 | `AuCell auCellAt23(const MorphState& base, std::uint8_t note, float loMs, float hiMs)` | `:1365-1385` | `cells.push_back(auCellAt23(as, note, 0.0f, 200.0f));` |
| L6 | `AuCell auCellAt24(const AuSetting& lo, const AuSetting& hi, const std::string& baseName, bool gated, const std::string& remark, std::uint8_t note, const std::string& ampStep)` | the AT-24 `cell` lambda after its lo / hi construction, `:1436-1457` | the lambda keeps its lo / hi construction and pushes `auCellAt24(auOne(lo), auOne(hi), …)` |
| L7 | `AuCell auCellAt25(const AuSetting& lo, const AuSetting& hi, std::uint8_t note, const std::string& ampStep)` | `:1470-1489` | `cells.push_back(auCellAt25(auOne(lo), auOne(hi), note, auAmpStepArm(&…::releaseMs, 50.0f, 2000.0f, note)));`. The ampStep render now runs before the two gate renders. Every render is a fresh engine, so the order cannot change a value; the FR-058 gate checks this |
| L8 | `double auDlk(const AuSetting& s, std::uint8_t note, double& outLk)` and `AuCell auCellAt27(const AuSetting& lo, const AuSetting& hi, const std::string& baseName, std::uint8_t note)` | the `dlk` and `cell` lambdas, `:1498-1521`. The `s.mass = mass` assignment moves to the call site | `auGateOnEither(auCellAt27(auOne(B at Mass 0), auOne(B at Mass 100), "B", note), auCellAt27(…B_s…, kAuBaseBs, note), cells, …)` |
| L9 | `double auOnsetDyn(const AuSetting& s, std::uint8_t note, double& outRatio)` and `AuCell auCellAt28(const AuSetting& lo, const AuSetting& hi, std::uint8_t note)` | the `onset` lambda and its cell, `:1528-1549`. `auStateB()` and the `transientAttackDb` assignment move to the call site | `cells.push_back(auCellAt28(auOne(B at 0 dB), auOne(B at +12 dB), note));` |
| L10 | `AuPhase auNoteMemoryPhase(const MorphState& base, NoteMemory::Mode m, AuTransition kind, std::uint8_t note, std::uint64_t t1)` (new leading parameter: `auOne(auStateAs())` at `:1964` becomes `auOne(base)`); `struct AuNoteMemoryRow` (the local `Row`, `:1981-1990`, moved to namespace scope); `const std::array<AuNoteMemoryRow, 4>& auNoteMemoryRows()` (the four rows, `:1993-2002`, as a function-local `static const`); `AuCell auCellNoteMemory(const MorphState& base, const AuNoteMemoryRow& r, std::uint8_t note, std::uint64_t t1)` (the loop body, `:2005-2030`) | as listed | `for (const AuNoteMemoryRow& r : auNoteMemoryRows()) cells.push_back(auCellNoteMemory(auStateAs(), r, note, t1));` |
| L11 | `AuReading auMacroAxisRead(const AuSetting& s, ProfundumMacro m, std::uint8_t note)` (the switch of `auMacroRead`, `:2076-2082`); `AuCell auMacroCellOn(ProfundumMacro m, const AuSetting& lo, const AuSetting& hi, const std::string& baseName, std::uint8_t note, bool gated, const std::string& remark)` (the body of `auMacroCell`, `:2097-2127`, reading `auMacroAxisRead(lo, m, note)` and `(hi, m, note)`) | as listed | `auMacroRead` keeps its signature: it builds `s` as before and returns `auMacroAxisRead(s, m, note)`. `auMacroCell` builds lo / hi as `auMacroRead` does (macro m at 0 / 1, the others neutral) and returns `auMacroCellOn(…)` |

Every other row that Phase 13 measures is already a callable entry point that takes its arm states: `auSteadyCell`,
`auFloorRule`, `auFieldMotionCell`, `auMotionCell`, `auRiseMs`, `auGateDt` and `auSteady`. Phase 13 arms use
**call-only composition**. An arm builds the lo / hi states from P, calls one header entry point per cell (plus
`auFloorRule` where the Phase 9 row applies it), and reads the returned `AuCell`'s `value` and `pass`. It does no
arithmetic on readings.

The render shim is the only new code in the header. It is placed above the moved block:

```cpp
namespace Krate::DSP::ProfundumAudibility::render {
using namespace Krate::DSP::ProfundumTest;   // every auPt::X except the two below resolves here, unchanged

/// Phase 13 (FR-057, plan P-1): a scoped hook that reroutes the moved readers' renders. Default: none.
using ScriptRenderFn = VoiceEngineTaps (*)(const VoiceEngineScript& script, bool twin, int twinVoice, void* ctx);
struct ScriptRenderHook {
    ScriptRenderFn fn = nullptr;
    void* ctx = nullptr;
};
[[nodiscard]] inline ScriptRenderHook& scriptRenderHook() noexcept {
    thread_local ScriptRenderHook hook{};
    return hook;
}
[[nodiscard]] inline VoiceEngineTaps voiceEngineRender(const VoiceEngineScript& s) {
    const ScriptRenderHook h = scriptRenderHook();
    return h.fn != nullptr ? h.fn(s, false, -1, h.ctx) : ProfundumTest::voiceEngineRender(s);
}
[[nodiscard]] inline VoiceEngineTaps voiceEngineRenderWithTwin(const VoiceEngineScript& s, int voice = -1) {
    const ScriptRenderHook h = scriptRenderHook();
    return h.fn != nullptr ? h.fn(s, true, voice, h.ctx) : ProfundumTest::voiceEngineRenderWithTwin(s, voice);
}
/// RAII: installs {fn, ctx} and restores the previous hook on destruction. Non-copyable.
class ScriptRenderScope {
public:
    ScriptRenderScope(ScriptRenderFn fn, void* ctx) noexcept : prev_(scriptRenderHook()) {
        scriptRenderHook() = ScriptRenderHook{fn, ctx};
    }
    ~ScriptRenderScope() { scriptRenderHook() = prev_; }
    ScriptRenderScope(const ScriptRenderScope&) = delete;
    ScriptRenderScope& operator=(const ScriptRenderScope&) = delete;
private:
    ScriptRenderHook prev_;
};
}  // namespace Krate::DSP::ProfundumAudibility::render
```

ODR sweep (this session): `ScriptRenderHook`, `ScriptRenderScope`, `ScriptRenderFn`, `ProfundumAudibility` and
`profundum_audibility_readers` each return 0 hits.

`auWriteEvidence` keeps `KRATE_DSP_TESTS_DIR` (`:180-182`). `profundum_tests` gains that compile definition (§9) so the
header compiles there. Plugin TUs never call `auReport`, so they never write Phase 9 evidence.

**No `dsp/tests/CMakeLists.txt` change.** Headers are not listed, and the TU stays registered at `:811`.

### 4.2 Category set: `plugins/profundum/src/preset/profundum_preset_config.h` (FR-001)

```cpp
inline Krate::Plugins::PresetManagerConfig makeProfundumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Profundum",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated"}};  // Q1; additive-only
}
/// "All", then subcategoryNames in order (copied from makeVoragoPresetTabLabels, vorago_preset_config.h:40-48).
[[nodiscard]] inline std::vector<std::string> makeProfundumPresetTabLabels();
```

The generated files go in `resources/presets/{Sub,Resonant,Harmonic,Driven,Plucked,Animated}/`. `Sub/.gitkeep` is deleted
by the first commit of the library (FR-002).

### 4.3 Definitions: `tools/profundum_preset_defs.h` (data only; namespace `Profundum::PresetDefs`)

The Vorago rules apply (`vorago_preset_defs.h:12-21`). Every function is `inline`, every table is a function-local
`static const`, the only project include is `plugin_ids.h`, and there is no state layout.

```cpp
namespace Profundum::PresetDefs {

struct ParamSetting {                       // a registered ID and its NORMALIZED value, as a host delivers it
    Steinberg::Vst::ParamID id;
    double normalized;
};

enum class CapabilityGroup : std::uint8_t { Core, Character, Anchor, Pitch, Envelope, Mass, Field, Motion, Morph,
                                            NoteMemory, Macros, Reference };

/// FR-010: the 50 AT columns in AT order, then the three reference columns.
enum class Capability : std::uint8_t {
    AT01, AT02, AT03, AT04, AT05, AT06, AT07, AT08, AT09, AT10, AT11, AT12, AT13, AT14, AT15, AT16, AT17, AT18, AT19,
    AT20, AT21, AT22, AT23, AT24, AT25, AT26, AT27, AT28, AT29, AT30, AT31, AT32, AT33, AT34, AT35, AT36, AT37, AT38,
    AT39, AT40, AT41, AT42, AT43, AT44, AT45, AT46, AT47, AT48, AT49, AT50, RSolid, RLock, RSine, Count
};
inline constexpr std::size_t kNumCapabilities = static_cast<std::size_t>(Capability::Count);
static_assert(kNumCapabilities == 53, "C-2.1: 50 AT columns + R-SOLID, R-LOCK, R-SINE");

struct CapabilityInfo {
    Capability column;
    CapabilityGroup group;
    std::uint8_t atId;            // 1..50; 0 for a reference column
    std::string_view label;       // "AT-04 Character Tube", printed in coverage-matrix.csv
};
[[nodiscard]] inline const std::array<CapabilityInfo, kNumCapabilities>& capabilityTable();

inline constexpr std::uint32_t kFactoryDefaultSeed = 0xB455u;   // == ProfundumEngine::kDefaultSeed (static_assert in the host)

struct ProfundumPresetDef {
    std::string_view name;                     // file stem and Info Name; unique; passes isValidPresetName
    std::string_view category;                 // one of kCategories
    std::string_view description;              // Info Comment; names the primary in words; no " & < > (written unescaped)
    Capability primary;
    std::vector<Capability> secondaries;
    std::vector<ParamSetting> endpointA;       // Sound-route IDs only (the 60 E-parameters)
    std::vector<ParamSetting> endpointB;       // Sound-route IDs only; EMPTY = endpoint B is a copy of endpoint A
    std::vector<ParamSetting> controls;        // 1, 100-103, 105, 410-416, 1000, 1002 (P-6 forbids 0, 2, 3, 417; also 4-7 and 1001)
    std::string_view nameA;                    // <= 31 UTF-8 bytes, already sanitised, never "A"
    std::string_view nameB;                    // never "B"
    std::uint32_t seed = kFactoryDefaultSeed;  // C-4 step 6
    bool seedDependent = false;                // C-7; verified in both directions by FR-044
};

inline constexpr std::array<std::string_view, 6> kCategories{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated"};
[[nodiscard]] inline const std::vector<ProfundumPresetDef>& allPresets();   // definition order == file order; EMPTY until G4
[[nodiscard]] inline std::string buildProfundumInfoXml(std::string_view name, std::string_view category,
                                                       std::string_view description);   // shape of vorago_preset_defs.h:1731-1755
}
```

ODR sweep: `ProfundumPresetDef`, `CapabilityInfo` and `kFactoryDefaultSeed` return 0 hits. `ParamSetting`, `Capability`
and `CapabilityGroup` exist in `Vorago::PresetDefs` / `Seraphis::PresetDefs`, but those are other namespaces and other
targets, and no TU includes two defs headers (spec ODR table).

### 4.4 Host: `plugins/profundum/tests/profundum_preset_host.h` (Catch2-free; namespace `ProfundumPluginTest`)

The shape is copied from `vorago_preset_host.h`: a heap processor, `prepare`, `process`, `loadState` and `saveState`, with
buffers sized once. It adds:

```cpp
class PresetHost {
public:
    PresetHost();
    ~PresetHost();                                                   // terminate / setActive(false), as Vorago does
    [[nodiscard]] Steinberg::tresult prepare(double sampleRate, Steinberg::int32 maxBlock);
    [[nodiscard]] Steinberg::tresult processParamsOnly(Steinberg::Vst::IParameterChanges* pc);   // numOutputs = 0 (P-8)
    [[nodiscard]] Steinberg::tresult process(std::size_t n, Steinberg::Vst::IEventList* ev,
                                             Steinberg::Vst::IParameterChanges* pc);
    [[nodiscard]] Steinberg::tresult sendEndpointName(int endpoint, std::string_view utf8);   // HostMessage kMsgEndpointName
    [[nodiscard]] Steinberg::tresult loadState(std::span<const std::uint8_t> comp);
    [[nodiscard]] bool saveState(std::vector<std::uint8_t>& out);
    [[nodiscard]] std::span<const float> outL() const noexcept;
    [[nodiscard]] std::span<const float> outR() const noexcept;
    [[nodiscard]] ::Profundum::Processor& processor() noexcept;
};

/// C-4 (FR-027): validate, drive, getState, seed patch, setState, getState. false + `why` on any failure.
[[nodiscard]] bool buildPresetComponentState(const Profundum::PresetDefs::ProfundumPresetDef& def,
                                             std::vector<std::uint8_t>& comp, std::string& why);
/// FR-024 / SC-010: the generator's whole body (directories, files, Info), shared with Profundum_GeneratorSmoke.
[[nodiscard]] bool writeFactoryTree(const std::filesystem::path& outDir, std::size_t& written, std::string& why);
```

**Validation** runs before any processing, and `why` names the preset and the point:
- every value is finite (bit pattern, `Krate::DSP::detail::isFinite`) and in [0, 1];
- endpoint lists contain only `RouteKind::Sound` IDs;
- `controls` contain only IDs 1, 100–103, 105, 410–416, 1000 and 1002;
- no ID appears twice in one list;
- each name is ≤ 31 bytes, equals `sanitizeEndpointName(name).view()`, and is not the default.

The host also has `static_assert(PresetDefs::kFactoryDefaultSeed == Krate::DSP::ProfundumEngine::kDefaultSeed)`.

**Drive** (using `Krate::Test::ParameterChanges` from `vst_param_changes.h`, which is header-only):
1. `prepare(48000.0, 512)` (spec C-4 step 1).
2. `processParamsOnly({1001 → 0.0} ∪ endpointA)`.
3. `processParamsOnly({1001 → 1.0} ∪ (endpointB.empty() ? endpointA : endpointB))`.
4. `processParamsOnly(controls ∪ {1001 → 0.0})` (FR-047: Edit Target A).
5. `sendEndpointName(0, nameA)`, `sendEndpointName(1, nameB)`.
6. `saveState(s1)`. Patch bytes 84–87 of `s1` with `def.seed` (little-endian). `loadState(s1)`, then `saveState(comp)`.
7. `decodePresetState(comp)` must equal `decodePresetState(s1)` **bitwise**, field by field (FR-049); otherwise false.

`writeFactoryTree` creates the six directories in `kCategories` order and writes `<category>/<name>.vstpreset` in
definition order. The writer uses the Vorago `writeVstPreset` layout, with the class ID from `kProcessorUID.toString`.
No timestamp, path string or RNG enters any byte.

### 4.5 Generator + CMake: `tools/profundum_preset_generator.cpp`, root `CMakeLists.txt` (after `:743`)

`main(argc, argv)` does three things: it sets `outputBase = argc > 1 ? argv[1] : "plugins/profundum/resources/presets"`,
calls `writeFactoryTree(outputBase, written, why)`, and prints `"wrote " << written << " presets"`. It exits 0 only when
no failure occurred. It defines `void* moduleHandle = nullptr;`, for the same reason the Vorago tool does.

```cmake
add_executable(profundum_preset_generator
    tools/profundum_preset_generator.cpp
    plugins/profundum/src/processor/processor.cpp
    plugins/profundum/src/processor/goertzel_bank_simd.cpp          # spec finding 3 (spectrum_producer.h:26)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    plugins/profundum/tests/vstgui_test_stubs.cpp)                   # GetPluginFactory only (stub :12)
target_link_libraries(profundum_preset_generator PRIVATE KrateDSP hwy KratePluginsShared sdk)   # NO vstgui_support, NO Catch2
target_include_directories(profundum_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/profundum/src ${CMAKE_SOURCE_DIR}/plugins/profundum/tests
    ${CMAKE_SOURCE_DIR}/tests/test_helpers ${CMAKE_SOURCE_DIR}/tools ${vst3sdk_SOURCE_DIR})
target_compile_features(profundum_preset_generator PRIVATE cxx_std_20)
set_target_properties(profundum_preset_generator PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# Warning posture: the Vorago block verbatim
# (MSVC /W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458 /wd4459; else -Wall -Wextra -Wpedantic -Wno-unused-parameter).
add_custom_target(generate_profundum_presets
    COMMAND profundum_preset_generator "${CMAKE_SOURCE_DIR}/plugins/profundum/resources/presets"
    DEPENDS profundum_preset_generator
    COMMENT "Generating Profundum factory presets (53 presets across 6 categories)" VERBATIM)
```

The include graph is the processor, the SDK, `profundum_preset_host.h`, the Catch2-free decode in
`preset_test_support.h`, `vst_param_changes.h` and `profundum_preset_defs.h`. It never includes `profundum_test_fixture.h`,
which includes Catch2 (`:57`). The SC-010 build-log check greps the generator's link line for `vstgui` and `Catch2`.

### 4.6 Browser: FR-030, FR-031, FR-032, FR-059

**Message ID** (`plugin_ids.h`, beside `:84-89`):

```cpp
inline constexpr const char* kMsgLoadPresetState = "ProfundumLoadPresetState";   // ctl -> proc, binary "state" (<= 4096 B)
```

ODR/ID sweep: `kMsgLoadPresetState` and `ProfundumLoadPresetState` return 0 hits. It is not a parameter and not a
state-format change.

**Processor test seam (P-11):** `setEngineSeamsForTest` and the `engineSeamsForTest_` member, applied at
`processor.cpp:276`, exactly as §2 P-11 states.

**Processor** (`processor.cpp` `notify`, a new branch before the `else`): read `getBinary("state", data, size)` and refuse
the message when `data == nullptr || size == 0 || size > 4096`. Otherwise build a `MemoryStream`, write the bytes, seek to
0 and `return setState(stream)`. `setState` already does everything FR-031 step 2 needs: the restore hand-off, the names,
the compare reset and `flushControllerSync`. `notify` runs on the UI / host thread, never on the audio thread, so it may
allocate the stream there. The processor already allocates IMessages off the audio thread (`processor.h:103-104`).

**Controller** (`controller.cpp:101-102`, following the Vorago pattern at `:113-118`):

```cpp
presetManager_->setStateProvider([this]() -> IBStream* { return createComponentStateStream(); });   // body of Vorago :153-168
presetManager_->setLoadProvider([this](IBStream* s, const Krate::Plugins::PresetInfo&) { return loadPresetStream(s); });
```

`bool Controller::loadPresetStream(IBStream*)` reads the chunk into a `std::vector<std::byte>` and refuses it if it is
larger than 4096 B or if its first int32 is above `kCurrentStateVersion`. It then:
1. sends a `kMsgLoadPresetState` `IMessage`, with `setBinary("state", …)`, through `sendMessage`;
2. calls `setComponentState` on a `MemoryStream` of the same bytes;
3. calls `componentHandler->restartComponent(kParamValuesChanged)`.

It returns true when step 2 returns `kResultOk`. With no host context, `allocateMessage()` returns nullptr. The load then
returns false, and `PresetManager` reports "Load provider failed". The controller mirror is never changed without the
processor.

- **`didOpen`:** create the browser view once, with
  `PresetBrowserView(frame->getViewSize(), presetManager_.get(), makeProfundumPresetTabLabels())`, and `frame->addView` it.
- **`willClose`:** `close()` the view if it is open, then null the pointer. The frame owns the view (Vorago `:402-410`).
- **`void Controller::openPresetBrowser()`** opens the view when it exists and is closed.
- **New member:** `Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;`.

**View factory:** `createCustomView` gains
`if (std::strcmp(name, "PresetBrowserButton") == 0) return new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS");`.

**Sub-controller** (`panel_sub_controller.h/.cpp`):
- adds `inline constexpr std::int32_t kPresetBrowserTag = 9400;`, which is not used in the D6 list (`:68-86`), and appends
  it to `kSessionTags` (13 → 14 entries);
- `verifyView` gives the control whose `custom-view-name` is `PresetBrowserButton` that tag and this listener (Vorago
  `:89-95`);
- `valueChanged` calls `owner_->openPresetBrowser()` for that tag (Vorago `:145-148`).

The tag is a session tag, not a ParamID and not state, so FR-059 holds.

**uidesc** (changed with XSLT, per the memory rule): one new child of the `header` container (`editor.uidesc:124`):

```xml
<view class="CView" custom-view-name="PresetBrowserButton" origin="452, 24" size="96, 24" tooltip="Browse and load factory and user presets."/>
```

It sits in the free span between the Glide readout, which ends at x = 436, and the page strip, which starts at x = 560.
No D1 control moves (FR-030).

### 4.7 Test support

**`plugins/profundum/tests/preset_test_support.h`** is Catch2-free and is included by both the host and the tests:

```cpp
struct DecodedPresetState {
    bool ok = false;
    std::int32_t version = 0;
    std::size_t bytes = 0;
    float outputGain = 0.0f, outputCeilingDb = 0.0f;
    std::int32_t voiceMode = 0, polyphony = 0;
    std::array<float, 4> macros{};
    struct Note {
        std::int32_t priority = 0, legato = 0;
        float portamentoMs = 0.0f;
        std::int32_t portamentoMode = 0, glideMode = 0, memory = 0, retrigger = 0;
        float bendSt = 0.0f;
    } note{};
    struct Morph {
        float position = 0.0f, rampMs = 0.0f;
        std::int32_t anchorMode = 0, editTarget = 0;
    } morph{};
    std::uint32_t seed = 0;
    Krate::DSP::MorphState a{}, b{};               // deserializeMorphState (morph_state.h:574)
    struct Runtime {
        std::uint8_t version = 0;
        float target = 0.0f, position = 0.0f, anchorPosition = 0.0f;
        std::uint8_t mode = 0;
        float rampMs = 0.0f;
        std::uint8_t mainMask = 0, anchorSelection = 0;
    } runtime{};                                   // offsets: plugin_ids.h:70-77
    std::array<std::string, 2> names;              // length byte + bytes (plugin_ids.h:55-57)
};
[[nodiscard]] DecodedPresetState decodePresetState(std::span<const std::uint8_t> comp);
/// Field-by-field compare. Ints, enums and names: exact. Floats: |x - y| <= tol * max(|y|, 1). MorphState: by leafBitsOf
/// leaf (float leaves with tol, the rest exact). The first difference is written to `where`.
[[nodiscard]] bool decodedEqual(const DecodedPresetState& x, const DecodedPresetState& y, double tol, std::string& where);
[[nodiscard]] bool presetFloatIsFinite(float x) noexcept;   // bit pattern; immune to fast-math
/// C-9 rule 5 (§6): an active endpoint (§5.6) stores Rectify, Crush or Fracture at Amount >= 0.9 with Anchor Level < 0.25.
/// One predicate, shared by Profundum_PresetDefsRules and the [.listen] case.
[[nodiscard]] bool fundamentalLossFlagged(const DecodedPresetState& d) noexcept;
struct SweepTimeline { double fs; std::size_t block; std::size_t noteOff; std::size_t total; };  // C-6 (§5.5)
[[nodiscard]] SweepTimeline makeTimeline(double fs, Steinberg::uint32 tailSamples);
```

Name collisions inside `ProfundumPluginTest`: the fixture already defines `isFiniteBits` (`profundum_test_fixture.h:925`),
so the Catch2-free header uses `presetFloatIsFinite`, which returns 0 hits. `fundamentalLossFlagged` also returns 0 hits. `decodePresetState` and
`buildPresetComponentState` also exist as functions in the `VoragoTest` and Seraphis test namespaces (sweep this session).
Those are other targets and are never linked together.

**`plugins/profundum/tests/preset_render_support.h`** uses Catch2. It includes the fixture, `profundum_audibility_readers.h`,
`band_level_metric.h`, `render_fingerprint.h` and `low_frequency_metrics.h`. It uses namespace aliases only:
`namespace auRd = Krate::DSP::ProfundumAudibility; namespace auPt = Krate::DSP::ProfundumTest;`. It never puts
`using namespace` on either, because `param_path_audibility_helpers.h` ports functions with the same names into
`ProfundumPluginTest`.

```cpp
/// The state a script is overlaid on (§5.2). preset == nullptr: probe context (script authoritative, default state).
struct PresetArmContext {
    const DecodedPresetState* preset = nullptr;
    std::span<const std::uint8_t> comp;            // the preset's Comp bytes (preset context)
    bool macrosFromScript = false;                 // true only for the AT-47..50 performance arms
    /// Preset context: the (a, b) endpoint pairs the arm passes to the entry point. Every render's script must match
    /// one pair leaf for leaf (§5.3 step 1a, P-10 endpoint guard). Single-state readers declare (x, x).
    std::vector<std::pair<Krate::DSP::MorphState, Krate::DSP::MorphState>> allowedEndpoints;
    std::vector<float> lastSumL, lastSumR;         // the last render's Sum tap (P-5 component 12's mid total)
    std::size_t anchorPhaseTapRenders = 0;         // processor renders that carried anchorPhaseTap (§5.7 gate (c), P-11)
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> overrides;   // recorded secondary-claim preconditions
    struct Fidelity { bool pass; double worstSample; double worstMetric; std::string tap; };
    std::vector<Fidelity> fidelity;                // one per twin render (C-5 fidelity arm)
};
/// §5.2: the script, with every carrier filled from the context.
[[nodiscard]] auPt::VoiceEngineScript effectiveScriptFor(const auPt::VoiceEngineScript& s, const PresetArmContext& ctx);
/// §5.3: one processor render of `s`. Taps come from the attached VoiceEngineObserver; outL / outR from the processor.
[[nodiscard]] auPt::VoiceEngineTaps renderScriptThroughProcessor(const auPt::VoiceEngineScript& s, bool twin, int twinVoice,
                                                                 PresetArmContext& ctx);
/// The hook adapter. Matches auRd::render::ScriptRenderFn; ctx is a PresetArmContext*.
[[nodiscard]] auPt::VoiceEngineTaps processorRenderHook(const auPt::VoiceEngineScript& s, bool twin, int voice, void* ctx);

/// One showcase, reverse or probe cell (§5.4).
struct ShowcaseArm {
    PresetDefs::Capability column;
    std::string variant;
    std::uint8_t note = 0;
    double lo = 0.0, hi = 0.0, value = 0.0, bar = 0.0;
    bool pass = false;
    bool fidelityPass = true;
    double windowMs = 0.0;
    std::string overrides;
    std::string descriptor, tap;
};
[[nodiscard]] std::vector<ShowcaseArm> showcaseColumn(const DecodedPresetState& p, std::span<const std::uint8_t> comp,
                                                      PresetDefs::Capability c, bool primary);
/// The C-7 descriptor of one preset at one note (§5.8).
struct PresetDescriptor {
    std::array<double, 15> x{};
    std::array<bool, 15> valid{};
    double midTotalDb = -200.0;
};
[[nodiscard]] PresetDescriptor describePreset(const DecodedPresetState& p, std::span<const std::uint8_t> comp,
                                              std::uint8_t note, std::size_t hostBlock, std::uint32_t seedOverride);
[[nodiscard]] double presetDistance(const std::array<PresetDescriptor, 2>& p, const std::array<PresetDescriptor, 2>& q,
                                    std::size_t& masked);
/// Sweep reading cache (§10): one render per (preset, note, fs, variant). Readings are kept and the audio is dropped.
struct PresetRenderReadings {
    bool allFinite = false;
    double heldRmsDb = -200.0, tailRmsDb = -200.0, truePeakDb = -200.0;
    Krate::DSP::StereoFieldTest::BandReading monoSum{};
    bool anchorBitIdentical = false, solidBitIdentical = false, controlFails = false;
    double midTotalDb = -200.0, t90Ms = 0.0;
    std::size_t windowStart = 0;
};
/// A function-local static map; test thread only.
[[nodiscard]] const PresetRenderReadings& presetReadings(std::size_t presetIndex, std::uint8_t note, double fs, int variant);
```

ODR sweep in `ProfundumPluginTest`, all 0 hits: `PresetArmContext`, `renderScriptThroughProcessor`, `effectiveScriptFor`,
`processorRenderHook`, `PresetRenderReadings`, `ShowcaseArm` (also 0 in the spec's sweep), and `PresetSweepObserver`
(§5.6). `PresetDescriptor`, `DecodedPresetState` and `SweepTimeline` exist only in other namespaces (spec table).

### 4.8 Documentation and rosters

- **`plugins/profundum/MANUAL.md` (FR-051).** It follows the Vorago section order from that manual's headings:
  Introduction, Signal Flow, Interface Overview, Macros, Header Controls, one section per page, Presets, Performance Notes,
  Cross-Platform, Tips & Tricks. It names every user-visible registered title verbatim and documents the Anchor / Mass
  split, Morph, Edit Target and Compare. It includes the macro table from Phase 12 D3, and it lists the library by
  category with each preset's one-line Comment.
- **`plugins/profundum/docs/` (FR-052):** `index.html`, `manual-template.html` and `assets/`, copied from
  `plugins/vorago/docs/` and re-texted. `index.html` links the manual.
- **`.claude/skills/release/SKILL.md` (FR-053):** `profundum` is added to Inputs (`:15-16`), and the row
  `| profundum | profundum_tests | Profundum.vst3 |` is added to the target table (`:22-31`).
- **`tools/check-preset-generator-determinism.js` (FR-028):** `GENERATORS` (`:54-57`) gains
  `profundum: 'profundum_preset_generator',`, and the header comment at `:21-22` mentions it.
- **`plugins/profundum/CHANGELOG.md` (FR-054):** the `[0.1.0] - Unreleased` entry gains three bullets: the factory
  library, the preset browser and the manual. `version.json` is not touched.
- **`plugins/profundum/CLAUDE.md`:** adds the presets, browser and load-message facts, and the new test TUs in the tests
  table.

---

## 5. Algorithms (exact)

### 5.1 The move gate (FR-058, SC-029)

1. **Baseline (the pre-move binary at HEAD).** Build `dsp_systems_tests`. Run the 16 moved cases (`:2285-2409`) as **16
   concurrent processes**, one per TEST_CASE name. Capture each one's stdout to `evidence/move-gate/pre/<case>.log`.
   `auReport` writes CSVs into `specs/profundum-phase9-voice-engine/evidence/audibility-table/`. Those are the Phase 9
   committed evidence, and the gate must not change them. So copy them to `evidence/move-gate/pre/`, then restore the
   directory with `git checkout -- <that dir>`.
2. **Move (§4.1).** Run `node specs/profundum-phase13-presets-release/evidence/move-check.js`. It reads
   `git show HEAD:<TU>` and the new header + TU, and removes mechanical edits 1–3 (the namespace line, the `inline `
   prefixes and the P-1 alias line).
   - **Outside the lifts:** it requires the remaining text, outside the lift regions, to be identical line by line.
   - **The lifts:** it reads the manifest `evidence/move-lifts.json`, which gives, for each of L1–L11, the function name,
     the source line range at HEAD and the header line range. Every lift region must be in the manifest, and a region
     that is not fails the check. For each lift it writes the source-vs-lifted diff to `evidence/move-gate/lifts.diff`
     for review.
   - **Output:** it prints `MOVE PURE changed=1 lifted=11`, or fails. Step 5 proves the lifts' behaviour; this step checks
     only their text.

   **2b. Path set (SC-029: "the move touches only `dsp/tests/unit/systems/`").** Run `git status --porcelain -- dsp/`.
   It must list exactly ` M dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` and
   `?? dsp/tests/unit/systems/profundum_audibility_readers.h`, and nothing else. `git status` is used, not
   `git diff --name-only`, because the diff does not list the untracked new header. The output is saved to
   `evidence/move-gate/paths.txt`.
3. **Rebuild** `dsp_systems_tests` with `-- /nr:false`. It must have 0 warnings; the log is grepped for `warning`.
4. **Post-move.** Make the same 16 concurrent runs, logged to `move-gate/post/`, and copy and restore the CSVs as in step 1.
5. **Verdict.** Every pre/post CSV pair must be byte-identical, and every case's last line must have the same Catch2
   summary (the same pass count). Any difference stops the gate. FR-058 requires "verdicts identical ... same printed values".

The full `dsp_systems_tests` suite and its other `[long]` cases are not run. Cost, from measurements: the slowest case is
Motion, at 884 s in the Phase 9 close. Each run therefore takes about 15 min wall on 16 cores, plus two rebuilds of about
5 min each.

### 5.2 Effective script and the carrier table (C-5 twin rule)

`effectiveScriptFor(s, ctx)` works in two contexts.

**Probe context** (`ctx.preset == nullptr`): it returns `s` unchanged. The processor's base config is
`makeProfundumEngineConfig()`, which equals `ProfundumEngineConfig{}` (`profundum_engine_config.h`). A Phase 9 script can
differ from it: `auAnchorFreq` sets `config.seams.anchorPhaseTap` (`:855`). The `config` row below therefore applies in
both contexts.

**Preset context** (decoded state `d`): each decoded field maps to a script carrier as follows.

| Decoded field | Script carrier | Rule |
|---|---|---|
| endpoint A, endpoint B | `a`, `b` | from `s` (the composite filled them from the setting the arm passed, with the lever applied) |
| Morph Position 105 | `position` | from `s` (P's own, or the performance arm's 0 / 0.5 / 1) |
| runtime block | none | FR-047 makes it a static copy of 105, so it carries nothing beyond `position` |
| macros 100–103 | `macros` | `d.macros`, unless `ctx.macrosFromScript` |
| Voice Mode 1 | `mode` | `d.voiceMode`, unless there is a recorded override |
| Polyphony 2 | `polyphony` | `d.polyphony` (always 4, by P-6) |
| seed 84–87 | `seed` | `d.seed` |
| 410 priority, 411 legato, 412 portamento ms, 413 portamento mode, 414 glide mode, 415 memory, 416 retrigger | events `MonoPriority`, `Legato`, `PortamentoTimeMs`, `PortamentoMode`, `GlideMode`, `NoteMemoryMode`, `NoteRetrigger` at sample 0 | prepended **before** `s.events`, so a lever event the composite placed at sample 0 (the AT-23 portamento, the AT-43–46 memory and retrigger) wins in the stable sort |
| 1002 ramp ms, 1000 Anchor morph mode | events `MorphRampMs`, `AnchorMorphMode` at sample 0 | as above |
| 0 output gain, 3 ceiling, 417 bend range, 1001 edit target | none | P-6: asserted equal to 1.0 / −1 dBTP / 2 st / A before the script is used; otherwise `FAIL` (the FR-023 stop) |
| `config.seams` | `Processor::setEngineSeamsForTest(e.config.seams)` before `prepare` (P-11) | only `anchorPhaseTap` may be set. Any other seam gives `FAIL("no processor carrier: seam")`, the FR-023 stop. `forceLinearCore` twins never reach the processor (§5.3 step 7) |
| `config.placement`, `.dynamicsOversampling`, `.dynamicsOversamplingFactor` | none | each must equal `makeProfundumEngineConfig()`'s value, otherwise `FAIL("no processor carrier: config")`, the FR-023 stop (both contexts) |
| fs, hostBlock, totalSamples, events, tappedVoices, attachObserver | same | from `s` (the stimulus is the row's own) |

### 5.3 Script → processor render (`renderScriptThroughProcessor`)

1. `e = effectiveScriptFor(s, ctx)`.
1a. **Endpoint guard (preset context only).** (`e.a`, `e.b`) must equal one pair in `ctx.allowedEndpoints`, compared
   leaf for leaf with `leafBitsOf` (`sound_param_row.h:75`). Otherwise the render stops with
   `FAIL("script endpoints are not the arm's (a reader hard-codes its base)")`. An empty `allowedEndpoints` in preset
   context also fails, so an arm cannot skip the declaration.
2. **State.** In probe context, start from `writeV2Stream(fresh)`; in preset context, from `ctx.comp`. Apply
   `withEndpoints(bytes, e.a, e.b)` (`profundum_test_fixture.h:1065`), patch the seed bytes 84–87 with `e.seed`
   (`patchU32`), then call `loadStateBytes`.
3. **Pre-prepare parameter-only call** (the Phase 11 order, `param_path_audibility_helpers.h:584-593`). The endpoints came
   through the state, so one block per Edit Target is not needed. One block carries:
   - 105 = `e.position`, 100–103 = `e.macros`, 1 = `e.mode`, and 2 = `(e.polyphony − 1) / 7`;
   - every sample-0 carrier event of §5.2, converted through its law (`ppNorm`; 1002 through `kMorphRampLaw`; 1000 as
     index / 2; 413, 414, 410 and 411 as index / max);
   - 1001 = 0.
4. **Config, observer and prepare.**
   - Apply the §5.2 `config` rows: a non-default placement or oversampling field, or any seam other than `anchorPhaseTap`,
     gives `FAIL` (the FR-023 stop). Otherwise call `setEngineSeamsForTest(e.config.seams)`.
   - Run `VoiceEngineTaps taps; voiceEngineAllocateTaps(taps, e); VoiceEngineObserver obs(taps);`, call
     `setEngineObserverForTest(&obs)` and `prepare(e.fs, e.hostBlock)`, and call `enableFTZDAZ()` on the test thread.
   - After the render, call `setEngineSeamsForTest({})`, so a reused processor never keeps a seam.
5. **Events with sample > 0.**
   - NoteOn and NoteOff become host events at their in-block offset.
   - Carrier-kind events (the §5.2 kinds plus `MorphPosition`) become a parameter point.
   - **Blocks are split at every carrier-event sample**: block length = min(hostBlock, next carrier sample − pos). The
     processor applies a queue's last point at the block start (`processor.cpp:858-861`), so splitting keeps each point
     on its sample. This matches the engine-direct driver, which splits at every event (`voiceEngineRender`).
   - Any other kind (`PitchBend`, `Tempo`, `Endpoint`, `VoiceMode`, `Polyphony`, `Macro`) gives
     `FAIL("no processor carrier")`, which is the FR-023 stop. The Phase 9 TU only uses `MorphPosition`, `MorphRampMs`,
     `NoteMemoryMode`, `NoteOff`, `NoteOn`, `NoteRetrigger` and `PortamentoTimeMs` (grep this session).
6. **Capture and detach.** Capture `outL / outR` into `taps.outL / outR`, and `REQUIRE(endpointRejectionsForTest() == 0)`.
   Copy `taps.sumL / sumR` into `ctx.lastSumL / lastSumR` (P-5). Detach the observer after `setActive(false)` (Phase 11
   step 6).
7. **Twin.** When `twin` is set, render `tw = ProfundumTest::voiceEngineRender(voiceEngineTwinScript(e))` (or use the
   `twinVoice` form of `voiceEngineRenderWithTwin`), and set `taps.linearCoreL/R = tw.coreL/R`.
   **Fidelity arm (preset context only):** render `fd = ProfundumTest::voiceEngineRender(e)` without `forceLinearCore`.
   Compare its `sumL/R` and `characterL/R` with the processor taps per channel, using
   `compareFingerprints(fingerprintRender(proc), fingerprintRender(direct))` at the shared tolerances, and append the
   result to `ctx.fidelity`. The check runs per (preset, note) wherever a twin is used (C-5).

### 5.4 Showcase arms per column (C-5, FR-012): `showcaseColumn`

Every arm runs under `auRd::render::ScriptRenderScope scope(processorRenderHook, &ctx)`. The table uses this shorthand:
- **S(P)** is the setting `AuSetting{P.a, P.b, P.position, P.macros}`.
- **A(P)** is P's endpoint A (P-3).
- **lo** is P with the lever at the row's `lo` value, applied to both endpoints.
- **Lx** is a P-10 lifted header function (§4.1, edit 4). Every arm uses call-only composition, so no Phase 13 code
  computes a cell value.
- **Endpoint declaration.** Before it calls the entry point, every arm writes the (a, b) pairs it passes into
  `ctx.allowedEndpoints`: (x, x) for each single-state state x, and (P.a, P.b) or the lever-applied pair for S(P). The
  §5.3 step 1a guard checks every render against them.
  - Some entry points build their lo state internally: `auFieldMotionCell` (depth 0, and its forced SPREAD / spread 1,
    `:1607-1609`) and `auMotionCell` (source Off). For these, the arm also declares that state, built from A(P) by the
    lever rule in the table.
  - Because the declared hi state is A(P) itself, the guard also proves P-4: if a forced value differs from P's stored
    value, the composite's hi render no longer matches A(P), and the arm fails.

The cells are E1 and C3, with these exceptions: AT-32 is E1 only; AT-26 and AT-27 collapse their per-base cells to one
cell per note; AT-13 follows P-2.

| Column | Moved entry point | Arms | Value / bar | Forced / preconditions (P-4 / C-5) |
|---|---|---|---|---|
| AT-01 | `auSteadyCell(…, AuDesc::DNl, "Char / twin", lo, S(P), kAuBarDb, ">= 3 dB")` | lo: `excitation.mix` 0 | \|hi−lo\| ≥ 3 dB | drive 1 (contrast) |
| AT-02 | `auSteadyCell` `DBr` | lo: `shape` 0 | ≥ 0.25 oct | — |
| AT-03 | `auSteadyCell` `DNl` | lo: `foldAmount` 0 | ≥ 3 dB | — |
| AT-04…09 | `auSteadyCell` `DNl` | lo: `character.amount` 0 | ≥ 3 dB | stored algorithm = the row's |
| AT-10 | `auSteadyCell` `ESub` | lo: `anchor.level` 0 | ≥ 2 dB | Sine, LOCK (contrast) |
| AT-11 | L1 `auCellOnsetESub(true, lo, A(P), w, note)`, with w = P's `punchTimeMs` | lo: A(P) with mode Lock | \|Δ\| ≥ 2 dB | p = 0 |
| AT-12 | L1 `auCellOnsetESub(false, lo, A(P), w, note)`, with w = P's `ghostTimeMs` | lo: A(P) with Lock | ≥ 2 dB | p = 0 |
| AT-13 | L2 `auCellAt13(alg, lock, A(P), note)`, with alg = P's stored algorithm; the processor carries `anchorPhaseTap` (P-11) | lock: A(P) with mode Lock | flipped and \|cents − expected\| ≤ 5 | algorithm Fracture or Rectify at Amount 1; P-2; p = 0 |
| AT-14…16 | `auSteadyCell` `DCx` | lo: waveform Sine | ≥ 3 dB | — |
| AT-17 | L3 `auCellAt17(lo, A(P), note)` | lo: A(P) with Sine | hi − lo ≥ 3 dB | p = 0 |
| AT-18 | `auSteadyCell` `DNl` | lo: `anchorFoldFeed` 0 | ≥ 3 dB | — |
| AT-19…22 | L4 `auDropCell(row, …, lo, A(P), note, …, pick, bar, …, ratio)`, with the row's own `pick`, bar and `ratio` flag (the four Phase 9 call sites) | lo: A(P) with amount 0 / time 60 ms / shape 0 / rebound 0 | AT-19 ≥ 6 st; AT-20 ratio ≥ 2; AT-21 ≥ 0.1; AT-22 ≥ 0.05·A_drop | AT-20…22: amount 24 st (contrast); p = 0 |
| AT-23 | L5 `auCellAt23(A(P), note, 0.0f, P's portamento ms)` | lo: portamento 0 (inside L5) | ratio ≥ 2 | MONO, legato (contrast); p = 0 |
| AT-24 | L6 `auCellAt24(lo, S(P), …, note, "")` | lo: S(P) with attack 0.1 ms | ratio ≥ 2 | — |
| AT-25 | L7 `auCellAt25(lo, S(P), note, "")` | lo: S(P) with release 50 ms | ratio ≥ 2 | — |
| AT-26 | `auSteadyCell` `ESub` | lo: `mass` 0 | ≥ 3 dB | — |
| AT-27 | L8 `auCellAt27(lo, S(P), …, note)` | lo: S(P) with mass 0 | \|Δ\| ≥ 3 dB | — |
| AT-28 | L9 `auCellAt28(lo, S(P), note)` | lo: S(P) with `transientAttackDb` 0 | ≥ 3 dB | — |
| AT-29 | `auSteadyCell` `DSt` + `auFloorRule`, as in `auShardField` `:1650-1660` | lo: SOLID | ≥ 3 dB | SPREAD, spread 1 |
| AT-30 | same as AT-29 | lo: SOLID | ≥ 3 dB | WIDE, spread 1 (C-9: never Rectify Tone 1 + Fold 100 + WIDE) |
| AT-31 | `auSteadyCell` `DSt` | lo: spread 0 | ≥ 3 dB | SPREAD |
| AT-32 | `auSteadyCell` `DStLow` (E1 only) | lo: gravity 0 | ≥ 3 dB | SPREAD, spread 1 |
| AT-33 | `auFieldMotionCell(A(P), …)` | depth 0 / 1, forced by the cell | ≥ 3 dB, with 0 invalid frames | **forced:** SPREAD, spread 1, depth 1; p = 0 |
| AT-34…38 | `auMotionCell(row, contrast, A(P), …, note, A(P).motion, d, seconds_row, {}, bar, …)` | lo: source Off (inside the cell) | ≥ 3 dB / 0.25 oct | source Cycle; target = the row's; p = 0; `seconds_row` = the value `auShardMotion` passes (`:1812-1870`) |
| AT-39 | `auMotionCell` + `auEnvRetriggers(note, seconds_row)` | lo: Off | ≥ 3 dB | ENV → Mass, Unipolar (C-9); p = 0 |
| AT-40 | `auMotionCell` | lo: Off | ≥ 3 dB | DRIFT → Mass; `seedDependent = true`; p = 0 |
| AT-41, AT-42 | `auSteadyCell` `DNl` (performance) | lo: position 0; hi: 1 / 0.5 | ≥ 3 dB | — (twin per arm) |
| AT-43…46 | L10 `auCellNoteMemory(A(P), A(P), r, note, t1)` (mode and reference bases; main loop 2026-10-24, so the Phase 9 call keeps its two `auStateAs()` evaluations per row and its assertion count, FR-058), with r the `auNoteMemoryRows()` entry of the row and `t1 = auPickTransition(auHz(note), auHz(auUpOctave(note)))` as at `:2003` | lo: the row's M_ref; hi: the row's M (both set by L10's own sample-0 events, which win over P's prepended events, §5.2) | ≥ 30° | MONO, Retrigger Reset (contrast); stored mode = the row's M; p = 0 |
| AT-47…50 | L11 `auMacroCellOn(m, lo, hi, …, note, true, "")`, with `macrosFromScript` | lo / hi: S(P) with macro m at 0 / 1 and the other three at P's stored values | AT-47 / 48 / 50: hi − lo ≥ 3 dB / 0.25 oct / 3 dB; AT-49: ratio hi / lo ≥ 2 (the axis switch inside L11) | AT-50: twin per arm |

**Secondary claims:** when the preset stores a different value for a precondition, the arm sets that precondition to the
row's value on both arms and appends `parameter=value` to `ShowcaseArm::overrides`. The `overrides` column records it. A
primary arm with a non-empty `overrides` fails (SC-007: "0 precondition overrides on primary arms").

**Setting-defined windows** (AT-11, AT-12) record the window in `windowMs`.

**Reference reverse arms** (FR-013) take the row's own `lo` as P:
- **R-LOCK:** L1 `auCellOnsetESub(true, A(P), hi, 40.0f, note)`, with lo = A(P) (LOCK) and hi = A(P) with PUNCH at
  boost 6 dB and 40 ms (the row's contrast).
- **R-SINE:** `auSteadyCell DCx`, with lo = S(P) (Sine) and hi = S(P) with Triangle.
- **R-SOLID:** `auSteadyCell DSt` + `auFloorRule`, with lo = S(P) (SOLID) and hi = S(P) with SPREAD and spread 1. The
  FR-042 SOLID bit-identity clause is checked per push on its own short render (§5.6, "SOLID"), not on the `[long]`
  sweep render.

The decoded-state check requires both endpoints to store LOCK / Sine / SOLID.

### 5.5 Sweep timeline, windows, RMS (C-6, FR-038–041)

- **Stimulus.** fs = 48000 for every preset at E1 and C3; 44100 and 96000 at E1 only. Host block 512. Velocity 100,
  normalized as 100 / 127 (the Phase 9 `kAuVelocity`).
- **Timeline.** `noteOff = llround(3.0 · fs)` and `total = noteOff + getTailSamples()`. The tail is read after both
  `setupProcessing` and `setState`, because it depends on both (`processor.cpp:570-594`). The render starts at sample 0
  with the note-on.
- **Poly presets** add the chord render: E1, B1, E2 (MIDI 28, 35, 40) on at 0 and off at `noteOff` (FR-063).
- **RMS.** RMS(a, b) = 10·log10(mean over [a, b) of (L² + R²) / 2), in dBFS. The held RMS over [0.2 s, 3.0 s) must be
  above −60 dBFS. The tail RMS over the last `llround(0.25 fs)` samples must be ≤ −100 dBFS.
- **Finite.** Every output sample's bit pattern is checked (`(bits & 0x7F800000) != 0x7F800000`), and the non-finite
  count must be 0.
- **True peak.** `measureTruePeakDb(L, R, total, fs) ≤ −1.0 + 0.1` (FR-039 with FR-060's stored ceiling).
- **Max Mass (FR-041).** The same render, preceded (before `setupProcessing`) by four parameter-only blocks:
  {1001 → 0, 104 → 1.0}, {1001 → 1, 104 → 1.0}, {1001 → 0}, and Mod Wheel 6 = 1.0. Checks: true peak ≤ −0.9 dBTP and
  every sample finite, at E1 and C3.
- **Per-push guard (FR-046).** Note-off at 0.5 s and total 0.75 s, stored and at max Mass, at E1 / 48 kHz.

### 5.6 Mono sum (FR-042)

The check runs on the 48 kHz E1 render with a slim observer, `PresetSweepObserver final :
Krate::DSP::ProfundumEngineObserver`, modelled on `MspObserver` (`mono_sum_params_test.cpp:93-132`). Per voice it counts
`anchorL != anchorR` (bitwise) on the fly, and it records the resolved crossover `r.resolved->state.field.crossoverHz`
(`:132`). It stores no per-sample vectors.

- **Crossover and windows.** f_c is the last recorded crossover. The first window is W₁ = [1.5 s, 1.5 s + 2^16). If
  `bandReading(L+W₁, R+W₁, 2^16, fs, 0, f_c).valid` is false, use W₂ = [t₉₀ + 20 ms, t₉₀ + 20 ms + 2^16). Here t₉₀ is the
  output-mid onset: the first 1 ms frame whose RMS is ≥ 0.9 × the maximum 1 ms frame RMS over [0, noteOff). If neither
  window is valid, the preset **fails**.
- **Gate.** On the processor output, `rho ≥ kSubBandRhoBar` (0.99) and `|q| ≤ kSubBandQBarDb` (1.0 dB)
  (`bass_stereo_field_test_helpers.h:95-97`).
- **Anchor.** The anchor mismatch count must be 0 for every voice.
- **SOLID.** FR-042 applies the clause to "a preset that stores SOLID on its active endpoints".
  - **Active endpoints**, from the stored Morph Position p (105): A alone at p = 0, B alone at p = 1, and both for
    0 < p < 1, because the resolved state then blends both.
  - **Which presets.** Every preset whose active endpoints all store SOLID. That includes a p = 0 preset with SOLID on A
    and another mode on B.
  - **Settle point.** The preset must read L == R bitwise on every output sample from
    `s₀ = kLimiterLookaheadSamples + llround(kModeCrossfadeMs · fs / 1000)` on. That is 64 + 480 = 544 samples at 48 kHz:
    `kLimiterLookaheadSamples = 64` (`profundum_engine.h:107`) and `kModeCrossfadeMs = 10.0f`, the image-mode
    `LinearRamp` (`bass_stereo_field.h:91`, configured at `:192`, member `:698`).
  - **Lanes.** The `[long]` SC-019 case applies the clause on the 48 kHz E1 sweep render. SC-008 is per push, so
    `Profundum_ReferenceColumns` also gives the R-SOLID preset its own short render: E1, 48 kHz, block 512, note-on at 0,
    note-off at 0.5 s, total 0.75 s (the FR-046 span), with the same L == R check from s₀. Every other per-push SOLID
    preset is checked on its `Profundum_PresetRenderGuard` render (§5.5), which has the same span.
- `main_page_sanity_test.cpp:10-26` was read and states only the window [1.5 s, 1.5 s + 2^16), the f_c gating and the
  ρ / Q bars, which this section reuses.
- **Non-vacuity.** R' = −L on a copy must fail the ρ bar.

### 5.7 The probe (FR-020, FR-022, SC-009), run before any preset definition

There are eleven hidden cases, `Profundum_PresetAudibilityProbe_<Group>` (`[profundum][.probe]`), one per Phase 9 shard
function. Each case does the following for each note in `kAuNotes`:

1. **Processor path.** Under `ScriptRenderScope(processorRenderHook, &probeCtx)` (probe context), call the moved
   `auShard<Group>(cells, note)` unchanged. This reproduces every Phase 9 row, base and candidate, with every render
   going through the processor.
2. **Engine-direct (fidelity).** Make the same call with no hook installed. This gives the live Phase 9 cells in this
   executable.
3. **No glue arm.** Under P-10, every §5.4 arm calls a header entry point. The FR-058 gate proves the lifted functions
   equal Phase 9 on the Phase 9 bases (byte-identical CSVs, §5.1), and the §5.3 step 1a guard proves each showcase arm
   renders the states it declared. So the probe runs no separate equivalence arm.
4. **Join and write.** Join the cells with the CSV's 102 gated keys (row, note, base text; the AT-13 variant is in the row
   text). For each key, write the AT id, variant, note and base; the processor value; the bar text; pass; the live
   engine-direct value; the CSV `value`; and the relative deviation |live − csv| / max(|csv|, 1e-12). The output goes to
   `evidence/probe-table/<Group>.csv`. `evidence/merge-csv.js` merges the eleven files into `evidence/probe-table.csv` and
   asserts 102 rows.
5. **Gates.**
   - (a) Every gated cell's processor reading passes its bar.
   - (b) Each live engine-direct value is within **10 % relative** of the CSV `value` (`kPpPortTolerance`, the Phase 11
     precedent, `param_path_audibility_helpers.h:111-112`).
   - (c) The AT-13 cells (Fracture and Rectify, at E1 and C3) were rendered with `anchorPhaseTap` set on the processor's
     engine. The renderer counts every processor render that carried the seam (`ctx.anchorPhaseTapRenders`, §4.7;
     §5.3 step 4). In the Anchor group the count must equal 4 per note: the `lock` and `follow` renders of each variant
     (`:1213-1214` call `auAnchorFreq` twice per variant). The count is written to the AT-13 probe rows.

   A miss in (a) is the FR-023 stop. Every deviation is reported, and the 10 % is never widened.

**FR-022 ordering:** the tasks put the probe-table commit before the first commit that adds a `ProfundumPresetDef`, and
SC-009 cites both commit hashes.

### 5.8 Descriptor, distance, F (C-7, FR-043, FR-064)

**Components.** For each preset at each note (E1, C3), `PresetDescriptor::x` holds:

| Index | Component | Source | Unit |
|---|---|---|---|
| 0–4 | mid band shape: `bandLevels(window).db[0][b] − 10 log10(Σ_b 10^(db[0][b]/10))` | descriptor window | 3 dB |
| 5–9 | side / mid per band: `db[1][b] − db[0][b]`, clamped to [−40, 0] | descriptor window | 3 dB |
| 10 | `dNlDb`, with its twin and fidelity arm | `auSteady(S(P), note, d)` under the preset hook (P-5) | 3 dB |
| 11 | `dBrOct` | `auSteady(S(P), note, d)` under the preset hook (P-5) | 0.25 oct |
| 12 | `eSubDb − midTotal_steady`, both on the same steady render's Sum tap over `readWindow` (P-5) | `auSteady(S(P), note, d)` under the preset hook; the mid total from `ctx.lastSumL / lastSumR` | 2 dB |
| 13 | t₉₀ in log₂ ms | `auRiseMs(S(P), note)` | 1 |
| 14 | `auP2p(auFrameSeries(sumL, sumR, f0, w.start, w.end, AuFrameDesc::ESub))` | descriptor window | 3 dB |

**Descriptor window.** The held window [1.0 s, 3.0 s), if its mid total is ≥ −60 dBFS. Otherwise [t₉₀, t₉₀ + 2^16) of the
same render. If that window is also below −60 dBFS, the preset fails SC-020.

**Masking.** In a given pair, a band component (0–9) is masked when that band's mid level (components 0–4) or side level
(components 5–9) is below −60 dBFS in either preset. Components 10–12 are masked when the steady window's mid total is
below −60 dBFS in either preset. The masked count is reported.

**Distance:** d(P, Q) = √(Σ over notes and unmasked k of ((x_P,k − x_Q,k)/u_k)²).

**Pilot (G3, `Profundum_PresetDistinctnessPilot`, `[.pilot]`):**
1. **Twins.** (i) The seed twin, at `seed = kDefaultSeed ^ 0x5A5A`, over the seed-independent presets. (ii) The block
   twin, host block 37 against 512, over every preset. t_max is the largest d(P, P_twin) over all twins.
2. **F = max(2.5 · t_max, 1.0)** (P-7).
3. **Near-variant control.** The pilot preset is **"Bedrock Sine"** (R-SINE, §7 row 53), with Mass (104) on endpoint A
   raised by 0.05 normalized. It must read d < F.
4. **Positive control.** Two single-endpoint presets, X₁ from `showcasePair().first` and X₂ from `.second`
   (`std::pair<MorphState, MorphState> showcasePair()`, `bass_morph_state_helpers.h:167`), must read d ≥ F.
   - **Construction.** These are MorphStates, not `ParamSetting` lists, so the C-4 drive is not used. Each control's
     `Comp` is `withEndpoints(writeV2Stream(*fresh), Xᵢ, Xᵢ)` (`profundum_test_fixture.h:1029`, `:1065`). `fresh` is a
     default processor, so every non-endpoint field is the registered default, which satisfies the P-6 pins.
   - **Why not the drive.** Encoding Xᵢ into the 60 E-parameter values would add a MorphState-to-normalized converter
     that nothing else needs, and every reader receives the state through the same `Comp` either way.
   - **Use.** Both controls are test-only, never committed and never part of `allPresets()`.
5. **Record.** Write F, t_max, the twin that set it, the near-variant row and the control row to
   `evidence/distinctness.csv`. If a control fails, stop. F is never lowered.

**Library check, sound part** (`Profundum_PresetDistinctness`, `[long]`): every pair must read d ≥ F. The case reports
the closest pair, its d and its masked count.

**Library check, parameter part** (per push): for every pair, count the registered parameters that differ by ≥ 0.1
normalized, or by list index for stepped ones. The count must be ≥ 3. Each endpoint's 60 E-parameters count separately,
so there are 120 endpoint slots plus 24 non-E slots. The values come from the decoded `Comp`, re-normalized through the
laws.

### 5.9 Determinism (FR-044)

All renders use the 48 kHz E1 sweep timeline.
- **Seed-dependent presets.** Render twice under the stored seed; every sample must agree within `kSampleTolerance`. Then
  render under `stored ^ 0x5A5A`; the render must differ by more than `kSampleTolerance` somewhere.
- **Seed-independent presets.** Render under `stored` and under `stored ^ 0x5A5A`; every sample must agree within
  `kSampleTolerance`.
- A measurement that contradicts the preset's declaration fails and names the flag.

### 5.10 Load under a held note (FR-048) and RT-safe load (FR-037)

**Held load.**
1. A default-state processor holds E1 from sample 0.
2. At L = 24 × 512 = 12288 samples, load the preset by calling `setState(Comp)` between two `process()` calls on the test
   thread.
3. Render to L + 100 ms + 2^12 + 512.

Define `step(a, b) = max over n ∈ [a, b), ch ∈ {L, R} of |y[n] − y[n−1]|`. The gate is
`step(L, L + 20 ms) ≤ 2 · max(step(L − 4096, L), step(L + 100 ms, L + 100 ms + 4096))`, with 0 non-finite samples. The
worst ratio is printed.

**RT-safe load,** for every preset:
1. Prepare a processor at block 512, holding E1.
2. Call `setState(Comp)`, outside the measured region.
3. Run one `process(512)` inside `TestHelpers::AllocationScope`; it must read `getAllocationCount() == 0`.

### 5.11 CPU (FR-045, FR-062): `Profundum_PresetCpu` in `integration/processor_cpu_test.cpp`

The case lives in the existing TU, so it reuses `kTrials = 640`, `ProcessorArm`, `DirectArm`, `timeArm` and the
interleaving (`:66-330`). It also stays in shipping FP mode, because that TU is deliberately outside the `-fno-fast-math`
block.

Each preset has up to four arms:
1. **MONO:** load `Comp`; if the preset stores Poly, write 1 = 0 (Mono) and record the override. Notes E1, B1, E2, G#2
   (MIDI 28, 35, 40, 44) go on at block 0.
2. **POLY4** (Poly presets only): the stored state, with the same four notes.
3. **D:** the same processor's engine, driven through `mutableEngineForTest()` with the same notes.
4. **D':** the D arm, timed a second time.

Each preset runs 640 trials of 10 blocks of 512 at 48 kHz. The arms alternate, the order flips on odd trials, and the
best reading per arm is kept.

- **Control.** best(D) / best(D') must lie in [1/1.05, 1.05]. Otherwise the run is INVALID and is repeated, never read.
- **Bars.** MONO ≤ 8 % and POLY4 ≤ 25 % of the block period.
- **Output.** Every reading, and the maximum, is printed to `evidence/cpu.csv`.
- **Lane.** Run only through `node tools/run-cpu-tests.js profundum_tests`: alone, after an idle, and never right after
  clang-tidy (handoff 13.5).

### 5.12 Loudness (FR-061)

For each preset, the level is L = 10 log10(Σ_b 10^(db[0][b]/10)) of `bandLevels` over the E1 48 kHz descriptor window
(§5.8). The gate is |L − median_L| ≤ 4 dB, and the median and every deviation are printed. A preset outside the band is
re-authored through its sound parameters, because Output Gain is pinned by P-6. The ±4 dB is never widened.

---

## 6. Authoring rules (checked per push by `Profundum_PresetDefsRules`)

From C-6, C-9, P-2…P-6 and FR-047/049:
1. On both endpoints (decoded): amp attack ≤ 1000 ms, attack + decay ≤ 2000 ms, release ≤ 4000 ms, Morph ramp ≤ 2000 ms.
2. Output Gain 1.0, Ceiling −1 dBTP, Bend Range 2 st, Polyphony 4 and Edit Target A (P-6, FR-060, FR-047).
3. An ENV Motion source implies Polarity Unipolar, unless the Comment says "resting offset" (C-9).
4. Never Rectify with Character Tone 1, Fold 100 and WIDE together (C-9).
5. Rectify, Crush or Fracture at Amount ≥ 0.9 with Anchor Level < 0.25, on an active endpoint (§5.6), requires an
   audition-sheet note with the measured change in summed fundamental (C-9).
   - **Per push.** `Profundum_PresetDefsRules` evaluates `fundamentalLossFlagged` (§4.7) on every decoded `Comp`, prints
     the flagged set, and checks the predicate on one constructed flagged state and one unflagged state (non-vacuity). It
     reads no listening sheet.
   - **The measurement** is written by the `[.listen]` case for each flagged preset. It is
     Δ_fund = E(stored) − E(Amount 0), where E is the summed (mid) fundamental `auWindowESubDb(outL, outR, w.start,
     w.length, f0)` (the moved reader, `:464`). It is read on the E1 48 kHz render over the §5.8 descriptor window. The
     reference arm is the same `Comp` with Character Amount set to 0 on both endpoints (`withEndpoints`).
   - **Output.** Δ_fund is written to `evidence/listen/fundamental-loss.csv` (preset, endpoint, algorithm, Amount, Anchor
     Level, E(stored), E(Amount 0), Δ_fund in dB) and as the preset's note in `listening-sheet.md`. The SC-023 script
     check asserts that the sheet holds exactly one Δ_fund note per flagged preset.
6. Names: A and B are both present, ≤ 31 bytes, sanitised, and not "A" / "B".
7. A claim of a single-state column (P-3) requires stored Morph Position 0. A claim of AT-33 requires SPREAD, spread 1
   and depth 1 (P-4).
8. A primary stores every precondition that its row's contrast names (§5.4).
9. `seedDependent` must be true when an active endpoint stores Motion DRIFT, Field Motion DRIFT with depth > 0, or a
   random Cycle Shape (`motion_params.h:42`). FR-044 then verifies the flag by measurement.
10. Exactly one primary per preset; every column is a primary; both AT-13 variants are claimed (P-2).

---

## 7. Matrix-derived library (N = 53; FR-004, C-2.2)

Each column gets one preset, in the sound class that column most naturally drives. The counts are Sub 12, Driven 11,
Harmonic 8, Animated 8, Plucked 7 and Resonant 7, so every category has at least 3.

The **hi values** below are intentions. G4 verifies each one with the showcase arm (FR-012), and a preset that misses is
re-authored, never its bar. Every name passes `isValidPresetName`. "p = 0" marks the P-3 rule.

| # | Column (primary) | Category | Name | Intended sound and lever (hi) | Notable secondaries |
|---|---|---|---|---|---|
| 1 | AT-01 excitation | Harmonic | Edge Ignition | sine recipe at edge 1, excitation mix 1, drive 1 | AT-03 |
| 2 | AT-02 SHAPE | Harmonic | Biting Reed | saw-like recipe, edge 0.5, SHAPE 1 (BITE) | — |
| 3 | AT-03 Fold | Driven | Folded Mass | Fold 100, Anchor 0.6 | — |
| 4 | AT-04 Tube | Driven | Valve Pressure | Tube, Amount 1 | — (AT-26 withdrawn, T027: 0.34 / 0.12 dB) |
| 5 | AT-05 Wavefold | Driven | Folding Wire | Character Fold, Amount 1 | — |
| 6 | AT-06 Rectify | Driven | Rectified Floor | Rectify Amount 1, Tone 0.4 (keeps C-9 rule 4) | — |
| 7 | AT-07 Bite | Driven | Bite Down | Bite Amount 1 | AT-28 |
| 8 | AT-08 Crush | Driven | Crushed Basalt | Crush Amount 1, Anchor ≥ 0.25 | — |
| 9 | AT-09 Fracture | Driven | Fault Line | Fracture Amount 1 on a harmonic-rich core, p = 0 | **AT-13 (Fracture variant)** |
| 10 | AT-10 Anchor level | Sub | Bedrock | thin core, Anchor Sine LOCK, level 1 | — |
| 11 | AT-11 PUNCH | Plucked | Punch Anchor | PUNCH 6 dB / 40 ms, decay 300 ms, p = 0 | — |
| 12 | AT-12 GHOST | Plucked | Ghost Kick | GHOST 100 ms, short decay, p = 0 | — |
| 13 | AT-13 FOLLOW | Driven | Rectified Follow | Rectify Amount 1, Anchor FOLLOW level 1, p = 0 (Rectify variant) | AT-06 |
| 14 | AT-14 Triangle | Sub | Triangle Floor | Anchor Triangle, level 1, attack 20 ms (T027 distinctness) | — |
| 15 | AT-15 Rounded Sine | Sub | Rounded Weight | Anchor Rounded Sine | — |
| 16 | AT-16 Asymmetric | Sub | Lopsided Sub | Anchor Asymmetric Sine | — |
| 17 | AT-17 Octave Down | Sub | Under Octave | Anchor Octave Down, p = 0 | — |
| 18 | AT-18 Fold Feed | Driven | Fed Fold | Fold 100, Anchor Fold Feed 1, SPREAD (keeps C-9 rule 4) | — |
| 19 | AT-19 Drop amount | Plucked | Dropping Stone | Drop 24 st, 200 ms, p = 0 | — |
| 20 | AT-20 Drop time | Plucked | Slow Fall | Drop 24 st, 500 ms, p = 0 | — |
| 21 | AT-21 Drop shape | Plucked | Curved Drop | Drop 24 st, shape 1, p = 0 | — |
| 22 | AT-22 Drop rebound | Plucked | Rebound | Drop 24 st, rebound 1, p = 0 | — |
| 23 | AT-23 Glide | Resonant | Glide Path | MONO legato, portamento 200 ms, p = 0 | AT-44 |
| 24 | AT-24 attack | Resonant | Slow Swell | attack 500 ms, sustain 0.8 | — |
| 25 | AT-25 release | Sub | Long Tail | release 2000 ms | — |
| 26 | AT-26 Mass | Sub | Heavy Mass | Mass 100, Anchor 0.5, sine recipe (T027) | — (AT-27 withdrawn, T027: ≤ 2.05 dB) |
| 27 | AT-27 dynamics gain | Harmonic | Pressure Front | Mass 100 on a B-like harmonic body, sustain 1 (T027) | — |
| 28 | AT-28 transient | Plucked | Hard Attack | transient attack +12 dB | — |
| 29 | AT-29 SPREAD | Harmonic | Wide Harmonics | SPREAD, spread 1, harmonic body | AT-31 |
| 30 | AT-30 WIDE | Driven | Wide Rectifier | Rectify 0.5, WIDE, spread 1, Tone ≤ 0.6 | — |
| 31 | AT-31 Spread | Harmonic | Spread Choir | SPREAD, spread 0.9, Body high | — |
| 32 | AT-32 Gravity | Resonant | Gravity Well | SPREAD, spread 1, Gravity 1 | — |
| 33 | AT-33 Field Motion | Animated | Orbiting Field | SPREAD, spread 1, Field Motion depth 1, p = 0 | — |
| 34 | AT-34 CYCLE → Mass | Animated | Mass Tide | Cycle → Mass, Anchor 0.5, p = 0 | — |
| 35 | AT-35 → Depth | Animated | Breathing Depth | Cycle → Depth, p = 0 | — |
| 36 | AT-36 → Body | Animated | Body Wave | Cycle → Body, p = 0 | — |
| 37 | AT-37 → Edge | Animated | Edge Cycle | saw-like recipe, Cycle → Edge, p = 0 | — |
| 38 | AT-38 → Shift | Animated | Shifting Spectrum | Cycle → Shift, p = 0 | — |
| 39 | AT-39 ENV → Mass | Animated | Envelope Mass | ENV → Mass Unipolar, attack 1000 ms, decay 1000 ms, Mass 0, SOLID, p = 0 (T027) | — |
| 40 | AT-40 DRIFT → Mass | Animated | Drifting Weight | DRIFT → Mass, smoothness 0.5, seedDependent, p = 0 | — |
| 41 | AT-41 Morph 0 → 1 | Harmonic | Morph Span | A: clean sub; B: driven harmonic; stored p = 0 | AT-42 |
| 42 | AT-42 Morph 0 → 0.5 | Resonant | Halfway Morph | A: round, Mass 20; B: hollow-bright, Mass 90 (T027); stored p = 0.5 | — |
| 43 | AT-43 AlwaysRetrigger | Resonant | Restrike | MONO, Retrigger Reset, AlwaysRetrigger, p = 0 | — |
| 44 | AT-44 Legato | Resonant | Legato Line | MONO, Reset, Legato, p = 0 | — |
| 45 | AT-45 EnvRetriggerOnly | Harmonic | Envelope Restart | MONO, Reset, EnvelopeRetriggerOnly, p = 0 | — |
| 46 | AT-46 OscContinuous | Sub | Continuous Phase | MONO, Reset, OscillatorContinuous, p = 0 | — |
| 47 | AT-47 Weight | Sub | Weight Lever | stored Weight 0.6 | — |
| 48 | AT-48 Dark ↔ Bright | Harmonic | Dark To Bright | B-like harmonic body, macro stored at 0.5 | — |
| 49 | AT-49 Tight ↔ Loose | Resonant | Loose Ends | sustain-1 body, macro stored at 0.5 | — |
| 50 | AT-50 Clean ↔ Violent | Driven | Clean To Violent | macro stored at 0.4 | — |
| 51 | R-SOLID | Sub | Mono Bedrock | SOLID on both endpoints, Tube 0.4 | — |
| 52 | R-LOCK | Sub | Locked Floor | LOCK on both endpoints, Anchor Triangle, SPREAD, p = 0 | AT-14 |
| 53 | R-SINE | Sub | Bedrock Sine | Sine on both endpoints, harmonic body, WIDE; **pilot preset** (§5.8) | — |

Presets 41, 42 and 48 (the performance columns) author a distinct endpoint B. The others copy A, or vary B gently so
that Morph is interesting. Every A / B name is authored together with its row (C-9). Distinctness risk is concentrated in
the Sub category (§12 R-6).

---

## 8. Test plan (FR / SC → TU, TEST_CASE, assertion)

Every case carries `[profundum]`. The new TUs are listed in `plugins/profundum/tests/CMakeLists.txt` and in its
`-fno-fast-math` block (for bit-pattern finiteness). `processor_cpu_test.cpp` is the exception and stays out of that block.

| SC | TU | TEST_CASE (tags) | Assertion strategy |
|---|---|---|---|
| SC-001 | `unit/preset/preset_library_test.cpp` | `Profundum_PresetCategoriesMatchFilesystem` | The config list equals the six names in order. The directory set equals the list, both ways, with 0 stray files (only `*.vstpreset`). The C-1 name rule (case-insensitive) is checked against every registered title and list string (`ParameterInfo` of the 84 IDs from a `ControllerFixture`), the 11 group names, and the leading words; the continuum words are exempt from the leading-word clause only |
| SC-002 | same | `Profundum_FactoryPresetContainer` | Parse the header, List, `Comp` and `Info`. The class ID equals `kProcessorUID`. Parse the Info XML and compare each attribute with an **independent** expectation, never with `buildProfundumInfoXml` (the writer under test): `MediaType` == `"VstPreset"`; `PlugInName` == `"Profundum"`; `PlugInCategory` == `"Synth"`; `Name` == the file stem; `MusicalCategory` == `MusicalInstrument` == `path.parent_path().filename()`. `Comment` is non-empty and contains every word of the primary column's label from `capabilityTable()` after its "AT-nn" / "R-" prefix (FR-003: it names its primary in words), compared case-insensitively. Exactly 7 attributes |
| SC-003 | same | `Profundum_FactoryLibraryShape` | 6 categories, each ≥ 3; N ≥ 53; unique names; `isValidPresetName` (red until G4) |
| SC-004 | same | `Profundum_FactoryPresetStateFormat` | Version 3, 574 B, 0 non-finite floats. Edit Target 0. Runtime target and ramped p within 2.5e-4 of 105. Seed equals the def |
| SC-005 | same | `Profundum_FactoryPresetRoundTrip` | `PresetHost` load → save → `decodedEqual(tol 2.5e-4)`; names byte-equal and not the defaults |
| SC-006 | same | `Profundum_CoverageMatrixComplete` | 53/53 columns have ≥ 1 primary; 1 primary per preset; both AT-13 variants claimed (P-2) |
| SC-006 | same | `Profundum_PresetDefsRules` | §6 rules 1–10, on the definitions and the decoded `Comp` |
| SC-007 | `integration/preset_showcase_test.cpp` | `Profundum_PresetShowcase_<k>of8` (`[long][profundum-showcase]`), k = 0…7 | `showcaseColumn` for every claim of the shard's presets. Every cell ≥ its bar; every fidelity arm within tolerance; 0 primary overrides. Writes `evidence/coverage-matrix/shard-<k>.csv`, merged by `merge-csv.js` |
| SC-008 | same | `Profundum_ReferenceColumns` (per push) | The three reference presets decode as LOCK / Sine / SOLID on both endpoints. The §5.4 reverse arms at E1 and C3 clear their bars. On its own per-push short render (§5.6), R-SOLID reads L == R bitwise from s₀ = 544 samples |
| SC-009 | `integration/preset_probe_test.cpp` | `Profundum_PresetAudibilityProbe_<Group>` (`[.probe]`) ×11 | §5.7 gates (a)–(c); 102 rows after the merge |
| SC-010 | build lane + `unit/preset/preset_library_test.cpp` | `Profundum_GeneratorSmoke` | `writeFactoryTree(tempDir)` returns true and writes N files. The build log shows 0 warnings and no `vstgui` / `Catch2` on the generator's link line. `./build/.../profundum_preset_generator <dir>` exits 0 |
| SC-011 | tool | — | `node tools/check-preset-generator-determinism.js --plugin profundum` exits 0 |
| SC-012 | `unit/preset/preset_library_test.cpp` | `Profundum_CommittedPresetTreeMatches` | `writeFactoryTree(temp)` against the committed tree: same paths, `Info` byte-equal, `decodedEqual(2.5e-4)` |
| SC-013 | `integration/preset_browser_test.cpp` | `Profundum_PresetBrowserLoad` | `ConnectedPair` + `RecordingComponentHandler`. For each preset, `presetManagerForTest()->loadPreset(PresetInfo{path, …})`. The processor's `getState` decode equals a fresh `setState(Comp)` decode, with 0 mismatches; the seed, names and runtime block are named fields. `restartFlags` contains `kParamValuesChanged`, and `toProcessor.count(kMsgLoadPresetState) == 1` |
| SC-013 (FR-031 state provider) | `integration/preset_browser_test.cpp` | `Profundum_PresetBrowserSave` | A `ConnectedPair` whose component handler is `PresetHostComponentHandler` (anonymous namespace of the TU). It is an `IComponentHandler` whose `queryInterface(IComponent::iid)` returns `fx.proc` with `addRef`, as a host's handler does, so the copied `createComponentStateStream()` (`FUnknownPtr<IComponent>(getComponentHandler())`, Vorago `controller.cpp:152-168`) reaches the processor. The case loads one preset, calls `presetManagerForTest()->savePreset("Profundum Save Probe <pid>", "Sub")` and reads the written file. Its `Comp` chunk decodes as version 3, 574 B, and `decodedEqual(…, tol 0)` holds against the decode of `fx.proc->getState`. An RAII guard deletes the file through `deletePreset`. **Non-vacuity:** with a plain `RecordingComponentHandler` (no `IComponent`), `savePreset` returns false. ODR sweep: `PresetHostComponentHandler` and `Profundum_PresetBrowserSave` return 0 hits |
| SC-013 | `unit/controller/editor_layout_test.cpp` | `Profundum_UiLayout` (extended) | Exactly one `OutlineBrowserButton` in `header`, at (452, 24, 96, 24), overlapping no D1 rect, at every zoom step. No name display and no previous / next controls. `kSessionTags.size() == 14` |
| SC-013 | existing | `Profundum_EditorLifecycle` | Stays green with the overlay added and removed |
| SC-014 | `integration/preset_browser_test.cpp` | `Profundum_FactoryPresetScan`; `Profundum_InstalledPresetTree` (`[.install]`, Windows) | Override scan: N presets, 0 empty subcategories. Installed: `getFactoryPresetDirectory()` equals `%PROGRAMDATA%\Krate Audio\Profundum`, and a scan with no override lists N presets under the six categories. `setup.iss:68` ships `presets\*`, and the release job copies in the generated tree (`release.yml:229-230`) |
| SC-015 | `integration/preset_rt_test.cpp` | `Profundum_FactoryPresetLoadRtSafe` | §5.10 RT-safe load, every preset, 0 allocations |
| SC-016 | `integration/preset_sweep_test.cpp` | `Profundum_PresetSweep` (`[long]`) | §5.5: finite, held RMS, tail RMS; the chord for Poly presets (note list asserted) |
| SC-017 | same | `Profundum_PresetSweepRates` (`[long]`) | §5.5 at 44.1 / 96 kHz, E1; worst true-peak margin printed |
| SC-018 | same | `Profundum_PresetTruePeakMaxMass` (`[long]`) | §5.5 max Mass, E1 and C3 |
| SC-019 | same | `Profundum_PresetMonoSum` (`[long]`) | §5.6; ungated count 0; the R = −L control fails |
| SC-020 | same | `Profundum_PresetDistinctness`: parameter part per push as `Profundum_PresetDistinctnessParams`, sound part `[long]`; `Profundum_PresetDistinctnessPilot` (`[.pilot]`) | §5.8 |
| SC-021 | same | `Profundum_PresetDeterminism` (`[long]`) | §5.9 |
| SC-022 / 032 | `integration/processor_cpu_test.cpp` | `Profundum_PresetCpu` (`[.perf][performance]`) | §5.11; note list printed and asserted |
| SC-023 | script | — | `evidence/listen/render.sh` exits 0; manifest lines = 2N + gestures; the sheet has N empty verdicts |
| SC-024 | `unit/manual_coverage_test.cpp` | `Profundum_ManualCoversSurface` | Read `MANUAL.md` (path through `PROFUNDUM_RESOURCES_DIR/..`). Every registered title except IDs 4–7 appears. `docs/index.html` exists and links the manual |
| SC-025 | release flow | — | The `release-readiness` matrix for `profundum` is green |
| SC-026 | tools | — | check-portability, lint-apple-globals and clang-tidy (including the generator by file list, P-9); 0 warnings |
| SC-027 | `integration/preset_rt_test.cpp` | `Profundum_PresetRenderGuard` (per push) | §5.5 short span, stored and at max Mass. The wall clock is printed and must be ≤ ~15 s; if measured above that, the case is split by preset index (`_0of4` to `_3of4` since 2026-10-25; R-11) |
| SC-028 | same | `Profundum_PresetLoadUnderHeldNote` (per push) | §5.10 held load; worst ratio printed |
| SC-029 | move gate | — | §5.1 |
| SC-030 | `unit/preset/preset_library_test.cpp` | `Profundum_FactoryPresetCeiling` | The decoded ceiling is −1 dBTP for 100 % of presets |
| SC-031 | `integration/preset_sweep_test.cpp` | `Profundum_PresetLoudnessMatch` (`[long]`) | §5.12 |

**Audition (FR-050).** `evidence/listen/render.sh` is copied from the Phase 12 `render.sh`. It builds `profundum_tests`
with `/nr:false` and runs the hidden case `Profundum_PresetAuditionRenders` (`[.listen]`, in `preset_sweep_test.cpp`).
That case writes `<category>/<name>_{E1,C3}.wav`, the gesture WAVs (`_morph`, `_macro`, `_legato`) and `manifest.txt`. It
also writes `listening-sheet.md`, with one empty `Verdict (user):` line per preset and the C-9 rule-5 fundamental-loss
notes, measured as §6 rule 5 defines (Δ_fund, also in `evidence/listen/fundamental-loss.csv`).

---

## 9. Build integration

| File | Change |
|---|---|
| root `CMakeLists.txt` | the §4.5 block, after `:743` |
| `plugins/profundum/tests/CMakeLists.txt` | Add `unit/preset/preset_library_test.cpp`, `unit/manual_coverage_test.cpp`, `integration/preset_browser_test.cpp`, `integration/preset_rt_test.cpp`, `integration/preset_probe_test.cpp`, `integration/preset_showcase_test.cpp` and `integration/preset_sweep_test.cpp`, and the same list in the `-fno-fast-math` block. `target_include_directories` += `${CMAKE_SOURCE_DIR}/tools`. `target_compile_definitions` += `KRATE_DSP_TESTS_DIR="${CMAKE_SOURCE_DIR}/dsp/tests"` (the same value as `dsp/tests/CMakeLists.txt:909-911`) and `PROFUNDUM_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/presets"` |
| `dsp/tests/CMakeLists.txt` | none |
| `tools/ci-test-shard.js:106-108` | The long lane gains `profundum_tests: 4`; Catch2 `--shard-count` spreads the eight showcase cases and the sweep cases. The per-push lane is unchanged unless SC-027's measured wall clock needs `profundum_tests: 2`. `tools/ci-test-times.json` gains the measured seconds |
| `tools/check-preset-generator-determinism.js` | as §4.8 |
| `.github/workflows/release.yml` | none: `:155-186` already maps `profundum` → `profundum_preset_generator` |

**Targets each gate builds** (one batched invocation, `-- /nr:false`):
- G0: `dsp_systems_tests`.
- G1 onward: `Profundum profundum_tests profundum_preset_generator`.

**Run scope** (root `CLAUDE.md`: run only the tests the change set can reach):
- **Modified shared files** are the Phase 9 TU and its new header. Their consumers are `dsp_systems_tests` (the moved
  cases only, FR-058) and `profundum_tests`.
- **Modified plugin sources** are `processor.cpp` (the `notify` branch), `controller.cpp`, the panel sub-controller, the
  uidesc and the preset config. They reach `profundum_tests` (the full per-push lane) and pluginval.
- No `dsp/include` header changes, so no other plugin suite runs.

---

## 10. Cost model and concurrency

Measured anchors: the Phase 9 table took about 1 700 s of engine-direct wall in total on the close lane (the sum of the
11 shard logs; Motion alone was 884 s). The processor path costs ≤ 1.05× engine-direct (the Phase 10 wrapper ratio, from
the roadmap status).

| Run | Work | Plan |
|---|---|---|
| G0 move gate | 16 cases × 2 runs | 16 concurrent processes per run; ≈ 15 min wall each (bound by Motion) |
| G2 probe | 11 groups × (processor + live) ≈ 2 × 1 700 s | 11 concurrent processes; Motion ≈ 30 min wall; the others < 10 min |
| G4 showcase | ≈ 53 primaries + ≈ 40 secondaries, at ≈ 34 s per column claim (the Phase 9 mean) × 1.5 for the fidelity arms ≈ 80 min CPU | 8 shards concurrently: ≈ 10–15 min wall locally; 4 job shards nightly |
| G4 sweep set | ≈ 140 s of audio per preset (sweep, rates, max Mass, twins, steady descriptor renders) × 53 ≈ 2 h of audio ≈ 8–10 min CPU | one process; the sweep cases share the render cache |
| Per push | render guard ≈ 80 s of audio, the load tests, reference arms ≈ 55 s of audio, the library checks | ≈ +30 s on the `profundum_tests` per-push lane; SC-027 records its own wall clock |
| CPU | 53 presets × 2–3 arms × 640 trials × 10 blocks | alone, after an idle; ≈ 15 min |

After each run, its wall clock is logged into `evidence/run-log/` and compared with this table (root `CLAUDE.md`:
optimise every run). Raw traces are cleaned with `node tools/clean-test-output.js --apply` at close.

---

## 11. Execution order (gated)

```
A   Spec rows       after the user confirms P-1/P-10, P-2, P-5, P-6, P-11 and the MONO arm: write each §2.0 amendment
                    into its spec row, then Clarifications; each lands before the gate named in §2.0
G0  FR-057 move     baseline run -> move (edits 1-4, lifts L1-L11) -> move-check.js -> path set (2b) -> rebuild
                    -> post run -> compare                                                               (SC-029)
                    STOP on any difference
G1  Infrastructure  categories + dirs; defs header (Capability, EMPTY allPresets); host + decode; generator + CMake;
                    browser (message, controller, sub-controller, uidesc via XSLT); processor setEngineSeamsForTest
                    (P-11); render support (renderer, hook adapter, endpoint guard, showcase arms, descriptor); every
                    TU registered. Per-push cases are red only where the library is empty
G2  Probe           run the 11 [.probe] groups concurrently -> commit probe-table.csv                    (SC-009)
                    FR-023 STOP on a miss
G3  Pilot           author rows 51-53 (+ the two showcasePair control presets as test-only defs)
                    -> F calibration -> distinctness.csv                                                 (SC-020)
                    STOP if a control fails
G4  Library         author rows 1-50 -> generate_profundum_presets -> per-push + [long] (8 showcase shards, sweep set)
                    concurrently -> re-author until green
G5  Close           audition renders; MANUAL / docs / CHANGELOG / SKILL / determinism script; check-portability,
                    lint-apple-globals, clang-tidy (incl. the generator by file list); pluginval 5; the CPU lane alone
                    after an idle; release-readiness for profundum
```

---

## 12. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | `inline` functions in a header change inlining under `/fp:fast` and shift a printed value (memory: COMDAT shifts of 1e-4), so the G0 gate fails | The header is included by only one TU per executable (in `dsp_systems_tests`, only the Phase 9 TU). The CSVs print at `setprecision(6)` (`:640`). Any difference stops G0 and is surfaced, never tolerated |
| R-2 | The Phase 11 FR-074 engine fixes moved some Phase 9 values, so a row fails the probe's 10 % fidelity | The live engine-direct value is reported beside the CSV value. A row off by more than 10 % is a G2 stop; the bar is the CSV's and is not re-derived |
| R-3 | The processor applies parameters per block, so mid-render carrier events would shift by up to one block | Blocks are split at carrier samples (§5.3 step 5) |
| R-4 | Observer taps for 8 voices × 6.5 s are large | `tappedVoices` follows the script (Phase 9 uses 0, except voice 0 in Pitch). The sweep uses the slim `PresetSweepObserver`, and the render cache keeps readings, not audio |
| R-5 | Decaying (Plucked) presets fail the steady descriptors and the mono-sum windows | The §5.6 / §5.8 fallback windows. Plucked primaries are onset-window columns (§7). An invalid read fails and is never skipped |
| R-6 | Sub presets sit too close in sound space (d < F) | The twelve Sub presets deliberately differ in Anchor waveform, image mode, body and Mass (§7). A failing pair is re-authored (FR-064) |
| R-7 | Loudness spread between the Sub and Driven classes exceeds ±4 dB | The limiter caps only peaks, so authoring balances Anchor level and Mass. The median and deviations are printed on every run |
| R-8 | Denormals in long tails | `ScopedDenormalMode` is the first statement of `process()` (`processor.cpp:376`). The probe and renders call `enableFTZDAZ()` |
| R-9 | Portability: `thread_local` in an inline function, `std::filesystem` in tests, designated initializers, `getenv` | All standard C++20. No `std::isnan` (bit patterns, §5.5) and no new SIMD. `check-portability.js` and `lint-apple-globals.js` run on every new TU, because an MSVC-green build proves nothing |
| R-10 | A host drops controller → processor messages and refuses the load message | The load provider returns false and the browser reports the failure, with no partial mirror update. Copy and Compare already depend on messages |
| R-11 | A per-push case exceeds ~15 s | SC-027 records its wall clock and splits itself by preset index: `_0of2` / `_1of2` first (T027), `_0of4` to `_3of4` since 2026-10-25 (two shards read 14.34 / 13.79 s alone, 16.02 / 15.68 s under the close lanes). The bar stays 15 s. It is never moved to `[long]` (C-10) |
| R-12 | The CPU lane reads slow after clang-tidy or under load | It runs alone, after an idle, through `run-cpu-tests.js`. An INVALID control run is repeated, never read, and budgets are never relaxed |
| R-13 | **AT-13 at G2.** The four gated AT-13 cells (Fracture and Rectify, at E1 and C3) need `anchorPhaseTap` on the processor's engine. Without it, the reader measures a per-chunk staircase silently (P-11). The seam also changes how the Anchor renders (per sample, `profundum_voice.h:881-885`), so the processor and the engine-direct runs must both carry it | P-11 carries the seam through `setEngineSeamsForTest`, and §5.7 gate (c) asserts that it was set on those four rows. If P-11 is refused, AT-13 is a G2 FR-023 stop: the column is surfaced with its measured value and no AT-13 preset is authored until the user rules |
| R-14 | A P-10 lift changes a Phase 9 value (evaluation order, a captured variable bound differently) | The FR-058 gate compares byte-identical CSVs and the printed values; any difference stops G0. The lifts' diffs are in `evidence/move-gate/lifts.diff` for review |

---

## 13. Open questions for the user (before the dependent task)

1. **P-1** (before G0): approve the one redirected alias line in the moved code (the render seam), or require a
   byte-verbatim move and accept a per-row re-port.
2. **P-2** (before G4): AT-13 counts as satisfied when both variants are claimed, each by the preset that stores that
   algorithm (Rectify as a primary, Fracture as a secondary), with no algorithm override.
3. **P-3 / P-4** (before G4): presets that claim single-state columns store Morph Position 0, and the AT-33 claimant stores
   SPREAD, spread 1 and depth 1.
4. **P-5** (before G3): the three Phase 8 steady components of the C-7 descriptor are read on an `auSteady` held render,
   not on the 2 s sweep window.
5. **P-6** (before G3): Output Gain, Bend Range and Polyphony are pinned to their defaults in every factory preset.
6. **P-7** (before G3): F = max(2.5 · t_max, 1.0), with the stated units.
7. **CPU** (before G5): on Poly presets, the MONO bar is measured with Voice Mode forced to Mono (recorded), plus the
   POLY4 arm (§5.11). This amends FR-045 and SC-022 (§2.0).
8. **P-10** (before G0): approve the eleven named-function lifts L1–L11 (route (b)), so that the showcase calls the same
   row functions Phase 9 calls, proven by the FR-058 gate. The alternative is route (a): Phase 13 glue functions with
   equivalence arms, under an amended FR-057 that allows them.
9. **P-11** (before G1): approve the processor test seam `setEngineSeamsForTest`, a plugin runtime edit at
   `processor.cpp:276`, so the AT-13 reader gets its `anchorPhaseTap` seam on the processor path.
10. **§2.0** (before each named gate): approve the amended row texts for FR-057 / SC-029, FR-012 / SC-007 / SC-006,
    C-7 / SC-020, C-9 / FR-007, FR-045 / SC-022 and SC-009.

---

## Review notes

- **Issue "AT-13 reader cannot run on the processor path" (blocker):** accepted and resolved by P-11 (§2, §5.2, §5.3,
  §12 R-13). One correction to the stated failure mode: the `REQUIRE(to <= ph.size())` at `:860` would **not** fire. The
  tap vector is allocated whenever the voice is tapped, seam or not (`profundum_voice_engine_test_helpers.h:219-224`).
  Without the seam, the voice fills it with one phase value per chunk (`profundum_voice.h:887-888`). The defect is
  therefore a silent mis-measurement, not a test failure. That is worse, and it is why §5.7 gate (c) asserts the seam
  explicitly.
- **Issue "Glue rows vs FR-057" (major):** resolved by route (b) (P-10). The §5.7(c) glue-equivalence arm is removed
  because no glue remains, and gate (c) now checks the AT-13 seam. The endpoint assertion the issue asks for is §5.3
  step 1a.
- **Issue "rulings contradict ratified rows" (major):** resolved by §2.0. The plan cannot ratify spec text itself. Each
  amendment is proposed verbatim and lands in its row (task A) before its gate, after the user confirms it.
