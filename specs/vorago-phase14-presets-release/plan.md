# Implementation Plan: Vorago Phase 14 — Factory Presets & Release Readiness (second pass)

**Spec:** `specs/vorago-phase14-presets-release/spec.md` (second specify pass, reviewed; Clarifications
sessions 2026-09-27 Q1–Q8 and 2026-09-29 Q1–Q8, plan-stage rulings R-1…R-8)
**Roadmap:** `specs/Vorago-roadmap.md` Part B, Phase 14 (lines 615–645, the variety paragraph and the Q2
ruling), with the Phase 13b hand-off (lines 598–610)
**Branch:** `feat/vorago-phase1-events-modulation` (one branch per roadmap)
**Date:** 2026-09-29 (first-pass plan 2026-09-27, superseded by this document)
**Status:** PLAN — nothing below is implemented except the first-pass artifacts the spec keeps (§0).
One gate remains: **G2**, the pilot's ruling of the take count K (FR-017a). G1 (roster ratification,
FR-071) is discharged: `R` = {`syncRate`, `selfAffinity`} (spec Clarifications 2026-09-29, Q1).
§3 lists seven findings from this session's reading. P2-1 predicts that G2 will stop, and the
execution order is built around that prediction.

Every file:line below was read in this planning session on HEAD `339cd501`. Anything taken from the
spec says so.

---

## 0. What changed from the first-pass plan

| First-pass plan (2026-09-27) | This plan | Why |
|---|---|---|
| Stage A (probe TU, part 0, inert friends) was pending | **Done and kept**: `ecosystem_rule_probe_test.cpp` (registered at `plugins/vorago/tests/CMakeLists.txt:52`, fast-math exemption `:137`), `preset_test_support.h` part 0, `detail::VoragoEcosystemRuleProbe` (`vorago_voice.h:171`, `:1568`; `vorago_engine.h:1304`) | spec status line; roadmap line 620 |
| Roster `R` open, 4–6 knobs, `.lo`/`.hi` pairs | `R` = {`syncRate`, `selfAffinity`}; cells `E6.hi`, `E7.hi` only; IDs 901, 902; `kStateV3Bytes = 436`; `kFieldCount = 33` | Clarifications 2026-09-29 Q1, Q5, Q7 |
| `F = max(4.0, 2·t_max)`, raised if the near-variant pair reads ≥ F | **F = 4.0 fixed.** `D(P)` is a K-take mean; K is ruled at the pilot as the smallest K ≤ 8 with `2·t_K ≤ F`; a near-variant pair at `d ≥ F` is a stop, never a raise | Clarifications 2026-09-29 Q2, Q3, Q6 |
| `s(P)` from one take | `s(P)` from the K-take-averaged `M1`, `M2`, `M3`; the long-render arms run on every take; ablation and E twins stay single-take at the stored seed | Q4 |
| D10.1 scored on the S8 preset's **only** render (the gesture render) | The S8 preset keeps its **ungestured** render for everything, and adds a **gesture render** plus a **dry-residue twin** (`kSpaceMixId` → 0) | spec C-6 arm 4, "Render identity" |
| Envelope primaries (D8.2, D9.1) scored on `Sus` by reversion | Scored by the **attack-window reversion**, with the `d_Sus + D_abl` attributability conjunct | spec C-7.4 |
| D1 material credited when it is Material A or B | Also needs **blend weight ≥ 0.35** for that material | spec C-2.1 D1 |
| S5 ablation: the three tone levels only | Also `kSubLevelOffsetId` 600 → 0.0 | spec C-2.1 S5 |
| `getTailSamples()` ghost term = 12 s (the configured grain), `ceil` | `G` = `AtmosphereEngine::kMaxGrainSeconds` = 30 s, `llround`, ±1 sample | spec FR-060 |
| Cost anchored on an estimate (0.15× real time) | Anchored on the **measured** probe wall clock: 28 renders × 340 s in 17 min 45 s = 0.112× real time (`compliance.md`, Run 1); Phase 13b recorded CPU at 89 % of before (roadmap line 609), so 0.10× is used | measured |
| N = 38 + \|E-ext\| (open) | **N = 40** | Clarifications 2026-09-29 Q5 |

Everything else in the first-pass plan that the spec did not change is carried forward: the host
design, the generator, the record/shard architecture (R-4), the exact skip rules, the non-subset
rule, the committed-tree tolerance procedure, the CPU arm (R-6), and the registration staging (R-7,
now FR-027a).

---

## 1. What was read (this session)

| Area | Files / lines |
|---|---|
| Roadmap | `specs/Vorago-roadmap.md:1-160` (philosophy, reuse inventory, Seraphis status) and `:519-713` (Part B, the 13b status block, Phase 14, dependency graph, cross-cutting constraints, OQs) |
| Spec | `spec.md` in full (1500 lines: changes block, findings 1–5, C-1…C-11, FR-001…FR-076, SC-001…SC-032, edge cases, OQ-1…OQ-11, all clarification sessions, both review-note blocks) |
| First-pass artifacts | `plan.md` (whole first-pass file, superseded here), `compliance.md:1-40` (probe Run 1 and its wall clock), `specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log:13,33-34,43,48,52,75` |
| DSP | `ecosystem_engine.h:168,230-240,280-290,336-420,455-470,588-606,695-730,2370-2415`; `vorago_voice.h:160-176,316-345,385-396,531,1430-1475,1540-1575,1676-1692,1762,2516`; `vorago_engine.h:140-195,225,255,370,845-885,1150-1172,1280,1297,1303-1305,1346,1761`; `vorago_macro_matrix.h:106,180,201,217,970-1015,1125-1140,1206-1207`; `atmosphere_engine.h:311,853-858`; `cavern_verb.h:255,626-627,735`; `aether_reverb.h:405-412` |
| Plugin | `plugin_ids.h:15-244`; `parameters/ecosystem_params.h` (whole file); `param_routes.h:36,63-86,115,126,139,198,205,255-300`; `macro_params.h:30-42`; `events_params.h:1-14`; `global_params.h:17,42-43,70-76,111,168,202-220`; `body_params.h:36-60`; `noise_params.h:44,79-92`; `sub_params.h:36-40`; `resonance_params.h:55-72`; `ghost_params.h:37-40`; `life_params.h:32-34`; `envelope_params.h:60-66`; `space_params.h:40-59,105`; `param_mapping.h:29-62,107-133`; `preset/vorago_preset_config.h` (whole file); `processor/processor.h:50-125,175-240`; `processor/processor.cpp:9-40,560-665,790-865,1004-1035`; `controller/controller.cpp:103,136-240`; `resources/editor.uidesc:110-111,383-412`; `CMakeLists.txt:20-46,76`; `version.json`; `CHANGELOG.md:1-20` |
| Plugin tests | `tests/CMakeLists.txt` (whole file); `preset_test_support.h` (whole file); `integration/ecosystem_rule_probe_test.cpp:1-470`; `vorago_test_fixture.h:36-49,166-210`; `vstgui_test_stubs.cpp` (whole file); `integration/soak_test.cpp:44-45,77`; `integration/processor_cpu_test.cpp:84-85,163`; `integration/automation_rt_test.cpp:9-15,115-121`; `unit/controller/editor_layout_test.cpp:9-10,109,252-256,830-845,873,909,922,1008,1127,1305-1348`; `dsp/tests/unit/systems/vorago_param_surface_test.cpp:830,1086-1096`; TEST_CASE lists of `state_v2_test.cpp`, `state_roundtrip_test.cpp`, `params/ecosystem_params_test.cpp`, `param_table_test.cpp` |
| Test helpers | `vst_param_changes.h:78,110`; `vst_event_list.h:36,63,77`; `render_fingerprint.h:27-28,58-63,73,104-108,120-130`; `vorago_fixtures.h:101,245,316,344,441,502`; `low_frequency_metrics.h:478`; `allocation_detector.h:105-190` (Catch2 include: none in any of these) |
| Shared | `preset_manager_config.h:15-30`; `preset_manager.h:55-62,70,75,120`; `preset_manager.cpp:260-280` |
| Seraphis precedent | root `CMakeLists.txt:590-672`; `tools/seraphis_preset_generator.cpp` (function outline, `:77-126,224-349`); `tools/seraphis_preset_defs.h:50-70` |
| Tooling / CI | `.github/workflows/release.yml:145-182`; `ci.yml` FILTER/timeout lines `:32,75-77,114-115,322,365-376,608,651-660,1069,1112-1121`; `long-tests-nightly.yml:1-90`; `tools/check-preset-generator-determinism.js` (seraphis sites `:7,53-60,111-113,209`); `tools/run-cpu-tests.js:55,92-93`; `.claude/workflows/release-readiness.js:10-24`; `.claude/skills/release/SKILL.md:10-32` |
| ODR sweep | `grep -rn -E "(class|struct|enum class|enum) <Name>\b" dsp/ plugins/ tools/ tests/` for every new name in §5 (results in §5 per name) |
| Review revision (same day) | `atmosphere_ghost_test.cpp:498-590`, `:1155-1176`, `:3175-3250`; `atmosphere_ghost_fixtures.h:378-392`; `atmosphere_engine.h:312-313`, `:582`, `:1083-1092`, `:1159`, `:1235`, `:2385-2396`; `vorago_engine.h:355-376`, `:1004-1011`, `:1515-1537`, and a `setDensity` grep of the Vorago engine/voice/plugin sources (one ghost writer, `:369`); `preset_manager.cpp:227-277`; `preset_manager.h:55-61`; `vorago_preset_config.h:20-29`; `space_params.h:14-18`, `:55`, `:177`, `:276`; `processor.cpp:964-991`; `plugin_ids.h:94-105`; `param_mapping.h:107-118`; `git log` for the phase base; spec FR-003, FR-010–FR-013, FR-037, FR-061, FR-076, C-2.1 Group D, SC-011, SC-024, SC-029 |

---

## 2. Execution order (gated)

```
A  DONE (first pass)     probe TU, preset_test_support.h part 0, inert rule-probe friends
C1 Registration + host   FR-027a's ONE mid-phase registration task: every new TU (as a skeleton), the
                         generator targets; PresetHost WITHOUT buildPresetComponentState (FR-021);
                         the constants, take-set, render/capture, env and pool parts of
                         preset_test_support.h (no decode, no timeline, no defs); the pilot TU
E0 EARLY WARNING         default-surface t_K curve: 16 takes of the default surface, K = 1, 2, 4, 8
                         (the pilot's first preset, §6.16), on the hard-coded default-surface
                         timeline of §6.16 step 1. ~9 min local.
   -- if 2·t_8(default) > F = 4.0: FR-017a STOP now, surface the curve (P2-1) --
B  Surface (FR-071a..074, SC-026a/027)  dsp R-1 append; IDs 901/902; state v3; controller; uidesc;
                         every count-bearing test; the roster probe friend
   + getTailSamples (FR-060)   independent of the pilot; touches processor.h/.cpp once with B
C2 Preset infrastructure categories (FR-001), defs header + Capability enum, buildPresetComponentState,
                         decodePresetState + makeTimeline (need B's v3 loader and tail_estimate.h),
                         generator, per-push harness
                         (FR-018..035, 039, 040), LibraryShape (red until F)
D  Sweep harness         records, sharding, pool, K-take main render, arms, gesture render, ablation /
                         E / attack-window twins, verification vector
E  Pilot (FR-017a)       the rest of the pilot set (6 presets + near-variant) through D; t_K curve per
                         pilot preset; showcase scores for the E6.hi / E7.hi candidates
   -- G2: rule K (smallest K <= 8 with 2·t_K <= 4.0 on every pilot preset); near-variant d < 4.0;
          each E-ext candidate at the primary bar. Any failure: FR-017 STOP and surface --
F  Library               matrix -> primaries -> secondaries -> categories -> author -> sweep loop
G  Deferred + docs       FR-061 enforcing case (§8) + record, FR-062 docs/index.html, plugins/vorago/CLAUDE.md
H  Tooling + CI          determinism --plugin, rosters, run-cpu-tests filter, nightly sweep jobs,
                         ci.yml filter; measure SC-013 on the runners
I  Release gate          1.0.0 + CHANGELOG, release-readiness, auval evidence, FR-042 listening,
                         FR-026/FR-027 install-path verification
```

**Why E0 comes before B, and exactly what it uses.** The whole harness is sized around K (§10), and
P2-1 predicts that K = 8 is marginal at the default surface. E0 references nothing that B or C2
creates. It uses exactly:
- `PresetHost::prepare`, `process`, `outL` and `outR` (§5.6), with **no** `loadState`: a freshly
  initialized processor is the default surface;
- `takeSeedIndex`, `seedNormalized` and `renderPreset` with an empty `comp` (no `setState`) and
  `seedIndex` set (§5.8);
- the shipped part-0 `describe`, `meanOf` and `descriptorDistance` (R-8);
- the hard-coded default-surface timeline of §6.16 step 1 (A = 155 s, M1–M3 over [160, 340] s,
  render end 340 s = H, so no NoteOff and no tail).

It does **not** use `decodePresetState` (it needs B's `loadEcosystemParamsV3Ext` and
`kStateV3Bytes`), `makeTimeline` (it needs `effectiveCavernDecaySeconds` from `tail_estimate.h`, a B
item, and a decode), or `buildPresetComponentState` (it needs `VoragoPresetDef` from
`tools/vorago_preset_defs.h`, a C2 item). All three are built in C2, after B. Running E0 first turns a
likely late stop into an early one. It is the
first preset of the FR-017a pilot, not an extra measurement, and it follows FR-027a because its TU is
registered in C1's single registration task.

---

## 3. Findings (this session) and the status of the plan-stage rulings

**P2-1 — K = 8 is predicted marginal, so G2 is predicted to stop.** For i.i.d. take noise, the
distance between two disjoint K-take means scales as `t_K ≈ t_1/√K`, because the difference of two
K-means has covariance `2Σ/K`. The measured `t_1` values (13b "t0on" = `d(default, seed twin)`,
`final2_table_default.log:43`; spec finding 5) give these predictions:

| Surface | `t_1` | `t_2` | `t_4` | `t_8` | `2·t_8` vs F = 4.0 |
|---|---|---|---|---|---|
| default | 5.9515 | 4.21 | 2.98 | 2.10 | 4.21 — **fails by 5 %** |
| Life max | 6.8514 | 4.84 | 3.43 | 2.42 | 4.84 — **fails by 21 %** |

This is arithmetic on one measured seed pair per surface, not a measurement. The variance of a
one-pair estimate is large, and colony takes need not be i.i.d. The spec rules the outcome
already: FR-017a stops and surfaces the measured `t_K` curve, and K is never pushed past 8 (the
seed-index cap). The plan's only lever is order: E0 (§2) measures the default surface first, so a
stop costs ~9 minutes of rendering, not a built harness.

**P2-2 — Take-set interpretation (needs acknowledgement).** The spec says two things about the take
sets. C-7.3 and FR-017a: "`t_K` is the `d` between two disjoint K-take means ... the K takes split
into two disjoint sets". The cap argument: "two disjoint sets of 16 seed indices admit at most 8
each ... K is hard-capped at 8". Only the second reading has each set holding K takes. The plan
therefore uses:
- `A_K(P) = {(s + j) mod 16 : j = 0 … K−1}`, the take set of `D(P)`, which starts at the stored
  seed `s`;
- `B_K(P) = {(s + K + j) mod 16 : j = 0 … K−1}`, the disjoint twin set, rendered only for the
  pilot and the control set;
- `t_K(P) = d(mean over A_K, mean over B_K)`.

At K = 8 the two sets cover all 16 seeds. `A_K ⊂ A_8` for every K, so the pilot derives K = 1, 2 and 4
from the 16 takes that K = 8 needs. `t_1` is then exactly the 13b seed twin (seed indices 0 and 1 for
the default surface), which cross-checks the harness against `final2_table_default.log:43`.

**P2-3 — Controls (a), (a′) and (c) are same-seed, single-take comparisons (needs acknowledgement).**
`D(P)` is a K-take mean. Comparing it with a single-take twin would put a take term of
√(1 + 1/K)·σ into every control and fail (a′) and (c) by construction. Twins rendered over all K
takes would cost K× the render time for no information, because the take term is **exactly zero**
for both parameters these controls move:
- Master gain is applied after the engine and the cavern (`renderGainAndOutputStage`,
  `processor.cpp:1101`, `:1106`).
- The sub offset feeds the global post-voice-sum chain. The ghost tap is taken "from the voice sum,
  BEFORE the subharmonic" (`vorago_engine.h:1154-1158`). No global stage feeds a voice.

At one seed the voice renders are therefore identical, and the only difference is the parameter.
The plan scores each of these controls as `d` between the stored-seed take's `M1…M3` mean and the
twin's `M1…M3` mean at that seed. For (a), the level twin, this is the buffer-scaling math on the
stored-seed take. (b) is the only control whose purpose is the take term, and it uses `t_K` as
defined in P2-2.

**P2-4 — SC-026a's friend is redundant (RULED 2026-09-29: DROPPED — the test reads `*p.engineForTest()` and defines no struct).** The spec
adds `Vorago::detail::VoragoEcosystemRosterProbe` as a friend of `Processor` so the test can reach
`engine_`. But `Processor::engineForTest()` already exists (`processor.h:116-118`: public, const,
returns `engine_.get()`). Every read SC-026a makes is const: `VoragoEngine::getVoice(i) const`
(`vorago_engine.h:1280`), `VoragoVoice::ecosystem() const` (`vorago_voice.h:1549`),
`getSyncRate() const` (`ecosystem_engine.h:601`) and `getAffinity(...) const` (`:721`).
- Recommended: drop the friend and read through `engineForTest()`. That is 0 plugin lines instead of
  2, and the test has the same teeth.
