# Feature Specification: Vorago Phase 5 — Feedback Ecology

**Spec slug:** `vorago-phase5-feedback-ecology`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 5 (lines 265–282); reuse-inventory row
`L5 Feedback Ecology` (line 113); ODR note (lines 127–129); Dormancy rule (lines 501–506);
cross-cutting constraints (lines 492–513). **Every roadmap line number in this document was
re-verified against `specs/Vorago-roadmap.md` this session**; nine citations in the Cross-Cutting
Constraints block were off by one to three lines and are corrected here and in the Traceability
table, which had disagreed with this header about the Dormancy rule.
**Layer:** one new Layer 3 component, `dsp/include/krate/dsp/systems/feedback_ecology.h`
(roadmap line 270).
**Test target:** `dsp_systems_tests` (enumerated source list, `dsp/tests/CMakeLists.txt:324-435` —
`add_executable(dsp_systems_tests` opens at `:324` and the list closes at `:435`, immediately after
the four Vorago Phase-3 TUs at `:432-435`); the `-fno-fast-math` block opens at
`dsp/tests/CMakeLists.txt:528` (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`). The list is
**enumerated, not globbed** — an unregistered TU silently drops out of the build and its cases never
run (`dsp/tests/CMakeLists.txt:409-410`, `:419-420`).
**Depends on:** nothing from Vorago Phases 2–4. Phase 1's `SlowEventScheduler`
(`processors/slow_event_scheduler.h:143`) is **not** included — this component takes a plain scalar
wake value, exactly as `NoiseOrganism::setSourceWake` does (`systems/noise_organism.h:844`).
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Clarifications

### Session 2026-09-12

- Q1: What are the resonator's default centre frequencies and RT60, and the loop filter's default Q? → A: Option B, **as amended by OQ-1** — an independent per-loop centre-frequency table one octave below the FR-053 cutoff table, `kDefaultLoopResonanceHz = {1200, 850, 600, 425, 300, 210}` Hz; `kDefaultFilterQ = SVF::kButterworthQ`; `kDefaultResonanceRt60 = 1.0f` s — **not** 2.0 s, because the ring ceiling at these centres is set by `kMaxResonatorQ = 100`, which reaches 1.05 s at 210 Hz and 0.18 s at 1200 Hz, so 1.0 s is reachable at the lowest centre and is silently clamped per-centre above it; the realised value is reported by a truthful `getLoopResonanceRt60(i)` (derived from the actually-applied, clamped Q), not the raw request. The reference patch's list of default tables gains FR-013, so SC-001, SC-002, SC-003, SC-004, SC-006, SC-011, SC-021 and SC-022 are all measured on stated values. [FR-013]
- OQ-1: Which component realises the loop resonator, and at what Q ceiling? → A: The `ResonatorBank` slot's RBJ bandpass at `Q <= kMaxResonatorQ = 100`, **not** a `SmoothedBiquad` at `Q <= biquad.h`'s `kMaxQ = 30` (user ruling 2026-09-12). Verified this session: `primitives/biquad.h:53` `kMaxQ = 30` and `BiquadCoefficients::calculate` clamps Q at `:673`, which `SmoothedBiquad::setTarget` routes through at `:554` — so the spec's own prior claim of a `kMaxResonatorQ = 100` ceiling was never actually reachable through `SmoothedBiquad`. `processors/resonator_bank.h:51` `kMaxResonatorQ = 100`, `rt60ToQ` at `:92` clamps to it, and `updateFilterCoefficients` (`:650` onward) computes the RBJ bandpass itself and hands the result to `Biquad::setCoefficients`, bypassing the `calculate()` clamp — that is the route FR-013 now specifies: a plain `Biquad` per loop, fed coefficients computed directly by this component using the `resonator_bank.h:668-682` formula, set via `setCoefficients` (never `configure`/`calculate`). Retunes are therefore a **hard-swap** (stepped), not a glide: the resonator centre and RT60 are user-set, not wander-driven, so a retune is a rare control event, declared stepped in FR-013 and FR-076 and excluded from the click-freedom assertion by name (SC-001 (d)), the same treatment as `setLoopFilterMode`. The single-slot-`ResonatorBank`-per-loop alternative remains on the record and is measured by FR-080's stage probe, but is not the default absent a criterion that needs it. [FR-013, FR-041, FR-076, SC-001]
- OQ-2: Which CPU levers are pre-authorised if FR-080's probe misses budget? → A: Levers 1–3 are authorised from the measured probe table: (1) `std::tanh` → the shipped `FastMath::fastTanh`, bound preserved; (2) stepped resonator retunes, which OQ-1 makes the default anyway; (3) skipping a coupling-pair's `OnePoleSmoother` advance once it has settled at its target (FR-034's carve-out). Lever 4 (a second `SVF` in Bandpass mode in place of the resonator, dropping the RT60 surface) and lever 5 (default `numLoops` 6 → 5) remain **user** decisions: if the total still exceeds `71 111 ns`/block after levers 1–3, the build stops and surfaces the measured per-stage table rather than reaching for 4 or 5 on its own. [FR-034, FR-080]
- OQ-3: When does the roadmap line-272 write-back (and deletion of the two ratified DEVIATION annotations) run? → A: Now — as an early task in the plan's build-prep group, so the roadmap and this spec's Traceability table agree before any code cites either; the docs commit that precedes the build stage carries both edits. The "Open Questions" section's provisional "plan phase's task, not this revision's" framing is retired along with the section itself (Clarifications instruction 4); the decision is recorded here instead. [Traceability]
- Q2: What does `setWanderEnabled(false)` actually do to the mapped values? → A: Option A, Phase-3 verbatim — disabling zeroes the FR-052/FR-053 depth term (mapped delay/cutoff glide back to base) while the lanes keep advancing underneath; re-enabling moves from base to the lane's current position. Verified against both siblings this session: `resonance_drift_network.h:742-744` ("Off zeroes every DEPTH; it does not rewind a lane") and `noise_organism.h:761-770` (zeroes the external spans and forwards to the chain filter's own randomiser). SC-011's "every mapped delay is still the base value" is then true at any time the setter is off. The "so a re-enable does not jump" rationale is removed — no reading delivers it. [FR-056, SC-011]
- Q3: Which sources are counted in FR-035's row sum `G_i`? → A: Option B — `j` ranges over `j < numLoops` (excluding `i`), ignoring wake/dormancy; slots at or above `numLoops` are excluded and sleeping loops still count. `getLoopAppliedTotalGain(i)` is therefore a pure function of the configuration alone, including at `numLoops = 1` where the survivor's applied total gain equals its own feedback gain within `1e-6`. [FR-035, SC-015, SC-022]
- Q4: Does cross-coupling read the pre-gate `y_j` or the post-gate `out_j`? → A: Option B — cross-coupling reads the previous sample's post-gate `out_j`; a loop's own feedback keeps reading its own previous-sample pre-gate `y_i`. A fading loop's contribution to its neighbours fades continuously with its gate; its own circulation is untouched and the gate stays a pure output gain. Proven at the `kMaxCouplingPerPair = 0.5` fixture, which under the rejected pre-gate reading would have carried a 0.5-amplitude step. [FR-015, FR-031, FR-062, SC-001, SC-014]
- Q5: One stereo input, or per-loop excitation from distinct sources? → A: Option A — keep one stereo input pre-summed to mono plus per-loop scalar tap gains (FR-074 as written), declared as a roadmap deviation: Phase 10 pre-sums the cloud and noise-organism taps before this stage, and per-loop diversity comes from tap level plus each loop's own filter/delay/resonator. The signature is frozen for Phases 8–12 by SC-016. [FR-074]
- Q6: What level does the governor's RMS tracker actually see? → A: Option B — `normGain × Σ_i b_i`, the same `1/sqrt(numLoops)` normalisation FR-017 applies to the wet sum, so the tracked quantity is the wet signal's own level and `kGovernorThresholdDb` names one fixed level at every loop count, agreeing within 1 dB between `numLoops = 1` and `numLoops = 6` (the arm that fails, by ≈7.8 dB, under a raw unscaled sum). The "no gain factor of any kind" wording is removed. [FR-043, SC-006]
- Q7: What does `getLoopCurrentCutoffHz(i)` report? → A: Option A — `svf_[i].getCutoff()`, the last commanded value; it coincides with `getLoopTargetCutoffHz(i)` while the loop runs and diverges from it only under FR-062's write skip while dormant. No `SVF` amendment. [FR-071, SC-014]
- Q8: Does `prepare()` on a live object discard the caller's configuration? → A: Option A, Phase-3 verbatim — every call to `prepare()`, including a re-prepare on a live object, restores every FR-013/FR-022/FR-033/FR-052/FR-053 default table, the coupling matrix, loop gains, mix and wet gain, then re-derives rate-dependent state; `reset()` still does not touch these. A Phase-11 note: a host sample-rate change requires re-pushing the full parameter set after `setupProcessing`. [FR-005, SC-011]
- DECISIONS-CONFIRMED: roadmap line 272 is amended (Phase-4 precedent — amend rather than carry deviations) to "filter (`SVF`)" and "resonator (one RBJ bandpass, the `ResonatorBank` slot's Q range)"; default loop count stays 6 (D-4); the wet path stays mono (D-6); `kDefaultWetGainDb = 0.0f` stands unless SC-018/SC-022 measure otherwise; the FR-080/SC-004 CPU basis (106 666 ns/block ceiling, 71 111 ns gated baseline) and its ordered-lever stop-and-surface rule are confirmed as written; FR-063's Dormancy-rule deviation (the sleep edge clears loop audio state) is confirmed and is recorded in the Traceability Dormancy row as the stated exception for feedback loops. [FR-010, FR-034, FR-040, FR-063, FR-072, FR-080, Traceability]

## Overview

Feedback Ecology is Vorago's *coupled-object* layer. It is a Layer 3 system that runs **six tiny
feedback loops** side by side inside one voice (roadmap line 268, "Five or six tiny interacting
feedback loops that behave like coupled vibrating objects"), each loop a short circulating path —
filter → delay → resonator → gain → back, with a DC blocker in the path (roadmap line 272) — and
each loop bleeding a few percent of its output into its neighbours through a **cross-coupling
matrix** (roadmap lines 274–275). A **global energy governor** watches the summed loop energy and
softly compresses the whole network's feedback so the loops can interact without any of them running
away (roadmap lines 276–277). Every loop's delay time and filter cutoff drift on their own bounded
Brownian lanes (roadmap line 277, "Per-loop tiny life-modulation of delay time and filter cutoff"),
so the ensemble never settles into a fixed comb.

The component is **not** a reverb and **not** a delay effect. Its loops are 10–500 ms — long enough
to be heard as separate rings rather than as a resonance, short enough that six of them overlap into
a single moving body — and its output is mixed back at a **low** level (roadmap line 278), colouring
the voice rather than replacing it.

**Boundedness is the phase.** The roadmap calls it out explicitly (line 280: "bounded output for ANY
parameter combination over 30 min renders (this is the critical test)"), and this spec answers it
with a **five-rung ladder** rather than a single guard, because each rung fails differently: a
structural loop-gain bound (FR-041), a per-loop `tanh` soft clip (FR-042), the energy governor
(FR-044), a hard output clamp with an engagement counter (FR-046), and a non-finite trap that clears
loop state (FR-047). Rung 5 is **defence in depth and is not reachable through the public API** — the loop's first
stage resets itself on a non-finite input, so no public call sequence can poison `b_i` (FR-047,
FR-048); it is tested through a declared, test-only fault-injection probe, the shipped
`SeraphisEngine` pattern. It exists because two of the shipped primitives this component composes do
**not** self-heal: `DCBlocker::process` states "NaN inputs are propagated (FR-016)"
(`primitives/dc_blocker.h:188`) and `EnvelopeFollower::processSample` states "Does NOT validate input
— caller must ensure no NaN/Inf for maximum speed" (`processors/envelope_follower.h:163`). A single
non-finite sample reaching either one poisons it for the life of the object;
`detail::flushDenormal` does not clear a NaN.

**Three roadmap component names do not survive contact with the shipped headers**, and each
substitution below is a measured or structural fact rather than a preference. They are collected here
because they are the spec's largest deviations and belong in front of the reader, not in a footnote:

1. **`MultimodeFilter` → `SVF`.** The roadmap names `MultimodeFilter` for the loop filter (line 272).
   Verified: its only per-sample entry point, `processSample(float)`
   (`processors/multimode_filter.h:204`), calls `updateCoefficientsFromSmoothed()` **on every sample**
   (`:218`, body at `:372-384`), which rebuilds biquad coefficients — trigonometry per stage per
   sample. Its cheap path, `process(float*, size_t)` (`:180`), updates coefficients once per block
   (`:186-187`) and is structurally unusable inside a feedback loop, where each output sample depends
   on the previous one. Six of the per-sample kind is this phase's dominant cost risk. The roadmap's
   own named topology reference resolves it: `FilterFeedbackMatrix`, which line 275 tells this spec to
   reuse, uses `std::array<SVF, N>` (`systems/filter_feedback_matrix.h:174`) for exactly this position.
   `SVF::process` (`primitives/svf.h:353`) is a dozen flops plus an optional coefficient step already
   in the `g`-domain (`advanceSmoother() :494`), with `std::tan` confined to `setCutoff`
   (`:204` → `computeG() :489`). FR-011 specifies `SVF`; **FR-080's probe measures both** and Open
   Question 1 asks the user to ratify the roadmap amendment.
2. **`IResonator` → one `Biquad` bandpass, fed directly-computed coefficients.** `IResonator`
   (`processors/iresonator.h:32`) is an abstract
   interface whose `process(float)` is **virtual** (`:55`), and the Phase-3 house rule states there is
   "no virtual dispatch on the per-sample path"
   (`systems/resonance_drift_network.h:118-120`). Its only two implementations are
   `ModalResonatorBank` (`processors/modal_resonator_bank.h:71`, `kMaxModes = 96` at `:73`) and
   `WaveguideString` (`processors/waveguide_string.h:38`) — both far larger than "a single mode". The
   thing a "single `IResonator` mode" actually *is*, in this repo, is one RBJ constant-peak-gain
   bandpass biquad: that is literally what each `ResonatorBank` slot holds
   (`processors/resonator_bank.h:498` processes `filters_[i]`, coefficients built at `:650-682`).
   FR-013 specifies one plain `Biquad` per loop, tuned through the shipped `rt60ToQ`
   (`resonator_bank.h:92`), with coefficients computed by this component using the same RBJ formula
   and written through `Biquad::setCoefficients` — **not** a `SmoothedBiquad`, and this is a
   **correction made this session (OQ-1), not the original design**: an earlier draft used
   `SmoothedBiquad::setTarget` (`biquad.h:547`) for a click-free 20 ms retune glide, but that method
   routes through `BiquadCoefficients::calculate` (`:673`), which clamps `Q` to `biquad.h`'s own
   `kMaxQ = 30.0f` (`:53`) — silently undercutting the `kMaxResonatorQ = 100` ceiling FR-013 specifies
   and that `resonator_bank.h`'s own `updateFilterCoefficients` (`:650`) reaches by computing
   coefficients directly and calling `Biquad::setCoefficients`, bypassing `calculate()` entirely. FR-013
   now does the same, and accepts a stepped hard-swap retune (FR-076) as the price of the correct
   ceiling — the resonator centre and RT60 are rare, user-set controls, not something driven at audio
   rate, so a click on retune is proportionate. A whole `ResonatorBank` per loop was rejected on measurement
   grounds recorded in D-3, though it remains on the record as an alternative FR-080's probe measures.
3. **`FilterFeedbackMatrix` cannot be the coupling matrix.** It is `template <size_t N>` with
   `static_assert(N >= 2 && N <= 4, "Filter count must be 2-4")`
   (`systems/filter_feedback_matrix.h:72-73`) and ships explicit instantiations for 2, 3 and 4 only
   (`:671-673`). This phase needs five or six loops. The layer lint would *permit* the include —
   `tools/lint-layers.js:74` fails only when `layerIndex(toLayer) > layerIndex(fromLayer)`, so a
   Layer 3 header may include another (and `noise_organism.h:108` already does) — so the reason is the
   capacity bound, not the layer. What is reused is its **topology knowledge**, cited requirement by
   requirement in FR-030..FR-035.

## Scope

In scope:

- One new Layer 3 component, `FeedbackEcology`, at
  `dsp/include/krate/dsp/systems/feedback_ecology.h` (roadmap line 270), header-only, stereo in /
  stereo out, dry/wet crossfaded at a **low** default wet level (roadmap line 278).
- **Six micro-loops** (roadmap line 272, "5–6 instances"), each `SVF` → `CrossfadingDelayLine`
  (10–500 ms) → `Biquad` bandpass → `DCBlocker` → loop gain (< 1) → back, with a runtime loop count
  in `[1, 6]`.
- A **6 × 6 cross-coupling matrix** with a row-sum normalisation that is what makes the boundedness
  claim structural (roadmap lines 274–275).
- A **global energy governor**: an `EnvelopeFollower` in `DetectionMode::RMS` on the pre-governor loop
  sum, driving a soft downward compression of every loop's feedback gain (roadmap lines 276–277).
- **Per-loop life modulation of delay time and filter cutoff** via owned `BrownianDrift` lanes,
  seeded through `deriveStreamSeed` on the Phase-3 salt-table pattern (roadmap line 277).
- **Per-loop input taps** and the low-level wet mix-back (roadmap line 278).
- **Per-loop sleep/wake** under the cross-cutting Dormancy rule (roadmap lines 501–506).
- Unit tests covering the roadmap's four Phase-5 success criteria (lines 280–282) plus the
  cross-cutting gates from lines 492–513 (30-minute boundedness soak, seed determinism, sample-rate
  change, block-partition invariance, zero allocation, layer/ODR lints, portability, no bit-exact
  goldens).

## Non-Goals (owned by later phases, or deliberately excluded)

- **Owning a modulator or a scheduler.** Following `NoiseOrganism::setSourceWake`
  (`systems/noise_organism.h:844`) and `ResonanceDriftNetwork::setPeakWake`
  (`systems/resonance_drift_network.h:771`), every externally driven target here is a plain scalar
  setter. `FeedbackEcology` is **not** a `ModulationSource` and adds no `ModSource` enumerator. The
  `BrownianDrift` lanes it *does* own are internal state, not a routing surface — the Phase-3
  precedent exactly (`resonance_drift_network.h:114-117`).
- **Placement in the signal chain.** Phase 10 (`VoragoVoice`) decides that the ecology is fed from the
  cloud and noise-organism taps and that its wet returns into the voice sum. This phase produces a
  stereo block and a control surface; nothing drives it yet.
- **Per-loop stereo panning.** Phase 3 has per-peak pan (`resonance_drift_network.h:829`) because its
  own spec asked for it; roadmap line 272–278 asks for nothing of the kind here, and inventing it
  would be a feature this phase was not given. The consequence is stated rather than hidden: the wet
  path is **mono** (FR-016), crossfaded against an untouched stereo dry (FR-072), so at high `mix` the
  image narrows. Recorded in D-6 so a Phase-10 listening checkpoint can raise it as a real finding
  rather than rediscover it as a bug.
- **Freeze / infinite hold.** `FlexibleFeedbackNetwork::setFreezeEnabled`
  (`systems/flexible_feedback_network.h:398`) and `AetherReverb` already own that. Verified this
  session: `feedback_network.h` declares **no** `setFreezeEnabled` at all, so the class-qualified name
  `FeedbackNetwork::setFreezeEnabled` used in an earlier draft named a method that does not exist; the
  other owners in the tree are `FreezeMode` (`effects/freeze_mode.h:271`, which delegates straight to
  `FlexibleFeedbackNetwork` at `:674`), `PatternFreezeMode` (`:248`) and `SpectralDelay` (`:524`). FR-041's structural bound keeps every reachable configuration a strict contraction, which is
  precisely the property a freeze breaks; a second undocumented freeze is excluded by construction.
- **Self-oscillation.** `FeedbackNetwork::kMaxFeedback = 1.2f` ("120% for self-oscillation",
  `systems/feedback_network.h:64`) is the delay-effect contract, not this one. Roadmap line 272 says
  "gain (< 1)" and line 276 says "interaction without runaway"; FR-040's ceiling is below unity and
  there is no path past it.
- **Amending `FilterFeedbackMatrix`, `FeedbackNetwork` or `FlexibleFeedbackNetwork`.** All three are
  shipped consumers of their own tests. FR-090 requires them byte-unchanged (SC-020). Raising
  `FilterFeedbackMatrix`'s `N` bound to 6 was considered and rejected in D-2.
- **Oversampling or nonlinear drive in the loop.** The only nonlinearity is FR-042's `tanh` safety
  clip, which is a bound and not a tone control. `SaturationProcessor` sits in `FeedbackNetwork`'s
  loop (`feedback_network.h:118-121`) because that component is a tape-delay feedback path; nothing in
  roadmap lines 272–278 asks for saturation here.
- **A public per-loop audio *output* bus.** FR-071's tap array exists for measurement (SC-002 needs
  per-loop waveforms) and for the Phase-13 ecosystem view; it is an optional, null-by-default
  parameter on a second entry point, and FR-073 requires the tapped path to produce a **bit-identical**
  main output to the untapped one.

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 5 reuses / relies on |
|---|---|---|
| `SVF` (L1) | `primitives/svf.h:110` | **The loop filter** (FR-011), substituted for the roadmap's `MultimodeFilter` — see Overview 1 and D-1. `void prepare(double sampleRate) :161`, `void setMode(SVFMode) :188`, `void setCutoff(float hz) :204`, `void setResonance(float q) :221`, `void enableSmoothing(bool, float timeSec = kDefaultSmoothingTimeSec) :239`, `void reset() :277`, `void snapToTarget() :309`, `[[nodiscard]] float process(float) :353`. `SVFMode` at `:38` (Lowpass/Highpass/Bandpass/Notch/Allpass/Peak/LowShelf/HighShelf). **Four load-bearing facts.** (a) `process()` **resets and returns 0 on a non-finite input** (`:360-363`) — so the SVF is one of the two self-healing stages in the loop. (b) With smoothing enabled, `setCutoff` only writes `gTarget_` (`:206-207`) and the per-sample `advanceSmoother()` (`:494`) interpolates `g`/`k` directly — **no `std::tan` on the per-sample path**; `computeG()` (`:489`) is called only from `setCutoff`/`updateCoefficients`. That is the whole reason a control-rate cutoff write is affordable six times over. (c) `kButterworthQ = 0.7071067811865476f` (`:117`), `kMinQ = 0.1f` (`:120`), `kMaxQ = 30.0f` (`:123`), `kMaxCutoffRatio = 0.495f` (`:129`). FR-012 clamps Q to `[kMinQ, kButterworthQ]` so the filter's magnitude response never exceeds unity in any of the three modes it is allowed — the first rung of FR-041. (d) `process()` flushes both integrator states through `detail::flushDenormal` (`:379-380`). Not modified. |
| `CrossfadingDelayLine` (L1) | `primitives/crossfading_delay_line.h:61` | **The loop delay** (FR-020), named by roadmap line 272. `void prepare(double sampleRate, float maxDelaySeconds) :100`, `void reset() :123`, `void setCrossfadeTime(float timeMs) :141`, `void setDelaySamples(float) :161`, `void setDelayMs(float) :195`, `void snapToDelaySamples(float) :202`, `void snapToDelayMs(float) :213`, `void write(float) :223`, `[[nodiscard]] float read() :233`, `[[nodiscard]] float process(float) :291`, `[[nodiscard]] bool isCrossfading() const :301`, `[[nodiscard]] float getCurrentDelaySamples() const :306`, `[[nodiscard]] size_t maxDelaySamples() const :312`. **The single most important verified fact in this spec** is at `:161-191`: `setDelaySamples` moves only the *inactive* tap, and starts a crossfade **only when the target has drifted `kCrossfadeThresholdSamples = 100.0f` (`:78`) from the ACTIVE tap** (`:183`). Below that threshold the active read position **does not move at all** (`:189-190`). So a slowly drifting delay time produces a **staircase**: hold, then one 20 ms equal-power crossfade to a position ≥ 100 samples away. Two consequences ride on it. (i) That staircase **is** the anti-zipper mechanism roadmap line 281 asks for — there is no continuously-moving read pointer to zipper (FR-021, SC-003). (ii) A wander whose peak excursion is under ~100 samples produces **no motion whatsoever**, silently: 100 samples is 2.268 ms at 44.1 kHz, 2.083 ms at 48 kHz, 0.521 ms at 192 kHz. FR-022's default delay table and FR-052's default wander depth are chosen against the **44.1 kHz** case, the binding one, and SC-005 exists solely to prove the delay actually moves. Constants: `kDefaultCrossfadeTimeMs = 20.0f` (`:68`), `kMinCrossfadeTimeMs = 5.0f` / `kMaxCrossfadeTimeMs = 100.0f` (`:71`/`:74`). Third fact: `prepare()` calls `setCrossfadeTime(kDefaultCrossfadeTimeMs)` at `:119` **after** setting `sampleRate_` at `:101`, so a crossfade time written before `prepare()` is overwritten and one written before `prepare()` on a fresh object would have used the constructed `sampleRate_ = 44100.0` default (`:335`) — FR-020 orders the calls accordingly. Not modified. |
| `DelayLine` (L1) | `primitives/delay_line.h:57` | Owned privately by `CrossfadingDelayLine` (`:317`); never touched directly. Supplies the **allocation model** FR-082 must reproduce: `prepare()` sets `maxDelaySamples_ = sampleRate * maxDelaySeconds` (`:269`) and resizes to `nextPowerOf2(maxDelaySamples_ + 1)` floats (`:273-275`). At the FR-020 request of 520 ms that is 24 960 → 32 768 floats = 131 072 bytes per loop at 48 kHz, and 99 840 → 131 072 floats = 524 288 bytes per loop at 192 kHz. Six loops therefore allocate 0.75 MB at 48 kHz and 3.0 MB at 192 kHz — the component's entire heap term. |
| `Biquad` (L1) | `primitives/biquad.h:308` | **The loop resonator** (FR-013), substituted for the roadmap's `IResonator` — see Overview 2 and D-3. `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) :330`, `void setCoefficients(const BiquadCoefficients&) :325`, `[[nodiscard]] const BiquadCoefficients& coefficients() const :341`, `[[nodiscard]] float process(float) :352`, `void processBlock(float*, size_t) :374`, `void reset() :385`. `FilterType` at `:68` — `Bandpass` is documented "Constant 0 dB peak gain" (`:71`), which is the second rung of FR-041: whatever its Q, the RBJ bandpass built at `resonator_bank.h:668-682` (`b0 = alpha`, `b1 = 0`, `b2 = -alpha`, normalised by `a0 = 1 + alpha`) has unit magnitude at its centre and less everywhere else. `process()` **resets and returns 0 on a non-finite input** (`:353-356`, via `detail::isFiniteBits`) and flushes both state words (`:367-368`) — the second self-healing stage. It does **not** guard against non-finite *coefficients* (the Phase-3 finding, `resonance_drift_network.h:571-577`), which is why FR-009 rejects non-finite setter arguments rather than clamping them. Constants: `kMinFilterFrequency = 1.0f` (`:47`), `kMinQ = 0.1f` (`:50`), `kMaxQ = 30.0f` (`:53`, `BiquadCoefficients::calculate` clamps to it at `:673`). FR-013 writes coefficients through `setCoefficients` directly, **never** `configure`, precisely to stay off that clamp and reach `resonator_bank.h`'s own `kMaxResonatorQ = 100` instead. **The same header also ships `SmoothedBiquad` (`:527`)** — `void setSmoothingTime(float ms, float sampleRate) :536`, `void setTarget(FilterType, float frequency, float Q, float gainDb, float sampleRate) :547`, `void snapToTarget() :559`, `[[nodiscard]] float process(float) :573`, `[[nodiscard]] bool isSmoothing() const :598`, `void reset() :610` — **read, and rejected this session (OQ-1), not used for the loop resonator.** An earlier draft used it for a click-free 20 ms retune glide, but `setTarget` (`:547`) calls `calculate()` internally (`:554`), inheriting the `kMaxQ = 30` clamp above and silently undercutting FR-013's `kMaxResonatorQ = 100` ceiling — the defect this revision corrects by dropping to the plain `Biquad` and accepting a stepped retune (FR-013, FR-076) instead. Two residual facts about `SmoothedBiquad`, kept for the record. (a) Its `process()` costs five one-pole smoother advances plus a `setCoefficients` per sample **on top of** `Biquad::process` — moot now that it is not used, but it is why an earlier probe design compared it against the plain `Biquad`. (b) Interpolating `(a1, a2)` linearly between two stable coefficient sets **cannot** leave the stability region, because that region is the convex triangle `|a2| < 1, |a1| < 1 + a2`; the peak magnitude mid-glide is not guaranteed to stay at 0 dB — moot for the same reason, since FR-013's stepped hard-swap has no mid-glide state at all. Not modified. |
| `DCBlocker` (L1) | `primitives/dc_blocker.h:94` | **In-loop DC removal** (FR-014), named by roadmap line 272. `void prepare(double sampleRate, float cutoffHz = 10.0f) :135`, `void reset() :155`, `void setCutoff(float) :168`, `[[nodiscard]] float process(float x) :190`. Law: `y[n] = x[n] - x[n-1] + R * y[n-1]` (`:194`). **Two facts.** (a) Its peak magnitude is `2/(1+R)` at Nyquist — marginally **above** unity (≈ 1.00066 at 10 Hz / 48 kHz), so it is the one in-loop stage that is not non-expansive; FR-041's 0.95 ceiling absorbs it explicitly rather than by hand-waving. (b) The header states "**NaN inputs are propagated (FR-016)**" (`:188`) and `process` has no finiteness branch — a NaN entering it sticks in `y1_` forever, and `detail::flushDenormal` (`:203`) does not clear it. This is rung 5 of the boundedness ladder (FR-047). Not modified. |
| `EnvelopeFollower` (L2) | `processors/envelope_follower.h:82` | **The energy governor's RMS tracker** (FR-043), named by roadmap line 276 ("global RMS tracker"). `void prepare(double sampleRate, size_t maxBlockSize) :106`, `void reset() :128`, `[[nodiscard]] float processSample(float input) :164`, `[[nodiscard]] float getCurrentValue() const :192`, `void setMode(DetectionMode) :202`, `void setAttackTime(float ms) :220`, `void setReleaseTime(float ms) :227`, `void setSidechainEnabled(bool) :234`. `DetectionMode::RMS` at `:43`; the RMS law is asymmetric smoothing in the squared domain plus a `std::sqrt` (`processRMS() :312-326`). **Three facts.** (a) `prepare()` **ignores `maxBlockSize`** (`:107`, `(void)maxBlockSize;`) and allocates nothing — the follower contributes zero to FR-082's footprint. (b) `processSample` documents "**Does NOT validate input** — caller must ensure no NaN/Inf for maximum speed" (`:163`); a non-finite loop sum poisons `squaredEnvelope_` permanently, which is why FR-047 resets the follower on the non-finite edge. (c) Ranges: `kMinAttackMs = 0.1f` / `kMaxAttackMs = 500.0f` (`:88-89`), `kMinReleaseMs = 1.0f` / `kMaxReleaseMs = 5000.0f` (`:90-91`). Sidechain HP stays **disabled** (FR-043) — the governor must see the sub content, which is Vorago's identity. Not modified. |
| `BrownianDrift` (L2) | `processors/brownian_drift.h:92` | **The two life-modulation lanes per loop** (FR-050), roadmap line 277. `void prepare(double sampleRate) :121`, `void reset() :133`, `void setSeed(std::uint32_t) :145`, `void setSmoothness(float normalized) :152`, `void setDepth(float normalized) :159`, `void setMean(float) :165`, `void process() :178`, `void processBlock(size_t numSamples) :194`, `[[nodiscard]] float getCurrentValue() const override :212`, `[[nodiscard]] std::pair<float,float> getSourceRange() const override :217`. Exact OU discretisation, `tau = lerp(kTauMin = 0.2f, kTauMax = 30.0f, smoothness)` (`:96-98`, derivation `:20-38`), output hard-clamped to `[-1, +1]` (`:213`), `kControlRateInterval = 32` (`:104`), `kDriftOutputSmoothMs = 150.0f` (`:100`). The proof at `:41-58` is what FR-051 relies on: the per-sample slew is bounded at ≈1.39e-3 of the span at 48 kHz **regardless of OU step size**, because the walk only ever moves a `OnePoleSmoother` target. `processBlock(n)` is the O(control-steps) advance FR-055 uses. Not modified. |
| `ResonatorBank` free functions + constants (L2) | `processors/resonator_bank.h:92`, `:39-81` | Included for **`rt60ToQ(float frequency, float rt60Seconds)` (`:92`)** — FR-013 specifies the loop resonator's decay in RT60 and converts with the shipped function, so this component and every other resonator in the library agree on what a decay time means — and for the namespace-scope constants `kMinResonatorFrequency = 20.0f` (`:42`), `kMaxResonatorFrequencyRatio = 0.45f` (`:45`), `kMinResonatorQ = 0.1f` (`:48`), `kMaxResonatorQ = 100.0f` (`:51`), `kMinDecayTime = 0.001f` / `kMaxDecayTime = 30.0f` (`:54`/`:57`), `kLn1000` (`:81`). The **class** `ResonatorBank` (`:174`) is deliberately **not** instantiated — D-3 records the measurement reason. Its Vorago-Phase-3 additions `processIndividual` (`:554`) and `resetResonatorState` (`:613`) are read and **not usable here**: `processIndividual` takes **one** input for the whole bank (`:554`), and six independently-excited loops have six different inputs. Not modified. |
| `FilterFeedbackMatrix<N>` (L3) | `systems/filter_feedback_matrix.h:72` | **Topology prior art — read in full, NOT instantiated** (roadmap line 275 says "reuse … topology knowledge", and that is exactly what is reused). Capacity bar: `static_assert(N >= 2 && N <= 4, "Filter count must be 2-4")` (`:72-73`), explicit instantiations for 2/3/4 only (`:671-673`). Four pieces of knowledge transferred with citations: (a) **previous-sample coupling** — `processNetwork` reads the delayed outputs of *all* filters including itself into `filterInputs[to]` before any filter runs (`:569-580`), and `filterOutputs[i]` is written only after (`:613`), which makes the network's behaviour independent of loop index order (FR-031); (b) **per-loop `tanh` before the feedback routing** — "Apply soft clipping (tanh) for stability before feedback routing (FR-011)" (`:609-610`) (FR-042); (c) **DC blocking on the feedback path, after the delay** (`:591`, `kDCBlockerCutoff = 10.0f` at `:89`) (FR-014); (d) **advance every smoother even on the skipped path** — the `else` branch at `:595-598` advances the delay smoother it did not use, because a smoother that stops advancing desynchronises from its neighbours (FR-034). Also transferred: the NaN/Inf entry guard that calls `reset()` (`:639-642`) and `detail::flushDenormal` on the summed output (`:626`). Not modified. |
| `FeedbackNetwork` (L3) | `systems/feedback_network.h:57` | **Single-loop prior art — read, NOT instantiated.** Its in-loop stage order is the one roadmap line 272 restates: read the delay, then filter, then saturate, then DC-block, then scale (`:174-200`). Two facts recorded so their absence here is a decision and not an oversight: `kMaxFeedback = 1.2f` "120% for self-oscillation" (`:64`) is a delay-effect contract this component rejects (Non-Goals); and its per-sample filter call is `filterL_.processSample(...)` (`:191`) — i.e. the expensive `MultimodeFilter` path measured in Overview 1, in a component that runs **one** loop, not six. Not modified. |
| `FlexibleFeedbackNetwork` (L3) | `systems/flexible_feedback_network.h:48` | **Read, NOT instantiated.** `kMaxDelayMs = 10000.0f` (`:51`), `void process(float* left, float* right, std::size_t, …) :175`, `void setProcessor(IFeedbackProcessor*, …) :323`, `void snapParameters() :453`, `[[nodiscard]] std::size_t getLatencySamples() const :437`. What transfers is `snapParameters()`'s existence as a named idiom — a prepared network must be able to reach steady state without a ramp — which FR-005 reproduces as part of `prepare()`. Its `IFeedbackProcessor` injection point (`primitives/i_feedback_processor.h:30`) is **not** used: that interface is stereo-block (`process(float*, float*, std::size_t) :45`) and virtual, and this component's loops are per-sample and mono. Not modified. |
| `NoiseOrganism` / `ResonanceDriftNetwork` (L3) | `systems/noise_organism.h:150,178,180,190,844,999`; `systems/resonance_drift_network.h:128-136,143-144,299,505-546,771,783,903-912` | **Convention source — read, and by design not included** (a same-layer include is legal per `tools/lint-layers.js:74` and `noise_organism.h:108` does it, but there is nothing here to consume). The Vorago house style this component matches, item by item: `kControlChunkSamples = 64` with a `static_assert` pinning it to the shared library-wide grid (`noise_organism.h:150-160`, `resonance_drift_network.h:135-136`); `kGainRampMs = 50.0f` (`noise_organism.h:178`); `kOutputClamp = 4.0f` (`noise_organism.h:180`); `kWakeSilenceEpsilon = 1.0e-6f` (`resonance_drift_network.h:265`); `kMinUsableSampleRate = 8000.0` and *why* it is not 1 Hz (`resonance_drift_network.h:283-296` — below 8 kHz the `[kMinResonatorFrequency, 0.45·fs]` pair **inverts**, and `std::clamp` with `hi < lo` is UB that MSVC's `<algorithm>` traps); the numbered, comment-annotated `prepare()` step order (`:322-405`); the **absolute** 64-sample control grid carried as a residue **across calls** rather than block-relative (`:530-546`, with the reason: a 36 + 28 split must run the same number of control steps as an unsplit 64); the normative argument contract — out-of-range index is a silent no-op / documented neutral, non-finite float is a **rejection** with the previous value standing, out-of-range float is clamped and the getter reports the clamp (`:549-576`); `getClampEngagementCount()` (`:904`); `getAllocatedBytes()` (`:912`, `noise_organism.h:999`); `PrepareConfig` nested with designated-initialiser-only construction (`:297-306`, `noise_organism.h:190-195`); the append-only per-lane salt table with overlap `static_assert`s (`:929-947`); and the sleep-edge state clear (`:52-56`). Not modified. |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Per-lane RNG streams. `nextFloat()` is bipolar `[-1,+1]` (`:59-63`); `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102-103`) is the lowbias32 finaliser with a guaranteed-non-zero result, load-bearing because `Xorshift32::seed()` silently substitutes its default for 0 (`:73-75`) and two lanes hashing to 0 would collapse onto one stream (`:97-100`). |
| `OnePoleSmoother` / `LinearRamp` (L1) | `primitives/smoother.h:134` / `:305` | Control smoothing. `LinearRamp`: `configure(float rampTimeMs, float sampleRate) :329`, `setTarget :342`, `getTarget :358`, `getCurrentValue :364`, `[[nodiscard]] float process() :370`, `isComplete :409`, `snapToTarget :414`, `snapTo(float) :421`. `LinearRamp` is the Phase-2/3 choice for gain gates (`noise_organism.h:98`, `resonance_drift_network.h:383-387`) and is what FR-060's 50 ms wake fade and FR-045's governor ramp use; `OnePoleSmoother` is what `SVF`'s internal smoothing already provides. |
| `detail::isNaN` / `isInf` / `isFinite` / `flushDenormal`, `dbToGain` / `gainToDb` (L0) | `core/db_utils.h:99` / `:260` / `:118` / `:245`, `:293` / `:317` | The `-ffast-math`-proof finiteness tests (bit pattern behind an opaque barrier) and the dB conversions. FR-008 forbids `std::isnan`/`std::isinf`/`std::isfinite`; `tools/lint-nonfinite-symbols.js` enforces it. `detail::constexprLn` (`:156`) over `detail::kLn2` (`:144`) is the constexpr-log idiom FR-012 needs, because `std::log2` is **not** constexpr in C++20 — it compiles as a GCC/MSVC builtin extension and is **rejected by Clang**, breaking the macOS and Linux legs while Windows stays green (`resonance_drift_network.h:255-266`). |
| `kPi`, `kTwoPi` (L0) | `core/math_constants.h:28`, `:32` | The RT60↔Q constants. **`core/audio_constants.h` is deliberately not included** (FR-001): its `kMaxAudioFreqHz` has no user here — the cutoff ceiling is `SVF::kMaxCutoffRatio * fs`. If a later phase introduces a use, the include comes back with that use. |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h:58 kSampleTolerance = 5.0e-4f`, `:61 kMetricTolerance = 2.5e-4`, `:63 struct RenderFingerprint`, `:122 compareFingerprints` for SC-010/SC-011; `allocation_detector.h:48 AllocationDetector` / `:111 AllocationScope` for SC-007; `artifact_detection.h:38 ClickDetectorConfig` / `:72 ClickDetection` / `:130 detect(...)` for SC-003; `signal_metrics.h:326 calculateSpectralFlatness`, `:222 calculateCrestFactorDb`; `spectral_flux.h:75 computeMagnitudeFlux`; `statistical_utils.h:41 computeMean` / `:76 computeStdDev`. **There is no coherence helper in the tree** (searched: `tests/test_helpers/*.h`), so SC-002's magnitude-squared-coherence estimator is a new plain function added to `tests/test_helpers/` and reused by every SC-002 arm — the Phase-4 precedent for a missing metric. |
| Perf-test idiom | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:1-90`; `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp` | The measurement basis SC-004 inherits: **ns per 512-sample block at 48 kHz** (`:67-76` — "A percent-of-core figure is not reproducible across dev machines or CI runners"), best-of-25 × 500 blocks after 400 warm-up blocks (`:78-84`), tagged `[.perf]` so the per-push CI filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` excludes it, with `static_assert`s on the checked-in baselines so the absolute ceiling is evaluated on every CI leg (`:26-28`). One block period at 48 kHz is **10 666 667 ns**, so the roadmap's 1 %/voice is **106 666 ns/block**. The **stop-and-surface rule** at `:59-65` is inherited verbatim by FR-080. |

