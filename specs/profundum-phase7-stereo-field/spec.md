# Feature Specification: Profundum Phase 7 — Bass Stereo Field

**Spec slug:** `profundum-phase7-stereo-field`
**Status:** Clarified (2026-10-18): Q1–Q8 ruled (see Clarifications). Every bar still marked *drafted* is a proposal
for the plan to harvest, not a ruling. Once a bar is ruled, the ruling goes into its FR / SC row first and then into
Clarifications.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 7** (lines 521–548). This spec also traces to:
- **Core Philosophy**, lines 45–48: "Mono core, stereo-capable image. Weight stays centred; harmonics above it may spread
  and move. Width is part of the generation architecture, not an effect bolted on at the end. Mono compatibility is
  measured, never assumed ('mono below 120 Hz' alone is rejected: crossovers shift phase and distortion creates
  harmonics on both sides of the boundary)."
- **Architecture Overview**, line 71 ("+ low-note guard, per-partial width") and line 96 ("STEREO FIELD SOLID · SPREAD ·
  WIDE + GRAVITY + MOTION", in the Global block).
- **Key Design Decision 2** (lines 125–131):
  - the Anchor is always centred;
  - the harmonic voice "may generate width (per-partial pan/phase offsets, L/R-different character processing)";
  - SPREAD is "zero = centred; rising = upper harmonics widen, lows stay put; crossover and curve exposed to advanced
    users";
  - there are three image modes;
  - "Mono-sum phase cancellation is checked by test at every stereo-touching stage."
  
  Line 136 adds: "Mono/stereo image and mono/poly are independent axes."
- **Reuse Inventory**, "Stereo" row (line 167). It lists `midside_processor.h`, `crossover_filter.h` (`CrossoverLR4`,
  flat-sum), `core/stereo_utils.h` and the bank's per-partial pan, and notes that `systems/stereo_field.h` is
  delay-flavoured. Verdict: "🆕 `BassStereoField` (L3): generation-domain width law + post safety stage. No Haas,
  rotation or frequency-dependent width exists." The ODR hazard list (line 175) names `StereoField`.
- **Phase 1** shipped the pan hook (line 249): "a per-partial pan/offset vector hook (consumed by Phase 7; identity in
  this phase)".
- **Phase 5 handoff** (lines 432–433): "the SC-013(b) offset ceilings (Rectify and Fracture at 0.10;
  evidence/sc013-offset-ceilings.txt) and the ±0.15 readings". Lines 450–451: "the stage is L/R-capable so Phase 7's WIDE
  mode can offset drive/tone per channel."
- **Phase 6 handoff** (lines 492–493): "nothing in the dynamics is stereo-aware beyond max(L, R) detection and one shared
  gain; SPREAD's side content passes the compressor unchanged."
- **Phase 8 consumer** (lines 562, 567, 573–574). `MorphState` holds "image mode / Spread / Gravity / Motion". Image mode
  is "stepped-with-crossfade", and the morph covers "mono → stereo (image mode stepped, Spread / Gravity / Motion
  continuous)".
- **Phase 9 consumer** (lines 607, 625). The engine chain is "voice sum → Mass dynamics → stereo field safety stage → DC
  safety → `TruePeakLimiter`", and Phase 9 runs a "mono-sum check on the full engine in every image mode".
- **Phase 10 / 11 consumer** (line 660): parameter IDs 800–899 hold the stereo field.
- **Phase 12 consumer** (lines 756–758, 764–765):
  - "no Advanced control may silently invalidate the stable, centred low end — a control that affects mono
    compatibility says so and its guarantee is the one Phase 7 measures";
  - "Gravity is the measured stereo-field law, … not a safety switch."
- **Cross-Cutting Constraints** (lines 912–935):
  - RT safety;
  - the audibility, mono-compatibility and anti-aliasing theme FRs;
  - "CPU budgets are FRs … global 6–7" (line 924);
  - close lanes scoped to the change set (lines 925–927);
  - no bit-exact float goldens;
  - portability;
  - shared-component changes are append-only (lines 934–935).
- **Open Question 6** (lines 949–950): "Gravity law — crossover frequency range, width curve family, and whether the
  post safety stage is minimum-phase (`CrossoverLR4`) or linear-phase — Phase 7, after mono-sum measurement."

**Upstream contracts consumed (ratified in earlier phases).**
- **Phase 1 FR-048** (`specs/profundum-phase1-harmonic-core/spec.md:454–468`).
  - `setPartialPanOffsets` forwards a non-zero vector once per control interval through `applyPanOffsets`.
  - An all-zero vector never reaches the bank. The return to all zeros restores the exact centre tables (FR-064), "so
    L == R bit-identically again from the next sample".
  - Clause (d): "per-partial inter-channel phase offsets / decorrelation (line 394) are deferred to Phase 7".
  - Last line: "Pan-offset changes are Phase 7's to smooth."
- **Phase 3 FR-023** (`specs/profundum-phase3-anchor/spec.md:315–321`) and its consumers. The voice feeds the Anchor
  a₁ = `deliveredGains()[0] · kCenterPanGain` (`dsp/tests/unit/systems/profundum_anchor_chain_test.cpp:578, 745`). That
  feed assumes h1 sits at the centre pan gain.
- **Phase 5 FR-027 / SC-013(b)** (`specs/profundum-phase5-shape-character/spec.md:375–377, 635`). These define the
  character stage's per-channel offsets:
  - gated at ±0.15 for Tube, Fold, Bite and Crush;
  - capped at a per-algorithm ceiling of **0.10** for Rectify and Fracture
    (`specs/profundum-phase5-shape-character/evidence/sc013-offset-ceilings.txt`). At 0.10 the (L + R)/2-versus-mean-L/R
    reading below 2·f₀ is −0.56 … −0.61 dB. At 0.15 it is −1.10 … −1.46 dB, outside the 1 dB tolerance.
  
  Phase 5's Non-Goals table (`spec.md:133`) assigns "WIDE's per-channel offset values, the mono-sum tolerance of the
  whole field and the Gravity safety stage" to this phase.

**Concept source:** `specs/Profundum-concept.md`.
- **§8 "Stereo should be frequency-dependent"** (lines 390–418):
  - "Rather than a conventional stereo knob: SPREAD";
  - the 20 Hz – 10 kHz MONO ←→ WIDE strip (lines 398–402);
  - "The low frequencies are permanently constrained toward mono, while upper harmonics can become increasingly stereo"
    (404);
  - "MOTION — The stereo field can slowly rotate / oscillate" (408–410);
  - "GRAVITY — which determines how aggressively the bass collapses toward mono as frequency decreases. That gives you
    Reese-like width without allowing the actual sub to become stupidly unstable" (414–418).
- **§13** (lines 600–627) orders the chain CHARACTER → COMPRESSOR → STEREO FIELD → LIMIT.

**Layer.** The roadmap names one component: `BassStereoField` (L3, `systems/bass_stereo_field.h`), header-only in
`Krate::DSP`. The name passes the ODR sweep (see "New components").

---

## Overview

Phase 7 builds Profundum's stereo image. The image comes from the generator, not from an effect at the end of the chain.
`BassStereoField` has two halves.

1. **The generation-domain width law** runs per voice.
   - It maps the field controls, the field's global control-rate state and the voice's f₀ to a per-partial pan vector,
     with the per-voice slew memory in a caller-owned `VoiceState` (FR-001). The vector is pan only (FR-014). The voice hands it to
     `ProfundumCore::setPartialPanOffsets` (the Phase 1 hook).
   - A partial's width depends on its frequency (k + 1)·f₀, on the crossover and on the width curve. GRAVITY sets how
     steeply width collapses below the crossover. SPREAD 0 centres everything.
   - The law also yields WIDE's per-channel character offsets, and it carries MOTION, the slow bounded movement of the
     pan vector.
2. **The post-sum safety stage** runs once, globally, after the Mass dynamics.
   - Distortion makes new harmonics on both sides of any boundary. A voice that is centred below the crossover at the
     generator can therefore still leave the Fold, the excitation or the character stage with side content down there.
   - The stage catches that content. It is an M/S encode, a minimum-phase `CrossoverLR4` split of the Side signal only at
     f_s = 2·f_c (FR-022, FR-024), side attenuation below f_s scaled by GRAVITY with a non-zero floor at Gravity 0
     (FR-021), and the decode. Mid is untouched and the stage has zero latency.

Three image modes select the behaviour:

| Mode | Generation law | WIDE character offsets | Safety stage | Output |
|---|---|---|---|---|
| **SOLID** | all-zero vector (exact centre tables) | 0 | side forced to exactly 0 | L == R, bit-identical |
| **SPREAD** (default) | frequency-dependent width | 0 | GRAVITY-scaled side attenuation below the stage crossover (non-zero floor at Gravity 0) | centred low end, stereo harmonics |
| **WIDE** | identical to SPREAD (FR-044) | per-channel Amount / Tone offsets = ceiling · Spread · harvested constant, within the Phase 5 ceilings | as SPREAD | as SPREAD, plus L/R-different character |

The Anchor never enters the width law, the offsets or MOTION. It is mono by construction
(`systems/anchor_oscillator.h:341–343`). This phase proves that it stays bit-identical L/R through the safety stage in
every mode.

## Scope

1. `BassStereoField` (L3), complete. It covers:
   - the control aggregate: image mode, Spread, Gravity, crossover (60–250 Hz), continuous width curve, MOTION depth and
     rate (FR-001–FR-004);
   - the generation-domain width law, with the h1 rule, the direction pattern, slew and the cap rules (FR-010–FR-019);
   - the post-sum safety stage (FR-020–FR-028);
   - MOTION (FR-030–FR-036);
   - WIDE's per-channel character offsets (FR-040–FR-044);
   - image-mode transitions (FR-050–FR-054);
   - the Anchor exclusion (FR-060–FR-062).
2. A validation-chain test fixture covering every stage that touches stereo, at voice level and engine level: core
   (+ excitation) with the pan vector → `ShapeMorph` → `HarmonicFold` → `BassCharacterStage` (with WIDE offsets) →
   + Anchor → `MassDynamics` → `BassStereoField` safety stage → `DCBlocker` ×2 → `TruePeakLimiter` (FR-070).
3. The bars the roadmap leaves to this spec:
   - the mono-sum tolerance below the crossover (SC-001);
   - the dB figure by which the safety stage attenuates distortion-born side content (SC-006);
   - the MOTION rate and depth bounds (SC-008);
   - Open Question 6, the Gravity law (OQ-6), ruled 2026-10-18: crossover 60–250 Hz, raised-cosine continuous curve,
     minimum-phase side-only topology (a) with the stage split at 2·f_c.
4. The decision Phase 1 deferred here (OQ-7, from Phase 1 FR-048(d)), ruled 2026-10-18: the width vector is **pan only**.
   There are no per-partial inter-channel phase offsets in this phase (FR-014, FR-073, SC-019).
5. Tests for:
   - the per-band mono sum, the sub-band correlation, width monotonicity in frequency and Spread, and SOLID
     bit-identity;
   - the safety stage's attenuation and its phase behaviour;
   - MOTION bounds, freedom from zipper and clicks, and mode transitions;
   - audibility, Anchor bit-identity, and anti-aliasing of the WIDE chain;
   - RT safety, non-finite safety, determinism, partition invariance, sample rates and CPU.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| Wiring the field into `ProfundumVoice` / `ProfundumEngine`. This covers calling the law per voice, the POLY sum into the safety stage, the engine chain order, and the full-engine mono-sum check in every image mode | Phase 9 | 600–607, 625 |
| The Motion section (ENV / CYCLE / DRIFT → `VoiceModRouter`, Motion Depth) and the Personality macros. Field MOTION here is the field's own bounded movement, not the modulation bus | Phase 9 | 609–615 |
| Image mode / Spread / Gravity / Motion as `MorphState` fields, and their interpolation laws | Phase 8 | 562, 567, 573–574 |
| Parameter IDs 800–899, names, normalisation and state | Phases 10, 11 | 660 |
| The image-mode selector, the SPREAD mini-spectrum, and the labels that state each control's mono guarantee | Phase 12 | 734–735, 756–758 |
| Haas delays, unison, chorus-style decorrelation, an M/S width knob on the whole signal, or a "low-end protection" switch | Never. Width is generated, not added at the end (lines 45–47, 524–525). The Anchor gets "no stereo modulation, unison or phase tricks" (126). The protection switch is "Not adopted" (764–765) | — |
| A user-facing quality switch for the safety stage, a linear-phase crossover, a second safety-stage topology, or a stage that adds latency | Never. OQ-6 ruled minimum-phase topology (a) with zero latency (lines 762–763 reject a switch that "adds latency") | — |
| Per-partial inter-channel phase offsets, and any change to `HarmonicOscillatorBank`, `ProfundumCore`, `BassCharacterStage`, `CrossoverLR4` or `MidSideProcessor` | None. OQ-7 ruled pan only (FR-014, FR-073) | 934–935 |

---

## Existing components (verified this session, 2026-10-18)

