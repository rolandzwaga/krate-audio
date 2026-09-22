# Feature Specification: Vorago Phase 10 — Voice & Engine

**Spec slug:** `vorago-phase10-voice-engine`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → **Phase 10** (lines 446–474). Supporting
roadmap statements this spec is traceable to: the reuse-inventory rows **L14 Acoustic Body**
(line 122), **Ecosystem agents** (line 123), **Voice / Poly** (line 124) and **Output safety**
(line 125); the **Seraphis-sequencing** note that names `seraphis_voice.h` / `seraphis_engine.h` /
`seraphis_macro_matrix.h` as Phase 10's pattern template (lines 140–142); the **ODR note**
(lines 127–129); the **dependency graph** (lines 530–543, where Phase 10 is the single convergence
node of phases 1–9); the **cross-cutting constraints** (lines 550–574), of which **Dormancy** is
lines 561–567, **no bit-exact float goldens** is line 568, **portability** is lines 569–571 and
**"Shared-component changes … ContinuousBody materials … must keep Seraphis's tests green"** is
lines 572–574; roadmap **Open Question 4** (lines 584–586), already **decided in Phase 6** and
therefore **binding, not open, here**; roadmap **Open Question 5** (line 588) and **Open Question 6**
(line 589) — the only two decisions the roadmap explicitly defers *to this spec*.
Every roadmap line number below was read and re-verified against `specs/Vorago-roadmap.md` this
session.

**Layer:** three new **Layer 3** components in `dsp/include/krate/dsp/systems/` — `VoragoVoice`
(`vorago_voice.h`), `VoragoEngine` (`vorago_engine.h`), `VoragoMacroMatrix`
(`vorago_macro_matrix.h`) — **plus two append-only extensions to shipped Layer 3 components**
(amendment **A-13**, plan S14.1):

1. **`ContinuousBody`** — the dark material table; justified in *Architecture ruling AR-4*, bounded
   by FR-030 – FR-039, gated by SC-016 and SC-025.
2. **`FeedbackEcology`** — one appended public method, `void silenceAudio() noexcept`, the RT-safe
   half of `reset()` (which the component itself documents as a control-thread call,
   `feedback_ecology.h:861-862`), required by the voice's steal and non-finite-recovery paths
   (plan B-7, FR-005, FR-072). **Zero deleted lines**, no new class, member, enumerator or constant.
   **SC-016's Seraphis gate does not widen to cover it**: `FeedbackEcology` has no Seraphis
   consumer — a repo-wide sweep finds only Vorago Layer 3 peers, `dsp/lint_all_headers.cpp` and its
   own five test TUs — so its green gate is its own suites, not `seraphis_*`.

**Test target:** `dsp_systems_tests` — an **enumerated, not globbed** source list opening at
`add_executable(dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`). A TU that is not listed there
silently drops out of the build and its cases never run. The Vorago Phase 2/3 blocks in that same
list (`:408-412`, `:413+`) state this rule in the file itself. **One exception:** the composed
engine+`CavernVerb` chain case registers with `dsp_effects_tests` instead, beside `CavernVerb`'s own
tests (FR-084a).

**Depends on (all shipped and verified this session, file:line in the *Existing components* table):**
`HarmonicCloud`, `NoiseOrganism`, `ResonanceDriftNetwork`, `FeedbackEcology`, `BloomEngine`,
`EcosystemEngine`, `SlowEventScheduler`, `ContinuousBody`, `AtmosphereEngine`, `SubharmonicEngine`,
`SpectralSmear`, `CavernVerb`, `VoiceAllocator`, `MultiStageEnvelope`, `GrowthEnvelope`,
`BreathingModulator`, `TidalModulator`, `BrownianDrift`, `TapeSaturator`, `TruePeakLimiter`.

**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only
(roadmap lines 152, 477–500).

---

## Overview

Phase 10 is the **composition phase**: it builds no new DSP algorithm. Everything that makes a sound
in Vorago already ships — phases 1–9 delivered nine components and Seraphis delivered five more —
and this phase wires them into a playable instrument core, in the exact shape Seraphis Phase 7
proved: a **voice** that owns its sub-components by value and renders them on one 64-sample control
grid, an **engine** that owns N voices plus the global post-sum chain, and a **macro matrix** whose
mapping is a `constexpr` data table rather than code (roadmap lines 451–468; pattern template at
`dsp/include/krate/dsp/systems/seraphis_voice.h:127`, `seraphis_engine.h:205`,
`seraphis_macro_matrix.h:182`).

Four facts found by reading the shipped headers this session shape every requirement below, and
three of them contradict the roadmap's own description of what Phase 10 can do:

1. **A Layer 3 engine cannot own the cavern space.** `CavernVerb` is Layer 4
   (`dsp/include/krate/dsp/effects/cavern_verb.h:193`); `VoragoEngine` is Layer 3. Roadmap line 461
   writes the global chain as "voice-sum → subharmonic engine → spectral smear → cavern space →
   output". Seraphis hit this exact wall and solved it: `SeraphisEngine::processOutputStage` is
   documented "**the caller runs this AFTER its reverb** … in the composed chain the buffer is the
   AetherReverb return" (`seraphis_engine.h:608-612`). Phase 10 inherits that seam — see **AR-1**.
2. **`EcosystemEngine` is not a `ModulationSource`.** Roadmap lines 393–397 promise "the ecosystem
   *is* a bank of `ModulationSource`s, so every engine from phases 2–7 hooks in **without new
   plumbing**". The shipped class declares `class EcosystemEngine {` with no base
   (`ecosystem_engine.h:143`) and publishes per-agent floats through
   `[[nodiscard]] float getAgentOutput(std::size_t i) const noexcept` (`:886`). `ModulationSource`
   is an abstract base with two pure virtuals (`core/modulation_source.h:31-43`). The wiring
   Phase 10 must write is therefore **explicit and direct**, and that is also what Seraphis does —
   `SeraphisVoice`'s size guard names `VoiceModRouter` and `ModulationEngine` among the six members
   it **forbids** (`seraphis_voice.h:196-203`). See **AR-2**.
3. **`AtmosphereEngine` has no reverse playback and no event-triggered scheduling.** Roadmap line 114
   asks Phase 10's ghost tap for "reverse playback per grain, event-triggered (not
   continuous-density) scheduling, darker blur defaults". The shipped control surface has
   `setDensity` documented "Trigger density in grains/second. Range [0.1, 20], default 4.0"
   (`atmosphere_engine.h:821-828`) and no reverse control anywhere in the header (a full-header grep
   for `reverse` returns only unrelated text). Phase 10 delivers the **ghost configuration** it can
   deliver without changing a Seraphis-shipped component — see **AR-3** and FR-017.
4. **The dark material table is an append-only change to a shared header whose consumers are wired
   to `kNumMaterials` in more places than an assertion sweep finds.**
   `ContinuousBody::BodyMaterial` is a five-value enum (`continuous_body.h:81`), `kNumMaterials` is
   `5` (`:84`) and `kMaterialProfiles` is a
   `static constexpr std::array<MaterialProfile, kNumMaterials>` indexed by the enumerator (`:729`).
   Three Seraphis-owned test sites **assert** the count — `continuous_body_perf_test.cpp:283`
   (`static_assert`), `continuous_body_test.cpp:1082` (`REQUIRE`) and `seraphis_perf_test.cpp:419`
   (`static_assert`) — **but four more sites are sized by it and would not break, they would rot**:
   `continuous_body_perf_test.cpp:352` (`kMaterials`) and `:360` (`kMaterialNames`),
   `seraphis_perf_test.cpp:571` (`kMaterials`) and `:579` (`kMaterialNames`). All four are
   `constexpr std::array<…, ContinuousBody::kNumMaterials>` written with **exactly five
   initialisers**; raising `kNumMaterials` to `11` still compiles, because aggregate initialisation
   zero-fills the remainder. The result is six `BodyMaterial{0}` (= `Glass`) entries silently
   surveyed at `seraphis_perf_test.cpp:662` and six **null** `const char*` entries streamed at
   `:1196` (`os << kMaterialNames[i]`) — undefined behaviour, not a build break. The count also
   drives **loop bounds and measured workloads**, not only names: `seraphis_perf_test.cpp:662`
   surveys every material and `:1205` feeds the argmax to Seraphis's own SC-001/SC-002 subject, and
   `continuous_body_perf_test.cpp:881-897` gates every material against Seraphis-owned baselines
   that are already at their ceiling. See **AR-4**, FR-038, FR-038a and FR-038b.

The fifth fact is arithmetic, and it is the single largest risk in this phase: **the roadmap's own
success criterion — "8 voices everything-on ≤ 30 % of one core @ 48 kHz" (line 470) — is not
reachable with the measured costs of the parts it asks the voice to contain.** The projection is in
FR-081 and drives **Open Question 1**.

---

## Clarifications

### Session 2026-09-17

- **Q1 — `VoragoMacroTarget` roster and the two non-float mappings.** Enumerate the roster at plan
  stage from FR-068's six mappings; every row is float-valued on a shipped setter. `Weight`'s body
  clause is the two-body blend `b` (FR-036) + `ContinuousBody::setDamping`. "Distance filtering" is
  `CavernVerb::setDarkness`/`setFog` (Cavern rows, `VoragoCavernTargets`) +
  `HarmonicCloud::setSpectralTiltDb` (voice row). No discrete-target rows, no new DSP. [FR-068,
  SC-008]
- **Q2 — Many-to-one ecosystem reduction and per-event slot draw.** Where several agents of one kind
  address the same destination slot, route only the strongest agent (argmax by
  `EcosystemEngine::getAgentEnergy`); a per-step scan is accepted cost. A slow event's slot within a
  multi-slot family is chosen by a seeded per-event draw, deterministic under FR-025. FR-023's
  maximum rule remains the scheduler-vs-ecosystem combine. [FR-020a, FR-022, SC-019b, SC-020a]
- **Q3 — What the voice hands `HarmonicCloud::setSpectralTarget`, and whether it supplies one while
  the bloom is inert.** While the bloom owns at least one live child, the voice supplies
  `ratios[i] = i + 1` exactly (no gravity, no inharmonicity) and `amplitudes[i] = n^{-p(r)}`,
  `p(r) = 3.0 - 2.5r` (`harmonic_cloud.h:200`), no tilt, count = `getActivePartialCount()` —
  duplicating the cloud's published laws so neutrality is exact at every setting. Whenever the bloom
  owns no live child, the voice calls `clearSpectralTarget()` instead so the richness rolloff stays
  live. The spawn/retire edge must be click-free. [FR-011, SC-018a]
- **Q4 — Which component the `Gravity` macro drives, and which `AnchorMode` ships.**
  `ResonanceDriftNetwork` ships in `AnchorMode::Hybrid`. The `Gravity` macro (bipolar, 0 = air,
  1 = stone) and FR-026's `BreathingModulator` lane both drive `ResonanceDriftNetwork::setGravity`,
  base 0. `HarmonicCloud::setSpectralGravity` is **not** a `Gravity`-macro target. FR-061's semantics
  and SC-008's `Gravity` metric stand as written. [FR-016, FR-026, FR-061, SC-008]
- **Q5 — The `Standard` envelope's shipped stage times, reconciled with short test windows.** Pin the
  drone defaults: attack 20 s, stages 30–60 s, release 45 s. Every short-window criterion (SC-021a's
  1 s non-silence check, SC-008's 10–60 s window, SC-022 (2)'s onset measurement) uses one named
  fast-attack fixture (attack ≤ 100 ms), defined once and cited by each; SC-004b's `T_settle` uses
  the real (slow) numbers, never the fixture — the shipped character is not bent to fit a test
  window. [FR-014, FR-014a, SC-021a, SC-008, SC-022, SC-004b]
- **Q6 — `kTailSilenceThreshold`, `kQuiescentChunksToRetire`, `kSilenceRampMs`, and their relation to
  the amnesty threshold.** `kTailSilenceThreshold = -90` dBFS, `kQuiescentChunksToRetire` = the
  chunk count for ≈10 s (a sample-rate-derived count, justified in the header),
  `kSilenceRampMs = 1.0` (Seraphis's ramp). Both retirement constants sit far below the -30 dBFS
  amnesty line, so SC-012's victim table stays non-degenerate; stealing is the normal allocation
  path and SC-011 gates it. [FR-013, SC-011, SC-012]
- **Q7 — How many `SlowEventScheduler`s a voice owns, and their depth/polarity.**
  `kNumEventSchedulers = 2`: a fast scheduler (~20–90 s) and a slow one (~3–10 min), both
  `setBipolarProbability(0)` and `setDepthRange(0.4, 1.0)`. Every event is a positive wake; sleep
  events are out of scope for this phase. [FR-022, SC-019, SC-020]
- **Q8 — The noise organism's mono → stereo rule.** A fixed 2nd-order all-pass pair, different
  coefficients per channel (two biquads per voice, named `constexpr` coefficients), for a flat mono
  magnitude response — the fixed, allocation-free rule FR-015 requires. [FR-015]
- **OQ1 (roadmap Open Question 5 / spec OQ-1) — ghost/atmosphere and ecosystem ownership; polyphony
  narrowed.** Ghost/atmosphere is **global** — one `AtmosphereEngine` owned by `VoragoEngine`, fed
  from the voice sum, event-gated at engine level (saves 113 298 ns/voice; L-1 applied). The
  ecosystem stays **per-voice** (voices diverge; roadmap line 390's agent counts stand; L-2
  rejected). Shipped polyphony is **not** ruled here: the plan runs the FR-082 stage-cost probe
  first, then presents the measured per-voice and global table with a recommended shipped polyphony
  and `kMaxVoices` (ceiling 8; ruled 6 at Q-H) as a plan-stage ruling for the user. The 30 % ceiling,
  `kRegressionFactor` 1.5 and the ban on relaxing thresholds or shrinking workloads all stand.
  [FR-002, FR-041, FR-056, FR-020b, FR-081, SC-001a]
- **OQ2 (roadmap Open Question 6 / spec OQ-2) — macro roster.** Confirmed at twelve: `Darkness, Age,
  Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass`. Folds:
  `Decay` → `Age` + `Depth`, `Instability` → `Entropy`, `Distance` → `Fog`. `kRows` is authored
  against this roster. [FR-061, FR-068, SC-008]
- **OQ3 (Assumption 5's test-placement question — distinct from spec `OQ-3`, which remains open) —
  where the composed engine+`CavernVerb` chain case is registered.** It lives in its own TU
  registered with `dsp_effects_tests`, beside `CavernVerb`'s own tests, compiling under that
  target's `KRATE_DSP_AETHER_TEST_HOOKS` define consistently with every other TU in that
  executable. Every other Phase 10 TU registers with `dsp_systems_tests` and includes no Layer 4
  header. [FR-084, FR-084a]
- **Q-B (plan-stage; spec OQ-3, roadmap line 114 + line 572) — does Phase 10 extend `AtmosphereEngine`
  with reverse grains and event-triggered grain scheduling?** No. FR-017's ghost **configuration**
  ships unconditionally in this phase; the two source-level behaviours are owned by a new roadmap
  phase, **Phase 10a — AtmosphereEngine ghost extension**, sequenced after this phase's polyphony
  ruling and before Phase 14's presets. This phase carries one shared-component source change
  (`ContinuousBody`, AR-4), not two. [FR-017, SC-027]
- **Q-C (plan-stage) — FR-087's non-finite probe shape.** A `detail`-namespace friend struct
  forward-declared in `vorago_engine.h` and defined in the test TU, in the shipped
  `SeraphisEngineNonFiniteProbe` shape; no `KRATE_DSP_VORAGO_TEST_HOOKS` compile definition, because
  the class definition does not change. FR-087's wording is amended at T002. [FR-087, SC-021]
- **Q-D (plan-stage) — what parent count the voice hands `BloomEngine::processChunk`, and when
  FR-011's `count` equals `getActivePartialCount()`.** The count handed to
  `BloomEngine::processChunk` is `reserveBase()` (`bloom_engine.h:710`), **never**
  `getActivePartialCount()` — handing the active count would overrun the component's reserved
  region and destroy `getOverlapEngagementCount()`'s meaning as a real signal. Because the voice
  keeps `setCapacity` at `clamp(getActivePartialCount(), kMinCloudCapacity, kMaxPartials)`,
  `count == getActivePartialCount()` holds whenever richness keeps the active count at or above
  `kMinCloudCapacity = 14`. FR-011/FR-012's wording is amended at T002. [FR-011, FR-012, SC-018]
- **Q-E (build-stage, T012) — FR-015's mono-sum clauses.** The clauses as written (mono-fold
  deviation ≤ 3.0 dB, and ≥ 6 dB better than a **one-sample**-delay pair) are unsatisfiable by
  construction: the fold is `2|cos(Δφ/2)|`, a unit delay reaches only 60° at 8 kHz and so barely
  folds (measured 1.25 dB), and even a perfect quadrature network folds a constant 3.01 dB.
  Measured on the shipped pair: 3.66 dB at 8 kHz; adding sections makes it worse (4.48 / 4.68 dB).
  Ruled 2026-09-18: **keep the code**; the bound becomes **≤ 4.0 dB** over [20 Hz, 8 kHz], and the
  comparator becomes the **1 ms fractional-delay pair** FR-015's prose names (measured 19.19 dB at
  465 Hz on the test's bin grid), at least 6 dB better. Per-channel flatness ≤ 0.01 dB stands.
  [FR-015, SC-021]
- **Q-F (build-stage, T009) — `kNumMaterials` sites outside FR-038's list.** Four more exist:
  `continuous_body_spectral_test.cpp` (three count-sized arrays, silent zero-fill), and in the
  **shipped Seraphis plugin** `dropdown_mappings.h` (the ID 800 label list and index clamp) and
  `param_perf_test.cpp` (a `static_assert` plus three count-sized arrays and two loops). Ruled
  2026-09-18: Seraphis's Body Material dropdown **stays at five**; every one of those sites is
  re-pointed at `kNumSeraphisMaterials` (FR-038a). ID 800's step count and normalised mapping are
  host-visible and do not move. FR-038's table gains rows 8–11; SC-016 gains clause 5. [FR-038,
  SC-016]
- **Q-G (build-stage, T010) — the envelope components' ceilings.** `MultiStageEnvelope` clamps
  every stage and the release to `kMaxStageTimeMs = 10 000 ms` and `GrowthEnvelope` clamps its
  duration to `kMaxDuration = 60 s`; FR-014's 20 s / 30–60 s / 45 s shape and FR-090's 120 s growth
  ran clamped while the voice's getters reported the authored numbers. Seraphis derives parameter
  ranges from both constants, so widening them is host-visible. Ruled 2026-09-18: **append-only
  per-instance ceiling setters** — `MultiStageEnvelope::setMaxStageTimeMs` (plus `getStageTime` /
  `getReleaseTime` / `getMaxStageTimeMs`) and `GrowthEnvelope::setMaxDuration` / `getMaxDuration` —
  defaulting to the shipped constants. `VoragoVoice::prepare()` raises them to
  `kEnvelopeMaxStageTimeMs = 120 000 ms` and `kGrowthMaxDurationSeconds = 120 s` before authoring
  a stage; `envelope().getStageTime(st)` now reads back FR-014's table. Two more shared-component
  changes under SC-016's expected-deletion bar. [FR-014, FR-090, SC-016]

### Session 2026-09-19 (build stage, after the first full [long] and isolated perf runs)