## New components

ODR sweep run **this session**, verbatim, from the repo root:

```
$ grep -rn "class FeedbackEcology"  dsp/ plugins/   -> 0 hits
$ grep -rn "class MicroLoop"        dsp/ plugins/   -> 0 hits
$ grep -rn "class EnergyGovernor"   dsp/ plugins/   -> 0 hits
$ grep -rn "class EcologyLoop"      dsp/ plugins/   -> 0 hits
$ grep -rn "class FeedbackLoop"     dsp/ plugins/   -> 0 hits
$ grep -rn "class LoopMatrix"       dsp/ plugins/   -> 0 hits
$ grep -rn "class CouplingMatrix"   dsp/ plugins/   -> 1 hit:
      plugins/membrum/src/dsp/coupling_matrix.h:21  (namespace Membrum, :19)
$ grep -rni "ecology" dsp/ plugins/ tools/          -> 0 hits
$ grep -rn  "Governor" dsp/ plugins/                -> 0 hits
$ grep -rn "\bFeedbackEcology\b|\bMicroLoop\b|\bEnergyGovernor\b" dsp/ plugins/ tools/ -> 0 hits
$ ls dsp/include/krate/dsp/systems/feedback_ecology.h -> No such file or directory
```

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `FeedbackEcology` | 3 | `dsp/include/krate/dsp/systems/feedback_ecology.h` (new) | **0 hits**, as is the identifier `FeedbackEcology` anywhere in `dsp/`, `plugins/` or `tools/`; the word "ecology" appears nowhere in the codebase. Near-name symbols that **do** exist and are not shadowed: `FeedbackNetwork` (`systems/feedback_network.h:57`), `FlexibleFeedbackNetwork` (`systems/flexible_feedback_network.h:48`), `FilterFeedbackMatrix` (`systems/filter_feedback_matrix.h:72`), `IFeedbackProcessor` (`primitives/i_feedback_processor.h:30`), `FeedbackDistortion` (`processors/feedback_distortion.h:82`), `ReverseFeedbackProcessor` (`processors/reverse_feedback_processor.h:51`). None collide. |
| `FeedbackEcology::PrepareConfig` (nested struct) | 3 | same header | Nested, following `NoiseOrganism::PrepareConfig` (`noise_organism.h:190`) and `ResonanceDriftNetwork::PrepareConfig` (`resonance_drift_network.h:299`). Several unrelated nested `PrepareConfig` structs already coexist, so nesting is the established, collision-free form. |
| `FeedbackEcology::Loop` (nested private struct) | 3 | same header | Nested and **private**, following `ResonanceDriftNetwork::Peak` and `NoiseOrganism::DustGrain` (`noise_organism.h:196-200`, which records the same reasoning: `Grain` at `primitives/grain_pool.h:23` already exists at namespace scope). **`MicroLoop` and `EcologyLoop` were both swept clean (0 hits) and are still rejected as top-level names** — a namespace-scope type for a private implementation detail is a future ODR liability for no gain. |
| `FeedbackEcology::FilterMode` (nested enum class) | 3 | same header | Nested. **Not** named `SVFMode`, `FilterType` or `FilterMode` at namespace scope: `SVFMode` (`primitives/svf.h:38`) and `FilterType` (`primitives/biquad.h:68`) are both already at namespace scope in headers this component includes, and `EnvelopeFilter` shows the nested-enum precedent for exactly this collision (`processors/envelope_filter.h:89`, a nested `FilterType`). **APPEND ONLY** — it becomes a persisted plugin parameter at Phase 12. |

No new top-level free functions, no new namespace-scope enum types, no new enumerators on existing
enums, no new `ModSource` values. `tools/lint-odr.js` and `tools/lint-layers.js` must pass on the
result (SC-019).

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** — `FeedbackEcology` is a Layer 3 class in `namespace Krate::DSP`, declared in
  `dsp/include/krate/dsp/systems/feedback_ecology.h`, header-only. Its includes reach **down only**:
  `core/db_utils.h`, `core/math_constants.h`, `core/random.h`, `primitives/smoother.h`,
  `primitives/svf.h`, `primitives/biquad.h`, `primitives/crossfading_delay_line.h`,
  `primitives/dc_blocker.h`, `primitives/delay_line.h` (for `nextPowerOf2`, `delay_line.h:26`, which
  `getAllocatedBytes()` calls and which reaches this header today only transitively through
  `crossfading_delay_line.h:31` — an IWYU or clang-tidy pass on another leg would break it),
  `processors/brownian_drift.h`, `processors/envelope_follower.h`,
  `processors/resonator_bank.h` (for `rt60ToQ` and the resonator constants only), plus
  `<algorithm> <array> <cmath> <cstddef> <cstdint>`. It includes **no** Layer 3 or Layer 4 header, and
  in particular not `filter_feedback_matrix.h`, `feedback_network.h` or
  `flexible_feedback_network.h` — all three are read-only prior art (FR-090).
- **FR-002** — Construction is trivial and allocation-free, leaving the object in the same
  well-defined state `prepare(48000.0, PrepareConfig{})` produces
  (`slow_event_scheduler.h:197` precedent). `void prepare(double sampleRate, const PrepareConfig& config) noexcept`
  is the **only** allocating method; every other public method is `noexcept` and allocation-free.
  `PrepareConfig` fields: `std::size_t numLoops = 6` (clamped `[1, kMaxLoops]`) and
  `std::size_t maxBlockSamples = 2048` (clamped `[64, 8192]`, reported by FR-070 and sizing nothing —
  the render is per-sample with local dry capture, the Phase-3 shape at
  `resonance_drift_network.h:300-304`). Callers **must** use designated initialisers so no narrowing
  conversion hides in a positional brace init — Clang errors where MSVC does not
  (`noise_organism.h:190-194`).
- **FR-003** — The render entry point is
  `void processBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept`,
  overwriting the outputs. The inputs may alias the outputs in either pairing. Guard ladder, in this
  order (`resonance_drift_network.h:510-514`): **any** null pointer writes nothing on either channel
  and advances nothing; `numSamples == 0` is a no-op consuming no control step; an un-prepared
  instance fills exactly `numSamples` zeros on both channels and advances nothing.
- **FR-004** — `void reset() noexcept` rewinds all **audio and modulation** state — every `SVF`,
  `CrossfadingDelayLine`, `Biquad`, `DCBlocker`, the governor's `EnvelopeFollower`, the twelve
  `BrownianDrift` lanes, every ramp, both previous-sample coupling vectors (`prevY_` and `prevOut_`,
  FR-031), the control-grid phase, the lane counter and the clamp-engagement counter — while
  **preserving configuration**, identically in meaning to `NoiseOrganism::reset()` and
  `ResonanceDriftNetwork::reset()` (`resonance_drift_network.h:409-419`), so Phase 10 can reset all
  three siblings at the same moment. It re-derives every lane stream from the stored seed (FR-054), so
  `reset()` makes a render reproducible from the top. It does **not** restore the
  FR-013/FR-022/FR-033/FR-052/FR-053 default tables, the coupling matrix, loop gains, `mix` or
  `wetGain` — `prepare()` is the only path back to those (FR-005).
- **FR-005** — `prepare()` runs a numbered, comment-annotated step order in which each comment states
  what breaks if the step moves (`resonance_drift_network.h:320-405` shape). Three orderings are
  load-bearing and must be called out in the header: (a) each `CrossfadingDelayLine::setCrossfadeTime`
  call comes **after** that line's `prepare()`, because `prepare()` both sets `sampleRate_` and
  overwrites the crossfade time with its own default (`crossfading_delay_line.h:100-120`); (b) the
  seed is distributed **last**, because `BrownianDrift::setSeed` reseeds the RNG and the subsequent
  `reset()` snaps the output smoother, so a seed distributed before the lanes are prepared is
  discarded (`noise_organism.h:299-302`, `resonance_drift_network.h:399-402`); (c) **every call to
  `prepare()` — including a re-`prepare()` on a live, previously-configured object — first restores
  every default: the FR-013 resonator table (centre Hz, RT60, filter Q), the FR-022 delay table, the
  FR-033 default coupling ring, the FR-052/FR-053 wander tables, `kDefaultLoopGain`, `kDefaultMix` and
  `kDefaultWetGainDb`, discarding whatever the caller had configured before the call — the Phase-3
  `applyDefaults()` rule verbatim (`resonance_drift_network.h:362`), applied on **every** call, not
  only the first. `prepare()` is therefore not idempotent with respect to configuration; only
  `reset()` is (FR-004). `prepare()` ends by
  **snapping** every derived value to its steady state — no ramp, no crossfade in flight, every
  delay position set with `snapToDelaySamples` (`crossfading_delay_line.h:202`) rather than
  `setDelaySamples` (the `FlexibleFeedbackNetwork::snapParameters()` idiom, `:453`).
- **FR-006** — All parameter setters are callable at any time from the control thread of the owner's
  choosing. The component is not internally synchronised: the owner calls setters and `processBlock`
  from the same thread, as every Vorago component does. No setter allocates, locks, throws, or
  performs I/O.
- **FR-007** — The component runs an **absolute 64-sample control grid**: `kControlChunkSamples = 64`,
  with `static_assert(kControlChunkSamples == 64, "shared 64-sample control grid")`, and the phase is
  a **residue carried across `processBlock` calls**, not measured from the block start
  (`resonance_drift_network.h:530-546` — a 36 + 28 split must run exactly the control steps an unsplit
  64 runs, or SC-010 fails). All lane advances, all coefficient writes, the governor evaluation and
  every gate re-target happen on that grid; the per-sample path carries only the loop arithmetic, the
  ramps and the output clamp.
- **FR-008** — Finiteness is tested only with `Krate::DSP::detail::isNaN` / `isInf` / `isFinite`
  (`core/db_utils.h:99` / `:260` / `:118`). `std::isnan`, `std::isinf` and `std::isfinite` are
  forbidden anywhere in the component or its tests (`tools/lint-nonfinite-symbols.js`).
