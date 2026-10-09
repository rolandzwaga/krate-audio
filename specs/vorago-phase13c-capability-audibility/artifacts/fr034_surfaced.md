# FR-034 surfaced list: pre-existing tests that encode the old voicing as data

Phase 13c, spec FR-034 (`spec.md:578-583`). A pre-existing test that encodes the pre-13c voicing as data is
entered here before it is re-measured or edited. Nothing is edited silently. Each entry gives the name, the
file:line, the reason, the rung that breaks it, and the proposed re-spec.

Created at T022. Later tasks append to this file (T024 and any red they surface, P5 under the Ghost/Partial
lane ruling).

---

## 1. `VoragoVoice_SpectralTargetIsNeutral`

| Field | Value |
|---|---|
| Test | `VoragoVoice_SpectralTargetIsNeutral`, `[systems][vorago]` |
| Location | `dsp/tests/unit/systems/vorago_voice_test.cpp:1439` (TEST_CASE); data at `:1369` |
| Data encoding the old voicing | `constexpr std::array<float, 4> kNeutralityRichness{0.0f, 0.25f, 0.5f, 1.0f};` (`:1369`) |
| Rung | L1 (T023: cloud richness floor `kCloudRichnessFloor = 0.6346f` plus the active-count decoupling) |
| Status | Surfaced (T022). Not edited. The re-spec is applied at T024. |

### What the test asserts today (read on this tree)

- Both arms are built with `makeFastAttackVoice(0xB100Du)`. Each arm takes `setRichness(richness)` and the bloom
  held inert (`setBloomDepth(0.0f)`, `makeBloomDeterministic`, `setWake(0.0f)`).
- The active count is read after warm-up (`plain->cloud().getActivePartialCount()`). The forced arm's
  amplitudes are `parentAmplitudeExp2(i, p)` with `p = richnessExponent(richness)` (`:1353-1361`) for
  `i < active`.
- Plain arm: `mutableCloud(*plain).clearSpectralTarget();` before every chunk (`:1529`).
- Forced arm: `clearSpectralTarget()` then `setSpectralTarget(ratios, amplitudes, active)` (`:1533-1541`).
- Assertions: `REQUIRE_FALSE(plain->cloud().hasSpectralTarget());` (`:1551`),
  `REQUIRE(forced->cloud().hasSpectralTarget());` (`:1552`), and stereo bit identity
  `VF::bitIdentical(plainOutL, forcedOutL)` / `…R` (`:1559-1560`).

### Reason it breaks below the floor

After T023, `setRichness(r)` stores the user value in `cloudRichness_` but drives the cloud with
`std::max(r, kCloudRichnessFloor)` (plan §2.1, tasks T023). For `r` in `{0, 0.25, 0.5}`, all below 0.6346:

- The plain arm's `clearSpectralTarget()` leaves the cloud running its own parametric law at the **floor**
  richness (14 partials, `p(0.6346)`). It does not leave it at the user law `p(r)` over `N(r)` partials.
- The voice's `updateSpectrumTarget()` now sets `wantTarget = (live > 0u) || (userCount < active)`. With
  `userCount < active` the voice supplies a target itself, so `REQUIRE_FALSE(plain->cloud().hasSpectralTarget())`
  is no longer the inert-bloom invariant at these points.
- The forced arm builds `p(r)` amplitudes over the floor's `active` slots. That is neither the floor law nor
  the user law that T023 installs (user parents up to `userCount`, zero from `userCount` up). So bit identity
  between the arms fails.

The test therefore encodes "cloud richness == user richness" as data at its three below-floor points. At
`r = 1.0` the floor does not engage and the test's premise is unchanged.

### Proposed re-spec (applied at T024, not here)

- Change only the richness set at `:1369` to `{0.6346f, 0.75f, 1.0f}`, at or above the floor, where cloud
  richness == user richness still holds and plain-vs-forced bit identity is the right claim. Add a comment
  citing FR-034 and this file. No tolerance, window or assertion changes. The array size changes 4 -> 3.
- The below-floor half of the claim (the user law over `N(r)` slots, silence from `N(r)` up to the floor
  count) moves to `VoragoVoice_CloudRichnessFloorNeutral` (T019, `vorago_voice_test.cpp:4178`). That test
  covers `r` in `{0, 0.25, 0.40, 0.55}` at a 0.5 dB tolerance.
- T023 verify clause: `…SpectralTargetIsNeutral` may be red **only** at its below-floor points
  (`r` in `{0, 0.25, 0.5}`). Any red at `r = 1.0` is a defect in T023, not a re-spec matter.

### Risk to check at T024 (recorded, not acted on)

The block comment above `:1369` (`:1363-1368`) says the set was chosen so that `p(r) = 3.0 + (-2.5) * r` is
exact in binary floating point. That keeps the test TU's `richnessExponent` and the cloud's computation from
diverging through FMA contraction. `0.75f` keeps that property (`p = 1.125`, exact). `0.6346f` is not
exactly representable, so `p(0.6346f)` may round differently depending on whether a TU fuses the
multiply-add. That would turn bit identity at that point into the toolchain lottery the comment warns about.
If T024's per-push run (or CI on another toolchain) is red at `0.6346f` alone, stop and surface it. Do not
loosen the comparison. Candidate fixes for the user to rule on: substitute an exactly-representable
at-or-above-floor value such as `0.6875f` (`11/16`, so `p = 1.28125`, exact), or keep the floor value and
document it.

---

## 2. `VoragoVoice_EcosystemLaneShapingFidelity`

