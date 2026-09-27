# Feature Specification: Vorago Phase 13b — Ecosystem Audibility

**Spec slug:** `vorago-phase13b-ecosystem-audibility`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, Phase 13b (lines 571–602); Cross-Cutting Constraints
(lines 660–684); Phase 14 status note (lines 609–612)
**Depends on:** Phase 10 (`VoragoVoice` / `VoragoEngine`, the wake-lane routing FR-020 … FR-023, ✅),
Phase 13 (the ecosystem view, ✅), Phase 14's first pass (the probe TU and its four measured runs — kept,
roadmap line 612)
**Blocks:** Phase 14 (paused at gate G1; re-runs its specify stage after this phase — roadmap lines 598–599)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-09-27 (branch `feat/vorago-phase1-events-modulation` at `9ecd6d10`)

---

## Overview

Vorago's flagship component, the `EcosystemEngine` colony, is measured **inaudible**. Phase 14's FR-070
probe (`plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`, `TEST_CASE("Vorago_EcosystemRuleProbe",
"[.probe][vorago]")` at `:380`) renders the shipped processor for 340 s per configuration and compares
renders with the C-7.2 sound-space descriptor (`plugins/vorago/tests/preset_test_support.h:45-54`,
`describe` `:96`, `descriptorDistance` `:151`). The yardstick `t0` is the distance between the default
surface and its seed twin — "one reseed". Four runs, recorded in
`specs/vorago-phase14-presets-release/compliance.md`, establish the premise this phase fixes:

| Run | Surface | Log | t0 | d(on, ecosystem off) | d/t0 | Knobs ≥ 2·t0 |
|---|---|---|---|---|---|---|
| 1 | default | `artifacts/fr070_probe_default_surface.log` | 1.7128 | (= grazeRate→0, 0.6311) | 0.368 | 0 of 14 |
| 2 | default, `900=0` reference | `artifacts/fr070_probe_ecosystem_off_default.log` | — | 0.6312 | 0.368 | — |
| 3 | Life max (`109=1`) | `artifacts/fr070_probe_life_max.log` | 2.1601 | 2.1248 (reference = depth 0.15, not off — see below) | 0.984 | 0 of 14 |
| 4 | wake bases forced 0 (`WAKEBASE=0`, `301=0`) | `artifacts/fr070_probe_wakebase0_default.log` | 2.1534 | 1.9267 | 0.895 | — |

(compliance.md lines 9, 15, 66, 70, 77–78, 131.) The two best knob extremes anywhere — grazeRate → 0 and
leakRate → 1 — sit at the ecosystem-off distance, i.e. they *starve the colony*, which is the same as
switching it off (compliance.md line 70).

**Run 3's reference was not "ecosystem off".** Parameter 900 is a macro-*base* route
(`{kEcosystemDepthId, Route::MB}`, `plugins/vorago/src/parameters/param_routes.h:115`, target
`EcosystemDepth` `:198`). `VoragoMacroMatrix::setTargetBase` replaces only the row base
(`vorago_macro_matrix.h:1006-1013`) and `evaluateAll` then adds every row's `contributionOf(row)` on top
(`:1183-1186`); for Life that is `amount * applyModCurve(Linear, m)` (`:1167`) with amount 0.15 (`:634`). So
`109=1` + `900=0` yields an ecosystem depth of 0 + 0.15 = **0.15**, not 0 (applied per voice at `:1103`). Run 3's
d = 2.1248 is therefore "depth 1.0 vs depth 0.15", not on vs off; the Life-max premise is re-measured with a
true-off reference (FR-007) in the FR-001 before-record. At the default surface Life = 0 (`param_table_expected.h:83`),
so run 2's `900=0` reference *was* depth 0.

**Cause, read in the code this session.** Phase 10 made the ecosystem a **wake-only lane**:

- `VoragoVoice::combineWake(float base, float eco, float sched)` returns `std::max(base, std::max(eco, sched))`
  (`dsp/include/krate/dsp/systems/vorago_voice.h:1018-1021`). A destination can never read below its base.
- The bases are high: `setNoiseWakeBase(0.35f)` (`:571`), `peakWakeBase_.fill(0.50f)` (`:585`),
  `loopWakeBase_.fill(0.50f)` (`:598`), and the two `SlowEventScheduler`s wake the same slots
  (`gatherSchedulerLanes`, combined at `applyIdentityLanes` `:1911-1944`).
- For three of the five agent kinds the *only* thing an agent does is raise a wake gate:
  `noise_.setSourceWake(...)` (`:1920-1921`), `resonance_.setPeakWake(...)` (`:1927-1929`),
  `ecology_.setLoopWake(...)` (`:1934-1936`). Level, timbre and motion of those sections are never touched
  by agent state. The sections are also mixed quietly at the default surface: noise at −18 dB
  (`setNoiseLevelDb(-18.0f)`, `:569`), peaks at −9 dB (`resonance_.setPeakLevel(p, -9.0f)`, `:581`), ecology
  mix at `kDefaultMix` 0.15 (`setEcologyMix(FeedbackEcology::kDefaultMix)`, `:589`).
- Only the `Partial` lane is additive (`cloud_.setMutation(clamp(mutationBase_ + partialEco))`,
  `bloom_.setDepth(clamp(bloomDepthBase_ + partialEco))`, `:1939-1941`), and the `Ghost` lane already drives a
  level (`atmos_.setLevel(ghostPeak_ * ghost)`, `vorago_engine.h:1508`).

**This phase** gives agent state direct, depth-scaled sonic levers on the sections it drives, retunes the wake
bases so the colony's decisions carry, and proves both on the same probe: ecosystem off vs on ≥ 2·t0 at the
default surface **and** at Life max, then at least four rule knobs whose best extreme is ≥ 2·t0 without killing
the colony. It is a **voicing change**: the default render is allowed to change and the before/after is recorded.
It touches **only** Vorago's voice and engine; Seraphis is untouched.

---

## Scope

1. **Direct sonic levers** driven by routed agent state on the Resonator, Noise and Feedback destinations
   (roadmap line 585's candidates: peak level and wander, noise-source level and colour, loop gain and
   coupling), depth-scaled by the existing FR-021 per-destination ecosystem depth. The `Partial` (bloom depth,
   cloud mutation) and `Ghost` (ghost level) candidates are already direct and are confirmed, not rebuilt.
2. **Wake-base retune** (roadmap line 587) so a colony decision is not masked by an already-open gate.
3. **The gate instrument**: the existing `Vorago_EcosystemRuleProbe` extended only as far as the roadmap's gate
   needs (per-minute RMS of every render, the colony-kill flag, a true ecosystem-off reference valid at any
   surface, the gate verdict lines) — same stimulus, same descriptor, same candidate set.
4. **Evidence**: both probe gates with cited logs, the full knob table on both surfaces, the before/after
   default-render descriptors, the CPU delta, and the regression sweep (roadmap line 601–602).

## Non-goals (what other phases own, or nobody does)

- **Registering rule knobs as parameters, state v3, the ecosystem-page controls** — Phase 14 (roadmap
  lines 599, 628–633). This phase registers **no** parameter and keeps `kCurrentStateVersion = 2`
  (`plugins/vorago/src/plugin_ids.h:23`).
- **Choosing the Phase 14 rule-knob roster** — Phase 14's Q2 takes it *from* this phase's probe table
  (roadmap line 598); this phase only produces the table.
- **Factory presets, the coverage matrix, distinctness thresholds** — Phase 14.
- **Changing the `EcosystemEngine` simulation** (rules, stages, agent kinds, energy budget). The colony is
  bounded and non-trivial per Phase 8; what is missing is its *coupling to sound*, not its behaviour. Its
  header (`dsp/include/krate/dsp/systems/ecosystem_engine.h`) is not edited.
