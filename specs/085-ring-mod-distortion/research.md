# Research: Ring Modulator Distortion

**Feature**: 085-ring-mod-distortion
**Date**: 2026-03-01

## R-001: Gordon-Smith Sine Oscillator for Ring Mod Carrier

**Decision**: Implement inline Gordon-Smith magic circle phasor in `RingModulator`, same pattern as `FrequencyShifter`.

**Rationale**: The FrequencyShifter at `dsp/include/krate/dsp/processors/frequency_shifter.h` already implements this pattern with `cosTheta_/sinTheta_/cosDelta_/sinDelta_` state variables. The ring mod only needs the sine output (not quadrature), making it even simpler. Per MEMORY.md, Gordon-Smith provides ~30% speedup vs interpolated table lookup. The formula is:
```
epsilon = 2 * sin(pi * freq / sampleRate)
s_new = s + epsilon * c
c_new = c - epsilon * s_new  // uses updated s!
```
Amplitude is inherently stable (determinant = 1). Periodic renormalization every ~1024 samples prevents floating-point drift.

**Alternatives considered**:
1. **Extract shared GordonSmithOscillator to Layer 1**: The spec notes this should happen at 3 users (FrequencyShifter = 1, RingModulator = 2). Deferred per spec guidance.
2. **Use PolyBlepOscillator for sine**: PolyBLEP's sine mode uses `std::sin()` which is more expensive (~4x). Unnecessary for pure sine.
3. **Wavetable sine**: Good performance but adds a table dependency. Gordon-Smith is self-contained.

## R-002: PolyBLEP Oscillator for Complex Carriers

**Decision**: Reuse `PolyBlepOscillator` directly for Triangle, Sawtooth, Square carriers.

**Rationale**: `PolyBlepOscillator` (Layer 1) supports all needed waveforms via `OscWaveform` enum and provides band-limited output. It has `prepare()`, `reset()`, `setFrequency()`, `setWaveform()`, `process()`, `processBlock()` -- all the interfaces needed. It is copyable (value semantics, no heap allocation).

**API signatures verified from `dsp/include/krate/dsp/primitives/polyblep_oscillator.h`:**
- `void prepare(double sampleRate) noexcept`
- `void reset() noexcept`
- `void setFrequency(float hz) noexcept`
- `void setWaveform(OscWaveform waveform) noexcept`
- `float process() noexcept`

## R-003: NoiseOscillator for Noise Carrier

**Decision**: Reuse `NoiseOscillator` directly with `NoiseColor::White`.

**Rationale**: `NoiseOscillator` (Layer 1) provides `process()` returning [-1, 1] white noise. It is copyable (value semantics). Only `prepare()` and `setColor()` needed for setup. Per spec, noise color is fixed to White -- not user-configurable.

**API signatures verified from `dsp/include/krate/dsp/primitives/noise_oscillator.h`:**
- `void prepare(double sampleRate) noexcept`
- `void reset() noexcept`
- `void setColor(NoiseColor color) noexcept`
- `float process() noexcept`

## R-004: OnePoleSmoother for Frequency Smoothing

**Decision**: Use existing `OnePoleSmoother` from `dsp/include/krate/dsp/primitives/smoother.h` with 5ms time constant.

**Rationale**: The spec requires ~5ms time constant for carrier frequency smoothing (FR-023). `OnePoleSmoother` already uses `configure(smoothTimeMs, sampleRate)` where smoothTimeMs is the time to reach 99% of target. The FrequencyShifter uses the same class with `kSmoothingTimeMs = 5.0f`. We need separate smoothers for center frequency and spread offset.

**API signatures verified from `dsp/include/krate/dsp/primitives/smoother.h`:**
- `void configure(float smoothTimeMs, float sampleRate) noexcept`
- `void setTarget(float target) noexcept`
- `void snapTo(float value) noexcept`
- `float process() noexcept`
- `void reset() noexcept`

## R-005: RuinaeDistortionType Enum Extension

**Decision**: Add `RingModulator` as value 6 (before `NumTypes`) in `RuinaeDistortionType` enum.

**Rationale**: The enum at `plugins/ruinae/src/ruinae_types.h` has `TapeSaturator = 5` and `NumTypes = 6`. Adding `RingModulator = 6` shifts `NumTypes` to 7. The `kDistortionTypeCount` in `dropdown_mappings.h` derives from `NumTypes` automatically. The distortion type registration in `distortion_params.h` uses `kDistortionTypeCount` for normalization. Old presets store type as int (0-5), so existing types are unaffected since their values don't change.