- **FR-009** — **The normative argument contract**, identical to Phase 3's
  (`resonance_drift_network.h:549-576`) so a Phase-10 caller learns one rule for all three components:
  an out-of-range loop index makes every setter a **silent no-op** and every getter return the
  documented neutral **without indexing the array** (`0.0f`, `0`, `false`, the enum's zero value); a
  **non-finite** float argument is **rejected** and the previous value stands; an out-of-range finite
  float is **clamped** and the matching getter reports the clamped value. The non-finite rejection is
  load-bearing, not hygiene: `std::clamp` does not reject NaN (with `v = NaN` both comparisons are
  false and `v` is returned), and the trace is fatal — a NaN cutoff reaches `SVF::setCutoff` →
  `computeG` → `std::tan(NaN)` → NaN coefficients, and `SVF::process` resets only on a non-finite
  **input sample** (`svf.h:360-363`), never on non-finite coefficients.

### FR-010 series — The micro-loop (roadmap line 272)

- **FR-010** — `kMaxLoops = 6`, with a runtime `numLoops` in `[1, kMaxLoops]` (default 6). Roadmap
  line 272 says "5–6 instances"; both are reachable and SC-022 exercises both. The count sizes
  nothing at runtime — all six loops are fixed-size members, and `numLoops` selects how many render
  and what FR-017 normalises by. Its setter and the mid-render semantics of a count change are
  **FR-075**.
- **FR-011** — **Stage 1, the filter.** One `SVF` per loop (`primitives/svf.h:110`), with per-sample
  coefficient smoothing enabled at construction (`enableSmoothing(true) :239`) so a control-rate
  cutoff write cannot click. Mode is selectable per loop from the nested
  `enum class FilterMode : std::uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 }` (APPEND ONLY),
  mapped to `SVFMode::Lowpass` / `Bandpass` / `Highpass`. Default `Lowpass` — the roadmap's dark
  identity (line 17, "subterranean").
- **FR-012** — **The loop filter's Q is clamped to `[SVF::kMinQ, SVF::kButterworthQ] = [0.1, 0.7071]`.**
  This is rung 1 of FR-041 and is a **requirement, not a taste**: at `Q <= kButterworthQ` the SVF's
  magnitude response peaks at exactly unity in Lowpass and Highpass and at `Q <= 1` in Bandpass, so
  the filter can never add gain anywhere in the spectrum. Above Butterworth it can add up to `Q`
  (≈ 30 at `SVF::kMaxQ`), which would put the loop's round-trip gain above 1 at some frequency for
  *any* feedback setting and make FR-041's contraction argument false. Resonance in this phase belongs
  to the **resonator** (FR-013), whose bandpass has constant unit peak gain by construction. The
  clamp is applied in the setter, which is the only place it cannot be bypassed. Default
  `kDefaultFilterQ = SVF::kButterworthQ` — the maximally flat setting, so the loop filter colours
  the spectrum without adding its own resonant bump; ringing at a distinct pitch is the resonator's
  job (FR-013), not the filter's.
- **FR-013** — **Stage 3, the resonator.** One plain `Biquad` per loop (`primitives/biquad.h:308`),
  targeted `FilterType::Bandpass`, **not** a `SmoothedBiquad` (Overview 2, OQ-1, D-3 amended this
  session). Its coefficients are computed **directly by this component**, using the same RBJ
  constant-peak-gain bandpass formula `ResonatorBank::updateFilterCoefficients` builds
  (`resonator_bank.h:650-682`, `b0 = alpha`, `b1 = 0`, `b2 = -alpha`, normalised by `a0 = 1 + alpha`),
  and are written with `Biquad::setCoefficients(const BiquadCoefficients&)` (`:325`) — **never**
  `Biquad::configure` (`:330`), because `configure` routes through `BiquadCoefficients::calculate`
  (`:673`), which clamps `Q` to `biquad.h`'s own `kMaxQ = 30.0f` (`:53`). That clamp is what
  `SmoothedBiquad::setTarget` (`:547`, `:554`) silently inherits, which is the defect OQ-1 found and
  this FR corrects: this component's Q ceiling is `resonator_bank.h`'s `kMaxResonatorQ = 100.0f`
  (`:51`), reached only by computing coefficients directly and calling `setCoefficients`, bypassing
  `calculate()` entirely. Its Q is **not** set
  directly: the control surface takes an **RT60 in seconds** and converts with the shipped
  `rt60ToQ(centreHz, rt60Seconds)` (`resonator_bank.h:92`), so this component and every other
  resonator in the library agree on what a decay time means. Centre frequency is clamped to
  `[kMinResonatorFrequency, kMaxResonatorFrequencyRatio * sampleRate]` and the requested RT60 to
  `[kMinDecayTime, kMaxDecayTime]`; the resulting Q is additionally clamped to
  `[kMinResonatorQ, kMaxResonatorQ] = [0.1, 100]` exactly as `ResonatorBank` does (`:662`). Because the
  RBJ bandpass is documented "Constant 0 dB peak gain" (`biquad.h:71`) and is built normalised
  (`resonator_bank.h:668-682`), an arbitrarily high Q here is safe: it lengthens the ring without
  raising the peak. That is rung 2 of FR-041.
  **Retunes are a stepped hard swap, not a glide.** `setLoopResonanceHz(i, …)` and
  `setLoopResonanceRt60(i, …)` recompute the coefficient set and call `setCoefficients` once, taking
  effect on the very next sample — there is no interpolation, because the resonator centre and RT60
  are user-set controls, not wander-driven, and a retune is therefore a rare control event rather than
  something automated at audio rate (OQ-1). FR-076 declares both setters **stepped** and SC-001 (d)
  exempts them from the click-freedom assertion by name, the same treatment already given
  `setLoopFilterMode`.
  **Default table**, one octave below FR-053's
  `kDefaultLoopCutoffHz`, index-for-index: `kDefaultLoopResonanceHz = {1200, 850, 600, 425, 300, 210}`
  Hz, `kDefaultFilterQ = SVF::kButterworthQ` (FR-012). The
  octave-below placement gives each loop an audible pitch under its comb rather than colouring only.
  **`kDefaultResonanceRt60 = 1.0f` seconds — not 2.0 s — and it is clamped per centre, not
  uniformly.** `kMaxResonatorQ = 100` bounds how long a ring can hold at a given centre frequency:
  the ceiling reaches **1.05 s at 210 Hz** (the lowest default centre) and only **0.18 s at 1200 Hz**
  (the highest), because a fixed Q's ring time falls as centre frequency rises. A 1.0 s *request* is
  therefore exactly reachable at the lowest centre (loop 5, 210 Hz) and is silently reduced by the Q
  clamp at every centre above it. `getLoopResonanceRt60(i)` (FR-070) is a **truthful getter**: it
  reports the RT60 implied by the coefficient set actually in force (i.e. derived back from the
  clamped `Q`, not an echo of the 1.0 s request), so a caller or test reading it back sees the real
  decay time rather than the number it asked for. 1.0 s keeps SC-001 (b)'s per-minute stationarity
  band easy to hold at every centre, including the shortest-ringing 1200 Hz loop. **This table joins
  FR-022/FR-033/FR-052/FR-053 in the reference patch's default tables**, so SC-001, SC-002, SC-003,
  SC-004, SC-006, SC-011, SC-021 and SC-022 are all measured on a stated resonator configuration. The
  header records all three constants — the centre-frequency table, `kDefaultFilterQ` and
  `kDefaultResonanceRt60` — with this reasoning, not merely their values.
  **The single-slot-`ResonatorBank`-per-loop alternative** (six `ResonatorBank` instances, each
  configured with one enabled slot) remains on the record: FR-080's stage probe measures it
  alongside this direct-coefficient `Biquad` and the decision stands as written here unless the
  probe shows the bank buys something a criterion needs, in which case this FR is revised before the
  build proceeds.
- **FR-014** — **Stage 4, the DC blocker.** One `DCBlocker` per loop, prepared at
  `kDcBlockerCutoffHz = 10.0f` — the value `FilterFeedbackMatrix` uses on its own feedback paths
  (`filter_feedback_matrix.h:89`) and `FeedbackNetwork` uses on its
  (`feedback_network.h:124-125`) — positioned **after** the resonator and **last in the circulating path** — the value it produces,
  `b_i`, is what the `tanh` and the next sample's input sum see (FR-015), so it sits before every
  feedback coefficient, per roadmap line 272 ("with `DCBlocker` in-loop"). It is the only in-loop stage whose magnitude can
  exceed unity: `2/(1+R) ≈ 1.00066` at Nyquist for a 10 Hz cutoff at 48 kHz. FR-041 absorbs that
  explicitly.
- **FR-015** — **The per-sample loop law**, for loop `i`, in this exact order:
  ```
  x_i  = inputTapGain_i * monoIn
         +  appliedOwnFb_i * prevY_i
         +  Σ_{j≠i} appliedCoupling_[j][i] * prevOut_j        // FR-035, the ONLY gain factors
  s_i  = svf_i.process(x_i)                       // FR-011
  d_i  = delay_i.process(s_i)                     // FR-020: write s_i, read the delayed tap
  r_i  = resonator_i.process(d_i)                 // FR-013: Biquad bandpass, stepped retune
  b_i  = dcBlocker_i.process(r_i)                 // FR-014
  y_i  = std::tanh(b_i * governorGain)            // FR-042, FR-044 — NO loop-gain factor here
  out_i = y_i * gate_i                            // FR-060's 50 ms wake ramp
  ```
  and the output stage, once per sample, after all loops have run:
  ```
  wetSum = Σ_i out_i * normGain          // FR-017: normGain tracks 1/sqrt(numLoops), FR-075
  wet    = clamp(wetGain * wetSum, -kOutputClamp, +kOutputClamp)   // FR-072 trim THEN FR-046 clamp
  outL   = (1 - mix) * dryL + mix * wet  // FR-072
  outR   = (1 - mix) * dryR + mix * wet
  ```
  **Two previous-sample vectors, not one** (FR-031): `prevY_i` is loop `i`'s own `y_i` — the
  **pre-gate** value — from the previous sample, read only by loop `i`'s own feedback term;
  `prevOut_j` is loop `j`'s own `out_j` — the **post-gate** value, `y_j * gate_j` — from the previous
  sample, read by every neighbour's cross-coupling term for every `j ≠ i`. Own feedback deliberately
  does **not** go through the gate: a loop's circulation is untouched by its own wake state, and the
  gate stays a pure output multiplier (FR-061); cross-coupling deliberately **does** go through the
  gate, so a loop fading toward sleep fades its contribution to its neighbours continuously with its
  gate rather than delivering its full undiminished signal for the whole 50 ms fade and then stepping
  to zero. Both vectors are formed from the previous sample before any loop's stages run for the
  current one, and both are rewritten only after every loop has produced its `y_i`/`out_i`.
  `governorGain` is the FR-045 ramp's per-sample value; `wetGain`, `mix` and every
  `inputTapGain` reach the per-sample path through the ramps FR-076 declares.
  **There is exactly ONE loop-gain factor per round trip, and it sits in the input sum.** An earlier
  draft of this law carried both an `ownFb_i * prevY_i` term *and* a `* loopGain_i` factor at the
  output, which would have applied the same stored quantity twice per circulation (0.90² = 0.81, not
  0.90) and would have made FR-035's `G_i` something other than the round-trip coefficient FR-041
  reasons about. The quantity is the loop's **own feedback gain** — stored once, written by
  `setLoopGain(i, …)`, read by `getLoopGain(i)`, bounded by `kMinLoopGain`/`kMaxLoopGain` (FR-018),
  normalised into `appliedOwnFb_i` by FR-035 and reported by `getLoopAppliedOwnFeedback(i)`
  (FR-071). The symbol `loopGain_i` does not appear anywhere else in this spec.
  Placing every feedback coefficient in the input sum is also what makes FR-041's contraction
  argument an ℓ∞ **row-sum** argument: the sum of the absolute coefficients feeding loop `i` is
  exactly `G_i ≤ kMaxTotalLoopGain`.
- **FR-016** — **The engine is mono.** The stereo input is summed to mono with a 0.5 factor before the
  taps (`resonance_drift_network.h:106-108` precedent: "sums it to mono for the engine"), the loops
  run once, and the wet mono is written to both output channels before the FR-072 crossfade against
  the untouched stereo dry. This halves the cost of the most expensive part of the phase and is what
  keeps FR-080's budget reachable. The consequence — a mono wet path — is stated in the Non-Goals and
  in D-6, not hidden.
- **FR-017** — The wet sum is normalised by `1 / sqrt(numLoops)` (`resonance_drift_network.h:110`,
  `:1234-1237`, `getNormalisationPeakCount() :897`), computed from **`numLoops`, never from the awake count**: an
  awake-count reading computes `1/sqrt(0)` = ∞ when every loop sleeps, and `∞ * 0.0f` is NaN. The
  divisor is reported by `getNormalisationLoopCount()` (FR-070). It reaches the per-sample path as
  `normGain` through a `LinearRamp` at `kGainRampMs = 50.0f`, re-targeted only by `setNumLoops`
  (FR-075), so a mid-render count change is a 50 ms glide rather than the 0.79 dB step that
  `1/sqrt(6) → 1/sqrt(5)` would otherwise be. `prepare()` snaps the ramp (FR-005).
- **FR-018** — **The loop's own feedback gain is strictly below unity.** `kMaxLoopGain = 0.90f`,
  `kMinLoopGain = 0.0f`, `kDefaultLoopGain = 0.72f`, written by `setLoopGain(i, …)`. Roadmap line 272
  says "gain (< 1)"; this is that, with the remaining margin to FR-041's total ceiling reserved for
  the coupling row (FR-035) and for the DC blocker's Nyquist overshoot (FR-014). It is applied
  **once** per circulation, in FR-015's input sum, and appears nowhere else in the per-sample path.
- **FR-019** — Every loop's five stages are cleared together by one private `clearLoopAudio(i)`:
  `svf_[i].reset()` (which also snaps the smoother targets via `reset() :277`), the delay clear
  described below, `resonator_[i].reset()` (`Biquad::reset() :385`, which
  clears the filter state and leaves the current coefficients — a hard-swap resonator has none of
  `SmoothedBiquad`'s coefficient smoothers to snap, FR-013), `dcBlocker_[i].reset()`, and
  `prevY_[i] = prevOut_[i] = 0.0f` (FR-031's two previous-sample vectors). It is called from
  `prepare()`, `reset()`, the FR-060 sleep edge and the FR-047 non-finite trap, and from nowhere else —
  one owner, so the four paths cannot drift apart.
  **The delay clear is split by caller thread, and the split is normative.** `delay_[i].reset()`
  (`crossfading_delay_line.h:123`) is an `std::fill` over the whole power-of-two buffer
  (`delay_line.h:281-285`) — 131 072 B per loop at 48 kHz and 524 288 B at 192 kHz (FR-082) — and two
  of the four callers run on the **audio thread**: the FR-063 sleep edge inside the control step and
  the FR-047 trap inside the per-sample body, where `setNumLoops(6 → 1)` can fire six of them inside
  one 64-sample control chunk. A single such fill already exceeds the whole chunk's share of FR-080's
  budget.
  - `prepare()` and `reset()` — **control-thread** calls with no real-time contract — keep the real
    O(buffer) `delay_[i].reset()`, and leave no read-mute window open. They are the only
    paths that leave the buffer literally zeroed, which is what SC-009 (b)'s reset-and-re-render
    reproducibility needs.
  - The FR-063 sleep edge and the FR-047 trap — **audio-thread** callers — instead open an **O(1)
    read-mute window of exactly one delay length**: the loop keeps writing but reads `0.0f` for
    `ceil(getCurrentDelaySamples()) + 1` samples, having snapped the line to its *current* position
    with `snapToDelaySamples` (`crossfading_delay_line.h:202`, which cancels any crossfade in flight
    and re-syncs both taps without touching the buffer). The delay position is **frozen** while the
    window runs — the control step skips that loop's delay write exactly as it does for a skipped
    loop — so the read head cannot outrun the fresh data; the target getters keep moving throughout
    (FR-062) and the position resumes tracking when the window closes.
  Both branches deliver FR-063's audible property, which is what the Dormancy deviation is justified
  in: **a woken loop refills from its input tap rather than replaying the stale ring.** `write()` and
  `read()` are separable public calls (`crossfading_delay_line.h:223`, `:233`), so a loop that writes
  without reading cannot emit anything the buffer already held. SC-004 (f) is the detector: it
  measures the block containing a simultaneous multi-loop sleep edge, and the block containing a trap
  fire, against the absolute per-block ceiling at both 48 and 192 kHz.

### FR-020 series — The delay stage and the delay-time staircase (roadmap lines 272, 281)

- **FR-020** — **Stage 2, the delay.** One `CrossfadingDelayLine` per loop, prepared with
  `maxDelaySeconds = (kMaxDelayMs + kDelayHeadroomMs) / 1000.0f` where `kMaxDelayMs = 500.0f` and
  `kDelayHeadroomMs = 20.0f`. Delay time is written with `setDelayMs` (`:195`) at control rate and
  read with `process(float)` (`:291`, which is `write` then `read`). The crossfade time is set once in
  `prepare()`, **after** the line's own `prepare()` (FR-005 (a)), to `kCrossfadeMs = 20.0f` — the
  line's own default (`:68`), restated explicitly so the value survives a future change of that
  default.
- **FR-021** — **The anti-zipper mechanism is the crossfading line's threshold, and the spec says so
  rather than claiming a smoother it does not have.** `setDelaySamples` starts a crossfade only when
  the target has drifted `kCrossfadeThresholdSamples = 100.0f` from the **active** tap
  (`crossfading_delay_line.h:78`, `:183`); below that the active read position does not move at all
  (`:189-190`). The realised delay is therefore a **staircase** of ≥ 100-sample steps joined by 20 ms
  equal-power crossfades, and there is no continuously-moving read pointer to produce the pitch
  artefacts and zipper that roadmap line 281 forbids. The component adds **no** smoother of its own on
  the delay path: one would be dead weight below the threshold and would fight the crossfade above it.
- **FR-022** — **Default base delay table**, six mutually prime values spread across the roadmap's
  10–500 ms range so no two loops share a comb and no sum is an integer multiple of another:
  `kDefaultLoopDelayMs = {41, 67, 109, 173, 281, 449}` ms. Range per loop:
  `[kMinDelayMs, kMaxDelayMs] = [10, 500]` ms (roadmap line 272), clamped in the setter.
  With loops 1–5 (or 1–6) selected, the shortest active loop is always 41 ms.
- **FR-023** — **The wander must actually move the delay, and the arithmetic is derived from the
  lane's *statistics*, not from its peak.** A crossfade fires only when the mapped delay has drifted
  `kCrossfadeThresholdSamples = 100` samples from the **active** tap — that is, from where the *last*
  crossfade left it (`crossfading_delay_line.h:180-190`) — so what governs the step rate is the
  **change** in the lane since the last step, not the lane's peak excursion. `BrownianDrift`'s
  stationary standard deviation is `kInternalStd = 0.5f` (`brownian_drift.h:101`; `:222-224` records
  that ±4 is "≥ 6 sigma … never reached in practice"), so the difference between two decorrelated
  lane readings has standard deviation `σ_Δ = 0.5·√2 = 0.707`. The lane displacement a step costs is
  ```
  Δlane_i = kCrossfadeThresholdSamples / (delayWanderFraction_i · baseDelayMs_i · 0.001 · sampleRate)
  ```
  At 44.1 kHz — the binding rate, since 100 samples is 2.268 ms there against 2.083 ms at 48 kHz and
  0.521 ms at 192 kHz — **the per-loop default fractions of FR-052 are chosen so every loop needs the
  same ≈0.3 lane displacement, about half of σ_Δ**:

  | loop | base ms | base samples @ 44.1 kHz | default fraction | samples per unit lane | Δlane a step costs | in units of σ_Δ |
  |---|---|---|---|---|---|---|
  | 0 | 41 | 1808 | 0.16 | 289 | 0.35 | 0.49 |
  | 1 | 67 | 2955 | 0.10 | 295 | 0.34 | 0.48 |
  | 2 | 109 | 4807 | 0.06 | 288 | 0.35 | 0.49 |
  | 3 | 173 | 7629 | 0.04 | 305 | 0.33 | 0.46 |
  | 4 | 281 | 12392 | 0.03 | 372 | 0.27 | 0.38 |
  | 5 | 449 | 19801 | 0.02 | 396 | 0.25 | 0.36 |

  **A single scalar default cannot satisfy this.** At a flat 0.08 the 41 ms loop needs Δlane = 0.69 ≈
  1 σ_Δ, whose crossings are rare enough inside a two-minute render that SC-005 would be a coin flip —
  the worst failure mode a gate can have. At a flat 0.16 the 449 ms loop would swing ±72 ms and be
  rectified by the `kMaxDelayMs` clamp. The table above swings ±6.6 ms (loop 0) to ±9.0 ms (loop 5),
  every excursion strictly inside `[kMinDelayMs, kMaxDelayMs]`, so **no default loop's lane is
  clipped**. **A caller can still configure a static delay** (a
  10 ms base at a 0.05 fraction is ±0.5 ms = 22 samples at 44.1 kHz, below the threshold, and that
  loop's delay will never move). That is a property of the shipped delay line, not a defect, and it is
  **observable**: `getLoopCurrentDelayMs(i)` (FR-071) reports the realised
  `getCurrentDelaySamples()` (`crossfading_delay_line.h:306`) converted to ms, so a caller and a test
  can both see it standing still. SC-005 asserts motion on the default table at 44.1 kHz and asserts
  the documented absence of motion on the sub-threshold configuration, so a build that silently
  never moves any delay cannot pass; its counts above the floor of "every loop steps at least once"
  are re-pinned from the build's own measurement of the step rate under the FR-055 mapping, under the
  FR-080 stop-and-surface rule.
- **FR-024** — Because each crossfade blends two taps of the same buffer at least 100 samples apart,
  a 20 ms comb transient is intrinsic to the staircase. It is bounded by the equal-power curve
  (`core/crossfade_utils.h:50 equalPowerGains`) and by the low `kDefaultMix` (FR-072). SC-003 measures
  it as a **click/discontinuity** budget rather than pretending it is absent.

### FR-030 series — The cross-coupling matrix (roadmap lines 274–275)

- **FR-030** — A `std::array<std::array<float, kMaxLoops>, kMaxLoops>` `coupling_`, where
  `coupling_[from][to]` is the fraction of loop `from`'s previous output added to loop `to`'s input.
  `coupling_[i][i]` is **not** used — self-feedback is `ownFb_i` (FR-018) and is a separate,
  separately-clamped quantity, so a matrix diagonal write is a silent no-op and the diagonal reads
  back `0.0f`.
- **FR-031** — **Coupling reads the previous sample, for every source including near neighbours — and
  reads a different previous sample than own feedback does.** Two `std::array<float, kMaxLoops>`
  members hold sample `n-1`'s state: `prevY_` holds each loop's own **pre-gate** `y_j`, read only by
  that same loop's own-feedback term in FR-015's input sum; `prevOut_` holds each loop's **post-gate**
  `out_j = y_j * gate_j`, read by every neighbour's cross-coupling term. All six `x_i` are formed from
  both arrays before any loop's stages run for the current sample, and both arrays are rewritten only
  after all loops have produced their `y_i` and `out_i`. This is `FilterFeedbackMatrix::processNetwork`'s
  previous-sample structure verbatim (`filter_feedback_matrix.h:569-580` then `:613`), generalised to
  two vectors instead of one because the gate now sits between the quantity a loop keeps for itself and
  the quantity it shares with its neighbours (FR-062, FR-063). It is still what makes the network's
  output **independent of loop index order** — without it, loop 5 would hear loop 0's *current* sample
  and loop 0 would hear loop 5's *previous* one, and renumbering the loops would change the sound — and
  it is additionally what makes a fading loop's contribution to its neighbours fade continuously with
  its gate rather than deliver undiminished energy until the gate settles and then step to zero:
  under a single pre-gate array a fading loop's neighbours would see up to a `kMaxCouplingPerPair = 0.5`
  amplitude step at the instant the gate reaches exactly zero, which SC-014 (c) and SC-001 (d) would
  then have had to tolerate rather than reject.
- **FR-032** — `void setCoupling(std::size_t from, std::size_t to, float amount) noexcept`, and a bulk
  `void setCouplingMatrix(const std::array<std::array<float, kMaxLoops>, kMaxLoops>&) noexcept`
  (`filter_feedback_matrix.h:188` precedent — the declaration itself; `:186` is a blank `///` line
  inside the same doc block). Per-pair range
  `[0.0f, kMaxCouplingPerPair] = [0, 0.5]`. **Coupling is non-negative here**, unlike
  `FilterFeedbackMatrix::kMinFeedback = -1.0f` (`:84`): a negative coupling is a phase inversion, and
  with six loops and a governor it produces cancellation notches that read as level drops rather than
  as interaction (`:178`, "negative inverts phase"). Excluded, and recorded in D-5.
- **FR-033** — **Default coupling: a ring, at "a few %".** Roadmap line 274 says "each loop bleeds a
  few % into its neighbours". The default is `kDefaultCoupling = 0.04f` from each loop to its two
  cyclic neighbours (`i → i±1 mod numLoops`) and 0 elsewhere. A ring rather than an all-to-all default
  because six loops all-to-all at 4 % is a row sum of 0.20, five times the ring's 0.08, and the
  roadmap's "neighbours" is literal. All-to-all remains reachable through `setCouplingMatrix`.
- **FR-034** — Every per-pair coupling value passes through its own `OnePoleSmoother` configured at
  `kCouplingSmoothMs = 20.0f`, and **every smoother advances on every control step whether or not its
  path contributed** — the `else` branch at `filter_feedback_matrix.h:595-598` exists precisely because
  a smoother that stops advancing desynchronises from its neighbours and produces a step when its path
  re-engages. **One narrow, correctness-preserving carve-out (OQ-2, lever 3):** a coupling-pair
  smoother that has already **settled** — its current value bit-exact at its target — may skip its
  `process()` call for that control step, because advancing a one-pole smoother that is already at its
  target is a no-op by construction and produces no desync: the value it would write is the value
  already there. This is a CPU lever only, applied under FR-080's stop-and-surface rule, never a
  general licence to skip an unsettled smoother.
- **FR-035** — **Row-sum normalisation, the structural half of boundedness.** For each destination
  loop `i`, on each control step, define `G_i = ownFb_i + Σ_{j≠i, j<numLoops} coupling_[j][i]` **from
  the smoothed values** (FR-034, FR-076) — the normalisation is the **last** step before the
  coefficients are handed to the per-sample path, so the applied row sum is bounded at every instant
  and not only at the settled endpoints. **The sum ranges over `j < numLoops`, excluding `i`, and
  ignores wake/dormancy state**: a slot at or above `numLoops` contributes nothing and is not counted
  in the sum at all, while a slot below `numLoops` that happens to be asleep still counts, exactly as
  if it were awake. Two consequences follow directly, both intentional: `numLoops = 1` normalises the
  survivor's own feedback alone (`G_0 = ownFb_0`, so `getLoopAppliedTotalGain(0)` equals `getLoopGain(0)`
  whatever the unused slots 1–5 hold — see the `numLoops = 1` Edge Cases entry and SC-015 (d)), and a
  neighbour going dormant does **not** change any survivor's applied total gain — `getLoopAppliedTotalGain(i)`
  is therefore a pure function of `numLoops` and the configuration, never of which loops happen to be
  awake at the instant it is read. If `G_i > kMaxTotalLoopGain`, **every
  term contributing to `G_i` is scaled by `kMaxTotalLoopGain / G_i`** before it is used, so the
  applied total is exactly `kMaxTotalLoopGain`. `kMaxTotalLoopGain = 0.95f`. The normalisation is
  applied to the **applied** values on the control grid, never to the stored targets, so a caller's
  configuration survives round-trip through the getters (FR-070) while the render stays bounded — and
  `getLoopAppliedTotalGain(i)` (FR-071) makes the difference visible. SC-015 asserts that the applied
  total never exceeds `kMaxTotalLoopGain` for any reachable configuration, including the worst case
  `ownFb = kMaxLoopGain` with all five neighbours at `kMaxCouplingPerPair` (raw `G_i = 0.90 + 2.50 =
  3.40`, applied 0.95).

### FR-040 series — Boundedness ladder and the energy governor (roadmap lines 276, 280)

- **FR-040** — **The five rungs are independent and are each separately asserted.** They are listed
  here as one requirement because the phase's central claim is the *conjunction*, and a reader who
  sees only one of them will mis-scope the tests. Rung 1 structural (FR-041), rung 2 `tanh`
  (FR-042), rung 3 governor (FR-044), rung 4 output clamp (FR-046), rung 5 non-finite trap (FR-047).
  No rung may be removed on the grounds that another one covers it.
- **FR-041** — **Rung 1: the linear network is a strict contraction, by construction.** Every in-loop
  stage is non-expansive except the DC blocker: `SVF` at `Q <= kButterworthQ` peaks at unity (FR-012);
  the RBJ bandpass has constant unit peak gain (FR-013, `biquad.h:71`); the delay is a pure delay plus
  an equal-power two-tap blend, whose combined gain is `cos θ + sin θ <= sqrt(2)` in the worst case of
  two *fully correlated* taps — bounded, and the taps are ≥ 100 samples apart in a decorrelating
  network, but the `sqrt(2)` is what FR-042's clip is sized against rather than assumed away; the
  `DCBlocker` peaks at `2/(1+R) ≈ 1.00066`. Every feedback coefficient — own feedback and every
  incoming coupling — sits in FR-015's input sum and is applied **once** per circulation, so the sum
  of the absolute coefficients feeding loop `i` is exactly FR-035's `G_i <= kMaxTotalLoopGain = 0.95`
  and the worst-case single-sample round-trip magnitude gain is `0.95 × 1.00066 ≈ 0.9506 < 1`: the
  linearised system's induced ℓ∞ gain is below unity and its state decays in the absence of input.
  The argument is stated for **settled coefficients**; a resonator retune (FR-013) is a **stepped hard
  swap** between two independently-computed RBJ bandpass coefficient sets, each individually
  unity-peak by construction, so there is no interpolated intermediate coefficient set for this rung
  to reason about and no transient magnitude excursion at a retune. The swap's audible discontinuity
  is a click concern the spec accepts and names explicitly (FR-013, FR-076), not a boundedness
  concern rung 1 needs to cover — rungs 2 and 3 remain the safety net for any other transient FR-040
  anticipates, which is why it forbids removing a rung on the grounds that another covers it. **The
  header must carry this arithmetic**, not a claim.
- **FR-042** — **Rung 2: per-loop `tanh` before the feedback routing.** `y_i = std::tanh(...)`
  (FR-015), placed exactly where `FilterFeedbackMatrix` places it — "Apply soft clipping (tanh) for
  stability before feedback routing (FR-011)" (`filter_feedback_matrix.h:609-610`). It bounds
  `|y_i| <= 1` **unconditionally**, whatever a future coefficient change, sample-rate extreme or
  arithmetic surprise does to rung 1, and it therefore bounds the wet sum at
  `sqrt(numLoops) <= 2.449` before the FR-072 trim. That bound is also why rung 4's clamp cannot be
  reached from the audio input at any drive level (FR-046) — a fact SC-013 had to be rewritten
  around. It is a bound, not a tone control: at the
  levels FR-018's default gain produces, `tanh` is within 2 % of linear.
- **FR-043** — **The governor's tracker.** One `EnvelopeFollower` in `DetectionMode::RMS`
  (`envelope_follower.h:43`), fed **`normGain * Σ_i b_i`** per sample — the loops' post-DC-blocker
  values, summed and then scaled by the **same** `1 / sqrt(numLoops)` `normGain` FR-017 applies to the
  wet sum (via the FR-075 ramp) — the quantity the governor is controlling, measured before its own
  action (pre-governor, pre-gate, pre-`tanh`), which is what makes the loop a first-order regulator
  rather than an oscillator. Scaling by `normGain` here, and not only at the wet-sum output, is
  load-bearing: without it, `Σ_i b_i` grows with `numLoops`, so `kGovernorThresholdDb` would
  name a different absolute wet level at `numLoops = 1` than at 6 — up to ≈ 7.8 dB apart
  (`10·log10(6)`) — and the threshold's meaning would shift again every time a loop sleeps or
  `setNumLoops` (FR-075) is called. With `normGain` applied, the tracked quantity **is** the wet
  signal's own level, so the threshold names one fixed level at every loop count and survives dormancy
  and `setNumLoops` unchanged (Assumption 1's calibration). The cost is one multiply per sample, using
  the ramped `normGain` value already computed for the output stage — no second ramp.
  `setAttackTime(kGovernorAttackMs = 20.0f)`, `setReleaseTime(kGovernorReleaseMs = 800.0f)` (inside
  `[kMinReleaseMs, kMaxReleaseMs] = [1, 5000]`, `:90-91`), **sidechain disabled** — the governor must
  see sub-bass, which is Vorago's identity, and `setSidechainEnabled` defaults off. The follower is
  advanced every sample and **read only on the control grid**.
- **FR-044** — **Rung 3: soft downward compression of the whole network.** On each control step, with
  `rms = follower.getCurrentValue()` and `over = rms / threshold`:
  ```
  targetGain = (over <= 1.0f) ? 1.0f
                              : std::clamp(std::pow(over, 1.0f/ratio - 1.0f), kGovernorMinGain, 1.0f)
  ```
  `threshold = dbToGain(governorThresholdDb_)`, default `kGovernorThresholdDb = -52.0f`, range
  `[-72, 0]` dB; `ratio` default `kGovernorRatio = 8.0f`, range `[1, 20]`;
  **AMENDED DURING THE BUILD (T012), BY MEASUREMENT.** The default and the range floor read `-6.0f`
  and `-36` dB until SC-006's sweep was first run; both came from Assumption 1, which sized a
  **tracker** threshold from the component's **input** level. FR-043's tracker reads
  `normGain · Σ_i b_i`, and on the default tables every loop carries a `Q ≈ 100` resonator whose
  equivalent noise bandwidth is 3.3–18.8 Hz out of 24 kHz, so a broadband drive reaches the tracker
  ≈ 30 dB down: measured at 48 kHz on the reference patch, the tracker reads **−42.3 dB at the
  −12 dBFS reference drive** and **−31.0 dB at 0 dBFS**, i.e. the entire old `[-36, 0]` dB window sat
  above every level the tracker can produce and the governor was inert at every setting, leaving rung
  3 of FR-041 decorative. This is the re-measurement SC-006 (a) prescribes in advance
  ("re-measure and record `kGovernorThresholdDb` the Phase-3 way, never move an assertion"); the
  measured sweep, the reason for `-52` specifically and the rejected alternatives are recorded in the
  header's DERIVATION TABLE 3 (`feedback_ecology.h`), which is the normative record of the figure.
  `kGovernorMinGain = 0.05f`. `ratio == 1` is exactly unity gain (the exponent is 0), which is the
  documented "governor off" setting; the governor **can never mute** the network, which is the other
  half of "a drone left running overnight must neither die nor explode" (roadmap line 94–95). One
  `std::pow` per control step — 750 per second at 48 kHz — never per sample.
- **FR-045** — The governor gain reaches the per-sample path through a `LinearRamp` configured at
  `kGovernorRampMs = 20.0f` (`primitives/smoother.h:329`), re-targeted once per control step and
  advanced once per sample. There is no per-sample governor arithmetic.
  **The ramp is re-targeted only when the target actually changes** (added during the build, T012).
  `LinearRamp::setTarget` recomputes `increment_ = (target_ − current_)/rampSamples`
  (`smoother.h:353`) from the *current* position, so re-issuing an unchanged target every 64 samples
  converts the 20 ms linear ramp into a geometric approach covering 64/960 = 6.67 % of the remaining
  distance per step, which never trips `process()`'s overshoot clamp (`smoother.h:378-382`) — the
  only place a `LinearRamp` is set exactly equal to its target. It then stalls permanently one
  rounding step short: once the remainder falls under `rampSamples · ulp(1.0f)/2 = 2.9e-5`,
  `current_ += increment_` rounds back to `current_`. Measured before the guard, every SC-006 sweep
  step *after* the governor's first momentary engagement reported `getGovernorGain() == 0.999973f`
  even 20 dB below the threshold, which breaks SC-006 (a)'s "exactly `1.0f` below the threshold" and
  makes SC-006 (f)'s crossing level report the first momentary touch instead of the level at which
  the governor is engaged (a 6 dB error at `numLoops = 1`). With the guard the constant-target case
  is the shipped linear ramp and reaches exactly `1.0f` within `kGovernorRampMs`; while the law's
  target is genuinely moving the behaviour is unchanged.
- **FR-046** — **Rung 4: the hard output clamp, and where exactly it sits.** The clamp is applied to
  `wetGain * (Σ_i out_i * normGain)` — i.e. **after** the FR-017 normalisation **and after** the
  FR-072 wet trim, immediately **before** the FR-072 mix crossfade (FR-015's output-stage block is
  normative). Placing it before the trim would leave the single largest gain in the component
  downstream of its own guard: `setWetGain(+24 dB)` is ×15.85, and `2.449 × 15.85 = 38.8` would leave
  the component with the clamp having done nothing. Bound: `[-kOutputClamp, +kOutputClamp]` with
  `kOutputClamp = 4.0f` (`noise_organism.h:180`); every engagement increments a
  `std::uint32_t clampEngagements_` counter readable through `getClampEngagementCount()`
  (`resonance_drift_network.h:904-906`).
  **Zero engagements are required in every in-spec configuration whose `wetGain <= 0 dB`** — the
  counter is a defect detector there, not a limiter — and the scope of that clause is deliberate and
  narrow, because the clamp is **unreachable through the audio input**: FR-042's `tanh` bounds
  `|y_i| <= 1` for any input level whatsoever, so the wet sum cannot exceed `sqrt(numLoops) <= 2.449`
  against a ceiling of 4.0, at any drive, with any parameter set. **The one control that can reach the
  clamp is `setWetGain` above `+4.27 dB`** (`4.0 / 2.449`), and that is the fixture SC-013 (b) uses to
  prove the counter is wired. Above 0 dB of trim the clamp is a working limiter and engagements are
  expected, not a defect; the boundedness claim of roadmap line 280 holds across the whole `[-24, +24]`
  dB range precisely **because** the clamp is downstream of the trim.
- **FR-047** — **Rung 5: the non-finite trap.** After `b_i` is computed and before it is used, if
  `!detail::isFinite(b_i)`, the component calls `clearLoopAudio(i)` (FR-019), sets `y_i = 0.0f`,
  **also resets the governor's `EnvelopeFollower`** and increments a separate
  `std::uint32_t nonFiniteResets_` counter readable through `getNonFiniteResetCount()`. Both extra
  actions are required, not defensive: `DCBlocker::process` "propagates" NaN
  (`dc_blocker.h:188`) and has no self-heal, so a poisoned blocker never recovers on its own; and
  `EnvelopeFollower::processSample` "does NOT validate input" (`:163`), so one non-finite sample sets
  `squaredEnvelope_` to NaN, `std::sqrt(NaN)` is NaN, `flushDenormal` does not clear it, and the
  governor would multiply the entire network by NaN forever. The check is one ordered comparison per
  loop per sample and is `-ffast-math`-proof by construction (bit-pattern test behind an opaque
  barrier, `db_utils.h:250-259`).
  **Rung 5 is defence in depth: it is not reachable through the public API, and the header must say
  so** rather than leave a future reader reading a zero counter as evidence of a tested guard. The
  loop's first stage is `SVF::process`, which **resets that filter and returns `0.0f` on a non-finite
  input** (`svf.h:359-363`), and the `Biquad` behind it does the same (`biquad.h:353-356`), so a
  non-finite sample arriving through `processBlock` is intercepted before `d_i`, never reaches the DC
  blocker or the follower, and correctly leaves `getNonFiniteResetCount()` at zero (SC-012 (b)
  asserts exactly that). FR-009's rejection of non-finite setter arguments closes the other public
  route, the non-finite-coefficient trace Phase 3 found. What rung 5 still covers is everything that
  is not a public call: a future in-loop stage without a self-heal, an arithmetic surprise at an
  extreme sample rate, a coefficient path a later phase adds. Its counter must therefore read **zero**
  in every in-spec configuration (SC-012 (a)–(b)), and its behaviour is proven through the declared
  fault-injection probe of **FR-048** (SC-012 (c)).

- **FR-048** — **The rung-5 fault-injection probe: test-only, with zero shipping surface.** Because
  FR-047's trap cannot be reached through the public API, the header declares — and **never defines** —
  `struct FeedbackEcologyNonFiniteProbe;` in `namespace Krate::DSP::detail`, and `FeedbackEcology`
  friend-declares it. This is the shipped house pattern for exactly this problem, quoted from
  `systems/seraphis_engine.h:180-195` ("FR-072 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A
  TEST TU … cannot be reached through the public API … It costs nothing at run time and adds no public
  surface: the library never defines it, so a shipping build has no way to call it") with the friend
  at `:1074`. The Phase-5 non-finite TU defines it and uses it to write a non-finite value into (a)
  one loop's `DCBlocker` state and (b) the governor's `EnvelopeFollower` — the two stages FR-047 names
  as unable to self-heal — and nothing else. ODR: swept this session,
  `grep -rn "FeedbackEcologyNonFiniteProbe" dsp/ plugins/ tools/` → **0 hits**. SC-012 (c) is its only
  consumer; no shipping code path, no plugin and no other test may use it.

### FR-050 series — Per-loop life modulation (roadmap line 277)

- **FR-050** — Each loop owns **two** `BrownianDrift` lanes — delay time and filter cutoff — for
  twelve lanes total. They are private members, not a routing surface (Non-Goals). Roadmap line 277
  names exactly these two targets and no others; the resonator centre, RT60, loop gain and coupling
  do **not** wander in this phase.
- **FR-051** — Lanes are read on the control grid and their values are mapped, not applied raw. The
  `BrownianDrift` contract that makes this safe is quoted rather than assumed: output is hard-clamped
  to `[-1, +1]` (`brownian_drift.h:212-214`) and the per-sample slew is bounded at ≈1.39e-3 of the
  span at 48 kHz **independently of the OU step size**, because the walk only moves a
  `OnePoleSmoother` target (`:41-58`).
- **FR-052** — **The delay lane.**
  `delayMs_i(t) = clamp(baseDelayMs_i * (1 + delayWanderFraction_i * lane), kMinDelayMs, kMaxDelayMs)`.
  The default is a **per-loop table**, not a scalar:
  `kDefaultDelayWanderFraction = {0.16, 0.10, 0.06, 0.04, 0.03, 0.02}`, paired index-for-index with
  FR-022's `kDefaultLoopDelayMs`; per-loop range `[0, kMaxDelayWanderFraction] = [0, 0.5]`, settable
  through `setLoopDelayWander(i, …)` and read back by `getLoopDelayWander(i)`. The
  multiplicative form is deliberate: a fixed ±ms depth would move a 449 ms loop imperceptibly and a
  41 ms loop by a tenth of its length. **The table is not a taste** — FR-023 derives it from
  `BrownianDrift`'s stationary statistics so that every default loop needs the same ≈0.3 lane
  displacement (≈0.5 σ_Δ) to cross the delay line's 100-sample crossfade threshold at 44.1 kHz, which
  is what makes SC-005 a gate rather than a coin flip, and so that no default excursion is rectified
  by the `[kMinDelayMs, kMaxDelayMs]` clamp above.
- **FR-053** — **The cutoff lane**, in the log2 domain:
  `cutoffHz_i(t) = clamp(exp2(log2(baseCutoffHz_i) + cutoffWanderOctaves_i * lane), kMinCutoffHz, maxCutoffHz)`
  with `kMinCutoffHz = 20.0f`, `maxCutoffHz = SVF::kMaxCutoffRatio * sampleRate` (`svf.h:129`),
  `kDefaultCutoffWanderOctaves = 0.5f`, range `[0, kMaxCutoffWanderOctaves] = [0, 4]`. Default base
  cutoffs descend with loop index — `kDefaultLoopCutoffHz = {2400, 1700, 1200, 850, 600, 420}` Hz —
  so the longer loops are the darker ones, which is what makes six overlapping rings read as one
  receding body rather than six delays. The log2 constants use `detail::constexprLn` over
  `detail::kLn2` (`db_utils.h:156`, `:144`), **never a `constexpr` `std::log2`**, which Clang rejects
  (`resonance_drift_network.h:255-266`).
- **FR-054** — **Seeding.** `void setSeed(std::uint32_t seed) noexcept` stores the seed and re-derives
  all twelve lane streams as `deriveStreamSeed(seed_, base + loop)` (`core/random.h:102`) from an
  **APPEND-ONLY** salt table with overlap `static_assert`s, the Phase-3 shape
  (`resonance_drift_network.h:929-947`):
  `kSaltDelayLane = 0` (+loop), `kSaltCutoffLane = 16` (+loop), `kSaltNextFree = 32`. Renumbering a
  base silently changes every Phase-5 render. Every `setSeed` re-seed is followed by a mandatory
  per-lane `reset()`, because `BrownianDrift::setSeed` reseeds the RNG but does not rewind the walk
  (`brownian_drift.h:145-148` vs `reset() :133`).
- **FR-055** — **One organism-wide wander rate**, `setWanderRate(float hz)`, default
  `kDefaultWanderRateHz = 0.03f` (`noise_organism.h:163`, `resonance_drift_network.h:147`), range
  `[kMinWanderRateHz, kMaxWanderRateHz] = [0.002, 1.0]` Hz, mapped to every lane's
  `setSmoothness` plus a shared lane **decimation** (advance the lanes once every
  `laneDecimation_` control steps) exactly as Phase 3 does (`resonance_drift_network.h:139-141`,
  `kMaxLaneDecimation = 17` chosen so `17 × BrownianDrift::kTauMax >= 500 s`). `setWanderRate` is the
  **single owner** of the decimation and of every lane's smoothness — never recomputed anywhere else,
  so `prepare()` and a later caller cannot disagree about the mapping.
- **FR-056** — `void setWanderEnabled(bool)` **zeroes the FR-052/FR-053 depth term** —
  `delayWanderFraction_i` and `cutoffWanderOctaves_i`'s contribution to the mapping — while disabled,
  without stopping the lanes: the lanes keep advancing underneath, but with no depth the mapped delay
  and cutoff glide back to their base values over the FR-076 smoothing paths (the delay-line crossfade
  staircase; the `SVF`'s own coefficient smoother). This is Phase 3's shipped behaviour verbatim: "Off
  zeroes every DEPTH; it does not rewind a lane" (`resonance_drift_network.h:742-744`). Re-enabling
  restores the depth term, so the mapped value moves from base to wherever the (still-advancing) lane
  now sits — a jump, not a continuation, because the mapping was suspended rather than frozen; there is
  no reading of this setter under which a re-enable is jump-free, since the lane never stopped moving.
  `isWanderEnabled()` reports the state.

### FR-060 series — Loop life cycle and the Dormancy rule (roadmap lines 501–506)

- **FR-060** — `void setLoopWake(std::size_t loop, float amount) noexcept` (clamped `[0,1]`, the clamp
  load-bearing exactly as at `resonance_drift_network.h:760-771`: a Phase-8 agent driving wake from
  energy, or a Phase-10 caller writing `getEnvelopeValue() * getActiveDepth()`, must not be able to
  push a loop past unity or invert it) and
  `void setLoopDormant(std::size_t loop, bool dormant) noexcept`. Both fold into **one** steady-state
  gate value per loop, which is what makes `setLoopDormant(i, true)` and `setLoopWake(i, 0.0f)`
  behaviourally indistinguishable — the cross-cutting Dormancy rule's core claim (roadmap lines
  501–506, `resonance_drift_network.h:782-790`). Any wake at or below
  `kWakeSilenceEpsilon = 1.0e-6f` snaps to **exactly** `0.0f`, so the sleep edge's `== 0.0f` test is
  valid by construction and a release tail that stops at 1e-8 cannot leave a loop burning forever
  (`resonance_drift_network.h:260-266`).
- **FR-061** — The gate is a per-loop `LinearRamp` configured at `kGainRampMs = 50.0f`
  (`noise_organism.h:178`), applied per sample to `y_i` on the way to the wet sum (FR-015). Re-entry
  is therefore the Dormancy rule's "50 ms per-sample linear fade", verbatim.
- **FR-062** — **At a settled gate of exactly zero the loop's chain is skipped** — no `SVF`, no delay
  read/write, no `Biquad`, no `DCBlocker`, no `tanh` — and its `prevOut_i` (FR-031's **post-gate**
  vector, the one its neighbours' coupling reads) is exactly `0.0f`. This now **follows by
  construction** rather than from a special-cased write: `out_i = y_i * gate_i` (FR-015), the FR-061
  ramp reaches its target of exactly `0.0f` before the chain is skipped, so the last `out_i` produced
  before the skip began is already exactly zero, and nothing thereafter writes a nonzero value into it
  — it therefore contributes nothing to its neighbours' coupling for as long as the loop stays skipped.
  Its **two `BrownianDrift` lanes keep advancing**,
  which is the Dormancy rule's other half and is what makes a woken loop's delay and cutoff arrive
  already displaced rather than at the position they held minutes ago.
  **What still runs, and what does not, is normative here, because the read surface disagrees with
  itself otherwise.** For a skipped loop the component **does** advance the two lanes on the FR-055
  decimated grid and **does** evaluate the FR-052/FR-053 mappings, storing the results where
  `getLoopTargetDelayMs(i)` and `getLoopTargetCutoffHz(i)` (FR-071) report them. It **does not** write
  them to the audio objects: no `CrossfadingDelayLine::setDelayMs`, no `SVF::setCutoff`, no resonator
  retune, because those objects are not being run and a queued crossfade would fire at the wake edge
  instead of being snapped by FR-064. The consequence must be stated rather than discovered:
  `getLoopCurrentDelayMs(i)` and `getLoopCurrentCutoffHz(i)` are **frozen while a loop is skipped** —
  `getCurrentDelaySamples()` is the gain-weighted tap average and the active tap only advances when a
  crossfade completes inside `read()`, which is precisely the call being skipped
  (`crossfading_delay_line.h:169-174`, `:245-285`, `:306`). The *target* getters are therefore the
  only observable proof that the lanes are alive, and SC-014 (b) asserts both halves: targets moving,
  realised values still.
- **FR-063** — **The sleep edge clears the loop's audio state** (`clearLoopAudio(i)`, FR-019), and
  this is a **deviation from the naive reading of the Dormancy rule that the spec must justify**,
  since the rule as written only says the chain is skipped. A feedback loop has no generator behind
  it — the loop *is* the chain — so "skipping" it freezes a fully charged delay line. On the wake
  edge that frozen ring would be re-injected at full amplitude behind a 50 ms fade, minutes after the
  audio that produced it: a listener hears a stale burst, not a loop opening. Clearing on the sleep
  edge makes a woken loop refill from its input tap, which is what "a feedback agent opens an
  ecology loop's coupling" (roadmap line 351) is supposed to sound like. The Dormancy rule's own
  escape clause is satisfied in the direction it asks for — this spec says what the listener would
  hear — and the clear is asserted by SC-014 (d).
- **FR-064** — The wake edge (gate target leaving zero) re-arms the loop **before** the ramp starts —
  the delay line is snapped to its current mapped delay with `snapToDelaySamples`
  (`crossfading_delay_line.h:202`) so the first samples after a wake are not spent in a crossfade from
  a stale tap position, and the `SVF` is written with its current mapped cutoff and then
  `snapToTarget()` (`svf.h:309`) so the filter does not spend the first 5 ms of the wake gliding from
  a minutes-old coefficient. Both writes are safe at the edge precisely because the loop is silent and
  FR-063 has already cleared its state — there is no signal in flight to click. This mirrors Phase 3's
  wake edge, which re-applies the bank slot and enables
  it on the *setter call* rather than up to 63 samples later (`resonance_drift_network.h:774-778`).

### FR-070 series — Control and read surface

- **FR-070** — **Configuration read surface**, all `[[nodiscard]] noexcept`, all returning the
  **clamped** value: `getNumLoops()`, `getMaxBlockSamples()`, `getSampleRate()`, `isPrepared()`,
  `getWanderRate()`, `isWanderEnabled()`, `getMix()`, `getWetGain()`, `getGovernorThresholdDb()`,
  `getGovernorRatio()`, `getNormalisationLoopCount()`, `getAllocatedBytes()`; and per loop
  `getLoopDelayMs(i)`, `getLoopCutoffHz(i)`, `getLoopFilterMode(i)`, `getLoopFilterQ(i)`,
  `getLoopResonanceHz(i)`, `getLoopResonanceRt60(i)`, `getLoopGain(i)`, `getLoopInputGain(i)`,
  `getLoopDelayWander(i)`, `getLoopCutoffWander(i)`, `getLoopWakeAmount(i)`, `isLoopDormant(i)`; and
  `getCoupling(from, to)`. Out-of-range index returns the documented neutral without indexing
  (FR-009).
- **FR-071** — **Realised-state read surface** — the values the render is actually using, which the
  configuration reads cannot show once smoothing, wander and FR-035 normalisation have acted:
  `getLoopCurrentDelayMs(i)` (from `CrossfadingDelayLine::getCurrentDelaySamples() :306`),
  `getLoopCurrentCutoffHz(i)`, `getLoopTargetDelayMs(i)`, `getLoopTargetCutoffHz(i)`,
  `getLoopCrossfadeCount(i)`, `getLoopAppliedOwnFeedback(i)`, `getLoopAppliedCoupling(from, to)`
  (FR-035's per-pair coefficient **after** normalisation), `getLoopAppliedTotalGain(i)` (FR-035's
  `G_i` after normalisation), `getLoopGate(i)`, `isLoopEngineActive(i)`, `getGovernorGain()`,
  `getGovernorRms()`, `getLaneDecimation()`, `getClampEngagementCount()`, `getNonFiniteResetCount()`.
  Five of these are here because a criterion could not otherwise be written against a correct
  implementation:
  - `getLoopTargetDelayMs(i)` / `getLoopTargetCutoffHz(i)` — the FR-052/FR-053 **mapped lane values**
    as of the last control step, reported **whether or not the loop is skipped** (FR-062). They are
    the continuous lane trajectories; the delay's `Current` value is the realised crossfade staircase,
    frozen while a loop sleeps and quantised to ≥ 100-sample steps while it runs (FR-021). SC-009 (d)
    correlates the target series (a near-constant staircase makes a Pearson correlation meaningless)
    and SC-014 (b) asserts lane advance on them.
  - `getLoopCurrentCutoffHz(i)` — returns `svf_[i].getCutoff()` (`svf.h:328`), the last **commanded**
    cutoff — no `SVF` amendment (FR-090), no component-side smoothed mirror. Unlike the delay pair,
    where `Current` is genuinely a different, staircased value from `Target` even while the loop runs,
    the cutoff `Current` getter **coincides exactly** with `getLoopTargetCutoffHz(i)` at every sample
    while the loop is running: `SVF`'s own per-sample coefficient smoothing (FR-011) changes the
    filter's internal `g`/`k` state, not the value `getCutoff()` reports, so there is no smoothed
    intermediate value to read back. The two cutoff getters diverge **only** under FR-062's write
    skip while dormant, when `setCutoff` is not called at all and the last commanded value stands
    still while the target keeps moving. SC-014 (b) asserts the divergence for cutoff and the freeze
    for delay as two distinct claims, not one.
  - `getLoopCrossfadeCount(i)` — a monotone `std::uint32_t` of crossfades **started** on that loop's
    delay line. A crossfade can only begin inside `setDelaySamples`
    (`crossfading_delay_line.h:180-190`), which this component calls only on the control grid, so the
    counter is incremented there when `isCrossfading()` (`:301`) reads true having read false at the
    previous write — an exact count of onsets, not of completed steps, and the two differ whenever a
    crossfade is retriggered mid-fade (`:176-181`). SC-003 (c) counts these instead of guessing at
    step detection.
  - `getLoopAppliedCoupling(from, to)` — the coupling coefficient in force from loop `from` into loop
    `to`, after FR-035's row normalisation; `0.0f` for an out-of-range index or `from == to`, without
    indexing (FR-009), and it adds no state. Without it SC-015 cannot observe the normalisation at
    all: `getLoopAppliedTotalGain(i)` is `min(G_i, kMaxTotalLoopGain)` **by construction**, so it
    satisfies SC-015 (a)'s clauses identically whatever coefficients are actually in force, and a
    build that computed the reported total correctly but dropped the normalisation scale from the
    coefficient writes passes unchanged. The per-pair getter is what turns that criterion from a
    tautology into a measurement.
  The rest exist for the same reason: SC-005 reads the realised delay, SC-006 the governor gain,
  SC-014 the gate and the engine-active flag, SC-015 the applied per-pair and total gains.
- **FR-072** — **Output stage.** `setMix(float)` clamped `[0,1]`, default
  **`kDefaultMix = 0.15f`** — roadmap line 278, "output mixed back at low level". At `mix == 0.0f`
  the dry path is **bit-exact** (the wet is not merely attenuated; it is not summed) while every loop,
  lane, governor and ramp keeps advancing, so a mix automation from 0 does not reveal a network that
  was asleep. A static `setWetGain(float dB)` trim, default `kDefaultWetGainDb = 0.0f`, range
  `[kMinWetGainDb, kMaxWetGainDb] = [-24, +24]` dB, exists for Phase 10 to balance the return without
  moving the crossfade; unlike
  Phase 3's `kDefaultWetGainDb = 34.5f` (`resonance_drift_network.h:230`) it is **not** a measured
  constant, because this component's loops are broadband and unity-ish rather than twelve narrow
  bandpasses — recorded in D-8.
  **Order in the output stage is normative** (FR-015's block, FR-046): the trim multiplies the
  normalised wet sum, the FR-046 clamp is applied to the **result of that multiplication**, and only
  then does the mix crossfade run. The range keeps its positive half — a return trim that can only
  attenuate is half a control, and Phase 10 may well need to lift a quiet ecology — and the cost of
  keeping it is stated rather than hidden: above `+4.27 dB` the trim can drive the FR-046 clamp
  (`4.0 / sqrt(6) = 1.633`, i.e. `+4.27 dB` above the `tanh` bound), so FR-046's zero-engagement
  requirement is scoped to `wetGain <= 0 dB`, SC-001's sweep draws `wetGain` from `[-24, 0]` dB, and
  SC-013 (b) uses `+24 dB` as the fixture that proves the clamp counter is wired. `mix` and `wetGain`
  both reach the per-sample path through the ramps FR-076 declares; at a **settled** `mix == 0.0f`
  the wet is not summed at all, which is what makes SC-018 (a) a bit-exact claim.
- **FR-073** — **The tap entry point.**
  `void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR, float* const* loopTaps, std::size_t numSamples) noexcept`,
  where `loopTaps` may be null (then it is exactly `processBlock`) or an array of `kMaxLoops`
  pointers, each of which may itself be null. When a pointer is non-null the loop's post-gate `out_i`
  is written to it per sample. **The main output must be bit-identical between the tapped and
  untapped paths** (SC-016) — the tap is an observation, never a code path with its own arithmetic.
  `processBlock` is implemented as `processBlockTapped(..., nullptr, ...)`, one function body, so the
  two cannot drift apart.
- **FR-074** — **Per-loop input taps.** `void setLoopInputGain(std::size_t loop, float gain)`,
  clamped `[0, 1]`, default `kDefaultLoopInputGain = 1.0f` for every loop
  (`filter_feedback_matrix.h:357` precedent — `inputGains_.fill(1.0f)` in the constructor's
  member-init block, which is where that class actually establishes default-to-unity; `:203` is only
  the `@param filterIndex` doc line of `setInputGain` and shows no default). Written through the
  per-sample ramp FR-076 declares, so a tap opening from zero is a 20 ms fade and not a step in `x_i`. Roadmap line 278's "input taps from cloud + noise
  organism" is the **caller's** sum: this component takes one stereo input and decides only how much
  of it each loop hears.

- **FR-075** — **`void setNumLoops(std::size_t n) noexcept`, and what a mid-render count change
  does.** `n` is clamped to `[1, kMaxLoops]`; the setter allocates nothing, because all six delay
  lines are prepared regardless (FR-081, FR-082). No FR before this one defined the setter, and three
  criteria call it, so its semantics are normative here:
  - Loops with index `>= n` are **dropped exactly the way a sleep is done**: their FR-061 gate target
    goes to zero, they fade over `kGainRampMs = 50 ms`, and they take the FR-063 sleep-edge clear when
    the gate settles at exactly zero. They are excluded from their neighbours' coupling as soon as the
    gate reaches zero, because `prevOut_i` is then held at `0.0f` (FR-062).
  - Loops re-admitted by a later, larger `n` take the FR-064 wake path unchanged.
  - The FR-017 divisor changes to the **new** `numLoops` immediately as a target — `getNumLoops()` and
    `getNormalisationLoopCount()` report it the moment the setter returns — but reaches the per-sample
    path through the `normGain` `LinearRamp` at `kGainRampMs`, so the `1/sqrt(6) → 1/sqrt(5)` change
    (0.79 dB) is a 50 ms glide. Stepping it would put a 0.79 dB discontinuity into a continuous signal
    at the instant of the call, against SC-022's own click assertion.
  - The divisor is **always** `numLoops`, never the awake count (FR-017), so the dropped loops' fade
    and the divisor ramp overlap by design; the worst-case transient is under 0.8 dB over 50 ms.
- **FR-076** — **Every scalar setter declares whether it is stepped or smoothed, and with what time
  constant.** SC-001 (d) jumps parameters to their extremes and asserts click-freedom, so a setter
  whose smoothing is unspecified is an unbuildable requirement. The table is normative and exhaustive;
  anything not listed does not exist on the surface.

  | Setter | Path to the per-sample math | Time constant |
  |---|---|---|
  | `setMix` | per-sample `LinearRamp` on the crossfade coefficient | `kMixRampMs = 20.0f` |
  | `setWetGain` | per-sample `LinearRamp` on the linear trim gain | `kMixRampMs` |
  | `setLoopInputGain` | per-sample `LinearRamp`, one per loop | `kMixRampMs` |
  | `setLoopGain` (own feedback) | control-grid `OnePoleSmoother`, then FR-035 normalisation | `kCouplingSmoothMs = 20.0f` |
  | `setCoupling` / `setCouplingMatrix` | per-pair control-grid `OnePoleSmoother`, then FR-035 (FR-034) | `kCouplingSmoothMs` |
  | `setLoopCutoffHz`, `setLoopFilterQ` | `SVF`'s own per-sample coefficient smoothing, enabled at construction (FR-011) | `SVF::kDefaultSmoothingTimeSec = 0.005f` (`svf.h:153`) |
  | `setLoopResonanceHz`, `setLoopResonanceRt60` | **stepped** — direct `Biquad::setCoefficients` (FR-013) | — |
  | `setLoopDelayMs`, `setLoopDelayWander` | the FR-021 crossfade staircase (20 ms equal-power) | `kCrossfadeMs = 20.0f` |
  | `setGovernorThresholdDb`, `setGovernorRatio` | the FR-045 governor `LinearRamp` | `kGovernorRampMs = 20.0f` |
  | `setLoopWake`, `setLoopDormant` | the FR-061 gate `LinearRamp` | `kGainRampMs = 50.0f` |
  | `setNumLoops` | FR-075: gate ramp for dropped loops + `normGain` ramp | `kGainRampMs` |
  | `setLoopFilterMode` | **stepped** | — |
  | `setWanderRate`, `setWanderEnabled`, `setSeed`, `prepare`, `reset` | **stepped** | — |

  Two notes the table cannot carry. (a) **The resonator is a plain `Biquad`, retuned by a stepped
  coefficient hard-swap, not a `SmoothedBiquad` glide.** `ResonatorBank` gets click-free retuning from
  its own per-sample smoothers (`resonator_bank.h:471-473`), and an earlier draft of this spec
  replaced that with `SmoothedBiquad` (`biquad.h:527`) to keep the same property. OQ-1 found that path
  defective — `SmoothedBiquad::setTarget` (`:547`) routes through `BiquadCoefficients::calculate`
  (`:673`), which silently re-clamps `Q` to `biquad.h`'s own `kMaxQ = 30`, undercutting the
  `kMaxResonatorQ = 100` ceiling FR-013 specifies — so this component instead computes RBJ
  coefficients directly and writes them with `setCoefficients`, and accepts the resulting hard-swap
  click as the cost of a correct Q ceiling. The resonator centre and RT60 are rare, user-set controls,
  not something driven at audio rate, so a stepped retune is proportionate rather than a shortcut; it
  is declared stepped here and excluded from SC-001 (d)'s click assertion by name, precisely like
  `setLoopFilterMode` below.
  (b) The **stepped** rows are stepped for a reason, not by omission: a filter-mode change
  re-points the `SVF`'s output tap and is a genuine signal discontinuity; `setSeed` restarts the
  modulation by design (Edge Cases); `setWanderEnabled` only zeroes a depth term (FR-056); and
  `setLoopResonanceHz`/`setLoopResonanceRt60` are a rare user retune whose click is accepted per (a).
  SC-001 (d) jumps
  them like everything else and holds them to the boundedness assertions, exempting them from the
  click assertion **by name**.

### FR-080 series — Safety, budget, footprint

- **FR-080** — **CPU: ≤ 1.5 % of one core per voice at 48 kHz** (roadmap line 282, **amended
  2026-09-13 by user decision from 1 %**). The measurement basis is **ns per 512-sample block at
  48 kHz**; one block period is 10 666 667 ns, so the ceiling is **160 000 ns/block**, with the gated
  baseline at `baseline × 1.5 <= 160 000`, i.e. **`baseline <= 106 667 ns`** (SC-004). *Why:* the
  build's isolated measurement put the reference arm at 1.73 % (≈ 1.0 % machine-corrected) against
  the original 1 %; the two engineering levers the ladder did not list — reading only the live delay
  tap outside a crossfade (`CrossfadingDelayLine::read`, ~half of the 62 091 ns delay stage) and
  pushing cutoff to the SVF only on a real move so its smoother early-out fires — brought it to
  **94 106 ns = 0.88 %** P-core-pinned, under the 1 % ceiling but over its 0.667 % gated line. With the
  roadmap's 4–5 % per-voice envelope and the Atmosphere (1 → 1.5 %) and Phase 2 (1 → 1.75 %) precedents,
  the budget was raised rather than a seventh of the ensemble (lever 5) or the resonator (lever 4, 3 %
  of the cost) dropped. The reference workload is `numLoops = 6`, all loops awake,
  default tables, wander on, governor engaged. **A stage-cost probe runs before the component is
  written** (the Phase-2/3 pattern, `resonance_drift_network_perf_test.cpp:30-50`) and measures, in
  the loop position and per 512-sample block: (a) `SVF::process` × 6, (b)
  `MultimodeFilter::processSample` × 6 — the roadmap's named filter, so the SVF-vs-MultimodeFilter
  decision (Overview 1, D-1) is decided
  from a number and not from prose, (c) `CrossfadingDelayLine::process` × 6, (d) `Biquad::process` × 6
  fed directly-computed RBJ coefficients — FR-013's realisation (ii), (e) `DCBlocker::process` × 6,
  (f) `std::tanh` × 6, (g) six single-slot `ResonatorBank` instances, one enabled slot each — FR-013's
  realisation (i), measured against (d) so OQ-1's realisation choice is also a number, not only a
  reading of the header, (h) the `EnvelopeFollower` RMS tracker, (i) the twelve
  `BrownianDrift` lanes at the FR-055 decimation, (j) the per-sample ramp bank
  FR-076 declares (mix, wet trim, six input gains, six gates, governor, `normGain` — fifteen
  `LinearRamp::process` advances per sample).
  ***STOP-AND-SURFACE RULE (inherited verbatim from `resonance_drift_network_perf_test.cpp:59-65`) —
  NON-NEGOTIABLE:*** no implementing agent may lower `kMaxLoops`, raise the budget, relax a threshold
  or shrink a workload to make a figure fit. Reduce cost, never move the line. **Levers 1–3 are
  pre-authorised from the measured probe table (OQ-2)** and may be applied without a further ask: (1)
  replace `std::tanh` with the shipped `FastMath` alternative under its own error-bound test; (2)
  the stepped resonator retune (FR-013) — already the default, so this lever is banked in the
  baseline rather than a further step to take; (3) skip a coupling-pair smoother's advance once it
  has settled (FR-034's carve-out). **If the total still exceeds `71 111 ns`/block after levers 1–3,
  the build stops and surfaces the measured per-stage table** rather than reaching for the remaining
  two on its own: lever 4, drop the resonator to a shared per-loop `SVF` in Bandpass mode (dropping the
  RT60 surface), and lever 5, reduce the default `numLoops` from 6 to 5 (roadmap line 272 permits it),
  are both **user** decisions, taken from that table.
- **FR-081** — **Zero allocation after `prepare()`.** Everything except the six delay buffers is a
  fixed-size member (`std::array`), and the delay buffers are `std::vector`s owned by
  `DelayLine` and sized once in `prepare()` (`delay_line.h:273-275`). No `processBlock` path, no
  setter and no `reset()` allocates.
- **FR-082** — `[[nodiscard]] std::size_t getAllocatedBytes() const noexcept` reports the exact heap
  footprint after `prepare()`. **Scope is normative**, because this component — unlike Phase 3, which
  correctly reports `0` (`resonance_drift_network.h:907-912`) — genuinely owns heap: the figure is
  ```
  kMaxLoops × nextPowerOf2(round(sampleRate × (kMaxDelayMs + kDelayHeadroomMs)/1000) + 1) × sizeof(float)
  ```
  reproducing `DelayLine::prepare` (`delay_line.h:267-275`) exactly. **The multiplier is `kMaxLoops`,
  not `numLoops`**, and the difference is not cosmetic: all six lines are prepared by `prepare()`
  regardless of the count, because `setNumLoops` must not allocate (FR-075, FR-081), so a
  `numLoops`-scaled figure would under-report the real footprint by 6× at `numLoops = 1` and SC-008
  would fail against it. At 48 kHz: `48000 × 0.52 = 24960`, `+1`, `nextPowerOf2 = 32768`, `× 4 bytes
  × 6 lines` = **786 432 bytes**; at 192 kHz, **3 145 728 bytes**. It is the assertion surface for
  SC-008, which requires the figure to be identical for `numLoops` of 1 and 6.
- **FR-083** — **Sample-rate floor.** `prepare()` substitutes 48 000 for a non-finite rate and then
  floors at `kMinUsableSampleRate = 8000.0`. Not 1 Hz, and the reason is transcribed rather than
  inherited: this component includes `resonator_bank.h` and uses
  `[kMinResonatorFrequency, kMaxResonatorFrequencyRatio * fs]` as its resonator clamp pair (FR-013);
  below 8 kHz that pair **inverts** (`0.45 × 1 Hz = 0.45 Hz < 20 Hz`), and `std::clamp` with
  `hi < lo` is undefined behaviour that MSVC's `<algorithm>` traps with
  `_STL_VERIFY("invalid bounds argument passed to std::clamp")`
  (`resonance_drift_network.h:283-296`). A `static_assert` pins the ordering at the floor.
- **FR-084** — **Denormal safety.** Every stage the component composes already flushes its own state
  (`SVF::process :379-380`, `Biquad::process :367-368`, `DCBlocker::process :203`,
  `EnvelopeFollower::processSample :183-184`), and the component additionally flushes `prevY_[i]`,
  `prevOut_[i]` (FR-031's two previous-sample vectors) and the wet sum through `detail::flushDenormal`
  (`db_utils.h:245`) so a decaying network cannot leave denormals circulating on the coupling path,
  which is the one signal path no sub-object owns.
  This is correct on a host that has not set the MXCSR bits, independently of the process-wide FTZ/DAZ
  the test harness enables (`tests/test_helpers/enable_ftz_daz.h`, applied in
  `dsp/tests/dsp_test_main.cpp:10,13`).
- **FR-085** — `processBlock` accepts **any** `numSamples`, including far above
  `PrepareConfig::maxBlockSamples`: the render is per-sample with local dry capture and there is no
  scratch buffer to overrun (`resonance_drift_network.h:300-304` precedent).

### FR-090 series — Shared components stay untouched

- **FR-090** — **`FilterFeedbackMatrix`, `FeedbackNetwork`, `FlexibleFeedbackNetwork`,
  `IFeedbackProcessor`, `MultimodeFilter`, `ResonatorBank`, `SVF`, `Biquad`, `DCBlocker`,
  `CrossfadingDelayLine`, `DelayLine`, `EnvelopeFollower` and `BrownianDrift` must be
  byte-unchanged** at the end of this phase (SC-020). Every one of them is a shipped consumer of its
  own tests, and four of them (`ResonatorBank`, `BrownianDrift`, `CrossfadingDelayLine`, `SVF`) are
  consumed by Seraphis or by Vorago Phases 2–3, whose criteria pin their behaviour. Roadmap lines
  511–513 make this a cross-cutting constraint.
- **FR-091** — The roadmap's reuse row (line 113) says the single-loop infrastructure "is mature" and
  the new work is the ecology. Nothing in this phase extracts, generalises or unifies any of it. If a
  future phase wants a shared micro-loop primitive, it can be lifted then, from two working
  implementations rather than from one and a guess — the same rule Phase 4 applied to the spectral
  blur (roadmap lines 252–254).
- **FR-092** — Raising `FilterFeedbackMatrix`'s `static_assert(N >= 2 && N <= 4)`
  (`filter_feedback_matrix.h:72-73`) to admit 5 and 6 is **forbidden** in this phase. It would add two
  explicit instantiations (`:671-673`) to a header every Iterum/Disrumpo build already compiles, its
  `std::array<std::array<DelayLine, N>, N>` grows as `N²` (36 delay lines at N = 6, against this
  component's 6), and its per-path delay model is not the one roadmap line 272 describes. Recorded
  in D-2.

## Success Criteria

Measurement conventions: renders at 48 kHz unless stated; "reference patch" is `numLoops = 6`, all
loops awake, the FR-013/FR-022/FR-033/FR-052/FR-053 default tables, wander on at `kDefaultWanderRateHz`,
governor at its defaults, `mix = 1.0` (so the criterion measures the wet path, not the crossfade),
`wetGain = 0 dB`, seed fixed.
"Reference drive" is white noise at **−12 dBFS RMS** (not peak — the two differ by 10–12 dB for white
noise and every governor threshold assertion turns on which one is meant), generated from a fixed
seed, on both channels unless stated. Every `ClickDetector` fixture sets
`ClickDetectorConfig::sampleRate` to the **render rate**; the struct's default is `44100.0f`
(`tests/test_helpers/artifact_detection.h`), which at a 48 kHz render mis-reports every
`timeSeconds` it returns. Features come from
`tests/test_helpers/`. No criterion uses a bit-exact float golden (roadmap line 507);
`render_fingerprint.h` tolerances are used where a render must be pinned. Test-case names are the
sketch the build implements; each becomes a compliance row.

- **SC-001 — Bounded output for ANY parameter combination, over 30 minutes. (Roadmap line 280 — "this
  is the critical test".)** *(`FeedbackEcology_BoundednessSoak`, `[long]`)*
  **(a) The worst-case corner, held.** `numLoops = 6`; every `ownFb = kMaxLoopGain = 0.90`; every
  off-diagonal `coupling = kMaxCouplingPerPair = 0.5` (raw row sum 3.40, FR-035-normalised to 0.95);
  every filter Q at `kButterworthQ`; every resonator RT60 at `kMaxDecayTime = 30 s`; wander at
  `kMaxWanderRateHz` with maximum depths; governor at `ratio = 1` (**off**, FR-044) so the structural
  rungs are measured without it. Reference drive for the first 60 s, then **silence for the remaining
  29 minutes**. Assertions over the whole render: peak `|sample| < kOutputClamp`; **zero** clamp
  engagements (`getClampEngagementCount() == 0`); **zero** non-finite resets
  (`getNonFiniteResetCount() == 0`); every sample finite by `detail::isFinite`; and the RMS of the
  final 60 s is **at least 60 dB below** the RMS of the 60 s ending at input cut-off — i.e. it decays,
  which is FR-041's contraction under test rather than merely "does not explode".
  **(b) Sustained drive, 30 minutes, governor on.** Same corner but with the drive running for the
  full 30 minutes and the governor at its defaults. Assertions: peak `< kOutputClamp`; zero clamp
  engagements; zero non-finite resets; and **level stationarity** — the RMS of each of the thirty
  1-minute windows is within **±1.5 dB** of the median window RMS, so the network neither creeps up
  nor dies (the roadmap's line 94–95 requirement, and the Phase-2 stationarity idiom).
  **(c) A randomised sweep, accelerated.** 256 seeded configurations drawing every scalar parameter
  uniformly from its full clamped range and every coupling entry from `[0, kMaxCouplingPerPair]`, each
  rendered 60 s at 48 kHz with the drive on for the first 10 s. Same four assertions as (a).
  **One scalar is drawn from a restricted range, and the reason is stated rather than glossed:**
  `wetGain` is drawn from `[kMinWetGainDb, 0]` dB instead of its full `[-24, +24]`, because above
  `+4.27 dB` the trim can legitimately drive the FR-046 clamp (FR-072), and this arm's third assertion
  is that the clamp never engages. The positive half of the trim is not left untested — SC-013 (b)
  covers it, and covers it as the *only* control that can reach the clamp. Every other scalar,
  `mix` included, is drawn from its full clamped range. 256 × 60 s
  is the accelerated stand-in for exhaustiveness that roadmap lines 496–498 ask for; the two
  30-minute arms are the real-time half.
  **(d) The parameter-jump arm.** On the reference patch, every 500 ms for 5 minutes, jump one
  randomly chosen parameter to a randomly chosen extreme of its range (including `numLoops` — FR-075 —
  `setLoopDormant` and whole-matrix `setCouplingMatrix` writes; `wetGain` again from `[-24, 0]` dB).
  The four boundedness assertions apply to **every** setter without exception. The click assertion —
  no click above the SC-003 threshold at any jump instant — applies to every setter FR-076 declares
  **smoothed**, and is **gated on a 110 Hz sine carrier at −12 dBFS inside a 70 ms window after each
  jump** (`kGainRampMs + kCrossfadeMs`, the longest smoothing the component performs); the same sweep
  is repeated on the broadband reference drive and its detector count is **reported, not gated**
  (amended 2026-09-13: `ClickDetector` is a per-frame mean+5σ relative detector, and a `setMix` ramp
  straddling a frame fires it on the dry noise itself — reproduced with the coupling matrix zeroed —
  so the broadband pass measures the detector, not the component); the five FR-076 declares **stepped** (`setLoopFilterMode`, `setSeed`,
  `setWanderEnabled`, `setLoopResonanceHz`, `setLoopResonanceRt60`) are jumped in the same arm and are
  exempt from the click assertion only, with
  the exemption listed by name in the test so it cannot quietly widen. **The matrix is seeded at
  `kMaxCouplingPerPair` on every off-diagonal pair before the sweep starts** (rather than the reference
  patch's default ring), so every `setLoopDormant` jump in the sweep is drawn against the worst-case
  coupling fixture for FR-031's post-gate construction, not the default 0.04 ring that would mask a
  pre-gate regression.
  *This criterion is the phase. If it cannot be made to pass by reducing gain — never by widening the
  clamp or shortening the render — the component is wrong.*
- **SC-002 — Cross-loop interaction is real, and rises with coupling. (Roadmap line 281.)**
  *(`FeedbackEcology_CrossLoopTransfer`, `[long]`)*
  **Why magnitude-squared coherence is reported and not gated.** All six loops are driven by the same
  mono signal (FR-016) through taps that default to unity (FR-074), so loop `i`'s output is
  `H_i(f)·X(f)` for one common `X`. For two outputs of a common source the magnitude-squared coherence
  is `|H_1|²|H_2|²S_xx² / (|H_1|²S_xx · |H_2|²S_xx) = 1` at every frequency, **independent of `H_1`,
  `H_2` and therefore of the coupling**. The only decorrelation in the component is FR-042's `tanh`
  (specified as within 2 % of linear at default levels) and the 0.03 Hz wander, which is near
  time-invariant across 4096-point/85 ms Welch segments — neither is a function of coupling. A
  coherence gate would therefore sit near unity before the first coupling is set, and an earlier draft
  of this criterion asserted a floor of ≤ 0.15 there: unsatisfiable by a correct build, for a reason
  that has nothing to do with coupling. The roadmap's phrase "coherence metric" (line 281) is honoured
  by reporting the figure; the **gated** instrument isolates coupling by construction.
  **Gated instrument: single-loop excitation, cross-loop transfer.** `setLoopInputGain(0, 1.0f)` and
  `setLoopInputGain(i, 0.0f)` for `i >= 1`; reference patch otherwise; drive on throughout a 60 s
  render with the FR-073 taps installed. **Fixture rule, mandatory and shared by every criterion in
  this phase: `configure → reset() → render`.** Every setter is called first, then `reset()`, then the
  render begins. `prepare()` snaps `inputRamp` to `kDefaultLoopInputGain = 1.0f` and the coupling
  smoothers to FR-033's default 0.04 ring (FR-005), so a fixture that renders straight after the
  setters glides for ~960 samples with loops 1–5 driven and coupled — which would make (a)'s exact
  zero unreachable on a **correct** build. `reset()` preserves the configured values and rewinds the
  ramps and the audio state to them (FR-004), so the render starts already at `c = 0` with silent
  delay lines. Define
  `T(c) = 20·log10( RMS over the taps of loops 1…5 / RMS of loop 0's tap )`, with every off-diagonal
  coupling entry set uniformly to `c`, **every loop's own feedback at `kSweepOwnFeedback = 0.5`**, and
  **every loop's resonator at `kSweepResonanceHz = 1000 Hz`** — an exact comb tooth of every default
  delay at once, since they are integer milliseconds (`f_i = round(1000·T_i)/T_i = 1000`) — with
  **every cutoff at `kSweepCutoffHz = 2400 Hz`** so the tooth is inside every SVF passband, and
  **wander off** so the tooth stays put (all amended 2026-09-13). The shared tooth puts coupled energy
  inside every receiver's 3 Hz passband *and* makes recirculation constructive in every loop — at an
  arbitrary round-trip phase the regeneration gain `1/|1 − 0.5·e^{jφ}|` ranges 0.67× to 2× per loop
  and the pooled figure averaged −0.04 dB; a per-loop nearest tooth to 300 Hz left the sender's tooth
  outside the receivers' bands (sender +1.7 dB, receivers +0.8 dB, T down 0.9 dB): on the
  default sub-cutoff table (1200 … 210 Hz) each Q ≈ 100 resonator rejects the others' centres by
  ≈ 30 dB, the coupled transfer is set by the receiver's first pass alone, and the build measured
  `T(c) − T_ff(c) = −0.10 dB` at every point — regeneration invisible on mismatched resonators, a
  property of the default voicing (see the compliance report's note for Phase 10), not a defect.
  Sweep `c ∈ {0, 0.01, 0.02, 0.04, 0.08}`. Both choices keep the FR-035 row sum
  under `kMaxTotalLoopGain = 0.95` at every point (`0.5 + 5 × 0.08 = 0.90`): at the reference
  `kDefaultLoopGain = 0.72` the cap engaged from `c = 0.05` and the normaliser converted own
  regeneration into cross paths, so raising `c` *lowered* regeneration — the 2026-09-13 build measured
  `T(0.20) − T(0.02) = 14.96 dB` against a feed-forward `19.55 dB`, the earlier clause inverted by the
  cap, not by the coupling.
  **(a) The floor is exact, not statistical.** At `c = 0` every undriven loop's tap is **exactly**
  `0.0f` for the whole render — its input sum is `0·monoIn + appliedOwnFb·0 + 0`, and `prepare()`
  zeroes the delay buffers — so the assertion is `== 0.0f` on every sample of five taps, not a
  threshold on a level.
  **(b) Monotone rise.** `T(c)` is **strictly increasing** across the four non-zero points.
  **(c) Anti-vacuity: regeneration, not just mixing (amended 2026-09-13).** Repeat the whole sweep
  with every `ownFb = 0` (six parallel feed-forward chains) and record `T_ff(c)`. At **every** non-zero
  point `T(c) >= T_ff(c) + kInteractionMarginDb`: the receiving loops regenerate what they are handed
  (`1 / (1 − 0.5) = 2×`, about +6 dB before the loop filters shape it), while a build that merely mixes
  the loop outputs reproduces `T_ff(c)` exactly and fails. The earlier form compared the *slopes*
  `T(0.20) − T(0.02)` against `T_ff(0.20) − T_ff(0.02)`; with the fixture over the row-sum cap that
  slope was set by the normaliser and the clause could not be satisfied by a correct build.
  **(d) Reported, never gated.** The mean pairwise magnitude-squared coherence over `[40 Hz, 4 kHz]`
  (Welch, 4096-point Hann, 50 % overlap) at each coupling point, from the new `tests/test_helpers/`
  estimator (none exists in the tree — searched this session), printed beside the transfer table so
  the roadmap's named metric is on the record together with the evidence for why it cannot gate here.
  **The one free number, pinned.** `kInteractionMarginDb = 0.5 dB` (pinned 2026-09-13; was a
  provisional 1.5 dB with nothing in the tree behind it). On the final fixture the build measured
  `T(c) − T_ff(c) = +1.062 / +1.081 / +1.118 / +1.188 dB` at `c = 0.01 / 0.02 / 0.04 / 0.08` —
  positive, monotone in `c`, spread by hundredths across four 60 s renders. It is below the ≈ +4 dB a
  pure in-band estimate predicts because loop 0's tap also carries its Q ≈ 100 resonator's broadband
  skirt leakage, which the receivers reject rather than regenerate. The margin is half the measured
  minimum; a build that merely mixes loop outputs sits at exactly 0 dB and fails. The **shape** assertions — (a)'s
  exact zero, (b)'s strict monotonicity, and (c)'s strict inequality against the measured
  feed-forward baseline — may never be weakened, and a margin may be re-pinned only by recording the
  measurement that justifies it.
- **SC-003 — No zipper on delay-time drift. (Roadmap line 281.)**
  *(`FeedbackEcology_DelayDriftClickFree`, `[long]`)*
  Input: a 110 Hz sine at −12 dBFS (a pitched tone makes delay-position artefacts audible where noise
  hides them), 300 s, reference patch but with `delayWanderFraction = kMaxDelayWanderFraction = 0.5`
  and `wanderRate = kMaxWanderRateHz` on every loop, so the crossfade fires as often as the component
  can make it fire.
  **(a)** `ClickDetector` (`tests/test_helpers/artifact_detection.h:130`) reports **zero** detections
  over the whole render, at a `ClickDetectorConfig` (`:38`) whose `sampleRate` is set to the render
  rate (measurement conventions — the struct's default is 44100.0f).
  **(b)** Two clauses, both relative to the render's own level so a quiet render cannot pass. The
  render's **peak absolute sample must be ≥ 0.05** with the wet trim at **`kMaxWetGainDb = +24 dB`**
  (amended 2026-09-13: the six Q ≈ 100 resonators pass a −12 dBFS sine ≈ 30 dB down — the header's
  DERIVATION TABLE 3 — so at the 0 dB default the peak is 0.0095 and the "unity-ish wet path" the
  floor assumed does not exist; the trim is a linear gain after the loops and does not touch the
  click clauses), and given that, the maximum absolute first difference of the output is **below
  5 % of that measured peak**, and no first difference exceeds **8×** the median of the largest 1 000
  first differences.
  **(c) The crossfades actually happened.** Sum `getLoopCrossfadeCount(i)` (FR-071 — an exact count of
  crossfade **onsets**, incremented where the component writes the delay and
  `CrossfadingDelayLine::isCrossfading()` (`:301`) has just gone true) over the six loops and require
  **≥ 200** over the 300 s. An earlier draft left the method open between "a white-box arm" and
  "`getLoopCurrentDelayMs(i)` step detection", which are not the same number — step detection counts
  completed steps and misses every crossfade retriggered mid-fade (`:176-181`) — so the count is
  defined on one named accessor. Without this arm a build whose delay never moves passes (a) and (b)
  trivially.
- **SC-004 — CPU ≤ 1.5 % of one core per voice at 48 kHz. (Roadmap line 282, amended 2026-09-13;
  see FR-080 for the measured history.)**
  *(`FeedbackEcology_CpuBudget`, `[.perf]`)*
  Basis: **ns per 512-sample block at 48 kHz**, best-of-25 trials × 500 blocks after 400 warm-up
  blocks, the `resonance_drift_network_perf_test.cpp:78-84` shape. Ceiling **160 000 ns/block**;
  gated baseline `kBaseline × 1.5 <= 160 000` (so `kBaseline <= 106 667 ns`), with
  `static_assert(kBaseline * kRegressionFactor <= kReferenceNs)` and
  `static_assert(kBaseline >= kReferenceNs / 50.0)` binding the absolute figure at compile time on
  every CI leg.
  Arms: **(a)** reference patch, six loops awake, wander on. **(b)** six loops, wander off (FR-056) —
  reported, and required to be **at most 2 % above (a)**; the band is one-sided and any saving passes
  (clarified 2026-09-13: with cutoff pushed to the SVF only on a real move, wander-off measures
  ~18 % *below* (a) because the SVF smoother sits settled, which is a saving, not a defect). FR-056
  keeps the lanes advancing and only zeroes the depth term the mapping applies, so an untoleranced
  equality here would fail on a scheduling accident inside a best-of-25 × 500-block measurement. An
  untoleranced comparison here fails on a scheduling accident, not on a defect — the Phase-3
  precedent, where the same arm's "≥ 10 % saving with wander off" was structurally unreachable once
  the Dormancy rule fixed that lanes keep advancing and had to be amended mid-build (roadmap lines
  218–222). **(c)** all six loops dormant — must be
  **at least 40 % cheaper** than (a), which is the observable consequence of FR-062's skipped chain
  and the only thing that proves dormancy is not cosmetic. **(d)** `numLoops = 1` — reported, sets the
  per-loop marginal cost. **(e)** the FR-080 stage probe table, `[.perf]`, printed not gated,
  including the `MultimodeFilter` and single-slot `ResonatorBank` alternatives that D-1 and D-3 are
  decided from. **(f) The transition blocks — FR-019's detector.** Arms (a)–(d) are all steady state,
  so a per-transition spike is unmeasured by design. Two 512-sample blocks are timed, each gated
  against the **absolute** 160 000 ns ceiling and not against `kBaseline` (a transition may cost more
  than steady state, just not more than the block period): the block containing a **simultaneous
  multi-loop sleep edge** (drive to steady state, then `setNumLoops(6 → 1)`, so five `clearLoopAudio`
  calls land inside one 64-sample control chunk), and the block containing a **rung-5 trap fire**
  injected through the FR-048 probe. Both are repeated at **192 kHz**, where the delay buffers are
  four times larger — that is where an O(buffer) delay clear on an audio-thread path shows first.
  At every rate two clauses hold: the transition block costs at most **1.1×** a steady block in the
  same state at the same rate (the O(buffer) detector: a buffer fill is a multiple, not 10 %, and
  the measured overheads are negative at both rates), and at most the **same absolute 160 000 ns**
  ceiling. **Amended 2026-09-15:** an earlier draft set the 192 kHz ceiling to 1.5 % of the 192 kHz
  block period (40 000 ns). A 512-sample block is four times shorter in wall time there for the same
  per-sample work, so that clause demanded the component be about twice as cheap per sample as at
  48 kHz; it never passed (Phase 5's own isolated run read 69 100 / 83 700 ns against it, steady
  ≈ 76 500 ns at 192 kHz), and the compliance record of 2026-09-13 that said it did was wrong. The
  1.5 %-per-voice budget is defined at 48 kHz. The percent-of-core figure is **reported, never asserted**
  (`resonance_drift_network_perf_test.cpp:67-76`). Run in isolation, nothing else executing
  (`node tools/run-cpu-tests.js`).
- **SC-005 — The delay wander actually moves the delay, and its absence is documented rather than
  silent.** *(`FeedbackEcology_DelayMotion`)*
  **(a)** At **44.1 kHz** (the binding rate, FR-023) on the reference patch: over **300 s**, for
  **every** one of the six loops, `getLoopCurrentDelayMs(i)` takes at least **3 distinct values**
  whose total spread is at least **2 %** of that loop's base delay; and within the first **120 s**,
  every loop whose base delay is ≥ 109 ms takes at least **4**. The counts are derived, not guessed:
  FR-023's table puts every default loop's step at ≈0.3 lane displacement ≈ 0.5 σ_Δ, and
  `BrownianDrift`'s decorrelation time under the FR-055 mapping — which this spec does not pin — sets
  how many independent excursions a 120 s window contains. **The floor that may never be weakened is
  ≥ 2 distinct values (one completed step) for every loop**: a loop whose delay never moves is the
  defect this criterion exists to catch. The counts above that floor are provisional until the build
  measures the step rate, and may be re-pinned only by recording the measurement (FR-080's
  stop-and-surface rule). If the measurement shows the shortest loop cannot clear the floor, the fix
  is FR-023's derivation — raise that loop's default wander fraction — never a shorter assertion.
  **(b)** At 44.1 kHz with `baseDelayMs = 10` and `delayWanderFraction = 0.05` (peak excursion 22
  samples, below `kCrossfadeThresholdSamples = 100`), `getLoopCurrentDelayMs(0)` is **constant** over
  120 s — the documented consequence of the shipped delay line, asserted so it is a known property
  rather than a discovered bug.
  **(c)** At 192 kHz the same reference patch also satisfies (a), which it must, since 100 samples is
  only 0.521 ms there.
- **SC-006 — The energy governor engages, is monotone, and never mutes.**
  *(`FeedbackEcology_Governor`)*
  **(a)** Drive the reference patch with white noise (RMS level, fixed seed) swept from −40 dBFS to
  0 dBFS in 6 dB steps, 20 s per step. **The threshold crossing is measured, not assumed.** The
  governor reads `normGain * Σ_i b_i` (FR-043), not the input, and the loops amplify: the ratio between the two
  is a property of the patch that no arithmetic in this spec pins. So the test runs the sweep **twice**
  — once at `ratio = 1` (governor off, FR-044) recording `getGovernorRms()` at the end of each step,
  then at the defaults — and asserts against the recorded readings and
  `threshold = dbToGain(getGovernorThresholdDb())`: `getGovernorGain()` is **non-increasing** across
  the sweep; **exactly 1.0f** at every step whose recorded RMS is below `threshold`; **strictly below
  1.0f** at every step above it; and **< 0.6** at 0 dBFS. The sweep must **straddle** the threshold —
  the −40 dBFS step below it, the 0 dBFS step above it — and that straddle is itself asserted. If it
  does not straddle, Assumption 1's input-level estimate is wrong and the response is to re-measure
  and record `kGovernorThresholdDb` the Phase-3 way, never to move an assertion.
  **(b)** The output RMS across those steps rises **sub-linearly**: the total output RMS increase from
  −40 to 0 dBFS is at least **12 dB less** than the 40 dB input increase, at the default
  `ratio = 8`.
  **(c)** `getGovernorGain()` is **never below `kGovernorMinGain = 0.05`** anywhere in SC-001's sweep.
  **(d)** At `ratio = 1.0` the governor gain is **exactly 1.0f** for every input level — the
  documented "off" setting, an exact-identity claim reachable because the exponent `1/ratio - 1` is
  exactly 0.
  **(e) No zipper from the governor:** on a 20 dB input step, `ClickDetector` reports zero detections
  in the 200 ms window beginning **one sample after the step sample**, which is FR-045's ramp under
  test. The offset is load-bearing: at `mix = 1` the reference patch passes the drive's own
  discontinuity into the wet path, so a window that includes the step sample fires the detector on
  the fixture rather than on the component. The step is a change in the drive's RMS level, applied at
  a zero crossing of the noise generator's output where one exists within ±1 sample.
  **(f) Loop-count invariance — the arm a raw, unscaled tracker fails.** Repeat (a)'s sweep twice more,
  at `numLoops = 1` and at `numLoops = 6` (reference RT60/Q/coupling defaults for the loops present in
  each case), and record each count's threshold-crossing input level — the input dBFS at which
  `getGovernorGain()` first drops below `1.0f`. The two crossing levels agree **within 1 dB**. Under
  the rejected raw, unscaled `Σ_i b_i` reading the two counts' effective thresholds differ by
  `10·log10(6) ≈ 7.8 dB`, so this arm is what a build that omits FR-043's `normGain` scaling fails, and
  no build that scales by it can fail.
  **Measured resolution, recorded during the build (T012), so a later reader does not over-read the
  arm.** The crossing level is quantised to the sweep's 6 dB grid, so "within 1 dB" is in practice
  "the same step", and the two counts' tracker curves are **not** identical even with `normGain`
  applied: `numLoops = 1` is loop 0 alone — the widest-band resonator of the six — and reads ≈ 2.9 dB
  hotter than the six-loop average (measured, per-step, both counts, three drive seeds). The arm
  therefore discriminates the 7.8 dB `normGain` defect it was written for, but it does not certify
  agreement finer than one sweep step, and `kDefaultGovernorThresholdDb = -52.0f` is placed inside
  the (−54.6, −49.2) dB window where both curves cross between the same pair of steps
  (DERIVATION TABLE 3).
- **SC-007 — Zero allocation after `prepare()`.** *(`FeedbackEcology_NoAllocation`)*
  `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around: 1 000 `processBlock`
  calls at irregular sizes; every setter on the surface at extremes; `reset()`; `setSeed()`;
  `setNumLoops()` across `[1, 6]`; `setLoopDormant`/`setLoopWake` edges; `setCouplingMatrix`. Zero
  allocations, zero frees.
- **SC-008 — Footprint is exact and block-size independent.** *(`FeedbackEcology_Footprint`)*
  `getAllocatedBytes()` equals FR-082's formula exactly at 44.1, 48, 96 and 192 kHz — **786 432** at
  48 kHz — and is **identical** for `maxBlockSamples` of 64, 512 and 8192 and for `numLoops` of 1 and
  6 (FR-082's `kMaxLoops` reading). Before `prepare()` it is 0.
- **SC-009 — Seed determinism, and twelve independent streams.**
  *(`FeedbackEcology_SeedDeterminism`)*
  **(a)** Two instances prepared identically with the same seed produce renders equal within
  `render_fingerprint.h` tolerances (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`).
  **(b)** `reset()` followed by the same render reproduces the first render within the same
  tolerances.
  **(c)** Two different seeds differ: the fingerprint comparison **fails**, the mean absolute
  difference between the two renders exceeds **50 % of the reference render's own mean absolute
  level** (`fingerprintRender(a).meanAbs`), and that difference also exceeds `kSampleTolerance` in
  absolute terms.
  **AMENDED after T015's first build, from a measurement (the original wording read "the mean
  absolute difference exceeds `100 × kSampleTolerance`" — 0.05 in absolute sample units — and is
  preserved here because the reason matters).** That floor is **unsatisfiable by any implementation
  of this spec, correct or broken, and by a factor of 43** — it is out of reach before the seed is
  even chosen. The reference patch is quiet **by design**, and this spec already records why: every
  default loop carries a Q ≈ 100 resonator (`kMaxResonatorQ`, DERIVATION TABLE 2) whose equivalent
  noise bandwidth is 3.3–18.8 Hz out of 24 kHz, so a broadband drive reaches the loops ≈ 30 dB down
  — the same measurement that moved `kDefaultGovernorThresholdDb` from −6 dB to −52 dB at T012
  (FR-044). Measured at T015 (30 s at 48 kHz, reference patch, seeds `0x5EED` and `0xA11CE`): each
  render has **RMS 0.0014655 (−56.7 dBFS), peak 0.0076627, meanAbs 0.0011695**, so
  `mean|a−b| ≤ mean|a| + mean|b| = 0.00234` for *any* pair of renders of this patch. No fixture
  rescues the old number either: the FR-044 governor turns a 40 dB drive rise into 20.6 dB of output
  rise (SC-006 (b)), so even the maximum wet trim (+24 dB, ×15.85) over a 0 dBFS drive lands near
  0.04 — and would then be measuring the trim rather than the seed.
  The replacement **floors the difference against the render's own level**, which is the quantity
  "audibly different" was always about. Two statistically independent renders of one process give
  `mean|a−b| / mean|a| = √2 = 1.414`; a build in which the seed never reaches the twelve lanes gives
  **exactly 0** — and that build is what arm (a) renders, which is why (a) is this arm's control;
  this build measures **1.138**, i.e. 80 % of the independent limit and a **2.28× margin** over the
  0.5 bound. The absolute clause survives as the comparator's own resolution: the measured
  **0.0013306** clears `kSampleTolerance = 5.0e-4` by 2.66×. The **intent**, the render length, the
  seeds and the fingerprint clause did not move; only the quantity the floor is expressed against
  did.
  **(d)** Lane independence, on two arms — one statistical, one deterministic.
  *Statistical:* over a 120 s render, the pairwise Pearson correlation between the **per-block
  first differences** of the twelve **lane target** trajectories, read through
  `getLoopTargetDelayMs(i)` / `getLoopTargetCutoffHz(i)` (FR-071) once per 512-sample block, is
  **below 0.25** for every pair; a **positive control** in the same arm — two lanes driven from one
  shared stream, read through the two different mappings — must report **above 0.9**, so the bound is
  known to discriminate rather than merely to be small.
  **AMENDED after T010's first build, from a measurement (the original wording correlated the
  trajectory LEVELS and is preserved here because the reason matters).** Levels cannot be correlated
  over this window: each lane is a `BrownianDrift` at the FR-055 default 0.03 Hz, whose real-time
  decorrelation time is 33.3 s (per-advance `tau` 16.667 s × `laneDecimation_` 2), so 120 s supplies
  N_eff ≈ 2 independent samples and the sampling standard deviation of r between two **independent**
  lanes is ≈ 0.5. A standalone probe driving twelve shipped `BrownianDrift` lanes with this
  component's seeds, smoothness and decimation — no `FeedbackEcology` code in the picture — measured
  worst |r| on levels = **0.6916** at seed `0x5EED` (the exact figure the component produced) and
  **0.5994 … 0.8894 across 64 different base seeds, above 0.25 for 64 of 64**. The level criterion was
  therefore unsatisfiable by any correct implementation, and it converges only on absurd windows
  (0.2970 at 1800 s, 0.1579 at 7200 s, 0.0819 at 28800 s). The **increments** are driven by each
  lane's own OU innovations, so they converge inside the pinned 120 s: worst |r| = **0.0715** at seed
  `0x5EED`, **0.0922** across the same 64 seeds — a 2.7× margin under the **unchanged** 0.25 bound —
  while a shared stream still reports **0.9855**. The **estimator** moved; the threshold, the render
  length and the criterion's intent did not.
  The realised readings must **not** be used here either: `getLoopCurrentDelayMs` is the crossfade
  staircase, which changes only on completed ≥ 100-sample steps and, per SC-005 (b), is documented to
  never change at all in some configurations — a near-constant series has ~zero variance and its
  correlation is dominated by two or three quantisation steps, or is 0/0.
  *Deterministic:* the twelve `deriveStreamSeed(seed, salt)` values from FR-054's salt table are
  pairwise **distinct**, computed directly in the test (`core/random.h:102`). This is the actual
  salt-collision guard (`:97-100` — `Xorshift32::seed(0)` silently substitutes its default, so two
  lanes hashing to the same value collapse onto one stream); the statistical arm alone cannot tell a
  collision from two independent lanes that each happened to step twice.
- **SC-010 — Block-partition invariance.** *(`FeedbackEcology_BlockPartition`)*
  The same 60 s render produced with block sizes 512, 64, 7, 4096, and an irregular pseudo-random
  partition, all agree within `render_fingerprint.h` tolerances. This is FR-007's absolute control
  grid under test: a block-relative grid runs two control steps for a 36 + 28 split where an unsplit
  64 runs one (`resonance_drift_network.h:534-538`).
- **SC-011 — Sample-rate independence.** *(`FeedbackEcology_SampleRate`)*
  Rendered at 44.1, 48, 96 and 192 kHz with the reference patch: RMS within **1 dB**, spectral centroid
  within **10 %**, and no clamp engagements or non-finite resets at any rate.
  **Realised delays, with a staircase-aware bound.** With `setWanderEnabled(false)` called immediately
  after `prepare()` — so every mapped delay is still the base value `prepare()` snapped it to (FR-005,
  `snapToDelaySamples`) — assert for every loop and every rate
  ```
  |getLoopCurrentDelayMs(i) − configured ms| <= max(1 % of the configured ms,
                                                    kCrossfadeThresholdSamples / sampleRate in ms)
  ```
  i.e. 2.268 ms at 44.1 kHz, 2.083 at 48, 1.042 at 96, 0.521 at 192 — a bound that tightens as the
  rate rises. A bare 1 % is not achievable on this component's own reference patch and an earlier
  draft asserted it: the patch has wander **on** at `kDefaultDelayWanderFraction`, which FR-052 moves
  the delay by up to ±16 % of base deliberately, and even with wander off
  `getCurrentDelaySamples()` is the gain-weighted tap average while `setDelaySamples` moves only the
  inactive tap until the target has drifted `kCrossfadeThresholdSamples = 100` from the active one
  (`crossfading_delay_line.h:161-191`, `:306`), so the realised value legitimately lags the commanded
  one by up to one threshold — 5 % of a 41 ms loop at 48 kHz, 20 % of a 10 ms one. The **measured**
  error is reported at every rate; with wander off and the FR-005 snap it is expected to be exactly
  zero, and the staircase term is the tolerance that keeps the criterion honest, not a licence to
  drift. Plus: `prepare()` at 1 Hz, at 0, at a negative rate and
  at a NaN rate all leave a usable object at `kMinUsableSampleRate` or above (FR-083), and rendering
  through it produces finite output.
  **Re-`prepare()` restores defaults (FR-005 (c)).** Starting from the reference patch, set a
  non-default coupling matrix (every off-diagonal pair at `kMaxCouplingPerPair`) and a non-default
  loop gain (`kMinLoopGain` on every loop), then call `prepare()` again at the same rate. Assert that
  `getCoupling(from, to)` reads back FR-033's default ring (`kDefaultCoupling` on cyclic neighbours,
  `0.0f` elsewhere) and `getLoopGain(i)` reads back `kDefaultLoopGain` for every loop and every pair —
  the caller's prior configuration does not survive a re-`prepare()`, only a `reset()` preserves it
  (FR-004).
- **SC-012 — Non-finite hygiene.** *(`FeedbackEcology_NonFinite`, the `-fno-fast-math` TU)*
  Non-finite values are built from **bit patterns behind a volatile sink**, never
  `std::numeric_limits<float>::quiet_NaN()`, which folds to finite garbage under the macOS leg's
  `-ffast-math` (`reference_fastmath_nan_in_tests`).
  **(a)** Every float setter, given NaN and ±Inf, is a **no-op** and the matching getter still reports
  the previous value (FR-009).
  **(b) The public path: the `SVF` intercepts the poison, and that is the whole story.** A single
  non-finite **input sample** injected into a settled render reaches every loop through the FR-016
  mono sum (so "the affected loop" is all six, not one), and every loop's **first** stage is
  `SVF::process`, which resets that filter and returns `0.0f` on a non-finite input
  (`svf.h:359-363`). The assertions are therefore what actually happens rather than a state the
  product cannot be in: the render is finite and bounded from the injection sample onward;
  `getNonFiniteResetCount()` stays **0** — the poison never reaches `b_i`, so FR-047 correctly does
  not fire, and an earlier draft's "increments by exactly 1" was unsatisfiable for that reason;
  `getClampEngagementCount()` stays 0; and the recovery edge is **click-bounded**, because six
  `SVF::reset()` calls dump six filters' integrator state at once, which is itself an audible edge:
  `ClickDetector` over the 200 ms following the injection reports at most **1** detection and the
  maximum absolute first difference stays inside SC-003 (b)'s shape bound.
  **(c) The trap itself, through the declared probe.** FR-047 is unreachable through the public API —
  (b) is the proof — so rung 5 is exercised through `detail::FeedbackEcologyNonFiniteProbe` (FR-048),
  run **once per non-self-healing stage the trap covers**: the in-loop `DCBlocker`
  (`dc_blocker.h:188`, "NaN inputs are propagated") and the governor's `EnvelopeFollower`
  (`envelope_follower.h:163`, "Does NOT validate input"). The probe writes a bit-pattern non-finite
  value into that stage's state on a settled render; assertions, per stage: `getNonFiniteResetCount()`
  increments by **exactly 1**; `clearLoopAudio(i)` ran (that loop's tap is `0.0f` at the trap sample
  and the loop audibly refills from its input tap rather than resuming its ring); the governor's
  follower was reset — `getGovernorGain()` is finite at every subsequent sample and `getGovernorRms()`
  returns to within 1 dB of its pre-poison value within 2 s; the **other five loops are untouched**,
  their taps bit-identical to an unpoisoned reference over the same block; and the main output is
  finite and bounded from the trap sample onward.
  **(d)** After the probe injection, a `reset()` returns the component to a state whose subsequent
  render matches a never-poisoned reference within `render_fingerprint.h` tolerances.
- **SC-013 — The output clamp never engages in-spec, and is proven wired.**
  *(`FeedbackEcology_OutputClamp`)*
  **(a)** `getClampEngagementCount() == 0` across every arm of SC-001, SC-002, SC-003, SC-006 and
  SC-011 — all of which hold `wetGain <= 0 dB`, which is the scope FR-046 gives the zero-engagement
  requirement. Note what (a) alone is worth: with `wetGain <= 0 dB` the bound is **structural**, since
  FR-042 caps `|y_i| <= 1` for any input level and the wet sum at `sqrt(numLoops) <= 2.449` against
  `kOutputClamp = 4.0`, so (a) is satisfied by construction and asserts nothing until (b) proves the
  counter increments at all.
  **(b) The one control that can reach the clamp.** Raising the *input* cannot: `tanh` discards it,
  which is the whole point of rung 2, and an earlier draft's "+24 dBFS drive" fixture was therefore
  unreachable. FR-046 places the clamp **after** the FR-072 wet trim, and the trim is the largest gain
  in the component: reference patch, `mix = 1`, `setWetGain(+24.0f)` (×15.85), drive at 0 dBFS RMS for
  10 s. The pre-clamp wet need only exceed `4.0 / 15.85 = 0.2524` for an engagement, against a
  structural ceiling of `2.449 × 15.85 = 38.8`. Assertions: `getClampEngagementCount() > 0`; every
  output sample inside `[-kOutputClamp, +kOutputClamp]`; every sample finite;
  `getNonFiniteResetCount() == 0`. The test **reports** the measured pre-clamp peak; if a build's wet
  path is quiet enough that 0.2524 is not reached, the response is a louder drive or a longer render —
  never a lower `kOutputClamp`.
- **SC-014 — Dormancy behaves exactly as the cross-cutting rule requires.**
  *(`FeedbackEcology_Dormancy`)*
  **(a)** `setLoopDormant(i, true)` and `setLoopWake(i, 0.0f)` produce **the same render** within
  `render_fingerprint.h` tolerances — the rule's core claim (roadmap lines 501–506).
  **(b) The lanes keep running, read where they are actually visible.** A settled-dormant loop reports
  `isLoopEngineActive(i) == false` and `getLoopGate(i) == 0.0f` exactly, while
  `getLoopTargetDelayMs(i)` and `getLoopTargetCutoffHz(i)` (FR-071) **keep changing** — the lanes and
  their FR-052/FR-053 mappings are still running (FR-062). The same arm asserts the other half:
  `getLoopCurrentDelayMs(i)` and `getLoopCurrentCutoffHz(i)` are **frozen**, because a skipped loop
  writes nothing to its audio objects and `getCurrentDelaySamples()` cannot advance while `read()` is
  not called (`crossfading_delay_line.h:169-174`, `:245-285`, `:306`), and `getLoopCurrentCutoffHz(i)`
  (FR-071, `svf_[i].getCutoff()`) does not change because `SVF::setCutoff` is never called while
  skipped — a different mechanism from the delay's staircase, but the same observable freeze. An
  earlier draft asserted the *realised* pair kept changing, which is structurally impossible in exactly
  the state being measured.
  **(b2) And the lane advance is real, observationally.** Sleep a loop, hold silence for 60 s, wake
  it: the delay and cutoff it wakes with — read after FR-064's snap — differ from the values it slept
  with by more than one FR-023 step (delay) and by more than 1 % (cutoff). This is FR-062's actual
  behavioural claim and it depends on no getter being live while the loop is dormant.
  **(c)** Wake re-entry is a 50 ms ramp: `getLoopGate(i)` reaches its target in
  `50 ms × sampleRate` samples ±1 control chunk, monotonically, and `ClickDetector` reports zero
  detections across the edge. **This is measured with every off-diagonal coupling entry set to
  `kMaxCouplingPerPair = 0.5`** — the fixture at which a neighbour reading the *pre-gate* value would
  see a 0.5-amplitude step the instant the sleeping loop's gate settled at zero, against which this
  arm asserts zero detections on every neighbour's tap as well as on the sleeping loop's own gate. It
  is the click-free proof of FR-031's post-gate coupling construction, not a coincidence of the default
  ring coupling's low `kDefaultCoupling = 0.04`.
  **(d) The sleep edge clears the loop (FR-063).** Charge a loop with 30 s of drive, sleep it, wait
  60 s of silence, then wake it with the input still silent: the loop's tap output stays below
  **−80 dBFS** for the first 500 ms after the gate opens. A build that only skips the chain replays
  the frozen ring and fails by 60 dB or more.
  **(e)** `setLoopWake(i, 1e-9f)` snaps the gate target to exactly `0.0f`
  (`kWakeSilenceEpsilon`, FR-060) and `isLoopEngineActive(i)` becomes false.
- **SC-015 — Row-sum normalisation is enforced and visible.**
  *(`FeedbackEcology_GainNormalisation`)*
  **(a)** For 1 000 randomly drawn configurations of `ownFb` and the coupling matrix,
  `getLoopAppliedTotalGain(i) <= kMaxTotalLoopGain + 1e-6f` for every loop, and equals
  `kMaxTotalLoopGain` (within 1e-5) whenever the raw sum exceeded it.
  **(b)** The **configuration** getters (`getLoopGain`, `getCoupling`) still report the caller's
  clamped values, unchanged by the normalisation (FR-035).
  **(c)** With the worst case `ownFb = 0.90` and all five neighbours at 0.5 (raw 3.40), the applied
  total is 0.95 and SC-001 (a)'s decay assertion still holds.
  **(d) `numLoops = 1` is count-inert, not dormancy-inert, and the difference is asserted separately.**
  With `numLoops = 1` and every entry of the coupling matrix — including every pair among the five
  unused slots `1..5` — written to `kMaxCouplingPerPair`, `getLoopAppliedTotalGain(0)` equals
  `getLoopGain(0)` within `1e-6`: the row sum ranges only over `j < numLoops` (FR-035), so slots the
  count excludes never attenuate the survivor. A second arm holds `numLoops` at 6, sets every coupling
  pair to `kMaxCouplingPerPair`, then puts loops `1..5` to sleep one at a time via `setLoopDormant`; at
  every step `getLoopAppliedTotalGain(0)` is unchanged to within `1e-6` of its all-awake value —
  dormancy does not modulate a survivor's applied gain.
- **SC-016 — The tap path changes nothing.** *(`FeedbackEcology_TapEquivalence`)*
  A 60 s render through `processBlock` and the same render through `processBlockTapped` with all six
  taps installed are **bit-identical** on both output channels (exact `==`, not a tolerance — this is
  the same code path, FR-073, so any difference is a real divergence). Also: the sum of the six taps,
  scaled by `1/sqrt(numLoops)`, multiplied by the linear `wetGain` and clamped to
  `[-kOutputClamp, +kOutputClamp]` — FR-015's output-stage block, in that order — reproduces the wet
  output within `kSampleTolerance`, which proves the taps are the real per-loop signals and not a
  debug approximation.
- **SC-017 — Control-surface clamps and the read-surface neutrals.**
  *(`FeedbackEcology_ControlSurfaceClamps`)*
  Every float setter driven to ±10× its range reports the clamped value; every index-taking setter at
  `kMaxLoops`, `kMaxLoops + 1` and `SIZE_MAX` is a silent no-op; every index-taking getter at those
  indices returns the documented neutral without reading out of bounds (ASan-clean); `setCoupling(i,
  i, x)` is a no-op and `getCoupling(i, i)` is `0.0f` (FR-030); `setNumLoops` clamps to `[1, 6]`;
  every getter on an unprepared instance returns its **post-construction default** — `getNumLoops()` is
  6, `getMix()` is `kDefaultMix`, `getLoopDelayMs(0)` is 41.0f, and so on, because FR-002 leaves
  construction in the state `prepare(48000.0, PrepareConfig{})` produces and FR-070 says every
  configuration getter returns the clamped configured value; the sole exceptions are
  `getAllocatedBytes()`, which is 0 before `prepare()` (SC-008), and `isPrepared()`, which is false.
  "Neutral" is reserved for the out-of-range-index case FR-009 defines and is not a second meaning for
  the unprepared state. Plus the **constexpr-log equivalence
  arm**: the `detail::constexprLn`-derived log2 constants of FR-053 agree with `std::log2` to within
  1e-6 at runtime, so the constexpr series and the library function cannot drift apart on any
  toolchain (`resonance_drift_network.h:260-266` precedent).
- **SC-018 — `mix = 0` is bit-exact dry, and the network is still running underneath.**
  *(`FeedbackEcology_DryIdentity`)*
  **(a)** With `mix = 0.0f` settled, a 10 s stereo render is **bit-identical** to its input on both
  channels.
  **(b)** During that render `getGovernorRms()`, `getLoopCurrentDelayMs(i)` and `getLoopGate(i)` all
  change, and a subsequent `setMix(1.0f)` produces, after the crossfade settles, a wet signal within
  `render_fingerprint.h` tolerances of a reference render that ran at `mix = 1` throughout — i.e. the
  loops were charged, not asleep (FR-072).
- **SC-019 — Layer, ODR, naming and portability gates.** *(no Catch2 case: every clause below is a
  `node tools/lint-*.js` / `check-portability.js` invocation, nothing a runtime test can observe. The
  criterion is discharged by the recorded transcript of those commands in the build log, plus CI.)*
  `node tools/lint-layers.js`, `node tools/lint-odr.js`, `node tools/lint-nonfinite-symbols.js`,
  `node tools/lint-float-bit-goldens.js`, `node tools/lint-arch-guarded-includes.js` and
  `node tools/check-portability.js` all pass on the new header and the four new TUs. No
  `std::isnan`/`isinf`/`isfinite` anywhere; no `constexpr std::log2`; no narrowing in brace init
  (designated initialisers throughout, FR-002); classes PascalCase, methods camelCase, members
  trailing underscore, constants `kPascalCase`.
- **SC-020 — Shared components are byte-unchanged.** *(compliance-pass `git diff` check)*
  `git diff --stat` over `filter_feedback_matrix.h`, `feedback_network.h`,
  `flexible_feedback_network.h`, `i_feedback_processor.h`, `multimode_filter.h`, `resonator_bank.h`,
  `svf.h`, `biquad.h`, `dc_blocker.h`, `crossfading_delay_line.h`, `delay_line.h`,
  `envelope_follower.h` and `brownian_drift.h` reports **no changes** (FR-090), and
  `dsp_primitives_tests`, `dsp_processors_tests` and `dsp_systems_tests` are all green.
- **SC-021 — Thirty minutes of evolution: never static, never divergent.**
  *(`FeedbackEcology_LongEvolution`, `[long]`)*
  A 30-minute render of the reference patch on 60 s of drive followed by a slow 0.05 Hz noise
  excitation. Compute the spectral centroid and the band-energy vector (8 octave bands) on
  10-second windows (180 windows). Every assertion is single-valued and reproducible — an earlier
  draft left two of them without an operational definition, so two implementers would have written
  two different tests:
  **(a) Not static.** The standard deviation of the centroid across the 180 windows is **> 3 %** of
  its mean.
  **(b) Not periodic (amended 2026-09-13).** Sample the six continuous FR-052 cutoff trajectories
  (`getLoopTargetCutoffHz(i)`) at the end of every window. For every lag from 15 to 25 minutes, **no
  two windows agree on all six values within 1e-6 relative**; the count of repeating pairs is asserted
  zero. A frozen wander repeats at every lag and an LFO-driven one repeats at the lag matching its
  period (the roadmap's "no LFO loops"), while a bounded random walk never does. The **log-band cosine
  similarity** (natural log of the 8 band energies) at those lags is still computed and its maximum
  **reported, not gated**: the build measured 0.99999 between windows 20 s apart in phase, because the
  0.05 Hz raised-cosine excitation makes the spectral envelope periodic by construction, so that
  metric measured the drive and not the ecology. The earlier "within 1 % of the window 20 minutes
  earlier" named no distance and no normalisation and could not have discriminated anything.
  **(c) Not divergent.** The centroid never leaves `[0.5×, 2×]` its median.
  **(d) Neither dying nor creeping.** The **least-squares slope** of the window RMS series in dB
  against time, multiplied by the render length, is within **±1 dB**. "No monotone trend" named no
  estimator, and monotonicity is not what a least-squares slope measures.
  This is roadmap lines 29–30's "Nothing repeats exactly" and lines 94–95's overnight requirement, at
  the length a test can afford; both are cited in the Traceability table so the cost of the second
  30-minute render is a recorded decision. Arm (d) deliberately overlaps SC-001 (b)'s per-minute
  stationarity assertion — that one runs on the worst-case corner with the governor on, this one on
  the reference patch under slow excitation, and a drift that only appears under one of the two is
  exactly the finding worth having.
- **SC-022 — Both loop counts the roadmap names are real.** *(`FeedbackEcology_LoopCount`)*
  **(a)** `numLoops = 5` and `numLoops = 6` each: render non-silent and satisfy SC-001 (a)'s four
  bounded assertions on a 60 s corner render.
  **(b) The FR-017 divisor is verified exactly, not through a level tolerance.** At each of the two
  counts, the wet output reproduces `clamp(wetGain × Σ_i tap_i / sqrt(numLoops))` — SC-016's identity,
  evaluated at that count — within `kSampleTolerance`. A build that omits FR-017, or divides by the
  awake count, or divides by `kMaxLoops`, misses by 0.79 dB (six against five) or produces NaN (awake
  count at zero) and fails by orders of magnitude more than the tolerance. **The wet RMS of the two
  patches is reported, never gated**, and an earlier draft's "within 2 dB" band is dropped in both
  directions: 2 dB is wider than the 0.79 dB effect it was meant to detect, so a build with no
  normalisation at all passed it, and *any* fixed band on that ratio is the wrong instrument — with
  six partially correlated loops the correct ratio lies anywhere between 0 dB (incoherent summing,
  where `1/sqrt(n)` exactly cancels) and +0.79 dB (fully coherent), so a band tight enough to catch a
  missing divisor would fail a correct build whose loops happen to be well correlated. The exact
  identity has neither problem.
  **(c) The mid-render count change is FR-075's, and it is click-free.** `setNumLoops` from 6 to 5
  during a render fades loop 5 out through the FR-061 gate ramp rather than cutting it
  (`resonance_drift_network.h:577-586` precedent) **and** ramps the FR-017 divisor over `kGainRampMs`
  instead of stepping it by 0.79 dB at the instant of the call; `ClickDetector` reports zero detections
  across the call and for 500 ms after it. Without FR-075's divisor ramp this assertion is unreachable —
  which is why FR-075 exists: no requirement defined `setNumLoops` at all before this revision, while
  three criteria called it.
- **SC-023 — The resonator's realised ring, not the value it filed.** *(`FeedbackEcology_ResonatorRing`)*
  FR-013's `kMaxResonatorQ = 100` ceiling has no other detector. `getLoopResonanceRt60(i)` derives
  from `appliedResonanceQ`, which `updateResonator` stores from `rt60ToQ(...)` **before** the
  coefficients are written, so a build that regressed to `Biquad::configure`/`BiquadCoefficients::calculate`
  — clamped at `biquad.h`'s `kMaxQ = 30` — would still store `Q = 95.5`, still report 1.0 s, and still
  run a filter at `Q = 30`. This criterion measures the ring itself.
  *Fixture (one `SECTION` per loop under test).* `prepare()`; `setLoopGain(i, 0.0f)` on every loop and
  every coupling entry to `0.0f`, so there is **no feedback** and the tap is the loop chain's own
  impulse response rather than a loop resonance; `setLoopCutoffHz(i, maxCutoffHz)` with
  `setLoopFilterMode(i, Lowpass)` so the `SVF` in front of the resonator is near-transparent at the
  resonator centre; `setWanderEnabled(false)`; `setLoopInputGain` 1.0 on the loop under test and 0.0
  elsewhere; then **`reset()`** (the mandatory `configure → reset() → render` rule, SC-002), then a
  single-sample unit impulse followed by 3 s of silence, read through the FR-073 tap.
  **(a) The realised ring at the lowest default centre.** Loop 5 (`kDefaultLoopResonanceHz[5] = 210 Hz`,
  `kDefaultResonanceRt60 = 1.0 s`) is the one centre at which 1.0 s is reachable without clamping:
  `rt60ToQ(210, 1.0) = 95.50 < kMaxResonatorQ`. Measure the decay — peak of `|tap|` in each successive
  1-cycle window, converted to dB, least-squares fit of the dB-versus-time line over the span from
  −5 dB to −40 dB below the initial peak, extrapolated to −60 dB. **The measured RT60 is within ±10 %
  of 1.000 s**, and is reported.
  **(b) The realised ring at a clamped centre.** Loop 0 (1200 Hz, request 1.0 s, `rt60ToQ` returns
  545.70, clamped to `kMaxResonatorQ = 100`, realised 0.1833 s). Same measurement, **within ±10 % of
  0.1833 s**.
  **(c) The getter tells the truth about the filter in force.** For every loop,
  `getLoopResonanceRt60(i)` agrees with that arm's measured RT60 to within the same ±10 %, and
  reproduces the six-row default table (0.1833, 0.2587, 0.3665, 0.5174, 0.7330, 1.0000 s) to within
  `1e-3 s`. (b) and (c) together are what tie the bookkeeping field to the coefficients.
  **The floor that may never be weakened**, because it is the whole point of the criterion: **loop 5's
  measured RT60 must exceed 0.5 s.** At `kMaxQ = 30` the realised figure is
  `30 × kLn1000 / (π × 210) = 0.3141 s`, and loop 0's is `0.0550 s` — the defect misses by 219 % and
  233 % against a ±10 % band. If the ±10 % band proves too tight for the measurement method (the delay
  and the DC blocker are in the path, and the fit is over a finite window), it may be re-pinned from a
  **recorded** measurement under FR-080's stop-and-surface rule; the 0.5 s floor and the shape of the
  assertion may not move, and the response to a genuine miss is to fix the coefficient path, never to
  lower the floor.
- **SC-024 — FR-003's entry-point contract: the guard ladder and both legal aliasings.**
  *(`FeedbackEcology_EntryPointContract`)*
  None of the criteria FR-003 was previously mapped to touches a null pointer, a zero length, an
  unprepared render or either aliasing. Four arms, on a prepared, settled reference-patch instance:
  **(a) Null pointers write nothing and advance nothing.** For each of `inL`, `inR`, `outL`, `outR`
  passed as `nullptr` in turn, and all four at once: pre-fill both output buffers with a sentinel
  (`-7.5f`), record `getLoopCurrentDelayMs(i)`, `getLoopGate(i)`, `getGovernorRms()` and
  `getLoopCrossfadeCount(i)` for every loop, call `processBlock`, then assert the output buffers are
  **unchanged sample for sample** and every recorded value is **unchanged**.
  **(b) `numSamples == 0` consumes no control step.** Render 512 samples, call `processBlock(..., 0)`
  a hundred times, render another 512; the concatenation is **bit-identical** to an unbroken
  1024-sample render on a second instance. This is SC-010's partition-invariance instrument applied to
  the zero-length case, and it is the only way to observe `controlPhase_`.
  **(c) An unprepared instance writes exactly `numSamples` zeros.** On a default-constructed instance,
  pre-fill both outputs with the sentinel beyond `numSamples`, call `processBlock` with
  `numSamples = 333`: the first 333 samples of both channels are exactly `0.0f`, sample 333 onward
  still holds the sentinel, and every FR-071 getter is unchanged.
  **(d) Both legal aliasings reproduce the non-aliased render bit-identically.** `inL == outL` (with
  `inR == outR`), and the crossed pairing `inL == outR` with `inR == outL`, each rendered 10 s on an
  instance configured identically to a non-aliased reference and compared with **exact `==`**, not a
  tolerance — the aliased and non-aliased paths are the same code, and the render's per-sample local
  dry capture is why both are legal. Any difference is a real divergence, not float drift.
- **SC-025 — FR-055's lane decimation, and the rebase that is not a reset.**
  *(`FeedbackEcology_WanderRateMapping`)*
  Every other wander-exercising criterion runs at the default (decimation 2, effective 33.3 s,
  indistinguishable from a hard-wired-decimation-1 build's `kTauMax`-saturated 30 s) or at
  `kMaxWanderRateHz` (decimation 1 in both builds). The mechanism only bites near `kMinWanderRateHz`,
  and the `laneCounter_ %= newDecimation` rebase was untested entirely.
  **(a) The mapping.** `setWanderRate(kMinWanderRateHz)` → `getLaneDecimation() == 17`;
  `setWanderRate(kDefaultWanderRateHz)` → `2`; `setWanderRate(kMaxWanderRateHz)` → `1`. Plus the clamp
  ends: `setWanderRate(0.0f)` and `setWanderRate(1e6f)` land on `kMinWanderRateHz` / `kMaxWanderRateHz`
  as reported by `getWanderRate()`, with the matching decimations; a non-finite argument is a no-op
  (FR-009).
  **(b) The behavioural arm — the one a hard-wired decimation fails.** Two 300 s renders of the
  reference patch at 48 kHz from the same seed, one at `kMinWanderRateHz = 0.002` (decimation 17,
  effective correlation time 500 s) and one at `0.0333 Hz` (decimation 1, `tau` saturated at
  `kTauMax = 30 s`). For every loop, record the **range** (max − min) of `getLoopTargetDelayMs(i)`
  sampled once per block over the window. **The mean range across the six loops at `kMinWanderRateHz`
  is at most half the mean range at 0.0333 Hz**, and both are reported. A build with `laneDecimation_`
  hard-wired to 1 produces a ratio of **1.0** and fails; a correct build's ratio is driven by
  `sqrt(30 / 500)` and is comfortably beyond 2. The **factor of 2 is the floor** and may be re-pinned
  upward, never downward, from a recorded measurement (FR-080).
  **(c) The rebase, not a reset.** Render 60 s of the reference patch while calling
  `setWanderRate(getWanderRate())` — the **same** value, so `smoothness` and the new decimation are
  unchanged — every 96 samples, a phase that is not a multiple of `kControlChunkSamples`. Compare
  against an unmolested 60 s reference from the same seed: **within `render_fingerprint.h`
  tolerances.** A build that wrote `laneCounter_ = 0` instead of `laneCounter_ %= newDecimation`
  advances the lanes on every control step instead of every second one and diverges grossly.

## Edge Cases

**RT-safety boundaries**

- `processBlock` with any null pointer, with `numSamples == 0`, and on an unprepared instance —
  FR-003's three-rung guard ladder, in that order. Unprepared writes exactly `numSamples` zeros and
  advances nothing.
- `numSamples` far above `maxBlockSamples` (65 536 against a configured 64) — accepted, FR-085.
- Input and output pointers aliased in both pairings (`inL == outL`, and `inL == outR` with
  `inR == outL`) — the dry capture is per-sample and local, so both are legal (FR-003).
- Setters called from the same thread between blocks and mid-block-boundary; no setter allocates,
  locks or throws (FR-006, SC-007).
- Denormals on the coupling path as the network decays to silence over the last 29 minutes of
  SC-001 (a) — FR-084's explicit `flushDenormal` on `prevY_`, `prevOut_` and the wet sum, not just the
  sub-objects' own flushes.

**Parameter extremes**

- Every scalar at both ends of its clamped range simultaneously — SC-001 (a)'s corner.
- Raw row sum 3.40 against `kMaxTotalLoopGain = 0.95` — FR-035, SC-015 (c).
- `ratio = 1.0` (governor off) combined with `ownFb = kMaxLoopGain` — the configuration in which only
  the structural rungs hold, which is why SC-001 (a) runs exactly there.
- `numLoops = 1`: no coupling partner exists; FR-035's row sum ranges over `j < numLoops`, so every
  slot at index ≥ 1 is **excluded from the sum**, not merely inert — a fully written coupling matrix on
  the unused slots does not attenuate the survivor, `getLoopAppliedTotalGain(0)` equals `getLoopGain(0)`
  exactly (SC-015 (d)), and `1/sqrt(1) = 1`.
- All six loops dormant: the wet sum is `0/sqrt(6) = 0`, **not** `0/sqrt(0)` — FR-017's divisor is
  `numLoops`, never the awake count.
- `baseDelayMs = kMinDelayMs = 10` with the maximum wander fraction: the clamp at FR-052 keeps the
  realised delay at or above 10 ms rather than letting it reach zero, which would make the loop a
  zero-delay algebraic loop.
- A delay wander whose peak excursion is under 100 samples: the delay **does not move**, by design
  and by the shipped line's threshold — SC-005 (b) asserts the documented stillness.
- Resonator RT60 at `kMaxDecayTime = 30 s` on all six loops with maximum feedback — the longest ring
  the component admits; SC-001 (a)'s 29-minute silent tail is 58 RT60s.
- `setCrossfadeTime` outside `[5, 100]` ms is clamped by the shipped line (`:142`); the component
  never writes outside that range.

**Sample-rate changes**

- `prepare()` re-called on a live object at a new rate: legal, but **not** configuration-preserving
  (FR-005 (c)) — it discards the caller's coupling matrix, every loop gain, the FR-013 resonator table,
  the FR-022 delay table, the FR-052/FR-053 wander tables, `mix` and `wetGain`, restoring every
  default, then re-derives every rate-dependent value; the seed itself survives because FR-005
  distributes it last. **A Phase-11 note, not a Phase-5 requirement:** a host sample-rate change is
  therefore not transparent to a caller — the full parameter set must be re-pushed after
  `setupProcessing`.
- 1 Hz, 0, negative and NaN rates: substituted/floored to `kMinUsableSampleRate = 8000.0` (FR-083),
  and the `static_assert` proves the resonator clamp pair stays ordered there — without the floor,
  `std::clamp(hz, 20.0f, 0.45f)` is UB that MSVC traps (`resonance_drift_network.h:283-296`).
- 192 kHz: the delay buffers quadruple to 3 145 728 bytes (FR-082) and 100 crossfade-threshold samples
  become 0.521 ms, so the delay staircase gets four times finer — SC-005 (c) and SC-008 cover both.
- The realised delay in **milliseconds** must match across rates within 1 % (SC-011), which is the
  test that catches a `float`/`double` sample-rate confusion in the ms↔samples conversions.

**Seed determinism**

- Same seed, two instances → identical within `render_fingerprint.h` tolerances (SC-009 (a)).
- `setSeed` mid-render: re-derives the streams and resets the lanes (FR-054); the audio state is
  **not** cleared, so the render continues without a discontinuity but the modulation restarts —
  documented, and the reason `setSeed` is separate from `reset()`.
- Seed `0`: `deriveStreamSeed`'s non-zero substitution (`core/random.h:112`) prevents the collapse
  onto one stream that `Xorshift32::seed(0)` would otherwise cause (`:73-75`).
- Salt-table collision: the `static_assert`s in FR-054 make it a compile error, and SC-009 (d) makes
  it a runtime failure if the asserts are ever weakened.
- **No bit-exact golden anywhere** (roadmap line 507): every determinism claim uses
  `render_fingerprint.h`, and `tools/lint-float-bit-goldens.js` gates it.

## Decisions taken where the roadmap is silent

- **D-1 — `SVF` for the loop filter, not `MultimodeFilter`.** Overview 1 carries the evidence:
  `MultimodeFilter::processSample` rebuilds coefficients every sample (`:218`, `:372`), its cheap
  block path is structurally unusable inside a feedback loop, and `FilterFeedbackMatrix` — the
  component roadmap line 275 tells this spec to learn from — uses `SVF` in exactly this position
  (`:174`). FR-080's probe measures both so the decision is ratified from a number (Clarifications,
  DECISIONS-CONFIRMED).
- **D-2 — `FilterFeedbackMatrix` is not extended to N = 6.** `static_assert(N >= 2 && N <= 4)`
  (`:72-73`) with explicit instantiations for 2/3/4 (`:671-673`); its `N²` delay-line grid would be 36
  lines at N = 6 against this component's 6; its per-path delay model (a delay on every coupling path)
  is not the roadmap's topology (a delay inside each loop). FR-092.
- **D-3 — One bandpass biquad per loop (a plain `Biquad`, directly-computed coefficients, FR-013/FR-076),
  not a `ResonatorBank` and not `IResonator`.** Overview 2 carries the
  `IResonator` half. The `ResonatorBank` half: a bank per loop costs, per sample, three
  `OnePoleSmoother::process()` advances plus a 16-iteration loop with 15 `continue`s
  (`resonator_bank.h:471-473`, `:486-489`) to reach one enabled biquad — six of those is 18 smoother
  advances and 96 loop iterations per sample for six biquads' worth of work. Phase 3 needed the bank
  because it wanted twelve resonators sharing configuration; this phase wants six independently
  excited single modes, and `processIndividual` (`:554`) cannot serve them because it takes **one**
  input for the whole bank. FR-080's stage probe measures the single-slot-bank-per-loop alternative
  anyway so the choice is measured, and this decision stands unless that probe shows the bank buys
  something a criterion needs (FR-013).
  **Retuning is a stepped hard swap, not smoothed (OQ-1, revised this session).** The bank's own
  per-sample `OnePoleSmoother`s (`:471-473`) are why a `ResonatorBank` retune does not click, and an
  earlier draft of this decision replaced that with `SmoothedBiquad` (`biquad.h:527`) to keep the same
  property. That replacement is now known to be defective: `SmoothedBiquad::setTarget` (`:547`) routes
  through `BiquadCoefficients::calculate` (`:673`), which clamps `Q` to `biquad.h`'s own `kMaxQ = 30`,
  silently undercutting the `kMaxResonatorQ = 100` ceiling FR-013 specifies. The corrected decision is
  a plain `Biquad` fed coefficients computed directly by this component (`resonator_bank.h:668-682`'s
  formula) and written through `setCoefficients`, with the resonator centre and RT60 setters declared
  **stepped** (FR-076) and excluded from the click-freedom assertion by name (SC-001 (d)) — accepted
  because a resonator retune is a rare, user-driven control event, not something automated at audio
  rate.
- **D-4 — Six loops by default, not five.** Roadmap line 272 says "5–6". Six is the richer ensemble
  and the more expensive measurement, so specifying six makes SC-004 the honest budget test; five
  remains reachable and SC-022 exercises it. If SC-004 misses, FR-080's third lever reduces the
  default to five — a user decision, from the measured table.
- **D-5 — Coupling is non-negative.** `FilterFeedbackMatrix` admits `[-1, +1]`
  (`kMinFeedback = -1.0f` at `filter_feedback_matrix.h:85`, `kMaxFeedback = 1.0f` at `:86`; the
  "negative inverts phase" wording is a separate doc comment on `setFeedbackAmount`'s `amount`
  parameter at `:178`, not co-located with the constants). Six mutually coupled loops with sign changes produce
  cancellation notches whose audible signature is a level drop, which is the opposite of the
  "interaction" roadmap line 281 wants measured, and they would make SC-002's coherence metric
  non-monotone for reasons unrelated to coupling strength. Excluded; a later phase can add polarity
  with its own criterion.
- **D-6 — The wet path is mono.** FR-016 and the Non-Goals carry it. The alternative — per-loop pan,
  as Phase 3 has (`resonance_drift_network.h:829`) — is a feature roadmap lines 272–278 do not
  request, and it roughly doubles the output-stage cost. The consequence (image narrowing at high
  `mix`) is real and is written down so Phase 10's listening checkpoint treats it as a known
  trade-off.
- **D-7 — Previous-sample coupling, not same-sample.** FR-031. Same-sample coupling would make the
  output depend on loop index order and would create an algebraic loop for any pair with mutual
  coupling. `FilterFeedbackMatrix` solves it the same way (`:569-580`, `:613`).
- **D-8 — `kDefaultWetGainDb = 0.0f`, and it is *not* a measured constant.** Phase 3 needed a
  measured +34.5 dB trim (`resonance_drift_network.h:230`, with the whole calibration recorded at
  `:172-229`) because twelve narrow bandpasses attenuate a broadband drive by ~38 dB. This
  component's loops are broadband feedback paths whose steady-state gain is of order unity, so no trim
  is needed and none is invented. SC-018 (b) and SC-022 are the criteria that would catch it if this
  assumption is wrong; if they show the wet is unusably quiet or loud, the response is to measure and
  record a constant the Phase-3 way, never to nudge one. Its **positive** half is bounded by
  construction rather than by convention: FR-046 places the output clamp downstream of the trim, so
  even `+24 dB` on a maximal wet sum leaves the component at `kOutputClamp`, and SC-013 (b) uses
  exactly that configuration as the proof that the clamp counter is wired.
- **D-9 — The governor is a control-rate regulator with a per-sample ramp**, not a per-sample
  compressor. One `std::pow` per 64 samples (FR-044) against 750 per second; the RMS follower's own
  20 ms attack is far slower than the control grid, so evaluating the law at 750 Hz loses nothing the
  follower could have expressed. FR-045's `LinearRamp` is what makes it click-free (SC-006 (e)).
- **D-10 — The non-finite trap resets the follower as well as the loop.** FR-047. Without it, one
  poisoned sample makes `getGovernorGain()` NaN and multiplies the **entire network** by NaN forever,
  because `EnvelopeFollower::processSample` does not validate its input (`:163`) and
  `flushDenormal` does not clear NaN. This is the single most likely way for this component to fail
  catastrophically and quietly, and it is why SC-012 (b) names the two stages explicitly.

## Traceability

| Roadmap statement (line) | Requirements | Criteria |
|---|---|---|
| "New component (L3, `systems/feedback_ecology.h`)" (270) | FR-001, FR-002 | SC-019 |
| "Micro-loop = filter (`SVF`) → …" (272) — the roadmap names `SVF` since the OQ-3 write-back; the substitution for the original `MultimodeFilter` was ratified in Clarifications (DECISIONS-CONFIRMED), D-1, evidence in Overview 1 | FR-011, FR-012 | SC-004 (e) probe arms (a)/(b), SC-001, SC-003 |
| "… → delay (`CrossfadingDelayLine`, 10–500 ms) → …" (272) | FR-020, FR-021, FR-022, FR-023, FR-024, FR-052 | SC-003, SC-005 |
| "… → resonator (one RBJ bandpass, the `ResonatorBank` slot's Q range) → …" (272) — the roadmap names the RBJ bandpass since the OQ-3 write-back; the realisation is one `Biquad` fed direct RBJ coefficients at `Q <= kMaxResonatorQ = 100` (OQ-1), ratified in Clarifications (DECISIONS-CONFIRMED), D-3, evidence in Overview 2 | FR-013 | SC-004 (e) probe arms (d)/(g), SC-001, **SC-023** |
| "… → gain (< 1) → back" (272) — applied **once** per circulation, in FR-015's input sum | FR-015, FR-018, FR-035, FR-041 | SC-001, SC-015 |
| "with `DCBlocker` in-loop" (272) | FR-014, FR-047, FR-048 | SC-001, SC-012 (b)–(d) |
| "5–6 instances" (272) | FR-010, FR-075 | SC-022 |
| "Cross-coupling matrix (each loop bleeds a few % into its neighbours)" (274) | FR-030, FR-031, FR-032, FR-033, FR-034 | SC-002, SC-015 |
| "reuse `FilterFeedbackMatrix`/`FlexibleFeedbackNetwork` topology knowledge" (275) — reused as **knowledge**, not code; D-2/FR-092 record why the class cannot be instantiated (`N <= 4`) | FR-031, FR-034, FR-042, FR-090, FR-091, FR-092 | SC-020 |
| "**Energy governor:** global RMS tracker with soft compression of total loop energy — interaction without runaway" (276–277) | FR-043, FR-044, FR-045 | SC-006, SC-001 (b) |
| "Per-loop tiny life-modulation of delay time and filter cutoff" (277) | FR-050, FR-051, FR-052, FR-053, FR-054, FR-055, FR-056 | SC-005, SC-009 (d), SC-021, **SC-025** (FR-055's lane decimation and the `laneCounter_` rebase, which no other criterion can discriminate) |
| "Input taps from cloud + noise organism" (278) — **DEVIATION: the two named sources reach the loops as one pre-summed stereo pair, not as distinct per-loop signals.** Phase 10 pre-sums the cloud and noise-organism taps before this stage; per-loop diversity comes from FR-074's scalar tap level plus each loop's own filter/delay/resonator settings, never from hearing a different source mix. This entry-point signature is frozen for Phases 8–12 the moment SC-016's bit-identity fixtures exist. | FR-003, FR-016, FR-074 | SC-016, SC-017, SC-018, **SC-024** (FR-003's guard ladder and both legal aliasings — no other criterion touches them) |
| "output mixed back at low level" (278) — trim, then clamp, then crossfade (FR-015's output-stage block) | FR-072 (`kDefaultMix = 0.15f`), FR-017, FR-046 | SC-018, SC-022, SC-013 |
| "bounded output for ANY parameter combination over 30 min renders … worst-case gain/coupling sweep" (280) | FR-040, FR-041, FR-042, FR-044, FR-046, FR-047, FR-035 | **SC-001** (a)–(d), SC-013, SC-015 |
| "audible cross-loop interaction (coherence metric between loop outputs rises with coupling)" (281) — the **gated** instrument is a cross-loop transfer measurement under single-loop excitation; magnitude-squared coherence is identically 1 under this component's common mono drive and is therefore **reported, not gated** (SC-002's opening paragraph carries the algebra) | FR-030, FR-031, FR-033, FR-073, FR-074 | SC-002, SC-016 |
| "no zipper on delay-time drift" (281) | FR-021, FR-023, FR-024, FR-052, FR-076 | SC-003, SC-005 |
| "CPU ≤ 1% per voice" (282) | FR-080 | SC-004 |
| "Nothing repeats exactly" (29–30) + "neither die nor explode" overnight (94–95) — the two statements SC-021's 30-minute evolution render discharges; recorded here because Phase 5's own four criteria (280–282) do not name a long-render evolution test and the second `[long]` render is therefore a deliberate cost | FR-050–FR-056, FR-041–FR-047 | SC-021, SC-001 (b) |
| RT safety, pools sized at prepare (494–495) | FR-002, FR-003, FR-006, FR-081, FR-082, FR-085 | SC-007, SC-008 |
| Boundedness is the theme-level FR; worst-case soak (496–498) | FR-040–FR-048 | SC-001, SC-012, SC-013, SC-021 |
| Layer discipline + ODR sweep (499) | FR-001, New-components table | SC-019 |
| CPU budgets are FRs (500) | FR-080 | SC-004 |
| Dormancy (501–506) — FR-063's sleep-edge clear is a **documented deviation** from the rule's "gain at zero means the component's processing chain is skipped" clause (line 501–502), not a discharge of the rule's escape clause: that clause covers a spec that wants a silent slot to **keep burning** its chain, which is the opposite direction. The deviation is justified in FR-063 in the terms the rule asks for (what the listener would hear) and has house support in the Phase-3 sleep-edge clear (`resonance_drift_network.h:52-56`) | FR-060, FR-061, FR-062, FR-063, FR-064, FR-075 | SC-014 (a)–(e), SC-004 (c) |
| No bit-exact float goldens (507) | — | SC-009, SC-010, SC-011, SC-014 (a), SC-018 (b) — all via `render_fingerprint.h` |
| Portability; WSL probe; aligned-load lint (508–509) | FR-008, FR-009, FR-053 (`constexprLn`, never `constexpr std::log2`), FR-084 | SC-012, SC-017 (constexpr-log arm), SC-019 |
| Naming conventions (510) | FR-001 (`kPascalCase` constants, trailing-underscore members, camelCase methods) | SC-019 |
| Shared-component changes keep Seraphis green (511–513) | FR-090, FR-091, FR-092 | SC-020 |

**Planned test translation units** (all registered by name in `dsp/tests/CMakeLists.txt`'s
`dsp_systems_tests` list, which is enumerated and **not** globbed — an unregistered TU silently drops
out of the build and its cases never run, `dsp/tests/CMakeLists.txt:409-410`):

| TU | Criteria |
|---|---|
| `dsp/tests/unit/systems/feedback_ecology_test.cpp` | SC-005, SC-006, SC-007, SC-008, SC-009, SC-010, SC-011, SC-013, SC-014, SC-015, SC-016, SC-017, SC-018, SC-022, SC-023, SC-024, SC-025 |
| `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp` | SC-001, SC-002, SC-003, SC-021 — the `[long]` set |
| `dsp/tests/unit/systems/feedback_ecology_perf_test.cpp` | SC-004 (a)–(f) including the FR-080 stage probe — `[.perf]` only, plus the `static_assert`ed baselines that CI evaluates on every leg |
| `dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp` | SC-012 only — **the one TU listed in the `-fno-fast-math` block** (`dsp/tests/CMakeLists.txt:528`), because it injects non-finite samples built from bit patterns |

New shared helper: a magnitude-squared-coherence estimator added to `tests/test_helpers/` (none
exists in the tree) for SC-002's **reported** arm (d), registered in
`tests/test_helpers/CMakeLists.txt`. SC-002's **gated** arms need no new helper — the cross-loop
transfer metric is plain RMS over the FR-073 taps.

## Assumptions

1. **The component's input is the voice-internal bus after the resonance drift network, at roughly
   −12 dBFS per channel.** The roadmap's architecture diagram (lines 45–72) places Feedback Ecology
   below the Resonance Drift Network and beside Granular Ghosts, inside the per-voice block. The
   level is an assumption used only to choose `kGovernorThresholdDb` and the SC drive level;
   Phase 10 owns the real level, and the governor threshold is a settable parameter precisely so that
   assumption is cheap to correct.
   **Corrected during the build (T012).** The assumption was used wrongly, not merely optimistically:
   the threshold names a level on FR-043's **tracker**, not on the input, and the two differ by
   ≈ 30 dB on this component's default tables (narrow resonators, broadband drive). The input-level
   assumption itself is untouched and still Phase 10's to confirm; what changed is that
   `kGovernorThresholdDb` is now derived from a *measured* tracker level (−52 dB default, range
   floor −72 dB — FR-044 and DERIVATION TABLE 3) instead of from the input level. If Phase 10 finds
   the real input level is not −12 dBFS, the correction is the same one-constant edit, re-measured
   the same way.
2. **Six loops per voice, at 4–8 voices, is the Phase-10 shape.** Roadmap line 92 budgets ~4–5 % per
   voice; the 1 % this phase claims is one fifth of that. If Phase 10 finds the ecology needs to be
   per-voice-optional, nothing here changes — the component is already fully bypassable at zero cost
   through `numLoops` and dormancy (SC-004 (c)).
3. **`tanh` is affordable six times per sample.** If SC-004 shows otherwise, FR-080's first lever is
   the shipped `FastMath` alternative under its own error-bound test — the documented pattern for a
   transcendental swap that the render fingerprint cannot see (`dsp/CLAUDE.md`, "Know the limit").
4. **The `CrossfadingDelayLine` staircase is musically acceptable at these depths.** It is the only
   delay-modulation behaviour the shipped primitive offers, roadmap line 281 asks for the absence of
   zipper rather than for continuous glide, and SC-003 bounds the artefact. A listening checkpoint
   before Phase 10 is the right place to revisit it; if continuous glide turns out to be wanted, that
   is a new primitive and a new phase, not a quiet change here.

## Review notes

This revision answers a spec review whose issues are recorded here where the resolution **differs
from the one the review suggested**, so a later reader can see the choice was taken rather than
missed. Everything not listed here was applied as suggested.

1. **The double loop-gain issue (FR-015/FR-018/FR-035/FR-041) — resolved the other way round.** The
   review was right that the law applied the loop gain twice and recommended keeping the factor at the
   **output** (`y_i = tanh(b_i · appliedOwnFb_i · governorGain)`) with an unweighted `prevOut_i` in the
   input sum. This spec instead puts the single factor in the **input sum** and leaves the output as
   `tanh(b_i · governorGain)`. Reason: with the factor at the output, the ℓ∞ gain of the map
   `prevOut → y` for loop `i` is `ownFb_i · (1 + Σ_j coupling_[j][i])` — at the worst case
   `0.90 × 3.50 = 3.15` — which is **not** FR-035's `G_i = ownFb_i + Σ coupling`, so FR-035's row-sum
   normalisation would no longer be the quantity FR-041's contraction argument bounds and both would
   have had to be re-derived. With every feedback coefficient in the input sum, the sum of absolute
   coefficients feeding a loop is exactly `G_i <= 0.95`, FR-041's `0.95 × 1.00066 ≈ 0.9506` stands as
   written, and `getLoopAppliedTotalGain(i)` reports precisely the number the argument uses. The
   review's substantive requirement — **one** factor per circulation, one name for it — is met.
2. **`setWetGain`'s range kept at `[-24, +24]` dB (the review's option (b), not option (a)).**
   Narrowing `kMaxWetGainDb` to 0 dB would have made the trim attenuation-only and would also have
   made FR-046's clamp unreachable through *any* control, leaving rung 4 provable only through a
   white-box arm. Keeping the positive half and placing the clamp **after** the trim gives the clamp
   exactly one reachable trigger, which SC-013 (b) now uses; boundedness across the whole range is
   preserved by the clamp itself, and FR-046's zero-engagement requirement is scoped to
   `wetGain <= 0 dB` with SC-001 (c) sweeping that half and saying why.
3. **SC-022's tolerance was not tightened to 0.3 dB; the level comparison was demoted to a report and
   replaced by an exact identity.** The review is right that 2 dB cannot detect a missing 0.79 dB
   normalisation. But no fixed band on that ratio can: with six partially correlated loops the correct
   five-against-six wet-RMS ratio lies anywhere between 0 dB (incoherent summing, where `1/sqrt(n)`
   cancels exactly) and +0.79 dB (fully coherent), so a 0.3 dB band would fail a correct build whose
   loops happen to be well correlated. SC-022 (b) instead asserts the FR-017 divisor exactly, through
   SC-016's tap-reconstruction identity evaluated at both counts, where a missing or wrong divisor
   misses by orders of magnitude more than `kSampleTolerance`. The review's second half — that no FR
   defined `setNumLoops` — is fully applied as **FR-075**.
4. **SC-021 is kept, with the traceability row the review asked for as the alternative.** Its arms are
   now operationally defined (cosine similarity on log-band-energy vectors; least-squares slope), and
   a Traceability row cites roadmap lines 29–30 and 94–95 so the second `[long]` render is a recorded
   decision rather than an unattributed criterion. The overlap with SC-001 (b) is kept deliberately
   and the difference between the two fixtures is stated.
5. **SC-005's counts were relaxed *and* the defaults were strengthened, in the same edit.** Relaxing
   an assertion alone would be dodging. FR-052's scalar `kDefaultDelayWanderFraction = 0.08` is
   replaced by a per-loop table derived in FR-023 from `BrownianDrift`'s stationary statistics
   (`kInternalStd = 0.5f`), so every default loop's crossfade step costs ≈0.5 σ_Δ instead of the
   41 ms loop's ≈1 σ_Δ; only then are the counts restated as what the statistics support, with a floor
   ("every loop steps at least once") that may never move and the remaining numbers marked
   re-pinnable only from measurement.
6. **SC-003 (b)'s absolute 0.05 first-difference threshold** is normalised to the render's measured
   peak, and the peak itself is asserted (≥ 0.05), which is the guard the parenthetical "against a
   signal peak of order 1" was assuming but not checking.

No issue in the review was rejected outright.