| Field | Value |
|---|---|
| Test | `VoragoVoice_EcosystemLaneShapingFidelity`, `[systems][vorago]` (13b, SC-021 / FR-019a / FR-016) |
| Location | `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp:1270` (TEST_CASE); assertions at `:1328-1333`. tasks.md cites `:1266` / `:1324-1329`; the +4 drift is T028's additions above it (Probe accessors `partialLaneGain()` / `ghostLaneGain()` at `:131-132`). Re-read on this tree at T029. |
| Data encoding the old voicing | The un-gained Partial and Ghost identities, written with the raw lane and no gain term (`:1328-1333`) |
| Rung | L2 (T030 adds `kPartialLaneGain` / `kGhostLaneGain` at 1.0; T031 ladders `kPartialLaneGain` 1.5 / 2 / 3 on E1 and `kGhostLaneGain` 1.5 / 2 on E5) |
| Status | Surfaced (T029). Not edited. Re-specced only if a gain other than 1 is ruled (spec P5, `spec.md:853-854`; plan `:1050-1051`). Not in plan §5's FR-034 list; found while writing T028. |

### What the test asserts today (read on this tree)

- The raw-lane half runs. The early `SUCCEED("shaping not used"); return;` (`:1284`) fires only when every
  `kLeverInputGain` entry is 1. The shipped table is `{1, 2, 2, 2, 1}` (`vorago_voice.h:1696-1697`), so the
  shaped kinds make `shapingUsed` true and the loop below runs.
- For `raw` in `{0.1, 0.3, 0.55, 0.8, 1.0}`, after `Probe::injectEco(uniformLanes(raw))`,
  `Probe::advanceLifeOnly`, `Probe::schedLanes(S)`:
  - `cloud().getMutation() == clamp(Probe::mutationBase + raw, 0, 1)` (`:1328-1329`)
  - `bloom().getDepth() == clamp(Probe::bloomDepthBase + raw, 0, 1)` (`:1330-1331`)
  - `getGhostRequest() == VoragoVoice::combineWake(0.0f, raw, S[kKindGhost][0])` (`:1332-1333`)
  - All three are exact (`REQUIRE(m.got == m.want)`).
- These mirror today's voice writes at `vorago_voice.h:2155-2159`:
  `cloud_.setMutation(std::clamp(mutationBase_ + partialEco, 0.0f, 1.0f))`,
  `bloom_.setDepth(std::clamp(bloomDepthBase_ + partialEco, 0.0f, 1.0f))`,
  `ghostRequest_ = combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0])`.

### Reason it breaks at a gain other than 1

T030 changes those writes to `mutationBase_ + kPartialLaneGain * partialEco`,
`bloomDepthBase_ + kPartialLaneGain * partialEco` and
`combineWake(0.0f, std::min(1.0f, kGhostLaneGain * eco), sched)`. At 1.0 the products are exact and the test
stays green unedited (T030's verify clause). At any ruled `kPartialLaneGain != 1`, the mutation and bloom-depth
checks miss for every `raw` where `base + raw` is below 1. At any ruled `kGhostLaneGain != 1`, the ghost check
misses wherever `combineWake`'s eco term changes (a gain above 1 moves every `raw < 1`; `raw = 1.0` then
saturates to 1 through `min(1, ...)` and may still pass). The test encodes "the Partial and Ghost writes take
the lane at unit gain" as data.

Not affected by the L2 gains (no re-spec needed):
- The `static_assert` at `:1271-1273` concerns `kLeverInputGain` (FR-019a shaping), which T030 does not touch
  (`vorago_voice.h:1698-1701` stays as is).
- The shaping-contract loop (`:1288-1307`), `checkWakes` (Noise / Resonator / Feedback only,
  `:368-380`) and `checkLevers` take no Partial or Ghost gain.

### Proposed re-spec (applied only at a ruled gain other than 1; T031, before the per-push run)

- Replace the three expected values with the T028 step-2 identities, read through the existing Probe
  accessors (`:131-132`):
  - mutation `== std::clamp(Probe::mutationBase(*voice) + Probe::partialLaneGain() * raw, 0.0f, 1.0f)`
  - bloom depth `== std::clamp(Probe::bloomDepthBase(*voice) + Probe::partialLaneGain() * raw, 0.0f, 1.0f)`
  - ghost `== VoragoVoice::combineWake(0.0f, std::min(1.0f, Probe::ghostLaneGain() * raw), S[kKindGhost][0])`
- Keep the `raw` set, the exact comparison and everything else. Change the header comment at `:1268-1269`
  ("the Partial / Ghost writes keep the RAW lane") to say they take the lane times their L2 gain, still
  unshaped. Add a comment citing FR-034 and this file.
- The expected values must use the same expression order as the voice (`base + gain * raw`, then clamp) so the
  exact comparison holds under FMA contraction in both TUs. If the per-push run (or CI on another toolchain) is
  red by 1 ulp at a ruled gain alone, stop and surface it. Do not loosen the comparison.

---

## 3. `VoragoEngine_GhostConfiguration` clause 1

| Field | Value |
|---|---|
| Test | `VoragoEngine_GhostConfiguration`, `[systems][vorago]`, SECTION "clause 1: FR-017's values read back immediately after prepare()" |
| Location | `dsp/tests/unit/systems/vorago_engine_test.cpp:2760` (TEST_CASE); assertion at `:2768`. tasks.md T033 and plan `:917` cite `:2767`; the +1 drift is from edits above it on this tree. Re-read at T033. |
| Data encoding the old voicing | `REQUIRE(atmos.getDensity() == 0.30f);` (`:2768`), the same literal `prepare()` installs at `vorago_engine.h:375` (`atmos_.setDensity(0.30f);`) |
| Rung | L3, **only if a density rung is ruled** (T035 ladders `kGhostDensity` 0.45 / 0.60 / 1.0 only where the make-up ladder `kGhostTapMakeupDb` 15 / 18 / 21 / 24 is short; T036 rules) |
| Status | Surfaced (T033). Not edited. Re-specced at T036 only if a density other than 0.30 is ruled. |

