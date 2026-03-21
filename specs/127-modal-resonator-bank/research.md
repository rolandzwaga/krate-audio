# Research: Modal Resonator Bank for Physical Modelling

**Date**: 2026-03-21 | **Spec**: 127 | **Branch**: `127-modal-resonator-bank`

## R-001: Coupled-Form Resonator Topology

**Decision**: Use the Gordon-Smith damped coupled-form resonator (not biquad two-pole).

**Rationale**: The existing `ModalResonator` (spec 086) uses a biquad-style two-pole topology with separate y1/y2 state arrays. The spec explicitly requires coupled-form for amplitude stability at high mode counts. The coupled-form update equations are:
```
epsilon = 2 * sin(pi * f / sampleRate)
R = exp(-decayRate / sampleRate)
inputGain = amplitude * (1.0f - R)
s_new = R * (s + epsilon * c) + inputGain * input
c_new = R * (c - epsilon * s_new)
```
This is mathematically equivalent to the Gordon-Smith oscillator with damping (R < 1). The key advantage is that the determinant of the state-transition matrix remains R^2, guaranteeing amplitude decay without drift -- critical for 96 parallel modes where small numerical errors would compound.

**Alternatives considered**:
1. **Existing `ModalResonator` (biquad two-pole)**: Uses `a1=-2R*cos(w)`, `a2=R^2` coefficients with y1/y2 states. Would work but spec explicitly requires coupled-form. Also lacks SoA/SIMD layout.
2. **Existing `ResonatorBank` (bandpass biquad filters)**: Uses full biquad with Q-based bandwidth. Completely different approach -- designed for spectral processing, not modal synthesis.
3. **Complex one-pole**: `z_new = R * e^(j*w) * z + input`. Equivalent to coupled-form but complex arithmetic is less SIMD-friendly.

## R-002: Input Gain Normalization

**Decision**: Use `(1 - R)` normalization (leaky-integrator compensation).

**Rationale**: Per spec clarification, the coupled-form acts as a leaky integrator with steady-state gain `1/(1-R)`. Multiplying the input by `(1-R)` compensates this exactly, ensuring consistent perceived loudness across all decay rates. The biquad-style `(1-R^2)/2` normalization does not apply here because it assumes zeros at z=+/-1, which the coupled-form injection does not have.

**Alternatives considered**:
1. **`(1 - R^2) / 2`**: Biquad transfer-function normalization. Overcompensates in coupled-form because the system operates in amplitude space, not energy space.
2. **No normalization**: Leads to wildly varying loudness across decay settings -- unusable.

## R-003: SIMD Strategy

**Decision**: SoA layout with Google Highway, following the existing `harmonic_oscillator_bank_simd.cpp` pattern. Scalar-first per Constitution Principle IV.

**Rationale**: The modal resonator bank is an ideal SIMD candidate:
- Up to 96 independent modes with identical computation per mode
- No inter-mode feedback dependencies (modes are fully independent)
- Inner loop is arithmetic-heavy with no branches (just mul/add)
- Data naturally maps to SoA layout (arrays of epsilon, R, s, c, inputGain)
- 96 modes = 24 AVX2 iterations (8 floats/lane) = excellent lane utilization

The existing `harmonic_oscillator_bank_simd.cpp` provides an exact template for the Highway self-inclusion pattern (`HWY_TARGET_INCLUDE`, `HWY_EXPORT`, `HWY_DYNAMIC_DISPATCH`).

However, per Constitution Principle IV (Scalar-First Workflow), the implementation MUST start with scalar code + full test suite + CPU baseline. SIMD optimization is Phase 2.

**Alternatives considered**:
1. **Scalar-only**: Would work but spec explicitly requires SoA layout and SIMD (FR-004, FR-005). SIMD is clearly beneficial here.
2. **Manual SSE intrinsics**: Not cross-platform. Highway provides automatic ISA selection.
3. **No SoA (AoS layout)**: Prevents effective vectorization. SoA is required for SIMD.

## R-004: Frequency-Dependent Damping Model

**Decision**: Chaigne-Lambourg quadratic damping law with `kMaxB3 = 4.0e-5f`.