- Plan default until ruled: the spec text (forward declaration + friend in `processor.h`). The test
  body is identical either way.

**P2-5 — The E-ext primaries must beat their measured best by 1.5–1.9×.** The counted extremes
measure `d` = 2.1606 (`syncRate` 0.5, default surface) and 2.6401 (`selfAffinity` +2, Life max) as
**single-take** distances from the default (spec C-2.3 table). Their primary bar is F = 4.0, and at
least `2·s(P)`. Each pilot candidate has to author a surface on which the colony carries more of the
sound. The rule-knob effect is a colony effect, so the plan's candidates raise colony-driven
material:
- Life high, Ecosystem Depth 1.0;
- the destination sections the colony drives loud (the noise, peak and loop levers read
  `lanes.eco` only, `vorago_voice.h:2099-2144` per the spec);
- the knob at its counted extreme.

If either candidate cannot reach the bar, FR-017 stops. This is not a new risk, but the spec's
arithmetic shows how narrow it is.

**P2-6 — The default surface is predicted to satisfy more D predicates than the first pass listed.**
Reading the pack defaults:

| Default | Source | Predicted default-state cell |
|---|---|---|
| body blend 0.35, so both materials are credited (A when blend ≤ 0.65, B when ≥ 0.35) | `body_params.h:38` | **D2**, D1.StoneChamber and D1.SteelTank |
| noise slot models {FilteredWind, GranularDust, Direct, MetallicHiss} | `noise_params.h:83` | D3.1–D3.4 |
| the Direct slot type is Brown, index 5 | `param_mapping.h:118` | **D4.6** (1-based, as the spec numbers D4.1–D4.12: D4.*t* is `kNoiseTypeByIndex[t−1]`, `param_mapping.h:107-112`) |
| anchor mode Hybrid | `resonance_params.h:57-58` | **D5.3** |
| every loop Lowpass | `vorago_engine.h:183-186` | **D6.1** |
| sub f/2 −18 dB is loudest, against f/4 −24 dB and fifth −30 dB | `sub_params.h:38-40` | **D7.1** |
| Standard envelope; default `A` = 155 s | — | D8.1, D9.2 |
| freeze Off; ghost triggers Off | — | D10.2, D12.2 |

Each prediction is conditional on its S conjunct (S10, S1, S2, S4, S5) verifying on the default
surface at D_abl. No count changes: none of these was a primary candidate. They only become
default-state secondaries that §7 attaches to factory presets. The nightly
`CoverageComplete` case compares the measured set with `kRecordedDefaultStateCells` (§6.12).

The "no count changes" statement holds only while the prediction holds. If S1 or S10 does **not**
verify on the default surface, D3.1–D3.4 (S1) or D1.StoneChamber / D1.SteelTank (S10) stop being
default-state, SC-029 then requires them as primaries, and the required primary count leaves 40.
That is an FR-017 stop (§6.12, "Required primaries"), not a re-count.

**P2-7 — macOS runner concurrency.** §10's sizing needs about 10 shards per OS. GitHub caps concurrent
macOS jobs lower than Linux and Windows jobs (5 on the free tier), so the macOS shards may queue, and
the nightly wall clock may exceed one job's duration. SC-013 judges **step** time against each job's
own `timeout-minutes`, not queue time, so the gate holds. The macOS queue time is recorded in the
compliance record as a fact, not gated.

**Plan-stage rulings R-1…R-8** (spec "Plan stage", re-affirmed by the second pass): all stand and are
applied below. R-1 is §5.1. R-2 gives N = 40 (§7). R-3 is FR-006/FR-031/C-9/SC-005 at v3. R-4 is §5.8
and §5.10. R-5 is D10.1 as a secondary of S8 (§6.3, §7). R-6 is §5.10. R-7 is FR-027a plus the
`LibraryShape` case (§8, §9). R-8 is the shipped `preset_test_support.h` part 0, reused in §6.4.

---

## 4. Verified reuse inventory

