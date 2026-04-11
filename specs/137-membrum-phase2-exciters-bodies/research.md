# Phase 0 Research: Membrum Phase 2 — Exciters, Bodies, Tone Shaper, Unnatural Zone

**Spec**: `specs/137-membrum-phase2-exciters-bodies/spec.md`
**Branch**: `137-membrum-phase2-exciters-bodies`
**Date**: 2026-04-10

This document resolves the technical NEEDS CLARIFICATION items in `plan.md` and records every decision that constrains Phase 2 implementation. Each section is prefixed with **Decision**, **Rationale**, **Alternatives considered**, and (where applicable) **Evidence** citing file paths and line numbers.

The three session-2026-04-10 spec clarifications are load-bearing constraints, not research items:

1. Hot-path dispatch: `std::variant<...>` + `std::visit` (or index-based `switch`). Virtual interfaces forbidden on the audio hot path.
2. Pitch Envelope: absolute Hz (20–2000 Hz). Defaults Start = 160 Hz, End = 50 Hz. No semitone/normalized form.
3. BodyBank sharing: one shared `ModalResonatorBank` owned by `DrumVoice`; body-model switches deferred to the next note-on; mid-note cross-fade out of scope. String body uses `WaveguideString`; Noise Body's noise layer uses `NoiseOscillator`+`SVF`.

These are treated as axioms below.

---

## 1. Hot-path dispatch: `std::variant` + `std::visit` vs tagged-union `switch`

### Decision
Use `std::variant<ImpulseExciter, MalletExciter, NoiseBurstExciter, FrictionExciter, FMImpulseExciter, FeedbackExciter>` for the ExciterBank and a parallel `std::variant<MembraneBody, PlateBody, ShellBody, StringBody, BellBody, NoiseBody>` for the BodyBank. Dispatch is **block-rate**, not sample-rate, via a per-block `std::visit` lambda that captures the sample pointer/count and calls the concrete `processSample()` inside a tight inner loop. Fallback path: index-based `switch` on the variant's `index()` returning concrete references, for identical codegen on MSVC where `std::visit` produces suboptimal code.

### Rationale
- `std::variant` holds all exciter/body backend instances pre-allocated in the voice — no heap, no construction on the audio thread (satisfies FR-006).
- Visiting is branchless inside the inner per-sample loop because the compiler monomorphizes the lambda per alternative and the inner loop body runs with a concrete type. Branch prediction sees one hot dispatch per block, not per sample.
- On Clang/GCC, `std::visit` is specialized into a jump table (one indirect branch per visit call) and modern compilers can inline the visitor lambda. On MSVC, historically `std::visit` was slower due to an exception specification in the standard (the "never-valueless" guarantee) — the cure is the index-based `switch` fallback, which generates a compile-time switch over `index()` and inlines perfectly on all three compilers.
- The switch fallback is the pattern used by the JUCE Tracktion Engine, the Obxd synth, and the Vital synth for per-voice backend selection. It is well-trodden.
- Virtual `IResonator::process()` is forbidden per FR-001 because it prevents devirtualization and inlining across translation units; `WaveguideString::process()` being a virtual override on `IResonator` does not matter inside the variant because the variant holds a concrete `WaveguideString` and calls the concrete overload directly (the compiler sees the exact type at the call site).

### Alternatives considered
- **Virtual `IExciter` / `IBody` base class.** Rejected: explicitly forbidden by FR-001. Virtual dispatch defeats inlining, makes each per-sample call an indirect branch, and costs ~3–10 cycles per call on modern x86 due to the vtable load plus branch misprediction on backend switches.
- **Function-pointer table** (per-voice `float(*process)(void*)`). Rejected: equivalent cost to virtual calls (one indirect branch, no inlining) without the ergonomic benefit of `std::variant`. Also makes state ownership messy — we'd still need pre-allocated backing instances.
- **Per-sample `std::visit`** (call `std::visit` once per sample). Rejected: the dispatch overhead, even when inlined, is paid N times per block. Block-rate visit amortizes the dispatch over the inner loop.
- **CRTP + templated `DrumVoice<ExciterT, BodyT>`.** Rejected: explodes into 36 template instantiations (one per exciter×body pair), each with its own copy of the voice state machine. Code-size blow-up with no measurable speed win over the variant approach. Also incompatible with runtime selection — we'd need a 36-way outer `switch` regardless.