**Rationale**: The Chaigne-Lambourg model (`decayRate_k = b1 + b3 * f_k^2`) is a well-established approximation for real-world material damping. The quadratic term makes high-frequency modes decay faster, which is physically correct for most materials (wood, metal, glass). The Brightness parameter (0-1) scales b3 between 0 (flat damping, metal-like) and `kMaxB3` (maximum HF damping, wood-like).

At f=2000 Hz: `b3_contribution = 4.0e-5 * 4e6 = 0.16 s^-1`, which gives a clear timbral difference between Brightness=0 and Brightness=1.

**Alternatives considered**:
1. **Linear damping (b1 + b2 * f_k)**: Less physically accurate. Real-world thermoelastic losses are quadratic in frequency.
2. **Logarithmic damping**: More complex, harder to parameterize for user control.
3. **Per-mode user-defined decay**: Too many parameters for practical use.

## R-005: Inharmonic Mode Warping

**Decision**: Two independent warping mechanisms: Stretch (stiff-string model) and Scatter (deterministic sinusoidal).

**Rationale**:
- **Stretch** (`f_k' = f_k * sqrt(1 + B * k^2)`, `B = stretch^2 * 0.001`): Physically motivated -- models the stiffness-induced inharmonicity of real strings and bars. Piano strings exhibit this exact behavior. The quadratic `stretch^2 * 0.001` scaling ensures gentle onset.
- **Scatter** (`f_k' *= (1 + C * sin(k * D))`, `C = scatter * 0.02`, `D = pi * (sqrt(5)-1)/2`): Perceptually motivated -- using the golden ratio creates quasi-random but deterministic mode displacement that avoids integer-ratio clustering. Produces plate/bell/gamelan character.

Both are applied multiplicatively to frequencies BEFORE the damping model, so warped modes get correct frequency-dependent decay.

**Alternatives considered**:
1. **Existing `ResonatorBank::setInharmonicSeries`**: Uses stiff-string formula but hardcoded to that bank's API. The formula itself can be referenced but not the code.
2. **Random scatter**: Non-deterministic would produce different timbres on each note -- unacceptable.

## R-006: Transient Emphasis

**Decision**: Envelope follower with ~5ms attack, continuous proportional boost on positive derivative, gain constant `kTransientEmphasisGain = 4.0f`.

**Rationale**: Raw residual signal is relatively flat-envelope noise. Modal resonators respond dramatically to transient excitation (like physical impact/strike). Emphasizing transients in the excitation signal produces more realistic "struck" character. The formula is `emphasis = 1.0f + kTransientEmphasisGain * max(0, envelopeDerivative)` — a continuous, proportional boost where subtle transients get gentle emphasis and strong transients get proportionally larger boost. This is NOT a binary on/off gate (which would slam every rising envelope with a flat 4x regardless of intensity).

The existing `EnvelopeDetector` in Innexus (`plugins/innexus/src/dsp/envelope_detector.h`) is NOT suitable -- it is a multi-frame spectral envelope detector for analysis, not a simple audio-rate follower. A simple one-pole envelope follower will be implemented inline in `ModalResonatorBank`.

**Alternatives considered**:
1. **Reuse `EnvelopeDetector`**: Wrong tool -- designed for frame-level spectral analysis, not sample-rate envelope following.
2. **External envelope follower class**: Overkill for a simple one-pole follower (~5 lines of code). Keep inline.

## R-007: Output Safety Limiter

**Decision**: Reuse existing `Krate::DSP::softClip()` from `dsp/include/krate/dsp/core/dsp_utils.h`.

**Rationale**: The existing `softClip()` provides a fast tanh approximation that clamps at +/-1. For the output limiter, the signal will be scaled to the threshold level (-3 dBFS ~ 0.707) before soft clipping, then scaled back. This provides the required safety limiting.

**Alternatives considered**:
1. **`std::tanh()`**: Exact but slower. The existing approximation is sufficient for a safety limiter.
2. **Hard clip**: Creates harsh artifacts. Soft clip is mandatory per FR-010.

## R-008: PhysicalModelMixer Placement

**Decision**: Plugin-local DSP at `plugins/innexus/src/dsp/physical_model_mixer.h`.

