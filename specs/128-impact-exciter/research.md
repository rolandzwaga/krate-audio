# Research: Impact Exciter (Spec 128)

**Date**: 2026-03-21 | **Status**: Complete

## R-001: Strike Position Comb Filter Implementation

**Question**: The spec defines `H(z) = 1 - z^(-floor(position * N))` -- should we use the existing `FeedforwardComb` at Layer 1 or implement inline?

**Decision**: Use the existing `FeedforwardComb` class from `dsp/include/krate/dsp/primitives/comb_filter.h`.

**Rationale**: The `FeedforwardComb` implements `y[n] = x[n] + g * x[n-D]` which is a feedforward comb. The spec's formula `H(z) = 1 - z^(-D)` is equivalent to `y[n] = x[n] - x[n-D]`, i.e., gain = -1.0. The existing class supports gain in [0.0, 1.0] which is positive only. We need negative gain (-1.0) for the notch comb, but the existing class clamps to [0.0, 1.0]. Two options:
1. Implement the strike comb filter inline in ImpactExciter (trivial: just write+read from a DelayLine with subtraction).
2. Modify FeedforwardComb to accept negative gain.

Given the spec also requires a 70/30 dry/wet blend (FR-023), and the computation is trivial (write, read, subtract, blend), implementing inline using `DelayLine` directly is cleaner and avoids modifying existing FeedforwardComb API. The delay needs to support up to `sampleRate / f0_min` samples; at 192 kHz and f0=20 Hz, that's 9600 samples, so a max delay of ~50ms is safe.

**Alternatives Considered**:
- `FeedforwardComb`: Close fit but gain is clamped positive; modifying it risks breaking other consumers.
- `FeedbackComb`: Wrong topology (IIR, not FIR).
- Inline with raw array: Would work but `DelayLine` is already real-time safe with power-of-2 wrapping.

## R-002: SVF Reuse for Hardness Filter

**Question**: Can the existing `Krate::DSP::SVF` be used directly for the hardness-controlled lowpass?

**Decision**: Yes, reuse `SVF` directly. Instantiate one per `ImpactExciter` instance.

**Rationale**: The SVF class provides:
- `setMode(SVFMode::Lowpass)` for 2-pole lowpass
- `setCutoff(float hz)` for dynamic cutoff control
- `process(float input)` for per-sample processing
- Built-in smoothing (optional) and denormal flushing
- Butterworth Q default (`kButterworthQ = 0.7071f`)

The exciter sets cutoff once per trigger (not per-sample), so smoothing is not needed -- we can use `snapToTarget()` at trigger time and leave smoothing disabled. The SVF's `process()` returns the filtered sample directly.

**Alternatives Considered**: None needed; SVF is the exact right component.

## R-003: PinkNoiseFilter Reuse

**Question**: The spec calls for a one-pole pinking filter with `b = 0.9f`. The existing `PinkNoiseFilter` uses the Paul Kellet 7-state algorithm. Which to use?

**Decision**: Implement the simple one-pole filter inline in ImpactExciter, NOT reuse `PinkNoiseFilter`.

**Rationale**: The spec explicitly defines the filter as `pink = white - b * prev; prev = pink` with fixed `b = 0.9f`. This is a single-pole highpass-like differentiator, NOT the Paul Kellet pink noise approximation. The existing `PinkNoiseFilter` is a 7-state filter with very different characteristics. Using it would violate the spec's formula and break golden-reference test determinism. The one-pole form is trivial (one multiply, one subtract, one state variable) and specified exactly.

**Alternatives Considered**:
- `PinkNoiseFilter`: Different algorithm, different spectral characteristics, would not match spec.

## R-004: XorShift32 RNG Placement

**Question**: Where should the `XorShift32` struct live? Layer 0 core utility vs. inline in ImpactExciter?

**Decision**: Define `XorShift32` in `dsp/include/krate/dsp/core/xorshift32.h` as a Layer 0 utility.

**Rationale**: The `ResidualSynthesizer` already has an RNG (`rng_` field). Future exciters (Bow) will also need per-voice RNG. Placing it at Layer 0 makes it available to all layers without circular dependencies. The struct is pure, stateless (aside from the state variable), and has no dependencies. It's already described in the spec as a reusable struct pattern.

**Alternatives Considered**:
- Inline in ImpactExciter: Would work but prevents reuse by Bow exciter (Phase 4) and other processors.
- Existing `ResidualSynthesizer::rng_`: This is an `std::minstd_rand` (LCG), not the spec's xorshift32. Different algorithm, different seeding.

## R-005: Energy Capping and Mallet Choke Placement