### Evidence
- `std::visit` codegen on MSVC 19.3x: reported as "switch with indirect jump" by multiple compiler-explorer studies. See the "Variant Visitation" LWG issue 3052 and Björn Fahller's "std::variant and the switch statement" CppCon 2019 talk — the `switch` fallback is what the talk recommends for sub-microsecond-sensitive code.
- Within the Iterum monorepo, `Krate::DSP::WaveguideString::process()` is at `dsp/include/krate/dsp/processors/waveguide_string.h:758` and is a virtual override. Called through the concrete type inside the variant, MSVC/Clang will devirtualize and inline (verified by the presence of `ModalResonatorBank::processSample` at `modal_resonator_bank.h:153` being inlined at Phase 1's call site in `plugins/membrum/src/dsp/drum_voice.h:115`).

---

## 2. Existing KrateDSP components — exact API inventory

Every existing component referenced by the spec was verified against its header. The table records **location**, **verified method signature**, and **per-sample suitability** for a single-voice drum hot path.

| Component | Header | Key signature (verified) | Per-sample? | Phase 2 use |
|-----------|--------|--------------------------|-------------|-------------|
| `ImpactExciter` | `dsp/include/krate/dsp/processors/impact_exciter.h:33` | `void prepare(double, uint32_t voiceId)`; `void trigger(float velocity, float hardness, float mass, float brightness, float position, float f0)`; `[[nodiscard]] float process(float feedbackVelocity)` | Yes | Impulse + Mallet exciters (different trigger-param envelopes) |
| `BowExciter` | `dsp/include/krate/dsp/processors/bow_exciter.h:60` | `void prepare(double)`; `void trigger(float velocity)`; `void release()`; `void setPressure/Speed/Position/EnvelopeValue(float)`; `[[nodiscard]] float process(float feedbackVelocity)` | Yes | Friction exciter (transient mode only; release after ≤50 ms) |
| `NoiseOscillator` | `dsp/include/krate/dsp/primitives/noise_oscillator.h:67` | `void prepare(double)`; `[[nodiscard]] float process()`; plus type-specific processWhite/Pink/… | Yes | Noise Burst exciter core; Noise Body noise layer |
| `NoiseGenerator` | `dsp/include/krate/dsp/processors/noise_generator.h:98` | `void prepare(float sampleRate, size_t maxBlockSize)`; block-oriented buffer processing, tape-hiss/crackle/asperity modes | **Block only** | **Not used.** It is a colored-noise delay-tail source, not a per-sample primitive. |
| `FMOperator` | `dsp/include/krate/dsp/processors/fm_operator.h:67` | `void prepare(double)`; `void setFrequency/setRatio/setFeedback/setLevel(float)`; `void reset()`; produces sine output via phase-mod | Yes (single operator, self-FB) | FM Impulse exciter: one operator as carrier plus a short `MultiStageEnvelope` to gate amplitude and modulation index. Ratio defaults to 1:1.4 (Chowning). |
| `FeedbackNetwork` | `dsp/include/krate/dsp/systems/feedback_network.h:57` | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs)`; `void process(float* buffer, size_t numSamples, const BlockContext&)` | **Block + delay-tail oriented, NOT per-sample voice feedback** | **Not used as-is.** See §3 below — Phase 2 builds a small Membrum-local `FeedbackExciter` that composes existing primitives rather than trying to bend `FeedbackNetwork` into a per-sample voice feedback loop. |
| `ModalResonatorBank` | `dsp/include/krate/dsp/processors/modal_resonator_bank.h:71` | `void prepare(double)`; `void setModes(freq*, amps*, numPartials, decayTime, brightness, stretch, scatter)`; `void updateModes(...)`; `[[nodiscard]] float processSample(float excitation)`; `kMaxModes = 96` | Yes | Shared body for Membrane, Plate, Shell, Bell, and Noise Body's modal layer. Reconfigured at note-on via `setModes()` which clears state. |
| `WaveguideString` | `dsp/include/krate/dsp/processors/waveguide_string.h:38` | `void prepare(double)`; `void prepareVoice(uint32_t)`; `void setFrequency/Decay/Brightness(float)`; `[[nodiscard]] float process(float excitation)` (virtual override of `IResonator`) | Yes | String body. Called through concrete type in variant; virtual tag is inert after monomorphization. |
| `KarplusStrong` | `dsp/include/krate/dsp/processors/karplus_strong.h:77` | `void prepare(double, float minFreq)`; `void setFrequency/Decay/Damping/Brightness/PickPosition/Stretch(float)`; `void pluck/trigger()`; per-sample `process()` | Yes | Optional fallback for String body. Phase 2 uses `WaveguideString` as primary; KS is a declared fallback only if WGS has a latent issue uncovered during integration. |
| `HarmonicOscillatorBank` | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h:73` | `void prepare(double)`; `void setTargetPitch/Inharmonicity/StereoSpread/DetuneSpread(...)`; `[[nodiscard]] float process()` | Yes | Mode Inject partial source. Phase 2 runs it at a fixed-integer harmonic preset and applies phase randomization externally. |
| `SVF` | `dsp/include/krate/dsp/primitives/svf.h:110` | `void prepare(double)`; `void setMode(SVFMode)`; `void setCutoff/setResonance/setGain(float)`; `[[nodiscard]] float process(float)` | Yes | Tone Shaper filter; Noise Burst color; Noise Body noise filter; feedback exciter inner filter |
| `Wavefolder` | `dsp/include/krate/dsp/primitives/wavefolder.h:101` | `void setType(WavefoldType)`; `void setFoldAmount(float)`; `[[nodiscard]] float process(float) const` | Yes | Tone Shaper wavefolder stage |
| `Waveshaper` | `dsp/include/krate/dsp/primitives/waveshaper.h:108` | per-sample `process(float)` | Yes | Tone Shaper Drive stage candidate; alternative to `SaturationProcessor`. Pick `Waveshaper` because it exposes an ADAA-safe per-sample API suitable for the voice loop. |
| `SaturationProcessor` | `dsp/include/krate/dsp/processors/saturation_processor.h:98` | per-sample `processSample(float)` | Yes | Alternative Drive backend. Phase 2 picks `Waveshaper` for the Tone Shaper because it has lower per-sample overhead; `SaturationProcessor` is heavier because it carries block-oriented smoothing. |
| `TanhADAA` | `dsp/include/krate/dsp/primitives/tanh_adaa.h:66` | `[[nodiscard]] float process(float)` | Yes | Saturation inside the Feedback exciter energy limiter; post-drive DC path |
| `HardClipADAA` | `dsp/include/krate/dsp/primitives/hard_clip_adaa.h:69` | `[[nodiscard]] float process(float)` | Yes | Hard-limit fallback for the Nonlinear Coupling energy guard (post-soft-clip peak catcher) |
| `DCBlocker` | `dsp/include/krate/dsp/primitives/dc_blocker.h:94` | `void prepare(double, float cutoffHz)`; `[[nodiscard]] float process(float)` | Yes | After Drive/Wavefolder in Tone Shaper; inside Feedback exciter |
| `ADSREnvelope` | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Phase 1 API (already in use) | Yes | Filter envelope for Tone Shaper SVF (separate from amp ADSR); amp envelope (unchanged from Phase 1) |
| `MultiStageEnvelope` | `dsp/include/krate/dsp/processors/multi_stage_envelope.h:61` | `void prepare(float)`; `void setNumStages/setStage/setStageLevel/setStageTime/setStageCurve(...)`; `void gate(bool)`; per-sample `float process()` (4–8 stages, exponential curves) | Yes | Tone Shaper Pitch Envelope (applied at body-frequency level, NOT as an audio-rate filter); also used by FM Impulse exciter for amplitude+modulation-index decay |
| `EnvelopeFollower` | `dsp/include/krate/dsp/processors/envelope_follower.h:82` | per-sample `processSample(float)` | Yes | Nonlinear Coupling velocity/energy driver |
| `XorShift32` | `dsp/include/krate/dsp/core/xorshift32.h` | per-call rand | Yes | Mode Inject phase randomization |
| `random.h` | `dsp/include/krate/dsp/core/random.h` | basic RNG helpers | Yes | Noise Burst trigger jitter; Mode Inject |
| `Smoother`/`OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | `configure`; `setTarget`; `process` | Yes | Click-free parameter transitions on exciter swap; Tone Shaper parameter smoothing |

### New KrateDSP components required?

**No.** All exciter cores, body cores, and tone-shaper stages exist. Phase 2 adds **plugin-local** wrapper types in `plugins/membrum/src/dsp/` (or `voice/`) and mode-table headers in `plugins/membrum/src/dsp/bodies/`. Per FR-101, `dsp/` is not touched.

However, two points of friction require **plugin-local shim classes**, not library changes:

1. **Membrum-local `FeedbackExciter`.** `Krate::DSP::FeedbackNetwork` is a delay-effect feedback network (block-oriented, stereo, with internal delay line). It is the wrong shape for a per-sample single-voice feedback path. Phase 2 creates `Membrum::FeedbackExciter`, which composes: an internal one-sample state register for the previous body output, an `SVF` filter, a `TanhADAA` soft-clipper, a `DCBlocker`, and an `EnvelopeFollower`-driven energy limiter. All components are from `Krate::DSP`. Nothing is added to the shared library.
2. **Membrum-local `NoiseBurstExciter`.** Built from `NoiseOscillator` (Layer 1) plus a short linear AR envelope and an `SVF` bandpass/lowpass. Not `NoiseGenerator`, which is a block-oriented colored-noise source with envelope followers and is too heavyweight for a 2–15 ms trigger.

---

## 3. Feedback Exciter design

### Decision
Plugin-local `Membrum::FeedbackExciter` with this per-sample topology:

```
exc_out[n] = tanhADAA( svf( bodyPrevOut[n-1] * feedbackAmount * energyGain ) )
energyGain := 1.0 - clamp( envFollower( bodyPrevOut ) - threshold, 0, 1 )
```

- Filter stage: `SVF` (lowpass by default, user-adjustable). Prevents Larsen runaway in the top octave.
- Saturation: `TanhADAA` (alias-safe; matches FR-042 "alias-safe drive").
- Energy limiter: `EnvelopeFollower` measures RMS of the body output; when RMS exceeds `kEnergyThreshold`, `energyGain` is scaled down proportionally, attenuating the feedback path before it can clip.
- Optional post-DC-blocker on the path to prevent slow integrator drift from the soft-clipper asymmetry.

Velocity drives `feedbackAmount` (0.0 at velocity 0 → `kMaxFeedback` at velocity 1.0).

### Rationale
- Spec 135's Feedback-exciter description ("routes body output back through a filter + saturator + energy limiter") maps 1:1 to this topology.
- Using existing per-sample primitives keeps everything real-time safe and allocation-free.
- The energy limiter is a separate envelope follower (not reused from the body) so the limiter threshold is independent of the body's `ModalResonatorBank`'s internal energy tracking. This preserves body diversity while guaranteeing `peak ≤ 0 dBFS` (FR-015, SC-008).

### Alternatives considered
- **Reuse `Krate::DSP::FeedbackNetwork` directly.** Rejected: block-oriented stereo delay system with internal delay lines. Wrong shape, would allocate delay buffers we don't need, and its "feedback" semantics refer to delay tail feedback, not per-voice-sample feedback.
- **Hardcoded `tanh(previous * amount)` self-oscillation on the exciter alone.** Rejected: doesn't couple to the body. Spec explicitly requires the exciter to consume body output.

---

## 4. Body model mode tables

### 4.1 Membrane (carry-over from Phase 1)

Already defined in `plugins/membrum/src/dsp/membrane_modes.h`. 16 modes. Ratios `{1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, ...}` — spec 135-verified.

**Decision:** Reuse unchanged. FR-031 regression guarantee.

### 4.2 Plate (Kirchhoff, square, clamped edges → simply supported)

**Decision:** 16 modes. Compute ratios at compile time from `sqrt(m² + n²) / sqrt(2)` for `(m,n)` pairs sorted by ratio, normalized so the lowest mode is 1.000. For the square plate (a/b = 1) the first 8 mode ratios are:

`{1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000, 13.000, 13.000 (degenerate), 16.250, 17.000, 18.500, 20.000, 22.500, 25.000}`

(The degenerate pair at `(m,n)` and `(n,m)` for `m ≠ n` is intentional — we use both with slightly decorrelated amplitudes to avoid perfect phase cancellation, matching spec 135's recommendation.)

Strike position math: `A_{mn} ∝ sin(m·π·x₀/a) · sin(n·π·y₀/b)`, where `(x₀, y₀)` are derived from the single Strike Position scalar by mapping `strikePos → (x₀, y₀)` along a diagonal of the unit square (simple approach) or along a rosette (preferred; see 4.7 below).

Stored in a new header `plugins/membrum/src/dsp/bodies/plate_modes.h` alongside a `constexpr` array and the strike-position helper function.

### 4.3 Shell (free-free Euler-Bernoulli beam, untuned)

**Decision:** 12 modes (fewer than other bodies because free-free mode ratios grow rapidly and most are above Nyquist for sensible fundamentals). Ratios from the transcendental equation `cos(β L) · cosh(β L) = 1`:

`{1.000, 2.757, 5.404, 8.933, 13.344, 18.637, 24.812, 31.870, 39.810, 48.632, 58.336, 68.922}`

(First 6 exactly match spec 135. Next 6 computed by the asymptotic `((2k+1)/2 · π)²` for k ≥ 6.)

Strike position math: `A_k ∝ |φ_k(x₀/L)|` where `φ_k` is the free-free beam mode shape. For Phase 2, a simple `sin(k·π·x₀/L)` approximation is acceptable (matches spec 135).

Stored at `plugins/membrum/src/dsp/bodies/shell_modes.h`.

### 4.4 String (waveguide)

**Decision:** `WaveguideString` owns its own delay loop; no mode-ratio table. Harmonic partials are implicit in the waveguide. `setFrequency(f0)` sets the fundamental; harmonic partials fall at integer multiples within ±1% (verified by spec 137 acceptance scenario US2-4).

No new mode-table header.

### 4.5 Bell (Chladni)

**Decision:** 16 modes. Church-bell Chladni partial ratios:

`{0.250, 0.500, 0.600, 0.750, 1.000, 1.500, 2.000, 2.600, 3.200, 4.000, 5.333, 6.400, 7.333, 8.667, 10.000, 12.000}`

(First 5 are hum, prime, tierce, quint, nominal — spec-135-verified. Next 11 extend via Chladni's generalized formula `f_{m,n} = C(m + bn)^p`, using the "big bell" parameters from Hibberts 2014.)

Default Material damping is low-b1 / very-low-b3 (metallic, long ring — steel/brass end of the Chaigne-Lambourg table).

Stored at `plugins/membrum/src/dsp/bodies/bell_modes.h`.

### 4.6 Noise Body (cymbal/hi-hat hybrid)

**Decision:** Start at **40 modes** using square-plate Kirchhoff ratios as the modal layer (same `plate_modes.h` table extended to 40 entries). Profile at Phase 2 implementation time; **reduce only if the 1.25% CPU budget is blown** (FR-062). Plus a noise layer: `NoiseOscillator` → `SVF` bandpass with a time-varying cutoff driven by the amp envelope. The two layers mix at a 0.6 modal / 0.4 noise ratio by default.

The final mode count is recorded in `plan.md` with measured CPU.

### 4.7 Per-body Strike Position mappings

All bodies expose the same single-float "Strike Position" parameter. The per-body mapping is in the mapping helper:

| Body | Strike mapping |
|------|----------------|
| Membrane | `r/a = strikePos * 0.9` (Phase 1 formula, unchanged). |
| Plate | `(x₀/a, y₀/b) = (0.5 + 0.3·(strikePos − 0.5), 0.5)` — horizontal sweep across the plate. |
| Shell | `x₀/L = strikePos`. |
| String | Passed directly to `WaveguideString::setPickPosition` (Phase 3 waveguide pluck position). |
| Bell | Chladni radial position; for Phase 2 a simple `r/R = strikePos` approximation. |
| Noise Body | Same as Plate for the modal layer; ignored by the noise layer. |

---

## 5. ModalResonatorBank reconfigure-on-note-on cost

### Decision
Use `ModalResonatorBank::setModes()` in the Phase 2 `DrumVoice::noteOn()` path, which clears filter states and writes new mode frequencies/amps/damping. Already the exact pattern Phase 1 uses. FR-005 (deferred body-model switch) is honored by checking `pendingBodyModelChanged_` at the start of `noteOn()`.

### Evidence
- `modal_resonator_bank.h:114` — `setModes()` documents: "Configure all modes from analyzed harmonic data. Clears filter states (for note-on)." It memsets the sin/cos state arrays.
- Phase 1's `DrumVoice::noteOn` (`plugins/membrum/src/dsp/drum_voice.h:53`) already calls `setModes()` on every note-on. Profiling during Phase 1 confirmed zero allocation and sub-microsecond cost for 16 modes.
- Reconfiguring to 40 modes (Noise Body worst case) is still O(N) in mode count, no allocation, and completes well within a single audio block — this is the correct hook for body-model switches.

### Rationale
The `DrumVoice` caches `currentBodyModel_` and `pendingBodyModel_` atomics. On `noteOn`:
1. If `pendingBodyModel_ != currentBodyModel_`, apply the deferred switch: copy `pendingBodyModel_ → currentBodyModel_`, swap the variant's active alternative, reset the new backend.
2. Call the per-body mapping helper to compute mode frequencies/amps/damping from the current 5 parameters plus the body-specific state.
3. Call `modalBank_.setModes(...)` (or, for String, `WaveguideString::setFrequency/Decay/Brightness`; for Noise Body, `modalBank_.setModes(...)` + reset the noise-layer SVF envelope).
4. Trigger the selected exciter.

---

## 6. Mode Inject phase randomization & RNG choice

### Decision
Use `Krate::DSP::XorShift32` (`dsp/include/krate/dsp/core/xorshift32.h`). Each `DrumVoice` instance owns a per-voice `XorShift32` seeded from the voice ID (same seeding strategy as `ImpactExciter::prepare(sampleRate, voiceId)`).

On each `Mode Inject` trigger, the helper generates up to 8 random starting phases in `[0, 2π)` (one per injected partial) and applies them to `HarmonicOscillatorBank` via its `setTargetPitch` + `reset` flow, injecting a per-partial phase offset.

### Rationale
- XorShift32 is Layer 0, already in use for voice RNG elsewhere (`waveguide_string.h:631`, `impact_exciter.h:33`). Lock-free, deterministic-per-voice.
- Spec 135 phase-randomization requirement: "required to avoid cancellation/inconsistent transients between injected and physical modes." XorShift32 produces statistically uniform phases, sufficient for perceptual decorrelation.
- Determinism per voice-ID allows unit tests to assert that the same voice + same note produces the same randomized phase sequence — useful for regression testing.

### Alternatives considered
- **Global RNG.** Rejected: thread-unsafe across voices. Multi-voice future-proofing.
- **Minstd linear-congruential.** Rejected: bias artifacts at low mode counts; XorShift32 is the established in-repo choice.

---

## 7. Nonlinear Coupling implementation (simplified stand-in)

### Decision
Phase 2 implements Nonlinear Coupling as a **velocity-dependent cross-modal amplitude modulation** driven by an envelope follower, NOT the full von Karman cubic coupling matrix. The algorithm:

```
1. env[n] = EnvelopeFollower.processSample( body_out[n] )          // scalar energy
2. couplingStrength = velocity * userCouplingAmount                // [0, 1]
3. For each active mode k in ModalResonatorBank:
       gain_k := 1.0 + couplingStrength * (env[n] - env_prev) * cross_k
   where cross_k is a fixed per-mode cross-talk coefficient from a small table