| Reused | Where (read this session) | Real signature / fact used |
|---|---|---|
| Rule-knob setters | `ecosystem_engine.h:595-601`, `:707-727` | `void setSyncRate(float v) noexcept` (non-finite rejected, `std::clamp(v, 0.0f, 0.5f)`), `[[nodiscard]] float getSyncRate() const noexcept`; `void setAffinity(Kind from, Kind to, float v) noexcept` (index guard first, then non-finite rejected, clamp `[kMinAffinity, kMaxAffinity]`, NOT symmetrised), `[[nodiscard]] float getAffinity(Kind from, Kind to) const noexcept` |
| Knob defaults / ranges | `ecosystem_engine.h:168`, `:237-238`, `:2386`, `:2403-2412` | `kNumKinds = 5`; `kMinAffinity = -2.0f`, `kMaxAffinity = +2.0f`; `syncRate_ = 0.0f`; `defaultAffinity()` −1.0 on the diagonal, +0.45 elsewhere |
| Knobs survive lifecycle | `ecosystem_engine.h` prepare comment (`:336-420` region: "The affinity matrix and all 23 rule knobs are deliberately untouched"; `setSeed` "does not touch any rule knob"); `vorago_voice.h:531`, `:1684`, `:1762` | voice `prepare`, `applySeeds` and `reset` call `ecosystem_.prepare` / `setSeed` / `reset`, none of which rewrites a knob |
| No other writer | `grep -rn "setSyncRate\|setAffinity" dsp/include plugins/vorago/src` (excluding `ecosystem_engine.h`): **0 hits** | the R-1 forwarders are the only production writers |
| Voice ecosystem | `vorago_voice.h:1549`, `:2516` | `const EcosystemEngine& ecosystem() const noexcept`; `EcosystemEngine ecosystem_;` (private) |
| `VoragoVoiceParams` | `vorago_engine.h:152-191` | trivially copyable; "NO FIELD HERE MAY NAME A VoragoMacroTarget"; "Every default member initializer is the voice's prepare() step-5 value, so broadcasting a default-constructed instance is a no-op"; `kFieldCount = 31` (`:188`) |
| `applyVoiceParams` | `vorago_engine.h:845-876` | `void applyVoiceParams(const VoragoVoiceParams& p) noexcept`, loop bound `kMaxVoices`, "Every forwarder early-outs an unchanged value ... a repeated identical broadcast is inert" |
| Engine read access | `vorago_engine.h:225`, `:255`, `:1280`, `:1297`, `:1346` | `kMaxVoices = 6`; `kOutputCeilingDb = -0.3f`; `const VoragoVoice& getVoice(std::size_t) const noexcept`; `const AtmosphereEngine& atmosphere() const noexcept`; `bool isRendering(std::size_t) const noexcept` |
| Processor state I/O | `processor.cpp:569-661` | `setState`: version > current → `kResultFalse`; `v1Complete` chain; `version >= 2` → `loadV2Tail`; `else` the default-pack tail through a stack `MemoryStream`; resets pedal/pressure; raises `forcePushPending_` and `latchReleasePending_`. `getState`: version, v1 block, `saveGlobalParamsV2Ext`, 14 packs, life last |
| VP push | `processor.cpp:796-801`, `:808-820`, `:826-864` | `markDirty` bumps `voiceParamGeneration_` for `Route::VP`; `pushAllSurfaces` invalidates it; `pushVoiceParams()` builds `VoragoVoiceParams p{}` from pack atomics, then `engine_->applyVoiceParams(p)` |
| Routes | `param_routes.h:36`, `:115`, `:279-284` | `std::array<ParamRouteEntry, 108> kParamRoutes`; `{kEcosystemDepthId, Route::MB}`; `idsStrictlyAscending()`; `countRoute(Route::VP) == 31` |
| Ecosystem pack | `ecosystem_params.h` (whole file) | `struct EcosystemParams { std::atomic<float> depth{0.85f}; }`; `handleEcosystemParamChange`, `registerEcosystemParams`, `formatEcosystemParam`, `saveEcosystemParams`, EOF-safe `loadEcosystemParams` (isFinite then clamp), `loadEcosystemParamsToController` |
| State constants | `plugin_ids.h:23`, `:28-47`, `:173`, `:236` | `kCurrentStateVersion = 2`; `kStateV2Bytes = 428` with its per-pack `static_assert`; `kEcosystemDepthId = 900`; `kEcosystemParamRangeEnd = 1000` |
| Controller state | `controller.cpp:171-235` | `applyStateStream`: rejects version > current; `loadV2Tail` of `...ToController` helpers; the v1 path serializes default packs into `owned(new MemoryStream())` |
| Test seams | `processor.h:62`, `:89`, `:116-121`, `:186`, `:228` | `struct VoragoMasterGainSmootherBypassProbe;` precedent; `// getTailSamples(): NOT overridden -> SDK default kNoTail`; `engineForTest()`, `cavernForTest()`; `friend struct detail::VoragoMasterGainSmootherBypassProbe;`; `std::unique_ptr<Krate::DSP::VoragoEngine> engine_;` |
| Output stage | `processor.cpp:1101`, `:1106` | `renderGainAndOutputStage(outL, outR, n)` after the engine and the cavern (P2-3) |
| Macro vector | `processor.cpp:1010-1027` | `buildMacroVector()`: knobs, with Pressure += channel pressure |
| Cavern decay | `param_routes.h:126`, `:205`; `processor.cpp:357`; `vorago_macro_matrix.h:979-1013`, `:1129-1140` | `kSpaceDecayId` is MB-routed to `VoragoMacroTarget::CavernDecaySeconds`; `setMacros(const VoragoMacroValues&)`; `setTargetBase(VoragoMacroTarget, float)` (non-finite no-op, not clamped); `VoragoCavernTargets computeCavernTargets() const noexcept` → `.decaySeconds` |
| Decay range | `space_params.h:42`, `:58-59` | default 20 s; `kSpaceDecayMinSeconds = 0.5`, `kSpaceDecayMaxSeconds = 60.0` |
| Ghost grain ceiling | `atmosphere_engine.h:311`, `:855-857`; `vorago_engine.h:370` | `static constexpr float kMaxGrainSeconds = 30.0f`; `setGrainSeconds` clamps to it; the engine configures 12 s (not used: FR-060 takes the ceiling) |
| Ghost tap placement | `vorago_engine.h:1154-1170` | ghost fed from the voice sum before the subharmonic; its wet return is summed into the bus (P2-3; C-6 arm 4's dry residue) |
| Freeze | `cavern_verb.h:735`; `aether_reverb.h:409-410`; `processor.cpp:985-988` | `setFreeze(bool)`; freeze rides `(1 - freezeRamp)` on every send; the CV push calls `cavern_->setFreeze` |
| Envelope | `vorago_voice.h:316-342`; `envelope_params.h:60-66` | stage times `{20000, 30000, 45000, 60000, 0, 0}` ms, release 45000 ms, growth 120 s, `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }` |
| Macro defaults | `macro_params.h:33-42` | every macro 0.0 except `gravity{0.5f}` (bipolar) |
| Pack defaults (P2-6) | `body_params.h:38-45`; `noise_params.h:79-87`; `sub_params.h:36-40`; `resonance_params.h:55-58`; `ghost_params.h:37-40`; `life_params.h:32-34`; `param_mapping.h:107-118` | body blend 0.35, mix 1.0, A = 5 StoneChamber, B = 6 SteelTank; noise −18 dB, models `{1,2,0,3}`, type index 5 = Brown; sub offset 0, f/2 −18, f/4 −24, fifth −30 dB; anchor Hybrid; ghost reverse 0, triggers 0; breathing 0.30, tidal 0.40 |
| Seeds / polyphony | `global_params.h:42-43`, `:76`, `:111` | `polyphony{4}` (list 1–6, default index 3); `seedIndex{0}` into 16 seeds, `indexFromNormalized(value, kNumSeeds)` |
| Tapers | `param_mapping.h:29-62` | `linearFromNormalized/linearToNormalized(double, mn, mx)`; `indexFromNormalized(n, count)`, `indexToNormalized(i, count)` |
| Preset config | `vorago_preset_config.h:24-44` | `makeVoragoPresetConfig()` (subcategories `{"Drones"}`), `makeVoragoPresetTabLabels()` derives from it |
| Preset manager | `preset_manager.h:55-61`, `:70`, `:75`, `:120`; `preset_manager.cpp:264-277` | ctor `(PresetManagerConfig, IComponent*, IEditController*, path userDirOverride = {}, path factoryDirOverride = {})`; `scanPresets()`; `getPresetsForSubcategory(const std::string&) const`; `static bool isValidPresetName(const std::string&)`; the exact `Info` XML bytes, unescaped |
| Test host pieces | `vst_param_changes.h:78`, `:110`; `vst_event_list.h:36`, `:63`, `:77` | `Krate::Test::ParameterChanges::addChange(ParamID, double)`; `Krate::Test::EventList::addNoteOn(int16 pitch, float velocity, ...)`, `addNoteOff(int16 pitch, int32 sampleOffset = 0)`; Catch2-free |
| Fixture (not tool-usable) | `vorago_test_fixture.h:38`, `:186-202` | includes Catch2; `REQUIRE` in `ProcessorFixture`'s constructor and `prepare` (spec finding 3) |
| Descriptor (R-8) | `preset_test_support.h` (whole file) | `struct PresetDescriptor`, `kDescriptorBands = 9`, `describe(span L, span R, double sr)`, `descriptorDistance`, `meanOf(span<const PresetDescriptor>)`; Catch2-free |
| Probe precedent | `ecosystem_rule_probe_test.cpp:108-175`, `:279-298`, `:358-470` | probe struct defined in the TU; `const_cast` of `engineForTest()`; seed change delivered as an offset-0 point in block 0 alongside the NoteOn; per-block finiteness by `Krate::DSP::detail::isFinite`; `readEnv` with `getenv_s` under MSVC |
| Metrics | `vorago_fixtures.h:101`, `:245`, `:316`, `:344`, `:441`, `:502`; `low_frequency_metrics.h:478` | `kPowerFloor = 1e-30`; `bandEnergyDb`, `crestFactorDb`, `blockRmsDb`, `perBandTotalVariation`, `perBinMagnitudeFlux`; `calculateCorrelation(const float*, const float*, std::size_t)` |
| Fingerprint | `render_fingerprint.h:58-63`, `:73`, `:108`, `:122-124` | `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`; `fingerprintRender(span)`; `compareFingerprints(actual, reference, ...)`; `.withinTolerance()` |
| Allocation | `allocation_detector.h:111-190`; `automation_rt_test.cpp:115-119` | `TestHelpers::AllocationScope` / `ThreadScopedAllocationScope`; the live count is read inside the scope via `TestHelpers::AllocationDetector::instance().getAllocationCount()` (the member count is latched only in the destructor) |
| Stimulus constants | `soak_test.cpp:44-45`; `processor_cpu_test.cpp:84-85` | `kVelocity100 = 100.0f / 127.0f`; `kOutputCeiling = 0.9661f`; `kCpuPolyphony = 4`; `kCpuNotes{36, 40, 43, 47}` |
| Generator precedent | root `CMakeLists.txt:604-669`; `seraphis_preset_generator.cpp:77-126`, `:224-265`, `:280-349` | processor.cpp + 5 SDK sources + the stub compiled into the tool; `void* moduleHandle = nullptr;` in the tool TU; `kProcessorUID.toString(buf)` → 32 chars; `writeVstPreset`; `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin` |
| Vorago stub | `plugins/vorago/tests/vstgui_test_stubs.cpp` | defines `GetPluginFactory()` only, so the generator TU defines `moduleHandle` as Seraphis's does |
| Release contract | `release.yml:150-182` | `*` case → `${PLUGIN}_preset_generator`; `./build/bin/${binary} generated-presets`; `if-no-files-found: error` |

---

## 5. Component design

### 5.1 DSP append (Layer 3, `dsp/include/krate/dsp/systems/`) — R-1, FR-071a, FR-076

This is the only `dsp/` edit of the phase. All of it is append-only, and no new class is created.

**`vorago_voice.h`.** Two public forwarders, placed after `getEcosystemDepthFor` (the FR-021 block,
`:1474-1477`):
```cpp
/// @brief Phase 14 FR-071a / R-1: the registered Ecosystem Sync knob (ID 901).
///        Forwards to EcosystemEngine::setSyncRate (ecosystem_engine.h:595): a
///        non-finite value is rejected, a finite one clamped to [0, 0.5].
///        A pure store, so a repeated identical broadcast is inert (the
///        applyVoiceParams contract, vorago_engine.h:852-855).
void setEcosystemSyncRate(float v) noexcept { ecosystem_.setSyncRate(v); }

/// @brief Phase 14 FR-071a / R-1: the registered Ecosystem Self Affinity knob
///        (ID 902). Writes the affinity DIAGONAL - setAffinity(k, k, v) for all
///        five kinds - exactly as the Phase 13b tables measured it
///        (ecosystem_rule_probe_test.cpp applyAffinityDiagonal). Off-diagonal
///        entries are untouched. Non-finite rejected, clamped to [-2, 2]
///        (ecosystem_engine.h:707).
void setEcosystemSelfAffinity(float v) noexcept {
    for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
        const auto kind = static_cast<EcosystemEngine::Kind>(k);
        ecosystem_.setAffinity(kind, kind, v);
    }
}
```
No getters are added. Tests read `ecosystem().getSyncRate()` and `ecosystem().getAffinity(k, k)`
through the existing const accessor (`:1549`).

**`vorago_engine.h`, `VoragoVoiceParams`.** Two fields are appended after `ecologyLoopFilterMode`
(`:183-186`), and `kFieldCount` is updated:
```cpp
    /// Phase 14 FR-071a (R-1). == EcosystemEngine::syncRate_ (ecosystem_engine.h:2386).
    float ecosystemSyncRate = 0.0f;
    /// Phase 14 FR-071a (R-1). The affinity DIAGONAL; == defaultAffinity()'s
    /// diagonal (ecosystem_engine.h:2403-2411).
    float ecosystemSelfAffinity = -1.0f;

    /// 2 + 2 + 4 x 5 + 1 + 6 + 2 = 33 scalar values (FR-003; Phase 14 R-1 +2).
    static constexpr std::size_t kFieldCount = 33;
```
Both defaults equal the engine's member defaults, so broadcasting a default-constructed instance
stays a no-op, which is the struct's own rule (`:156-158`). Neither field names a `VoragoMacroTarget`
(`:152-154`). No macro row targets a rule knob: the grep above found 0 writers outside the engine.

**`applyVoiceParams`.** Two lines are appended inside the slot loop, after the loop-filter loop
(`:873-875`):
```cpp
            voice.setEcosystemSyncRate(p.ecosystemSyncRate);
            voice.setEcosystemSelfAffinity(p.ecosystemSelfAffinity);
```

**RT safety and lifecycle.**
- Cost per broadcast: 6 voices × (1 + 5) clamped stores. No allocation, no branch on audio data.
- The knobs survive voice `prepare`, `reset` and reseed (§4, "Knobs survive lifecycle").
  `pushAllSurfaces(Scope::Reprepared)` re-broadcasts them anyway (`processor.cpp:808-820`).
- Seraphis is untouched: `VoragoVoice` and `VoragoEngine` are Vorago-only, and `ecosystem_engine.h`
  is not edited.

**ODR:** `setEcosystemSyncRate`, `setEcosystemSelfAffinity`, `ecosystemSyncRate` and
`ecosystemSelfAffinity` each had **0 hits** under `dsp/ plugins/ tools/ tests/`.

### 5.2 Plugin surface — FR-072…FR-074, SC-026a, SC-027

**IDs** (`plugin_ids.h`, after `:173`), both plain continuous `Steinberg::Vst::Parameter`, never list
types (`:82-83` freeze types, not IDs):
```cpp
    kEcosystemDepthId = 900,
    kEcosystemSyncRateId = 901,      // Phase 14 FR-072, R ratified 2026-09-29
    kEcosystemSelfAffinityId = 902,  // Phase 14 FR-072
```

**State v3** (`plugin_ids.h:23`, `:28-47`):
```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 3;   // Phase 14 FR-072
constexpr std::size_t kStateV2Bytes = 428;             // unchanged; still asserted
/// Phase 14 FR-072: v3 = the v2 stream + the ecosystem rule-knob extension,
/// appended AFTER the life pack (the v2 stream is a strict prefix).
constexpr std::size_t kStateV3Bytes = kStateV2Bytes + 4    // float syncRate
                                                    + 4;   // float selfAffinity
static_assert(kStateV3Bytes == 436, "spec FR-006: 428 + 4 * |R|, |R| = 2");
```
The extension goes **after** the life pack. It does not go inside the mid-stream ecosystem pack,
because the v2 stream stays a strict prefix, as the v1 stream is of v2 (`processor.cpp:564-565`).

**Pack** (`ecosystem_params.h`):
```cpp
inline constexpr double kEcosystemSyncRateMin = 0.0;       // == setSyncRate clamp
inline constexpr double kEcosystemSyncRateMax = 0.5;
inline constexpr double kEcosystemSyncRateDefault = 0.0;  // n0 = 0
inline constexpr double kEcosystemSelfAffinityMin = -2.0; // == kMinAffinity
inline constexpr double kEcosystemSelfAffinityMax = 2.0;  // == kMaxAffinity
inline constexpr double kEcosystemSelfAffinityDefault = -1.0;  // n0 = 0.25
static_assert(kEcosystemSelfAffinityMin == Krate::DSP::EcosystemEngine::kMinAffinity &&
              kEcosystemSelfAffinityMax == Krate::DSP::EcosystemEngine::kMaxAffinity);

struct EcosystemParams {
    std::atomic<float> depth{0.85f};
    std::atomic<float> syncRate{0.0f};       ///< 901, [0, 0.5], linear
    std::atomic<float> selfAffinity{-1.0f};  ///< 902, [-2, 2], linear
};

/// FR-072: the v3 extension, 8 bytes, written after saveLifeParams.
inline void saveEcosystemParamsV3Ext(const EcosystemParams&, Steinberg::IBStreamer&);
/// EOF-safe like loadEcosystemParams: false at the first failed read, later
/// fields unchanged; non-finite rejected (field unchanged); finite clamped.
inline bool loadEcosystemParamsV3Ext(EcosystemParams&, Steinberg::IBStreamer&);
template <typename SetParamFunc>
inline void loadEcosystemParamsV3ExtToController(Steinberg::IBStreamer&, SetParamFunc);
```
- **Taper.** Both knobs are linear. The registered range is the setter's clamp range, so no stored
  value is one the engine would clamp again.
- **Handler.** `handleEcosystemParamChange` gains two cases, each
  `linearFromNormalized(value, Min, Max)` stored relaxed.
- **Registration.** `registerEcosystemParams` gains two `addParameter` calls:
  - `"Ecosystem Sync"` with defaults 0.0 / 0.0;
  - `"Ecosystem Self Affinity"` with default normalized 0.25.

  Both use `ParameterInfo::kCanAutomate` and unit `""`.
- **Formatting.** `formatEcosystemParam` prints sync as `"%.2f"` and affinity as `"%+.2f"`.
- **Existing functions.** `saveEcosystemParams` and `loadEcosystemParams` do not change: the v2 pack
  is still one float.

**Routes** (`param_routes.h`):
- Rows `{kEcosystemSyncRateId, Route::VP}` and `{kEcosystemSelfAffinityId, Route::VP}` go directly
  after `{kEcosystemDepthId, Route::MB}` (`:115`), keeping the order strictly ascending.
- `std::array<ParamRouteEntry, 108>` becomes `110`.
- `countRoute(Route::VP) == 31` becomes `33` (`:281`).

VP is the right route: the knobs are per-voice, broadcast, and not macro targets.

**Processor**:
- `processParameterChanges` needs no change: the band dispatch `id < kEcosystemParamRangeEnd`
  (`processor.cpp:718`) already reaches `handleEcosystemParamChange`, and `markDirty` bumps the VP
  generation from the route table.
- `pushVoiceParams()` gains two copies before the broadcast (`:862`):
  ```cpp
    p.ecosystemSyncRate = ecosystemParams_.syncRate.load(kRelaxed);
    p.ecosystemSelfAffinity = ecosystemParams_.selfAffinity.load(kRelaxed);
  ```
- `getState()` appends `saveEcosystemParamsV3Ext(ecosystemParams_, s);` after `saveLifeParams`
  (`:659`).
- `setState()` keeps its structure and adds a v3 step after the existing v2 branch:
  ```cpp
    bool v2Complete = false;                       // (was a [[maybe_unused]] local, :596)
    if (version >= 2) { if (v1Complete) { v2Complete = loadV2Tail(s); } }
    else { /* existing default-v2-tail block, unchanged (:597-625) */ }
    if (version >= 3) {
        if (v2Complete) { [[maybe_unused]] const bool v3 = loadEcosystemParamsV3Ext(ecosystemParams_, s); }
    } else {
        // FR-072: a version < 3 stream leaves every roster knob at its REGISTERED
        // DEFAULT, whatever the previous state held (the :595-609 pattern).
        constexpr std::size_t kV3ExtBytes = kStateV3Bytes - kStateV2Bytes;
        std::array<char, kV3ExtBytes> ext{};
        MemoryStream extStream(ext.data(), static_cast<TSize>(ext.size()));
        IBStreamer out(&extStream, kLittleEndian);
        saveEcosystemParamsV3Ext(EcosystemParams{}, out);
        extStream.seek(0, IBStream::kIBSeekSet, nullptr);
        [[maybe_unused]] const bool defaultsLoaded = loadEcosystemParamsV3Ext(ecosystemParams_, out);
        assert(defaultsLoaded);
    }
  ```
- The header comment (`:564-568`) is updated to "v3, 436 bytes".

**Controller**:
- `applyStateStream` (`controller.cpp:171-235`) mirrors the processor. For `version >= 3` it calls
  `loadEcosystemParamsV3ExtToController(streamer, setParam)`. Otherwise it serializes
  `EcosystemParams{}`'s extension into `owned(new MemoryStream())` and feeds it through the same
  helper, the `:212-234` pattern.
- `registerEcosystemParams` (`:103`) and `formatEcosystemParam` (`:247`) already dispatch the
  ecosystem band.

**UI** (`resources/editor.uidesc`, page-6, `:383-387`):
- Two `ArcKnob`s go on row r0, beside EventsRateScale (x 18) and EcosystemDepth (x 86) in the
  68-px column pitch:
  - `EcosystemSyncRate` at `origin="154, 4"`, label "Eco Sync" at `142, 52`;
  - `EcosystemSelfAffinity` at `origin="222, 4"`, label "Self Affin" at `210, 52`.
- Each has the `size`, `arc-color`, `guide-color` and label attributes of `:387-388`, plus a tooltip.
- Two `control-tag` rows go after `:111`: `name="EcosystemSyncRate" tag="901"` and
  `name="EcosystemSelfAffinity" tag="902"`.
- The edit is made with an XSLT stylesheet (the project rule for uidesc edits).

**SC-026a access path.** RULED 2026-09-29 (P2-4): the friend is DROPPED — the test reads
`*p.engineForTest()` (`processor.h:116-118`) and defines no struct; `processor.h` is not touched for
SC-026a. The paragraph below records the superseded default. In
`processor.h`, the `detail` block (`:58-63`) gains `struct VoragoEcosystemRosterProbe;` beside
`VoragoMasterGainSmootherBypassProbe`, and the private section (`:186`) gains
`friend struct detail::VoragoEcosystemRosterProbe;`. The struct is defined only in
`unit/ecosystem_roster_test.cpp`:
```cpp
namespace Vorago::detail {
struct VoragoEcosystemRosterProbe {
    static const Krate::DSP::VoragoEngine& engine(const Processor& p) { return *p.engine_; }
};
}
```
ODR: `VoragoEcosystemRosterProbe` had **0 hits**.

**Count-bearing tests to update.** Every constant becomes a named expression
(`108 + kNumEcosystemRosterParams`), not a new literal. The sweep that finds them is
`grep -rn "108\|106\|kStateV2Bytes\|kCurrentStateVersion\|kFieldCount" plugins/vorago/tests dsp/tests/unit/systems/vorago_param_surface_test.cpp`.
The hit list this session:
- `editor_layout_test.cpp`:
  - `kIdNames` size (`:109`);
  - the page-6 set (`:252-256`), which gains 901 and 902;
  - the counts all108 (`:838`), nonHidden (`:839`), boundIds (`:873`), expectedName (`:909`),
    tagMap (`:922`), checked (`:1008`), pageUnion 90 → 92 (`:1127`) and getParameterCount
    (`:1305-1348`);
  - the header comment (`:9-10`).
- `param_table_expected.h` and `param_table_test.cpp`: rows for 901/902 in `Vorago_ParamIdMap`,
  `Vorago_RouteTable`, `Vorago_ParameterInfoTable` and `Vorago_ParamInputHygiene`.
- `state_roundtrip_test.cpp` (`Vorago_StateRoundTrip`, `:110`): current-stream sizes and `memcmp` at
  `kStateV3Bytes`; the version field is 3; the `kCurrentStateVersion + 1` rejection cases now reject 4.
- `state_v2_test.cpp` (`Vorago_StateRoundTripV2`, `:388`) becomes a **legacy v2 load** test. The v2
  stream is built as `getState()` bytes with the version int set to 2, truncated to `kStateV2Bytes`.
  This is exact, because v2 is a strict prefix of v3.
- `automation_rt_test.cpp`, `continuity_test.cpp`, `ecosystem_frame_test.cpp`,
  `param_surface_test.cpp`, `preset_browser_test.cpp`, `processor_cpu_test.cpp`, `soak_test.cpp`,
  `body_params_test.cpp`, `envelope_params_test.cpp` and `param_denorm_test.cpp`: each hit is read in
  context. Lines that mean "the registered surface" or "the current stream" move; lines that mean a v2
  literal stay.
- `dsp/tests/unit/systems/vorago_param_surface_test.cpp:1088`: `STATIC_REQUIRE(VoragoVoiceParams::kFieldCount == 31u)` becomes `33u`.

`plugins/vorago/CLAUDE.md` gets the matching updates:
- the ID table (ecosystem band);
- "110 registered IDs, 108 persisted";
- the route totals;
- the state table (a v3 row, the 436-byte total);
- the page-6 row;
- the 1.0.0 controller-interface freeze (FR-064).

### 5.3 `getTailSamples()` — FR-060, SC-030 — `src/processor/tail_estimate.h` (new, header-only)

ODR: `effectiveCavernDecaySeconds` and `tail_estimate` had **0 hits**.
```cpp
#pragma once
#include "parameters/space_params.h"
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <algorithm>

namespace Vorago {
/// FR-060's G: the longest life a ghost grain born before NoteOff can have
/// (atmosphere_engine.h:311; setGrainSeconds clamps to it, :855-857). A
/// state-independent ceiling, NOT the 12 s the engine configures.
inline constexpr double kGhostGrainCeilingSeconds =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds);

/// C-6 / FR-060 effective RT60: the shipped matrix's CavernDecaySeconds with the
/// stored decay installed as that target's base, the KNOB macros applied, then
/// clamped to the cavern's [0.5, 60] s range (space_params.h:58-59). One
/// definition, used by getTailSamples AND by the harness timeline.
[[nodiscard]] inline float effectiveCavernDecaySeconds(const Krate::DSP::VoragoMacroValues& knobs,
                                                       float storedDecaySeconds) noexcept {
    Krate::DSP::VoragoMacroMatrix m{};
    m.setTargetBase(Krate::DSP::VoragoMacroTarget::CavernDecaySeconds, storedDecaySeconds);
    m.setMacros(knobs);
    return std::clamp(m.computeCavernTargets().decaySeconds,
                      static_cast<float>(kSpaceDecayMinSeconds),
                      static_cast<float>(kSpaceDecayMaxSeconds));
}

/// FR-060: Rel + RT60_eff + G, in seconds.
[[nodiscard]] inline double tailSeconds(float releaseMs, float rt60Seconds) noexcept {
    return static_cast<double>(releaseMs) / 1000.0 + static_cast<double>(rt60Seconds) +
           kGhostGrainCeilingSeconds;
}
}  // namespace Vorago
```
`Processor` replaces the comment at `processor.h:89` with
`Steinberg::uint32 PLUGIN_API getTailSamples() override;`. The body:
- if `spaceParams_.freeze` (relaxed) is non-zero, return `Steinberg::Vst::kInfiniteTail`;
- otherwise build the **knob** macro vector from `macroParams_` alone, not `buildMacroVector()`,
  because FR-060 says "from the currently decoded state" and channel pressure is not state;
- return `static_cast<uint32>(std::llround(tailSeconds(releaseMs, effectiveCavernDecaySeconds(knobs, decay)) * processSetup.sampleRate))`.

Largest value: (60 + 60 + 30) s × 192 kHz = 2.88e7, far below `kInfiniteTail`. The function reads
relaxed atomics only, and the matrix is a stack value (two 1-D arrays, `vorago_macro_matrix.h:1206-1207`),
so it is allocation-free and safe on any host thread. `processor.cpp` includes the new header, which
needs no CMake change.

### 5.4 Category config — FR-001

`vorago_preset_config.h:29` becomes
`/*.subcategoryNames  =*/{"Drones", "Abyss", "Caverns", "Organisms", "Machines", "Textures", "Ghosts"}`.
`makeVoragoPresetTabLabels()` (`:36-44`) derives its list from this and needs no change. The banner
(`:3-12`) gains the seven-name ruling (Clarifications 2026-09-27 Q6). On disk there are seven
directories. `Drones/.gitkeep` is deleted once `Drones` holds a preset.

### 5.5 Definitions — `tools/vorago_preset_defs.h` (data only, namespace `Vorago::PresetDefs`) — FR-010, FR-022

The rules are Seraphis's (`seraphis_preset_defs.h`):
- every function is `inline`;
- every table is a function-local `static const`;
- the only project include is `plugin_ids.h`, plus std;
- no state layout.

```cpp
namespace Vorago::PresetDefs {
struct ParamSetting { Steinberg::Vst::ParamID id; double normalized; };

enum class CapabilityGroup : std::uint8_t { S, M, E, D };

/// FR-010: exactly the C-2.1 cells plus the ratified E-ext cells, in this order.
enum class Capability : std::uint8_t {
    S1Noise, S2Resonance, S3Smear, S4Ecology, S5Sub, S6Bloom, S7Ecosystem, S8Cavern, S9Ghost, S10Body,
    M1Darkness, M2Age, M3Density, M4Movement, M5Gravity, M6Entropy,              // VoragoMacro order
    M7Pressure, M8Weight, M9Fog, M10Life, M11Depth, M12Mass,
    E1PartialBloom, E2ResonatorPeaks, E3NoiseWake, E4FeedbackLoopWake, E5GhostBursts,
    E6SyncRateHi, E7SelfAffinityHi,                                               // FR-075
    D1Glass, D1Strings, D1MetalPlate, D1Chamber, D1Ice, D1StoneChamber, D1SteelTank,
    D1WoodenHull, D1CathedralColumn, D1CavernWall, D1GlassSphere,                 // BodyMaterial order
    D2BlendBoth,
    D3Direct, D3FilteredWind, D3GranularDust, D3MetallicHiss,                     // NoiseOrganismModel order
    D4Type1, D4Type2, D4Type3, D4Type4, D4Type5, D4Type6,                         // spec D4.1-D4.12:
    D4Type7, D4Type8, D4Type9, D4Type10, D4Type11, D4Type12,                      // D4.t = kNoiseTypeByIndex[t-1]
    D5Free, D5Keyed, D5Hybrid,
    D6Lowpass, D6Bandpass, D6Highpass,
    D7Div2, D7Div4, D7FifthBelow,
    D8Standard, D8Growth,
    D9FastAttack, D9SlowAttack,
    D10FreezeHolds, D10FreezeOff,
    D11GhostReverse,
    D12TriggersOn, D12TriggersOff,
    D13SlowEvents, D13FastEvents,
    D14Breathing, D14Tidal,
    Count
};

enum class Verification : std::uint8_t {
    Ablation,            // S, M: C-7.4 with `ablation`
    RouteIsolated,       // E1..E5: C-2.1 Group E
    ExtReversion,        // E6.hi, E7.hi: side predicate AND C-7.4 knob reversion
    StateWithS,          // D1..D7, D11, D12.1: state AND the named S cell verified (secondary bar)
    StateWithReversion,  // D13, D14: state AND S7 (D13) AND the reversion / depth ablation
    AttackWindow,        // D8, D9: state; primaries by the C-7.4 attack-window reversion
    FreezeGesture,       // D10.1: the S8 preset's gesture render + dry-residue twin (never primary)
    StateOnly            // D10.2, D12.2 (default-state by construction)
};

struct CellSpec {
    Capability cell;
    CapabilityGroup group;
    std::string_view label;                // "S1 noise organism", printed in the matrix
    Verification verification;
    std::array<ParamSetting, 4> ablation;  // override (S, M, E-ext, D13, D14, D primaries); unused {0, -1}
    std::uint8_t ablationCount;
    Capability sConjunct;                  // StateWithS / D13: the S cell required; else Count
};
[[nodiscard]] inline const std::array<CellSpec, static_cast<std::size_t>(Capability::Count)>& cellSpecs();
static_assert(static_cast<std::size_t>(Capability::Count) == 79);   // 10 S + 12 M + 7 E + 50 D

/// The measured default-state set (§6.12), initialised from P2-6's prediction and corrected
/// from the first measured default-surface run before authoring.
[[nodiscard]] inline std::span<const Capability> kRecordedDefaultStateCells();
/// SC-029's required primaries, DERIVED from kRecordedDefaultStateCells (§6.12):
/// S1-S10, M1-M12, E1-E5, E6.hi, E7.hi, plus (D1.* u D3.* u D8.* u D9.*) minus default-state.
[[nodiscard]] inline std::vector<Capability> requiredPrimaryCells();

struct VoragoPresetDef {
    std::string_view name;
    std::string_view category;
    std::string_view description;          // P-7: no " & < > (written unescaped, preset_manager.cpp:271-274)
    Capability primary;
    std::vector<Capability> secondaries;
    std::vector<ParamSetting> params;      // normalized; untouched IDs keep registered defaults (C-4)
};
[[nodiscard]] inline const std::vector<VoragoPresetDef>& allPresets();   // definition order == file order
inline constexpr std::array<std::string_view, 7> kCategories{
    "Drones", "Abyss", "Caverns", "Organisms", "Machines", "Textures", "Ghosts"};
[[nodiscard]] inline std::string buildVoragoInfoXml(std::string_view name, std::string_view category,
                                                    std::string_view description);
}  // namespace Vorago::PresetDefs
```
- `buildVoragoInfoXml` writes the bytes of `preset_manager.cpp:265-277` exactly, with
  `MusicalCategory == MusicalInstrument == category` and the `Comment` line present when the
  description is non-empty.
- ODR (this session): `VoragoPresetDef`, `CellSpec`, `CapabilityGroup`, `Capability`,
  `Verification`, `kRecordedDefaultStateCells` and `requiredPrimaryCells` each had **0 hits**.
- `kRecordedDefaultStateCells` is a function returning a span over a function-local `static const`
  array, which keeps the header's "every table is a function-local `static const`" rule. The `k`
  prefix stays because every other part of this plan names it that way.
- `ParamSetting` has 1 hit, `Seraphis::PresetDefs::ParamSetting` (`seraphis_preset_defs.h:59`). It
  is in a different namespace and a different target, and no TU includes both defs headers.

### 5.6 Catch2-free host — `plugins/vorago/tests/vorago_preset_host.h` — FR-021

The host is shared by the generator and by every preset test TU. It includes no Catch2 header, and
the generator target does not link Catch2, so any Catch2 dependency fails the generator build (the
proof). ODR: `PresetHost` had **0 hits**. `buildComponentState` has 1 hit, a global-namespace free
function in Iterum's `tools/preset_generator.cpp:476`, so this plan names its function
`buildPresetComponentState` to keep grep unambiguous.
```cpp
namespace VoragoTest {
class PresetHost {
public:
    PresetHost();   // std::make_unique<::Vorago::Processor>() (the processor is never on the stack)
    ~PresetHost();  // setActive(false) if active, then terminate()
    PresetHost(const PresetHost&) = delete; PresetHost& operator=(const PresetHost&) = delete;

    /// initialize(nullptr) -> setupProcessing({kRealtime, kSample32, maxBlock, sr}) -> setActive(true).
    /// Sizes outL_/outR_ to maxBlock ONCE (never regrown).
    [[nodiscard]] Steinberg::tresult prepare(double sampleRate, Steinberg::int32 maxBlock);
    [[nodiscard]] Steinberg::tresult process(std::size_t n, Steinberg::Vst::IEventList* ev,
                                             Steinberg::Vst::IParameterChanges* pc);   // n <= maxBlock
    [[nodiscard]] Steinberg::tresult loadState(std::span<const std::uint8_t> comp);    // setState(MemoryStream)
    [[nodiscard]] bool saveState(std::vector<std::uint8_t>& out);                      // getState(MemoryStream)
    [[nodiscard]] std::span<const float> outL() const noexcept;
    [[nodiscard]] std::span<const float> outR() const noexcept;
    [[nodiscard]] ::Vorago::Processor& processor() noexcept;
private:
    std::unique_ptr<::Vorago::Processor> proc_;
    std::vector<float> outL_, outR_;
    bool active_ = false;
};

/// C-4, THE drive (FR-021): prepare(48000, 512) -> ONE process(512) carrying every
/// def.params point at offset 0 -> getState. Used by the generator AND by
/// Vorago_FactoryPresets_TreeMatchesGenerator. Rejects (why set): any value outside
/// [0, 1] or non-finite (bit pattern); any point for kSustainPedalId (4) or
/// kChannelPressureId (5); a duplicate ID.
[[nodiscard]] bool buildPresetComponentState(const Vorago::PresetDefs::VoragoPresetDef& def,
                                             std::vector<std::uint8_t>& comp, std::string& why);
}  // namespace VoragoTest
```
Parameter points are delivered through `Krate::Test::ParameterChanges` (`vst_param_changes.h:78`),
and notes through `Krate::Test::EventList` (`vst_event_list.h:36`). Both headers are Catch2-free.

**Staging (§2).** C1 builds the class alone. `buildPresetComponentState` is added in C2, together
with `tools/vorago_preset_defs.h`. Until then the host header does not include the defs header, so the
C1 host and E0 compile without any C2 item.

### 5.7 Generator + CMake — FR-018…FR-020, FR-023, FR-025

`tools/vorago_preset_generator.cpp` follows `seraphis_preset_generator.cpp` without its partials
block:
- defines `void* moduleHandle = nullptr;` (the Vorago stub defines only `GetPluginFactory`);
- takes the class id from `Vorago::kProcessorUID.toString(buf)` and requires exactly 32 characters;
- takes the output base from `argv[1]`, defaulting to `plugins/vorago/resources/presets`;
- creates the seven `kCategories` directories;
- iterates `allPresets()` in definition order and calls `buildPresetComponentState` for each;
- writes the 48-byte header + `Comp` + `Info` + `List` layout of `writeVstPreset`
  (`seraphis_preset_generator.cpp:224-265`), duplicated into this TU's anonymous namespace so the
  Seraphis tool is not edited;
- exits 1 on any failure;
- uses no timestamp, no directory iteration and no RNG (FR-023).

In the root `CMakeLists.txt`, after `generate_seraphis_presets` (`:664-669`), add a block copied from
`:597-662`:
- **Sources:** `tools/vorago_preset_generator.cpp`, `plugins/vorago/src/processor/processor.cpp`, the
  same five SDK sources, and `plugins/vorago/tests/vstgui_test_stubs.cpp`. `processor.cpp` is the only
  plugin `.cpp` it needs: its plugin includes are header-only (`processor.cpp:11-14`;
  `src/processor/` and `src/engine/` hold no other `.cpp`).
- **Link:** `target_link_libraries(vorago_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)`,
  with no VSTGUI. `processor.cpp`'s `dataexchange.h` include (`:20`) is SDK utility code, and
  `vorago_tests` resolves it from `sdk` without listing a source.
- **Include dirs:** `plugins/vorago/src`, `plugins/vorago/tests` (the host header),
  `tests/test_helpers` (header-only use, not the Catch2-linking target), `tools`, and
  `${vst3sdk_SOURCE_DIR}`.
- **Properties:** `cxx_std_20` and `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`.
- **Warnings:** the Seraphis MSVC/GCC flag set, including `/wd4459`, which the same shared KrateDSP
  headers need.
```cmake
add_custom_target(generate_vorago_presets
    COMMAND vorago_preset_generator "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets"
    DEPENDS vorago_preset_generator
    COMMENT "Generating Vorago factory presets (40 presets across 7 categories)"
    VERBATIM)
```
`release.yml` needs no edit (`:150-182`). A WSL/GCC build of the target runs before C2 merges
(FR-025).

### 5.8 Harness — `plugins/vorago/tests/preset_test_support.h` (Catch2-free, extended) — FR-028…FR-038

Part 0 (shipped) stays byte-for-byte except for one additive overload (§6.4). Everything below is new,
in namespace `VoragoTest`.

**Staging (§2).** C1 builds Constants, Take sets, Render, Env and Pool: everything E0 needs, and
none of it references B's v3 surface or the defs header. C2, after B, builds Container, Info, Typed
decode, Timeline, the Descriptor overload, Outcomes and Vector. D builds Record, Sharding and Memo.

| Piece | Signature sketch | Notes |
|---|---|---|
| Constants | `inline constexpr double kFloorF = 4.0; kSecondaryBar = 1.5; kSeedTwinMargin = kFloorF / 2; kLevelTwinBound = 0.05; kFreezeFloorDb = 20.0; kTailDropDb = 40.0;` `inline constexpr int kNumSeedIndices = 16, kMaxTakes = 8;` `inline constexpr int kRuledTakes = /* set at G2 */;` | every threshold is named once; `kRuledTakes` is written only after G2 (before G2 it does not exist, and the pilot TU loops K itself) |
| Take sets (P2-2) | `[[nodiscard]] int takeSeedIndex(int stored, int set /*0=A,1=B*/, int j, int K) noexcept` → `(stored + set·K + j) mod 16`; `double seedNormalized(int index)` → `index / 15.0` | `static_assert(2 * kMaxTakes == kNumSeedIndices)` |
| Container | `struct PresetFile { std::string classId; std::vector<std::uint8_t> comp; std::string info; bool ok; std::string why; };` `PresetFile parseVstPreset(const std::filesystem::path&)` | magic, version, 32-character id, list offset in bounds, `Comp` and `Info` present, offsets and sizes in bounds (FR-028). ODR: `PresetFile` has 1 hit, in `SeraphisTest` (a different namespace and target) |
| Info | `std::map<std::string, std::string> parseInfoAttributes(std::string_view)` | six attributes + `Comment` |
| Typed decode (FR-031) | `struct DecodedPresetState { ::Vorago::GlobalParams global; ::Vorago::MacroParams macros; ::Vorago::CloudParams cloud; … ::Vorago::LifeParams life; std::int32_t version; std::size_t bytesConsumed; };` `bool decodePresetState(std::span<const std::uint8_t>, DecodedPresetState&)` | calls the shipped `load*Params` in `getState()` order (`processor.cpp:643-659`), then `loadEcosystemParamsV3Ext`; succeeds only if every loader returns true and `bytesConsumed == kStateV3Bytes`. The packs hold atomics, so the struct is non-copyable and filled through the out-param. ODR: `DecodedPresetState` has 1 hit, in `SeraphisTest` |
| Timeline (C-6) | `struct SweepTimeline { double A, rel, rt60, sus0, sus1, m[3][2], H, tail0, tail1, total; bool freezeOnTail; };` `SweepTimeline makeTimeline(const DecodedPresetState&, bool freezeGesture)` | §6.1; `rt60` from `effectiveCavernDecaySeconds` (§5.3). ODR: 1 hit, `SeraphisTest::SweepTimeline` |
| Render | `struct RenderSpec { std::span<const std::uint8_t> comp; std::vector<std::pair<Steinberg::Vst::ParamID, double>> block0; int seedIndex = -1; double sr = 48000; std::vector<std::int16_t> notes{36}; std::int32_t forcePolyIndex = -1; double noteOffAt = -1; double freezeAt = -1; double end; std::vector<std::pair<double,double>> capture; };` `struct SweepCapture { bool finite; float peak; std::vector<double> blockPowerL, blockPowerR; std::vector<std::vector<float>> capL, capR; };` `SweepCapture renderPreset(const RenderSpec&)` | streaming: each block updates bit-pattern finiteness (`Krate::DSP::detail::isFinite`) and the stereo peak, and records one power sum per 512-sample block; samples are copied only inside `capture` windows. `seedIndex >= 0` adds `kSeedId → seedNormalized(i)` to block 0. An empty `comp` skips `setState` (the default surface, E0). `block0` holds plain (ID, normalized) pairs, not `PresetDefs::ParamSetting`, so the C1 render has no defs dependency. ODR: `RenderSpec` has 1 hit, in an anonymous namespace in a Seraphis TU; `SweepCapture` 0 |
| Descriptor | shipped `describe` / `descriptorDistance` / `meanOf`, plus `describeWithEnergyFloor(L, R, sr, double floorDb)` (§6.4) | R-8: one implementation |
| Outcomes | `enum class ClaimRole : std::uint8_t { Secondary, Primary };` `struct CellOutcome { bool stateOk; bool conjunctOk; bool rendered; double d; double twoS; double attribBase; std::string skip; };` `[[nodiscard]] bool verifiedAt(const CellOutcome&, Verification, ClaimRole) noexcept` | `verifiedAt` dispatches on the cell's `Verification` kind (`cellSpecs()[c].verification`), per the table "`verifiedAt` by kind" in §6.12. The `d`, `twoS` and `attribBase` terms enter **only** for the render-scored kinds; a state-only kind never reads them. The raw terms are recorded, so any bar is re-evaluable offline. ODR: `ClaimRole` 0 hits; `CellOutcome` 0 hits (renamed from the first pass's `CellResult`, which has a function-local hit in `ecosystem_engine_longrun_test.cpp:2204`) |
| Vector | `struct VerificationVector { std::array<CellOutcome, static_cast<std::size_t>(Capability::Count)> cells; };` `std::optional<Capability> findWitness(const VoragoPresetDef& p, const VerificationVector& q, Capability qPrimary)` | §6.12. ODR 0 hits for both |
| Record | `struct TakeRecord { int seedIndex; bool finite; float peak; double worstHiDb, worstLoDb, lateVsSusDb, tailDb; std::array<PresetDescriptor, 3> minutes; };` `struct SweepRecord { std::string name; SweepTimeline tl; std::vector<TakeRecord> takes; PresetDescriptor mean; double selfDistance; double levelTwinD; VerificationVector vec; GestureResult gesture; RateResult rates; bool reproducible; };` `void writeRecord(const SweepRecord&, const std::filesystem::path&)`; `bool readRecord(const std::filesystem::path&, SweepRecord&)` | text `key value…` lines, doubles as `%.17g`. A record is a transient CI artifact: never committed and never compared across toolchains, so it is not a golden (C-8). ODR: `SweepRecord`, `TakeRecord`, `GestureResult`, `RateResult` 0 hits |
| Sharding | `struct Shard { std::size_t index, count; }; Shard shardFromEnv(); bool inShard(std::size_t defIndex, Shard)` | `VORAGO_SWEEP_SHARD=i/n`, default `0/1`; index `N` (= 40) is the default-surface pseudo-preset; a malformed value makes `shardFromEnv` return `{0, 0}`, and the calling TEST_CASE REQUIREs `count > 0`. ODR: `Shard` 0 hits |
| Env | `std::optional<std::string> sweepEnv(const char*)` | `getenv_s` under `_MSC_VER` (the probe's pattern, `ecosystem_rule_probe_test.cpp:186-197`), `std::getenv` elsewhere. Named differently from the probe's anonymous-namespace `readEnv`, which is left untouched (FR-070) |
| Pool | `void runJobs(std::vector<std::function<void()>>& jobs, unsigned threads)` | plain `std::thread` + join (not `std::jthread`); threads = `VORAGO_SWEEP_THREADS`, or `min(hardware_concurrency, 4)`; jobs never call Catch2 macros; results are asserted on the test thread |
| Memo | `const SweepRecord& sweepRecordFor(std::size_t defIndex)` | in-process, function-local static cache. With `VORAGO_SWEEP_IN` set it loads the record file instead of rendering (the aggregate job); with `VORAGO_SWEEP_OUT` set it writes each computed record |

`MultiParamChanges` stays in `vorago_test_fixture.h` for the Catch2 TUs. The host only ever needs one
point per ID at one offset, which `Krate::Test::ParameterChanges` provides.

### 5.9 `plugins/vorago/docs/index.html` — FR-062

Copy the structure of `plugins/seraphis/docs/index.html` and its `assets/` (directory listing read).
The page covers:
- what Vorago is;
- the twelve concept macros;
- the ecosystem view and the two rule knobs;
- the seven categories, one line each;
- the freeze-gesture instruction;
- system requirements;
- links.

`docs/.gitkeep` is removed once the page lands. `docs.yml` needs no edit (spec C-11).

### 5.10 Tooling and CI — FR-024, FR-063, FR-066, R-6

- **`check-preset-generator-determinism.js`.** Add `--plugin <name>`, default `seraphis`. A table
  `{seraphis: 'seraphis_preset_generator', vorago: 'vorago_preset_generator'}` builds the default
  binaries (`:53-55`), the not-found message (`:111-113`) and the temp prefix
  (`:209`, `${plugin}-presets-`). The default path is unchanged byte for byte (FR-024). USAGE is
  updated.
- **`run-cpu-tests.js:55`.** `FILTER` becomes
  `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'` (R-6).
- **Rosters.**
  - `release-readiness.js:14-22` gains `vorago: { testTarget: 'vorago_tests', bundle: 'Vorago.vst3' }`.
  - `SKILL.md` gains vorago in both its plugin list (`:15-16`) and its table (`:21-31`).
  - `node tools/lint-plugin-roster.js` is run afterwards.
- **`ci.yml`.** The three nightly filters `FILTER='[long]'` (`:369`, `:655`, `:1116`) become
  `FILTER='[long]~[vorago-sweep]'`. Nothing else in `ci.yml` changes, so the per-push lane is
  untouched.
- **`long-tests-nightly.yml`.** Two new jobs, both gated on `check-activity` like `long-tests`:
  ```yaml
  vorago-sweep:
    needs: check-activity
    if: needs.check-activity.outputs.should_run == 'true'
    strategy: { fail-fast: false, matrix: { os: [windows-2022, macos-latest, ubuntu-latest], shard: [0 .. n-1] } }
    runs-on: ${{ matrix.os }}
    timeout-minutes: 180
    steps: checkout; configure (the leg's ci.yml configure flags minus AU/ccache options);
           build --target vorago_tests;
           run: VORAGO_SWEEP_SHARD=${{ matrix.shard }}/n VORAGO_SWEEP_OUT=sweep-out
                <bin>/vorago_tests "[vorago-sweep]~[vorago-aggregate]" -d yes
           upload-artifact: vorago-sweep-${{ matrix.os }}-${{ matrix.shard }} (sweep-out/)
  vorago-sweep-aggregate:
    needs: [check-activity, vorago-sweep]
    if: ${{ !cancelled() && needs.check-activity.outputs.should_run == 'true' }}
    strategy: { fail-fast: false, matrix: { os: [windows-2022, macos-latest, ubuntu-latest] } }
    timeout-minutes: 180
    steps: build vorago_tests; download-artifact pattern vorago-sweep-${{ matrix.os }}-* merge-multiple;
           run: VORAGO_SWEEP_IN=sweep-in <bin>/vorago_tests "[vorago-aggregate]" -d yes
  ```
  The shard count `n` is ruled from the measured cost (§10) and recorded. `-d yes` puts every
  case's duration in the log (SC-013). The aggregate REQUIREs a record for every definition index
  `0…N`, so a missing shard fails loudly.

---

## 6. Algorithms (exact)

### 6.1 Timeline (C-6)

All values come from the typed decode:
- `A` = `mode == Growth ? growthDurationSeconds : (stage0 + stage1 + stage2 + stage3) / 1000`.
- `Rel = releaseMs / 1000`.
- `RT60 = effectiveCavernDecaySeconds(knob macros, decaySeconds)`.
- `Sus = M1 = [A+5, A+65]`, `M2 = [A+65, A+125]`, `M3 = [A+125, A+185]`, `H = A + 185`.
- `Tail`:
  - Freeze-On tail (the decoded freeze toggle, or the gesture render): `[H+Rel+10, H+Rel+70]`;
  - otherwise `[H+Rel+RT60+5, H+Rel+RT60+15]`.
- `Total = Tail.end`.
- Seconds convert to samples as `llround(t · sr)`.

**Stimulus:**
- NoteOn 36 at `kVelocity100` (sample 0, block 0, offset 0).
- NoteOff at sample `llround(H · sr)`, placed in its block at its exact in-block offset
  (`addNoteOff(36, offset)`).
- Block 512, sample rate 48 000.
- Take seeds and ablation overrides are delivered as offset-0 points in block 0, the probe's
  precedent. Parameter changes latch before events in `process()` (`processor.cpp:298`).

**Freeze gesture.** `kSpaceFreezeId` → 1.0 is delivered as an offset-0 point in the **first block
whose start sample is ≥ `llround((A+65)·sr)`**. That is at most 10.7 ms late, so the gesture render's
`[0, Sus.end]` is sample-identical to the ungestured stored-seed take.

### 6.2 Windows and RMS

- Stereo power of a span = `(ΣL² + ΣR²) / (2n)`; in dB, `10·log10(max(p, 1e-24))` (the probe's
  `stereoRmsDb`).
- **Windowing.** "Every 10 s window over [a, b]" means `k = floor((b−a)/10)` full windows from `a`,
  plus one more window right-aligned at `b` when there is a remainder. Every sample is covered and no
  window is shorter than 10 s.
- **Evaluation.** Windows are evaluated from the per-512-block power sums and snapped inward to block
  edges. The slack is at most 511 samples (10.6 ms) per edge.

### 6.3 Arms (C-6), per take

1. **Bounded over `[0, Total]`:** every sample finite by bit pattern; `peak ≤ 0.9661f`; every 10 s
   window ≤ −6 dBFS.
2. **Non-silence over `[A, H]`:** every 10 s window ≥ −60 dBFS.
3. **Late vs early:** `RMS([H−60, H]) − RMS(Sus)` ∈ `[−18, +12]` dB.
4. **Tail:**
   - Freeze Off: `RMS(Tail) ≤ RMS(Sus) − 40`.
   - Freeze On, over the six 10 s windows of the 60 s `Tail`: `last ≤ loudest + 1.0`,
     `last ≥ loudest − 6.0`, and `loudest ≥ RMS(Sus) − 20`.

Arms 1–4 run on **every** take of `A_K` (FR-033, Q4). `RMS(Sus)` is each take's own.

**Freeze-gesture protocol (FR-033, SC-024; S8 preset only; stored seed only):**
- **G:** the gesture render, the stored-seed take with the gesture of §6.1, rendered to
  `[0, H+Rel+70]`. It is scored by arm 1 over its whole length and by arm 4's Freeze-On criteria,
  including the floor, where `RMS(Sus)` is taken from the **ungestured** stored-seed take.
- **G₀:** the same render with `kSpaceMixId` (1105) → 0.0 added at block 0. Its loudest 10 s window
  over the same `Tail` must be ≤ `RMS(Sus) − 40`.

The record holds G's loudest, last and floor figures, G₀'s loudest, and the difference G − G₀. Per
Render identity, nothing else reads G or G₀.

**Authoring rule.** No factory preset stores Freeze On. If one ever does, it is scored by arm 4's
Freeze-On criteria on its own render with no gesture; failing the floor is an FR-017 finding, never a
processor fix (FR-033).

### 6.4 Descriptor (C-7.2) — shipped, plus one overload

`describe` (part 0) is the C-7.2 descriptor exactly:
- bands relative to the sub-free `E_hi = E(80, 20000)`, clamped at −60 dB, then /3;
- `log2`-floored motion and flux;
- correlation /0.25;
- one-second stereo dB spread /2;
- crest /3.

The attack window (§6.8) needs the same descriptor with the one-second stereo dB values clamped
below. Part 0 is refactored without changing what `describe` computes:
```cpp
namespace detail {
[[nodiscard]] inline PresetDescriptor describeImpl(std::span<const float> L, std::span<const float> R,
                                                   double sr, std::optional<double> energyFloorDb);
}
[[nodiscard]] inline PresetDescriptor describe(std::span<const float> L, std::span<const float> R,
                                               double sr) {
    return detail::describeImpl(L, R, sr, std::nullopt);             // body moved verbatim
}
/// C-7.4 D_att: each one-second stereo dB value feeding `e` is clamped below at
/// floorDb (= RMS(Sus_P) - 60 dB) before the spread is taken.
[[nodiscard]] inline PresetDescriptor describeWithEnergyFloor(std::span<const float> L,
                                                              std::span<const float> R, double sr,
                                                              double floorDb) {
    return detail::describeImpl(L, R, sr, floorDb);
}
```
`std::optional` is used, not a −∞ sentinel, because the generator and the macOS leg build with
`-ffast-math`, which assumes no infinities. A per-push unit case asserts that `describe` returns
bit-identical components for a fixed synthetic signal before and after the refactor. The expected
values are captured from the shipped function in the same binary, not from a stored digest (C-8).

### 6.5 Take sets, `D(P)`, `s(P)`, `t_K` (C-7.3, P2-2)

- Stored seed `s` = the decoded `seedIndex`. Take `j` of set X ∈ {A, B} renders at seed index
  `takeSeedIndex(s, X, j, K)`.
- Per take `j`: minute descriptors `m_j^1, m_j^2, m_j^3 = describe(M1), describe(M2), describe(M3)`.
- `D(P)` = the component-wise mean of the 3K descriptors in `A_K`
  (`meanOf` over the three minute means).
- `s(P) = max(d(m̄¹, m̄²), d(m̄¹, m̄³), d(m̄², m̄³))`, where `m̄ⁱ = meanOf_j(m_j^i)` over `A_K` (Q4).
- `t_K(P) = d(D_A(P), D_B(P))`, rendered only for the pilot and the control set.
- **Twin self-distance.** For a single-take twin rendered only through `Sus` (ablation, E routes),
  `s` = `d(describe(first 30 s of Sus), describe(last 30 s))` of that one render.

### 6.6 Distance and floor

- `d(P, Q)` = `descriptorDistance(D(P), D(Q))` over the 14 scaled components.
- Pair floor: `d(P, Q) ≥ max(4.0, 2·max(s(P), s(Q)), 2·t_max)`, where `t_max` = the largest `t_K`
  over the control set (§6.13).
- The printed summary: min / median / max `d`, the minimum pair's names, every `s(P)`, `t_max`, the
  ruled K, and the effective floor.

### 6.7 Ablation twins (C-7.4) — overrides in normalized units, single take at the stored seed

The twin is `setState(P)` plus the overrides as offset-0 points in block 0, rendered on P's timeline
to `Sus.end`. `P_Sus` is `describe(M1)` of P's **stored-seed** take.

| Cell | Override |
|---|---|
| S1 | 300 → 0.0 |
| S2 | 401 → 0.0 |
| S3 | 700 → 0.0, 701 → 0.0 |
| S4 | 500 → 0.0 |
| S5 | 610, 611, 612 → 0.0; 600 → 0.0 |
| S6 | 1300 → 0.0 |
| S7 | 900 → 0.0 |
| S8 | 1105 → 0.0 |
| S9 | 1400 → 0.0 |
| S10 | 1003 → 0.0 |
| M m (m = 1…12) | `99 + m` → its registered default (0.0; Gravity 104 → 0.5) |
| E6.hi | 901 → 0.0 (registered default, `syncRate` 0) |
| E7.hi | 902 → 0.25 (registered default, `selfAffinity` −1) |
| D13.x (reversion) | 800 → 0.5 (the 1.0× default, exact log midpoint, `events_params.h:7`) |
| D14.1 / D14.2 (depth ablation) | 1500 → 0.0 / 1502 → 0.0 |
| D1.x as a **primary** (per-material reversion) | each of 1004 / 1005 that holds material x → its registered default index (5 / 6), normalized `indexToNormalized(5, 11)` / `(6, 11)` |
| D8.2 / D9.1 as primaries | attack-window reversion (§6.8), never `Sus` |

- **Verification.** `d = d(P_Sus, describe(twin M1))`, `twoS = 2·s(P)` (P's K-take `s`, Q4).
  - Claimed as primary: verified iff `d ≥ max(4.0, twoS)`.
  - Claimed as secondary: verified iff `d ≥ max(1.5, twoS)`.
  - Unclaimed: the raw terms are recorded (FR-012, FR-037).
- **M foregrounding.** An M claim also needs its state conjunct: the stored macro is displaced from
  its default by ≥ 0.5 (Gravity: `|g − 0.5| ≥ 0.35`).
- **E-ext side predicate (FR-075, Q7).** `E6.hi` needs decoded normalized 901 ≥ `0 + 0.5·(1 − 0)` =
  0.5 (plain ≥ 0.25). `E7.hi` needs 902 ≥ `0.25 + 0.5·0.75` = 0.625 (plain ≥ +0.5). The reversion is
  scored only when the predicate holds.

### 6.8 Envelope cells — attack-window reversion (C-7.4)

This twin is rendered for the D8.2 and D9.1 **primaries** only, and only the primary verdict reads
it. As secondaries, and in every vector entry, D8.x and D9.x are *always-audible* state cells (C-2.1
D8/D9 rows; §6.12 "`verifiedAt` by kind"). The two default-state members, D8.1 and D9.2, could not be
scored this way anyway: their reversion to the registered envelope is P itself (`d = 0`).

**The twin.** `P_rev` = P with its envelope IDs reset to registered defaults, at the same seed and
stimulus:
- D8.2 (Growth): 1200 → 0.0 (Standard);
- D9.1 (fast attack): 1201–1204 → their registered defaults, and 1200 → 0.0 if Growth is stored.

`A_rev` comes from `P_rev`'s decode (the override applied to the decoded state).

**Windows and renders.**
- `W_end = max(A_P, A_rev) + 5 s`.
- `P_rev` is rendered from t = 0 to `max(W_end, A_rev + 65)`, capturing `[0, W_end]` and its own
  `Sus_rev = [A_rev+5, A_rev+65]`.
- `P`'s `[0, W_end]` is captured from its stored-seed main take. `W_end ≤ 185 s ≤ Total`, so no extra
  render is needed. The capture window is added to that take's `RenderSpec` only for the D8.2 and
  D9.1 primary presets.

**Descriptors.**
- `floorDb = RMS(Sus_P) − 60`.
- `D_att(X) = describeWithEnergyFloor(X[0, W_end], floorDb)`.
- `d_att = d(D_att(P), D_att(P_rev))`.
- `d_Sus = d(describe(Sus_P), describe(Sus_rev))`.

**Verdict.** The cell verifies iff `d_att ≥ max(bar, 2·s(P))` **and** `d_att ≥ d_Sus + 1.5`. This is
recorded in `CellOutcome` as `d = d_att`, `attribBase = d_Sus`.

**Printed.** For each render, the time to first reach `RMS(Sus) − 6 dB`: the first one-second window
at or above it, from `blockRmsDb` at 48 000.

### 6.9 Route-isolated E arms (C-2.1 Group E) — single take at the stored seed

Destinations: E1↔S6, E2↔S2, E3↔S1, E4↔S4, E5↔S9.
- For each k:
  - `R_k` = P + the S-overrides (§6.7) of the other four destinations;
  - `R_k⁰` = `R_k` + `900 → 0.0`.
- `R_∅` = P + all five destination overrides; `R_∅⁰` = `R_∅` + `900 → 0.0`.
- Every render uses P's timeline and runs to `Sus.end`. That is 12 renders per preset.

E_k is verified iff `d(R_k, R_k⁰) ≥ max(bar, 2·s(R_k))` **and** `d(R_k, R_k⁰) ≥ d(R_∅, R_∅⁰) + 1.5`,
where `s(R_k)` is the half-window self-distance of `R_k`. In `CellOutcome` terms: `twoS = 2·s(R_k)`,
`attribBase = d(R_∅, R_∅⁰)`.

### 6.10 D predicates (decoded state, FR-031)

| Cell | State predicate | Conjunct |
|---|---|---|
| D1.x | (`materialA == x` and `blend ≤ 0.65`) or (`materialB == x` and `blend ≥ 0.35`) | S10 (secondary bar); as a primary, also the per-material reversion (§6.7) at F |
| D2 | `0.35 ≤ blend ≤ 0.65` | S10 |
| D3.m | some slot has `model[s] == m` | S1 |
| D4.t | some slot has `model[s] == Direct` **and** `type[s] == t` | S1 |
| D5.a | `anchorMode == a` | S2 |
| D6.f | some loop has `loopFilterMode[l] == f` | S4 |
| D7.t | the stored level of tone t is **strictly** the loudest of `div2LevelDb`, `div4LevelDb`, `fifthBelowLevelDb` (ties fail) | S5 |
| D8.s | `mode == s` | always audible; a primary is scored by §6.8 |
| D9.1 / D9.2 | `A ≤ 10` / `A ≥ 90` | always audible; a primary (D9.1) is scored by §6.8 |
| D10.1 | P is the S8-primary preset | §6.3 gesture protocol: G passes arm 1 and Freeze-On arm 4, and G₀ passes −40 dB |
| D10.2 | `freeze == 0` | default state |
| D11 | `ghostReverseProbability ≥ 0.5` | S9 |
| D12.1 / D12.2 | `eventTriggers == 1` / `== 0` | S9 / default state |
| D13.1 / D13.2 | `eventRateScale ≤ 0.3` / `≥ 3.0` | S7 **and** the reversion (800 → 0.5) at the role bar |
| D14.1 / D14.2 | `breathingDepth ≥ 0.7` / `tidalDepth ≥ 0.7` | the depth → 0 ablation at the role bar |
| E6.hi / E7.hi | §6.7 side predicate | the knob reversion at the role bar |

"Conjunct S*n*" means `verifiedAt(vec[Sn], Verification::Ablation, ClaimRole::Secondary)`, i.e. `R(1.5)` of §6.12, on the **same** preset's vector.

### 6.11 Full verification vector and the exact skip rules (FR-012, FR-037)

The vector is computed once per preset and once for the default-surface pseudo-preset. A render is
skipped **only** when its outcome is known exactly, and the skip reason is recorded:
- **Override equals the stored value** for every overridden ID: the twin *is* P, so the cell records
  `d = 0` and is not verified.
- **M cell whose displacement is below the threshold:** the state conjunct fails.
- **E1–E5 with 900 stored at 0.0:** `R_k ≡ R_k⁰`.
- **D cell with a false state predicate:** the conjunct is not evaluated.
- **E-ext cell with a false side predicate.**

These skips are a **plan ruling, not a spec ruling**. FR-037 asks for a render per every S/M cell
and ablation-conjunct D cell, "recorded regardless", and C-10 absorbs cost "never by narrowing the
vector". The verdicts are identical either way. The skips lose only the raw `d` of the last four
kinds, which the matrix prints as the `skip` reason. They await acknowledgement (§12 item 5). If the
ruling is "render regardless", the conditions above stop gating renders and only gate verdicts, and
§10's no-skip figure applies.

### 6.12 Non-subset (FR-011a) and coverage (FR-013)

**One rule.** Q verifies cell `c` iff `verifiedAt(Q.vec[c], kind(c), ClaimRole::Secondary)`, where
`kind(c) = cellSpecs()[c].verification`. For `c == Q.primary`, the entry must also satisfy
`verifiedAt(Q.vec[c], kind(c), ClaimRole::Primary)`, which Q's own shard case already requires. No
other cell is judged at F in this check. The same rule decides coverage, the default-state set (on the
pseudo-preset) and every claim check in `Vorago_PresetSweep_AblationVerifiesClaims`.

**`verifiedAt` by kind.** Let `R(bar)` = `stateOk && conjunctOk && rendered && d ≥ max(bar, twoS) &&
(attribBase < 0 || d ≥ attribBase + 1.5)`, the render-scored predicate. Secondary uses bar 1.5,
primary uses bar F = 4.0.

| `Verification` | Cells | `ClaimRole::Secondary` | `ClaimRole::Primary` |
|---|---|---|---|
| `Ablation` | S1–S10, M1–M12 | `R(1.5)`; S: `stateOk = conjunctOk = true`; M: `stateOk` = the displacement conjunct (§6.7) | `R(4.0)` |
| `RouteIsolated` | E1–E5 | `R(1.5)`, `attribBase = d(R_∅, R_∅⁰)` (§6.9) | `R(4.0)` |
| `ExtReversion` | E6.hi, E7.hi | `R(1.5)`, `stateOk` = the side predicate (§6.7) | `R(4.0)` |
| `StateWithReversion` | D13.x, D14.x | `R(1.5)`, `stateOk` = the state predicate, `conjunctOk` = S7's secondary verdict (D13) or `true` (D14), `d` = the reversion / depth ablation (§6.7) | `R(4.0)` (no §7 row uses it) |
| `StateWithS` | D1–D7, D11, D12.1 | `stateOk && conjunctOk`, where `conjunctOk` = the named S cell's secondary verdict on the same vector (§6.10). **No `d` term.** | D1.x only: `stateOk && conjunctOk && rendered && d ≥ max(4.0, twoS)`, with `d` = the per-material reversion (§6.7). Any other `StateWithS` cell: `false`. That case is reachable only after the "Required primaries" stop below, which would have to rule its reversion first |
| `AttackWindow` | D8.x, D9.x | `stateOk` (always audible; `conjunctOk = true`). **No `d` term.** | D8.2, D9.1: `R(4.0)` with `d = d_att`, `attribBase = d_Sus` (§6.8) |
| `FreezeGesture` | D10.1 | `stateOk && conjunctOk`, where `stateOk` = "P is the S8-primary preset" and `conjunctOk` = G passes arm 1 and Freeze-On arm 4 including the floor, **and** G₀ passes its −40 dB bound (§6.3). **No `d` term.** | `false` (FR-011b) |
| `StateOnly` | D10.2, D12.2 | `stateOk` | `false` (default-state by construction) |

For every entry a state-only rule scores, the vector records `rendered = false`, `d = 0`, `twoS = 0`
and `skip = "state-only kind"`. These values are never read. Every render-scored entry keeps its raw
terms.

**Witness search.** For each ordered pair (P, Q), P ≠ Q, scan P's claims: the primary first, then the
secondaries in definition order. The witness is the first claim that Q does not verify. No witness is
a failure ("SUBSET"). Output is printed per P: `P vs Q: witness <cell>` or `P vs Q: SUBSET`.

**`findWitness` is unit-tested per push** on hand-built vectors (`Vorago_PresetMatrix_NonSubsetRule`):
- P's only claim `X` sits in Q with `1.5 < d < 4.0` and every conjunct true: SUBSET.
- The same with `d = 1.0`: witness `X`.
- The same with `2·s(Q) > d`: witness `X`.
- P's only claim is a `StateWithS` cell (D3.2) that Q holds with its S1 conjunct true, `rendered =
  false` and `d = 0`: SUBSET (the state-only rule has no `d` term).
- The same with Q's S1 conjunct false: witness D3.2. With Q's state predicate false: witness D3.2.
- P's only claim is a `StateOnly` cell (D12.2): `stateOk` true in Q gives SUBSET, false gives witness
  D12.2.
- P's only claim is an `AttackWindow` secondary (D9.2) with `stateOk` true in Q and no render: SUBSET.
- P's only claim is D10.1 (`FreezeGesture`) and Q is not the S8 preset (`stateOk` false): witness
  D10.1.
- `verifiedAt` at `ClaimRole::Primary` on a D1 entry with state and S10 true: `d = 3.9` gives
  `false`; `d = 4.1` with `twoS ≤ 4.1` gives `true`. A D3 entry at `Primary` gives `false` whatever
  `d` holds.

**Coverage** is counted over factory presets only (indices `0…39`). The pseudo-preset (index 40) only
marks the default-state set. `Vorago_PresetMatrix_CoverageComplete` asserts:
- every cell has at least one factory verifier;
- E1–E5, `E6.hi` and `E7.hi` are each the verified primary (at 4.0) of a distinct preset, seven
  presets in total;
- no primary is in the measured default-state set;
- no preset's primary is D10.1 (FR-011b);
- the measured default-state set equals `kRecordedDefaultStateCells`, the constant the per-push
  `Vorago_PresetDefs_ClaimsWellFormed` checks primaries against. P2-6's prediction is the constant's
  initial value; it is corrected from the first measured run, before authoring;
- the set of primaries equals `requiredPrimaryCells()` computed from the **measured** default-state
  set, not only from the recorded constant.

**Required primaries (SC-029).** `requiredPrimaryCells()` = S1–S10 ∪ M1–M12 ∪ E1–E5 ∪ {E6.hi,
E7.hi} ∪ ((D1.* ∪ D3.* ∪ D8.* ∪ D9.*) \ `kRecordedDefaultStateCells`). These are the 11 body
materials, the 4 noise models and the D8/D9 envelope extremes SC-029 names. With P2-6's predicted
set the count is 29 + (11 − 2) + (4 − 4) + (2 − 1) + (2 − 1) = **40**. The per-push
`Vorago_FactoryPresets_LibraryShape` derives its required set from this function. It does not keep a
hard-coded list.

**Stop rule.** SC-029 fixes N = 40. If the measured default-state set makes
`|requiredPrimaryCells()| ≠ 40`, that is an **FR-017 stop**, surfaced with the measured set before
any authoring. The plan does not choose between N and the primary list. Two examples: S1 fails on the
default surface, so D3.1–D3.4 need noise-model primaries (44); or S10 fails there, so
D1.StoneChamber and D1.SteelTank need primaries (42). The same stop applies if a later nightly
measures a different set: `kRecordedDefaultStateCells` is not re-recorded in a way that changes the
count until the stop is ruled.

### 6.13 Controls (C-7.3; P2-3)

The control set C is printed, deduplicated, and holds at least 3 presets:
- the argmax and argmin of `s(P)`;
- the S8 preset (the D10.1 host);
- the preset with the highest stored Pressure;
- the preset with the highest stored Weight.

| Control | Where | Rule |
|---|---|---|
| (a) level twin | every preset, in its shard | `d(meanOf(minutes of the stored-seed take), meanOf(the same buffers × 10^(−6/20)))` < 0.05 |
| (a′) gain twin | highest-Pressure preset, aggregate job | stored-seed render with 0 → `0.5 × stored normalized`, M1…M3 mean vs the stored-seed take's M1…M3 mean: `d ≤ 2.0` |
| (b) seed twin | each C preset, aggregate job | render the `B_K` set (K takes); `t_K = d(D_A, D_B)` ≤ 2.0; `t_max` = max over C |
| (c) sub twin | each C preset, aggregate job | stored-seed render with 600 at `stored ± 0.125`; each in-range side, M1…M3 mean vs the stored-seed take's: `d < 4.0`; a side leaving [0, 1] is not rendered and the sides run are recorded |

Any control failure is an FR-017 stop. Nothing is widened.

### 6.14 Parameter space (C-7.1)

- Initialize one `Vorago::Controller`. For each preset, `setComponentState(comp)`, then read
  `getParamNormalized(id)` for every persisted ID: all registered IDs except 4 and 5, 108 IDs after B.
- The default surface is the freshly initialized controller.
- List IDs are those with `ParameterInfo::stepCount > 0`. Two values differ iff
  `round(n·stepCount)` differs. Continuous IDs differ iff `|Δn| ≥ 0.10`.
- Every pair, and every preset against the default, needs ≥ 8 differing IDs. The minimum pair count
  is printed.

### 6.15 Committed-tree tolerance (C-9, FR-032)

`Vorago_FactoryPresets_TreeToleranceProbe` (`[.measure]`) regenerates each preset through
`buildPresetComponentState` and prints, per float field, the worst `|c − r| / max(|c|, 1e-30)`.
- **Toolchains.** It runs on MSVC and WSL/GCC locally. On AppleClang it runs through one
  `workflow_dispatch` run with a temporary step. The step is removed once the figure is recorded, and
  the run URL goes into the compliance record.
- **Pin.** `max(10 × worst over all three, 1.19e-7)`.
- **What must match exactly.** Int fields, `Info` bytes, version (3), length (436) and the path set.
- **Printing.** The enforcing test prints its per-field worst on every leg. A later breach is
  re-measured on all three toolchains and recorded as a ruling, never widened silently.

### 6.16 Pilot, E0 and the K ruling (FR-017a, G2)

**Pilot set.** Seven presets plus one near-variant:
- P0: the default surface (E0);
- P1: E6.hi candidate "Locked Choir";
- P2: E7.hi candidate "Clotting Colony";
- P3: S5 "Tectonic Floor";
- P4: S8 "Cathedral Void";
- P5: D8.2 "Growth Ring";
- P6: D1.Glass "Glass Well";
- plus P3′, a deliberately authored near-variant of P3 (sub offset +6 dB and one small section
  tweak).

**Steps.**
1. **E0.** Render P0 at all 16 seed indices (`A_8 ∪ B_8`) to `A + 185` (340 s), capturing M1–M3. The timeline is hard-coded in the pilot TU from the shipped defaults, because `makeTimeline` does not exist until C2: `A` = (20 000 + 30 000 + 45 000 + 60 000) ms = 155 s (`vorago_voice.h:316-342`, Standard mode), `M1` = [160, 220] s, `M2` = [220, 280] s, `M3` = [280, 340] s, end = `H` = 340 s. No NoteOff and no tail are rendered. After C2, `Vorago_PresetPilot_Calibrate` asserts that `makeTimeline(decode(default getState))` gives the same `A`, `M1`–`M3` and `H`.
   Compute `t_K` for K = 1, 2, 4, 8. Cross-check: `t_1` must equal the 13b `t0on` 5.9515 within
   `compareFingerprints`-grade tolerance of the descriptor (same binary; both sides render the
   shipped processor), and a mismatch is itself surfaced. If `2·t_8 > 4.0`, **STOP** and surface the
   curve (FR-017a); no pilot preset is authored.
2. Render P1–P6 at 16 seed indices each, and compute `t_K` curves the same way.
3. **Rule K** = the smallest K ∈ {1, 2, 4, 8} with `2·t_K ≤ 4.0` for **every** P0–P6. None found:
   STOP and surface all curves. K is never raised past 8, F is never raised, and nothing is silently
   retried.
4. With K fixed:
   - `d(P3, P3′)` over `A_K` must be < 4.0. At ≥ 4.0: STOP (Q6), and the pair never sets F.
   - Score P1 and P2 at the primary bar through their §6.7 reversions (`s(P)` from `A_K`). Below the
     bar: STOP (Q5, FR-017).
   - Record every pilot preset's `s(P)` and its claimed primary's `d`.
5. Freeze `kRuledTakes = K` in `preset_test_support.h`. Record K, the curves, the pair `d` and the
   candidate scores in `compliance.md`.

The pilot TU is `integration/preset_pilot_test.cpp`, `TEST_CASE("Vorago_PresetPilot_Calibrate",
"[.probe][vorago]")`. It is hidden, run once by hand, alone, with its log saved under `artifacts/`.
E0 is `TEST_CASE("Vorago_PresetPilot_DefaultSurfaceTakeCurve", "[.probe][vorago]")` in the same TU.

### 6.17 Preset CPU (FR-041, R-6)

`[.perf]`, run only via `node tools/run-cpu-tests.js vorago_tests`. For each preset and for the
default surface:
1. Host at 48k/512, `setState`, `kPolyphonyId → 0.6` (index 3, 4 voices).
2. NoteOn for all four `kCpuNotes` at t = 0.
3. Untimed pre-roll to the patch's own `A + 5 s`.
4. Time 16 trials × 100 blocks with `std::chrono::steady_clock`, **interleaving** one preset block
   and one default block.

The default host is re-pre-rolled whenever its next block would leave its own `Sus`.

- **Gate:** `min-trial(preset) / min-trial(default) ≤ 1.15` for the worst preset.
- **Printed, not gated:** the absolute figure against `kReferenceNs` (3 200 000 ns) and the
  stored-polyphony figure.

---

## 7. Matrix-derived library (FR-016) — N = 40

**Derivation.**
1. Enumerate the 79 cells (the `Capability` enum: 10 S + 12 M + 7 E + 50 D).
2. Run the default surface through the harness to get the default-state set (P2-6 predicts it).
3. Make every S, M and E cell a primary, including `E6.hi` and `E7.hi`: 29 presets.
4. Make every cell of `(D1.* ∪ D3.* ∪ D8.* ∪ D9.*) \ default-state` a primary
   (`requiredPrimaryCells()`, §6.12). With P2-6's predicted set, that is the 9 non-default D1
   materials, D8.2 and D9.1, because D3.1–D3.4 are predicted default-state: 11 presets. The total is
   **40** (SC-029). A measured set that moves this count is an FR-017 stop (§6.12), not a re-count.
5. Attach every remaining cell, including every default-state cell, as a **secondary** to a preset
   whose section it needs (its S conjunct). Coverage counts factory presets only.
6. File each preset by how it sounds.
7. Check FR-004 (≥ 3 per category).

The list is provisional: names and categories move if the harness or listening says so. The
primaries do not.

| # | Primary | Preset | Secondaries (plan) | Category |
|---|---|---|---|---|
| 1 | S1 | Wind Through Basalt | D3.1 Direct, D3.2 FilteredWind, D4.1–D4.3 (3 Direct slots + 1 FilteredWind) | Textures |
| 2 | S2 | Resonant Shaft | D5.2 Keyed | Caverns |
| 3 | S3 | Smeared Horizon | D8.1 Standard, D10.2 freeze off | Textures |
| 4 | S4 | Feedback Mire | D6.1 Lowpass, D6.2 Bandpass, D6.3 Highpass (one loop each at least) | Machines |
| 5 | S5 | Tectonic Floor | D7.3 fifth-below | Abyss |
| 6 | S6 | Slow Bloom | D14.1 breathing, D9.2 slow attack | Drones |
| 7 | S7 | Colony Pulse | D13.2 fast events | Organisms |
| 8 | S8 | Cathedral Void | **D10.1** freeze holds (gesture; `Comment` instructs "engage Freeze once the drone has bloomed") | Caverns |
| 9 | S9 | Choir of Absence | D11 reverse, D12.1 triggers on (additive, FR-061) | Ghosts |
| 10 | S10 | Hull Resonance | D2 blend, D1.StoneChamber, D1.SteelTank (default materials, blend in [0.35, 0.65]) | Machines |
| 11 | M1 | Lightless | D12.2 triggers off | Abyss |
| 12 | M2 | Erosion | D3.3 GranularDust, D4.4–D4.6 | Textures |
| 13 | M3 | Crowded Dark | — | Drones |
| 14 | M4 | Drifting Strata | D14.2 tidal | Drones |
| 15 | M5 | Stone Gravity | D5.3 Hybrid (Gravity acts through Hybrid) | Abyss |
| 16 | M6 | Entropic Hum | — | Machines |
| 17 | M7 | Pressure Front | — | Machines |
| 18 | M8 | Weighted Deep | D7.2 f/4 | Abyss |
| 19 | M9 | Fogbound | D4.10–D4.12 (verifies S1 as a secondary) | Ghosts |
| 20 | M10 | Teeming | D13.1 slow events | Organisms |
| 21 | M11 | Endless Descent | — | Caverns |
| 22 | M12 | Monolith | D7.1 f/2 | Drones |
| 23 | E1 | Bloom Colony | — | Organisms |
| 24 | E2 | Singing Colony | D5.1 Free | Organisms |
| 25 | E3 | Swarm Breath | D3.4 MetallicHiss, D4.7–D4.9 | Textures |
| 26 | E4 | Feeding Loops | — | Machines |
| 27 | E5 | Haunted Colony | — | Ghosts |
| 28 | E6.hi | Locked Choir (sync 901 ≥ 0.5) | — | Organisms |
| 29 | E7.hi | Clotting Colony (self affinity 902 ≥ 0.625) | — | Organisms |
| 30 | D1.Glass | Glass Well | — | Caverns |
| 31 | D1.Strings | Strung Abyss | — | Drones |
| 32 | D1.MetalPlate | Iron Plate | — | Machines |
| 33 | D1.Chamber | Chamber Drone | — | Drones |
| 34 | D1.Ice | Ice Shelf | — | Textures |
| 35 | D1.WoodenHull | Hull Ark | — | Drones |
| 36 | D1.CathedralColumn | Column Hymn | — | Caverns |
| 37 | D1.CavernWall | Cavern Wall | — | Caverns |
| 38 | D1.GlassSphere | Glass Sphere | — | Ghosts |
| 39 | D8.2 | Growth Ring | — | Organisms |
| 40 | D9.1 | Sudden Chasm | — | Abyss |

**Category counts:**

| Category | Count |
|---|---|
| Textures | 5 |
| Caverns | 6 |
| Machines | 6 |
| Abyss | 5 |
| Drones | 7 |
| Organisms | 7 |
| Ghosts | 4 |

Every category holds ≥ 4, so FR-004 has margin for a later re-filing.

**Fixes the first-pass table needed.** The first-pass S1 row asked for four Direct slots plus a
FilteredWind slot, which does not fit in four slots. The twelve D4 types are therefore spread over
four S1-verifying presets, three Direct slots each: S1, M2, E3 and M9. D4.6 (Brown, index 5) is the
default type, so it is a default-state cell and M2 carries it as a secondary. D4 labels are 1-based,
as in the spec (D4.1–D4.12; D4.*t* = `kNoiseTypeByIndex[t−1]`).

**Authoring constraints on every row:**
- polyphony index ≤ 3, normalized ≤ 0.6 (FR-007);
- `A ≤ 180 s`, `Rel ≤ 60 s` (FR-008);
- Freeze stored Off;
- no point for ID 4 or 5;
- output saturation only through its MB base (C-5);
- no `" & < >` in the description (P-7);
- at least 8 IDs displaced by ≥ 0.10 from the default and from every other preset (C-7.1).

---

## 8. Test plan

Every new TU is listed in `plugins/vorago/tests/CMakeLists.txt` (FR-027a, §9).

| Lane | Tags |
|---|---|
| per-push | `[vorago][preset]` |
| sharded nightly | `[vorago][preset][long][vorago-sweep]` |
| aggregate | the sharded tags plus `[vorago-aggregate]` |
| hand-run | `[.probe]`, `[.measure]`, `[.perf]` |

**Tolerances:**
- descriptor thresholds are the spec's (F 4.0, D_abl 1.5, 0.05, F/2);
- reproducibility uses `compareFingerprints` at the shared constants (`render_fingerprint.h:58-63`);
- the tree test uses the pinned measured tolerance (§6.15);
- no bit-exact float golden anywhere (C-8).

| FR / SC | File | TEST_CASE | Assertion strategy |
|---|---|---|---|
| FR-071a / SC-026a | `unit/ecosystem_roster_test.cpp` (new) | `Vorago_EcosystemRosterReachesEngine` | defines the SC-026a probe (§5.2). Pass 1, no note: 901 → 1.0, 902 → 1.0, one `process()`; for v = 0…5 `getVoice(v).ecosystem().getSyncRate() == 0.5f` and `getAffinity(k, k) == 2.0f` for all five kinds; off-diagonal still `0.45f` (the knob writes the diagonal only); voices checked == 6. Pass 2: the same with a note held. Pass 3: 901 → 0.0, 902 → 0.25 gives `0.0f` / `-1.0f`. Exact float equality is valid: linear taper at the range ends / midpoint-quarter, exactly representable |
| FR-076 / SC-027 | same | `Vorago_VoiceParams_FieldCount` | `static_assert(Krate::DSP::VoragoVoiceParams::kFieldCount == 33)`; `VoragoVoiceParams{}.ecosystemSyncRate == EcosystemEngine{}.getSyncRate()` and the diagonal default, on a heap-constructed engine |
| FR-072 / SC-027 | `unit/state_v3_test.cpp` (new) | `Vorago_StateRoundTripV3` | `getState` length == 436; set/get byte-identical; version int == 3 |
| FR-072 / SC-027 | same | `Vorago_State_V2LoadsWithRosterDefaults` | drive 901/902 to 1.0 (counted extremes), `process()`; build the v2 stream (§5.2), `setState`, `process()`; decode `getState` → 0.0 / −1.0; engine getters on all 6 voices (via the probe) == 0.0 / −1.0 |
| FR-072 | same | `Vorago_State_V3TruncatedKeepsPrefix`, `Vorago_State_V3NonFiniteKnobRejected` | a stream cut after the life pack leaves the knobs unchanged; a NaN bit pattern leaves its field unchanged |
| FR-072 / FR-074 | `unit/state_v2_test.cpp` (updated) | `Vorago_StateRoundTripV2` → legacy load | v2 streams load; every v2 field matches |
| FR-074 | `unit/state_roundtrip_test.cpp` (updated) | `Vorago_StateRoundTrip` | sizes and `memcmp` at `kStateV3Bytes`; version 3; version 4 rejected |
| FR-074 | `unit/params/ecosystem_params_test.cpp` | `Vorago_EcosystemParamsContract` (extended) | per knob: registered default == engine default; normalized ↔ plain round-trip within 1e-6; clamp; format string |
| FR-074 | `unit/param_table_test.cpp` + `param_table_expected.h` | `Vorago_ParamIdMap`, `Vorago_RouteTable`, `Vorago_ParameterInfoTable`, `Vorago_ParamInputHygiene` | rows for 901/902; VP count 33; NaN/Inf normalized inputs on both IDs |
| FR-071a (dsp) | `dsp/tests/unit/systems/vorago_param_surface_test.cpp` | new `VoragoVoice_EcosystemRuleForwarders`; `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short` (`STATIC_REQUIRE` 33), `VoragoEngine_ApplyVoiceParamsReachesAllSlots` (extended) | the forwarders reach `ecosystem().get*()`; non-finite values rejected; self affinity writes the five diagonal entries only; the default broadcast stays within `compareFingerprints` of no broadcast |
| FR-073 / SC-028 | `unit/controller/editor_layout_test.cpp` | `Vorago_UidescBindsEverySurfaceId`, `Vorago_UidescTagTable`, `Vorago_UidescViewClassRule`, `Vorago_UidescLayout`, new `Vorago_Ecosystem_PageBindsRosterIds` | counts through named constants; page 6 contains 901 and 902; the allowlist is still {4, 5}; the new knobs sit inside the 1100 × 296 page with no overlap |
| FR-060 / SC-030 | `unit/tail_samples_test.cpp` (new) | `Vorago_Processor_GetTailSamplesMatchesState` | expected = `llround(tailSeconds(...)·sr)` from **independently decoded** state (`decodePresetState` of `getState`), ±1 sample, for: (i) decay 0.5 s → (45 + 0.5 + 30)·48000 = 3 624 000; (ii) decay 60 with Depth = Age = 1 → clamp at 60 → 135 s = 6 480 000; (iii) Freeze On → `kInfiniteTail`; (iv) default → 95 s = 4 560 000; (v) every factory preset. Also `kGhostGrainCeilingSeconds == AtmosphereEngine::kMaxGrainSeconds` |
| FR-001 / SC-001 | `unit/preset/factory_preset_test.cpp` (new) | `Vorago_FactoryPresets_CategoriesMatchConfig` | config list == `kCategories` == C-1 order; dirs == list both ways; no stray file |
| FR-003, FR-028 / SC-002 | same | `Vorago_FactoryPresets_ContainerAndInfo` | FR-028 fields; `Info` bytes equal to `buildVoragoInfoXml`; **and**, independently of the helper, each preset's parsed attributes against literals: `MediaType == "VstPreset"`, `PlugInName == "Vorago"`, `PlugInCategory == "Synth"`, `Name` == the file stem, `MusicalCategory == MusicalInstrument ==` the parent directory name, `Comment` == the definition's description (absent iff empty), in the attribute order of `preset_manager.cpp:266-275`; no `" & < >` in `Comment` |
| FR-003 / SC-002 | same | `Vorago_FactoryPresets_InfoMatchesSavePreset` | pins `buildVoragoInfoXml` to the shared writer. A `PresetHost` loads one factory preset per category (seven). `PresetManager(makeVoragoPresetConfig(), &host.processor(), nullptr, tempUserDir)` (`preset_manager.h:55-61`) calls `savePreset(name, category, description)` (`preset_manager.cpp:227-277`). The `Info` chunk of the written file, read by `parseVstPreset`, must be byte-equal to `buildVoragoInfoXml(name, category, description)`. One further call with an empty description covers the no-`Comment` branch (`:273-275`) |
| FR-029 / SC-003 | same | `Vorago_FactoryPresets_RoundTrip` | `setState` → `kResultOk`; `getState` byte-identical |
| FR-030 / SC-004 | same | `Vorago_FactoryPresets_BrowserScan` | `PresetManager(makeVoragoPresetConfig(), nullptr, nullptr, tempUserDir, factoryRoot)`; count N; all `isFactory`; no empty subcategory; per-category counts == definitions; tab labels == `{"All"} ∪ names` |
| FR-005…FR-009 / SC-005 | same | `Vorago_FactoryPresets_StreamShape` | version == 3, length == 436; names valid, unique, ASCII; polyphony index ≤ 3; `A ≤ 180`, `Rel ≤ 60`; every float finite by bit pattern |
| FR-004 / SC-029 / SC-031 | same | `Vorago_FactoryPresets_LibraryShape` (R-7(ii)) | the required set is `requiredPrimaryCells()`, **derived** from `kRecordedDefaultStateCells` (§6.12: S ∪ M ∪ E ∪ ((D1.* ∪ D3.* ∪ D8.* ∪ D9.*) \ default-state)), never a hard-coded list; `requiredPrimaryCells().size() == 40` (a different size is the §6.12 FR-017 stop); N == that size; each required cell is exactly one preset's primary; ≥ 3 per category |
| FR-010 | same | `Vorago_PresetDefs_CellSpecsMatchSpec` | expected values are literals typed from the spec's C-2.1 table, not read back from `cellSpecs()`: `Capability::Count == 79`, group counts 10 / 12 / 7 / 50; each S row's override IDs, values and `ablationCount` equal §6.7 (S1 300 → 0.0; S2 401; S3 700 + 701; S4 500; S5 610 + 611 + 612 **+ 600**, all → 0.0; S6 1300; S7 900; S8 1105; S9 1400; S10 1003); each M*m* overrides only `99 + m` → its registered default (0.0; M5 Gravity 104 → 0.5); E6.hi 901 → 0.0 and E7.hi 902 → 0.25, both `ExtReversion`; E1–E5 `RouteIsolated`; D13.x 800 → 0.5, D14.1 1500 → 0.0, D14.2 1502 → 0.0, all `StateWithReversion`; every D cell's `verification` and `sConjunct` equal §6.10 (D1/D2 → S10, D3/D4 → S1, D5 → S2, D6 → S4, D7 → S5, D11/D12.1 → S9, D13 → S7, D14 → `Count`; D8/D9 `AttackWindow`; D10.1 `FreezeGesture`; D10.2/D12.2 `StateOnly`) |
| FR-011 / FR-011b (static) | same | `Vorago_PresetDefs_ClaimsWellFormed` | unique primaries; no primary in `kRecordedDefaultStateCells`; no primary D10.1; D10.1 claimed only by the S8 preset; every E-ext `CellSpec` has `ExtReversion` |
| FR-075 | same | `Vorago_PresetDefs_EExtSidePredicate` | hand-built decoded states: normalized 901 at 0.5 passes `E6.hi`, 0.49 fails; 902 at 0.625 passes `E7.hi`, 0.62 fails; the defaults fail both |
| FR-011a / SC-008 | same | `Vorago_PresetMatrix_NonSubsetRule` | `findWitness` and `verifiedAt` on hand-built vectors, including the `StateWithS`, `StateOnly`, `AttackWindow`, `FreezeGesture` and D1-primary cases (§6.12) |
| FR-061 | `unit/ghost_triggers_additive_test.cpp` (new) | `Vorago_Ghost_TriggersAddToDensityScheduler` | No existing test enforces this. `AtmosphereGhost_TriggerAccounting` (`atmosphere_ghost_test.cpp:3190`) runs at `kMinDensity` and bounds scheduler births; it never compares against a trigger-free arm at Vorago's density. Two heap `AtmosphereEngine`s are configured exactly as `VoragoEngine::prepare` configures `atmos_` (`vorago_engine.h:361-374`, density 0.30 at `:369`), with the same seed and the same deterministic excitation through `processStereoBlock`. A warm-up comes first, then a 60 s span. Arm Off never triggers. Arm On calls `triggerGrain()` N = 12 times, one every 5 s. Assertions: pool-full and ring-cold deltas are 0 in both arms; On's `getTotalTriggeredGrainsBorn()` delta == N; On's `(getTotalGrainsBorn() − getTotalTriggeredGrainsBorn())` delta == Off's `getTotalGrainsBorn()` delta, which is > 0. The scheduler is its own seeded stream and runs first each sample (`atmosphere_engine.h:2391-2396`, scheduler seeded `:582`), so the equality is exact. The Vorago half is cited: `setGhostEventTriggers` writes only the flag and latch (`vorago_engine.h:1004-1010`), the trigger site only calls `triggerGrain()` (`:1530-1537`), and `:369` is the only ghost `setDensity` writer (grep this session) |
| C-7.2 (R-8 refactor) | same | `Vorago_PresetDescriptor_RefactorIsIdentity` | `describe` on a fixed synthetic stereo signal equals `detail::describeImpl(..., std::nullopt)` component for component (`==`, same binary) |
| FR-021 / FR-032 / SC-006 | same | `Vorago_FactoryPresets_TreeMatchesGenerator` + `Vorago_FactoryPresets_TreeToleranceProbe` `[.measure]` | §6.15; prints the per-field worst on every leg |
| FR-014 / FR-035 / SC-009 | same | `Vorago_PresetMatrix_ParameterSpaceDistinct` | §6.14 |
| FR-034 / SC-014 | `integration/preset_sweep_test.cpp` (new) | `Vorago_PresetSweep_ShortBounded` (per-push) | 8 s @ 48 kHz, 4 s @ 44.1 kHz, 4 s @ 96 kHz single-note; 8 s `kCpuNotes` chord at forced polyphony 4 @ 48 kHz; finite + peak ≤ 0.9661; 2-thread pool |
| FR-033 / SC-012 | same | `Vorago_PresetSweep_LongRender` `[long][vorago-sweep]` | §6.3 arms 1–4 on every take of `A_K`; prints A, Rel, RT60 and every arm's figure per take |
| FR-033 / SC-024 | same | `Vorago_PresetSweep_FreezeGesture` `[long][vorago-sweep]` | §6.3 G and G₀ on the S8 preset; prints G's loudest, last and floor figures, G₀'s loudest, and the difference |
| FR-033a / SC-022 | same | `Vorago_PresetSweep_SustainAtAllRates` `[long][vorago-sweep]` | arm 1 over `[0, A+65]` at 44.1 and 96 kHz, stored seed |
| FR-012, FR-037 / SC-011 | same | `Vorago_PresetSweep_AblationVerifiesClaims` `[long][vorago-sweep]` | full vector (§6.7–§6.11); every claim verified at its role's bar; vector written to the record |
| FR-038 / SC-015 | same | `Vorago_PresetSweep_RendersAreReproducible` `[long][vorago-sweep]` | two fresh hosts on two threads over `[0, A+65]`, stored seed; `compareFingerprints(...).withinTolerance()` per channel |
| FR-011a, FR-013 / SC-008, SC-018, SC-028, SC-029 | `integration/preset_matrix_test.cpp` (new) | `Vorago_PresetMatrix_CoverageComplete`, `Vorago_PresetMatrix_NoShowcaseSubset` `[long][vorago-sweep][vorago-aggregate]` | computed from the records (§6.12); prints the full preset × cell matrix (primary / secondary / claimed-failed / unclaimed), the default-state cells and every witness |
| FR-015, FR-036 / SC-010 | same | `Vorago_PresetSweep_SoundSpaceDistinct` `[long][vorago-sweep][vorago-aggregate]` | §6.6 over C(40, 2) = 780 pairs, §6.13 controls; prints min/median/max d, the minimum pair, every `s(P)`, `t_max`, K and the floor |
| FR-017a | `integration/preset_pilot_test.cpp` (new) | `Vorago_PresetPilot_DefaultSurfaceTakeCurve`, `Vorago_PresetPilot_Calibrate` `[.probe]` | §6.16; assertions only on finiteness and bounds; the verdicts are read from the log into the compliance record |
| FR-039 / SC-016 | `integration/preset_load_rt_test.cpp` (new) | `Vorago_FactoryPresets_SequentialLoadNoAlloc` | warm `ProcessorFixture` (48k/512, note held); per preset `setState`, then an `AllocationScope` around each of the next 4 `process()` calls, with the count read inside the scope → 0 |
| FR-040 / SC-016 | same | `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe` | streams prebuilt; a message thread loops `setState` over all presets; the audio thread renders 4 s under `ThreadScopedAllocationScope` → 0; every call `kResultOk`; finite, peak ≤ 0.9661 |
| FR-041 / SC-017 | `integration/preset_cpu_test.cpp` (new) | `Vorago_PresetCpu` `[.perf]` | §6.17 |
| FR-070 / SC-025, SC-026 | compliance record | — | cite both 13b tables' rows for `syncRate` and `selfAffinity` (`final2_table_default.log:52`, `:75` and the Life-max log); ratification date 2026-09-29 before the first commit adding 901/902 (`git log` order) |
| FR-024 / SC-007 | CLI | `node tools/check-preset-generator-determinism.js --plugin vorago` | exit 0; the run without `--plugin` still passes for Seraphis |
| FR-026 / FR-027 / SC-032 | compliance record | — | after building `Vorago`, list `%PROGRAMDATA%\Krate Audio\Vorago\` (seven dirs, 40 files); cite `plugins/vorago/CMakeLists.txt:123`, `preset_paths.h:27`, `setup.iss:66-68`, `installers/linux/README.txt:29-44` |
| SC-024 (freeze load path) | compliance record | — | the phase base is `339cd501`, the planning HEAD. No Phase 14 code commit precedes it, and Stage A's inert friends landed earlier, in `9ecd6d10`. (1) `git diff 339cd501..HEAD -- dsp/include/krate/dsp/effects/cavern_verb.h dsp/include/krate/dsp/effects/aether_reverb.h plugins/vorago/src/parameters/space_params.h` is **empty**. `space_params.h` holds the freeze field's load (`loadSpaceParams`, `:276`) and its handler (`:177`). (2) `git diff 339cd501..HEAD -- plugins/vorago/src/processor/processor.cpp` is shown in full, and every hunk is attributed: the v3 `setState` / `getState` hunks → FR-072; `pushVoiceParams` → FR-071a; `getTailSamples` → FR-060, which reads `spaceParams_.freeze` and never writes it. No hunk touches `pushCavernParams` (`:964-991`, the freeze push at `:985-988`), and no hunk touches the `loadSpaceParams` call in `setState` |
| FR-076 | compliance record | — | `git diff --stat 339cd501..HEAD -- dsp/include` lists exactly `systems/vorago_voice.h` and `systems/vorago_engine.h`. `git diff 339cd501..HEAD -- dsp/include` shows **added lines only** (no `-` line other than the `kFieldCount` value line and its comment, which R-1 changes from 31 to 33). The added lines are the two forwarders, the two fields and the two `applyVoiceParams` lines of §5.1 |
| FR-042 / SC-020; FR-061; FR-063 / SC-021; FR-064/FR-065 / SC-019; FR-066 / SC-023; SC-013 | compliance record | — | listening notes per preset; FR-061 citation on the S9 preset, plus `Vorago_Ghost_TriggersAddToDensityScheduler`'s log line; greps; `version.json` / `CHANGELOG.md`; release-readiness row; auval line from `ci.yml:800-808` on the release commit; nightly run URL; per-job step durations |

`preset_sweep_test.cpp` and `preset_matrix_test.cpp` share `sweepRecordFor`'s function-local cache.
In a local full run the aggregate reuses the records the shard cases produced.

---

## 9. Build integration

**`dsp/`:** `vorago_voice.h` and `vorago_engine.h` change (§5.1). There is no DSP CMake change,
because `vorago_param_surface_test.cpp` is already registered. Build and run `dsp_systems_tests`.

**Root `CMakeLists.txt`:** add `vorago_preset_generator` and `generate_vorago_presets` (§5.7).

**`plugins/vorago/tests/CMakeLists.txt`: one registration task (FR-027a, R-7(i)) in C1.** It adds
all of the following at once, each created as a skeleton TU (header comment + includes, no test cases
yet, which compiles):
- `unit/ecosystem_roster_test.cpp`
- `unit/state_v3_test.cpp`
- `unit/tail_samples_test.cpp`
- `unit/preset/factory_preset_test.cpp`
- `integration/preset_sweep_test.cpp`
- `integration/preset_matrix_test.cpp`
- `integration/preset_pilot_test.cpp`
- `integration/preset_load_rt_test.cpp`
- `integration/preset_cpu_test.cpp`
- `unit/ghost_triggers_additive_test.cpp` (FR-061)

All of them except `preset_cpu_test.cpp` and `ghost_triggers_additive_test.cpp` (it makes no
finiteness check) also join the `-fno-fast-math -fno-finite-math-only` list
(`:108-138`), because they check finiteness by bit pattern. The CPU TU stays out for the reason
`processor_cpu_test.cpp` does (`:140`). The final task group only audits this list. The probe TU was
registered before G1 (`:52`) and is not touched.

**`plugins/vorago/CMakeLists.txt`:** no change. `tail_estimate.h` is header-only, and
`krate_plugin_install_presets` is already called (`:123`, spec FR-026).

**Targets built and run:**
- `dsp_systems_tests`;
- `vorago_tests`, per-push filter and, alone, the `[long]` / `[vorago-sweep]` cases;
- `vorago_preset_generator`, `generate_vorago_presets`;
- `Vorago`, for pluginval 5;
- `seraphis_tests` is not affected, because no shared header is edited (`node tools/check-seraphis-green.js`
  confirms).

**Gates before each commit:**
- zero MSVC warnings;
- `node tools/check-portability.js`, then `wsl --shutdown`;
- clang-tidy `vorago` and `dsp`;
- pluginval 5 (for B and later);
- the determinism script (C2 and later);
- `node tools/lint-float-bit-goldens.js` and `node tools/lint-nonfinite-symbols.js`, both covered by
  the portability gate.

---

## 10. Cost model and CI sizing

**Anchor (measured).** The probe rendered 28 × 340 s in 17 min 45 s, which is 0.112× real time,
single-threaded, locally (`compliance.md`, Run 1). After 13b's 89 % CPU (roadmap line 609) that is
≈ 0.10×. CI runners are assumed to be 2.5× slower. This is an estimate that §2 stage H replaces with
a measurement.

**Per preset, K = 8** (the worst case K can take). Averages: `A` ≈ 60 s, Rel ≈ 30 s, RT60 ≈ 25 s, so
Total ≈ 315 s.

| Render | Audio-equivalent s | Count |
|---|---|---|
| main C-6, every take of `A_K` | 315 | K = 8 |
| S/M/D/E-ext single-take twins after §6.11 skips | 125 each | ≈ 14 |
| E routes | 125 each | 12 |
| 44.1 + 96 kHz sustain (cost 0.92 + 2.0) | 125 × 2.92 | 1 |
| reproducibility | 125 × 2 | 1 |
| **total** | **≈ 6 400** | ≈ 11 min local, ≈ 27 min CI |

**No-skip figure (§6.11, §12 item 5).** If every S/M/D-ablation/E-ext twin is rendered regardless,
there are 28 twins (10 S + 12 M + 4 D13/D14 + 2 E-ext) instead of ≈ 14. The per-preset total becomes
2 520 + 3 500 + 1 500 + 365 + 250 ≈ **8 150 s**: ≈ 14 min local, ≈ 34 min CI. The nightly then needs
41 × 34 / (108 × 1.6) ≈ 8.1, so **9 shards per OS**, still inside the starting n = 10.

**Nightly load.** 41 units (40 presets + the pseudo-preset) × 27 min ≈ 18.5 CI-hours per OS. The
usable budget per job is 60 % of 180 min = 108 min. With 2 threads on a 2-vCPU runner, at a measured
speed-up (≈ 1.6× assumed), that needs ≈ 7 shards per OS. The plan starts at **n = 10 per OS** (30
jobs) for margin and adjusts from the first nightly's `-d yes` durations. At K = 4 the total drops
by ≈ 20 % (the twins dominate).

**Aggregate job.** It renders the control twins:

| Control | Renders |
|---|---|
| B sets | 5 presets × K = 40 renders × 315 s |
| gain twin | 1 × 315 s |
| sub twins | ≤ 10 × 315 s |

That is ≈ 16 000 s of audio, ≈ 67 min on CI single-threaded, ≈ 40 min with 2 threads. It fits the
108-min budget.

**Per push.**
- Short guard: 41 × (8 + 3.7 + 8 + 8 × 2.5 chord) s-equivalent ≈ 41 × 40 s × 0.10 ≈ 2.7 min local,
  ≈ 7 min on CI (2 threads: ≈ 4 min).
- Container, tree and parameter-space tests: < 30 s.
- The shared "Run Tests" step's total is measured from the CI log before C2 merges (SC-013's 80 %
  clause). A pre-existing overrun is an FR-017 finding, and nothing is trimmed to hide it.

**Pilot (local, hand-run).**
- E0: 16 × 340 s × 0.10 ≈ 9 min.
- Full pilot: 7 × 16 × 340 s ≈ 63 min single-threaded, ≈ 20 min on 4 threads, plus a few candidate
  twins.

---

## 11. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **K = 8 does not bring `2·t_K` under 4.0 (P2-1, predicted)** | E0 runs first (§2), so the stop costs ~9 minutes. The stop is the spec's ruled path (FR-017a): surface the curve, no fallback |
| An E-ext primary cannot reach F = 4.0 (P2-5) | pilot candidates P1/P2 are authored colony-forward; a failure is an FR-017 stop, never a lever or span change (Non-goals: 13b owns the colony) |
| A material swap or a macro cannot reach the primary bar | pilot includes D1.Glass (P6) and the S5/S8/D8.2 primaries; re-author with stronger contrast; a persistent failure is FR-017 |
| The default-state set differs from P2-6 | FR-012's default-surface run is the first sweep executed; `kRecordedDefaultStateCells` and §7's secondaries are re-derived before authoring; nightly asserts equality. If the measured set moves `|requiredPrimaryCells()|` away from 40 (e.g. S1 fails on the default surface, so the four noise models need primaries), that is an FR-017 stop (§6.12), because SC-029 fixes N = 40 |
| The take-set reading (P2-2) or the same-seed controls (P2-3) are not what the user meant | both are surfaced for acknowledgement before C1; switching either changes only `takeSeedIndex` or the control-render loop, not the harness structure |
| Chaotic divergence inflates same-seed ablation `d` (a knob change reroutes the colony) | this is the spec's ruled comparison (Q4). `s(P)` from the K-take-averaged minutes and the E / attack-window attributability conjuncts are the defences the spec provides; the printed matrix records every raw term for the compliance record |
| A non-thread-safe static in KrateDSP breaks parallel renders | reproducibility renders its two hosts on two threads; the pilot compares a 1-thread and a 4-thread record of P0 within fingerprint tolerance before the pool is trusted |
| Memory on the 7 GB macOS runner | streaming capture: 180 s × 2 ch × 4 B × 48 k ≈ 69 MB per main take, plus ≤ 71 MB for an attack window; ≤ 4 concurrent jobs per process |
| COMDAT / `-fno-fast-math` TUs instantiating inline DSP differently (memory note on render goldens) | no render is compared across binaries: the tree test compares state bytes under a measured tolerance, reproducibility stays inside one binary, and E0's `t_1` cross-check against 13b is a descriptor-level comparison with a tolerance, not a bit match |
| NaN/Inf under macOS `-ffast-math` | every finite check is `Krate::DSP::detail::isFinite` (bit pattern); the new TUs join the exemption list; `describeImpl` uses `std::optional`, never an infinity sentinel |
| Narrowing in brace init (Clang) | designated initializers for `ProcessSetup`, `RenderSpec` and `CellSpec`; `static_cast` on every `size_t` → `int32` |
| MSVC C4996 on `std::getenv` | `sweepEnv` uses `getenv_s` under `_MSC_VER` |
| `std::jthread` missing on AppleClang | plain `std::thread` + join |
| Denormals in long tails | `ScopedDenormalMode` is the first statement of every `process()` (`processor.cpp:294`), per thread; analysis runs in double |
| The v3 bump breaks hosts' saved v2 state | SC-027's v2-load test; version > 3 is still rejected |
| Controller interface freeze at 1.0.0 | this phase adds parameters, not interfaces; FR-064 note in the plugin `CLAUDE.md` |
| The generator pulls VSTGUI or Catch2 | the target links `KrateDSP KratePluginsShared sdk` only; a transitive include fails its build (the proof) |
| The per-push roster already exceeds SC-013's 80 % | measured from the CI log before C2 merges; FR-017 finding, never a trim |
| Stored-On freeze leaves an empty tank | authoring rule: Freeze stored Off; D10.1 by gesture (R-5) |
| The ghost scheduler replays stale capture after release | the Freeze-Off tail arm catches it; FR-017 finding; the window is never widened |
| macOS shard queueing (P2-7) | SC-013 measures step time; queue time is recorded, not gated |

---

## 12. Open questions for the user (before C1)

1. **P2-2, take sets.** Acknowledge `A_K = {s … s+K−1}` for `D(P)` and `B_K = {s+K … s+2K−1}` for
   `t_K`. Each set holds K takes, which is what makes K ≤ 8.
2. **P2-3, controls.** Acknowledge that (a), (a′) and (c) are same-seed, single-take comparisons,
   because the take term is exactly zero for master gain and sub offset. The alternative is K-take
   twins at ≈ K× the aggregate cost.
3. **P2-4, SC-026a's friend.** Drop `VoragoEcosystemRosterProbe` and read through the existing
   `engineForTest()` (recommended; needs an SC-026a / New-components amendment), or keep it as the
   spec states (plan default).
4. **P2-1, order.** Acknowledge that E0 runs before stage B. A stop there surfaces the curve for a
   ruling before any surface work is done. E0 uses the hard-coded default-surface timeline and no
   decode, defs or tail estimate (§2).
5. **§6.11 skip rules vs FR-037's literal render set.** FR-037 asks for one ablation render per every
   S/M cell and per ablation-conjunct D cell, "recorded regardless". C-10 absorbs cost "never by
   narrowing the vector". §6.11 skips a render only where its verdict is known exactly without it:
   - an override equal to the stored value (the twin is P, and `d = 0` is recorded exactly);
   - an M cell below its displacement threshold;
   - E1–E5 with 900 stored at 0;
   - a D cell with a false state predicate;
   - an E-ext cell with a false side predicate.

   The verdicts are identical either way. The skips lose the raw `d` of the last four kinds. Either
   acknowledge the skips, or rule "render regardless". That costs ≈ 8 150 s instead of ≈ 6 400 s per
   preset and 9 shards per OS instead of 7, within the starting n = 10 (§10). Nothing else changes.
6. **SC-011's envelope clause.** SC-011 says envelope cells (D8.x/D9.x) are "scored by the C-7.4
   attack-window reversion". The plan applies that to the envelope **primaries** (D8.2, D9.1) and
   scores D8/D9 **secondaries** as always-audible state cells, per C-2.1's D8/D9 rows. The §7
   secondaries D8.1 and D9.2 are default-state: their reversion is the preset itself (`d = 0`), so an
   attack-window verdict could never pass them. Acknowledge this reading (§6.8, §6.12).

**Ruled 2026-09-29 (spec Clarifications "Plan stage (2026-09-29)"):** 1 acknowledged; 2 same-seed
single-take controls; 3 the friend is DROPPED (`engineForTest()`); 4 E0 before stage B, acknowledged;
5 the skips are acknowledged (verdict recorded regardless, skip reason recorded); 6 acknowledged. Also
confirmed: T003 as the one mid-phase registration with T060 auditing, R-6 at T002, the tasks agent's
added cases, E0's 0.0015 tolerance. Push-dependent tasks stay pending until a push is granted.

No other ruling is needed. G1 is discharged, every other number is the spec's, and G2 is measured.
One conditional stop is named in advance: a measured default-state set that moves the required
primary count away from 40 (§6.12).

---

## Review notes (spec-coverage review, 2026-09-29)

- **verifiedAt per kind (major): applied.** See the §6.12 table "`verifiedAt` by kind", the Outcomes row in §5.8, the §6.8 scope, and the extended `Vorago_PresetMatrix_NonSubsetRule`.
- **LibraryShape derived from the default-state set (major): applied.** `requiredPrimaryCells()` is in §5.5 and §6.12, together with the FR-017 stop when the count is not 40. See also the §7 step 4, §8 and §11 rows.
- **C1/E0 ordering (major): applied as option (b).** E0 uses a hard-coded default-surface timeline and the raw host. `decodePresetState`, `makeTimeline` and `buildPresetComponentState` move to C2, after B (§2, §5.6, §5.8). Option (a) was not taken: a v2-tolerant decode would be test code that exists only to be deleted at B.
- **§6.11 skips (minor): routed to §12 item 5 for acknowledgement, not reversed.** The skips change no verdict. §10 now states the no-skip cost (9 shards per OS, inside n = 10), so either ruling is costed.
- **Info pin, CellSpecs test, SC-024/FR-076 rows, FR-061 case (minors): applied** (§8, §9). The D4 labels are now 1-based, and the cell count is corrected to 79.
- **Also surfaced (§12 item 6):** SC-011's envelope clause applies to the envelope primaries. D8/D9 secondaries are always-audible state cells.