**Question**: Should energy capping (FR-034) and mallet choke (FR-035) live in ImpactExciter or in InnexusVoice?

**Decision**: Energy capping lives in `ImpactExciter` (it's specific to the excitation signal). Mallet choke envelope logic lives in `InnexusVoice` (it affects the `ModalResonatorBank`, which is shared across all exciter types). The `ModalResonatorBank::processSample()` API needs a `decayScale` parameter (FR-035).

**Rationale**: The spec explicitly states "the voice layer owns the choke envelope" and "the resonator remains a pure physics component." Energy capping is about limiting new excitation energy, which is exciter-specific. Choke is about accelerating resonator decay, which applies to any exciter type that feeds the resonator. Separating them follows the spec's design intent and the open-closed principle.

**Alternatives Considered**:
- Both in ImpactExciter: Would couple choke logic to one exciter type.
- Both in InnexusVoice: Energy capping doesn't belong at voice level since it's specific to impact pulse energy.

## R-006: ModalResonatorBank API Extension for decayScale

**Question**: How should `ModalResonatorBank::processSample()` be extended to accept `decayScale`?

**Decision**: Add an overload `float processSample(float excitation, float decayScale)` that applies `R_eff = powf(R, decayScale)` per mode when `decayScale != 1.0f`. The existing `processSample(float excitation)` overload remains unchanged (delegates with `decayScale = 1.0f`).

**Rationale**: This is backward-compatible. The existing `processSampleCore()` already has access to `radius_[k]` per mode. Adding a temporary scaling factor before the resonator step and restoring afterward is clean. The `powf()` per mode is only needed when `decayScale > 1.0f` (during choke), which is a brief transient (~10ms per retrigger).

**Alternatives Considered**:
- Separate `setDecayScale()` method: Would require storing state and be less explicit about when scaling applies.
- Modifying radius_ directly: Would corrupt the stored coefficients and require recalculation.

## R-007: Exciter Type Enum and Voice Switching

**Question**: How should the exciter type selection work in `InnexusVoice` and `Processor`?

**Decision**: Add an `ExciterType` enum in `plugin_ids.h` (matching parameter values: Residual=0, Impact=1, Bow=2). Store `exciterType_` as an atomic in Processor. In the voice processing loop, check the exciter type to select excitation source. The ImpactExciter is a member of InnexusVoice (alongside residualSynth and modalResonator).

**Rationale**: This mirrors how other type-selection parameters work in the plugin (e.g., `InputSource`, `LatencyMode`). The exciter type is a per-plugin setting (not per-voice), so it's stored in Processor and read during the voice loop. The ImpactExciter instance is per-voice because it contains per-voice state (RNG, SVF state, pulse phase).

**Alternatives Considered**:
- Virtual dispatch (base class Exciter): Overengineered for 2-3 types. Simple switch/if is clearer and faster (no vtable indirection in audio loop).
- Per-voice exciter type: The spec doesn't require different exciter types per voice.

## R-008: Attack Ramp Implementation

**Question**: How should the 0.1-0.5ms attack ramp (FR-033) be implemented?

**Decision**: Implement as a simple linear ramp counter in ImpactExciter. On trigger, set `attackRampSamples_ = sampleRate * 0.0003f` (0.3ms default, within spec range). Each sample, multiply output by `rampProgress = min(1.0f, sampleCounter / attackRampSamples)`.

**Rationale**: The ramp is only active during the first ~13 samples at 44.1 kHz (0.3ms). A linear ramp is sufficient -- the pulse envelope itself already shapes the attack. The ramp's purpose is solely to prevent sample-level discontinuities at the very start, not to shape the attack character.

**Alternatives Considered**:
- Exponential ramp: Unnecessary complexity for a 0.3ms transient.
- Per-voice smoother: Overengineered; the ramp is reset per trigger and is trivial.

## R-009: Comb Filter Delay Line Size

**Question**: What max delay does the strike position comb filter need?

**Decision**: Max delay = `sampleRate / 20.0f` samples (one period at 20 Hz, the lowest expected f0). At 192 kHz, this is 9600 samples. Allocate with `DelayLine::prepare(sampleRate, 0.055f)` (55ms covers 1/18Hz with margin).

**Rationale**: The comb delay is `position * N` where `N = sampleRate / f0`. At position=0.5 (max effective delay), the delay is `0.5 * sampleRate / f0`. For f0=20Hz and sr=192kHz, that's 4800 samples (25ms). Using 55ms max provides headroom for any reasonable fundamental frequency.

**Alternatives Considered**: None needed; this is straightforward sizing.