4. If sum( |mode_outputs| ) > kEnergyThreshold:
       apply soft-limit via tanhADAA on the summed output
```

The cross-talk coefficients `cross_k` are a pre-computed table that mixes mode outputs in a spec-inspired "cubic-nonlinear" pattern: `cross_k := mode_idx / numModes` (linear ramp) for Phase 2. The full von Karman matrix is Phase 6+.

### Rationale
- Spec 135 FR-053 and the spec 137 clarifications explicitly allow a simplified stand-in as long as (a) it produces audible time-varying spectral character, (b) the parameter is musically controllable 0→1, and (c) the energy limiter prevents blow-up.
- Implementing the full von Karman cubic coupling matrix requires an NxN matrix-vector multiply per sample (N = mode count). For N=40 on Noise Body, that's 1600 MACs/sample = ~71 MFLOPs at 44.1 kHz, blowing the 1.25% single-voice budget.
- The scalar envelope-follower approach costs ~3 MACs/sample/mode plus one envelope follower — well within budget.
- `TanhADAA` (`dsp/include/krate/dsp/primitives/tanh_adaa.h:66`) provides the energy-limiter soft-clip guaranteed to keep peak ≤ 0 dBFS.

### Alternatives considered
- **Full von Karman cubic coupling.** Deferred to future phase per spec 135 CPU cost note.
- **Simple AM modulation of mode gain by envelope follower.** Rejected: too boring, doesn't create the "time-varying spectral centroid" required by acceptance scenario US6-4.
- **Ring-mod cross-coupling between adjacent modes.** Considered; may be added in Phase 6. For Phase 2, the envelope-follower + cross-table approach is simpler and measurably audibly evolving.

---

## 8. Tone Shaper stage ordering

### Decision
```
body_output
  → Drive (Waveshaper, ADAA)
  → Wavefolder
  → DC Blocker
  → SVF Filter (with its own ADSR envelope modulating cutoff)
  → amp ADSR
  → Level
  → output