- **Changing the FR-023 combine rule.** The wake combine stays the maximum; SC-019a stays green unedited.
- **Any Seraphis-consumed component** (`harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`,
  `aether_reverb.h`, `entropy_processor.h`, the life modulators, `seraphis_*.h`) — untouched (roadmap
  line 595).
- **UI changes.** The Phase 13 ecosystem view reads agent state (`VoragoVoice::ecosystem() const`,
  `vorago_voice.h:1516`), which this phase does not alter.

---

## Existing components (verified this session)

| Component | Header | What is reused (real signature, read this session) |
|---|---|---|
| `VoragoVoice` | `dsp/include/krate/dsp/systems/vorago_voice.h` | `static constexpr float combineWake(float base, float eco, float sched) noexcept` (`:1018`); `void applyIdentityLanes(const IdentityLanes& lanes) noexcept` (`:1911`) — the one place lanes are written; `void publishIdentity() noexcept` (`:2022`) — the only writer of wake/depth surfaces; `void installIdentityNeutral() noexcept` (`:1970`) — every clearing path installs the bases through it; `void setEcosystemDepth(float d) noexcept` (`:1405`), `void setEcosystemDepthFor(EcosystemEngine::Kind dest, float d) noexcept` (`:1432`), `float getEcosystemDepthFor(EcosystemEngine::Kind) const noexcept` (`:1441`); `void gatherEcosystemLanes(IdentityLanes&) const noexcept` (`:1839`) with the FR-020a argmax reduction writing `lanes.eco[k][s] = ecosystemDepth_[k] * output[i]` (`:1831`); members `noiseWakeBase_` / `peakWakeBase_` / `loopWakeBase_` (`:2364-2366`), `ecosystemDepth_` (`:2370`), `mutationBase_ = 0.15f`, `bloomDepthBase_ = 0.60f` (`:2375-2376`); forwarders `void setNoiseLevelDb(float dB) noexcept` (`:1236`, with the FR-067 early-out on an unchanged value), `void setEcologyLoopGain(float g) noexcept` (`:1321`); `friend struct detail::VoragoEcosystemRuleProbe;` (`:1535`) |
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h` | ghost lane: `atmos_.setLevel(ghostPeak_ * ghost)` (`:1508`) with `ghost = max over rendering voices of getGhostRequest()` (`:1490`); `static constexpr float kGhostBurstPeak = 0.60f` (`:265`); `void setGhostPeakLevel(float v) noexcept` clamping `ghostPeak_` to [0, 1] (`:978-983`) |
| `VoragoMacroMatrix` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` | Life row `EcosystemDepth` base 0.85, amount 0.15 (`:630-635`); `voice.setEcosystemDepth(at(v, VoragoMacroTarget::EcosystemDepth))` (`:1103`) |
| `EcosystemEngine` | `dsp/include/krate/dsp/systems/ecosystem_engine.h` | read-only: `enum class Kind : std::uint8_t { Partial, Resonator, Noise, Feedback, Ghost }` (`:282-288`); `float getAgentOutput(std::size_t i) const noexcept` (`:886`, [0, 1], held between steps); `double getAgentEnergy(std::size_t) const noexcept` (`:890`); `Kind getAgentKind(std::size_t) const noexcept` (`:893`); `void setAgentDormant(std::size_t i, bool dormant) noexcept` (`:776`); the 14 probe knobs' setters (`setPredation` `:491` … `setAffinity(Kind, Kind, float)` `:707`) |
| `NoiseOrganism` | `dsp/include/krate/dsp/systems/noise_organism.h` | `void setSourceLevel(std::size_t slot, float dB) noexcept` (`:488`, clamp [−96, +12], constant-duration `levelRamp`, `:497-508`); `void setFilterBaseCutoff(std::size_t slot, float hz) noexcept` (`:600`, clamp [20 Hz, 0.45·fs], calls `applySlotConfiguration`); `void setSourceWake(std::size_t, float) noexcept` (`:844`); read-back `float getSourceLevel(std::size_t) const noexcept` (`:872`), `float getFilterCurrentCutoff(std::size_t) const noexcept` (`:956`), `float getSourceWakeAmount(std::size_t) const noexcept` (`:880`) |
| `ResonanceDriftNetwork` | `dsp/include/krate/dsp/systems/resonance_drift_network.h` | `void setPeakLevel(std::size_t peak, float dB) noexcept` (`:661`, clamp [`kMinPeakLevelDb` −60, `kMaxPeakLevelDb` +12] `:236-237`, per-peak `levelRamp`); `void setFreqWander(std::size_t peak, float semitones) noexcept` (`:681`, clamp [0, `kMaxFreqWanderSemis` 24] `:238`, **stored directly, no smoother**); `void setGainWander(std::size_t, float dB) noexcept` (`:697`, clamp [0, `kMaxGainWanderDb` 24] `:240`); `void setSlewCeilings(float, float) noexcept` (`:767`); `void setPeakWake(std::size_t, float) noexcept` (`:786`); read-back `getPeakLevel` (`:847`), `getFreqWander` (`:853`), `getGainWander` (`:859`), `getPeakWakeAmount` (`:871`) |
| `FeedbackEcology` | `dsp/include/krate/dsp/systems/feedback_ecology.h` | `void setLoopGain(std::size_t loop, float gain) noexcept` (`:1111`, clamp [`kMinLoopGain` 0, `kMaxLoopGain` 0.90] `:264-266`, through `ownFbSmoother`); `void setCoupling(std::size_t from, std::size_t to, float amount) noexcept` (`:1139`, clamp [0, `kMaxCouplingPerPair` 0.5] `:269`, through `couplingSmoother_`); `void setLoopWake(std::size_t, float) noexcept` (`:1293`); read-back `getLoopGain` (`:1406`), `getCoupling` (`:1434`), `getLoopWakeAmount` (`:1425`) |
| `HarmonicCloud` | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | `void setMutation(float m) noexcept` (`:452`), `float getMutation() const noexcept` (`:493`) — already the additive `Partial` destination; **not edited** (Seraphis-shared) |
| `BloomEngine` | `dsp/include/krate/dsp/systems/bloom_engine.h` | `void setDepth(float depth) noexcept` (`:532`, 50 ms ramp), `float getDepth() const noexcept` (`:679`) — already additive |
| `AtmosphereEngine` | `dsp/include/krate/dsp/systems/atmosphere_engine.h` | `void setLevel(float level) noexcept` (`:1035`, [0, 2], 20 ms smoother) — already the `Ghost` level destination; **not edited** (Seraphis-shared) |
| Probe TU | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` | `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` (`:380`); `kCandidates` — 14 knobs with clamp ends and defaults (`:217-232`); `RenderResult { PresetDescriptor d; bool allFinite; float peak; double m1RmsDb; }` (`:236-241`); `RenderResult renderOnce(KnobApply, float, double seedNormalized, const std::vector<Override>&, double wakeBase)` (`:260`); env options `VORAGO_PROBE_SURFACE` / `_KNOBS` / `_REF` / `_WAKEBASE` (header `:27-38`); friend `detail::VoragoEcosystemRuleProbe::{forEachEcosystem, setWakeBases}` (`:70-88`) |
| Descriptor | `plugins/vorago/tests/preset_test_support.h` | `struct PresetDescriptor` (`:47-54`, 9 bands + motion, flux, corr, energySpread, crest); `PresetDescriptor describe(std::span<const float>, std::span<const float>, double sr)` (`:96`); `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)` (`:151`); `PresetDescriptor meanOf(std::span<const PresetDescriptor>)` (`:167`) — the one implementation (Phase 14 ruling R-8) |
| Click detector | `tests/test_helpers/artifact_detection.h` | `struct ClickDetectorConfig` (`:38`), `class ClickDetector` (`:99`) |
| Fingerprint | `tests/test_helpers/render_fingerprint.h` | measured-tolerance determinism comparison (project rule: no bit-exact goldens) |
| Phase 10 routing tests | `dsp/tests/unit/systems/` | `VoragoVoice_WakeCombineRule` (`vorago_voice_test.cpp:2593`), `VoragoVoice_AgentReductionRule` (`:2687`), `VoragoVoice_EcosystemRouting` `[long]` (`vorago_voice_longrun_test.cpp:631`), `VoragoEngine_CpuBudget` `[.perf]` (`vorago_perf_test.cpp:971`) |
| Plugin params | `plugins/vorago/src/plugin_ids.h` | `kSeedId = 2` (`:88`), `kMacroLifeId = 109` (`:103`), `kNoiseWakeId = 301` (`:118`, default 0.35 per `plugins/vorago/tests/unit/param_table_expected.h:94`, macro target `NoiseWakeBase` per `parameters/param_routes.h:185`), `kEcosystemDepthId = 900` (`:173`) |

## New components

**None.** Roadmap Phase 13b names no new class; the work is new private members and lane-application code
inside `VoragoVoice` (and, if FR-016 retunes it, a constant in `VoragoEngine`), plus probe-TU reporting.

### ODR sweep — run this session

Names a plan might reach for, swept with `grep -rn "<Name>" dsp/ plugins/` (any occurrence, not only
`class`): `EcosystemLever` 0, `EcosystemLevers` 0, `EcosystemAudibility` 0, `LeverDepth` 0,
`applyEcosystemLevers` 0 — **all clear**. The existing `detail::VoragoEcosystemRuleProbe` friend struct
(`vorago_voice.h:171`, defined only in the probe TU) is reused, not duplicated. **FR-024** requires the plan to
re-run the sweep for any name it actually introduces.

---

## Functional Requirements

### A. Baseline and gate instrument

- **FR-001 — Before-record.** Before any voicing change (the FR-004/FR-005/FR-007 instrument extensions may
  land first; they change no production code), on the unmodified production tree:
  (a) the probe is run for every configuration the gates use (FR-010 – FR-013), with the **true-off
  reference of FR-007** on both surfaces — this re-derives SC-002's "before" value, replacing run 3's
  depth-0.15 figure — and the logs are checked in under
  `specs/vorago-phase13b-ecosystem-audibility/artifacts/`;
  (b) `VoragoEngine_CpuBudget` is run with `node tools/run-cpu-tests.js dsp_systems_tests`, alone, machine
  idle and cooled, and its log (including the WARN block with both clause lines) is checked into the same
  `artifacts/` directory.
  These are the "before" side of FR-030 and FR-032; SC-002's and SC-016's deltas are computed against these
  logged figures, never against a checked-in constant.
  [roadmap 594: "before/after descriptors are recorded"; roadmap 596–597: "CPU delta"]
- **FR-002 — Same probe.** The gates are measured by `Vorago_EcosystemRuleProbe` with the stimulus, timeline,
  descriptor and distance unchanged: NoteOn 36 velocity 100/127 at sample 0, 512-sample blocks, 48 kHz,
  340 s renders, D = mean of the three minute descriptors via `VoragoTest::describe` / `meanOf`,
  `t0 = d(default, seed twin)` (probe header `:5-17`). The descriptor stays in `preset_test_support.h` only
  (R-8: no local copy). [roadmap 590]
- **FR-003 — Same candidates.** The 14 candidates, their clamp ends and defaults (`kCandidates`,
  `:217-232`) are unchanged, and `leakExponent` stays excluded. A gate may not be reached by widening a range
  or swapping a candidate. [roadmap 590–592: "at least four rule knobs" of the same probe]
- **FR-004 — Colony-kill flag, every minute.** Today `RenderResult` carries only `m1RmsDb`, computed over the
  first capture minute (`res.m1RmsDb = stereoRmsDb(l.first(kMinuteSamples), …)`,
  `ecosystem_rule_probe_test.cpp:361`); M2 and M3 are described (`:352-357`) but never RMS-checked. The roadmap's
  test is that the extreme's render "**stays** within ±6 dB of the base RMS and above the −60 dBFS non-silence
  floor", which covers the whole render. The probe therefore records, for every render, the stereo RMS of
  **each** captured minute (M1, M2, M3; `stereoRmsDb` over `l.subspan(m * kMinuteSamples, kMinuteSamples)`),
  and flags an extreme **KILL** when, for **any** minute m ∈ {M1, M2, M3}, its RMS differs from the **same
  minute** of the same surface's base render by more than 6 dB **or** falls below −60 dBFS.
  An extreme counts toward FR-012 only if (i) `d(extreme, base) ≥ 2·t0`, (ii) it is not KILL, **and**
  (iii) it is not **OFF-LIKE**: `d(extreme, true-off reference) ≥ t0` (FR-007) — i.e. it is further from
  "ecosystem off" than one reseed. Clause (iii) closes the E-9 hole: once Gate 1 puts ecosystem-off ≥ 2·t0
  from the base, a colony-starving extreme (grazeRate → 0, leakRate → 1) would otherwise land at the off
  distance, pass the RMS test, and count as audible while being exactly the colony kill the roadmap excludes.
  A full-table run therefore always renders the FR-007 reference. [roadmap 592–593]
- **FR-005 — Verdict lines.** The probe prints, per run: `t0`, the reference distance and `d/t0` when a
  reference is requested, the per-knob table with each extreme's `d`, `d/t0`, `d(extreme, off)/t0`, the
  **M1, M2 and M3 RMS** and each minute's ΔRMS vs the base render's same minute, and KILL / OFF-LIKE /
  INAUDIBLE flags, and the count "audible non-kill knobs: N of 14". It additionally **reports (informative,
  not gating)** each render's mean colony output per minute — the mean over every voice's agents of
  `EcosystemEngine::getAgentOutput(i)` (`ecosystem_engine.h:886`), sampled once per processed block through
  `detail::VoragoEcosystemRuleProbe::forEachEcosystem` — and its ratio to the base render's, so a starving
  colony is visible directly and not only by inference.
- **FR-007 — True ecosystem-off reference.** `900 = 0` is ecosystem off **only at Life = 0**: at Life max it
  yields depth 0.15 (Overview). The gate reference must hold every voice's routed ecosystem contribution at
  exactly zero for the whole capture on **any** surface. The probe gains a reference mode (e.g.
  `VORAGO_PROBE_REF=off`) that, through the existing friend `detail::VoragoEcosystemRuleProbe`
  (`vorago_voice.h:1535`, `vorago_engine.h:1304`, defined only in the probe TU `:75`), silences the colony's
  coupling to sound independently of the macro path. The mechanism is the plan's, subject to: it must not be
  undone by the per-block macro apply (`voice.setEcosystemDepth(...)`, `vorago_macro_matrix.h:1103`) nor by a
  voice/ecosystem `reset()` (which clears dormancy, `ecosystem_engine.h:2293`), so it is **re-applied after
  every `process()` block** (candidates: `setAgentDormant(i, true)` on every agent of every voice,
  `ecosystem_engine.h:776`, or a friend write of every voice's per-kind depth to 0). The probe **asserts** its
  own validity: from M1 start to the end of M3, every voice's every `getAgentOutput(i)` reads 0 (dormancy) or
  every `ecosystemDepth_[k]` reads 0 (depth), whichever is used. At the default surface the probe also renders
  `900=0` and reports `d(true-off, 900=0)/t0` as a cross-check (expected ≈ 0 up to the gate-ramp transient
  before M1).
- **FR-006 — Probe stays hidden and non-gating in CI.** The TU keeps its `[.probe]` tag; its assertions
  remain boundedness (finite by bit pattern, peak ≤ 0.9661) and the default M1 RMS ≥ −60 dBFS (header
  `:19-21`), plus the FR-007 reference-validity assertion (a reference that is not actually off must fail the
  run, not produce a gate figure). The gate verdicts are read from the logs by the compliance step (the Phase 14 FR-070 shape), not
  asserted — a 28-render, multi-minute run is not a per-push test.

### B. The gates

- **FR-010 — Gate 1a, default surface.** `d(ecosystem on, ecosystem off) ≥ 2·t0`, ecosystem off = the FR-007
  true-off reference, on the shipped default surface. (`VORAGO_PROBE_REF=900=0` is equivalent here because
  Life = 0, and is reported as the FR-007 cross-check.) [roadmap 590–591]
- **FR-011 — Gate 1b, Life max.** The same inequality with `VORAGO_PROBE_SURFACE=109=1` (Life macro at 1.0),
  ecosystem off = the FR-007 true-off reference. `900=0` is **not** an admissible reference here (it leaves
  depth 0.15). [roadmap 591]
- **FR-012 — Gate 2, rule knobs.** At the default surface, at least **four** of the 14 candidates have a best
  counted extreme — `d ≥ 2·t0`, not KILL in any minute, not OFF-LIKE (FR-004). [roadmap 591–593]
- **FR-013 — Full knob table on both surfaces.** The complete 14-knob table is recorded at the default surface
  and at Life max (the latter informative; FR-012 gates the default surface). [roadmap 601: "the full knob
  table recorded"]
- **FR-014 — Order.** Gate 2 is evaluated only after both Gate 1 arms pass on the same tree; a knob table
  measured on a tree that fails Gate 1 is recorded but does not count. [roadmap 591: "then at least four"]

### C. Direct sonic levers (VoragoVoice)

- **FR-015 — Levers on the wake-only destinations.** For each of the three wake-only kinds, the routed
  ecosystem contribution for a slot (after the FR-020a argmax reduction and the FR-021 depth,
  i.e. `lanes.eco[k][s]`, `vorago_voice.h:1831`) additionally drives **at least one** direct lever on the
  **same slot** that changes level, timbre or motion — chosen from the roadmap's candidates:

  | Kind | Wake (kept) | Lever candidates (existing setters) |
  |---|---|---|
  | `Resonator` | `setPeakWake(p, …)` | peak level `setPeakLevel(p, dB)`; peak wander `setFreqWander(p, semitones)` / `setGainWander(p, dB)` |
  | `Noise` | `setSourceWake(s, …)` | source level `setSourceLevel(s, dB)`; source colour `setFilterBaseCutoff(s, hz)` |
  | `Feedback` | `setLoopWake(l, …)` | loop gain `setLoopGain(l, g)`; coupling `setCoupling(l, to, a)` |

  If **coupling** is chosen as the Feedback lever, the `from` loop's lane drives **only** the shipped
  neighbour-ring pair `(l, (l+1) % numLoops)` (Clarifications Q1–Q8, session 2026-09-27, Q8); no additional
  pair is turned on, and SC-011's exposure analysis stays bounded to today's active pairs.

  Which candidate(s) per kind ship, and their mapping curves and spans, are decided by the plan against the
  FR-010 – FR-012 probe (OQ-1). [roadmap 584–586]
- **FR-016 — The already-direct lanes.** `Partial` (cloud mutation + bloom depth, additive,
  `vorago_voice.h:1939-1941`) and `Ghost` (level `ghostPeak_ * ghost`, `vorago_engine.h:1508`) already shape
  sound directly; their routing shape is kept. Their **bases/spans** (`mutationBase_`, `bloomDepthBase_`,
  `kGhostBurstPeak`) may be retuned under FR-018 if the probe shows it is needed; any retune is recorded with
  before/after values. [roadmap 586: "already additive", "ghost level"]
- **FR-017 — Depth-scaled, neutral at depth 0.** Every lever's offset from its base is a function of
  `lanes.eco[k][s]` alone (which already carries `ecosystemDepth_[k]`) and is **exactly zero** when that
  lane is zero. With ecosystem depth 0 every lever destination reads exactly its configured base — FR-021's
  "at depth 0 the ecosystem changes nothing" extended to the new surfaces. Consequently the FR-007 true-off
  reference is "ecosystem off" on every surface; `900 = 0` is ecosystem off **only at Life = 0** (Life's row
  adds 0.15 · Life on top of the 900 base, `vorago_macro_matrix.h:634`, `:1167`, `:1183-1186`).
  The **sign** of a lever's offset — whether agent activity raises or lowers its destination (e.g. level
  offsets up, cutoff offsets down) — is chosen per lever by the plan (Clarifications Q3); the zero-at-lane-0
  invariant holds regardless of sign, and E-5/SC-011's bound analysis covers each lever's chosen direction.
  [roadmap 584: "depth-scaled"; Phase 10 FR-021]
- **FR-018 — Wake-base retune.** `peakWakeBase_` (0.50, `:585`) and `loopWakeBase_` (0.50, `:598`) are
  retuned so an agent's wake decision is not masked by an already-open gate; the shipped values are chosen by
  probe measurement and recorded, before and after, with the run that justified them. `noiseWakeBase_`
  (0.35, `:571`) — parameter 301's default and the `NoiseWakeBase` macro base — is **left unchanged**
  (Clarifications Q1): the FR-010/FR-011 gates are reached through the voice-owned peak/loop bases and the
  FR-015 levers, not through 301. "Not masked" is measured by SC-020 (the fraction of control steps on which
  the ecosystem lane exceeds the shipped base); SC-020's `Noise` arm is **recorded, not gated**, for this
  reason. Should probe measurement (SC-020's `Noise` arm) show the noise wake stays masked even after the
  peak/loop retunes and the FR-015 levers, moving 301's default becomes admissible only afterward, as its own
  explicitly recorded ruling (Clarifications Q1) — never a silent change in this pass. [roadmap 587]
- **FR-018a — Lever-destination default retune (Clarifications Q2).** Beyond the FR-018 wake-base retune,
  the plan may also retune the **voice-owned default/base** of a section value that a chosen FR-015 lever
  drives — e.g. noise level `setNoiseLevelDb(-18.0f)` (`:569`), peak level `resonance_.setPeakLevel(p,
  -9.0f)` (`:581`), freq wander default 1.5 st (`:582`), ecology mix `kDefaultMix` 0.15 (`:589`), loop
  gain 0.72 (`:590`), coupling 0.12 (`:596`) — when doing so helps that lever reach its gate. This applies
  **only** to a value that is a chosen lever's destination; it does not extend to any value that is not
  lever-driven. The corresponding **macro-row base** in `vorago_macro_matrix.h` is left unchanged
  (Clarifications Q2), so FR-026's edit set and FR-027's no-parameter-change rule both hold unaffected; any
  retuned value is recorded before/after under FR-030.
- **FR-019 — Agent state only.** Levers read the **ecosystem** lane, never the scheduler lane or the combined
  wake: a `SlowEventScheduler` event still wakes a slot (FR-022/FR-023 unchanged) but does not move a lever.
  This keeps "ecosystem off" a clean reference and makes the lever attributable to the colony. [roadmap 584:
  "Agent state gets direct … levers"]
- **FR-020 — Wake combine unchanged.** `combineWake` and its call sites keep the maximum rule; the lever is a
  separate write, not a new combine. [roadmap 596: SC-019 observability stays green]
- **FR-019a — Lane-shaping fallback (Clarifications Q5).** If FR-015's levers and FR-018/FR-018a's base
  retunes cannot reach the FR-010 – FR-012 gates, a **voice-side per-kind lane-shaping function** (a gain or a
  curve) may be applied to `lanes.eco[k][s]` **on the lever input only**, before it is used to compute that
  kind's lever offset(s). The shaping is exactly 0 when its input is 0 and its output stays clamped to
  [0, 1], extending FR-017's zero-at-lane-0 invariant through the shaping. `combineWake` and every other
  consumer of `lanes.eco` continue to read the **raw, unshaped** lane — FR-020's combine rule stays literal
  and unedited. The shaping lives entirely inside `vorago_voice.h`; no `EcosystemEngine` edit (the
  Non-goals fence holds).
- **FR-021 — Single owner, base read-back preserved.** Each lever destination has a **voice-owned base
  shadow**; the existing user/macro path (e.g. `setNoiseLevelDb` `:1236`, `setEcologyLoopGain` `:1321`,
  per-peak level/wander writers) writes the shadow, and the lane application writes `base + offset` to the
  component. Existing getters that Phase 10/12 tests read back as "the configured value" (e.g.
  `getNoiseLevelDb()` `:1258`, `getEcologyLoopGain()`) keep returning the **base**, not the modulated value.
  No lever write may re-arm a component ramp with an unchanged target (the FR-067 early-out hazard documented at
  `:1240-1247`).
- **FR-022 — One writer, one clearing path.** Levers are written only from `applyIdentityLanes` (via
  `publishIdentity`, `:2022`), so rendering and `advanceOneChunkLifeOnly` (`:2274`) stay indistinguishable (Phase 10
  SC-030), and `installIdentityNeutral` (`:1970`) installs every lever at its base, so reset / steal /
  recovery render as a freshly prepared voice.
- **FR-023 — Bounded and click-free.** Every lever output is clamped inside the component's own clamp and
  inside a span chosen so the worst case keeps every boundedness soak green. The worst case is **every
  `lanes.eco[k][s]` = 1** for every kind and slot of every voice — the domain maximum of the lane
  (`ecosystemDepth_[k]` ≤ 1 × `getAgentOutput` ≤ 1, `vorago_voice.h:1831`, `:1405-1409`). The natural colony
  does not reach it (the energy-budget normalisation in `EcosystemEngine::publish`,
  `ecosystem_engine.h:2331-2332`, spreads output across agents), so SC-010(a) and SC-011(a) drive it through a
  **lane-injection test seam**: a friend of `VoragoVoice` (the existing `detail::VoragoEcosystemRuleProbe`
  declaration at `vorago_voice.h:171`/`:1535`, or a new ODR-swept friend declared in `vorago_voice.h`) that
  substitutes given `lanes.eco` values for `gatherEcosystemLanes`' output before `applyIdentityLanes`
  (`:1911`) runs, so levers are exercised through the real application path. A friend struct is defined in
  exactly one TU per test executable.
  Every lever reaches audio through a smoother: the component's own ramp where one exists (`setSourceLevel`,
  `setPeakLevel`, `setLoopGain`, `setCoupling`, `BloomEngine::setDepth`, `AtmosphereEngine::setLevel`); a
  destination that stores its value directly (`setFreqWander`, `:681-685`; `setFilterBaseCutoff` via
  `applySlotConfiguration`) is slewed by the voice before the write, or not chosen.
- **FR-024 — RT safety, layer, ODR.** Lever code runs at control rate (once per 64-sample chunk inside
  `publishIdentity`), uses fixed arrays sized by `NoiseOrganism::kMaxSources`,
  `ResonanceDriftNetwork::kMaxPeaks`, `FeedbackEcology::kMaxLoops` and `EcosystemEngine::kNumKinds`, and
  performs no allocation, lock, exception or I/O. Code stays in Layer 3 (`vorago_voice.h`, `vorago_engine.h`)
  including only what those headers already include. Any new type name is ODR-swept first.
- **FR-025 — Determinism.** Lever values are pure functions of seeded agent state and configuration; same seed
  and configuration ⇒ same render within `render_fingerprint.h` tolerances, different seed ⇒ different render.

### D. Scope fences

- **FR-026 — Vorago-only edit set.** Production edits are confined to `vorago_voice.h` and
  `vorago_engine.h`. Per Clarifications Q1 and Q2, `vorago_macro_matrix.h` and the plugin's parameter
  default table are **not** edited by this phase (301 stays at 0.35; every macro-row base stays);
  FR-018a's lever-destination base retunes are voice-owned constants inside `vorago_voice.h`/
  `vorago_engine.h`, not macro rows. No Seraphis-consumed header and no Vorago component header
  (`noise_organism.h`, `resonance_drift_network.h`, `feedback_ecology.h`, `bloom_engine.h`,
  `ecosystem_engine.h`) is edited: every lever uses a setter that already exists (Existing components).
  [roadmap 595]
- **FR-027 — No parameter or state change.** No parameter is registered, removed, re-typed or re-ranged; the
  state version stays 2. [roadmap 599: state v3 is Phase 14's; project rule: no param-type swap]
- **FR-028 — Ecosystem view unchanged.** The Phase 13 ecosystem-frame path and its tests are untouched.

### E. Evidence and regression

- **FR-030 — Default-render change documented.** The before (FR-001) and after default-surface descriptors
  (all 14 components), M1 RMS and `t0` are recorded side by side in compliance, and the change is stated as
  intentional voicing. The magnitude of that change is **bounded and gating** (Clarifications Q4, SC-005):
  default-surface M1 RMS stays within ±3 dB of the FR-001(a) before value, and limiter gain reduction at the
  default surface stays ≤ 1 dB — the phase's gates (SC-001 – SC-004) must be reached without leaning on
  loudness or limiter voicing. [roadmap 594, 601–602]
- **FR-031 — No regression.** Green, with no threshold relaxed: Phase 10's SC-019 / SC-019a / SC-019b
  routing tests; every boundedness soak (Phase 10 SC-004a/SC-004b and the overnight soak, the plugin
  `soak_test.cpp`); the Phase 2–13 suites (`dsp_*_tests`, `vorago_tests`); `seraphis_tests`. A pre-existing
  test that encodes the **old default voicing as data** (a measured value, not a behavioural bound) is a
  stop-and-surface item: it is listed with the reason before it is re-measured, never silently edited.
  [roadmap 595–596]
- **FR-032 — CPU.** `VoragoEngine_CpuBudget` (Phase 10 SC-001b) stays green on **both** clauses it asserts
  (`dsp/tests/unit/systems/vorago_perf_test.cpp:1011-1012`): clause (i) **engine + `kCavernMeasuredNsPerBlock`
  ≤ `kReferenceNs`** (3 200 000 ns per 512-sample block at polyphony 4, 48 kHz) — the roadmap ceiling — and
  clause (ii) engine ≤ `kEngineBaselineNsAtPoly4` × 1.5. The ceiling binds first: the regression headroom
  actually available is **1.141× of the baseline** (`kAvailableRegressionHeadroom`, `:246-261`), so "≤ 1.5×"
  is **not** the admissible regression and an engine-only figure is never compared to `kReferenceNs`.
  Measured alone (`node tools/run-cpu-tests.js dsp_systems_tests`); the before/after delta is computed against
  the FR-001(b) log and recorded in ns and %. [roadmap 596–597]
- **FR-033 — Cross-cutting gates.** Zero warnings; `node tools/check-portability.js` clean; clang-tidy
  `vorago` and `dsp` targets clean; pluginval strictness 5 on `Vorago.vst3` (the plugin binary changes with the
  voice).
- **FR-034 — Phase 14 hand-off.** The FR-013 tables are the input Phase 14's re-run specify stage uses for
  its Q2 roster (roadmap 598); this phase names no roster.

---

## Success Criteria

| ID | Metric | Threshold | How measured (test sketch) |
|---|---|---|---|
| **SC-001** | `d(on, off)/t0`, default surface, off = FR-007 true-off | **≥ 2.0** (was 0.368 with `900=0`, which is depth 0 at Life 0) | `vorago_tests.exe "Vorago_EcosystemRuleProbe"` with `VORAGO_PROBE_KNOBS=-` and the FR-007 true-off reference, run alone; the `900=0` cross-check line reported alongside; log in `artifacts/`, row cites the log line |
| **SC-002** | `d(on, off)/t0`, Life max, off = FR-007 true-off | **≥ 2.0**; "before" = the FR-001(a) true-off measurement (run 3's 0.984 was vs depth 0.15 and is **not** the before value) | same, plus `VORAGO_PROBE_SURFACE=109=1`; the log also carries the FR-007 validity assertion (all agent outputs / depths 0 over M1–M3) |
| **SC-003** | audible non-kill knobs, default surface | **≥ 4 of 14**, each counted best extreme: `d ≥ 2·t0`; **each of M1, M2, M3 RMS within ±6 dB of the base render's same minute and ≥ −60 dBFS**; `d(extreme, true-off) ≥ t0` (not OFF-LIKE) | full default probe run (no filter, true-off reference rendered); count line of FR-005; each counted row cited with its three per-minute RMS cells and its `d(extreme, off)/t0` |
| **SC-004** | knob-table completeness | 14 knobs × every non-default extreme × {default, Life max}, each with `d`, `d/t0`, `d(extreme, off)/t0`, M1/M2/M3 RMS, per-minute ΔRMS, mean colony output ratio, flags | the two full logs; compliance table transcribes both |
| **SC-005** | default render before/after | both descriptors (14 components), M1 RMS and `t0` recorded; after-render finite, peak ≤ 0.9661, M1 RMS ≥ −60 dBFS; **gating (Clarifications Q4):** after-render default-surface M1 RMS within ±3 dB of the FR-001(a) before value, **and** limiter gain reduction at the default surface ≤ 1 dB — SC-001 – SC-004 may not be reached through loudness or limiter voicing | FR-001 log vs post-change log; limiter gain reduction measured at the default surface |
| **SC-006** | lever neutrality at depth 0 | with ecosystem depth 0 (`setEcosystemDepth(0)`; the schedulers run as shipped — `VoragoVoice` has no scheduler-off control: the scheduler lane has no depth factor, `vorago_voice.h:1889`, and `setEventRateScale` clamps to [0.1, 10], `:1449-1454`), every chosen lever destination reads **exactly** its base over a 10-minute render at polyphony 1 | `VoragoVoice_EcosystemLeverNeutral` — reads `getSourceLevel` / `getPeakLevel` / `getFreqWander` / `getGainWander` / `getLoopGain` / `getCoupling` / `getFilterCurrentCutoff` (whichever are chosen) against the shadows |
| **SC-007** | lever ignores schedulers | ecosystem depth 0, schedulers at shipped rates: every lever destination reads exactly its base, **with the precondition that at least one event of each wake-only family — `NoiseWake`, `PeakWake`, `LoopWake` — is observed in the window**, counted on the onset edge via `lastEventTarget_` (`vorago_voice.h:1876`, `:2390`, through a friend seam) or by a non-zero `lanes.sched[kind]` for the kind `kindForFamily` maps it to (`:1708`). A `BloomTrigger`/`GhostBurst`-only window does **not** satisfy it. The test **fails** (not skips, not passes) if the precondition is unmet; the plan sizes the accelerated window so it is met | `VoragoVoice_EcosystemLeverSchedulerBlind` (accelerated per Phase 10 FR-086); per-family event counts printed |
| **SC-008** | lever attribution | at depth 1: each chosen lever shows **≥ 3 value changes per minute, each at least the lever's minimum step**: level levers ≥ 0.5 dB; filter-cutoff (colour) ≥ 0.1 octave; frequency wander ≥ 0.1 semitone; gain wander ≥ 0.5 dB; loop gain ≥ 0.01; coupling ≥ 0.01 (changes smaller than the step do not count). Then setting that kind's agents dormant (`EcosystemEngine::setAgentDormant`, `ecosystem_engine.h:776`) makes that lever read **exactly** its base after a settle window `T_settle` = the dormancy gate ramp (`rampSteps_` publishes = `ceil(0.050 / dt_)` steps, `ecosystem_engine.h:357`, `:2318-2329`) + one simulation step (the output is held between steps, `:884-887`) + the lever's own ramp/slew time (component ramp or FR-023 voice slew, stated by the plan with its derivation), and stay there for the rest of the minute, while every other kind's chosen lever keeps meeting the ≥ 3-changes criterion | `VoragoVoice_EcosystemLeverAttribution` `[long]`, SC-019-clause-3 shape; `T_settle` printed |
| **SC-009** | base read-back preserved | after `prepare()` and after every user/macro setter, each existing getter returns the written base, not the modulated value, while the lever is active | `VoragoVoice_EcosystemLeverBaseReadBack`; plus Phase 10 SC-009 / Phase 12 param round-trip tests green unedited |
| **SC-010** | click-free levers | `ClickDetector` (`artifact_detection.h:99`) with a pinned `ClickDetectorConfig` (`:38-44`): `sampleRate` = the render rate (48 000), `frameSize` 512, `hopSize` 256, `detectionThreshold` 5.0 σ, `energyThresholdDb` −60, `mergeGap` 5. **Relative** criterion, same drone, stimulus and seed in each pair: **(a) lever-mapping stress** (not a colony state) — through the FR-023 lane-injection seam, each kind's `lanes.eco[k][s]` is stepped 0 → 1 → 0 (instantaneous steps, hold ≥ 1 s each, every slot, one kind at a time and then all kinds together): click count ≤ the click count of the same render with the injected lanes held at 0; **(b) natural colony** — ecosystem depth 1, 10-minute render. **Pass rule (Clarifications Q7): zero clicks within
  ±1 chunk (64 samples) of any ecosystem simulation-step boundary** (where `output_` jumps,
  `ecosystem_engine.h:884-887`) is the **gate**; clicks elsewhere in the render are **recorded, not gating** —
  a depth-1 and depth-0 render are different sounds, and a legitimate transient away from a step boundary is
  not evidence of a lever click. SC-010(a)'s lever-mapping-stress test is what stresses the mapping itself. Dormancy toggles are **not** an admissible drive (the gate ramps, `:2318-2329`, and cannot force output 1) | `VoragoVoice_EcosystemLeverClickFree`; both counts of each pair printed |
| **SC-011** | worst-case boundedness | **(a) deterministic bound** — through the FR-023 seam every `lanes.eco[k][s]` = 1 (the lane's domain maximum: depth ≤ 1 × output ≤ 1) in every voice, the **shipped** spans, Life 1, 60 s full-poly render; **(b) natural** — depth 1, Life 1, shipped spans, 60 s full-poly render of the natural colony. Each: all samples finite by bit pattern, \|out\| ≤ 0.9661, `getNonFiniteRecoveryCount() == 0`, `getAllocatedBytes()` unchanged; limiter gain reduction printed (informative at these worst-case surfaces per E-5/Clarifications Q4 — the
  default-surface bound is gated separately by SC-005); the bound covers whichever direction each lever moves
  (Clarifications Q3); plus the existing 8 h-equivalent soak green | `VoragoEngine_EcosystemLeverBounded` (both parts; per-push, not `[long]` — a NaN/bounds sentinel; wall clock printed and cited, stop-and-surface above 4 min — plan ruling R-2) + Phase 10 SC-004b |
| **SC-012** | reset contract | after `reset()`, steal and recovery, every lever destination equals a freshly prepared voice's value | `VoragoVoice_EcosystemLeverReset` |
| **SC-013** | determinism | same seed twice: `render_fingerprint.h` match within its tolerances; seeds 1 vs 2 differ | extend the Phase 10 determinism harness with levers active |
| **SC-014** | regression | 100 % pass: `dsp_core/primitives/processors/systems/effects_tests`, `vorago_tests`, `seraphis_tests`, `shared_tests`, including `VoragoVoice_WakeCombineRule`, `VoragoVoice_AgentReductionRule`, `VoragoVoice_EcosystemRouting` — **no test file edited** except under FR-031's surfaced list | full suite logs, `[long]` included |
| **SC-015** | Seraphis untouched | `git diff --name-only <phase-base>..HEAD` lists no `plugins/seraphis/**` and none of the Seraphis-consumed headers | diff output in compliance |
| **SC-016** | CPU | clause (i) engine + `kCavernMeasuredNsPerBlock` ≤ `kReferenceNs` (3 200 000 ns/block) **and** clause (ii) engine ≤ `kEngineBaselineNsAtPoly4` × 1.5, as asserted at `vorago_perf_test.cpp:1011-1012`; effective regression headroom 1.141× of baseline (`kAvailableRegressionHeadroom`); delta vs the FR-001(b) logged measurement recorded in ns and % | `node tools/run-cpu-tests.js dsp_systems_tests`, alone, cooled; the compliance row cites **both** the "clause (i) ceiling" and the "clause (ii) baseline" lines of the WARN block from the before and after logs |
| **SC-017** | no surface change | registered parameter count and every ID/type/range unchanged; `kCurrentStateVersion == 2`; v2 state round-trip green | existing `param_table_test`, `state_v2_test` unedited (Clarifications Q1: parameter 301 and its default are untouched) |
| **SC-018** | sample-rate robustness | SC-006, SC-008 and SC-011 also pass at 44.1 kHz and 96 kHz | the same TUs parameterised over {44100, 48000, 96000} |
| **SC-019** | cross-platform | zero warnings; `check-portability.js` clean; clang-tidy `dsp` + `vorago` 0 findings; pluginval 5 clean | tool logs |
| **SC-020** | wake decisions not masked (FR-018) | for each wake-only kind (`Noise`, `Resonator`, `Feedback`), the fraction of (control step, slot) pairs over the M1–M3 window (155–340 s) — counting only pairs whose slot has **at least one valid routed agent at that step** (`agentValid_`; Clarifications Q6) — on which `lanes.eco[k][s]` > that kind's **shipped** wake base is **≥ 25 %** (ratified, Clarifications Q6) at the default surface (ecosystem depth 0.85, the Life row base, `vorago_macro_matrix.h:633`; default seed; 48 kHz). Before (old bases, FR-001 tree) and after fractions and base values are both recorded. Per Clarifications Q1, `noiseWakeBase_` stays at 0.35: the `Noise` arm's fraction is **recorded, not gated** — the `Resonator` and `Feedback` thresholds are the unconditional gates | `VoragoVoice_EcosystemWakeUnmasked` (reads the lanes through the FR-023 friend seam); per-kind fractions and bases printed |
| **SC-021** | lane-shaping fidelity (if used, FR-019a, Clarifications Q5) | for any kind whose lever input is shaped: shaped(0) == 0 exactly; shaped output ∈ [0, 1] for every input in [0, 1]; the value `combineWake` and every other consumer of `lanes.eco` receive is bit-identical to the unshaped `lanes.eco[k][s]` | `VoragoVoice_EcosystemLaneShapingFidelity` (exercised only if the plan uses shaping) |

---

## Edge cases

- **E-1 Depth 0 vs lanes at zero.** With depth > 0 but every agent output 0 (colony starved), levers sit at
  base — identical to depth 0. This is why a colony kill can look like "ecosystem off" (see E-9).
- **E-2 Scheduler-only activity.** Events keep waking slots through `combineWake`; levers must not follow
  (FR-019, SC-007). A slot can therefore be awake at base level — the Phase 10 behaviour — when only a
  scheduler woke it.
- **E-3 Dormancy.** A lever never substitutes for the wake gate: a slot whose wake is 0 is dormant by the
  cross-cutting rule (roadmap 669–679) regardless of its level lever; a feedback loop's sleep edge still clears
  its audio state (Phase 5 FR-063 exception), so a gain lever cannot revive a stale burst.
- **E-4 Feedback extremes.** Loop gain is clamped by `FeedbackEcology` to ≤ 0.90 and coupling to ≤ 0.5 per pair;
  the governor stays the energy authority. SC-011 is the check that lever + coupling at maximum cannot run away.
  A coupling lever, if chosen, only ever adjusts a pair already active in the shipped ring topology
  (Clarifications Q8) — it does not enable a new pair.
- **E-5 Level extremes.** Level levers are bounded in whichever direction they move (per Clarifications Q3,
  the sign is chosen per lever) so that all levers at their extreme, full poly, still clear the output ceiling
  through the existing `TruePeakLimiter`; the limiter must not become the voicing. At SC-011's worst-case
  surfaces, limiter gain reduction is recorded as informative; at the **default surface** it is **gating**:
  ≤ 1 dB (Clarifications Q4, SC-005, FR-030).
- **E-6 Unsmoothed destinations.** `setFreqWander` writes directly and `setFilterBaseCutoff` reconfigures a
  slot; per-chunk writes need voice-side slew or they are excluded (FR-023).
- **E-7 Ramp re-arm.** Rewriting an unchanged target into a `LinearRamp` stretches a glide (the FR-067
  hazard, `vorago_voice.h:1240-1247`); lever writes early-out on unchanged values.
- **E-8 User/macro vs lever.** A user or macro write to a lever destination changes the base shadow; the
  next control step applies `base + offset`. The getter reports the base (FR-021).
- **E-9 "Kill that is not a kill".** The roadmap's operational kill test is RMS-based (±6 dB, −60 dBFS).
  After this phase ecosystem-off is ≥ 2·t0 away by construction, so an extreme that starves the colony
  (grazeRate → 0) may pass the RMS test while sounding like "ecosystem off". This is **gated**, not only
  reported: FR-004 clause (iii) excludes any extreme with `d(extreme, true-off) < t0` (OFF-LIKE), and the
  RMS test covers every captured minute so a colony that starves only in M2 or M3 is KILL. The per-minute
  mean colony output (FR-005) is reported so the reader sees starvation directly.
- **E-10 `t0` moves too.** Making the colony audible can also enlarge the seed-twin distance (different seeds
  grow different colonies). The gates are ratios to the *same run's* `t0`; a lever that raises both
  proportionally does not pass, which is the intended strictness.
- **E-11 Sample-rate changes.** Levers run per 64-sample chunk, whose duration scales with fs; ramp times are
  in ms in the components. `prepare()` at a new rate re-installs bases through `installIdentityNeutral`
  (SC-012, SC-018). Below `kMinUsableSampleRate` the ecosystem is neutral and levers sit at base.
- **E-12 Seed determinism.** A Seed-parameter change reseeds the colony (`EcosystemEngine::setSeed` calls
  `reset()`); levers follow from the new state without a jump because each rides a smoother (FR-023).
- **E-13 Voice steal / polyphony.** A stolen voice's levers return to base on `resetForSteal` (FR-022); the
  ghost level stays the maximum over rendering voices (`vorago_engine.h:1484-1491`), unchanged.
- **E-14 Non-finite input.** Agent outputs are [0, 1] by `EcosystemEngine`'s contract; every setter already
  rejects non-finite values with the previous value standing. No `std::isnan` under fast-math is introduced.

---

## Open questions (only where the roadmap defers to this spec)

- **OQ-1 — Which candidate levers ship, per kind, and with what spans.** The roadmap lists them as
  *candidates* (line 585). This spec requires at least one per wake-only kind (FR-015); the plan picks, with
  the probe (FR-010 – FR-012) as the deciding measurement and CPU (SC-016) as the constraint.

(OQ-2 — how the noise wake base is retuned — is resolved: see Clarifications Q1, FR-018.)

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| Premise, runs 1–4 (576–583) | Overview table; FR-001, FR-002 |
| Direct, depth-scaled levers on driven sections; candidates (584–586) | FR-015, FR-016, FR-017, FR-019, FR-021–FR-025; SC-006–SC-013 |
| Wake bases retuned (587) | FR-018; FR-018a; SC-020 |
| Rule knobs audible enough to expose in Phase 14 (587–588) | FR-012, FR-013, FR-034; SC-003, SC-004 |
| Gate on the same probe; off vs on ≥ 2·t0 default and Life max (590–591) | FR-002, FR-003, FR-007, FR-010, FR-011, FR-014; SC-001, SC-002 |
| ≥ 4 knobs ≥ 2·t0, not a colony kill (±6 dB, −60 dBFS) (591–593) | FR-004, FR-005, FR-007, FR-012; SC-003, SC-004 |
| Default render may change; before/after recorded (594–595) | FR-001, FR-030; SC-005 |
| Seraphis untouched; Vorago-only voice and engine (595) | FR-026; SC-015 |
| SC-019 observability, soaks, Phase 2–13 suites green (596) | FR-020, FR-031; SC-011, SC-014 |
| CPU inside Phase 10's ceiling, delta recorded (596–597) | FR-001(b), FR-032; SC-016 |
| Phase 14 re-runs specify; Q2 roster from 13b table; state v3 later (598–599) | FR-027, FR-034; SC-017 |
| Cross-cutting: RT, layer, ODR, no bit-exact goldens, portability (660–684) | FR-024, FR-025, FR-033; SC-013, SC-019 |
| Clarify-session decisions Q1–Q8 (2026-09-27) | FR-015, FR-017, FR-018, FR-018a, FR-019a, FR-026, FR-030; SC-005, SC-010, SC-011, SC-017, SC-020, SC-021 |

---

## Review notes

Revision 2026-09-27 (spec challenge). All twelve issues accepted; none rejected, no threshold relaxed.

- **Kill test covered only M1** (fidelity, major): FR-004 now checks M1, M2 and M3 against the base
  render's same minute. SC-003 and SC-004 carry the per-minute cells.
- **Colony-kill extremes could count** (testability, major): FR-004 clause (iii) adds the OFF-LIKE
  exclusion, `d(extreme, true-off) ≥ t0`, as a gate. The mean colony output per minute is reported
  (FR-005). E-9 is rewritten to match.
- **Life-max reference was depth 0.15, not off** (testability, major): FR-007 adds a true-off reference that
  is re-applied every block and asserted valid. FR-010, FR-011 and FR-017 are restated. FR-001(a)
  re-derives SC-002's before value, and the Overview flags run 3's figure.
- **SC-010 fixture could not produce its state** (testability, major): dormancy drive is dropped in favour
  of lane injection through the real `applyIdentityLanes` path (FR-023 seam) plus a natural-colony render.
  The `ClickDetectorConfig` is pinned and the criterion is relative to a depth-0 or lanes-0 twin.
- **SC-011 never reached FR-023's worst case** (testability, major): split into (a) every lane = 1 through
  the seam and (b) the natural render. "Spans at maximum" is replaced by "shipped spans".
- **CPU clauses** (fidelity, minor): FR-032 and SC-016 restate both asserted clauses, the Cavern term and the
  1.141× headroom.
- **CPU before-side** (fidelity, minor): FR-001(b) adds the logged before measurement.
- **SC-006 scheduler depths** (minor): the clause is dropped because no such control exists. The fixture
  names the real API limits.
- **SC-007 family coverage** (minor): the test requires one event per wake-only family and fails if that
  precondition is not met.
- **SC-008 magnitude and settle** (minor): adds minimum steps per lever type and a derived `T_settle`.
- **FR-018 criterion** (minor): SC-020 is added. Its 25 % floor is this spec's choice, not a roadmap figure.
  The Noise arm is record-only because Clarifications Q1 ratified leaving the noise base at 0.35.
- **Citation off-by-ones** (reality, minor): corrected to `:569`, `:581`, `:589` and `:767` after
  re-reading the tree.

---

## Clarifications

### Session 2026-09-27

- **Q1: How is the noise wake base (parameter 301, default 0.35, macro base `NoiseWakeBase`) retuned?** →
  (b) Left unchanged at 0.35; the FR-010/FR-011 gates are reached through the voice-owned peak/loop wake
  bases and the FR-015 levers, not through 301. SC-020's `Noise` arm is recorded, not gated. Moving 301's
  default becomes admissible only afterward, as its own explicitly recorded ruling, if the probe shows the
  noise wake stays masked. [FR-018, SC-017, SC-020]
- **Q2: Besides the wake bases (FR-018) and the FR-016 bases, may this phase retune the default section
  levels/voicing the levers ride on (noise level, peak level, freq wander, ecology mix, loop gain, coupling)?**
  → (b) Yes, for the voice-owned default/base of a value that is a chosen lever's destination (e.g. noise
  level −18 dB, peak level −9 dB, freq wander 1.5 st, ecology mix 0.15, loop gain 0.72, coupling 0.12 are all
  in scope where they are a lever's destination); the corresponding macro-row base in
  `vorago_macro_matrix.h` stays untouched, so there is no parameter-table or state fallout. [FR-018a, FR-026]
- **Q3: Lever polarity — may a lever move its destination below its base, or only above it?** → (b) The plan
  chooses the sign per lever (e.g. level up, cutoff down), still exactly 0 offset at lane 0 in every case;
  E-5/SC-011's bound analysis covers each lever's chosen direction. [FR-017, SC-011]
- **Q4: Beyond boundedness, is there a limit on how far the default-surface render may move?** → (b)
  Default-surface M1 RMS stays within ±3 dB of the FR-001 before value, and limiter gain reduction at the
  default surface stays ≤ 1 dB; E-5 becomes gating at the default surface. The phase's gates may not be
  reached through loudness or limiter voicing. [SC-005, FR-030, E-5]
- **Q5: If the levers plus a base retune cannot reach the gates, what may change the value the colony feeds
  in?** → (b) Fallback: a voice-side per-kind lane shaping (gain or curve, exactly 0 at 0, clamped to [0,1])
  applied to the lever input only; `combineWake` still sees the raw, unshaped lane. Stays inside
  `vorago_voice.h`; no `EcosystemEngine` edit; FR-020 stays literal. [FR-019a, SC-021]
- **Q6: SC-020 denominator and threshold — which (control step, slot) pairs count, and is 25 % ratified?** →
  (a) Denominator = (control step, slot) pairs whose slot has at least one valid routed agent at that step
  (`agentValid_`); threshold 25 % ratified. [SC-020]
- **Q7: SC-010(b) natural-colony click pass rule?** → (c) Absolute: zero clicks within ±1 chunk of any
  ecosystem simulation-step boundary is the gate; elsewhere record-only. SC-010(a)'s lever-mapping-stress
  test is what covers mapping stress. [SC-010]
- **Q8: If coupling is chosen as a Feedback lever, which lane drives pair (from, to), and which pairs?** →
  (a) The `from` loop's lane drives only the shipped neighbour-ring pair `(l, (l+1) % numLoops)`; no new
  pairs are turned on. SC-011's exposure stays bounded to today's pairs. [FR-015, SC-011, E-4]

Every clause of every answer above is carried by the FR/SC ids cited in brackets; this log is not their only
carrier.

### Plan stage (2026-09-27)

Rulings on the plan's §9 items, taken before the build stage.

- **R-1 — CMake registration split accepted.** The two new `dsp_systems_tests` TUs are registered in
  the task right after they are created (so their failing tests build and fail before the production
  code lands) and the final group re-verifies the registration. [FR-031, SC-014]
- **R-2 — `VoragoEngine_EcosystemLeverBounded` stays per-push, measured.** The case prints and the
  compliance record cites its wall clock; if the measured local wall clock exceeds **4 minutes**, the
  build stops and surfaces the figure — it is never shortened, split or tagged `[long]` silently.
  [SC-011, SC-018]
- **R-3 — L8 pre-authorised: retune parameter 301.** If the §3.5 ladder is exhausted at L7 with Gate 1
  or Gate 2 unmet, the next rung is lowering the noise wake base default (`kNoiseWakeId` = 301, 0.35)
  with the Phase 12 parameter-table expectation updated as a recorded ruling (Clarifications Q1's
  deferred option); the run stops and surfaces only if L8 also fails. Macro-row bases and
  `EcosystemEngine` remain out of scope. [FR-018, FR-026, SC-017, SC-020]
