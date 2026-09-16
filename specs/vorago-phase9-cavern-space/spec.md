# Feature Specification: Vorago Phase 9 — Cavern Space Engine

**Spec slug:** `vorago-phase9-cavern-space`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 9 (lines 410–430); reuse-inventory row
**L9 Space Engine** (line 117); the Seraphis-sequencing note (lines 137–139); the ODR note
(lines 127–129); the dependency graph (lines 518–531, where `AetherReverb ✅ (shipped)` is Phase 9's
only edge); the cross-cutting constraints (lines 538–562), of which **"Shared-component changes …
`AetherReverb` extensions … must keep Seraphis's tests green"** is lines 560–562 and the **Dormancy**
rule is lines 547–555; roadmap **Open Question 3** (lines 569–570) — the one decision the roadmap
explicitly defers *to this spec*. Every roadmap line number below was read and re-verified against
`specs/Vorago-roadmap.md` this session.
**Layer:** one new Layer 4 component, `CavernVerb`, at `dsp/include/krate/dsp/effects/cavern_verb.h`
(roadmap line 418), **plus one append-only extension to the shipped Layer 4 `AetherReverb`**
(justified in *Architecture ruling* below; bounded by FR-040 – FR-048 and gated by SC-012).
**Test target:** `dsp_effects_tests` — an **enumerated, not globbed** source list opening at
`add_executable(dsp_effects_tests` (`dsp/tests/CMakeLists.txt:504`) and closing at `:533`. A TU that
is not listed there silently drops out of the build and its cases never run. That target already
carries `target_compile_definitions(dsp_effects_tests PRIVATE KRATE_DSP_AETHER_TEST_HOOKS)`
(`:549`), and the comment above it (`:541-548`) states why the define is **target-wide and not
per-source**: it changes `AetherReverb`'s class definition, so every TU in the image must see the
same one or it is an ODR violation. Any new TU added here inherits that define automatically.
**Depends on:** `AetherReverb` — **shipped and verified this session** at
`dsp/include/krate/dsp/effects/aether_reverb.h:1377` (Seraphis Phase 6). No Vorago phase 1–8 header
is included by this component. Phase 10 composes it with everything else.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

---

## Clarifications

### Session 2026-09-16

- **Q: Is the append-only `AetherReverb` extension (FR-040 – FR-048) approved?** → Approved as
  specified: `setDamperOffsetsOctaves` + `getEffectiveDampingCoefficient`, exact inertness by
  assignment at depth 0 applied after the existing epsilon gate, latched under freeze, Seraphis
  suites unedited. [FR-040, FR-041, FR-043, FR-044, FR-045, FR-046, FR-047, SC-012]
- **Q: Damper excursion ceiling and default rate?** → `kMaxDamperOctaves = 1.5`; default damper rate
  at the slow end (`τ ≈ 25.5 s`, inside the 20–30 s policy band); default damper depth
  `kDefaultDamperDepth = 0.35`. [FR-031, FR-032, FR-066]
- **Q: ER tap count and first-arrival floor?** → 12 taps (`kEarlyTapCount = 12`); first arrival
  `kEarlyFirstArrivalFloorMs = 60.0 ms` at the default ER size; default ER size (the last tap)
  `220.0 ms` (a 3.7 : 1 span). [FR-020, FR-021, FR-023, FR-066]
- **Q: Does the 5 % global CPU budget cover `CavernVerb` in total, or minus the already-budgeted
  `AetherReverb`?** → The literal total of the whole `CavernVerb` (ER + dampers + owned
  `AetherReverb` + STFT): `kMaxAdmissibleNs = 355 555.6` ns/block at 48 kHz; the shipped
  `AetherReverb` baselines (114 595 default, 200 114 worst) are the starting headroom SC-009
  measures against. [FR-081, SC-009]
- **Q: What are the shipped default values of the seventeen controls, and of
  `maxEarlySeconds`?** → A full numeric default table is pinned (FR-066); SC-007 (a)/(b)'s
  thresholds (0.35×, −30 %) are re-derived against that table and retained unchanged. [FR-066,
  SC-007]
- **Q: How does `setDamperDepth` become an octave offset — scale, bipolar or darker-only, and the
  depth path?** → Variance-normalised, zero-mean bipolar:
  `offset_i = depth · kMaxDamperOctaves · (drift_i.getCurrentValue() / kInternalStd)`, clamped to
  `±kMaxDamperOctaves`; `depth` is routed through `BrownianDrift::setDepth` so depth automation is
  ramped click-free by the 150 ms output smoother. [FR-031, SC-003]
- **Q: What are the ER tap delays, gains, polarities and side assignments?** → A generating law, not
  a hand-picked table: delays from a coprime integer series scaled to the default ER size (first
  60 ms, last 220 ms, 12 taps); gains `g_i = g_0 · exp(−α·d_i)`, all positive (no negative taps);
  a deterministic alternating L/R side rule starting at L; `kEarlySizeMinMs = 80.0 ms`,
  `kEarlySizeMaxMs = 600.0 ms`, default `maxEarlySeconds = 0.30 s`. The implementer instantiates the
  concrete integer series and `static_assert`s FR-022 and FR-028 over it. [FR-020, FR-021, FR-023,
  FR-028, FR-066]
- **Q: What is the ER absorption law?** → Per-tap cutoff geometric in tap index:
  `fc_i = fc_max·(fc_min/fc_max)^(v·i/(n−1))`, `fc_max = 18 000 Hz`, `fc_min = 1 200 Hz`; `v = 0` is
  an exact bypass by assignment (no coefficient evaluated); `n = kEarlyTapCount` one-pole states,
  one per tap; `getEarlyTapGain` reports the static table gain only, absorption state is exposed by
  a separate accessor. [FR-025, FR-027]
- **Q: What exactly excites the owned FDN?** → The post-absorption ER tap sum only, at the send
  gain — no direct/dry input path. The send is taken after the ER size smoother's interpolated
  reads, the identical taps the output sees, and that same mono sum feeds both the owned engine's L
  and R inputs. The ≈80 ms onset gap is deliberate. [FR-026]
- **Q: What is the ER's stereo topology?** → Mono-sum source: the input is summed to mono before the
  ER delay line (state may be a single mono line); taps read that mono signal and are placed into
  the output L/R busses by the side rule, so the ER pattern is invariant to input panning.
  `setWidth` applies to the late field only, never to the ER bus. [FR-019, FR-020]
- **Q: How is `CavernVerb`'s control grid coupled to the owned engine's, and when are damper offsets
  published?** → `CavernVerb` slices every `processStereoBlock` call into its own
  `kControlChunkSamples`-sample sub-blocks anchored to an absolute counter started at `prepare()`,
  publishes the damper offsets at each sub-block boundary, and calls the owned engine once per
  sub-block — one owned-engine call per 64 samples, accepted as the cost of keeping the two control
  grids phase-aligned by construction. [FR-007]
- **Q: Does `CavernVerb` keep an internal dry path and `setMix`?** → Yes, kept as specified:
  equal-power dry/wet (FR-063), the internal dry path with its four alignment lines (dry L/R, ER
  L/R, each `getLatencySamples()` long), 17 controls total; `CavernVerb` remains a self-contained
  insert like `AetherReverb`. FR-065's wet chain keeps running at `mix == 0`. [FR-019, FR-061,
  FR-062, FR-063, FR-065]

---
- **Q (plan-stage, B-1): FR-022 at order 8 with tolerance 0.05 is jointly infeasible with FR-028's
  gain-sum cap — which constant moves?** → `kIncommensurabilityOrder = 6`; tolerance, tap count,
  60/220 ms, `g_0` and the 2.0 ceiling unchanged; the plan's verified coprime table measures
  0.05411. [FR-022, SC-006]
- **Q (plan-stage, B-2): SC-003 (a)'s bound was derived for the post-clamp offset while FR-037
  published pre-clamp, and the literals 0.02655 / 0.00677 were arithmetically wrong (the smoother
  constant is a time to 99 %; the published swing is 4.5, not 3, octaves).** →
  `getDamperOffsetOctaves` publishes the **post-clamp** value; SC-003 (a)/(b) are written as the
  parameterised law, 0.19562 octaves/chunk and 0.04988 in coefficient units at defaults. [FR-037,
  SC-003]
- **Q (plan-stage): FR-025's absorption endpoints are not realisable below 40 kHz.** → The Nyquist
  guard is part of the law: `fc_max = min(18 kHz, 0.45 · sr)`, `fc_min = min(1.2 kHz, 0.40 · fc_max)`,
  named public constants `kEarlyAbsorptionNyquistFraction` / `kEarlyAbsorptionSpanFraction`. [FR-025,
  SC-006]
- **Q (plan-stage, confirmations):** (1) damper depth is applied exactly once, via
  `BrownianDrift::setDepth` — FR-031's `v ·` is that call, never a second multiply [FR-031]; (2)
  SC-007 (b) is `centroid_cavern ≤ 0.70 × centroid_bare` against a bare `AetherReverb` at its own
  defaults [SC-007]; (3) CMake/lint registration lands up front (T001) so every task's failing test
  builds, with the final-group audit as the completeness check [FR-080]; (4) whether FR-028's
  gain-sum check is a `static_assert` or a `REQUIRE` is the implementer's choice, recorded in the
  header [FR-028].


## Overview

Vorago's space is an **enormous underground bunker**: sparse stone slap-backs arriving tens to
hundreds of milliseconds after the source, then a huge, dark, slowly-breathing late field that can be
held indefinitely (roadmap lines 415–427). The roadmap's strategic instruction is blunt and is the
spine of this spec: *"Avoid building two big FDNs"* (line 117). The FDN, its orthogonal morphing
feedback matrix, its Jot per-line absorption, its input diffuser, its freeze latch, its STFT tail
smear and its non-finite recovery machinery already exist, shipped and tested, inside `AetherReverb`.
Phase 9 therefore builds **only what Vorago adds**:

1. **Moving dampers** — the per-delay-line damping filters' cutoffs wander slowly and
   *independently*, "so the space itself breathes darkly" (roadmap lines 421–422).
2. **A cavern early-reflection pattern** — sparse, long-pre-delay, stone-flavoured arrivals, as
   against Seraphis's air-flavoured onset (roadmap lines 423–424).
3. **Dark tuning** — HF decay strongly shortened, size range biased huge, **no shimmer-up taps**
   ("+12 shimmer is Seraphis's identity, not Vorago's", roadmap lines 425–426).
4. **Infinite hold (freeze) retained** — "a drone instrument needs it" (roadmap line 427).

Three facts found by reading the shipped header this session shape every requirement below, and two
of them contradict the roadmap's own description of `AetherReverb`:

- **`AetherReverb` has no early-reflection stage.** Roadmap line 117 and line 415 both describe it as
  "ER → diffusion → FDN → spectral damping". What it actually has in front of the FDN is a **stereo
  pre-delay pair** (`DelayLine preDelayL_/preDelayR_`, `aether_reverb.h:4531-4532`, fed at
  `:4174-4181`, capped at `kMaxPreDelayMs = 200.0f`, `:2743`) followed by a `DiffusionNetwork`
  (`:4183-4185`). A pre-delay is one arrival, not a reflection pattern. **The cavern ER is genuinely
  new work**, not a configuration of something that exists.
- **`AetherReverb`'s "spectral damping" is a per-bin *phase* smear, not damping.** `setSpectralDiffusion`
  (`:2310`) drives `smearSpectrum` inside `runSpectralStage` (`:4047`), an STFT stage that randomises
  phase and applies a coherence make-up gain (`kCoherenceMakeup`, `:2775-2776`). The frequency-dependent
  decay — the thing that actually makes a space dark — is the per-line one-pole at `:4282`
  (`const float c = dampCoeff_[i];`) driven by `setDamping` (`:2244`) through
  `T60_nyq = T60_dc · kDampingNyquistRatio^damping` with `kDampingNyquistRatio = 0.05f` (`:2787-2788`).
  That one-pole **is** the damper the roadmap wants to move.
- **Damping is global and its coefficients are recompute-gated.** There is exactly one `setDamping`
  control; the per-line coefficients `dampCoeff_[i]` are derived from it *and* from the line's own
  length inside `updateDecayAndDamping()` (`:3120-3150`), behind a three-way epsilon gate
  (`:3128-3137`) that exists because that function is "16*N powf calls … the heaviest control-grid
  item". So the naive route — *modulate `setDamping` from a `BrownianDrift`* — is wrong twice over:
  it moves **all N lines in lockstep** (a tremolo of the tail's colour, not a breathing space), and a
  control that never settles **permanently defeats the powf gate**, paying 16·N `powf` every 64
  samples for the privilege. FR-040 – FR-048 take the other route.

The result is one new Layer 4 class and one small, append-only, default-inert hole punched in
`AetherReverb` for the per-line damper offsets. Nothing else in the shipped header moves.

---

## Scope

In scope:

- One new Layer 4 component, `CavernVerb`, at `dsp/include/krate/dsp/effects/cavern_verb.h`
  (roadmap line 418), header-only, owning one `AetherReverb` by value and configuring it dark.
- The **cavern early-reflection stage** (roadmap lines 423–424): a fixed, sparse, incommensurate tap
  pattern with a long first arrival, per-tap stone absorption, scalable in size, feeding both the
  output and the FDN.
- The **moving dampers** (roadmap lines 421–422): one `BrownianDrift` per delay line inside
  `CavernVerb`, published to `AetherReverb` as a per-line octave offset on the damping cutoff.
- The **append-only `AetherReverb` extension** that makes those offsets reachable at all
  (FR-040 – FR-048): one setter, one accessor, two private arrays, one stated octave→coefficient law
  (FR-048) and one read-site swap. Default-inert.
- **Dark tuning** (roadmap lines 425–426) expressed as a deliberately restricted, dark-biased control
  surface: shimmer and bloom are not merely defaulted to zero, they are **not constructed**.
- **Freeze / infinite hold** (roadmap line 427) forwarded, with the damper offsets latched across it.
- Unit tests covering the roadmap's own Phase 9 success criteria (lines 429–430) — the inherited
  `AetherReverb` gates, the damper-motion smoothness test, and the ≤ 5 % global CPU budget — plus the
  cross-cutting gates (roadmap lines 538–562), each as a numbered criterion.

## Non-Goals (owned by later phases, or deliberately excluded)

- **Any Vorago voice, engine or macro wiring.** `VoragoVoice` / `VoragoEngine` and the concept macros
  (Darkness, Fog, Depth, …) are **Phase 10** (roadmap lines 434–461). This component includes no
  Vorago header and knows nothing about voices, notes or macros. It exposes scalars; Phase 10 maps
  macros onto them.
- **Global chain composition.** Roadmap line 449 puts the order at voice-sum → subharmonic → spectral
  smear → **cavern space** → output (`TapeSaturator` + `TruePeakLimiter`). Phase 10 owns that chain.
  `CavernVerb` does not include `processors/spectral_smear.h`, `systems/subharmonic_engine.h`,
  `processors/tape_saturator.h` or `processors/true_peak_limiter.h`.
- **A second FDN.** Explicitly forbidden by roadmap line 117. No delay-feedback topology of any kind
  is written in this phase: the only recirculating structure is `AetherReverb`'s, untouched.
- **Shimmer, in-loop harmonic bloom, and the bloom note API.** Roadmap line 426 rules shimmer out of
  Vorago's identity and puts bloom in the voice (Phase 7's shipped `BloomEngine`,
  `systems/bloom_engine.h`). `CavernVerb` prepares `AetherReverb` with `shimmerEnabled = false` and
  `bloomEnabled = false` and forwards neither `bloomNoteOn` (`aether_reverb.h:2392`) nor `bloomNoteOff`
  (`:2473`) nor the three send setters (`:2280`, `:2285`, `:2295`).
- **Changing any `AetherReverb` behaviour Seraphis can observe.** The extension is append-only and
  inert at its default; SC-012 is the gate, and roadmap lines 560–562 are the rule.
- **Re-deriving or duplicating `AetherReverb`'s internals.** Its matrix morph, Jot gains, freeze
  latch, amortized clear, non-finite sweep and STFT stage are consumed as shipped. This spec adds no
  requirement that restates one of that phase's FRs.
- **A visualization feed.** Phase 13 (roadmap lines 498–506).

---

## Architecture ruling — roadmap Open Question 3, **RESOLVED**

> *"Cavern space: configuration layer over shared `AetherReverb` vs separate L4 effect — Phase 9,
> after Seraphis Phase 6 exists."* (roadmap lines 569–570; the same choice is offered at line 418.)

**Ruling: a separate Layer 4 class, `CavernVerb`, that OWNS an `AetherReverb` by value — plus one
append-only extension to `AetherReverb` for the per-line damper offsets.** Neither option in the
roadmap's either/or is sufficient alone, and the reasons are structural, not stylistic:

| Option | Verdict | Reason (each fact verified in the header this session) |
|---|---|---|
| **Pure configuration layer** (a `makeCavernConfig()` helper + documented setter values, no new class) | **Rejected** | It cannot host the cavern ER: a reflection pattern is an audio stage with its own delay line and taps, and `AetherReverb` has no ER stage to configure (its pre-delay pair, `:4531-4532`, is one arrival). It also cannot host the moving dampers: `dampCoeff_` is `private` (`:4489`, below the `private:` at `:2724`) and `updateDecayAndDamping()` recomputes it from the *global* `setDamping`. A configuration layer has no seam. |
| **Separate L4 effect that re-derives the FDN** (the `AetherReverb`-from-`FDNReverb` pattern, its banner item (2), `:13-39`) | **Rejected** | Roadmap line 117: *"Avoid building two big FDNs."* Re-deriving would duplicate the orthogonal morph (`MatrixMorph`, `:2819`), the freeze latch (FR-033), the amortized clear (`kClearStageCount = 12`, `:3640`) and the non-finite recovery — ~4 700 lines of shipped, tested code — for a phase whose *entire* delta is dampers, ER and voicing. |
| **Separate L4 class owning an `AetherReverb`, + append-only damper hook** | **Adopted** | `tools/lint-layers.js` fails only when `layerIndex(to) > layerIndex(from)` (`tools/lint-layers.js:74`), so an `effects/` header may include an `effects/` header; the precedent is `effects/fdn_reverb.h:48` (`#include <krate/dsp/effects/reverb.h>`). Seraphis Phase 6's C-1 ("does NOT include fdn_reverb.h", `aether_reverb.h:14-16`) was **that spec's** constraint against inheriting a topology, not a repo rule, and it does not bind a consumer that deliberately reuses the whole engine. |

**Why the extension cannot be avoided, stated so it can be checked.** The roadmap asks for
"per-delay-line damping filters whose cutoffs wander slowly" (line 421–422). In the shipped engine
the per-line damping one-pole is applied at `aether_reverb.h:4282-4285`, its coefficient lives in the
private `dampCoeff_[kMaxChannels]` (`:4489`), and the only public lever is the **global** `setDamping`
(`:2244`). An owning wrapper can reach neither. The two alternatives to extending were both measured
against the header and rejected:

- *Modulate `setDamping` globally from one `BrownianDrift`.* Produces **correlated** motion on all N
  lines — a slow tremolo on the tail's colour, which is the opposite of "the space itself breathes"
  — and, because `updateDecayAndDamping()`'s gate is `|damping − lastJotDamping_| ≤ kJotRecomputeEpsilon`
  with `kJotRecomputeEpsilon = 1e-7f` (`:3128-3137`, `:2785`), a continuously-wandering value defeats
  the gate on **every** control chunk, paying the `16·N powf` the gate exists to avoid. SC-004
  (per-line decorrelation) is unreachable by this route by construction.
- *Put the `BrownianDrift` bank inside `AetherReverb`.* A larger shared-header change (N modulators,
  new seed salts, new depth/rate setters, new prepare/reset/seed paths) that installs **Vorago's
  identity in Seraphis's file**. Rejected on the roadmap's own principle (line 98: "Share the
  Seraphis substrate, diverge at the identity layer").

The adopted extension is therefore the **minimum** seam: `AetherReverb` learns to accept a per-line
damping-cutoff offset it does not generate, and `CavernVerb` owns the generator, the seeds, the
depth, the rate and the character. Precedent: Vorago Phase 6 made exactly one append-only change to a
shared header (`SubOscillator::advance()`, roadmap lines 310–312).

---

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 9 reuses / relies on |
|---|---|---|
| `AetherReverb` (L4) | `effects/aether_reverb.h:1377` | **The space core, consumed whole (roadmap line 117).** Lifecycle: nested `struct PrepareConfig` (`:1577`) with `numChannels` (8 or 16), `maxBlockSamples`, `maxDelaySeconds`, `bool shimmerEnabled`, `PitchMode shimmerMode`, `bool bloomEnabled`, `bool spectralDiffusionEnabled`, `std::size_t diffusionFftSize`, `std::uint32_t seed`; `void prepare(double sampleRate, const PrepareConfig& config) noexcept` (`:1614`, "THE ONLY NON-REAL-TIME-SAFE METHOD"); `void reset() noexcept` (`:1971`); `void silence() noexcept` (`:2145`); `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, std::size_t numSamples) noexcept` (`:2164`). Non-copyable, movable (`:1597-1606`). Controls used: `setSize` (`:2208`), `setDensity` (`:2211`), `setDecaySeconds` (`:2214`), `setFreeze(bool)` (`:2230`), `setDimensionality` (`:2239`), `setDamping` (`:2244`), `setPreDelayMs` (`:2247`), `setModDepth` (`:2254`), `setModSmoothness` (`:2268`), `setSpectralDiffusion` (`:2310`), `setSizeBreathDepth` (`:2320`), `setDimensionalityTideDepth` (`:2328`), `setWidth` (`:2333`), `setMix` (`:2336`), `setSeed(std::uint32_t)` (`:2361`). Controls deliberately **not** used: `setShimmerOctaveSend` (`:2280`), `setShimmerFifthSend` (`:2285`), `setBloomSend` (`:2295`), `setBloomDecay` (`:2301`), `bloomNoteOn` (`:2392`), `bloomNoteOff` (`:2473`). Introspection forwarded: `isPrepared` (`:2486`), `isFrozen` (`:2493`, "true only once the 50 ms latch has COMPLETED"), `getEffectiveDelayLengthSamples(std::size_t)` (`:2506`), `getModalDensityPerHz` (`:2511`), `getMaxSizeScale` (`:2523`), `getStateEnergy` (`:2559`), `getNonFiniteRecoveryCount` (`:2588`), `isRecovering` (`:2603`), `getLatencySamples` (`:2612` — `spectralEnabled_ ? diffusionFftSize_ : 0`). Public constants consumed: `kControlChunkSamples = 64` (`:1386`), `kMinSampleRate = 8000.0f` / `kMaxSampleRate = 192000.0f` (`:1392-1393`), `kSizeScaleMin = 0.25f` / `kSizeScaleMax = 4.0f` (`:1396-1397`), `kRefDelays8[8] = {967,1217,1543,1973,2477,3163,4001,5087}` (`:1564`), the seed salts `kMatrixSalt = 0 … kDriftSaltBase = 16` (`:1546-1552`). **Private, therefore NOT reachable and re-declared by `CavernVerb` where needed:** `kMaxChannels = 16` (`:2725`), `kDecayMinSeconds = 0.5f` / `kDecayMaxSeconds = 60.0f` (`:2735-2736`), `kDefaultDamping = 0.40f` (`:2740`), `kMaxPreDelayMs = 200.0f` (`:2743`), `kDampingNyquistRatio = 0.05f` (`:2788`), `dampCoeff_[kMaxChannels]` (`:4489`). |
| `AetherReverb` damping path (L4) | `aether_reverb.h:3120-3150`, `:4282-4285`, `:3610-3617` | **The extension point.** `updateDecayAndDamping()` computes `gDC = pow(10, -3m/(T60_dc·sr))`, `gNyq` at `T60_nyq = T60_dc · kDampingNyquistRatio^damping`, then `dampCoeff_[i] = clamp(2·ratio/(1+ratio), 0.001f, 1.0f)` (`:3148`); the loop applies `filterState_[i] = c·delRead[i] + (1−c)·filterState_[i]` (`:4283`). The header states the stability property FR-045 rests on: *"The one-pole y = c*x + (1-c)*y has DC gain exactly 1 and Nyquist gain c/(2-c)"* (`:3085-3086`) — non-expansive for every `c ∈ (0,1]`. `refreshControlState()` (`:3610`) runs `updateGeometry()` and `updateDecayAndDamping()` **only when `!freezeTarget_`** (`:3611-3614`), with the reason given at `:3600-3606`: under freeze "`effectiveDelay_`, `feedbackGain_` and `dampCoeff_` keep their latched values … recomputing either under freeze could only break SC-002". FR-046 inherits that branch exactly. |
| `BrownianDrift` (L2) | `processors/brownian_drift.h:94` | **The damper generator, consumed unchanged.** `class BrownianDrift : public ModulationSource`; `void prepare(double sampleRate) noexcept` (`:121`, no allocation — it only derives coefficients and configures an `OnePoleSmoother`), `void reset() noexcept` (`:133`), `void setSeed(std::uint32_t) noexcept` (`:145`), `void setSmoothness(float normalized) noexcept` (`:152`), `void setDepth(float) noexcept`, `void setMean(float) noexcept`, `void process() noexcept`, `void processBlock(size_t numSamples) noexcept` (`:194`), `[[nodiscard]] float getCurrentValue() const noexcept override` (`:212`, hard-clamped to `[-1,+1]`), `getSourceRange()` → `{-1,+1}` (`:217`). Load-bearing for FR-033/SC-003: output is slew-limited by an internal `OnePoleSmoother` at `kDriftOutputSmoothMs = 150.0f` (`:103`), the walk is hard-clamped to `±kWalkLimit = 4` (`:226`) and flushed below `kDenormalFloor = 1e-20f` (`:228`), and the decorrelation time is `tau = kTauMin + smoothness·(kTauMax − kTauMin)` with `kTauMin = 0.2f`, `kTauMax = 30.0f` (`:97-99`). Same class `AetherReverb` already uses for its own delay-length jitter (`BrownianDrift drift_[kMaxChannels/2]`, `:4640`). |
| `deriveStreamSeed` / `Xorshift32` (L0) | `core/random.h:102` / `:41` | **Consumed.** `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102`), lowbias32 finaliser with a non-zero substitution because `Xorshift32::seed()` silently replaces 0 with its default (`:99-101`, `:72-74`). Load-bearing for FR-034: `AetherReverb` already derives its own streams from the *same* function with `kDriftSaltBase + j` for `j < N/2` (`:2986`), so a damper bank seeded from the same base with a colliding salt would **render the identical trajectory as a delay-line jitter stream**. |
| `DelayLine` (L1) | `primitives/delay_line.h:57` | **The ER buffer.** `void prepare(double sampleRate, float maxDelaySeconds) noexcept` (`:86`, allocates), `void reset() noexcept` (`:92`), `void write(float) noexcept` (`:104`), `[[nodiscard]] float read(size_t delaySamples) const noexcept` (`:114`), `readLinear(float)` (`:124`), `readCubic(float)` (`:134`), `makeLinearTap(float)` / `readLinear(const LinearTap&)` (`:174`, `:177`), `maxDelaySamples()` (`:235`). The same primitive `AetherReverb` uses for its pre-delay pair and dry alignment (`:4531-4532`, `:4619-4620`). |
| `OnePoleSmoother` (L1) | `primitives/smoother.h` (used via `AetherReverb`, `:4636-4650`) | **Consumed** for `CavernVerb`'s own control smoothing (ER size, ER level, absorption, dry/wet), on the same 64-sample control grid. `advanceSamples(std::size_t)` is the block-rate advance idiom every shipped engine uses (`aether_reverb.h:3894-3907`). |
| `ClickDetector` (test helper) | `tests/test_helpers/artifact_detection.h:99`, config `:38` | **SC-003, SC-008.** The same calibrated detector Seraphis Phase 6's SC-015 used; `aether_reverb.h:4270-4281` records the measured failure mode (a C0 linear gate producing detections at 105.019 s) that forced a smoothstep gate — the precedent FR-023's ER-size smoothing must not repeat. |
| Normalised echo density (NED) | `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` | **SC-002.** Already implemented: 1 ms windows, RMS per window, fraction of windows whose RMS exceeds `peak · 0.01` (−40 dB), on the mono sum of the impulse response. Seraphis Phase 6's SC-003 (`specs/seraphis-phase6-aether-space/spec.md:1494-1530`) additionally derives the measurement window from the geometry — `t_start` = first occupied window, `W = max(250 ms, 3·m_long)` — because a fixed window is arithmetically unsatisfiable at `S = 4`. SC-002 inherits that definition verbatim. |
| `render_fingerprint.h` (test helper) | `tests/test_helpers/render_fingerprint.h:58,61,63,122` | **SC-011.** `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `struct RenderFingerprint`, `compareFingerprints` — the repo's **no bit-exact float goldens** rule (roadmap line 556, `dsp/CLAUDE.md` "Never pin a render with a bit-exact digest over float samples"). |
| `AllocationDetector` / `AllocationScope` | `tests/test_helpers/allocation_detector.h:48`, `:111` | **SC-010.** The RT-safety gate every Vorago phase has used. |
| Perf-test idiom | `dsp/tests/unit/effects/aether_reverb_perf_test.cpp:135-147`, `:322-354` | **SC-009, inherited verbatim.** Basis is **nanoseconds per 512-sample block at 48 kHz**, not % of core ("A percent-of-core figure is not reproducible across dev machines or CI runners", `:23-30`). `kBlockBudgetNs = (512/48000)·1e9 = 10 666 666.7`; `kRegressionFactor = 1.5` (`:138`); `kReferenceNs = kBlockBudgetNs · 0.05 = 533 333.3` (`:142`); `kMaxAdmissibleNs = kReferenceNs / kRegressionFactor = 355 555.6` (`:147`). Each checked-in baseline carries **both** `static_assert(baseline·kRegressionFactor <= kReferenceNs)` and `static_assert(baseline <= kMaxAdmissibleNs)` alongside the runtime `REQUIRE` (`:350-354`), so the absolute roadmap figure binds on every machine even though the `[.perf]` tag keeps the timing out of CI. The shipped `AetherReverb` baselines — the number Phase 9 must fit **underneath** — are `kBaselineCoreNsPerBlock = 69 593` (`:322`), `kBaselineDefaultNsPerBlock = 114 595` (`:326`), `kBaselineWorstNsPerBlock = 200 114` (`:330`), `kBaselineFrozenNsPerBlock = 98 443` (`:334`), `kBaselineMorphNsPerBlock = 118 464` (`:338`), `kBaselineClearChunkNs = 53 760` (`:343`). The **stop-and-surface rule** (`:81-82`: "NEVER raise a baseline, never relax the reference, never renegotiate `kRegressionFactor` at implementation time") is inherited by FR-082. |
| Layer / ODR / portability gates | `tools/lint-layers.js:74`, `tools/lint-odr.js:16-23`, `tools/check-portability.js`, `tools/lint-arch-guarded-includes.js`, `tools/lint-float-bit-goldens.js`, `tools/lint-nonfinite-symbols.js` | **FR-080, FR-084.** `lint-layers.js` permits same-layer includes (it flags only `layerIndex(to) > layerIndex(from)`); `lint-odr.js` qualifies nested types by their enclosing class, so nested `PrepareConfig`/`Tap` names cannot collide. |
| `dsp/lint_all_headers.cpp` | `:197` (`#include <krate/dsp/effects/aether_reverb.h>`) | **FR-084 registration.** The strict clang-tidy TU that includes every public header; a new header that is not listed here is never linted. |

---

## New components

| Class | Layer | Header | ODR sweep result (run this session) |
|---|---|---|---|
| `CavernVerb` | 4 | `dsp/include/krate/dsp/effects/cavern_verb.h` | **Clean.** `grep -rn "\(class\|struct\|enum class\|enum\) CavernVerb\b" dsp/ plugins/ tools/` → 0 matches; `grep -rni "cavern" dsp/ plugins/ --include=*.h --include=*.cpp` → **0 matches anywhere in the tree**. The file does not exist. |
| `CavernVerb::PrepareConfig` (nested struct) | 4 | same file | **Clean by construction** — `tools/lint-odr.js:20-21` qualifies nested types by their enclosing class. Matches the house pattern (`aether_reverb.h:1577`, `noise_organism.h:190`, `bloom_engine.h:338`). |
| `CavernVerb::EarlyTap` (private nested struct) | 4 | same file | **Clean.** `grep -rn "\(class\|struct\) EarlyTap\b" dsp/ plugins/ tools/` → 0 matches; nested and private besides. |

**Near-name hazards swept and cleared** (the roadmap's own hazard list at lines 127–129 plus every
name this component could plausibly have claimed). Each was run this session as
`grep -rn "\(class\|struct\|enum class\|enum\) <Name>\b" dsp/ plugins/ tools/` **and** as a
whole-word mention count over the same roots:

| Candidate name | Declarations | Any mention | Disposition |
|---|---|---|---|
| `CavernVerb` | 0 | 0 | **Adopted** (roadmap line 418 names the header `effects/cavern_verb.h`). |
| `CavernSpaceEngine`, `CavernSpace`, `CavernReverb`, `CavernConfig` | 0 | 0 | Not introduced. |
| `MovingDampers`, `MovingDamper`, `DamperBank` | 0 | 0 | **Not introduced** — the damper bank is a private array of the shipped `BrownianDrift`; no new type is needed and none is added. |
| `CavernEarlyReflections`, `EarlyReflections` | 0 | 0 | **Not introduced** — the ER stage is private state of `CavernVerb`, made testable through public accessors (FR-027) rather than through a second namespace-scope name. |
| `ResonatorBank`, `FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`, `PatternScheduler` (roadmap's list) | — | — | Not referenced, not shadowed, not included. |

The component therefore adds exactly **one** namespace-scope name to `Krate::DSP`, and introduces
**no** new class inside `AetherReverb`.

---

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001.** `CavernVerb` is a Layer 4, header-only class in namespace `Krate::DSP`, declared in
  `dsp/include/krate/dsp/effects/cavern_verb.h`, with a layer banner naming the layer, the spec slug
  and the roadmap lines (the house format, `aether_reverb.h:1-10`, `ecosystem_engine.h` banner).
  It includes only Layers 0–3 **plus** `effects/aether_reverb.h` (the adopted same-layer include,
  permitted by `tools/lint-layers.js:74`). It includes no Vorago Phase 1–8 header.
- **FR-002.** `CavernVerb` owns exactly one `AetherReverb` **by value**. It is non-copyable and
  movable, matching its member (`aether_reverb.h:1597-1606`: `STFT`, `OverlapAdd`, `DelayLine` and
  `PitchShiftProcessor` all delete their copy operations).
- **FR-003.** `void prepare(double sampleRate, const PrepareConfig& config) noexcept` is the **only**
  method that allocates. It is not an audio-thread operation. It may be called repeatedly. Every
  field of `PrepareConfig` is **clamped in place, never rejected** — the `AetherReverb::prepare`
  discipline (`:1611-1613`).
- **FR-004.** `PrepareConfig` is a nested struct with designated-initialiser-friendly defaults
  (no narrowing in brace init, the project's Clang rule) carrying at minimum: `maxBlockSamples`
  (clamped `[64, 8192]`, forwarded), `numChannels` (8 or 16, forwarded), `maxEarlySeconds`
  (clamped `[0.05, 0.60]`, sizes the ER delay lines, default `0.30` — FR-066), `spectralDiffusionEnabled`
  (default `true`, forwarded), `diffusionFftSize` (forwarded), `seed`.
- **FR-005.** `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
  float* outRight, std::size_t numSamples) noexcept` is the audio entry point. `numSamples == 0` is a
  no-op that advances no state. A null pointer in any of the four arguments returns without writing
  (the `AetherReverb::processStereoBlock` contract, `:2166-2170`). When not prepared it fills the
  outputs with silence (`:2174-2178`).
- **FR-006.** `void reset() noexcept` and `void silence() noexcept` forward to the owned engine
  (`:1971`, `:2145`) and additionally clear the ER delay lines, the alignment lines and the ER tap
  smoothers. Both are `noexcept` and allocation-free; neither is claimed to be an audio-thread
  operation (the `AetherReverb` wording, `:1966-1970`).
- **FR-007.** `CavernVerb` runs on the **same 64-sample control grid** as its member, anchored to an
  absolute sample counter, never to caller block boundaries — the property that makes render output
  invariant to how the host partitions blocks (`aether_reverb.h:2180-2184`). It must advance every
  owned modulator by a **full** `AetherReverb::kControlChunkSamples`, never by a slice length: the
  header records that `processBlock(36) + processBlock(28)` is **not** the same state as
  `processBlock(64)` for `BrownianDrift` (`:3877-3884`, `brownian_drift.h:194-206`).
  **The coupling mechanism, stated exactly:** `processStereoBlock` slices its `numSamples` into its
  own `kControlChunkSamples`-sample sub-blocks, anchored to an absolute sample counter started at
  `prepare()` (never reset by a caller's block boundary). At each sub-block boundary `CavernVerb`
  publishes the current damper offsets via `AetherReverb::setDamperOffsetsOctaves` and *then* calls
  the owned engine's `processStereoBlock` with that same sub-block — one owned-engine call per 64
  samples, accepted as the cost of keeping the two control grids phase-aligned by construction
  (P-2, SC-011). A trailing partial sub-block shorter than 64 samples (the remainder of the caller's
  `numSamples`) is passed to the owned engine as-is; it does not itself publish a new offset
  (offsets change only at full 64-sample boundaries).
- **FR-008.** `[[nodiscard]] bool isPrepared() const noexcept` and every forwarded accessor listed in
  the *Existing components* row for `AetherReverb` are exposed. `getStateEnergy()`,
  `getNonFiniteRecoveryCount()`, `isRecovering()`, `isFrozen()` and `getEffectiveDelayLengthSamples()`
  are forwarded **unmodified** — SC-001, SC-009 and SC-014 measure through them.
- **FR-009.** `[[nodiscard]] std::size_t getAllocatedBytes() const noexcept` reports the bytes owned
  by `CavernVerb`'s own buffers (ER lines + alignment lines), following the house accessor
  (`noise_organism.h:999`, `bloom_engine.h:817`).

### FR-010 series — Dark tuning of the reused core (roadmap lines 425–426)

- **FR-010.** `prepare()` configures the owned `AetherReverb` with `shimmerEnabled = false` and
  `bloomEnabled = false`. Consequence, verified: `shimmerAllocated_ = config.shimmerEnabled && (sr >= 44100)`
  (`:1631`), so no `PitchShiftProcessor` is prepared and `isShimmerActive()` (`:2498`) reports false.
  This is roadmap line 426 ("no shimmer-up taps … +12 shimmer is Seraphis's identity") implemented as
  *absence*, not as a zeroed send.
- **FR-011.** `CavernVerb` exposes **no** shimmer or bloom control and forwards **no** bloom note
  event. `setShimmerOctaveSend`, `setShimmerFifthSend`, `setBloomSend`, `setBloomDecay`,
  `bloomNoteOn` and `bloomNoteOff` are unreachable through its surface.
- **FR-012.** `void setSize(float v) noexcept`, `v ∈ [0,1]`, maps onto `AetherReverb::setSize` over a
  **restricted upper band** `[kCavernSizeFloor, 1.0]` with `kCavernSizeFloor` a named public constant
  **≥ 0.55** — roadmap line 425's "size range biased huge". The floor is **strictly above 0.5 and by a
  stated margin**, not at it: `AetherReverb::sizeScale(v) = min(0.25·2^(4v), maxSizeScale_)`
  (`:3011-3015`) gives exactly `S = 1.0` at a size control of 0.5, i.e. exactly `4.0 × kSizeScaleMin`,
  and SC-007 (c) asserts a ratio **greater** than 4; a floor of exactly 0.5 would put the criterion on
  the knife-edge of its own threshold. At `kCavernSizeFloor = 0.55` the reachable `S` at `v = 0` is
  `0.25·2^2.2 = 1.1487`, i.e. **4.59×** `kSizeScaleMin = 0.25f` (`:1396`), and at `v = 1` it is
  `S = 4.0`. The mapping is documented and asserted.
- **FR-013.** `void setDarkness(float v) noexcept`, `v ∈ [0,1]`, maps onto `AetherReverb::setDamping`
  over a restricted upper band `[kCavernDampingFloor, 1.0]` with `kCavernDampingFloor` a named public
  constant ≥ 0.5, and defaults to a value in the top half of that band — roadmap line 425's "HF decay
  strongly shortened". At damping 1 the shipped law gives `T60_nyq = T60_dc · 0.05`, i.e. 20× shorter
  at Nyquist (`:2787-2788`).
- **FR-014.** `void setDecaySeconds(float seconds) noexcept` forwards to `:2214`, clamped to
  `[0.5, 60.0]` — `CavernVerb` **re-declares** those bounds as its own named constants because
  `AetherReverb::kDecayMinSeconds`/`kDecayMaxSeconds` are private (`:2735-2736`); the values must
  match, and a static comment records the source line. The default is long (drone-scale), named as a
  public constant, and ≥ 10 s.
- **FR-015.** `void setDensity(float v) noexcept` forwards to `:2211` (the input diffuser). Its
  default is at or above the engine's own 0.70 — a stone chamber is diffuse, and SC-002's echo-density
  gate depends on it.
- **FR-016.** `void setDimensionality(float v) noexcept` forwards to `:2239` (the orthogonal matrix
  morph). It is exposed **because** the morph is the only motion freeze leaves alive
  (`:3607-3609`, RA-5), which a drone instrument holding a frozen cavern depends on.
- **FR-017.** `void setBreath(float v) noexcept`, `v ∈ [0,1]`, drives `setSizeBreathDepth` (`:2320`)
  and `setDimensionalityTideDepth` (`:2328`) together on a documented, monotone mapping — the
  roadmap's "the space itself breathes" (line 422) at the geometry level, complementing the dampers.
  Both are unsmoothed by contract; their targets are already slew-limited internally (`:4631-4635`).
- **FR-018.** `void setFog(float v) noexcept` forwards to `setSpectralDiffusion` (`:2310`). Named for
  what it does in Vorago (roadmap line 455 lists Fog among the concept macros) and documented as a
  per-bin **phase** smear, not damping (Overview fact 2).
- **FR-019.** `void setWidth(float)` and `void setMix(float)` are exposed. **The owned engine is run
  at `setMix(1.0)` permanently** — `CavernVerb` performs its own dry/wet mix so the ER can sit in the
  wet path. `setMix` on `CavernVerb` is `CavernVerb`'s own equal-power dry/wet, not a forward.
  `setPreDelayMs` is **not** exposed: the cavern's pre-delay is the ER geometry (FR-021), and the
  engine's own pre-delay is set to 0 so the two cannot double up. **`setWidth` forwards to the owned
  engine's `setWidth` and governs the late field only** — it has no effect on the ER bus, whose
  stereo image is fixed entirely by the tap side rule (FR-020), so `setWidth(0)` cannot collapse the
  ER geometry.

### FR-020 series — Cavern early reflections (roadmap lines 423–424)

- **FR-020.** `CavernVerb` implements a **sparse early-reflection stage** in front of the FDN, built
  as **one mono `DelayLine`** (`primitives/delay_line.h:57`), not a stereo pair: the input is
  summed to mono (`0.5·(inLeft + inRight)`) before being written, since every tap reads that same
  mono signal regardless of the side it is placed on (Clarifications, ER stereo topology) — the ER
  pattern is therefore invariant to input panning, and its width is purely geometric (the side
  rule below), never a function of `setWidth` (FR-019). In front of it sits a **fixed table** of
  exactly `kEarlyTapCount = 12` taps (fixed, not a range), each `{delay fraction, gain, side}`,
  exposed as public constants so tests name them instead of literals (the `kRefDelays8` precedent,
  `aether_reverb.h:1558-1568`).
  **The table is a generating law, not a hand-picked list:**
  - *Delays.* `d_i` (ms, at the default ER size) are drawn from a coprime integer series
    `{n_0 … n_11}`, linearly scaled so `d_0 = kEarlyFirstArrivalFloorMs = 60.0f` and
    `d_11 = kDefaultEarlySizeMs = 220.0f` (FR-021, FR-023, FR-066): `d_i = d_0 + (d_11 − d_0) ·
    (n_i − n_0) / (n_11 − n_0)`. The implementer chooses the concrete pairwise-coprime `{n_i}` and
    confirms FR-022's incommensurability inequality holds over the resulting `d_i` by
    `static_assert`.
  - *Gains.* `g_i = kEarlyGainG0 · exp(−kEarlyGainAlphaPerMs · d_i)`, with named public constants
    `kEarlyGainG0 = 0.5f` and `kEarlyGainAlphaPerMs = 0.01f` (per ms). **Every `g_i` is strictly
    positive — no negative-polarity taps** — and the implementer confirms
    `Σ|gain_i| = kEarlyGainSum ≤ 2.0` (FR-028) over the instantiated table by `static_assert`.
  - *Side.* A deterministic, alternating rule: tap `i` is placed `L` if `i` is even, `R` if `i` is
    odd (tap 0 is `L`). No randomisation, no seed dependency (FR-035).
- **FR-021.** The pattern is **long-pre-delay**: the first tap's delay at the default ER size is
  `kEarlyFirstArrivalFloorMs = 60.0f` ms (a named, fixed public constant — not merely bounded
  below), and the table is **strictly ascending**. This is the cavern's pre-delay;
  `AetherReverb::setPreDelayMs` is held at 0 (FR-019).
- **FR-022.** The tap delays are **pairwise incommensurate**, stated as a number rather than as the
  much weaker "not an integer multiple of another" (a table holding 60 ms and 90 ms — a 3 : 2 ratio —
  passes an integer-multiple test and still fuses into exactly the periodic flutter this requirement
  exists to prevent; `AetherReverb`'s own reference lines use **distinct primes, hence pairwise
  coprime**, for this reason, `aether_reverb.h:1555-1557`). **The binding property:** with `d_i` the
  tap delays **in milliseconds** at the default ER size, for every ordered pair `i ≠ j`

  ```
  min over p, q ∈ {1 … kIncommensurabilityOrder} of  |p·d_i − q·d_j| / min(d_i, d_j)
        ≥ kEarlyIncommensurabilityTol
  ```

  with `kIncommensurabilityOrder = 6` and `kEarlyIncommensurabilityTol = 0.05f` both named public
  constants. (Order 6, not 8, by the 2026-09-16 ruling: at order 8 the inequality is jointly
  infeasible with FR-028's gain-sum cap — best achievable 0.0452 over pairwise-coprime integer
  series; at order 6 the plan's verified table measures 0.05411, an 8.2 % margin. Tolerance, tap
  count, 60/220 ms, `g_0` and the 2.0 gain ceiling are unchanged.) Milliseconds, not samples, so the property is rate-invariant (the Edge Cases note on
  8 kHz quantisation). The property is asserted over the shipped table by a `static_assert` where the
  table is `constexpr`-reachable, and otherwise by a table-driven test that reports the **measured
  minimum** of the left-hand side as a figure; it is never merely asserted in prose. SC-006 (d) is the
  criterion.
- **FR-023.** `void setEarlySizeMs(float ms) noexcept` scales the whole table (`ms` is the **last**
  tap's delay), clamped to `[kEarlySizeMinMs, kEarlySizeMaxMs]` — `kEarlySizeMinMs = 80.0f`,
  `kEarlySizeMaxMs = 600.0f` (named, fixed public constants) — with the maximum additionally bounded
  by `PrepareConfig::maxEarlySeconds`. The default is `kDefaultEarlySizeMs = 220.0f` ms — the
  "default ER size" every other requirement and criterion measures at (FR-066). The value is
  smoothed on the control grid and the tap reads are
  **fractional-interpolated** (`DelayLine::readLinear`, `:124`), so a size change is click-free. The
  smoothing must be a shaped (C1) ramp, not a C0 linear one, at the gate endpoints — the measured
  failure recorded at `aether_reverb.h:4270-4281` is the precedent.
- **FR-024.** `void setEarlyLevel(float v) noexcept`, `v ∈ [0,1]`, is the ER contribution to the wet
  output, smoothed. At `v = 0` the ER contributes exactly `0.0f` to the output **by assignment, not
  by multiplication** (the `clearPending_` discipline at `aether_reverb.h:4198-4201`: a buffer that
  may hold a non-finite value must be replaced, because `NaN · 0` is `NaN`).
- **FR-025.** `void setEarlyAbsorption(float v) noexcept`, `v ∈ [0,1]`, applies a per-tap one-pole
  low-pass — stone absorption, and the mechanism that makes later taps duller than earlier ones.
  **The law, stated exactly:** `n = kEarlyTapCount` independent one-pole states, one per tap (not
  one filter shared across taps), with a per-tap cutoff **geometric in tap index**:
  `fc_i = fc_max · (fc_min / fc_max)^(v · i / (n − 1))`, named public constants
  `kEarlyAbsorptionFcMaxHz = 18000.0f` and `kEarlyAbsorptionFcMinHz = 1200.0f` — tap 0 stays near
  `fc_max` regardless of `v` and the last tap reaches `fc_min` at `v = 1`, which is the mechanism
  that makes later taps duller. **The Nyquist guard is part of the law** (2026-09-16 ruling): the
  endpoints actually used are `fc_max = min(kEarlyAbsorptionFcMaxHz, kEarlyAbsorptionNyquistFraction · sr)`
  and `fc_min = min(kEarlyAbsorptionFcMinHz, kEarlyAbsorptionSpanFraction · fc_max)`, with the named
  public constants `kEarlyAbsorptionNyquistFraction = 0.45f` and `kEarlyAbsorptionSpanFraction = 0.40f`,
  so every tap's cutoff is realisable at every legal sample rate (18 kHz is above Nyquist at 8–32 kHz).
  The guard is inactive at and above 40 kHz, which is where SC-006's rate-invariance arms (44.1, 48,
  96 kHz) run; the header, this requirement and `CavernVerb_EarlyAbsorption` state the same law. **At `v = 0` the filter is an exact bypass by assignment** — no
  coefficient is evaluated and no cutoff is computed, matching the FR-024/FR-044 discipline (a
  buffer path that might carry a stale non-finite value must be replaced, not attenuated to
  identity by a coefficient that happens to be 1). The filter is non-expansive for every admissible
  coefficient (same one-pole form as FR-045).
- **FR-026.** The ER stage **also feeds the FDN**: the signal `CavernVerb` hands to
  `AetherReverb::processStereoBlock` is the ER stage output (taps summed, at the send gain below),
  not the raw input — so the late field is excited by the reflections and inherits their timing.
  **Three points fixed exactly, so the excitation path is unambiguous:**
  (i) **post-absorption only, no direct path** — the sum is taken *after* FR-025's per-tap filter,
  and no raw or unabsorbed copy of the input reaches the owned engine by any other route; the
  reverb onset is therefore first-tap-arrival-plus-shortest-line, deliberately gapped (≈ 80 ms at
  defaults, roadmap's "enormous bunker" read literally);
  (ii) **taken after the ER size smoother's interpolated reads** — the send sums the *same*
  post-absorption tap reads the output bus sees (FR-023's `readLinear` results), never a
  pre-smoothing or otherwise separate read, so SC-013 render (ii) and SC-006 (a) measure one
  consistent signal;
  (iii) **mono** — the identical post-absorption tap sum is sent to *both* the owned engine's left
  and right inputs (consistent with the ER's mono-sum architecture, FR-020); the owned engine's own
  matrix and per-line drift are what decorrelate the two channels of the late field.
  The send gain is **a control**, `void setEarlySend(float v) noexcept`, `v ∈ [0,1]`, smoothed, with a
  named public default constant `kDefaultEarlySend` in the upper half of its range; it is the
  **seventeenth** control in FR-061's enumeration and it is *not* a free-floating named constant (the
  earlier "a named constant **or** a control" wording contradicted FR-061's closed surface and is
  struck). It is independent of `setEarlyLevel`, so the ER can be inaudible while still exciting the
  late field — **and, in the other direction, `setEarlySend(0.0f)` is a reachable product state in
  which the owned engine receives digital silence and the output carries the ER pattern alone.** That
  state is what SC-006 (a) and (c) render; without it the ER-isolation clauses would be measuring the
  ER on top of the diffuse field the ER itself excites, which is not executable.
- **FR-027.** The ER geometry is **readable** for tests without a second class:
  `[[nodiscard]] std::size_t getEarlyTapCount() const noexcept`,
  `[[nodiscard]] float getEarlyTapDelaySamples(std::size_t tap) const noexcept` (current, size-scaled)
  and `[[nodiscard]] float getEarlyTapGain(std::size_t tap) const noexcept`, which reports **the
  static table gain `g_i` only** (FR-020) — unaffected by `setEarlyAbsorption` or size scaling.
  Absorption's own state is exposed **separately**:
  `[[nodiscard]] float getEarlyTapAbsorptionCutoffHz(std::size_t tap) const noexcept`, reporting the
  cutoff FR-025's law currently applies to that tap (`fc_max` when bypassed at `v = 0`). Out-of-range
  indices return 0 rather than reading out of bounds (the `getEffectiveDelayLengthSamples` idiom,
  `:2506-2509`).
- **FR-028.** The ER stage contains **no feedback path**. It is feed-forward only, so its
  boundedness is structural: output magnitude ≤ `Σ|gain_i|` × input magnitude. `Σ|gain_i|` over the
  shipped table is itself a named public constant, `kEarlyGainSum`, **≤ 2.0**, asserted against the
  table by `static_assert`, so SC-014's peak bound is derived arithmetic rather than an observation.
  With FR-020's fixed gain law (`g_i = kEarlyGainG0 · exp(−kEarlyGainAlphaPerMs · d_i)`,
  `kEarlyGainG0 = 0.5f`, `kEarlyGainAlphaPerMs = 0.01f`) over 12 taps spanning `d_i ∈ [60, 220]` ms,
  every `g_i ∈ (0.055, 0.274)`, so the 12-term sum sits comfortably under the 2.0 ceiling with
  margin; the implementer's `static_assert` confirms the exact figure once the concrete `{n_i}`
  series (FR-020) is instantiated.
- **FR-029.** The ER stage is **not** silenced by freeze. `setFreeze(true)` holds the late field
  (FR-050); the dry path and the early reflections continue to follow the input, which is what makes
  a frozen cavern playable. Under freeze `AetherReverb`'s own injection is already ramped to zero
  (`:3118-3119`, "at `freezeRamp = 1` the gain is 1 and the injection is 0"), so FR-026's send needs
  no extra gating.

### FR-030 series — Moving dampers, generator side (roadmap lines 421–422)

- **FR-030.** `CavernVerb` owns **one `BrownianDrift` per prepared delay line** — up to 16, matching
  `AetherReverb`'s private `kMaxChannels = 16` (`:2725`), which `CavernVerb` re-declares as its own
  named constant with a comment citing that line. Exactly `numChannels` of them are prepared and
  advanced.
- **FR-031.** `void setDamperDepth(float v) noexcept`, `v ∈ [0,1]`, maps to a **per-line cutoff
  excursion in octaves**, `0 … kMaxDamperOctaves`, with `kMaxDamperOctaves = 1.5f` a named, fixed
  public constant. **The scale, stated exactly:** for line `i`,
  `offset_i = v · kMaxDamperOctaves · (drift_i.getCurrentValue() / kInternalStd)`, clamped to
  `±kMaxDamperOctaves` — where the factor `v` is realised **exactly once**, by
  `BrownianDrift::setDepth(v)`: `getCurrentValue()` already carries the depth
  (`brownian_drift.h:249-251`, `outputTarget() = clamp(depth_ · x_)`), so the implementation multiplies
  by `kMaxDamperOctaves / kInternalStd` only and never applies `v` a second time (2026-09-16
  confirmation) — variance-normalised (divided by `BrownianDrift::kInternalStd = 0.5`,
  `brownian_drift.h:101`) so that `kMaxDamperOctaves` is the *typical* peak excursion at `v = 1`, not
  a cap the walk reaches only on rare tail excursions, and zero-mean bipolar (a line wanders both
  darker and brighter, "breathes", rather than only darkening). `v` (the depth) is routed through
  `BrownianDrift::setDepth`, not applied as an external multiply on `getCurrentValue()`, so a depth
  change is itself ramped click-free by the same 150 ms output smoother FR-033 relies on (SC-003 (d)
  covers a depth step). At `v = 0` every published offset is exactly `0.0f` (FR-044's inertness
  precondition). **Default:** `kDefaultDamperDepth = 0.35f` (FR-066).
- **FR-032.** `void setDamperRate(float v) noexcept`, `v ∈ [0,1]`, forwards to
  `BrownianDrift::setSmoothness` on **every** damper via the fixed linear mapping
  `smoothness = 1 − v` — `v = 0` gives `smoothness = 1` (`τ = kTauMax = 30 s`, slowest) and `v = 1`
  gives `smoothness = 0` (`τ = kTauMin = 0.2 s`, fastest), consistent with `tau = kTauMin +
  smoothness·(kTauMax − kTauMin)` (`brownian_drift.h:97-99`). **Default:**
  `kDefaultDamperRate = 0.15f`, giving `smoothness = 0.85` and `τ ≈ 25.5 s` — inside the decided
  20–30 s "wanders **slowly**" band (roadmap line 422; FR-066).
- **FR-033.** The published offsets are **slew-limited by construction**: `BrownianDrift`'s output
  passes an internal `OnePoleSmoother` at `kDriftOutputSmoothMs = 150.0f`
  (`brownian_drift.h:103`, `:212-214`), so the per-control-chunk step is bounded without a second
  smoother. Because FR-031 routes `setDamperDepth` through `BrownianDrift::setDepth` rather than an
  external multiply, a depth change is slew-limited by the same smoother and is not a separate,
  unramped step. `CavernVerb` states the bound it relies on and SC-003 measures it. No additional
  smoothing stage is added unless SC-003 fails — and if it does, the fix is a smoother, never a
  relaxed criterion (FR-082).
- **FR-034.** **Damper seeds must not collide with the engine's own streams.** `AetherReverb`
  derives its delay-length jitter streams as `deriveStreamSeed(seed_, kDriftSaltBase + j)` with the
  **public** `kDriftSaltBase = 16` (`:1552`, `:2986`) for `j < numChannels/2`, i.e. salts 16–23. The
  owned engine is therefore seeded with `deriveStreamSeed(seed, kCavernReverbSalt)` and damper `i`
  with `deriveStreamSeed(seed, kCavernDamperSaltBase + i)`, where `kCavernDamperSaltBase` is a named
  public constant chosen clear of `{0,1,2,3,4}` and `[16, 24)`. A damper that silently rendered the
  same trajectory as a delay-line jitter lane would make SC-004 pass for the wrong reason.
- **FR-035.** `void setSeed(std::uint32_t seed) noexcept` re-seeds and resets **every** owned
  stochastic stream and forwards the derived seed to `AetherReverb::setSeed` (`:2361`). After
  `setSeed(s)` followed by `reset()`, a render is reproducible (SC-011). The ER stage uses **no**
  RNG — its tap table is fixed — so the seed has no effect on it, and that is stated in the doxygen.
- **FR-036.** Every damper advances **unconditionally** on the control grid, including while frozen
  and while the input is digital silence — the `AetherReverb` FR-074 rule ("THE LIFE MODULATORS
  ADVANCE UNCONDITIONALLY … the space engine breathes at idle", `:3873-3878`) and the cross-cutting
  Dormancy rule's "its source/generator and modulation lanes keep running" (roadmap lines 547–550).
  What freeze suspends is the **application** of the offsets (FR-046), not their generation.
- **FR-037.** `[[nodiscard]] float getDamperOffsetOctaves(std::size_t line) const noexcept` publishes
  the current per-line offset (post-depth, **post-clamp** — the value actually published to
  `AetherReverb::setDamperOffsetsOctaves`, by the 2026-09-16 ruling) for tests and for Phase 13's
  visualisation.
  Out-of-range indices return 0.

### FR-040 series — The `AetherReverb` extension (append-only, default-inert)

This is the **only** change to a shipped shared header in this phase. Roadmap lines 560–562 govern
it; SC-012 gates it.

- **FR-040.** `AetherReverb` gains exactly **one** new public method:
  `void setDamperOffsetsOctaves(const float* offsets, std::size_t count) noexcept`. It copies
  `min(count, numChannels_)` values into a new private array `damperOffset_[kMaxChannels]`, replacing
  any non-finite value with `0.0f` using the existing fast-math-immune `isFinite` helper
  (`:2937`), and clamping each to `±kMaxDamperOffsetOctaves` (a new **public** constant). A null
  pointer or `count == 0` clears the array to zero. It is real-time safe, allocates nothing, and is a
  plain member (no virtual, no new interface). `kMaxDamperOffsetOctaves` is the engine-side hostile-
  input ceiling and is **≥ `CavernVerb::kMaxDamperOctaves`** (FR-031), so `CavernVerb`'s full
  admissible excursion survives it unclipped and SC-005's depth ordering is not silently flattened at
  the top by a clamp belonging to the other class. FR-084's `static_assert` covers the relation.
- **FR-041.** `AetherReverb` gains exactly **one** new public accessor,
  `[[nodiscard]] float getEffectiveDampingCoefficient(std::size_t channel) const noexcept`, returning
  the coefficient the loop is currently applying to that line. Out-of-range returns 0. It exists so
  SC-003, SC-004 and SC-005 can measure the dampers without a test hook or a friend declaration.
- **FR-042.** No other public member of `AetherReverb` changes signature, semantics, default value or
  order. No existing constant changes value. No method is removed or renamed. The change is
  **append-only** in the sense Phase 6 used for `SubOscillator::advance()` (roadmap lines 310–312).
- **FR-043.** The offsets are applied **after** `updateDecayAndDamping()`'s epsilon-gated
  recomputation, never inside it: the gate at `:3128-3137` and its `lastJotScale_`/`lastJotDecay_`/
  `lastJotDamping_` sentinels are untouched, so a continuously-moving offset costs **zero** additional
  `powf`. The application writes a **new** array, `effectiveDampCoeff_[kMaxChannels]`, and
  `renderSlice` step 3 (`:4282-4283`) reads that array instead of `dampCoeff_`. **What the application
  computes is FR-048's law**; this requirement fixes only *where* it happens.
- **FR-044.** **Inertness is exact, not approximate.** When every entry of `damperOffset_` is `0.0f`,
  `effectiveDampCoeff_[i]` is produced by **assignment** from `dampCoeff_[i]` — not by multiplying by
  a unity factor or exponentiating by zero — so the value the loop applies is the identical `float`
  object value the shipped engine applies today. This is what makes SC-012's bit-identity claim
  checkable rather than hopeful.
- **FR-045.** **Boundedness is preserved for every admissible offset.** The applied coefficient is
  clamped to the same `[0.001f, 1.0f]` range the shipped code uses (`:3148`). The header already
  records the property this rests on — the one-pole `y = c·x + (1−c)·y` has DC gain exactly 1 and
  Nyquist gain `c/(2−c)` — so it is non-expansive for every `c ∈ (0,1]`, and `AetherReverb`'s FR-032
  ("loop gain ≤ 1.0 at all times outside freeze") survives **any** offset vector, including a hostile
  one. No new stability argument is introduced; the existing one is shown to still apply.
- **FR-046.** **Under freeze the offsets are latched**, exactly as `dampCoeff_` already is:
  `effectiveDampCoeff_` is refreshed only on the `!freezeTarget_` branch of `refreshControlState()`
  (`:3611-3614`), for the reason the header gives at `:3600-3606` — recomputing the per-line loop
  coefficients under freeze "could only break SC-002". SC-001 is the gate.
- **FR-047.** The offsets are **not** persisted, serialised, or exposed through any Seraphis-facing
  API, and `AetherReverb` never generates them. `prepare()` and `reset()` clear `damperOffset_[i]` to
  zero **and initialise `effectiveDampCoeff_[i]` from `dampCoeff_[i]` by FR-044's plain assignment,
  for every `i < kMaxChannels`** — so an engine that is prepared and never told otherwise behaves
  exactly as it does today. This second half is load-bearing and not decorative: FR-046 refreshes
  `effectiveDampCoeff_` only on the `!freezeTarget_` branch of `refreshControlState()`
  (`:3611-3614`), so an engine that is prepared (or reset) and frozen **before** its first thawed
  control chunk would otherwise run the `:4283` one-pole on an array that was never written — an
  undefined loop coefficient, and a silent hole in SC-012's bit-identity claim.

- **FR-048.** **The octave → coefficient law, stated exactly** (the behavioural content of D-7; FR-043
  fixes only where it is evaluated). Two implementers handed "publishes offsets in octaves" would
  otherwise recover a cutoff from `c` differently, and could do it in opposite directions. For every
  line `i` with `damperOffset_[i] != 0.0f`, at the sample rate `sr`:

  1. **Recover the one-pole's cutoff.** `c_i = clamp(dampCoeff_[i], 0.001f, 1.0f − 1e-6f)`, then
     `fc_i = −(sr / 2π) · ln(1 − c_i)` — the inverse of the standard one-pole relation
     `c = 1 − exp(−2π·fc/sr)`, which is the form the loop at `:4283` applies. The upper clamp keeps the
     logarithm finite at `c = 1` (the "no damping" endpoint the shipped clamp at `:3148` can reach).
  2. **Scale it logarithmically.** `fc'_i = fc_i · exp2(−damperOffset_[i])`. **Sign convention, fixed
     here and nowhere else: a POSITIVE offset LOWERS the cutoff** — darker line, shorter HF decay,
     smaller `c`. A negative offset raises it. SC-005 asserts this direction, not merely monotonicity.
  3. **Re-derive the coefficient.** `c'_i = 1 − exp(−2π·fc'_i / sr)`.
  4. **Clamp.** `effectiveDampCoeff_[i] = clamp(c'_i, 0.001f, 1.0f)` — the same range as `:3148`
     (FR-045), so the one-pole stays non-expansive for every offset vector.

  At `damperOffset_[i] == 0.0f` the law is **not** evaluated at all: FR-044's plain assignment applies,
  because steps 1–3 are an identity only in exact arithmetic and would perturb the last float bits.
  **Two properties this law is chosen for, both used downstream:** (a) the same `depth` in octaves is
  the same perceived amount of movement at every `setDarkness`, which is D-7's reason; and (b) the map
  is globally Lipschitz in the offset with a **darkness-independent** constant —
  `|dc/doffset| = ln2 · u · e^(−u)` with `u = 2π·fc'/sr`, and `max(u·e^(−u)) = e^(−1)`, so
  `|Δc| ≤ ln2 · e^(−1) · |Δoffset| = 0.25499 · |Δoffset|`. SC-003 (b) uses exactly that constant to
  turn clause (a)'s octaves-per-chunk bound into a coefficient-per-chunk bound. Cost: two
  transcendentals per line per control chunk, and only on lines whose offset is non-zero — no `powf`,
  and the `:3128-3137` gate is untouched (FR-043).

### FR-050 series — Freeze / infinite hold (roadmap line 427)

- **FR-050.** `void setFreeze(bool on) noexcept` forwards to `:2230`. `[[nodiscard]] bool isFrozen()
  const noexcept` forwards to `:2493` and therefore reports true **only once the 50 ms latch has
  completed** — the distinction the shipped accessor documents, and the one SC-001 samples against.
- **FR-051.** Entering and leaving freeze is click-free at every admissible damper depth and rate.
  The engine's own 50 ms latch (`kFreezeLatchMs`, `:1388`) does the work; FR-046 keeps the dampers
  from moving the loop coefficients underneath it.
- **FR-052.** While frozen, `getStateEnergy()` (`:2559`) is the conserved quantity SC-001 measures.
  `CavernVerb` forwards it unmodified and adds nothing to the recirculating path, so the shipped
  freeze proof carries over unchanged.

### FR-060 series — Control surface, smoothing, latency, output

- **FR-060.** Every setter follows the house contract: `clamp(isFinite(x) ? x : default, lo, hi)`,
  then a smoother target or a raw member, `noexcept`, real-time safe, accepted before `prepare()`
  and re-applied by it (`aether_reverb.h:2202-2204`, `:2950`).
- **FR-061.** The complete public control surface is exactly: `setSize`, `setDarkness`,
  `setDecaySeconds`, `setDensity`, `setDimensionality`, `setBreath`, `setFog`, `setEarlySizeMs`,
  `setEarlyLevel`, `setEarlyAbsorption`, **`setEarlySend`** (FR-026), `setDamperDepth`,
  `setDamperRate`, `setFreeze`, `setWidth`, `setMix`, `setSeed`. **Seventeen** controls; no others.
  Each maps to a roadmap statement in the Traceability table. A control that is not in that table is
  out of scope. `setEarlySend` is the seventeenth deliberately and not by drift: FR-026's alternative
  wording ("a named constant *or* a control") admitted two different public APIs, and with the send
  fixed as a constant SC-006's ER-isolation clauses have no reachable state to render in.
- **FR-062.** `[[nodiscard]] std::size_t getLatencySamples() const noexcept` reports
  `CavernVerb`'s **total** algorithmic latency. When the owned engine's spectral stage is enabled it
  is `diffusionFftSize` (`:2612`); otherwise 0. The dry path and the ER output are delayed by that
  same amount through `CavernVerb`'s own alignment lines, so the three paths are time-aligned at the
  mix. **The dry and ER busses cannot share one alignment pair**: `setMix` scales the dry bus while
  `setEarlyLevel` scales the ER bus, and the ER sits in the wet path (FR-019, FR-024, D-2), so the two
  carry different gains and the alignment is therefore **four mono lines — dry L/R and ER L/R**, each
  of `getLatencySamples()` length. Both gains are applied **after** the alignment. FR-075 accounts for
  all four. SC-013 measures reported against actual, in two renders.
- **FR-063.** The dry/wet mix is equal-power and smoothed, applied by `CavernVerb` with the owned
  engine at `setMix(1.0)` (FR-019). At `mix = 0` the output is the (aligned) dry signal and the wet
  path contributes exactly `0.0f` by assignment (FR-024's rule).
- **FR-064.** Output is finite for every admissible input and parameter combination. Non-finite
  **input** is replaced with 0 at the boundary and does **not** increment any recovery counter — the
  shipped discipline at `:4163-4172` ("A replacement, never a counter increment"). Non-finite
  **state** inside the owned engine is handled by its existing sweep and reported through the
  forwarded `getNonFiniteRecoveryCount()` (`:2588`).
- **FR-065. Dormancy, ruled per zero-gain slot** (the cross-cutting rule, roadmap lines 547–555:
  *"gain at zero means the component's processing chain is skipped; its source/generator and
  modulation lanes keep running; re-entry is a 50 ms per-sample linear fade"*, plus the obligation
  that *"a spec that wants a silent slot to keep burning its chain must say what the listener would
  hear that justifies it"*). `CavernVerb` has three zero-gain slots, and they are ruled differently
  because the audible consequence differs:
  - **`setEarlyLevel == 0` *and* `setEarlySend == 0` → the ER tap loop is skipped.** Both gains must
    be zero: at `earlyLevel == 0` alone the taps are still the only excitation the FDN receives
    (FR-026), so skipping them would silence the late field. With both at zero nothing downstream can
    observe the taps, the input is still written into the ER lines (so the pattern is charged when a
    gain returns), and only the per-tap read/absorb/sum loop is skipped.
  - **`setMix == 0` → the wet path (owned engine + ER) keeps running.** This is the case the roadmap
    requires a justification for, and the justification is the phase's own subject matter: the FDN is
    a 60-second-decay recirculating state that a drone player holds, freezes and returns to. A `mix`
    automated to 0 and back is a *duck*, not a *stop* — skipping the chain would drain the state, and
    what the listener would hear on the way back is a cavern rebuilding from silence over tens of
    seconds instead of the tail they left, plus a freeze (FR-050) that latches nothing. The stated
    exception's own precedent is the reverse case: Phase 5 FR-063 cleared a feedback loop at the
    sleep edge *because* it has no generator behind it and would replay a stale burst; here there is
    a generator (the input, the dampers, the matrix morph) and the state is the instrument.
  - **`setDamperDepth == 0` → generation continues, application is inert.** FR-036 keeps every
    `BrownianDrift` advancing (clause (ii) of the rule); FR-031/FR-044 make the published offsets
    exactly `0.0f` and the coefficient a plain assignment, so there is no chain left to skip.

  **Re-entry (clause (iii)) applies to both gated slots:** a transition of `earlyLevel`, `earlySend`
  or `mix` away from exactly zero ramps the restored contribution in over **≥ 50 ms**, per sample, on
  the shaped (C1) ramp FR-023 requires — never a block-boundary step and never a C0 corner. SC-003 (d)
  is the criterion.

- **FR-066. Pinned default values.** Every one of FR-061's seventeen controls except `setSeed` and
  `setFreeze` (whose "default" is simply off, at `AetherReverb`'s own construction state), plus
  `PrepareConfig::maxEarlySeconds`, has a numeric default fixed **here**, so no "at defaults"
  criterion is self-contradictory across two implementations:

  | Control | Default | Basis |
  |---|---|---|
  | `setSize` | `0.50` | mid of `CavernVerb`'s own `[0,1]` domain; via FR-012's mapping this reaches `S ≈ 2.14`, inside the "biased huge" upper band |
  | `setDarkness` | `0.80` | top half of `[kCavernDampingFloor, 1.0] = [0.5, 1.0]` (FR-013) |
  | `setDecaySeconds` | `20.0` s | drone-scale, ≥ 10 s (FR-014) |
  | `setDensity` | `0.75` | at/above the engine's own `0.70` default (FR-015) |
  | `setDimensionality` | `0.50` | mid-range; exposed per FR-016, no stated floor |
  | `setBreath` | `0.50` | moderate size/dimensionality tide (FR-017) |
  | `setFog` | `0.30` | modest phase smear; `spectralDiffusionEnabled = true` (FR-018, FR-004) |
  | `setEarlySizeMs` | `220.0` ms | `kDefaultEarlySizeMs` (FR-023); the "default ER size" |
  | `setEarlyLevel` | `0.80` | ER audibly present (FR-024) |
  | `setEarlyAbsorption` | `0.60` | later taps noticeably duller (FR-025) |
  | `setEarlySend` | `0.70` | `kDefaultEarlySend`, upper half of its range (FR-026) |
  | `setDamperDepth` | `0.35` | `kDefaultDamperDepth` (FR-031) |
  | `setDamperRate` | `0.15` | `kDefaultDamperRate`, `τ ≈ 25.5 s` (FR-032) |
  | `setFreeze` | `false` | off |
  | `setWidth` | `1.00` | full width late field |
  | `setMix` | `1.00` | fully wet |
  | `PrepareConfig::maxEarlySeconds` | `0.30` s | matches SC-009 arms (a), (c), (d) (FR-004) |

  **SC-007 (a) and (b) were re-derived against this table, not assumed to hold by coincidence,**
  before this table was pinned: `setDarkness = 0.80` sits in the top half of the damping band as
  FR-013 already required, so the 8 kHz/250 Hz T60 ratio it produces is materially darker than the
  midpoint the earlier qualitative text would have allowed, and `setSize = 0.50` plus
  `setDamperDepth = 0.35` do not counteract that darkness (breath and dampers move the spectrum, they
  do not brighten the base cutoff). The **thresholds themselves are unchanged** (0.35× and −30 %) —
  re-deriving against the pinned table confirmed they remain reachable and non-trivial rather than
  requiring a change; FR-082's stop-and-surface rule governs if an implementation measurement ever
  disagrees.

### FR-070 series — Real-time safety, portability, budget

- **FR-070.** No allocation, lock, exception or I/O on the audio path. All buffers are sized in
  `prepare()`. SC-010 is the gate.
- **FR-071.** No `std::isnan`, `std::isinf` or `std::isfinite` anywhere in the header — the macOS leg
  builds with `-ffast-math`. Finiteness is tested through the repo's bit-pattern helpers
  (`core/db_utils.h` `detail::isNaN`/`isInf`, or `AetherReverb`'s own `isFinite`, `:2937`);
  `tools/lint-nonfinite-symbols.js` enforces it.
- **FR-072.** No narrowing in brace initialisation; designated initialisers for aggregate configs; no
  arch-guarded krate include (`tools/lint-arch-guarded-includes.js`); any SIMD uses unaligned
  load/store unless alignment is proven. `node tools/check-portability.js` must pass.
- **FR-073.** Denormal hygiene: the ER lines and the damper offsets flush sub-`1e-20f` magnitudes to
  zero, matching `BrownianDrift`'s own `kDenormalFloor` (`brownian_drift.h:228`).
- **FR-074.** **Sample rates.** `prepare()` accepts `[8 kHz, 192 kHz]`, clamped as the owned engine
  clamps (`:1615-1616`). ER tap delays are specified in **milliseconds** and re-derived per sample
  rate, so the pattern is rate-invariant. There is no 44.1 kHz floor, because shimmer — the only
  stage that had one (`kShimmerMinSampleRate = 44100.0`, `:1394`; `:1631`) — is not constructed
  (FR-010).
- **FR-075.** Memory is reported by `getAllocatedBytes()` (FR-009) and is dominated by the ER line
  pair (`maxEarlySeconds · sr · 2` mono samples) and the **four** alignment lines FR-062 requires —
  dry L/R and ER L/R, i.e. `diffusionFftSize · 4` mono samples, not `· 2`; the dry and ER busses
  carry different gains and cannot share one aligned pair. At the
  defaults this must be a small fraction of the owned engine's own footprint (its banner records
  `delayBuffer_` at 432 KiB and the pre-delay pair at 128 KiB, `:480`).

### FR-080 series — Budget, registration, and the stop-and-surface rule

- **FR-080.** **Registration checklist**, all of it, in the same change: the new header is added to
  `dsp/lint_all_headers.cpp` in the Layer 4 block (beside `:197`); the new test TUs are added to the
  **enumerated** `dsp_effects_tests` list (`dsp/tests/CMakeLists.txt:504-533`); `node tools/lint-odr.js`,
  `node tools/lint-layers.js`, `node tools/check-portability.js`,
  `node tools/lint-arch-guarded-includes.js`, `node tools/lint-float-bit-goldens.js` and
  `node tools/lint-nonfinite-symbols.js` all pass; `./tools/run-clang-tidy.ps1 -Target dsp` is clean.
  **Additionally, the NED metric is lifted, not copied.** SC-002's metric currently lives as inline
  body code inside a Catch2 `TEST_CASE` (`dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` — local
  `windowSize`/`numWindows`/`amplitude` vectors ending in `REQUIRE(ned >= 0.8)` at `:373`), so it is
  not callable and "exactly as implemented" would mandate a copy-paste that diverges the moment
  SC-002's geometry-derived windowing is applied to it. This phase moves it into
  `tests/test_helpers/` as a named function —
  `normalisedEchoDensity(std::span<const float> monoIr, std::size_t windowSamples,
  std::size_t startWindow, std::size_t windowCount)` — and **both** `fdn_reverb_test.cpp` and the new
  `CavernVerb` TU call it. `fdn_reverb_test.cpp` is not an `aether_reverb_*` or `seraphis_*` TU, so
  this does not touch anything SC-012 (b) freezes, and its existing `REQUIRE(ned >= 0.8)` must hold
  unchanged through the lift.
- **FR-081.** **CPU budget, as a functional requirement** (roadmap line 430: "CPU ≤ 5% global"; roadmap
  line 546: "CPU budgets are FRs, measured in tests"). The basis is ns per 512-sample block at 48 kHz;
  the reference is `kReferenceNs = 533 333.3 ns`; the admissible baseline ceiling is
  `kMaxAdmissibleNs = 355 555.6 ns` (`aether_reverb_perf_test.cpp:135-147`). This is a **global**
  figure — one `CavernVerb` instance for the whole engine — and it does **not** multiply by
  polyphony, matching the shipped `AetherReverb` budget's own note (`:49-53`).
- **FR-082.** **Stop and surface.** No implementing agent may raise a baseline, relax a threshold,
  shrink a workload, lower a tap count or reduce a capacity to make a figure fit. Reduce cost, or
  stop and surface the measurement to the user for an explicit decision (the rule at
  `aether_reverb_perf_test.cpp:81-82`, and the precedent set by Phase 2's 1 % → 1.75 %, Phase 5's
  1 % → 1.5 % and Phase 6's rulings — each an explicit user decision recorded in the spec, never an
  implementer's convenience).
- **FR-083.** **Seraphis stays green.** Roadmap lines 560–562. Every `AetherReverb` test TU
  (`aether_reverb_test.cpp`, `_matrix_`, `_spectral_`, `_perf_`, `_nonfinite_`,
  `dsp/tests/CMakeLists.txt:528-532`) and **every TU matching
  `dsp/tests/unit/systems/seraphis_*.cpp`** — **seven** at the time of writing, listed by `ls` this
  session: `seraphis_engine_test.cpp`, `seraphis_macro_test.cpp`, `seraphis_nonfinite_test.cpp`,
  `seraphis_param_broadcast_test.cpp`, `seraphis_partial_fanout_test.cpp`, `seraphis_perf_test.cpp`,
  `seraphis_voice_test.cpp` — plus `plugins/seraphis`, must pass unchanged, with **no test edited to
  accommodate the extension**. The enumeration is by glob and not by a hand-kept list of four: the
  three that the earlier list omitted include direct consumers of the extended header
  (`seraphis_param_broadcast_test.cpp` references `AetherReverb` five times,
  `seraphis_voice_test.cpp` once), so the omission dropped exactly the suites most exposed to it. The
  gate is stated as **the whole `dsp_systems_tests` and `dsp_effects_tests` executables passing**, so
  a TU added to either target after this spec is written is covered without a spec edit. SC-012 is
  the gate.
- **FR-084.** The header carries a `static_assert` that `CavernVerb`'s re-declared constants agree
  with the reachable public facts they mirror, so a future change to the shared header breaks the
  build rather than drifting silently. **The gate is scoped to what is actually reachable**, because
  the constants most at risk of divergence are `private` and therefore cannot appear in a
  `static_assert` from `CavernVerb` at all: `kDecayMinSeconds`/`kDecayMaxSeconds` (`:2735-2736`,
  mirrored by FR-014), `kMaxChannels = 16` (`:2725`, mirrored by FR-030) and `kDampingNyquistRatio =
  0.05f` (`:2788`, relied on by the Overview and FR-013). The compile-time gate therefore covers the
  **public** facts only — `kControlChunkSamples` (`:1386`), `kSizeScaleMin`/`kSizeScaleMax`
  (`:1396-1397`), `kMinSampleRate`/`kMaxSampleRate` (`:1392-1393`), `kDriftSaltBase` (`:1552`) and
  `kRefDelays8` (`:1564`) — and the three private mirrors are pinned **at runtime, through public
  behaviour**, by SC-016. Claiming compile-time protection for a private constant would be a
  requirement that cannot deliver what it promises.

---

## Success Criteria

Shared preconditions, cited by number so no criterion defines its own input:

- **P-1.** 48 kHz, `numChannels = 8`, `maxBlockSamples = 512`, default `PrepareConfig`, seed 1.
- **P-2.** Renders are produced through `processStereoBlock` in blocks whose lengths are
  **deliberately not multiples of 64** (e.g. 37, 111, 513), to exercise FR-007's absolute control
  grid. 512 is *not* such a block — it is exactly `8 · 64` and is control-grid aligned, so it
  exercises none of FR-007's partitioning behaviour and is not used for this purpose (it remains the
  `maxBlockSamples` of P-1 and the perf basis of SC-009, which are different roles).
- **P-3.** Inputs: **G-1** a unit impulse (wet-only measurement, `mix = 1`); **G-2** a **continuously
  generated, seeded** band-limited noise **stream**, 80 Hz – 11 kHz, of whatever length the criterion
  asks for — it is a generator, **never a 2 s buffer that is looped**, because a looped buffer would
  put a 0.5 Hz periodicity into SC-005's centroid statistic and a seam into SC-003's click record;
  **G-3** digital silence; **G-4** a 220 Hz sine. Every criterion that uses G-2 states its own length.
- **P-5.** Wherever a criterion asserts "zero `ClickDetector` detections", the configuration is the
  one Seraphis Phase 6's SC-015 pinned (`specs/seraphis-phase6-aether-space/spec.md:2027-2030`),
  written in designated-initialiser form and **with the sample rate corrected to P-1's 48 kHz** (the
  struct default is 44 100, `tests/test_helpers/artifact_detection.h:38-48`, which would silently
  mis-scale every reported detection time):
  `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256,
  .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`. Seraphis SC-015's
  calibration discipline is inherited whole: if a no-transition reference render of the same length
  reports a non-zero false-positive count, `detectionThreshold` may be raised to the smallest value
  giving zero on **that** render, **capped at 8.0**, and the calibrated config must still report ≥ 1
  detection on a control render carrying a single-sample step of amplitude 0.1. The threshold, the
  false-positive floor and the control count are recorded. The 0-detection requirement is never
  relaxed.
- **P-4.** Timing criteria (SC-009) run alone, nothing else executing, per
  `node tools/run-cpu-tests.js` and the project's CPU-test isolation rule.

---

- **SC-001 — Freeze conserves energy: ±0.5 dB over 60 s, with the dampers live** (roadmap line 429,
  inheriting Seraphis Phase 6 SC-002, `specs/seraphis-phase6-aether-space/spec.md:1434-1493`).
  *Input:* 2 s of G-2, then `setFreeze(true)`, then 60 s of G-3.
  *Clause 1 — the conserved quantity, with the matrix morphing.*
  *Configuration:* `setDamperDepth(1.0)`, `setDamperRate(1.0)` (fastest), `setEarlyLevel(1.0)`,
  `setEarlySend(1.0)`, `setSize(1.0)`, `setDarkness(1.0)`, **`setBreath(1.0)` and
  `setDimensionality(1.0)`** — i.e. every Phase-9 addition at maximum, set **before** the freeze.
  The last two are not decoration and are the correction of a real weakening: FR-017 drives
  `setDimensionalityTideDepth` from `setBreath`, and the inherited criterion's clause 1 *mandates*
  `dimensionalityTideDepth = 1` "so the matrix is *morphing* throughout", on the stated ground that
  "the configuration C-3 shows a naive lerp cannot survive"
  (`specs/seraphis-phase6-aether-space/spec.md:1445-1447`); its always-on core is `size = 1.0`,
  `dimensionality = 1`, `N = 8`. Leaving breath and dimensionality at their defaults would freeze a
  near-static matrix — precisely the easy configuration the source criterion rejects, and the one
  motion freeze leaves alive by this spec's own FR-016 (`aether_reverb.h:3604-3609`).
  *Metric:* forwarded `getStateEnergy()`, sampled once per second, in dB relative to the
  first sample after `isFrozen()` first reports true. *Threshold:* every sample within **±0.5 dB**
  over the full 60 s. *Why it is the sharp one:* it fails if FR-046's latch is omitted — a damper
  that keeps moving the per-line loop coefficient under freeze changes the loop's decay while the
  loop is supposed to be lossless.
  *Clause 2 — per octave* (inherited from the source criterion's clause 3,
  `specs/seraphis-phase6-aether-space/spec.md:1470-1481`, and the clause that catches a latched-but-
  wrong per-line coefficient draining a single band while the total is conserved).
  *Configuration:* clause 1's, **but with `setBreath(0.0)`** so the tide is at 0 and the matrix is a
  fixed orthogonal map — a morphing mixer moves energy *across* frequency while conserving the total,
  so a per-band bound does not follow from losslessness while it morphs. *Metric:* the same ±0.5 dB
  window bound applied independently to octave bands centred at 125, 250, 500, 1 k, 2 k, 4 k and
  8 kHz, with a **−80 dBFS noise-floor gate** (a band whose reference-window level is below it is
  skipped) and an assertion that **at least 6 of the 7 bands qualified**. The qualifying count is
  recorded.
  *Clauses deliberately not re-run, stated as an exclusion rather than left to omission.* The source
  criterion's clause 2 (output-tap level, ±1.0 dB) and clause 4 (the disable paths live before
  freeze) are **not** inherited. Clause 2 measures `AetherReverb`'s own fixed rank-2 output tap, which
  this phase does not touch — `CavernVerb` consumes `processStereoBlock`'s output whole (FR-005) and
  adds only a post-tap dry/wet mix (FR-063) that is outside the recirculating path. Clause 4 measures
  the shimmer and bloom sends inside the freeze loop; FR-010/FR-011 do not *construct* those stages,
  so there is no send to set live and the clause has no configuration in this phase. Both remain
  gated for the shipped engine by SC-012 (b), which requires the unedited Seraphis suites to pass.
  *Test:* `CavernVerb_FreezeEnergyConservation`.

- **SC-002 — No metallic ringing: echo density** (roadmap line 429, inheriting Seraphis Phase 6
  SC-003). *Metric:* normalised echo density through the **lifted helper** FR-080 requires,
  `TestUtils::normalisedEchoDensity(...)` — the computation formerly inline at
  `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` (1 ms windows, RMS per window, fraction of
  windows whose RMS exceeds `peak · 0.01`, on the mono sum), called by both that TU and this one so
  the two cannot diverge. The window is the **geometry-derived** one from Seraphis Phase 6 SC-003:
  `t_start` = first 1 ms window whose RMS exceeds `peak · 0.01`, `W = max(250 ms, 3·m_long)` with
  `m_long` the longest Size-scaled line read through `getEffectiveDelayLengthSamples`.
  *Input:* G-1, `mix = 1`. *Threshold:* **NED ≥ 0.8** over the grid
  `setSize ∈ {0, 0.5, 1}` × **`setDimensionality ∈ {0, 1}`**, `N = 8`, `setDensity` at its default
  (`0.75`, FR-066).
  The dimensionality axis is the source criterion's own always-on core
  (`specs/seraphis-phase6-aether-space/spec.md:1529-1533`) and is restored here rather than dropped:
  the matrix morph is what redistributes energy across the lines, so it is the axis most able to
  change how quickly the field fills, and FR-016 exposes it.
  *Second clause (Phase-9 specific):* the ER must **not** create a sparse hole — NED with
  `setEarlyLevel(1.0)` is **not lower** than with `setEarlyLevel(0.0)` by more than 0.05.
  **Both figures are measured over one common window**, and that window is derived from the
  `setEarlyLevel(0.0)` render: at `setEarlyLevel(1.0)` `t_start` is the first ER arrival
  (≥ `kEarlyFirstArrivalFloorMs`), while at `setEarlyLevel(0.0)` the ER no longer reaches the output
  at all (FR-024) and the first occupied window is the FDN onset, later by roughly the shortest line
  plus diffusion (`kRefDelays8[0] = 967` samples = 20.1 ms at 48 kHz, `aether_reverb.h:1564`).
  Comparing an NED over `[t_start_A, +W]` against one over `[t_start_B, +W]` with
  `t_start_A ≠ t_start_B` does not test the clause's claim, so the per-render derivation of the first
  clause is **not** inherited here: `[t_start, t_start + W]` comes from the `setEarlyLevel(0.0)`
  render and is applied unchanged to both. The test records **both** `t_start` values side by side so
  a drift between them is visible, plus `m_long`, `W`, the excluded window count and both NED figures
  per configuration.
  *Test:* `CavernVerb_EchoDensity`.

- **SC-003 — Damper motion is smooth** (roadmap line 430, "damper-motion smoothness test").
  *Configuration:* P-1 with `setDamperDepth(1.0)` and `setDamperRate(1.0)` (the fastest, worst case).
  *Clause (a) — trajectory, with the constant written out.* Over a 120 s render of G-2, the
  per-control-chunk change in `getDamperOffsetOctaves(i)` is bounded for every line by

  ```
  kMaxOffsetStepPerChunk = α · (kMaxDamperOctaves / kInternalStd + kMaxDamperOctaves)
        α = 1 − exp(−5000 · kControlChunkSamples / (kDriftOutputSmoothMs · sr))
          = 1 − exp(−5000 · 64 / (150 · 48000)) = 0.0434712
  ```

  i.e. the smoother's per-chunk approach fraction times the **worst single-chunk target swing** of
  the published (post-clamp, FR-037) quantity. Two facts fix the constants (2026-09-16 ruling, plan
  S0.2 B-2): `OnePoleSmoother`'s `smoothTimeMs` is a **time to 99 %**, not a time constant
  (`smoother.h:77-93`, per-sample pole `exp(−5000 / (ms · sr))`), so the approach fraction over a
  64-sample chunk at 48 kHz is 0.0434712; and the published value is
  `clamp(kMaxDamperOctaves · s / kInternalStd, ±kMaxDamperOctaves)` with `s ∈ [−1, +1]`
  (`brownian_drift.h:212`), whose worst chunk starts at the clamp (`−kMaxDamperOctaves`) and heads
  for the unclamped target (`+kMaxDamperOctaves / kInternalStd`), a swing of 4.5 octaves at
  `kMaxDamperOctaves = 1.5`. The bound is therefore **0.19562 octaves per chunk** (the earlier
  literal 0.02655 carried both errors and was corrected under FR-082, not relaxed: it remains a hard
  smoother property binding every chunk on every line). The constant is parameterised on
  `kMaxDamperOctaves`, `kInternalStd`, `kDriftOutputSmoothMs` and `sr`, so a future change to any of
  them moves the bound arithmetically and not by re-derivation. The measured
  maximum is recorded as a figure. This bound is a smoother property, not a statistical one, so it
  is not sampling-limited.
  *Clause (b) — coefficient, in coefficient units.* The quantity the audio loop actually uses is
  `AetherReverb::getEffectiveDampingCoefficient(i)` (FR-041), which is **dimensionally different**
  from clause (a)'s octaves: `c` is a dimensionless one-pole coefficient in `[0.001, 1.0]`
  (`aether_reverb.h:3148`), so clause (a)'s number cannot bind it. The bound is derived instead from
  FR-048's Lipschitz constant, which is **independent of `setDarkness`**:

  ```
  |Δc| per chunk  ≤  ln2 · e^(−1) · kMaxOffsetStepPerChunk  =  0.25499 · kMaxOffsetStepPerChunk
  ```

  — `= 0.04988` at `kMaxDamperOctaves = 1.5` (0.25499 × 0.19562; the earlier literal 0.00677 carried
  clause (a)'s error). The measured maximum `|Δc|` is recorded as a figure
  alongside it.
  *Clause (c) — audible:* `ClickDetector` (`tests/test_helpers/artifact_detection.h:99`) with **P-5's
  configuration** over the same 120 s render with G-2 reports **zero** detections.
  *Clause (d) — dormancy re-entry (FR-065), plus a depth step.* In one further render,
  `setEarlyLevel`, `setEarlySend` and `setMix` are each stepped `0 → 1` in a single call at pinned
  times ≥ 5 s apart, and again `1 → 0`; P-5's detector reports **zero** detections across every
  transition. This is the clause that catches a skipped chain being re-entered as a step rather than
  on FR-065's ≥ 50 ms shaped ramp. **Additionally**, in the same render `setDamperDepth` is stepped
  `0.35 → 1.0` and back at two further pinned times ≥ 5 s apart: P-5's detector reports **zero**
  detections across those transitions too — this is a different mechanism than FR-065's dormancy
  ramp (depth is not a zero-gain chain-skip slot), and the click-freedom instead comes from FR-031's
  routing of `setDamperDepth` through `BrownianDrift::setDepth`'s 150 ms output smoother.
  *Test:* `CavernVerb_DamperMotionSmoothness`.

- **SC-004 — Damper motion is per-line, not global** (roadmap line 421: "per-delay-line damping
  filters"). *Metric:* Pearson correlation between every pair of the `numChannels` offset
  trajectories, sampled once per control chunk over 10 minutes at `setDamperDepth(1.0)`.
  ***`setDamperRate` is pinned, and the thresholds are derived from it*** — leaving it at FR-032's
  slow default would make the criterion fail on correct code. `BrownianDrift`'s decorrelation time is
  `τ = kTauMin + smoothness·(kTauMax − kTauMin)`, `kTauMin = 0.2 s`, `kTauMax = 30 s`
  (`brownian_drift.h:97-99`); at the fixed default (FR-032, `τ ≈ 25.5 s`) a 600 s record holds
  only `T/(2τ) ≈ 10` effectively independent samples, so the sampling s.d. of Pearson `r` between two
  genuinely independent lines is `≈ 1/√10 ≈ 0.32` — across the 28 pairs of an `N = 8` configuration
  the maximum `|r|` would routinely exceed 0.60 and the mean would sit at the 0.30 bound, on an
  implementation that is right.
  *Arm (a) — the gating arm.* `setDamperRate(1.0)`, i.e. `τ = 0.2 s` and `≈ 1500` effective
  independent samples, giving `s.d.(r) ≈ 0.026`. *Threshold:* **mean pairwise |r| ≤ 0.30** and
  **max pairwise |r| ≤ 0.60** — both now more than ten sampling s.d. clear of zero.
  *Arm (b) — the shipped default rate.* Same record at FR-032's default `setDamperRate`, gated
  against an **in-test null distribution** rather than a fixed number: a bank of `numChannels`
  independently re-seeded `BrownianDrift` instances at the same rate is advanced over the same record
  length, its 28 pairwise `|r|` values computed, and the criterion requires the measured
  `max |r| ≤ the 99th percentile of that null`. Both arms record their measured mean and max.
  *Negative control (the criterion's teeth):* the same statistic computed on a reference render in
  which one drift value is broadcast to all lines must **exceed** those bounds — otherwise the metric
  cannot discriminate and the criterion is vacuous. Additionally, no damper trajectory may equal an
  `AetherReverb` delay-jitter trajectory (FR-034's salt separation), asserted by comparing the
  published offsets against a `BrownianDrift` seeded with `deriveStreamSeed(seed, kDriftSaltBase + j)`.
  *Test:* `CavernVerb_DamperDecorrelation`.

- **SC-005 — The dampers change the sound, monotonically in depth** (roadmap line 422: "the space
  itself breathes darkly"). *Input:* G-2 held for 120 s, wet-only.
  *Configuration:* **`setDamperRate(1.0)`** — pinned for the same reason as SC-004, and here it is
  what makes the statistic usable at all: at the slow default the 115 s of 1 s centroid frames after
  the first 5 s hold only `≈ 115/(2τ) ≈ 2–6` effectively independent samples, so the s.d. estimator
  carries tens of percent relative error and adjacent depth levels (0.5 vs 0.75, a nominal 1.5×
  separation) cross by chance. At `τ = 0.2 s` the same record holds `≈ 290`.
  *Metric:* the standard deviation over time of the tail's spectral centroid, measured in 1 s frames
  after the first 5 s, **averaged over ≥ 8 seeds** at each depth.
  *Threshold — three clauses, none of them a strict ordering of five noisy estimates:*
  (a) **Endpoint separation, with a margin:** the statistic at `setDamperDepth(1.0)` is **≥ 3×** its
  value at `setDamperDepth(0.0)`.
  (b) **Ordering, non-strict:** over `setDamperDepth ∈ {0, 0.25, 0.5, 0.75, 1.0}` the Spearman rank
  correlation between depth and the statistic is **≥ 0.9**.
  (c) **Inertness at zero, as a number:** at `depth = 0` the statistic is **≤ 1.05 ×** the value from
  a reference render made with the extension's offsets never set ("within the measurement noise" is
  not a threshold and is struck).
  *Direction, not merely monotonicity (D-7 / FR-048's sign convention):* in a separate render at
  `setDamperDepth(0)` with a **static** `+0.5` octave offset published to every line, the measured
  T60 in the 8 kHz octave band is **lower** than with a static `−0.5` octave offset — i.e. a positive
  offset lowers the cutoff and darkens the line. A monotone-in-depth criterion alone is satisfied by
  *any* monotone map, including one with the sign inverted, which is exactly the mistake FR-048
  exists to prevent.
  Reported, not gated: the absolute centroid excursion in Hz. *Test:*
  `CavernVerb_DamperSpectralMotion`.

- **SC-006 — Cavern early reflections: sparse, late, incommensurate** (roadmap lines 423–424).
  *Input:* G-1, `setEarlyLevel(1.0)`, `setMix(1.0)`, and — for clauses (a) and (c) —
  **`setEarlySend(0.0)`**, which is a reachable product state under FR-026/FR-061 and is the whole
  reason `setEarlySend` is a control: the owned engine then receives digital silence and the render
  carries the ER pattern **alone**. The earlier wording ("the owned engine's wet contribution muted")
  named no mechanism and none existed — with the send fixed as a constant the FDN is excited by the
  ER itself, and the verified geometry makes that fatal: `kRefDelays8 = {967 … 5087}`
  (`aether_reverb.h:1564`, 20.15–105.98 ms at 48 kHz) with FR-012 forcing `S ≥ 1.0`, so the FDN starts
  emitting ~20–25 ms after the first ER tap (`kEarlyFirstArrivalFloorMs = 60` ms, FR-021), i.e. from
  ~85 ms onward — across taps 2…12 of the 220 ms default table (FR-023). Peak-picking discrete arrivals out of a diffuse field is not executable, and
  an NED "over the ER window alone" would be measuring the late field.
  *Clause (a):* at `setEarlySend(0.0)`, the located arrival times match `getEarlyTapDelaySamples(i)`
  within ±1 sample for every tap, at `setEarlySizeMs ∈ {min, default, max}` and at 44.1 / 48 / 96 kHz.
  *Clause (b):* the first arrival is ≥ `kEarlyFirstArrivalFloorMs` at the default ER size (FR-021).
  *Clause (c):* at `setEarlySend(0.0)`, the tap set is sparse — measured NED (SC-002's helper) over
  `[first arrival, last arrival]` is **below** 0.5, the negative control that distinguishes a
  reflection pattern from a diffuse field (Seraphis Phase 6's own SC-003 uses a low-density negative
  control the same way, `spec.md:879`).
  *Clause (d) — incommensurability, with a metric and a number.* FR-022's inequality is evaluated over
  the shipped table in **milliseconds** at the default ER size: for every ordered pair `i ≠ j`,
  `min over p, q ∈ {1…kIncommensurabilityOrder} of |p·d_i − q·d_j| / min(d_i, d_j)
  ≥ kEarlyIncommensurabilityTol` (`= 0.05f`). The **measured minimum over all pairs** is recorded as a
  figure. "Not an integer multiple" is not the property and is not what is asserted.
  *Clause (e) — the send is not vacuous:* with `setEarlySend(1.0)` and the same input, the render's
  energy after `t = 2 · (last tap delay)` exceeds the `setEarlySend(0.0)` render's by **≥ 20 dB**, so
  clauses (a) and (c) cannot be passing because the send silently does nothing.
  *Test:* `CavernVerb_EarlyReflectionGeometry`.

- **SC-007 — Dark tuning is measurable, and there is no shimmer** (roadmap lines 425–426).
  *Clause (a) — HF decay shortened:* at defaults (FR-066), the measured T60 in the 8 kHz octave band
  is **≤ 0.35 ×** the T60 in the 250 Hz band (G-1, Schroeder integration per band). The threshold was
  re-derived against FR-066's pinned table and retained unchanged (FR-066).
  *Clause (b) — darker than the Seraphis default:* the tail's long-term spectral centroid at
  `CavernVerb` defaults (FR-066) is **at least 30 % lower** than a bare `AetherReverb` at its own
  defaults, same input, same render length — asserted as `centroid_cavern ≤ 0.70 × centroid_bare`,
  the bare reference at `AetherReverb`'s own defaults (`kDefaultSize = 0.50`, `kDefaultDamping =
  0.40`, `aether_reverb.h:2730`, `:2740`), both figures reported (2026-09-16 confirmation). The
  threshold was likewise re-derived against FR-066's pinned table and retained unchanged.
  *Clause (c) — size biased huge:* `getEffectiveDelayLengthSamples` summed over the lines at
  `setSize(0)` is at least **4.4×** the value a bare `AetherReverb` reaches at `setSize(0)` (FR-012).
  ***The comparison is made drift-free and seed-matched, and the threshold has margin*** — the bare
  ">= 4×" form was a coin flip on this spec's own admissible values. `sizeScale(v) =
  min(0.25·exp2(4v), maxSizeScale_)` (`aether_reverb.h:3011-3015`) gives exactly `S = 1.0` at a size
  control of 0.5 and exactly `S = 0.25` at 0, so a `kCavernSizeFloor` of exactly 0.5 makes the nominal
  ratio exactly 4.0; and `getEffectiveDelayLengthSamples` returns `effectiveDelay_`, which on the
  longest half of the channels carries `modDepth · kModExcursionFraction · base · drift_[j]`
  (`:3042-3053`, `kDefaultModDepth = 0.25f` at `:2746`, `kModExcursionFraction = 0.005f` at `:1398`),
  with independent drift state in the two instances. Three corrections, all of them tightenings:
  (i) FR-012 now fixes `kCavernSizeFloor ≥ 0.55`, so the nominal ratio is **4.59**, not 4.00;
  (ii) the bare comparison instance is seeded with **`deriveStreamSeed(1, kCavernReverbSalt)`** — the
  exact seed `CavernVerb` hands its owned engine under FR-034 — and both are sampled at the same
  absolute sample index after identical render lengths, so the two jitter realisations are the *same*
  and the differential vanishes; (iii) `setBreath(0)` on `CavernVerb` and
  `setSizeBreathDepth(0)`/`setDimensionalityTideDepth(0)` on the bare instance remove the second
  unsampled drift source (FR-017). With (ii) and (iii) the ratio is the nominal `S₁/S₂` up to float
  rounding; the residual spread is recorded as a figure, and the 4.4 threshold leaves ~4 % of margin
  below the 4.59 nominal. `setModDepth` is deliberately absent from FR-061's surface and is **not**
  added for this measurement.
  *Clause (d) — no shimmer:* with G-4 (220 Hz sine) rendered for 20 s wet-only, the energy in
  narrow bands at 440 Hz and 330 Hz is **≤ −40 dB** relative to the 220 Hz band, and
  `isShimmerActive()` reports **false**. *Test:* `CavernVerb_DarkTuning`.

- **SC-008 — Infinite hold is usable** (roadmap line 427).
  ***The render is a pinned timeline, not "ten cycles"*** — with the engine thawed between cycles and
  a default decay of ≥ 10 s (FR-014), the state energy at each successive freeze entry depends
  entirely on the unstated thaw duration and input, so a ±0.5 dB comparison *across* cycles has no
  defined referent. The binding protocol, at P-1 with `setDamperDepth(1.0)`:

  ```
  2 s G-2   →   [ setFreeze(true), 5 s G-3, setFreeze(false), 3 s G-2 ] × 10
  ```

  *Clause (a) — conservation is measured WITHIN each frozen span, not across cycles:*
  `getStateEnergy()` at the moment `isFrozen()` first reports true (i.e. at latch completion) versus
  at the sample before `setFreeze(false)`, for each of the ten spans independently: every pair within
  **±0.5 dB**. Ten figures are recorded.
  *Clause (b) — no clicks:* **zero** `ClickDetector` detections across the whole render, with **P-5's
  configuration** (the earlier form named no config, and the detector's verdict is entirely determined
  by it).
  *Clause (c) — the live paths (FR-029), on one named span:* during the **third** frozen span only —
  named so the measurement has one referent — the input is G-2 rather than G-3, and the output must be
  non-zero and correlated with the input at the ER tap delays. That span is excluded from clause (a),
  whose reading requires the silent input; the other nine carry it.
  *Test:* `CavernVerb_FreezeCycles`.

- **SC-009 — CPU ≤ 5 % of one core, global** (roadmap line 430). *Basis:* ns per 512-sample block at
  48 kHz, best-of-N over ≥ 500 blocks after ≥ 400 warm-up blocks, `[.perf]` tagged, run alone (P-4),
  against a checked-in baseline with **both** `static_assert(baseline · 1.5 <= 533 333.3)` and
  `static_assert(baseline <= 355 555.6)` plus the runtime `REQUIRE(measured <= baseline · 1.5)` —
  the `aether_reverb_perf_test.cpp:135-147`, `:350-354` construction, inherited unchanged.
  ***Every arm pins its prepare-time fields***, because `diffusionFftSize` and `maxEarlySeconds` are
  the two that dominate the STFT and ER cost and an unpinned arm is not reproducible:
  arms (a), (c) and (d) run at `diffusionFftSize = 1024` and `maxEarlySeconds = 0.30`;
  arm (b) runs at **`diffusionFftSize = 4096`** and **`maxEarlySeconds = 0.60`** (both maxima), which
  is what makes it comparable with the shipped worst-case baseline below.
  *Arms:* (a) **default** cavern (FR-066's pinned table); (b) **worst case** — `setSize(1)`, `setDamperDepth(1)`,
  `setDamperRate(1)`, `setFog(1)`, `setEarlyLevel(1)`, `setEarlySend(1)`, `setEarlySizeMs(max)`,
  `setBreath(1)`, `N = 16`, prepared as above; (c) **frozen**; (d) **damper delta** — arm (a) with
  `setDamperDepth(0)`, so the cost attributable to the moving dampers is reported as a difference
  rather than asserted in the abstract. *Headroom this criterion starts from, stated so a failure is
  diagnosable:* the shipped `AetherReverb` worst arm is 200 114 ns/block, and its configuration is
  "`N = 16`, everything on, **spectral @ 4096**, size = density = 1, 32 resonators"
  (`aether_reverb_perf_test.cpp:322-330`) **with shimmer and bloom live**, both of which `CavernVerb`
  does not construct (FR-010); its default arm is 114 595 ns/block. Arm (b) is pinned at
  `diffusionFftSize = 4096` precisely so that comparison is apples to apples — at the `PrepareConfig`
  default of 1024 the stated room would not be the margin the arm consumes. Against
  `kMaxAdmissibleNs = 355 555.6` the Phase-9 additions have ≳ 150 000 ns/block of room measured
  against that shimmer-inclusive, 4096-point worst case. If an arm exceeds it, FR-082 applies.
  *Test:* `CavernVerb_CpuBudget`.

- **SC-010 — Zero allocation after prepare.** `AllocationScope`
  (`tests/test_helpers/allocation_detector.h:111`) around 60 s of rendering with every setter
  exercised mid-render (including `setEarlySizeMs`, `setFreeze`, `setSeed` and
  `setDamperDepth`) records **zero** allocations. `getAllocatedBytes()` is constant across the
  render. *Test:* `CavernVerb_NoAllocation`.

- **SC-011 — Determinism under seed.** Two instances prepared identically with the same seed produce
  renders agreeing within `render_fingerprint.h` tolerances (`kSampleTolerance = 5.0e-4f`,
  `kMetricTolerance = 2.5e-4`); two instances with **different** seeds differ by more than those
  tolerances on the total-variation metric. `setSeed(s); reset();` mid-life restores the opening
  trajectory. **No bit-exact float golden** (roadmap line 556; `tools/lint-float-bit-goldens.js`).
  *Test:* `CavernVerb_Determinism`.

- **SC-012 — The `AetherReverb` extension is inert and Seraphis is green** (roadmap lines 560–562;
  FR-044, FR-083).
  *Clause (a) — the read site still yields the pre-extension coefficient.* FR-047 clears
  `damperOffset_` to zero at `prepare()` and FR-043/FR-044 route **both** arms through the same
  `effectiveDampCoeff_` read site, so "a bare engine never told about offsets" and "one explicitly
  given an all-zero vector" hold identical state and execute identical instructions: their
  bit-identity is guaranteed by construction and proves nothing about whether the extension changed
  what the shipped engine does. The clause with teeth is therefore a **recomputation** of the
  published law: over a sweep of `setSize`, `setDecaySeconds`, `setDensity` and `setDamping`
  (≥ 5 values each, `N ∈ {8, 16}`), with all offsets zero, the test recomputes
  `gDC = 10^(−3m/(T60_dc·sr))`, `gNyq` at `T60_nyq = T60_dc · kDampingNyquistRatio^damping`,
  `ratio = clamp(gNyq/gDC, 0, 1)` and `c = clamp(2·ratio/(1+ratio), 0.001f, 1.0f)`
  (`aether_reverb.h:3140-3148`) from `getEffectiveDelayLengthSamples(i)` and the sample rate, and
  `REQUIRE`s **exact float equality** against `getEffectiveDampingCoefficient(i)` for every line. The
  two-instance render comparison is kept **only as a cheap smoke check** and is labelled as one in
  the test: a bare `AetherReverb` and one given an all-zero vector produce bit-identical output over
  a 10 s render (a comparison of two runs of the *same* build, so the cross-toolchain objection to
  bit-exact goldens does not apply — nothing is checked in, and the float-golden lint scans for
  committed digests).
  *Clause (b):* the **whole `dsp_effects_tests` and `dsp_systems_tests` executables pass** — which
  covers all five `aether_reverb_*` TUs (`dsp/tests/CMakeLists.txt:528-532`) and all **seven**
  `dsp/tests/unit/systems/seraphis_*.cpp` TUs FR-083 enumerates, not the four an earlier list named —
  with **no test file edited** as part of this phase, asserted by inspection of the phase diff, which
  must touch no file under `dsp/tests/unit/effects/aether_reverb_*` or
  `dsp/tests/unit/systems/seraphis_*`.
  *Clause (c):* `Seraphis.vst3` passes pluginval strictness 5 unchanged. *Test:*
  `AetherReverb_DamperOffsetInert` plus the existing suites.

- **SC-013 — Reported latency equals actual.** **Two renders, because one cannot carry both clauses:**
  the ER lives in the wet path (FR-019, FR-024, D-2) and at `mix = 0` the wet path contributes exactly
  `0.0f` by assignment (FR-063), so a single `mix = 0` render measures an ER signal that is
  identically zero.
  *Render (i) — dry path:* an impulse at `setMix(0.0)` emerges delayed by exactly
  `getLatencySamples()` samples.
  *Render (ii) — ER path:* an impulse at `setMix(1.0)` with `setEarlyLevel(1.0)` and
  `setEarlySend(0.0)` (so the arrivals are pickable, SC-006) locates the ER arrivals, and arrival `i`
  lands at **`getLatencySamples() + getEarlyTapDelaySamples(i)`** within ±1 sample.
  Both renders run at `spectralDiffusionEnabled ∈ {true, false}` and
  `diffusionFftSize ∈ {256, 1024, 4096}`. Together they establish that dry, ER and wet are mutually
  aligned (FR-062). *Test:* `CavernVerb_Latency`.

- **SC-014 — Bounded under soak, at the worst parameter combination** (roadmap line 543:
  "Boundedness is the theme-level FR"). **A 30-minute render at SC-009 arm (b) driven by G-2 for the
  full 30 minutes** — continuously generated per P-3, unfrozen, peak-normalised to `|x| ≤ 1.0`.
  ***The earlier "G-2 for 60 s then G-3 for the remaining ~29 minutes" form was unsatisfiable:*** the
  arm is not frozen and decay is clamped to ≤ 60 s (`aether_reverb.h:2735-2736`, FR-014), so with no
  input after 60 s the tail decays monotonically to zero and the per-minute RMS **does** collapse
  monotonically, by construction. A boundedness statement needs continuous excitation.
  *Clause (a) — peak:* `max |output|` over the whole render is **≤ 4.0**, a number and not "a stated
  bound". Derivation, so it can be checked rather than trusted: the ER stage is feed-forward with
  `Σ|gain_i| = kEarlyGainSum ≤ 2.0` (FR-028), the owned engine's loop gain is ≤ 1.0 at all times
  outside freeze (its FR-032, re-established for every admissible offset by FR-045/FR-048), and
  `CavernVerb`'s equal-power dry/wet has unit maximum gain (FR-063) — so `1.0 · 2.0` for the ER
  contribution plus an order-unity late field sits inside 4.0 with margin. The **measured** peak is
  recorded as a figure; a measurement above 4.0 is a defect to fix, never an occasion to re-derive
  the bound (FR-082).
  *Clause (b) — no drift:* the per-minute RMS neither grows nor collapses monotonically across the
  30 samples (no strictly monotone run of length 30, and the last minute within ±3 dB of the tenth).
  *Clause (c):* `getNonFiniteRecoveryCount()` returns **0** and no output sample is non-finite
  (bit-pattern test, not `std::isnan`).
  *Clause (d) — the frozen soak:* a second 30-minute render with `setFreeze(true)` entered at 60 s and
  G-3 thereafter, asserting SC-001 clause 1's **±0.5 dB** `getStateEnergy()` conservation over the
  full 29 remaining minutes. This is where "neither dies nor explodes overnight" is actually tested
  under silence; clause (b) cannot be, and is not, asked to carry it.
  Tagged `[long]` per the project's tag convention (toolchain-independent, > 15 s).
  *Test:* `CavernVerb_Soak`.

- **SC-015 — Sample-rate coverage.** `prepare()` at 44 100 / 48 000 / 96 000 / 192 000 Hz followed by
  a 10 s render each: no non-finite output; **ER tap times agree across rates within ±0.05 ms**, a
  tolerance stated in **time units** because "±1 sample" across four rates is unit-ambiguous and
  unsatisfiable read one way — one sample at 44.1 kHz is 22.7 µs, which is 4.35 samples at 192 kHz,
  so the comparison would fail on quantisation alone (±0.05 ms is ~2.2 samples at 44.1 kHz and ~9.6 at
  192 kHz — loose enough for rounding at every rate, and well inside the minimum tap separation
  FR-022's 0.05 relative tolerance guarantees, which is ≥ 3 ms at FR-021's fixed 60 ms first
  arrival). `getLatencySamples()` is consistent with the configured FFT size;
  re-`prepare()` at a new rate mid-life leaves no stale state (a second render matches a
  freshly-constructed instance within `render_fingerprint.h` tolerances). *Test:*
  `CavernVerb_SampleRates`.

- **SC-016 — The private constants `CavernVerb` mirrors are pinned through behaviour** (FR-084).
  `AetherReverb::kDecayMinSeconds`/`kDecayMaxSeconds` (`:2735-2736`), `kMaxChannels = 16` (`:2725`)
  and `kDampingNyquistRatio = 0.05f` (`:2788`) are **private**, so no `static_assert` from
  `CavernVerb` can reach them and FR-084's compile-time gate cannot cover the three mirrors most at
  risk. They are probed at runtime instead, through public behaviour only:
  *(a) Decay clamp:* `setDecaySeconds(0.4f)` and `setDecaySeconds(61.0f)` produce measured broadband
  T60s indistinguishable (within 5 %) from `setDecaySeconds(0.5f)` and `setDecaySeconds(60.0f)`
  respectively — i.e. the shipped clamp is where FR-014 re-declares it.
  *(b) Channel count:* prepared at `numChannels = 16`, `getEffectiveDelayLengthSamples(15) != 0.0f`
  and `getEffectiveDelayLengthSamples(16) == 0.0f` — the `:2506-2509` out-of-range idiom pinning the
  ceiling FR-030 mirrors.
  *(c) Nyquist ratio:* at `setDarkness(1.0)` (damping 1) the measured T60 in the 8 kHz octave band is
  within 15 % of `0.05 ×` the measured T60 at DC-adjacent (125 Hz) band, per the shipped law.
  A future divergence in any of the three then breaks a test rather than drifting silently.
  *Test:* `CavernVerb_MirroredConstants`.

---

## Edge Cases

**Real-time boundaries**

- `processStereoBlock(numSamples = 0)` — returns without advancing the control grid or any modulator
  (FR-005), matching the owned engine (`:2171-2173`).
- Any of the four pointers null — returns without writing (FR-005).
- A block larger than `maxBlockSamples` — the control grid is anchored to the absolute sample counter
  and slices internally (FR-007), so an oversized block is processed correctly; no buffer is indexed
  by the caller's block length.
- A setter called before `prepare()` — accepted and stored; `prepare()` re-applies it (FR-060).
- `setSeed` / `setFreeze` / `setEarlySizeMs` called every block — must not allocate (SC-010) and must
  not stretch any smoother's ramp without bound. The recorded precedent is
  `feedback_ecology.h:2290-2296`, where a per-block wake write stretched every other loop's ramp; the
  same guard discipline applies to `CavernVerb`'s smoothed controls.
- `silence()` / `reset()` mid-render — allocation-free and `noexcept`, but documented as **not**
  audio-thread operations (FR-006), following the owned engine's own wording.

**Parameter extremes**

- `setDamperDepth(1)` with `setDamperRate(1)` and `setDarkness(1)` simultaneously — the fastest,
  deepest motion on the darkest base coefficient. The clamp at FR-045 keeps `c ∈ [0.001, 1]`, so the
  loop stays non-expansive; SC-003 and SC-014 cover it.
- `setDarkness(0)` with `setDamperDepth(1)` — an offset that would push the coefficient **above** 1
  is clamped to 1, i.e. to "no damping", not to an expansive value.
- A hostile caller invoking `AetherReverb::setDamperOffsetsOctaves` directly with ±1e30, NaN, Inf, a
  null pointer, `count = 0` or `count > numChannels` — each handled by FR-040 (non-finite → 0, clamp,
  null/zero → cleared, excess ignored). No configuration of offsets can make the loop expansive.
- `setEarlySizeMs` at its maximum with `maxEarlySeconds` at its minimum — the setter's clamp is the
  buffer-derived one; the ER never reads past `DelayLine::maxDelaySamples()`.
- `setEarlyLevel(0)` and `setMix(0)` together — output is exactly the aligned dry signal, with the
  wet contribution assigned (not multiplied) to zero (FR-024, FR-063). Dormancy (FR-065): with
  `setEarlySend(0)` as well, the ER tap loop is skipped; the wet path keeps running regardless,
  because the FDN state is the instrument a drone player returns to. Leaving either zero ramps back
  in over ≥ 50 ms (SC-003 (d)).
- `setDecaySeconds(60)` plus `setFreeze(true)` — freeze supersedes decay; the shipped engine clamps
  decay to 60 s and documents that "infinite" *is* `setFreeze` (`:2735-2736`).

**Sample-rate and prepare changes**

- Re-`prepare()` at a new rate while frozen — `prepare()` is not an audio-thread call; the
  implementation must leave no latched freeze state that contradicts the new geometry. SC-015 covers
  the fresh-instance equivalence.
- 8 kHz (the engine's floor, `:1392`) — ER tap delays in milliseconds may quantise coarsely; the
  incommensurability property (FR-022) is asserted in milliseconds, not in samples, and the test
  records the sample-quantised values.
- 192 kHz — the largest ER buffer; `getAllocatedBytes()` scales linearly and is reported.
- Shimmer's 44.1 kHz floor is irrelevant here because shimmer is not constructed (FR-010, FR-074) —
  the one place where Vorago's configuration removes a Seraphis-era constraint rather than inheriting
  it.

**Seed determinism**

- Same seed, different block partitions (P-2) — identical within `render_fingerprint.h` tolerances,
  because the control grid is absolute (FR-007).
- Same seed, two `CavernVerb` instances — identical (SC-011).
- `kCavernDamperSaltBase` colliding with `kDriftSaltBase + j` — the failure this would cause is
  silent (a damper tracking a delay-jitter lane exactly), which is why FR-034 fixes the salts and
  SC-004 asserts the separation rather than trusting it.
- Seed 0 — `deriveStreamSeed` substitutes a non-zero value (`core/random.h:110`), so seed 0 is a
  valid, distinct configuration and not a collapsed one.

**Freeze boundaries**

- `setFreeze(true)` during the 50 ms latch of a previous `setFreeze(false)` — the owned engine owns
  this behaviour; `CavernVerb` adds nothing and `isFrozen()` still reports only completed latches.
- Damper offsets moving while the latch is in flight — FR-046 stops the **application** at the
  `freezeTarget_` edge, i.e. at the moment the caller asks, not at the end of the latch, exactly as
  `dampCoeff_` already behaves (`:3611-3614`).
- Freeze with digital-silence input for an hour — the dampers keep advancing (FR-036) but change
  nothing; `getStateEnergy()` stays inside SC-001's window.

---

## Decisions taken where the roadmap is silent

- **D-1. The ER is mixed into the wet path and also feeds the FDN** (FR-026), rather than being a
  parallel dry-side effect. Reason: roadmap line 415 puts ER *before* diffusion and the FDN in the
  chain, and a reflection pattern that does not excite the late field produces a slap-back sitting in
  front of an unrelated reverb.
- **D-2. The owned engine runs at `setMix(1.0)` and `CavernVerb` owns the dry/wet mix** (FR-019,
  FR-063). Reason: the ER must sit in the wet path, and there is no way to inject it into
  `AetherReverb`'s internal mix. The cost is one alignment delay-line pair (FR-062).
- **D-3. The ER tap table is fixed and seedless** (FR-035). Reason: the roadmap asks for a *pattern*
  ("stone-space flavoured"), not a randomised one, and a seeded ER would make SC-006 a statistical
  criterion for no musical gain. Voicing variety comes from `setEarlySizeMs` and
  `setEarlyAbsorption`.
- **D-4. `spectralDiffusionEnabled` defaults to `true`** (FR-004) and the latency it costs is paid
  through `CavernVerb`'s own alignment lines. Reason: it is the roadmap's "spectral damping" stage
  (line 415) and Vorago's Fog macro (line 455) needs it. It remains a prepare-time opt-out for a
  zero-latency configuration.
- **D-5. Seventeen controls, enumerated** (FR-061). Reason: Phase 10 owns the macro layer; a space
  engine that proxies all twenty `AetherReverb` setters would force Phase 10 to re-decide the dark
  bias that is *this* phase's job (roadmap line 425). The seventeenth, `setEarlySend` (FR-026), is
  there because a closed enumeration and FR-026's "a named constant **or** a control" admitted two
  different public APIs, and because SC-006's ER-isolation clauses need `setEarlySend(0.0)` to be a
  state the product can actually be in — with the send fixed as a constant the only signal reaching
  the FDN is the ER itself, and the clauses would be peak-picking discrete arrivals out of the
  diffuse field those arrivals excite.
- **D-6. No new class for the dampers or the ER.** Reason: the ODR note (roadmap lines 127–129) and
  the hazard list; the component adds exactly one namespace-scope name, with testability supplied by
  accessors (FR-027, FR-037, FR-041) instead of by exposed types.
- **D-7. The extension publishes offsets in *octaves*, not in coefficients.** Reason: the audible
  quantity is the damper's cutoff, which is logarithmic in frequency; a linear offset on `c` would
  make the same `depth` inaudible at one damping setting and violent at another, and would make
  SC-005's monotonicity a function of `setDarkness`. **The behavioural content of this decision is
  FR-048**, which writes out the cutoff recovery, the `2^(−offset)` scaling, the sign convention (a
  positive offset *lowers* the cutoff) and the re-derivation and clamp — a decision stated only in a
  unit name and a method name would be implemented two different ways, and in opposite directions, by
  two different implementers. SC-005 asserts the direction, not only the monotonicity.

---

## Traceability

| Roadmap statement (line) | Requirement(s) | Criterion(s) |
|---|---|---|
| "Reuse the AetherReverb FDN/diffusion/spectral-damping core" (420) | FR-002, FR-010, FR-012 – FR-019 | SC-002, SC-007, SC-009 |
| "Avoid building two big FDNs" (117) | Architecture ruling; Non-Goals | SC-009, SC-012 |
| "Moving dampers — per-delay-line damping filters whose cutoffs wander slowly (`BrownianDrift`)" (421–422) | FR-030 – FR-037, FR-040 – FR-048 | SC-001, SC-003, SC-004, SC-005 |
| "so the space itself breathes darkly" (422) | FR-017, FR-031, FR-036 | SC-005 |
| "Cavern ER pattern — sparse, long-predelay early reflections (stone-space flavoured)" (423–424) | FR-020 – FR-029 (incl. FR-026's `setEarlySend`) | SC-006, SC-002 (second clause) |
| "Dark tuning: HF decay strongly shortened" (425) | FR-013, FR-014 | SC-007 (a), (b) |
| "size range biased huge" (425) | FR-012 | SC-007 (c) |
| "no shimmer-up taps … bloom lives in the voice" (425–426) | FR-010, FR-011 | SC-007 (d) |
| "Infinite-hold (freeze) retained — a drone instrument needs it" (427) | FR-050 – FR-052, FR-029 | SC-001, SC-008 |
| "inherits AetherReverb's gates (energy conservation in freeze ±0.5 dB/60 s …)" (429) | FR-046, FR-052 | SC-001 |
| "… no metallic ringing via echo-density metric" (429) | FR-015, FR-020 – FR-022 | SC-002 |
| "+ damper-motion smoothness test" (430) | FR-033 | SC-003 |
| "CPU ≤ 5% global" (430) | FR-081, FR-082 | SC-009 |
| Open Question 3 — "configuration layer vs separate L4 effect" (569–570) | Architecture ruling; FR-001, FR-002, FR-040–FR-047 | SC-012 (decided; approved by the user — see Clarifications) |
| Cross-cutting: RT safety (539–540) | FR-003, FR-070 | SC-010 |
| Cross-cutting: boundedness is the theme-level FR (541–544) | FR-028 (`kEarlyGainSum`), FR-045, FR-048, FR-064 | SC-014 |
| Cross-cutting: layer discipline + ODR sweep (545) | FR-001, New components table | FR-080 gates |
| Cross-cutting: CPU budgets are FRs, measured in tests (546) | FR-081 | SC-009 |
| Cross-cutting: Dormancy — chain skipped at zero gain, generators keep running, 50 ms re-entry fade (547–555) | FR-036 (generators), **FR-065** (chain skip / stated justification / re-entry fade) | SC-001, SC-003 (d), SC-005 |
| Cross-cutting: no bit-exact float goldens (556) | FR-080 | SC-011, SC-012 (a) |
| Cross-cutting: portability (557–558) | FR-071, FR-072 | FR-080 gates |
| Cross-cutting: shared-component changes keep Seraphis green (560–562) | FR-042, FR-044, FR-047, FR-083 | SC-012 |
| (derived) Constants `CavernVerb` mirrors from the shared header must not drift | FR-084 | SC-016 |
| (derived) Pinned default control values, so "at defaults" is unambiguous | FR-066 | SC-007 (a), (b) |

---

## Assumptions

1. **`AetherReverb` is the intended core and needs no re-validation.** Roadmap lines 137–139 state it
   ships and is the shared space-engine core; its five test TUs are in the effects target
   (`dsp/tests/CMakeLists.txt:528-532`). This spec re-states none of its requirements and adds no
   criterion that would re-test them, except where FR-046/SC-001 show a Phase-9 addition could break
   one.
2. **`N = 8` is the working configuration.** `AetherReverb` ships both orders (`:1564`, `:1567`) and
   defaults to 8 (`:1578`). `N = 16` appears only in SC-009 arm (b) as the worst case, as Seraphis
   Phase 6's own criteria do.
3. **One `CavernVerb` per engine, global, post-voice-sum.** Roadmap line 449 places the space engine
   in the global chain; the budget at line 430 is global; the shipped `AetherReverb` budget note says
   the figure "does NOT multiply by polyphony" (`aether_reverb_perf_test.cpp:49-53`). Phase 10 owns
   the instancing decision if it ever changes (roadmap Open Question 5, line 573, covers voice count
   and per-voice vs global for ghost/atmosphere, not for the space engine).
4. **The ER is stereo in / stereo out with a fixed L/R tap assignment**, not a panner. Stereo width
   of the late field is `AetherReverb`'s `setWidth`; the ER's width is its tap geometry.
5. **No Vorago plugin, parameter ID, preset or UI work is in scope.** Phases 11–14 (roadmap
   lines 465–514).

---

## Review notes

**No issue from the fidelity/testability/reality review was rejected.** All blockers and majors are
applied in full and every minor was applied. Three issues offered an either/or; this section records
which branch was taken and why, so a later reader does not have to reconstruct it, and two places
where the fix went slightly beyond the suggestion.

1. **ER → FDN send (FR-026 vs FR-061; SC-006 (a)/(c)).** Taken: **`setEarlySend` added as the
   seventeenth control**, rather than deleting FR-026's "or a control" and finding another route to
   ER isolation. The alternative — a prepare-time "late field off" flag — would have put a
   test-only configuration into the product's lifecycle surface, and SC-006 would then be measuring a
   state no player can reach. A control is a real product state, and it earns its keep musically
   (ER audible without exciting the late field, and the reverse). D-5, FR-026, FR-061, SC-006,
   SC-009 arm (b) and SC-013 render (ii) were all updated together, and SC-006 gained a new
   clause (e) so a send that silently does nothing cannot make clauses (a) and (c) pass.

2. **SC-007 (c) "size biased huge".** Taken: **all three** of the offered fixes, because each closes
   a different hole — `kCavernSizeFloor` raised to ≥ 0.55 (nominal ratio 4.59, not 4.00), the bare
   comparison instance seeded with the *same* derived seed the owned engine gets so the two jitter
   realisations cancel, and breath zeroed on both sides. The threshold moved 4.0 → **4.4**, which is
   a *tightening* relative to the geometry the spec now mandates, not a relaxation: at the old
   admissible floor of exactly 0.5 the nominal ratio was exactly 4.0, so the old "≥ 4×" was a coin
   flip; at the new floor it has ~4 % of margin. `setModDepth` was **not** added to the control
   surface to make the measurement easier.

3. **SC-014 soak.** Taken: **continuous G-2 for the full 30 minutes** (the first offered branch), and
   the second branch kept as well, as a new clause (d) — a 30-minute *frozen* soak asserting SC-001's
   ±0.5 dB conservation. The two test different failures: clause (b) catches an unfrozen engine that
   drifts under excitation, clause (d) catches a frozen one that dies or grows overnight, which is
   the roadmap's actual words (lines 541–544) and which no continuously-excited render can show. The
   peak bound is now the number 4.0 with its derivation written out, per FR-028's `kEarlyGainSum`.

Two further notes on scope. **SC-016 is new** — FR-084 was claiming compile-time protection for three
`private` `AetherReverb` constants that no `static_assert` outside the class can reach, so the
requirement was narrowed to the reachable public facts and the three private mirrors were given a
runtime criterion that probes them through public behaviour. And **FR-065 is new**: the Traceability
table discharged the roadmap's Dormancy rule (lines 547–555) with FR-036, which covers only the
"generators keep running" clause; the "chain is skipped at zero gain" and "50 ms re-entry fade"
clauses had no requirement, and the rule's explicit obligation — *"a spec that wants a silent slot to
keep burning its chain must say what the listener would hear that justifies it"* — was undischarged
for the `setMix == 0` case. FR-065 skips what can be skipped, states the justification for what
cannot, and SC-003 gained clause (d) to measure the re-entry.