### What the test asserts today (read on this tree)

- `makeEngine(kSampleRate48, VoragoEngineConfig{})`, then reads `engine->atmosphere()` straight after
  `prepare()` (`:2762-2764`).
- Exact comparisons against literals: density `0.30f` (`:2768`), grain seconds `12.0f`, pitch `-12.0f`,
  position spread `0.90f`, blur `0.85f`, decorrelation `0.85f` (`:2769-2773`), and level `0.0f` (`:2777`).
- The comment at `:2766-2767` states the intent: "both sides are the same literals, and a drifted copy is
  precisely what this clause rules out."

### Reason it breaks at a ruled density

T034 names the literal `static constexpr float kGhostDensity = 0.30f;` beside `kGhostBurstPeak`
(`vorago_engine.h:271`) and makes `:375` read `atmos_.setDensity(kGhostDensity);`. At 0.30 the clause stays
green unedited (T034's verify clause). At any ruled `kGhostDensity` other than 0.30 (0.45, 0.60 or 1.0),
`getDensity()` after `prepare()` returns the new value and `== 0.30f` fails. The test encodes "the ghost
density is 0.30 grains/s" as data.

Not affected by a density rung (no re-spec needed): the other six values in clause 1 (`:2769-2777`) and
clause 2's event gating (`:2780` on), which counts bursts, not grains per second.

Not affected by the make-up rungs: `kGhostTapMakeupDb` does not touch `atmos_.setDensity`.

### Proposed re-spec (applied only at a ruled density other than 0.30; T036, before the per-push run)

- Change only `:2768` to `REQUIRE(atmos.getDensity() == VoragoEngine::kGhostDensity);`. Keep the exact
  comparison: both sides are the same `constexpr float` passed through `AtmosphereEngine::setDensity` /
  `getDensity`, so the clause's "a drifted copy" intent (`:2766-2767`) holds with the named constant as the
  single source. Add a comment citing FR-034 and this file. No other assertion changes.
- If the ruled value is clamped or altered by `AtmosphereEngine::setDensity` so that the read-back differs from
  the constant, stop and surface it. Do not loosen the comparison.

### Note for T035 (recorded, not acted on)

The density ladder can be measured without a rebuild: `VoragoEngine::setGhostDensity` / `getGhostDensity`
already exist (`vorago_engine.h:1039-1045`, the lever-measurement seam described at `:1015-1021`, driven by
`preset_pilot_test.cpp:915`). `prepare()` still installs the compiled value, so a ruled rung is a rebuild with
the new `kGhostDensity`, and it is only that rebuild that reaches this clause.

## Entry 3 — `VoragoEngine_GhostExtensionWiring` SC-010 (a) fingerprint (FR-035, T059) — surfaced 2026-10-02 (B-6/B-7)

- `dsp/tests/unit/systems/vorago_ghost_ext_test.cpp:507` `REQUIRE(cmp.withinTolerance())` against
  `kBaseCommitVoragoFingerprint` (`atmosphere_ghost_fixtures.h:673`, seventh harvest at +12 dB make-up).
- Red on the Group 8 ruled tree: `l3_ruled_suite.log:1391-1398` — checkpoint[23] actual 0.159469 vs reference
  −0.012388, worst metric relative error 0.9076 (bound 0.005). The ONLY per-push red (6061499 of 6061500).
- Cause: the deliberate ghost-tap make-up change 12 → 21 dB (ruling B-6) and the ghost density route (B-7)
  change what Fixture B renders, exactly as the sixth and seventh harvests did when the make-up went to +12.
- Plan: NOT re-harvested now. FR-035 / T059 re-harvests ONCE on the final tree, inside the consuming binary,
  with the provenance block refilled in the same edit (reference_comdat_fp_fast_render_golden_context). Until
  T059 this red is expected on every per-push run of `dsp_systems_tests`; nothing else may be charged to it.

---

## 4. T040 — tests that read level inside the first 20 s of a default-attack note (L5, FR-016 option (b))

Surfaced at T040 (tasks.md Group 10). **Nothing below is edited yet.** Each re-spec is applied at T044 ("apply
the T040 re-specs"), and only for the rung that is ruled. The rung under test is the stage-0 reshape of plan
§2.5: stage 0 runs Linear and the voice raises the phase to `kAttackShapePower` n (ladder 4 / 5 / 6 / 7).

### 4.0 Method and what the predictions are

- **Grep scope.** All 13 `dsp/tests/unit/systems/vorago_*_test.cpp`, `dsp/tests/unit/effects/vorago_composed_chain_test.cpp`
  (same name pattern, a different layer dir), and every `.cpp` under `plugins/vorago/tests/`. For each
  TEST_CASE: does it call `noteOn`, does it shorten stage 0 first (`makeFastAttackVoice` (`vorago_voice_test.cpp:1316`),
  `applyFastAttack` / `kFastAttackEnvelopeConfig`, `setEnvelopeStageTimeMs`, the longrun `kAccelerationFactor`),
  and does it read a level (peak / RMS / dB / `getVoiceLevel` / fingerprint) before 20 s. Then every
  `REQUIRE`/`CHECK` with a `>` / `>=` level floor in `plugins/vorago/tests/` was opened and its window read.
- **Plugin tests run the default attack.** The processor registers stage 0 at
  `VoragoVoice::kDefaultStageTimesMs[0]` = 20 000 ms (`plugins/vorago/src/parameters/envelope_params.h:61`). The
  processor fixtures do not write `kEnvelopeStage0TimeId` (grep: only `state_v2_test.cpp:151` names it, as a
  table row).
- **Measured "today" figures** come from the last full per-push run of `vorago_tests` on this tree,
  `run_vorago_tests_gate_epoch4.log` (repo root; "All tests passed (4022852 assertions in 118 test cases)" at
  `:249`). Each figure cites its line. A case that prints only on failure (INFO) has no measured figure, and
  the entry says so.
- **Predicted drop, envelope only.** The drop is `20·log10(u^n / e_exp(t))`, where u = t / 20 s and
  e_exp is plan §2.5's reference for today's Exponential stage 0, `min(1, 1.3(1 − (0.3/1.3)^{t/T}))`. That
  reference gives 0.33 at 4 s, which matches plan §5's figure.

  | t | today e_exp | n = 4 | n = 5 | n = 6 | n = 7 |
  |---|---|---|---|---|---|
  | 1.57 s | 0.141 | −71 dB | — | — | — |
  | 2 s | 0.177 | −65.0 dB | −85.0 | −105.0 | −125.0 |
  | 4 s | 0.330 | −46.3 dB | −60.3 | −74.3 | −88.2 |
  | 6 s | 0.463 | −35.1 dB | −45.6 | −56.1 | −66.5 |
  | 8 s | 0.577 | −27.1 dB | −35.0 | −43.0 | −50.9 |
  | 10 s | 0.676 | −20.7 dB | −26.7 | −32.7 | −38.7 |
  | 20 s | 1.000 | 0 | 0 | 0 | 0 |

  These figures are the envelope's alone. The output also carries resonator, ecology and body memory, and
  those tails were excited by an even quieter past, so the real drop is about this size. That is a
  prediction, not a measurement. Every verdict below marked "predicted" is re-read on the L5 rung build
  before the ruling (FR-026). A case is **red** at a rung when its predicted drop exceeds its measured
  margin.
- **Line drift found while reading (for T044).** On this tree `setEnvelopeStageTimeMs` is at
  `vorago_voice.h:1089` (tasks.md / plan cite `:1068`), `noteOn` is at `:944` with `mse_.gate(true)` at `:961`
  (cited `:944`), and the Standard loop's `velocity_ * mse_.process()` is at `:2529` (cited `:2449`).

### 4.1 Class A — an absolute level floor that the reshape breaks or puts at risk

| # | Test (file:line) | Window, bar | Today (measured) | Margin | Predicted verdict |
|---|---|---|---|---|---|
| A1 | `VoragoVoice_SilenceClearsEcologyAudio` (`vorago_voice_test.cpp:877`; drive `:908-914`, `REQUIRE(steadyDb > -50.0)` `:923`) | peak of the last 512 block at 4 s, > −50 dBFS | −47.2 dBFS (MSVC, the case's own comment above `:904`) | 2.8 dB | **red at every rung**, including n = 1 (−4.3 dB) |
| A2 | `Vorago_ProcessorRendersHeldNote` / `HeldNoteIsAudible` (`processor_audio_test.cpp:248`, `REQUIRE` `:278`) | last-second peak of 8 s at registered defaults, ≥ 1e-4 | 0.0317 (log `:81`) = −30.0 dBFS | 50.0 dB | at risk: n = 6 +7 dB, **n = 7 red** (−0.9 dB) |
| A3 | `Vorago_ParamFlowReachesEngine` / `GainZeroSilences` (`param_flow_test.cpp:92`, non-vacuity `:107`) | 4 s peak, ≥ 1e-4 | 0.00863 (log, `param_flow_test.cpp(106)`) = −41.3 dBFS | 38.7 dB | **red at every rung** |
| A4 | `…ParamFlowReachesEngine` / `MacrosAreLive` (`param_flow_test.cpp:167`; precondition `:205`, rmsDiff `:214`) | 8 s; peak ≥ 1e-4; rmsDiff(M0, M1) over [3072, end) > 1e-3 | peak 0.0317, rmsDiff 0.00673 / 0.00486 (log `param_flow_test.cpp(204)`, `(213)`) | 50.0 dB / 16.6 dB | rmsDiff **red at every rung**; precondition n = 7 red |
| A5 | `…ParamFlowReachesEngine` / `ParamTimingIsBlockGranular` (`param_flow_test.cpp:217`; window from block 469 ≈ 5.06 s, `:224-228`; `:264`) | 1 s rmsDiff > 1e-3 | 0.00847 (log `(263)`) | 18.6 dB | **red at every rung** (drop at 6 s ≥ 35 dB) |
| A6 | `Vorago_MacrosDriveTheMatrix` (`param_surface_test.cpp:280`; reference RMS `:340-341`; rmsDiff `:374`) | 4 s (`renderFourSeconds`); RMS ≥ 1e-4; rmsDiff > 1e-3 | rmsDiff 0.00845 (log `(373)`); RMS INFO only | 18.5 dB | **red at every rung** |
| A7 | `Vorago_MacroPushOncePerProcess` / `BlockSizeInvariance` (`param_surface_test.cpp:586`, `:597`) | 4 s, ref peak over [3072, end) ≥ 1e-4 | 0.00190 (log `(596)`) = −54.4 dBFS | 25.6 dB | **red at every rung** |
| A8 | `Vorago_EveryRouteReachesTheChain` / `Cavern` (`param_surface_test.cpp:1591`; `:1640`, `REQUIRE` `:1670-1671`) | 4 s, reference RMS ≥ 1e-4 | INFO only | — | predicted **red at every rung** (a 4 s window) |
| A9 | `Vorago_Phase12DefaultsMatchPhase11Chain` (`param_surface_test.cpp:63`, `:91-92`) | 8 s, ref peak ≥ 1e-4 (note 36) | not printed | — | predicted at risk at n = 7, like A2 |
| A10 | `Vorago_HostResendsEveryParameter` (`param_surface_test.cpp:2068`, `:2113-2114`) | 10 s, peak ≥ 1e-4 | not printed | — | predicted green (drop at 10 s is ≤ 39 dB); re-read only |
| A11 | `Vorago_MidiEventTranslation` / `OffsetTiming` `:324`, `OffsetClamping` `:356`, `:371`, `OutOfOrderEventsAreSorted` `:390` (`midi_event_test.cpp`) | `kTimingSamples` = 3372 + 72 000 ≈ 1.57 s (`:53-54`), peak ≥ `kAudibleFloor` 1e-4 (`:48`) | 0.00365 / 0.00366 / 0.00416 / 0.00413 (log `(323)`, `(355)`, `(370)`, `(389)`) ≈ −48 dBFS | ≈ 31 dB | **red at every rung** (−71 dB at n = 4) |
| A12 | `…MidiEventTranslation` / `BlockSizeInvariance` (`midi_event_test.cpp:432`, `:462`) | 4 s (`kInvarianceSamples`, `:57`), ref peak ≥ 1e-4 | 0.00932 (log `(461)`) = −40.6 dBFS | 39.4 dB | **red at every rung** |
| A13 | `holdUntilAudible`, cap 375 blocks = 4 s, level ≥ `kTailSilenceThreshold` 3.1623e-5 (−90 dBFS, `vorago_voice.h:290`): `midi_event_test.cpp:128-139` (used by `NoteOffReleases` `:206`, `:225`); `sustain_test.cpp:205`, `:300-310` (every `Vorago_SustainLatch` SECTION through `strikeAudible`, `:346-468`); `channel_pressure_test.cpp:321`, `:361-367` (`SetStateZeroesSustainAndPressureUntilNextProcess`) | voice level crosses −90 dBFS within 4 s | crossing count not printed. The held voice's median level over 20 s is 0.085 (log, `ecosystem_frame_test.cpp(794)`), so its 4 s level today is about 0.03–0.05 | ≈ 60 dB at 4 s | predicted: n = 4/5 green, **n = 6/7 red** (−74 / −88 dB) |
| A14 | `Vorago_SeedParameter` (`seed_test.cpp:95`): helper non-vacuity `:83` (8 s whole render); `(5) LiveReseed` `:134`, warm-end peak `:156-157` (last block at ≈ 2 s), post-change peak `:186` ([2, 6] s) | peak ≥ 1e-4 | not printed | — | `:156-157` predicted **red at every rung**; `:186` red at n ≥ 6; `:83` at risk at n = 7 |
| A15 | `Vorago_ProcessorLifecycle` / `SetActiveClearsTail` (`lifecycle_test.cpp:215`; `:236`, `:238`, `:245`) | 8 s hold at velocity 1.0, last-second peak ≥ 1e-4 | not printed (≈ A2 + 2 dB) | ≈ 52 dB | at risk at n = 7, like A2 |
| A16 | `VoragoEngine_RepeatedBroadcastIsInert_Short` (`vorago_param_surface_test.cpp:1551`, 2 s; `REQUIRE(rmsAC > 1e-3)` `:1546`); the `[long]` twin `:1555` (10 s) | A-vs-C rmsDiff > 1e-3 over the whole render. `VoragoVoiceParams` carries no envelope field (`vorago_engine.h:160`), so the attack stays at 20 s | INFO only | — | `_Short` predicted **red at every rung**; the 10 s `[long]` twin at risk at n ≥ 5 |
| A17 | `VoragoEngine_OvernightSoak` `[long]` (`vorago_engine_longrun_test.cpp:797`; monotone clause over [0, T_settle = 120 s) `:836-858`; FR-014a bans the fast attack here, `:209-215`, asserted `:820-826`) | 10 s moving mean square never dips by more than 0.1 dB (`kMonotoneRelativeEpsilon`, `:250`) | green on base (8 h nightly) | — | **unknown, needs measuring.** At n = 6 the first ≈ 10 s carry almost no enveloped signal, so the moving average is the unenveloped / chaotic remainder, where a > 0.1 dB dip is plausible |
| A18 | `Vorago_ParameterStepsAreContinuous` `[long]` (`continuity_test.cpp:309`; steps from 1 s every 288 ms to ≈ 19.4 s, `:68-77`; clause 3 `maxTest ≤ 1.5·maxRef` `:347`, controls `:366`, `:381`) | ratio test vs an unstepped twin, inside the attack | green on base | — | **unknown, needs measuring.** Rows whose effect bypasses the voice envelope (cavern, output stage, ghost, sub) see `maxRef` collapse during the shaped attack while their own step does not, so the ratio can blow up |
| A19 | `Vorago_EcosystemFrame_FocusVoice` (`ecosystem_frame_test.cpp:1019`): `holdUntilAudible` (−80 dBFS `kSilenceLevel` `:90`, cap 20 s, `:240-247`), then time-to-Idle ≤ `kBoundSeconds` 60 s (`:1020`, `:1056`, `:1083`, `:1151`, `:1166`) | — | time to Idle 12.48 / 13.13 / 13.55 s; orphan tail 18.65 s (log `(1069)`, `(1093)`, `(1154)`, `(1181)`) | — | the hold passes, later. Time-to-Idle is **at risk** at every rung: see 4.4, release from the unshaped level |

**Proposed re-specs (not applied). Two rules, matched to what each case claims:**

1. **The case measures something other than the attack** (A1, A3–A8, A11–A14, A16, A19).
   - The DSP-library cases shorten stage 0 for the drive through `setEnvelopeStageTimeMs(0, …)`
     (`vorago_voice.h:1089`; the engine fan-out in `vorago_param_surface_test.cpp` is `VoragoEngine::setEnvelopeStageTimeMs`).
   - The plugin cases write `kEnvelopeStage0TimeId` (`plugin_ids.h:212`) in block 0 on **every** arm,
     including the hand-built reference chain where one exists (`renderPhase11ReferenceChain` hooks).
   - Bars, windows and assertions stay unchanged.
   - **A1, the ruled shape (tasks T040):** `voice->setEnvelopeStageTimeMs(0, 1000.0f)` before `noteOn`
     (`:905`), with a comment citing FR-034 and this file. The −50 dB control (`:923`) and the −80 dB claim
     (`:949`) stay. Stage 0 then completes at 1 s, and the 4 s drive sits at about 0.96 of stage 1 instead of
     about 0.33, so the driven level rises (predicted ≈ +9 dB). The claim therefore has more stored energy to
     clear. That makes the case stricter, not looser.
   - **The precedent cuts both ways.** The Phase 11 / 12 non-vacuity floors (A7, A12, and the "P-6"
     comments at `param_surface_test.cpp:597` and `midi_event_test.cpp:324`, `:462`) were ruled under P-6:
     "the script is lengthened (longer render, velocity 127), never the threshold"
     (`specs/vorago-phase11-plugin-scaffold/spec.md:815-816`). Lengthening every 4 s script past 20 s
     multiplies those cases' wall clock about 6× (A7 is 8.0 s today, log `(633)`; A12 is 7.8 s, log `(495)`).
     Shortening stage 0 leaves the threshold untouched, which is what P-6 protects.
   - **Decision to confirm at T044:** stage-0 shortening (proposed) or P-6 lengthening, for the P-6-tagged
     cases.
2. **The case's premise is the registered defaults** (A2 "registered defaults (no parameter changes)"
   `processor_audio_test.cpp:249-250`, A9, A10, A15).
   - These are not re-parameterised. They follow P-6 literally: lengthen the render past the stage-0 time
     (for example A2 at 24 s, last second [23, 24] s; velocity unchanged).
   - Re-specced only at a rung where the L5 build shows them red. At the proposed n = 6 the prediction is
     green (+7 dB).

**A17 and A18 are `[long]`.** They are re-read on the L5 rung build (A17 through a scoped 120 s render of
the three soak seeds, printing `worstRatio`, not the 8 h run). If either is red they are **surfaced, not
re-specced here**. A17's slow attack is the property under test (FR-014a), so the fix would be a ruling on
the clause, not a fixture change.

### 4.2 Class B — the case stays green, but its comparison approaches silence (vacuity weakens)

Each of these compares two arms that share the envelope, with no level floor. Each stays green under the
reshape, but against near-silent renders: at 1 s, n = 6 is −135 dB below today. They are listed so the
weakening is not silent. The proposed re-spec is the same stage-0 shortening on both arms, applied with the
4.1 group if the user wants the comparisons to keep their teeth.

- `VoragoVoice_SeedsAreDistinctStreams`, "two voices at the same seed are bit-identical"
  (`vorago_voice_test.cpp:1172`, 1 s, `:1180-1184`).
- `VoragoVoice_EcosystemLeverReset` (`vorago_ecosystem_lever_test.cpp:676`, 1 s `kRenderSamples` `:685`).
  Fingerprint twin `:824-841`. Absolute `kSampleTolerance` 5e-4 checkpoints
  (`tests/test_helpers/render_fingerprint.h:58`) become vacuous; the relative metrics stay live.
- `VoragoEngine_NewSettersDefaultInert_Short` (`vorago_param_surface_test.cpp:1364`, 4 s) and
  `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short` (`:1094`, 4 s): `maxAbs ≤ 1e-6`, no non-vacuity floor
  (`:1088-1089`, `:1257-1258`).
- `Vorago_EcosystemFrame_MatchesEngine` (`ecosystem_frame_test.cpp:749`): the held voiceLevel
  min / median / max is printed as OQ-3 evidence (log `(794)`). The figures move; nothing asserts on them.
- `Vorago_SeedTableSpread` `[long]` (`seed_test.cpp:190`, 30 s from 3072): non-vacuity `:217` passes. The
  spread statistic includes the attack, so its printed value moves. Re-read only.

### 4.3 Checked and not sensitive (no entry needed)

- **Every DSP case that shortens stage 0 first:** `makeFastAttackVoice` (`vorago_voice_test.cpp`, all cases
  from `:1391` on), `applyFastAttack` (engine, nonfinite, macro, ghost-ext, composed-chain, perf,
  ecosystem-lever click-free / determinism), and the voice longrun `accelerate()` (`vorago_voice_longrun_test.cpp:259`).
  The longrun cases read lanes and counts, not audio level. A 50 ms stage 0 is reshaped too, but every
  reading sits well after it.
- `VoragoVoice_EnvelopeShapeIsShipped` (`:965`): state reads only. No test reads a stage curve:
  `kStageCurve` appears only at `vorago_perf_test.cpp:588`.
- The ecosystem-lever lane cases (`:548`–`:1411`): exact lane reads. `VoragoEngine_EcosystemLeverBounded`
  (`:1810`): an upper bound, plus `peak > 0`.
- `VoragoEngine_StealPolicy` and `VoragoEngine_SeedIsPerSlotNotPerNote`: no render.
- `VoragoEngine_UnpreparedAndDegenerate`, `VoragoVoice_UnpreparedAndDegenerate`, `VoragoVoice_AllocationAccounting`:
  finite / upper-bound / count checks.
- `VoragoEngine_OvernightEvolution` and `Vorago_RandomSurfaceSoak` `[long]`: windows after 120 s and 50 s.
- `Vorago_FullSurfaceAutomationAllocFree`: every parameter, stage times included, is redrawn each block.
- `Vorago_StateRoundTripV2` `:529`: 4 s, but every persisted field, stage 0 included, is a seeded
  off-default draw (`state_v2_test.cpp:444`), so this is not a default-attack note. Re-read on the L5 build
  only.
- Hidden `[.probe]` cases: not per-push. `Vorago_EcosystemRuleProbe` reads from 160 s;
  `VoragoEngine_GhostLevelProbe` from 220 s.
- **The factory-preset harness** (`preset_sweep_test.cpp` `[long][vorago-sweep]` arms, `preset_pilot_test.cpp`
  probes): this is FR-016's measured-reach window, owned by T043 / T045, not re-specced here.
  `Vorago_PresetSweep_LongRender` arm 2 (every 10 s window of [A, H] ≥ −60 dBFS,
  `preset_sweep_test.cpp:1045`) depends on where T045 puts A. T045's verify clause must re-read it.

### 4.4 Finding for T044 (outside T040's edit scope): release from mid-stage-0 jumps up

Plan §2.5 shapes `e` only while `mse_.getCurrentStage() == 0 && mse_.getState() == MultiStageEnvState::Running`.
`MultiStageEnvelope::gate(false)` enters release from any active state (`multi_stage_envelope.h:124-127`), and
`processReleasing()` decays the generator's own `output_` (`:352-354`), which is the **unshaped** Linear value.

A note released during stage 0 therefore steps the gain from `e0 + span·u^n` up to the Linear `u`. At t = 4 s
and n = 6 that is 6.4e-5 → 0.20, about +70 dB in one sample. That is a click, and FR-016 requires continuity.

- T041 clause 4 covers only a note-on while Running at 0.85.
- The same jump lengthens every release tail that A13 / A19 time, which is why A19's 60 s bound is at risk.
- A legato re-strike while Releasing enters Sustaining at the current unshaped level (`:110-119`), but that
  path starts from Releasing, so it inherits the same step.

T044 needs a continuity rule for gate-off inside stage 0, and a test for it, before L5 can be ruled. Options
for the user (no recommendation implied):

- (a) latch the shaped / unshaped ratio at gate-off and carry it through the release;
- (b) shape the release start (requires a writable start level; `MultiStageEnvelope` is read-only for this
  phase);
- (c) accept and document.

### 4.5 Applied after the B-10 ruling (n = 4), plugin side (gate fix, 2026-10-02)

Rule 1 (stage-0 shortening; thresholds, windows and assertions unchanged) applied to the plugin cases the
n = 4 `vorago_tests` gate showed red (`run_vorago_tests_gate_epoch4b.log`): A3, A4, A5, A6, A7, A8, A11, A12,
A14 (`seed_test.cpp` LiveReseed warm-end check). Mechanism: `ProcessorFixture::shortenStage0()` writes
`kEnvelopeStage0TimeId` = `envelopeTimeToNormalized(1000 ms)` as a parameter-only call on **every** arm
(`vorago_test_fixture.h`); the hand-built reference chain gets the identical plain value through the new
`RefChainSpec::stage0TimeMs` (`phase11_reference_chain.h`, set by `param_surface_test.cpp` `fourSecondSpec()`).
The P-6 "lengthen, never the threshold" alternative was not taken (section 4.1 rule 1 proposal; ~6x wall clock).
Re-read after the fix (`F:/tmp/p13c_fix/run_targeted.log`): A11 peaks 0.0042 / 0.0043 / 0.0048 / 0.0049, A12
0.0142, A3 0.00934, A4 rmsDiff 0.00687 / 0.00494, A5 0.00834 / 0.00559, A7 0.00184; full filtered
`vorago_tests` 119 / 119 cases green (`F:/tmp/p13c_fix/run_full.log`). Rule-2 cases (A2, A9, A10, A15) and A13
were green at n = 4 and are untouched. A1 / A16 (dsp_systems_tests) are not in this change.

**A13 applied after Group 12 (ruling B-12, 2026-10-03).** With `kBreathGravityLaneGain = 3.0` the default held voice at pitch 60 / velocity 100 reads 2.17545e-05 (< 3.1623e-5) at the 375-block cap: 10 `Vorago_SustainLatch` SECTIONs (`sustain_test.cpp:308`) and `Vorago_ChannelPressure` / `SetStateZeroesSustainAndPressureUntilNextProcess` (`channel_pressure_test.cpp:367`) red. Bisect: the same tree with the gain at 1.0 is green (`F:/tmp/p13c_g12/sus_b1.log`, all passed), so the breath lane is the cause, not a defect (the 4 s point sits inside the ruled 20 s shaped attack). Rule 1 applied: `SustainRig` and that SECTION call `ProcessorFixture::shortenStage0()` right after `prepare()`; cap (375 blocks) and threshold (`kTailSilenceThreshold`) unchanged. `midi_event_test.cpp` `NoteOffReleases` (pitch 48) stays green unedited. Re-read: filtered `vorago_tests` 118 / 119 (`F:/tmp/p13c_g12/run_full.log`), the one red being the factory-preset tree mismatch at Feedback Mire life[0] handed to Phase 14 (tasks.md T050 amendment).

## 5. `VoragoVoice_LifeModulatorLanes` (T049, L6 life lanes, FR-017b) — surfaced 2026-10-02

**Applied 2026-10-02 (ruling B-12, `kBreathGravityLaneGain = 3.0`):** extremes `± kBreathGravityLaneGain · 0.30 ± 0.01`, and the non-zero-base sum check reads `clamp(0.25 + lane, −1, 1)` because the gravity sum clamps (plan §2.6). Green: `l6_breath3_verify.log`.

| Field | Value |
|---|---|
| Name | `VoragoVoice_LifeModulatorLanes` (`[systems][vorago]`) |
| File:line | `dsp/tests/unit/systems/vorago_voice_test.cpp:2921` (TEST_CASE); the extremes assertions at `:2983-2984` (tasks.md cites `:2910` / `:2971-2972`, the line numbers before the T048-era edits shifted the file) |
| Encodes | The published breathing lane's extremes equal the configured breathing depth: `REQUIRE(std::abs(hi - 0.30f) <= 0.01f)` and `REQUIRE(std::abs(lo + 0.30f) <= 0.01f)` at depth 0.30. That holds only while `getBreathingGravityLane()` is `1.0 · breath_.getCurrentValue()`. |
| Status at T049 | Green unedited: T049 ships `kBreathGravityLaneGain = 1.0f` and `kTidalFogLaneGain = 1.0f` (`vorago_voice.h`, the no-change value, plan §2.6), so `publishLifeLanes()` publishes `1.0 · breath` and `clamp(1.0 · tide, 0, 1)` = `max(0, tide)` for the tide's [−1, 1] output. Not edited. |
| Rung that breaks it | Any L6 ladder rung with `kBreathGravityLaneGain ≠ 1` (T050 ladder 1.5, 2, 3). The lane is published UNCLAMPED (plan §2.6), so at gain g the extremes move to `±0.30 · g`. The zero-base worstSumError clause and `hi − lo ≥ 0.20` are unaffected (at base 0 the sum is `±0.30 · g` ≤ 0.90 for g ≤ 3, inside the ±1 gravity clamp). The non-zero-base clause (`:2988-2996`, base 0.25, `worstOffsetError == 0.0f`) holds while `0.25 + 0.30 · g ≤ 1`, i.e. g ≤ 2.5; at the g = 3 rung the gravity sum reaches 1.15 and clamps, so that clause would also break wherever the 400-step window reaches the breath peak. Re-spec for that clause if g = 3 is ruled: compare against `std::clamp(0.25f + lane, -1.0f, 1.0f)` (the clamp is plan §2.6's specified behaviour). A `kTidalFogLaneGain` change does not touch this test. |
| Proposed re-spec | Extremes = `kBreathGravityLaneGain · 0.30 ± 0.01`: `REQUIRE(std::abs(hi - VoragoVoice::kBreathGravityLaneGain * 0.30f) <= 0.01f)` and `REQUIRE(std::abs(lo + VoragoVoice::kBreathGravityLaneGain * 0.30f) <= 0.01f)`. The "not its square" intent (a second depth multiply would put the extremes at ±0.09 · g) is preserved. Applied at T050 only if the breath gain is ruled away from 1. |

## 6. `VoragoEngine_GhostConfiguration` default-level clause and `ghost_params_test` default (B-14) — applied 2026-10-04

`vorago_engine_test.cpp:281` asserted `getGhostPeakLevel() == kGhostBurstPeak`; `ghost_params_test.cpp:138` and
`:181` and `param_table_expected.h:172` expected the parameter default 0.60; the `makeGhostEngine` and
`makeWiringEngine` fixtures (`vorago_engine_test.cpp`, `vorago_ghost_ext_test.cpp`) relied on the default to leave
the gate OPEN (`REQUIRE(getGhostPeakLevel() > 0)`). Ruling B-14 moves the default level to
`VoragoEngine::kGhostDefaultPeakLevel` (0.0) and leaves `kGhostBurstPeak` to the SC-027 trigger thresholds only; the
default assertions now read 0.0 / the new constant, and the two fixtures open the gate explicitly at
`kGhostBurstPeak` (the pre-B-14 level), so every clause they count is unchanged. No threshold, window or render
length changed. Green: `l7_b14_respecs.log`; the fingerprint clause of `VoragoEngine_GhostExtensionWiring` stays
the entry 3 red until the T059 re-harvest.

## 7. `VoragoVoice_BloomSlotAccounting` clause 3 (`[long]`, T023 / ruling Q2) — found at T060, applied 2026-10-05

`vorago_voice_longrun_test.cpp:555` asserted `hasSpectralTarget() == (liveChildCount > 0)`, the pre-13c rule. T023
(FR-011, ruling Q2) makes the voice ALSO engage the target whenever the user's N(r) is below the cloud's floored active
count, which restores the user's spectrum under `kCloudRichnessFloor` (`vorago_voice.h` `updateSpectrumTarget`). The case
is `[long]`, so no per-push gate ran it; it first ran in the T060 CPU lane (`cpu_final_dsp_full.log`: "first violation at
control step 0: hasSpectralTarget 1 but liveChildCount 0"). Re-spec: the expectation mirrors the voice,
`(live > 0) || (N(r_user) < getActivePartialCount())`, with N(r) evaluated as `partialCountFor` does. Clauses 1 and 2
(capacity and reserve bounds) are unchanged; no threshold moved. Green: `slot_accounting_respec.log`.