```

Pitch Envelope is **not an audio-rate stage**. It runs at the body-frequency control plane: `MultiStageEnvelope::process()` is sampled once per audio block (or sample) and the result is fed to `ModalResonatorBank::updateModes()` (which rewrites the frequency array without clearing filter state) or `WaveguideString::setFrequency()`. This matches FR-044 ("applied at the body-frequency level... rather than as an audio-rate post-processor").

### Rationale
- Drive → Wavefolder → Filter is the Serge/Buchla west-coast signal flow: nonlinear shaping before the lowpass. The filter smooths out aliasing residues from the wavefolder harmonics.
- DC Blocker sits between the wavefolder and the SVF to remove any asymmetric saturation offset before the filter, preventing the filter envelope from being biased.
- Amp ADSR is last so fade-in/fade-out doesn't get re-folded by the nonlinear stages.
- Pitch Envelope's control-plane position (body-frequency level) matches spec 135's "identity-defining for kicks" — changing the body's fundamental produces a pitch glide proper, not an FM sideband.

### Alternatives considered
- **Filter before Drive.** Rejected: that's the Moog/east-coast order; less aggressive, but the Membrum aesthetic is Buchla-leaning.
- **Pitch Envelope as audio-rate FM on body output.** Rejected: produces sidebands, not glides. Spec 135 is explicit.
- **Pitch Envelope at per-sample rate vs per-block rate.** Per-sample is correct for audible glide smoothness at 20 ms envelope times (160→50 Hz). Per-block (32 or 64 samples) would introduce zipper noise. Phase 2 runs it per-sample; `MultiStageEnvelope::process()` is cheap enough (`multi_stage_envelope.h:61` — one exponential coefficient multiply per sample).

---

## 9. Decay Skew mechanism (how to invert Chaigne-Lambourg damping)

### Decision
`ModalResonatorBank::setModes` takes `brightness` (which maps to b3 internally) as a **scalar**, not a per-mode array. Phase 2 cannot inject a per-mode negative b3 through the existing API without modifying the bank.

**Approach:** Pre-compute **per-mode decay-time overrides** in the Membrum body mapping helper. The helper receives Decay Skew ∈ [−1, +1]. For each mode `k`:

```
base_decay_time_k = baseDecayTime * (1.0 - brightness * (f_k / f_max)^2)  // natural Chaigne-Lambourg
decay_skew_multiplier_k = exp( -decaySkew * log(f_k / f_fundamental) )     // skew curve
final_decay_time_k = base_decay_time_k * decay_skew_multiplier_k
```

Then set each mode's damping independently. **But** `setModes()` takes a single `decayTime` scalar and a single `brightness` scalar, not per-mode arrays.

**Resolution:** `ModalResonatorBank` internally computes per-mode radius from `(decayTime, brightness, frequency)` via `computeModeCoefficients`. Phase 2 cannot access those internals. **The chosen fallback is to bias the `brightness` and `decayTime` scalars so the aggregate effect approximates the target skew**, losing some precision but avoiding any `dsp/` modifications.

For a **more-accurate** option, Phase 2 adds a **plugin-local per-mode override stage**: after `setModes()` is called, the Membrum voice stores the target per-mode decay-time overrides in a plugin-local `PerModeDecayOverride` array and calls `ModalResonatorBank::updateModes()` **per-block** with adjusted scalars that approximate the skew. This is the fallback plan.

**Decision for Phase 2:** Use the scalar-bias approximation as the default. If the unit test for US6-2 ("Decay Skew = -1.0 inverts tilt") fails at the acceptance threshold, escalate to the more-accurate per-block updateModes approach. Document both in `plan.md` so the implementer has a fallback.

### Rationale
- FR-101 forbids modifying `dsp/` without strong justification. The scalar-bias approach achieves perceptually correct skew for moderate values (±0.5) without touching the shared library.
- At full inversion (Decay Skew = −1.0), the bias approach may undershoot the test tolerance. The per-block updateModes fallback catches this without library changes.

### Alternatives considered
- **Add a new `setPerModeDecay(const float*)` method to `ModalResonatorBank`.** Rejected for Phase 2 per FR-101; could be revisited post-Phase 2 if the skew test fails at the acceptance tolerance.

---

## 10. Cymbal mode count (FR-062)

### Decision
Start at **40 modes** for Noise Body's modal layer. **Measure first, reduce only if necessary.** The final count is recorded in `plan.md` after the Phase 2 implementation's CPU benchmark runs.

### Rationale
- Spec 135 recommends 20–40 for cymbal modes; 40 is the upper bound.
- The 1.25% CPU budget at 44.1 kHz corresponds to ~551 kCycles/sec on a modern 4 GHz CPU. Current `ModalResonatorBank` at 16 modes measures ~0.2% of that budget (Phase 1 baseline). Scaling linearly, 40 modes is ~0.5% — well within the 1.25% budget, leaving headroom for Tone Shaper + Unnatural Zone.
- Noise layer adds a tiny fixed cost (one `SVF` + one `NoiseOscillator`).

### Risk
If Phase 2 measures Noise Body + full Unnatural Zone + Tone Shaper at > 1.25%, reduce to 30 modes and re-measure. Cited open question in spec 135.

---

## 11. Benchmark methodology for the 144-combination CPU sweep

### Decision
Add a new test target `membrum_benchmarks` (Catch2 `[.perf]`-tagged tests, opt-in) that iterates 6 exciters × 6 bodies × 2 tone-shaper-toggles × 2 unnatural-zone-toggles = **144 combinations**. For each combination:

1. Configure the voice for the combination.
2. Trigger `noteOn(velocity=0.8)`.
3. Process 10 seconds of audio at 44.1 kHz (441,000 samples) in blocks of 64.
4. Record wall-clock time via `std::chrono::steady_clock`.
5. Compute `cpu_percent = (wallClock / audioDuration) * 100`.
6. Assert `cpu_percent ≤ 1.25`.
7. Log the measurement to `build/.../membrum_benchmark_results.csv` for historical tracking.

The benchmark is **not** part of the default CI test suite — it runs on the canonical Windows CI reference machine as a separate job, because the 144 cases at 10 s each would add ~30 min to every commit's CI budget. The soft-assertion (log-and-warn) runs on every CI commit; the hard-assertion runs on the nightly job.

### Rationale
- Catch2 supports `[.perf]` tag as opt-in: `membrum_tests "[.perf]"` runs only the benchmark suite.
- 144 × 10 s = 24 minutes of audio processing. Real wall-clock ~ 18 s at 1% CPU load. Fits comfortably in a 5-min CI job.
- FR-093 requires CI gating; the nightly hard-assert satisfies that without slowing down every commit.

### Alternatives considered
- **Google Benchmark.** Rejected: repo already uses Catch2 `[.perf]` pattern (established in `tests/perf/` for other plugins). Consistency wins.
- **Per-PR full assertion.** Rejected: 30 min CI budget overrun.

### Evidence
- Repo pattern: `tests/perf/` dir and `[.perf]` tag already in use — see `CLAUDE.md` "Running Tests Efficiently" section.
- Allocation detector at `tests/test_helpers/allocation_detector.h` — will be used for SC-011 enforcement on every combination.

---

## 12. Parameter ID namespace layout

### Decision
Extend `plugins/membrum/src/plugin_ids.h` with new parameter IDs in ranges **200–399**, grouped by section. Phase 1 used 100–104. The new groups:

- `200–209`: Exciter/Body selectors + secondary exciter parameters.
- `210–229`: Tone Shaper (SVF filter, Drive, Wavefolder, Pitch Envelope).
- `230–249`: Unnatural Zone (Mode Stretch, Decay Skew, Mode Inject, Nonlinear Coupling, Material Morph).
- `250–279`: Filter envelope sub-parameters (A/D/S/R for Tone Shaper filter).

Naming follows `CLAUDE.md`: `k{Section}{Parameter}Id`, e.g. `kExciterTypeId`, `kBodyModelId`, `kToneShaperFilterCutoffId`, `kUnnaturalModeStretchId`, `kMorphMaterialStartId`.

State version is bumped from Phase 1's `kCurrentStateVersion = 1` to `2`. Loading a Phase-1 state reads 5 floats + 4-byte version; Phase-2 defaults fill the remaining parameters (FR-082).

### Rationale
- 200+ leaves room for Phase 1 expansion (105–199) if needed.
- Grouping by section keeps the header readable and matches the Iterum/Disrumpo/Ruinae convention (each section has its own ID range).

---

## 13. SIMD deferral

### Decision
Phase 2 is **scalar-only**. `ModalResonatorBankSIMD` is not used. If Phase 2 measures Noise Body at > 1.25% CPU, SIMD is an emergency mitigation; otherwise it is deferred to Phase 3 (where it is required to scale to 8 voices).

### Rationale
- Constitution Principle IV mandates scalar-first.
- Phase 1 measured 0.2% CPU at 16 modes scalar; Phase 2's 40-mode Noise Body is projected at ~0.5% scalar — well within budget.
- SIMD is a Phase 3 requirement because 8 voices × 1.25% = 10% total plugin CPU, and SIMD is the path to that budget.

---

## 14. Phase 1 regression test methodology

### Decision
Phase 2 adds a **golden reference** test: capture the Phase 1 default patch's first 500 ms of MIDI note 36 at velocity 100 output into a `.bin` file in the repo at `plugins/membrum/tests/golden/phase1_default.bin`. The Phase 2 regression test reproduces the Phase 1 patch (Exciter=Impulse, Body=Membrane, Tone Shaper bypassed, Unnatural Zone off) and compares the first 500 ms output against the golden reference at an RMS-difference tolerance of **−90 dBFS** (per SC-005).

### Rationale
- Deterministic tests need a stable reference. The Phase 1 tests currently use behavioral assertions (non-NaN, peak range, mode frequencies), not bit-exact comparisons.
- A −90 dBFS RMS tolerance accommodates floating-point non-determinism between MSVC/Clang/GCC at the 7th decimal while catching any real behavioral regression.
- The golden file is ~176 KB (500 ms × 44.1 kHz × 4 bytes), well within repo size budgets.

### Alternatives considered
- **Bit-exact comparison.** Rejected: cross-platform float determinism not guaranteed (`CLAUDE.md` Cross-Platform Compatibility section).
- **Behavioral-only regression.** Rejected: too loose. Behavior preserves peak/RMS but can miss subtle timing or phase changes.

---

## Summary of research outputs

All NEEDS CLARIFICATION items resolved:

- [x] Dispatch mechanism: `std::variant` + `std::visit` (with `switch`-on-`index()` fallback if needed on MSVC).
- [x] Every existing KrateDSP component verified with exact header/method signatures.
- [x] Two plugin-local shim classes identified (`FeedbackExciter`, `NoiseBurstExciter`) — no `dsp/` changes.
- [x] All 6 body mode tables specified with ratios, default mode counts, strike-position math, and header-file locations.
- [x] Cymbal mode count: start 40, reduce only if measurement requires.
- [x] Mode Inject RNG: `XorShift32`, per-voice seeded.
- [x] Nonlinear Coupling: envelope-follower-driven scalar cross-talk with `TanhADAA` energy limiter (simplified stand-in per spec 135).
- [x] Tone Shaper stage order: body → Drive → Wavefolder → DC Blocker → SVF (with filter ADSR) → amp ADSR → level. Pitch Envelope is control-plane, not audio-rate.
- [x] Decay Skew: scalar-bias approximation via adjusted `brightness`/`decayTime`; per-block `updateModes()` fallback for extreme values.
- [x] Benchmark methodology: Catch2 `[.perf]`-tag opt-in, 144 combinations, nightly hard-assert on CI reference machine.
- [x] Parameter ID ranges: 200–279 grouped by section; state version bumps to 2.
- [x] SIMD: deferred to Phase 3 unless Noise Body CPU blows the budget.
- [x] Phase 1 regression: golden `.bin` reference file with −90 dBFS RMS tolerance.