Paths are relative to `dsp/include/krate/dsp/` unless stated. Every row was opened and read this session.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `HarmonicOscillatorBank` | `processors/harmonic_oscillator_bank.h:75` | The pan tables the width vector writes (through the core) | Layer 2. `kMaxPartials = 96` (`processors/harmonic_types.h:21`).<br>• `void applyPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept` (647–659). For **every** one of the 96 partials it computes `newPos = std::clamp(panPosition_[i] + offsets[i], -1.0f, 1.0f)`, `angle = kQuarterPi + newPos * kQuarterPi`, `panLeft_[i] = std::cos(angle)`, `panRight_[i] = std::sin(angle)`. The new tables take effect at once: there is **no pan smoothing**.<br>• `void restoreCenterPan() noexcept` (708–711) refills both tables with `kCenterPanGain = 0.7071067811865476f` (107).<br>• `setStereoSpread(float)` (545) is the bank's own law: odd harmonics left, even right, with h1 scaled by `kFundamentalSpreadScale = 0.25f` (104, 1155–1162). It is not frequency-dependent, and `ProfundumCore` never calls it (grep of `systems/*.h`). `panPosition_` therefore stays 0 (reset, 227), and the core's offsets are **absolute** pan positions.<br>• The render sums `ampSample * panLeft_[i]` and `ampSample * panRight_[i]` from one oscillator state per partial (776–777, 809–810; the SIMD kernel's contract, `harmonic_oscillator_bank_simd.h:26–29`). **L and R of a partial share one phase.** The bank can pan a partial, but it cannot offset the partial's inter-channel phase. |
| `ProfundumCore` | `systems/profundum_core.h:92` | The pan hook; the generator | Layer 3. `kDefaultPartials = 64` (96), `kControlInterval = 32` (97), `kPitchUpdateInterval = 16` (98).<br>• `void setPartialPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept` (218–235). A non-finite element becomes 0.0f. An unchanged vector (bit compare) sets nothing dirty. A changed vector is forwarded at the next control update or note-on by `forwardPan()` (614–625). That calls `bank_.applyPanOffsets` for a non-zero vector and `bank_.restoreCenterPan()` on the return to all zeros.<br>• Partial k is harmonic k + 1 (`p.harmonicIndex = static_cast<int>(k + 1)`, 409).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (237–238).<br>• The edge excitation runs **per channel** when `right != left` (`exciteRun`, 1077–1099). Panned partials therefore leave the excitation with L/R-different nonlinear products.<br>• `deliveredGains()` (298). |
| `CrossoverLR4`, `CrossoverLR4Outputs` | `processors/crossover_filter.h:96, 51` | The band split of the safety stage | Layer 2 (line 2). **One channel per instance.** It cascades two Butterworth LP biquads and two HP biquads (`updateCoefficients`, 356–366; members 373–376), so it is a minimum-phase IIR. "Outputs sum to flat frequency response" (line 5): low + high is an all-pass, flat in magnitude but not in phase.<br>• Constants: `kMinFrequency = 20.0f`, `kMaxFrequencyRatio = 0.45f`, `kDefaultSmoothingMs = 5.0f`, `kDefaultFrequency = 1000.0f` (102–105); `kHysteresisThreshold = 0.1f` Hz (106).<br>• `void prepare(double sampleRate) noexcept` is documented "NOT real-time safe" (169).<br>• `void reset() noexcept` (200). `void setCrossoverFrequency(float hz) noexcept` (218) is smoothed and stored atomically. `void setSmoothingTime(float ms) noexcept` (231). `void setTrackingMode(TrackingMode) noexcept` (245), with `TrackingMode { Efficient, HighAccuracy }` (41–44).<br>• `[[nodiscard]] CrossoverLR4Outputs process(float input) noexcept` (289). Before `prepare` it returns {0, 0}.<br>• `void processBlock(const float* input, float* low, float* high, size_t numSamples) noexcept` (335). |
| `MidSideProcessor` | `processors/midside_processor.h:59` | The side attenuation (roadmap line 535) | Layer 2. Mid = (L + R)/2 and Side = (L − R)/2; L = Mid + Side and R = Mid − Side (process, 183–230).<br>• Width 0…200 % (65–67) and mid / side gain −96…+24 dB (69–71), all smoothed by `OnePoleSmoother` at `kDefaultSmoothingMs = 10.0f` (73).<br>• `void prepare(float sampleRate, size_t maxBlockSize) noexcept` (96; the rate is a **float**). `reset()` (114), `setWidth(float widthPercent)` (133), `setMidGain(float gainDb)` (141), `setSideGain(float gainDb)` (149).<br>• `void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples) noexcept` (183). It is **broadband**: it encodes and decodes in one call, with no band split. With L == R input, side is exactly 0 and every output equals the input (mid = (x + x)·0.5 = x). |
| `stereoCrossBlend` | `core/stereo_utils.h:41` | **Not composed** | Layer 0. `constexpr void stereoCrossBlend(float inL, float inR, float crossAmount, float& outL, float& outR) noexcept` is a cross-feed blend for feedback networks (15–51). It is not a width law. |
| `StereoField`, `StereoMode` | `systems/stereo_field.h:73, 47` | **Not composed. ODR hazard** (the names are taken) | Layer 3, "High-level stereo processing modes for delay effects" (line 4). `enum class StereoMode : uint8_t { Mono, Stereo, PingPong, DualMono, MidSide }` (47–53). Test TU `dsp/tests/unit/systems/stereo_field_test.cpp`, registered at `dsp/tests/CMakeLists.txt:382`. |
| `BrownianDrift` | `processors/brownian_drift.h:94` | MOTION's slow random source (roadmap line 539) | Layer 2, `class BrownianDrift : public ModulationSource`. A discrete Ornstein–Uhlenbeck walk at control rate `kControlRateInterval = 32` (105), smoothed by a 150 ms one-pole (`kDriftOutputSmoothMs`, 103). τ runs from `kTauMin = 0.2f` to `kTauMax = 30.0f` s (97–99). `kDefaultDriftSeed = 0xB17Eu` (109).<br>• `void prepare(double sampleRate) noexcept` (121). `void reset() noexcept` (133: "the same seed re-renders identically"). `setSeed(std::uint32_t)` (145), `setSmoothness(float)` (152), `setDepth(float)` (159), `setMean(float)` (165).<br>• `void process() noexcept` (178), `void processBlock(size_t numSamples) noexcept` (194).<br>• `getCurrentValue()` clamps to [−1, 1] (212–214); `getSourceRange()` is {−1, 1} (217–219). |
| `LFO` | `primitives/lfo.h:63` | MOTION's periodic, tempo-syncable source (roadmap line 539) | Layer 1. `enum class Waveform : uint8_t { Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom }` (47–54) sits at **namespace scope**, so the new class's nested names must not collide with it.<br>• `kMinFrequency = 0.01f` and `kMaxFrequency = 20.0f` Hz (34–35).<br>• `void prepare(double sampleRate) noexcept` (83). `void reset() noexcept` (95) uses the deterministic random seed 12345. `[[nodiscard]] float process() noexcept` (114). `setWaveform` (189), `setFrequency(float hz)` (218), `setPhaseOffset(float degrees)` (226), `setTempoSync(bool)` (237), `setTempo(float bpm)` (246), `setNoteValue(NoteValue, NoteModifier)` (255).<br>• `class LFO {` has **no base class** (63). It is not a `ModulationSource`, although `core/modulation_source.h:28` lists it as a known implementation. |
| `ModulationSource` | `core/modulation_source.h:31` | The concept any MOTION value exposed to Phase 9 conforms to | `virtual float getCurrentValue() const noexcept = 0` (37) and `getSourceRange()` (39–41). |
| `BassCharacterStage` | `systems/bass_character_stage.h:76` | WIDE's per-channel offsets | Layer 3. `enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture }` (78).<br>• `kParamSmoothMs = 10.0f` ("Amount, Tone, offsets", 84). `kMaxChannelOffset = 0.15f` ("D-12 (gated value; the setter clamps to [-1, 1])", 85).<br>• `void setChannelOffsets(float amountOffset, float toneOffset) noexcept` (273–282): "L = centre + offset, R = centre - offset, each clamped to [0, 1]. Offsets in [-1, 1]. A nonzero offset unlinks the channels (they stay unlinked until reset()). Never moves r" (271–272).<br>• `reset()` "Re-links the channels when the offsets are 0" (204–234; `linked_ = offsetsZero()` at 230).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (334). `latencySamples() == 0` (504). |
| `HarmonicFold` | `processors/harmonic_fold.h:76` | Chain fixture: the main source of distortion-born side content | Layer 2. It has per-channel windowed-peak detectors (lines 14–15, "ratified per-channel"). A mono link runs channel 0 alone while the inputs are bit-identical, and unlinks "at the first differing sample" (199–208). `prepare(double, std::size_t)` (121), `setAmount(float)` 0–100 (168), `processBlock(float* left, float* right, std::size_t)` (188). |
| `ShapeMorph` | `systems/shape_morph.h:98` | Chain fixture | Layer 3. `prepare(double, std::size_t)` (135), `setShape(float)` (194), `processBlock(float* left, float* right, std::size_t, …)` (221). |
| `AnchorOscillator` | `systems/anchor_oscillator.h:55` | Chain fixture; the Anchor bit-identity gate | Layer 3. `processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, …)` (285). It writes one value to both channels, `left[i] = y; if (right != left) right[i] = y;`, commented "FR-050: one value, both channels" (341–343). `kAnchorToH1 = 1.0f` (70). |
| `MassDynamics`, `BassCompressor` | `systems/mass_dynamics.h:469, 58` | Chain fixture: the stage before the safety stage (roadmap line 607) | Layer 3. `kControlInterval = 32` (471). `prepare(double sampleRate, std::size_t maxBlockSize)` (482), `processBlock(float* left, float* right, std::size_t)` (539). The compressor detects `std::max(std::abs(xL), std::abs(xR))` (224) and applies one gain to both channels. |
| `DCBlocker`, `TruePeakLimiter` | `primitives/dc_blocker.h:135, 228`; `processors/true_peak_limiter.h:60, 105` | Chain fixture: the output stages after the safety stage | `DCBlocker::prepare(double sampleRate, float cutoffHz = 10.0f)` (135), `processBlock(float* buffer, size_t)` (228). `TruePeakLimiter::prepare(double, std::size_t)` (60), `setCeilingDb(float)` (86), `processBlock(float* left, float* right, int numSamples)` (105). |
| `OnePoleSmoother`, `LinearRamp`, `SlewLimiter` | `primitives/smoother.h:134, 305, 468` | Slew of the width vector, of the side gain and of the mode crossfade | Signatures as verified in Phase 6 (`specs/profundum-phase6-mass-dynamics/spec.md:156`). The plan re-reads the lines it uses. |
| `detail::isFinite` | `core/db_utils.h:118` | Non-finite guards that survive `-ffast-math` | Used the same way `ProfundumCore` uses it (profundum_core.h:19, 223). |

**Test helpers (verified).**
- `dsp/tests/unit/systems/profundum_core_test_helpers.h`:
  - `requireLREqual` (61), `renderCore` (90), `rmsDbOverPeriods` (126), `goertzelPhase` (157), `bh7PowerSpectrum` (206);
  - `renderedHarmonicPowers` (235), `describeRendered` (251), `aliasedPower` (273), `rmsDbfs` (453);
  - `sidebandPower` (323), the Phase 1 zipper / sideband measure.
- `dsp/tests/unit/systems/mass_dynamics_test_helpers.h`: `ChainConfig` (173), `ChainTaps` (195), `renderChain` (372),
  `renderPoly` (454), `SubBand` (545). The Phase 7 chain fixture extends this one rather than writing a second chain.
- `tests/test_helpers/render_fingerprint.h`: `fingerprintRender` (73), `compareFingerprints` (122).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope` (111).

**Test registration (verified).** `dsp/tests/CMakeLists.txt` lists TUs explicitly.
- The systems list holds `stereo_field_test.cpp` (382), `bass_character_stage_test.cpp` (712) and the Mass dynamics TUs
  (734–742). The `-fno-fast-math` block lists the non-finite TUs (1236–1237).
- The Phase 7 TUs register under `dsp_systems_tests`, with the non-finite TU in the `-fno-fast-math` block. Every Phase 7
  case carries the `[profundum]` tag.

### Reuse-inventory corrections (found while verifying)

- **The bank pans; it does not decorrelate.** The roadmap's "per-partial pan/decorrelation vector for the core bank
  (Phase 1 hook)" (line 532) and KDD 2's "pan/phase offsets" (line 127) both assume more than the hook does.
  - Each partial is one oscillator whose output is multiplied by `panLeft_` and `panRight_` (bank:776–777). A single
    panned partial is therefore perfectly correlated between L and R: correlation +1 at any pan.
  - Pan alone decorrelates only through *neighbouring partials panned to opposite sides*.
  - True inter-channel phase offsets would need a new append-only bank hook. Phase 1 deferred exactly this choice here
    (FR-048(d)); OQ-7 is ruled **pan only**: no hook, no phase offsets (FR-014, FR-073).
- **The bank does not smooth pan changes.** `applyPanOffsets` writes the new tables at once (bank:647–659), and the core
  forwards a changed vector once per 32-sample control interval (profundum_core.h:614–625). A moving vector is therefore
  a staircase of gain steps on every partial. Phase 1 FR-048 left the smoothing to this phase, and FR-015 owns it.
- **`applyPanOffsets` costs 96 `cos` + 96 `sin` per forwarded change.** It loops over all `kMaxPartials`, not the active
  count (bank:651–658). A vector that moves every control interval (MOTION, a Drop, a Spread sweep) pays that cost per
  voice per interval. SC-013(b) gates it, and FR-016 bounds how often the vector changes.
- **`MidSideProcessor` is broadband** (midside_processor.h:183–230). It cannot attenuate side content "below the
  crossover" by itself. The band split comes from `CrossoverLR4`, which handles one channel per instance. The topology
  that combines them is ruled: FR-022 topology (a), side-only split.
- **No linear-phase crossover exists in KrateDSP.** A grep for "linear.phase" under `dsp/include/krate/dsp` finds only
  the oversampler's FIR mode and unrelated processors (`oversampler.h`, `multistage_env_filter.h`,
  `phase_distortion_oscillator.h`, `spectral_distortion.h`). The linear-phase branch of OQ-6 would have been new code and
  would have added latency; it is ruled out (FR-022).
- **`LFO` is not a `ModulationSource`.** lfo.h:63 declares no base class, although modulation_source.h:28 says
  otherwise. If Phase 9 must be able to route MOTION's periodic source as a `ModulationSource`, the field exposes its own
  MOTION value through that concept (FR-034) rather than relying on `LFO`.

---

## New components

ODR sweep, run 2026-10-18: `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/ tools/` and
`find dsp plugins tests tools -iname "*bass_stereo*" -o -iname "*stereo_field*"`.

| Class / type | Layer | Header | ODR sweep result |
|---|---|---|---|
| `BassStereoField`, with nested `ImageMode`, `Controls` and helper types | 3 | `systems/bass_stereo_field.h` | **0 hits.** No `*bass_stereo*` file exists. The only `*stereo_field*` files are `systems/stereo_field.h` and its test `dsp/tests/unit/systems/stereo_field_test.cpp`.<br>Taken near names, none reused: `StereoField` (`systems/stereo_field.h:73`) and `StereoMode` (`systems/stereo_field.h:47`).<br>Names swept and free (0 hits each): `ImageMode`, `StereoImageMode`, `WidthLaw`, `PartialWidthLaw`, `WidthCurve`, `GravityLaw`, `StereoSafetyStage`, `BassStereoSafety`, `FieldMotion`, `StereoMotion`, `MotionSource`.<br>Nested types stay inside `BassStereoField`, so `BassStereoField` is the only new namespace-scope name. If the plan needs another namespace-scope name, it sweeps and records that name first. |

**Layer discipline.**
- `BassStereoField` includes L0 (`db_utils.h`), L1 (`smoother.h`, `lfo.h`) and L2 (`crossover_filter.h`,
  `midside_processor.h`, `brownian_drift.h`, and `harmonic_types.h` for `kMaxPartials`).
- It may include L3 peers (`profundum_core.h`, `bass_character_stage.h`), but only for their constants and the
  `Algorithm` enum. It never includes an L4 header.
- The field never calls the core or the character stage itself. It returns values, and the voice (Phase 9) or the test
  fixture passes them to those components' setters.

---

## Functional Requirements

### Descriptor and fixture definitions (used by FRs and SCs)

All descriptors are computed offline in double precision, over whole periods of f₀, from the rendered L and R.
- **Mid and side.** M = (L + R)/2 and S = (L − R)/2, computed in double.
- **Harmonic-resolved powers.** wM_n and wS_n are the powers of harmonic n in M and in S, from `renderedHarmonicPowers`
  (profundum_core_test_helpers.h:235). wL_n and wR_n are the same on L and R.
- **Width of harmonic n.** W_n = wS_n / wM_n in dB, floored at −120 dB.
  - For a constant-power pan at position p (bank:1167–1170), W_n = 20·log₁₀|tan(π·p/4)|.
  - A centred partial has W_n at the floor.
- **Band.** A band is the set of harmonics n with n·f₀ inside [f_lo, f_hi). The **sub band** is every harmonic below the
  crossover f_c in force.
- **Sub-band correlation ρ_sub.** The zero-lag Pearson correlation of L and R, ρ = Σ L·R / √(Σ L² · Σ R²), computed
  after an offline zero-phase FFT band mask to [0, f_c). The mask adds no phase of its own.
- **Band validity (ruled 2026-10-18, G8-1).** A band is read only when its taper-compensated mean energy is ≥ **−60 dBFS**
  (was −100 dBFS: residue at −80 … −100 dBFS, inaudible, was being read as anti-correlated). An unread band is
  neither gated nor counted.
- **Mono-sum ratio of a band.** Q_band = E[((L + R)/2)²] / ((E[L²] + E[R²])/2) in dB, over the band mask.
  - Q_band is 0 dB for a centred signal.
  - A constant-power pan of in-phase content can lower it by at most 3.01 dB (at hard pan). A reading below −3.01 dB can
    only come from phase cancellation.
  - This is Phase 5 SC-013(b)'s measure ("the energy of (L + R)/2 … within 1 dB of the mean L/R energy"), extended to a
    band.
- **Stage crossover.** f_s = kStageCrossoverMultiple · f_c,s with the named constant kStageCrossoverMultiple = 2.0
  (FR-024). The law uses f_c; the safety stage splits at f_s.
- **Distortion-born side content.** FR-012 lets c fall *toward* 0 below f_c; it does not require c = 0 there, at any
  Gravity. The SC-006 fixtures therefore impose the precondition explicitly, as a documented test configuration:
  - **Generator below f_c forced to centre.** The fixture takes the law's vector and sets every entry with f_k < f_c to
    0.0f before handing it to the core. Every side component below f_c at the safety-stage input is then born in a
    nonlinear stage (excitation, Fold, character, WIDE offsets), not in the generator.
  - **Generator Gravity held fixed.** The generator vector is evaluated on a second `BassStereoField` instance held at
    Gravity max, so a Gravity sweep of the stage under test does not change the stage's input render.
  - **One band below the crossover.** B = [20 Hz, f_c), through the offline zero-phase band mask; it is the whole
    sub-crossover range. D_side(B) is the side energy in B at the safety-stage input and D_side'(B) the same reading at
    its output, **on the same render**. The same readings are also taken per third-octave inside B.
  - The attenuation is A(B) = D_side(B) − D_side'(B) in dB: an input/output ratio on one render.
  - The plan names the fixtures that produce enough content in B; SC-006 lists the drafted ones.
- **Notes:** E0 = 20.60 Hz, E1 = 41.20 Hz, E2 = 82.41 Hz, C2 = 65.41 Hz, C3 = 130.81 Hz, C4 = 261.63 Hz.
- **Control interval.** "Control interval" means 32 samples (`ProfundumCore::kControlInterval`, profundum_core.h:97;
  `MassDynamics::kControlInterval`, mass_dynamics.h:471). It is phase-tracked across `processBlock` calls and never
  means one host block.
- **Fixtures.**
  - **Clean core:** `kSawAnchor` (the richest linear base) and `kGrowl` (presence-dominant), with the excitation off
    unless stated.
  - **Dirty arm:** core → `ShapeMorph` at SHAPE 1 (BITE) → `HarmonicFold` at 100 → `BassCharacterStage` at each
    algorithm, Amount 1.
  - **Anchor arm:** the dirty arm plus `AnchorOscillator` on Sine / LOCK at `kDefaultLevel`, with a₁ fed as in Phase 3.
    FR-061 keeps that feed valid.

### FR-001 series — `BassStereoField` interface and controls (roadmap lines 527–533, 539–541)

- **FR-001 Interface.** `BassStereoField` (L3, `systems/bass_stereo_field.h`) provides:
  - `prepare(double sampleRate, std::size_t maxBlockSize) noexcept`, which is not RT-safe and may size buffers, and
    `reset() noexcept`, which is RT-safe;
  - `setControls(const Controls&) noexcept`, RT-safe. Out-of-range values clamp; a non-finite value keeps the previous
    one;
  - the generation-domain query of FR-010, per voice. It is `const` on the field and keeps no state of its own. The
    per-voice memory it needs (the previous vector for FR-015's slew, the f_c it last used for SC-009(c), and the
    handoff counter of FR-016) lives in a nested `BassStereoField::VoiceState`, one per voice, owned by the caller (the
    Phase 9 voice, or the test fixture) and passed by reference. The plan states the exact signature; the drafted shape
    is `void computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out) const noexcept`.
    `VoiceState::reset()` serves FR-015's Reset exemption. Tests call this query, never a fixture-side slew;
  - the global control-rate state the law reads: the smoothed crossover f_c,s (FR-024, `smoothedCrossoverHz()`), the
    MOTION value (FR-034) and the mode-crossfade position (FR-052). It advances once per 32-sample control interval on
    the same absolute-sample grid as the core's (FR-074). The plan states the call order (a per-interval advance call,
    or inside `processBlock`) so that in each interval the voices' law queries and the safety stage read the same
    interval's values (SC-009(c), SC-012);
  - the WIDE offset query of FR-040 (pure, `const`);
  - `processBlock(float* left, float* right, std::size_t numSamples) noexcept`, in place, which runs the safety stage.
    `right == left` processes one channel, which has no side;
  - `latencySamples()`, which returns exactly 0 (the minimum-phase topology (a) of FR-022 adds no latency in any mode,
    at any setting; SC-005 gates it);
  - a host-tempo setter for Cycle's synced mode (FR-036).
- **FR-002 Controls.** `Controls` is a plain aggregate, because Phase 8 morphs it and Phase 11 registers it. Its fields:
  - `mode`: `ImageMode { Solid, Spread, Wide }`, default **Spread** (roadmap line 529);
  - `spread` in [0, 1], where 0 = everything centred (line 533);
  - `gravity` in [0, 1] (line 534);
  - `crossoverHz` (Advanced, line 533) in **[60, 250] Hz, default 120 Hz** (the law's f_c);
  - `curve` (Advanced, line 533), a **continuous shape parameter in [0, 1]** within ONE family (a raised cosine in
    log-frequency; FR-012), with Gravity as its slope. It is not an enumerated selector, so Phase 8 interpolates it
    linearly and Phase 11 registers it as a continuous parameter (FR-080);
  - `motionDepth` in [0, 1] and `motionRate` (lines 539–540), with bounds ruled by SC-008, plus the MOTION source
    selection of FR-031.
  
  The plan drafts the defaults and the clarify session ratifies them. Spread 0 in SPREAD or WIDE gives the centred image
  of FR-011.
- **FR-003 Validation.** A `static` validity check (`isValidControls`) accepts exactly the clamped ranges, so Phase 8's
  `isValidMorphState` can call it. It accepts every `crossoverHz` in [60, 250] and every `curve` in [0, 1], so a linear
  interpolation between two valid control sets is itself valid (SC-021).
- **FR-004 Independence from voice count.** Nothing in `Controls` depends on MONO / POLY (line 136). The per-voice law
  depends only on the controls, the field's global control-rate state, the voice's f₀ and that voice's `VoiceState`.
  The safety stage is one global instance.

### FR-010 series — The generation-domain width law (roadmap lines 531–534)

- **FR-010 Output.** The law runs for a voice at f₀, the core's resolved f₀ for the control interval (Drop and glide
  included).
  - It fills a `std::array<float, kMaxPartials>` pan vector p_k for k = 0 … N−1, where partial k sits at (k + 1)·f₀
    (profundum_core.h:409). Entries k ≥ N are 0.
  - The voice passes the vector to `ProfundumCore::setPartialPanOffsets`.
- **FR-011 Zero is centred (ruled 2026-10-18, C-15).** At Spread 0, and in SOLID at any Spread, the law's **target**
  vector is exactly `0.0f` in every entry at once; under FR-015's slew the **emitted** vector reaches bit-zero
  (`std::bit_cast<std::uint32_t>` = 0, so −0 fails) within ⌈1 / kMaxPanStepPerInterval⌉ = 334 control intervals of the
  switch and stays there, and a render started in SOLID or at Spread 0 emits bit-zero from the first query
  (`BassStereoField_SolidZeroVector`). Once the vector is all-zero the core either never calls `applyPanOffsets` or
  restores the exact centre tables (Phase 1 FR-048(a)/(c)), so the core output is L == R bit-identical (SC-004); the
  safety stage gives L == R from the end of its 10 ms crossfade regardless of the slew (FR-052).
- **FR-012 Width magnitude.** |p_k| = Spread · c(f_k; f_c, curve, gravity), with f_k = (k + 1)·f₀, at MOTION depth 0
  (FR-030 gives the moving form). The width curve c lies
  in [0, 1], and:
  - c is non-decreasing in f_k, so width is monotonic in frequency (line 546);
  - |p_k| is non-decreasing in Spread, so width is monotonic in Spread (line 546);
  - below f_c, c falls toward 0 as f_k falls, with a steepness that rises with Gravity (line 534: "the steepness of the
    generation-domain law");
  - **Family (ruled).** c is a raised cosine in log-frequency: c = 0 at and below a lower knee, 1 at and above f_c·(a
    fixed upper factor harvested by the plan), and a half-cosine ramp in log₂ f between them. The `curve` control in
    [0, 1] is a continuous shape parameter of this one family and Gravity is its slope (steeper collapse below f_c at
    higher Gravity). c is continuous in `curve`, Gravity and f_k, so a linear interpolation of the controls (Phase 8)
    moves the vector continuously. There is no second family and no family selector.
  - The family must make SC-001 and SC-002 hold at **every** Gravity (including Gravity 0, where the stage's floor of
    FR-021 applies), Spread, curve ∈ [0, 1] and crossover ∈ [60, 250] Hz. Phase 12 (lines 756–758) requires the
    guarantee across the whole Advanced range, not only at a default. The SC grids use the range extremes (crossover 60
    and 250 Hz, curve 0 and 1) plus the defaults.
- **FR-013 h1 is always centred (D-1, confirmed by the Q8 ruling).** p_0 = 0 at every note, mode and setting, even when f₀ is above f_c. The
  reasons:
  - KDD 2: "weight stays centred";
  - the Phase 3 a₁ feed assumes h1 at `kCenterPanGain` (FR-061);
  - the bank's own stereo law already treats h1 specially (`kFundamentalSpreadScale`, bank:104, 1159–1162).
- **FR-014 Direction pattern and pan-only vector (ruled, D-2 and OQ-7).** sign(p_k) alternates with harmonic parity: odd
  harmonics (k even) go to one side and even harmonics to the other. This is the bank's own convention (bank:1155–1156).
  - The pattern is fixed per partial index, so it tracks the keyboard: harmonic 4 is always harmonic 4 (roadmap lines
    38–39). It is deterministic: no seed, no random vector, and no state in `VoiceState` for it.
  - MOTION never swaps sides: sign(p_k(t)) is the same s_k at every t where p_k(t) ≠ 0 (FR-030, SC-008, SC-019).
  - **Pan only.** The width vector carries pan and nothing else. This phase adds no per-partial inter-channel phase
    offset, decorrelation or phase vector; L and R of every partial share one phase, as the bank renders them.
    Pan alone decorrelates the channels only through neighbours on opposite sides (see "Reuse-inventory
    corrections"). Width therefore comes from neighbouring harmonics panned to opposite sides plus WIDE's per-channel
    character offsets (FR-040). The mono-sum guarantee holds by construction: a constant-power pan of in-phase content
    cannot cancel (Q_band ≥ −3.01 dB, SC-001(b)). SC-019 gates both clauses.
- **FR-015 Slew.** Phase 1 FR-048 says "Pan-offset changes are Phase 7's to smooth."
  - Between consecutive control intervals, each p_k moves by at most Δp_max.
  - Δp_max is *drafted* as the step that keeps the per-interval pan-gain staircase at or below Phase 1's sideband bar:
    −60 dB re total (Phase 1 SC-010(b), measured with `sidebandPower`). It must hold at the fastest Spread automation, the
    fastest MOTION and a 48 st Drop.
  - The previous vector the slew starts from is held in the voice's `VoiceState` (FR-001), not in the fixture.
  - Exemption: a note-on with the Reset retrigger policy may land the vector at its target without slew. The bank's
    tables are restored at reset (profundum_core.h:552–553), so no audio carries the old pan.
- **FR-016 Update cadence.** The law is evaluated once per control interval per voice. The vector is handed to the core
  only when it changed; the core already ignores a bit-identical vector (profundum_core.h:222–234). A static note at
  constant controls with MOTION depth 0 forwards nothing once it settles.
  - The handoff count is a counter in `VoiceState`, incremented each time the query's output differs bitwise from the
    previous interval's, i.e. each time the caller hands a changed vector to `setPartialPanOffsets`. SC-013(c) reads
    it.
  - The bank's only pan counter, `panRecomputeCount_`, counts `setStereoSpread` calls (harmonic_oscillator_bank.h:570,
    getter 612), which the core never makes; `applyPanOffsets` and `restoreCenterPan` count nothing, and
    `ProfundumCore` exposes no forward counter. The reading therefore measures the handoff, not the core's
    `forwardPan()` (profundum_core.h:614–625).
- **FR-017 Caps.** The core does not render some partials: those at or above its count N, and those the Low Note Guard
  or the Nyquist cap attenuate to zero.
  - Their pan is inaudible, so they carry whatever value the law gives.
  - The law is still evaluated for every k < N, so a Drop that brings a partial back below Nyquist finds its pan already
    in place.
- **FR-018 Clamp.** Every |p_k| ≤ 1. The bank clamps to [−1, 1] anyway (bank:654), but the law never relies on that
  clamp.
- **FR-019 Excitation interaction.** With a non-zero vector, the core's edge excitation sees L ≠ R and runs per channel
  (profundum_core.h:1078, 1099). Side content born in the excitation is treated like any other distortion-born side
  content:
  - FR-012 keeps the generator centred below f_c;
  - the safety stage catches what the nonlinearity spreads below f_c (SC-006 includes an excitation arm).

### FR-020 series — GRAVITY and the post-sum safety stage (roadmap lines 534–538)

- **FR-020 One Gravity control, two effects.** Gravity sets both the generation law's steepness below f_c (FR-012) and
  the safety stage's side attenuation below the stage crossover f_s (FR-021). Raising Gravity never widens anything
  (SC-003(c)). Gravity at its minimum is **not** "stage off": the stage keeps a non-zero attenuation floor there (FR-021,
  SC-020), so the sub is never left unprotected.
- **FR-021 Side attenuation (ruled, Q4).** Below the stage crossover f_s (FR-024) the stage multiplies the side signal
  by g_side(Gravity) ∈ [0, 1), which is non-increasing in Gravity:
  - **Floor at Gravity 0.** g_side(0) = g_floor with 0 < g_floor < 1 (g_floor is a named constant, harvested inside the
    consuming test binary so that SC-001 and SC-002 hold on the dirty arm at Gravity 0, on all three arms and across the
    whole grid). The stage is never transparent below f_s. **Harvest (ruled 2026-10-18, G8-1):** g_floor is the largest
    candidate of {0.71, 0.5, 0.35, 0.25, 0.18, 0.125, 0.1, 0.071, 0.05, 0.035, 0.03} that passes every loud,
    non-exempt cell of every arm and the SC-010 Gravity lever; the escalation floor is 0.03 (−30 dB; was 0.1). K_wide is
    searched in {1, 0.75} on SC-007 aliasing and the SC-010 WIDE-versus-SPREAD lever (the lever already fails at 0.5);
    its mono-sum readings are recorded. The measured need at K_wide 0.5 was −7 … −13 dB for most cells and −16 … −24 dB
    for Crush / Rectify / Bite, so Gravity 0 sits near −20 … −26 dB below f_s.
  - **Full removal at Gravity max.** g_side(1) = 0 exactly: the side signal below f_s is removed, leaving S_high.
  - **Monotone between.** Higher Gravity deepens the attenuation monotonically (SC-020).
  - Above f_s the side signal passes (the LR4 transition band is a property of the split, not a gain). The mid signal is
    not attenuated in any band.
  - SOLID forces g_side = 0 in every band (FR-051).
- **FR-022 Topology (ruled, Q2): side-only split, minimum phase, zero latency.** The stage is composed from
  `CrossoverLR4` and the `MidSideProcessor` encode / decode (roadmap line 535) in exactly one topology:
  - Encode M/S. Split **only the Side signal** with one minimum-phase `CrossoverLR4`. Form
    S' = g_side·S_low + S_high, and decode with the **untouched** M.
  - The mono sum L + R = 2M is therefore bit-untouched by construction; only the side's phase moves (S_low + S_high is
    an all-pass of S, crossover_filter.h:5).
  - `latencySamples() == 0` (FR-001). There is no linear-phase crossover, no both-channel split and no second topology,
    and no user-facing switch between topologies.
  - SC-001, SC-005 and SC-006 are gated on this one topology.
- **FR-023 Phase behaviour is measured (not selecting).** The stage's effect on the phase of M and of S, relative to the
  input, is measured per frequency and recorded (SC-005). The topology is already fixed by FR-022; the record states what
  it does (M exactly unchanged, S's phase rotation and group delay at f_s).
- **FR-024 Crossover relation and changes (ruled, Q3).** The field owns the one smoothed law crossover f_c,s, advanced on
  its control grid (FR-001) and readable through `smoothedCrossoverHz()`.
  - **The stage splits at a fixed multiple above the law's f_c:** f_s = kStageCrossoverMultiple · f_c,s, with
    kStageCrossoverMultiple a named constant = 2.0. The law crossover f_c and the stage crossover f_s are deliberately
    different values. With f_s = 2·f_c, the LR4 high band is −24.6 dB at f = f_s/2 = f_c and lower below it, so one bar of
    ≥ 24 dB side attenuation holds over the whole band [20 Hz, f_c) at Gravity max (SC-006). f_s stays inside
    `CrossoverLR4`'s range: 2·250 Hz = 500 Hz ≤ 0.45·fs at every supported rate.
  - `CrossoverLR4`'s own smoother cannot be the shared value. `getCrossoverFrequency()` returns the **target**
    (crossover_filter.h:253–257; the atomic stored by `setCrossoverFrequency`, 217–221). The smoothed value lives only
    inside `frequencySmoother_` (`process()`, 295), and in `Efficient` mode the coefficients lag it by up to the
    0.1 Hz hysteresis (306–310).
  - Each control interval the field hands f_s = kStageCrossoverMultiple · f_c,s to the safety stage's `CrossoverLR4`
    through `setCrossoverFrequency`, with its own smoothing time reduced so that its smoother settles on the handed value
    (within 0.1 Hz) inside one control interval. **Ratified 2026-10-18 (C-18):** the stage's crossover keeps
    `TrackingMode::Efficient` (HighAccuracy recomputes only while the smoother is incomplete, so a step it snaps in one
    call never reaches the coefficients and a slow automation can drift the corner without bound); its smoothing time
    is the smallest the smoother accepts (0.1 ms, smoother.h:58); one control interval after a handoff the coefficient
    corner is within ε_s(fs) + 0.1 Hz of the handed value, ε_s(fs) being the smoother residual after 32 samples
    (≤ 0.091 Hz at 192 kHz), and within 0.1 Hz once settled (SC-009(c)).
  - The law reads f_c,s. SC-009(c) gates the relation f_stage = kStageCrossoverMultiple · f_c,law.
- **FR-025 Transparency.**
  - At g_side = 1 (a test-only setting; the product never reaches it, FR-021), the stage's M output is bit-identical to its
    input M.
  - **(ruled 2026-10-18, C-1)** In every mode and at every setting the Mid path is bit-untouched inside the stage (no
    filter, gain or smoother touches the encoded M), and the measured output mid (L_out + R_out)/2 equals
    (L_in + R_in)/2 within 2⁻²³ · (|M| + |S'|) per sample (the encode rounding plus the two decode roundings, about
    −138 dB); it is bit-identical wherever S' = 0 (SOLID once settled; an L == R input with zero side state).
    Bit-identity for a non-zero S' is not claimed: fl(M + S') + fl(M − S') ≠ 2M whenever a decode rounding is non-zero,
    in any topology (SC-005).
  - **(ruled 2026-10-18, C-2)** With L == R input, the output is L == R bit-identical, and in this topology it equals the
    input, from `reset()`, while the side path is idle (SOLID settled), or once the side crossover's state has decayed
    to exact zero after earlier L ≠ R content; the decay time to exact zero per crossover is recorded, not gated
    (SC-004(b)).
- **FR-026 Global placement.** One instance sits after the Mass dynamics and before the DC safety (roadmap line 607). It
  sees the POLY sum, and nothing in it is per voice.
- **FR-027 Side-gain smoothing.** Changes to g_side ramp. `MidSideProcessor` smooths its side gain at 10 ms
  (midside_processor.h:73, 149); the stage uses its own smoother of the same kind. A Gravity step is click-free
  (SC-009).
- **FR-028 Reset.** `reset()` clears the crossover states, snaps the smoothers to their targets, and returns MOTION to its
  seeded start (FR-033).

### FR-030 series — MOTION (roadmap lines 539–540)

- **FR-030 What moves.** MOTION moves the **generation-domain pan vector**.
  - m(t) ∈ [−1, 1] is the MOTION source and μ ∈ [0, 1] the depth. With s_k = sign(p_k) (FR-014) and the static
    magnitude P_k = Spread · c_k (FR-012), the moving vector is
    p_k(t) = s_k · P_k · (1 − κ·μ + κ·μ·m(t)),
    with the excursion constant κ = **0.5** (ruled, Q7). Depth moves the width INWARD from the static envelope, never outward.
  - The centre of the motion is p̄_k = s_k · P_k · (1 − κ·μ), and the excursion is |p_k(t) − p̄_k| ≤ κ·μ·P_k. At μ = 0
    the vector is FR-012's static vector.
  - **Boundary.** The motion stays inside the static envelope: |p_k(t)| ≤ P_k ≤ 1 at every t, and with κ ≤ 0.5 it never
    crosses the centre (1 − 2κμ ≥ 0). FR-018's clamp therefore never engages, and the excursion is never clipped
    one-sided. The cost is that depth pulls the time-averaged width in by up to κ·μ·P_k (the time-averaged width falls with
    depth; SC-008 records it); SC-003 reads monotonicity at μ = 0, and SC-001 / SC-002 run at μ ∈ {0, max}, where they
    hold trivially because no partial exceeds its static width or crosses centre.
  - **Sides never swap.** sign(p_k(t)) = s_k at every t where p_k(t) ≠ 0 (FR-014). MOTION never moves a partial through
    centre to the other side.
  - Partials with c_k = 0 never move, so nothing in the region the law keeps centred moves. h1 never moves (FR-013).
- **FR-031 Source (ruled, Q7: both ship).** The roadmap names "`BrownianDrift` or synced `LFO`" (line 539). The field owns one of
  each, and `Controls` selects one:
  - **Drift**: `BrownianDrift`, seeded;
  - **Cycle**: `LFO` Sine, free-running or tempo-synced through `setTempoSync` / `setNoteValue`. The synced case takes
    the host tempo through the field's setter (FR-036).
- **FR-032 M/S rotation is not used (drafted, D-4).** The roadmap's alternative, "M-S rotation" (line 540), rotates the
  post-sum signal.
  - Above f_c it trades mid for side, which changes the mono sum.
  - Below f_c it would move the sub unless it were band-limited.
  - The draft moves the pan vector only, so SC-001's mono-sum guarantee holds with MOTION at maximum.
- **FR-033 Determinism.** The Drift seed is fixed by default (`kDefaultDriftSeed`, brownian_drift.h:109, or a field
  constant), is settable, and is restored by `reset()`. `LFO::reset()` is deterministic (lfo.h:95–100). Two renders from
  reset are bit-identical (SC-012).
- **FR-034 Exposure.** The current MOTION value is readable (`motionValue()`, in [−1, 1]). The field can expose it as a
  `ModulationSource` (core/modulation_source.h:31–41), so Phase 9 may route it unchanged. The field routes nothing
  itself.
- **FR-035 Bounds.** Depth and rate are bounded by SC-008's ruled figures, because the roadmap requires MOTION to be
  "bounded and slow" (line 540).
  - The MOTION source is advanced once per control interval, the vector cadence of FR-016.
  - The vector's change per interval stays within FR-015's Δp_max.
- **FR-036 Host-tempo setter (ruled, Q7).** The field's API has a setter for the host tempo in BPM
  (`setTempoBpm(float)`, RT-safe; a non-finite or non-positive value keeps the previous one) that feeds Cycle's
  `LFO::setTempo`. The field never reads the host itself: Phase 9 wires the setter to the process context and Phase 11
  to the sync-mode and note-value parameters (FR-080). With sync on, the Cycle frequency follows the tempo and note
  value, bounded by FR-035 and SC-008 at the fastest synced value at 200 BPM (SC-008). **Octave fold (ruled 2026-10-18,
  C-9 option A):** the field passes the LFO a tempo halved by octaves, bpm_eff = bpm / 2^j with the smallest j ≥ 0 such
  that (bpm_eff / 60) / beats ≤ kCycleMaxHz (1 Hz), so every synced setting obeys the bar and cycles still land on bar
  lines; note values whose unfolded rate exceeds 1 Hz alias onto one folded rate (`kCycleSyncFold = true`); the unfolded
  and folded rate of every note value and modifier at 120 and 200 BPM is recorded in `evidence/sc008-sync-fold.csv`, and
  the aliasing is stated in the Phase 11 and Phase 12 handoffs.

### FR-040 series — WIDE per-channel character offsets (roadmap lines 530, 541–542)

- **FR-040 Output.** In WIDE the field yields (amountOffset, toneOffset) for the current
  `BassCharacterStage::Algorithm`. The voice passes them to `setChannelOffsets` (bass_character_stage.h:273). In SOLID and
  SPREAD both offsets are exactly 0. **Signs (ruled 2026-10-18, C-10):** amountOffset = +K and toneOffset = −K
  (K = ceiling · Spread · K_wide, FR-041), so one channel is hotter and darker and the other lighter and brighter.
  **Engagement ramp (ruled 2026-10-18, G7-2):** K is multiplied by a WIDE engagement scalar that `advanceControl()`
  slews toward 1 in WIDE and toward 0 in SOLID and SPREAD by kMaxPanStepPerInterval per control interval, landing
  exactly within 334 intervals (the FR-015 slew length, about 220 ms at 48 kHz). A SPREAD ↔ WIDE step is therefore as
  slow as a pan slew; the Mass dynamics' transient shaper no longer reads the offset onset as an attack (SC-009(b)).
  `wideOffsets` returns exactly {0, 0} while the scalar is 0.
- **FR-041 Ceilings (Phase 5 handoff).** |amountOffset| and |toneOffset| never exceed the per-algorithm ceiling:
  - **0.15** for Tube, Fold, Bite and Crush (`kMaxChannelOffset`, bass_character_stage.h:85);
  - **0.10** for Rectify and Fracture (sc013-offset-ceilings.txt).
  
  The offsets scale with Spread (ruled, Q6): offset = ceiling(algorithm) · Spread · K_wide, where K_wide is a named
  constant in (0, 1] harvested inside the consuming test binary. Because K_wide ≤ 1 and Spread ≤ 1, the Phase 5
  ceilings hold by construction. At Spread 0 both offsets are exactly 0 in every mode.
- **FR-044 WIDE differs from SPREAD only by the offsets (ruled, Q6).** The generation-domain width law (FR-010–FR-018,
  MOTION included) and the safety stage are identical in WIDE and SPREAD: the same vector for the same controls and f₀,
  bit for bit. WIDE's only additions are the per-channel character offsets of FR-040. Spread 0 in WIDE stays exactly
  centred (FR-011): the vector is all-zero and both offsets are 0.
- **FR-042 Algorithm switches.** When the algorithm changes, the offsets for the new algorithm take effect through the
  stage's own 10 ms offset smoothing (bass_character_stage.h:84). On a switch to Rectify or Fracture, an offset above 0.10
  is clamped to 0.10 before it is passed.
- **FR-043 Return to zero.** Leaving WIDE ramps both offsets to exactly 0 over the FR-015 slew length (334 control
  intervals; ruled 2026-10-18, G7-2), and the stage's 10 ms smoothing follows underneath. The character stage stays unlinked until its next
  `reset()` (bass_character_stage.h:272). With zero offsets its two channels compute from equal targets. SOLID
  bit-identity is gated after a reset or through FR-052.

### FR-050 series — Image modes and transitions (roadmap lines 529–530)

- **FR-050 Modes.** SOLID, SPREAD and WIDE behave as the Overview table states.
- **FR-051 SOLID.** The law returns the all-zero vector and the offsets are 0. The safety stage forces the side signal to
  exactly 0 in every band (zero, not merely attenuated), so its output is L = R = M. With every upstream stage at L == R,
  the whole chain is L == R bit-identical (roadmap line 546; SC-004).
- **FR-052 Transitions.** A mode change is click-free (SC-009):
  - the pan vector slews per FR-015;
  - the offsets engage and release over the FR-015 slew length (334 intervals, ruled 2026-10-18, G7-2), then ride the
    stage's 10 ms smoothing;
  - the safety stage crossfades the side signal (*drafted* 10 ms, matching Phase 5's ramps).
  
  On entering SOLID, once the crossfade ends the side path is exactly 0 and its filter state is cleared. Where
  bit-identity holds after a switch into SOLID is stated per tap:
  - **The safety-stage output** is L == R bit-identical from the crossfade's last sample + 1 (*drafted*, D-6), whatever
    the upstream history. This is the field's own guarantee; SC-004(a2) gates it.
  - **Downstream of the field it cannot be bit-identical at that sample.** `DCBlocker` keeps per-channel state x1_ / y1_
    (dc_blocker.h:208–214, 250–251) and `TruePeakLimiter` keeps per-channel oversampler state `osL_` / `osR_`
    (true_peak_limiter.h:176–177, cleared only by `reset()`, 79–84). Both processed L ≠ R before the switch. With
    identical inputs afterwards, the two DC states differ by δ·Rⁿ, with R = exp(−2π·10/48000) ≈ 0.9987
    (dc_blocker.h:243), and float rounding does not guarantee they ever become bit-equal. Upstream,
    `BassCharacterStage` also stays unlinked until `reset()` (FR-043), but the safety stage's forced side = 0 hides
    that. The full chain therefore **converges** to L == R after a switch (SC-004(a3)); it is bit-identical only for a
    render started in SOLID (SC-004(a1)) or after the downstream states are re-synchronised.
  - Re-synchronising the downstream states at the crossfade's end is a Phase 9 engine decision, handed over in FR-080.
    `DCBlocker::setState` exists (dc_blocker.h:159–167) but no state getter does, and the limiter has no state copy,
    so a copy would need append-only additions; a `reset()` of both is the other option. This phase changes neither
    class.

  Phase 8 morphs image mode "stepped-with-crossfade" (line 567) through this transition.
- **FR-053 Continuous controls inside a mode.** Spread, Gravity, crossover, curve and MOTION are continuous controls in
  SPREAD and WIDE. Changing them never triggers a mode crossfade.
- **FR-054 Mode × voice independence.** Mode transitions are global. Every voice's law reads the same mode at the same
  control interval; Phase 9 applies it.

### FR-060 series — The Anchor never enters (roadmap lines 126, 541–542)

- **FR-060 No Anchor input.** No field method takes or returns anything for the Anchor. The Anchor stays outside the law,
  the offsets and MOTION.
- **FR-061 a₁ stays valid.** p_0 = 0 (FR-013) in every mode.
  - For the all-zero vector (SOLID, or Spread 0) the core never calls `applyPanOffsets`, or restores the exact centre
    tables (profundum_core.h:614–625). h1 then renders at exactly `kCenterPanGain` (bank:107) on both channels, and the
    Phase 3 feed `deliveredGains()[0] · kCenterPanGain` is exact.
  - For a non-zero vector (SPREAD, WIDE), `applyPanOffsets` rewrites every element, element 0 included, as
    `std::cos(kQuarterPi + 0)` and `std::sin(kQuarterPi + 0)` in float (bank:647–659). Phase 1 FR-048(a) states these
    need not be bit-equal to each other or to `kCenterPanGain`. h1 is then only ulp-close to the centre, its L and R
    gains are not claimed bit-identical, and the a₁ feed is accurate within SC-018's 0.01 dB.
  - If D-1 is overruled, a ruled a₁ form must replace this FR before the plan.
- **FR-062 Anchor through the safety stage.** An Anchor-only input (L == R) leaves the safety stage L == R
  bit-identical in every mode and at every setting, from `reset()`, after a SOLID crossfade, or once the side state has
  decayed to exact zero (ruled 2026-10-18, C-2: the side crossover's biquads ring down after earlier L ≠ R content; that
  decay is recorded). The output is also bit-identical to the input (SC-004(c)).

### FR-070 series — Chain fixture, cross-cutting and handoffs

- **FR-070 Chain fixture.** This is the test fixture of Scope item 2.
  - It is built on `mass_dynamics_test_helpers.h` (`ChainConfig`, `renderChain`, 173, 372), so the Phase 6 chain is not
    duplicated.
  - It exposes taps at the core output, the dirty-arm output, the voice sum, the safety-stage input and output, and the
    limiter output.
- **FR-071 RT safety.** `processBlock`, `setControls`, the law and offset queries, `reset` and every MOTION advance
  allocate nothing, take no lock and throw nothing (SC-011). Scratch buffers are sized at `prepare`, and a longer block is
  split.
- **FR-072 Non-finite safety.**
  - A non-finite control keeps the previous value.
  - A non-finite input sample on a channel produces exactly 0.0f on that channel's output sample. An input guard alone
    cannot give this: with L non-finite and R finite, the guarded encode gives M = R/2 and S = −R/2, and the decode
    gives L_out = R/2 + S', where S' = g·S_low + S_high is a filtered version of S that does not cancel M. The stage
    therefore guards the input (non-finite → 0 before the encode) **and** records, per sample, which channels were
    non-finite, then forces those output samples to 0.0f after the decode (an output mask).
  - The filter state is finite again within one block, and L / R return to the clean render within a bound (SC-014).
  - Guards use `detail::isFinite`, which survives `-ffast-math`.
- **FR-073 Shared headers (ruled, Q1).** No shared header changes, and no exception. OQ-7 ruled pan only, so there is no
  `HarmonicOscillatorBank` hook, no SIMD-kernel change and no `ProfundumCore` setter for phase offsets. The
  `HarmonicOscillatorBank`, `ProfundumCore`, `BassCharacterStage`, `CrossoverLR4` and `MidSideProcessor` headers stay
  untouched (roadmap lines 934–935; SC-015 gates it).
- **FR-074 Determinism and partition invariance.** Every control update lands on the same absolute sample regardless of
  the host block size, because the control interval is phase-tracked across calls.
- **FR-075 Sample rates.** 44.1, 48, 88.2, 96 and 192 kHz. The law works in Hz, so its widths do not depend on the sample
  rate. The safety stage re-prepares its crossovers.
- **FR-076 Audibility (theme FR, lines 916–918).** Every lever moves a stereo descriptor by at least the ruled bar on a
  documented showcase setting (SC-010). The levers are Spread, Gravity, crossover, curve, MOTION depth, MOTION rate, and
  WIDE versus SPREAD.
- **FR-077 Mono compatibility (theme FR, lines 919–920).** The mono-sum test ships at every stereo-touching stage of this
  phase: the law's generator output, the dirty arm, the WIDE offsets, the safety stage and the full chain (SC-001).
- **FR-078 Anti-aliasing (theme FR, lines 921–922).** This phase adds no nonlinear stage. The law changes static pan
  gains, and the safety stage is LTI apart from its smoothed gains. WIDE does make the character stage offset Amount and
  Tone per channel, so the WIDE dirty arm is re-measured at the E0–E2 worst case against Phase 5's −80 dBFS bar
  (SC-007).
- **FR-079 CPU (lines 548, 924).**
  - The global part (the safety stage, the MOTION source and the law's global control-rate work) costs ≤ **0.5 % of one
    core** at 48 kHz (SC-013(a)). It is charged to Phase 9's globals line (≈ 1.5 %, roadmap line 622).
  - The per-voice part (the law query plus the core's pan forwarding, 96 `cos` + 96 `sin` per changed vector) is
    **gated** in this phase at its worst-case cadence: ≤ **0.25 %** of one core per voice and ≤ **1.0 %** at 4 voices
    (*drafted*, D-5; SC-013(b)). The plan may lower the cadence (FR-016) to meet it, but never changes the bank.
  - **Which Phase 9 line absorbs it.** Phase 9's per-voice figure (≈ 4.8 %, line 622) is derived from the Phase 1–5
    per-voice budgets and has no line for this cost. It is charged to the headroom between Phase 9's ceilings and the
    derived sums (lines 621–622): 8 % − (4.8 % + 1.5 %) = 1.7 % in MONO, and 25 % − (4 × 4.8 % + 1.5 %) = 4.3 % at
    4 voices. FR-080 hands Phase 9 a new per-voice budget line, "stereo-field law + pan forwarding", at the ruled figure.
- **FR-080 Handoffs.** The plan records, for later phases:
  - Phase 8: `Controls` is the morphable field set, and image mode morphs only through FR-052's transition.
  - Phase 9:
    - the law is called per voice per control interval with the resolved f₀;
    - the offsets go to every voice's character stage;
    - the safety stage sits at roadmap line 607's position;
    - the full-engine mono-sum check reuses SC-001's measure;
    - one `VoiceState` per voice, reset on a Reset note-on (FR-001, FR-015);
    - the per-voice CPU line of FR-079;
    - whether to re-synchronise the downstream DC and limiter states at the end of a SOLID crossfade (FR-052), so the
      full engine is bit-identical after a switch rather than converged.
  - Phase 8: `curve` is a continuous [0, 1] parameter of one family and interpolates linearly; crossover (60–250 Hz) is
    continuous; image mode alone is stepped-with-crossfade (FR-002, FR-052, SC-021).
  - Phase 9 also wires the field's host-tempo setter `setTempoBpm` to the process context (FR-036).
  - Phase 11: the controls for IDs 800–899: image mode (stepped), Spread, Gravity, crossover 60–250 Hz default 120 Hz,
    `curve` as a **continuous** [0, 1] parameter (not a list), MOTION depth, rate, source, and the tempo-sync mode and
    note value that feed `setTempoBpm` / `LFO::setNoteValue` (FR-036), with ranges from FR-002 and SC-008.
  - Phase 12: which controls affect mono compatibility, and the guarantee each one carries (SC-001's statement).

---

## Success Criteria

**Differential click count** (used by SC-009). This adopts the Phase 5 definition unchanged, as Phase 6 did
(`specs/profundum-phase6-mass-dynamics/spec.md:595–601`):
- two phase-aligned reference renders, held at the start and the end setting;
- a match window of ±N ≤ period / 8;
- the 1.1× amplitude guard;
- a bar of zero counted detections;
- positive controls (an unsmoothed full-range step and a hard switch), flagged per arm and per note;
- the Phase 5 **long-ramp clause** (ruled 2026-10-18, G7-1): a ramp longer than the smoother's settling through a stage
  whose steady output depends on the setting is gated by the sample-delta bound (every |Δy| ≤ 1.1 × the steady maximum
  over ≥ 21 traversed settings) and the two-reference count is recorded; SC-009(b) applies it to the chain output,
  where FR-015's pan slew makes every mode step a ~220 ms ramp of the generator's vector.

| ID | Criterion (roadmap line) | Metric, threshold, method | Test name sketch |
|---|---|---|---|
| **SC-001** | Mono sum per band, with no cancellation below the crossover (544–545) | **Grid:** every mode × Spread ∈ {0, 0.5, 1} × Gravity ∈ {min (0, so g_side = g_floor, FR-021), 0.5, max} × crossover ∈ {60, 120, 250} Hz × curve ∈ {0, 1} × MOTION depth ∈ {0, max} (both sources), at E0, E1, E2 and C3. Measured on the clean core, the dirty arm (every algorithm) and the Anchor arm, at the safety-stage output (gated there and at the generator output, (c); the chain output after Phase 6's DC blockers and limiter is **recorded** — G8-1 refinement: the limiter's oversampled residue above the partial range sits at −45 … −55 dBFS, exactly anti-phase between channels, and is not the field's).<br>**(a)** Sub band: Q_sub within **±1 dB** of 0 (*drafted*: Phase 5 SC-013(b)'s tolerance). It holds at Gravity min on all three arms (Q4). The sub-band gates of (a) and SC-002 apply where a harmonic lies in [20 Hz, f_c), the Band definition; a cell whose sub band holds no harmonic (f₀ ≥ f_c) is **recorded** (G10-2: at C3 with f_c 60 Hz the mask reads only Fracture's sub-f₀ products at −59.5 dBFS, ρ 0.987 after the stage).<br>**(b)** Every octave band above f_c up to 10 kHz: Q_band ≥ **−3.11 dB** (*drafted*: the −3.01 dB constant-power floor minus 0.1 dB, so nothing reads below the pan floor and no phase cancellation occurs) — **gated on the clean core** (every tap); **recorded per band on the two nonlinear arms** (the dirty arm and the Anchor arm, SPREAD and WIDE; ruled 2026-10-18, G8-1: a per-channel nonlinearity after the parity pan makes cross products of random relative phase in every band, so the octave Q of those arms sits at −3 dB ± the products' share and no floor or K_wide moves it — measured −3.8 dB on the SPREAD Rectify arm with no offsets and down to −10.8 dB in WIDE). Every reading is recorded.<br>**(c)** The same measurement on the generator output alone, before any nonlinear stage. | `BassStereoField_MonoSumPerBand` |
| **SC-002** | Sub-band correlation ≥ 0.99 in every mode (545) | On the SC-001 grid: ρ_sub ≥ **0.99** at the safety-stage output, on all three arms. On the clean core's generator output alone, ρ_sub ≥ 0.99 as well, because the law alone must not decorrelate the sub band. The sub-band side-to-mid energy ratio is recorded beside it. **Anti-phase exemption (ruled 2026-10-18, G8-1):** a nonlinear-arm cell whose sub band at the character tap (pre-stage) is predominantly anti-correlated (ρ_pre ≤ **−0.5**: the per-channel nonlinearity has inverted the fundamental between the channels, seen at the offset ceilings on Fold C3 / crossover 250 Hz and Rectify / Fold E2 with a pre-stage side-to-mid of +6 … +18 dB) is **recorded**, not gated, for SC-001(a) and SC-002; the exempt cells are listed in the evidence and counted (harvest 2026-10-18: 9 of 216 cells per nonlinear arm, Fold and Rectify at E2 / C3 only, pre-stage side-to-mid +3 … +18 dB). The floor is harvested so that every other loud cell passes (FR-021). | `BassStereoField_SubBandCorrelation` |
| **SC-003** | Width monotonic in frequency and Spread (546) | **Grid:** Gravity ∈ {min, 0.5, max} × crossover ∈ {60, 120, 250} Hz × curve ∈ {0, 1}, MOTION depth 0, at E1 and C2 (partials up to Nyquist), in SPREAD and WIDE. Each reading is taken at the generator output **and** at the safety-stage output.<br>**Clean core** (in WIDE the vector is SPREAD's, FR-040, so this proves the mode leaves the law unchanged):<br>**(a)** W_n is non-decreasing in n·f₀ (non-strict; partials with c = 0 sit at the floor), and the octave-band ρ is non-increasing in the octave-band centre frequency (read over bands that lie wholly inside the partial range, n·f₀ ≤ N·f₀ — G10-3: a top band truncated by the 64-partial cap holds 3–4 harmonics whose odd/even balance sets its ρ; W_n is read where ≥ −100 dB, as in (c));<br>**(b)** at fixed n, W_n is non-decreasing in Spread over 11 steps 0…1, and ρ over each octave band is non-increasing in Spread;<br>**(c)** raising Gravity never raises W_n at any n, and never raises an octave-band side-to-mid ratio, below or above f_c (the ratio is read where it is ≥ −100 dB in either reading — G8-1 refinement: at the harvested floor a centred band's side-to-mid is float residue near −116 dB and wandered 0.14 dB with Gravity against the 0.01 dB tolerance). At n·f₀ ≥ 5·f_c (= 2.5·f_s, f_s = 2·f_c, FR-024; the region is stated relative to the stage crossover because the LR4 high band is −6 dB at f_s and reaches −0.5 dB only near 2·f_s), raising Gravity from min to max lowers W_n by ≤ **0.5 dB** (*drafted*; the bar is unchanged by the Q3 ruling);<br>**(d)** at Spread 0, every W_n is at the floor and L == R is bit-identical.<br>**WIDE, dirty arm** (every algorithm, offsets at the FR-041 ceilings), at the safety-stage output: the octave-band side-to-mid ratio per band and per Spread step (11 steps) is **recorded** (ruled 2026-10-19, G10-1: the same physics as G8-1 — a per-channel nonlinearity after the parity pan makes random-phase cross products per band, measured non-monotone by up to 22 dB in band and 11 dB in Spread on Rectify at E1, 273 of 1007 and 209 of 1035 checks; the dirty arm's audibility stays gated by SC-010's WIDE-versus-SPREAD lever and the clean-core width law by (a)–(d)). Every reading is recorded. | `BassStereoField_WidthMonotonic` |
| **SC-004** | SOLID output is L == R bit-identical, and the Anchor stays bit-identical L/R (546; theme lines 919–920) | **(a1) Started in SOLID.** Every Spread / Gravity / crossover / curve / MOTION setting, on the full chain fixture (core + excitation + dirty arm with every algorithm + Anchor + Mass dynamics + safety stage + DC + limiter), at block sizes {1, 17, 32, 512}, from `reset()`: every sample at the safety-stage output and at the limiter output is L == R bit-identical (`requireLREqual`) from sample 0.<br>**(a2) Switched into SOLID, safety-stage output.** The same grid, with renders started in SPREAD and in WIDE (offsets at their ceilings, so every upstream stage runs L ≠ R) and then switched into SOLID: the safety-stage output is L == R bit-identical from the crossfade's last sample + 1 (FR-052).<br>**(a3) Switched into SOLID, full chain.** The same renders at the limiter output: max \|L − R\| ≤ **−120 dBFS** (*drafted*, D-5) from **500 ms** (*drafted*) after the crossfade's end; the time at which it first crosses −120 dBFS is recorded. Bit-identity is not claimed at this tap after a switch (FR-052).<br>**(b) Any mode (ruled 2026-10-18, C-2).** An L == R input to the safety stage gives L == R output, bit-identically, and the output equals the input bit-identically: gated from `reset()` in every mode and at every setting, and again after a SOLID crossfade (from the first sample with the mode fade at 0, + 1). After earlier L ≠ R content without a mode switch the side crossover's biquads ring down, so the first sample from which every output pair is bit-equal is **recorded** per crossover (`evidence/side-decay-record.csv`), not gated.<br>**(c) Anchor only (ruled 2026-10-18, C-2).** In every mode and at every setting, an Anchor-only input leaves the safety stage L == R bit-identical, from `reset()` and after a SOLID crossfade; the dirty-state decay is recorded as in (b). | `BassStereoField_SolidBitIdentical`, `BassStereoField_AnchorBitIdentical` |
| **SC-005** | The safety stage's phase behaviour is measured and recorded (537–538, OQ-6) | For the one topology (FR-022: side-only split, minimum phase) at crossovers f_c ∈ {60, 120, 250} Hz (stage f_s = 2·f_c), **recorded**:<br>• the phase of M_out / M_in and of S_out / S_in from 20 Hz to 10 kHz (M_out / M_in is exactly 1);<br>• the group delay of S at f_s;<br>• Q_band on a broadband stereo noise fixture with ρ = 0.5.<br>**Gated:** `latencySamples() == 0` in every mode and at every setting; the Mid path is bit-untouched and the measured (L_out + R_out)/2 equals (L_in + R_in)/2 within 2⁻²³ · (|M| + |S'|) per sample, bit-identical wherever S' = 0 (FR-025, ruled 2026-10-18, C-1; the bit-difference count for non-zero S' is recorded); the stage meets SC-001 and SC-006 at every crossover. SC-005 selects nothing: there is no second topology and no linear-phase candidate. | `BassStereoField_SafetyStagePhase` |
| **SC-006** | Distortion-born side content below the crossover is attenuated by ≥ 24 dB over the whole band (547) | **Fixtures** (*drafted*), each with the descriptor block's generator configuration (generator below f_c forced to centre, generator Gravity held at max):<br>(i) the dirty arm with the WIDE offsets at their ceilings and the SPREAD vector at Spread 1;<br>(ii) the core with the edge excitation at mix 1 and maximum drive, Spread 1 — **recorded** (G10-5: its distortion-born side content below f_c is under −60 dBFS at f_c 60 and 120 Hz, so the fixture is vacuous there; the gate is carried by (i) and the positive controls (iii));<br>(iii) positive controls: synthetic side-only tones at √(20 · f_c) Hz (the geometric centre of B) and at 0.95·f_c (the worst case just below f_c), so each tone lies inside B at every crossover of [60, 250] Hz.<br>**Gate**, at E0, E1, E2 and C3, with the stage at Gravity max, at every crossover f_c ∈ {60, 120, 250} Hz (stage split f_s = 2·f_c, FR-024):<br>• **A(B) ≥ 24 dB over the whole band B = [20 Hz, f_c)** (ruled, Q3: ONE bar; the stage splits at 2·f_c, so the LR4 high band is about −24.6 dB at f_c and lower below it). The bar is read on the band and on every third-octave inside B;<br>• each positive-control tone is attenuated by at least 24 dB, or the arm is flagged invalid.<br>**Recorded:** A(B) and A per third-octave across [20 Hz, f_c) at Gravity min, where SC-001 and SC-002 must still hold and A(B) is strictly positive (SC-020).<br>**Monotonic:** A(B) is non-decreasing in the stage's Gravity over 11 steps, with the input render fixed by the held generator Gravity. | `BassStereoField_SafetyStageAttenuation` |
| **SC-007** | Anti-aliasing of the WIDE chain (theme, 921–922) | WIDE dirty arm, every algorithm at Amount 1 with the offsets at the FR-041 ceilings, Fold 100, at E0–E2 and 44.1 / 48 / 96 kHz: aliased power per channel ≤ **−80 dBFS** (Phase 5's bar, measured with `aliasedPower`). | `BassStereoField_WideAliasing` |
| **SC-008** | MOTION rate and depth bounded (539–540, 548) | *Drafted bounds (D-5):* Cycle rate ≤ **1 Hz**, free-running and at the fastest synced value at 200 BPM; Drift τ inside **[0.5, 30] s** at every rate setting; excursion constant κ = **0.5** (FR-030).<br>**Read directly from the law output** p_k(t), one value per control interval, over a 60 s render at μ ∈ {0.5, 1} and Spread ∈ {0.5, 1}:<br>• **Excursion:** for every k, max_t \|p_k(t) − p̄_k\| ≤ κ · μ · P_k + 1e-6, and max_t \|p_k(t)\| ≤ P_k ≤ 1, so the clamp never engages. For Cycle, the maximum excursion is also ≥ 0.99 · κ · μ · P_k (non-vacuity);<br>• **Cycle rate:** the spectral peak of p_k(t) (BH7 window over the 60 s) is ≤ the rate bar and within one FFT bin of the configured rate;<br>• **Drift τ:** the τ the field configures through `BrownianDrift::setSmoothness` (brownian_drift.h:152), converted by the drift's own mapping, lies inside [0.5, 30] s at every rate setting. `BrownianDrift::kTauMin` is 0.2 s (brownian_drift.h:97), so this asserts the field's own clamp. The fitted 1/e autocorrelation time of p_k(t) is within a factor of **2** of the configured τ where τ ≤ 6 s (at least 10 decorrelation times in 60 s), and is recorded above that;<br>• h1 and every partial with c_k = 0 never move (p_k bit-equal throughout);<br>• **No side swap:** for every k with P_k > 0, sign(p_k(t)) = s_k at every t (FR-014); no sample crosses centre;<br>• **Inward motion, recorded:** the time-averaged width mean_t |p_k(t)| for both sources at μ ∈ {0, 0.5, 1}; it is non-increasing in μ for Cycle (mean = P_k·(1 − κ·μ) within 1e-3 of P_k) and recorded for Drift;<br>• **Tempo-synced Cycle (FR-036):** with `setTempoBpm` at 120 and 200 BPM and the note value giving the fastest synced rate, the spectral peak of p_k(t) is within one FFT bin of the tempo-derived rate and ≤ the rate bar; a non-finite or non-positive tempo keeps the previous one. | `BassStereoField_MotionBounded` |
| **SC-009** | No zipper and no clicks (FR-015, FR-027, FR-052) | **(a) Zipper.** A full-speed Spread 0 ↔ 1 sweep, a 48 st Drop at Spread 1, and MOTION at maximum. The product's vector sequence (one vector per control interval, from the query of FR-001) is recorded. Two test-side double-precision stereo references are rendered from it with identical oscillators: the Phase 1 reference (`renderReference`, profundum_core_test_helpers.h:498, as `renderIdeal` uses it, 674) extended to L/R, with the pan gains either **held** per control interval (the bank's staircase: `applyPanOffsets` writes the tables at once, bank:647–659) or **interpolated** per sample (the ideal pan). The zipper residual is held − interpolated, which isolates the pan staircase from the core's own pitch-update quantisation.<br>• Spread sweep and MOTION (static f₀): `sidebandPower` (323) of the residual, with the product render as `totalRef`, ≤ **−60 dB** re total;<br>• Drop (moving f₀, where `sidebandPower`'s static-f₀ regions, 309–333, do not apply): the broadband power of the residual re the product's total power ≤ **−60 dB**, and the differential click count of the product render is 0;<br>• validity: on a static note at Spread 1, the product render minus the held reference is ≤ −60 dB re total on each channel, so the reference reproduces the bank.<br>**(b) Clicks (ruled 2026-10-18, G7-1: the long-ramp form of the Phase 5 definition).** Mode steps SOLID ↔ SPREAD ↔ WIDE, Gravity steps 0 ↔ max, and crossover steps 60 ↔ 250 Hz, at block sizes {64, 512}, on the chain fixture at E0, E2 and C3 with `kSawAnchor`. For the generator these steps are ramps longer than any smoother's settling (FR-015's pan slew lands within 334 intervals, about 220 ms at 48 kHz; the f_c smoother is 20 ms) through a stage (Fold 100 → character) whose steady waveform depends on the setting, so the two-reference count is ill-posed on the chain output (Phase 5 Clarifications 2026-10-16, "Group 6 gate ruling"; measured: the counted detections sit 5–170 ms after the step, are 0 at the core tap, and steady renders at in-between settings reach the same |Δy| within 0.99–1.06×). On `outL` and `outR` the gate is therefore the **sample-delta bound**: every |Δy| during the step's ramp and settle ≤ **1.1 ×** the maximum steady-state |Δy| over a grid of ≥ 21 settings along the traversed path (endpoints included; the ratio printed per cell), and the differential click count is **recorded** (`evidence/sc009b-clicks.csv`). The differential count stays **gated at 0** at the core tap (`coreL/R`, linear in the vector) for every step. The positive controls (an unsmoothed full-range pan jump and a hard L/R switch) are flagged and must count ≥ 1 on the chain output.<br>**(c) Agreement.** After a crossover step 60 ↔ 250 Hz, from one control interval after the step on, \|f_stage − kStageCrossoverMultiple · f_c,law\| ≤ **0.1 Hz** (the crossover's hysteresis, crossover_filter.h:106), with kStageCrossoverMultiple = 2.0. f_c,law is the value the law query used (recorded in `VoiceState`); f_stage is `getCrossoverFrequency()` of the stage's crossover, which equals the value the field handed it (FR-024). The law and stage values are different on purpose; the test asserts the relation, not equality. The crossovers' smoothing time is checked by computation: their smoother settles on a full-range handoff to within 0.1 Hz inside one control interval. | `BassStereoField_NoZipper`, `BassStereoField_TransitionsClickFree` |
| **SC-010** | Audibility gates (theme, 916–918) | On a documented showcase (SPREAD, `kSawAnchor`, E1 and C3), each lever alone, between its extremes:<br>• **Static levers** (Spread, Gravity, crossover, curve, and WIDE versus SPREAD on the dirty arm) move a time-averaged stereo descriptor by at least the bar (the Gravity lever is gated at E1 and **recorded** at C3 — G8-1 refinement: at C3 with the default 120 Hz crossover no harmonic lies below f_s = 240 Hz except the centred h1, so Gravity has no band to act on there by design; measured 13 → 6 dB at E1 over the floor candidates, 1.8 → 0.6 dB at C3) (*drafted*: ≥ **3 dB** on an octave-band side-to-mid ratio, or ≥ **0.1** on an octave-band ρ);<br>• **MOTION depth:** the standard deviation over time of W_oct(t), in dB, rises by ≥ **1 dB** (*drafted*) from depth 0 to max;<br>• **MOTION rate:** the dominant modulation frequency of W_oct(t) (Cycle), or its fitted 1/e autocorrelation time (Drift), changes by a ratio ≥ **4** (*drafted*) between the rate extremes.<br>W_oct(t) is the short-window side-to-mid ratio of the octave band with the largest static width: a BH7 window of 4096 samples at 48 kHz (scaled with the sample rate) or the power of two holding ≥ 8 periods of f₀, whichever is longer, with a quarter-window hop, over 60 s (G10-6: at E1 the 85 ms window held 3.5 periods and the per-window estimate jittered faster than any Drift, reading a 1/e time of 45 ms at both rate extremes). | `BassStereoField_LeversAudible` |
| **SC-011** | RT safety (912–913) | `AllocationScope` reports 0 allocations across `processBlock`, `setControls`, the law and offset queries, `reset`, every mode switch and every MOTION source switch. | `BassStereoField_NoAllocation` |
| **SC-012** | Determinism and partition invariance (FR-033, FR-074) | Block sizes {1, 17, 32, 64, 512}, with a mode switch, a Spread ramp and MOTION on (both sources):<br>• the output is bit-identical across partitions;<br>• two renders from `reset()` are bit-identical;<br>• a changed Drift seed changes the output. | `BassStereoField_Deterministic` |
| **SC-013** | CPU ≤ 0.5 % global (548, 924) | **(a) Gated.** 48 kHz stereo; the safety stage at Gravity max; MOTION on, with the more expensive of the two sources; the law's global work. Cost ≤ **0.5 %** of one core. It runs alone in the isolated CPU lane, pinned, tagged `[performance]`.<br>**(b) Per voice, gated.** 48 kHz, at the worst-case cadence: MOTION at maximum depth and the fastest rate (each source), Spread 1, so the vector changes every control interval. Cost = the law query plus the core's pan forwarding (`applyPanOffsets`, 96 `cos` + 96 `sin` per change, bank:651–658), measured as the increment over the same core at Spread 0. Ceilings: ≤ **0.25 %** of one core for one voice and ≤ **1.0 %** for 4 voices (*drafted*, D-5; FR-079). Same isolated `[performance]` lane.<br>**(c) Recorded.** The handoff count per second (FR-016's `VoiceState` counter) on a static note with MOTION off, expected 0 after settling. | `BassStereoField_Cpu` |
| **SC-014** | Non-finite safety (FR-072) | A 16-sample NaN / ±Inf burst into the safety stage, and non-finite values in every control:<br>• every output sample is finite (bit-pattern test);<br>• each output sample of a channel whose input sample was non-finite is exactly 0.0f, through FR-072's output mask, including the case of one channel non-finite and the other finite;<br>• the state is finite within one block;<br>• within 100 ms, the output matches a clean render to within −60 dB. | `BassStereoField_NonFinite` |
| **SC-015** | Shared suites, portability, tidy (925–935) | • `dsp_systems_tests` and the Profundum suites are green within the change set (close-lane rule, lines 925–927);<br>• no shared header changes (FR-073): the `HarmonicOscillatorBank`, `ProfundumCore`, `BassCharacterStage`, `CrossoverLR4` and `MidSideProcessor` headers are unchanged in the phase's diff, so the Ruinae, Seraphis, Vorago and Innexus suites need no extra run beyond the change-set rule;<br>• `node tools/check-portability.js` is clean;<br>• clang-tidy is clean on the phase's TUs. | — (gate) |
| **SC-016** | Sample-rate behaviour (FR-075) | At 44.1, 48, 88.2, 96 and 192 kHz:<br>• the output is finite and bounded;<br>• at E1, W_n per harmonic is within **0.25 dB** of the 48 kHz value where W_n at 48 kHz is ≥ −100 dB (G10-4: at 192 kHz the float residue of the 240 Hz LR4 biquads read −112 … −119 dB against −120 dB at 48 kHz; the gated harmonics agree within 0.004 dB), and the SC-006 attenuation is within 1 dB of it;<br>• a render after prepare(48k) → prepare(96k) → prepare(48k) equals a fresh 48 kHz render. | `BassStereoField_SampleRates` |
| **SC-017** | WIDE offsets respect the Phase 5 ceilings (FR-041) | For every algorithm and for Spread in 11 steps, and across every algorithm switch: \|amountOffset\| and \|toneOffset\| are ≤ 0.15 for Tube, Fold, Bite and Crush and ≤ 0.10 for Rectify and Fracture, exactly; both are 0 in SOLID and SPREAD once the engagement ramp has landed (at once from `prepare()` / `reset()`; within 334 intervals after leaving WIDE, G7-2), and both are exactly 0 at Spread 0 in WIDE; after entering WIDE the offsets rise monotonically and reach their full value exactly within 334 intervals. The offset equals ceiling · Spread · K_wide (FR-041) with 0 < K_wide ≤ 1, within 1e-6. In WIDE the pan vector is bit-identical to SPREAD's for the same controls and f₀ (FR-044). | `BassStereoField_WideOffsetCeilings` |
| **SC-018** | h1 centred and the a₁ feed exact (FR-013, FR-061) | Every mode and setting, notes E0 … C4 (so at the top h1 is above f_c):<br>• p_0 == 0.0f;<br>• the rendered h1 on L and on R equals `deliveredGains()[0] · kCenterPanGain` within 0.01 dB (exact for the all-zero vector, ulp-close in SPREAD and WIDE, FR-061);<br>• **core output tap:** the h1 power in M = (L + R)/2, before any nonlinear stage, matches the Spread 0 render within **0.1 dB** in every mode;<br>• **Anchor output:** the `AnchorOscillator` output power (L == R) in the Anchor arm matches the Spread 0 render within **0.1 dB** in every mode;<br>• **recorded, not gated:** the sub-band M level after the character stage, per mode. In WIDE it is read against Phase 5's measured offsets (−0.56 … −0.61 dB below 2·f₀ at 0.10, sc013-offset-ceilings.txt), because the per-channel drives change the nonlinear products that land in the sub band for reasons outside the field. | `BassStereoField_H1Centred` |
| **SC-019** | Pan-only vector and the direction pattern (FR-014; Q1, Q8) | On the clean core at the generator output, Spread ∈ {0.5, 1}, every mode, MOTION depth ∈ {0, max} (both sources), E1 and C2:<br>• for every k with p_k ≠ 0, sign(p_k) = +s for odd harmonics and −s for even harmonics, one fixed global s (the bank's convention, bank:1155–1156), at every control interval;<br>• the pattern is identical across `reset()`, across `VoiceState` instances and across Drift seeds (no seed, no per-voice state);<br>• **no inter-channel phase offset:** for every harmonic n with energy, the phase of harmonic n on L minus its phase on R (`goertzelPhase`) is 0 within **0.01 rad** (*drafted*);<br>• the harmonic-resolved mono-sum ratio of every harmonic is ≥ −3.01 dB − 0.1 dB, i.e. pan alone cannot cancel. | `BassStereoField_PanOnlyDirection` |
| **SC-020** | Gravity-min floor and monotone side gain (FR-021; Q4) | On the stage, over 11 Gravity steps 0 … 1: the side gain below f_s is non-increasing, 0 < g_side(0) = g_floor < 1 and g_side(1) = 0 exactly. At Gravity 0 the measured A(B) of SC-006 is strictly greater than 0 dB, and SC-001 and SC-002 hold at Gravity 0 on the clean core, the dirty arm (every algorithm) and the Anchor arm at every crossover (the g_floor value is harvested in the consuming binary; the harvest is recorded; the nonlinear arms under G8-1's readings: octave Q recorded, anti-phase cells recorded). At Gravity 1 the side below f_s is the S_high path only. | `BassStereoField_GravityFloor` |
| **SC-021** | The `curve` control is one continuous family (FR-002, FR-003, FR-012; Q5) | The law is evaluated for `curve` ∈ {0, 0.1, …, 1} at crossover ∈ {60, 120, 250} Hz and Gravity ∈ {min, max}: every `Controls` with a `curve` in [0, 1] and a crossover in [60, 250] passes `isValidControls`, values outside clamp; the vector is finite and bounded by 1; for the linear interpolation of two valid `Controls` at t ∈ {0.25, 0.5, 0.75} (what Phase 8 will do), the result is valid, the vector finite and bounded by 1, and (ruled 2026-10-18, C-17) for pairs that move one control, or several controls in the same width direction (Spread up, Gravity down, crossover down, curve down), its W_n lies between the two endpoints' W_n within 0.5 dB; for mixed pairs (controls moving in opposite width directions, e.g. (Spread 0, crossover 60 Hz) → (Spread 1, crossover 250 Hz)) the W_n path is continuous (largest change per 0.05 of t recorded) and its excursion beyond the endpoints is printed, not bounded (c is monotone per control, not jointly); the largest adjacent-step change of W_n over the 11 curve steps is recorded. SC-001 / SC-002 / SC-003 run at the range extremes (curve 0 and 1; crossover 60 and 250 Hz) and the defaults. | `BassStereoField_CurveContinuous` |

---

## Edge Cases

- **Low notes.** At E0 the first five harmonics sit below a 120 Hz crossover. The law keeps them centred, and SC-001 /
  SC-002 run at E0. The Low Note Guard (Phase 1) already thins the upper partials there, and their pans are inaudible.
- **High notes.** At C4, h1 is above most crossovers. FR-013 keeps it centred anyway (D-1).
- **Drop sweeps.** A 48 st Drop moves every partial's frequency through f_c. The law follows f₀ per control interval,
  and FR-015's slew bounds each step. SC-009(a) covers it.
- **Spread / Gravity / crossover extremes.** Spread 0 and 1; Gravity min and max; the crossover at both ends of its
  60–250 Hz range (the stage then splits at 120–500 Hz). The crossover is clamped to `CrossoverLR4`'s own range: ≥ 20 Hz and ≤ 0.45·fs (crossover_filter.h:102–103,
  351–352).
- **Mode change mid-note.** Handled by FR-052. No retrigger or re-arm is issued.
- **SOLID with stale state.** The side path's filter state is cleared when SOLID's crossfade ends, so no residue breaks
  the safety-stage output's bit-identity (FR-052). The per-channel DC and limiter states downstream converge rather than
  match (SC-004(a3)); re-synchronising them is Phase 9's decision.
- **POLY.** Four voices are summed into one safety stage. Each voice's law reads its own f₀, while the safety stage is
  global. The 4-voice SC-001 reading is recorded for Phase 9.
- **`right == left`.** One channel has no side, so the stage passes it through.
- **Block size.** Any host block size. Scratch buffers are sized at `prepare`, and longer calls are split (the
  `TruePeakLimiter` precedent, true_peak_limiter.h:105).
- **Sample-rate change.** `prepare` re-prepares both crossovers (`CrossoverLR4::prepare` is not RT-safe,
  crossover_filter.h:169) and the MOTION sources.
- **Denormals.** FTZ / DAZ is the host's precondition. The crossovers are biquads.
- **Seed determinism.** The Drift source is seeded, and `reset()` restores the seed. The LFO's random waveforms are not
  used (Sine only). SC-012 covers it.
- **Pan clamp.** The law never exceeds |p| = 1, so the bank's clamp never bends the law (FR-018).

---

## Decisions (ruled 2026-10-18, and bars still drafted for the plan to harvest)

**Ruled (Q1–Q8; see Clarifications).**
- **D-1 h1 always centred.** p_0 = 0 at every note (FR-013, FR-061, SC-018). Confirmed by the Q8 ruling.
- **D-2 Direction pattern.** Odd and even harmonics go to opposite sides, fixed per harmonic, no seed, MOTION never swaps
  sides (FR-014, SC-019, SC-008). Ruled Q8.
- **D-3 MOTION sources.** Both `BrownianDrift` (Drift) and `LFO` Sine (Cycle, free or tempo-synced through a field
  setter), one selected; depth moves width inward, κ = 0.5 (FR-030, FR-031, FR-036, SC-008). Ruled Q7.
- **D-5 SC-006 reading of roadmap line 547.** One bar, ≥ 24 dB over the whole [20 Hz, f_c), met by splitting the stage
  at 2·f_c (FR-024, SC-006). Ruled Q3.
- **D-7 WIDE offset law.** offset = ceiling(algorithm) · Spread · K_wide; the law is identical to SPREAD (FR-041, FR-044,
  SC-017). Ruled Q6.
- **OQ-6 Gravity law.** Crossover 60–250 Hz default 120 Hz; `curve` a continuous [0, 1] raised-cosine-in-log-frequency
  parameter with Gravity as its slope (FR-002, FR-012, SC-021); minimum-phase side-only topology, zero latency (FR-022,
  SC-005); Gravity 0 keeps a non-zero side-attenuation floor (FR-021, SC-020). Ruled Q2–Q5.
- **OQ-7 Inter-channel phase offsets.** Pan only; no shared-header change (FR-014, FR-073, SC-019). Ruled Q1.

**Still drafted (the plan harvests, the compliance pass verifies).**
- **D-4 MOTION target.** The pan vector only, with no post-sum M/S rotation (FR-032).
- **D-5 Bars.**
  - SC-001: ±1 dB in the sub band, and the −3.01 dB floor (−3.11 dB with margin) above f_c;
  - SC-003: Gravity lowers W_n by ≤ 0.5 dB at n·f₀ ≥ 5·f_c (= 2.5 × the stage crossover); the WIDE dirty arm's
    octave-band monotonicity within 0.5 dB;
  - SC-004(a3): |L − R| ≤ −120 dBFS from 500 ms after the SOLID crossfade;
  - SC-008: Cycle rate ≤ 1 Hz, Drift τ in [0.5, 30] s;
  - SC-009: −60 dB; SC-009(c) agreement within 0.1 Hz;
  - SC-010: 3 dB or 0.1 for the static levers; a 1 dB rise in the std of W_oct(t) for MOTION depth; a ratio of 4 for
    MOTION rate;
  - SC-013(b): ≤ 0.25 % per voice and ≤ 1.0 % at 4 voices, charged to Phase 9's headroom (FR-079);
  - SC-019: inter-channel phase difference ≤ 0.01 rad; SC-021: interpolation within 0.5 dB.
- **D-6 Mode crossfade.** A 10 ms side crossfade. The safety-stage output is bit-identical from the crossfade's end;
  the full chain converges to L == R (SC-004(a2), (a3)); Phase 9 decides whether to re-synchronise the downstream states
  (FR-052, FR-080).

---

## Traceability

| Roadmap statement (line) | FRs | SCs |
|---|---|---|
| New L3 component `systems/bass_stereo_field.h` (527) | FR-001–FR-004 | SC-011, SC-012 |
| Image modes SOLID / SPREAD / WIDE, with the Anchor centred (529–530) | FR-050–FR-054, FR-060–FR-062 | SC-004, SC-009(b) |
| SPREAD: a per-partial vector, width = f(frequency; crossover, curve), zero = centred (531–533) | FR-010–FR-018 | SC-003, SC-009(a), SC-018 |
| "pan/decorrelation vector" (532) and "pan/phase offsets" (127): ruled pan only | FR-014, FR-073 | SC-002, SC-015, SC-019 |
| GRAVITY: the law's steepness and a post-sum safety stage (534–536) | FR-012, FR-020–FR-028 | SC-003(c), SC-006 |
| Distortion makes harmonics on both sides; the phase is measured (537–538) | FR-019, FR-022, FR-023 | SC-005, SC-006 |
| MOTION: BrownianDrift or synced LFO, bounded and slow (539–540) | FR-030–FR-035 | SC-008, SC-012 |
| WIDE: per-channel drive/tone offsets; the Anchor never enters (541–542) | FR-040–FR-043, FR-060 | SC-007, SC-017, SC-004(c) |
| Mono sum per band, no cancellation below the crossover (544–545) | FR-021, FR-077 | SC-001 |
| Sub-band correlation ≥ 0.99 in every mode (545) | FR-012, FR-021 | SC-002 |
| Width monotonic in frequency and Spread (546) | FR-012 | SC-003 |
| SOLID L == R bit-identical (546) | FR-011, FR-051, FR-052 | SC-004 |
| Side content attenuated by ≥ a ruled figure (547) | FR-021, FR-024 | SC-006 |
| Gravity min keeps a non-zero side floor; crossover 60–250 Hz; continuous `curve` (Q4, Q5) | FR-002, FR-003, FR-012, FR-020, FR-021 | SC-020, SC-021 |
| WIDE = SPREAD law + offsets only (Q6) | FR-040, FR-041, FR-044 | SC-003, SC-017 |
| Host-tempo setter for synced Cycle (Q7) | FR-031, FR-036 | SC-008 |
| MOTION rate and depth bounded (548) | FR-035 | SC-008 |
| CPU ≤ 0.5 % global (548, 924), plus the per-voice part gated here | FR-079 | SC-013(a), SC-013(b) |
| Audibility, mono and anti-aliasing themes (916–922) | FR-076–FR-078 | SC-001, SC-007, SC-010 |
| RT safety, portability, shared headers (912–913, 930–935) | FR-071–FR-073 | SC-011, SC-014, SC-015 |
| Phase 1 FR-048: pan-offset changes are Phase 7's to smooth | FR-015, FR-016 | SC-009(a), SC-013(b), SC-013(c) |
| Phase 3 FR-023: a₁ at the centre pan gain | FR-013, FR-061 | SC-018 |
| Phase 5 handoff: offset ceilings of 0.10 for Rectify and Fracture (432–433) | FR-041, FR-042 | SC-017 |
| Phase 6 handoff: side content passes the compressor (492–493) | FR-026, FR-070 | SC-001 (Anchor arm through `MassDynamics`) |

## Assumptions

- The field is global, except for the law and offset queries. Phase 9 calls those per voice per control interval, with
  that voice's resolved f₀ and current character algorithm.
- The calibration input level is the core's −12 dBFS RMS (`HarmonicFold::kCalibrationPeak`), as in Phases 2–6.
- The safety stage sits after `MassDynamics` and before `DCBlocker` / `TruePeakLimiter` (roadmap line 607). The chain
  fixture uses that order.
- For Cycle's synced mode, the host supplies tempo through the field's setter, and Phase 9 forwards it.


## Clarifications

### Session 2026-10-18

All eight rulings are the user's, given 2026-10-18 (each the recommended option). Each line ends with the FR / SC ids
that enforce its clauses; the spec body above states the behaviour without needing this log.

- Q: Does the width vector carry per-partial inter-channel phase offsets in addition to pan (OQ-7)? -> A: **Pan only.**
  No `HarmonicOscillatorBank` / `ProfundumCore` change, no per-partial inter-channel phase offsets in this phase; width
  comes from neighbouring harmonics panned to opposite sides plus WIDE's per-channel character offsets; the mono-sum
  guarantee holds by construction. [FR-014, FR-073, SC-015, SC-019, SC-001, SC-002]
- Q: Is the safety stage committed to one topology, or are several built and measured (OQ-6 phase, FR-022)? -> A:
  **Topology (a) now:** M/S encode, only Side is split by the minimum-phase `CrossoverLR4` and attenuated below the stage
  crossover, Mid untouched, zero latency; L + R = 2M is bit-untouched. SC-005 records phase but no longer selects; no
  linear-phase crossover and no second topology. [FR-001, FR-022, FR-023, FR-025, SC-005, SC-004(b)]
- Q: How is "side content below the crossover attenuated by >= X dB" met near f_c (D-5 / SC-006)? -> A: **The stage
  splits at a fixed multiple above the law's f_c** (stage split f_s = 2 * f_c, the multiple a named constant), so ONE bar
  of >= 24 dB holds over the whole [20 Hz, f_c) at maximum Gravity. The law and stage crossovers are deliberately
  different values; FR-024 and SC-009(c) are written to that relation. [FR-024, FR-021, SC-006, SC-009(c), SC-003(c)]
- Q: What does Gravity at its minimum mean for the safety stage (FR-020 / FR-021)? -> A: **A non-zero side-attenuation
  floor** below the stage crossover (g_side < 1, value harvested inside the consuming test binary) so SC-001 / SC-002
  hold on the distorted arm at Gravity 0; higher Gravity deepens the attenuation monotonically to full removal at
  Gravity max. The sub is never left unprotected. [FR-020, FR-021, SC-020, SC-001, SC-002]
- Q: What are the crossover range and the shape of `curve` (OQ-6 range, FR-002)? -> A: **Crossover 60-250 Hz, default
  120 Hz.** `curve` is a continuous [0, 1] shape parameter within one family (raised cosine in log-frequency), Gravity its
  slope; it interpolates linearly in Phase 8 and registers as a continuous parameter in Phase 11. The SC grids use the
  range extremes. [FR-002, FR-003, FR-012, FR-080, SC-001, SC-003, SC-021]
- Q: Does WIDE differ from SPREAD only by the character offsets, and how are they scaled (FR-040 / FR-041 / D-7)? -> A:
  **Offsets only.** offset = per-algorithm ceiling (0.15 Tube / Fold / Bite / Crush, 0.10 Rectify / Fracture) * Spread *
  a harvested constant; the width law is identical in both modes; Spread 0 stays exactly centred; the Phase 5 ceilings
  hold by construction. [FR-040, FR-041, FR-044, FR-011, SC-003, SC-017]
- Q: Which MOTION sources ship and how does depth act (FR-030 / FR-031 / D-3)? -> A: **Both:** Drift (`BrownianDrift`)
  and Cycle (sine LFO, free-running or tempo-synced, with a host-tempo setter on the field's API that Phases 9 and 11
  wire). Depth moves width INWARD from the static envelope (kappa = 0.5): no partial crosses centre or exceeds the static
  width, so SC-001 holds trivially at maximum depth; time-averaged width falls with depth (recorded). [FR-030, FR-031,
  FR-036, FR-080, SC-008, SC-001]
- Q: What is the per-partial pan direction pattern (FR-014 / D-2)? -> A: **Alternating by harmonic parity:** odd
  harmonics one side, even the other, fixed per harmonic index (the bank's `setStereoSpread` convention); deterministic,
  follows the keyboard, no seed, no extra state; MOTION never swaps sides; h1 stays centred (D-1). [FR-014, FR-013,
  FR-030, SC-019, SC-008]

### Session 2026-10-18 (plan-stage rulings, before the build)

The plan (S1) flagged three gated bars as unmeetable as written and four refinements for ratification. Rulings C-1,
C-2, C-17, C-9, C-10 and C-15 are the user's (each the recommended option); C-18 and the C-13 escalation floors were
accepted by the main loop (FR-024 delegated that verification to the plan; C-13 stops and asks either way).

- C-1 (FR-025 bullet 2, SC-005): "M output bit-identical to M input" cannot hold in float when S' ≠ 0 (two decode
  roundings). -> **Numeric bound:** Mid bit-untouched inside the stage; measured (L_out + R_out)/2 within
  2⁻²³ · (|M| + |S'|) of (L_in + R_in)/2 per sample; bit-exact wherever S' = 0. `kGateMidBitExact = false`. [FR-025, SC-005]
- C-2 (FR-025 bullet 3, FR-062, SC-004(b)(c)): L == R bit-identity needs a zero side state (the side biquads ring
  down after L ≠ R content). -> **From reset(), while the side path is idle (SOLID settled), or once the side state has
  decayed to exact zero;** the decay time per crossover is recorded. `kGateLrBitExactAnyState = false`. [FR-025, FR-062,
  SC-004(b), SC-004(c)]
- C-17 (SC-021): mixed control pairs cannot keep W_n between their endpoints (c is monotone per control, not jointly).
  -> **The 0.5 dB endpoint bar applies to single-control and same-direction pairs;** mixed pairs are gated on validity,
  finiteness, |p| ≤ 1 and continuity, their excursion printed. `kGateMixedInterpolationPairs = false`. [SC-021]
- C-9 (FR-036, SC-008): synced Cycle can exceed the 1 Hz bar. -> **Octave fold (option A):** tempo halved by octaves
  until the synced rate is ≤ 1 Hz; fast note values alias; recorded in `evidence/sc008-sync-fold.csv`; stated in the
  Phase 11/12 handoffs. `kCycleSyncFold = true`. [FR-036, SC-008]
- C-15 (FR-011 with FR-015): SOLID's and Spread 0's zero vector is reached by slew. -> **Ratified:** target exactly 0
  at once; emitted vector bit-zero within 334 intervals and from the first query when started there. [FR-011, SC-004]
- C-10 (FR-040, FR-041): WIDE offset signs. -> **amount +K, tone −K** (one channel hotter and darker). [FR-040, SC-017]
- C-18 (FR-024, SC-009(c)): tracking mode and corner bound. -> **Efficient tracking, 0.1 ms smoothing, corner within
  ε_s(fs) + 0.1 Hz one interval after a handoff** (main-loop ratification). [FR-024, SC-009(c)]
- C-13 (harvest escalation): no pair with g_floor ≥ 0.1 and K_wide ≥ 0.5 passing SC-001 / SC-002 / SC-010 on the WIDE
  dirty arm at Gravity 0 -> the build stops and asks (lower K_wide, lower floor, or re-read SC-002). Floors accepted.
  [FR-021, FR-041, SC-020]
- R-1 (SC-013(b)): if T001 measures the per-voice pan forwarding above budget, the only lever (handoff cadence) bends
  Cycle above ≈ 0.7 Hz and needs a ruling then; nothing ruled in advance.
- P-0 (workflow): the `[profundum]` gate filter also ran five hidden Phase 1–6 cases (one a 10–30 min harvest);
  `~[.]` appended to PHASE_FILTER in `.claude/workflows/profundum-phase.js` before the build stage.

### Session 2026-10-18 (build rulings)

- G7-1 (Group 7 gate, SC-009(b), user): 14 transition cells (E2 and C3 SOLID ↔ SPREAD, C3 crossover 60 ↔ 250 Hz)
  counted clicks with the two-reference differential count; every detection sits 5–170 ms after the step inside FR-015's
  pan slew, is 0 at the core tap, first appears after Fold 100 → character, and steady renders at in-between settings
  reach the same |Δy| (0.99–1.06×). -> **Apply the Phase 5 long-ramp form on the chain output:** gate by the
  sample-delta bound (≤ 1.1 × the steady maximum over ≥ 21 traversed settings, per channel), record the differential
  count; keep the count gated at 0 at the core tap; positive controls still count. Phase 7's bullet list had omitted the
  clause the adopted definition already carried. No field behaviour changes. [SC-009(b), FR-015, FR-052]
- G7-2 (Group 7 gate after G7-1, SC-009(b), user): one cell stayed red, SPREAD → WIDE at C3 (right channel, ratio
  1.115 against 1.1 at both block sizes). The field and the character stage sit inside the steady path; the excess is
  born in Phase 6's Mass dynamics: the offsets switched on through the character stage's 10 ms smoothing and the
  transient shaper read the right channel's edge-level rise as an attack (+1.24 dB in the first 20 ms, decaying over
  220 ms). FR-044 keeps the vector across SPREAD ↔ WIDE, so that step was a 10 ms event, not the ~220 ms ramp G7-1
  assumed. -> **Field change: the offsets engage and release over the FR-015 slew length** (a 0–1 scalar slewed by
  kMaxPanStepPerInterval per interval, exact landing within 334 intervals). FR-040, FR-043, FR-052 and SC-017 are
  written to it; SC-009(b) keeps its 1.1 bar on the chain output. Alternatives declined: gating SPREAD ↔ WIDE before
  the dynamics; accepting the shaper's response as a bar change. [FR-040, FR-043, FR-052, SC-017, SC-009(b)]
- G8-1 (T015 harvest, C-13 escalation, user): no K_wide in {1, 0.75, 0.5} passed at the seed floor 0.5, and the
  per-cell data showed K_wide is not the lever. (1) SC-002's 0.99 failed in 64 loud WIDE dirty cells; from the
  pre-stage side-to-mid the floor needs −7 … −13 dB for most, −16 … −24 dB for Crush / Rectify / Bite and −30 … −41 dB
  for three cells whose folded channels carry the fundamental in anti-phase; the SC-010 WIDE lever already fails at
  K_wide 0.5. (2) SC-001(b)'s −3.11 dB octave bar failed in 78 cells inside the harmonic range (worst −10.8 dB) and on
  the SPREAD dirty arm with no offsets: random-phase cross products of a per-channel nonlinearity after the parity pan,
  which no floor touches. (3) Near-silent bands (−80 … −100 dBFS) were read. -> **Deep floor + record the octaves:**
  SC-001(a) and SC-002 stay gated on the nonlinear arms at Gravity 0 with the anti-phase cells recorded; the floor is
  harvested down to the deepest passing candidate (escalation floor 0.03), K_wide searched in {1, 0.75} on SC-007 and
  the SC-010 lever; a band is read only above −60 dBFS; SC-001(b) is gated on the clean core and recorded per band on
  the dirty and Anchor arms (the Anchor arm shares the dirty arm's nonlinear stage above f_c; it keeps the sub-band
  gates). Alternatives declined: full removal at Gravity 0; relaxing the dirty-arm ρ bar. [SC-001, SC-002, SC-020, FR-021]
- G8-1 refinements (main loop, after the second harvest run; readings, not bars): the SC-001 / SC-002 gates read the
  generator output and the safety-stage output, the taps the rows name; the chain output after Phase 6's DC and limiter is
  recorded (the limiter's oversampled residue above the partial range, −45 … −55 dBFS and anti-phase, failed 36 clean
  Growl cells at every floor). The anti-phase exemption is ρ_pre ≤ −0.5 (ρ_pre < 0 had caught 31 cells, most of them
  Crush's weakly anti-correlated sub-band noise, which the floor must handle). The Gravity lever is gated at E1 and
  recorded at C3. SC-003(c)'s octave side-to-mid monotonicity is read only where the ratio is ≥ −100 dB (a −116 dB
  float residue wandered 0.14 dB after the floor landed at 0.035). [SC-001, SC-002, SC-003, SC-010]

### Session 2026-10-19 (close rulings)

- G10-1 (long lane, SC-003 WIDE dirty arm, user): the octave-band side-to-mid monotonicity in band and in Spread
  failed 273 of 1007 and 209 of 1035 checks, by up to 22 dB (Rectify, E1) — G8-1's physics. -> **Recorded** per
  algorithm, band and Spread step; the clean-core clauses (a)–(d) stay gated (46 080 W_n checks pass), WIDE's vector
  equality with SPREAD stays gated, SC-010's WIDE lever gates the dirty arm's audibility. Alternatives declined: a
  fitted band-trend gate; keeping the clause. [SC-003]
- G10-2 (main loop, SC-001(a) / SC-002): two MOTION shards (Fracture, C3, f_c 60 Hz, Spread 1, Drift) read ρ 0.987 at
  the stage output in a sub band holding no harmonic (f₀ = 130.8 Hz > f_c): the mask reads Fracture's sub-f₀ products
  at −59.5 dBFS. The spec's Band is a set of harmonics, so the sub-band gates apply where a harmonic lies in [20 Hz, f_c);
  otherwise recorded. The next floor candidate (0.03) would have read 0.9887 and the escalation floor is 0.03. [SC-001, SC-002]
- G10-3 (main loop, SC-003(a)(b)): 108 octave-ρ checks failed by ≤ 0.022 in top bands truncated by the 64-partial cap
  (3–4 harmonics); 3 W_n checks failed by ≤ 1.7 dB at W ≈ −113 dB (float residue). -> bands read only when wholly
  inside the partial range; W_n read where ≥ −100 dB. [SC-003]
- G10-4 (main loop, SC-016): at 192 kHz W_1…W_3 at the stage output read −119 / −112 / −102 dB against −120 / −120 /
  −104 dB at 48 kHz (LR4 float residue at ω = 0.008); every harmonic above −100 dB agrees within 0.004 dB. -> W_n
  compared where the 48 kHz reading is ≥ −100 dB. [SC-016]
- G10-5 (main loop, SC-006): fixture (ii), the edge excitation, makes no distortion-born side content above −60 dBFS
  below f_c at 60 and 120 Hz (E0, E1), so its non-vacuity REQUIRE was vacuous. -> recorded; (i) and (iii) carry the
  gate (A(B) 27.7 dB on Tube at E0 / 60 Hz; every control attenuated ≥ 24 dB). [SC-006]
- G10-6 (main loop, SC-010): the Drift rate lever read a 1/e time of 45 ms and 74 ms at E1 (ratio 1.65 against 4)
  because the 85 ms analysis window held 3.5 periods and its estimate jittered; at C3 it read 8.07 s / 0.37 s (22×).
  -> the window holds ≥ 8 periods of f₀ (power of two), hop a quarter. [SC-010]
- Close-lane scope note: the suites lane ran the whole dsp_systems_tests per-push set (84 min) although no tracked
  shared file changed; the next phase's lane runs the phase tag only (CLAUDE.md scope rule).

**Consequence recorded by the Q3 ruling (not a weakening).** With the stage split at 2 * f_c, the stage's LR4 transition
band reaches above f_c, so SC-003(c)'s "Gravity moves W_n by <= 0.5 dB" region is stated relative to the stage crossover
(n * f0 >= 2.5 * f_s = 5 * f_c); the 0.5 dB bar itself is unchanged (SC-003(c)).