**Alternatives considered**:
1. **Insert before TapeSaturator**: Would break backward compat for TapeSaturator presets. Rejected.
2. **Use a different mechanism**: Unnecessary; the enum extension pattern is clean and established.

## R-006: Parameter ID Allocation

**Decision**: Use IDs 560-564 as specified: `kDistortionRingFreqId = 560`, `kDistortionRingFreqModeId = 561`, `kDistortionRingRatioId = 562`, `kDistortionRingWaveformId = 563`, `kDistortionRingStereoSpreadId = 564`.

**Rationale**: The distortion parameter range is 500-599. Tape uses 550-552 (the highest allocated sub-range). IDs 553-559 are reserved for potential Tape expansion. 560-564 is the next clean block. Verified no collisions exist by reading `plugin_ids.h` (distortion IDs: 500-503, 510-512, 520-522, 530-533, 540, 550-552).

## R-007: State Save/Load Backward Compatibility

**Decision**: Append Ring Mod fields at the end of the distortion state block in `distortion_params.h`, using the same optional-read pattern as other type-specific fields.

**Rationale**: The existing `loadDistortionParams()` reads type-specific fields with `if (streamer.readFloat(floatVal))` -- if the field isn't in the stream (old preset), the read silently fails and the default value in the struct is used. Ring Mod fields will be appended after Tape fields. Old presets will not have them, so defaults (freq=440Hz via normalized, mode=NoteTrack, ratio=2.0, waveform=Sine, spread=0.0) apply automatically.

## R-008: Voice Integration - Note Frequency Forwarding

**Decision**: Add a `setDistortionRingNoteFreq(float freq)` method to `RuinaeVoice` that forwards the current `noteFrequency_` to the ring modulator for Note Track mode.

**Rationale**: The voice already has `noteFrequency_` which is updated on `noteOn()`, `setFrequency()`, and portamento updates. The ring modulator needs this value to compute `carrier_freq = noteFrequency * ratio` in Note Track mode. The processor's `applyVoiceParams()` method already reads `noteFrequency_` for other purposes (oscillator tuning). We can forward it to the ring mod in the same location where oscillator frequencies are updated.

**Key insight**: The frequency should be forwarded at the same rate as other per-voice params -- per block in `processBlock()`. This is where `updateOscFrequencies()` is called, and the ring mod note frequency should be set there too.

## R-009: Oversampling Consideration

**Decision**: No oversampling for ring modulation.

**Rationale**: Per the spec assumptions section: "No oversampling is required for the sine carrier (pure multiplication of band-limited signals). For complex carriers (Triangle, Sawtooth, Square), the PolyBLEP oscillator's built-in anti-aliasing is sufficient." Ring modulation is pure multiplication (linear operation when considering the carrier as a known signal), unlike waveshaping which creates harmonics via nonlinear transfer functions. The carrier is band-limited by PolyBLEP, and while sum/difference frequencies can alias, this is an inherent characteristic of ring modulation consistent with hardware ring modulators.

**Constitution Principle X**: The oversampling rule applies to "saturation/distortion/waveshaping" (nonlinear processes). Ring modulation is a linear multiplication, not waveshaping. No violation.

## R-010: SIMD Viability

**Decision**: NOT BENEFICIAL for SIMD.

**Rationale**: The ring modulator processes one sample at a time with feedback from the carrier oscillator state (Gordon-Smith phasor has s[n] depending on s[n-1] and c[n-1]). The per-sample carrier oscillator advance creates a serial dependency that prevents SIMD parallelization across samples. Cross-voice SIMD (processing 4+ voices simultaneously) is theoretically possible but would require SoA layout across voices, which is not how the RuinaeVoice architecture works (each voice owns its own distortion instance). The per-voice CPU budget is <0.3% which is very achievable with scalar code (Gordon-Smith phasor is 6 ops/sample; multiplication is 1 op/sample).

## R-011: Stereo Spread Maximum Offset

**Decision**: Maximum offset at spread=1.0 is +/-50 Hz, implemented as a compile-time constant.

**Rationale**: Per FR-006, the maximum offset is +/-50 Hz. This is wide enough to produce clearly audible L/R frequency differences (creating spatial movement) without being so extreme that the effect sounds broken at moderate carrier frequencies. For example, at 500 Hz carrier with spread=1.0: L=475 Hz, R=525 Hz -- a clearly audible but musically useful difference.

**Note**: In current voice architecture, voices are mono (single channel), so stereo spread has no effect. The stereo API is provided for forward compatibility.