**Rationale**: Per FR-029, the mixer is Innexus-specific. It implements a simple crossfade formula:
```
dry = harmonicSignal + residualSignal
wet = harmonicSignal + physicalSignal
output = dry * (1 - mix) + wet * mix
```
This is equivalent to: `output = harmonicSignal + (1 - mix) * residualSignal + mix * physicalSignal`. This means the harmonic signal passes through unchanged at all mix levels, and the mix only controls the residual-vs-physical balance.

## R-009: Voice Integration Pattern

**Decision**: Add `ModalResonatorBank` and `PhysicalModelMixer`-related state to `InnexusVoice` struct. Process modal resonator in the per-voice sample loop alongside oscillator bank and residual synth.

**Rationale**: The existing voice rendering loop (processor.cpp ~L1577-1689) processes each voice per-sample:
1. `oscillatorBank.processStereo(vL, vR)` -- harmonic signal
2. `residualSynth.process()` -- residual signal
3. Mix them: `vL = vL * harmLevel + resContrib`

The new processing inserts after step 2:
1. Apply transient emphasis to residual signal
2. Process `modalResonatorBank.processSample(excitation)` -- physical signal
3. Apply `PhysicalModelMixer` blend instead of the simple mix

This minimal-change approach preserves the existing architecture and all backwards compatibility.

## R-010: Parameter ID Range

**Decision**: Use range 800-899 for Physical Model parameters.

**Rationale**: Looking at the existing parameter ID allocation in `plugin_ids.h`:
- 0-99: Global
- 100-199: Analysis
- 200-299: Oscillator bank
- 300-399: Musical control
- 400-499: Residual model
- 500-599: Sidechain
- 600-699: Modulators
- 700-799: Harmonic Physics + ADSR + Voice Mode

Range 800-899 is the next available block and provides room for all 5 new parameters plus future physical modelling parameters (Phase 2-5).

## R-011: State Save/Load Versioning

**Decision**: Add new parameters to the existing flat-format state stream (version 1) at the end, using optional reads on load.

**Rationale**: The current state format uses version 1 with sequential `streamer.writeFloat()` calls. New parameters will be appended at the end. On load, if the stream ends before the new parameters, defaults are used (mix=0, decay=0.5, brightness=0.5, stretch=0, scatter=0). This preserves backwards compatibility with existing presets.

## R-012: Coefficient Smoothing Implementation

**Decision**: Use `Krate::DSP::OnePoleSmoother` for per-mode epsilon and R smoothing with ~2ms time constant.

**Rationale**: The existing `OnePoleSmoother` in `dsp/include/krate/dsp/primitives/smoother.h` provides exactly the needed one-pole interpolation with configurable time constant. However, with 96 modes, using 192 individual `OnePoleSmoother` instances (one for epsilon, one for R per mode) would be excessive memory. Instead, the smoothing will be implemented inline using the coefficient formula: `coeff = exp(-1.0 / (0.002 * sampleRate))`, applied per-sample to each mode's epsilon and R arrays.

## R-012b: Simpler Damping Alternative (Fallback)

**Note**: The simpler alternative from Mutable Instruments Elements (`q *= q_loss` per mode, where `q_loss < 1`) is a reasonable approximation that avoids the `b3 * f²` computation entirely. If the Chaigne-Lambourg model proves too computationally expensive or difficult to tune during listening tests, this is a viable fallback. However, the Chaigne-Lambourg model is preferred because it produces physically correct frequency-dependent damping that distinguishes metal/wood/glass material archetypes — the simpler `q_loss` model produces uniform damping across all frequencies, which limits the timbral palette.

## R-013: Denormal Protection Strategy

**Decision**: Per-block energy check (`s^2 + c^2 < 1e-12`) with state zeroing, plus reliance on existing FTZ/DAZ settings.

**Rationale**: FTZ/DAZ handles most denormal cases at the hardware level. The explicit per-block check catches modes that have decayed to near-silence and zeros their states, preventing the accumulation of very small (but not quite denormal) values that could waste computation. The threshold `1e-12` corresponds to approximately -120 dB, well below audibility.