- **Q-H (Q-A / OQ-1(a)) — shipped polyphony and `kMaxVoices`.** Measured P-core-pinned and alone:
  V = 506 961 ns per voice, L = 12 692 per idle slot, G = 404 518 global, CavernVerb 124 497.
  Polyphony 4 everything-on = 2 704 220 ns/block = 25.4 % of one core, inside roadmap line 470's
  30 % ceiling; it fails only SC-001b's clause (ii) (baseline ≤ ceiling/1.5). Ruled: **shipped
  polyphony 4, `kMaxVoices = 6`** (roadmap line 460's "4–8" becomes "4–6"), no voicing levers
  (L-3 refused, L-4 held in reserve), and SC-001b clause (ii) **reformulated**: the gate is
  measured ≤ `kReferenceNs` AND measured ≤ checked-in baseline × `kRegressionFactor`; the baseline
  is ceil(measured × 1.05) and may sit above `kMaxAdmissibleNs`, which is recorded as the
  regression headroom actually available (the ceiling binds first). The baseline is transcribed
  from a re-measurement taken after a restart and cool-down, not from the warm-machine survey.
  **Transcribed 2026-09-21** (P-core-pinned, `[.perf]` cases only, run alone after a restart and a
  15-minute idle): engine at polyphony 4 = 2 566 170 ns/block (2 690 670 with the Cavern term,
  84.1 % of the reference; the warm survey's 2 579 723 agrees within 0.6 %). Checked-in baseline
  `kEngineBaselineNsAtPoly4 = 2 694 479` (engine term only; the Cavern term is the Phase 9
  constant). Available regression headroom before the ceiling trips: 1.141× of the baseline. A
  run of the same lane started two minutes after a 25-minute test lane, with the survey placed
  after three accelerated soaks, read 2 757 260 (+7.4 %) and was not transcribed — the protocol's
  "after a restart and cool-down" is load-bearing.
  [FR-041, FR-081, SC-001a, SC-001b]
- **Q-I — SC-004b settle-window tolerance.** The 10 s moving-average monotone clause carries a
  stated tolerance of **0.1 dB** (2.28 % in mean-square). The test's 1e-9 epsilon failed on a
  single 0.25 % (0.011 dB) dip during the attack of a chaotic drone; the clause's intent is a rise,
  not a collapse, and 0.1 dB still fails any real sag. [SC-004b]
- **Q-J — SC-005 centroid CV shortfall (0.031 vs 0.05 on all three seeds).** Ruled: probe the
  shipped travel levers with accelerated renders, raise FR-090's defaults where a lever moves the
  CV, and re-soak once. Recorded below when the probe lands. [SC-005, FR-090]
- **Q-K — SC-016 clause 3.** The pre-change survey (T001) was captured on a heat-soaked machine
  with ten unrelated timing reds; every Seraphis figure read 8–27 % higher than the isolated
  post-change run and the argmax (Chamber vs Strings) is inside a 3.3 % gap. Ruled: re-capture
  **both** surveys in one thermal state (the pre-change tree from a worktree at `ad7481f6`, then
  the post-change tree, each alone), and clause 3 becomes **noise-robust**: the argmax stays within
  the five Seraphis materials AND no Seraphis material's figure moves by more than **10 %** between
  the pair. [SC-016]
- **Q-L — B-7's 10× clearing-path ratio.** `silenceAudio()` wipes the same delay memory
  `FeedbackEcology::reset()` does, so the RT-safe paths differ from `reset()` by one small call
  (measured 23.4 µs vs 37.3 µs at 48 kHz). The ratio clause rested on a false premise and is
  **dropped**; each RT-safe path must be no dearer than `reset()` and must fit one 64-sample control
  chunk at both rates (the absolute clause, which passed by 50×). [FR-013, plan B-7]
- **Q-M — SC-026 (a)/(b) construction.** As literally written they contradict FR-046/SC-030 (an
  idle slot's identity layer keeps advancing so a re-triggered voice never starts cold). Ruled:
  (a) and (b) rewind with `VoragoEngine::reset()` before the replay, which restores run state
  without re-deriving slot seeds — so a note cycle that consumed or advanced a slot seed would
  replay differently, which is what the criterion tests. The stale "noteOn re-derives run state"
  sentences in `vorago_engine.h` and plan.md are corrected. [SC-026, FR-046, FR-048]
- **Q-N — SC-008 rows after the first sweep.** Life's `EventRateScale` row carried an inverted
  sign (fixed: the voice divides the interval ranges by the scale); Movement's amounts were
  retuned to the component ceilings; Age, Entropy and Life gained measurement constructions that
  can see them (a C4 note for Age, a harmonic-band flatness for Entropy, a 210 s render with the
  edge count over [90 s, 210 s] for Life). Ruled for the rows still short: **Darkness, Mass and
  Fog** get metrics that can see the concept on this instrument (energy ratio above/below 4·f0;
  energy below 80 Hz plus readbacks; harmonic-band flatness plus readbacks) with thresholds
  derived from a probe and recorded; **Gravity and Pressure** are recorded as **failed**, thresholds
  untouched (the keyed ratio table caps Gravity's air/stone endpoint at 24 % in theory, 18 %
  measured, against 30 %; saturation moves the crest of a sub-plus-fundamental beat by 0.4 dB
  against 3 dB) — a Phase 12 product decision, never a threshold move. [SC-008]
- **Q-O — SC-008 Depth (composed).** The wet/dry RMS share cannot see decay time on this drone:
  its energy sits at 16–65 Hz, below the cavern FDN's DC-blocker corner, so decay is invisible even
  at the 60 s clamp and a larger size lowers steady-state energy. Ruled: the Depth metric becomes
  the **post-note-off tail** (time for the composed output to fall 30 dB after note-off), which
  `CavernDecaySeconds` moves directly; monotone positive with an endpoint derived from a probe.
  Amends A-1. [SC-008]
- **Q-Q — the post-note-off sub floor.** Measuring Depth's tail exposed a −20 dB floor that never
  decays: FR-090 shipped sub tracking at 0.60, leaving 40 % of the sub level static and independent
  of the voice envelope (the component's own default is 1.0). Ruled: **sub tracking ships at 1.0**
  (the subs follow the voice-sum envelope and decay with the tail), and the Mass → SubTrackingAmount
  row becomes a claim row (amount 0). With the subs fully tracked no Mass row can raise the < 80 Hz
  band (measured −0.35 dB endpoint, rho −1.0), so **Mass is recorded as failed** alongside Gravity
  and Pressure. [FR-090, SC-008]
- **Q-R — SC-005 levers (closing Q-J).** Probed on an exact harness of the test's statistic:
  drift depth, breathing, tidal, wander rates, bloom, smear, blur and ghost move the full-band
  centroid CV by ≤ 0.01; the centroid sits at 47 Hz, pinned by the subs and fundamental exactly as
  SC-008's Darkness row was. Ecosystem depth 0.50 → **0.85** is the largest admissible lever (1.0
  pins the cloud mutation clamp and breaks Entropy's folded clause) and measures CV 0.0509 / 0.0490 /
  0.0499 unaccelerated over 8 h — a coin flip against 0.05. Ruled: FR-090 ships ecosystem depth
  0.85, and **SC-005's centroid is computed on the band above 4·f0** (sub-immune, the Darkness
  construction); the 0.05 bar is unchanged. Measured on the same harness after the change, 8 h
  unaccelerated at depth 0.85: CV **0.168 / 0.165 / 0.187**, worst autocorrelation 0.36 at lag 2
  (60 s); at the old depth 0.50, seed 1 reads 0.122. [SC-005, FR-090]
- **Q-P — SC-015 materials.** All six were separated by alpha alone (minimum pair separation 0.08,
  all below Glass 899.6 Hz); no material dropped. Measured under SC-015's construction: raising
  `b1` **brightens** the centroid, so the FR-035 note's "alpha and b1" lever is corrected to alpha.
  [FR-035, SC-015]

### Session 2026-09-21 (build stage, after the restart and the cooled re-measurement)

- **Q-S — SC-016 clause 3's measuring TU (closing Q-K).** Q-K's paired capture was taken seven
  times, in both orders (pre-first ×4, post-first ×3), each survey alone and P-core-pinned. The
  `seraphis_perf_test.cpp` material survey (best-of-16 × 200 blocks) scatters ±20 % between
  identical-code runs of the **same** tree, and the untouched pre-change tree failed its own
  composition-ratio gate in two of three runs (1.21, 1.29 against 1.10) — so its per-material
  medians (Glass −4 %, Strings +2 %, MetalPlate +16 %, Chamber +15 %, Ice +17 %) measure the
  machine, not the header. `ContinuousBody_CpuBudget` measures the same standalone body at the
  same five materials with best-of-25 × 500 in three configurations; its paired tables read **fourteen of
  fifteen** Seraphis figures within ±3 % between the trees on the cleanest pair (steady / operating /
  crossfade, post-first, `cbpair7`); the fifteenth is MetalPlate’s operating row, 49 459 pre vs 39 671
  post (−19.8 %), where the **pre** side is the outlier — the same row reads 38 390 and 38 936 in the
  other two pre captures, and the post side matches them — and a fourth pair showed a uniform +25 % step on **every** material in
  one table and none in the next — a load burst, not a material. Seraphis's 8-voice headline is
  within 1.5 % across all pairs and the argmax stayed within the five in every capture. Ruled:
  clause 3's per-material 10 % comparison is taken from **`ContinuousBody_CpuBudget`'s paired
  tables**, the survey's medians and the 8-voice headline are reported alongside, and the argmax
  clause is unchanged. No threshold moved. [SC-016]

### Session 2026-09-22 (build stage, after the workflow’s compliance pass)

- **Q-T — FR-006’s literal total and the widened header set.** The workflow’s remediation agent closed
  FR-006 (“`getAllocatedBytes()` reports the prepare-time total”) by appending `const noexcept`
  `getAllocatedBytes()` getters down the chain the voice’s two `ContinuousBody` instances own:
  `primitives/delay_line.h` (the only true allocator, `buffer_.capacity() * sizeof(float)`),
  `primitives/comb_filter.h`, `processors/diffusion_network.h` (stage and network),
  `processors/waveguide_string.h`, `systems/timevar_comb_bank.h` and `systems/continuous_body.h`, with
  the voice sum at `vorago_voice.h` including both bodies (engine total 7 974 936 → 10 924 056 bytes,
  still invariant across render / noteOff / silence / steal / recovery / reset,
  `VoragoVoice_AllocationAccounting`). That is **five shared headers outside the phase’s enumerated
  set**, one of them Layer 1 and consumed by every plugin — a scope widening the phase did not
  announce. Verified before the ruling: every edit append-only (0 deletions; `continuous_body.h` still
  exactly the two FR-039 deletions, `check-seraphis-green` in scope), none on an audio path, the
  core / primitives / processors / shared / seraphis / systems / effects per-push suites green,
  clang-tidy 0 / 0, lint-layers and lint-odr clean. Ruled: **accepted**; the widened set is named here
  and in the commit. The same pass also amended FR-069 (a non-finite macro write keeps the previous
  value), FR-021 (routing depth per destination, `setEcosystemDepthFor`), FR-083/FR-086/FR-068 text,
  and roadmap line 470’s “8 voices” wording to the Q-H ruling — each under the spec’s own FR text,
  no threshold moved. [FR-006, FR-021, FR-069, SC-016]

Every question the pre-clarification scan raised is answered above; downstream stages (plan, tasks,
comply) read the FR/SC updates this session made, not this log.

---

## Scope

In scope, and nothing else:

1. **`VoragoVoice`** (L3) — the per-voice composition of roadmap line 453–456: harmonic cloud +
   noise organism → resonance drift network → feedback ecology → two-body acoustic blend →
   ghost/atmosphere tap, with the bloom lifecycle and the voice envelope, plus the per-voice
   identity layer (ecosystem agents + slow-event schedulers) that drives the sleep/wake and trigger
   surfaces phases 2, 3, 5, 7 and 8 already expose.
2. **The dark material data** (roadmap lines 457–459) — six materials appended to `ContinuousBody`
   as mode-ratio tables and damping laws, i.e. **data authoring, not new DSP**, plus the two-body
   blend control that consumes them.
3. **`VoragoEngine`** (L3) — polyphony via `VoiceAllocator` with quietest-steal and long-release
   amnesty, per-voice unique seeds, the voice sum, the global post-sum chain that Layer 3 may own
   (subharmonic engine → spectral smear), the documented Layer-4 seam for `CavernVerb`, and the
   output stage (`TapeSaturator` at low drive + `TruePeakLimiter`).
4. **`VoragoMacroMatrix`** (L3) — the concept macro system of roadmap lines 463–468, as a
   `constexpr` row table over named targets, in the `SeraphisMacroMatrix` shape.
5. **The four Phase-10 success criteria of roadmap lines 470–473** — full-poly CPU, the 8 h soak,
   macro-sweep render verification, and the determinism harness through
   `tests/test_helpers/render_fingerprint.h`.

---

## Non-Goals (owned by later phases, or deliberately excluded)

- **No plugin work of any kind** — no `plugins/vorago/`, no parameter IDs, no `plugin_ids.h`, no
  `editor.uidesc`, no FUIDs, no AU subtype. Phase 11 owns all of it (roadmap lines 482–500). This
  spec therefore registers **no** `k{Section}{Parameter}Id`; the naming rule at roadmap line 571
  binds Phase 12, not this one.
- **No new DSP algorithm.** Every generator, filter, resonator, delay, grain, agent and reverb this
  phase renders is already shipped and already unit-tested in its own phase. A requirement below
  that reads like new signal processing is a defect in this spec.
- **No re-litigation of phases 1–9.** Their budgets, defaults, dormancy semantics and amended
  success criteria stand as shipped. Phase 10 measures the *composition*, and where it finds a
  component's shipped default unsuitable for a drone it changes the **setting**, never the
  component.
- **No `CavernVerb` ownership.** The engine does not include `effects/cavern_verb.h` (layer
  discipline, AR-1). The composed instrument — engine + cavern — is exercised in **one dedicated TU
  registered with `dsp_effects_tests`** (FR-084a), not `dsp_systems_tests` — the only Phase 10 TU
  that names Layer 4, exactly as Seraphis's composed tests do.
- **Sleep events (negative-polarity `SlowEventScheduler` output) are out of scope for this phase**
  (Q7). `kNumEventSchedulers = 2` (FR-022) ship at `setBipolarProbability(0)`: every event is a
  positive wake.
- **`AtmosphereEngine` reverse grains and event-triggered grain scheduling are *owned by Phase 10a*,
  not excluded.** FR-017's ghost **configuration** ships unconditionally. The two behaviours
  roadmap line 114 additionally names were **OQ-3**, ruled 2026-09-17 (Clarifications, Q-B): a new
  roadmap phase, Phase 10a — AtmosphereEngine ghost extension, extends the component append-only
  on the AR-4 model, after this phase's polyphony ruling and before Phase 14.
- **No macro persistence, no preset format, no state versioning.** Phase 12 (`vorago-phase12-parameters`)
  and Phase 14 (`vorago-phase14-presets-release`) own those.
- **No MPE / channel-pressure mapping** (roadmap Open Question 7 — explicitly a Phase 11/12 scope
  call, line 591).
- **No UI, no ecosystem visualization, no DataExchange** (roadmap Phase 13, lines 510–518).

---

## Architecture rulings

These are determinations, not open questions: the roadmap does not defer them, and the shipped code
decides them.

### AR-1 — The cavern space sits **outside** `VoragoEngine`, at a documented seam

`VoragoEngine` is Layer 3 and may not name a Layer 4 type. It therefore exposes **two** render
entry points and the caller runs `CavernVerb` between them:

```
engine.processStereoBlock(l, r, n);   // voices -> sum -> SubharmonicEngine -> SpectralSmear
cavern.processStereoBlock(l, r, l, r, n);   // Layer 4, owned by the CALLER
engine.processOutputStage(l, r, n);   // TapeSaturator (low drive) -> TruePeakLimiter
```

This is `SeraphisEngine`'s shipped contract verbatim (`seraphis_engine.h:608-624`, whose
`processOutputStage` doxygen names the AetherReverb return as the intended input buffer), and it is
the only construction that satisfies both roadmap line 461's ordering and roadmap line 558's layer
discipline. `VoragoMacroMatrix` mirrors `SeraphisMacroMatrix`'s answer to the same problem for its
cavern-owned rows: they are **computed and returned as plain floats** in a POD
(`SeraphisAetherTargets`, `seraphis_macro_matrix.h:123-133`), never written by `apply()`.

### AR-2 — Identity-layer routing is explicit, not `ModulationEngine`

`EcosystemEngine` and `SlowEventScheduler` outputs are read directly by `VoragoVoice` at its control
step and pushed into the shipped sleep/wake and depth setters of phases 2, 3, 5 and 7. No
`ModulationEngine`, no `VoiceModRouter`, no `ModulationSource` virtual dispatch on the audio thread.
Fact basis: `EcosystemEngine` has no base class (`ecosystem_engine.h:143`) and publishes
`getAgentOutput` (`:886`); `SeraphisVoice` forbids `VoiceModRouter`/`ModulationEngine` as members by
size guard (`seraphis_voice.h:196-203`). `SlowEventScheduler` **is** a `ModulationSource`
(`slow_event_scheduler.h:143`), but is read through its concrete type
(`getCurrentValue`, `getActiveTarget` `:356`, `getEventPhase` `:365`), so no virtual call occurs.

### AR-3 — The ghost tap is a **configuration**; the two unshipped behaviours are OQ-3, not a Non-Goal

Roadmap line 114 is a 🔶 row: "*Add* ghost-flavoured config: reverse playback per grain,
event-triggered (not continuous-density) scheduling, darker blur defaults." Two of those three are
not configuration of the shipped component — they are new behaviour in a Seraphis-shipped Layer 3
engine (fact 3 above). Phase 10 **specifies in full** the third plus everything the surface *does*
support: low density, long grains, downward pitch offset, wide position spread, high blur, high
decorrelation, and **event-gated level** (the slow-event scheduler drives `setLevel`, which produces
"bursts of ghosts" from a continuous-density scheduler without touching it). FR-017 pins the
configuration numerically and SC-027 verifies it.

What this ruling does **not** do is delete the other two. The roadmap asks for them in two separate
places — the reuse-inventory row (line 114) and the cross-cutting list, which names
"AtmosphereEngine ghost config" among the **shared-component changes** that must keep Seraphis's
tests green (line 572), i.e. it anticipates a *source* change. This spec accepts a structurally
identical shared-component change for `ContinuousBody` (AR-4), so excluding this one unilaterally
would be an inconsistent call, and dropping a roadmap deliverable is the user's ruling to take, not
the spec's. It is therefore **OQ-3**, with the cost, the existing `primitives/reverse_buffer.h`
substrate and the Seraphis-green obligation stated there. Until OQ-3 is ruled, FR-017 is what
ships.

### AR-4 — Dark materials are appended to `ContinuousBody`, and every count-sized consumer is updated

The only public way to select a body voicing is `setMaterial(BodyMaterial)`
(`continuous_body.h:1122`); the profile table is `private`-adjacent class data (`:729`) with no
public "install a profile" surface. Adding materials therefore means appending to the enum, raising
`kNumMaterials`, appending rows to `kMaterialProfiles` and adding ratio tables for the modal ones.
The change is **append-only**: `Glass = 0, Strings, MetalPlate, Chamber, Ice` keep their values, so
no stored Seraphis material index moves.

The three `static_assert`/`REQUIRE` sites that pin `kNumMaterials == 5` exist precisely to force
this conversation — but they are **not sufficient**, because four more sites are *sized* by the
count rather than asserting it and would zero-fill silently (fact 4 above). FR-038 enumerates all
seven sites; SC-016 gates the result.

Two further determinations follow, and they are what make the change safe rather than merely
compiling:

- **The Seraphis perf survey stays a five-material survey.** `seraphis_perf_test.cpp` does not only
  assert the count: `surveyMaterials()` loops every material (`:662`) and
  `const ContinuousBody::BodyMaterial worstMaterial = kMaterials[survey.worstIndex];` (`:1205`)
  feeds `buildChainSubject` / `measureVoiceSubject` / `measureBodySubject`. The `static_assert`
  message at `:419` says it outright: *"SC-001 measures all five materials and uses the worst"*.
  If the survey widened to eleven, Seraphis's **shipped** SC-001/SC-002 subject would be chosen
  from a set containing Vorago materials, and a single Vorago material costlier than `MetalPlate`
  would re-point Seraphis's checked-in baselines at a workload they were never set for — with
  SC-016 simultaneously forbidding those baselines to move. That is unresolvable, so it is ruled
  out by construction: `ContinuousBody` gains
  `static constexpr std::size_t kNumSeraphisMaterials = 5;` (an appended line, FR-039 intact) and
  `seraphis_perf_test.cpp` iterates that **prefix**. FR-038a.
- **The six new materials are budget-gated, on Vorago's account, not Seraphis's.**
  `continuous_body_perf_test.cpp` asserts its four per-configuration budgets over **every** material
  index (`:881-897`), and two of those baselines are already **capped rather than
  measurement-pinned** — the file records "operating 41.1 → 41,200, which exceeds
  `kMaxAdmissibleHalfPctNs` by 15.7 %", capped at 35,500 (`:169-180`, `:222-229`). Appending six
  materials therefore silently subjects them to Seraphis-owned ceilings with no stated lever. A
  32-mode `CathedralColumn` or `CavernWall` may well cost more than `MetalPlate`. FR-038b makes
  that a Vorago requirement with a Vorago lever (re-voice or drop the material), never a baseline
  move.

**A-13 — this phase makes *two* append-only shared-component changes, not one.** AR-4 is the first.
The second is `FeedbackEcology`, which gains exactly one appended public method,
`void silenceAudio() noexcept` — the RT-safe half of `reset()`, which the component's own comment
declares a control-thread call (`feedback_ecology.h:861-862`) and whose per-loop `delay.reset()`
`std::fill` the component itself quantifies at 131 072 B at 48 kHz and 524 288 B at 192 kHz, one
131 KB fill alone exceeding the control-chunk budget (`:2401-2412`). The voice's steal path and
FR-072's deferred non-finite recovery are audio-thread paths (plan B-7) and may not reach `reset()`.
The change adds **no** class, member, enumerator or constant and deletes **zero** lines, so
`tools/lint-odr.js` sees nothing new. **SC-016's Seraphis gate does not widen to cover it**:
`FeedbackEcology` has no Seraphis consumer (repo-wide sweep: only Vorago Layer 3 peers,
`dsp/lint_all_headers.cpp` and its own five test TUs include it), so its green gate is its own
`FeedbackEcology*` suites, not `seraphis_*`. Assumption 2 is amended to match.

### AR-5 — The voice envelope gates the **excitation**, not the voice output

Roadmap line 456 lists "voice envelope" last in the voice's arrow chain. Applying a gate there would
cut the resonance network's, the ecology's and the body's tails on `noteOff` — which is precisely
what a drone instrument must not do, and which is why Seraphis states the opposite rule normatively:
the envelope multiplies the cloud in place and "**Nothing downstream of this point is gated**"
(`seraphis_voice.h:1066`). Phase 10 adopts the Seraphis rule: the envelope gates the
cloud+noise excitation bus, everything downstream rings out, and the voice retires on a level
detector (FR-013). Roadmap line 456's ordering is read as an enumeration of the voice's parts, not
as a signal-order mandate.

### AR-6 — The macro system is a `constexpr` row table, **not** "`ModulationEngine` presets"

Roadmap line 463 writes the concept macro system as "(via `ModulationEngine` presets)". This spec
implements it as `VoragoMacroMatrix`'s `static constexpr std::array<VoragoMacroRow, kNumRows> kRows`
(FR-060) and names `ModulationEngine` nowhere. **This is a deviation from a roadmap line, and it is
recorded here rather than buried in a Traceability row**, because the roadmap is not silent on the
point and the "Decisions taken where the roadmap is silent" list therefore cannot carry it.

Two reasons, both from shipped code read this session:

1. **Precedent.** Seraphis solved the identical problem with a data table —
   `struct SeraphisMacroRow` (`seraphis_macro_matrix.h:159`), `kRows` (`:213`), five `constexpr`
   predicates asserting the table's invariants below the class (`:600-685`). That is the pattern
   template roadmap lines 140–142 name for this phase, and it is the one that makes FR-064's
   shared-base invariant and FR-065's curve restriction **compile-time** facts.
2. **Dispatch.** AR-2's argument applies here too: a `ModulationEngine` route is virtual dispatch
   per destination per control step, and `SeraphisVoice`'s size guard **forbids** `ModulationEngine`
   and `VoiceModRouter` as members (`seraphis_voice.h:196-203`). A macro table that wrote through
   one would reintroduce exactly what the template excludes.

Nothing roadmap line 463 asks for behaviourally is lost: it asks for "a documented multi-target
mapping" per concept, which is what `kRows` **is**, in a form a `constexpr` predicate can check.

---

## Existing components (verified this session)

Every signature below was read in the header at the cited line this session and is quoted verbatim.

| Component | Header (`dsp/include/krate/dsp/…`) | What Phase 10 reuses — verified signature |
|---|---|---|
| `HarmonicCloud` | `systems/harmonic_cloud.h:127` | `void setSpectralTarget(const float* ratios, const float* amplitudes, std::size_t count) noexcept` (`:769`), `void processStereoBlock(float* leftOutput, float* rightOutput, std::size_t numSamples) noexcept` (`:878`), `setRichness` (`:412`), `setSpectralGravity` (`:478`), `setSpectralTiltDb` (`:439`), `setInharmonicity` (`:426`), `setMutation` (`:452`), `setDriftDepthCents` (`:501`), `getActivePartialCount` (`:950`), `kMaxPartials = 64` (`:138`). `HarmonicCloud`'s own cadence note (`:736-753`, its spec's FR-086) **requires** targets be supplied in slices ≤ 64 samples. |
| `NoiseOrganism` | `systems/noise_organism.h:136` | `void processBlock(float* output, std::size_t numSamples) noexcept` (`:414`) — **mono out**; `setNumSources` (`:451`), `setSourceModel` (`:463`), `setSourceLevel` (`:488`), `setSourceDormant` (`:833`), `setSourceWake(std::size_t slot, float amount)` (`:844`), `setWanderRate` (`:781`), `kMaxSources = 4` (`:142`). |
| `ResonanceDriftNetwork` | `systems/resonance_drift_network.h:121` | `void processBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept` (`:515`, inputs may alias outputs); `enum class AnchorMode { Free, Keyed, Hybrid }` (`:293`), `setNoteFrequency` (`:624`), `setGravity` (`:633`), `setPeakWake` (`:771`), `setPeakDormant` (`:784`), `setMix` (`:796`), `kMaxPeaks = 12` (`:128`). |
| `FeedbackEcology` | `systems/feedback_ecology.h:185` | `void processBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept` (`:914`) forwarding to `processBlockTapped(… , float* const* loopTaps, …)` (`:944`); `setCoupling(from,to,amount)` (`:1107`), `setLoopWake` (`:1261`), `setLoopDormant` (`:1271`), `setMix` (`:1282`), `kMaxLoops = 6` (`:193`), `kDefaultMix = 0.15f` (`:582`). |
| `BloomEngine` | `systems/bloom_engine.h:193` | `[[nodiscard]] std::size_t processChunk(float* ratios, float* amplitudes, std::size_t parentCount, std::size_t numSamples) noexcept` (`:494`) — **normative precondition: both arrays address ≥ `kMaxSlots` (64) writable floats** (`:466-481`); `setCapacity` (`:628`), `setConsumerTiltDb` (`:618`), `triggerBloom()` (`:663`), `setDepth` (`:532`), `reserveBase()` (`:710`). |
| `EcosystemEngine` | `systems/ecosystem_engine.h:143` | `void processChunk(std::size_t numSamples) noexcept` (`:431`); `[[nodiscard]] float getAgentOutput(std::size_t i) const noexcept` (`:886`, held between steps, `[0,1]`); `enum class Kind { Partial, Resonator, Noise, Feedback, Ghost }` (`:282`, **append-only, normative order**); `getAgentKind` (`:893`), `setAgentWake` (`:741`), `kMaxAgents = 48` (`:152`), `kDefaultStepIntervalChunks = 8` (`:196`). **No `ModulationSource` base.** |
| `SlowEventScheduler` | `processors/slow_event_scheduler.h:143` | `class SlowEventScheduler final : public ModulationSource`; `void processBlock(std::size_t numSamples) noexcept` (`:304`), `getCurrentValue()` (`:331`), `[[nodiscard]] std::uint8_t getActiveTarget() const noexcept` (`:356`), `getEventPhase()` (`:365`), `setIntervalRange` (`:238`), `setTargetCount` (`:271`), `setSeed` (`:229`). |
| `ContinuousBody` | `systems/continuous_body.h:71` | `void processStereoBlock(const float* inLeft, const float* inRight, …)` (`:1369`) — **not in place**; `setMaterial(BodyMaterial)` (`:1122`), `setNoteFrequencyHz` (`:1190`), `setResonance` (`:1161`), `setDamping` (`:1170`), `setMix` (`:1209`), `setCloudMix` (`:1219`), `setWidth` (`:1264`); `struct MaterialProfile` (`:653`), `kMaterialProfiles` (`:729`), `kNumMaterials = 5` (`:84`), `kNumSlots = 2` (`:90`, the crossfade slots — internal, unaffected by AR-4). Copy ctor deleted, no move members (per `seraphis_voice.h:158-163`). |
| `AtmosphereEngine` | `systems/atmosphere_engine.h:179` | `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, …)` (`:674`) — **wet texture only**; `setLevel` (`:982`), `setBlur` (`:910`), `setDensity` (`:828`, doc `:821`: grains/s, `[0.1, 20]`, default 4.0), `setGrainSeconds` (`:815`), `setPitchSemitones` (`:858`), `setPositionSpread` (`:851`), `setDecorrelation` (`:902`), `captureFreeze` (`:945`) / `releaseFreeze` (`:964`). **No reverse control; no event-trigger entry point.** |
| `SubharmonicEngine` | `systems/subharmonic_engine.h:155` | `void processBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept` (`:545`, in-place supported) and `processBlockTapped(…, float* subTap, …)` (`:554`); `setFundamentalHz` (`:616`), `setToneLevelDb` (`:638`), `setTrackingAmount` (`:674`), `setWetGainDb` (`:725`), `setSubToMainEnabled` (`:745`), `enum class Tone { Div2, Div4, FifthBelow }` (`:320`), `kNumTones = 3` (`:162`). |
| `SpectralSmear` | `processors/spectral_smear.h:76` | `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (`:341`) — **in place**; `setSmearAmount` (`:376`), `setDecoherence` (`:381`), `setSmearTilt` (`:386`), `[[nodiscard]] std::size_t getLatencySamples() const noexcept` (`:472`), `kDefaultFftSize = 2048` (`:86`). |
| `CavernVerb` | `effects/cavern_verb.h:193` | **Layer 4 — read by the test TU and by `VoragoMacroMatrix`'s POD, never included by the engine.** `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, …)` (`:568`); `setSize` (`:613`), `setDarkness` (`:619`), `setDecaySeconds` (`:626`), `setFog` (`:652`), `setDamperDepth` (`:717`), `setFreeze` (`:735`), `setMix` (`:748`), `getLatencySamples` (`:786`). |
| `VoiceAllocator` | `systems/voice_allocator.h:187` | `[[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, …) noexcept` (`:228`), `noteOff` (`:258`), `voiceFinished` (`:288`), `setVoiceCount` (`:326`), `setStealMode` (`:317`), `getVoiceFrequency` (`:446`); `enum class VoiceState` (`:43`), `struct VoiceEvent` (`:102`). |
| `MultiStageEnvelope` | `processors/multi_stage_envelope.h` | `void setStage(int stage, float level, float ms, EnvCurve curve) noexcept` (`:166`), `void gate(bool on) noexcept` (`:99`), `[[nodiscard]] float process() noexcept` (`:223`), `setSustainPoint` (`:178`), `setReleaseTime` (`:206`), `kMinStages = 4` (`:63`). |
| `GrowthEnvelope` | `processors/growth_envelope.h:93` | `class GrowthEnvelope : public ModulationSource`; `void trigger() noexcept` (`:161`), `void processBlock(size_t numSamples) noexcept` (`:185`), `getCurrentValue()` (`:197`), `setDuration(float seconds)` (`:144`). |
| `BreathingModulator` | `processors/breathing_modulator.h:105` | `class BreathingModulator : public ModulationSource`; `processBlock` (`:209`), `getCurrentValue` (`:222`), `setRate` (`:170`), `setDepth` (`:177`), `setIrregularity` (`:184`), `setSeed` (`:164`). |
| `TidalModulator` | `processors/tidal_modulator.h:122` | `class TidalModulator : public ModulationSource`; `processBlock` (`:250`), `getCurrentValue` (`:263`), `setRate` (`:202`), `setDepth` (`:209`), `getBasePeriodSeconds` (`:217`). |
| `TapeSaturator` | `processors/tape_saturator.h` | `void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept` (`:141`), `setDrive(float dB)` (`:239`), `setSaturation(float amount)` (`:248`), per-channel mono in-place `process` (used at `seraphis_engine.h:625-627`). |
| `TruePeakLimiter` | `processors/true_peak_limiter.h:44` | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (`:59`), `void processBlock(float* left, float* right, int numSamples) noexcept` (`:104`), `setCeilingDb` (`:85`). |
| **Pattern template** | `systems/seraphis_voice.h:127`, `seraphis_engine.h:205`, `seraphis_macro_matrix.h:182` | The carry-FIFO whole-chunk render (`seraphis_voice.h:451-484`), `advanceLifeOnly` (`:492`), `silence()` ramp (`:424`), level detector + retirement (`:1178`), seed salts (`:179-183`), `kVoiceSizeBound` (`:204`), the engine's pre/post control steps (`:1149`, `:1215`), amnesty steal (`kAmnestyLevelThreshold`, `:247`), `processOutputStage` (`:619`), and the macro table shape (`struct SeraphisMacroRow`, `:159`; `kRows`, `:213`). |
| **Determinism harness** | `tests/test_helpers/render_fingerprint.h` | `struct RenderFingerprint {rms, peak, meanAbs, totalVariation, checkpoints[32]}` (`:63`), `fingerprintRender(std::span<const float>)` (`:73`), `compareFingerprints(actual, reference, metricTolerance = 2.5e-4, sampleTolerance = 5.0e-4)` (`:122`). |

---

## New components

| Class | Layer | Header | ODR sweep result (run this session) |
|---|---|---|---|
| `VoragoVoice` | 3 | `dsp/include/krate/dsp/systems/vorago_voice.h` | `grep -rn "\bVoragoVoice\b" dsp/ plugins/ tools/ tests/` → **0 matches** |
| `VoragoVoiceConfig` | 3 (namespace-scope POD) | same header | **0 matches** |
| `VoragoEngine` | 3 | `dsp/include/krate/dsp/systems/vorago_engine.h` | **0 matches** |
| `VoragoEngineConfig` | 3 (namespace-scope POD) | same header | **0 matches** |
| `VoragoVoiceParams` | 3 (namespace-scope POD) | same header | **0 matches** |
| `VoragoMacroMatrix` | 3 | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` | **0 matches** |
| `VoragoMacro` (enum class) | 3 | same header | **0 matches** |
| `VoragoMacroTarget` (enum class) | 3 | same header | **0 matches** |
| `VoragoMacroTargetOwner` (enum class) | 3 | same header | **0 matches** |
| `VoragoMacroRow` (struct) | 3 | same header | **0 matches** |
| `VoragoMacroValues` (struct) | 3 | same header | **0 matches** |
| `VoragoCavernTargets` (struct) | 3 | same header | **0 matches** (swept as `VoragoCavernTargets`; the `SeraphisAetherTargets` analogue) |
| `detail::VoragoVoiceSilenceRampProbe` | 3 | `vorago_voice.h` | **0 matches** |
| `detail::VoragoEngineNonFiniteProbe` | 3 | `vorago_engine.h` | **0 matches**. A `detail`-namespace friend struct **forward-declared** in `vorago_engine.h` and **defined in the test TU** (`vorago_nonfinite_test.cpp`), in the `seraphis_engine.h:193-195` shape — **no compile definition** (Q-C, FR-087); exercises FR-072 for SC-029 |

**Near-name hazards checked** (roadmap line 128 names the long list): `ResonatorBank`,
`FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`, `PatternScheduler` all exist and are **not**
used as new names here. No new class in this phase reuses any existing identifier; every name is
`Vorago`-prefixed, which is what makes the sweep above a clean zero.

**No new class names are added to `ContinuousBody`.** AR-4's change appends **enumerators** and
**table rows** to existing class-scoped entities; `MaterialProfile` (`continuous_body.h:653`) is
reused as-is. **Nor to `FeedbackEcology`** (A-13): its change appends one public **method**,
`silenceAudio()`, and no class, member, enumerator or constant.

---

## Functional Requirements

### FR-001 series — `VoragoVoice`: contract and lifecycle

- **FR-001** `VoragoVoice` is a Layer 3 class declared in
  `dsp/include/krate/dsp/systems/vorago_voice.h`, in `namespace Krate::DSP`, header-only. It
  includes Layers 0–2 and Layer 3 peers only; it **must not** include any `effects/` header.
- **FR-002** The voice owns, **by value**, exactly: one `HarmonicCloud`, one `NoiseOrganism`, one
  `ResonanceDriftNetwork`, one `FeedbackEcology`, one `BloomEngine`, **two** `ContinuousBody`, one
  `EcosystemEngine`, one `MultiStageEnvelope`, one `GrowthEnvelope`, `kNumEventSchedulers`
  `SlowEventScheduler`s, one `BreathingModulator` and one `TidalModulator`. **The ecosystem stays
  per-voice** (OQ-1 ruling (c), Clarifications session above — roadmap line 390's per-voice agent
  counts stand) and **the ghost/atmosphere tap is not**: `AtmosphereEngine` moves to `VoragoEngine`
  (OQ-1 ruling (b), FR-056), so the voice **must not** contain a `ModulationEngine`, `VoiceModRouter`,
  `PolySynthEngine`, `SynthVoice`, `CavernVerb`, `SubharmonicEngine`, `SpectralSmear` **or
  `AtmosphereEngine`** member. A `static_assert` on `sizeof(VoragoVoice) <= kVoiceSizeBound` enforces the
  negative half, in the `seraphis_voice.h:196-204` shape, with the bound set to
  `ceil(measured × 1.05)` rounded up to the next 64 B and the measured figure recorded in the header.
- **FR-003** `void prepare(double sampleRate, const VoragoVoiceConfig& cfg) noexcept` is the **only**
  allocating path. Every `cfg` field is **clamped, never rejected**. A second call fully
  reconfigures. It ends with `reset()`, so a freshly prepared voice is silent.
- **FR-004** `VoragoVoiceConfig` is a designated-initialiser-only POD (no positional brace init
  anywhere in repo code or tests — Clang errors on narrowing where MSVC does not;
  `ecosystem_engine.h:290-301` states the rule). It carries at minimum: `maxBlockSamples`
  (clamped to `[1, kMaxBlockSamples]`), the noise-organism source count, the ecology loop count, the
  resonance peak count, the ecosystem agent/cell counts and step interval, and the bloom child-slot
  count. **The atmosphere capture/FFT options move to `VoragoEngineConfig`** (FR-042) —
  `AtmosphereEngine` is engine-owned, not voice-owned (OQ-1 ruling (b)).
- **FR-005** `void reset() noexcept` clears all audio state and re-derives run state without
  allocating; `void silence() noexcept` arms a `kSilenceRampMs` linear fade of the voice's emitted
  output (`seraphis_voice.h:424`, `:155`) so a steal completes inside one control chunk.
- **FR-006** Every method other than `prepare()` is `noexcept`, allocation-free and lock-free.
  `getAllocatedBytes()` reports the prepare-time total and is unchanged by any subsequent call.

  **"The prepare-time total" is literal, and the sub-components had to be made able to say it
  (2026-09-22).** Of the voice's members only `NoiseOrganism`, `FeedbackEcology`,
  `ResonanceDriftNetwork`, `BloomEngine` and `EcosystemEngine` published a figure; the voice
  summed those five and its own getter said so, describing itself as *incomplete by construction*.
  That is a partial figure, not a total. The one real gap was `ContinuousBody` — **two** per voice,
  each holding a waveguide, a comb bank and the decay cloud's delays and diffusion. It now publishes
  its own figure, via a `getAllocatedBytes()` added down the chain it owns: `DelayLine` (the only
  true allocator — `buffer_.capacity() * sizeof(float)`), `FeedbackComb`, `DiffusionNetwork`'s
  `AllpassStage`, `DiffusionNetwork`, `WaveguideString`, `TimeVaryingCombBank`, `ContinuousBody`.
  The seven remaining voice members (`HarmonicCloud`, `MultiStageEnvelope`, `GrowthEnvelope`,
  `SlowEventScheduler`, `BreathingModulator`, `TidalModulator`, and this class's own arrays)
  reach no allocator at all — every one of those headers declares only `std::array` and scalar
  members and names no `std::vector`, `new` or allocator — so they contribute a provable zero and
  the sum is the whole figure. `VoragoVoice_AllocationAccounting` asserts it as an **exact
  equality** against the per-sub-component sum with a non-vacuity floor on each heap-owning part, so
  a member dropped from the sum is red.

  **SC-016 scope:** `continuous_body.h` gains a public `const` getter — **append-only**, zero
  deletions, no change to any Seraphis-observable behaviour, and `node tools/check-seraphis-green.js`
  still reports the required exactly-two deletions. The other six are Layer 0–2 primitives gaining
  the same kind of inert `const` getter.

  This does **not** displace SC-014: an invariant total proves nothing was *resized*; `AllocationScope`
  proves nothing was *allocated*. Both still run. The **engine**'s `getAllocatedBytes()` stays
  explicitly partial — `AtmosphereEngine`, `SubharmonicEngine` and `TruePeakLimiter` publish no
  figure — because this requirement words the total clause on the **voice**; the engine getter carries
  only SC-004a/SC-004b's invariance clause, which a partial-but-stable figure satisfies.
- **FR-007** `kControlChunkSamples = 64`, matching `HarmonicCloud`, `ContinuousBody`,
  `AtmosphereEngine`, `NoiseOrganism`, `ResonanceDriftNetwork`, `FeedbackEcology`, `BloomEngine`
  and `EcosystemEngine` (all cited in the *Existing components* table). The voice **never renders a
  partial chunk**: whole chunks are rendered into a 64-sample stereo carry FIFO and the caller is
  served out of it (`seraphis_voice.h:440-484`). Rendering `n` samples as `36 + 28` and as one `64`
  must leave identical state and produce identical output.
- **FR-008** `void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept` guards, in
  order: any null pointer → write nothing and advance nothing; `n == 0` → no-op consuming no control
  step; `!prepared_` → `n` zeros on both channels and no advance.
- **FR-009** `void advanceLifeOnly(std::size_t n) noexcept` advances every modulation lane, the
  ecosystem and the schedulers on the same carry clock without rendering audio, so that an idle
  voice's life state matches a rendering voice's after the same sample count
  (`seraphis_voice.h:486-506`, `harmonic_cloud.h:893-903`).

### FR-010 series — `VoragoVoice`: the signal chain (roadmap lines 453–456)

- **FR-010** One control chunk executes, in this fixed order:
  1. advance the identity layer (FR-020) and publish its routed values;
  2. `BloomEngine::processChunk(ratios_, amplitudes_, parentCount_, 64)` over the voice's own
     spectrum arrays, then `HarmonicCloud::setSpectralTarget(ratios_, amplitudes_, returnedCount)`
     while the bloom owns at least one live child, or `HarmonicCloud::clearSpectralTarget()` when it
     does not (FR-011);
  3. `HarmonicCloud::processStereoBlock(cloudL, cloudR, 64)`;
  4. `NoiseOrganism::processBlock(noiseMono, 64)` and sum into the excitation bus at the configured
     noise level and stereo placement;
  5. the **voice envelope**, applied in place on the excitation bus (AR-5);
  6. `ResonanceDriftNetwork::processBlock(excL, excR, resL, resR, 64)`;
  7. `FeedbackEcology::processBlock(resL, resR, ecoL, ecoR, 64)`;
  8. the **two-body blend** (FR-030 series) — `ContinuousBody::processStereoBlock` twice, not in
     place, then blended;
  9. silence-fade tail, level detector, retirement counter.

  **The ghost/atmosphere tap is not part of the voice chain** (OQ-1 ruling (b)): `AtmosphereEngine`
  is owned and stepped by `VoragoEngine` (FR-056), fed from the voice sum rather than from this
  per-voice body output. The voice instead publishes `getGhostRequest()` (FR-020b) for the engine to
  read.
- **FR-011** The voice owns two `std::array<float, HarmonicCloud::kMaxPartials>` spectrum arrays
  (`ratios_`, `amplitudes_`) of **exactly 64 entries each**, satisfying `BloomEngine::processChunk`'s
  normative precondition (`bloom_engine.h:466-481`). **While the bloom owns at least one live
  child**, each control step the voice calls
  `HarmonicCloud::setSpectralTarget(ratios_, amplitudes_, count)` with
  `count = getActivePartialCount()`, `ratios_[i] = i + 1` **exactly** (no gravity warp, no
  inharmonicity stretch) and `amplitudes_[i] = (i + 1)^{-p(r)}` with `p(r) = 3.0 - 2.5r`
  (`harmonic_cloud.h:200`, the cloud's own published richness law), and **no tilt applied by the
  voice**. This duplicates the cloud's own unmodified laws exactly — not by cancelling a gravity warp
  or a tilt multiply the cloud still applies on top of a supplied target
  (`harmonic_cloud.h:1312-1330`, `:1332`, `:1493`) — so the supplied target is level- and
  timbre-neutral **at every richness, tilt, gravity and inharmonicity setting**, by construction
  (Q3). **Whenever the bloom owns no live child**, the voice calls
  `HarmonicCloud::clearSpectralTarget()` (`harmonic_cloud.h:1297`) instead, so the shipped richness
  rolloff, tilt, gravity and inharmonicity stay live and are never shadowed by a stale target. Both
  the spawn edge (0 → 1 live child) and the retire edge (1 → 0) must be **click-free**; SC-018a
  measures it.
- **FR-012** `BloomEngine::setCapacity()` is kept equal to the parent count the voice publishes, and
  `setConsumerTiltDb()` is kept equal to the cloud's current `getSpectralTiltDb()`, at every control
  step in which either changes. `reserveBase()` never exceeds `HarmonicCloud::kMaxPartials`.
  **(Q-D)** The parent count the voice hands `BloomEngine::processChunk` is `reserveBase()`
  (`bloom_engine.h:710`), never `getActivePartialCount()`, and — because `setCapacity` is kept at
  `clamp(getActivePartialCount(), kMinCloudCapacity, HarmonicCloud::kMaxPartials)` — FR-011's
  `count == getActivePartialCount()` holds whenever richness keeps the active count at or above
  `kMinCloudCapacity = 14`.
- **FR-013** Voice lifecycle: `noteOn(float frequencyHz, float velocity)` retunes the cloud
  (`setFundamentalHz`), both bodies (`setNoteFrequencyHz`) and the resonance network
  (`setNoteFrequency`), and gates the envelope; `noteOff()` releases the envelope **only**.
  Everything downstream of the envelope rings out. The voice reports `isFinished()` from a level
  detector with instant attack and a `kLevelReleaseMs` release, retiring after
  `kQuiescentChunksToRetire` consecutive chunks below `kTailSilenceThreshold`
  (`seraphis_voice.h:149-157`, `:1178-1196`). Because a Vorago voice's tail is minutes long by
  design, these are **this spec's own constants**, pinned here and justified in the header, not
  inherited from Seraphis (Q6): `kTailSilenceThreshold = -90` dBFS; `kQuiescentChunksToRetire` = the
  number of 64-sample control chunks spanning **≈10 s** at the prepared sample rate
  (`round(10 × sampleRate / kControlChunkSamples)`, a **sample-rate-derived count**, computed at
  `prepare()` and justified in the header — never a literal that silently means a different duration
  at 192 kHz); `kSilenceRampMs = 1.0` (Seraphis's ramp, `seraphis_voice.h:155`). Both retirement
  constants sit **far below** `kAmnestyLevelThreshold` (−30 dBFS, FR-044), so SC-012's enumerated
  victim table stays non-degenerate; with a 4–8 voice pool, **stealing is the normal allocation
  path, not an exception**, and SC-011 gates its click-freedom.
- **FR-014** Envelope mode is selectable, `enum class EnvelopeMode : std::uint8_t { Standard = 0,
  Growth = 1 }` (class-scoped). `Standard` uses the 4-stage `MultiStageEnvelope` with the drone
  defaults **pinned here** (Q5; roadmap line 456's "tens of seconds", made numeric): **attack 20 s**;
  three body stages sweeping **30–60 s** each (the exact per-stage levels and individual stage times
  are fixed in FR-090's table, which **reproduces** this range and may not narrow or accelerate it);
  **release 45 s**. `Growth` multiplies the stage envelope by `GrowthEnvelope::getCurrentValue()`,
  held across the chunk (`seraphis_voice.h:1064-1072`). Like FR-017's ghost values and FR-061's
  macro neutrals, these three numbers are a block FR-090 may only **reproduce**, never re-derive.

  **(A-4) "The 4-stage `MultiStageEnvelope`" means four *pre-sustain* stages, not a stage count of
  four.** The shipped configuration is `kEnvelopeStages = 6` with `kEnvelopeSustainPoint = 4`: the
  four pre-sustain stages at indices 0–3 (attack plus three body stages) are followed by a **0 ms
  sustain-hold at index 4** and a **0 ms post-sustain stage at index 5**, both required by
  `MultiStageEnvelope::advanceToNextStage()`'s contract. This is a restatement of what FR-014 always
  meant, not a change: the attack, stage range and release numbers above are untouched, and FR-090's
  table reproduces all six rows.

  **(Q-G) The shape actually runs.** `MultiStageEnvelope` ships a 10 s per-stage ceiling and
  `GrowthEnvelope` a 60 s duration ceiling; both components gain an append-only, default-inert
  per-instance ceiling setter, and `VoragoVoice::prepare()` raises this instance's ceilings
  (`kEnvelopeMaxStageTimeMs = 120 000 ms`, `kGrowthMaxDurationSeconds = 120 s`) **before** it
  authors a stage. The generator's own `getStageTime()` / `getReleaseTime()` and
  `growth().getDuration()` therefore read back this table, and `VoragoVoice_EnvelopeShapeIsShipped`
  pins the generator, not only the voice's shadow arrays.
- **FR-014a** **The fast-attack test fixture (Q5).** Because FR-014's drone envelope has a 20 s
  attack, several criteria need a render that reaches full level inside a short window without
  altering the shipped character. **One** named fixture, `kFastAttackEnvelopeConfig` —
  `EnvelopeMode::Standard` with attack ≤ **100 ms** and every stage time ≤ **100 ms**, defined once
  in the test helpers and cited by name — is used by **SC-021a**'s 1 s non-silence check,
  **SC-008**'s macro-sweep windows and **SC-022 (2)**'s onset-index measurement. **SC-004b's
  `T_settle` derivation uses FR-014's real numbers, not this fixture**: the 8 h soak renders the
  shipped slow envelope precisely because it is the property under test, and substituting the
  fixture there would be exactly the forbidden "bend the shipped character to fit a test window"
  (roadmap line 558).
- **FR-015** The noise organism is summed into the excitation bus at a settable level and stereo
  placement. Its output is **mono** (`noise_organism.h:414`); the voice decorrelates it to stereo by
  a **fixed 2nd-order all-pass filter pair** (Q8) — one biquad per channel, **different, named
  `constexpr` coefficients per channel**, stated in the header, with the right channel additionally
  delayed by one sample (plan B-3) — **per-channel magnitude exactly flat** (≤ 0.01 dB), and a
  **bounded mono sum**: the mono-fold deviation over [20 Hz, 8 kHz] is **≤ 4.0 dB** and at least
  **6 dB better** than a 1 ms fractional-delay pair, which comb-filters on the mono sum (Q-E: a flat
  mono sum is unattainable with two sections, and the earlier 3.0 dB / one-sample-delay clauses were
  unsatisfiable by construction; measured 3.66 dB at 8 kHz on the shipped pair). Allocation-free,
  sized at `prepare()`, two biquads per voice.
- **FR-016** `ResonanceDriftNetwork` ships in **`AnchorMode::Hybrid`** (not `Keyed`) — the only mode
  that consumes `setGravity` (`resonance_drift_network.h:1412-1437`) — with `setNoteFrequency`
  tracking the voice pitch as the anchor (Q4). `setGravity` (base **0**) is driven by two lanes
  summed: the **`Gravity` macro** (FR-061, bipolar, 0 = air / 0.5 = neutral / 1 = stone) and
  `BreathingModulator` (roadmap line 121: "*breathing* = gravity oscillating via
  `BreathingModulator`"). **`HarmonicCloud::setSpectralGravity` is not a `Gravity`-macro target**
  and is not modulated by either lane — it stays at the cloud's own shipped default/control surface,
  untouched by this macro. `FeedbackEcology` runs as an insert at its shipped `kDefaultMix = 0.15f`
  unless the default table (FR-090) says otherwise; Phase 5's own note that cross-loop interaction is
  ≈ −84 dB at the default voicing (roadmap lines 276–278) is acknowledged in the header, and
  Phase 10's default voicing is chosen to make it audible (shared resonances or lower Q) or the
  header records why it was not.
- **FR-017** **Ghost configuration (AR-3).** The atmosphere tap ships with these **numeric**
  drone-scaled defaults, each written at `VoragoEngine::prepare()` over the component's shipped
  default (`AtmosphereEngine` is engine-owned, OQ-1 ruling (b), FR-056), each read back by SC-027.
  Adjectives are not requirements; these numbers are.
  | Setter | Vorago value | Component's shipped default | Clamp range (cited) | Why |
  |---|---:|---:|---|---|
  | `setDensity` | **0.30** grains/s | 4.0 | `[0.1, 20]` (`atmosphere_engine.h:821-828`) | a ghost is an event, not a wash: ~1 grain per 3.3 s |
  | `setGrainSeconds` | **12.0** s | 4.0 | `[0.05, 30]` (`:814-818`) | a grain is a memory of the drone, not a texture |
  | `setPitchSemitones` | **−12.0** | 0.0 | `[−24, +24]` (`:856-861`) | ghosts sit an octave under the voice |
  | `setPositionSpread` | **0.90** | 0.3 | `[0, 1]` (`:849-853`) | read ages scatter across the whole capture |
  | `setBlur` | **0.85** | 0.0 | `[0, 1]` (`:907-912`) | roadmap line 114's "darker blur defaults", the one item of the 🔶 row this phase delivers outright |
  | `setDecorrelation` | **0.85** | 0.5 | `[0, 1]` (`:899-904`) | wide, unlocalised |
  | `setLevel` | **event-driven**, base **0.0**, scheduler peak **0.60** | 1.0 | `[0, 2]` (`:979-985`) | FR-022's ghost-burst destination; a base of 0 means silence between events, which is what makes a burst a burst |
  These are the shipped values, not a ceiling: FR-090's table reproduces them and the plan may not
  re-derive them, only cite them. `setLevel`'s base/peak pair is the **event gating** that
  substitutes for the event-triggered scheduling `AtmosphereEngine` does not have (AR-3; OQ-3 may
  replace it), now driven each control chunk by the **maximum across voices** of every rendering
  voice's `getGhostRequest()` (FR-020b, FR-056) rather than a single voice's scheduler writing the
  component directly. No `AtmosphereEngine` source change is made by this requirement.

### FR-020 series — `VoragoVoice`: the identity layer (roadmap lines 388–401, 453–456)

- **FR-020** The voice owns one `EcosystemEngine` *(OQ-1 (c))* and advances it once per control chunk
  with `processChunk(64)`. Its per-agent outputs are read with `getAgentOutput(i)` and routed by
  `getAgentKind(i)` to exactly these destinations, which are the shipped surfaces named by roadmap
  lines 394–398:
  | `EcosystemEngine::Kind` | Destination (shipped setter) |
  |---|---|
  | `Partial` | `HarmonicCloud::setMutation` and `BloomEngine::setDepth` |
  | `Resonator` | `ResonanceDriftNetwork::setPeakWake(peak, amount)` |
  | `Noise` | `NoiseOrganism::setSourceWake(slot, amount)` |
  | `Feedback` | `FeedbackEcology::setLoopWake(loop, amount)` |
  | `Ghost` | the voice's `getGhostRequest()` accumulator (FR-020b) — **not** `AtmosphereEngine`
  directly; the engine owns that component (OQ-1 ruling (b), FR-056) |
  The agent → slot assignment is a **fixed, deterministic** mapping computed at `prepare()` (agents
  of a kind are dealt round-robin over that kind's slots), never re-derived per block.
- **FR-020a** **Many-to-one reduction (Q2).** `EcosystemEngine`'s round-robin deal
  (`ecosystem_engine.h:2173-2216`) does not divide evenly: at the shipped default agent count, several
  agents of one kind can address the same destination slot (e.g. `Partial` agents into
  `HarmonicCloud::setMutation`/`BloomEngine::setDepth`, `Ghost` agents into the single ghost
  accumulator), while `Resonator` agents can also outnumber `kMaxPeaks`, leaving surplus slots
  unaddressed. Where several agents of one kind address the same slot, the slot reads **only the
  strongest addressing agent**, selected by `argmax` over `EcosystemEngine::getAgentEnergy(i)`. A
  per-control-step argmax scan over the addressing agents is accepted cost. This reduction is
  independent of, and evaluated before, FR-023's scheduler-vs-ecosystem maximum combine.
- **FR-020b** **The voice's ghost request (OQ-1 ruling (b)).** Because `AtmosphereEngine` is
  engine-owned (FR-056), the voice does not call `AtmosphereEngine::setLevel` itself. It instead
  exposes `[[nodiscard]] float getGhostRequest() const noexcept`: the FR-023 **maximum** of its
  routed `Ghost`-kind ecosystem contribution (FR-020, reduced per FR-020a) and its ghost-destined
  `SlowEventScheduler`'s current value (FR-022), held between control steps. The voice's own
  excitation/resonance/ecology/body chain (FR-010) is unaffected — only the atmosphere destination
  moves off the voice.
- **FR-021** Routing depth is a per-destination scalar in `[0, 1]`; at depth 0 every destination
  reads its configured base value and the ecosystem changes nothing. An unprepared or zero-agent
  ecosystem is neutral.

  **"Per destination" is the `EcosystemEngine::Kind` roster, and it is literal (2026-09-22).** The
  destinations are exactly the five kinds the routing already indexes — `Partial` → cloud mutation
  and bloom depth, `Resonator` → peak wakes, `Noise` → source wakes, `Feedback` → loop wakes,
  `Ghost` → the FR-020b ghost request (`slotCountForKind()`, `vorago_voice.h`) — so the depth is
  `std::array<float, EcosystemEngine::kNumKinds>` and is applied per kind where the lane is stored.
  An earlier draft carried **one shared scalar** and leaned on the per-destination wake **bases**
  (`noiseWakeBase_` / `peakWakeBase_` / `loopWakeBase_`) to satisfy this sentence; the bases are a
  different quantity, and the sentence says *depth*. `setEcosystemDepth(d)` writes all five — how
  every shipped caller uses it, prepare()'s FR-090 default and the `Life` macro row's
  `EcosystemDepth` target included — and `setEcosystemDepthFor(kind, d)` moves one alone, with
  FR-071's index and non-finite rules. `getEcosystemDepth()` reports the **maximum** over the five,
  so a caller using only the scalar setter reads back exactly what it wrote (SC-009's
  `base == getter-after-prepare()` row) and "is anything routed at all" stays one call. Asserted by
  `VoragoVoice_AgentReductionRule`, section *"FR-021: the depth is PER DESTINATION"*, which a single
  shared scalar fails.
- **FR-022** `kNumEventSchedulers = 2` (Q7): a **fast** `SlowEventScheduler` with
  `setIntervalRange` in **20–90 s** and a **slow** one in **3–10 min**, each with its own derived
  seed, `setBipolarProbability(0)` (every event is a **positive wake** — sleep events are out of
  scope for this phase, Q7) and `setDepthRange(0.4, 1.0)`, advanced once per control chunk with
  `processBlock(64)`. Their `getActiveTarget()` (`slow_event_scheduler.h:356`) selects the event
  destination **family**; the families are exactly: bloom trigger (`BloomEngine::triggerBloom()`),
  noise-source wake (`NoiseOrganism::setSourceWake`), resonance-peak wake
  (`ResonanceDriftNetwork::setPeakWake`), ecology-loop wake (`FeedbackEcology::setLoopWake`) and
  ghost-burst level (the voice's `getGhostRequest()` accumulator, FR-020b — not `AtmosphereEngine`
  directly, OQ-1 ruling (b)). `setTargetCount` is set to that family count. **The slot within a
  multi-slot family** (which of the noise sources, peaks or loops) is chosen by a **seeded per-event
  draw** (Q2): drawn once per event from the scheduler's own derived seed (FR-025), uniformly over
  the family's slot count, so the same seed and configuration draw the same slot sequence and a
  different seed does not.
- **FR-023** Where a scheduler and the ecosystem address the same destination, the two contributions
  combine by a single documented rule — **the maximum**, so neither can silence a slot the other woke
  — and the rule is stated in the header. **SC-019a** asserts it over an enumerated pair table; it is
  not left to the routing criterion's prose.
- **FR-024** **Dormancy** (roadmap lines 561–567) is honoured as the components define it: the voice
  sets `setSourceWake` / `setPeakWake` / `setLoopWake` / `setAgentWake` and never reaches past them.
  It does not add a second gate of its own, and it does not assume a component's chain keeps running
  at zero gain.
- **FR-025** `void setSeed(std::uint32_t seed) noexcept` derives every sub-component seed through
  `deriveStreamSeed` (`core/random.h:102`) with **pairwise distinct** class-scoped salts, in the
  `seraphis_voice.h:179-184` shape, including a `static_assert` that the salts are distinct. Two
  voices with the same seed and the same parameters produce the same render; two voices with
  different seeds do not.
- **FR-026** `BreathingModulator` drives `ResonanceDriftNetwork::setGravity` (roadmap line 121; base
  0, summed with the `Gravity` macro per FR-016 — **not** `HarmonicCloud::setSpectralGravity`, Q4)
  and `TidalModulator` drives the smear/fog macro depth the voice publishes (roadmap line 257: "fog
  rolls in via `TidalModulator`"). Both are advanced once per chunk with `processBlock(64)`.

  **(A-12) Neither lane carries a voice-side depth factor, and both are observable.** `BreathingModulator`
  and `TidalModulator` each scale their own output internally through `setDepth`
  (`breathing_modulator.h:177`, `tidal_modulator.h:209`), so a second multiply in the voice would be
  a duplicate depth control the spec never asked for and a value neither component's getter would
  report. The voice therefore publishes what it routes, unscaled: the breathing lane as the value it
  sums into `ResonanceDriftNetwork::setGravity` (base 0, FR-016), and the tidal lane as a **net**,
  `max(0, ·)`, because the smear/fog macro depth it feeds is unipolar and a negative tide would
  subtract from the macro rather than roll fog in. Both become observable through
  `VoragoVoice::getBreathingGravityLane()` and `VoragoVoice::getTidalFogDepth()`, which is what lets
  SC-008's `Entropy` folded clause read "the life-modulator depths" off the product instead of
  inferring them from the render.

### FR-030 series — Dark materials and the two-body blend (roadmap lines 457–459, 122)

- **FR-030** Six materials are **appended** to `ContinuousBody::BodyMaterial`, in this order and no
  other: `StoneChamber`, `SteelTank`, `WoodenHull`, `CathedralColumn`, `CavernWall`, `GlassSphere`.
  `Glass = 0, Strings, MetalPlate, Chamber, Ice` keep their existing values (append-only).
- **FR-031** `kNumMaterials` becomes `11`, and `kMaterialProfiles` gains exactly six
  `MaterialProfile` rows in the same order, written with **designated initialisers and explicit `f`
  suffixes** (`continuous_body.h:717-725` states why).
- **FR-032** Each new material's row selects one of the three shipped engines
  (`Engine::Modal | Waveguide | Comb`, `continuous_body.h:82`) and supplies `ratios`,
  `defaultModeCount`, `amplitudeExponent`, `damping` (`{b1, b3}`), `stretch`, `scatter`,
  `referenceHz`, `t60AtMaxResonanceSec` and `hfDampingParam`. **No new engine, no new profile field,
  no new DSP.**
- **FR-033** Modal materials that do not reuse `kGlassRatios` or `kPlateRatios` add a class-scoped
  `static constexpr std::array<float, kModeCountCeiling>` ratio table that is **strictly increasing
  over all 32 entries** — the property `continuous_body.h:669-671` says makes Nyquist prefix
  truncation exact. A `static_assert` (or `constexpr` predicate in the same shape as
  `aetherTableStrictlyAscending`, `cavern_verb.h:121`) enforces it.
- **FR-034** Every new material's mode ratios and damping law are **sourced**: the header records the
  physical model or published table each is derived from, in the shape `continuous_body.h:673-710`
  uses for Glass (Rossing wine glasses) and Plate (Rossing thin circular plate). A table with no
  cited basis is a defect.
- **FR-035** Each new material is **darker than every shipped Seraphis material** on a stated,
  measurable axis: its steady-state spectral centroid under the same excitation is below the minimum
  of the five shipped materials' centroids. SC-015 measures it.
- **FR-036** `VoragoVoice` owns **two** `ContinuousBody` instances, each with an independently
  settable material, and blends them: `out = (1 − b)·A + b·B` with `b ∈ [0, 1]` smoothed at the
  voice's control-rate gain ramp. `b = 0` and `b = 1` are **exact** single-body renders (the unused
  body still runs, so its state matches — the composed render must not depend on blend history).
- **FR-037** Blend automation from `b` to `b'` produces no discontinuity: the ramp is applied
  per-sample inside the chunk, **not stepped at the chunk boundary**. A chunk-stepped blend is a
  64-sample staircase — the same zipper SC-010 guards for macros — and SC-017's endpoint and
  ramp-return clauses would not see it, so **SC-017a** measures it directly.
- **FR-038** **Every site sized or pinned by `kNumMaterials` is updated in the same change.** There
  are **eleven**, in six files (Q-F widened the original seven-in-three), and they do **not** all
  take the same treatment. Four of them are
  `constexpr std::array<…, ContinuousBody::kNumMaterials>` written with exactly five initialisers:
  raising the count leaves them **compiling** and zero-filled, which is silent UB (six `Glass`
  entries, six null `const char*`), not a build break. Enumerated:

  | # | Site | Current form | Required treatment |
  |---|---|---|---|
  | 1 | `continuous_body_perf_test.cpp:283` | `static_assert(kNumMaterials == 5, "SC-005 measures 5 materials x 4 configurations = 20 measurements")` | becomes `== 11`; the message becomes 11 × 4 = 44 |
  | 2 | `continuous_body_perf_test.cpp:352` | `constexpr std::array<BodyMaterial, kNumMaterials> kMaterials` | **extended to eleven initialisers**, in enumerator order |
  | 3 | `continuous_body_perf_test.cpp:360` | `constexpr std::array<const char*, kNumMaterials> kMaterialNames` | **extended to eleven initialisers**, same order |
  | 4 | `continuous_body_perf_test.cpp:373-379` | `crossfadePartner` = cyclic successor `(idx + 1) % kNumMaterials` | **must wrap inside each block**: `(i + 1) % kNumSeraphisMaterials` for `i < 5`, and `5 + ((i − 5 + 1) % 6)` for the six new ones. Plain widening would re-point Ice's partner from Glass to StoneChamber and thereby change a **Seraphis-measured** crossfade pairing, which SC-016 forbids. |
  | 5 | `continuous_body_test.cpp:1082` | `REQUIRE(CB::kNumMaterials == 5u)` | becomes `== 11u` |
  | 6 | `seraphis_perf_test.cpp:419` | `static_assert(kNumMaterials == 5, "SC-001 measures all five materials and uses the worst")` | **re-pointed at `kNumSeraphisMaterials`** (FR-038a), message unchanged in substance |
  | 7 | `seraphis_perf_test.cpp:571` + `:579` + `:654` + `:662` + `:1196` | `kMaterials`, `kMaterialNames`, `MaterialSurvey::nsPerBlock` and both loops, all sized/bounded by `kNumMaterials` | **re-sized/re-bounded to `kNumSeraphisMaterials`**; the five initialisers and their content are **unchanged** (FR-038a) |
  | 8 | `continuous_body_spectral_test.cpp:202` + `:210` + `:1457` | `kAllMaterials`, `kAllNames`, `kExpectedT60` and every per-material result array, sized by `kNumMaterials` with five initialisers | **re-sized to `kNumSeraphisMaterials`** with a `static_assert(kNumSeraphisMaterials == 5)`; SC-003's thresholds were measured over the five (Q-F) |
  | 9 | `plugins/seraphis/src/parameters/dropdown_mappings.h:201` | `std::array<const TChar*, 5> kBodyMaterialLabels` + `static_assert(size() == kNumMaterials)` | **sized by and asserted against `kNumSeraphisMaterials`**, plus `static_assert(kNumSeraphisMaterials == 5)`: ID 800 keeps five steps (Q-F) |
  | 10 | `plugins/seraphis/src/parameters/dropdown_mappings.h:302` | `toBodyMaterial` clamps to `kNumMaterials − 1` | clamps to **`kNumSeraphisMaterials − 1`** so a corrupt index can never select a Vorago material through Seraphis's parameter |
  | 11 | `plugins/seraphis/tests/integration/param_perf_test.cpp:584` + `:1884` + `:1888` + `:1896` + `:1909` + `:2532` | `static_assert(kNumMaterials == 5)`, `MaterialSurvey::modeCount`, `kMaterials`, `kMaterialNames` and both survey loops | **re-pointed / re-sized / re-bounded to `kNumSeraphisMaterials`**; initialisers unchanged (FR-038a) |

  No other Seraphis assertion, baseline or default may move. SC-016 gates it.
- **FR-038a** **`ContinuousBody` gains `static constexpr std::size_t kNumSeraphisMaterials = 5;`**,
  documented as "the five materials shipped with Seraphis Phase 4; the prefix
  `seraphis_perf_test.cpp` surveys, so appending materials cannot re-point Seraphis's measured
  SC-001/SC-002 subject." It is an **appended line** — FR-039's zero-deletion rule is intact.
  Seraphis's perf survey (`surveyMaterials()`, `:662`) iterates that prefix, so
  `kMaterials[survey.worstIndex]` (`:1205`) — which feeds `buildChainSubject`,
  `measureVoiceSubject` and `measureBodySubject` — continues to select from exactly the five
  materials Seraphis's checked-in baselines were set against. **Phase 10 does not measure Vorago
  materials on Seraphis's account, and Seraphis does not measure Vorago's.**
- **FR-038b** **Each new material carries the shipped `continuous_body_perf_test` budgets.** That
  case asserts `steady`, `operating`, `crossfade` and `cloudOnly` over **every** material index
  (`:881-897`) against `kSteadyBaselineNsPerBlock` / `kOperatingBaselineNsPerBlock` /
  `kCrossfadeBaselineNsPerBlock` / `kCloudOnlyBaselineNsPerBlock` at `kRegressionFactor = 1.5`.
  Two of those four are **capped, not measurement-pinned** — the file records "operating 41.1 →
  41,200, which exceeds `kMaxAdmissibleHalfPctNs` by 15.7 %", capped at 35,500 (`:169-180`,
  `:222-229`) — so the headroom a new material inherits is 29 %, not 50 %.
  Each of the six new materials' standalone steady / operating / crossfade / cloud-only cost
  **must satisfy those shipped baselines at `kRegressionFactor`**. A material that does not is
  **re-voiced** (fewer modes, a cheaper engine, a gentler damping law) or **dropped from the six**,
  and the drop is surfaced to the user. It is **never** accommodated by raising a baseline, by
  narrowing the measured configuration, or by excluding the material from the loop — all three are
  the forbidden move of roadmap line 558 and `CLAUDE.md`. SC-025 measures it.
- **FR-039** The `continuous_body.h` diff is **append-only except for two arithmetic widenings that
  cannot be expressed as an append**, and those two are enumerated so "append-only" is a checkable
  claim rather than a slogan:
  1. `enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice };`
     (`:81`) gains six enumerators **after** `Ice`. No existing enumerator's value moves, so no
     stored Seraphis material index moves.
  2. `static constexpr std::size_t kNumMaterials = 5;` (`:84`) becomes `= 11;`.

  Everything else — the six `kMaterialProfiles` rows, the new ratio tables, `kNumSeraphisMaterials`
  (FR-038a) and every `static_assert` this phase adds — is a **pure append**. Verification is
  therefore `git diff --numstat dsp/include/krate/dsp/systems/continuous_body.h` showing **exactly
  two deleted lines**, and `git diff -U0` on the same file showing those two deletions are the
  `BodyMaterial` line and the `kNumMaterials` line and nothing else (the Phase 3 `resonator_bank.h`
  precedent, recorded at roadmap lines 218–220's compliance note). **No behavioural line, no
  profile row, no default and no clamp of a shipped material is edited.**

### FR-040 series — `VoragoEngine`: polyphony and allocation

- **FR-040** `VoragoEngine` is a Layer 3 class in
  `dsp/include/krate/dsp/systems/vorago_engine.h`, `namespace Krate::DSP`, header-only, including
  Layers 0–2 and Layer 3 peers only. It **must not** include `effects/cavern_verb.h` or any other
  `effects/` header (AR-1).
- **FR-041** It owns `kMaxVoices` `VoragoVoice` instances by value in a `std::array`, one
  `VoiceAllocator`, one `SubharmonicEngine`, one `SpectralSmear`, **one `AtmosphereEngine`** (the
  global ghost/atmosphere tap, OQ-1 ruling (b), FR-056), two `TapeSaturator` and one
  `TruePeakLimiter`. `kMaxVoices` is the ceiling; the *shipped* polyphony is set by OQ-1 (a) — the
  one sub-decision the Clarifications session left open (see Open Questions).
- **FR-042** `void prepare(double sampleRate, const VoragoEngineConfig& cfg) noexcept` is the only
  allocating path; it prepares every voice, the global `AtmosphereEngine` and every other owned
  component regardless of the current polyphony, so `setPolyphony()` never allocates.
  `VoragoEngineConfig` carries the atmosphere capture/FFT options forwarded to
  `AtmosphereEngine::PrepareConfig` (moved from `VoragoVoiceConfig`, FR-004, since the component is
  now engine-owned).
- **FR-043** `void setPolyphony(std::size_t n) noexcept` clamps to `[1, kMaxVoices]`, frees voices
  above the new count through the allocator, and ramps the voice-sum gain rather than stepping it
  (`seraphis_engine.h:428`, `:219-246` — 100 ms, with the measured justification for why 20 ms is a
  click).
- **FR-044** `noteOn(std::uint8_t note, std::uint8_t velocity)` / `noteOff(std::uint8_t note)` go
  through `VoiceAllocator`, whose events are dispatched to voices. Steal policy is
  **quietest-victim with long-release amnesty** (roadmap line 460), and the amnesty **protects the
  loud, it does not prefer them**: an idle voice is always taken first; failing that, the victim is
  the **sounding voice with the lowest level**; a voice whose level is **above**
  `kAmnestyLevelThreshold` is granted **amnesty** and is stolen only when no idle voice exists
  **and every sounding voice is above the threshold**. This matches `seraphis_engine.h:247` —
  which documents the constant as the "−30 dBFS long-release steal amnesty threshold", i.e.
  still-ringing voices are the ones kept — its `freeChosenVictimSlot` path (`:1360`), and SC-012.
  A steal calls the victim's
  `silence()` (FR-005) so the transition is a `kSilenceRampMs` fade, never a cut.
- **FR-045** Per-voice seeds are derived from one engine seed through `deriveStreamSeed` with a voice
  salt base **disjoint from every `VoragoVoice` salt** (`seraphis_engine.h:217-218`). Roadmap line
  461: "per-voice unique seeds". Two voices playing the same note at the same time must not render
  identically.
- **FR-046** Voices that are allocated but not rendering still advance their life state via
  `advanceLifeOnly` (FR-009), so a voice that is stolen into after minutes of silence does not start
  from a cold ecosystem. Asserted **at the engine level** by SC-030, not only as a voice-level
  property.
- **FR-047** `void reset()`, `void silence()`, `getActiveVoiceCount()`, `getRenderingVoiceCount()`,
  `getVoiceLevel(i)`, `getVoiceState(i)`, `getVoice(i)` (const) and `getSeed()` are provided, in the
  `seraphis_engine.h:979-1033` shape.
- **FR-048** **Seeds are per voice *slot*, and the seed is never advanced per note.** A slot's seed
  is derived once, at `prepare()`/`setSeed()`, from the engine seed and the slot index (FR-045).
  `noteOn` **re-derives the slot's run state** from that fixed slot seed — it does not consume,
  advance, reseed or perturb it. The consequences are normative, stated in the header, and
  measured by SC-026:
  - the **same slot** playing the **same note** twice, after a retire or a steal, reproduces its
    first trajectory;
  - a **different slot** playing that same note does **not** (FR-045's disjoint salts);
  - therefore the whole instrument's render is a pure function of (engine seed, configuration, note
    sequence), which is what makes a Phase 14 preset render reproducible and what makes SC-006
    meaningful. Advancing a seed per note would make every render of the same preset differ, and is
    forbidden.

### FR-050 series — `VoragoEngine`: the global chain (roadmap line 461, OQ-4 decided)

- **FR-050** `void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept` renders the
  voice sum and then, **in this order**: `SubharmonicEngine::processBlock(l, r, l, r, n)` (in place
  is supported, `subharmonic_engine.h:539`), then `SpectralSmear::processBlock(l, r, n)` (in place,
  `spectral_smear.h:341`).
- **FR-051** The subharmonic engine is **global, post-voice-sum** — roadmap Open Question 4 was
  **decided in Phase 6** (roadmap lines 584–586) and this spec does not reopen it. Its
  `setFundamentalHz` tracks the **lowest sounding voice**; when no voice sounds, the last value is
  held (never reset to a default, which would glissando the subs on every note).
- **FR-052** `SpectralSmear` reports latency (`getLatencySamples()`, `spectral_smear.h:472`,
  constant for a prepared instance and `0` when the prepare-time `enabled` flag is false). The
  engine exposes `[[nodiscard]] std::size_t getLatencySamples() const noexcept` returning it, so the
  Phase 11 plugin can report host latency. Nothing in this phase compensates it internally.
  `VoragoEngineConfig` carries the `SpectralSmear` prepare-time **enable** flag and forwards it, so
  the component's documented bit-identical bypass (`spectral_smear.h:15-16`, `:342-347`) is a
  reachable engine state — which is what makes SC-022's group-delay measurement possible on the
  real object rather than on a fabricated fixture.
- **FR-053** `void processOutputStage(float* l, float* r, std::size_t n) noexcept` applies
  `TapeSaturator` per channel at **low drive** (roadmap line 462) followed by `TruePeakLimiter`
  **always last, over the whole block** (`seraphis_engine.h:619-629`). It is documented as the
  after-the-reverb call (AR-1) and is correct on a buffer the engine did not produce.
- **FR-054** `processOutputStage` guards `l == nullptr || r == nullptr || n == 0 || !prepared_` by
  returning without writing.
- **FR-055** The engine exposes the cavern targets it does **not** own as a POD:
  `[[nodiscard]] VoragoCavernTargets computeCavernTargets() const noexcept` on the macro matrix
  (FR-063), never a `CavernVerb` reference.
- **FR-056** **The ghost/atmosphere tap is global (OQ-1 ruling (b)).** `VoragoEngine` owns one
  `AtmosphereEngine` (FR-041), fed from the **voice sum** — the same buffer FR-050's
  `SubharmonicEngine`/`SpectralSmear` stage consumes, tapped **before** them so the ghost hears the
  raw ensemble, not the subharmonic-and-smear-processed signal — and summed back as **wet texture
  only**, with no second gain (the component's own `setLevel` trim is already applied), into the
  same bus. FR-017's ghost-configuration values are installed at `VoragoEngine::prepare()`, not
  `VoragoVoice::prepare()`. **Event-gated at engine level:** each control chunk, the engine reads
  every rendering voice's `getGhostRequest()` (FR-020b) and calls `AtmosphereEngine::setLevel` with
  the **maximum** across voices — the same non-silencing combine principle FR-023 already
  establishes for a single voice's scheduler-vs-ecosystem contributions — so any one voice's
  ecosystem or slow-event ghost activity is heard by the whole instrument. This relocation removes
  `AtmosphereEngine`'s **113 298 ns/voice** from each voice and adds **one** instance (113 298 ns) to
  this global stage (FR-081's "L-1" row, now **applied**, not merely a candidate lever).

### FR-060 series — `VoragoMacroMatrix` (roadmap lines 463–468)

- **FR-060** `VoragoMacroMatrix` is a Layer 3 class in
  `dsp/include/krate/dsp/systems/vorago_macro_matrix.h`. The mapping is **data**: a
  `static constexpr std::array<VoragoMacroRow, kNumRows> kRows`, in the `seraphis_macro_matrix.h:213`
  shape. `struct VoragoMacroRow { VoragoMacro macro; VoragoMacroTargetOwner owner;
  VoragoMacroTarget target; float base; float amount; ModCurve curve; }`.
- **FR-061** The macro roster is `enum class VoragoMacro : std::uint8_t` with the **twelve** concepts
  of the roadmap's architecture overview (lines 79–81): `Darkness, Age, Density, Movement, Gravity,
  Entropy, Pressure, Weight, Fog, Life, Depth, Mass, Count`. The three concepts that appear only in
  the Phase 10 prose list (line 464: `Decay`, `Instability`, `Distance`) are **folded**, with the
  folding recorded in the header: `Decay` → `Age` + `Depth`; `Instability` → `Entropy`;
  `Distance` → `Fog`. Roadmap line 468 authorises the trim ("15 concepts is a ceiling, not a
  target"); OQ-2 carries the confirmation. **A fold is not a deletion**: FR-068 makes each folded
  concept's behaviour normative on its survivors, and SC-008 asserts it directionally. A survivor
  that does not carry the folded behaviour has lost a roadmap concept, which is a defect.

  **The twelve neutrals are stated here, not deferred**, because SC-008 ("everything else at
  neutral") and SC-009 ("every macro at its neutral") cannot be written without them:

  | Macro | Neutral | Shape |
  |---|---:|---|
  | `Darkness`, `Age`, `Density`, `Movement`, `Entropy`, `Pressure`, `Weight`, `Fog`, `Life`, `Depth`, `Mass` | **0.0** | unipolar: 0 = off, 1 = full |
  | `Gravity` | **0.5** | **bipolar**: 0 = air (pushed off the harmonic grid), 0.5 = neutral, 1 = stone (pulled onto it) |

  `Gravity` is bipolar for the same reason Seraphis's is (`seraphis_macro_matrix.h:396-401`): the
  concept has two opposed directions from the shipped voicing, and one row must express both. Its
  shipped target is `ResonanceDriftNetwork::setGravity` (FR-016), base 0;
  `HarmonicCloud::setSpectralGravity` is **not** a `Gravity`-macro target (Q4).
- **FR-062** `enum class VoragoMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Cavern }`.
  `Voice`- and `Engine`-owned rows are written by `void apply(VoragoEngine&) const noexcept`.
  `Cavern`-owned rows are **never written by `apply()`** — the header may not name a Layer 4 type
  (`seraphis_macro_matrix.h:52-58` states exactly this constraint for its own `Aether`/`Effects`
  owners).
- **FR-063** `[[nodiscard]] VoragoCavernTargets computeCavernTargets() const noexcept` returns the
  Cavern-owned rows as plain floats in a POD whose **field order matches the enumerator order**, so
  the field index is a pure offset. Each field's default is the corresponding `CavernVerb` shipped
  default, **duplicated as a literal with the source line cited** (the `SeraphisAetherTargets`
  construction, `seraphis_macro_matrix.h:123-133`): `kDefaultSize = 0.50f` (`cavern_verb.h:253`),
  `kDefaultDarkness = 0.80f` (`:254`), `kDefaultDecaySeconds = 20.0f` (`:255`),
  `kDefaultFog = 0.30f` (`:259`), `kDefaultDamperDepth = 0.35f` (`:247`),
  `kDefaultMix = 1.00f` (`:264`), `kDefaultWidth = 1.00f` (`:263`).
- **FR-064** **What `base` means, and why it is not the component default.** For a `Voice`- or
  `Engine`-owned row, `base` is **the FR-090 prepare-time value that `VoragoVoice` /
  `VoragoEngine` actually installs on that target**, cited to its FR-090 table row — **not** the
  owning component's own shipped default. Only `Cavern`-owned rows use duplicated `CavernVerb`
  component defaults (FR-063), because nothing in this phase prepares a `CavernVerb`.

  This is the template's definition, not a relaxation: `seraphis_macro_matrix.h:160-163` defines
  `base` as "The FR-019 shipped **VOICE** default, read off `SeraphisVoice::prepare` step 6
  (`seraphis_voice.h:269-313`)", and `:205-208` repeats it above `kRows`. The alternative is
  incoherent with this spec's own requirements: FR-017 sets atmosphere density to 0.30 over the
  component's shipped 4.0 (`atmosphere_engine.h:821-828`), so a row basing on 4.0 would have
  `apply()` at neutral **write 4.0 back**, destroying the ghost configuration on the first block
  and falsifying FR-066's exact identity and SC-009 by construction.

  Rows that share a target **must** carry the same `base`; a `constexpr` predicate asserts it below
  the class, in the `everyRowSharesOneBasePerTarget` shape (`seraphis_macro_matrix.h:673-685`).
  SC-009 additionally asserts, **per row**, that `base` equals the value read back from that
  target's getter immediately after `prepare()` — the mechanical check that keeps FR-066 true and
  that catches an FR-090 default and a `kRows` literal drifting apart.
- **FR-064a** **How a row evaluates, including at a non-zero neutral.** `apply()` computes, per
  target, `acc = base(target)` once and then adds one contribution per row on that target
  (`seraphis_macro_matrix.h:929-938`'s `contributionOf`, and the "no neutral fast path" note at
  `:940-945`):
  - **unipolar macro** (neutral 0): `contribution = amount × curve(m)`.
  - **bipolar `Gravity`** (neutral 0.5): `g = (m − 0.5) × 2` over `[−1, +1]`, and
    `contribution = amount × curve(|g|) × sign(g)`.

  `applyModCurve(c, 0) == 0` for all three permitted curves (FR-065), and `g == 0` at `m = 0.5`,
  so **every** term is exactly `0` at every macro's neutral and `acc == base` follows from the
  arithmetic rather than from a `if (neutral) return;` shortcut. There is deliberately **no** such
  shortcut: it would let a mis-signed row hide behind it, which is exactly what SC-009 exists to
  catch.
- **FR-065** Curves are `ModCurve::Linear | Exponential | SCurve` only. `Stepped` is excluded — it
  breaks continuity by construction (`seraphis_macro_matrix.h:170-174`), and a `constexpr` predicate
  asserts its absence.
- **FR-066** At every macro's neutral, `apply()` and `computeCavernTargets()` are an **exact
  identity**: the rendered instrument is bit-identical to the same instrument with the matrix never
  called.
- **FR-067** `apply()` is **idempotent**: calling it every block with unchanged macro values steps
  nothing. Every writable target either early-outs on an unchanged value or is a plain scalar store;
  the matrix adds no smoother of its own (`seraphis_macro_matrix.h:176-181`).
- **FR-068** Each of the twelve macros has a **documented, directional** multi-target mapping.
  **Six** of them are normative here — the three roadmap line 465–467 exemplars, plus the three
  survivors that carry a folded concept (FR-061), because a fold that nothing requires is a fold
  that vanishes in the build:
  - **Weight** (roadmap 465) = sub tone levels ↑ + **body mass materials** (expressed, per Q1, as
    the two-body blend **`b`**, FR-036, biased toward the darker/heavier body, plus
    `ContinuousBody::setDamping` raised on both bodies — **not** a discrete material switch; there is
    no discrete-target row, Q1(a)) + spectral tilt darkening.
  - **Life** (roadmap 466) = ecosystem activity ↑ + slow-event rate ↑ + bloom probability ↑.
  - **Fog** (roadmap 467) = smear amount ↑ + ghost mix ↑ + **distance filtering ↑** — this is also
    where **`Distance`** lands (FR-061), and "distance filtering ↑" is the folded behaviour,
    expressed (per Q1) as **`CavernVerb::setDarkness`/`CavernVerb::setFog`** (both `Cavern`-owned
    rows via `VoragoCavernTargets`, FR-063) plus **`HarmonicCloud::setSpectralTiltDb`** on the
    voice-owned side.
  - **Age** carries the **shortening** half of `Decay`: in addition to its own HF-loss mapping, it
    **raises `ContinuousBody::setDamping` on both bodies and shortens the cavern decay target**
    (`VoragoCavernTargets::decaySeconds` ↓). An `Age` that only darkens has lost `Decay`.
  - **Depth** carries the **lengthening** half of `Decay`: in addition to raising the reverb return
    share, it **lengthens the cavern decay target** (`decaySeconds` ↑) and raises
    `CavernVerb::setSize`.
  - **Entropy** carries **`Instability`**: in addition to its own spectral-flatness mapping, it
    **raises `HarmonicCloud::setMutation` and the life-modulator depths** (drift depth, breathing
    irregularity), i.e. the instrument becomes less predictable over time, not merely noisier in
    one frame.

  **Every `VoragoMacroTarget` enumerator is float-valued, on a shipped setter** (Q1): the roster is
  enumerated at plan stage from exactly this section's six mappings (plus the remaining six macros'
  single-target mappings authored in `kRows`); there is no discrete-target enumerator and this phase
  adds no new DSP component to express one.

  SC-008 asserts the folded clauses directionally on the `Age`, `Depth` and `Entropy` rows, so a
  mapping that drops one is red. The remaining six mappings are authored in `kRows` and each row's
  direction is stated in a comment above it.

  **Amended 2026-09-19/09-22 — two rows above ship as CLAIM ROWS (`amount = 0`).** Both follow
  from rulings already recorded in Clarifications; this paragraph is those rulings written into
  FR-068's own text, which is where a reader checks the mapping:

  - **`Fog` → `HarmonicCloud::setSpectralTiltDb` (the voice-owned half of the folded
    `Distance`).** Q-N re-based SC-008's `Fog` metric on **harmonic-band spectral flatness**,
    which the smear and the ghost *raise* (blur) and which a **steeper tilt collapses** (measured
    −61 % for the tilt alone against +19 % for the ghost). The two moves therefore oppose each
    other on the same metric, and the tilt is the smaller, darker one. Ruled: the target stays
    **claimed at the FR-090 tilt base (−4.0 dB) with `amount = 0`**, and the folded `Distance`
    behaviour is carried **entirely by the two `Cavern`-owned rows**, `CavernFog` (+0.55) and
    `CavernDarkness` (+0.15). "Distance filtering ↑" is therefore satisfied cavern-side; the
    voice-side row exists because FR-063/`everyTargetIsClaimed` requires every enumerator to be
    claimed exactly once, not because it is expected to move.
  - **`Depth` → `CavernMix` ("raising the reverb return share").** Q-O established that the
    wet/dry RMS share cannot see this drone at all — its energy sits at 16–65 Hz, below the cavern
    FDN's DC-blocker corner — and amended SC-008's `Depth` metric to the **post-note-off tail**.
    Independently, `CavernMix`'s FR-090 base is **1.00**, the component's own clamp maximum: the
    row is already fully wet at `Depth = 0`, so its available travel is **exactly zero** and any
    non-zero amount would be clamped away rather than heard. Ruled: `CavernMix` ships
    **claimed at 1.00 with `amount = 0`**, and `Depth`'s movement is carried by
    `CavernDecaySeconds` ↑ and `CavernSize` ↑ (+0.45), which are the two rows SC-008's amended
    metric measures.

  A claim row is **not** a dropped mapping: it is a target whose value this phase pins and does not
  modulate, and both are visible as such in `kRows` with the reasoning above quoted at the row
  (`vorago_macro_matrix.h`). Moving either one is a Phase 12 product decision — a different
  shipped `CavernMix` base, or a `Fog` metric that separates blur from tilt — never a silent
  amount edit.
- **FR-069** `setMacro(VoragoMacro, float)` clamps to `[0, 1]`, rejects non-finite input (the
  previous value stands), and `getMacro` reports the clamp. `setMacros(const VoragoMacroValues&)`
  and `getMacros()` round-trip. An out-of-range `VoragoMacro` is a silent no-op on set and returns
  the macro's neutral on get. SC-028 is the setter-contract case.

  **`setTargetBase` / `resetTargetBases` / `getTargetBase` are NOT provided by this phase.** They
  are Phase 12's parameter-override surface (`seraphis_macro_matrix.h:872-894` is Seraphis's, added
  by *its* parameter phase, not by its composition phase), the roadmap's Phase 10 bullet does not
  ask for them, and this spec's Non-Goals disclaim all Phase 12 parameter work. They also collide
  with FR-064: a runtime-mutable base would make `everyRowSharesOneBasePerTarget` a compile-time-only
  guarantee and make SC-009's per-row `base == getter-after-prepare()` check conditional on override
  state. Phase 12 adds them and specifies that interaction, with the no-headroom-rescaling ruling
  Seraphis recorded at `seraphis_macro_matrix.h:866-871`.

### FR-070 series — Real-time safety, determinism, portability

- **FR-070** No allocation, lock, exception, system call or I/O outside `prepare()` on any path in
  any of the three new components. A zero-allocation assertion (the repo's
  `getAllocatedBytes()`-invariance pattern) is asserted over an 8 h-equivalent accelerated render.

  **(A-3) Division of labour between SC-014 and SC-004b — neither criterion is narrowed by the
  other.** Two arms carry this requirement, and they carry different halves of it:
  **SC-014** is the **per-push** arm — an `AllocationScope`/`allocation_detector` assertion over a
  **10-minute-equivalent** accelerated render that exercises every wake/sleep edge, every bloom
  spawn/retire, every steal and every macro extreme, i.e. it gates the *edge coverage* on every
  push. **SC-004b** carries this requirement's **8 h `getAllocatedBytes()`-invariance clause**
  **unaccelerated**, in the `[long]` lane, i.e. it gates the *duration*. Reading either as
  superseding the other would drop a half: SC-014 at 10 minutes does not prove 8 h invariance, and
  SC-004b's 8 h held note does not walk the edges SC-014 walks. Both stand as written.
- **FR-071** Every public float setter rejects non-finite input (previous value stands) and clamps
  out-of-range input, with the getter reporting the clamp. Every index-taking setter treats an
  out-of-range index as a silent no-op. This is the uniform rule phases 2–9 all adopted
  (`bloom_engine.h:524-529`).
- **FR-072** Non-finite containment: if any voice's render produces a non-finite sample, the engine
  resets that voice's audio state, counts the event in
  `[[nodiscard]] std::uint32_t getNonFiniteRecoveryCount() const noexcept`, and the block still
  leaves the output finite. **The recovery path is exercised, not merely asserted-absent:**
  `detail::VoragoEngineNonFiniteProbe` (FR-087) injects the fault and SC-029 gates the outcome —
  every other criterion in this spec asserts the counter is **zero**, which on its own would let a
  dead, mis-indexed or wrong-voice containment branch pass the entire suite.
  Detection uses **bit-pattern** tests, never `std::isnan`/`std::isfinite`
  (roadmap line 570; `seraphis_voice.h:919`, `seraphis_engine.h:1092`).
- **FR-073** The output of `processOutputStage` is bounded: `|out| ≤ 1.0` after the limiter, for any
  parameter combination, any macro combination and any polyphony.
- **FR-074** Determinism: for a fixed engine seed, fixed configuration and fixed note sequence, two
  runs in the same process produce renders whose `render_fingerprint.h` comparison is within the
  shared tolerances (`kMetricTolerance = 2.5e-4`, `kSampleTolerance = 5.0e-4`,
  `render_fingerprint.h:55-61`). **No bit-exact float golden is checked in anywhere** (roadmap
  line 568).
- **FR-075** Portability: `node tools/check-portability.js` is clean; no narrowing in brace init
  (designated initialisers throughout); no `std::isnan` under `-ffast-math`; no aligned SIMD load or
  store is introduced by this phase (it introduces no SIMD at all). The new TUs are additionally
  syntax-checked against libstdc++ (the Phase 3 precedent at roadmap lines 220–222).
- **FR-076** Sample-rate independence: `prepare()` at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz
  produces a working instrument with no assertion, no allocation failure and no non-finite output.
  A non-finite sample rate is substituted, then floored at the components' shared
  `kMinUsableSampleRate = 8000.0` (`ecosystem_engine.h:201`, `bloom_engine.h:229`).
- **FR-077** `prepare()` may be called repeatedly; configuration (seeds, macro values, every setter
  value) survives it, and only derived state is rebuilt — the rule every Vorago component already
  follows (`ecosystem_engine.h:330-335`).

### FR-080 series — Budget, registration, and the stop-and-surface rule

- **FR-080** The instrument's **full-polyphony, everything-on** cost, measured as ns per
  512-sample block at 48 kHz, is a functional requirement, not an aspiration (roadmap line 558).
  Measurement basis, inherited verbatim from `cavern_verb_perf_test.cpp:109-125`:
  `kBlockBudgetNs = (512 / 48000) × 1e9 = 10 666 666.67`; `kRegressionFactor = 1.5`;
  `kReferenceNs = kBlockBudgetNs × 0.30 = 3 200 000.0` (roadmap line 470);
  `kMaxAdmissibleNs = kReferenceNs / kRegressionFactor = 2 133 333.3`.
- **FR-081** **The projection, and why OQ-1 exists.** Per-voice cost is projected from the
  *measured, P-core-pinned, isolated* figures each phase recorded in its own `compliance.md`
  (ns per 512-sample block at 48 kHz):

  | Sub-component | Measured ns/block | Source |
  |---|---:|---|
  | `HarmonicCloud` | 33 173.5 | `specs/seraphis-phase7-voice-engine/compliance.md:174` |
  | `NoiseOrganism` | 152 888.4 | `specs/vorago-phase2-noise-organism/compliance.md:51` |
  | `ResonanceDriftNetwork` | 52 657.4 | `specs/vorago-phase3-resonance-drift/compliance.md:27` |
  | `FeedbackEcology` | 95 679.4 | `specs/vorago-phase5-feedback-ecology/compliance.md:110` |
  | `BloomEngine` | 503.2 | `specs/vorago-phase7-harmonic-bloom/compliance.md:213` |
  | `EcosystemEngine` | 29 074.0 | `specs/vorago-phase8-ecosystem/compliance.md:62` |
  | `ContinuousBody` × 2 | 105 206.0 | `specs/seraphis-phase7-voice-engine/compliance.md:174` (52 603 each) |
  | `AtmosphereEngine` | 113 298.0 | same — **relocated to global by the OQ-1 ruling below; not part of the per-voice figure going forward** |
  | envelope + spatial + M/S | 5 213.0 | same |
  | **per voice (pre-ruling, isolated components)** | **587 692.9** | **5.51 % of one core** |

  Global (pre-ruling total, before `AtmosphereEngine` relocates here): `SubharmonicEngine`
  **19 591.6** (0.18 %; the ruled, checked-in figure —
  `specs/vorago-phase6-subharmonic/compliance.md:191` records arm (i) "the whole engine at defaults"
  and `dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp:837` carries it as
  `constexpr double kBaselineDefaultsNs = 19591.6;`), `SpectralSmear` ≤ 53 333 (its 0.5 % ceiling,
  roadmap line 261), `CavernVerb` 205 107 (`specs/vorago-phase9-cavern-space/compliance.md`),
  output stage ≤ 20 000 → **298 031.6 ns (2.79 %)**. **Post-ruling** (with `AtmosphereEngine`'s
  113 298 relocated here per FR-056): **411 329.6 ns (3.85 %)** — the "L-1" row's `G` below.

  **The solve, in two columns.** Assumption 3 states that the composition's measured cost may
  exceed the bare sum by SC-003's overhead factor, so a projection that ignores it is not the
  planning instrument it claims to be. Both columns are given; the right-hand one is the one a
  ruling should be taken against.

  | | bare sum (`V = 587 693`) | with SC-003 overhead (`V = 587 693 × 1.15 = 675 847`) |
  |---|---:|---:|
  | `N × V + 298 032 ≤ 3 200 000` (`kReferenceNs`) | N ≤ **4.94** → **4** | N ≤ **4.29** → **4** |
  | `N × V + 298 032 ≤ 2 133 333` (`kMaxAdmissibleNs`) | N ≤ **3.12** → **3** | N ≤ **2.71** → **2** |

  **Roadmap line 470's "8 voices everything-on ≤ 30 %" is therefore not reachable as written**, and
  roadmap Open Question 5 (line 588) is exactly the decision that resolves it.

  **OQ-1 partial ruling (Clarifications session, 2026-09-17).** Two of the three OQ-1 sub-decisions
  are now taken, narrowing what remains: **L-1 is applied** — the ghost/atmosphere tap is global
  (FR-056) — and **L-2 is rejected** — the ecosystem stays **per-voice** (roadmap line 390's agent
  counts stand, FR-002). The baseline the remaining levers are measured against is therefore the row
  the table below already labels "L-1": `V = 474 395`, `G = 411 330`. Rows in the table below that
  additionally apply L-2 are **moot**, kept only as the historical record of the option that was
  rejected; the live ladder from here is baseline (= the "L-1" row) → +L-3 → +L-4, which the plan
  stage recomputes against **FR-082's measured figures**, not this projection. **Only the shipped
  polyphony (OQ-1 (a)) remains open** (see Open Questions).

  The ordered levers, applied in this order before any budget is renegotiated:
  **L-7** *(A-5 — **the ladder's first step**, despite its number: it is appended to the list and
  the list is not renumbered)* **lower `kMaxVoices`**. `kMaxVoices` is a **budget number, not a free
  ceiling** (plan B-6): the CPU solve is `N·V + (kMaxVoices − N)·L + G ≤ budget`, where `L` is a
  non-rendering slot's `advanceLifeOnly` cost (FR-009, FR-046), so every slot above the shipped
  polyphony is paid for on every block whether or not it sounds. Lowering the ceiling is therefore
  the cheapest lever and is tried first. **Bounded below at the shipped polyphony** — a ceiling below
  the number of voices that actually sound is a reduction of the shipped polyphony, which is L-5, and
  L-5's own floor of 4 applies to it;
  **L-1** *(APPLIED — OQ-1 ruling (b), FR-056)* make the ghost/atmosphere tap **global** instead of
  per-voice (roadmap line 588 names this as part of the same question) — removes 113 298 from
  **each** voice but **adds one instance to the global stage**, so the global figure rises to
  411 330; it is a relocation, not a deletion, and the table below accounts for it that way;
  **L-2** *(REJECTED — OQ-1 ruling (c): the ecosystem stays per-voice)* make the ecosystem **global**
  (one habitat per instrument) — same construction: −29 074 per voice, +29 074 global;
  **L-3** drop the two-body blend to one body — saves 52 603 per voice outright, at the cost of a
  roadmap deliverable (line 122's "multi-body blend");
  **L-4** reduce the shipped per-voice source/loop/peak counts (`NoiseOrganism` 4 → 2,
  `FeedbackEcology` 6 → 4, `ResonanceDriftNetwork` 12 → 8) — projected ≈ −125 889 per voice, and
  **this row is a projection of a projection**: FR-082's probe supplies the real figure before it is
  relied on;
  **L-5** reduce the shipped polyphony — **bounded below at 4.** Roadmap line 460 says "4–6 voices"
  ("4–8" before Q-H lowered `kMaxVoices`)
  and roadmap Open Question 5 offers "4, 6, or 8". A polyphony below 4 is **outside the roadmap**
  and is not a lever this spec may pull: it requires an explicit, recorded user ruling amending
  roadmap line 460, taken at L-6, not a quiet reduction inside the build;
  **L-6** **stop and surface** — take the measurement, the lever table and the residual to the user
  and take a ruling. **Never** relax `kReferenceNs`, shrink the measured workload, or raise a
  checked-in baseline (roadmap line 558; the Phase 8 precedent at roadmap lines 375–378).

  **The lever ladder, both columns, against the roadmap's floor of 4.** `V` is per voice, `G`
  global; `N_ref` is the largest N satisfying `N·V + G ≤ 3 200 000`, `N_gate` the largest satisfying
  `N·V + G ≤ 2 133 333`:

  | Levers applied | `V` | `G` | `N_ref` / `N_gate` (bare) | `N_ref` / `N_gate` (×1.15) |
  |---|---:|---:|---:|---:|
  | none (as specified) | 587 693 | 298 032 | 4 / 3 | 4 / 2 |
  | L-1 | 474 395 | 411 330 | 5 / 3 | 5 / 3 |
  | L-1 + L-2 | 445 321 | 440 404 | 6 / 3 | 5 / 3 |
  | L-1 + L-2 + L-3 | 392 718 | 440 404 | 7 / 4 | 6 / 3 |
  | L-1 + L-2 + L-3 + L-4 *(projected)* | 266 829 | 440 404 | 8 / 6 | 8 / 5 |

  **The admissible option set the arithmetic actually supports**, which is what OQ-1 (a) must
  choose from:
  - **N = 4** is reachable at `kReferenceNs` with **L-1 alone**, and at the regression-gated line
    only with **L-1 + L-2 + L-3** (bare) or **L-1 + L-2 + L-3 + L-4** (with overhead).
  - **N = 6** is reachable at `kReferenceNs` with **L-1 + L-2**, and at the regression-gated line
    only with the full **L-1 … L-4** ladder.
  - **N = 8** is reachable at `kReferenceNs` only with the full ladder including the **projected**
    L-4, and is **not** reachable at the regression-gated line in either column on these numbers.
  - **N ≤ 3 is not on the ballot** (L-5's floor, above), so "do nothing and ship 3" is not an
    option this spec offers.

  Every figure above is a projection from isolated measurements. FR-082's probe and SC-001a's
  whole-instrument measurement replace it with real numbers **before** OQ-1 is ruled; the table's
  purpose is to make the ruling a choice from a non-empty, honest menu rather than a hope.
- **FR-082** A **stage-cost probe** (`[.perf]`, WARN-reporting, assertions limited to
  finite-and-positive) measures each voice stage and each global stage **standalone, in the same
  TU** and prints the table, so a lever is chosen from measurement and not from guesswork. This is
  the Phase 3 FR-060 / Phase 8 pattern, and it runs **before** the polyphony ruling is taken. The
  figures are standalone by construction — this spec declares no in-situ per-stage timing hook — so
  the probe **reports** the breakdown (SC-002) and the composition overhead is gated once, by
  SC-003, at 1.15× (FR-081's × 1.15 column).
- **FR-083** Every checked-in perf baseline is `⌈measured × 1.05⌉` transcribed from a clean,
  P-core-pinned, alone, post-idle run via `node tools/run-cpu-tests.js dsp_systems_tests`, with a
  `static_assert` in the TU pinning the transcription (baseline = ⌈measured × 1.05⌉) **and** a
  `static_assert` pinning the baseline against the gate the criterion is actually taken at. A
  baseline above `kMaxAdmissibleNs` means the phase is over budget and FR-081's ladder applies.

  **Amended 2026-09-19 by Q-H, which this sentence completes.** Q-H ran FR-081's ladder, ruled
  shipped polyphony 4 with `kMaxVoices = 6` and **reformulated SC-001b clause (ii)**: the gate is
  measured ≤ `kReferenceNs` AND measured ≤ baseline × `kRegressionFactor`, and the baseline **may
  sit above `kMaxAdmissibleNs`**, with the regression headroom that remains recorded beside it.
  The original `static_assert(baseline <= kMaxAdmissibleNs, …)` is therefore **not** the assertion
  this phase ships — it asserts a clause the ruling replaced, and at
  `kEngineBaselineNsAtPoly4 = 2 694 479` against `kMaxAdmissibleNs = 2 133 333` it would fail the
  build on a figure the user has already ruled admissible. The shipped assertions are the three
  SC-001b names: baseline = ⌈measured × 1.05⌉, baseline + Cavern ≤ `kReferenceNs`, headroom > 1
  (`dsp/tests/unit/systems/vorago_perf_test.cpp:257-273`). The ladder was applied, not skipped;
  what changed is which inequality the baseline is asserted against, and only for a phase whose
  ruling names the replacement. A phase without such a ruling still ships the
  `baseline <= kMaxAdmissibleNs` form.
  **No full-instrument baseline may be checked in before SC-001b exists**, i.e. before the OQ-1
  ruling has been recorded in this spec as an amendment naming the polyphony, the lever set and the
  `kReferenceNs` the gate is taken against. A baseline transcribed against an unruled configuration
  pins the wrong workload and is the one way FR-081's ladder gets skipped in practice.
- **FR-084** **Registration, day one:** every new TU **except the composed-chain case** (FR-084a) is
  added to the enumerated `add_executable(dsp_systems_tests` list (`dsp/tests/CMakeLists.txt:324`),
  with a comment block in the Vorago-Phase-2/3 style naming which criteria each TU carries; the three
  new headers are added to `dsp/lint_all_headers.cpp` (the Phase 9 precedent is `:198`);
  `node tools/gen-specs-index.js` is re-run. A TU that is not in the list never runs.
- **FR-084a** **The composed-chain test's target is `dsp_effects_tests`, decided (OQ3).** The case
  exercising `engine.processStereoBlock` → `CavernVerb` → `engine.processOutputStage` is a **single
  dedicated TU**, registered with `dsp_effects_tests` **beside `CavernVerb`'s own tests**, so it
  compiles under that target's `KRATE_DSP_AETHER_TEST_HOOKS` definition **consistently with every
  other TU linked into the same executable** — the ODR reasoning that applies whenever a compile
  definition **changes a class definition**. (Q-C/A-11 removes the symmetric `dsp_systems_tests`
  case: FR-087's probe is a forward-declared friend, which changes no class definition, so **this
  phase adds no `target_compile_definitions` line anywhere** and `KRATE_DSP_AETHER_TEST_HOOKS` —
  which already exists on `dsp_effects_tests` — is the only such definition in play.) It is the
  **only** Phase 10 TU registered there. **Every other** Phase 10 TU (voice, engine, macro-matrix, dark-material, budget and
  determinism cases) registers with `dsp_systems_tests` (FR-084) and includes **no** `effects/`
  header, mirroring the production code's Layer 3 discipline (AR-1).
- **FR-085** Multi-minute render cases carry the `[long]` tag **only** when they cost > ~15 s **and**
  their failure mode is toolchain-independent. NaN/Inf-guard, bounded-grid and determinism cases
  **must not** be tagged `[long]` (project rule, `CLAUDE.md`). Perf cases carry `[.perf]` and are run
  alone via `node tools/run-cpu-tests.js`.

  **Three criteria in this spec are cross-platform sentinels wearing a soak's clothes, and they are
  split rather than tagged.** Each has a small **per-push, untagged** arm and an exhaustive `[long]`
  arm, and the build **may not re-merge them**:

  | Criterion | Per-push arm (untagged) | `[long]` arm |
  |---|---|---|
  | SC-013 (bounded grid + NaN/Inf guard) | **SC-013a**: 32 seeded configurations × 2 s | **SC-013b**: the remaining 968 × 10 s |
  | SC-021 (sample-rate sweep) | **SC-021a**: 48 kHz + 192 kHz × 1 s | **SC-021b**: 44.1 / 88.2 / 96 / 176.4 kHz × 10 s |
  | SC-004 (soak) | **SC-004a**: 60 s full-poly render — finite, `\|out\| ≤ 1.0`, `getNonFiniteRecoveryCount() == 0`, `getAllocatedBytes()` unchanged | **SC-004b**: the 8 h-equivalent soak |

  The split is not a convenience: a `[long]`-only boundedness case is excluded from the per-push
  lane and its Linux/macOS failures surface a day late, which is precisely the failure mode the
  project rule names. SC-006 (determinism), SC-009 (neutral identity), SC-026 (seed reproducibility)
  and SC-029 (non-finite containment) are untagged in full.
- **FR-086** **"Accelerated" is defined once, here, and nowhere else.** Several criteria assert
  properties of a render longer than a test may take. The **only** permitted acceleration is
  **scaling the event and lifecycle clocks**; everything in the audio path renders every sample at
  the shipped rate. Normative:
  - **Permitted to scale**, by one stated factor `A` shared by the whole render:
    `SlowEventScheduler::setIntervalRange` (`slow_event_scheduler.h:238`),
    `GrowthEnvelope::setDuration` (`growth_envelope.h:144`),
    `EcosystemEngine`'s step interval (`ecosystem_engine.h`, `kDefaultStepIntervalChunks`),
    `BloomEngine`'s lifecycle durations, `TidalModulator::setRate` and
    `BreathingModulator::setRate`, and the voice envelope's stage times.
  - **Forbidden to change**: `kControlChunkSamples` (FR-007 fixes it at 64, matched to eight
    components — raising it changes the system under test), the sample rate, the block size, the
    polyphony, any macro value, and any gain, mix, damping or feedback parameter.
  - **`A` and the resulting wall-clock cost are stated in the test and recorded in
    `compliance.md`**, in the shape Phase 8 used (`specs/vorago-phase8-ecosystem/spec.md:1198`
    records "2 700 000 control steps per seed" so the cost is on the record).
  - **A property that does not survive acceleration may not claim it.** Statistics over a *rendered
    audio* trajectory — SC-004b's block-RMS and SC-005's centroid series — are asserted on an
    **unaccelerated** render in the `[long]` lane, with the real wall-clock cost recorded. Only
    criteria whose property is about *event counts, accounting and boundedness* — SC-013b, SC-014,
    SC-018, SC-019, SC-020, SC-020a **and SC-027** — may use `A`.

    **SC-027 is in that list, corrected here.** SC-027's own text has always read "over a 10-minute
    render (accelerated per FR-086)", and its clause 2 counts **ghost burst edges on a control
    lane** — an event-count property, the family this enumeration admits, not a statistic over a
    rendered audio trajectory. Its omission from the list was a transcription gap between the two
    paragraphs, not a decision: at the shipped ranges (fast 20–90 s, slow 3–10 min, one family in
    five being the ghost) 600 s of instrument time carries about 2.5 expected ghost events against
    a **≥ 6** bar, so the criterion is unreachable without the acceleration its own sentence grants.
    `vorago_engine_test.cpp` flagged the inconsistency in-place; this is the resolution.

  Phase 8's own acceleration is **not** transferable: there it meant calling
  `EcosystemEngine::processChunk` with no audio at all. Phase 10's criteria assert properties of
  rendered audio, which cannot be produced without rendering the samples.
- **FR-087** **The non-finite fault-injection hook is declared, not improvised.**
  `detail::VoragoEngineNonFiniteProbe` writes a non-finite value into one voice's audio state so
  FR-072's containment path can be exercised (SC-029). **It is a `detail`-namespace friend struct
  forward-declared in `vorago_engine.h` and defined in `vorago_nonfinite_test.cpp`, in the shape
  `seraphis_engine.h:193-195` uses — no compile definition is added, because the class definition
  does not change.**

  **(A-11)** The probe's shape is carried as **Q-C** (Clarifications, 2026-09-17), and the sentence
  above is that ruling written in. The earlier wording specified a target-wide
  `KRATE_DSP_VORAGO_TEST_HOOKS` definition on `dsp_systems_tests`, in the
  `dsp/tests/CMakeLists.txt:547-555` / `KRATE_DSP_AETHER_TEST_HOOKS` shape; that reasoning applies
  only when the hook **changes the class definition**, which a forward-declared friend does not, so
  the ODR hazard it guarded against does not arise and **no `target_compile_definitions` line is
  added anywhere by this phase** (plan B-4). The shipping build still has no way to call the probe —
  the library never defines it — and `dsp_lint_stub` still does not see it.

### FR-090 series — Defaults

- **FR-090** A **complete numeric default table** is pinned in the spec and reproduced in the
  headers: every `VoragoVoiceConfig` field, every `VoragoEngineConfig` field, every voice-level
  setter's shipped default, every sub-component setting the voice or engine overrides at `prepare()`
  (and, for each, the shipped component default it replaces and why), and every macro's neutral.
  Success criteria whose thresholds are derived from a default are re-derived against this table.
  The table is authored at plan stage from the shipped component defaults cited in this spec; it is
  normative once written.

  **Three blocks of it are already pinned in this spec and the plan may only reproduce them, never
  re-derive them:** FR-017's seven ghost-configuration values, FR-061's twelve macro neutrals, and
  FR-014's three envelope numbers (attack 20 s, stage range 30–60 s, release 45 s). The plan
  authoring a different number for any of the three is a defect, not a refinement.

  **The table is the source of `kRows`' `base` column** (FR-064): for every `Voice`- or
  `Engine`-owned macro target, the FR-090 row **is** the row's `base`, and SC-009's per-row check
  reads it back from the getter after `prepare()`. A default that changes must change both, in the
  same commit.
- **FR-091** Any default that differs from the owning component's shipped default carries a
  one-line justification naming the drone-scale reason (e.g. "grain length 8 s, not the shipped
  0.1 s: a ghost is a memory of the drone, not a texture").

---

## Success Criteria

Each criterion names the metric, the threshold and the test that measures it. Test names are
sketches; the plan fixes them.

> **Evidence ledger for the `[.perf]` and `[long]` criteria (2026-09-21).** Those cases assert
> wall-clock or render for hours, so they are measured ONCE, alone and P-core-pinned, by the main
> loop — never re-run inside the workflow, where three verifiers share the machine and every CPU
> figure reads red on untouched code. Compliance rows for SC-001a, SC-001b, SC-002, SC-003, SC-004b,
> SC-005, SC-008, SC-013b, SC-015, SC-016 clause 3, SC-018–SC-021b and SC-025 quote these logs
> under `specs/vorago-phase10-voice-engine/artifacts/` (local, uncommitted, like every earlier phase):
> `systems-long-and-perf-lane-2026-09-21.log` (the full `[long]` + `[.perf]` systems lane, 4 h 03 min;
> its CPU figures are HOT and superseded), `systems-perf-only-cooled-2026-09-21.log` (`[.perf]` minus
> `[long]`, after a restart and a 15-minute idle — the SC-001a figures the baseline is transcribed
> from), `sc001b-cpubudget-alone-2026-09-21.log` (SC-001b alone), `effects-long-and-perf-lane-2026-09-21.log`,
> `systems-perpush-2026-09-21.log`, `effects-perpush-2026-09-21.log`, and `sc016-c3-paired/` (seven
> Seraphis pairs and three ContinuousBody pairs, pre-change worktree at `ad7481f6` vs this tree).

- **SC-001 — Full-polyphony CPU (roadmap line 470).** FR-081's arithmetic shows this criterion has
  **no satisfiable option at spec time**: at `V = 587 693` and `G = 298 032` the regression-gated
  line admits 3 voices (2 with SC-003's overhead) and `kReferenceNs` admits 4, while OQ-1 (a) offers
  4, 6 or 8 and L-5's floor forbids 3. A single criterion that is **known red before a line is
  implemented** is not a criterion, so it is split into a measurement that always runs and a gate
  that is fixed by the ruling.

  **The parent form is retired, and the roadmap now says so (amended 2026-09-22).** Roadmap line
  470 read "8 voices everything-on ≤ 30 % of one core @ 48 kHz". Q-H ruled `kMaxVoices = 6`, so
  **8 voices is not a configuration this instrument can be put into** and the sentence cannot be
  measured, let alone met — SC-001a still *surveys* polyphony 8 by construction, but only through
  the arithmetic projection, and the survey reads 43.4 % of one core already at polyphony 6. That
  is a roadmap amendment following a ruling the roadmap itself records two bullets earlier
  (`specs/Vorago-roadmap.md`, the `VoragoEngine` bullet), **not** a threshold move: the 30 %
  ceiling is untouched and is met at the ruled polyphony (2 690 670 ns/block with the Cavern term
  = 84.1 % of `kReferenceNs` = 25.2 % of one core). Compliance for SC-001 is therefore read off
  **SC-001a and SC-001b**, which are the criterion; the parent line is a pointer to them.

  - **SC-001a — measure and report (runs unconditionally, gates nothing).** With `kMaxVoices`
    voices sounding, every sub-component enabled at the FR-090 defaults, macros at neutral, and the
    composed chain including `CavernVerb`: measure ns per 512-sample block at 48 kHz, at **every**
    polyphony in {1, 2, 4, 6, 8} — with the OQ-1(b)/(c) architecture (ghost/atmosphere global,
    FR-056; ecosystem per-voice, FR-002) already fixed, so only the polyphony axis is swept — and
    print the figure, its percentage of one core, the FR-082 per-stage breakdown and the FR-081
    lever ladder re-computed from the **measured** `V` and `G`.
    Assertions limited to finite-and-positive; everything else is `WARN`. This is the artefact OQ-1
    is ruled from. `VoragoEngine_CpuSurvey` `[.perf]`.
  - **SC-001b — the gate (ruled 2026-09-19, Q-H; written before any baseline is checked in).** At
    the **ruled** shipped polyphony **4** with `kMaxVoices = 6` and **no voicing levers**: measured
    ns per 512-sample block at 48 kHz **≤ `kReferenceNs` (3 200 000)** AND **≤ the checked-in
    baseline × `kRegressionFactor` (1.5)**, where the baseline is ceil(clean measurement × 1.05).
    The earlier clause (ii) — baseline ≤ `kMaxAdmissibleNs` (2 133 333) — is **not met** at
    polyphony 4 (measured 2 704 220 on the warm machine, 84.5 % of the reference) and is replaced
    by ruling: the ceiling binds before the 1.5× line does, and the headroom actually available is
    recorded next to the baseline. Best-of-25 × 500 blocks, P-core-pinned, run alone, after a
    restart and cool-down.
    The admissible (polyphony, lever set) pairs are enumerated in FR-081's option set; a ruling
    outside it is a roadmap amendment, not a spec edit. **No baseline may be checked in before this
    amendment exists** — that is the FR-083 discipline applied to the one criterion that cannot
    state its own threshold yet. `VoragoEngine_CpuBudget` `[.perf]`.
    **Baseline transcribed 2026-09-21 (Q-H):** engine 2 566 170 ns/block at polyphony 4, cooled
    protocol; `kEngineBaselineNsAtPoly4 = 2 694 479` = ⌈2 566 170 × 1.05⌉, with the Cavern term
    added arithmetically in clause (i) only. Baseline + Cavern = 2 818 976 > `kMaxAdmissibleNs`, so
    the original clause (ii) is not met and the available headroom (1.141×) is recorded in the TU
    next to the baseline. Three `static_assert`s pin it: baseline = ⌈measured × 1.05⌉ of the
    transcribed measurement, baseline + Cavern ≤ `kReferenceNs`, headroom > 1.
- **SC-002 — Per-stage cost breakdown (measurement, not a closure gate).** The FR-082 stage probe
  measures each stage **standalone, in the same TU** — cloud, noise, resonance, ecology, bloom,
  ecosystem, body A, body B, envelope/mix for the **voice**; voice sum, **atmosphere** (A-6: OQ-1
  ruling (b) moved `AtmosphereEngine` to `VoragoEngine`, FR-056, so its cost is a global stage paid
  once, not a per-voice stage paid `N` times), subharmonic, smear, output stage for the **engine** —
  and prints the table with each figure's share of the directly
  measured whole. **Assertions are limited to finite-and-positive; the breakdown is `WARN`-reported
  (FR-082).**

  The earlier "sum within 5 % of the whole" clause is **deleted**, and deliberately: this spec
  declares no in-situ per-stage timing hook (the *New components* table lists only
  `detail::VoragoVoiceSilenceRampProbe` and `detail::VoragoEngineNonFiniteProbe`), so the figures
  are necessarily **standalone** — which makes a 5 % closure bound the same statement as SC-003's
  composition bound, only tighter, and the two would contradict each other: 5 % forces
  `whole ≤ 1.053 × sum` and a voice legitimately passing SC-003 at 1.10 would fail SC-002. The
  composition overhead is gated **once**, by SC-003, at 1.15. Seraphis's precedent is the same
  standalone construction: `specs/seraphis-phase7-voice-engine/compliance.md:174` records "sum of
  eight = 216942 ns/block; SeraphisVoice = 226562 ns/block; ratio = 1.04434".
  `VoragoVoice_StageCostProbe` `[.perf]`.
- **SC-003 — Composition overhead.** `VoragoVoice` costs no more than **1.15 ×** the sum of its
  FR-002 sub-components measured standalone in the same TU (Seraphis's bound is 1.1 and it passed
  with 1.5 % margin, `specs/seraphis-phase7-voice-engine/compliance.md:144`; Vorago's voice has more
  inter-stage buffer traffic, hence 1.15). `VoragoVoice_CompositionOverhead` `[.perf]`.
- **SC-004 — Soak, bounded (roadmap lines 471, 553–555).** Split per FR-085.
  - **SC-004a — per-push, untagged.** A 60 s full-polyphony render with one held note: every sample
    finite, `|out| ≤ 1.0`, `getNonFiniteRecoveryCount() == 0`, `getAllocatedBytes()` unchanged from
    immediately after `prepare()`. This is the cross-platform sentinel; it must not ride inside a
    `[long]` soak. `VoragoEngine_SoakSentinel`.
  - **SC-004b — the 8 h soak, `[long]`, unaccelerated** (FR-086: block-RMS trajectory is not a
    property that survives clock scaling). Full polyphony, one held note. The RMS bound is **scoped
    to a settling window**, because the spec's own requirements make an unscoped bound red on a
    correct implementation: FR-003 ends `prepare()` with `reset()` "so a freshly prepared voice is
    silent" (block 0 RMS is −∞ dBFS) and FR-014 requires `Standard` envelope attack and stage times
    **in the tens of seconds**, so the opening minute is far below −60 dBFS *by design*.
    - **Settling window** `T_settle` = the FR-090 `Standard`-envelope attack + first stage time, plus
      the longest bloom fade-in and the atmosphere capture fill time, each cited to its FR-090 row,
      **rounded up to the next whole minute** and stated numerically in the test.
    - **During** `[0, T_settle)`: block RMS is asserted only to be **monotone non-decreasing on a
      10 s moving average** from silence — a rise, not a bound — with a stated tolerance of
      **0.1 dB** between consecutive averages (Q-I).
    - **After** `T_settle`: block RMS stays within **[−60 dBFS, −6 dBFS]** for the remainder of the
      render, and the RMS of the **last** 60 s is within **±6 dB** of the RMS of the 60 s window
      starting at `T_settle + 300 s` (neither dies nor runs away).
    - Throughout: no non-finite sample, `getAllocatedBytes()` unchanged, `getNonFiniteRecoveryCount()
      == 0`. `VoragoEngine_OvernightSoak` `[long]`.
- **SC-005 — 8 h soak, non-static (roadmap line 471).** Over the same unaccelerated render, and
  measured **after** SC-004b's `T_settle`: the spectral centroid sampled every 30 s has a
  coefficient of variation **≥ 0.05** and its autocorrelation at every lag from 60 s to 30 min stays
  **below 0.9** (no frozen point, no short limit cycle).

  **The population is stated, because "population mean" is otherwise undefined here.** Phase 8's
  ruling (`specs/vorago-phase8-ecosystem/compliance.md:112`) moved *its* SC-002/SC-005 from
  per-agent to a mean over the **agent population** of `getAgentOutput`; SC-005's per-window
  statistic is a single scalar (one window's centroid) and has no such population, so that citation
  does not transfer and is dropped. Instead: **three engine seeds**. The CV and the autocorrelation
  are computed **per seed, on that seed's own 30 s centroid series**; the criterion gates the
  **mean across the three seeds**, and each seed's own value is additionally recorded in the output.
  Averaging the centroid *series* across seeds is forbidden — it suppresses exactly the variation
  being measured. `VoragoEngine_OvernightEvolution` `[long]`.
- **SC-006 — Determinism harness (roadmap line 473).** Two engines with the same seed, configuration
  and note sequence, rendered for 60 s: `compareFingerprints` **within tolerance** at the shared
  constants (`render_fingerprint.h:55-61`). Two engines whose **only** difference is the seed:
  `worstMetricRelativeError` **> 100 ×** `kMetricTolerance` (they genuinely differ). No bit pattern
  is stored. `VoragoEngine_DeterminismHarness`.
- **SC-007 — Partition invariance, asserted as exactly as FR-007 requires it.** Rendering 4096
  samples as one call, as 8 × 512, and as the pathological split `36, 28, 1, 2047, …` produces
  **bit-identical output buffers** (`std::memcmp` over the whole render, or an exact element-wise
  compare), and **exactly equal** `EcosystemEngine::getControlStepCount()`
  (`ecosystem_engine.h:1001`) and `getActiveVoiceCount()`.

  Fingerprint tolerance is **not** used here. FR-007 demands exactness — "must leave identical state
  and produce identical output" — and both arms are the same build in the same process, where exact
  equality is achievable; `render_fingerprint.h`'s tolerances exist for cross-toolchain spread, and
  at `kMetricTolerance = 2.5e-4` a genuine partition-dependent drift (a control step counted per
  *call* rather than per 64 elapsed *samples*, a smoother advanced by `slice` instead of by the
  chunk) stays comfortably inside them on a slowly-evolving drone and passes. This is a run-to-run
  identity inside one process, not a stored golden, so the no-goldens rule is untouched; fingerprint
  tolerance is reserved for SC-006's cross-run determinism arm.
  `VoragoVoice_PartitionInvariance`, `VoragoEngine_PartitionInvariance`.
- **SC-008 — Macro sweeps render-verified along documented axes (roadmap line 472).**

  **Fixture.** For each of the twelve macros: **five sweep points** — 0, 0.25, 0.50, 0.75, 1 — ×
  **three engine seeds**, each a 60 s render, with every other macro at its FR-061 neutral. Each
  render uses the **FR-014a `kFastAttackEnvelopeConfig`** fixture, not the shipped 20 s-attack
  `Standard` default (Q5), so the `[10 s, 60 s]` window (and the folded-concept and non-silence
  clauses below, which share it) measures the macro's effect rather than the envelope's own attack.
  (`Gravity`'s five points are read against its bipolar neutral of 0.5, so its sweep spans air →
  stone.) Two points cannot establish monotonicity — they establish only a direction — and against a
  stochastic instrument (seeded ecosystem, slow events, brownian drift) an unthresholded inequality
  on a single seed pair is a coin flip that passes on noise and would not catch an unrouted or
  wrong-signed row, which is the failure mode this criterion exists to prevent.

  **Assertion, per macro (one per row):** the **Spearman rank correlation** of the primary metric
  against macro value, computed per seed and averaged over the three seeds, is
  **≥ 0.9 in magnitude and of the documented sign** — that is what makes "monotonically" testable —
  **and** the endpoint difference between value 0 and value 1 meets the row's numeric threshold.
  Every row carries a threshold; none is a bare inequality.

  | Macro | Primary metric | Direction, 1 vs 0 | Endpoint threshold |
  |---|---|---|---|
  | Darkness | **energy ratio above vs below 4·f0** (A-3; was spectral centroid) | lower | ≥ 6 dB (A-3, derived) |
  | Age | HF band energy (> 4 kHz) | lower | ≥ 3 dB |
  | Density | active partial count + awake noise-source count, summed | higher | ≥ 50 % |
  | Movement | per-band energy total variation | higher | ≥ 20 % |
  | Gravity | mean \|log2(partial ratio) − nearest integer\| | lower | ≥ 30 % — **recorded as failed (A-3): measured 18 %; the shipped keyed ratio table caps the theoretical endpoint at 24 %** |
  | Entropy | spectral flatness | higher | ≥ 25 % |
  | Pressure | crest factor | lower | ≥ 3 dB — **recorded as failed (A-3): measured 0.07–0.16 dB; saturation at its ceiling barely moves the crest of a sub-plus-fundamental beat** |
  | Weight | energy below 80 Hz | higher | ≥ 6 dB |
  | Fog | **spectral flatness over the harmonic band [0.94·f0, 20·f0]** (A-3; was per-bin flux, which the smear stage itself raises) | higher | ≥ 10 % (A-3, derived) |
  | Life | wake/sleep edges per minute, counted over the five routed destination families | higher | ≥ 2 × |
  | Depth | **post-note-off tail (A-3, Q-O; replaces A-1's level ratio)**: seconds after note-off until the composed chain's 100 ms mean-square envelope first falls 30 dB below its last-second-before-note-off level, note **C4**, hold 10 s, fast-attack fixture, capped at 120 s | higher | ≥ 3 s (A-3, derived from a mean measured endpoint of +6.4 s) |
  | Mass | **energy below 80 Hz at C3** (A-3; the modal-band share was capped near 0 dB because the subs never sit in body A's mode bands) | higher | ≥ 0.25 dB (A-3, derived) — **recorded as failed (Q-Q): with the subs fully tracked every Mass row thins the bus; measured −0.35 to −0.45 dB** |

  The `Depth` and `Mass` metrics are stated this way because their earlier forms were **not
  observable**: "late/early energy ratio" is an impulse-response statistic and there is no impulse
  and no defined `t = 0` on a continuous 60 s drone, and "body-stage energy share" needs a per-stage
  tap that FR-047 does not expose and FR-010's chain does not survive past one control step. Both
  replacements are computed **at the engine's own output**, which is the only thing the product
  exposes.

  **A-1 — why the `Depth` metric is a level ratio and not a wet/total share.** The earlier form
  expressed the same measurement as "a ratio of the wet path to the total". That share is
  **saturated at both endpoints** and therefore measures nothing on this product: FR-063 duplicates
  `CavernVerb`'s `kDefaultMix = 1.00f` (`cavern_verb.h:264`) and `setMix`'s own doxygen defines the
  control as the wet/dry blend at full wet by default, so the wet share is 1.0 at macro 0 and 1.0 at
  macro 1 and the endpoint difference is 0 dB by construction. The **dB ratio of the two renders'
  RMS** — composed, versus the same render with `CavernVerb::setMix(0)` — is the same physical
  quantity expressed on a scale the product can actually move. **The ≥ 6 dB endpoint threshold, the
  `[10 s, 60 s]` window, the direction and the Spearman ≥ 0.9 requirement are unchanged.**

  **A-2 — the `Mass` row stands unchanged, and the deletion next to it is deliberate.** `Mass`'s
  metric (energy in body A's modal band relative to broadband) and its **≥ 4 dB** endpoint threshold
  are **not amended**. What changed is in `kRows`, not here: the plan **deleted the
  `Mass → BodyBlend` row**, because biasing the two-body blend `b` toward body B moves energy out of
  the band the metric measures on body **A** and therefore **opposes** the metric — a row that would
  have made a correct implementation red. The row's absence is a deliberate design decision, not an
  omission to be repaired by a later reader; `Mass`'s remaining rows carry it. `Weight`'s own
  blend clause (FR-068) is unaffected: it is a different macro with a different metric (energy below
  80 Hz), which the blend does not oppose.

  **A-3 — the first full sweep (2026-09-19, Q-N / Q-O / Q-Q) and what it changed.** Measured on
  the built instrument, the output at C1 is a near-sinusoid (the bodies leave harmonics ≥ 35 dB
  down) with its magnitude mass in the sub tones and the fundamental, so three metrics could not see
  their concept and were replaced (Darkness, Fog, Mass, above), and the composed Depth metric was
  replaced because the drone's energy sits below the cavern FDN's ~40 Hz DC-blocker corner where
  decay time is invisible in RMS. Thresholds marked *derived* are half the mean measured endpoint
  rounded to a clean figure, recorded with the measurement in compliance.md. Measurement
  constructions that changed (all still the named metric): Age and Weight/Mass are measured at
  **C4 / C3** (at C1 the 18-partial cloud tops out at 1.2 kHz and the fundamental owns the < 80 Hz
  band); Life renders **210 s** and counts edges over **[90 s, 210 s]** (every scheduler draws its
  first onset from the unscaled 20–90 s range at prepare); Entropy's flatness is taken over the
  **harmonic band [0.94·f0, 20·f0]** (the full band is pinned by ~3 400 empty bins). Rows retuned in
  `kRows`: Life → EventRateScale sign corrected (−0.65 → +9.0; the voice divides the interval
  ranges by the scale); Movement raised to the component ceilings; Fog tilt row made a claim row
  and ghost amount 0.4; Mass resonance-mix −0.30 and the sub-tracking row a claim row (Q-Q). Three
  rows are **recorded as failed** with their measured tables, thresholds untouched: Gravity,
  Pressure, Mass — a Phase 12 product decision. The case asserts every row with `CHECK` so one run
  prints every verdict, and renders the sweep once.

  **Folded-concept clauses (FR-061, FR-068), asserted as additional rows on the same fixture:**
  - **Age** — cavern decay target (`VoragoCavernTargets::decaySeconds`) at macro 1 is **shorter**
    than at macro 0, and body damping read back from `ContinuousBody::getDamping()` is **higher**.
    (Carries `Decay`'s shortening half.)
  - **Depth** — `decaySeconds` at macro 1 is **longer** than at macro 0, and `CavernVerb::setSize`'s
    target is **larger**. (Carries `Decay`'s lengthening half.)
  - **Entropy** — `HarmonicCloud::getMutation()` and the life-modulator depths read back **higher**
    at macro 1. (Carries `Instability`.)
  - **Fog** — the distance-filtering target reads **more filtered** at macro 1. (Carries
    `Distance`.)

  **Non-silence clause (Edge Cases → parameter extremes), asserted here because it is asserted
  nowhere else:** the render with **every macro at 0 simultaneously** (`Gravity` at 0, i.e. its air
  extreme, not its neutral) has broadband RMS **> −60 dBFS** over the window `[10 s, 60 s]`. **A
  macro set of all zeros is not a mute.** Without this clause an all-zeros mute passes every
  criterion in this spec.

  `VoragoMacro_SweepAxes` `[long]`.
- **SC-009 — Macro neutrality (FR-064, FR-066).** Three clauses.
  1. **Per-row base check (the mechanical one that makes the rest true).** For **every** row of
     `kRows` whose owner is `Voice` or `Engine`: `row.base` equals the value read back from that
     target's getter on a voice/engine **immediately after `prepare()`**, exactly. This is what
     catches an FR-090 default and a `kRows` literal drifting apart — e.g. the atmosphere-density
     case FR-064 names, where a row based on the component's shipped 4.0 would write the ghost
     configuration away on the first `apply()`. For `Cavern`-owned rows, `row.base` equals the
     corresponding `VoragoCavernTargets` field default (FR-063's duplicated literals).
  2. **Arithmetic inertness.** With every macro at its FR-061 neutral, `apply()` leaves every
     writable target at exactly `base` and `computeCavernTargets()` returns exactly the defaults —
     asserted per target, not only on the render, so a mis-signed row cannot hide behind a
     cancellation.
  3. **Render identity.** With every macro at its neutral, a 10 s render is **bit-identical** to the
     same render with the matrix never applied. (This is an identity between two runs of the same
     build, not a stored golden — the project rule bans stored bit-exact goldens, not run-to-run
     identity.)

  `VoragoMacro_NeutralIsIdentity`.
- **SC-010 — Macro continuity (FR-067).** Automating any single macro from 0 to 1 over 5 s produces
  a render whose maximum per-sample delta inside any 20 ms window on the ramp is **≤ 1.5 ×** the
  same statistic measured 64 ms clear of the ramp (the `seraphis_engine.h:232-244` measurement
  shape). `VoragoMacro_NoZipper` `[long]`.
- **SC-011 — Steal is click-free.** Stealing a sounding voice mid-chunk produces no sample-to-sample
  delta above **1.5 ×** the pre-steal maximum within the steal window, and the stolen voice is
  silent within `kSilenceRampMs`. `VoragoEngine_StealRamp`.
- **SC-012 — Amnesty steal policy (FR-044).** With all voices busy, a `noteOn` steals the voice with
  the lowest level; a voice above `kAmnestyLevelThreshold` is stolen only when every voice is above
  it. Asserted over an enumerated victim table. `VoragoEngine_StealPolicy`.
- **SC-013 — Output bounded for any configuration (FR-073, roadmap line 553).** This is a
  bounded-grid **and** a NaN/Inf-guard case, i.e. a cross-platform sentinel, so it is **split** per
  FR-085 rather than tagged `[long]` in one piece.
  - **SC-013a — per-push, untagged.** **32** seeded random configurations (every macro, every
    exposed setter, every polyphony) × **2 s** renders, **unaccelerated**: `|out| ≤ 1.0` on every
    sample, no non-finite sample, `getNonFiniteRecoveryCount() == 0`.
    `VoragoEngine_ConfigurationFuzzSentinel`.
  - **SC-013b — the exhaustive remainder, `[long]`.** The other **968** configurations × 10 s
    renders, accelerated under FR-086's definition (`A` and the wall-clock cost stated in the test).
    Same three assertions. `VoragoEngine_ConfigurationFuzz` `[long]`.
- **SC-014 — Zero allocation after prepare (FR-070).** `getAllocatedBytes()` is identical
  immediately after `prepare()` and after a 10-minute-equivalent render (accelerated per FR-086;
  allocation accounting is an event-and-accounting property, so it survives clock scaling) that
  exercises every wake/sleep edge, every bloom spawn/retire, every steal and every macro extreme.

  **(A-3) This is the per-push `AllocationScope` arm, and it is not FR-070's whole allocation
  clause.** SC-014 gates the **edge coverage** at 10-minute-equivalent on every push; **SC-004b**
  carries FR-070's **8 h `getAllocatedBytes()`-invariance clause unaccelerated** in the `[long]`
  lane and gates the **duration**. Neither narrows the other, and the build may not merge them:
  10 minutes does not prove 8 h, and an 8 h held note does not walk SC-014's edges.
  `VoragoEngine_NoAllocationAfterPrepare`.
- **SC-015 — Dark materials are darker (FR-035).** Under identical excitation and identical
  resonance/damping settings, each of the six new materials' steady-state spectral centroid is
  **below the minimum** of the five shipped materials' centroids, and each new material's centroid
  is **distinct from every other new material's by ≥ 5 %** (six materials, not one repeated six
  times). This criterion measures **timbre only**; the six materials' **CPU** is a separate,
  non-negotiable requirement measured by **SC-025** against the shipped `continuous_body_perf_test`
  baselines (FR-038b). `ContinuousBody_DarkMaterialsSpectral` `[long]`.
- **SC-016 — Seraphis stays green (roadmap lines 572–574).** After the **FR-030 – FR-039** change
  (A-7: the earlier upper bound named a requirement number this spec does not define; the range ends
  at FR-039), the
  full `dsp_systems_tests` suite passes, including every `continuous_body_*` and `seraphis_*` case,
  with **no Seraphis assertion, threshold, baseline or default modified** other than the eleven
  enumerated `kNumMaterials` sites (FR-038). Five clauses:
  1. **Modification scope.** `git diff --numstat --diff-filter=M dsp/tests/unit/systems/` names
     **exactly** `continuous_body_perf_test.cpp`, `continuous_body_spectral_test.cpp`,
     `continuous_body_test.cpp` and `seraphis_perf_test.cpp` (Q-F added the spectral TU). Every other file this phase touches in that directory is newly
     **ADDED** (`--diff-filter=A`), with zero pre-existing lines changed. The earlier
     `git diff --stat` form was false by construction of this spec — FR-084 requires new Phase 10
     TUs in the same directory, so a bare `--stat` always names them and the check would be loosened
     at build time to whatever passes.
  2. **Header scope.** `git diff --numstat dsp/include/krate/dsp/systems/continuous_body.h` shows
     **exactly two deleted lines**, and `git diff -U0` confirms they are the `BodyMaterial`
     enumerator line and the `kNumMaterials` line and nothing else (FR-039).
  3. **Seraphis's measured subject is unchanged.** `seraphis_perf_test.cpp`'s material survey still
     runs over `kNumSeraphisMaterials` (FR-038a); the pre-change and post-change surveys are
     captured **in one thermal state** (the pre-change tree built from a worktree at the docs
     commit, each survey run alone, back to back after the runner's settle), and the comparison is
     **noise-robust** (Q-K): the worst material stays **within the five Seraphis materials** and no
     Seraphis material's ns/block figure moves by more than **10 %** between the pair. **The
     per-material figures are `ContinuousBody_CpuBudget`'s paired tables (Q-S: best-of-25 × 500,
     steady / operating / crossfade, the same standalone body at the same five materials), captured
     pre and post in alternating order; the Seraphis survey's argmax, its per-material medians and
     the 8-voice headline are reported alongside.** Seraphis's
     SC-001 / SC-002 subject may not be re-pointed at a Vorago material by this phase.
  4. **Seraphis's crossfade pairings are unchanged.** `continuous_body_perf_test.cpp`'s
     `crossfadePartner` still maps Glass→Strings→MetalPlate→Chamber→Ice→Glass for the five Seraphis
     materials (FR-038 site 4); the six new materials cycle among themselves.
  5. **The shipped plugin (Q-F).** `git diff --numstat --diff-filter=M plugins/seraphis/` names
     **exactly** `src/parameters/dropdown_mappings.h` and `tests/integration/param_perf_test.cpp`
     (FR-038 sites 9–11); the `Seraphis` target and `seraphis_tests` build with zero warnings, the
     per-push `seraphis_tests` run is green, and `pluginval --strictness-level 5` passes on the
     rebuilt bundle. ID 800 still reports five steps.

  **(A-13) This gate does not widen to the phase's second shared-component change.** `FeedbackEcology`
  gains `silenceAudio()` (A-13, AR-4's neighbourhood) and has **no Seraphis consumer** — a repo-wide
  sweep finds only Vorago Layer 3 peers, `dsp/lint_all_headers.cpp` and its own five test TUs — so
  no `seraphis_*` case can observe it and there is nothing for this criterion to gate. Its green
  gate is its own `FeedbackEcology*` suites, plus the zero-deletion bar clause 2 applies to
  `continuous_body.h`, applied separately to `feedback_ecology.h`:
  `git diff --numstat dsp/include/krate/dsp/systems/feedback_ecology.h` shows **zero deleted lines**.

  **(Q-G) The envelope ceilings are the phase's third and fourth shared-component changes, and both
  HAVE Seraphis consumers.** `multi_stage_envelope.h` and `growth_envelope.h` each gain a
  per-instance ceiling setter that defaults to the shipped constant; their only non-append effect is
  that the existing clamp lines read the instance ceiling instead of the constant. The bar is
  therefore **exactly the expected deletions**: three `std::clamp(ms, 0.0f, kMaxStageTimeMs)` lines
  in the first, one doc line and one `std::clamp(seconds, kMinDuration, kMaxDuration)` line in the
  second, and nothing else — `tools/check-seraphis-green.js` matches them by pattern. Seraphis's
  envelope parameter ranges still derive from the untouched constants; clause 5's `seraphis_tests`
  run and `dsp_processors_tests`' `MultiStageEnvelope*` / `GrowthEnvelope*` suites are the green
  gate, and the two new `per-instance ... ceiling` cases pin the default-inert behaviour.

  `ContinuousBody_MaterialTableIsAppendOnly` plus a full suite run.
- **SC-017 — Two-body blend endpoints are exact (FR-036).** Restated in terms the product can
  actually reach. There is **no single-body state**: FR-002 makes the voice own exactly two
  `ContinuousBody` instances by value, `VoragoVoiceConfig` (FR-004) has no body-count field, and
  FR-036 says outright that the unused body still runs — so "a single-body render" is not a
  configuration, and comparing against a synthesised bare `ContinuousBody` would compare against a
  different object fed a bus FR-047 does not expose. The endpoints are therefore asserted as
  **contribution nullity**, which is the property FR-036 actually claims:
  1. At `b = 0`, a render with body B set to material **X** is **bit-identical** to the same render
     with body B set to material **Y** (X ≠ Y): body B contributes nothing.
  2. At `b = 0`, a render is **bit-identical** across two different body-B **seeds**.
  3. Symmetrically at `b = 1` for body **A** (both material and seed).
  4. **Ramp history does not leak:** a render at `b = 0` reached by ramping down from `b = 1` is
     within fingerprint tolerance of a render at `b = 0` from the start.

  `VoragoVoice_BodyBlendEndpoints`.
- **SC-017a — The blend ramp is per-sample, not chunk-stepped (FR-037).** Automating `b` from 0 to 1
  over 5 s produces a render whose maximum per-sample delta inside any 20 ms window on the ramp is
  **≤ 1.5 ×** the same statistic measured 64 ms clear of the ramp — SC-010's measurement shape,
  applied to the blend. Without it a chunk-stepped blend (a 64-sample staircase, exactly the zipper
  SC-010 guards for macros) passes SC-017's endpoint and ramp-return clauses untouched.
  `VoragoVoice_BodyBlendNoZipper`.
- **SC-018 — Bloom accounting never exceeds cloud capacity (roadmap line 362).** Over a 30-minute
  render accelerated per **FR-086** (an accounting property, so it survives clock scaling; `A` and
  the wall-clock cost stated in the test), the count returned by `BloomEngine::processChunk` and handed to
  `setSpectralTarget` never exceeds `HarmonicCloud::kMaxPartials`, `reserveBase()` never goes
  negative, and `setSpectralTarget` is never rejected. Asserted every control step.
  `VoragoVoice_BloomSlotAccounting` `[long]`.
- **SC-018a — The spectral-target spawn/retire edge is click-free (FR-011).** Triggering a bloom
  from zero live children (the **spawn** edge, `HarmonicCloud::clearSpectralTarget()` →
  `setSpectralTarget(...)`) and letting a bloom's last child retire (the **retire** edge, the reverse
  transition) each produce no sample-to-sample delta above **1.5 ×** the pre-edge maximum within a
  20 ms window — the SC-010/SC-017a measurement shape, applied to this transition.
  `VoragoVoice_SpectralTargetEdge`.
- **SC-019 — Ecosystem routing is observable (FR-020).** Three clauses, the first of which carries a
  precondition the earlier wording omitted.
  1. **Unmodulated baseline.** With ecosystem depth at 0 **AND every `SlowEventScheduler` depth at
     0** — the only state in which the destinations are unmodulated — the routed destinations read
     **exactly** their configured bases over a 10-minute render, read back through
     `NoiseOrganism::getSourceWakeAmount` (`noise_organism.h:880`),
     `ResonanceDriftNetwork::getPeakWakeAmount` (`resonance_drift_network.h:855`),
     `FeedbackEcology::getLoopWakeAmount` (`feedback_ecology.h:1393`), for the **`Partial`** family
     `HarmonicCloud::getMutation()` (`harmonic_cloud.h:493`) and `BloomEngine::getDepth()`
     (`bloom_engine.h:679`) **(A-9 — the `Partial` family's two destinations were routed by FR-020
     but read back by nothing, so clause 1 asserted its base-equality over four families out of
     five)**, and, for the `Ghost` family,
     `VoragoVoice::getGhostRequest()` (FR-020b — `AtmosphereEngine` itself is engine-owned, OQ-1
     ruling (b); this clause is measured at polyphony 1 so the voice's own request is directly
     observable). The scheduler precondition is
     **required**, not decorative: FR-022 routes the schedulers to four of the same five destination
     families and FR-023 combines the two contributions by maximum, so ecosystem depth 0 alone does
     **not** stop the destinations moving, and a criterion asserting otherwise would be red on a
     correct implementation — or quietly implemented with the schedulers disabled, a state the
     instrument is never in.
  2. **Ecosystem contribution with the schedulers live.** With the schedulers at their shipped
     depths and ecosystem depth at 1, each destination's trace **never falls below** the
     scheduler-only trace recorded on the same seed with ecosystem depth 0 — the FR-023 maximum rule,
     observed on the product in the state it actually runs in. **(A-9) For the `Partial` family this
     arm reduces to "never below base"**: FR-022's five scheduler families are bloom trigger,
     noise-source wake, resonance-peak wake, ecology-loop wake and ghost-burst level — **no
     scheduler family writes `HarmonicCloud::setMutation` or `BloomEngine::setDepth`** — so the
     scheduler-only trace for those two targets is their configured base, and asserting
     "never below the scheduler-only trace" against a family no scheduler addresses would be
     asserting a maximum combine that does not exist there.
  3. **Attribution.** At ecosystem depth 1, each of the five destination families — including the
     `Partial` family, read back through `getMutation()` and `getDepth()` **(A-9)** — shows **≥ 3
     distinct value changes per minute** attributable to the ecosystem (i.e. changes at control
     steps where the scheduler contribution is unchanged; for the `Partial` family every change is
     so attributable, since no scheduler writes it), and each family's driving agent kind is
     confirmed by setting that kind's agents dormant and observing the ecosystem contribution go
     static while the scheduler contribution continues.

  Accelerated per FR-086 (an event-and-accounting property). `VoragoVoice_EcosystemRouting` `[long]`.
- **SC-019a — The FR-023 combine rule is the maximum (per-push).** Over an enumerated table of
  (scheduler contribution, ecosystem contribution) pairs spanning both orderings, both zeros, both
  ones and the equal case, each shared destination reads back **exactly**
  `max(scheduler, ecosystem)` — so neither source can silence a slot the other woke. No render
  needed; read back through the getters cited in SC-019 (1). `VoragoVoice_WakeCombineRule`.
- **SC-019b — The many-to-one reduction is the strongest agent (FR-020a).** Over an enumerated
  table of per-slot agent-energy tuples (several `EcosystemEngine::getAgentEnergy` values addressing
  one destination slot, spanning ties and a clear winner), the slot's routed contribution equals
  **exactly** the output of the argmax-energy agent, never a blend or an average of the addressing
  agents. Untagged, no render needed. `VoragoVoice_AgentReductionRule`.
- **SC-020 — Slow events fire and are heard (FR-022).** Over a 30-minute render accelerated per
  **FR-086** (an event-count property; `A` and the wall-clock cost stated in the test), the
  measured inter-event interval distribution lies inside the configured range, every target index in
  `[0, targetCount)` is selected at least once, and each event produces a measurable change on its
  destination's observable. `VoragoVoice_SlowEventRouting` `[long]`.
- **SC-020a — Slow-event slot draws are deterministic (FR-022).** For a multi-slot destination
  family (noise sources, resonance peaks, ecology loops): two schedulers with the **same** seed and
  configuration draw the **same** slot sequence over a 30-minute render accelerated per FR-086; two
  schedulers differing **only** in seed draw **measurably different** slot sequences (at least one
  differing draw). Untagged (a determinism case, FR-085). `VoragoVoice_SlotDrawDeterminism`.
- **SC-021 — Sample-rate sweep (FR-076).** A boundedness-and-finiteness sentinel, therefore **split**
  per FR-085 rather than tagged `[long]` in one piece.
  - **SC-021a — per-push, untagged.** At **48 kHz and 192 kHz** (the base rate and the extreme that
    has historically broken first), using the **FR-014a `kFastAttackEnvelopeConfig`** fixture (Q5;
    the shipped 20 s-attack `Standard` default cannot be non-silent in 1 s): a **1 s** full-poly
    render is non-silent, finite and bounded, and 192 kHz's broadband RMS is within **±3 dB** of
    48 kHz's. `VoragoEngine_SampleRateSentinel`.
  - **SC-021b — the remaining rates, `[long]`.** At 44.1 / 88.2 / 96 / 176.4 kHz: 10 s full-poly
    renders, same four assertions against the 48 kHz reference.
    `VoragoEngine_SampleRateSweep` `[long]`.
- **SC-022 — Latency is reported correctly (FR-052).** The earlier impulse clause was
  unimplementable against this product: `VoragoEngine` has **no audio input** (FR-050's
  `processStereoBlock(float* outL, float* outR, std::size_t n)` is output-only and renders the voice
  sum), and the only buffer-taking entry point, `processOutputStage` (FR-053), sits **after**
  `SpectralSmear` and cannot observe its latency. Voices are drone generators, not input processors,
  so there is no path by which a test can inject an impulse and measure its arrival. Replaced with
  two clauses measurable on the real object:
  1. **Forwarding equality.** `VoragoEngine::getLatencySamples()` equals the owned
     `SpectralSmear::getLatencySamples()` (`spectral_smear.h:472`), at every prepare-time
     configuration including `enabled = false`, where both are **0**.
  2. **Group delay on the engine's own output.** Two engines, identical seed, configuration and
     note sequence, differing **only** in the prepare-time `SpectralSmear` enable flag — which
     `spectral_smear.h:15-16, 342-347` documents as a **bit-identical bypass**, so the comparison is
     of the same signal, delayed. **(A-10) The smear-*enabled* arm is configured
     `setSmearAmount(0)` (`spectral_smear.h:376`) and `setDecoherence(0)` (`:381`) before the
     render**, because only at 0/0 is `SpectralSmear` an exact identity delayed by `fftSize`: at any
     non-zero amount or decoherence it is a spectral *smear*, the onset is smeared along with
     everything else, and the onset-index difference measures the smear's time spread rather than
     its latency — which would make the ±1-sample bound below unsatisfiable on a correct
     implementation. `noteOn` one voice, render, and compare the onset sample index
     (first sample above a stated threshold, measured using the **FR-014a `kFastAttackEnvelopeConfig`**
     fixture so the onset is sharp enough to index) of the smear-enabled render against the
     smear-disabled one. The difference **equals `getLatencySamples()` within ±1 sample**.

  FR-052 is amended accordingly: `VoragoEngineConfig` carries the `SpectralSmear` prepare-time
  enable flag so clause 2 has a product state to compare against; nothing in this phase compensates
  the latency internally. `VoragoEngine_ReportedLatency`.
- **SC-023 — Unprepared and degenerate calls are safe.** Every public method on an unprepared voice,
  engine and matrix returns its documented neutral and writes nothing out of bounds; `n = 0`,
  null pointers, polyphony 1, zero agents, zero noise sources, zero ecology loops and zero resonance
  peaks all render silence or pass-through without a non-finite sample.

  **(A-8) Two arms this criterion's own text demanded but did not name.**
  1. **The matrix arm.** The sentence above says "voice, engine **and matrix**", but the named case
     covers only the engine. `VoragoMacroMatrix` gets its own case: every public method on an
     unprepared matrix returns its documented neutral, `apply()` on an unprepared engine writes
     nothing, `computeCavernTargets()` returns exactly the FR-063 defaults, and an out-of-range
     `VoragoMacro` is a silent no-op on set and returns the macro's neutral on get (FR-069).
     `VoragoMacro_UnpreparedAndDegenerate`.
  2. **The degenerate sample-rate arm — the only assertion of FR-076's second sentence anywhere in
     this spec.** FR-076 requires that a non-finite sample rate is **substituted** and that any rate
     below the components' shared `kMinUsableSampleRate = 8000.0` is **floored, not rejected**; SC-021a
     and SC-021b sweep only valid rates and would pass with a `prepare()` that rejected a bad one.
     `prepare()` is therefore called at three degenerate rates — **NaN** (constructed from a bit
     pattern through a `volatile`, **never** `std::numeric_limits`, which `-ffast-math` folds;
     roadmap line 570, FR-072), **`0.0`**, and **`4000.0`** — and in all three `isPrepared()` is
     **true** and a subsequent render is finite and bounded. No threshold elsewhere is affected.

  `VoragoEngine_UnpreparedAndDegenerate`, `VoragoMacro_UnpreparedAndDegenerate`.
- **SC-024 — Portability gate (FR-075).** `node tools/check-portability.js` is clean, the three new
  headers compile in `dsp/lint_all_headers.cpp`, and the new TUs compile under libstdc++ as well as
  MSVC. Recorded, not asserted in-suite.
- **SC-025 — The six new materials are inside the shipped body budgets (FR-038b).** With
  `continuous_body_perf_test.cpp` extended to eleven materials (FR-038 sites 1–4), each of the six
  new materials' `steady`, `operating`, `crossfade` and `cloudOnly` figures satisfies the shipped
  baselines at `kRegressionFactor = 1.5`, exactly as the five Seraphis materials do (`:881-897`).
  The per-material table (44 measurements, up from 20) is printed in full **before** any `REQUIRE`
  fires, preserving that file's own stated discipline that the first over-budget configuration must
  not hide the rest (`:875-880`). A material over budget is **re-voiced or dropped** (FR-038b); the
  baselines do not move. `ContinuousBody_DarkMaterialBudgets` `[.perf]`.
- **SC-026 — A slot's seed is per slot, not per note (FR-048).** Same engine seed and configuration
  throughout. (a) Play note *n* on slot *s*, let it retire, **rewind with `VoragoEngine::reset()`**
  (which restores run state but does not re-derive slot seeds), play note *n* on slot *s* again: the
  two renders compare **within fingerprint tolerance** (the same slot reproduces its trajectory).
  (b) Force a **steal** of slot *s* mid-render, rewind the same way, then play note *n* on it: same
  result, i.e. a steal does not advance the slot's seed. (Q-M: without the rewind the replay
  legitimately differs, because FR-046/SC-030 keep an idle slot's identity layer advancing.) (c) Play note *n* on slot *s* and on slot *s′* ≠ *s*
  simultaneously: `worstMetricRelativeError` **> 100 ×** `kMetricTolerance` (FR-045's disjoint
  salts genuinely separate them). Untagged — it is a determinism case (FR-085).
  `VoragoEngine_SlotSeedReproducibility`.
- **SC-027 — The ghost configuration is what FR-017 says it is, on the object that now owns it
  (OQ-1 ruling (b)).** Two clauses, both on `VoragoEngine`'s owned `AtmosphereEngine`.
  1. **Read-back after `VoragoEngine::prepare()`**, per the FR-017 table: `getDensity() == 0.30`,
     `getGrainSeconds() == 12.0`, `getPitchSemitones() == −12.0`, `getPositionSpread() == 0.90`,
     `getBlur() == 0.85`, `getDecorrelation() == 0.85` (`atmosphere_engine.h:819, 832, 854, 862,
     905, 914`), each compared exactly against its FR-090 row. Without this clause an implementation
     that left `AtmosphereEngine` at its shipped defaults (density 4.0, `:821-828`) would satisfy
     every other criterion in this spec, and roadmap line 114's deliverable would ship unverified.
  2. **Event gating.** At polyphony 1, over a 10-minute render (accelerated per FR-086) with that
     voice's ghost-destined scheduler live, `getLevel()` shows **≥ 6 burst edges** (transitions from
     the 0.0 base to ≥ half the configured peak and back); with that scheduler's depth at 0, it shows
     **exactly 0** and `getLevel()` holds its base. This is the observable that distinguishes "bursts
     of ghosts" from a continuous wash, now read on the engine's single global instance rather than a
     per-voice one. `VoragoEngine_GhostConfiguration`.
- **SC-028 — Setter contracts (FR-069, FR-071).** On the matrix, the voice and the engine: a
  non-finite argument to any public float setter leaves the previous value standing (read back by
  the getter); an out-of-range argument is **clamped** and the getter reports the clamp; an
  out-of-range **index** is a silent no-op that writes nothing; `setMacro`/`getMacro` and
  `setMacros`/`getMacros` **round-trip** over an enumerated value table including both endpoints,
  every neutral, and non-finite inputs. Untagged. `VoragoMacro_SetterContract`,
  `VoragoEngine_SetterContract`.
- **SC-029 — Non-finite containment actually runs (FR-072).** Every other criterion that mentions
  `getNonFiniteRecoveryCount()` asserts it is **zero**, i.e. asserts the recovery path never runs —
  so an implementation whose containment branch is dead, mis-indexed or resets the wrong voice would
  pass the whole suite. This criterion injects the fault deliberately, through
  `detail::VoragoEngineNonFiniteProbe` — forward-declared in `vorago_engine.h` and **defined in this
  criterion's own TU**, `vorago_nonfinite_test.cpp`, in the `seraphis_engine.h:193-195` shape, with
  **no compile definition** (Q-C, FR-087, A-11). With `k ≥ 2` voices sounding, inject a non-finite
  value into voice *i*'s audio state mid-render and assert: the block output is **finite**
  throughout; `getNonFiniteRecoveryCount()` increments by **exactly 1**; the **other** voices'
  output is **bit-unchanged** against a reference render with no injection; voice *i* **resumes
  rendering** (non-silent within a stated number of chunks). Repeat for each voice index, so a
  mis-indexed reset is caught. **Per-push, untagged** — it is a NaN/Inf-guard case (FR-085).
  `VoragoEngine_NonFiniteContainment`.
- **SC-030 — An idle voice's life state matches a rendering one (FR-009, FR-046).** At the engine
  level: two voices from the same slot-seed derivation, one rendering and one only
  `advanceLifeOnly`-advanced over the **same sample count** across a partitioned schedule, have
  **exactly equal** `EcosystemEngine::getControlStepCount()` and equal scheduler event counts, and
  the idle voice, once `noteOn`'d, does **not** start from a cold ecosystem — its first-chunk agent
  outputs match the rendering voice's within fingerprint tolerance. Untagged.
  `VoragoEngine_AdvanceLifeOnlyParity`.
- **SC-031 — The sub-fundamental is held, never reset (FR-051).** `noteOn` the lowest note, render,
  `noteOff` **all** notes, render through a silence gap of ≥ 10 s, then `noteOn` a different note:
  `SubharmonicEngine::getFundamentalHz()` is **unchanged** across the whole gap (no reset to a
  default, which would glissando the subs on every note) and the render across the gap contains no
  sample-to-sample delta above the SC-011 click threshold. Untagged.
  `VoragoEngine_HeldSubFundamental`.

---

## Edge Cases

**Real-time boundaries**
- `processStereoBlock` called with `n` larger than `cfg.maxBlockSamples`: the carry-FIFO design
  (FR-007) makes this safe by construction — the voice loops chunks and never indexes a
  prepare-sized scratch by `n`. Asserted at `n = 65536`.
- `n` not a multiple of 64, repeatedly, at a phase that never aligns (e.g. 37): control steps must
  still occur exactly once per 64 samples of elapsed time (FR-007), not once per call.
- A steal arriving mid-chunk: the silence ramp is armed from the last sample the caller **actually
  received**, not from the tail of the rendered chunk (`seraphis_voice.h:471-478`).
- `prepare()` called while sounding: fully reconfigures and ends silent (FR-003); no stale pointer
  into a re-sized atmosphere buffer survives.

**Parameter extremes**
- Every macro at 1 simultaneously, polyphony at maximum, every wake at 1, feedback coupling at
  `kMaxCouplingPerPair`, ecology gain at `kMaxLoopGain`, resonance Q at maximum: SC-013a (per-push)
  and SC-013b (`[long]`) are the gate; the ecology governor and the true-peak limiter are the two guarantees.
- Every macro at 0 simultaneously: the instrument must still make sound (a macro set of all zeros is
  not a mute). Asserted **explicitly** as SC-008's non-silence clause — broadband RMS > −60 dBFS
  over `[10 s, 60 s]`. Without that clause an all-zeros mute passes every criterion in this spec.
- `setPolyphony(1)` while 8 voices sound: the 7 freed voices ramp, the sum gain ramps (FR-043), no
  step.
- Note frequencies at the extremes: `ContinuousBody` clamps to [20, 8000] Hz and `HarmonicCloud` to
  [20, 4000] Hz — **they clamp differently**, so MIDI 127 lands at different places in the two
  engines (`seraphis_voice.h:512-516` documents exactly this for Seraphis). Vorago documents it and
  does not repair it.
- Sub-engine fundamental with no voice sounding: held, never reset (FR-051), asserted by SC-031.

**Sample-rate changes**
- `prepare()` at a new rate mid-session: every component re-derives; seeds and macro values survive
  (FR-077); the first block after is silent rather than a discontinuity.
- 192 kHz: the ecosystem's `dt_`, the smear's pole tables and the ecology's delay ranges all scale;
  SC-021a (48 kHz + 192 kHz, per-push) and SC-021b (the remaining rates, `[long]`) are the gate.
  Note the Phase 5 compliance record's caution that a high-rate arm can be the one that exceeds
  budget — SC-001a and SC-001b are both measured at 48 kHz, and any higher-rate figure is recorded,
  not gated.
- Sample rate below `kMinUsableSampleRate`: floored, not rejected (FR-076).

**Seed determinism**
- Same seed, same parameters, same note sequence → same render within fingerprint tolerance
  (SC-006). Different seed → measurably different (SC-006's negative arm).
- Two voices allocated to the same note at the same instant must differ (FR-045).
- A voice reused after a steal replays the **same** trajectory for the same note: `setSeed`
  derivation is per voice **slot**, the seed is never advanced per note, and the run state is
  re-derived at `noteOn` — so the same slot playing the same note twice gives the same trajectory.
  This is **intended**, is required by **FR-048**, is asserted by **SC-026**, and is stated in the
  header, because the alternative (advancing a seed per note) makes the instrument non-reproducible
  for Phase 14's preset renders.
- `advanceLifeOnly` and `processStereoBlock` over the same sample count must leave identical life
  state (FR-009, FR-046) — otherwise a voice's trajectory depends on whether it happened to be
  audible. Asserted at the engine level by SC-030, not only voice-level prose.

**Dormancy boundaries (roadmap lines 561–567)**
- Wake at exactly 0 with `dormant == false`: the component's chain is skipped and its generator and
  lanes keep running — the Phase 2 FR-073 ruling. The voice must not re-implement or contradict it.
- Wake edge during a bloom fade or a steal ramp: the 50 ms re-entry fade and the 1 ms silence ramp
  are independent and may overlap; neither may be truncated by the other.
- An ecology loop's sleep edge clears its audio state (Phase 5 FR-063 stated exception, roadmap
  lines 565–567) — the voice must not assume symmetry with the other wake surfaces.

---

## Decisions taken where the roadmap is silent

1. **The voice envelope gates the excitation** (AR-5), following `seraphis_voice.h:1066`.
2. **The cavern space is outside the engine** (AR-1), following `seraphis_engine.h:608-612`.
3. **Identity routing is explicit, not `ModulationEngine`** (AR-2).
4. **Ghosts are a configuration in this phase** (AR-3, FR-017, SC-027). The two unshipped behaviours
   roadmap line 114 also names are **OQ-3**, a user ruling — *not* a Non-Goal this spec took on its
   own authority.
5. **Dark materials are appended to `ContinuousBody`** (AR-4), append-only but for two enumerated
   widenings (FR-039), with **all seven** count-sized sites updated in the same change (FR-038),
   Seraphis's perf survey pinned to its own five (FR-038a) and the new materials budget-gated on
   Vorago's account (FR-038b, SC-025).
6. **The macro roster is twelve, not fifteen** (FR-061), folding `Decay`, `Instability` and
   `Distance` — authorised by roadmap line 468, confirmed by the Clarifications session (OQ-2) — and
   each fold is made **normative** on its survivor (FR-068) and asserted (SC-008), so a fold is not a
   quiet deletion.
7. **Voice seeds are per slot, not per note** — **FR-048**, asserted by SC-026, so a preset render is
   reproducible. (This was previously stated only in the decisions log and the Edge Cases prose,
   which downstream stages do not consume.)
8. **`SpectralSmear` latency is reported, not compensated** (FR-052) — compensation is a plugin-level
   concern and belongs to Phase 11.
9. **The macro system is a `constexpr` table, not "`ModulationEngine` presets"** (AR-6). This is a
   **deviation from roadmap line 463**, not a silence the spec filled, and is recorded as an
   architecture ruling for that reason.
10. **`setTargetBase` / `resetTargetBases` / `getTargetBase` are deferred to Phase 12** (FR-069) —
    speculative API for a phase that will re-specify it, and in tension with FR-064's compile-time
    shared-base invariant.
11. **Ghost/atmosphere is engine-owned and global; the ecosystem stays per-voice** — decided in the
    Clarifications session (OQ-1 rulings (b) and (c), FR-002, FR-041, FR-056, FR-020b). Only the
    shipped polyphony (OQ-1 (a)) remains open.
12. **The composed engine+`CavernVerb` test case registers with `dsp_effects_tests`, not
    `dsp_systems_tests`** (FR-084a) — decided in the Clarifications session, resolving the open point
    in Assumption 5.

---

## Open Questions

Two are now narrowed or resolved by the Clarifications session above; the third the roadmap defers
to this spec, and this spec, in turn, refuses to decide on its own authority.

- **OQ-1 (roadmap Open Question 5, line 588) — voice count. Narrowed: only sub-decision (a) remains
  open.** Sub-decisions (b) and (c) are **decided** (Clarifications session, 2026-09-17): **(b)**
  ghost/atmosphere is **global** — `VoragoEngine` owns one `AtmosphereEngine`, fed from the voice sum
  and event-gated at engine level (FR-056) — and **(c)** the ecosystem stays **per-voice** (roadmap
  line 390's agent counts stand, FR-002). This is exactly FR-081's "L-1" row (`V = 474 395`,
  `G = 411 330`): L-1 applied, L-2 rejected.

  **What remains open is (a): the shipped polyphony** (`kMaxVoices` ceiling, 8, and the default) —
  4, 6 or 8 — **together with whichever of L-3/L-4 makes it fit**, against the L-1-only baseline
  above. L-5's floor still forbids dropping below the roadmap's 4 (line 460). **This is not ruled at
  spec stage:** FR-082's stage-cost probe and SC-001a's measured survey run first; the plan stage
  then presents the **measured** per-voice and global table (not this spec's projection) with a
  recommended shipped polyphony and `kMaxVoices` (ceiling 8; **ruled 4 and 6 at Q-H**), and that
  recommendation is a **plan-stage ruling for the user**, taken before SC-001b's gate or any baseline is checked in. The
  30 % ceiling (`kReferenceNs`), `kRegressionFactor = 1.5` and the ban on relaxing thresholds or
  shrinking workloads all stand.
- **OQ-2 (roadmap Open Question 6, line 589) — macro roster trim. Resolved: confirmed at twelve.**
  The Clarifications session confirms FR-061's roster exactly as written — `Darkness, Age, Density,
  Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass`, folding `Decay` → `Age` +
  `Depth`, `Instability` → `Entropy`, `Distance` → `Fog` — with no change. `kRows` is authored
  against this roster (FR-061, FR-068, SC-008). Roadmap line 589 splits this across Phase 10 and 12;
  this spec takes the **roster**, Phase 12 takes the **parameter surface**.
- **OQ-3 (roadmap line 114 + line 572) — does Phase 10 extend `AtmosphereEngine`, or does a named
  later phase?** **Ruled 2026-09-17 (Clarifications, Q-B): a named later phase, Phase 10a.** The
  record of the question follows. Roadmap line 114's 🔶 row asks this phase's ghost tap for three things: *reverse
  playback per grain*, *event-triggered (not continuous-density) scheduling*, and *darker blur
  defaults*. FR-017 delivers the third in full and substitutes **event-gated `setLevel`** for the
  second. The first two are not configuration — the shipped header has no reverse control and no
  event-trigger entry point (Overview fact 3) — so delivering them means a **source change to a
  Seraphis-shipped Layer 3 component**.

  This spec **does not decide that on its own authority**, for three reasons: the roadmap asks for
  it in two independent places, the second of which (line 572) explicitly lists "AtmosphereEngine
  ghost config" among the **shared-component changes** that must keep Seraphis's tests green — i.e.
  the roadmap already anticipates a source change and already states its obligation; this spec
  accepts a structurally identical shared-component change for `ContinuousBody` (AR-4); and dropping
  a roadmap deliverable is the user's ruling.

  **The options, with their costs:**
  - **(A) Ship FR-017 only** (what this spec builds absent a ruling). Zero risk to Seraphis. The
    roadmap's reverse-grain deliverable then has **no owning phase** and must be struck from line
    114 or assigned to one, explicitly.
  - **(B) Extend `AtmosphereEngine` in Phase 10**, append-only, on the AR-4 model. The substrate
    exists: `dsp/include/krate/dsp/primitives/reverse_buffer.h` is shipped and is already named in
    roadmap line 114's own reuse column. Cost: a per-grain reverse flag read at grain birth (the
    component already snapshots pitch, position and drift at birth — `atmosphere_engine.h:805-812`),
    an event-trigger entry point beside the density scheduler, new FRs and SCs, a Seraphis-green
    gate in SC-016's shape, and the CPU it adds inside FR-081's per-voice figure — which is the
    figure OQ-1 is already tight against.
  - **(C) Name the later phase that owns it** (Phase 13's UI phase does not; Phase 14 is presets),
    and record the roadmap edit.

  A recommendation is due at plan stage alongside OQ-1's. Until ruled, **(A)** is what ships and
  FR-017 plus SC-027 are its verification.

---

## Traceability

| Roadmap statement | Line(s) | Requirements |
|---|---|---|
| Phase 10 goal: "compose everything into the playable instrument core" | 451 | FR-001, FR-040 |
| `VoragoVoice` chain: cloud + noise → resonance → ecology tap → bloom → body → ghost tap → envelope | 453–456 | FR-002, FR-010, FR-011, FR-013–FR-017, AR-5 |
| Dark material data: six materials, mode ratios + damping laws, "data authoring, not new DSP" | 457–459 | FR-030–FR-035, FR-038, FR-038a, FR-038b, FR-039, SC-015, SC-016, SC-025 |
| Two-body blend | 122, 456 | FR-036, FR-037, SC-017, SC-017a |
| `VoragoEngine`: 4–8 voices, `VoiceAllocator`, quietest-steal with long-release amnesty | 460 | FR-041, FR-043, FR-044, SC-011, SC-012, OQ-1 |
| Per-voice unique seeds | 461 | FR-025, FR-045, FR-048, SC-006, SC-026 |
| voice-sum → subharmonic → smear → cavern → output (`TapeSaturator` + `TruePeakLimiter`) | 461–462, 125 | FR-050–FR-055, AR-1 |
| Subharmonic is global, post-voice-sum (OQ-4, decided in Phase 6) | 584–586 | FR-051, SC-031 |
| Concept macro system, ≤ 15 concepts, documented multi-target mappings (Weight / Life / Fog) | 463–468 | FR-060–FR-069, SC-008–SC-010, SC-028, OQ-2. **Deviation recorded:** line 463's "via `ModulationEngine` presets" mechanism is replaced by the `constexpr` `kRows` table — see **AR-6** |
| Ecosystem outputs drive resonance peaks, noise sources, ecology loops | 394–398 | FR-020, FR-020a, FR-020b, FR-021, FR-023, SC-019, SC-019a, SC-019b, AR-2 |
| Bloom trigger from `SlowEventScheduler` | 359–360 | FR-022, SC-020 |
| Gravity breathes via `BreathingModulator`; fog rolls in via `TidalModulator` | 121, 257 | FR-026 |
| Ghost-flavoured atmosphere config: darker blur defaults, low density, long grains | 114 | FR-017, FR-056, SC-027, AR-3 |
| Ghost-flavoured atmosphere config: reverse grains + event-triggered scheduling | 114, 572 | **OQ-3** — deliberately NOT decided by this spec; AR-3 |
| SC: 8 voices everything-on ≤ 30 % of one core @ 48 kHz | 470 | FR-080, FR-081, SC-001a (measure + report), SC-001b (the gate, written as an amendment after the OQ-1(a) ruling), OQ-1. **Not reachable as written** — FR-081's two-column solve. OQ-1(b)/(c) now decided (FR-056, FR-002); only OQ-1(a) (polyphony) remains open |
| SC: overnight soak (8 h) bounded and non-static | 471 | SC-004a, SC-004b, SC-005, FR-086 |
| SC: macro sweeps render-verified along documented axes | 472 | SC-008, FR-068 |
| SC: determinism harness, `render_fingerprint.h` tolerances, no bit-exact goldens | 473, 568 | FR-074, SC-006 |
| Pattern template: `seraphis_voice.h` / `seraphis_engine.h` / `seraphis_macro_matrix.h` | 140–142 | FR-002, FR-005, FR-007, FR-044, FR-053, FR-060 |
| RT safety: no allocation/locks/exceptions/IO; pools sized at prepare | 551–552 | FR-003, FR-006, FR-070, FR-072, FR-087, SC-014, SC-029 |
| Boundedness is the theme-level FR; worst-case soak | 553–555 | FR-073, FR-085, SC-004a/b, SC-013a/b, SC-021a/b |
| Layer discipline + ODR sweep before every new class name | 558 | AR-1, *New components* table |
| CPU budgets are FRs, measured in tests | 558 | FR-080–FR-083, FR-038b, SC-001a, SC-001b, SC-002, SC-003, SC-025 |
| Dormancy | 561–567 | FR-024, Edge Cases |
| Portability | 569–571 | FR-075, SC-024 |
| Shared-component changes must keep Seraphis's tests green | 572–574 | FR-038, FR-038a, FR-038b, FR-039, SC-016, SC-025, AR-4; **OQ-3** for the AtmosphereEngine half |
| ODR note / near-name hazard list | 127–129 | *New components* table |

---

## Assumptions

1. **No Vorago component acquires a new public method in this phase.** Phases 2–9 shipped every
   surface Phase 10 needs — sleep/wake, trigger, depth, mix, seed. If the build finds a missing
   accessor, the response is to surface it, not to extend a shipped component quietly (the Phase 6
   `SubOscillator::advance()` precedent: one append-only change, spec'd, recorded at roadmap lines
   309–311). **One exception, surfaced rather than taken quietly (A-13):** `FeedbackEcology` acquires
   `void silenceAudio() noexcept`, on exactly that precedent — the voice's steal and non-finite
   recovery paths are audio-thread paths and the component's only clearing entry point, `reset()`,
   documents itself as control-thread (`feedback_ecology.h:861-862`). It is spec'd here, recorded in
   AR-4's neighbourhood, and gated by its own suites.
2. **Two shared components change in this phase, both append-only** (**A-13**, superseding this
   assumption's earlier "`ContinuousBody` is the only shared component this phase changes"):
   `ContinuousBody` gains append-only **data** (AR-4, FR-030 – FR-039), and `FeedbackEcology` gains
   one appended public **method**, `silenceAudio()` (AR-4's neighbourhood, plan B-7) — zero deleted
   lines, no new class, member, enumerator or constant, and no Seraphis consumer, so SC-016's gate
   does not widen.
3. **Measured figures in FR-081 are the isolated, P-core-pinned figures** each phase recorded, and
   the composition's measured cost may exceed their sum by the SC-003 overhead factor — which is why
   FR-081 now solves the polyphony question in **two columns**, bare and × 1.15, rather than
   acknowledging the factor and then ignoring it. The projection is a planning instrument;
   **SC-001a's measurement** is what the OQ-1 ruling is taken from and **SC-001b** is the criterion.
4. **`tests/test_helpers/render_fingerprint.h` is reachable from `dsp_systems_tests`** — confirmed
   this session by its use in `dsp/tests/unit/systems/bloom_engine_test.cpp:54`,
   `continuous_body_test.cpp:40` and `atmosphere_engine_test.cpp:78`.
5. **`CavernVerb` may be named by the Phase 10 test TU.** A test TU is not a library header and is
   not bound by the Layer 3 include rule; Seraphis's composed tests set the precedent. **Decided
   (FR-084a, OQ3):** the composed-chain case registers with `dsp_effects_tests`, beside `CavernVerb`'s
   own tests, compiling under that target's `KRATE_DSP_AETHER_TEST_HOOKS` consistently with every
   other TU in that executable; every other Phase 10 TU registers with `dsp_systems_tests` (FR-084)
   and avoids the hooks entirely, which is what makes both placements ODR-safe
   (`dsp/tests/CMakeLists.txt:547-555`).
6. **The Phase 8 offline prototype's tuned rule set is already encoded** in `EcosystemEngine`'s
   shipped defaults; Phase 10 does not retune agent rules, only routing depths.

